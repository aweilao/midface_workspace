#pragma once

#include "core/DiagnosticSink.hpp"
#include "core/MidSurfaceNewTypes.hpp"

namespace midsurface_new
{
struct Step1FaceAnalyzeOptions
{
    Step1FaceAnalyzeOptions();

    logical build_adjacency;
    logical build_sample_summary;
    logical emit_face_events;
};

struct FaceTable
{
    std::vector<FaceRecord> records;
};

struct FaceAdjacencyGraph
{
    std::vector<FaceAdjacency> edges;
};

struct FaceAnalyzeStats
{
    FaceAnalyzeStats();

    int face_count;
    int valid_face_count;
    int invalid_face_count;
    int adjacency_count;
};

struct Step1FaceAnalyzeState
{
    Step1FaceAnalyzeState();

    BODY* input_body;
    Step1FaceAnalyzeOptions options_snapshot;
    FaceTable faces;
    FaceAdjacencyGraph adjacency;
    std::vector<PointSample> sample_summary;
    ModelScaleContext model_scale;
    FaceAnalyzeStats stats;
};

struct Step1FaceAnalyzeResult
{
    Step1FaceAnalyzeResult();

    logical ok;
    Step1FaceAnalyzeState state;
};

logical RunStep1FaceAnalyze(
    BODY* body,
    const Step1FaceAnalyzeOptions& options,
    DiagnosticSink* diagnostics,
    Step1FaceAnalyzeResult& result);
} // namespace midsurface_new
