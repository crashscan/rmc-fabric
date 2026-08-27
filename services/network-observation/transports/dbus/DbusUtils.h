//
// Created by vvass on 25-Jul-26.
//
/**
 * @file DbusUtils.h
 * @brief Reusable D-Bus variant map reader for deserialization.
 */
#pragma once
#include <dbus-cxx.h>
#include <map>
#include <set>
#include <string>

namespace RSCGroup {

/**
 * @brief Convenience reader for a{sv} D-Bus dict entries.
 *
 * Wraps a const std::map<std::string, DBus::Variant>& and
 * provides typed accessors that return default values when
 * a key is absent or the variant type doesn't match.
 */
class DbusVariantMapReader {
public:
    explicit DbusVariantMapReader(const std::map<std::string, DBus::Variant>& m)
        : m_(m) {}

    int getInt(const char* k) const {
        return m_.contains(k) ? static_cast<int>(m_.at(k).to_int32()) : 0;
    }

    std::string getStr(const char* k) const {
        return m_.contains(k) ? m_.at(k).to_string() : std::string{};
    }

    bool getBool(const char* k) const {
        return m_.contains(k) && m_.at(k).to_bool();
    }

    std::set<std::string> getStrSet(const char* k) const {
        if (!m_.contains(k)) return {};
        std::set<std::string> out;
        auto varCopy = m_.at(k);
        for (const auto& v : varCopy.to_vector<DBus::Variant>())
            out.insert(v.to_string());
        return out;
    }

private:
    const std::map<std::string, DBus::Variant>& m_;
};

} // namespace RSCGroup