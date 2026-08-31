#pragma once

#include "IObservationTransport.h"

#include <memory>
#include <string>

namespace RSCGroup {

enum class TransportKind {
    Dbus,
    Stdout,
};

[[nodiscard]] std::shared_ptr<IObservationTransport> createTransport(TransportKind kind, const std::string& config = "");
[[nodiscard]] std::shared_ptr<IObservationTransport> createTransport(const std::string& name, const std::string& config = "");

} // namespace RSCGroup
