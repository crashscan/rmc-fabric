#include <DefaultInventoryManager.h>
#include <IInventorySource.h>
#include <InventoryDbusCodec.h>
#include <NetworkObservationDbusCodec.h>
#include <ObservationService.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
namespace inventory = interop_contract::inventory;
namespace observation = interop_contract::network_observation;

class StaticInventorySource final : public RSCGroup::IInventorySource {
public:
    StaticInventorySource(std::string name, std::string fieldBase, int fieldCount)
        : name_(std::move(name))
    {
        for (int index = 0; index < fieldCount; ++index) {
            ownedFields_.push_back(fieldBase + std::to_string(index));
        }
    }

    std::string getName() const override { return name_; }
    bool isRequired() const override { return true; }
    inventory::FieldNameList getOwnedFields() const override { return ownedFields_; }
    inventory::InventoryFields collect() override
    {
        inventory::InventoryFields fields;
        for (std::size_t index = 0; index < ownedFields_.size(); ++index) {
            fields.emplace(ownedFields_[index], std::string("value-") + std::to_string(generation_) + "-" + std::to_string(index));
        }
        ++generation_;
        return fields;
    }
    inventory::SourceState getState() const override
    {
        inventory::SourceState state;
        state.name = name_;
        state.required = true;
        state.health = inventory::SourceHealth::OK;
        return state;
    }

private:
    std::string name_;
    inventory::FieldNameList ownedFields_;
    int generation_{0};
};

class NullObservationRuntime final : public RSCGroup::IObservationRuntime {
public:
    void setEventSink(RSCGroup::IModelEventSink*) override {}
    void setInterfacePolicy(std::unique_ptr<RSCGroup::IInterfacePolicy>) override {}
    void setClassifier(std::unique_ptr<RSCGroup::ICandidateClassifier>) override {}
    bool start() override { running_ = true; return true; }
    void stop() override { running_ = false; }
    bool isRunning() const override { return running_; }
    RSCGroup::LocalNetworkSnapshot localSnapshot() const override { return {}; }
    std::vector<RSCGroup::RemoteCandidate> remoteCandidates() const override { return {}; }
    std::optional<RSCGroup::RemoteCandidate> findCandidateByMac(const std::string&) const override { return std::nullopt; }
    void age(std::chrono::steady_clock::time_point) override {}

private:
    bool running_{false};
};

class CountingTransport final : public RSCGroup::IObservationTransport {
public:
    void bindQueryService(RSCGroup::IObservationQueryService&) override {}
    bool start() override { return true; }
    void stop() override {}
    std::string name() const override { return "counting"; }
    void publishReadyChanged(bool) override {}
    void publishLocalStateChanged() override { ++localSignals_; }
    void publishInterfaceChanged(const std::string&) override { ++interfaceSignals_; }
    void publishInterfaceRemoved(const std::string&) override { ++interfaceSignals_; }
    void publishCandidateChanged(const std::string&) override { ++candidateSignals_; }
    void publishCandidateRemoved(const std::string&) override { ++candidateSignals_; }

    int localSignals_{0};
    int interfaceSignals_{0};
    int candidateSignals_{0};
};

template <typename Fn>
double measureMilliseconds(int iterations, Fn&& fn)
{
    const auto start = Clock::now();
    for (int index = 0; index < iterations; ++index) {
        fn();
    }
    const auto end = Clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

void printResult(const std::string& name, int iterations, double milliseconds)
{
    std::cout << "{\"benchmark\":\"" << name
              << "\",\"iterations\":" << iterations
              << ",\"elapsed_ms\":" << milliseconds << "}\n";
}

void benchmarkInventoryManagerRefresh()
{
    RSCGroup::DefaultInventoryManager manager;
    for (int source = 0; source < 8; ++source) {
        manager.addSource(std::make_shared<StaticInventorySource>(
            "source-" + std::to_string(source),
            "field" + std::to_string(source) + ".",
            16));
    }
    const int iterations = 200;
    printResult("inventory_manager_refresh", iterations, measureMilliseconds(iterations, [&] {
        (void)manager.refreshAll();
    }));
}

void benchmarkInventoryCodec()
{
    inventory::InventorySnapshot snapshot;
    snapshot.version = 42;
    snapshot.timestamp = 1725102000;
    snapshot.ready = true;
    snapshot.phase = "live";
    for (int index = 0; index < 32; ++index) {
        snapshot.fields.emplace("field" + std::to_string(index),
                                std::string("value-") + std::to_string(index));
    }
    const auto encoded = RSCGroup::InventoryDbusCodec::encodeSnapshot(snapshot);

    const int iterations = 5000;
    printResult("inventory_codec_encode_snapshot", iterations, measureMilliseconds(iterations, [&] {
        (void)RSCGroup::InventoryDbusCodec::encodeSnapshot(snapshot);
    }));
    printResult("inventory_codec_decode_snapshot", iterations, measureMilliseconds(iterations, [&] {
        (void)RSCGroup::InventoryDbusCodec::decodeSnapshot(encoded);
    }));
}

void benchmarkNetworkCodec()
{
    observation::RemoteCandidate candidate;
    candidate.mac = "00:11:22:33:44:55";
    candidate.classification = observation::CandidateClassification::RemoteEndpoint;
    candidate.status = observation::CandidateStatus::Confirmed;
    candidate.seenInFdb = true;
    candidate.seenInNeigh = true;
    candidate.seenInLldp = true;
    candidate.neighborIfaces = {"eth0", "eth1", "eth2"};
    candidate.ipv4 = {"10.0.0.2/24", "10.0.0.3/24"};
    candidate.ipv6 = {"fe80::2/64"};
    const auto encoded = RSCGroup::NetworkObservationDbusCodec::toVariantMap(candidate);

    const int iterations = 5000;
    printResult("network_codec_encode_candidate", iterations, measureMilliseconds(iterations, [&] {
        (void)RSCGroup::NetworkObservationDbusCodec::toVariantMap(candidate);
    }));
    printResult("network_codec_decode_candidate", iterations, measureMilliseconds(iterations, [&] {
        (void)RSCGroup::NetworkObservationDbusCodec::fromVariantMapCandidate(encoded);
    }));
}

void benchmarkObservationFanout()
{
    auto primaryTransport = std::make_shared<CountingTransport>();
    RSCGroup::ObservationService service(std::make_unique<NullObservationRuntime>(),
                                         primaryTransport,
                                         std::chrono::hours(1));
    std::vector<std::shared_ptr<CountingTransport>> extra;
    for (int index = 0; index < 7; ++index) {
        auto transport = std::make_shared<CountingTransport>();
        service.addTransport(transport);
        extra.push_back(std::move(transport));
    }
    if (!service.start()) {
        throw std::runtime_error("benchmark observation service failed to start");
    }

    RSCGroup::ModelEvent interfaceEvent;
    interfaceEvent.kind = RSCGroup::ModelEventKind::LocalInterfaceChanged;
    interfaceEvent.ifname = "eth0";

    RSCGroup::ModelEvent candidateEvent;
    candidateEvent.kind = RSCGroup::ModelEventKind::CandidateUpdated;
    candidateEvent.mac = "00:11:22:33:44:55";

    const int iterations = 5000;
    printResult("observation_event_fanout_interface", iterations, measureMilliseconds(iterations, [&] {
        service.onModelEvent(interfaceEvent);
    }));
    printResult("observation_event_fanout_candidate", iterations, measureMilliseconds(iterations, [&] {
        service.onModelEvent(candidateEvent);
    }));
    service.stop();
}

void benchmarkServiceStartStop()
{
    const int iterations = 200;
    printResult("observation_service_start_stop", iterations, measureMilliseconds(iterations, [&] {
        auto transport = std::make_shared<CountingTransport>();
        RSCGroup::ObservationService service(std::make_unique<NullObservationRuntime>(),
                                             std::move(transport),
                                             std::chrono::hours(1));
        if (!service.start()) {
            throw std::runtime_error("service start failed");
        }
        service.stop();
    }));
}

} // namespace

int main()
{
    try {
        benchmarkInventoryManagerRefresh();
        benchmarkInventoryCodec();
        benchmarkNetworkCodec();
        benchmarkObservationFanout();
        benchmarkServiceStartStop();
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
