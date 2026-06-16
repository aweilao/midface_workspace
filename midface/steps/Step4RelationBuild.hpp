#pragma once

#include "core/DiagnosticSink.hpp"
#include "steps/Step3PairBuild.hpp"

namespace midsurface_new
{
struct Step4RelationOptions
{
    Step4RelationOptions();

    logical build_pair_links;
    logical build_walls;
    logical emit_relation_events;
    logical enable_tiny_face_partner_patch;
    logical enable_orphan_wall_bridge;
    logical enable_mm2_angle_gate;
    logical enable_mm2_sweep_wall;
    logical mm2_use_all_hit_buckets;
    logical mm2_per_edge_sweep_only;
    logical mm2_enable_smooth_sampled_path;
    logical mm2_smooth_keep_closed;
    logical mm2_enable_connect_strategy;
    logical mm2_endpoint_single_use;
    logical mm2_sweep_owner1_or3_only;
    logical mm2_extend_only_open_ends;
    logical mm2_path_end_tbar_enable;
    logical mm2_fallback_edgewise_sweep;
    int min_link_hits;
    double pair_mm2_angle_deg;
    double mm2_smooth_sample_step_ratio;
    double mm2_smooth_sample_step_abs_min;
    double mm2_connect_gap_ratio;
    double mm2_connect_gap_abs_min;
    double mm2_sweep_thickness_scale;
    double mm2_sweep_thickness_min;
    double mm2_path_end_extend_ratio;
    double mm2_path_end_extend_abs_min;
    double mm2_path_end_tbar_ratio;
};

struct PairRelationTable
{
    std::vector<PairRelationRecord> relations;
};

struct PairWallRelationTable
{
    std::vector<PairWallRelationRecord> relations;
};

struct RelationBuildStats
{
    RelationBuildStats();

    int relation_count;
    int wall_count;
    int group_adjacency_count;
    int coedge_total;
    int coedge_with_partner;
    int coedge_partner_ring_gt2;
    int unique_nonmanifold_edges;
    int partner_ring_max;
    int tiny_faces_found;
    int tiny_partner_coedges;
    int tiny_patch_applied;
    int pair_wall_relation_count;
    int orphan_wall_group_count;
    int orphan_bridge_injected_count;
    int direct_pair_link_count;
    int mm1_count;
    int mm2_count;
    int virtual_wall_try_count;
    int virtual_wall_ok_count;
};

struct Step4RelationState
{
    Step4RelationState();

    const Step3PairState* input_step3;
    Step4RelationOptions options_snapshot;
    PairRelationTable pair_relations;
    PairWallRelationTable pair_wall_relations;
    WallTable walls;
    RelationBuildStats stats;
};

struct Step4RelationResult
{
    Step4RelationResult();

    logical ok;
    Step4RelationState state;
};

logical RunStep4RelationBuild(
    const Step3PairState& step3,
    const Step4RelationOptions& options,
    DiagnosticSink* diagnostics,
    Step4RelationResult& result);
} // namespace midsurface_new
