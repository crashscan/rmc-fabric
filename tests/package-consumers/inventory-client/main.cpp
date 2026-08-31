#include <InventoryClient.h>

int main()
{
    using Result = interop_contract::ClientResult<interop_contract::inventory::InventorySnapshot>;
    Result result = interop_contract::ClientError{
        interop_contract::ClientErrorCode::service_unavailable,
        "diagnostic",
    };

    RSCGroup::InventoryClient* client = nullptr;
    return client == nullptr && !result ? 0 : 1;
}
