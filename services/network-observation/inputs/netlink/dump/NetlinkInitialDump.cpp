//
// Created by vvass on 05-Sep-26.
//

#include "NetlinkInitialDump.h"

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace RSCGroup {

namespace {

constexpr std::size_t dumpReceiveBufferSize = 16 * 1024;

void setFamily(
    ifinfomsg& message,
    std::uint8_t family) noexcept
{
    message.ifi_family = family;
}

void setFamily(
    ifaddrmsg& message,
    std::uint8_t family) noexcept
{
    message.ifa_family = family;
}

void setFamily(
    ndmsg& message,
    std::uint8_t family) noexcept
{
    message.ndm_family = family;
}

[[nodiscard]] int positiveKernelError(
    int error) noexcept
{
    return error < 0 ? -error : error;
}

} // namespace

NetlinkInitialDump::NetlinkInitialDump(
    EventFdSignal& stopSignal,
    MessageHandler messageHandler)
    : socket_(NetlinkRouteSocket::open())
    , stopSignal_(stopSignal)
    , messageHandler_(std::move(messageHandler))
{
    validateHandler();
}

NetlinkInitialDump::NetlinkInitialDump(
    int dumpFd,
    EventFdSignal& stopSignal,
    MessageHandler messageHandler)
    : socket_(dumpFd)
    , stopSignal_(stopSignal)
    , messageHandler_(std::move(messageHandler))
{
    validateHandler();
}

void NetlinkInitialDump::validateHandler() const
{
    if (!messageHandler_) {
        throw std::invalid_argument(
            "NetlinkInitialDump: message handler is empty");
    }
}

NetlinkInitialDump::Result
NetlinkInitialDump::run()
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
NetlinkInitialDump::Result
NetlinkInitialDump::requestDump(
    std::uint16_t type,
    std::uint8_t family)
{
    struct Request {
        nlmsghdr header;
        Message message;
    };

    const std::uint32_t sequence =
        nextSequence_++;

    Request request{};
    request.header.nlmsg_len =
        NLMSG_LENGTH(sizeof(Message));
    request.header.nlmsg_type = type;
    request.header.nlmsg_flags =
        NLM_F_REQUEST | NLM_F_DUMP;
    request.header.nlmsg_seq = sequence;

    setFamily(request.message, family);

    for (;;) {
        const ssize_t sent = ::send(
            socket_.fd(),
            &request,
            request.header.nlmsg_len,
            0);

        if (sent ==
            static_cast<ssize_t>(
                request.header.nlmsg_len)) {
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

        // Netlink datagrams must be sent atomically.
        return {
            Status::send_failed,
            EIO,
        };
    }

    return readDumpResponses(sequence);
}

NetlinkInitialDump::Result
NetlinkInitialDump::readDumpResponses(
    std::uint32_t sequence)
{
    std::vector<char> buffer(
        dumpReceiveBufferSize);

    for (;;) {
        const NetlinkWaitResult waitResult =
            waitForNetlinkDataOrStop(
                socket_.fd(),
                stopSignal_);

        switch (waitResult.status) {
            case NetlinkWaitStatus::data_ready:
                break;

            case NetlinkWaitStatus::stopped:
                return {
                    Status::interrupted,
                    0,
                };

            case NetlinkWaitStatus::stop_drain_failed:
                return {
                    Status::stop_drain_failed,
                    waitResult.error,
                };

            case NetlinkWaitStatus::poll_failed:
            case NetlinkWaitStatus::data_fd_failed:
            case NetlinkWaitStatus::stop_fd_failed:
                return {
                    Status::poll_failed,
                    waitResult.error,
                };
        }

        const NetlinkReceiveResult receiveResult =
            receiveNetlinkDatagram(
                socket_.fd(),
                std::span<char>{
                    buffer.data(),
                    buffer.size(),
                });

        switch (receiveResult.status) {
            case NetlinkReceiveStatus::received:
                break;

            case NetlinkReceiveStatus::truncated:
                return {
                    Status::malformed_message,
                    receiveResult.error,
                };

            case NetlinkReceiveStatus::failed:
            case NetlinkReceiveStatus::closed:
                return {
                    Status::receive_failed,
                    receiveResult.error,
                };
        }

        int remaining =
            static_cast<int>(receiveResult.size);

        for (auto* header =
                 reinterpret_cast<nlmsghdr*>(
                     buffer.data());
             NLMSG_OK(header, remaining);
             header =
                 NLMSG_NEXT(header, remaining)) {
            if (header->nlmsg_seq != sequence) {
                continue;
            }

            if (header->nlmsg_type == NLMSG_DONE) {
                if ((header->nlmsg_flags &
                     NLM_F_DUMP_INTR) != 0) {
                    return {
                        Status::dump_interrupted,
                        EINTR,
                    };
                }

                return {
                    Status::completed,
                    0,
                };
            }

            if (header->nlmsg_type ==
                NLMSG_ERROR) {
                if (header->nlmsg_len <
                    NLMSG_LENGTH(sizeof(nlmsgerr))) {
                    return {
                        Status::malformed_message,
                        EPROTO,
                    };
                }

                const auto* netlinkError =
                    reinterpret_cast<
                        const nlmsgerr*>(
                        NLMSG_DATA(header));

                // error == 0 is a successful ACK.
                if (netlinkError->error == 0) {
                    continue;
                }

                return {
                    Status::kernel_error,
                    positiveKernelError(
                        netlinkError->error),
                };
            }

            if (header->nlmsg_type ==
                NLMSG_OVERRUN) {
                return {
                    Status::receive_failed,
                    ENOBUFS,
                };
            }

            messageHandler_(header);
        }

        if (remaining != 0) {
            return {
                Status::malformed_message,
                EPROTO,
            };
        }
    }
}

const char* NetlinkInitialDump::statusName(
    Status status) noexcept
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
