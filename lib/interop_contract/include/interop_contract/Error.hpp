#pragma once

#include <string>

namespace interop_contract {

struct Error
{
    std::string code;
    std::string message;

    bool operator==(const Error&) const = default;
};
} // namespace interop_contract
