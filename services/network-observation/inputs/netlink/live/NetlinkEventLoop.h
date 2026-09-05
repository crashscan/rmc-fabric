//
// Created by vvass on 05-Sep-26.
//

#pragma once

#include "NetlinkIo.h"

#include <EventFdSignal.h>

#include <functional>
#include <span>
#include <stop_token>

struct nlmsghdr;

namespace RSCGroup {

/**
 * Owns and runs the subscribed NETLINK_ROUTE live-event socket.
 *
 * The production constructor creates and owns the socket. The injection
 * constructor borrows a caller-owned descriptor for tests.
 *
 * This class is not thread-safe. The message handler is invoked synchronously
 * from run(), and each nlmsghdr pointer is valid only during the callback.
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
        int error{0};

        [[nodiscard]] bool stopped() const noexcept
        {
            return status == Status::stopped;
        }
    };

    using MessageHandler =
        std::function<void(const nlmsghdr*)>;

    /**
     * Creates, binds, subscribes, and owns a NETLINK_ROUTE socket.
     */
    NetlinkEventLoop(
        EventFdSignal& stopSignal,
        MessageHandler messageHandler);

    /**
     * Borrows liveFd. The caller must keep it open until run() returns.
     */
    NetlinkEventLoop(
        int liveFd,
        EventFdSignal& stopSignal,
        MessageHandler messageHandler);

    ~NetlinkEventLoop() = default;

    NetlinkEventLoop(
        const NetlinkEventLoop&) = delete;

    NetlinkEventLoop& operator=(
        const NetlinkEventLoop&) = delete;

    NetlinkEventLoop(
        NetlinkEventLoop&&) = delete;

    NetlinkEventLoop& operator=(
        NetlinkEventLoop&&) = delete;

    /**
     * Runs until stop is requested or an I/O/protocol failure occurs.
     *
     * Requesting the stop token must also signal stopSignal so a blocking
     * poll can wake promptly.
     */
    [[nodiscard]] Result run(
        std::stop_token stopToken);

    [[nodiscard]] static const char* statusName(
        Status status) noexcept;

private:
    [[nodiscard]] Result mapWaitFailure(
        const NetlinkWaitResult& result) const noexcept;

    [[nodiscard]] bool receiveAndDispatch(
        std::span<char> buffer,
        Result& terminalResult);

    void validateHandler() const;

    NetlinkRouteSocket socket_;
    EventFdSignal& stopSignal_;
    MessageHandler messageHandler_;
};

} // namespace RSCGroup
