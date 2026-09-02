//
// Created by vvass on 02-Sep-26.
//

#include "InventoryQueryHandler.h"

#include <glog/logging.h>

#include "InventoryDbusCodec.h"

namespace RSCGroup {
using namespace interop_contract::inventory;

std::map<std::string, DBus::Variant> InventoryQueryHandler::GetIdentity() {
    if (auto guard = binding->acquire()) {
        try { return InventoryDbusCodec::encodeSnapshot(guard->getIdentity()); } catch (const std::exception &e) {
            LOG(ERROR) << "GetIdentity failed: " << e.what();
        }
    }
    return {};
}

std::map<std::string, DBus::Variant> InventoryQueryHandler::GetField(std::string fieldName) {
    if (auto guard = binding->acquire()) {
        try { return InventoryDbusCodec::encodeFields(guard->getField(fieldName)); } catch (const std::exception &e) {
            LOG(ERROR) << "GetField failed: " << e.what();
        }
    }
    return {};
}

std::map<std::string, std::map<std::string, DBus::Variant> > InventoryQueryHandler::GetSourceStates() {
    if (auto guard = binding->acquire()) {
        try { return InventoryDbusCodec::encodeSourceStates(guard->getSourceStates()); } catch (const std::exception &
            e) { LOG(ERROR) << "GetSourceStates failed: " << e.what(); }
    }
    return {};
}

std::map<std::string, std::map<std::string, DBus::Variant> > InventoryQueryHandler::GetIssues() {
    if (auto guard = binding->acquire()) {
        try { return InventoryDbusCodec::encodeIssues(guard->getIssues()); } catch (const std::exception &e) {
            LOG(ERROR) << "GetIssues failed: " << e.what();
        }
    }
    return {};
}

bool InventoryQueryHandler::GetReady() {
    if (auto guard = binding->acquire()) return guard->getReady();
    return false;
}

std::string InventoryQueryHandler::GetPhase() {
    if (auto guard = binding->acquire()) return guard->getPhase();
    return "unknown";
}

uint64_t InventoryQueryHandler::GetVersion() {
    if (auto guard = binding->acquire()) return guard->getVersion();
    return 0;
}

void InventoryQueryHandler::Refresh() {
    if (auto guard = binding->acquire()) guard->refresh();
}
} // namespace RSCGroup
