#include "Status.hpp"

#include <cstdlib>
#include <iostream>

using interop_contract::Error;
using interop_contract::Status;

int main()
{
    {
        const auto status = Status::ok();
        if (!status.isOk())
        {
            std::cerr << "expected ok status\n";
            return EXIT_FAILURE;
        }
        if (status.error().has_value())
        {
            std::cerr << "ok status should not carry an error\n";
            return EXIT_FAILURE;
        }
    }

    {
        const auto status = Status::fail(Error{"E_TEST", "failure"});
        if (status.isOk())
        {
            std::cerr << "expected failing status\n";
            return EXIT_FAILURE;
        }
        if (!status.error().has_value() ||
            status.error().value() != Error{"E_TEST", "failure"})
        {
            std::cerr << "unexpected error payload\n";
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}
