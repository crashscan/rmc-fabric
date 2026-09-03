//
// Created by vvass on 07-Jul-26.
//
/**
 * @file NetlinkNetworkMonitor.cpp
 * @brief Impl of NetlinkNetworkMonitor: socket management, event loop, and dump orchestration.
 */
#include "public/NetlinkNetworkMonitor.h"
#include "NetlinkParser.h"
#include "NetlinkState.h"
#include <EventFdSignal.h>
#include <UniqueFd.h>
#include <glog/logging.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <poll.h>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace RSCGroup {

enum class DumpKind { Link, Addr, Neigh };

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

        if (!openLiveSocket()) {
            lifecycleState_ = State::Stopped;
            return false;
        }

        UniqueFd dumpFd;
        if (!openDumpSocket(dumpFd)) {
            closeResources();
            lifecycleState_ = State::Stopped;
            return false;
        }

        lock.unlock();

        bool dumpOk = performInitialDump(dumpFd.get());
        dumpFd.reset();

        lock.lock();

        if (!dumpOk || lifecycleState_ != State::Starting) {
            closeResources();
            netlinkState_.clear();
            lifecycleState_ = State::Stopped;
            return false;
        }

        worker_ = std::jthread([this](const std::stop_token& st) {
            {
                std::scoped_lock localLock(lifecycleMutex_);
                workerThreadId_ = std::this_thread::get_id();
            }
            runLoop(st);
        });
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
        if (worker_.joinable() && std::this_thread::get_id() == workerThreadId_) {
            LOG(ERROR) << "stop() must not be called from a monitor callback";
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
    MonitorCallbacks callbacks_;
    std::set<std::string> watchedInterfaces_;
    State lifecycleState_ = State::Stopped;
    UniqueFd netlinkFd_;
    EventFdSignal stopSignal_;
    std::jthread worker_;
    std::thread::id workerThreadId_;
    std::uint32_t nextDumpSeq_ = 1;
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
        workerThreadId_ = {};
        lifecycleState_ = State::Stopped;
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

        if (!stopSignal_.open()) {
            PLOG(ERROR) << "eventfd failed";
            return false;
        }

        return true;
    }

    bool openDumpSocket(UniqueFd& fd)
    {
        fd.reset(socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE));
        if (!fd.valid()) {
            PLOG(ERROR) << "socket(AF_NETLINK) for dump failed";
            return false;
        }

        sockaddr_nl addr{};
        addr.nl_family = AF_NETLINK;

        if (bind(fd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            PLOG(ERROR) << "bind(netlink) for dump failed";
            fd.reset();
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
        if (!stopSignal_.signal()) {
            PLOG(WARNING) << "stopSignal_.signal() failed";
        }
    }

    void drainStopSignal()
    {
        if (!stopSignal_.drain()) {
            PLOG(WARNING) << "stopSignal_.drain() failed";
        }
    }

    void runLoop(const std::stop_token& stopToken)
    {
        std::vector<char> buffer(16384);
        pollfd fds[2]{};
        fds[0].fd = netlinkFd_.get();
        fds[0].events = POLLIN;
        fds[1].fd = stopSignal_.fd();
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

    template <typename Msg>
    bool sendDumpRequest(int dumpFd, std::uint16_t type, std::uint8_t family)
    {
        struct Request { nlmsghdr nh; Msg msg; } req{};
        req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(Msg));
        req.nh.nlmsg_type = type;
        req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
        req.nh.nlmsg_seq = nextDumpSeq_;
        req.msg = Msg{};
        setFamily(req.msg, family);

        if (send(dumpFd, &req, req.nh.nlmsg_len, 0) < 0) {
            PLOG(ERROR) << "send dump request failed type=" << type
                        << " family=" << static_cast<int>(family);
            return false;
        }
        return true;
    }

    static void setFamily(ifinfomsg& msg, std::uint8_t family) { msg.ifi_family = family; }
    static void setFamily(ifaddrmsg& msg, std::uint8_t family)  { msg.ifa_family = family; }
    static void setFamily(ndmsg& msg, std::uint8_t family)      { msg.ndm_family = family; }

    bool requestDump(int dumpFd, std::uint16_t type, std::uint8_t family, DumpKind kind)
    {
        switch (kind) {
            case DumpKind::Link:  if (!sendDumpRequest<ifinfomsg>(dumpFd, type, family)) return false; break;
            case DumpKind::Addr:  if (!sendDumpRequest<ifaddrmsg>(dumpFd, type, family)) return false; break;
            case DumpKind::Neigh: if (!sendDumpRequest<ndmsg>(dumpFd, type, family))     return false; break;
        }
        return readDumpResponses(dumpFd, nextDumpSeq_++);
    }

    bool readDumpResponses(int dumpFd, std::uint32_t seq)
    {
        std::vector<char> buffer(16384);
        while (true) {
            pollfd pfds[2]{};
            pfds[0].fd = dumpFd;             pfds[0].events = POLLIN;
            pfds[1].fd = stopSignal_.fd();   pfds[1].events = POLLIN;

            int pr = poll(pfds, 2, -1);
            if (pr < 0) {
                if (errno == EINTR) continue;
                PLOG(ERROR) << "poll during dump failed";
                return false;
            }

            if ((pfds[1].revents & POLLIN) != 0) {
                drainStopSignal();
                LOG(INFO) << "dump interrupted by stop signal";
                return false;
            }

            if ((pfds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                LOG(ERROR) << "dump socket error revents=" << pfds[0].revents;
                return false;
            }

            if ((pfds[0].revents & POLLIN) == 0) continue;

            ssize_t len = recv(dumpFd, buffer.data(), buffer.size(), 0);
            if (len < 0) {
                if (errno == EINTR) continue;
                PLOG(ERROR) << "recv dump response failed";
                return false;
            }
            if (len == 0) {
                LOG(ERROR) << "recv returned 0 on dump socket";
                return false;
            }

            int remaining = static_cast<int>(len);
            for (auto* nh = reinterpret_cast<nlmsghdr*>(buffer.data());
                 NLMSG_OK(nh, remaining);
                 nh = NLMSG_NEXT(nh, remaining)) {
                if (nh->nlmsg_seq != seq) continue;
                if (nh->nlmsg_type == NLMSG_DONE) return true;
                if (nh->nlmsg_type == NLMSG_ERROR) {
                    if (nh->nlmsg_len < static_cast<int>(NLMSG_LENGTH(sizeof(nlmsgerr)))) {
                        LOG(ERROR) << "netlink dump error too short, len=" << nh->nlmsg_len;
                        return false;
                    }
                    const auto* err = reinterpret_cast<const nlmsgerr*>(NLMSG_DATA(nh));
                    LOG(ERROR) << "netlink dump error=" << err->error;
                    return false;
                }
                processSingleMessage(nh);
            }
        }
    }

    bool performInitialDump(int dumpFd)
    {
        VLOG(1) << "performing initial netlink dump";
        return requestDump(dumpFd, RTM_GETLINK, AF_PACKET, DumpKind::Link)
            && requestDump(dumpFd, RTM_GETADDR, AF_INET,     DumpKind::Addr)
            && requestDump(dumpFd, RTM_GETADDR, AF_INET6,    DumpKind::Addr)
            && requestDump(dumpFd, RTM_GETNEIGH, AF_UNSPEC,  DumpKind::Neigh)
            && requestDump(dumpFd, RTM_GETNEIGH, AF_BRIDGE,  DumpKind::Neigh);
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