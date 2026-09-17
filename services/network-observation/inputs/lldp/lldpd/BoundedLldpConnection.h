//
// Created by vvass on 17-Sep-26.
//

#pragma once

#include <lldpctl.h>

#include <sys/un.h>

#include <chrono>
#include <memory>
#include <string_view>

namespace RSCGroup {
/**
 * @brief Synchronous lldpctl connection with bounded IO.
 *
 * liblldpctl's default synchronous transport (nullptr callbacks) blocks
 * without any timeout and exposes no fd to configure one — a hung (not
 * dead) lldpd wedges the caller forever.  This class owns the unix socket
 * itself and drives the library through user-supplied send/recv callbacks,
 * so every blocking step is bounded by a timeout.
 *
 * Non-copyable: liblldpctl forbids using one connection from several
 * threads, and the callbacks capture `this`.
 */
class BoundedLldpConnection {
public:
    BoundedLldpConnection(std::string_view ctlname, std::chrono::milliseconds connectTimeout,
                          std::chrono::milliseconds ioTimeout);

    ~BoundedLldpConnection();
    BoundedLldpConnection(const BoundedLldpConnection &) = delete;
    BoundedLldpConnection &operator=(const BoundedLldpConnection &) = delete;
    BoundedLldpConnection(BoundedLldpConnection &&) = delete;
    BoundedLldpConnection &operator=(BoundedLldpConnection &&) = delete;

    /// One bounded request/response round-trip. False on any failure,
    /// including IO timeout (lldpd hung) and connect failure (lldpd dead).
    [[nodiscard]] bool QueryInterfacesOk() const;

    /// Raw access for reuse by bounded enumerate/refresh paths.
    [[nodiscard]] lldpctl_conn_t *connection() const { return conn_.get(); }

private:
    static void connectBounded(int fd, const sockaddr_un &addr, std::chrono::milliseconds timeout);
    static void setIoTimeouts(int fd, std::chrono::milliseconds timeout);
    static ssize_t sendCb(lldpctl_conn_t *, const uint8_t *data, size_t length, void *userData);
    static ssize_t recvCb(lldpctl_conn_t *, const uint8_t *data, size_t length, void *userData);

    int fd_ = -1;
    std::unique_ptr<lldpctl_conn_t, decltype(&lldpctl_release)> conn_{nullptr, &lldpctl_release};
};
} // namespace RSCGroup
