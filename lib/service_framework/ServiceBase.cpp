#include "ServiceBase.h"
#include "ITransport.h"

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
    if (ready_ == ready) {
        return;
    }
    ready_ = ready;
    for (auto& transport : transports_) {
        try {
            transport->publishReadyChanged(ready_);
        } catch (const std::exception& e) {
            LOG(ERROR) << serviceName_ << ": publishReadyChanged(" << ready_
                       << ") failed for transport " << transport->name() << ": " << e.what();
        } catch (...) {
            LOG(ERROR) << serviceName_ << ": publishReadyChanged(" << ready_
                       << ") failed for transport " << transport->name() << ": unknown exception";
        }
    }
}

bool ServiceBase::start()
{
    if (running_) {
        LOG(WARNING) << serviceName_ << ": start() called while already running";
        return true;
    }

    try {
        validateConfiguration();
    } catch (const std::exception& e) {
        LOG(ERROR) << serviceName_ << ": configuration validation failed: " << e.what();
        return false;
    }

    try {
        if (!initializeComponents()) {
            LOG(ERROR) << serviceName_ << ": initializeComponents() failed";
            return false;
        }
    } catch (const std::exception& e) {
        LOG(ERROR) << serviceName_ << ": initializeComponents() threw: " << e.what();
        return false;
    } catch (...) {
        LOG(ERROR) << serviceName_ << ": initializeComponents() threw an unknown exception";
        return false;
    }

    // Start transports in registration order; roll back on failure.
    std::size_t started = 0;
    for (auto& transport : transports_) {
        LOG(INFO) << serviceName_ << ": starting transport " << transport->name();
        try {
            if (transport->start()) {
                ++started;
                continue;
            }
        } catch (const std::exception& e) {
            LOG(ERROR) << serviceName_ << ": transport " << transport->name()
                       << " threw during start(): " << e.what();
            rollbackStartedTransports(started, transport.get());
            return false;
        } catch (...) {
            LOG(ERROR) << serviceName_ << ": transport " << transport->name()
                       << " threw during start(): unknown exception";
            rollbackStartedTransports(started, transport.get());
            return false;
        }

        LOG(ERROR) << serviceName_ << ": transport " << transport->name() << " failed to start";
        rollbackStartedTransports(started, transport.get());
        return false;
    }

    running_ = true;
    ready_ = false;
    LOG(INFO) << serviceName_ << ": started";
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
    LOG(INFO) << serviceName_ << ": stopped";
}

void ServiceBase::rollbackStartedTransports(std::size_t startedCount, IServiceTransport* currentTransport) noexcept
{
    if (currentTransport) {
        try {
            LOG(INFO) << serviceName_ << ": rolling back transport " << currentTransport->name();
            currentTransport->stop();
        } catch (const std::exception& e) {
            LOG(ERROR) << serviceName_ << ": rollback stop failed for transport "
                       << currentTransport->name() << ": " << e.what();
        } catch (...) {
            LOG(ERROR) << serviceName_ << ": rollback stop failed for transport "
                       << currentTransport->name() << ": unknown exception";
        }
    }

    for (std::size_t i = startedCount; i > 0; --i) {
        auto& transport = transports_[i - 1];
        try {
            LOG(INFO) << serviceName_ << ": rolling back transport " << transport->name();
            transport->stop();
        } catch (const std::exception& e) {
            LOG(ERROR) << serviceName_ << ": rollback stop failed for transport "
                       << transport->name() << ": " << e.what();
        } catch (...) {
            LOG(ERROR) << serviceName_ << ": rollback stop failed for transport "
                       << transport->name() << ": unknown exception";
        }
    }
}

void ServiceBase::stopAllTransports() noexcept
{
    for (auto it = transports_.rbegin(); it != transports_.rend(); ++it) {
        try {
            LOG(INFO) << serviceName_ << ": stopping transport " << (*it)->name();
            (*it)->stop();
        } catch (const std::exception& e) {
            LOG(ERROR) << serviceName_ << ": stop() failed for transport "
                       << (*it)->name() << ": " << e.what();
        } catch (...) {
            LOG(ERROR) << serviceName_ << ": stop() failed for transport "
                       << (*it)->name() << ": unknown exception";
        }
    }
}

} // namespace RSCGroup
