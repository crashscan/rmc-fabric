#include "ObservationService.h"

#include <glog/logging.h>

#include <stdexcept>

namespace RSCGroup {
namespace {

[[nodiscard]] std::vector<std::shared_ptr<IObservationTransport>> observationTransports(const ServiceBase& service)
{
    std::vector<std::shared_ptr<IObservationTransport>> typed;
    typed.reserve(service.transports().size());
    for (const auto& transport : service.transports()) {
        if (auto typedTransport = std::dynamic_pointer_cast<IObservationTransport>(transport)) {
            typed.push_back(std::move(typedTransport));
        }
    }
    return typed;
}

} // namespace

ObservationService::ObservationService(std::unique_ptr<IObservationRuntime> runtime,
                                       std::shared_ptr<IObservationTransport> transport,
                                       std::chrono::steady_clock::duration agingInterval)
    : ServiceBase("observation-service")
    , runtime_(std::move(runtime))
    , agingInterval_(agingInterval)
{
    if (!runtime_) {
        throw std::invalid_argument("ObservationService: runtime is null");
    }
    if (!transport) {
        throw std::invalid_argument("ObservationService: transport is null");
    }
    ServiceBase::addTransport(std::move(transport));
}

ObservationService::~ObservationService()
{
    stop();
}

void ObservationService::validateConfiguration()
{
}

bool ObservationService::initializeComponents()
{
    runtime_->setEventSink(this);
    for (const auto& transport : observationTransports(*this)) {
        transport->bindQueryService(*this);
    }
    return true;
}

bool ObservationService::start()
{
    std::scoped_lock lock(lifecycleMutex_);
    if (ServiceBase::isRunning()) {
        return true;
    }

    if (!ServiceBase::start()) {
        return false;
    }
    if (!runtime_->start()) {
        ServiceBase::stop();
        return false;
    }

    ServiceBase::setReady(true);
    try {
        agingThread_ = std::jthread([this](std::stop_token st) {
            try {
                agingLoop(st);
            } catch (const std::exception& e) {
                LOG(ERROR) << "ObservationService: aging loop failed: " << e.what();
            } catch (...) {
                LOG(ERROR) << "ObservationService: aging loop failed";
            }
        });
    } catch (const std::exception& e) {
        runtime_->stop();
        ServiceBase::stop();
        LOG(ERROR) << "ObservationService: failed to start aging thread: " << e.what();
        return false;
    } catch (...) {
        runtime_->stop();
        ServiceBase::stop();
        LOG(ERROR) << "ObservationService: failed to start aging thread";
        return false;
    }
    return true;
}

void ObservationService::stop()
{
    std::scoped_lock lock(lifecycleMutex_);
    if (!ServiceBase::isRunning() && !agingThread_.joinable()) {
        return;
    }
    stopOwnedState();
    ServiceBase::stop();
}

void ObservationService::agingLoop(std::stop_token st)
{
    std::unique_lock lk(agingMutex_);
    while (!st.stop_requested()) {
        if (agingCv_.wait_for(lk, st, agingInterval_, [&] { return st.stop_requested(); })) {
            break;
        }
        runtime_->age(std::chrono::steady_clock::now());
    }
}

void ObservationService::onModelEvent(const ModelEvent& event)
{
    const auto typedTransports = observationTransports(*this);

    switch (event.kind) {
        case ModelEventKind::LocalInterfaceChanged:
            if (event.ifname) {
                for (const auto& transport : typedTransports) {
                    try {
                        transport->publishInterfaceChanged(*event.ifname);
                    } catch (const std::exception& e) {
                        LOG(ERROR) << "ObservationService: publishInterfaceChanged failed for transport "
                                   << transport->name() << ": " << e.what();
                    } catch (...) {
                        LOG(ERROR) << "ObservationService: publishInterfaceChanged failed for transport "
                                   << transport->name();
                    }
                }
            }
            for (const auto& transport : typedTransports) {
                try {
                    transport->publishLocalStateChanged();
                } catch (const std::exception& e) {
                    LOG(ERROR) << "ObservationService: publishLocalStateChanged failed for transport "
                               << transport->name() << ": " << e.what();
                } catch (...) {
                    LOG(ERROR) << "ObservationService: publishLocalStateChanged failed for transport "
                               << transport->name();
                }
            }
            break;

        case ModelEventKind::LocalInterfaceRemoved:
            if (event.ifname) {
                for (const auto& transport : typedTransports) {
                    try {
                        transport->publishInterfaceRemoved(*event.ifname);
                    } catch (const std::exception& e) {
                        LOG(ERROR) << "ObservationService: publishInterfaceRemoved failed for transport "
                                   << transport->name() << ": " << e.what();
                    } catch (...) {
                        LOG(ERROR) << "ObservationService: publishInterfaceRemoved failed for transport "
                                   << transport->name();
                    }
                }
            }
            for (const auto& transport : typedTransports) {
                try {
                    transport->publishLocalStateChanged();
                } catch (const std::exception& e) {
                    LOG(ERROR) << "ObservationService: publishLocalStateChanged failed for transport "
                               << transport->name() << ": " << e.what();
                } catch (...) {
                    LOG(ERROR) << "ObservationService: publishLocalStateChanged failed for transport "
                               << transport->name();
                }
            }
            break;

        case ModelEventKind::LocalAddressChanged:
            if (event.ifname) {
                for (const auto& transport : typedTransports) {
                    try {
                        transport->publishInterfaceChanged(*event.ifname);
                    } catch (const std::exception& e) {
                        LOG(ERROR) << "ObservationService: publishInterfaceChanged failed for transport "
                                   << transport->name() << ": " << e.what();
                    } catch (...) {
                        LOG(ERROR) << "ObservationService: publishInterfaceChanged failed for transport "
                                   << transport->name();
                    }
                }
            }
            for (const auto& transport : typedTransports) {
                try {
                    transport->publishLocalStateChanged();
                } catch (const std::exception& e) {
                    LOG(ERROR) << "ObservationService: publishLocalStateChanged failed for transport "
                               << transport->name() << ": " << e.what();
                } catch (...) {
                    LOG(ERROR) << "ObservationService: publishLocalStateChanged failed for transport "
                               << transport->name();
                }
            }
            break;

        case ModelEventKind::CandidateAdded:
        case ModelEventKind::CandidateUpdated:
        case ModelEventKind::CandidateConfirmed:
        case ModelEventKind::CandidateAged:
        case ModelEventKind::ClassificationChanged:
            if (event.mac) {
                for (const auto& transport : typedTransports) {
                    try {
                        transport->publishCandidateChanged(*event.mac);
                    } catch (const std::exception& e) {
                        LOG(ERROR) << "ObservationService: publishCandidateChanged failed for transport "
                                   << transport->name() << ": " << e.what();
                    } catch (...) {
                        LOG(ERROR) << "ObservationService: publishCandidateChanged failed for transport "
                                   << transport->name();
                    }
                }
            }
            break;

        case ModelEventKind::CandidateExpired:
        case ModelEventKind::CandidateRemoved:
            if (event.mac) {
                for (const auto& transport : typedTransports) {
                    try {
                        transport->publishCandidateRemoved(*event.mac);
                    } catch (const std::exception& e) {
                        LOG(ERROR) << "ObservationService: publishCandidateRemoved failed for transport "
                                   << transport->name() << ": " << e.what();
                    } catch (...) {
                        LOG(ERROR) << "ObservationService: publishCandidateRemoved failed for transport "
                                   << transport->name();
                    }
                }
            }
            break;
    }
}

LocalNetworkSnapshot ObservationService::localSnapshot() const
{
    return runtime_->localSnapshot();
}

std::optional<LocalInterfaceState> ObservationService::getInterface(const std::string& ifname) const
{
    auto snapshot = runtime_->localSnapshot();
    auto it = snapshot.interfaces.find(ifname);
    if (it != snapshot.interfaces.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<RemoteCandidate> ObservationService::remoteCandidates() const
{
    return runtime_->remoteCandidates();
}

std::optional<RemoteCandidate> ObservationService::getCandidateByMac(const std::string& mac) const
{
    return runtime_->findCandidateByMac(mac);
}

void ObservationService::stopOwnedState()
{
    if (agingThread_.joinable()) {
        agingThread_.request_stop();
        agingThread_.join();
    }
    runtime_->stop();
}

} // namespace RSCGroup
