//
// Created by vvass on 01-Sep-26.
//
#include <interop_contract.hpp>

#include <cstdlib>
#include <string>
#include <type_traits>

int main()
{
    using namespace interop_contract;

    static_assert(std::is_constructible_v<
                  ClientResult<int>, ClientError>);

    ClientResult<int> result{
        ClientError{
            ClientErrorCode::service_unavailable,
            "unavailable",
        }};

    if (result.hasValue()) {
        return EXIT_FAILURE;
    }

    DecodeError decodeError{
        DecodeErrorCode::invalid_type,
        "invalid type",
    };

    if (decodeError.code() != DecodeErrorCode::invalid_type) {
        return EXIT_FAILURE;
    }

    inventory::InventorySnapshot inventorySnapshot;
    network_observation::LocalNetworkSnapshot observationSnapshot;

    (void)inventorySnapshot;
    (void)observationSnapshot;
    (void)ingress::kMaxStringLength;

    return EXIT_SUCCESS;
}
