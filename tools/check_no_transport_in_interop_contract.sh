#!/usr/bin/env sh
set -eu

if [ ! -d "lib/interop_contract" ]; then
    exit 0
fi

if find lib/interop_contract -type f \( -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.hxx' \) \
    -exec grep -n -E 'dbus-cxx|<DBus/|\bDBus::|DbusVariantMapReader|dbus_client_support' {} +; then
    echo "transport-specific DBus usage found under lib/interop_contract" >&2
    exit 1
fi

exit 0
