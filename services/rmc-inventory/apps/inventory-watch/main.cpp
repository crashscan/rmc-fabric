#include "InventoryClient.h"
#include <interop_contract/inventory.hpp>

#include <dbus-cxx.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <variant>

using namespace RSCGroup;
using namespace interop_contract::inventory;

namespace {
std::atomic<bool> g_running{true};
std::atomic<bool> g_shuttingDown{false};

void handleSignal(int)
{
    g_shuttingDown = true;
    g_running = false;
}

std::mutex g_outMutex;
std::mutex g_eventMutex;

std::atomic<bool> g_refreshRequested{false};
std::atomic<bool> g_seenInventoryChanged{false};
std::atomic<bool> g_seenSourceStateChanged{false};
std::atomic<bool> g_seenReadyChanged{false};

std::optional<std::string> g_lastInventoryField;
std::optional<std::string> g_lastSourceName;
std::optional<bool> g_lastReadyValue;

std::string fieldValueToString(const FieldValue& v)
{
    return std::visit([](const auto& x) -> std::string {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, bool>) {
            return x ? "true" : "false";
        } else if constexpr (std::is_same_v<T, std::string>) {
            return "\"" + x + "\"";
        } else {
            return std::to_string(x);
        }
    }, v);
}

void printSnapshotUnlocked(const InventorySnapshot& snap)
{
    std::cout << "--- Inventory Snapshot ---\n";
    std::cout << "version:   " << snap.version << "\n";
    std::cout << "timestamp: " << snap.timestamp << "\n";
    std::cout << "ready:     " << (snap.ready ? "true" : "false") << "\n";
    std::cout << "phase:     " << snap.phase << "\n";
    std::cout << "fields (" << snap.fields.size() << "):\n";
    for (const auto& [key, value] : snap.fields) {
        std::cout << "  " << key << " = " << fieldValueToString(value) << "\n";
    }
}

void printSourceStatesUnlocked(const interop_contract::inventory::SourceStateMap& states)
{
    std::cout << "--- Source States ---\n";
    for (const auto& [name, state] : states) {
        std::string healthStr;
        switch (state.health) {
            case SourceHealth::OK:       healthStr = "ok"; break;
            case SourceHealth::DEGRADED: healthStr = "degraded"; break;
            case SourceHealth::FAILED:   healthStr = "failed"; break;
        }
        std::cout << "  " << name
                  << ": health=" << healthStr
                  << " required=" << (state.required ? "true" : "false")
                  << " stale=" << (state.stale ? "true" : "false");
        if (state.lastError) {
            std::cout << " error=\"" << *state.lastError << "\"";
        }
        if (state.origin) {
            std::cout << " origin=\"" << *state.origin << "\"";
        }
        std::cout << "\n";
    }
}

void printIssuesUnlocked(const interop_contract::inventory::InventoryIssues& issues)
{
    if (issues.empty()) {
        std::cout << "--- Issues: none ---\n";
        return;
    }
    std::cout << "--- Issues ---\n";
    for (const auto& [name, fields] : issues) {
        std::cout << "  " << name << ":";
        for (const auto& [k, v] : fields) {
            std::cout << " " << k << "=" << fieldValueToString(v);
        }
        std::cout << "\n";
    }
}

void printAll(const InventoryClient& client)
{
    if (g_shuttingDown.load()) {
        return;
    }

    try {
        const auto snap   = client.getIdentity();
        const auto states = client.getSourceStates();
        const auto issues = client.getIssues();

        if (g_shuttingDown.load()) {
            return;
        }

        std::scoped_lock lock(g_outMutex);
        printSnapshotUnlocked(snap);
        printSourceStatesUnlocked(states);
        printIssuesUnlocked(issues);
        std::cout << std::endl;
    } catch (const std::exception& e) {
        if (!g_shuttingDown.load()) {
            std::scoped_lock lock(g_outMutex);
            std::cerr << "printAll failed: " << e.what() << "\n";
        }
    }
}

} // namespace

int main()
{
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    try {
        auto dispatcher = DBus::StandaloneDispatcher::create();
        auto connection = dispatcher->create_connection(DBus::BusType::SYSTEM);
        auto client = std::make_shared<InventoryClient>(connection);

        if (!client->waitReady(std::chrono::seconds(5))) {
            std::scoped_lock lock(g_outMutex);
            std::cout << "Note: daemon is not ready yet (still initializing)\n";
        }

        {
            std::scoped_lock lock(g_outMutex);
            std::cout << "=== Initial inventory ===\n";
        }
        printAll(*client);

        client->onInventoryChanged([](const std::string& fieldPath) {
            if (g_shuttingDown.load()) return;
            {
                std::scoped_lock lock(g_eventMutex);
                g_lastInventoryField = fieldPath;
            }
            g_seenInventoryChanged = true;
            g_refreshRequested = true;
        });

        client->onSourceStateChanged([](const std::string& sourceName) {
            if (g_shuttingDown.load()) return;
            {
                std::scoped_lock lock(g_eventMutex);
                g_lastSourceName = sourceName;
            }
            g_seenSourceStateChanged = true;
            g_refreshRequested = true;
        });

        client->onReadyChanged([](bool ready) {
            if (g_shuttingDown.load()) return;
            {
                std::scoped_lock lock(g_eventMutex);
                g_lastReadyValue = ready;
            }
            g_seenReadyChanged = true;
            g_refreshRequested = true;
        });

        {
            std::scoped_lock lock(g_outMutex);
            std::cout << "Monitoring inventory changes (Ctrl-C to exit)...\n";
        }

        while (g_running.load()) {
            if (g_refreshRequested.exchange(false)) {
                std::optional<std::string> field;
                std::optional<std::string> source;
                std::optional<bool> ready;

                const bool sawInventory = g_seenInventoryChanged.exchange(false);
                const bool sawSource    = g_seenSourceStateChanged.exchange(false);
                const bool sawReady     = g_seenReadyChanged.exchange(false);

                {
                    std::scoped_lock lock(g_eventMutex);
                    field  = g_lastInventoryField;
                    source = g_lastSourceName;
                    ready  = g_lastReadyValue;
                }

                {
                    std::scoped_lock lock(g_outMutex);
                    if (sawInventory && field) {
                        std::cout << "[event] InventoryChanged: " << *field << "\n";
                    }
                    if (sawSource && source) {
                        std::cout << "[event] SourceStateChanged: " << *source << "\n";
                    }
                    if (sawReady && ready) {
                        std::cout << "[event] ReadyChanged: "
                                  << (*ready ? "true" : "false") << "\n";
                    }
                }

                if (!g_shuttingDown.load()) {
                    printAll(*client);
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        {
            std::scoped_lock lock(g_outMutex);
            std::cout << "\nShutting down...\n";
        }

        g_shuttingDown = true;

        client.reset();
        connection.reset();

        try {
            dispatcher->stop();
        } catch (const std::exception& e) {
            std::scoped_lock lock(g_outMutex);
            std::cerr << "dispatcher stop failed: " << e.what() << "\n";
        }

        dispatcher.reset();
    } catch (const std::exception& e) {
        std::scoped_lock lock(g_outMutex);
        std::cerr << "inventory-watch failed: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
