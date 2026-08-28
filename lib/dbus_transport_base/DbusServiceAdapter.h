#pragma once

#include <memory>
#include <string>

namespace DBus {
class Object;
template<typename> class Signal;
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
    virtual void onTransportStopping() {}

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
