//
// Created by vvass on 20-Jul-26.
//
/**
 * @file ModelConfig.h
 * @brief Configuration for the network observation model.
 */
#pragma once
#include "ClassifierFactory.h"
#include "IInterfacePolicy.h"
#include <chrono>
#include <memory>

namespace RSCGroup {

/**
 * @brief Configuration for the network observation model.
 *
 * Move-only — contains std::unique_ptr members.
 */
struct ModelConfig {
    bool skipNullMac = true;
    bool skipLoopbackInterface = true;
    bool skipMulticastIPv4 = true;
    bool skipMulticastIPv6 = true;
    bool skipMulticastMac = true;
    bool skipIeeeReservedMac = true;
    bool emitDeviceIfIpOnly = false;
    std::chrono::seconds candidateAgeout{60};
    std::chrono::seconds candidateExpire{300};
    std::unique_ptr<IInterfacePolicy> interfacePolicy;
    ClassifierFactoryConfig classifierConfig;

    ModelConfig() = default;
    ModelConfig(ModelConfig&&) = default;
    ModelConfig& operator=(ModelConfig&&) = default;
    ModelConfig(const ModelConfig&) = delete;
    ModelConfig& operator=(const ModelConfig&) = delete;
};

} // namespace RSCGroup
