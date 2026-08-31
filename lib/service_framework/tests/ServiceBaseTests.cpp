#include <ServiceBase.h>
#include <ITransport.h>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace RSCGroup;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

class RecordingTransport final : public IServiceTransport {
public:
    RecordingTransport(std::string name,
                       std::vector<std::string>& events,
                       bool startResult = true,
                       bool throwOnReady = false,
                       bool throwOnStop = false)
        : name_(std::move(name))
        , events_(events)
        , startResult_(startResult)
        , throwOnReady_(throwOnReady)
        , throwOnStop_(throwOnStop)
    {
    }

    bool start() override
    {
        events_.push_back(name_ + ".start");
        ++startCount_;
        return startResult_;
    }

    void stop() override
    {
        events_.push_back(name_ + ".stop");
        ++stopCount_;
        if (throwOnStop_) {
            throw std::runtime_error(name_ + ".stop failure");
        }
    }

    void publishReadyChanged(bool ready) override
    {
        events_.push_back(name_ + (ready ? ".ready.true" : ".ready.false"));
        readyTrueCount_ += ready ? 1 : 0;
        readyFalseCount_ += ready ? 0 : 1;
        if (throwOnReady_) {
            throw std::runtime_error(name_ + ".ready failure");
        }
    }

    [[nodiscard]] std::string name() const override
    {
        return name_;
    }

    [[nodiscard]] int startCount() const { return startCount_.load(); }
    [[nodiscard]] int stopCount() const { return stopCount_.load(); }
    [[nodiscard]] int readyTrueCount() const { return readyTrueCount_.load(); }
    [[nodiscard]] int readyFalseCount() const { return readyFalseCount_.load(); }

private:
    std::string name_;
    std::vector<std::string>& events_;
    bool startResult_{true};
    bool throwOnReady_{false};
    bool throwOnStop_{false};
    std::atomic<int> startCount_{0};
    std::atomic<int> stopCount_{0};
    std::atomic<int> readyTrueCount_{0};
    std::atomic<int> readyFalseCount_{0};
};

class TestService final : public ServiceBase {
public:
    explicit TestService(bool initializeResult = true)
        : ServiceBase("test-service")
        , initializeResult_(initializeResult)
    {
    }

    bool initializeComponents() override
    {
        ++initializeCalls_;
        return initializeResult_;
    }

    void validateConfiguration() override
    {
        ++validateCalls_;
    }

    [[nodiscard]] int initializeCalls() const { return initializeCalls_; }
    [[nodiscard]] int validateCalls() const { return validateCalls_; }

private:
    bool initializeResult_{true};
    int initializeCalls_{0};
    int validateCalls_{0};
};

void testStartStopOrderAndReadyTransitions()
{
    std::vector<std::string> events;
    auto first = std::make_shared<RecordingTransport>("first", events);
    auto second = std::make_shared<RecordingTransport>("second", events);

    TestService service;
    service.addTransport(first);
    service.addTransport(second);

    expect(service.start(), "ServiceBase should start successfully");
    service.setReady(true);
    service.setReady(true);
    service.stop();
    service.stop();

    const std::vector<std::string> expected = {
        "first.start",
        "second.start",
        "first.ready.true",
        "second.ready.true",
        "first.ready.false",
        "second.ready.false",
        "second.stop",
        "first.stop",
    };
    expect(events == expected, "ServiceBase should start in order, publish readiness once per transition, and stop in reverse order");
    expect(first->stopCount() == 1 && second->stopCount() == 1, "ServiceBase::stop must be idempotent");
}

void testRollbackStopsStartedTransportsExactlyOnce()
{
    std::vector<std::string> events;
    auto first = std::make_shared<RecordingTransport>("first", events, true);
    auto second = std::make_shared<RecordingTransport>("second", events, false);
    auto third = std::make_shared<RecordingTransport>("third", events, true);

    TestService service;
    service.addTransport(first);
    service.addTransport(second);
    service.addTransport(third);

    expect(!service.start(), "ServiceBase should report failed transport start");
    expect(!service.isRunning(), "ServiceBase should not be running after rollback");
    expect(first->startCount() == 1, "first transport should start once");
    expect(first->stopCount() == 1, "first transport should be rolled back exactly once");
    expect(second->startCount() == 1, "second transport should attempt start once");
    expect(second->stopCount() == 1, "failing transport should be rolled back exactly once");
    expect(third->startCount() == 0, "later transports should not be started after failure");
}

void testReadyPublicationFailureDoesNotBlockLaterTransports()
{
    std::vector<std::string> events;
    auto first = std::make_shared<RecordingTransport>("first", events, true, true);
    auto second = std::make_shared<RecordingTransport>("second", events);

    TestService service;
    service.addTransport(first);
    service.addTransport(second);

    expect(service.start(), "ServiceBase should start successfully");
    service.setReady(true);
    service.stop();

    expect(first->readyTrueCount() == 1, "first transport should still see ready=true once");
    expect(second->readyTrueCount() == 1, "later transports must still see ready=true after earlier failure");
    expect(second->readyFalseCount() == 1, "later transports must still see ready=false after earlier failure");
}

void testStopFailureDoesNotBlockRemainingTransports()
{
    std::vector<std::string> events;
    auto first = std::make_shared<RecordingTransport>("first", events);
    auto second = std::make_shared<RecordingTransport>("second", events, true, false, true);

    TestService service;
    service.addTransport(first);
    service.addTransport(second);

    expect(service.start(), "ServiceBase should start successfully");
    service.stop();

    expect(second->stopCount() == 1, "failing stop transport should be attempted once");
    expect(first->stopCount() == 1, "later reverse-order stop should continue after earlier failure");
}

} // namespace

int main()
{
    testStartStopOrderAndReadyTransitions();
    testRollbackStopsStartedTransportsExactlyOnce();
    testReadyPublicationFailureDoesNotBlockLaterTransports();
    testStopFailureDoesNotBlockRemainingTransports();
    return EXIT_SUCCESS;
}
