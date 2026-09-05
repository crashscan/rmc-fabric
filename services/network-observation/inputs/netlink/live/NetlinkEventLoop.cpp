//
// Created by vvass on 05-Sep-26.
//

#include "NetlinkEventLoop.h"

#include <glog/logging.h>

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <cerrno>
#include <cstddef>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace RSCGroup {

namespace {

constexpr std::size_t receiveBufferSize = 64 * 1024;

} // namespace

NetlinkEventLoop::NetlinkEventLoop(
    EventFdSignal& stopSignal,
    MessageHandler messageHandler)
    : NetlinkEventLoop(
          openLiveSocket(),
          stopSignal,
          std::move(messageHandler),
          OwnedSocketTag{})
{
}

NetlinkEventLoop::NetlinkEventLoop(
    int liveFd,
    EventFdSignal& stopSignal,
    MessageHandler messageHandler)
    : liveFd_(liveFd)
    , stopSignal_(stopSignal)
    , messageHandler_(std::move(messageHandler))
{
    if (liveFd_ < 0) {
        throw std::invalid_argument(
            "NetlinkEventLoop: invalid borrowed descriptor");
    }

    if (!messageHandler_) {
        throw std::invalid_argument(
            "NetlinkEventLoop: message handler is empty");
    }
}

NetlinkEventLoop::NetlinkEventLoop(
    UniqueFd&& ownedLiveFd,
    EventFdSignal& stopSignal,
    MessageHandler messageHandler,
    OwnedSocketTag)
    : ownedLiveFd_(std::move(ownedLiveFd))
    , liveFd_(ownedLiveFd_.get())
    , stopSignal_(stopSignal)
    , messageHandler_(std::move(messageHandler))
{
    if (!ownedLiveFd_.valid()) {
        throw std::invalid_argument(
            "NetlinkEventLoop: invalid owned descriptor");
    }

    if (!messageHandler_) {
        throw std::invalid_argument(
            "NetlinkEventLoop: message handler is empty");
    }
}

UniqueFd NetlinkEventLoop::openLiveSocket()
{
    UniqueFd fd(::socket(
        AF_NETLINK,
        SOCK_RAW | SOCK_CLOEXEC,
        NETLINK_ROUTE));

    if (!fd.valid()) {
        const int error = errno;

        throw std::system_error(
            error,
            std::generic_category(),
            "NetlinkEventLoop: socket(AF_NETLINK) failed");
    }

    int receiveBufferSizeBytes = 256 * 1024;

    if (::setsockopt(
            fd.get(),
            SOL_SOCKET,
            SO_RCVBUF,
            &receiveBufferSizeBytes,
            sizeof(receiveBufferSizeBytes)) < 0) {
        PLOG(WARNING)
            << "NetlinkEventLoop: setsockopt(SO_RCVBUF) failed";
    }

    sockaddr_nl address{};
    address.nl_family = AF_NETLINK;
    address.nl_groups =
        RTMGRP_LINK |
        RTMGRP_IPV4_IFADDR |
        RTMGRP_IPV6_IFADDR |
        RTMGRP_NEIGH;

    if (::bind(
            fd.get(),
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) < 0) {
        const int error = errno;

        throw std::system_error(
            error,
            std::generic_category(),
            "NetlinkEventLoop: bind(AF_NETLINK) failed");
    }

    return fd;
}

NetlinkEventLoop::Result NetlinkEventLoop::run(
    std::stop_token stopToken)
{
    std::vector<char> buffer(receiveBufferSize);

    pollfd descriptors[2]{};
    descriptors[0].fd = liveFd_;
    descriptors[0].events = POLLIN;
    descriptors[1].fd = stopSignal_.fd();
    descriptors[1].events = POLLIN;

    while (!stopToken.stop_requested()) {
        descriptors[0].revents = 0;
        descriptors[1].revents = 0;

        const int pollResult = ::poll(
            descriptors,
            2,
            -1);

        if (pollResult < 0) {
            if (errno == EINTR) {
                continue;
            }

            return {
                Status::poll_failed,
                errno,
            };
        }

        // Prioritize shutdown if both descriptors become readable.
        if ((descriptors[1].revents & POLLIN) != 0) {
            const int drainError = stopSignal_.drain();

            if (drainError != 0) {
                return {
                    Status::stop_drain_failed,
                    drainError,
                };
            }

            return {
                Status::stopped,
                0,
            };
        }

        if ((descriptors[1].revents &
             (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            return {
                Status::poll_failed,
                EIO,
            };
        }

        if ((descriptors[0].revents &
             (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            return {
                Status::poll_failed,
                EIO,
            };
        }

        if ((descriptors[0].revents & POLLIN) == 0) {
            continue;
        }

        if (auto result = receiveAndDispatch(
                buffer.data(),
                buffer.size())) {
            return *result;
        }
    }

    return {
        Status::stopped,
        0,
    };
}

std::optional<NetlinkEventLoop::Result>
NetlinkEventLoop::receiveAndDispatch(
    char* buffer,
    std::size_t bufferSize)
{
    iovec ioVector{};
    ioVector.iov_base = buffer;
    ioVector.iov_len = bufferSize;

    msghdr message{};
    message.msg_iov = &ioVector;
    message.msg_iovlen = 1;

    ssize_t received;

    for (;;) {
        received = ::recvmsg(
            liveFd_,
            &message,
            0);

        if (received >= 0) {
            break;
        }

        if (errno == EINTR) {
            continue;
        }

        return Result{
            Status::receive_failed,
            errno,
        };
    }

    if ((message.msg_flags & MSG_TRUNC) != 0) {
        return Result{
            Status::truncated_message,
            EMSGSIZE,
        };
    }

    if (received == 0) {
        return Result{
            Status::receive_failed,
            ECONNRESET,
        };
    }

    int remaining = static_cast<int>(received);

    for (auto* header =
             reinterpret_cast<nlmsghdr*>(buffer);
         NLMSG_OK(header, remaining);
         header = NLMSG_NEXT(header, remaining)) {
        if (header->nlmsg_type == NLMSG_OVERRUN) {
            return Result{
                Status::socket_overrun,
                ENOBUFS,
            };
        }

        messageHandler_(header);
    }

    if (remaining != 0) {
        return Result{
            Status::malformed_message,
            EPROTO,
        };
    }

    return std::nullopt;
}

const char* NetlinkEventLoop::statusName(Status status) noexcept
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
