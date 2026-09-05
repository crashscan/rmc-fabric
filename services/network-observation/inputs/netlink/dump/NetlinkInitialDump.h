//
// Created by vvass on 05-Sep-26.
//

#pragma once

#include "NetlinkIo.h"

#include <EventFdSignal.h>

#include <cstdint>
#include <functional>

struct nlmsghdr;

namespace RSCGroup {

/**
 * Executes the initial NETLINK_ROUTE snapshot dump.
 *
 * The dump requests links, IPv4 addresses, IPv6 addresses, general
 * neighbors, and bridge FDB entries in sequence. Matching response messages
 * are forwarded to the supplied handler.
 *
 * This class is not thread-safe. The message handler is invoked synchronously
 * from run(). The nlmsghdr pointer passed to the handler is valid only for the
 * duration of the callback.
 */
class NetlinkInitialDump {
public:
    enum class Status {
        completed,
        interrupted,
        dump_interrupted,
        send_failed,
        poll_failed,
        receive_failed,
        malformed_message,
        kernel_error,
        stop_drain_failed,
    };

    struct Result {
        Status status{Status::completed};

        /**
         * Positive errno value for local and kernel errors.
         *
         * Zero is used when the status does not carry an error code.
         */
        int error{0};

        [[nodiscard]] bool completed() const noexcept
        {
            return status == Status::completed;
        }

        [[nodiscard]] bool interrupted() const noexcept
        {
            return status == Status::interrupted;
        }
    };

    using MessageHandler =
        std::function<void(const nlmsghdr*)>;

    /**
     * Creates, binds, and owns an AF_NETLINK/NETLINK_ROUTE dump socket.
     */
    NetlinkInitialDump(
        EventFdSignal& stopSignal,
        MessageHandler messageHandler);

    /**
     * Borrows dumpFd. The caller must keep it open until run() returns.
     */
    NetlinkInitialDump(
        int dumpFd,
        EventFdSignal& stopSignal,
        MessageHandler messageHandler);

    ~NetlinkInitialDump() = default;

    NetlinkInitialDump(
        const NetlinkInitialDump&) = delete;

    NetlinkInitialDump& operator=(
        const NetlinkInitialDump&) = delete;

    NetlinkInitialDump(
        NetlinkInitialDump&&) = delete;

    NetlinkInitialDump& operator=(
        NetlinkInitialDump&&) = delete;

    /**
     * Executes these transactions in order:
     *
     *  1. RTM_GETLINK  / AF_PACKET
     *  2. RTM_GETADDR  / AF_INET
     *  3. RTM_GETADDR  / AF_INET6
     *  4. RTM_GETNEIGH / AF_UNSPEC
     *  5. RTM_GETNEIGH / AF_BRIDGE
     */
    [[nodiscard]] Result run();

    [[nodiscard]] static const char* statusName(
        Status status) noexcept;

private:
    template<typename Message>
    [[nodiscard]] Result requestDump(
        std::uint16_t type,
        std::uint8_t family);

    [[nodiscard]] Result readDumpResponses(
        std::uint32_t sequence);

    void validateHandler() const;

    NetlinkRouteSocket socket_;
    EventFdSignal& stopSignal_;
    MessageHandler messageHandler_;
    std::uint32_t nextSequence_{1};
};

} // namespace RSCGroup
