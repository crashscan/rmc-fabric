//
// Created by vvass on 21-Jul-26.
//
/**
 * @file DbusTransport.h
 * @brief D-Bus transport for network-observationd.
 *
 * Delegates all D-Bus infrastructure to DbusTransportBase and all
 * signal/method binding to NetworkObservationDbusAdapter.
 */
#pragma once
#include "ITransport.h"
#include <DbusTransportBase.h>
#include <memory>
#include <string>

namespace RSCGroup {

class NetworkObservationDbusAdapter;

class DbusTransport : public ITransport, public DbusTransportBase {
public:
    explicit DbusTransport(const std::string& busType = "system");
    ~DbusTransport() override = default;

    DbusTransport(const DbusTransport&) = delete;
    DbusTransport& operator=(const DbusTransport&) = delete;

    void setQueryProvider(IObservationQueryService* provider) override;

    [[nodiscard]] bool start() override;
    void stop() override;

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