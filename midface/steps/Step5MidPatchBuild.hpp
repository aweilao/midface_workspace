#pragma once

#include "core/DiagnosticSink.hpp"
#include "steps/Step4RelationBuild.hpp"

namespace midsurface_new
{
struct Step5MidPatchOptions
{
    Step5MidPatchOptions();

    logical build_sheet_body;
    logical emit_patch_events;
    int min_component_pairs;
    logical keep_isolated_pairs;
    logical enable_one_to_n_merge_units;
    logical spline_fit_only;
    double spline_fit_tol_scale;
    double spline_fit_tol_min;
    double spline_fit_tol_max;
    int spline_grid_min;
    int spline_grid_max;
    int freeform_uv_init_u;
    int freeform_uv_init_v;
    int freeform_uv_refine_rounds;
    double freeform_uv_refine_angle_deg;
    int freeform_uv_max_u;
    int freeform_uv_max_v;
    double freeform_uv_fail_ratio_max;
    int freeform_uv_min_valid_points;
    double analytic_patch_inflate;
    double analytic_min_half_extent;
};

struct MidPatchTable
{
    std::vector<MidPatchRecord> patches;
};

struct MidPatchJunctionTable
{
    std::vector<MidPatchJunctionRecord> junctions;
};

struct MidPatchComponentTable
{
    std::vector< std::vector<int> > components;
};

struct MidPatchBuildStats
{
    MidPatchBuildStats();

    int input_pair_count;
    int component_count;
    int requested_patch_count;
    int built_patch_count;
    int failed_patch_count;
    int junction_count;
    int merged_unit_count;
    int merged_pair_count;
};

struct Step5MidPatchState
{
    Step5MidPatchState();

    const Step4RelationState* input_step4;
    Step5MidPatchOptions options_snapshot;
    MidPatchTable patches;
    MidPatchJunctionTable junctions;
    MidPatchComponentTable components;
    MidPatchBuildStats stats;
};

struct Step5MidPatchResult
{
    Step5MidPatchResult();

    logical ok;
    Step5MidPatchState state;
};

logical RunStep5MidPatchBuild(
    const Step4RelationState& step4,
    const Step5MidPatchOptions& options,
    DiagnosticSink* diagnostics,
    Step5MidPatchResult& result);
} // namespace midsurface_new
