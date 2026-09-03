//
// Created by vvass on 03-Sep-26.
//

#pragma once

#include "ModelConfig.h"

#include <memory>

namespace RSCGroup {

class INetworkObservationModel;

/**
 * Construct the repository's default network-observation model.
 *
 * Callers depend on the model interface, not ObservationModelEngine.
 */
[[nodiscard]] std::unique_ptr<INetworkObservationModel> createNetworkObservationModel(ModelConfig config);

} // namespace RSCGroup