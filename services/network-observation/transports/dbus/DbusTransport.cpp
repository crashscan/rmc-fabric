//
// Created by vvass on 21-Jul-26.
//
#include "DbusTransport.h"
#include "NetworkObservationDbusAdapter.h"

#include <glog/logging.h>

namespace RSCGroup {

namespace {
constexpr auto DBUS_SERVICE   = "org.rsc.NetworkObservation";
constexpr auto DBUS_PATH      = "/org/rsc/NetworkObservation";
constexpr auto DBUS_INTERFACE = "org.rsc.NetworkObservation";
} // namespace

DbusTransport::DbusTransport(const std::string& busType)
    : DbusTransportBase(busType,
                        std::make_unique<NetworkObservationDbusAdapter>(),
                        DBUS_SERVICE,
                        DBUS_PATH,
                        DBUS_INTERFACE)
{}

NetworkObservationDbusAdapter* DbusTransport::obsAdapter() const
{
    return static_cast<NetworkObservationDbusAdapter*>(getAdapter());
}

void DbusTransport::setQueryProvider(IObservationQueryService* provider)
{
    obsAdapter()->setService(provider);
}

bool DbusTransport::start()
{
    try {
        DbusTransportBase::start();
        return true;
    } catch (const std::exception& e) {
        LOG(ERROR) << "DbusTransport start failed: " << e.what();
        obsAdapter()->setService(nullptr);
        return false;
    }
}

void DbusTransport::stop()
{
    DbusTransportBase::stop();
    // onTransportStopping() (called by DbusTransportBase::stop()) already clears
    // the service pointer via NetworkObservationDbusAdapter::onTransportStopping().
}

void DbusTransport::publishLocalStateChanged()
{
    if (!isRunning()) return;
    obsAdapter()->publishLocalStateChanged();
}

void DbusTransport::publishInterfaceChanged(const std::string& ifname)
{
    if (!isRunning()) return;
    obsAdapter()->publishInterfaceChanged(ifname);
}

void DbusTransport::publishInterfaceRemoved(const std::string& ifname)
{
    if (!isRunning()) return;
    obsAdapter()->publishInterfaceRemoved(ifname);
}

void DbusTransport::publishCandidateChanged(const std::string& mac)
{
    if (!isRunning()) return;
    obsAdapter()->publishCandidateChanged(mac);
}

void DbusTransport::publishCandidateRemoved(const std::string& mac)
{
    if (!isRunning()) return;
    obsAdapter()->publishCandidateRemoved(mac);
}

void DbusTransport::publishReadyChanged(bool ready)
{
    if (!isRunning()) return;
    obsAdapter()->publishReadyChanged(ready);
}

} // namespace RSCGroup