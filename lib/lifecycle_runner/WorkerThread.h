#pragma once

#include "StopToken.h"

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace RSCGroup {

/**
 * @brief A named worker thread with cooperative stop coordination.
 *
 * WorkerThread wraps a std::jthread.  The provided work function receives a
 * StopToken and should return when stop_requested() becomes true.
 *
 * Lifecycle:
 *  - Construct with a name and a work function.
 *  - Call start() to launch the thread; returns false if already running.
 *  - Call stop() to request stop and join; idempotent.
 *
 * Thread safety: start() and stop() must not be called concurrently from
 * different threads.
 */
class WorkerThread {
public:
    using WorkFn = std::function<void(StopToken)>;

    /**
     * @param name  Human-readable name (used in log messages).
     * @param work  Function to run on the thread; receives a StopToken and
     *              must return when stop_requested() is true.
     */
    WorkerThread(std::string name, WorkFn work);
    ~WorkerThread();

    WorkerThread(const WorkerThread&) = delete;
    WorkerThread& operator=(const WorkerThread&) = delete;

    /**
     * @brief Launch the worker thread.
     * @return true if launched; false if already running.
     */
    bool start();

    /**
     * @brief Request stop and join the worker thread.
     *
     * Idempotent: safe to call even if stop() was already called or start()
     * was never called.
     */
    void stop();

    [[nodiscard]] bool isRunning() const { return running_.load(std::memory_order_acquire); }

    [[nodiscard]] const std::string& threadName() const { return name_; }

private:
    std::string name_;
    WorkFn work_;
    std::atomic<bool> running_{false};
    std::jthread thread_;
};

} // namespace RSCGroup
