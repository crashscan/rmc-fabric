//
// Created by vvass on 21-Jul-26.
//
#include "ObservationService.h"

namespace RSCGroup {

ObservationService::ObservationService(
    std::unique_ptr<NetworkObservationAdapter> adapter,
    std::unique_ptr<ITransport> transport)
    : adapter_(std::move(adapter))
    , transport_(std::move(transport))
{}

bool ObservationService::start()
{
    ready_ = false;
    adapter_->setEventSink(this);
    transport_->setQueryProvider(this);
    if (!transport_->start()) {
        return false;
    }
    if (!adapter_->start()) {
        transport_->stop();
        return false;
    }
    ready_ = true;
    transport_->publishReadyChanged(true);

    agingThread_ = std::jthread([this](std::stop_token st) { agingLoop(st); });
    return true;
}

void ObservationService::stop()
{
    if (agingThread_.joinable()) {
        agingThread_.request_stop();   // wakes the cv wait immediately
        agingThread_.join();
    }
    if (ready_.exchange(false)) {
        transport_->publishReadyChanged(false);
    }
    adapter_->stop();
    transport_->stop();
}

void ObservationService::agingLoop(std::stop_token st)
{
    std::unique_lock lk(agingMutex_);
    while (!st.stop_requested()) {
        if (agingCv_.wait_for(lk, st, kAgingInterval,
                              [&] { return st.stop_requested(); }))
            break;
        adapter_->age(std::chrono::steady_clock::now());
    }
}

void ObservationService::onModelEvent(const ModelEvent& event)
{
    switch (event.kind) {
        case ModelEventKind::LocalInterfaceChanged:
            if (event.ifname) transport_->publishInterfaceChanged(*event.ifname);
            transport_->publishLocalStateChanged();
            break;

        case ModelEventKind::LocalInterfaceRemoved:
            if (event.ifname) transport_->publishInterfaceRemoved(*event.ifname);
            transport_->publishLocalStateChanged();
            break;

        case ModelEventKind::LocalAddressChanged:
            if (event.ifname) transport_->publishInterfaceChanged(*event.ifname);
            transport_->publishLocalStateChanged();
            break;

        case ModelEventKind::CandidateAdded:
        case ModelEventKind::CandidateUpdated:
        case ModelEventKind::CandidateConfirmed:
        case ModelEventKind::CandidateAged:
        case ModelEventKind::ClassificationChanged:
            if (event.mac) transport_->publishCandidateChanged(*event.mac);
            break;

        case ModelEventKind::CandidateExpired:
        case ModelEventKind::CandidateRemoved:
            if (event.mac) transport_->publishCandidateRemoved(*event.mac);
            break;
    }
}

LocalNetworkSnapshot ObservationService::localSnapshot() const
{
    return adapter_->localSnapshot();
}

std::optional<LocalInterfaceState> ObservationService::getInterface(const std::string& ifname) const
{
    auto snapshot = adapter_->localSnapshot();
    auto it = snapshot.interfaces.find(ifname);
    if (it != snapshot.interfaces.end()) return it->second;
    return std::nullopt;
}

std::vector<RemoteCandidate> ObservationService::remoteCandidates() const
{
    return adapter_->remoteCandidates();
}

std::optional<RemoteCandidate> ObservationService::getCandidateByMac(const std::string& mac) const
{
    return adapter_->findCandidateByMac(mac);
}

} // namespace RSCGroup
