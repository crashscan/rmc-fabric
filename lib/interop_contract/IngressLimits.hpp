#pragma once

#include <cstddef>

namespace interop_contract::ingress {

inline constexpr std::size_t kMaxKeyLength = 255;
inline constexpr std::size_t kMaxStringLength = 4096;

namespace inventory {
inline constexpr std::size_t kMaxFields = 256;
inline constexpr std::size_t kMaxSources = 128;
inline constexpr std::size_t kMaxIssues = 128;
inline constexpr std::size_t kMaxIssueFields = 16;
} // namespace inventory

namespace network_observation {
inline constexpr std::size_t kMaxInterfaces = 256;
inline constexpr std::size_t kMaxCandidates = 1024;
inline constexpr std::size_t kMaxStringSetEntries = 128;
inline constexpr std::size_t kMaxIssues = 128;
inline constexpr std::size_t kMaxIssueFields = 16;
} // namespace network_observation

} // namespace interop_contract::ingress
