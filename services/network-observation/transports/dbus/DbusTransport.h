#pragma once

#include "IObservationTransport.h"

#include <DbusTransportBase.h>

#include <memory>
#include <string>

namespace RSCGroup {

class NetworkObservationDbusAdapter;

class DbusTransport : public IObservationTransport, public DbusTransportBase {
public:
    explicit DbusTransport(const std::string& busType = "system");
    ~DbusTransport() override = default;

    DbusTransport(const DbusTransport&) = delete;
    DbusTransport& operator=(const DbusTransport&) = delete;

    void bindQueryService(IObservationQueryService& provider) override;

    [[nodiscard]] bool start() override;
    void stop() override;
    void quiesceQueries() override;
    [[nodiscard]] std::string name() const override;

    void publishLocalStateChanged() override;
    void publishInterfaceChanged(const std::string& ifname) override;
    void publishInterfaceRemoved(const std::string& ifname) override;
    void publishCandidateChanged(const std::string& mac) override;
    void publishCandidateRemoved(const std::string& mac) override;
    void publishReadyChanged(bool ready) override;

private:
    [[nodiscard]] NetworkObservationDbusAdapter* obsAdapter() const;
};

} // namespace RSCGroup
