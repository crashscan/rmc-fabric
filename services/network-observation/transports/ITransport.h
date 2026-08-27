//
// Created by vvass on 21-Jul-26.
//
/**
 * @file ITransport.h
 * @brief Transport abstraction for network-observationd.
 *
 * Defines an entity-oriented publishing contract. Each transport
 * implementation (D-Bus, MQTT, Unix socket) maps these semantic
 * notifications to its native protocol.
 */
#pragma once
#include "IObservationQueryService.h"
#include <string>

namespace RSCGroup {

/**
 * @brief Pluggable transport layer for publishing observation service state.
 *
 * Transports receive entity-level change notifications and are responsible
 * for mapping them to protocol-specific representations.
 *
 * Raw observations (LinkObservation, NeighborObservation, etc.) are NOT
 * exposed through this interface — only modeled entities.
 */
class ITransport {
public:
    virtual ~ITransport() = default;

    /**
     * @brief Initialize and start the transport.
     * @return True on success.
     */
    [[nodiscard]] virtual bool start() = 0;

    /// Stop and release transport resources.
    virtual void stop() = 0;

    // --- Entity-oriented change notifications ---

    /// One or more local entities changed (coarse invalidation)
    virtual void publishLocalStateChanged() = 0;

    /// A specific interface changed (link state, addresses, master relationship)
    virtual void publishInterfaceChanged(const std::string& ifname) = 0;

    /// An interface is no longer present; it will be absent from GetLocalSnapshot / GetInterface
    virtual void publishInterfaceRemoved(const std::string& ifname) = 0;

    /// A remote candidate's properties changed (IP set, classification, status, etc.)
    virtual void publishCandidateChanged(const std::string& mac) = 0;

    /// A remote candidate is no longer queryable via GetCandidateByMac / GetRemoteCandidates
    virtual void publishCandidateRemoved(const std::string& mac) = 0;

    /// The service readiness changed
    virtual void publishReadyChanged(bool ready) = 0;

    /**
     * @brief Provide a query interface for read-only access to observation state.
     *
     * Only transports that service method calls (e.g. D-Bus) need this;
     * non-query transports inherit a no-op default.
     */
    virtual void setQueryProvider(IObservationQueryService* provider) { (void)provider; }
};

} // namespace RSCGroup