# ---------------------------------------------------------------------------
# libobservation-model.so — observation model / classifier / hard filter
# ---------------------------------------------------------------------------
add_library(observation-model SHARED
    engine/ObservationModelEngine.cpp
    engine/HardFilter.cpp
    engine/LocalStateTracker.cpp
    classifier/RuleBasedClassifier.cpp
    classifier/ScoringClassifier.cpp
    classifier/ClassifierFactory.cpp
    policy/DefaultInterfacePolicy.cpp
    adapter/NetworkObservationAdapter.cpp
)
target_include_directories(observation-model PRIVATE
    public
    engine
    classifier
    policy
    adapter
    ${NETLINK_BASE}
    ${NETLINK_BASE}/types
    ${NETLINK_BASE}/utils
    ${NETLINK_BASE}/parser
    ${NETLINK_BASE}/state
)
target_link_libraries(observation-model pthread glog rsc_util lldp-observer netlink-monitor liblldpctl.so)
