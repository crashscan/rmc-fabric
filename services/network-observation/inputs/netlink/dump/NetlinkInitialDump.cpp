//
// Created by vvass on 05-Sep-26.
//

#include "NetlinkInitialDump.h"

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <poll.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstdint>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace RSCGroup {
namespace {

void setFamily(ifinfomsg& message, std::uint8_t family) noexcept
{
    message.ifi_family = family;
}

void setFamily(ifaddrmsg& message, std::uint8_t family) noexcept
{
    message.ifa_family = family;
}

void setFamily(ndmsg& message, std::uint8_t family) noexcept
{
    message.ndm_family = family;
}

[[nodiscard]] int positiveKernelError(int error) noexcept
{
    return error < 0 ? -error : error;
}

} // namespace

NetlinkInitialDump::NetlinkInitialDump(
    EventFdSignal& stopSignal,
    MessageHandler messageHandler)
    : NetlinkInitialDump(
          openDumpSocket(),
          stopSignal,
          std::move(messageHandler),
          OwnedSocketTag{})
{
}

NetlinkInitialDump::NetlinkInitialDump(
    int dumpFd,
    EventFdSignal& stopSignal,
    MessageHandler messageHandler)
    : dumpFd_(dumpFd)
    , stopSignal_(stopSignal)
    , messageHandler_(std::move(messageHandler))
{
    if (dumpFd_ < 0) {
        throw std::invalid_argument(
            "NetlinkInitialDump: invalid borrowed descriptor");
    }

    if (!messageHandler_) {
        throw std::invalid_argument(
            "NetlinkInitialDump: message handler is empty");
    }
}

NetlinkInitialDump::NetlinkInitialDump(UniqueFd&& ownedDumpFd,EventFdSignal& stopSignal,MessageHandler messageHandler,OwnedSocketTag)
    : ownedDumpFd_(std::move(ownedDumpFd))
    , dumpFd_(ownedDumpFd_.get())
    , stopSignal_(stopSignal)
    , messageHandler_(std::move(messageHandler))
{
    if (!ownedDumpFd_.valid()) {
        throw std::invalid_argument(
            "NetlinkInitialDump: invalid owned descriptor");
    }

    if (!messageHandler_) {
        throw std::invalid_argument(
            "NetlinkInitialDump: message handler is empty");
    }
}

UniqueFd NetlinkInitialDump::openDumpSocket()
{
    UniqueFd fd(
        ::socket(
            AF_NETLINK,
            SOCK_RAW | SOCK_CLOEXEC,
            NETLINK_ROUTE));

    if (!fd.valid()) {
        const int error = errno;
        throw std::system_error(
            error,
            std::generic_category(),
            "NetlinkInitialDump: socket(AF_NETLINK) failed");
    }

    sockaddr_nl address{};
    address.nl_family = AF_NETLINK;

    if (::bind(
            fd.get(),
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) < 0) {
        const int error = errno;
        throw std::system_error(
            error,
            std::generic_category(),
            "NetlinkInitialDump: bind(AF_NETLINK) failed");
    }

    return fd;
}

NetlinkInitialDump::Result NetlinkInitialDump::run()
{
    auto result = requestDump<ifinfomsg>(
        RTM_GETLINK,
        AF_PACKET);

    if (!result.completed()) {
        return result;
    }

    result = requestDump<ifaddrmsg>(
        RTM_GETADDR,
        AF_INET);

    if (!result.completed()) {
        return result;
    }

    result = requestDump<ifaddrmsg>(
        RTM_GETADDR,
        AF_INET6);

    if (!result.completed()) {
        return result;
    }

    result = requestDump<ndmsg>(
        RTM_GETNEIGH,
        AF_UNSPEC);

    if (!result.completed()) {
        return result;
    }

    return requestDump<ndmsg>(
        RTM_GETNEIGH,
        AF_BRIDGE);
}

template<typename Message>
NetlinkInitialDump::Result NetlinkInitialDump::requestDump(
    std::uint16_t type,
    std::uint8_t family)
{
    struct Request {
        nlmsghdr header;
        Message message;
    };

    const std::uint32_t sequence = nextSequence_++;

    Request request{};
    request.header.nlmsg_len = NLMSG_LENGTH(sizeof(Message));
    request.header.nlmsg_type = type;
    request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    request.header.nlmsg_seq = sequence;

    setFamily(request.message, family);

    for (;;) {
        const ssize_t sent = ::send(
            dumpFd_,
            &request,
            request.header.nlmsg_len,
            0);

        if (sent == static_cast<ssize_t>(request.header.nlmsg_len)) {
            break;
        }

        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }

            return {
                Status::send_failed,
                errno,
            };
        }

        // Netlink messages must be sent atomically.
        return {
            Status::send_failed,
            EIO,
        };
    }

    return readDumpResponses(sequence);
}

NetlinkInitialDump::Result NetlinkInitialDump::readDumpResponses(
    std::uint32_t sequence)
{
    std::vector<char> buffer(16384);

    for (;;) {
        pollfd descriptors[2]{};

        descriptors[0].fd = dumpFd_;
        descriptors[0].events = POLLIN;

        descriptors[1].fd = stopSignal_.fd();
        descriptors[1].events = POLLIN;

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

        if ((descriptors[1].revents & POLLIN) != 0) {
            const int drainError = stopSignal_.drain();

            if (drainError != 0) {
                return {
                    Status::stop_drain_failed,
                    drainError,
                };
            }

            return {
                Status::interrupted,
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

        const ssize_t received = ::recv(
            dumpFd_,
            buffer.data(),
            buffer.size(),
            0);

        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }

            return {
                Status::receive_failed,
                errno,
            };
        }

        if (received == 0) {
            return {
                Status::receive_failed,
                ECONNRESET,
            };
        }

        int remaining = static_cast<int>(received);

        for (auto* header =
                 reinterpret_cast<nlmsghdr*>(buffer.data());
             NLMSG_OK(header, remaining);
             header = NLMSG_NEXT(header, remaining)) {
            if (header->nlmsg_seq != sequence) {
                continue;
            }

            if (header->nlmsg_type == NLMSG_DONE) {
                if ((header->nlmsg_flags & NLM_F_DUMP_INTR) != 0) {
                    return {Status::dump_interrupted,EINTR,};
                }
                return {Status::completed,0,};
            }

            if (header->nlmsg_type == NLMSG_ERROR) {
                if (header->nlmsg_len <
                    NLMSG_LENGTH(sizeof(nlmsgerr))) {
                    return {Status::malformed_message,EPROTO,};
                }

                const auto* netlinkError = reinterpret_cast<const nlmsgerr*>(NLMSG_DATA(header));

                // A zero-valued NLMSG_ERROR is a successful ACK.
                if (netlinkError->error == 0) {
                    continue;
                }

                return { Status::kernel_error, positiveKernelError(netlinkError->error),};
            }

            messageHandler_(header);
        }

        // NLMSG_NEXT updates remaining. Nonzero trailing data means the
        // datagram did not contain a complete sequence of netlink messages.
        if (remaining != 0) {
            return {Status::malformed_message,EPROTO,};
        }
    }
}

const char* NetlinkInitialDump::statusName(const Status status) noexcept
{
    switch (status) {
        case Status::completed:
            return "completed";

        case Status::interrupted:
            return "interrupted";

        case Status::dump_interrupted:
            return "dump_interrupted";

        case Status::send_failed:
            return "send_failed";

        case Status::poll_failed:
            return "poll_failed";

        case Status::receive_failed:
            return "receive_failed";

        case Status::malformed_message:
            return "malformed_message";

        case Status::kernel_error:
            return "kernel_error";

        case Status::stop_drain_failed:
            return "stop_drain_failed";
    }

    return "unknown";
}

} // namespace RSCGroup