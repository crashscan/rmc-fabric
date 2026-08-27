#pragma once

#include "interop_contract/Error.hpp"

#include <optional>
#include <utility>

namespace interop_contract {
class [[nodiscard]] Status {
public:
    static Status ok() {
        return {};
    }

    static Status fail(Error error) {
        return Status(std::move(error));
    }

    [[nodiscard]] bool isOk() const {
        return !m_error.has_value();
    }

    [[nodiscard]] const std::optional<Error> &error() const {
        return m_error;
    }

private:
    Status() = default;

    explicit Status(Error error) : m_error(std::move(error)) {
    }

    std::optional<Error> m_error;
};
} // namespace interop_contract
