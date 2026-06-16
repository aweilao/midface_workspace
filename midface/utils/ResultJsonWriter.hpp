#pragma once

#include "MidSurface.h"
#include "core/RunContext.hpp"

namespace midsurface_new
{
std::string ResultJsonPath(const RunContext& context, int body_index);

logical SaveMidSurfaceResultJson(
    const RunContext& context,
    int body_index,
    const MidSurfaceResult& result);
} // namespace midsurface_new
