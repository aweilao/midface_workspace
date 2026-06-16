#pragma once

#include "core/DiagnosticSink.hpp"
#include "steps/Step2GroupBuild.hpp"

namespace midsurface_new
{
struct Step3PairOptions
{
    Step3PairOptions();

    logical enable_coarse_normal_prefilter;
    logical enable_surface_prefilter;
    logical enable_pair_refine;
    logical allow_multi_pair_per_group;
    logical emit_candidate_events;
    logical emit_refine_events;
    logical emit_pair_events;

    double pair_opposite_angle_deg;
    double pair_direction_angle_deg;
    int pair_min_pass_count;
    double pair_min_pass_ratio;
    double pair_min_small_coverage;
    logical pair_same_type_only;
    double pair_interior_sample_density;
    int pair_interior_sample_min;
    int pair_interior_sample_max;
    double pair_distance_quantile;
    double pair_thickness_min;
    double pair_thickness_max;
    double pair_adaptive_dist_ratio;
    logical pair_enable_facing_dist_relax;
    double pair_facing_relax_start;
    double pair_facing_dist_relax_max;
    double pair_facing_dist_relax_power;
    double pair_thinness_max;
    double pair_min_score;
    int pair_max_per_group;
    double pair_group_thickness_ratio_max;
    double rib_area_ratio;
};

struct PairCandidateTable
{
    std::vector<PairCandidate> candidates;
};

struct PairDecisionTable
{
    std::vector<PairDecisionRecord> decisions;
};

struct PairTable
{
    std::vector<PairRecord> pairs;
};

struct GroupToPairMap
{
    std::map<int, std::vector<int> > group_to_pairs;
};

struct WallTable
{
    std::vector<WallRecord> walls;
};

struct GroupToWallMap
{
    std::map<int, int> group_to_wall;
};

struct PairBuildStats
{
    PairBuildStats();

    int candidate_count;
    int accepted_count;
    int rejected_count;
    int pair_count;
    int coarse_prefilter_pass_count;
    int coarse_prefilter_reject_count;
    int surface_prefilter_pass_count;
    int surface_prefilter_reject_count;
    int refine_pass_count;
    int refine_reject_count;
    int thickness_filter_dropped_count;
    int rib_candidate_count;
    int uncertain_count;
    int wall_count;
    int pair_group_count;
    int wall_group_count;
};

struct Step3PairState
{
    Step3PairState();

    const Step2GroupState* input_step2;
    Step3PairOptions options_snapshot;
    PairCandidateTable candidates;
    PairDecisionTable decisions;
    PairTable pairs;
    WallTable walls;
    GroupToPairMap group_to_pairs;
    GroupToWallMap group_to_walls;
    PairBuildStats stats;
};

struct Step3PairResult
{
    Step3PairResult();

    logical ok;
    Step3PairState state;
};

logical RunStep3PairBuild(
    const Step2GroupState& step2,
    const Step3PairOptions& options,
    DiagnosticSink* diagnostics,
    Step3PairResult& result);
} // namespace midsurface_new
