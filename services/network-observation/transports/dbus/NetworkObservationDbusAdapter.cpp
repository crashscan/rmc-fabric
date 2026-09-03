#include "NetworkObservationDbusAdapter.h"
#include "NetworkObservationDbusCodec.h"
#include "NetworkObservationQueryHandler.h"

#include <dbus-cxx.h>
#include <sigc++/sigc++.h>

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace RSCGroup {

namespace {
    namespace contract = interop_contract::network_observation;
}

NetworkObservationDbusAdapter::NetworkObservationDbusAdapter()
    : handler_(std::make_shared<NetworkObservationQueryHandler>(binding_))
{}

NetworkObservationDbusAdapter::~NetworkObservationDbusAdapter() = default;

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

void NetworkObservationDbusAdapter::quiesceQueries() noexcept
{
    binding_.detach();
}

void NetworkObservationDbusAdapter::onTransportStopping()
{
    quiesceQueries();
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

void NetworkObservationDbusAdapter::bindMethods(const std::shared_ptr<DBus::Object>& object, const std::string& interfaceName)
{
    auto& h = *handler_;
    object->create_method<std::map<std::string, DBus::Variant>()>(
        interfaceName, std::string(contract::METHOD_GET_LOCAL_SNAPSHOT),
        sigc::mem_fun(h, &NetworkObservationQueryHandler::getLocalSnapshot));
    object->create_method<std::map<std::string, DBus::Variant>(std::string)>(
        interfaceName, std::string(contract::METHOD_GET_INTERFACE),
        sigc::mem_fun(h, &NetworkObservationQueryHandler::getInterface));
    object->create_method<std::vector<std::string>()>(
        interfaceName, std::string(contract::METHOD_GET_REMOTE_CANDIDATE_MACS),
        sigc::mem_fun(h, &NetworkObservationQueryHandler::getRemoteCandidateMacs));
    object->create_method<std::map<std::string, DBus::Variant>(std::string)>(
        interfaceName, std::string(contract::METHOD_GET_CANDIDATE_BY_MAC),
        sigc::mem_fun(h, &NetworkObservationQueryHandler::getCandidateByMac));
    object->create_method<std::map<std::string, std::map<std::string, DBus::Variant>>()>(
        interfaceName, std::string(contract::METHOD_GET_ISSUES),
        sigc::mem_fun(h, &NetworkObservationQueryHandler::getIssues));
    object->create_method<bool()>(
        interfaceName, std::string(contract::METHOD_GET_READY),
        sigc::mem_fun(h, &NetworkObservationQueryHandler::getReady));
    object->create_method<std::string()>(
        interfaceName, std::string(contract::METHOD_GET_PHASE),
        sigc::mem_fun(h, &NetworkObservationQueryHandler::getPhase));
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
