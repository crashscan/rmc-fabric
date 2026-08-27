#!/usr/bin/env sh
set -eu

if [ ! -d "lib/interop_contract" ]; then
    exit 0
fi

if grep -R -n -E 'dbus-cxx|<DBus/|\bDBus::' lib/interop_contract; then
    echo "transport-specific DBus usage found under lib/interop_contract" >&2
    exit 1
fi

exit 0
