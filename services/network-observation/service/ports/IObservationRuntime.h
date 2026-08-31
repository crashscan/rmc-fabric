#pragma once

#include "CandidateTypes.h"
#include "IModelEventSink.h"
#include "IObservationQueryService.h"
#include "LocalStateTypes.h"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace RSCGroup {

class ICandidateClassifier;
class IInterfacePolicy;

class IObservationRuntime {
public:
    virtual ~IObservationRuntime() = default;

    virtual void setEventSink(IModelEventSink* sink) = 0;
    virtual void setInterfacePolicy(std::unique_ptr<IInterfacePolicy> policy) = 0;
    virtual void setClassifier(std::unique_ptr<ICandidateClassifier> classifier) = 0;

    [[nodiscard]] virtual bool start() = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual bool isRunning() const = 0;

    [[nodiscard]] virtual LocalNetworkSnapshot localSnapshot() const = 0;
    [[nodiscard]] virtual std::vector<RemoteCandidate> remoteCandidates() const = 0;
    [[nodiscard]] virtual std::optional<RemoteCandidate> findCandidateByMac(const std::string& mac) const = 0;
    virtual void age(std::chrono::steady_clock::time_point now) = 0;
};

} // namespace RSCGroup
