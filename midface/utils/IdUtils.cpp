#include "utils/IdUtils.hpp"

#include "logical.h"

#include <sstream>

namespace midsurface_new
{
logical IsValidId(int id)
{
    return id >= 0 ? TRUE : FALSE;
}

int NextSequentialId(int current_count)
{
    if (current_count < 0)
        return 0;
    return current_count;
}

std::string MakePairKey(int id_a, int id_b)
{
    int first = id_a;
    int second = id_b;
    if (second < first)
    {
        const int tmp = first;
        first = second;
        second = tmp;
    }

    std::ostringstream ss;
    ss << first << ":" << second;
    return ss.str();
}
} // namespace midsurface_new
