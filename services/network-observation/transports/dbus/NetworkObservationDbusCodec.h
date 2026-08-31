#pragma once

#include <interop_contract/network_observation/NetworkObservationContracts.hpp>
#include <interop_contract/network_observation/NetworkObservationTypes.hpp>

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

[[nodiscard]] interop_contract::network_observation::LocalInterfaceState
fromVariantMapIface(const std::map<std::string, DBus::Variant>& m);

[[nodiscard]] interop_contract::network_observation::RemoteCandidate
fromVariantMapCandidate(const std::map<std::string, DBus::Variant>& m);

} // namespace RSCGroup::NetworkObservationDbusCodec
