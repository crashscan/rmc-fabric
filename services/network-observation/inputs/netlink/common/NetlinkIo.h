//
// Created by vvass on 05-Sep-26.
//

#pragma once

#include <EventFdSignal.h>
#include <UniqueFd.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace RSCGroup {

/**
 * Owns or borrows a NETLINK_ROUTE socket.
 *
 * A socket returned by open() owns its descriptor. A socket constructed from
 * an int borrows that descriptor and does not close it.
 */
class NetlinkRouteSocket {
public:
    struct OpenOptions {
        std::uint32_t groups{0};
        std::optional<int> receiveBufferBytes;
    };

    /**
     * Creates and binds an unconfigured NETLINK_ROUTE socket.
     */
    [[nodiscard]] static NetlinkRouteSocket open();

    /**
     * Creates, configures, and binds a NETLINK_ROUTE socket.
     *
     * Socket creation and bind failures throw std::system_error. Failure to
     * apply the optional receive-buffer size is logged as a warning and does
     * not prevent socket creation.
     */
    [[nodiscard]] static NetlinkRouteSocket open(
        OpenOptions options);

    /**
     * Borrows an existing descriptor.
     *
     * @throws std::invalid_argument if fd is negative.
     */
    explicit NetlinkRouteSocket(int fd);

    ~NetlinkRouteSocket() = default;

    NetlinkRouteSocket(const NetlinkRouteSocket&) = delete;
    NetlinkRouteSocket& operator=(
        const NetlinkRouteSocket&) = delete;

    NetlinkRouteSocket(
        NetlinkRouteSocket&& other) noexcept;

    NetlinkRouteSocket& operator=(
        NetlinkRouteSocket&& other) noexcept;

    [[nodiscard]] int fd() const noexcept
    {
        return fd_;
    }

    [[nodiscard]] bool ownsDescriptor() const noexcept
    {
        return ownedFd_.valid();
    }

private:
    explicit NetlinkRouteSocket(
        UniqueFd&& ownedFd) noexcept;

    UniqueFd ownedFd_;
    int fd_{-1};
};

enum class NetlinkWaitStatus {
    data_ready,
    stopped,
    poll_failed,
    data_fd_failed,
    stop_fd_failed,
    stop_drain_failed,
};

struct NetlinkWaitResult {
    NetlinkWaitStatus status{
        NetlinkWaitStatus::data_ready};

    int error{0};

    [[nodiscard]] bool dataReady() const noexcept
    {
        return status == NetlinkWaitStatus::data_ready;
    }

    [[nodiscard]] bool stopped() const noexcept
    {
        return status == NetlinkWaitStatus::stopped;
    }
};

/**
 * Polls a netlink data descriptor together with an EventFdSignal.
 *
 * EINTR is retried. If both descriptors become readable, shutdown takes
 * priority. A readable stop signal is drained before stopped is returned.
 */
[[nodiscard]] NetlinkWaitResult
waitForNetlinkDataOrStop(
    int dataFd,
    EventFdSignal& stopSignal);

enum class NetlinkReceiveStatus {
    received,
    failed,
    closed,
    truncated,
};

struct NetlinkReceiveResult {
    NetlinkReceiveStatus status{
        NetlinkReceiveStatus::received};

    std::size_t size{0};
    int error{0};

    [[nodiscard]] bool received() const noexcept
    {
        return status == NetlinkReceiveStatus::received;
    }
};

/**
 * Receives one complete datagram with recvmsg().
 *
 * EINTR is retried and MSG_TRUNC is reported explicitly.
 */
[[nodiscard]] NetlinkReceiveResult
receiveNetlinkDatagram(
    int fd,
    std::span<char> buffer);

} // namespace RSCGroup
