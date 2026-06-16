#pragma once

#include "core/DiagnosticSink.hpp"
#include "steps/Step1FaceAnalyze.hpp"

namespace midsurface_new
{
struct Step2GroupOptions
{
    Step2GroupOptions();

    logical enable_surface_prefilter;
    logical enable_sample_refine;
    logical emit_prefilter_events;
    logical emit_refine_events;
    logical emit_group_events;
    logical emit_group_artifacts;
    logical emit_traced_pair_samples;

    double normal_angle_deg;
    double axis_angle_deg;
    double plane_tol;
    double radius_tol;
    logical auto_tolerance;

    double sample_density;
    int sample_min_count;
    int sample_max_count;
    int edge_min_samples_per_edge;
    double edge_sample_alpha;
    int max_samples_per_face;
    double edge_length_eps;
    logical auto_edge_length_eps;

    int local_nearest_count;
    int local_min_pass_count;
    double local_distance_gate_scale;
    double local_normal_component_max;
};

struct GroupCandidateTable
{
    std::vector<GroupMergeCandidate> candidates;
};

struct GroupDecisionTable
{
    std::vector<GroupMergeDecision> decisions;
};

struct GroupTable
{
    std::vector<GroupRecord> groups;
};

struct FaceToGroupMap
{
    std::map<int, int> face_to_group;
};

struct GroupBuildStats
{
    GroupBuildStats();

    int candidate_count;
    int accepted_count;
    int rejected_count;
    int group_count;
    int valid_face_count;
    int surface_prefilter_pass_count;
    int surface_prefilter_reject_count;
    int sample_refine_pass_count;
    int sample_refine_reject_count;
    int single_face_group_count;
};

struct Step2GroupState
{
    Step2GroupState();

    const Step1FaceAnalyzeState* input_step1;
    Step2GroupOptions options_snapshot;
    GroupCandidateTable candidates;
    GroupDecisionTable decisions;
    GroupTable groups;
    FaceToGroupMap face_to_group;
    std::map<std::string, int> reject_reason_stats;
    GroupBuildStats stats;
};

struct Step2GroupResult
{
    Step2GroupResult();

    logical ok;
    Step2GroupState state;
};

logical RunStep2GroupBuild(
    const Step1FaceAnalyzeState& step1,
    const Step2GroupOptions& options,
    DiagnosticSink* diagnostics,
    Step2GroupResult& result);
} // namespace midsurface_new
