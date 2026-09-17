#pragma once

#include <lldpctl.h>

#include <sys/un.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string_view>

#include <EventFdSignal.h>
#include <UniqueFd.h>

namespace RSCGroup {
/**
 * @brief Synchronous lldpctl connection with bounded IO.
 *
 * liblldpctl's default synchronous transport (nullptr callbacks) blocks
 * without any timeout and exposes no fd to configure one — a hung (not
 * dead) lldpd wedges the caller forever. This class owns the unix socket
 * itself and drives the library through user-supplied send/recv callbacks.
 *
 * Two IO modes, because the two use sites need opposite things:
 *
 *  - Mode::Bounded (probe, enumeration): SO_RCVTIMEO/SO_SNDTIMEO. Every
 *    blocking step returns within ioTimeout.
 *
 *  - Mode::Interruptible (watch): NO socket timeout — lldpctl_watch() is
 *    meant to block until the next notification, and a recv timeout would
 *    turn the watch loop into a busy-poll on an idle link. Instead recv
 *    waits on poll(socket, wakeup-eventfd) with no deadline, and unblock()
 *    releases it. Setup (connect + the lldpctl_watch_callback2 subscribe
 *    round-trip) still runs Bounded, then switches.
 *
 * Non-copyable: liblldpctl forbids using one connection from several
 * threads, and the callbacks capture `this`.
 */
class BoundedLldpConnection {
public:
    enum class Mode { Bounded, Interruptible };

    BoundedLldpConnection(std::string_view ctlname,
                          std::chrono::milliseconds connectTimeout,
                          std::chrono::milliseconds ioTimeout,
                          Mode mode = Mode::Bounded);

    ~BoundedLldpConnection();
    BoundedLldpConnection(const BoundedLldpConnection &) = delete;
    BoundedLldpConnection &operator=(const BoundedLldpConnection &) = delete;
    BoundedLldpConnection(BoundedLldpConnection &&) = delete;
    BoundedLldpConnection &operator=(BoundedLldpConnection &&) = delete;

    /// One bounded request/response round-trip. False on any failure,
    /// including IO timeout (lldpd hung) and connect failure (lldpd dead).
    [[nodiscard]] bool QueryInterfacesOk() const;

    /**
     * @brief Leave setup mode: drop socket timeouts, recv now waits on
     *        poll(socket, wakeupFd) instead.
     *
     * Call once, after every bounded setup round-trip has completed.
     * No-op when constructed with Mode::Bounded.
     */
    void enterInterruptibleMode();

    /**
     * @brief Wake a recv blocked in interruptible mode; it returns
     *        LLDPCTL_ERR_EOF so the caller's watch loop unwinds.
     *
     * Sticky and idempotent — once unblocked, subsequent recvs return
     * immediately. Async-signal-safe (a single eventfd write).
     */
    void unblock() noexcept;

    /// Raw access for reuse by bounded enumerate/refresh/watch paths.
    /// Never outlive this object; prefer the callback-form helpers.
    [[nodiscard]] lldpctl_conn_t *connection() const { return conn_.get(); }

private:
    /// Throws on failure. Static so it can run in the member-init list.
    [[nodiscard]] static UniqueFd makeSocket();
    /// nullopt for Mode::Bounded — no wakeup descriptor is needed.
    [[nodiscard]] static std::optional<EventFdSignal> makeWakeupSignal(Mode mode);
    static void connectBounded(int fd, const sockaddr_un &addr,
                               std::chrono::milliseconds timeout);
    static void setIoTimeouts(int fd, std::chrono::milliseconds timeout);
    static ssize_t sendCb(lldpctl_conn_t *, const uint8_t *data, size_t length, void *userData);
    static ssize_t recvCb(lldpctl_conn_t *, const uint8_t *data, size_t length, void *userData);

    /// Blocks until the socket is readable or unblock() is called.
    /// Returns false when woken by unblock() (or on poll error).
    [[nodiscard]] bool waitReadable() const;
    // Immutable after construction. recvCb reads these from the watch
    // thread AND, during the subscribe round-trip, from the constructing
    // thread; const removes the data race by construction.
    const UniqueFd fd_;
    /// Present only in Mode::Interruptible. EventFdSignal owns the eventfd
    /// and its EAGAIN-is-benign write semantics.
    const std::optional<EventFdSignal> wakeupSignal_;
    const Mode mode_;

    std::atomic<bool> unblocked_{false};
    // MUST remain last: destroyed first, so the library connection is
    // released while both descriptors are still open.
    std::unique_ptr<lldpctl_conn_t, decltype(&lldpctl_release)> conn_{nullptr, &lldpctl_release};
};
} // namespace RSCGroup
