//
// Created by vvass on 24-Jul-26.
//
/**
 * @file LldpUtils.h
 * @brief LLDP-specific identity parsing and normalization helpers.
 *
 * Shared between LldpdSource (cache keying) and ObservationModelEngine
 * (candidate matching) so that source-side and engine-side identity
 * resolution remain aligned.
 */
#pragma once
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace RSCGroup {

/**
 * @brief Check whether a string looks like a MAC address.
 *
 * Expects exactly 17 characters: six hex-digit pairs separated by colons.
 */
inline bool isMacLike(std::string_view s)
{
    if (s.size() != 17) return false;
    for (int i = 0; i < 17; ++i) {
        if (i == 2 || i == 5 || i == 8 || i == 11 || i == 14) {
            if (s[i] != ':') return false;
        } else {
            if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
        }
    }
    return true;
}

/**
 * @brief Normalize a MAC-like string to lowercase colon-separated form.
 */
inline std::string normalizeMac(std::string_view s)
{
    std::string out;
    out.reserve(17);
    for (char c : s)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

/**
 * @brief Resolve a MAC-based LLDP identity from chassis and port IDs.
 *
 * Prefers MAC-like chassis ID, falls back to MAC-like port ID.
 * Returns a normalized MAC string if resolvable, empty string otherwise.
 */
inline std::string resolveLldpIdentity(
    const std::optional<std::string>& remoteChassisId,
    const std::optional<std::string>& remotePortId)
{
    if (remoteChassisId && isMacLike(*remoteChassisId))
        return normalizeMac(*remoteChassisId);
    if (remotePortId && isMacLike(*remotePortId))
        return normalizeMac(*remotePortId);
    return {};
}

} // namespace RSCGroup