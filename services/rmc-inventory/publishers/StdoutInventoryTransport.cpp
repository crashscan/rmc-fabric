#include "StdoutInventoryTransport.h"

#include <inventory_service_api/IInventoryQueryService.h>

#include <iostream>
#include <type_traits>
#include <variant>

namespace RSCGroup {
namespace {

std::string fieldValueToString(const interop_contract::inventory::FieldValue& v)
{
    return std::visit([](const auto& x) -> std::string {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, bool>) {
            return x ? "true" : "false";
        } else if constexpr (std::is_same_v<T, std::string>) {
            return x;
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return std::to_string(x);
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            return std::to_string(x);
        } else {
            static_assert(!sizeof(T*), "Unhandled FieldValue alternative");
        }
        return {};
    }, v);
}

} // namespace

void StdoutInventoryTransport::start(IInventoryQueryService& queryService)
{
    query_ = &queryService;
    running_.store(true, std::memory_order_release);
    std::cout << "[transport] StdoutInventoryTransport started" << std::endl;
}

void StdoutInventoryTransport::stop()
{
    running_.store(false, std::memory_order_release);
    query_ = nullptr;
    std::cout << "[transport] StdoutInventoryTransport stopped" << std::endl;
}

void StdoutInventoryTransport::publishInventoryChanged(const std::string& fieldPath)
{
    if (!running_.load(std::memory_order_acquire)) return;
    std::cout << "[event] InventoryChanged: " << fieldPath;
    printField(fieldPath);
    std::cout << std::endl;
}

void StdoutInventoryTransport::publishSourceStateChanged(const std::string& sourceName)
{
    if (!running_.load(std::memory_order_acquire)) return;
    std::cout << "[event] SourceStateChanged: " << sourceName << std::endl;
}

void StdoutInventoryTransport::publishReadyChanged(bool ready)
{
    if (!running_.load(std::memory_order_acquire)) return;
    std::cout << "[event] ReadyChanged: " << (ready ? "true" : "false") << std::endl;
}

void StdoutInventoryTransport::printField(const std::string& fieldPath) const
{
    if (!query_) return;
    const auto fields = query_->getField(fieldPath);
    if (fields.empty()) {
        std::cout << " = <absent>";
        return;
    }
    std::cout << " = " << fieldValueToString(fields.begin()->second);
}

} // namespace RSCGroup
