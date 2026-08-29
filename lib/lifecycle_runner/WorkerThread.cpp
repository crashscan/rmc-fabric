#include "WorkerThread.h"

#include <glog/logging.h>

namespace RSCGroup {

WorkerThread::WorkerThread(std::string name, WorkFn work)
    : name_(std::move(name))
    , work_(std::move(work))
{}

WorkerThread::~WorkerThread()
{
    stop();
}

bool WorkerThread::start()
{
    if (running_.load(std::memory_order_acquire)) {
        return false;
    }

    running_.store(true, std::memory_order_release);
    try {
        thread_ = std::jthread([this](std::stop_token st) {
            LOG(INFO) << "WorkerThread '" << name_ << "': started";
            try {
                work_(st);
            } catch (const std::exception& e) {
                LOG(ERROR) << "WorkerThread '" << name_ << "': crashed: " << e.what();
            } catch (...) {
                LOG(ERROR) << "WorkerThread '" << name_ << "': crashed with unknown exception";
            }
            LOG(INFO) << "WorkerThread '" << name_ << "': stopped";
            running_.store(false, std::memory_order_release);
        });
    } catch (...) {
        running_.store(false, std::memory_order_release);
        throw;
    }
    return true;
}

void WorkerThread::stop()
{
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
}

} // namespace RSCGroup
