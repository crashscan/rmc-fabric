#include "ObservationService.h"

#include <OperationalDiagnostics.h>
#include <network_observation/NetworkObservationContracts.hpp>

#include <glog/logging.h>

#include <map>
#include <stdexcept>

namespace RSCGroup {
namespace {

namespace contract = interop_contract::network_observation;

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

[[nodiscard]] std::string makeTransportIssueCode(const std::string& transportName)
{
    return "observation.transport." + diagnostics::sanitizeField(transportName) + ".publish.failed";
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

void ObservationService::addTransport(std::shared_ptr<IObservationTransport> transport)
{
    if (!transport) {
        throw std::invalid_argument("ObservationService::addTransport: transport is null");
    }

    std::scoped_lock lock(lifecycleMutex_);
    if (ServiceBase::isRunning()) {
        throw std::runtime_error("ObservationService::addTransport: cannot add transports after start");
    }

    ServiceBase::addTransport(std::move(transport));
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
        diagnostics::logError(name(), "runtime", "start", "runtime_start_failed", "runtime", "runtime start returned failure");
        ServiceBase::stop();
        return false;
    }

    refreshRuntimeIssues();
    ServiceBase::setReady(true);
    try {
        agingThread_ = std::jthread([this](std::stop_token st) {
            try {
                agingLoop(st);
            } catch (const std::exception& e) {
                reportIssue(std::string(contract::ISSUE_CODE_AGING_LOOP_STOPPED),
                            std::string(contract::SEVERITY_ERROR),
                            "worker.aging",
                            "age",
                            "worker_loop_failed",
                            "aging",
                            e.what());
            } catch (...) {
                reportIssue(std::string(contract::ISSUE_CODE_AGING_LOOP_STOPPED),
                            std::string(contract::SEVERITY_ERROR),
                            "worker.aging",
                            "age",
                            "worker_loop_failed",
                            "aging",
                            "unknown exception");
            }
        });
    } catch (const std::exception& e) {
        runtime_->stop();
        ServiceBase::stop();
        diagnostics::logError(name(), "worker.aging", "start", "worker_start_failed", "aging", e.what());
        return false;
    } catch (...) {
        runtime_->stop();
        ServiceBase::stop();
        diagnostics::logError(name(), "worker.aging", "start", "worker_start_failed", "aging", "unknown exception");
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
    {
        std::scoped_lock lock(issuesMutex_);
        issues_.clear();
    }
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
        refreshRuntimeIssues();
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
                        clearTransportPublishFailure(transport->name());
                    } catch (const std::exception& e) {
                        noteTransportPublishFailure(transport->name(), "publish_interface_changed", e.what());
                    } catch (...) {
                        noteTransportPublishFailure(transport->name(), "publish_interface_changed", "unknown exception");
                    }
                }
            }
            for (const auto& transport : typedTransports) {
                try {
                    transport->publishLocalStateChanged();
                    clearTransportPublishFailure(transport->name());
                } catch (const std::exception& e) {
                    noteTransportPublishFailure(transport->name(), "publish_local_state_changed", e.what());
                } catch (...) {
                    noteTransportPublishFailure(transport->name(), "publish_local_state_changed", "unknown exception");
                }
            }
            break;

        case ModelEventKind::LocalInterfaceRemoved:
            if (event.ifname) {
                for (const auto& transport : typedTransports) {
                    try {
                        transport->publishInterfaceRemoved(*event.ifname);
                        clearTransportPublishFailure(transport->name());
                    } catch (const std::exception& e) {
                        noteTransportPublishFailure(transport->name(), "publish_interface_removed", e.what());
                    } catch (...) {
                        noteTransportPublishFailure(transport->name(), "publish_interface_removed", "unknown exception");
                    }
                }
            }
            for (const auto& transport : typedTransports) {
                try {
                    transport->publishLocalStateChanged();
                    clearTransportPublishFailure(transport->name());
                } catch (const std::exception& e) {
                    noteTransportPublishFailure(transport->name(), "publish_local_state_changed", e.what());
                } catch (...) {
                    noteTransportPublishFailure(transport->name(), "publish_local_state_changed", "unknown exception");
                }
            }
            break;

        case ModelEventKind::LocalAddressChanged:
            if (event.ifname) {
                for (const auto& transport : typedTransports) {
                    try {
                        transport->publishInterfaceChanged(*event.ifname);
                        clearTransportPublishFailure(transport->name());
                    } catch (const std::exception& e) {
                        noteTransportPublishFailure(transport->name(), "publish_interface_changed", e.what());
                    } catch (...) {
                        noteTransportPublishFailure(transport->name(), "publish_interface_changed", "unknown exception");
                    }
                }
            }
            for (const auto& transport : typedTransports) {
                try {
                    transport->publishLocalStateChanged();
                    clearTransportPublishFailure(transport->name());
                } catch (const std::exception& e) {
                    noteTransportPublishFailure(transport->name(), "publish_local_state_changed", e.what());
                } catch (...) {
                    noteTransportPublishFailure(transport->name(), "publish_local_state_changed", "unknown exception");
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
                        clearTransportPublishFailure(transport->name());
                    } catch (const std::exception& e) {
                        noteTransportPublishFailure(transport->name(), "publish_candidate_changed", e.what());
                    } catch (...) {
                        noteTransportPublishFailure(transport->name(), "publish_candidate_changed", "unknown exception");
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
                        clearTransportPublishFailure(transport->name());
                    } catch (const std::exception& e) {
                        noteTransportPublishFailure(transport->name(), "publish_candidate_removed", e.what());
                    } catch (...) {
                        noteTransportPublishFailure(transport->name(), "publish_candidate_removed", "unknown exception");
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

std::string ObservationService::getPhase() const
{
    if (!ServiceBase::isRunning()) {
        return std::string(contract::PHASE_STOPPED);
    }
    return ServiceBase::isReady() ? std::string(contract::PHASE_LIVE)
                                  : std::string(contract::PHASE_INITIALIZING);
}

contract::ObservationIssues ObservationService::getIssues() const
{
    std::scoped_lock lock(issuesMutex_);
    return issues_;
}

void ObservationService::stopOwnedState()
{
    if (agingThread_.joinable()) {
        agingThread_.request_stop();
        agingThread_.join();
    }
    runtime_->stop();
}

void ObservationService::refreshRuntimeIssues()
{
    const auto health = runtime_->health();
    if (!health.running) {
        reportIssue(std::string(contract::ISSUE_CODE_RUNTIME_STOPPED),
                    std::string(contract::SEVERITY_ERROR),
                    "runtime",
                    "poll_health",
                    "runtime_stopped",
                    "runtime",
                    "runtime is not running");
    } else {
        clearIssue(std::string(contract::ISSUE_CODE_RUNTIME_STOPPED),
                   "runtime",
                   "runtime",
                   "runtime recovered");
    }

    if (!health.lldpAvailable) {
        reportIssue(std::string(contract::ISSUE_CODE_LLDP_UNAVAILABLE),
                    std::string(contract::SEVERITY_WARNING),
                    "input.lldp",
                    "poll_health",
                    "input_degraded",
                    "lldp",
                    "LLDP observer is unavailable");
    } else {
        clearIssue(std::string(contract::ISSUE_CODE_LLDP_UNAVAILABLE),
                   "input.lldp",
                   "lldp",
                   "LLDP observer recovered");
    }
}

void ObservationService::reportIssue(const std::string& issueCode,
                                     const std::string& severity,
                                     const std::string& component,
                                     const std::string& operation,
                                     const std::string& category,
                                     const std::string& identity,
                                     const std::string& message)
{
    contract::ObservationIssueFields fields;
    fields.emplace(std::string(contract::ISSUE_SEVERITY), diagnostics::sanitizeField(severity));
    fields.emplace(std::string(contract::ISSUE_COMPONENT), diagnostics::sanitizeField(component));
    fields.emplace(std::string(contract::ISSUE_OPERATION), diagnostics::sanitizeField(operation));
    fields.emplace(std::string(contract::ISSUE_CATEGORY), diagnostics::sanitizeField(category));
    fields.emplace(std::string(contract::ISSUE_IDENTITY), diagnostics::sanitizeField(identity));
    fields.emplace(std::string(contract::ISSUE_MESSAGE), diagnostics::sanitizeMessage(message));

    bool changed = false;
    {
        std::scoped_lock lock(issuesMutex_);
        auto it = issues_.find(issueCode);
        if (it == issues_.end() || it->second != fields) {
            issues_[issueCode] = fields;
            changed = true;
        }
    }

    if (changed) {
        diagnostics::logError(name(), component, operation, category, identity, message);
    }
}

void ObservationService::clearIssue(const std::string& issueCode,
                                    const std::string& component,
                                    const std::string& identity,
                                    const std::string& message)
{
    bool removed = false;
    {
        std::scoped_lock lock(issuesMutex_);
        removed = issues_.erase(issueCode) != 0;
    }

    if (removed) {
        diagnostics::logInfo(name(), component, "recover", "issue_cleared", identity, message);
    }
}

void ObservationService::noteTransportPublishFailure(const std::string& transportName,
                                                     const std::string& operation,
                                                     const std::string& message)
{
    reportIssue(makeTransportIssueCode(transportName),
                std::string(contract::SEVERITY_WARNING),
                "transport." + diagnostics::sanitizeField(transportName),
                operation,
                "transport_publish_failed",
                transportName,
                message);
}

void ObservationService::clearTransportPublishFailure(const std::string& transportName)
{
    clearIssue(makeTransportIssueCode(transportName),
               "transport." + diagnostics::sanitizeField(transportName),
               transportName,
               "transport publish recovered");
}

} // namespace RSCGroup
