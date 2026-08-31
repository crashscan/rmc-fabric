#!/usr/bin/env sh
set -eu

root_dir="${1:-.}"
source_glob='.*\.(h|hh|hpp|hxx|c|cc|cpp|cxx)$'

find_sources() {
    find "$1" -type f | grep -E "$source_glob" || true
}

extract_add_executable_block() {
    awk "/add_executable\\($1/{flag=1} flag{print} /^\\)/&&flag{exit}" "$2"
}

inv_client_cmake="$root_dir/services/rmc-inventory/clients/inventory-client/CMakeLists.txt"
inv_agentd_cmake="$root_dir/services/rmc-inventory/apps/inventory-agentd/CMakeLists.txt"
obsd_cmake="$root_dir/services/network-observation/apps/network-observationd/CMakeLists.txt"
obs_cli_cmake="$root_dir/services/network-observation/apps/net-observe/CMakeLists.txt"
obs_client_cmake="$root_dir/services/network-observation/clients/dbus/CMakeLists.txt"

if grep -n -E '\binventory_transport\b' "$inv_client_cmake"; then
    echo "inventory-client must not depend on inventory_transport" >&2
    exit 1
fi

if grep -n -E '\bnetwork_observation_(dbus|stdout)_transport\b' "$obs_client_cmake"; then
    echo "network_observation_client must not depend on server transport implementations" >&2
    exit 1
fi

inv_agent_sources="$(extract_add_executable_block inventory-agentd "$inv_agentd_cmake")"
if printf '%s\n' "$inv_agent_sources" | grep -E "$source_glob" | grep -v 'main\.cpp'; then
    echo "inventory-agentd must be a thin composition root (main.cpp only)" >&2
    exit 1
fi

obsd_sources="$(extract_add_executable_block network-observationd "$obsd_cmake")"
if printf '%s\n' "$obsd_sources" | grep -E "$source_glob" | grep -v 'main\.cpp'; then
    echo "network-observationd must be a thin composition root (main.cpp only)" >&2
    exit 1
fi

obs_cli_sources="$(extract_add_executable_block net-observe "$obs_cli_cmake")"
if printf '%s\n' "$obs_cli_sources" | grep -E "$source_glob" | grep -v 'main\.cpp'; then
    echo "net-observe must be a thin composition root (main.cpp only)" >&2
    exit 1
fi

if find_sources "$root_dir/services/rmc-inventory/core" | xargs grep -n -E '#include .*(Dbus|Transport|InventoryService|Stdout|Adapter)'; then
    echo "inventory core must not include service or transport implementation headers" >&2
    exit 1
fi

if find_sources "$root_dir/services/network-observation/core" | xargs grep -n -E '#include .*(Dbus|Transport|ObservationService|NetlinkNetworkMonitor|LldpObserver|LldpdSource)'; then
    echo "network observation core must not include service, transport, or concrete input implementation headers" >&2
    exit 1
fi

if find_sources "$root_dir/services/rmc-inventory/service/ports" | xargs grep -n -E '#include .*(Dbus|Stdout|Adapter)'; then
    echo "inventory service ports must not include concrete transports" >&2
    exit 1
fi

if find_sources "$root_dir/services/network-observation/service/ports" | xargs grep -n -E '#include .*(Dbus|Stdout|Adapter)'; then
    echo "network observation service ports must not include concrete transports" >&2
    exit 1
fi

if find_sources "$root_dir/lib/interop_contract" | xargs grep -n -E '#include .*(dbus|DBus|Transport)'; then
    echo "interop_contract headers must remain transport-neutral" >&2
    exit 1
fi

exit 0
