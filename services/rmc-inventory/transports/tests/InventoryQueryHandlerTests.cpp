//
// Created by vvass on 03-Sep-26.
//

#include "InventoryQueryHandler.h"

#include <IInventoryQueryService.h>
#include <ServiceBinding.h>

#include <dbus-cxx/variant.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;
using namespace RSCGroup;
using namespace interop_contract::inventory;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

template<typename Function>
void expectContained(Function&& function, const std::string& message)
{
    try {
        std::forward<Function>(function)();
    } catch (const std::exception& error) {
        std::cerr << message << ": escaped std::exception: "
                  << error.what() << '\n';
        std::exit(EXIT_FAILURE);
    } catch (...) {
        std::cerr << message << ": escaped non-standard exception\n";
        std::exit(EXIT_FAILURE);
    }
}

template<typename Predicate>
bool waitFor(
    Predicate&& predicate,
    std::chrono::milliseconds timeout = 500ms)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < deadline) {
        if (std::forward<Predicate>(predicate)()) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }

    return std::forward<Predicate>(predicate)();
}

class FakeInventoryQueryService final : public IInventoryQueryService {
public:
    enum class Failure {
        none,
        standard,
        nonStandard,
    };

    InventorySnapshot getIdentity() const override
    {
        ++identityCalls_;
        maybeThrow();
        return {};
    }

    InventoryFields getField(const std::string&) const override
    {
        ++fieldCalls_;
        maybeThrow();
        return {};
    }

    SourceStateMap getSourceStates() const override
    {
        ++sourceStateCalls_;
        maybeThrow();
        return {};
    }

    InventoryIssues getIssues() const override
    {
        ++issueCalls_;
        maybeThrow();
        return {};
    }

    bool getReady() const override
    {
        ++readyCalls_;
        maybeThrow();

        std::unique_lock lock(readyMutex_);
        if (blockReady_) {
            readyEntered_ = true;
            readyEnteredCv_.notify_all();

            readyReleaseCv_.wait(lock, [this] {
                return readyReleased_;
            });
        }

        return readyValue_;
    }

    std::string getPhase() const override
    {
        ++phaseCalls_;
        maybeThrow();
        return phase_;
    }

    std::uint64_t getVersion() const override
    {
        ++versionCalls_;
        maybeThrow();
        return version_;
    }

    void refresh() override
    {
        ++refreshCalls_;
        maybeThrow();
    }

    void setFailure(Failure failure)
    {
        failure_.store(failure, std::memory_order_release);
    }

    void setReadyValue(bool value)
    {
        std::scoped_lock lock(readyMutex_);
        readyValue_ = value;
    }

    void setBlockReady(bool value)
    {
        std::scoped_lock lock(readyMutex_);
        blockReady_ = value;
        readyEntered_ = false;
        readyReleased_ = !value;
    }

    void waitUntilReadyEntered() const
    {
        std::unique_lock lock(readyMutex_);
        readyEnteredCv_.wait(lock, [this] {
            return readyEntered_;
        });
    }

    void releaseReady() const
    {
        {
            std::scoped_lock lock(readyMutex_);
            readyReleased_ = true;
        }
        readyReleaseCv_.notify_all();
    }

    [[nodiscard]] int refreshCalls() const
    {
        return refreshCalls_.load(std::memory_order_acquire);
    }

private:
    void maybeThrow() const
    {
        switch (failure_.load(std::memory_order_acquire)) {
            case Failure::none:
                return;

            case Failure::standard:
                throw std::runtime_error("fake query failure");

            case Failure::nonStandard:
                throw 42;
        }
    }

    mutable std::atomic<Failure> failure_{Failure::none};

    mutable std::atomic<int> identityCalls_{0};
    mutable std::atomic<int> fieldCalls_{0};
    mutable std::atomic<int> sourceStateCalls_{0};
    mutable std::atomic<int> issueCalls_{0};
    mutable std::atomic<int> readyCalls_{0};
    mutable std::atomic<int> phaseCalls_{0};
    mutable std::atomic<int> versionCalls_{0};
    std::atomic<int> refreshCalls_{0};

    mutable std::mutex readyMutex_;
    mutable std::condition_variable readyEnteredCv_;
    mutable std::condition_variable readyReleaseCv_;
    mutable bool blockReady_{false};
    mutable bool readyEntered_{false};
    mutable bool readyReleased_{true};
    bool readyValue_{true};

    std::string phase_{"live"};
    std::uint64_t version_{17};
};

void expectFallbacks(
    InventoryQueryHandler& handler,
    const std::string& context)
{
    expect(
        handler.getIdentity().empty(),
        context + ": identity fallback must be empty");

    expect(
        handler.getField("nodeName").empty(),
        context + ": field fallback must be empty");

    expect(
        handler.getSourceStates().empty(),
        context + ": source-state fallback must be empty");

    expect(
        handler.getIssues().empty(),
        context + ": issues fallback must be empty");

    expect(
        !handler.getReady(),
        context + ": ready fallback must be false");

    expect(
        handler.getPhase() == "unknown",
        context + ": phase fallback must be unknown");

    expect(
        handler.getVersion() == 0,
        context + ": version fallback must be zero");
}

void testFallbacksWhenNotBound()
{
    ServiceBinding<IInventoryQueryService> binding;
    InventoryQueryHandler handler(binding);

    expectFallbacks(handler, "before bind");

    handler.refresh();
}

void testFallbacksAfterDetach()
{
    ServiceBinding<IInventoryQueryService> binding;
    FakeInventoryQueryService service;
    InventoryQueryHandler handler(binding);

    binding.bind(&service);

    expect(handler.getReady(), "bound handler should reach service");
    expect(handler.getPhase() == "live", "bound phase should reach service");
    expect(handler.getVersion() == 17, "bound version should reach service");

    handler.refresh();
    expect(service.refreshCalls() == 1, "bound refresh should reach service");

    binding.detach();

    expectFallbacks(handler, "after detach");

    handler.refresh();
    expect(
        service.refreshCalls() == 1,
        "refresh after detach must not reach service");
}

void testStandardExceptionsAreContained()
{
    ServiceBinding<IInventoryQueryService> binding;
    FakeInventoryQueryService service;
    InventoryQueryHandler handler(binding);

    binding.bind(&service);
    service.setFailure(FakeInventoryQueryService::Failure::standard);

    expectContained(
        [&] {
            expectFallbacks(handler, "standard exception");
        },
        "standard query exceptions must be contained");

    expectContained(
        [&] {
            handler.refresh();
        },
        "standard refresh exception must be contained");

    binding.detach();
}

void testNonStandardExceptionsAreContained()
{
    ServiceBinding<IInventoryQueryService> binding;
    FakeInventoryQueryService service;
    InventoryQueryHandler handler(binding);

    binding.bind(&service);
    service.setFailure(FakeInventoryQueryService::Failure::nonStandard);

    expectContained(
        [&] {
            expectFallbacks(handler, "non-standard exception");
        },
        "non-standard query exceptions must be contained");

    expectContained(
        [&] {
            handler.refresh();
        },
        "non-standard refresh exception must be contained");

    binding.detach();
}

void testDetachWaitsForAdmittedQueryAndRejectsNewQueries()
{
    ServiceBinding<IInventoryQueryService> binding;
    FakeInventoryQueryService service;
    InventoryQueryHandler handler(binding);

    service.setReadyValue(true);
    service.setBlockReady(true);
    binding.bind(&service);

    auto activeQuery = std::async(std::launch::async, [&handler] {
        return handler.getReady();
    });

    service.waitUntilReadyEntered();

    auto detach = std::async(std::launch::async, [&binding] {
        binding.detach();
    });

    // getPhase() normally returns "live". Seeing "unknown" proves detach()
    // closed admission while still waiting for the active getReady() lease.
    expect(
        waitFor([&] {
            return handler.getPhase() == "unknown";
        }),
        "detach must close admission before waiting for active queries");

    expect(
        detach.wait_for(25ms) == std::future_status::timeout,
        "detach must wait while an admitted query is active");

    expect(
        activeQuery.wait_for(25ms) == std::future_status::timeout,
        "blocked admitted query should remain active before release");

    service.releaseReady();

    expect(
        activeQuery.get(),
        "the admitted query should complete with the service result");

    expect(
        detach.wait_for(500ms) == std::future_status::ready,
        "detach should complete after the active query releases its lease");

    detach.get();

    expect(
        !handler.getReady(),
        "queries after completed detach must receive fallback values");
}

void testBindAfterDetachReopensAdmission()
{
    ServiceBinding<IInventoryQueryService> binding;
    FakeInventoryQueryService first;
    FakeInventoryQueryService second;
    InventoryQueryHandler handler(binding);

    first.setReadyValue(false);
    second.setReadyValue(true);

    binding.bind(&first);
    expect(
        !handler.getReady(),
        "handler should initially reach the first service");

    binding.detach();
    expect(
        !handler.getReady(),
        "detached handler should return the ready fallback");

    binding.bind(&second);
    expect(
        handler.getReady(),
        "rebinding should reopen admission for the second service");

    binding.detach();
}

} // namespace

int main()
{
    testFallbacksWhenNotBound();
    testFallbacksAfterDetach();
    testStandardExceptionsAreContained();
    testNonStandardExceptionsAreContained();
    testDetachWaitsForAdmittedQueryAndRejectsNewQueries();
    testBindAfterDetachReopensAdmission();

    return EXIT_SUCCESS;
}
