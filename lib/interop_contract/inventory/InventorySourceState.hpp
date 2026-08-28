#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace interop_contract::inventory {

enum class SourceHealth {
    OK,
    DEGRADED,
    FAILED
};

struct SourceState {
    std::string name;
    bool required{false};
    bool stale{false};
    SourceHealth health{SourceHealth::FAILED};

    int64_t lastAttemptTs{0};
    int64_t lastSuccessTs{0};

    std::optional<std::string> lastError;
    std::optional<std::string> origin;
};

using SourceStateMap = std::map<std::string, SourceState>;

} // namespace interop_contract::inventory