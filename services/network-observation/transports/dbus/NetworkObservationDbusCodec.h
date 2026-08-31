#pragma once

#include <network_observation/NetworkObservationContracts.hpp>
#include <network_observation/NetworkObservationTypes.hpp>

#include <map>
#include <string>

namespace DBus {
class Variant;
}

namespace RSCGroup::NetworkObservationDbusCodec {

[[nodiscard]] std::map<std::string, DBus::Variant>
toVariantMap(const interop_contract::network_observation::LocalInterfaceState& iface);

[[nodiscard]] std::map<std::string, DBus::Variant>
toVariantMap(const interop_contract::network_observation::RemoteCandidate& c);

[[nodiscard]] std::map<std::string, std::map<std::string, DBus::Variant>>
encodeIssues(const interop_contract::network_observation::ObservationIssues& issues);

[[nodiscard]] interop_contract::network_observation::LocalNetworkSnapshot
fromVariantMapLocalSnapshot(const std::map<std::string, DBus::Variant>& m);

[[nodiscard]] interop_contract::network_observation::LocalInterfaceState
fromVariantMapIface(const std::map<std::string, DBus::Variant>& m);

[[nodiscard]] interop_contract::network_observation::RemoteCandidate
fromVariantMapCandidate(const std::map<std::string, DBus::Variant>& m);

[[nodiscard]] interop_contract::network_observation::ObservationIssues
decodeIssues(const std::map<std::string, std::map<std::string, DBus::Variant>>& issues);

} // namespace RSCGroup::NetworkObservationDbusCodec
