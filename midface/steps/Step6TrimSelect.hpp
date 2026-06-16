#pragma once

#include "core/DiagnosticSink.hpp"
#include "steps/Step5MidPatchBuild.hpp"
#include "steps/Step7StitchInput.hpp"

#include "box.hxx"
#include "coedge.hxx"

namespace midsurface_new
{
struct Step6TrimSelectOptions
{
    Step6TrimSelectOptions();

    logical enable_trim;
    logical emit_selection_events;
    logical build_slice_adjacency;
    int edge_match_sample_count;
    double edge_match_tolerance;
    double edge_match_length_tolerance;
    double adjacency_distance_units;
    int adjacency_sample_count;
    double adjacency_min_pass_ratio;

    logical enable_source_trim;
    logical enable_body_prewall_extend;
    double mid_extend_scale;
    double mid_extend_min;
    double wall_extend_scale;
    double wall_extend_min;
    double wall_sphere_radius_boost_ratio;
    logical use_api_extend_fail_fallback_only;
    logical api_extend_fail_fallback_use_uv_expand;
    double cylinder_uv_expand_scale;
    double sphere_uv_expand_scale;
    double torus_uv_expand_scale;
    int extend_fail_fallback_sample_count;
    int uniform_sample_count;
    double pass_ratio_required;
    double facing_min_cos;
    double area_min_ratio;
    logical use_between_score_pick;
    double between_score_min;
    double between_area_floor_ratio;
    int between_sample_count;
    int select_sample_count;
    double min_sample_success_ratio;
    double max_norm_std;
    double max_mean_thickness_error;
    double min_normal_support_ratio;
    double nonfree_relax_factor;
    double nonfree_min_pass_ratio;
    double freeform_relax_factor;
    double freeform_min_pass_ratio;
    double small_face_ratio;
};

struct TrimSelectionTable
{
    std::vector<TrimSelectionRecord> selections;
};

enum Step6ImprintSide
{
    STEP6_IMPRINT_SIDE_A = 0,
    STEP6_IMPRINT_SIDE_B = 1
};

struct Step6EdgeGeom
{
    Step6EdgeGeom();

    EDGE* edge;
    COEDGE* coedge;
    SPAposition p0;
    SPAposition p1;
    SPAposition mid;
    SPAbox bbox;
    double length;
    int curve_type;
    logical valid;
};

struct Step6SliceRecord
{
    Step6SliceRecord();

    int slice_id;
    FACE* face;
    int selected_index;
    int source_patch_id;
    int source_pair_id;
    int source_seed_rep_pair;
    double area;
};

struct Step6RawImprintSideEdge
{
    Step6RawImprintSideEdge();

    int side_edge_id;
    int raw_id;
    Step6ImprintSide side;
    Step6EdgeGeom geom;
};

struct Step6RawImprintEdgePair
{
    Step6RawImprintEdgePair();

    int imprint_edge_id;
    int raw_a;
    int raw_b;
    int side_edge_a_id;
    int side_edge_b_id;
    EDGE* edge_a;
    EDGE* edge_b;
    logical reversed;
    double endpoint_error;
    double sample_error;
    double length_error;
    logical ambiguous;
};

struct Step6SliceEdgeUse
{
    Step6SliceEdgeUse();

    int use_id;
    int slice_id;
    EDGE* slice_edge;
    int imprint_edge_id;
    Step6ImprintSide side;
    double coverage_ratio;
};

struct Step6SliceAdjacency
{
    Step6SliceAdjacency();

    int adjacency_id;
    int slice_a;
    int slice_b;
    int imprint_edge_id;
    double shared_length;
};

struct Step6SliceTable
{
    std::vector<Step6SliceRecord> slices;
};

struct Step6RawImprintEdgeTable
{
    std::vector<Step6RawImprintSideEdge> side_edges;
    std::vector<Step6RawImprintEdgePair> pairs;
};

struct Step6SliceEdgeUseTable
{
    std::vector<Step6SliceEdgeUse> uses;
};

struct Step6SliceAdjacencyTable
{
    std::vector<Step6SliceAdjacency> adjacencies;
};

struct TrimSelectStats
{
    TrimSelectStats();

    int input_patch_count;
    int input_pair_count;
    int mid_seed_try_count;
    int mid_seed_ok_count;
    int mid_seed_reused_count;
    int wall_seed_try_count;
    int wall_seed_ok_count;
    int virtual_wall_seed_count;
    int virtual_wall_raw_count;
    int mm1_bidirectional_edge_count;
    int mm1_directional_edge_count;
    int mm2_skipped_count;
    int trim_seed_group_count;
    int trim_tool_try_count;
    int trim_tool_ok_count;
    int preselect_split_face_count;
    int selected_count;
    int rejected_count;
    int selected_copy_fail_count;
    int selected_dedup_skip_count;
    int slice_count;
    int side_edge_count;
    int raw_imprint_edge_pair_count;
    int ambiguous_edge_pair_count;
    int slice_edge_use_count;
    int slice_adjacency_count;
};

struct Step6TrimSelectState
{
    Step6TrimSelectState();

    const Step5MidPatchState* input_step5;
    Step6TrimSelectOptions options_snapshot;
    TrimSelectionTable selections;
    Step6SliceTable slices;
    Step6RawImprintEdgeTable raw_imprint_edges;
    Step6SliceEdgeUseTable slice_edge_uses;
    Step6SliceAdjacencyTable slice_adjacencies;
    Step7StitchInput step7_stitch_input;
    TrimSelectStats stats;
};

struct Step6TrimSelectResult
{
    Step6TrimSelectResult();

    logical ok;
    Step6TrimSelectState state;
};

logical RunStep6TrimSelect(
    const Step5MidPatchState& step5,
    const Step6TrimSelectOptions& options,
    DiagnosticSink* diagnostics,
    Step6TrimSelectResult& result);
} // namespace midsurface_new
