#pragma once

#include <network_observation/NetworkObservationContracts.hpp>
#include <network_observation/NetworkObservationTypes.hpp>

#include <map>
#include <string>
#include <vector>

namespace DBus {
class Variant;
}

namespace RSCGroup::NetworkObservationDbusCodec {

using VariantMap = std::map<std::string, DBus::Variant>;
using NestedVariantMap = std::map<std::string, VariantMap>;

[[nodiscard]] VariantMap toVariantMap(const interop_contract::network_observation::LocalInterfaceState& iface);
[[nodiscard]] VariantMap toVariantMap(const interop_contract::network_observation::RemoteCandidate& c);
[[nodiscard]] NestedVariantMap encodeIssues(const interop_contract::network_observation::ObservationIssues& issues);
[[nodiscard]] interop_contract::network_observation::LocalNetworkSnapshot fromVariantMapLocalSnapshot(const VariantMap& m);
[[nodiscard]] interop_contract::network_observation::LocalInterfaceState fromVariantMapIface(const VariantMap& m);
[[nodiscard]] interop_contract::network_observation::RemoteCandidate fromVariantMapCandidate(const VariantMap& m);
[[nodiscard]] interop_contract::network_observation::ObservationIssues decodeIssues(const NestedVariantMap& issues);
[[nodiscard]] VariantMap encodeLocalSnapshot(const interop_contract::network_observation::LocalNetworkSnapshot& snapshot);
[[nodiscard]] std::vector<std::string> encodeCandidateMacs(const std::vector<std::string>& candidates);

} // namespace RSCGroup::NetworkObservationDbusCodec
