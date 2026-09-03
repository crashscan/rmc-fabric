//
// Created by vvass on 20-Jul-26.
//
#pragma once
#include <ICandidateClassifier.h>
#include <ClassifierConfig.h>
#include <memory>

namespace RSCGroup {

std::unique_ptr<ICandidateClassifier> createClassifier(const ClassifierConfig& config);

} // namespace RSCGroup