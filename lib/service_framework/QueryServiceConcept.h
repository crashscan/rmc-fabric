#pragma once

#include <concepts>

namespace RSCGroup {

/**
 * @brief Concept that all query service types must satisfy.
 *
 * Every query service must expose an isReady() method that returns a
 * bool-compatible value, allowing transports to check readiness before
 * servicing requests.
 *
 * Extensible: additional requirements (e.g. getStatus(), isHealthy()) can
 * be added here as the framework evolves.
 */
template <typename T>
concept QueryService = requires(const T& t) {
    { t.isReady() } -> std::convertible_to<bool>;
};

} // namespace RSCGroup
