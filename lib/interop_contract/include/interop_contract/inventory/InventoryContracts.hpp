#pragma once

#include <string_view>

namespace interop_contract::inventory {

inline constexpr std::string_view SERVICE_NAME  = "org.rsc.Inventory";
inline constexpr std::string_view OBJECT_PATH   = "/org/rsc/Inventory";
inline constexpr std::string_view INTERFACE     = "org.rsc.Inventory";

// Methods
inline constexpr std::string_view METHOD_GET_IDENTITY      = "GetIdentity";
inline constexpr std::string_view METHOD_GET_FIELD         = "GetField";
inline constexpr std::string_view METHOD_GET_SOURCE_STATES = "GetSourceStates";
inline constexpr std::string_view METHOD_GET_ISSUES        = "GetIssues";
inline constexpr std::string_view METHOD_GET_READY         = "GetReady";
inline constexpr std::string_view METHOD_GET_PHASE         = "GetPhase";
inline constexpr std::string_view METHOD_GET_VERSION       = "GetVersion";
inline constexpr std::string_view METHOD_REFRESH           = "Refresh";

// Signals
inline constexpr std::string_view SIGNAL_INVENTORY_CHANGED    = "InventoryChanged";
inline constexpr std::string_view SIGNAL_SOURCE_STATE_CHANGED = "SourceStateChanged";
inline constexpr std::string_view SIGNAL_READY_CHANGED        = "ReadyChanged";

// Phase values
inline constexpr std::string_view PHASE_INITIALIZING = "initializing";
inline constexpr std::string_view PHASE_LIVE         = "live";

// Metadata keys (never source-owned, never diffed)
inline constexpr std::string_view FIELD_VERSION   = "version";
inline constexpr std::string_view FIELD_TIMESTAMP = "timestamp";
inline constexpr std::string_view FIELD_READY     = "ready";
inline constexpr std::string_view FIELD_PHASE     = "phase";

// Field keys (flat camelCase, v1 contract)
inline constexpr std::string_view FIELD_UUID             = "uuid";
inline constexpr std::string_view FIELD_NODE_NAME        = "nodeName";
inline constexpr std::string_view FIELD_FIRMWARE_VERSION = "firmwareVersion";
inline constexpr std::string_view FIELD_SOFTWARE_VERSION = "softwareVersion";
inline constexpr std::string_view FIELD_DEVICE_CLASS     = "deviceClass";
inline constexpr std::string_view FIELD_DEVICE_MODEL_ID  = "deviceModelId";
inline constexpr std::string_view FIELD_DEVICE_PROJECT   = "deviceProject";

// Source state keys
inline constexpr std::string_view SOURCE_STATE_HEALTH           = "health";
inline constexpr std::string_view SOURCE_STATE_REQUIRED         = "required";
inline constexpr std::string_view SOURCE_STATE_STALE            = "stale";
inline constexpr std::string_view SOURCE_STATE_LAST_ATTEMPT_TS  = "lastAttemptTs";
inline constexpr std::string_view SOURCE_STATE_LAST_SUCCESS_TS  = "lastSuccessTs";
inline constexpr std::string_view SOURCE_STATE_LAST_ERROR       = "lastError";
inline constexpr std::string_view SOURCE_STATE_ORIGIN           = "origin";

// Source health values
inline constexpr std::string_view HEALTH_OK       = "ok";
inline constexpr std::string_view HEALTH_DEGRADED = "degraded";
inline constexpr std::string_view HEALTH_FAILED   = "failed";

// Issue keys (GetIssues inner maps)
inline constexpr std::string_view ISSUE_SEVERITY = "severity";
inline constexpr std::string_view ISSUE_MESSAGE  = "message";
inline constexpr std::string_view ISSUE_ORIGIN   = "origin";

// Severity values
inline constexpr std::string_view SEVERITY_ERROR   = "error";
inline constexpr std::string_view SEVERITY_WARNING = "warning";

} // namespace interop_contract::inventory
