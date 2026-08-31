#pragma once

#include "CandidateTypes.h"
#include "IObservationQueryService.h"
#include "LocalStateTypes.h"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace RSCGroup {

struct ObservationRuntimeHealth {
    bool running{false};
    bool lldpAvailable{true};
};

class ICandidateClassifier;
class IInterfacePolicy;
class IModelEventSink;

class IObservationRuntime {
public:
    virtual ~IObservationRuntime() = default;

    virtual void setEventSink(IModelEventSink* sink) = 0;
    virtual void setInterfacePolicy(std::unique_ptr<IInterfacePolicy> policy) = 0;
    virtual void setClassifier(std::unique_ptr<ICandidateClassifier> classifier) = 0;

    [[nodiscard]] virtual bool start() = 0;

    /**
     * @brief Stop the runtime and drain all producer threads and callbacks.
     *
     * Postcondition when stop() returns:
     *  - all producer threads (netlink monitor, LLDP watch callbacks) have
     *    stopped and been joined;
     *  - no IModelEventSink call is active or will be made until a
     *    successful restart;
     *  - setEventSink(nullptr) has been called (the sink pointer is cleared);
     *  - any runtime-owned resources (file descriptors, watch handles) are
     *    released.
     *
     * Ordering note: the netlink monitor is stopped before the LLDP source
     * because netlink callbacks may invoke the LLDP source.  The event sink
     * is detached only after both producers have fully drained.
     *
     * Must be idempotent: calling stop() on an already-stopped runtime is
     * a no-op.
     */
    virtual void stop() = 0;
    [[nodiscard]] virtual bool isRunning() const = 0;
    [[nodiscard]] virtual ObservationRuntimeHealth health() const = 0;

    [[nodiscard]] virtual LocalNetworkSnapshot localSnapshot() const = 0;
    [[nodiscard]] virtual std::vector<RemoteCandidate> remoteCandidates() const = 0;
    [[nodiscard]] virtual std::optional<RemoteCandidate> findCandidateByMac(const std::string& mac) const = 0;
    virtual void age(std::chrono::steady_clock::time_point now) = 0;
};

} // namespace RSCGroup
