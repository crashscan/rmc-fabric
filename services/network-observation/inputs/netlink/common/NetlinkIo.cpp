//
// Created by vvass on 05-Sep-26.
//

#include "NetlinkIo.h"

#include "NetlinkIo.h"

#include <glog/logging.h>

#include <linux/netlink.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <cerrno>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace RSCGroup {

NetlinkRouteSocket::NetlinkRouteSocket(int fd)
    : fd_(fd)
{
    if (fd_ < 0) {
        throw std::invalid_argument(
            "NetlinkRouteSocket: invalid borrowed descriptor");
    }
}

NetlinkRouteSocket::NetlinkRouteSocket(
    UniqueFd&& ownedFd) noexcept
    : ownedFd_(std::move(ownedFd))
    , fd_(ownedFd_.get())
{
}

NetlinkRouteSocket::NetlinkRouteSocket(
    NetlinkRouteSocket&& other) noexcept
    : ownedFd_(std::move(other.ownedFd_))
    , fd_(other.fd_)
{
    other.fd_ = -1;
}

NetlinkRouteSocket& NetlinkRouteSocket::operator=(
    NetlinkRouteSocket&& other) noexcept
{
    if (this != &other) {
        ownedFd_ = std::move(other.ownedFd_);
        fd_ = other.fd_;
        other.fd_ = -1;
    }

    return *this;
}

NetlinkRouteSocket NetlinkRouteSocket::open()
{
    return open(OpenOptions{});
}

NetlinkRouteSocket NetlinkRouteSocket::open(
    OpenOptions options)
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
            "NetlinkRouteSocket: socket(AF_NETLINK) failed");
    }

    if (options.receiveBufferBytes) {
        const int receiveBufferBytes =
            *options.receiveBufferBytes;

        if (::setsockopt(
                fd.get(),
                SOL_SOCKET,
                SO_RCVBUF,
                &receiveBufferBytes,
                sizeof(receiveBufferBytes)) < 0) {
            PLOG(WARNING)
                << "NetlinkRouteSocket: "
                   "setsockopt(SO_RCVBUF) failed";
        }
    }

    sockaddr_nl address{};
    address.nl_family = AF_NETLINK;
    address.nl_groups = options.groups;

    if (::bind(
            fd.get(),
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) < 0) {
        const int error = errno;

        throw std::system_error(
            error,
            std::generic_category(),
            "NetlinkRouteSocket: bind(AF_NETLINK) failed");
    }

    return NetlinkRouteSocket(std::move(fd));
}

NetlinkWaitResult waitForNetlinkDataOrStop(
    int dataFd,
    EventFdSignal& stopSignal)
{
    pollfd descriptors[2]{};

    descriptors[0].fd = dataFd;
    descriptors[0].events = POLLIN;

    descriptors[1].fd = stopSignal.fd();
    descriptors[1].events = POLLIN;

    for (;;) {
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
                NetlinkWaitStatus::poll_failed,
                errno,
            };
        }

        // Shutdown wins when both descriptors become readable.
        if ((descriptors[1].revents & POLLIN) != 0) {
            const int drainError = stopSignal.drain();

            if (drainError != 0) {
                return {
                    NetlinkWaitStatus::stop_drain_failed,
                    drainError,
                };
            }

            return {
                NetlinkWaitStatus::stopped,
                0,
            };
        }

        if ((descriptors[1].revents &
             (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            return {
                NetlinkWaitStatus::stop_fd_failed,
                EIO,
            };
        }

        if ((descriptors[0].revents &
             (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            return {
                NetlinkWaitStatus::data_fd_failed,
                EIO,
            };
        }

        if ((descriptors[0].revents & POLLIN) != 0) {
            return {
                NetlinkWaitStatus::data_ready,
                0,
            };
        }
    }
}

NetlinkReceiveResult receiveNetlinkDatagram(
    int fd,
    std::span<char> buffer)
{
    if (buffer.empty()) {
        return {
            NetlinkReceiveStatus::failed,
            0,
            EINVAL,
        };
    }

    iovec ioVector{};
    ioVector.iov_base = buffer.data();
    ioVector.iov_len = buffer.size();

    msghdr message{};
    message.msg_iov = &ioVector;
    message.msg_iovlen = 1;

    ssize_t received;

    for (;;) {
        message.msg_flags = 0;

        received = ::recvmsg(
            fd,
            &message,
            0);

        if (received >= 0) {
            break;
        }

        if (errno == EINTR) {
            continue;
        }

        return {
            NetlinkReceiveStatus::failed,
            0,
            errno,
        };
    }

    if ((message.msg_flags & MSG_TRUNC) != 0) {
        return {
            NetlinkReceiveStatus::truncated,
            0,
            EMSGSIZE,
        };
    }

    if (received == 0) {
        return {
            NetlinkReceiveStatus::closed,
            0,
            ECONNRESET,
        };
    }

    return {
        NetlinkReceiveStatus::received,
        static_cast<std::size_t>(received),
        0,
    };
}

} // namespace RSCGroup
