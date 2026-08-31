#pragma once

#include <memory>
#include <string>

namespace DBus {
class Object;
template<typename...> class Signal;
}

namespace RSCGroup {

/**
 * @brief Abstract adapter that binds a service domain to a D-Bus object.
 *
 * Concrete subclasses register D-Bus methods and signals for their specific
 * service. DbusTransportBase calls bind() once during start() and the
 * lifecycle hooks at start/stop time.
 */
class DbusServiceAdapter {
public:
    DbusServiceAdapter() = default;
    virtual ~DbusServiceAdapter() = default;

    DbusServiceAdapter(const DbusServiceAdapter&) = delete;
    DbusServiceAdapter& operator=(const DbusServiceAdapter&) = delete;

    /**
     * @brief Register D-Bus methods and create signals on the given object.
     *
     * Called once by DbusTransportBase::start() after the object is created.
     */
    virtual void bind(const std::shared_ptr<DBus::Object>& object,
                      const std::string& interfaceName) = 0;

    // Adapter lifecycle hooks: called by DbusTransportBase
    virtual void onTransportStarting() {}

    /**
     * @brief Close query admission and wait for in-flight query handlers to
     *        drain.  Called by DbusTransportBase::quiesceQueries() and by
     *        the default onTransportStopping() implementation.
     *
     * Postcondition: no query handler is executing; no new query can be
     * admitted; publication resources remain open.
     *
     * Default is a no-op for adapters without query methods.
     */
    virtual void quiesceQueries() {}

    /**
     * @brief Called by DbusTransportBase::stop() just before the D-Bus object
     *        is unregistered.  The default implementation delegates to
     *        quiesceQueries() so concrete adapters only need to override
     *        quiesceQueries().
     *
     * Implementations that need additional teardown beyond query drain should
     * override onTransportStopping() and call quiesceQueries() explicitly.
     */
    virtual void onTransportStopping() { quiesceQueries(); }

protected:
    [[nodiscard]] std::shared_ptr<DBus::Signal<void(std::string)>> createStringSignal(
        const std::shared_ptr<DBus::Object>& object,
        const std::string& interfaceName,
        const std::string& signalName);

    [[nodiscard]] std::shared_ptr<DBus::Signal<void(bool)>> createBoolSignal(
        const std::shared_ptr<DBus::Object>& object,
        const std::string& interfaceName,
        const std::string& signalName);

    [[nodiscard]] std::shared_ptr<DBus::Signal<void()>> createVoidSignal(
        const std::shared_ptr<DBus::Object>& object,
        const std::string& interfaceName,
        const std::string& signalName);
};

} // namespace RSCGroup
