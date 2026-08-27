//
// Created by vvass on 21-Jul-26.
//
#include "TransportFactory.h"
#include "DbusTransport.h"
#include "StdoutTransport.h"
#include <unordered_map>

namespace RSCGroup {

std::unique_ptr<ITransport> createTransport(TransportKind kind, const std::string& config)
{
    switch (kind) {
        case TransportKind::Dbus:
            return std::make_unique<DbusTransport>(config.empty() ? "system" : config);
        case TransportKind::Stdout:
            return std::make_unique<StdoutTransport>();
    }
    return nullptr;
}

std::unique_ptr<ITransport> createTransport(const std::string& name, const std::string& config)
{
    static const std::unordered_map<std::string, TransportKind> map = {
        {"dbus", TransportKind::Dbus},
        {"stdout", TransportKind::Stdout},
    };

    auto it = map.find(name);
    if (it == map.end()) {
        return nullptr;
    }
    return createTransport(it->second, config);
}

} // namespace RSCGroup