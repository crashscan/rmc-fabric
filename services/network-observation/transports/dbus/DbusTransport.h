//
// Created by vvass on 21-Jul-26.
//
/**
 * @file DbusTransport.h
 * @brief D-Bus transport — typed signals + methods for network-observationd.
 */
#pragma once
#include "ITransport.h"
#include <memory>
#include <string>

namespace RSCGroup {

class DbusTransport : public ITransport {
public:
    explicit DbusTransport(const std::string& busType = "system");
    ~DbusTransport() override;

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
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace RSCGroup