#include "NetworkObservationAdapter.h"

#include "LldpObserver.h"
#include "LldpdSource.h"
#include "NetlinkNetworkMonitor.h"
#include "NetlinkTypes.h"
#include "ObservationModelEngine.h"

#include <glog/logging.h>
#include <linux/rtnetlink.h>

#include <chrono>

namespace RSCGroup {

static NeighborReachability nudToReachability(unsigned short nudState)
{
    using enum NeighborReachability;
    switch (nudState) {
        case NUD_INCOMPLETE: return Incomplete;
        case NUD_REACHABLE: return Reachable;
        case NUD_STALE: return Stale;
        case NUD_DELAY: return Delay;
        case NUD_PROBE: return Probe;
        case NUD_FAILED: return Failed;
        case NUD_NOARP: return NoArp;
        case NUD_PERMANENT: return Permanent;
        default: return Unknown;
    }
}

static FdbEntryKind fdbToEntryKind(const FdbEvent& e)
{
    if (e.local) return FdbEntryKind::Local;
    if (e.permanent) return FdbEntryKind::Static;
    return FdbEntryKind::Dynamic;
}

static ObservationEvent toObsEvent(bool present)
{
    return present ? ObservationEvent::Present : ObservationEvent::Removed;
}

static std::string makeCidr(const InterfaceIpEvent& e)
{
    return e.address + "/" + std::to_string(static_cast<int>(e.prefixLen));
}

NetworkObservationAdapter::NetworkObservationAdapter(ModelConfig config)
    : model_(std::make_unique<ObservationModelEngine>(std::move(config)))
{
}

NetworkObservationAdapter::NetworkObservationAdapter(std::unique_ptr<INetworkObservationModel> model)
    : model_(std::move(model))
{
}

NetworkObservationAdapter::~NetworkObservationAdapter() = default;

static MonitorCallbacks makeCallbacks(INetworkObservationModel& model, LldpObserver* lldpObserver)
{
    MonitorCallbacks cb;
    auto now = [] { return std::chrono::steady_clock::now(); };

    cb.onLinkChanged = [&model, now, lldpObserver](const LinkEvent& e) {
        LinkObservation obs;
        obs.observedAt = now();
        obs.kind = ObservationKind::Link;
        obs.ifindex = e.ifindex;
        obs.ifname = e.ifname;
        obs.mac = e.mac;
        obs.adminUp = e.adminUp;
        obs.running = e.running;
        obs.operstate = std::to_string(e.operState);
        obs.masterIfname = e.masterIfname;
        obs.event = toObsEvent(e.present);
        model.onLinkObservation(obs);

        if (lldpObserver) {
            if (!e.present || !e.running) {
                lldpObserver->onInterfaceDown(e.ifname);
            } else {
                lldpObserver->onInterfaceUp(e.ifname);
            }
        }
    };

    cb.onInterfaceIpChanged = [&model, now](const InterfaceIpEvent& e) {
        AddressObservation obs;
        obs.observedAt = now();
        obs.kind = ObservationKind::Address;
        obs.ifname = e.ifname;
        obs.event = toObsEvent(e.present);
        obs.family = e.family;
        obs.cidr = makeCidr(e);
        model.onAddressObservation(obs);
    };

    cb.onFdbChanged = [&model, now](const FdbEvent& e) {
        FdbObservation obs;
        obs.observedAt = now();
        obs.kind = ObservationKind::Fdb;
        obs.portIfname = e.ifname;
        obs.event = toObsEvent(e.present);
        obs.mac = e.mac;
        obs.entryKind = fdbToEntryKind(e);
        model.onFdbObservation(obs);
    };

    cb.onNeighborChanged = [&model, now](const NeighborEvent& e) {
        NeighborObservation obs;
        obs.observedAt = now();
        obs.kind = ObservationKind::Neighbor;
        obs.ifname = e.ifname;
        obs.event = toObsEvent(e.present);
        obs.family = e.family;
        obs.mac = e.mac;
        obs.ip = e.ip;
        obs.reachability = nudToReachability(e.nudState);
        model.onNeighborObservation(obs);
    };

    return cb;
}

bool NetworkObservationAdapter::start()
{
    model_->prepareForRestart();

    auto lldpSource = std::make_unique<LldpdSource>(
        LldpSourceConfig{},
        [this](const LldpObservation& obs) {
            model_->onLldpObservation(obs);
        });
    lldpObserver_ = std::make_unique<LldpObserver>(std::move(lldpSource));
    if (!lldpObserver_->start()) {
        LOG(WARNING) << "LLDP observer failed to start — LLDP unavailable";
        lldpObserver_.reset();
    }

    auto callbacks = makeCallbacks(*model_, lldpObserver_.get());
    monitor_ = std::make_unique<NetlinkNetworkMonitor>(std::move(callbacks));
    if (!monitor_->start()) {
        lldpObserver_.reset();
        return false;
    }

    model_->markLive();

    LOG(INFO) << "NetworkObservationAdapter started";
    return true;
}

void NetworkObservationAdapter::stop()
{
    if (monitor_) {
        monitor_->stop();
        monitor_.reset();
    }
    if (lldpObserver_) {
        lldpObserver_->stop();
        lldpObserver_.reset();
    }
}

bool NetworkObservationAdapter::isRunning() const
{
    return monitor_ && monitor_->isRunning();
}

void NetworkObservationAdapter::setEventSink(IModelEventSink* sink)
{
    model_->setEventSink(sink);
}

void NetworkObservationAdapter::setInterfacePolicy(std::unique_ptr<IInterfacePolicy> policy)
{
    model_->setInterfacePolicy(std::move(policy));
}

void NetworkObservationAdapter::setClassifier(std::unique_ptr<ICandidateClassifier> classifier)
{
    model_->setClassifier(std::move(classifier));
}

LocalNetworkSnapshot NetworkObservationAdapter::localSnapshot() const
{
    return model_->localSnapshot();
}

std::vector<RemoteCandidate> NetworkObservationAdapter::remoteCandidates() const
{
    return model_->remoteCandidates();
}

std::optional<RemoteCandidate> NetworkObservationAdapter::findCandidateByMac(const std::string& mac) const
{
    return model_->findCandidateByMac(mac);
}

void NetworkObservationAdapter::age(std::chrono::steady_clock::time_point now)
{
    model_->age(now);
}

} // namespace RSCGroup
