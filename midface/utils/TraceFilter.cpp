#include "utils/TraceFilter.hpp"

namespace midsurface_new
{
TraceFilterOptions::TraceFilterOptions()
    : enabled(FALSE)
{
}

TraceFilter::TraceFilter()
{
}

void TraceFilter::Configure(const TraceFilterOptions& options)
{
    options_ = options;
}

logical TraceFilter::IsEnabled() const
{
    return options_.enabled;
}

logical TraceFilter::ContainsInt(const std::vector<int>& values, int value) const
{
    int i = 0;
    for (i = 0; i < (int)values.size(); ++i)
    {
        if (values[i] == value)
            return TRUE;
    }
    return FALSE;
}

logical TraceFilter::ContainsString(const std::vector<std::string>& values, const char* value) const
{
    if (value == nullptr)
        return FALSE;

    int i = 0;
    for (i = 0; i < (int)values.size(); ++i)
    {
        if (values[i] == value)
            return TRUE;
    }
    return FALSE;
}

logical TraceFilter::ShouldTraceFace(int face_id) const
{
    if (options_.enabled == FALSE)
        return FALSE;
    if (options_.face_ids.empty())
        return TRUE;
    return ContainsInt(options_.face_ids, face_id);
}

logical TraceFilter::ShouldTraceGroup(int group_id) const
{
    if (options_.enabled == FALSE)
        return FALSE;
    if (options_.group_ids.empty())
        return TRUE;
    return ContainsInt(options_.group_ids, group_id);
}

logical TraceFilter::ShouldTracePair(int pair_id) const
{
    if (options_.enabled == FALSE)
        return FALSE;
    if (options_.pair_ids.empty())
        return TRUE;
    return ContainsInt(options_.pair_ids, pair_id);
}

logical TraceFilter::ShouldTraceFacePair(int face_a, int face_b) const
{
    if (options_.enabled == FALSE)
        return FALSE;
    if (options_.face_ids.empty())
        return TRUE;
    return ContainsInt(options_.face_ids, face_a) && ContainsInt(options_.face_ids, face_b);
}

logical TraceFilter::ShouldTraceTag(const char* tag) const
{
    if (options_.enabled == FALSE)
        return FALSE;
    if (options_.tags.empty())
        return TRUE;
    return ContainsString(options_.tags, tag);
}
} // namespace midsurface_new
