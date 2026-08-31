#pragma once

#include <stdexcept>
#include <string>

namespace interop_contract {

enum class DecodeErrorCode {
    missing_required_field,
    invalid_type,
    invalid_value,
    limit_exceeded,
};

class DecodeError : public std::runtime_error {
public:
    DecodeError(DecodeErrorCode code, std::string message)
        : std::runtime_error(std::move(message))
        , code_(code)
    {
    }

    [[nodiscard]] DecodeErrorCode code() const noexcept { return code_; }

private:
    DecodeErrorCode code_;
};

} // namespace interop_contract
