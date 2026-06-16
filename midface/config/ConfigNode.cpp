#include "config/ConfigNode.hpp"

#include <cstdlib>

namespace midsurface_new
{
namespace
{
std::string LowerText(std::string value)
{
    int i = 0;
    for (i = 0; i < (int)value.size(); ++i)
    {
        if (value[i] >= 'A' && value[i] <= 'Z')
            value[i] = (char)(value[i] - 'A' + 'a');
    }
    return value;
}

logical ParseLogicalValue(const std::string& value, logical fallback)
{
    const std::string lower = LowerText(value);
    if (lower == "true" || lower == "yes" || lower == "on" || lower == "1")
        return TRUE;
    if (lower == "false" || lower == "no" || lower == "off" || lower == "0")
        return FALSE;
    return fallback;
}
} // namespace

ConfigNode::ConfigNode()
    : has_value_(FALSE)
{
}

logical ConfigNode::has_value() const
{
    return has_value_;
}

const std::string& ConfigNode::value() const
{
    return value_;
}

void ConfigNode::SetValue(const std::string& value)
{
    has_value_ = TRUE;
    value_ = value;
}

logical ConfigNode::HasChild(const std::string& key) const
{
    return children_.find(key) != children_.end() ? TRUE : FALSE;
}

const ConfigNode* ConfigNode::FindChild(const std::string& key) const
{
    std::map<std::string, ConfigNode>::const_iterator it = children_.find(key);
    if (it == children_.end())
        return nullptr;
    return &it->second;
}

ConfigNode& ConfigNode::Child(const std::string& key)
{
    return children_[key];
}

const std::map<std::string, ConfigNode>& ConfigNode::children() const
{
    return children_;
}

void ConfigNode::AddListItem(const ConfigNode& item)
{
    list_.push_back(item);
}

const std::vector<ConfigNode>& ConfigNode::list() const
{
    return list_;
}

logical ConfigNode::IsList() const
{
    return !list_.empty() ? TRUE : FALSE;
}

std::string ConfigNode::GetString(const std::string& key, const std::string& fallback) const
{
    const ConfigNode* child = FindChild(key);
    if (child == nullptr || child->has_value() == FALSE)
        return fallback;
    return child->value();
}

int ConfigNode::GetInt(const std::string& key, int fallback) const
{
    const ConfigNode* child = FindChild(key);
    if (child == nullptr || child->has_value() == FALSE)
        return fallback;
    return std::atoi(child->value().c_str());
}

double ConfigNode::GetDouble(const std::string& key, double fallback) const
{
    const ConfigNode* child = FindChild(key);
    if (child == nullptr || child->has_value() == FALSE)
        return fallback;
    return std::atof(child->value().c_str());
}

logical ConfigNode::GetLogical(const std::string& key, logical fallback) const
{
    const ConfigNode* child = FindChild(key);
    if (child == nullptr || child->has_value() == FALSE)
        return fallback;
    return ParseLogicalValue(child->value(), fallback);
}

std::vector<std::string> ConfigNode::GetStringList(const std::string& key, const std::vector<std::string>& fallback) const
{
    const ConfigNode* child = FindChild(key);
    if (child == nullptr)
        return fallback;

    std::vector<std::string> out;
    const std::vector<ConfigNode>& items = child->list();
    int i = 0;
    for (i = 0; i < (int)items.size(); ++i)
    {
        if (items[i].has_value() != FALSE)
            out.push_back(items[i].value());
    }
    if (out.empty() && child->has_value() != FALSE)
        out.push_back(child->value());
    return out.empty() ? fallback : out;
}

std::vector<int> ConfigNode::GetIntList(const std::string& key, const std::vector<int>& fallback) const
{
    const ConfigNode* child = FindChild(key);
    if (child == nullptr)
        return fallback;

    std::vector<int> out;
    const std::vector<ConfigNode>& items = child->list();
    int i = 0;
    for (i = 0; i < (int)items.size(); ++i)
    {
        if (items[i].has_value() != FALSE)
            out.push_back(std::atoi(items[i].value().c_str()));
    }
    if (out.empty() && child->has_value() != FALSE)
        out.push_back(std::atoi(child->value().c_str()));
    return out.empty() ? fallback : out;
}
} // namespace midsurface_new
