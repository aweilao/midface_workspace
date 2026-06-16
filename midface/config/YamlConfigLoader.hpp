#pragma once

#include "config/ConfigNode.hpp"

namespace midsurface_new
{
logical LoadYamlConfigFile(const char* yaml_path, ConfigNode& out_root);
} // namespace midsurface_new
