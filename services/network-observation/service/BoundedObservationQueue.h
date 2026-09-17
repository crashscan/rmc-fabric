//
// Created by vvass on 17-Sep-26.
//
#pragma once

#include "ObservationTypes.h"

#include <BoundedQueue.h>   // rsc_util — adjust to the util include path

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <utility>
#include <variant>

namespace RSCGroup {
/// Origin of a queued observation. Drives resync accounting: on overflow the
/// bit of the DISCARDED item's source is raised, so the consumer can repair
/// the model by re-reading that source (netlink: snapshot; LLDP: refreshAll).
enum class ObservationSource : std::uint8_t {
    Netlink = 0,
    Lldp = 1,
};

[[nodiscard]] constexpr std::uint32_t sourceBit(ObservationSource source) noexcept {
    return std::uint32_t{1} << static_cast<std::uint8_t>(source);
}

using ObservationPayload = std::variant<
    LinkObservation,
    AddressObservation,
    NeighborObservation,
    FdbObservation,
    LldpObservation>;

struct QueuedItem {
    ObservationSource source;
    ObservationPayload payload;
};

/**
 * @brief Service-facing bounded observation queue.
 *
 * Generic drop-oldest storage (thread_safe::bounded_queue) plus per-source
 * resync accounting: when an item is discarded, the resync bit of the
 * DISCARDED item's source is raised. The consumer takes the mask with
 * takeResyncMask() and repairs the divergence by re-reading those sources.
 *
 * Non-copyable, non-movable (the drop hook captures `this`).
 */
class BoundedObservationQueue {
public:
    using Stats = thread_safe::bounded_queue<QueuedItem>::Stats;

    explicit BoundedObservationQueue(std::size_t capacity)
        : queue_(capacity, [this](const QueuedItem &discarded) {
            resyncMask_.fetch_or(sourceBit(discarded.source),
                                 std::memory_order_release);
        }) {
    }

    BoundedObservationQueue(const BoundedObservationQueue &) = delete;

    BoundedObservationQueue &operator=(const BoundedObservationQueue &) = delete;

    /// Never blocks. False exactly when an older item was discarded (its
    /// source's resync bit is now raised). After close(): silently ignored.
    bool push(QueuedItem item) { return queue_.push(std::move(item)); }

    /// Oldest item, waiting while empty; nullopt once closed AND drained,
    /// or when @p st is requested.
    std::optional<QueuedItem> waitPop(std::stop_token st = {}) {
        return queue_.waitPop(std::move(st));
    }

    /// Oldest item if immediately available.
    std::optional<QueuedItem> tryPop() { return queue_.tryPop(); }

    /// Discard the queued backlog without consuming it. Called by the
    /// consumer immediately after takeResyncMask() returns non-zero: the
    /// backlog predates the repair, and replaying it would both waste the
    /// headroom the resync traffic needs and risk re-overflowing into
    /// another resync cycle. Does not fire the drop hook, so it cannot
    /// re-raise the bits just cleared.
    void discardAll() { queue_.discardAll(); }

    /// Wake a blocked waitPop() without closing. This is the ManagedWorker
    /// Wake callback: close() is one-way and must never be used as the wake
    /// path, or the queue stays dead across stop/start and silently
    /// discards every push.
    void wake() noexcept { queue_.wake(); }

    /// Fetch and clear the accumulated resync bits. Taking the mask
    /// acknowledges the repair work about to be performed; any later drop
    /// re-raises its source's bit for the next cycle.
    std::uint32_t takeResyncMask() {
        return resyncMask_.exchange(0, std::memory_order_acq_rel);
    }

    [[nodiscard]] std::uint32_t resyncMask() const {
        return resyncMask_.load(std::memory_order_acquire);
    }

    void close() { queue_.close(); }
    [[nodiscard]] bool closed() const { return queue_.closed(); }
    [[nodiscard]] std::size_t size() const { return queue_.size(); }
    [[nodiscard]] std::size_t capacity() const { return queue_.capacity(); }
    [[nodiscard]] Stats stats() const { return queue_.stats(); }

private:
    // Declared first: the drop hook registered with queue_ touches this.
    std::atomic<std::uint32_t> resyncMask_{0};
    thread_safe::bounded_queue<QueuedItem> queue_;
};
} // namespace RSCGroup
