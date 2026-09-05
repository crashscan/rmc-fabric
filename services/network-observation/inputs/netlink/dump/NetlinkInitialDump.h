//
// Created by vvass on 05-Sep-26.
//

#pragma once

#include <EventFdSignal.h>
#include <UniqueFd.h>

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
         * Zero is used when the status does not carry an error code, such as
         * completed or interrupted.
         */
        int error{0};

        [[nodiscard]] bool completed() const noexcept { return status == Status::completed; }
        [[nodiscard]] bool interrupted() const noexcept { return status == Status::interrupted; }
    };

    using MessageHandler = std::function<void(const nlmsghdr*)>;

    /**
     * Production constructor.
     *
     * Creates, binds, and owns an AF_NETLINK/NETLINK_ROUTE dump socket.
     *
     * @throws std::system_error if socket creation or binding fails.
     * @throws std::invalid_argument if messageHandler is empty.
     */
    NetlinkInitialDump(EventFdSignal& stopSignal, MessageHandler messageHandler);

    /**
     * Test/injection constructor.
     *
     * Borrows dumpFd. The caller retains ownership and must keep the
     * descriptor open until run() returns. Destruction of this object does
     * not close dumpFd.
     *
     * @throws std::invalid_argument if dumpFd is negative or messageHandler
     *         is empty.
     */
    NetlinkInitialDump(int dumpFd,EventFdSignal& stopSignal,MessageHandler messageHandler);

    ~NetlinkInitialDump() = default;

    NetlinkInitialDump(const NetlinkInitialDump&) = delete;
    NetlinkInitialDump& operator=(const NetlinkInitialDump&) = delete;
    NetlinkInitialDump(NetlinkInitialDump&&) = delete;
    NetlinkInitialDump& operator=(NetlinkInitialDump&&) = delete;

    /**
     * Executes all initial dump transactions.
     *
     * Requests are issued in this order:
     *  1. RTM_GETLINK / AF_PACKET
     *  2. RTM_GETADDR / AF_INET
     *  3. RTM_GETADDR / AF_INET6
     *  4. RTM_GETNEIGH / AF_UNSPEC
     *  5. RTM_GETNEIGH / AF_BRIDGE
     */
    [[nodiscard]] Result run();

    [[nodiscard]] static const char* statusName(Status status) noexcept;

private:
    struct OwnedSocketTag {};

    /**
     * Delegating constructor used by the production constructor.
     */
    NetlinkInitialDump(UniqueFd&& ownedDumpFd,EventFdSignal& stopSignal,MessageHandler messageHandler,OwnedSocketTag);

    template<typename Message>
    [[nodiscard]] Result requestDump(std::uint16_t type,std::uint8_t family);
    [[nodiscard]] Result readDumpResponses(std::uint32_t sequence);

    /**
     * Creates and binds the production netlink dump socket.
     */
    [[nodiscard]] static UniqueFd openDumpSocket();

    // Engaged only for the production constructor. It is intentionally
    // declared before dumpFd_ so dumpFd_ may safely refer to it.
    UniqueFd ownedDumpFd_;

    // Used by all operations. It either refers to ownedDumpFd_ or to a
    // descriptor borrowed from the caller.
    int dumpFd_{-1};

    EventFdSignal& stopSignal_;
    MessageHandler messageHandler_;
    std::uint32_t nextSequence_{1};
};

} // namespace RSCGroup