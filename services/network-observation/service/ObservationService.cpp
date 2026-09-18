#include "ObservationService.h"

#include <OperationalDiagnostics.h>
#include <network_observation/NetworkObservationContracts.hpp>

#include <glog/logging.h>

#include <map>
#include <stdexcept>
#include <string_view>

namespace RSCGroup {
namespace {
    namespace contract = interop_contract::network_observation;

    [[nodiscard]] std::vector<std::shared_ptr<IObservationTransport> >
    observationTransports(const ServiceBase &service) {
        std::vector<std::shared_ptr<IObservationTransport> > typed;
        typed.reserve(service.transports().size());
        for (const auto &transport: service.transports()) {
            if (auto typedTransport = std::dynamic_pointer_cast<IObservationTransport>(transport)) {
                typed.push_back(std::move(typedTransport));
            }
        }
        return typed;
    }

    [[nodiscard]] std::string makeTransportIssueCode(const std::string &transportName,
                                                     const std::string &operation) {
        return "observation.transport." + diagnostics::sanitizeField(transportName)
               + "." + diagnostics::sanitizeField(operation) + ".failed";
    }
} // namespace

ObservationService::ObservationService(std::unique_ptr<IObservationRuntime> runtime,
                                       std::shared_ptr<IObservationTransport> transport,
                                       std::chrono::steady_clock::duration agingInterval
                                       )
    : ServiceBase("observation-service")
      , runtime_(std::move(runtime))
      , agingInterval_(agingInterval)
      , supervisionInterval_(agingInterval)
      , supervisionWorker_("observation-supervision",
                           [this](std::stop_token st) { supervisionLoop(std::move(st)); },
                           [this] {
                               std::scoped_lock lk(supervisionMutex_);
                               supervisionCv_.notify_all();
                           },
                           [this](const ManagedWorker::Exit &exit) { onSupervisionWorkerExit(exit); })
      , agingWorker_("observation-aging",
                     [this](std::stop_token st) { agingLoop(std::move(st)); },
                     [this] {
                         std::scoped_lock agingLock(agingMutex_);
                         agingCv_.notify_all();
                     },
                     [this](const ManagedWorker::Exit &exit) { onAgingWorkerExit(exit); })
      , publicationWorker_("observation-publication",
                           [this](std::stop_token st) { publicationLoop(std::move(st)); },
                           // Wake, not close: close() is one-way and would
                           // leave the queue dead across a restart.
                           [this] { publicationQueue_.wake(); },
                           [this](const ManagedWorker::Exit &exit) { onPublicationWorkerExit(exit); }){
    if (!runtime_) {
        throw std::invalid_argument("ObservationService: runtime is null");
    }
    if (!transport) {
        throw std::invalid_argument("ObservationService: transport is null");
    }
    ServiceBase::addTransport(std::move(transport));
}

ObservationService::~ObservationService() {
    // Destructors must not throw.  stop() is structurally non-throwing, but
    // the guard makes that explicit at the destruction boundary.
    try {
        stop();
    } catch (...) {
        diagnostics::logError(name(), "service.lifecycle", "destroy", "service_stop_failed", "observation-service",
                              "stop() threw during destruction");
    }
}

void ObservationService::addTransport(std::shared_ptr<IObservationTransport> transport) {
    if (!transport) {
        throw std::invalid_argument("ObservationService::addTransport: transport is null");
    }

    if (ServiceBase::isRunning()) {
        throw std::runtime_error("ObservationService::addTransport: cannot add transports after start");
    }

    ServiceBase::addTransport(std::move(transport));
}

void ObservationService::validateConfiguration() {
}

bool ObservationService::initializeComponents() {
    runtime_->setEventSink(this);
    for (const auto &transport: observationTransports(*this)) {
        transport->bindQueryService(*this);
    }
    return true;
}

void ObservationService::onStartFailedcleanUp() {
    if(supervisionWorker_.isRunning()) {
        supervisionWorker_.stop();
    }
    if (agingWorker_.isRunning()) {
        agingWorker_.stop();
    }
    if (runtime_->isRunning()) {
        runtime_->stop();
    }
    publicationQueue_.close();
    if (publicationWorker_.isRunning()) {
        publicationWorker_.stop();
    }
    ServiceBase::stop();
}

bool ObservationService::start() {
    auto transition = lifecycle_.beginStart();
    if (!transition) {
        // beginStart() returns an unowned transition only when the epoch was
        // observed running under the coordinator lock.  Unlike inventory,
        // observation treats an aging-worker crash as degradation, so a
        // repeated start() is an idempotent no-op.
        return true;
    }

    if (!ServiceBase::start()) {
        transition.fail();
        return false;
    }
    publicationQueue_.reopen();
    if (!publicationWorker_.start()) {
        diagnostics::logError(name(), "worker.publication", "start", "worker_start_failed", "publication",
                      "worker start returned failure");
        ServiceBase::stop();
        transition.fail();
        return false;
    }
    if (!runtime_->start()) {
        diagnostics::logError(name(), "runtime", "start", "runtime_start_failed", "runtime",
                              "runtime start returned failure");
        onStartFailedcleanUp();
        transition.fail();
        return false;
    }

    refreshRuntimeIssues();
    ServiceBase::setReady(true);
    try {
        (void) agingWorker_.start();
    } catch (const std::exception &e) {
        diagnostics::logError(name(), "worker.aging", "start", "worker_start_failed", "aging", e.what());
        onStartFailedcleanUp();
        transition.fail();
        return false;
    } catch (...) {
        diagnostics::logError(name(), "worker.aging", "start", "worker_start_failed", "aging", "unknown exception");
        onStartFailedcleanUp();
        transition.fail();
        return false;
    }
    try {
        (void) supervisionWorker_.start();
    } catch (const std::exception &e) {
        diagnostics::logError(name(), "worker.supervision", "start", "worker_start_failed", "supervision", e.what());
        onStartFailedcleanUp();
        transition.fail();
        return false;
    } catch (...) {
        diagnostics::logError(name(), "worker.supervision", "start", "worker_start_failed", "supervision",
                              "unknown exception");
        transition.fail();
        return false;
    }
    transition.complete();
    return true;
}

void ObservationService::stop() {
    // Self-stop is rejected *before* shutdown is claimed.  There is no detach
    // path: a detached worker capturing `this` would open a use-after-free
    // window and break the producer-drain guarantee.
    if (agingWorker_.isCurrentThread() || supervisionWorker_.isCurrentThread()) {
        diagnostics::logError(name(), "worker", "stop", "self_stop_rejected", "worker",
                              "stop() called from a service worker thread; request shutdown externally");
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

    // Step 2: Join both workers — supervision FIRST: LldpdSource's
    // refreshAll()/stop() state hand-off relies on tick() being quiescent
    // before runtime_->stop() (M3 invariant). Both joins are bounded:
    // tick() never blocks longer than the probe timeouts.
    supervisionWorker_.stop();
    agingWorker_.stop();

    // Step 3: Stop the runtime (netlink join → LLDP callback drain → sink
    // detach).  Failures are isolated so they cannot leave the lifecycle
    // coordinator stuck in a transitional state.
    try {
        runtime_->stop();
        // Step 3a: close the queue. Already-marked publications remain takeable,
        // so terminal removals still reach the bus.
        publicationQueue_.close();

        // Step 3b: join the publication worker. Bounded by however long the
        // transports take on one final generation.
        publicationWorker_.stop();
    } catch (const std::exception &e) {
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

void ObservationService::onAgingWorkerExit(const ManagedWorker::Exit &exit) {
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
    } catch (const std::exception &e) {
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

void ObservationService::onSupervisionWorkerExit(const ManagedWorker::Exit &exit) {
    // Same degradation policy as the aging worker: surface as an issue,
    // never drive lifecycle or readiness.
    if (exit.reason != ManagedWorker::ExitReason::exception) {
        return;
    }

    std::string detail = "unknown exception";
    try {
        if (exit.exception) {
            std::rethrow_exception(exit.exception);
        }
    } catch (const std::exception &e) {
        detail = e.what();
    } catch (...) {
    }

    reportIssue(std::string(contract::ISSUE_CODE_SUPERVISION_LOOP_STOPPED),
                std::string(contract::SEVERITY_ERROR),
                "worker.supervision",
                "tick",
                "worker_loop_failed",
                "supervision",
                detail);
}

void ObservationService::agingLoop(std::stop_token st) {
    std::unique_lock lk(agingMutex_);
    while (!st.stop_requested()) {
        if (agingCv_.wait_for(lk, st, agingInterval_, [&] { return st.stop_requested(); })) {
            break;
        }
        // Body runs outside agingMutex_: the workers' wake callback takes
        // the mutex to notify agingCv_.
        lk.unlock();
        runtime_->age(std::chrono::steady_clock::now());
        // Diagnostics belong on the loop that always ticks: supervision
        // can be parked in a bounded probe or a reconnect.
        refreshRuntimeIssues();
        lk.lock();
    }
}

// Supervision (LLDP retry / probe / keepalive driving) on its own worker so
// a slow lldpd probe can never starve aging.  tick() remains bounded by the
// BoundedLldpConnection timeouts plus, on a failed probe, the reconnect
// drain bound (kRefreshDrainTimeout) — worst case ~6s per cycle.
void ObservationService::supervisionLoop(std::stop_token st) {
    std::unique_lock lk(supervisionMutex_);
    while (!st.stop_requested()) {
        if (supervisionCv_.wait_for(lk, st, supervisionInterval_, [&] { return st.stop_requested(); })) {
            break;
        }
        // Body outside the mutex: the wake callback takes it to notify.
        lk.unlock();
        runtime_->tick(std::chrono::steady_clock::now());
        lk.lock();
    }
}

// Runs on a producer thread (netlink monitor, or the LLDP watch thread via
// the engine). Must stay cheap and non-blocking: this is the boundary that
// previously let a stalled D-Bus transport back-pressure into netlink
// parsing. Marking is a map insert under a short mutex; all transport I/O
// happens on the publication worker.
void ObservationService::onModelEvent(const ModelEvent &event) {
    switch (event.kind) {
        case ModelEventKind::LocalInterfaceChanged:
        case ModelEventKind::LocalAddressChanged:
            if (event.ifname) {
                publicationQueue_.markInterfaceChanged(*event.ifname);
            }
            publicationQueue_.markLocalStateChanged();
            break;

        case ModelEventKind::LocalInterfaceRemoved:
            if (event.ifname) {
                publicationQueue_.markInterfaceRemoved(*event.ifname);
            }
            publicationQueue_.markLocalStateChanged();
            break;

        case ModelEventKind::CandidateAdded:
        case ModelEventKind::CandidateUpdated:
        case ModelEventKind::CandidateConfirmed:
        case ModelEventKind::CandidateAged:
        case ModelEventKind::ClassificationChanged:
            if (event.mac) {
                publicationQueue_.markCandidateChanged(*event.mac);
            }
            break;

        case ModelEventKind::CandidateExpired:
        case ModelEventKind::CandidateRemoved:
            if (event.mac) {
                publicationQueue_.markCandidateRemoved(*event.mac);
            }
            break;
    }
}

void ObservationService::publicationLoop(std::stop_token st) {
    // waitAndTake still drains a pending generation when stop is requested —
    // it returns nullopt only when the set is empty. So a stop during
    // shutdown does not strand already-marked publications.
    while (auto pending = publicationQueue_.waitAndTake(st)) {
        publishBatch(*pending);
    }
}

void ObservationService::publishBatch(const PendingPublication &pending) {
    dispatchPublication(
        pending,
        [this](const std::string &mac, PublicationIntent intent) {
            if (intent == PublicationIntent::Changed) {
                publishToAll("publish_candidate_changed",
                             [&](IObservationTransport &t) { t.publishCandidateChanged(mac); });
            } else {
                publishToAll("publish_candidate_removed",
                             [&](IObservationTransport &t) { t.publishCandidateRemoved(mac); });
            }
        },
        [this](const std::string &ifname, PublicationIntent intent) {
            if (intent == PublicationIntent::Changed) {
                publishToAll("publish_interface_changed",
                             [&](IObservationTransport &t) { t.publishInterfaceChanged(ifname); });
            } else {
                publishToAll("publish_interface_removed",
                             [&](IObservationTransport &t) { t.publishInterfaceRemoved(ifname); });
            }
        },
        [this] {
            publishToAll("publish_local_state_changed",
                         [](IObservationTransport &t) { t.publishLocalStateChanged(); });
        });
}

template <typename Publish>
void ObservationService::publishToAll(std::string_view operation, Publish &&publish) {
    const auto op = std::string(operation);
    for (const auto &transport : observationTransports(*this)) {
        try {
            publish(*transport);
            clearTransportPublishFailure(transport->name(), op);
        } catch (const std::exception &e) {
            noteTransportPublishFailure(transport->name(), op, e.what());
        } catch (...) {
            noteTransportPublishFailure(transport->name(), op, "unknown exception");
        }
    }
}

LocalNetworkSnapshot ObservationService::localSnapshot() const {
    return runtime_->localSnapshot();
}

std::optional<LocalInterfaceState> ObservationService::getInterface(const std::string &ifname) const {
    auto snapshot = runtime_->localSnapshot();
    auto it = snapshot.interfaces.find(ifname);
    if (it != snapshot.interfaces.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<RemoteCandidate> ObservationService::remoteCandidates() const {
    return runtime_->remoteCandidates();
}

std::optional<RemoteCandidate> ObservationService::getCandidateByMac(const std::string &mac) const {
    return runtime_->findCandidateByMac(mac);
}

std::string ObservationService::getPhase() const {
    if (!ServiceBase::isRunning()) {
        return std::string(contract::PHASE_STOPPED);
    }
    return ServiceBase::isReady()
               ? std::string(contract::PHASE_LIVE)
               : std::string(contract::PHASE_INITIALIZING);
}

contract::ObservationIssues ObservationService::getIssues() const {
    std::scoped_lock lock(issuesMutex_);
    return issues_;
}

void ObservationService::refreshRuntimeIssues() {
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

void ObservationService::reportIssue(const std::string &issueCode,
                                     const std::string &severity,
                                     const std::string &component,
                                     const std::string &operation,
                                     const std::string &category,
                                     const std::string &identity,
                                     const std::string &message) {
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

void ObservationService::clearIssue(const std::string &issueCode,
                                    const std::string &component,
                                    const std::string &identity,
                                    const std::string &message) {
    bool removed = false;
    {
        std::scoped_lock lock(issuesMutex_);
        removed = issues_.erase(issueCode) != 0;
    }

    if (removed) {
        diagnostics::logInfo(name(), component, "recover", "issue_cleared", identity, message);
    }
}

void ObservationService::noteTransportPublishFailure(const std::string &transportName,
                                                     const std::string &operation,
                                                     const std::string &message) {
    reportIssue(makeTransportIssueCode(transportName, operation),
                std::string(contract::SEVERITY_WARNING),
                "transport." + diagnostics::sanitizeField(transportName),
                operation,
                "transport_publish_failed",
                transportName,
                message);
}

void ObservationService::clearTransportPublishFailure(const std::string &transportName, const std::string &operation) {
    clearIssue(makeTransportIssueCode(transportName, operation),
               "transport." + diagnostics::sanitizeField(transportName),
               transportName,
               "transport publish recovered");
}
} // namespace RSCGroup
