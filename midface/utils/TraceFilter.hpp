#pragma once

#include "logical.h"

#include <string>
#include <vector>

namespace midsurface_new
{
struct TraceFilterOptions
{
    TraceFilterOptions();

    logical enabled;
    std::vector<int> face_ids;
    std::vector<int> group_ids;
    std::vector<int> pair_ids;
    std::vector<std::string> tags;
};

class TraceFilter
{
public:
    TraceFilter();

    void Configure(const TraceFilterOptions& options);
    logical IsEnabled() const;
    logical ShouldTraceFace(int face_id) const;
    logical ShouldTraceGroup(int group_id) const;
    logical ShouldTracePair(int pair_id) const;
    logical ShouldTraceFacePair(int face_a, int face_b) const;
    logical ShouldTraceTag(const char* tag) const;

private:
    logical ContainsInt(const std::vector<int>& values, int value) const;
    logical ContainsString(const std::vector<std::string>& values, const char* value) const;

private:
    TraceFilterOptions options_;
};
} // namespace midsurface_new
