#include "ManagedWorker.h"

#include <glog/logging.h>

#include <stdexcept>
#include <utility>

namespace RSCGroup {

ManagedWorker::ManagedWorker(std::string name,
                             Work work,
                             Wake wake,
                             ExitHandler onExit)
    : name_(std::move(name))
    , work_(std::move(work))
    , wake_(std::move(wake))
    , onExit_(std::move(onExit))
{
    if (!work_) {
        throw std::invalid_argument("ManagedWorker: work function is null");
    }
}

ManagedWorker::~ManagedWorker()
{
    if (isCurrentThread()) {
        // Ownership violation: the worker cannot outlive or destroy its owner.
        // Detaching is forbidden because the work/wake/exit callbacks capture
        // the owning object, so there is no safe recovery here.
        LOG(ERROR) << "ManagedWorker '" << name_
                   << "': destroyed from its own worker thread; this is an ownership violation";
        return;
    }

    requestStop();
    std::scoped_lock opLock(opMutex_);
    requestStop();
    joinLocked();
}

bool ManagedWorker::start()
{
    std::scoped_lock opLock(opMutex_);

    {
        std::scoped_lock stateLock(stateMutex_);
        if (running_) {
            return false;
        }
    }

    // Reap a finished-but-unjoined worker explicitly.  Move-assigning a fresh
    // std::jthread over it would implicitly request stop and join *without*
    // invoking the configured wake callback.
    joinLocked();

    {
        std::scoped_lock stateLock(stateMutex_);
        running_ = true;
        lastExit_ = Exit{};
        workerThreadId_ = std::thread::id{};
        stopSource_ = std::stop_source{std::nostopstate};
    }

    try {
        thread_ = std::jthread([this](std::stop_token stopToken) {
            {
                std::scoped_lock stateLock(stateMutex_);
                workerThreadId_ = std::this_thread::get_id();
            }

            Exit exit;
            try {
                work_(stopToken);
                // Advisory only: a natural return may race a stop request.
                exit.reason = stopToken.stop_requested() ? ExitReason::stop_requested
                                                         : ExitReason::returned;
            } catch (...) {
                exit.reason = ExitReason::exception;
                exit.exception = std::current_exception();
            }

            {
                std::scoped_lock stateLock(stateMutex_);
                running_ = false;
                lastExit_ = exit;
            }

            if (onExit_) {
                try {
                    onExit_(exit);
                } catch (const std::exception& e) {
                    LOG(ERROR) << "ManagedWorker '" << name_
                               << "': exit handler threw: " << e.what();
                } catch (...) {
                    LOG(ERROR) << "ManagedWorker '" << name_
                               << "': exit handler threw an unknown exception";
                }
            }

            // Cleared only after the handler returns, so isCurrentThread()
            // and self-operation detection stay valid inside the handler.
            {
                std::scoped_lock stateLock(stateMutex_);
                workerThreadId_ = std::thread::id{};
            }
        });
    } catch (...) {
        std::scoped_lock stateLock(stateMutex_);
        running_ = false;
        lastExit_ = Exit{};
        throw;
    }

    {
        std::scoped_lock stateLock(stateMutex_);
        stopSource_ = thread_.get_stop_source();
    }
    return true;
}

void ManagedWorker::requestStop() noexcept
{
    std::stop_source source;
    {
        std::scoped_lock stateLock(stateMutex_);
        source = stopSource_;
    }

    if (!source.stop_possible()) {
        return;
    }
    // request_stop() returns true only for the first successful request, so
    // the wake callback fires exactly once per real stop request.
    if (source.request_stop()) {
        invokeWake();
    }
}

void ManagedWorker::join()
{
    // Checked before acquiring opMutex_: a worker-thread caller (for example
    // an exit handler) must fail deterministically rather than deadlock.
    if (isCurrentThread()) {
        throw std::logic_error("ManagedWorker '" + name_ + "': join() called from the worker thread");
    }

    std::scoped_lock opLock(opMutex_);
    joinLocked();
}

void ManagedWorker::stop()
{
    if (isCurrentThread()) {
        throw std::logic_error("ManagedWorker '" + name_ + "': stop() called from the worker thread");
    }

    // The first request is issued *without* opMutex_ so a concurrent join()
    // that already holds it can observe the stop request and return.
    requestStop();
    // The second request is issued under opMutex_ so that a start() which
    // raced ahead of this stop still observes the shutdown intent.
    std::scoped_lock opLock(opMutex_);
    requestStop();
    joinLocked();
}

bool ManagedWorker::isRunning() const noexcept
{
    std::scoped_lock stateLock(stateMutex_);
    return running_;
}

bool ManagedWorker::isJoinable() const noexcept
{
    std::scoped_lock opLock(opMutex_);
    return thread_.joinable();
}

bool ManagedWorker::isCurrentThread() const noexcept
{
    std::scoped_lock stateLock(stateMutex_);
    return workerThreadId_ != std::thread::id{} && workerThreadId_ == std::this_thread::get_id();
}

ManagedWorker::Exit ManagedWorker::lastExit() const
{
    std::scoped_lock stateLock(stateMutex_);
    return lastExit_;
}

void ManagedWorker::joinLocked()
{
    if (thread_.joinable()) {
        thread_.join();
    }
}

void ManagedWorker::invokeWake() noexcept
{
    if (!wake_) {
        return;
    }
    try {
        wake_();
    } catch (const std::exception& e) {
        LOG(ERROR) << "ManagedWorker '" << name_ << "': wake callback threw: " << e.what();
    } catch (...) {
        LOG(ERROR) << "ManagedWorker '" << name_ << "': wake callback threw an unknown exception";
    }
}

} // namespace RSCGroup
