#include <InventoryDbusCodec.h>

#include <dbus-cxx.h>

int main()
{
    interop_contract::inventory::InventorySnapshot snapshot;
    snapshot.fields["hostname"] = std::string("test-host");

    const auto encoded = RSCGroup::InventoryDbusCodec::encodeSnapshot(snapshot);
    return encoded.empty() ? 1 : 0;
}
