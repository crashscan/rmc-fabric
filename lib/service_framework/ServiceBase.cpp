#include "ServiceBase.h"
#include "ITransport.h"
#include "OperationalDiagnostics.h"

#include <glog/logging.h>

namespace RSCGroup {

ServiceBase::ServiceBase(const std::string& serviceName)
    : serviceName_(serviceName)
{}

ServiceBase::~ServiceBase()
{
    stop();
}

std::string ServiceBase::name() const
{
    return serviceName_;
}

bool ServiceBase::isReady() const
{
    return ready_;
}

bool ServiceBase::isRunning() const
{
    return running_;
}

void ServiceBase::addTransport(std::shared_ptr<IServiceTransport> transport)
{
    transports_.push_back(std::move(transport));
}

const std::vector<std::shared_ptr<IServiceTransport>>& ServiceBase::transports() const
{
    return transports_;
}

void ServiceBase::setReady(bool ready)
{
    if (ready_.exchange(ready) == ready) {
        return;
    }
    for (auto& transport : transports_) {
        try {
            transport->publishReadyChanged(ready);
        } catch (const std::exception& e) {
            diagnostics::logError(serviceName_,
                                  "transport." + diagnostics::sanitizeField(transport->name()),
                                  "publish_ready_changed",
                                  "transport_publish_failed",
                                  transport->name(),
                                  e.what());
        } catch (...) {
            diagnostics::logError(serviceName_,
                                  "transport." + diagnostics::sanitizeField(transport->name()),
                                  "publish_ready_changed",
                                  "transport_publish_failed",
                                  transport->name(),
                                  "unknown exception");
        }
    }
}

bool ServiceBase::start()
{
    if (running_) {
        LOG(WARNING) << diagnostics::formatError(serviceName_,
                                                 "service.lifecycle",
                                                 "start",
                                                 "already_running",
                                                 serviceName_,
                                                 "start() called while already running");
        return true;
    }

    try {
        validateConfiguration();
    } catch (const std::exception& e) {
        diagnostics::logError(serviceName_, "service.configuration", "validate", "validation_failed", serviceName_, e.what());
        return false;
    }

    try {
        if (!initializeComponents()) {
            diagnostics::logError(serviceName_, "service.lifecycle", "initialize_components", "initialization_failed", serviceName_, "initializeComponents() failed");
            return false;
        }
    } catch (const std::exception& e) {
        diagnostics::logError(serviceName_, "service.lifecycle", "initialize_components", "initialization_failed", serviceName_, e.what());
        return false;
    } catch (...) {
        diagnostics::logError(serviceName_, "service.lifecycle", "initialize_components", "initialization_failed", serviceName_, "unknown exception");
        return false;
    }

    // Start transports in registration order; roll back on failure.
    std::size_t started = 0;
    for (auto& transport : transports_) {
        diagnostics::logInfo(serviceName_,
                             "transport." + diagnostics::sanitizeField(transport->name()),
                             "start",
                             "transport_lifecycle",
                             transport->name(),
                             "starting transport");
        try {
            if (transport->start()) {
                ++started;
                continue;
            }
        } catch (const std::exception& e) {
            diagnostics::logError(serviceName_,
                                  "transport." + diagnostics::sanitizeField(transport->name()),
                                  "start",
                                  "transport_start_failed",
                                  transport->name(),
                                  e.what());
            rollbackStartedTransports(started, transport.get());
            return false;
        } catch (...) {
            diagnostics::logError(serviceName_,
                                  "transport." + diagnostics::sanitizeField(transport->name()),
                                  "start",
                                  "transport_start_failed",
                                  transport->name(),
                                  "unknown exception");
            rollbackStartedTransports(started, transport.get());
            return false;
        }

        diagnostics::logError(serviceName_,
                              "transport." + diagnostics::sanitizeField(transport->name()),
                              "start",
                              "transport_start_failed",
                              transport->name(),
                              "transport returned failure");
        rollbackStartedTransports(started, transport.get());
        return false;
    }

    running_ = true;
    ready_ = false;
    diagnostics::logInfo(serviceName_, "service.lifecycle", "start", "service_started", serviceName_, "started");
    return true;
}

void ServiceBase::stop()
{
    if (!running_) {
        return;
    }

    if (ready_) {
        setReady(false);
    }

    stopAllTransports();

    running_ = false;
    diagnostics::logInfo(serviceName_, "service.lifecycle", "stop", "service_stopped", serviceName_, "stopped");
}

void ServiceBase::rollbackStartedTransports(std::size_t startedCount, IServiceTransport* currentTransport) noexcept
{
    if (currentTransport) {
        try {
            diagnostics::logInfo(serviceName_,
                                 "transport." + diagnostics::sanitizeField(currentTransport->name()),
                                 "rollback_stop",
                                 "transport_lifecycle",
                                 currentTransport->name(),
                                 "rolling back transport");
            currentTransport->stop();
        } catch (const std::exception& e) {
            diagnostics::logError(serviceName_,
                                  "transport." + diagnostics::sanitizeField(currentTransport->name()),
                                  "rollback_stop",
                                  "transport_stop_failed",
                                  currentTransport->name(),
                                  e.what());
        } catch (...) {
            diagnostics::logError(serviceName_,
                                  "transport." + diagnostics::sanitizeField(currentTransport->name()),
                                  "rollback_stop",
                                  "transport_stop_failed",
                                  currentTransport->name(),
                                  "unknown exception");
        }
    }

    for (std::size_t i = startedCount; i > 0; --i) {
        auto& transport = transports_[i - 1];
        try {
            diagnostics::logInfo(serviceName_,
                                 "transport." + diagnostics::sanitizeField(transport->name()),
                                 "rollback_stop",
                                 "transport_lifecycle",
                                 transport->name(),
                                 "rolling back transport");
            transport->stop();
        } catch (const std::exception& e) {
            diagnostics::logError(serviceName_,
                                  "transport." + diagnostics::sanitizeField(transport->name()),
                                  "rollback_stop",
                                  "transport_stop_failed",
                                  transport->name(),
                                  e.what());
        } catch (...) {
            diagnostics::logError(serviceName_,
                                  "transport." + diagnostics::sanitizeField(transport->name()),
                                  "rollback_stop",
                                  "transport_stop_failed",
                                  transport->name(),
                                  "unknown exception");
        }
    }
}

void ServiceBase::stopAllTransports() noexcept
{
    for (auto it = transports_.rbegin(); it != transports_.rend(); ++it) {
        try {
            diagnostics::logInfo(serviceName_,
                                 "transport." + diagnostics::sanitizeField((*it)->name()),
                                 "stop",
                                 "transport_lifecycle",
                                 (*it)->name(),
                                 "stopping transport");
            (*it)->stop();
        } catch (const std::exception& e) {
            diagnostics::logError(serviceName_,
                                  "transport." + diagnostics::sanitizeField((*it)->name()),
                                  "stop",
                                  "transport_stop_failed",
                                  (*it)->name(),
                                  e.what());
        } catch (...) {
            diagnostics::logError(serviceName_,
                                  "transport." + diagnostics::sanitizeField((*it)->name()),
                                  "stop",
                                  "transport_stop_failed",
                                  (*it)->name(),
                                  "unknown exception");
        }
    }
}

} // namespace RSCGroup
