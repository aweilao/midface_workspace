#pragma once

#include "core/MidSurfaceNewTypes.hpp"

#include "edge.hxx"
#include "face.hxx"
#include "logical.h"
#include "position.hxx"

#include <string>
#include <vector>

namespace midsurface_new
{
struct SampleTargetOptions
{
    SampleTargetOptions();

    double density;
    int min_count;
    int max_count;
};

struct BoundarySampleOptions
{
    BoundarySampleOptions();

    SampleTargetOptions target;
    int min_samples_per_edge;
    double extra_sample_alpha;
    int max_samples_per_face;
    double edge_length_eps;
    double dedup_tol2;
    const char* role;
};

struct InteriorSampleOptions
{
    InteriorSampleOptions();

    SampleTargetOptions target;
    int candidate_multiplier;
    int candidate_max;
};

int ComputeSampleTarget(double area_proxy, const SampleTargetOptions& options);
logical CollectFaceEdges(FACE* face, std::vector<EDGE*>& out_edges);
logical ProjectPointToFace(FACE* face, const SPAposition& guess, SPAposition& out_point);
logical IsNearAnyPoint(const SPAposition& p, const std::vector<SPAposition>& samples, double tol2);
logical IsNearAnyPointSample(const SPAposition& p, const std::vector<PointSample>& samples, double tol2);

void AddPointSample(
    int face_id,
    const SPAposition& point,
    const SPAunit_vector& normal,
    const char* role,
    std::vector<PointSample>& out_samples);

logical BuildBoundaryPointSamples(
    const FaceRecord& source,
    const BoundarySampleOptions& options,
    std::vector<PointSample>& out_samples,
    std::string& out_reason);

logical BuildInteriorPositionSamples(
    const FaceRecord& record,
    const InteriorSampleOptions& options,
    std::vector<SPAposition>& out_samples,
    std::string& out_reason);
} // namespace midsurface_new
