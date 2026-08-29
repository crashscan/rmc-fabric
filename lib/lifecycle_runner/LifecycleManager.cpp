#include "LifecycleManager.h"

#include <glog/logging.h>

#include <stdexcept>

namespace RSCGroup {

void LifecycleManager::add(std::string name, std::shared_ptr<Startable> component)
{
    if (!component) {
        throw std::invalid_argument("LifecycleManager::add: component is null");
    }
    if (running_) {
        throw std::logic_error("LifecycleManager::add: cannot add components after start()");
    }
    components_.push_back({std::move(name), std::move(component)});
}

bool LifecycleManager::start()
{
    if (running_) {
        return true;
    }

    std::size_t startedCount = 0;
    for (auto& entry : components_) {
        LOG(INFO) << "LifecycleManager: starting '" << entry.name << "'";
        bool ok = false;
        try {
            ok = entry.component->start();
        } catch (const std::exception& e) {
            LOG(ERROR) << "LifecycleManager: component '" << entry.name
                       << "' threw on start: " << e.what();
        } catch (...) {
            LOG(ERROR) << "LifecycleManager: component '" << entry.name
                       << "' threw unknown exception on start";
        }

        if (!ok) {
            LOG(ERROR) << "LifecycleManager: component '" << entry.name
                       << "' failed to start; rolling back";
            // Roll back already-started components in reverse order.
            for (std::size_t i = startedCount; i > 0; --i) {
                auto& prev = components_[i - 1];
                LOG(INFO) << "LifecycleManager: rollback stop '" << prev.name << "'";
                try {
                    prev.component->stop();
                } catch (const std::exception& e) {
                    LOG(ERROR) << "LifecycleManager: rollback stop '" << prev.name
                               << "' threw: " << e.what();
                } catch (...) {
                    LOG(ERROR) << "LifecycleManager: rollback stop '" << prev.name
                               << "' threw unknown exception";
                }
            }
            return false;
        }
        ++startedCount;
    }

    running_ = true;
    return true;
}

void LifecycleManager::stop()
{
    if (!running_) {
        return;
    }
    running_ = false;

    for (auto it = components_.rbegin(); it != components_.rend(); ++it) {
        LOG(INFO) << "LifecycleManager: stopping '" << it->name << "'";
        try {
            it->component->stop();
        } catch (const std::exception& e) {
            LOG(ERROR) << "LifecycleManager: stop '" << it->name
                       << "' threw: " << e.what();
        } catch (...) {
            LOG(ERROR) << "LifecycleManager: stop '" << it->name
                       << "' threw unknown exception";
        }
    }
}

} // namespace RSCGroup
