//
// Created by vvass on 17-Sep-26.
//

#include "BoundedLldpConnection.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>
#include <system_error>

namespace RSCGroup {


// --- ctor: create the wakeup fd up front, but stay Bounded for setup ---
BoundedLldpConnection::BoundedLldpConnection(std::string_view ctlname,
                                             std::chrono::milliseconds connectTimeout,
                                             std::chrono::milliseconds ioTimeout,
                                             Mode mode) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        throw std::system_error(errno, std::generic_category(), "BoundedLldpConnection: socket");
    }
    int wake = -1;
    try {
        if (mode == Mode::Interruptible) {
            wake = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
            if (wake < 0) {
                throw std::system_error(errno, std::generic_category(),
                                        "BoundedLldpConnection: eventfd");
            }
        }

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        // Filesystem paths only: abstract sockets (leading NUL) are not
        // supported here and are not produced by lldpctl_get_default_transport().
        if (ctlname.size() >= sizeof(addr.sun_path)) {
            throw std::system_error(std::make_error_code(std::errc::filename_too_long),
                                    "BoundedLldpConnection: ctl socket path");
        }
        std::memcpy(addr.sun_path, ctlname.data(), ctlname.size());

        connectBounded(fd, addr, connectTimeout);
        // Setup always runs bounded — including, for a watch connection,
        // the lldpctl_watch_callback2 subscribe round-trip. This is the
        // step that hangs makeWatch() against a wedged lldpd.
        setIoTimeouts(fd, ioTimeout);

        conn_.reset(::lldpctl_new_name(std::string(ctlname).c_str(),
                                       &BoundedLldpConnection::sendCb,
                                       &BoundedLldpConnection::recvCb,
                                       this));
        if (!conn_) {
            throw std::system_error(std::make_error_code(std::errc::not_enough_memory),
                                    "BoundedLldpConnection: lldpctl_new_name");
        }
    } catch (...) {
        if (wake >= 0) ::close(wake);
        ::close(fd);
        throw;
    }
    fd_ = fd;
    wakeupFd_ = wake;
    mode_ = mode;   // still behaving Bounded until enterInterruptibleMode()
}

BoundedLldpConnection::~BoundedLldpConnection() {
    conn_.reset();  // release library connection while fd is valid
    if (wakeupFd_ >= 0) ::close(wakeupFd_);
    if (fd_ >= 0) ::close(fd_);
}

void BoundedLldpConnection::enterInterruptibleMode() {
    if (mode_ != Mode::Interruptible) return;
    // Clear SO_RCVTIMEO/SO_SNDTIMEO: from here recv blocks in waitReadable()
    // until data arrives or unblock() fires. A socket timeout would busy-poll
    // the watch loop on an idle link.
    setIoTimeouts(fd_, std::chrono::milliseconds{0});   // {0,0} == no timeout
}

void BoundedLldpConnection::unblock() noexcept {
    if (wakeupFd_ < 0) return;
    unblocked_.store(true, std::memory_order_release);
    const std::uint64_t one = 1;
    // Sticky: EAGAIN only if the counter saturated, which already means woken.
    (void)::write(wakeupFd_, &one, sizeof(one));
}

bool BoundedLldpConnection::waitReadable() const {
    for (;;) {
        if (unblocked_.load(std::memory_order_acquire)) return false;
        pollfd pfds[2]{{fd_, POLLIN, 0}, {wakeupFd_, POLLIN, 0}};
        const int rc = ::poll(pfds, 2, -1);   // no deadline: idle is normal
        if (rc < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (pfds[1].revents != 0) return false;             // unblock()
        if (pfds[0].revents & (POLLIN | POLLHUP | POLLERR)) return true;
    }
}

ssize_t BoundedLldpConnection::recvCb(lldpctl_conn_t *, const uint8_t *data,
                                      size_t length, void *userData) {
    auto *self = static_cast<BoundedLldpConnection *>(userData);
    if (!data || length == 0) return LLDPCTL_ERR_CALLBACK_FAILURE;

    // Interruptible mode only: park in poll() until readable or unblocked.
    // In Bounded mode SO_RCVTIMEO does the bounding and we fall straight
    // through to recv().
    if (self->mode_ == Mode::Interruptible && self->wakeupFd_ >= 0) {
        if (!self->waitReadable()) return LLDPCTL_ERR_EOF;   // unwinds the loop
    }

    // The const on `data` is a liblldpctl API wart: per the
    // lldpctl_recv_callback doc it IS the buffer we must fill.
    for (;;) {
        const ssize_t n = ::recv(self->fd_, const_cast<uint8_t *>(data), length, 0);
        if (n > 0) return n;
        if (n == 0) return LLDPCTL_ERR_EOF;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return LLDPCTL_ERR_WOULDBLOCK;
        if (errno == ECONNRESET) return LLDPCTL_ERR_EOF;
        return LLDPCTL_ERR_CALLBACK_FAILURE;
    }
}



/// One bounded request/response round-trip. False on any failure,
/// including IO timeout (lldpd hung) and connect failure (lldpd dead).
bool BoundedLldpConnection::QueryInterfacesOk() const {
    lldpctl_atom_t *interfaces = ::lldpctl_get_interfaces(conn_.get());
    if (!interfaces) {
        return false; // last_error: WOULDBLOCK (timeout), EOF, ...
    }
    ::lldpctl_atom_dec_ref(interfaces);
    return true;
}

void BoundedLldpConnection::connectBounded(int fd, const sockaddr_un &addr, std::chrono::milliseconds timeout) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        throw std::system_error(errno, std::generic_category(), "fcntl");
    }
    // Dead lldpd fails fast here (ENOENT/ECONNREFUSED). A hung lldpd
    // that stopped accept()ing fills the backlog — then connect blocks,
    // hence non-blocking + poll.
    int rc = ::connect(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));
    if (rc < 0 && errno == EINPROGRESS) {
        pollfd pfd{fd, POLLOUT, 0};
        rc = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
        if (rc == 0) {
            throw std::system_error(std::make_error_code(std::errc::timed_out),
                                    "BoundedLldpConnection: connect");
        }
        if (rc < 0) {
            throw std::system_error(errno, std::generic_category(), "poll");
        }
        int err = 0;
        socklen_t len = sizeof(err);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
            throw std::system_error(errno, std::generic_category(), "getsockopt");
        }
        if (err != 0) {
            throw std::system_error(err, std::generic_category(), "connect");
        }
    } else if (rc < 0) {
        throw std::system_error(errno, std::generic_category(), "connect");
    }
    if (::fcntl(fd, F_SETFL, flags) < 0) {
        // restore blocking mode;
        throw std::system_error(errno, std::generic_category(), "fcntl restore");
    } // per-IO bound is SO_RCVTIMEO
}

void BoundedLldpConnection::setIoTimeouts(int fd, std::chrono::milliseconds timeout) {
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    const auto usecs = std::chrono::duration_cast<std::chrono::microseconds>(timeout - secs);
    const timeval tv{
        static_cast<time_t>(secs.count()),
        static_cast<suseconds_t>(usecs.count())
    };
    if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0 ||
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        throw std::system_error(errno, std::generic_category(), "setsockopt");
    }
}

ssize_t BoundedLldpConnection::sendCb(lldpctl_conn_t *, const uint8_t *data, size_t length,
                                      void *userData) {
    auto *self = static_cast<BoundedLldpConnection *>(userData);
    for (;;) {
        const ssize_t n = ::send(self->fd_, data, length, MSG_NOSIGNAL);
        if (n >= 0) return n;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return LLDPCTL_ERR_WOULDBLOCK;
        if (errno == EPIPE || errno == ECONNRESET) return LLDPCTL_ERR_EOF;
        return LLDPCTL_ERR_CALLBACK_FAILURE;
    }
}

} // namespace RSCGroup