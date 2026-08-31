#include <ObservationService.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
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

std::size_t fdCount()
{
    return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator("/proc/self/fd"),
                                                  std::filesystem::directory_iterator()));
}

class NullObservationRuntime final : public IObservationRuntime {
public:
    void setEventSink(IModelEventSink*) override {}
    void setInterfacePolicy(std::unique_ptr<IInterfacePolicy>) override {}
    void setClassifier(std::unique_ptr<ICandidateClassifier>) override {}
    [[nodiscard]] bool start() override
    {
        running_ = true;
        return true;
    }
    void stop() override { running_ = false; }
    [[nodiscard]] bool isRunning() const override { return running_; }
    [[nodiscard]] ObservationRuntimeHealth health() const override
    {
        return ObservationRuntimeHealth{running_, true};
    }
    [[nodiscard]] LocalNetworkSnapshot localSnapshot() const override { return {}; }
    [[nodiscard]] std::vector<RemoteCandidate> remoteCandidates() const override { return {}; }
    [[nodiscard]] std::optional<RemoteCandidate> findCandidateByMac(const std::string&) const override
    {
        return std::nullopt;
    }
    void age(std::chrono::steady_clock::time_point) override {}

private:
    bool running_{false};
};

class NullObservationTransport final : public IObservationTransport {
public:
    void bindQueryService(IObservationQueryService&) override {}
    [[nodiscard]] bool start() override { return true; }
    void stop() override {}
    [[nodiscard]] std::string name() const override { return "null"; }
    void publishLocalStateChanged() override {}
    void publishInterfaceChanged(const std::string&) override {}
    void publishInterfaceRemoved(const std::string&) override {}
    void publishCandidateChanged(const std::string&) override {}
    void publishCandidateRemoved(const std::string&) override {}
};

void testObservationServiceShortSoak(int cycles)
{
    const auto startFds = fdCount();
    const auto start = std::chrono::steady_clock::now();

    for (int cycle = 0; cycle < cycles; ++cycle) {
        auto transport = std::make_shared<NullObservationTransport>();
        ObservationService service(std::make_unique<NullObservationRuntime>(),
                                   transport,
                                   std::chrono::hours(1));
        expect(service.start(), "observation service short-soak start failed");
        expect(service.isReady(), "observation service short-soak should become ready");
        service.stop();
        expect(service.getIssues().empty(), "observation service short-soak should reset issues on stop");
    }

    const auto end = std::chrono::steady_clock::now();
    const auto endFds = fdCount();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    const auto fdGrowth = static_cast<long long>(endFds) - static_cast<long long>(startFds);

    std::cout << "{\"test\":\"service_soak_short\",\"cycles\":" << cycles
              << ",\"elapsed_ms\":" << elapsedMs
              << ",\"fd_start\":" << startFds
              << ",\"fd_end\":" << endFds
              << ",\"fd_growth\":" << fdGrowth
              << "}\n";

    expect(fdGrowth <= 2, "observation service short-soak leaked file descriptors");
}

} // namespace

int main()
{
    int cycles = 200;
    if (const char* cyclesEnv = std::getenv("RMC_FABRIC_SOAK_CYCLES")) {
        cycles = std::atoi(cyclesEnv);
        expect(cycles > 0, "RMC_FABRIC_SOAK_CYCLES must be positive");
    }
    testObservationServiceShortSoak(cycles);
    return EXIT_SUCCESS;
}
