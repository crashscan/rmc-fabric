#include <ClientResult.hpp>
#include <inventory.hpp>
#include <network_observation/NetworkObservationTypes.hpp>

int main()
{
    interop_contract::ClientResult<int> value = 42;
    if (!value || value.value() != 42) {
        return 1;
    }

    interop_contract::inventory::InventorySnapshot snapshot;
    interop_contract::network_observation::LocalNetworkSnapshot local;
    return snapshot.fields.empty() && local.interfaces.empty() ? 0 : 1;
}
