//
// Created by vvass on 07-Jul-26.
//
/**
 * @file NetlinkNetworkMonitor.cpp
 * @brief Impl of NetlinkNetworkMonitor: socket management, event loop, and dump orchestration.
 */
#include "public/NetlinkNetworkMonitor.h"
#include "ErrnoString.h"
#include "NetlinkInitialDump.h"
#include "NetlinkParser.h"
#include "NetlinkState.h"
#include <EventFdSignal.h>
#include <UniqueFd.h>
#include <glog/logging.h>
#include <linux/netlink.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <condition_variable>
#include <poll.h>
#include <optional>
#include <functional>
#include <exception>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <linux/rtnetlink.h>

namespace RSCGroup {

class NetlinkNetworkMonitor::Impl {
public:
    explicit Impl(MonitorCallbacks callbacks, std::set<std::string> watchedInterfaces)
        : callbacks_(std::move(callbacks))
        , watchedInterfaces_(std::move(watchedInterfaces))
        , onLinkHandler_([this](const LinkEvent& e)       { onLinkCallback(e); })
        , onIpHandler_  ([this](const InterfaceIpEvent& e)  { onIpCallback(e); })
        , onFdbHandler_ ([this](const FdbEvent& e)        { onFdbCallback(e); })
        , onNeighHandler_([this](const NeighborEvent& e)   { onNeighCallback(e); })
        , onDeviceHandler_([this](const DeviceEvent& de)  { if (callbacks_.onDeviceChanged) callbacks_.onDeviceChanged(de); })
    {}

    ~Impl() { stop(); }

    bool start()
    {
        std::unique_lock lock(lifecycleMutex_);

        if (lifecycleState_ != State::Stopped) {
            LOG(WARNING) << "start() called while monitor is not stopped";
            return false;
        }

        lifecycleState_ = State::Starting;
        startupThreadId_ = std::this_thread::get_id();

        if (!openLiveSocket()) {
            closeResources();
            startupThreadId_ = {};
            lifecycleState_ = State::Stopped;
            lifecycleCv_.notify_all();
            return false;
        }

        lock.unlock();

        NetlinkInitialDump::Result dumpResult;

        try {
            NetlinkInitialDump dump(
                *stopSignal_,
                [this](const nlmsghdr* message) { processSingleMessage(message); }
                );
            dumpResult = dump.run();
        } catch (const std::exception& error) {
            LOG(ERROR) << "initial netlink dump threw: " << error.what();
            lock.lock();
            finishStop();
            return false;
        } catch (...) {
            LOG(ERROR) << "initial netlink dump threw an unknown exception";
            lock.lock();
            finishStop();
            return false;
        }
        lock.lock();

        if (!dumpResult.completed() || lifecycleState_ != State::Starting) {
            if (!dumpResult.completed() && !dumpResult.interrupted()) {
                LOG(ERROR) << "initial netlink dump failed: status=" << NetlinkInitialDump::statusName( dumpResult.status) << ", error=" << dumpResult.error;
            }
            finishStop();
            return false;
        }

        startupThreadId_ = {};
        try {
            worker_ = std::jthread([this](const std::stop_token& st) {
                {
                    std::scoped_lock localLock(lifecycleMutex_);
                    workerThreadId_ = std::this_thread::get_id();
                }
                runLoop(st);
            });
        } catch (const std::exception& error) {
            LOG(ERROR) << "failed to start netlink monitor worker: " << error.what();
            finishStop();
            return false;
        } catch (...) {
            LOG(ERROR) << "failed to start netlink monitor worker: " << "unknown exception";
            finishStop();
            return false;
        }
        lifecycleState_ = State::Running;
        LOG(INFO) << "NetlinkNetworkMonitor started";
        return true;
    }

    void stop()
    {
        std::unique_lock lock(lifecycleMutex_);

        if (lifecycleState_ == State::Stopped) {
            return;
        }

        // Reject self-stop from the monitor I/O thread (e.g. from a callback).
        // A self-stop would leave the object in a partially-stopped state
        // that cannot be restarted and holds resources indefinitely.
        // Do not depend on worker_.joinable(). An external stop caller may
        // already have moved worker_ into a local jthread while joining it.
        const auto currentThreadId = std::this_thread::get_id();
        if (currentThreadId == workerThreadId_) {
            LOG(ERROR) << "stop() must not be called from a monitor callback";
            return;
        }

        // Another caller already owns teardown. Wait until it has completed
        // rather than manipulating the worker or descriptors concurrently.
        if (lifecycleState_ == State::Stopping) {
            // An initial-dump callback must return to the start() thread so
            // that it can leave dump.run() and perform final cleanup.
            if (currentThreadId == startupThreadId_) {
                return;
            }
            lifecycleCv_.wait(lock, [this] { return lifecycleState_ == State::Stopped; });
            return;
        }

        // During Starting, the startup thread owns the local dump descriptor
        // and may currently be polling stopSignal_. Signal it, but leave
        // resource destruction to the startup thread.
        if (lifecycleState_ == State::Starting) {
            lifecycleState_ = State::Stopping;
            signalStop();

            // Initial-dump callbacks execute synchronously on the start()
            // thread. Waiting here from such a callback would deadlock:
            // this same thread must leave performInitialDump() and perform
            // the final cleanup.
            if (currentThreadId == startupThreadId_) {
                return;
            }

            lifecycleCv_.wait(lock, [this] { return lifecycleState_ == State::Stopped; });
            return;
        }

        lifecycleState_ = State::Stopping;

        if (worker_.joinable()) {

            worker_.request_stop();
        }

        signalStop();

        std::jthread worker = std::move(worker_);
        lock.unlock();

        if (worker.joinable()) {
            worker.join();
        }

        lock.lock();
        finishStop();
    }

    bool isRunning() const
    {
        std::scoped_lock lock(lifecycleMutex_);
        return lifecycleState_ == State::Running;
    }

    std::vector<DeviceEvent> getDevicesSnapshot() const
    {
        return netlinkState_.getDevicesSnapshot();
    }

    std::vector<LinkEvent> getLinksSnapshot() const
    {
        return netlinkState_.getLinksSnapshot();
    }

private:
    enum class State { Stopped, Starting, Running, Stopping };

    mutable std::mutex lifecycleMutex_;
    std::condition_variable lifecycleCv_;
    MonitorCallbacks callbacks_;
    std::set<std::string> watchedInterfaces_;
    State lifecycleState_ = State::Stopped;
    UniqueFd netlinkFd_;
    std::optional<EventFdSignal> stopSignal_;
    std::jthread worker_;
    std::thread::id startupThreadId_;
    std::thread::id workerThreadId_;
    NetlinkState netlinkState_;

    // Pre-bound handlers (created once, zero allocation per message)
    std::function<void(const LinkEvent&)>       onLinkHandler_;
    std::function<void(const InterfaceIpEvent&)> onIpHandler_;
    std::function<void(const FdbEvent&)>        onFdbHandler_;
    std::function<void(const NeighborEvent&)>   onNeighHandler_;
    std::function<void(const DeviceEvent&)>     onDeviceHandler_;

    // Precondition: lifecycleMutex_ is held
    void finishStop()
    {
        closeResources();
        netlinkState_.clear();
        startupThreadId_ = {};
        workerThreadId_ = {};
        lifecycleState_ = State::Stopped;
        lifecycleCv_.notify_all();
        LOG(INFO) << "NetlinkNetworkMonitor stopped";
    }

    bool openLiveSocket()
    {
        netlinkFd_.reset(socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE));
        if (!netlinkFd_.valid()) {
            PLOG(ERROR) << "socket(AF_NETLINK) for live events failed";
            return false;
        }

        int rcvbuf = 256 * 1024;
        if (setsockopt(netlinkFd_.get(), SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) < 0) {
            PLOG(WARNING) << "setsockopt(SO_RCVBUF) failed";
        }

        sockaddr_nl addr{};
        addr.nl_family = AF_NETLINK;
        addr.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR | RTMGRP_NEIGH;

        if (bind(netlinkFd_.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            PLOG(ERROR) << "bind(netlink) for live events failed";
            return false;
        }

        try {
            stopSignal_.emplace();
        } catch (const std::exception& error) {
            LOG(ERROR) << "failed to create netlink stop eventfd: " << error.what();
            return false;
        } catch (...) {
            LOG(ERROR) << "failed to create netlink stop eventfd: unknown exception";
            return false;
        }

        return true;
    }

    void closeResources()
    {
        stopSignal_.reset();
        netlinkFd_.reset();
    }

    void signalStop()
    {
        if (!stopSignal_) {
            LOG(FATAL) << "netlink stop eventfd is unavailable during teardown";
            return;
        }

        for (;;) {
            const int error = stopSignal_->signal();
            if (error == 0) {
                return;
            }

            if (error == EINTR) {
                continue;
            }
            // A failed wake would leave the worker blocked indefinitely in
            // poll(), making the subsequent join unsafe. The descriptor is
            // guaranteed to be live while Starting or Running, so any other
            // error is a structural lifecycle violation.
            LOG(FATAL) << "failed to signal netlink stop eventfd: " << errnoToString(error);
            return;
        }
    }

    void drainStopSignal()
    {
        if (!stopSignal_) {
            LOG(ERROR) << "netlink stop eventfd is unavailable while draining";
            return;
        }

        if (const int error = stopSignal_->drain(); error != 0) {
            LOG(ERROR) << "failed to drain netlink stop eventfd: " << errnoToString(error);
        }
    }

    void runLoop(const std::stop_token& stopToken)
    {
        std::vector<char> buffer(16384);
        pollfd fds[2]{};
        fds[0].fd = netlinkFd_.get();
        fds[0].events = POLLIN;
        fds[1].fd = stopSignal_->fd();
        fds[1].events = POLLIN;

        while (!stopToken.stop_requested()) {
            fds[0].revents = 0;
            fds[1].revents = 0;

            if (poll(fds, 2, -1) < 0) {
                if (errno == EINTR) continue;
                PLOG(ERROR) << "poll failed";
                break;
            }

            if ((fds[1].revents & POLLIN) != 0) {
                drainStopSignal();
                break;
            }

            if ((fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                LOG(ERROR) << "netlink poll error revents=" << fds[0].revents;
                break;
            }

            if ((fds[0].revents & POLLIN) != 0) {
                if (!recvAndProcess(buffer)) break;
            }
        }
    }

    bool recvAndProcess(std::vector<char>& buffer)
    {
        const ssize_t len = recv(netlinkFd_.get(), buffer.data(), buffer.size(), 0);
        if (len < 0) {
            if (errno == EINTR) return true;
            PLOG(ERROR) << "recv failed";
            return false;
        }
        if (len == 0) {
            LOG(ERROR) << "recv returned 0 on netlink socket";
            return false;
        }

        int remaining = static_cast<int>(len);
        for (auto* nh = reinterpret_cast<nlmsghdr*>(buffer.data());
             NLMSG_OK(nh, remaining);
             nh = NLMSG_NEXT(nh, remaining)) {
            if (nh->nlmsg_type == NLMSG_DONE) break;
            processSingleMessage(nh);
        }
        return true;
    }

    void onLinkCallback(const LinkEvent& e)
    {
        auto ev = netlinkState_.updateLink(e);
        if (ev && callbacks_.onLinkChanged) callbacks_.onLinkChanged(*ev);
    }

    void onIpCallback(const InterfaceIpEvent& e)
    {
        auto ev = netlinkState_.updateAddress(e);
        if (ev && callbacks_.onInterfaceIpChanged) callbacks_.onInterfaceIpChanged(*ev);
    }

    void onFdbCallback(const FdbEvent& e)
    {
        if (!watchedInterfaces_.empty() && !watchedInterfaces_.contains(e.ifname)) return;
        auto ev = netlinkState_.updateFdb(e, onDeviceHandler_);
        if (ev && callbacks_.onFdbChanged) callbacks_.onFdbChanged(*ev);
    }

    void onNeighCallback(const NeighborEvent& e)
    {
        if (!watchedInterfaces_.empty() && !watchedInterfaces_.contains(e.ifname)) return;
        auto ev = netlinkState_.updateNeighbor(e, onDeviceHandler_);
        if (ev && callbacks_.onNeighborChanged) callbacks_.onNeighborChanged(*ev);
    }

    void processSingleMessage(const nlmsghdr* nh)
    {
        processMessage(nh, onLinkHandler_, onIpHandler_, onFdbHandler_, onNeighHandler_);
    }
};

NetlinkNetworkMonitor::NetlinkNetworkMonitor(
    MonitorCallbacks callbacks,
    std::set<std::string> watchedInterfaces)
    : impl_(std::make_unique<Impl>(std::move(callbacks), std::move(watchedInterfaces)))
{}

NetlinkNetworkMonitor::~NetlinkNetworkMonitor() = default;

bool NetlinkNetworkMonitor::start() { return impl_->start(); }
void NetlinkNetworkMonitor::stop()  { impl_->stop(); }
bool NetlinkNetworkMonitor::isRunning() const { return impl_->isRunning(); }
std::vector<DeviceEvent> NetlinkNetworkMonitor::getDevicesSnapshot() const { return impl_->getDevicesSnapshot(); }
std::vector<LinkEvent> NetlinkNetworkMonitor::getLinksSnapshot() const { return impl_->getLinksSnapshot(); }

} // namespace RSCGroup