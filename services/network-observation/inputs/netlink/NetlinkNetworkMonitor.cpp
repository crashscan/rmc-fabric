//
// Created by vvass on 07-Jul-26.
//
/**
 * @file NetlinkNetworkMonitor.cpp
 * @brief Lifecycle, state, and callback orchestration for netlink monitoring.
 */

#include "public/NetlinkNetworkMonitor.h"

#include "ErrnoString.h"
#include "NetlinkEventLoop.h"
#include "NetlinkInitialDump.h"
#include "NetlinkParser.h"
#include "NetlinkState.h"

#include <EventFdSignal.h>
#include <LifecycleCoordinator.h>
#include <ManagedWorker.h>

#include <glog/logging.h>

#include <atomic>
#include <cerrno>
#include <exception>
#include <functional>
#include <optional>
#include <set>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace RSCGroup {
class NetlinkNetworkMonitor::Impl {
public:
    explicit Impl(
        MonitorCallbacks callbacks,
        std::set<std::string> watchedInterfaces,
        std::function<int()> liveFdProvider = {})
        : callbacks_(std::move(callbacks))
          , watchedInterfaces_(std::move(watchedInterfaces))
          , liveFdProvider_(std::move(liveFdProvider))
          , onLinkHandler_([this](const LinkEvent &event) {
              onLinkCallback(event);
          })
          , onIpHandler_([this](const InterfaceIpEvent &event) {
              onIpCallback(event);
          })
          , onFdbHandler_([this](const FdbEvent &event) {
              onFdbCallback(event);
          })
          , onNeighHandler_([this](const NeighborEvent &event) {
              onNeighCallback(event);
          })
          , onDeviceHandler_([this](const DeviceEvent &event) {
              if (callbacks_.onDeviceChanged) {
                  callbacks_.onDeviceChanged(event);
              }
          })
          , worker_(
              "netlink-monitor",
              [this](std::stop_token stopToken) {
                  runLiveLoop(std::move(stopToken));
              },
              [this] {
                  signalStop();
              },
              [this](const ManagedWorker::Exit &exit) {
                  onWorkerExit(exit);
              }) {
    }

    ~Impl() {
        stop();
    }

    bool start() {
        auto transition = lifecycle_.beginCancellableStart();
        if (!transition) {
            LOG(WARNING) << "start() called while netlink monitor epoch is not stopped";
            return false;
        }

        // A newly claimed epoch starts with healthy worker state.
        workerFailed_.store(false, std::memory_order_release);

        try {
            stopSignal_.emplace();
            // Separate eventfd for redumps; see the member declaration for
            // why it must not share stopSignal_.
            redumpStopSignal_.emplace();

            /*
             * Bind the subscribed live socket before starting the initial
             * dump. Notifications arriving while the dump runs remain queued
             * and will be processed when the worker starts.
             */
            NetlinkEventLoop::MessageHandler messageHandler = [this](const nlmsghdr *message) {
                processSingleMessage(message);
            };
            if (liveFdProvider_) {
                const int liveFd = liveFdProvider_();
                liveLoop_.emplace(liveFd, *stopSignal_, std::move(messageHandler));
            } else {
                liveLoop_.emplace(*stopSignal_, std::move(messageHandler));
            }
        } catch (const std::exception &error) {
            LOG(ERROR) << "failed to initialize netlink monitoring: " << error.what();
            return rollbackStartup(transition);
        } catch (...) {
            LOG(ERROR) << "failed to initialize netlink monitoring: " << "unknown exception";
            return rollbackStartup(transition);
        }

        NetlinkInitialDump::Result dumpResult;

        try {
            /*
             * beginStop() requests transition.stopToken(). Registering this
             * callback converts lifecycle cancellation into an eventfd wake
             * for the dump's blocking poll().
             *
             * If cancellation was requested before registration, the callback
             * executes synchronously during construction.
             *
             * The scope is deliberate: the stop callback is destroyed before
             * any path calls closeResources(), so signalStop() cannot race
             * destruction of stopSignal_.
             */
            std::stop_callback cancellationWake(transition.stopToken(), [this] { signalStop(); });
            NetlinkInitialDump dump(
                *stopSignal_,
                [this](const nlmsghdr *message) {
                    processSingleMessage(message);
                });

            dumpResult = dump.run();
        } catch (const std::exception &error) {
            LOG(ERROR) << "initial netlink dump threw: " << error.what();
            return rollbackStartup(transition);
        } catch (...) {
            LOG(ERROR) << "initial netlink dump threw an unknown exception";
            return rollbackStartup(transition);
        }

        if (!dumpResult.completed()) {
            if (!dumpResult.interrupted()) {
                LOG(ERROR)
                        << "initial netlink dump failed: status="
                        << NetlinkInitialDump::statusName(dumpResult.status)
                        << ", error=" << dumpResult.error;
            }
            return rollbackStartup(transition);
        }

        /*
         * The dump can return completed after a callback in the final datagram
         * requested cancellation. In that case the stop token is authoritative
         * even though NLMSG_DONE was also observed.
         */
        if (transition.stopRequested()) {
            return rollbackStartup(transition);
        }

        try {
            if (!worker_.start()) {
                /*
                 * Lifecycle state says this is a fresh start, so an existing
                 * live worker is a structural invariant violation.
                 */
                LOG(ERROR) << "netlink worker was already running during startup";
                return rollbackStartupWithWorker(transition);
            }
        } catch (const std::exception &error) {
            LOG(ERROR) << "failed to start netlink monitor worker: " << error.what();
            return rollbackStartup(transition);
        } catch (...) {
            LOG(ERROR) << "failed to start netlink monitor worker: " << "unknown exception";
            return rollbackStartup(transition);
        }

        /*
         * Detect the common immediate-failure case before publishing running.
         * A residual race remains between this load and tryComplete(); failures
         * after running is published are exposed through isRunning().
         */
        if (workerFailed_.load(std::memory_order_acquire)) {
            return rollbackStartupWithWorker(transition);
        }

        /*
         * The worker must exist before running is published. Otherwise a
         * concurrent stop() could claim the running -> stopping transition
         * before there is a worker to stop.
         *
         * If cancellation won after worker startup, tryComplete() returns
         * false and keeps the lifecycle in starting until rollback finishes.
         */
        if (!transition.tryComplete()) {
            return rollbackStartupWithWorker(transition);
        }

        LOG(INFO) << "NetlinkNetworkMonitor started";
        return true;
    }

    void stop() {
        /*
         * ManagedWorker::stop() cannot join itself. Reject callback-originated
         * live-loop stop before claiming lifecycle teardown.
         */
        if (worker_.isCurrentThread()) {
            LOG(ERROR) << "stop() must not be called from a netlink monitor callback";
            return;
        }

        /*
         * An initial-dump callback runs synchronously on the startup owner
         * thread. It may request cancellation but must not wait for itself to
         * finish startup rollback.
         */
        const auto waitPolicy =
                lifecycle_.isStartOwnerThread()
                    ? LifecycleCoordinator::WaitPolicy::no_wait
                    : LifecycleCoordinator::WaitPolicy::wait;

        auto transition = lifecycle_.beginStop(waitPolicy);

        if (!transition) {
            /*
             * Possible cases:
             *
             *  - already stopped;
             *  - another stop owns teardown and this caller waited for it;
             *  - cancellable startup owns cleanup;
             *  - startup callback requested cancellation with no_wait.
             */
            return;
        }

        try {
            /*
             * ManagedWorker::stop():
             *
             *  1. requests its stop token;
             *  2. invokes signalStop() through the wake callback;
             *  3. serializes and joins the worker.
             */
            worker_.stop();
        } catch (const std::exception &error) {
            /*
             * Self-stop was rejected above. Any remaining join failure is a
             * structural ownership violation: resources cannot safely be
             * destroyed while the worker may still access them.
             */
            LOG(FATAL) << "failed to stop netlink monitor worker: " << error.what();
        } catch (...) {
            LOG(FATAL) << "failed to stop netlink monitor worker: " << "unknown exception";
        }

        /*
         * onWorkerExit() runs before the worker thread terminates, and stop()
         * joins that thread. Therefore, this reset happens after any
         * exit-handler store and deterministically wins during explicit
         * teardown.
         */
        workerFailed_.store(false, std::memory_order_release);

        // Wait out an in-flight redump before destroying the signal it is
        // polling. Bounded: it was just interrupted and only has to unwind.
        while (redumpInFlight_.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        closeResources();
        netlinkState_.clear();
        transition.complete();

        LOG(INFO) << "NetlinkNetworkMonitor stopped";
    }

    [[nodiscard]] bool isRunning() const {
        return lifecycle_.isRunning() &&
               !workerFailed_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::vector<DeviceEvent> getDevicesSnapshot() const {
        return netlinkState_.getDevicesSnapshot();
    }

    [[nodiscard]] std::vector<LinkEvent> getLinksSnapshot() const {
        return netlinkState_.getLinksSnapshot();
    }

    bool requestRedump() {
        if (worker_.isCurrentThread()) {
            LOG(ERROR) << "requestRedump() must not be called from a netlink callback";
            return false;
        }
        if (!lifecycle_.isRunning()) {
            LOG(WARNING) << "requestRedump() while monitor is not running";
            return false;
        }

        // Reject rather than serialise: two overlapping clear-then-replay
        // sequences leave netlinkState_ matching neither dump.
        bool expected = false;
        if (!redumpInFlight_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            LOG(WARNING) << "requestRedump() ignored: a redump is already in flight";
            return false;
        }
        struct Clear {
            std::atomic<bool> &flag;
            ~Clear() { flag.store(false, std::memory_order_release); }
        } clear{redumpInFlight_};

        // Uses its own eventfd, not stopSignal_ — see the member declaration. stop() signals both.
        if (!redumpStopSignal_) {
            return false;
        }

        try {
            // Clear BEFORE the dump: NetlinkState dedups, so replaying
            // against live state would suppress every message and the
            // resync would emit nothing at all.
            netlinkState_.clear();

            NetlinkInitialDump dump(*redumpStopSignal_,
                                    [this](const nlmsghdr *message) {
                                        processSingleMessage(message);
                                    });
            const auto result = dump.run();
            if (!result.completed()) {
                LOG(ERROR) << "netlink redump failed: status="
                        << NetlinkInitialDump::statusName(result.status)
                        << ", error=" << result.error;
                return false;
            }
            LOG(INFO) << "netlink redump completed";
            return true;
        } catch (const std::exception &error) {
            LOG(ERROR) << "netlink redump threw: " << error.what();
            return false;
        } catch (...) {
            LOG(ERROR) << "netlink redump threw an unknown exception";
            return false;
        }
    }

private:
    using StartTransition = LifecycleCoordinator::CancellableStart;

    [[nodiscard]] bool rollbackStartup(StartTransition &transition) {
        closeResources();
        netlinkState_.clear();
        workerFailed_.store(false, std::memory_order_release);
        // Publish stopped only after startup resources are gone.
        transition.fail();
        return false;
    }

    [[nodiscard]] bool rollbackStartupWithWorker(StartTransition &transition) {
        stopWorkerDuringStartupRollback();
        return rollbackStartup(transition);
    }

    void closeResources() {
        // Wait out an in-flight redump before destroying the signal it is
        // polling. Bounded: signalStop() already interrupted it, so it only
        // has to unwind. Placed here rather than in stop() so the startup
        // rollback paths are covered too.
        while (redumpInFlight_.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        // NetlinkEventLoop borrows stopSignal_.
        liveLoop_.reset();
        stopSignal_.reset();
        redumpStopSignal_.reset();
    }

    void signalStop() noexcept {
        if (!stopSignal_) {
            /*
             * ManagedWorker invokes its wake callback only for a live stop
             * source. A missing eventfd at that point means resource and worker
             * lifetimes have diverged.
             */
            LOG(FATAL) << "netlink stop eventfd is unavailable during teardown";
            return;
        }
        // Interrupt an in-flight redump too — otherwise stop() waits out
        // the full five round-trips before the supervision thread returns.
        if (redumpStopSignal_) {
            (void) redumpStopSignal_->signal();
        }
        for (;;) {
            const int error = stopSignal_->signal();

            if (error == 0) {
                return;
            }

            if (error == EINTR) {
                continue;
            }

            /*
             * The netlink worker polls indefinitely, and the eventfd is its
             * only deterministic wake path. A failed wake would make the
             * subsequent join potentially unbounded, so this component uses
             * the approved abort-over-hang policy.
             */
            LOG(FATAL) << "failed to signal netlink stop eventfd: " << errnoToString(error);
            return;
        }
    }

    void stopWorkerDuringStartupRollback() noexcept {
        try {
            worker_.stop();
        } catch (const std::exception &error) {
            LOG(FATAL)
                    << "failed to stop netlink worker during startup rollback: "
                    << error.what();
        } catch (...) {
            LOG(FATAL)
                    << "failed to stop netlink worker during startup rollback: "
                    "unknown exception";
        }
    }

    void runLiveLoop(std::stop_token stopToken) {
        if (!liveLoop_) {
            throw std::logic_error("NetlinkNetworkMonitor: live event loop is unavailable");
        }
        const NetlinkEventLoop::Result result = liveLoop_->run(std::move(stopToken));
        if (result.stopped()) {
            return;
        }

        throw std::system_error(
            result.error != 0 ? result.error : EIO,
            std::generic_category(),
            std::string("NetlinkEventLoop: ") +
            NetlinkEventLoop::statusName(result.status));
    }

    void onWorkerExit(const ManagedWorker::Exit &exit) noexcept {
        /*
         * This callback runs on the worker thread. It must not call start(),
         * stop(), join(), closeResources(), or otherwise drive lifecycle.
         */
        if (exit.reason == ManagedWorker::ExitReason::exception) {
            workerFailed_.store(true, std::memory_order_release);
            std::string detail{"unknown exception"};
            try {
                if (exit.exception) {
                    std::rethrow_exception(exit.exception);
                }
            } catch (const std::exception &error) {
                detail = error.what();
            } catch (...) {
            }

            LOG(ERROR) << "netlink monitor worker terminated with an exception: " << detail;
            return;
        }

        /*
         * ExitReason is advisory, so consult lifecycle state before recording
         * an unexpected normal return. During an ordinary stop, lifecycle is
         * already stopping.
         */
        const auto lifecycleState = lifecycle_.state();
        if (exit.reason == ManagedWorker::ExitReason::returned &&
            (lifecycleState == LifecycleCoordinator::State::starting ||
             lifecycleState == LifecycleCoordinator::State::running)) {
            workerFailed_.store(true, std::memory_order_release);
            LOG(ERROR) << "netlink monitor worker returned unexpectedly while " << "lifecycle was starting or running";
        }
    }

    void onLinkCallback(const LinkEvent &event) {
        auto changed = netlinkState_.updateLink(event);
        if (changed && callbacks_.onLinkChanged) {
            callbacks_.onLinkChanged(*changed);
        }
    }

    void onIpCallback(const InterfaceIpEvent &event) {
        auto changed = netlinkState_.updateAddress(event);
        if (changed && callbacks_.onInterfaceIpChanged) {
            callbacks_.onInterfaceIpChanged(*changed);
        }
    }

    void onFdbCallback(const FdbEvent &event) {
        if (!watchedInterfaces_.empty() && !watchedInterfaces_.contains(event.ifname)) {
            return;
        }
        auto changed = netlinkState_.updateFdb(event, onDeviceHandler_);
        if (changed && callbacks_.onFdbChanged) {
            callbacks_.onFdbChanged(*changed);
        }
    }

    void onNeighCallback(const NeighborEvent &event) {
        if (!watchedInterfaces_.empty() && !watchedInterfaces_.contains(event.ifname)) {
            return;
        }
        auto changed = netlinkState_.updateNeighbor(event, onDeviceHandler_);
        if (changed && callbacks_.onNeighborChanged) {
            callbacks_.onNeighborChanged(*changed);
        }
    }

    void processSingleMessage(const nlmsghdr *message) {
        processMessage(
            message,
            onLinkHandler_,
            onIpHandler_,
            onFdbHandler_,
            onNeighHandler_);
    }

    MonitorCallbacks callbacks_;
    std::set<std::string> watchedInterfaces_;
    /*
     * Set only through the private test-support constructor. Called once per
     * newly claimed startup epoch. Each returned descriptor is borrowed.
     */
    std::function<int()> liveFdProvider_;

    /*
     * Resource dependency order:
     *
     * liveLoop_ borrows stopSignal_, so liveLoop_ must be destroyed first.
     */
    std::optional<EventFdSignal> stopSignal_;
    /*
     * Separate from stopSignal_ on purpose. waitForNetlinkDataOrStop()
     * DRAINS the signal it observes, so sharing one eventfd between the
     * live worker and a concurrent redump lets whichever wakes first
     * consume the other's wake — leaving the live worker blocked in
     * poll(-1) with no further signal coming, and stop() hung on the join.
     */
    std::optional<EventFdSignal> redumpStopSignal_;
    std::optional<NetlinkEventLoop> liveLoop_;

    LifecycleCoordinator lifecycle_;
    NetlinkState netlinkState_;

    /// Monitor-owned worker health. The coordinator tracks the resource epoch
    /// only and intentionally does not transition itself on worker exit; this
    /// flag records loss of the live producer while the epoch remains running.
    std::atomic<bool> workerFailed_{false};
    /// Excludes concurrent redumps; see requestRedump().
    std::atomic<bool> redumpInFlight_{false};

    // Pre-bound handlers: constructed once, no per-message binding.
    std::function<void(const LinkEvent &)> onLinkHandler_;
    std::function<void(const InterfaceIpEvent &)> onIpHandler_;
    std::function<void(const FdbEvent &)> onFdbHandler_;
    std::function<void(const NeighborEvent &)> onNeighHandler_;
    std::function<void(const DeviceEvent &)> onDeviceHandler_;
    /*
     * Must remain last. Its work, wake, and exit callbacks capture this and
     * access every relevant member above. Reverse member destruction therefore
     * stops and joins the worker before those members are destroyed.
     */
    ManagedWorker worker_;
};

NetlinkNetworkMonitor::NetlinkNetworkMonitor(
    MonitorCallbacks callbacks,
    std::set<std::string> watchedInterfaces)
    : impl_(std::make_unique<Impl>(std::move(callbacks), std::move(watchedInterfaces))) {
}

NetlinkNetworkMonitor::NetlinkNetworkMonitor(
    std::function<int()> liveFdProvider,
    MonitorCallbacks callbacks,
    std::set<std::string> watchedInterfaces)
    : impl_(
        std::make_unique<Impl>(
            std::move(callbacks),
            std::move(watchedInterfaces),
            std::move(liveFdProvider))) {
}

NetlinkNetworkMonitor::~NetlinkNetworkMonitor() = default;

bool NetlinkNetworkMonitor::start() {
    return impl_->start();
}

void NetlinkNetworkMonitor::stop() {
    impl_->stop();
}

bool NetlinkNetworkMonitor::isRunning() const {
    return impl_->isRunning();
}

std::vector<DeviceEvent> NetlinkNetworkMonitor::getDevicesSnapshot() const {
    return impl_->getDevicesSnapshot();
}

std::vector<LinkEvent> NetlinkNetworkMonitor::getLinksSnapshot() const {
    return impl_->getLinksSnapshot();
}

bool NetlinkNetworkMonitor::requestRedump() {
    return impl_->requestRedump();
}
} // namespace RSCGroup
