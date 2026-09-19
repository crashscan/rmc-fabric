//
// Created by vvass on 19-Sep-26.
//
#pragma once
#include <optional>
#include <string>

namespace RSCGroup {
/// Cached neighbour identity. Defined here so the factory can build both
/// Present and Removed observations from a cache entry without the cache
/// type leaking into every call site.
struct CachedLldpNeighbor {
    std::optional<std::string> rawChassisId;
    std::optional<std::string> rawPortId;
    std::optional<std::string> rawSystemName;
};
} // namespace RSCGroup