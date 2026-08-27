//
// Created by vvass on 24-Jul-26.
//
#include "LldpdSource.h"
#include "LldpUtils.h"
#include <lldpctl.h>
#include <lldpctl.hpp>
#include <glog/logging.h>
#include <chrono>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace RSCGroup {

namespace {

struct CachedLldpNeighbor {
    std::optional<std::string> rawChassisId;
    std::optional<std::string> rawPortId;
    std::optional<std::string> rawSystemName;
};

}

class LldpdSource::Impl {
public:
    Impl(LldpSourceConfig config, LldpObservationCallback cb)
        : config_(std::move(config)), callback_(std::move(cb)) {}

    ~Impl() { stop(); }

    bool start() {
        std::scoped_lock lock(lifecycleMutex_);
        if (running_.load()) return true;
        running_ = true;
        if (!makeWatch()) {
            running_ = false;
            LOG(ERROR) << "LldpdSource start failed";
            return false;
        }

        // lldpctl_watch() only delivers future changes.
        // Enumerate existing neighbors so the cache and model are
        // initialized with current state.
        enumerateInitialNeighbors();

        LOG(INFO) << "LldpdSource started (push via lldpctl_watch)";
        return true;
    }

    void stop() {
        {
            std::scoped_lock lock(lifecycleMutex_);
            if (!watch_ && !running_.load()) return;
            running_ = false;
            watch_.reset();
        }
        std::scoped_lock lock(cacheMutex_);
        byInterface_.clear();
    }

    bool isRunning() const { return running_.load(); }

    void refreshAll() {
        std::scoped_lock lock(lifecycleMutex_);
        if (!running_) return;
        LOG(INFO) << "LldpdSource refreshAll: reconnecting";
        running_ = false;
        watch_.reset();
        {
            std::scoped_lock cacheLock(cacheMutex_);
            byInterface_.clear();
        }
        if (makeWatch()) {
            enumerateInitialNeighbors();
            running_ = true;
            LOG(INFO) << "LldpdSource reconnected";
        } else {
            running_ = false;
            LOG(ERROR) << "LldpdSource reconnection failed";
        }
    }

    void refreshInterface(const std::string& /*ifname*/) {}

    void removeInterface(const std::string& ifname) {
        std::vector<CachedLldpNeighbor> toRemove;
        {
            std::scoped_lock lock(cacheMutex_);
            auto it = byInterface_.find(ifname);
            if (it == byInterface_.end()) return;
            for (const auto& [_, entry] : it->second)
                toRemove.push_back(entry);
            byInterface_.erase(it);
        }

        for (const auto& entry : toRemove) {
            LldpObservation obs;
            obs.observedAt    = std::chrono::steady_clock::now();
            obs.kind          = ObservationKind::Lldp;
            obs.localIfname   = ifname;
            obs.event         = ObservationEvent::Removed;
            obs.remoteChassisId  = entry.rawChassisId;
            obs.remotePortId     = entry.rawPortId;
            obs.remoteSystemName = entry.rawSystemName;
            if (callback_) callback_(obs);
        }
    }

private:
    bool makeWatch() {
        try {
            watch_ = std::make_unique<lldpcli::LldpWatch<void, void>>(
                std::make_optional<lldpcli::LldpWatch<void, void>::ChangeCallback<void>>(
                    [this](std::string_view ifname,
                           lldpctl_change_t change,
                           const lldpcli::LldpAtom& /*interface*/,
                           const lldpcli::LldpAtom& neighbor,
                           void* /*ctx*/) {
                        handleChange(ifname, change, neighbor);
                    }
                )
            );
            return true;
        } catch (const std::exception& e) {
            LOG(ERROR) << "Lldpd watch creation failed: " << e.what();
            return false;
        }
    }

    void enumerateInitialNeighbors() {
        // Precondition: lifecycleMutex_ is held, running_ is true
        try {
            lldpcli::LldpCtl ctl;
            for (const auto& iface : ctl.GetInterfaces()) {
                auto ifname = iface.GetValue<std::string>(lldpctl_k_interface_name);
                if (!ifname) continue;

                if (!config_.watchedInterfaces.empty()) {
                    auto it = std::find(config_.watchedInterfaces.begin(),
                                        config_.watchedInterfaces.end(),
                                        *ifname);
                    if (it == config_.watchedInterfaces.end()) continue;
                }

                auto port = iface.GetPort();
                auto neighbors = port.GetAtomList(lldpctl_k_port_neighbors);

                for (const auto& nb : neighbors) {
                    handleChange(*ifname, lldpctl_c_added, nb);
                }
            }
            VLOG(1) << "LLDP initial enumeration complete";
        } catch (const std::exception& e) {
            LOG(ERROR) << "LLDP initial enumeration failed: " << e.what();
        }
    }

    void handleChange(std::string_view ifname,
                      lldpctl_change_t change,
                      const lldpcli::LldpAtom& neighbor) {
        if (!running_.load()) return;

        if (!config_.watchedInterfaces.empty()) {
            auto it = std::find(config_.watchedInterfaces.begin(),
                                config_.watchedInterfaces.end(),
                                ifname);
            if (it == config_.watchedInterfaces.end()) return;
        }

        if (change == lldpctl_c_deleted && !config_.emitRemovals) return;

        LldpObservation obs;
        obs.observedAt   = std::chrono::steady_clock::now();
        obs.kind         = ObservationKind::Lldp;
        obs.localIfname  = std::string(ifname);
        obs.event        = (change == lldpctl_c_deleted)
            ? ObservationEvent::Removed
            : ObservationEvent::Present;

        auto chassisId = neighbor.GetValue<std::string>(lldpctl_k_chassis_id);
        if (chassisId) obs.remoteChassisId = *chassisId;

        auto portId = neighbor.GetValue<std::string>(lldpctl_k_port_id);
        if (portId) obs.remotePortId = *portId;

        auto chassisName = neighbor.GetValue<std::string>(lldpctl_k_chassis_name);
        if (chassisName) obs.remoteSystemName = *chassisName;

        std::string key = resolveLldpIdentity(obs.remoteChassisId, obs.remotePortId);
        if (key.empty()) {
            VLOG(1) << "LLDP neighbor on " << ifname
                    << " — non-MAC identity, not cached";
        } else {
            std::scoped_lock lock(cacheMutex_);
            auto& ifaceCache = byInterface_[std::string(ifname)];
            if (obs.event == ObservationEvent::Removed) {
                VLOG(1) << "LLDP cache erase: ifname=" << ifname << " key=" << key;
                ifaceCache.erase(key);
            } else {
                VLOG(1) << "LLDP cache insert: ifname=" << ifname << " key=" << key;
                ifaceCache[key] = CachedLldpNeighbor{
                    obs.remoteChassisId,
                    obs.remotePortId,
                    obs.remoteSystemName
                };
            }
        }

        if (callback_) callback_(obs);
    }

    LldpSourceConfig config_;
    LldpObservationCallback callback_;
    std::unique_ptr<lldpcli::LldpWatch<void, void>> watch_;
    std::atomic<bool> running_{false};

    std::mutex lifecycleMutex_;
    std::mutex cacheMutex_;
    std::unordered_map<std::string,
        std::unordered_map<std::string, CachedLldpNeighbor>> byInterface_;
};

LldpdSource::LldpdSource(LldpSourceConfig config, LldpObservationCallback cb)
    : impl_(std::make_unique<Impl>(std::move(config), std::move(cb))) {}

LldpdSource::~LldpdSource() = default;

bool LldpdSource::start()   { return impl_->start(); }
void LldpdSource::stop()    { impl_->stop(); }
bool LldpdSource::isRunning() const { return impl_->isRunning(); }
void LldpdSource::refreshAll()      { impl_->refreshAll(); }
void LldpdSource::refreshInterface(const std::string& ifname) { impl_->refreshInterface(ifname); }
void LldpdSource::removeInterface(const std::string& ifname)  { impl_->removeInterface(ifname); }

} // namespace RSCGroup