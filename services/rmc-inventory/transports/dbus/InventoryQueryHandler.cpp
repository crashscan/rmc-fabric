//
// Created by vvass on 02-Sep-26.
//

#include "InventoryQueryHandler.h"

#include "InventoryDbusCodec.h"

#include <IInventoryQueryService.h>
#include <ServiceBinding.h>

#include <dbus-cxx.h>
#include <glog/logging.h>

#include <exception>
#include <functional>
#include <string>
#include <utility>

namespace RSCGroup {
namespace {

template<typename Result, typename Function>
[[nodiscard]] Result invokeQuery(ServiceBinding<IInventoryQueryService>& binding,const char* operation,Result fallback,Function&& function)
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

template<typename Function>
void invokeCommand(ServiceBinding<IInventoryQueryService>& binding,const char* operation,Function&& function)
{
    auto guard = binding.acquire();
    if (!guard) {
        return;
    }

    try {
        std::invoke(std::forward<Function>(function),*guard.get());
    } catch (const std::exception& error) {
        LOG(ERROR) << operation << " failed: " << error.what();
    } catch (...) {
        LOG(ERROR) << operation << " failed: unknown exception";
    }
}

} // namespace

InventoryQueryHandler::InventoryQueryHandler(
    ServiceBinding<IInventoryQueryService>& binding) noexcept
    : binding_(binding)
{
}

std::map<std::string, DBus::Variant>
InventoryQueryHandler::getIdentity()
{
    using Result = std::map<std::string, DBus::Variant>;

    return invokeQuery(
        binding_,
        "GetIdentity",
        Result{},
        [](IInventoryQueryService& service) {
            return InventoryDbusCodec::encodeSnapshot(
                service.getIdentity());
        });
}

std::map<std::string, DBus::Variant>
InventoryQueryHandler::getField(std::string fieldName)
{
    using Result = std::map<std::string, DBus::Variant>;

    return invokeQuery(
        binding_,
        "GetField",
        Result{},
        [&fieldName](IInventoryQueryService& service) {
            return InventoryDbusCodec::encodeFields(
                service.getField(fieldName));
        });
}

std::map<std::string, std::map<std::string, DBus::Variant>>
InventoryQueryHandler::getSourceStates()
{
    using Result =
        std::map<std::string, std::map<std::string, DBus::Variant>>;

    return invokeQuery(
        binding_,
        "GetSourceStates",
        Result{},
        [](IInventoryQueryService& service) {
            return InventoryDbusCodec::encodeSourceStates(
                service.getSourceStates());
        });
}

std::map<std::string, std::map<std::string, DBus::Variant>>
InventoryQueryHandler::getIssues()
{
    using Result =
        std::map<std::string, std::map<std::string, DBus::Variant>>;

    return invokeQuery(
        binding_,
        "GetIssues",
        Result{},
        [](IInventoryQueryService& service) {
            return InventoryDbusCodec::encodeIssues(
                service.getIssues());
        });
}

bool InventoryQueryHandler::getReady()
{
    return invokeQuery(
        binding_,
        "GetReady",
        false,
        [](IInventoryQueryService& service) {
            return service.getReady();
        });
}

std::string InventoryQueryHandler::getPhase()
{
    return invokeQuery(
        binding_,
        "GetPhase",
        std::string{"unknown"},
        [](IInventoryQueryService& service) {
            return service.getPhase();
        });
}

std::uint64_t InventoryQueryHandler::getVersion()
{
    return invokeQuery(
        binding_,
        "GetVersion",
        std::uint64_t{0},
        [](IInventoryQueryService& service) {
            return service.getVersion();
        });
}

void InventoryQueryHandler::refresh()
{
    invokeCommand(
        binding_,
        "Refresh",
        [](IInventoryQueryService& service) {
            service.refresh();
        });
}

} // namespace RSCGroup
