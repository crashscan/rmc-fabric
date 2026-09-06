#include "ManagedWorker.h"

#include <glog/logging.h>

#include <stdexcept>
#include <utility>

namespace RSCGroup {

ManagedWorker::ManagedWorker(
    std::string name,
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
        LOG(FATAL)
            << "ManagedWorker '" << name_
            << "': destroyed from its own worker thread; "
               "this is an ownership violation";
    }

    try {
        stop();
    } catch (const std::exception& error) {
        LOG(FATAL)
            << "ManagedWorker '" << name_
            << "': destruction failed while stopping worker: "
            << error.what();
    } catch (...) {
        LOG(FATAL)
            << "ManagedWorker '" << name_
            << "': destruction failed while stopping worker: "
               "unknown exception";
    }
}

ManagedWorker::GenerationPtr
ManagedWorker::currentGeneration() const noexcept
{
    std::scoped_lock stateLock(stateMutex_);
    return currentGeneration_;
}

bool ManagedWorker::isCurrentGeneration(const GenerationPtr& generation) const noexcept {
    std::scoped_lock stateLock(stateMutex_);
    return generation != nullptr && currentGeneration_ == generation;
}

bool ManagedWorker::start() {
    std::scoped_lock operationLock(opMutex_);

    GenerationPtr previousGeneration;
    {
        std::scoped_lock stateLock(stateMutex_);
        if (running_) {
            return false;
        }
        previousGeneration = currentGeneration_;
    }

    /*
     * A returned or failed worker can remain joinable. Reap and retire that
     * exact generation before publishing its replacement.
     */
    if (thread_.joinable()) {
        if (!previousGeneration) {
            LOG(FATAL)
                << "ManagedWorker '" << name_
                << "': joinable thread exists without generation state";
        }
        joinAndRetireLocked(previousGeneration);
    } else if (previousGeneration) {
        LOG(FATAL)
            << "ManagedWorker '" << name_
            << "': generation state exists without a joinable thread";
    }

    const auto generation = std::make_shared<GenerationControl>();
    {
        std::scoped_lock stateLock(stateMutex_);
        currentGeneration_ = generation;
        running_ = true;
        lastExit_ = Exit{};
        workerThreadId_ = {};
    }

    try {
        /*
         * The std::jthread-provided token is deliberately unused. Work receives
         * the immutable stop token owned by this generation.
         *
         * Worker-side code must never acquire opMutex_. External join/stop may
         * hold it while waiting for this entry point and ExitHandler to finish.
         */
        thread_ = std::jthread(
            [this, generation] {
                const std::stop_token stopToken = generation->stopSource.get_token();
                {
                    std::scoped_lock stateLock(stateMutex_);
                    if (currentGeneration_ == generation) {
                        workerThreadId_ = std::this_thread::get_id();
                    }
                }
                Exit exit;
                try {
                    /*
                     * A stop request may win before this thread is first
                     * scheduled. Avoid entering owner Work after its one-shot
                     * Wake has already occurred.
                     *
                     * The token can still become stopped immediately after
                     * this check, so Work must always honor the token itself.
                     */
                    if (!stopToken.stop_requested()) {
                        work_(stopToken);
                    }

                    /*
                     * Advisory classification: this reflects token state at
                     * finalization, not necessarily why Work returned.
                     */
                    exit.reason = stopToken.stop_requested() ? ExitReason::stop_requested : ExitReason::returned;
                } catch (...) {
                    exit.reason = ExitReason::exception;
                    exit.exception = std::current_exception();
                }

                bool publishExit = false;
                {
                    std::scoped_lock stateLock(stateMutex_);
                    if (currentGeneration_ == generation) {
                        running_ = false;
                        lastExit_ = exit;
                        publishExit = true;
                    }
                }

                /*
                 * Owner callback code runs without any ManagedWorker mutex.
                 */
                if (publishExit && onExit_) {
                    try {
                        onExit_(exit);
                    } catch (const std::exception& error) {
                        LOG(ERROR)
                            << "ManagedWorker '" << name_
                            << "': exit handler threw: "
                            << error.what();
                    } catch (...) {
                        LOG(ERROR)
                            << "ManagedWorker '" << name_
                            << "': exit handler threw an unknown exception";
                    }
                }

                /*
                 * Clear identity only after ExitHandler returns so
                 * isCurrentThread() remains true throughout that callback.
                 */
                {
                    std::scoped_lock stateLock(stateMutex_);
                    if (currentGeneration_ == generation) {
                        workerThreadId_ = {};
                    }
                }
            });
    } catch (...) {
        /*
         * No worker thread was installed. Retire the published generation so
         * the object remains internally consistent and restartable.
         */
        retireGeneration(generation);
        throw;
    }
    return true;
}

void ManagedWorker::requestStopForGeneration(const GenerationPtr& generation) noexcept {
    if (!generation) {
        return;
    }
    {
        std::scoped_lock stateLock(stateMutex_);
        if (currentGeneration_ != generation) {
            return;
        }
    }

    /*
     * request_stop() executes registered stop callbacks synchronously.
     * No ManagedWorker mutex may be held here.
     */
    if (!generation->stopSource.request_stop()) {
        return;
    }

    /*
     * The caller that wins request_stop() is responsible for Wake.
     *
     * Retirement either waits for this Wake invocation or disables Wake before
     * this caller reaches the generation barrier.
     */
    std::scoped_lock wakeLock(generation->wakeMutex);

    if (!generation->wakeEnabled) {
        return;
    }

    invokeWake();
}

void ManagedWorker::requestStop() noexcept {
    requestStopForGeneration(currentGeneration());
}

void ManagedWorker::join() {
    if (isCurrentThread()) {
        throw std::logic_error(
            "ManagedWorker '" + name_ +
            "': join() called from the worker thread");
    }

    GenerationPtr observedGeneration = currentGeneration();
    std::scoped_lock operationLock(opMutex_);
    if (observedGeneration) {
        /*
         * The observed generation was retired and replaced while this caller
         * waited for operation serialization. Never join its replacement.
         */
        if (!isCurrentGeneration(observedGeneration)) {
            return;
        }
    } else {
        /*
         * A concurrent start() may already have owned opMutex_ without having
         * published its generation when join() began.
         */
        observedGeneration = currentGeneration();
        if (!observedGeneration) {
            return;
        }
    }
    joinAndRetireLocked(observedGeneration);
}

void ManagedWorker::stop() {
    if (isCurrentThread()) {
        throw std::logic_error(
            "ManagedWorker '" + name_ +
            "': stop() called from the worker thread");
    }

    GenerationPtr observedGeneration = currentGeneration();
    /*
     * Request stop before operation serialization. This lets stop interrupt a
     * worker while another external thread holds opMutex_ inside join().
     */
    requestStopForGeneration(observedGeneration);
    std::unique_lock operationLock(opMutex_);
    if (observedGeneration) {
        /*
         * Generation N was retired and replaced while this caller waited.
         * This stop belongs to N and must not affect N+1.
         */
        if (!isCurrentGeneration(observedGeneration)) {
            return;
        }
    } else {
        /*
         * No generation was visible at entry. A concurrent start() may have
         * owned opMutex_ but not yet published its generation. Adopt that
         * concurrently launched generation.
         */
        observedGeneration = currentGeneration();

        if (!observedGeneration) {
            return;
        }
    }

    /*
     * An adopted generation has not yet received a request from this caller.
     * For an originally observed generation, this is an idempotent backstop.
     *
     * request_stop() executes registered callbacks synchronously, so release
     * opMutex_ before requesting stop.
     */
    operationLock.unlock();
    requestStopForGeneration(observedGeneration);
    operationLock.lock();

    /*
     * Another stop or join may have retired the observed generation while
     * opMutex_ was released. It may also have installed a replacement.
     * Never join or retire that replacement.
     */
    if (!isCurrentGeneration(observedGeneration)) {
        return;
    }

    joinAndRetireLocked(observedGeneration);
}

void ManagedWorker::joinAndRetireLocked(const GenerationPtr& generation) {
    /*
     * Precondition: opMutex_ is held.
     *
     * The identity check is defensive; operation serialization prevents
     * replacement while the caller continuously owns opMutex_.
     */
    if (!isCurrentGeneration(generation)) {
        return;
    }

    /*
     * Never hold stateMutex_ or a generation wake mutex while joining. The
     * worker epilogue requires stateMutex_, and retirement may need wakeMutex.
     */
    if (thread_.joinable()) {
        thread_.join();
    }

    retireGeneration(generation);
}

void ManagedWorker::retireGeneration(const GenerationPtr& generation) noexcept {
    if (!generation) {
        return;
    }

    /*
     * Wait for an already-running Wake callback. If retirement obtains this
     * mutex first, a delayed stop requester observes wakeEnabled == false and
     * cannot invoke owner Wake code.
     */
    {
        std::scoped_lock wakeLock(generation->wakeMutex);
        generation->wakeEnabled = false;
    }

    {
        std::scoped_lock stateLock(stateMutex_);

        if (currentGeneration_ != generation) {
            return;
        }

        running_ = false;
        workerThreadId_ = {};
        currentGeneration_.reset();
    }
}

bool ManagedWorker::isRunning() const noexcept {
    std::scoped_lock stateLock(stateMutex_);
    return running_;
}

bool ManagedWorker::isJoinable() const noexcept {
    std::scoped_lock operationLock(opMutex_);
    return thread_.joinable();
}

bool ManagedWorker::isCurrentThread() const noexcept {
    std::scoped_lock stateLock(stateMutex_);
    return workerThreadId_ != std::thread::id{} && workerThreadId_ == std::this_thread::get_id();
}

ManagedWorker::Exit ManagedWorker::lastExit() const {
    std::scoped_lock stateLock(stateMutex_);
    return lastExit_;
}

void ManagedWorker::invokeWake() noexcept {
    if (!wake_) {
        return;
    }

    try {
        wake_();
    } catch (const std::exception& error) {
        LOG(ERROR)
            << "ManagedWorker '" << name_
            << "': wake callback threw: "
            << error.what();
    } catch (...) {
        LOG(ERROR)
            << "ManagedWorker '" << name_
            << "': wake callback threw an unknown exception";
    }
}

} // namespace RSCGroup
