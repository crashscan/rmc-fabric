//
// Created by vvass on 03-Sep-26.
//
#pragma once

#include <dbus-cxx/variant.h>

#include <map>
#include <string>
#include <vector>

namespace RSCGroup {

class IObservationQueryService;

template<typename T>
class ServiceBinding;

/**
 * D-Bus query boundary for the network-observation service.
 *
 * Each method acquires one ServiceBinding lease covering the complete service
 * call and wire encoding operation. Query quiescence closes admission and
 * waits for all such leases to be released.
 *
 * The binding is mandatory and must outlive this handler. The owning adapter
 * enforces that relationship through member declaration order.
 *
 * All service and encoding exceptions are contained here and converted to the
 * method's existing wire-compatible fallback value.
 */
class NetworkObservationQueryHandler final {
public:
    explicit NetworkObservationQueryHandler(ServiceBinding<IObservationQueryService>& binding) noexcept;
    NetworkObservationQueryHandler(const NetworkObservationQueryHandler&) = delete;
    NetworkObservationQueryHandler& operator=(const NetworkObservationQueryHandler&) = delete;

    [[nodiscard]] std::map<std::string, DBus::Variant> getLocalSnapshot();
    [[nodiscard]] std::map<std::string, DBus::Variant> getInterface(std::string ifname);
    [[nodiscard]] std::vector<std::string> getRemoteCandidateMacs();
    [[nodiscard]] std::map<std::string, DBus::Variant> getCandidateByMac(std::string mac);
    [[nodiscard]] std::map<std::string, std::map<std::string, DBus::Variant>> getIssues();

    [[nodiscard]] bool getReady();
    [[nodiscard]] std::string getPhase();

private:
    ServiceBinding<IObservationQueryService>& binding_;
};

} // namespace RSCGroup