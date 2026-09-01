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

[[nodiscard]] std::string makeTransportIssueCode(const std::string& transportName,
                                                   const std::string& operation)
{
    return "observation.transport." + diagnostics::sanitizeField(transportName)
           + "." + diagnostics::sanitizeField(operation) + ".failed";
}

} // namespace

ObservationService::ObservationService(std::unique_ptr<IObservationRuntime> runtime,
                                       std::shared_ptr<IObservationTransport> transport,
                                       std::chrono::steady_clock::duration agingInterval)
    : ServiceBase("observation-service")
    , runtime_(std::move(runtime))
    , agingInterval_(agingInterval)
    , agingWorker_("observation-aging",
                   [this](std::stop_token st) { agingLoop(std::move(st)); },
                   [this] {
                       std::scoped_lock agingLock(agingMutex_);
                       agingCv_.notify_all();
                   },
                   [this](const ManagedWorker::Exit& exit) { onAgingWorkerExit(exit); })
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
    // Destructors must not throw.  stop() is structurally non-throwing, but
    // the guard makes that explicit at the destruction boundary.
    try {
        stop();
    } catch (...) {
        diagnostics::logError(name(), "service.lifecycle", "destroy", "service_stop_failed", "observation-service", "stop() threw during destruction");
    }
}

void ObservationService::addTransport(std::shared_ptr<IObservationTransport> transport)
{
    if (!transport) {
        throw std::invalid_argument("ObservationService::addTransport: transport is null");
    }

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
    auto transition = lifecycle_.beginStart();
    if (!transition) {
        return lifecycle_.isRunning();
    }

    if (!ServiceBase::start()) {
        transition.fail();
        return false;
    }
    if (!runtime_->start()) {
        diagnostics::logError(name(), "runtime", "start", "runtime_start_failed", "runtime", "runtime start returned failure");
        ServiceBase::stop();
        transition.fail();
        return false;
    }

    refreshRuntimeIssues();
    ServiceBase::setReady(true);
    try {
        (void)agingWorker_.start();
    } catch (const std::exception& e) {
        runtime_->stop();
        ServiceBase::stop();
        diagnostics::logError(name(), "worker.aging", "start", "worker_start_failed", "aging", e.what());
        transition.fail();
        return false;
    } catch (...) {
        runtime_->stop();
        ServiceBase::stop();
        diagnostics::logError(name(), "worker.aging", "start", "worker_start_failed", "aging", "unknown exception");
        transition.fail();
        return false;
    }

    transition.complete();
    return true;
}

void ObservationService::stop()
{
    // Self-stop is rejected *before* shutdown is claimed.  There is no detach
    // path: a detached worker capturing `this` would open a use-after-free
    // window and break the producer-drain guarantee.
    if (agingWorker_.isCurrentThread()) {
        diagnostics::logError(name(), "worker.aging", "stop", "self_stop_rejected", "aging", "stop() called from the aging worker thread; request shutdown externally");
        return;
    }

    // Claim shutdown.  A concurrent stop() waits here and returns only once
    // the active teardown has completed.
    auto transition = lifecycle_.beginStop();
    if (!transition) {
        return;
    }

    // Step 1: Quiesce D-Bus query admission — snapshot/query calls drain.
    // Structural, local, noexcept.
    quiesceQueriesOnTransports();

    // Step 2: Request stop, notify the aging condition variable, and join.
    agingWorker_.stop();

    // Step 3: Stop the runtime (netlink join → LLDP callback drain → sink
    // detach).  Failures are isolated so they cannot leave the lifecycle
    // coordinator stuck in a transitional state.
    try {
        runtime_->stop();
    } catch (const std::exception& e) {
        diagnostics::logError(name(), "runtime", "stop", "runtime_stop_failed", "runtime", e.what());
    } catch (...) {
        diagnostics::logError(name(), "runtime", "stop", "runtime_stop_failed", "runtime", "unknown exception");
    }

    // Step 4: Clear runtime issue state.
    {
        std::scoped_lock lock(issuesMutex_);
        issues_.clear();
    }

    // Step 5: Emit terminal ReadyChanged(false) and stop transports in
    // reverse registration order.
    ServiceBase::stop();

    transition.complete();
}

void ObservationService::onAgingWorkerExit(const ManagedWorker::Exit& exit)
{
    // Runs on the worker thread after the worker state has been finalized.
    // Aging-worker failure is observation-owned degradation policy: it is
    // surfaced as an issue and never changes readiness or drives lifecycle.
    if (exit.reason != ManagedWorker::ExitReason::exception) {
        return;
    }

    std::string detail = "unknown exception";
    try {
        if (exit.exception) {
            std::rethrow_exception(exit.exception);
        }
    } catch (const std::exception& e) {
        detail = e.what();
    } catch (...) {
    }

    reportIssue(std::string(contract::ISSUE_CODE_AGING_LOOP_STOPPED),
                std::string(contract::SEVERITY_ERROR),
                "worker.aging",
                "age",
                "worker_loop_failed",
                "aging",
                detail);
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
                        clearTransportPublishFailure(transport->name(), "publish_interface_changed");
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
                    clearTransportPublishFailure(transport->name(), "publish_local_state_changed");
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
                        clearTransportPublishFailure(transport->name(), "publish_interface_removed");
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
                    clearTransportPublishFailure(transport->name(), "publish_local_state_changed");
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
                        clearTransportPublishFailure(transport->name(), "publish_interface_changed");
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
                    clearTransportPublishFailure(transport->name(), "publish_local_state_changed");
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
                        clearTransportPublishFailure(transport->name(), "publish_candidate_changed");
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
                        clearTransportPublishFailure(transport->name(), "publish_candidate_removed");
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
    reportIssue(makeTransportIssueCode(transportName, operation),
                std::string(contract::SEVERITY_WARNING),
                "transport." + diagnostics::sanitizeField(transportName),
                operation,
                "transport_publish_failed",
                transportName,
                message);
}

void ObservationService::clearTransportPublishFailure(const std::string& transportName,
                                                       const std::string& operation)
{
    clearIssue(makeTransportIssueCode(transportName, operation),
               "transport." + diagnostics::sanitizeField(transportName),
               transportName,
               "transport publish recovered");
}

} // namespace RSCGroup
