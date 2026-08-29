#include "ServiceBase.h"
#include "ITransport.h"

#include <glog/logging.h>

namespace RSCGroup {

ServiceBase::ServiceBase(const std::string& serviceName)
    : serviceName_(serviceName)
{}

ServiceBase::~ServiceBase() = default;

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
        transport->publishReadyChanged(ready_);
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

    if (!initializeComponents()) {
        LOG(ERROR) << serviceName_ << ": initializeComponents() failed";
        transports_.clear();
        return false;
    }

    // Start transports in registration order; roll back on failure.
    std::size_t started = 0;
    for (auto& transport : transports_) {
        LOG(INFO) << serviceName_ << ": starting transport " << transport->name();
        if (!transport->start()) {
            LOG(ERROR) << serviceName_ << ": transport " << transport->name() << " failed to start";
            // Roll back already-started transports in reverse order.
            for (std::size_t i = started; i > 0; --i) {
                transports_[i - 1]->stop();
            }
            return false;
        }
        ++started;
    }

    running_ = true;
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

    // Stop transports in reverse registration order.
    for (auto it = transports_.rbegin(); it != transports_.rend(); ++it) {
        LOG(INFO) << serviceName_ << ": stopping transport " << (*it)->name();
        (*it)->stop();
    }

    running_ = false;
    LOG(INFO) << serviceName_ << ": stopped";
}

} // namespace RSCGroup
