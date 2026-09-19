//
// Created by vvass on 17-Sep-26.
//

#include "BoundedLldpWatch.h"

#include <glog/logging.h>

#include <system_error>

namespace RSCGroup {

BoundedLldpWatch::BoundedLldpWatch(std::string_view ctlname,
                                   std::chrono::milliseconds connectTimeout,
                                   std::chrono::milliseconds ioTimeout)
    : BoundedLldpWatch(ctlname, connectTimeout, ioTimeout, ChangeCallback{}) {
}

void BoundedLldpWatch::setCallback(ChangeCallback callback) {
    std::scoped_lock lk(callbackMutex_);
    callback_ = std::move(callback);
}

BoundedLldpWatch::BoundedLldpWatch(std::string_view ctlname,
                                   std::chrono::milliseconds connectTimeout,
                                   std::chrono::milliseconds ioTimeout,
                                   ChangeCallback callback)
    : callback_(std::move(callback)) {
    // Bounded during setup. Throws on a dead or hung backend instead of
    // parking the supervision thread.
    conn_ = std::make_unique<BoundedLldpConnection>(
        ctlname, connectTimeout, ioTimeout,
        BoundedLldpConnection::Mode::Interruptible);

    if (::lldpctl_watch_callback2(conn_->connection(), &BoundedLldpWatch::trampoline,
                                  static_cast<void *>(this)) < 0) {
        throw std::system_error(
            make_error_code(lldpctl_last_error(conn_->connection())),
            "BoundedLldpWatch: lldpctl_watch_callback2");
    }

    // Subscribe round-trip is done; switch to interruptible blocking so an
    // idle link does not busy-poll on SO_RCVTIMEO.
    conn_->enterInterruptibleMode();

    thread_ = std::jthread{
        [this](std::stop_token stop) {
            while (!stop.stop_requested()) {
                if (::lldpctl_watch(conn_->connection()) < 0) {
                    // EOF here is the normal shutdown path (unblock()); anything
                    // else means the backend went away. Either way the watch is
                    // over — LldpdSource's liveness probe re-acquires.
                    if (!stop.stop_requested()) {
                        LOG(WARNING) << "BoundedLldpWatch: watch ended: "
                                << ::lldpctl_strerror(lldpctl_last_error(conn_->connection()));
                    }
                    return;
                }
            }
        }
    };
}

BoundedLldpWatch::~BoundedLldpWatch() {
    thread_.request_stop();
    // Two wakeups, both needed: sync_unblock releases a wait inside the
    // library, unblock() releases our own poll() in recvCb.
    if (conn_) {
        ::lldpctl_watch_sync_unblock(conn_->connection());
        conn_->unblock();
    }
    if (thread_.joinable()) thread_.join();
    conn_.reset(); // only after the loop thread is gone
}

void BoundedLldpWatch::trampoline(lldpctl_change_t change,
                                  lldpctl_atom_t *interface,
                                  lldpctl_atom_t *neighbor,
                                  void *userData) {
    auto *self = static_cast<BoundedLldpWatch *>(userData);
    if (!self) {
        return;
    }

    // Copy under the lock, dispatch outside it. Holding callbackMutex_
    // across the callback would deadlock a downstream handler that
    // re-enters the owner and triggers a setCallback().
    ChangeCallback callback;
    {
        std::scoped_lock lk(self->callbackMutex_);
        callback = self->callback_;
    }
    if (!callback) {
        return;   // detached — owner is mid-reconnect
    }

    try {
        // Non-owning wrappers: the library owns these atoms for the duration
        // of the callback only. Callees must not retain them.
        const lldpcli::LldpAtom ifaceAtom(interface, false, nullptr);
        const lldpcli::LldpAtom neighborAtom(neighbor, false, nullptr);
        std::string_view ifname;
        if (const char *n = ::lldpctl_atom_get_str(interface, lldpctl_k_interface_name)) {
            ifname = n;
        }
        callback(ifname, change, ifaceAtom, neighborAtom);
    } catch (const std::exception &e) {
        // Must not propagate into the C library.
        LOG(ERROR) << "BoundedLldpWatch: callback exception: " << e.what();
    } catch (...) {
        LOG(ERROR) << "BoundedLldpWatch: callback unknown exception";
    }
}
} // namespace RSCGroup
