#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace interop_contract {

enum class ClientErrorCode {
    service_unavailable,
    timeout,
    transport_error,
    decode_error,
    invalid_response,
};

struct ClientError {
    ClientErrorCode code{ClientErrorCode::transport_error};
    std::string message;
};

template <class T>
class ClientResult {
public:
    ClientResult(const T& value)
        : data_(value)
    {
    }

    ClientResult(T&& value)
        : data_(std::move(value))
    {
    }

    ClientResult(const ClientError& error)
        : data_(error)
    {
    }

    ClientResult(ClientError&& error)
        : data_(std::move(error))
    {
    }

    [[nodiscard]] bool hasValue() const noexcept { return std::holds_alternative<T>(data_); }
    [[nodiscard]] explicit operator bool() const noexcept { return hasValue(); }

    [[nodiscard]] const T& value() const&
    {
        if (!hasValue()) {
            throw std::logic_error("ClientResult has no value");
        }
        return std::get<T>(data_);
    }

    [[nodiscard]] T& value() &
    {
        if (!hasValue()) {
            throw std::logic_error("ClientResult has no value");
        }
        return std::get<T>(data_);
    }

    [[nodiscard]] T&& value() &&
    {
        if (!hasValue()) {
            throw std::logic_error("ClientResult has no value");
        }
        return std::move(std::get<T>(data_));
    }

    [[nodiscard]] const ClientError& error() const&
    {
        if (hasValue()) {
            throw std::logic_error("ClientResult has no error");
        }
        return std::get<ClientError>(data_);
    }

    template <class U>
    [[nodiscard]] T valueOr(U&& fallback) const
    {
        if (hasValue()) {
            return std::get<T>(data_);
        }
        return static_cast<T>(std::forward<U>(fallback));
    }

private:
    std::variant<T, ClientError> data_;
};

template <>
class ClientResult<void> {
public:
    ClientResult() = default;

    ClientResult(const ClientError& error)
        : error_(error)
    {
    }

    ClientResult(ClientError&& error)
        : error_(std::move(error))
    {
    }

    [[nodiscard]] bool hasValue() const noexcept { return !error_.has_value(); }
    [[nodiscard]] explicit operator bool() const noexcept { return hasValue(); }

    void value() const
    {
        if (error_) {
            throw std::logic_error("ClientResult has no value");
        }
    }

    [[nodiscard]] const ClientError& error() const&
    {
        if (!error_) {
            throw std::logic_error("ClientResult has no error");
        }
        return *error_;
    }

private:
    std::optional<ClientError> error_;
};

} // namespace interop_contract
