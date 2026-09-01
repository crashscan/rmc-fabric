#pragma once

#include <ClientResult.hpp>
#include <DecodeError.hpp>

#include <dbus-cxx.h>
#include <glog/logging.h>

#include <exception>
#include <utility>

namespace RSCGroup::dbus_client_support {

/**
 * Map dbus-cxx exceptions onto the transport-neutral public client error
 * model. Callers must branch on ClientErrorCode rather than message text.
 */
[[nodiscard]] inline interop_contract::ClientError classifyDbusError(
    const DBus::Error& error)
{
    using interop_contract::ClientError;
    using interop_contract::ClientErrorCode;

    if (dynamic_cast<const DBus::ErrorTimeout*>(&error) ||
        dynamic_cast<const DBus::ErrorTimedOut*>(&error) ||
        dynamic_cast<const DBus::ErrorNoReply*>(&error)) {
        return {ClientErrorCode::timeout, error.what()};
    }

    if (dynamic_cast<const DBus::ErrorServiceUnknown*>(&error) ||
        dynamic_cast<const DBus::ErrorNameHasNoOwner*>(&error) ||
        dynamic_cast<const DBus::ErrorNoConnection*>(&error) ||
        dynamic_cast<const DBus::ErrorNoServer*>(&error) ||
        dynamic_cast<const DBus::ErrorUnknownObject*>(&error) ||
        dynamic_cast<const DBus::ErrorUnknownInterface*>(&error) ||
        dynamic_cast<const DBus::ErrorUnknownMethod*>(&error)) {
        return {ClientErrorCode::service_unavailable, error.what()};
    }

    if (dynamic_cast<const DBus::ErrorUnexpectedResponse*>(&error) ||
        dynamic_cast<const DBus::ErrorInvalidReturn*>(&error) ||
        dynamic_cast<const DBus::ErrorInvalidSignature*>(&error) ||
        dynamic_cast<const DBus::ErrorInvalidMessageType*>(&error) ||
        dynamic_cast<const DBus::ErrorBadVariantCast*>(&error)) {
        return {ClientErrorCode::invalid_response, error.what()};
    }

    return {ClientErrorCode::transport_error, error.what()};
}

/**
 * Execute a D-Bus client operation and translate boundary exceptions into
 * ClientResult<T>.
 *
 * The callable may return either T or ClientResult<T>. ClientResult's
 * converting constructors normalize both forms.
 */
template<class T, class Fn>
[[nodiscard]] interop_contract::ClientResult<T> invokeQuery(
    const char* operation,
    Fn&& function)
{
    try {
        return std::forward<Fn>(function)();
    } catch (const interop_contract::DecodeError& error) {
        LOG(ERROR) << operation << " failed: " << error.what();
        return interop_contract::ClientError{
            interop_contract::ClientErrorCode::decode_error,
            error.what(),
        };
    } catch (const DBus::Error& error) {
        auto mapped = classifyDbusError(error);
        LOG(ERROR) << operation << " failed: " << mapped.message;
        return mapped;
    } catch (const std::exception& error) {
        LOG(ERROR) << operation << " failed: " << error.what();
        return interop_contract::ClientError{
            interop_contract::ClientErrorCode::transport_error,
            error.what(),
        };
    } catch (...) {
        LOG(ERROR) << operation << " failed: unknown exception";
        return interop_contract::ClientError{
            interop_contract::ClientErrorCode::transport_error,
            "unknown exception",
        };
    }
}

} // namespace RSCGroup::dbus_client_support
