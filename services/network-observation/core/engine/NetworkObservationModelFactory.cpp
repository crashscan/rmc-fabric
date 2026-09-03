//
// Created by vvass on 03-Sep-26.
//

#include "NetworkObservationModelFactory.h"
#include "ObservationModelEngine.h"

#include <memory>
#include <utility>

namespace RSCGroup {

std::unique_ptr<INetworkObservationModel> createNetworkObservationModel(ModelConfig config)
{
    return std::make_unique<ObservationModelEngine>(std::move(config));
}

} // namespace RSCGroup