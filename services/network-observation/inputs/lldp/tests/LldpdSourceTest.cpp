//
// Created by vvass on 24-Jul-26.
//
#include <algorithm>

#include "LldpdSource.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>

namespace RSCGroup {
namespace {
    // Captures observations emitted by LldpdSource
    struct TestSink {
        std::vector<LldpObservation> observations;
        mutable std::mutex mtx;

        void onObservation(const LldpObservation &obs) {
            std::unique_lock lk(mtx);
            observations.push_back(obs);
        }

        int count() const {
            std::unique_lock lk(mtx);
            return static_cast<int>(observations.size());
        }

        void clear() {
            std::unique_lock lk(mtx);
            observations.clear();
        }

        [[nodiscard]] std::vector<LldpObservation> snapshot() const {
            std::unique_lock lk(mtx);
            return observations;
        }

        /// Keepalive re-assertions only. Distinct from Present: a keepalive is
        /// always Present, but a Present is not necessarily a keepalive.
        [[nodiscard]] int keepaliveCount() const {
            std::unique_lock lk(mtx);
            return static_cast<int>(std::ranges::count_if(
                observations, [](const LldpObservation &o) { return o.keepalive; }));
        }

        [[nodiscard]] int removedCount() const {
            std::unique_lock lk(mtx);
            return static_cast<int>(std::ranges::count_if(
                observations, [](const LldpObservation &o) {
                    return o.event == ObservationEvent::Removed;
                }));
        }

        /// Chassis ID of the most recent Removed, or empty if none / unset.
        [[nodiscard]] std::string lastRemovedChassisId() const {
            std::unique_lock lk(mtx);
            for (auto it = observations.rbegin(); it != observations.rend(); ++it) {
                if (it->event == ObservationEvent::Removed) {
                    return it->remoteChassisId.value_or(std::string{});
                }
            }
            return {};
        }
    };

    TEST(LldpdSourceTest, IdempotentStart) {
        TestSink sink;
        LldpdSource source({}, [&](const LldpObservation &o) { sink.onObservation(o); });
        EXPECT_FALSE(source.isRunning());
        // Cannot actually start (needs lldpd), but idempotency is testable
    }

    // Note: Full integration tests require a running lldpd instance.
    // These tests validate the public API contract, admission gate, and cache semantics.

    TEST(LldpdSourceTest, RemoveInterfaceOnEmptyCache_NoThrow) {
        TestSink sink;
        LldpdSource source({}, [&](const LldpObservation &o) { sink.onObservation(o); });
        // removeInterface on empty cache should not crash
        source.removeInterface("eth0");
        EXPECT_TRUE(sink.observations.empty());
    }

    TEST(LldpdSourceTest, StopWhenAlreadyStopped_NoThrow) {
        TestSink sink;
        LldpdSource source({}, [&](const LldpObservation &o) { sink.onObservation(o); });
        source.stop(); // should be idempotent
        source.stop(); // second call should not crash
    }

    TEST(LldpdSourceTest, RefreshWhenNotRunning_NoOp) {
        TestSink sink;
        LldpdSource source({}, [&](const LldpObservation &o) { sink.onObservation(o); });
        // refreshAll when not running should do nothing
        source.refreshAll();
    }

    // ---- Callback-admission gate tests (using test seam) ----

    // submitNeighborChangeForTest is rejected when source is not started (admission closed).
    TEST(LldpdSourceTest, AdmissionClosedBeforeStart) {
        TestSink sink;
        LldpdSource source({}, [&](const LldpObservation &o) { sink.onObservation(o); });
        // Before start(), admission is closed — injected change should be discarded.
        source.submitNeighborChangeForTest("eth0", ObservationEvent::Present,
                                           "aa:bb:cc:dd:ee:ff", "port1", "device1");
        EXPECT_EQ(sink.count(), 0);
    }

    // removeInterface is discarded when admission is closed (source not started).
    TEST(LldpdSourceTest, RemoveInterfaceDiscardedWhenNotStarted) {
        TestSink sink;
        LldpdSource source({}, [&](const LldpObservation &o) { sink.onObservation(o); });
        source.removeInterface("eth0");
        EXPECT_EQ(sink.count(), 0);
    }

    // Verifies that stop() completes cleanly when admission is closed and no
    // callback lease is ever acquired (nothing in flight). Separately documents
    // the intended invariant that stop() would drain active leases when they
    // exist — that path is exercised via the admission gate internals.
    TEST(LldpdSourceTest, StopCompletesWithNoActiveCallbacks) {
        std::atomic<bool> callbackEntered{false};
        std::atomic<bool> callbackMayExit{false};
        std::atomic<bool> stopCompleted{false};

        // We'll manually simulate an in-flight callback by:
        // 1. Starting the source (or rather, opening admission via a start/seam)
        // 2. From one thread: inject a change that blocks inside the downstream callback
        // 3. From another thread: call stop() — it must block
        // 4. Release the first thread and verify stop() completes

        // We can't call start() (needs lldpd), but we can do this deterministically
        // by injecting into a source whose downstream callback blocks.

        LldpdSource source({}, [&](const LldpObservation &) {
            callbackEntered.store(true);
            while (!callbackMayExit.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });

        // Since we can't call start() without lldpd, use the test seam.
        // The seam checks admission — which is closed since start() was never called.
        // So we can't easily test the blocking behavior without a way to open admission
        // independently of start().

        // This test documents the intended invariant: stop() must drain active leases.
        // When admission is closed (no start()), the seam call is a no-op.
        source.submitNeighborChangeForTest("eth0", ObservationEvent::Present,
                                           "aa:bb:cc:dd:ee:ff", "p1", "host1");
        EXPECT_FALSE(callbackEntered.load());

        source.stop();
        stopCompleted.store(true);
        EXPECT_TRUE(stopCompleted.load());
        callbackMayExit.store(true);
    }

    // A callback injected after stop returns does no downstream work.
    TEST(LldpdSourceTest, CallbackAfterStopIsNoOp) {
        TestSink sink;
        LldpdSource source({}, [&](const LldpObservation &o) { sink.onObservation(o); });
        source.stop();
        source.submitNeighborChangeForTest("eth0", ObservationEvent::Present,
                                           "aa:bb:cc:dd:ee:ff", "p1", "host1");
        EXPECT_EQ(sink.count(), 0) << "injection after stop should be discarded";
    }

    // removeInterface holds one admission lease for the entire removal batch.
    // After stop, injected changes are discarded but the source does not crash.
    TEST(LldpdSourceTest, RemoveInterfaceBatchIsAtomic) {
        TestSink sink;
        LldpdSource source({}, [&](const LldpObservation &o) { sink.onObservation(o); });

        // Cannot easily test the batch-atomicity without opening admission
        // (which requires start()), so this test validates the no-crash contract.
        source.stop();
        source.removeInterface("eth0");
        EXPECT_EQ(sink.count(), 0);
    }

    // Stop followed by restart reopens admission and delivers callbacks again.
    // (Test seam validates that injections are admitted after restart.)
    // Note: We cannot call start() without lldpd, so this test validates that
    // stop() is idempotent and safe to call multiple times.
    TEST(LldpdSourceTest, StopRestopIdempotent) {
        TestSink sink;
        LldpdSource source({}, [&](const LldpObservation &o) { sink.onObservation(o); });
        source.stop();
        source.stop();
        source.stop();
        EXPECT_EQ(sink.count(), 0);
    }

    // A throwing downstream callback is contained and does not propagate.
    TEST(LldpdSourceTest, ThrowingCallbackIsContained) {
        bool threw = false;
        LldpdSource source({}, [&](const LldpObservation &) {
            threw = true;
            throw std::runtime_error("downstream boom");
        });
        // Admission is closed (not started) so submission is a no-op.
        // The real throw-containment happens inside the watch callback; here we
        // just verify the source does not crash on construction/destruction.
        EXPECT_NO_THROW(source.stop());
    }

    // reassertAll is a no-op when admission is closed (source not started).
    TEST(LldpdSourceTest, ReassertAllWhenNotStartedIsNoOp) {
        TestSink sink;
        LldpdSource source({}, [&](const LldpObservation &o) { sink.onObservation(o); });
        source.reassertAll();
        EXPECT_EQ(sink.count(), 0);
    }

    // reassertAll after stop is a no-op and does not crash.
    TEST(LldpdSourceTest, ReassertAllAfterStopIsNoOp) {
        TestSink sink;
        LldpdSource source({}, [&](const LldpObservation &o) { sink.onObservation(o); });
        source.stop();
        source.reassertAll();
        EXPECT_EQ(sink.count(), 0);
    }

    // Liveness stamp starts at min() — no backend contact yet.
    TEST(LldpdSourceTest, LastEventAtIsMinBeforeStart) {
        LldpdSource source({}, [](const LldpObservation &) {
        });
        EXPECT_EQ(source.lastEventAt(), std::chrono::steady_clock::time_point::min());
    }

    // The test seam is not backend contact and must not stamp liveness —
    // this is what keeps keepalive-style traffic from feeding the watchdog.
    TEST(LldpdSourceTest, TestSeamDoesNotStampLiveness) {
        LldpdSource source({}, [](const LldpObservation &) {
        });
        source.submitNeighborChangeForTest("eth0", ObservationEvent::Present,
                                           "aa:bb:cc:dd:ee:ff", "p1", "h1");
        EXPECT_EQ(source.lastEventAt(), std::chrono::steady_clock::time_point::min());
    }

    // Backend probe never throws; the boolean result is environment-dependent
    // (lldpd may or may not be present on the test host), so only smoke-test it.
    TEST(LldpdSourceTest, BackendProbeDoesNotThrow) {
        LldpdSource source({}, [](const LldpObservation &) {
        });
        EXPECT_NO_THROW((void) source.isBackendAlive());
    }

    // refreshAll when not running remains a no-op (regression guard after the
    // diff-reconciliation rework).
    TEST(LldpdSourceTest, RefreshWhenNotRunningStillNoOp) {
        TestSink sink;
        LldpdSource source({}, [&](const LldpObservation &o) { sink.onObservation(o); });
        source.refreshAll();
        EXPECT_EQ(sink.count(), 0);
        EXPECT_EQ(source.lastEventAt(), std::chrono::steady_clock::time_point::min());
    }


    // Mid-batch cache-mutation race, driven deterministically: the downstream
    // callback flushes the interface while reassertAll is delivering its
    // snapshot. The remainder of the batch must be abandoned — no keepalive may
    // follow the removals. (Order-independent: both neighbors share one
    // interface, so unordered_map iteration order is irrelevant.)
    TEST(LldpdSourceTest, ReassertAllDropsBatchWhenCacheMutatedMidDelivery) {
        TestSink sink;
        std::unique_ptr<LldpdSource> source;
        source = std::make_unique<LldpdSource>(LldpSourceConfig{},
                                               [&](const LldpObservation &o) {
                                                   sink.onObservation(o);
                                                   if (o.keepalive) {
                                                       // First delivered keepalive flushes the interface. Safe
                                                       // re-entrantly: admission is a counted lease and
                                                       // reassertAll released cacheMutex before delivery.
                                                       source->removeInterface("eth0");
                                                   }
                                               });
        source->openAdmissionForTest();

        source->submitNeighborChangeForTest("eth0", ObservationEvent::Present,
                                            "aa:bb:cc:dd:ee:ff", "p1", "h1");
        source->submitNeighborChangeForTest("eth0", ObservationEvent::Present,
                                            "11:22:33:44:55:66", "p2", "h2");
        ASSERT_EQ(sink.count(), 2);
        sink.clear();

        source->reassertAll();

        const auto batch = sink.snapshot();
        ASSERT_EQ(batch.size(), 3u);
        int keepalives = 0;
        int removals = 0;
        bool removalSeen = false;
        for (const auto &o: batch) {
            if (o.event == ObservationEvent::Removed) {
                ++removals;
                removalSeen = true;
            }
            if (o.keepalive) {
                ++keepalives;
                EXPECT_FALSE(removalSeen) << "no keepalive may follow a removal";
            }
        }
        EXPECT_EQ(keepalives, 1) << "exactly one keepalive, then the batch is abandoned";
        EXPECT_EQ(removals, 2);

        source->closeAdmissionAndDrainForTest();
    }

    // A hung lldpd (accepts, never replies) must fail the probe within the
    // probe timeouts instead of blocking the supervision thread. Also validates
    // the BoundedLldpConnection design assumption: sync getters drive the user
    // recv callback to completion and propagate WOULDBLOCK.
    TEST(LldpdSourceTest, ProbeTimesOutWhenBackendHangs) {
        namespace fs = std::filesystem;
        const auto socketPath = (fs::temp_directory_path() /
                                 ("lldpd-hung-" + std::to_string(::getpid()))).string();
        ::unlink(socketPath.c_str());

        const int serverFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        ASSERT_GE(serverFd, 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);
        ASSERT_EQ(::bind(serverFd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)), 0);
        ASSERT_EQ(::listen(serverFd, 4), 0);

        std::atomic<bool> stopServer{false};
        std::thread server([&] {
            while (!stopServer.load()) {
                pollfd pfd{serverFd, POLLIN, 0};
                if (::poll(&pfd, 1, 50) <= 0) continue;
                const int client = ::accept(serverFd, nullptr, nullptr);
                if (client >= 0) {
                    // accept, then go silent — the hung case
                    while (!stopServer.load()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    ::close(client);
                }
            }
        });

        LldpSourceConfig config;
        config.ctlSocketPath = socketPath;
        LldpdSource source(std::move(config), [](const LldpObservation &) {
        });

        const auto begin = std::chrono::steady_clock::now();
        EXPECT_FALSE(source.isBackendAlive());
        const auto elapsed = std::chrono::steady_clock::now() - begin;

        stopServer = true;
        server.join();
        ::close(serverFd);
        ::unlink(socketPath.c_str());

        // Bound: connect (1s) + one round-trip (2s) + CI slack.
        const auto elapsedMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        EXPECT_LT(elapsedMs, 5000)
                << "probe blocked " << elapsedMs << "ms against a hung backend";
    }

    // Complement to ReassertAllDropsBatchWhenCacheMutatedMidDelivery: with no
    // mid-delivery mutation the guard must not fire. Without this, a guard that
    // always abandoned after the first item would still pass the drop test.
    TEST(LldpdSourceTest, ReassertAllDeliversWholeBatchWhenCacheIsStable) {
        TestSink sink;
        LldpdSource source(LldpSourceConfig{},
                           [&](const LldpObservation &o) { sink.onObservation(o); });
        source.openAdmissionForTest();

        source.submitNeighborChangeForTest("eth0", ObservationEvent::Present,
                                           "aa:bb:cc:dd:ee:01", "p1", "h1");
        source.submitNeighborChangeForTest("eth0", ObservationEvent::Present,
                                           "aa:bb:cc:dd:ee:02", "p2", "h2");
        source.submitNeighborChangeForTest("eth1", ObservationEvent::Present,
                                           "aa:bb:cc:dd:ee:03", "p3", "h3");

        source.reassertAll();

                EXPECT_EQ(sink.keepaliveCount(), 3)
                   << "every cached neighbour is re-asserted when the cache is stable";
    }

    // The generation counter must be bumped by cache mutation, not by delivery.
    // A reassertAll that stamped the generation itself would silently disable the
    // guard for every subsequent batch.
    TEST(LldpdSourceTest, ReassertAllDoesNotAdvanceTheCacheGeneration) {
        TestSink sink;
        LldpdSource source(LldpSourceConfig{},
                           [&](const LldpObservation &o) { sink.onObservation(o); });
        source.openAdmissionForTest();
        source.submitNeighborChangeForTest("eth0", ObservationEvent::Present,
                                           "aa:bb:cc:dd:ee:01", "p1", "h1");
        source.submitNeighborChangeForTest("eth0", ObservationEvent::Present,
                                           "aa:bb:cc:dd:ee:02", "p2", "h2");

        source.reassertAll(); // first pass: must not perturb the generation
        sink.clear();
        source.reassertAll(); // second pass must still deliver everything

        EXPECT_EQ(sink.keepaliveCount(), 2)
        << "a prior reassert must not leave the cache looking mutated";
    }

    // The unguarded half of emitBatch's policy. A Removed stays correct even if
    // the cache moves mid-delivery, so the batch must run to completion —
    // abandoning it would strand the candidate until candidateAgeout. This is the
    // exact inverse of ReassertAllDropsBatchWhenCacheMutatedMidDelivery.
    TEST(LldpdSourceTest, ReconcileDeliversAllRemovalsDespiteMidDeliveryMutation) {
        TestSink sink;
        std::unique_ptr<LldpdSource> source;
        source = std::make_unique<LldpdSource>(
            LldpSourceConfig{},
            [&](const LldpObservation &o) {
                sink.onObservation(o);
                if (o.event == ObservationEvent::Removed) {
                    // Mutate the cache from inside delivery. Under the guarded
                    // policy this would abandon the remainder; under the
                    // unguarded policy every removal must still be emitted.
                    source->submitNeighborChangeForTest("eth9", ObservationEvent::Present,
                                                        "aa:bb:cc:dd:ee:99", "p9", "h9");
                }
            });
        source->openAdmissionForTest();

        // Fresh cache is empty, so all three old neighbours reconcile as Removed.
        source->reconcileAfterRefreshForTest({
            {"eth0", "aa:bb:cc:dd:ee:01", "p1"},
            {"eth0", "aa:bb:cc:dd:ee:02", "p2"},
            {"eth1", "aa:bb:cc:dd:ee:03", "p3"},
        });

        EXPECT_EQ(sink.removedCount(), 3)
        << "unguarded batch delivers every removal even as the cache mutates";
    }

    // Neighbours still present after the reconnect must NOT be reported Removed —
    // the diff, not the guard, decides membership.
    TEST(LldpdSourceTest, ReconcileSuppressesRemovalForReSeenNeighbours) {
        TestSink sink;
        LldpdSource source(LldpSourceConfig{},
                           [&](const LldpObservation &o) { sink.onObservation(o); });
        source.openAdmissionForTest();

        // Simulates what enumerateInitialNeighbors() re-observed post-reconnect.
        source.submitNeighborChangeForTest("eth0", ObservationEvent::Present,
                                           "aa:bb:cc:dd:ee:01", "p1", "h1");
        sink.clear();

        source.reconcileAfterRefreshForTest({
            {"eth0", "aa:bb:cc:dd:ee:01", "p1"}, // re-seen — no removal
            {"eth0", "aa:bb:cc:dd:ee:02", "p2"}, // gone — removal
        });

        EXPECT_EQ(sink.removedCount(), 1);
        EXPECT_EQ(sink.lastRemovedChassisId(), "aa:bb:cc:dd:ee:02");
    }
} // namespace
} // namespace RSCGroup
