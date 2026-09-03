//
// Created by vvass on 03-Sep-26.
//

#include <CandidateTypes.h>
#include <ClassifierConfig.h>
#include <ICandidateClassifier.h>
#include <IInterfacePolicy.h>
#include <INetworkObservationModel.h>
#include <LocalStateTypes.h>
#include <ModelConfig.h>
#include <NetworkObservationModelFactory.h>
#include <ObservationTypes.h>

#include <memory>

namespace {

class TestPolicy final : public RSCGroup::IInterfacePolicy {
public:
    bool includeInLocalState(std::string_view) const override
    {
        return true;
    }

    bool allowRemoteNeighborEvidence(std::string_view) const override
    {
        return true;
    }

    bool allowRemoteFdbEvidence(std::string_view) const override
    {
        return true;
    }

    bool allowLldpEvidence(std::string_view) const override
    {
        return true;
    }
};

} // namespace

int main()
{
    RSCGroup::ModelConfig config;
    config.interfacePolicy = std::make_unique<TestPolicy>();
    config.classifierConfig.kind = RSCGroup::ClassifierKind::Scoring;

    auto model =
        RSCGroup::createNetworkObservationModel(std::move(config));

    return model ? 0 : 1;
}
