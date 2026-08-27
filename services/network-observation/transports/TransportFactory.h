//
// Created by vvass on 21-Jul-26.
//
/**
 * @file TransportFactory.h
 * @brief Factory for creating ITransport instances by name.
 */
#pragma once
#include "ITransport.h"
#include <memory>
#include <string>

namespace RSCGroup {

/**
 * @brief Transport kind selection.
 */
enum class TransportKind {
    Dbus,
    Stdout,
};

/**
 * @brief Create a transport instance by name.
 * @param kind Transport kind
 * @param config Opaque config string (transport-specific)
 * @return The transport, or nullptr if the kind is unknown
 */
std::unique_ptr<ITransport> createTransport(TransportKind kind, const std::string& config = "");

/**
 * @brief Create a transport instance by string name.
 *
 * Recognized names: "dbus", "stdout"
 *
 * @param name Transport name
 * @param config Opaque config string (transport-specific)
 * @return The transport, or nullptr if the name is unknown
 */
std::unique_ptr<ITransport> createTransport(const std::string& name, const std::string& config = "");

} // namespace RSCGroup