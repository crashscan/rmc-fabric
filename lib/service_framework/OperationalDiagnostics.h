#pragma once

#include <glog/logging.h>

#include <algorithm>
#include <string>
#include <string_view>

namespace RSCGroup::diagnostics {

inline constexpr std::size_t kMaxDiagnosticFieldLength = 64;
inline constexpr std::size_t kMaxDiagnosticMessageLength = 160;

inline std::string sanitizeField(std::string_view value,
                                 std::size_t maxLength = kMaxDiagnosticFieldLength)
{
    std::string sanitized;
    sanitized.reserve(std::min(value.size(), maxLength));
    for (char ch : value) {
        if (sanitized.size() >= maxLength) {
            break;
        }
        const bool allowed =
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '.' || ch == '_' || ch == '-' || ch == ':' || ch == '/';
        sanitized.push_back(allowed ? ch : '_');
    }
    if (sanitized.empty()) {
        sanitized = "unknown";
    }
    return sanitized;
}

inline std::string sanitizeMessage(std::string_view value)
{
    std::string sanitized;
    sanitized.reserve(std::min(value.size(), kMaxDiagnosticMessageLength));
    for (char ch : value) {
        if (sanitized.size() >= kMaxDiagnosticMessageLength) {
            break;
        }
        sanitized.push_back((ch >= 32 && ch <= 126) ? ch : ' ');
    }
    while (!sanitized.empty() && sanitized.back() == ' ') {
        sanitized.pop_back();
    }
    return sanitized;
}

inline std::string formatError(std::string_view service,
                               std::string_view component,
                               std::string_view operation,
                               std::string_view category,
                               std::string_view identity,
                               std::string_view message)
{
    return "service=" + sanitizeField(service) +
           " component=" + sanitizeField(component) +
           " operation=" + sanitizeField(operation) +
           " category=" + sanitizeField(category) +
           " identity=" + sanitizeField(identity) +
           " message=\"" + sanitizeMessage(message) + "\"";
}

inline void logError(std::string_view service,
                     std::string_view component,
                     std::string_view operation,
                     std::string_view category,
                     std::string_view identity,
                     std::string_view message)
{
    LOG(ERROR) << formatError(service, component, operation, category, identity, message);
}

inline void logInfo(std::string_view service,
                    std::string_view component,
                    std::string_view operation,
                    std::string_view category,
                    std::string_view identity,
                    std::string_view message)
{
    LOG(INFO) << formatError(service, component, operation, category, identity, message);
}

} // namespace RSCGroup::diagnostics
