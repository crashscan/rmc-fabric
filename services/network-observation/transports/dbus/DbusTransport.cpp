#include "DbusTransport.h"

#include "NetworkObservationDbusAdapter.h"

#include <interop_contract/network_observation/NetworkObservationContracts.hpp>

#include <glog/logging.h>

namespace RSCGroup {
namespace {
using namespace interop_contract::network_observation;
} // namespace

DbusTransport::DbusTransport(const std::string& busType)
    : DbusTransportBase(busType,
                        std::make_unique<NetworkObservationDbusAdapter>(),
                        std::string(SERVICE_NAME),
                        std::string(OBJECT_PATH),
                        std::string(INTERFACE))
{
}

NetworkObservationDbusAdapter* DbusTransport::obsAdapter() const
{
    return getTypedAdapter<NetworkObservationDbusAdapter>();
}

void DbusTransport::bindQueryService(IObservationQueryService& provider)
{
    obsAdapter()->setService(&provider);
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
}

std::string DbusTransport::name() const
{
    return "dbus";
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
