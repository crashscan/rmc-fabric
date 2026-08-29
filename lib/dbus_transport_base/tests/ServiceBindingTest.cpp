// Tests for ServiceBinding<T> lifecycle and thread-safety.
//
// These tests do NOT require D-Bus; they exercise the binding abstraction in
// isolation using a minimal fake service.

#include "ServiceBinding.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include <cassert>
#include <cstdlib>
#include <iostream>

namespace RSCGroup {
namespace {

struct FakeService {
    std::string name;
    int callCount = 0;

    std::string query() { ++callCount; return name; }
};

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(EXIT_FAILURE);
    }
}

// --- Basic lifecycle ---

void testBindAndAcquire()
{
    ServiceBinding<FakeService> binding;
    FakeService svc{"alice"};

    binding.bind(&svc);
    auto guard = binding.acquire();
    expect(static_cast<bool>(guard), "guard should be non-null after bind");
    expect(guard->query() == "alice", "guard->query() should return service name");
}

void testDetachPreventsAcquire()
{
    ServiceBinding<FakeService> binding;
    FakeService svc{"bob"};

    binding.bind(&svc);
    binding.detach();

    auto guard = binding.acquire();
    expect(!static_cast<bool>(guard), "guard should be null after detach");
    expect(guard.get() == nullptr, "guard.get() should be nullptr after detach");
}

void testRebindAfterDetach()
{
    ServiceBinding<FakeService> binding;
    FakeService svc1{"first"};
    FakeService svc2{"second"};

    binding.bind(&svc1);
    binding.detach();
    binding.bind(&svc2);

    auto guard = binding.acquire();
    expect(static_cast<bool>(guard), "guard should be non-null after rebind");
    expect(guard->query() == "second", "guard should point to second service");
}

void testAcquireWithoutBind()
{
    ServiceBinding<FakeService> binding;
    auto guard = binding.acquire();
    expect(!static_cast<bool>(guard), "guard should be null without prior bind");
}

void testGuardAllowsMultipleAccesses()
{
    ServiceBinding<FakeService> binding;
    FakeService svc{"multi"};
    binding.bind(&svc);

    {
        auto guard = binding.acquire();
        expect(static_cast<bool>(guard), "guard should be valid");
        guard->query();
        guard->query();
        expect(svc.callCount == 2, "callCount should be 2");
    }
    // guard released here; binding still live
    auto guard2 = binding.acquire();
    expect(static_cast<bool>(guard2), "second acquire should succeed");
}

// --- Concurrent tests ---

// detach() must wait for an in-flight call to finish.
void testDetachWaitsForInFlight()
{
    ServiceBinding<FakeService> binding;
    FakeService svc{"concurrent"};
    binding.bind(&svc);

    std::atomic<bool> handlerEnteredGuard{false};
    std::atomic<bool> okToReleaseGuard{false};
    std::atomic<bool> detachCompleted{false};

    // Thread A: simulates a long-running D-Bus handler
    std::thread handlerThread([&] {
        auto guard = binding.acquire();
        expect(static_cast<bool>(guard), "handler should acquire before detach");
        handlerEnteredGuard.store(true);
        // Hold the guard until the test signals we can release
        while (!okToReleaseGuard.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        // guard released here at end of scope
    });

    // Wait until handler has the guard
    while (!handlerEnteredGuard.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    // Thread B: simulates transport shutdown
    std::thread stopThread([&] {
        binding.detach(); // must block until handlerThread releases the guard
        detachCompleted.store(true);
    });

    // Give stopThread a moment to block on the unique_lock
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // detach should NOT have completed yet
    expect(!detachCompleted.load(), "detach should be blocked while guard is held");

    // Release the handler guard
    okToReleaseGuard.store(true);
    handlerThread.join();
    stopThread.join();

    expect(detachCompleted.load(), "detach should complete after guard is released");

    // Further acquires should fail
    auto guard2 = binding.acquire();
    expect(!static_cast<bool>(guard2), "acquire after detach should fail");
}

// Multiple concurrent acquires should all succeed simultaneously.
void testConcurrentAcquires()
{
    ServiceBinding<FakeService> binding;
    FakeService svc{"parallel"};
    binding.bind(&svc);

    constexpr int kThreads = 8;
    std::atomic<int> concurrent{0};
    std::atomic<int> errors{0};

    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&] {
            auto guard = binding.acquire();
            if (!guard) { ++errors; return; }
            ++concurrent;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            --concurrent;
        });
    }

    for (auto& t : threads) t.join();

    expect(errors.load() == 0, "all concurrent acquires should succeed");
}

} // namespace
} // namespace RSCGroup

int main()
{
    using namespace RSCGroup;

    testBindAndAcquire();
    testDetachPreventsAcquire();
    testRebindAfterDetach();
    testAcquireWithoutBind();
    testGuardAllowsMultipleAccesses();
    testDetachWaitsForInFlight();
    testConcurrentAcquires();

    std::cout << "All ServiceBinding tests passed.\n";
    return EXIT_SUCCESS;
}
