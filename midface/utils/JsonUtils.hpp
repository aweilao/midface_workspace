#pragma once

#include "logical.h"

#include "point.hxx"

#include <string>
#include <vector>

#include "third_party/nlohmann/json.hpp"

namespace midsurface_new
{
typedef nlohmann::json JsonValue;

std::string JsonString(const std::string& value);
std::string JsonBool(logical value);
JsonValue JsonPoint(const SPAposition& point);
std::string JsonDump(const JsonValue& value);
logical IsLikelyJsonValue(const std::string& value);
} // namespace midsurface_new
