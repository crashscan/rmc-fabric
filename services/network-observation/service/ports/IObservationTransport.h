#pragma once

#include <ITransport.h>

#include <string>

namespace RSCGroup {

class IObservationQueryService;

class IObservationTransport : public IServiceTransport {
public:
    ~IObservationTransport() override = default;

    virtual void bindQueryService(IObservationQueryService& provider) = 0;

    virtual void publishLocalStateChanged() = 0;
    virtual void publishInterfaceChanged(const std::string& ifname) = 0;
    virtual void publishInterfaceRemoved(const std::string& ifname) = 0;
    virtual void publishCandidateChanged(const std::string& mac) = 0;
    virtual void publishCandidateRemoved(const std::string& mac) = 0;
};

} // namespace RSCGroup
