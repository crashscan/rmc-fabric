#pragma once

#include "IObservationTransport.h"

#include <string>

namespace RSCGroup {

class StdoutTransport : public IObservationTransport {
public:
    void bindQueryService(IObservationQueryService& provider) override;
    [[nodiscard]] bool start() override;
    void stop() override;
    [[nodiscard]] std::string name() const override;

    void publishLocalStateChanged() override;
    void publishInterfaceChanged(const std::string& ifname) override;
    void publishInterfaceRemoved(const std::string& ifname) override;
    void publishCandidateChanged(const std::string& mac) override;
    void publishCandidateRemoved(const std::string& mac) override;
    void publishReadyChanged(bool ready) override;

private:
    bool running_ = false;
};

} // namespace RSCGroup
