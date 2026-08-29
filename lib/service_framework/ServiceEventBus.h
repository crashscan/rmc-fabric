#pragma once

#include <any>
#include <functional>
#include <mutex>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace RSCGroup {

/**
 * @brief Lightweight synchronous event bus for intra-service communication.
 *
 * Publishers call publish<EventT>(event) to broadcast a typed event.
 * Subscribers call subscribe<EventT>(handler) to receive events of that type.
 *
 * Events are delivered synchronously in the calling thread of publish().
 * The internal subscription lock is not held during delivery, so handlers
 * may themselves publish or subscribe without deadlock.
 *
 * Thread safety: subscribe() and publish() are thread-safe with respect to
 * each other.
 *
 * Example:
 * @code
 *   struct ReadyChanged { bool ready; };
 *
 *   ServiceEventBus bus;
 *   bus.subscribe<ReadyChanged>([](const ReadyChanged& e) {
 *       LOG(INFO) << "ready=" << e.ready;
 *   });
 *   bus.publish(ReadyChanged{true});
 * @endcode
 */
class ServiceEventBus {
public:
    using HandlerFn = std::function<void(const std::any&)>;

    ServiceEventBus() = default;

    /**
     * @brief Subscribe to events of type @p EventT.
     * @param handler  Called once per publish<EventT>() with the event value.
     */
    template <typename EventT>
    void subscribe(std::function<void(const EventT&)> handler)
    {
        auto wrapped = [h = std::move(handler)](const std::any& a) {
            h(std::any_cast<const EventT&>(a));
        };
        std::lock_guard<std::mutex> lock(mutex_);
        handlers_[std::type_index(typeid(EventT))].push_back(std::move(wrapped));
    }

    /**
     * @brief Publish an event of type @p EventT to all subscribers.
     * @param event  Event value; copied into a std::any before dispatch.
     */
    template <typename EventT>
    void publish(const EventT& event)
    {
        std::vector<HandlerFn> snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = handlers_.find(std::type_index(typeid(EventT)));
            if (it == handlers_.end()) {
                return;
            }
            snapshot = it->second;
        }
        std::any wrapped = event;
        for (const auto& fn : snapshot) {
            fn(wrapped);
        }
    }

    /**
     * @brief Remove all subscriptions (useful in unit tests).
     */
    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        handlers_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::type_index, std::vector<HandlerFn>> handlers_;
};

} // namespace RSCGroup
