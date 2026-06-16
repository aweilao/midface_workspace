#pragma once

#include "logical.h"

#include <string>

namespace midsurface_new
{
logical IsValidId(int id);
int NextSequentialId(int current_count);
std::string MakePairKey(int id_a, int id_b);
} // namespace midsurface_new
