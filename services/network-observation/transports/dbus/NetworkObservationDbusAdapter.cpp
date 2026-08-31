#include "NetworkObservationDbusAdapter.h"
#include "NetworkObservationDbusCodec.h"

#include "IObservationQueryService.h"
#include "LocalStateTypes.h"
#include "CandidateTypes.h"

#include <dbus-cxx.h>
#include <glog/logging.h>
#include <sigc++/sigc++.h>

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace RSCGroup {

namespace {

namespace contract = interop_contract::network_observation;

/// Bridge: convert the service-internal RSCGroup::RemoteCandidate to the wire
/// type expected by the shared toVariantMap helper.
contract::RemoteCandidate toWireCandidate(const RemoteCandidate& c)
{
    contract::RemoteCandidate w;
    w.mac             = c.mac;
    w.classification  = c.classification;
    w.status          = c.status;
    w.seenInFdb       = c.seenInFdb;
    w.seenInNeigh     = c.seenInNeigh;
    w.seenInLldp      = c.seenInLldp;
    w.bridgePort      = c.bridgePort;
    w.remoteChassisId = c.remoteChassisId;
    w.remotePortId    = c.remotePortId;
    w.remoteSystemName = c.remoteSystemName;
    w.neighborIfaces  = c.neighborIfaces;
    w.ipv4            = c.ipv4;
    w.ipv6            = c.ipv6;
    return w;
}

} // anonymous namespace

struct NetworkObservationHandler {
    /// Thread-safe service access: shared lock held during the call, exclusive
    /// lock taken by onTransportStopping() to clear the binding.
    ServiceBinding<IObservationQueryService>* binding = nullptr;

    std::map<std::string, DBus::Variant> GetLocalSnapshot()
    {
        std::map<std::string, DBus::Variant> result;
        if (auto guard = binding->acquire()) {
            auto snapshot = guard->localSnapshot();
            for (const auto& [name, iface] : snapshot.interfaces)
                result[name] = DBus::Variant(NetworkObservationDbusCodec::toVariantMap(iface));
        }
        return result;
    }

    std::map<std::string, DBus::Variant> GetInterface(std::string ifname)
    {
        if (auto guard = binding->acquire()) {
            auto iface = guard->getInterface(ifname);
            if (iface) return NetworkObservationDbusCodec::toVariantMap(*iface);
        }
        return {};
    }

    std::vector<std::string> GetRemoteCandidateMacs()
    {
        std::vector<std::string> result;
        if (auto guard = binding->acquire()) {
            for (const auto& c : guard->remoteCandidates())
                result.push_back(c.mac);
        }
        return result;
    }

    std::map<std::string, DBus::Variant> GetCandidateByMac(std::string mac)
    {
        if (auto guard = binding->acquire()) {
            auto c = guard->getCandidateByMac(mac);
            if (c) return NetworkObservationDbusCodec::toVariantMap(toWireCandidate(*c));
        }
        return {};
    }

    std::map<std::string, std::map<std::string, DBus::Variant>> GetIssues()
    {
        if (auto guard = binding->acquire()) {
            return NetworkObservationDbusCodec::encodeIssues(guard->getIssues());
        }
        return {};
    }

    bool GetReady()
    {
        if (auto guard = binding->acquire()) return guard->isReady();
        return false;
    }

    std::string GetPhase()
    {
        if (auto guard = binding->acquire()) return guard->getPhase();
        return std::string(contract::PHASE_STOPPED);
    }
};

NetworkObservationDbusAdapter::NetworkObservationDbusAdapter()
    : handler_(std::make_shared<NetworkObservationHandler>())
{
    handler_->binding = &binding_;
}

void NetworkObservationDbusAdapter::setService(IObservationQueryService* service)
{
    binding_.bind(service);
}

void NetworkObservationDbusAdapter::bind(const std::shared_ptr<DBus::Object>& object,
                                         const std::string& interfaceName)
{
    if (!object) {
        throw std::invalid_argument("NetworkObservationDbusAdapter::bind: object is null");
    }
    createSignals(object, interfaceName);
    bindMethods(object, interfaceName);
}

void NetworkObservationDbusAdapter::onTransportStopping()
{
    binding_.detach();
}

void NetworkObservationDbusAdapter::createSignals(const std::shared_ptr<DBus::Object>& object,
                                                  const std::string& interfaceName)
{
    signalLocalStateChanged_ = createVoidSignal(object, interfaceName,
                                                std::string(contract::SIGNAL_LOCAL_STATE_CHANGED));
    signalInterfaceChanged_  = createStringSignal(object, interfaceName,
                                                  std::string(contract::SIGNAL_INTERFACE_CHANGED));
    signalInterfaceRemoved_  = createStringSignal(object, interfaceName,
                                                  std::string(contract::SIGNAL_INTERFACE_REMOVED));
    signalCandidateChanged_  = createStringSignal(object, interfaceName,
                                                  std::string(contract::SIGNAL_CANDIDATE_CHANGED));
    signalCandidateRemoved_  = createStringSignal(object, interfaceName,
                                                  std::string(contract::SIGNAL_CANDIDATE_REMOVED));
    signalReadyChanged_      = createBoolSignal(object, interfaceName,
                                                std::string(contract::SIGNAL_READY_CHANGED));
}

void NetworkObservationDbusAdapter::bindMethods(const std::shared_ptr<DBus::Object>& object,
                                                const std::string& interfaceName)
{
    auto& h = *handler_;
    object->create_method<std::map<std::string, DBus::Variant>()>(
        interfaceName, std::string(contract::METHOD_GET_LOCAL_SNAPSHOT),
        sigc::mem_fun(h, &NetworkObservationHandler::GetLocalSnapshot));
    object->create_method<std::map<std::string, DBus::Variant>(std::string)>(
        interfaceName, std::string(contract::METHOD_GET_INTERFACE),
        sigc::mem_fun(h, &NetworkObservationHandler::GetInterface));
    object->create_method<std::vector<std::string>()>(
        interfaceName, std::string(contract::METHOD_GET_REMOTE_CANDIDATE_MACS),
        sigc::mem_fun(h, &NetworkObservationHandler::GetRemoteCandidateMacs));
    object->create_method<std::map<std::string, DBus::Variant>(std::string)>(
        interfaceName, std::string(contract::METHOD_GET_CANDIDATE_BY_MAC),
        sigc::mem_fun(h, &NetworkObservationHandler::GetCandidateByMac));
    object->create_method<std::map<std::string, std::map<std::string, DBus::Variant>>()>(
        interfaceName, std::string(contract::METHOD_GET_ISSUES),
        sigc::mem_fun(h, &NetworkObservationHandler::GetIssues));
    object->create_method<bool()>(
        interfaceName, std::string(contract::METHOD_GET_READY),
        sigc::mem_fun(h, &NetworkObservationHandler::GetReady));
    object->create_method<std::string()>(
        interfaceName, std::string(contract::METHOD_GET_PHASE),
        sigc::mem_fun(h, &NetworkObservationHandler::GetPhase));
}

void NetworkObservationDbusAdapter::publishLocalStateChanged()
{
    if (!signalLocalStateChanged_) return;
    signalLocalStateChanged_->emit();
}

void NetworkObservationDbusAdapter::publishInterfaceChanged(const std::string& ifname)
{
    if (!signalInterfaceChanged_) return;
    signalInterfaceChanged_->emit(ifname);
}

void NetworkObservationDbusAdapter::publishInterfaceRemoved(const std::string& ifname)
{
    if (!signalInterfaceRemoved_) return;
    signalInterfaceRemoved_->emit(ifname);
}

void NetworkObservationDbusAdapter::publishCandidateChanged(const std::string& mac)
{
    if (!signalCandidateChanged_) return;
    signalCandidateChanged_->emit(mac);
}

void NetworkObservationDbusAdapter::publishCandidateRemoved(const std::string& mac)
{
    if (!signalCandidateRemoved_) return;
    signalCandidateRemoved_->emit(mac);
}

void NetworkObservationDbusAdapter::publishReadyChanged(bool ready)
{
    if (!signalReadyChanged_) return;
    signalReadyChanged_->emit(ready);
}

} // namespace RSCGroup
