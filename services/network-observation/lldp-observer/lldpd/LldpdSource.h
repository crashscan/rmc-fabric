//
// Created by vvass on 24-Jul-26.
//
/**
 * @file LldpdSource.h
 * @brief LLDP source backed by the lldpd daemon via liblldpctl.
 *
 * Wraps lldpcli::LldpWatch for push-based change notifications.
 * Implements ILldpSource — refresh methods are advisory (reconnect).
 *
 * @section limitations v1 Limitations
 * Only MAC-like LLDP identities are cached and forwarded to the
 * observation model. Non-MAC chassis IDs (hostnames, network
 * addresses, local identifiers) are passed through as observations
 * but not cached for interface-flush purposes. Full non-MAC identity
 * support is deferred to v2.
 */
#pragma once
#include "ILldpSource.h"
#include "LldpObserverTypes.h"
#include <memory>
#include <string>

namespace RSCGroup {

class LldpdSource : public ILldpSource {
public:
    LldpdSource(LldpSourceConfig config, LldpObservationCallback cb);
    ~LldpdSource() override;

    bool start() override;
    void stop() override;
    [[nodiscard]] bool isRunning() const override;

    void refreshAll() override;
    void refreshInterface(const std::string& ifname) override;
    void removeInterface(const std::string& ifname) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace RSCGroup