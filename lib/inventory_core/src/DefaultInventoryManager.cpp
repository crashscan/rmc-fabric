#include "inventory_core/DefaultInventoryManager.h"

#include "inventory_core/InventoryFieldUtil.h"
#include "inventory_core/InventorySourceUtil.h"

#include <interop_contract/inventory.hpp>

#include <algorithm>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>

#include <glog/logging.h>

namespace RSCGroup {
namespace {
    using namespace interop_contract::inventory;

    [[nodiscard]] int64_t wallNowSec() {
        return std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                .count();
    }

    [[nodiscard]] bool fieldValuesEqual(const FieldValue &lhs, const FieldValue &rhs) {
        return lhs == rhs;
    }

    struct CollectedSourceResult {
        std::string name;
        bool required{false};
        InventoryFields collectedFields;
        SourceState state;
    };
} // namespace

void DefaultInventoryManager::addSource(std::shared_ptr<IInventorySource> source) {
    if (!source) {
        throw std::invalid_argument("DefaultInventoryManager::addSource: source is null");
    }

    std::scoped_lock lock(mutex_);
    validateSourceRegistration(source);
    sources_.push_back(std::move(source));
}

InventoryDiff DefaultInventoryManager::refreshAll() {
    std::vector<std::shared_ptr<IInventorySource> > sourcesCopy;
    {
        std::scoped_lock lock(mutex_);
        sourcesCopy = sources_;
    }

    std::vector<CollectedSourceResult> results;
    results.reserve(sourcesCopy.size());
    const int64_t attemptTs = wallNowSec();

    for (const auto &source: sourcesCopy) {
        CollectedSourceResult result;
        result.name = source->getName();
        result.required = source->isRequired();
        result.state = source->getState();
        result.state.name = result.name;
        result.state.required = result.required;
        result.state.lastAttemptTs = attemptTs;

        try {
            InventoryFields rawFields = source->collect();

            for (const auto &field: rawFields | std::views::keys) {
                if (InventoryFieldUtil::isMetadataField(field)) {
                    throw std::runtime_error(
                        "Inventory source '" + result.name +
                        "' returned reserved metadata field '" + field + "'");
                }
            }

            const auto ownedFields = source->getOwnedFields();
            if (!InventorySourceUtil::isSubsetOfOwnedFields(rawFields, ownedFields)) {
                const auto undeclared = InventorySourceUtil::getUndeclaredFields(rawFields, ownedFields);
                for (const auto &badField: undeclared) {
                    LOG(WARNING) << "Inventory source '" << result.name
                            << "' returned undeclared field '" << badField
                            << "'; dropping it";
                    rawFields.erase(badField);
                }
            }

            result.collectedFields = std::move(rawFields);
            result.state.health = SourceHealth::OK;
            result.state.stale = false;
            result.state.lastSuccessTs = wallNowSec();
            result.state.lastError.reset();
        } catch (const std::exception &e) {
            result.state.health = SourceHealth::FAILED;
            result.state.lastError = e.what();
        } catch (...) {
            result.state.health = SourceHealth::FAILED;
            result.state.lastError = "unknown exception";
        }

        results.push_back(std::move(result));
    }

    std::scoped_lock lock(mutex_);

    InventoryFields newFields = snapshot_.fields;
    SourceStateMap newStates = sourceStates_;

    for (const auto &result: results) {
        const auto ownerIt = std::ranges::find_if(sources_,
                                                  [&](const auto &s) { return s->getName() == result.name; });

        if (ownerIt == sources_.end()) {
            LOG(ERROR) << "DefaultInventoryManager::refreshAll: source disappeared during merge for '"
                    << result.name << "'; skipping update for this cycle";
            continue;
        }

        newStates[result.name] = result.state;

        if (result.state.health == SourceHealth::OK) {
            const auto &ownerSource = *ownerIt;
            for (const auto &ownedField: ownerSource->getOwnedFields()) {
                newFields.erase(ownedField);
            }
            for (const auto &[key, value]: result.collectedFields) {
                newFields[key] = value;
            }
        } else if (const auto prev = sourceStates_.find(result.name); prev != sourceStates_.end()) {
            newStates[result.name].lastSuccessTs = prev->second.lastSuccessTs;
        }
    }

    const InventoryDiff diff = diffSourceOwnedFields(snapshot_.fields, newFields);

    bool readyLatched = snapshot_.ready;
    if (!readyLatched && allRequiredSourcesOk(newStates)) {
        readyLatched = true;
    }

    snapshot_.ready = readyLatched;
    snapshot_.phase = readyLatched ? std::string(PHASE_LIVE) : std::string(PHASE_INITIALIZING);
    sourceStates_ = std::move(newStates);
    snapshot_.fields = std::move(newFields);

    if (!diff.empty()) {
        ++snapshot_.version;
        snapshot_.timestamp = wallNowSec();
    }

    return diff;
}

InventorySnapshot DefaultInventoryManager::getSnapshot() const {
    std::scoped_lock lock(mutex_);
    return snapshot_;
}

SourceStateMap DefaultInventoryManager::getSourceStates() const {
    std::scoped_lock lock(mutex_);
    return sourceStates_;
}

bool DefaultInventoryManager::isReady() const
{
    std::scoped_lock lock(mutex_);
    return snapshot_.ready;
}

std::string DefaultInventoryManager::getPhase() const
{
    std::scoped_lock lock(mutex_);
    return snapshot_.phase;
}

uint64_t DefaultInventoryManager::getVersion() const
{
    std::scoped_lock lock(mutex_);
    return snapshot_.version;
}

void DefaultInventoryManager::validateSourceRegistration(const std::shared_ptr<IInventorySource> &source) {
    const auto sourceName = source->getName();
    if (sourceName.empty()) throw std::runtime_error("Inventory source declares an empty name");
    for (const auto &existing: sources_) {
        if (existing && existing->getName() == sourceName) {
            throw std::runtime_error("Duplicate inventory source name '" + sourceName + "'");
        }
    }
    const auto ownedFields = source->getOwnedFields();
    for (const auto &field: ownedFields) {
        if (field.empty()) throw std::runtime_error(
            "Inventory source '" + sourceName + "' declares an empty owned field");
        if (InventoryFieldUtil::isMetadataField(field)) {
            throw std::runtime_error(
                "Inventory source '" + sourceName + "' attempts to own reserved metadata field '" + field + "'");
        }
        if (const auto it = fieldOwners_.find(field); it != fieldOwners_.end()) {
            throw std::runtime_error(
                "Inventory field ownership collision for field '" + field + "': '" + it->second + "' vs '" + sourceName
                + "'");
        }
    }
    for (const auto &field: ownedFields) fieldOwners_[field] = sourceName;
}

InventoryDiff DefaultInventoryManager::diffSourceOwnedFields(const InventoryFields &oldFields,
                                                             const InventoryFields &newFields) const {
    InventoryDiff diff;
    for (const auto &[field, newValue]: newFields) {
        const auto oldIt = oldFields.find(field);
        if (oldIt == oldFields.end()) {
            diff.changedFields.push_back(field);
            continue;
        }
        if (!fieldValuesEqual(oldIt->second, newValue)) diff.changedFields.push_back(field);
    }
    for (const auto &field: oldFields | std::views::keys ) {
        if (newFields.find(field) == newFields.end()) diff.removedFields.
            push_back(field);
    }
    return diff;
}

bool DefaultInventoryManager::allRequiredSourcesOk(const SourceStateMap &states) const {
    for (const auto &source: sources_) {
        const auto it = states.find(source->getName());
        if (it == states.end()) {
            if (source->isRequired()) return false;
            continue;
        }
        if (source->isRequired() && it->second.health != SourceHealth::OK) return false;
    }
    return true;
}
} // namespace RSCGroup
