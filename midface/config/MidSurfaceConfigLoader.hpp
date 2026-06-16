#pragma once

#include "MidSurface.h"
#include "config/ConfigNode.hpp"

namespace midsurface_new
{
logical ApplyMidSurfaceConfigNode(const ConfigNode& root, MidSurfaceConfig& config);
logical LoadMidSurfaceConfigFromYaml(const char* yaml_path, MidSurfaceConfig& config);
} // namespace midsurface_new
