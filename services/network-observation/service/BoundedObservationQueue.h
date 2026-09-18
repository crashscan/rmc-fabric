//
// Created by vvass on 17-Sep-26.
//
#pragma once

#include <BoundedQueue.h>   // rsc_util — adjust to the util include path
#include "ObservationItem.h"

#include <atomic>
#include <cstddef>
#include <optional>
#include <stop_token>
#include <utility>

namespace RSCGroup {

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
    using Stats = thread_safe::bounded_queue<ObservationItem>::Stats;

    explicit BoundedObservationQueue(std::size_t capacity)
        : queue_(capacity, [this](const ObservationItem &discarded) {
            resyncMask_.fetch_or(sourceBit(discarded.source),
                                 std::memory_order_release);
        }) {
    }

    BoundedObservationQueue(const BoundedObservationQueue &) = delete;

    BoundedObservationQueue &operator=(const BoundedObservationQueue &) = delete;

    /// Never blocks. False exactly when an older item was discarded (its
    /// source's resync bit is now raised). After close(): silently ignored.
    bool push(ObservationItem item) { return queue_.push(std::move(item)); }

    /// Oldest item, waiting while empty; nullopt once closed AND drained,
    /// or when @p st is requested.
    std::optional<ObservationItem> waitPop(std::stop_token st = {}) {
        return queue_.waitPop(std::move(st));
    }

    /// Oldest item if immediately available.
    std::optional<ObservationItem> tryPop() { return queue_.tryPop(); }

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

    /// Re-raise a source's resync bit after a failed repair.
    ///
    /// takeResyncMask() clears the mask, so a consumer that takes it and
    /// then fails to repair must put the bit back — otherwise the model
    /// stays diverged with no error on any path: the drop already happened
    /// and nothing else will raise it again.
    void raiseResync(ObservationSource source) {
        resyncMask_.fetch_or(sourceBit(source), std::memory_order_release);
    }

    /// Reset to a fresh, open, empty queue for a new service epoch. Clears
    /// the resync mask as well: the bits describe divergence in the previous
    /// epoch's model, which no longer exists.
    void reopen() {
        queue_.reopen();
        resyncMask_.store(0, std::memory_order_release);
    }

    void close() { queue_.close(); }
    [[nodiscard]] bool closed() const { return queue_.closed(); }
    [[nodiscard]] std::size_t size() const { return queue_.size(); }
    [[nodiscard]] std::size_t capacity() const { return queue_.capacity(); }
    [[nodiscard]] Stats stats() const { return queue_.stats(); }

private:
    // Declared first: the drop hook registered with queue_ touches this.
    std::atomic<std::uint32_t> resyncMask_{0};
    thread_safe::bounded_queue<ObservationItem> queue_;
};
} // namespace RSCGroup
