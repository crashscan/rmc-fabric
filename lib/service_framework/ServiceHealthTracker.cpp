#include "ServiceHealthTracker.h"

#include <algorithm>

namespace RSCGroup {

void ServiceHealthTracker::report(const std::string& component,
                                  HealthStatus       status,
                                  const std::string& message)
{
    ComponentHealth entry{component, status, message};

    std::vector<Listener> snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        records_[component] = entry;
        snapshot = listeners_;
    }

    for (const auto& listener : snapshot) {
        listener(entry);
    }
}

ComponentHealth ServiceHealthTracker::getHealth(const std::string& component) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = records_.find(component);
    if (it == records_.end()) {
        return ComponentHealth{component, HealthStatus::Unknown, {}};
    }
    return it->second;
}

HealthStatus ServiceHealthTracker::overallStatus() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    HealthStatus worst = HealthStatus::Ok;
    for (const auto& [name, entry] : records_) {
        if (entry.status == HealthStatus::Failed) {
            return HealthStatus::Failed; // can't get worse
        }
        if (entry.status == HealthStatus::Degraded) {
            worst = HealthStatus::Degraded;
        } else if (entry.status == HealthStatus::Unknown &&
                   worst == HealthStatus::Ok) {
            worst = HealthStatus::Unknown;
        }
    }
    return worst;
}

void ServiceHealthTracker::addListener(Listener listener)
{
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_.push_back(std::move(listener));
}

void ServiceHealthTracker::clearListeners()
{
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_.clear();
}

} // namespace RSCGroup
