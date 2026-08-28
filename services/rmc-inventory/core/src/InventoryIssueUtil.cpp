#include "core/InventoryIssueUtil.h"

#include <interop_contract/inventory.hpp>

namespace RSCGroup::InventoryIssueUtil {
namespace {
    namespace C = interop_contract::inventory;
}

InventoryIssues deriveIssues(const interop_contract::inventory::SourceStateMap& states)
{
    InventoryIssues out;
    for (const auto &[name, state]: states) {
        if (state.health == SourceHealth::OK && !state.stale) continue;

        InventoryFields issue;
        const bool isError = (state.health == SourceHealth::FAILED) && state.required;
        issue.emplace(std::string(C::ISSUE_SEVERITY),
                      isError
                          ? std::string(C::SEVERITY_ERROR)
                          : std::string(C::SEVERITY_WARNING));

        if (state.lastError && !state.lastError->empty()) {
            issue.emplace(std::string(C::ISSUE_MESSAGE), *state.lastError);
        } else if (state.stale) {
            issue.emplace(std::string(C::ISSUE_MESSAGE), std::string("source data is stale"));
        } else {
            issue.emplace(std::string(C::ISSUE_MESSAGE), std::string("source not healthy"));
        }
        if (state.origin) issue.emplace(std::string(C::ISSUE_ORIGIN), *state.origin);
        out.emplace(name, std::move(issue));
    }
    return out;
}

} // namespace RSCGroup::InventoryIssueUtil
