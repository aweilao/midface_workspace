#pragma once

#include "logical.h"

#include <map>
#include <string>
#include <vector>

namespace midsurface_new
{
class ConfigNode
{
public:
    ConfigNode();

    logical has_value() const;
    const std::string& value() const;
    void SetValue(const std::string& value);

    logical HasChild(const std::string& key) const;
    const ConfigNode* FindChild(const std::string& key) const;
    ConfigNode& Child(const std::string& key);
    const std::map<std::string, ConfigNode>& children() const;

    void AddListItem(const ConfigNode& item);
    const std::vector<ConfigNode>& list() const;
    logical IsList() const;

    std::string GetString(const std::string& key, const std::string& fallback) const;
    int GetInt(const std::string& key, int fallback) const;
    double GetDouble(const std::string& key, double fallback) const;
    logical GetLogical(const std::string& key, logical fallback) const;
    std::vector<std::string> GetStringList(const std::string& key, const std::vector<std::string>& fallback) const;
    std::vector<int> GetIntList(const std::string& key, const std::vector<int>& fallback) const;

private:
    logical has_value_;
    std::string value_;
    std::map<std::string, ConfigNode> children_;
    std::vector<ConfigNode> list_;
};
} // namespace midsurface_new
