//
// Created by vvass on 24-Jul-26.
//
/**
 * @file LldpObserver.h
 * @brief Stable public LLDP observer type.
 *
 * Owns an ILldpSource backend and bridges observations to the
 * observation model. Supports interface lifecycle hooks for
 * netlink-driven refresh/flush.
 */
#pragma once
#include "public/ILldpSource.h"
#include "public/LldpObserverTypes.h"
#include <memory>
#include <string>

namespace RSCGroup {

class LldpObserver {
public:
    explicit LldpObserver(std::unique_ptr<ILldpSource> source);
    ~LldpObserver();

    LldpObserver(const LldpObserver&) = delete;
    LldpObserver& operator=(const LldpObserver&) = delete;

    [[nodiscard]] bool start();
    void stop();
    [[nodiscard]] bool isRunning() const;

    void refreshAll();
    void refreshInterface(const std::string& ifname);

    /**
     * @name Interface lifecycle hooks
     *
     * Called by the composite adapter when netlink reports link state
     * changes. These allow the LLDP module to react to local topology
     * changes without coupling to netlink internals.
     * @{
     */
    void onInterfaceUp(const std::string& ifname);
    void onInterfaceDown(const std::string& ifname);
    void onInterfaceRemoved(const std::string& ifname);
    /** @} */

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace RSCGroup