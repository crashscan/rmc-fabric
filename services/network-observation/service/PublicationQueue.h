//
// Created by vvass on 18-Sep-26.
//

#pragma once

#include <DirtySet.h>   // rsc_util — adjust to the util include path

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <stop_token>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace RSCGroup {
enum class PublicationIntent : std::uint8_t {
    Changed, // (re-)query the key
    Removed, // the key is gone
};

struct CandidateKey {
    std::string mac;

    bool operator==(const CandidateKey &) const = default;

    bool operator<(const CandidateKey &o) const { return mac < o.mac; }
};

struct InterfaceKey {
    std::string ifname;

    bool operator==(const InterfaceKey &) const = default;

    bool operator<(const InterfaceKey &o) const { return ifname < o.ifname; }
};

/// Unit key: the localStateDirty_ flag falls out as a single map entry.
struct LocalStateKey {
    bool operator==(const LocalStateKey &) const = default;

    bool operator<(const LocalStateKey &) const { return false; }
};

/// NOTE: alternative order IS emission order. std::variant::operator<
/// compares index first, so std::map iterates interfaces → localState →
/// candidates. Do not reorder without accepting a signal-ordering change.
using PublicationKey = std::variant<InterfaceKey, LocalStateKey, CandidateKey>;
using PendingPublication = std::map<PublicationKey, PublicationIntent>;

/**
 * @brief Coalescing publication queue (model → transport).
 *
 * Signals are notifications — clients re-query via GetCandidateByMac /
 * GetLocalSnapshot — so only the latest intent per identity matters:
 *   candidates_:  mac    → {Changed | Removed}
 *   interfaces_:  ifname → {Changed | Removed}
 *   localState_:  unit key
 * Bounded by model cardinality, not event rate. No capacity constant, no
 * overflow policy, no resync path.
 *
 * Last-writer-wins per key: Removed supersedes a pending Changed, and a
 * revival Changed supersedes a pending Removed — both correct because the
 * client re-queries on receipt.
 *
 * Self-tuning: a fast transport drains after every insert (one signal per
 * event, as today); a slow transport accumulates and coalesces. Coalescing
 * appears only under the backpressure that makes it necessary — and a
 * stalled transport can never back-pressure into the model.
 */
class PublicationQueue {
public:
    using Stats = thread_safe::dirty_set<PublicationKey, PublicationIntent>::Stats;

    bool markCandidateChanged(std::string mac) {
        return dirty_.mark(CandidateKey{std::move(mac)}, PublicationIntent::Changed);
    }

    bool markCandidateRemoved(std::string mac) {
        return dirty_.mark(CandidateKey{std::move(mac)}, PublicationIntent::Removed);
    }

    bool markInterfaceChanged(std::string ifname) {
        return dirty_.mark(InterfaceKey{std::move(ifname)}, PublicationIntent::Changed);
    }

    bool markInterfaceRemoved(std::string ifname) {
        return dirty_.mark(InterfaceKey{std::move(ifname)}, PublicationIntent::Removed);
    }

    bool markLocalStateChanged() {
        return dirty_.mark(LocalStateKey{}, PublicationIntent::Changed);
    }

    /// unlocked. nullopt once closed AND nothing pending, or when @p st is
    /// requested.
    std::optional<PendingPublication> waitAndTake(std::stop_token st = {}) {
        return dirty_.waitAndTake(std::move(st));
    }

    /// Take the pending set if non-empty.
    std::optional<PendingPublication> tryTake() { return dirty_.tryTake(); }

    /// Wake a blocked waitAndTake() without closing. This is the
    /// ManagedWorker Wake callback: close() is one-way and must never be
    /// used as the wake path, or the queue stays dead across stop/start and
    /// silently discards every mark.
    void wake() noexcept { dirty_.wake(); }

    void close() { dirty_.close(); }
    [[nodiscard]] bool closed() const { return dirty_.closed(); }
    [[nodiscard]] std::size_t size() const { return dirty_.size(); }
    [[nodiscard]] Stats stats() const { return dirty_.stats(); }

private:
    thread_safe::dirty_set<PublicationKey, PublicationIntent> dirty_;
};

/// Dispatch helper for the publication worker: invokes the matching handler
/// per entry, in deterministic (sorted) key order.
template<typename OnCandidate, typename OnInterface, typename OnLocalState>
void dispatchPublication(const PendingPublication &pending,
                         OnCandidate &&onCandidate,
                         OnInterface &&onInterface,
                         OnLocalState &&onLocalState) {
    for (const auto &[key, intent]: pending) {
        std::visit([&](const auto &k) {
            using K = std::decay_t<decltype(k)>;
            if constexpr (std::is_same_v<K, CandidateKey>) {
                onCandidate(k.mac, intent);
            } else if constexpr (std::is_same_v<K, InterfaceKey>) {
                onInterface(k.ifname, intent);
            } else {
                onLocalState();
            }
        }, key);
    }
}
} // namespace RSCGroup
