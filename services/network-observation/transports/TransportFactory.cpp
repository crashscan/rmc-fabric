#include "TransportFactory.h"

#include "DbusTransport.h"
#include "StdoutTransport.h"

#include <unordered_map>

namespace RSCGroup {

std::shared_ptr<IObservationTransport> createTransport(TransportKind kind, const std::string& config)
{
    switch (kind) {
        case TransportKind::Dbus:
            return std::make_shared<DbusTransport>(config.empty() ? "system" : config);
        case TransportKind::Stdout:
            return std::make_shared<StdoutTransport>();
    }
    return nullptr;
}

std::shared_ptr<IObservationTransport> createTransport(const std::string& name, const std::string& config)
{
    static const std::unordered_map<std::string, TransportKind> map = {
        {"dbus", TransportKind::Dbus},
        {"stdout", TransportKind::Stdout},
    };

    const auto it = map.find(name);
    if (it == map.end()) {
        return nullptr;
    }
    return createTransport(it->second, config);
}

} // namespace RSCGroup
