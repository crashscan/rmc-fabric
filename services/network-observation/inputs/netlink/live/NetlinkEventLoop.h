//
// Created by vvass on 05-Sep-26.
//

#pragma once

#include <EventFdSignal.h>
#include <UniqueFd.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <stop_token>

struct nlmsghdr;

namespace RSCGroup {

/**
 * Owns and runs the subscribed NETLINK_ROUTE live-event socket.
 *
 * The production constructor creates and owns the netlink socket. The
 * injection constructor borrows a caller-owned descriptor for tests.
 *
 * This class is not thread-safe. run() must be called by at most one thread.
 * The message handler is invoked synchronously from run(). The nlmsghdr
 * pointer remains valid only for the duration of the callback.
 */
class NetlinkEventLoop {
public:
    enum class Status {
        stopped,
        poll_failed,
        receive_failed,
        malformed_message,
        truncated_message,
        socket_overrun,
        stop_drain_failed,
    };

    struct Result {
        Status status{Status::stopped};

        /**
         * Positive errno value when the status represents an error.
         * Zero is used for an ordinary stop.
         */
        int error{0};

        [[nodiscard]] bool stopped() const noexcept
        {
            return status == Status::stopped;
        }
    };

    using MessageHandler = std::function<void(const nlmsghdr*)>;

    /**
     * Production constructor.
     *
     * Creates, binds, subscribes, and owns an AF_NETLINK/NETLINK_ROUTE socket.
     *
     * @throws std::system_error if socket creation or binding fails.
     * @throws std::invalid_argument if messageHandler is empty.
     */
    NetlinkEventLoop(
        EventFdSignal& stopSignal,
        MessageHandler messageHandler);

    /**
     * Test/injection constructor.
     *
     * Borrows liveFd. The caller retains ownership and must keep the
     * descriptor open until run() returns. Destruction of this object does
     * not close liveFd.
     *
     * @throws std::invalid_argument if liveFd is negative or messageHandler
     *         is empty.
     */
    NetlinkEventLoop(
        int liveFd,
        EventFdSignal& stopSignal,
        MessageHandler messageHandler);

    ~NetlinkEventLoop() = default;

    NetlinkEventLoop(const NetlinkEventLoop&) = delete;
    NetlinkEventLoop& operator=(const NetlinkEventLoop&) = delete;
    NetlinkEventLoop(NetlinkEventLoop&&) = delete;
    NetlinkEventLoop& operator=(NetlinkEventLoop&&) = delete;

    /**
     * Runs until stop is requested or an I/O/protocol failure occurs.
     *
     * The stop token is cooperative. The owner must also signal stopSignal
     * when requesting stop so that a blocking poll() wakes promptly.
     */
    [[nodiscard]] Result run(std::stop_token stopToken);

    [[nodiscard]] static const char* statusName(Status status) noexcept;

private:
    struct OwnedSocketTag {};

    NetlinkEventLoop(
        UniqueFd&& ownedLiveFd,
        EventFdSignal& stopSignal,
        MessageHandler messageHandler,
        OwnedSocketTag);

    /**
     * Creates and binds the production live socket.
     *
     * The socket is subscribed before the initial dump begins, ensuring that
     * notifications arriving during the dump remain queued for the live loop.
     */
    [[nodiscard]] static UniqueFd openLiveSocket();

    /**
     * Receives and dispatches one datagram.
     *
     * Returns nullopt when processing should continue. Returns a Result when
     * the event loop must terminate.
     */
    [[nodiscard]] std::optional<Result> receiveAndDispatch(
        char* buffer,
        std::size_t bufferSize);

    // Engaged only when the production constructor is used.
    UniqueFd ownedLiveFd_;

    // Refers either to ownedLiveFd_ or to a caller-owned descriptor.
    int liveFd_{-1};

    EventFdSignal& stopSignal_;
    MessageHandler messageHandler_;
};

} // namespace RSCGroup
