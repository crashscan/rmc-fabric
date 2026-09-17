//
// Created by vvass on 24-Jul-26.
//
/**
 * @file LldpObserverTypes.h
 * @brief Public types for the LLDP observer module.
 */
#pragma once
#include "ObservationTypes.h"
#include <functional>
#include <string>
#include <vector>

namespace RSCGroup {
struct LldpSourceConfig {
    /// Optional list of interfaces to monitor (empty = all)
    std::vector<std::string> watchedInterfaces;
    /// Whether to emit Removal observations for disappeared neighbors
    bool emitRemovals = true;
    /// lldpd control socket used by bounded connections (liveness probe,
    /// bounded enumeration, and the watch). Empty =
    /// lldpctl_get_default_transport().
    std::string ctlSocketPath;
};

using LldpObservationCallback = std::function<void(const LldpObservation &)>;
} // namespace RSCGroup