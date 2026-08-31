#include <DbusClient.h>

int main()
{
    using Result = interop_contract::ClientResult<
        interop_contract::network_observation::LocalNetworkSnapshot>;
    Result result = interop_contract::ClientError{
        interop_contract::ClientErrorCode::timeout,
        "diagnostic",
    };

    RSCGroup::DbusClient* client = nullptr;
    return client == nullptr && !result ? 0 : 1;
}
