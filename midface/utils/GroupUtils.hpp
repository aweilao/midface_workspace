#pragma once

#include "core/MidSurfaceNewTypes.hpp"
#include "steps/Step2GroupBuild.hpp"
#include "utils/SamplingUtils.hpp"

#include "face.hxx"
#include "logical.h"
#include "position.hxx"

#include <utility>
#include <vector>

namespace midsurface_new
{
struct GroupClosestPointResult
{
    GroupClosestPointResult();

    logical found;
    SPAposition point;
    FACE* face;
    int face_id;
    double distance;
};

double GroupAreaSafe(const GroupRecord& group);
double GroupScale(const GroupRecord& group);
double FaceWidthScale(const FaceRecord& record);
const FaceRecord* FindFaceRecord(const Step2GroupState& step2, int face_id);
double GroupWidthScale(const GroupRecord& group, const Step2GroupState& step2);

void CollectGroupInteriorSamples(
    const GroupRecord& group,
    const Step2GroupState& step2,
    const InteriorSampleOptions& sample_options,
    std::vector<std::pair<const FaceRecord*, SPAposition> >& out_samples);

logical FindClosestPointOnGroup(
    const GroupRecord& group,
    const Step2GroupState& step2,
    const SPAposition& point,
    GroupClosestPointResult& out_result);
} // namespace midsurface_new
