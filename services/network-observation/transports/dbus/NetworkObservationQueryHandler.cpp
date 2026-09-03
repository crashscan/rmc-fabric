#include "NetworkObservationQueryHandler.h"

#include "NetworkObservationDbusCodec.h"

#include <CandidateTypes.h>
#include <IObservationQueryService.h>
#include <LocalStateTypes.h>
#include <ServiceBinding.h>

#include <network_observation/NetworkObservationContracts.hpp>
#include <network_observation/NetworkObservationTypes.hpp>

#include <glog/logging.h>

#include <exception>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace RSCGroup {
namespace {

namespace contract = interop_contract::network_observation;

[[nodiscard]] contract::LocalNetworkSnapshot toWireSnapshot(const LocalNetworkSnapshot& snapshot)
{
    contract::LocalNetworkSnapshot wire;
    wire.interfaces = snapshot.interfaces;
    return wire;
}

/**
 * Convert the model's extended candidate type to its wire representation.
 *
 * Model-only evidence and timestamp fields are intentionally omitted.
 */
[[nodiscard]] contract::RemoteCandidate toWireCandidate(const RemoteCandidate& candidate)
{
    contract::RemoteCandidate wire;

    wire.mac = candidate.mac;
    wire.classification = candidate.classification;
    wire.status = candidate.status;
    wire.seenInFdb = candidate.seenInFdb;
    wire.seenInNeigh = candidate.seenInNeigh;
    wire.seenInLldp = candidate.seenInLldp;
    wire.bridgePort = candidate.bridgePort;
    wire.remoteChassisId = candidate.remoteChassisId;
    wire.remotePortId = candidate.remotePortId;
    wire.remoteSystemName = candidate.remoteSystemName;
    wire.neighborIfaces = candidate.neighborIfaces;
    wire.ipv4 = candidate.ipv4;
    wire.ipv6 = candidate.ipv6;

    return wire;
}

/**
 * Acquire one query lease and invoke a service/encoding operation.
 *
 * The lease remains active through result encoding, ensuring that query
 * quiescence cannot destroy service-owned data while it is being converted to
 * its D-Bus representation.
 */
template<typename Result, typename Function>
[[nodiscard]] Result invokeQuery(
    ServiceBinding<IObservationQueryService>& binding,
    const char* operation,
    Result fallback,
    Function&& function)
{
    auto guard = binding.acquire();
    if (!guard) {
        return fallback;
    }

    try {
        return std::invoke(std::forward<Function>(function),*guard.get());
    } catch (const std::exception& error) {
        LOG(ERROR) << operation << " failed: " << error.what();
    } catch (...) {
        LOG(ERROR) << operation << " failed: unknown exception";
    }

    return fallback;
}

} // namespace

NetworkObservationQueryHandler::NetworkObservationQueryHandler(
    ServiceBinding<IObservationQueryService>& binding) noexcept
    : binding_(binding)
{
}

std::map<std::string, DBus::Variant> NetworkObservationQueryHandler::getLocalSnapshot()
{
    using Result = std::map<std::string, DBus::Variant>;
    return invokeQuery(binding_, "GetLocalSnapshot", Result{}, [](IObservationQueryService& service) {
        return NetworkObservationDbusCodec::encodeLocalSnapshot(toWireSnapshot(service.localSnapshot()));
    });
}

std::map<std::string, DBus::Variant>
NetworkObservationQueryHandler::getInterface(std::string ifname)
{
    using Result = std::map<std::string, DBus::Variant>;

    return invokeQuery(
        binding_,
        "GetInterface",
        Result{},
        [&ifname](IObservationQueryService& service) {
            const auto interface = service.getInterface(ifname);
            if (!interface) {
                return Result{};
            }

            return NetworkObservationDbusCodec::toVariantMap(
                *interface);
        });
}

std::vector<std::string> NetworkObservationQueryHandler::getRemoteCandidateMacs()
{
    using Result = std::vector<std::string>;
    return invokeQuery(binding_, "GetRemoteCandidateMacs", Result{}, [](IObservationQueryService& service) {
        const auto candidates = service.remoteCandidates();
        Result macs;
        macs.reserve(candidates.size());
        for (const auto& candidate : candidates) macs.push_back(candidate.mac);
        return NetworkObservationDbusCodec::encodeCandidateMacs(macs);
    });
}

std::map<std::string, DBus::Variant>
NetworkObservationQueryHandler::getCandidateByMac(std::string mac)
{
    using Result = std::map<std::string, DBus::Variant>;

    return invokeQuery(
        binding_,
        "GetCandidateByMac",
        Result{},
        [&mac](IObservationQueryService& service) {
            const auto candidate =
                service.getCandidateByMac(mac);

            if (!candidate) {
                return Result{};
            }

            return NetworkObservationDbusCodec::toVariantMap(
                toWireCandidate(*candidate));
        });
}

std::map<std::string, std::map<std::string, DBus::Variant>>
NetworkObservationQueryHandler::getIssues()
{
    using Result =
        std::map<std::string,
                 std::map<std::string, DBus::Variant>>;

    return invokeQuery(
        binding_,
        "GetIssues",
        Result{},
        [](IObservationQueryService& service) {
            return NetworkObservationDbusCodec::encodeIssues(
                service.getIssues());
        });
}

bool NetworkObservationQueryHandler::getReady()
{
    return invokeQuery(
        binding_,
        "GetReady",
        false,
        [](IObservationQueryService& service) {
            return service.isReady();
        });
}

std::string NetworkObservationQueryHandler::getPhase()
{
    return invokeQuery(
        binding_,
        "GetPhase",
        std::string{contract::PHASE_STOPPED},
        [](IObservationQueryService& service) {
            return service.getPhase();
        });
}

} // namespace RSCGroup