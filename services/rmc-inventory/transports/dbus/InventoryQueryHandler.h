//
// Created by vvass on 02-Sep-26.
//
#pragma once

#include <dbus-cxx.h>
#include "IInventoryQueryService.h"
#include "ServiceBinding.h"

namespace RSCGroup {

class InventoryQueryHandler {
public:
    /// Thread-safe service access: shared lock held during the call, exclusive
    /// lock taken by onTransportStopping() to clear the binding.
    ServiceBinding<IInventoryQueryService>* binding = nullptr;
    std::map<std::string, DBus::Variant> GetIdentity();
    std::map<std::string, DBus::Variant> GetField(std::string fieldName);
    std::map<std::string, std::map<std::string, DBus::Variant>> GetSourceStates();
    std::map<std::string, std::map<std::string, DBus::Variant>> GetIssues();
    bool GetReady();
    std::string GetPhase();
    uint64_t GetVersion();
    void Refresh();
};

} // namespace RSCGroup