#include "utils/GroupUtils.hpp"

#include "queryapi.hxx"

#include <algorithm>
#include <cmath>
#include <string>

namespace midsurface_new
{
namespace
{
double Clamp01(double x)
{
    if (x < 0.0)
        return 0.0;
    if (x > 1.0)
        return 1.0;
    return x;
}

double Quantile(std::vector<double> vals, double q)
{
    if (vals.empty())
        return -1.0;
    std::sort(vals.begin(), vals.end());
    q = Clamp01(q);
    const double pos = q * (double)(vals.size() - 1);
    const int i0 = (int)std::floor(pos);
    const int i1 = (int)std::ceil(pos);
    if (i0 == i1)
        return vals[i0];
    const double t = pos - (double)i0;
    return vals[i0] * (1.0 - t) + vals[i1] * t;
}
} // namespace

GroupClosestPointResult::GroupClosestPointResult()
    : found(FALSE),
      face(nullptr),
      face_id(-1),
      distance(0.0)
{
}

double GroupAreaSafe(const GroupRecord& group)
{
    return group.area_sum > 1.0e-12 ? group.area_sum : 1.0e-12;
}

double GroupScale(const GroupRecord& group)
{
    return std::sqrt(GroupAreaSafe(group));
}

double FaceWidthScale(const FaceRecord& record)
{
    if (record.has_edge_lengths != FALSE && record.edge_max > 1.0e-12)
    {
        const double w = record.area_proxy / record.edge_max;
        if (w > 1.0e-12)
            return w;
    }
    if (record.has_edge_lengths != FALSE && record.edge_min > 1.0e-12)
        return record.edge_min;
    if (record.has_area_proxy != FALSE && record.area_proxy > 1.0e-12)
        return std::sqrt(record.area_proxy);
    return 1.0e-6;
}

const FaceRecord* FindFaceRecord(const Step2GroupState& step2, int face_id)
{
    if (step2.input_step1 == nullptr)
        return nullptr;

    const std::vector<FaceRecord>& records = step2.input_step1->faces.records;
    int i = 0;
    for (i = 0; i < (int)records.size(); ++i)
    {
        if (records[i].face_id == face_id)
            return &records[i];
    }
    return nullptr;
}

double GroupWidthScale(const GroupRecord& group, const Step2GroupState& step2)
{
    std::vector<double> widths;
    int i = 0;
    for (i = 0; i < (int)group.face_ids.size(); ++i)
    {
        const FaceRecord* record = FindFaceRecord(step2, group.face_ids[i]);
        if (record == nullptr || record->valid == FALSE)
            continue;
        const double w = FaceWidthScale(*record);
        if (w > 1.0e-12)
            widths.push_back(w);
    }
    if (widths.empty())
        return GroupScale(group);
    return Quantile(widths, 0.50);
}

void CollectGroupInteriorSamples(
    const GroupRecord& group,
    const Step2GroupState& step2,
    const InteriorSampleOptions& sample_options,
    std::vector<std::pair<const FaceRecord*, SPAposition> >& out_samples)
{
    out_samples.clear();

    int i = 0;
    for (i = 0; i < (int)group.face_ids.size(); ++i)
    {
        const FaceRecord* record = FindFaceRecord(step2, group.face_ids[i]);
        if (record == nullptr || record->valid == FALSE || record->face == nullptr)
            continue;

        std::vector<SPAposition> face_samples;
        std::string sample_reason;
        (void)BuildInteriorPositionSamples(*record, sample_options, face_samples, sample_reason);

        int k = 0;
        for (k = 0; k < (int)face_samples.size(); ++k)
            out_samples.push_back(std::make_pair(record, face_samples[k]));
    }
}

logical FindClosestPointOnGroup(
    const GroupRecord& group,
    const Step2GroupState& step2,
    const SPAposition& point,
    GroupClosestPointResult& out_result)
{
    out_result = GroupClosestPointResult();

    int i = 0;
    for (i = 0; i < (int)group.face_ids.size(); ++i)
    {
        const FaceRecord* record = FindFaceRecord(step2, group.face_ids[i]);
        if (record == nullptr || record->valid == FALSE || record->face == nullptr)
            continue;

        SPAposition cp;
        double d = 0.0;
        SPAposition query = point;
        outcome r = api_entity_point_distance(record->face, query, cp, d);
        if (!r.ok())
            continue;

        if (out_result.found == FALSE || d < out_result.distance)
        {
            out_result.found = TRUE;
            out_result.point = cp;
            out_result.face = record->face;
            out_result.face_id = record->face_id;
            out_result.distance = d;
        }
    }
    return out_result.found;
}
} // namespace midsurface_new
