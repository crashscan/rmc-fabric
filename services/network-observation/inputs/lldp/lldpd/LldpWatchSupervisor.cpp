//
// Created by vvass on 19-Sep-26.
//

#include "LldpWatchSupervisor.h"

#include <glog/logging.h>

namespace RSCGroup {

LldpWatchSupervisor::LldpWatchSupervisor(std::string ctlPath,
                                         std::chrono::milliseconds connectTimeout,
                                         std::chrono::milliseconds ioTimeout)
    : ctlPath_(std::move(ctlPath)), connectTimeout_(connectTimeout),
      ioTimeout_(ioTimeout) {
}

LldpWatchSupervisor::~LldpWatchSupervisor() {
    // Unbounded on purpose: there is no caller left to protect from a hang,
    // and leaking the watch here would leave its thread aliasing freed state.
    std::scoped_lock lk(mutex_);
    stopped_ = true;
    watch_.reset();
}

std::unique_ptr<BoundedLldpWatch> LldpWatchSupervisor::buildSilent() const {
    try {
        // Silent: subscribes and drains the socket but dispatches nothing.
        return std::make_unique<BoundedLldpWatch>(ctlPath_, connectTimeout_, ioTimeout_);
    } catch (const std::exception &e) {
        LOG(ERROR) << "LldpWatchSupervisor: watch creation failed: " << e.what();
        return nullptr;
    } catch (...) {
        LOG(ERROR) << "LldpWatchSupervisor: watch creation failed: unknown exception";
        return nullptr;
    }
}

void LldpWatchSupervisor::armExitHandler(BoundedLldpWatch &watch) {
    watch.setExitHandler([this] {
        // Runs on the dying loop thread. Record only — taking mutex_ here
        // would deadlock against a refresh joining this thread.
        watchDied_.store(true, std::memory_order_release);
        LOG(WARNING) << "LldpWatchSupervisor: watch died unsolicited";
    });
}

bool LldpWatchSupervisor::start(const CallbackFactory &makeCallback) {
    auto fresh = buildSilent();
    if (!fresh) return false;

    std::scoped_lock lk(mutex_);
    // A dead watch is replaceable: its loop thread has already exited, so
    // resetting here joins an exited thread. Only a LIVE watch is a
    // programming error.
    if (watch_ && !watchDied_.load(std::memory_order_acquire)) {
        LOG(WARNING) << "LldpWatchSupervisor: start() with a watch installed";
        return false;
    }
    watch_.reset();
    watchDied_.store(false, std::memory_order_release);
    armExitHandler(*fresh);

    // Attach before publishing: unlike refresh() there is no old watch to
    // retire first, and start()'s caller expects events immediately.
    if (makeCallback) fresh->setCallback(makeCallback());
    watch_ = std::move(fresh);
    stopped_ = false;
    return true;
}

bool LldpWatchSupervisor::refresh(const CallbackFactory &makeCallback,
                                  const std::function<void()> &onSwap) {
    // Built OUTSIDE the lock: a bounded connect plus subscribe round-trip.
    // Holding the lock across it would make stop() wait out the reconnect.
    auto fresh = buildSilent();
    if (!fresh) return false;

    std::scoped_lock lk(mutex_);
    if (stopped_) {
        // stop() won the race. `fresh` is dropped here, never attached —
        // its destructor joins a loop thread that dispatched nothing.
        return false;
    }

    // Destroying the old watch JOINS its loop thread, the only thread that
    // can invoke its callback. On return no old-watch callback is executing
    // or ever will be. This is the drain, scoped to exactly one watch.
    watch_.reset();

    // Between teardown and attach: nothing is dispatching, so the caller's
    // cache snapshot cannot race a fresh event.
    if (onSwap) onSwap();

    watchDied_.store(false, std::memory_order_release);
    armExitHandler(*fresh);
    if (makeCallback) fresh->setCallback(makeCallback());
    watch_ = std::move(fresh);
    return true;
}

bool LldpWatchSupervisor::stop(std::chrono::milliseconds timeout) {
    std::unique_lock lk(mutex_, timeout);
    if (!lk) {
        // A refresh is wedged on a downstream callback that will not return.
        // Do not block the caller — but the no-callback-executing
        // postcondition does not hold, and the watch is leaked.
        LOG(ERROR) << "LldpWatchSupervisor: stop timed out; refresh holds the lock";
        return false;
    }
    stopped_ = true;
    watch_.reset();   // joins the loop thread
    return true;
}

bool LldpWatchSupervisor::isWatchAlive() const {
    if (watchDied_.load(std::memory_order_acquire)) return false;
    std::scoped_lock lk(mutex_);
    return watch_ != nullptr && !stopped_;
}
} // namespace RSCGroup
