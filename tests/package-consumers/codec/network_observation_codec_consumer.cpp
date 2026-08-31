#include <NetworkObservationDbusCodec.h>

#include <dbus-cxx.h>

int main()
{
    interop_contract::network_observation::LocalInterfaceState iface;
    iface.ifindex  = 2;
    iface.ifname   = "eth0";
    iface.mac      = "00:11:22:33:44:55";
    iface.adminUp  = true;
    iface.running  = true;
    iface.operstate = "up";

    const auto encoded = RSCGroup::NetworkObservationDbusCodec::toVariantMap(iface);
    return encoded.empty() ? 1 : 0;
}
