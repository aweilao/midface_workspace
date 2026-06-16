#include "utils/JsonUtils.hpp"

namespace midsurface_new
{
std::string JsonString(const std::string& value)
{
    return JsonValue(value).dump();
}

std::string JsonBool(logical value)
{
    return JsonValue(value != FALSE).dump();
}

JsonValue JsonPoint(const SPAposition& point)
{
    JsonValue out = JsonValue::array();
    out.push_back(point.x());
    out.push_back(point.y());
    out.push_back(point.z());
    return out;
}

std::string JsonDump(const JsonValue& value)
{
    return value.dump();
}

logical IsLikelyJsonValue(const std::string& value)
{
    return JsonValue::accept(value) ? TRUE : FALSE;
}
} // namespace midsurface_new
