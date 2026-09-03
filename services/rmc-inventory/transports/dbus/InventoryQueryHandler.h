//
// Created by vvass on 02-Sep-26.
//
#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace DBus {
    class Variant;
}

namespace RSCGroup {

class IInventoryQueryService;

template<typename T>
class ServiceBinding;

/**
 * D-Bus query-method boundary for the inventory service.
 *
 * The handler does not own the service. Each method acquires a short-lived
 * ServiceBinding lease covering the complete service call and result encoding.
 * Query quiescence closes admission and waits for all such leases to drain.
 *
 * The binding is mandatory and must outlive this handler. InventoryDbusAdapter
 * guarantees that lifetime by declaring binding_ before handler_, causing the
 * handler to be destroyed first.
 *
 * Exceptions are contained at this boundary and never cross into dbus-cxx.
 */
class InventoryQueryHandler final {
public:
    explicit InventoryQueryHandler(ServiceBinding<IInventoryQueryService>& binding) noexcept;

    InventoryQueryHandler(const InventoryQueryHandler&) = delete;
    InventoryQueryHandler& operator=(const InventoryQueryHandler&) = delete;

    [[nodiscard]] std::map<std::string, DBus::Variant> getIdentity();
    [[nodiscard]] std::map<std::string, DBus::Variant> getField(std::string fieldName);

    [[nodiscard]] std::map<std::string, std::map<std::string, DBus::Variant>> getSourceStates();
    [[nodiscard]] std::map<std::string, std::map<std::string, DBus::Variant>> getIssues();

    [[nodiscard]] bool getReady();
    [[nodiscard]] std::string getPhase();
    [[nodiscard]] std::uint64_t getVersion();

    void refresh();

private:
    ServiceBinding<IInventoryQueryService>& binding_;
};

} // namespace RSCGroup