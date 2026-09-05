//
// Created by vvass on 05-Sep-26.
//

#include "NetlinkEventLoop.h"

#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace RSCGroup {

namespace {

constexpr std::size_t liveReceiveBufferSize =
    64 * 1024;

constexpr int liveSocketReceiveBufferBytes =
    256 * 1024;

constexpr std::uint32_t liveGroups =
    RTMGRP_LINK |
    RTMGRP_IPV4_IFADDR |
    RTMGRP_IPV6_IFADDR |
    RTMGRP_NEIGH;

} // namespace

NetlinkEventLoop::NetlinkEventLoop(
    EventFdSignal& stopSignal,
    MessageHandler messageHandler)
    : socket_(
          NetlinkRouteSocket::open({
              .groups = liveGroups,
              .receiveBufferBytes =
                  liveSocketReceiveBufferBytes,
          }))
    , stopSignal_(stopSignal)
    , messageHandler_(std::move(messageHandler))
{
    validateHandler();
}

NetlinkEventLoop::NetlinkEventLoop(
    int liveFd,
    EventFdSignal& stopSignal,
    MessageHandler messageHandler)
    : socket_(liveFd)
    , stopSignal_(stopSignal)
    , messageHandler_(std::move(messageHandler))
{
    validateHandler();
}

void NetlinkEventLoop::validateHandler() const
{
    if (!messageHandler_) {
        throw std::invalid_argument(
            "NetlinkEventLoop: message handler is empty");
    }
}

NetlinkEventLoop::Result
NetlinkEventLoop::run(
    std::stop_token stopToken)
{
    std::vector<char> buffer(
        liveReceiveBufferSize);

    while (!stopToken.stop_requested()) {
        const NetlinkWaitResult waitResult =
            waitForNetlinkDataOrStop(
                socket_.fd(),
                stopSignal_);

        if (waitResult.status ==
            NetlinkWaitStatus::stopped) {
            return {
                Status::stopped,
                0,
            };
        }

        if (!waitResult.dataReady()) {
            return mapWaitFailure(waitResult);
        }

        Result terminalResult;

        if (!receiveAndDispatch(
                std::span<char>{
                    buffer.data(),
                    buffer.size(),
                },
                terminalResult)) {
            return terminalResult;
        }
    }

    return {
        Status::stopped,
        0,
    };
}

NetlinkEventLoop::Result
NetlinkEventLoop::mapWaitFailure(
    const NetlinkWaitResult& result) const noexcept
{
    switch (result.status) {
        case NetlinkWaitStatus::stop_drain_failed:
            return {
                Status::stop_drain_failed,
                result.error,
            };

        case NetlinkWaitStatus::poll_failed:
        case NetlinkWaitStatus::data_fd_failed:
        case NetlinkWaitStatus::stop_fd_failed:
            return {
                Status::poll_failed,
                result.error,
            };

        case NetlinkWaitStatus::stopped:
            return {
                Status::stopped,
                0,
            };

        case NetlinkWaitStatus::data_ready:
            break;
    }

    return {
        Status::poll_failed,
        EIO,
    };
}

bool NetlinkEventLoop::receiveAndDispatch(
    std::span<char> buffer,
    Result& terminalResult)
{
    const NetlinkReceiveResult receiveResult =
        receiveNetlinkDatagram(
            socket_.fd(),
            buffer);

    switch (receiveResult.status) {
        case NetlinkReceiveStatus::received:
            break;

        case NetlinkReceiveStatus::truncated:
            terminalResult = {
                Status::truncated_message,
                receiveResult.error,
            };
            return false;

        case NetlinkReceiveStatus::failed:
        case NetlinkReceiveStatus::closed:
            terminalResult = {
                Status::receive_failed,
                receiveResult.error,
            };
            return false;
    }

    int remaining =
        static_cast<int>(receiveResult.size);

    for (auto* header =
             reinterpret_cast<nlmsghdr*>(
                 buffer.data());
         NLMSG_OK(header, remaining);
         header = NLMSG_NEXT(header, remaining)) {
        if (header->nlmsg_type ==
            NLMSG_OVERRUN) {
            terminalResult = {
                Status::socket_overrun,
                ENOBUFS,
            };
            return false;
        }

        messageHandler_(header);
    }

    if (remaining != 0) {
        terminalResult = {
            Status::malformed_message,
            EPROTO,
        };
        return false;
    }

    return true;
}

const char* NetlinkEventLoop::statusName(
    Status status) noexcept
{
    switch (status) {
        case Status::stopped:
            return "stopped";

        case Status::poll_failed:
            return "poll_failed";

        case Status::receive_failed:
            return "receive_failed";

        case Status::malformed_message:
            return "malformed_message";

        case Status::truncated_message:
            return "truncated_message";

        case Status::socket_overrun:
            return "socket_overrun";

        case Status::stop_drain_failed:
            return "stop_drain_failed";
    }

    return "unknown";
}

} // namespace RSCGroup
