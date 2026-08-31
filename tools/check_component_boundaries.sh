#!/usr/bin/env sh
set -eu

root_dir="${1:-.}"

inv_client_cmake="$root_dir/services/rmc-inventory/libs/inventory-client/CMakeLists.txt"
inv_agentd_cmake="$root_dir/services/rmc-inventory/apps/inventory-agentd/CMakeLists.txt"
obsd_cmake="$root_dir/services/network-observation/apps/network-observationd/CMakeLists.txt"
obs_cli_cmake="$root_dir/services/network-observation/apps/net-observe/CMakeLists.txt"

if grep -n -E '\binventory_transport\b' "$inv_client_cmake"; then
    echo "inventory-client must not depend on inventory_transport" >&2
    exit 1
fi

inv_agent_sources="$(awk '/add_executable\(inventory-agentd/{flag=1} flag{print} /\)/&&flag{exit}' "$inv_agentd_cmake")"
if printf '%s\n' "$inv_agent_sources" | grep -n -E '\$\{SRC_BASE\}/|InventoryService\.cpp'; then
    echo "inventory-agentd must be a thin composition root (main.cpp only)" >&2
    exit 1
fi

obsd_sources="$(awk '/add_executable\(network-observationd/{flag=1} flag{print} /\)/&&flag{exit}' "$obsd_cmake")"
if printf '%s\n' "$obsd_sources" | grep -n -E '\$\{TRANSPORT_BASE\}/|ObservationService\.cpp'; then
    echo "network-observationd must not compile service/transport implementation files directly" >&2
    exit 1
fi

obs_cli_sources="$(awk '/add_executable\(net-observe/{flag=1} flag{print} /\)/&&flag{exit}' "$obs_cli_cmake")"
if printf '%s\n' "$obs_cli_sources" | grep -n -E '\$\{TRANSPORT_BASE\}/dbus/DbusClient\.cpp|\bnetwork_observation_dbus_transport\b'; then
    echo "net-observe must not compile or depend directly on server transport implementation" >&2
    exit 1
fi

exit 0
