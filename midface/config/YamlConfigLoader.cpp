#include "config/YamlConfigLoader.hpp"

#include "third_party/mini-yaml/yaml/Yaml.hpp"

namespace midsurface_new
{
namespace
{
void ConvertYamlNode(const Yaml::Node& src, ConfigNode& dst)
{
    if (src.IsScalar())
    {
        dst.SetValue(src.As<std::string>());
        return;
    }

    if (src.IsSequence())
    {
        Yaml::ConstIterator it = src.Begin();
        for (; it != src.End(); it++)
        {
            const Yaml::Node& src_item = (*it).second;
            ConfigNode dst_item;
            ConvertYamlNode(src_item, dst_item);
            dst.AddListItem(dst_item);
        }
        return;
    }

    if (src.IsMap())
    {
        Yaml::ConstIterator it = src.Begin();
        for (; it != src.End(); it++)
        {
            const std::string& key = (*it).first;
            const Yaml::Node& src_child = (*it).second;
            ConfigNode& dst_child = dst.Child(key);
            ConvertYamlNode(src_child, dst_child);
        }
    }
}
} // namespace

logical LoadYamlConfigFile(const char* yaml_path, ConfigNode& out_root)
{
    if (yaml_path == nullptr)
        return FALSE;

    Yaml::Node root;
    try
    {
        Yaml::Parse(root, yaml_path);
    }
    catch (const Yaml::Exception&)
    {
        return FALSE;
    }

    out_root = ConfigNode();
    ConvertYamlNode(root, out_root);
    return TRUE;
}
} // namespace midsurface_new
