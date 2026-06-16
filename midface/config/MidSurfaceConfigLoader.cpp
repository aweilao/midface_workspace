#include "config/MidSurfaceConfigLoader.hpp"

#include "config/YamlConfigLoader.hpp"

namespace midsurface_new
{
namespace
{
void ApplyRunContext(const ConfigNode& node, RunContextOptions& options)
{
    options.run_id = node.GetString("run_id", options.run_id);
    options.workspace_root = node.GetString("workspace_root", options.workspace_root);
    options.start_method = node.GetString("start_method", options.start_method);
    options.input_sat_path = node.GetString("input_sat_path", options.input_sat_path);
    options.output_root = node.GetString("output_root", options.output_root);
    options.output_sat_dir = node.GetString("output_sat_dir", options.output_sat_dir);
    options.output_log_dir = node.GetString("output_log_dir", options.output_log_dir);
    options.checkpoint_root = node.GetString("checkpoint_root", options.checkpoint_root);
    options.stop_after_step = node.GetInt("stop_after_step", options.stop_after_step);
    options.debug_sat_outputs = node.GetStringList("debug_sat_outputs", options.debug_sat_outputs);
    options.checkpoint_outputs = node.GetStringList("checkpoint_outputs", options.checkpoint_outputs);
    options.resume_from_checkpoint = node.GetString("resume_from_checkpoint", options.resume_from_checkpoint);
    options.enable_diagnostics = node.GetLogical("enable_diagnostics", options.enable_diagnostics);
    options.append_diagnostics = node.GetLogical("append_diagnostics", options.append_diagnostics);
    options.enable_checkpoints = node.GetLogical("enable_checkpoints", options.enable_checkpoints);

    const ConfigNode* trace = node.FindChild("trace");
    if (trace != nullptr)
    {
        options.trace.enabled = trace->GetLogical("enabled", options.trace.enabled);
        options.trace.face_ids = trace->GetIntList("face_ids", options.trace.face_ids);
        options.trace.group_ids = trace->GetIntList("group_ids", options.trace.group_ids);
        options.trace.pair_ids = trace->GetIntList("pair_ids", options.trace.pair_ids);
        options.trace.tags = trace->GetStringList("tags", options.trace.tags);
    }
}

void ApplyStep1(const ConfigNode& node, Step1FaceAnalyzeOptions& options)
{
    options.build_adjacency = node.GetLogical("build_adjacency", options.build_adjacency);
    options.build_sample_summary = node.GetLogical("build_sample_summary", options.build_sample_summary);
    options.emit_face_events = node.GetLogical("emit_face_events", options.emit_face_events);
}

void ApplyStep2(const ConfigNode& node, Step2GroupOptions& options)
{
    options.enable_surface_prefilter = node.GetLogical("enable_surface_prefilter", options.enable_surface_prefilter);
    options.enable_sample_refine = node.GetLogical("enable_sample_refine", options.enable_sample_refine);
    options.emit_prefilter_events = node.GetLogical("emit_prefilter_events", options.emit_prefilter_events);
    options.emit_refine_events = node.GetLogical("emit_refine_events", options.emit_refine_events);
    options.emit_group_events = node.GetLogical("emit_group_events", options.emit_group_events);
    options.emit_group_artifacts = node.GetLogical("emit_group_artifacts", options.emit_group_artifacts);
    options.emit_traced_pair_samples = node.GetLogical("emit_traced_pair_samples", options.emit_traced_pair_samples);
    options.normal_angle_deg = node.GetDouble("normal_angle_deg", options.normal_angle_deg);
    options.axis_angle_deg = node.GetDouble("axis_angle_deg", options.axis_angle_deg);
    options.plane_tol = node.GetDouble("plane_tol", options.plane_tol);
    options.radius_tol = node.GetDouble("radius_tol", options.radius_tol);
    options.auto_tolerance = node.GetLogical("auto_tolerance", options.auto_tolerance);
    options.sample_density = node.GetDouble("sample_density", options.sample_density);
    options.sample_min_count = node.GetInt("sample_min_count", options.sample_min_count);
    options.sample_max_count = node.GetInt("sample_max_count", options.sample_max_count);
    options.edge_min_samples_per_edge = node.GetInt("edge_min_samples_per_edge", options.edge_min_samples_per_edge);
    options.edge_sample_alpha = node.GetDouble("edge_sample_alpha", options.edge_sample_alpha);
    options.max_samples_per_face = node.GetInt("max_samples_per_face", options.max_samples_per_face);
    options.edge_length_eps = node.GetDouble("edge_length_eps", options.edge_length_eps);
    options.auto_edge_length_eps = node.GetLogical("auto_edge_length_eps", options.auto_edge_length_eps);
    options.local_nearest_count = node.GetInt("local_nearest_count", options.local_nearest_count);
    options.local_min_pass_count = node.GetInt("local_min_pass_count", options.local_min_pass_count);
    options.local_distance_gate_scale = node.GetDouble("local_distance_gate_scale", options.local_distance_gate_scale);
    options.local_normal_component_max = node.GetDouble("local_normal_component_max", options.local_normal_component_max);
}

void ApplyStep3(const ConfigNode& node, Step3PairOptions& options)
{
    options.enable_coarse_normal_prefilter = node.GetLogical("enable_coarse_normal_prefilter", options.enable_coarse_normal_prefilter);
    options.enable_surface_prefilter = node.GetLogical("enable_surface_prefilter", options.enable_surface_prefilter);
    options.enable_pair_refine = node.GetLogical("enable_pair_refine", options.enable_pair_refine);
    options.allow_multi_pair_per_group = node.GetLogical("allow_multi_pair_per_group", options.allow_multi_pair_per_group);
    options.emit_candidate_events = node.GetLogical("emit_candidate_events", options.emit_candidate_events);
    options.emit_refine_events = node.GetLogical("emit_refine_events", options.emit_refine_events);
    options.emit_pair_events = node.GetLogical("emit_pair_events", options.emit_pair_events);
    options.pair_opposite_angle_deg = node.GetDouble("pair_opposite_angle_deg", options.pair_opposite_angle_deg);
    options.pair_direction_angle_deg = node.GetDouble("pair_direction_angle_deg", options.pair_direction_angle_deg);
    options.pair_min_pass_count = node.GetInt("pair_min_pass_count", options.pair_min_pass_count);
    options.pair_min_pass_ratio = node.GetDouble("pair_min_pass_ratio", options.pair_min_pass_ratio);
    options.pair_min_small_coverage = node.GetDouble("pair_min_small_coverage", options.pair_min_small_coverage);
    options.pair_same_type_only = node.GetLogical("pair_same_type_only", options.pair_same_type_only);
    options.pair_interior_sample_density = node.GetDouble("pair_interior_sample_density", options.pair_interior_sample_density);
    options.pair_interior_sample_min = node.GetInt("pair_interior_sample_min", options.pair_interior_sample_min);
    options.pair_interior_sample_max = node.GetInt("pair_interior_sample_max", options.pair_interior_sample_max);
    options.pair_distance_quantile = node.GetDouble("pair_distance_quantile", options.pair_distance_quantile);
    options.pair_thickness_min = node.GetDouble("pair_thickness_min", options.pair_thickness_min);
    options.pair_thickness_max = node.GetDouble("pair_thickness_max", options.pair_thickness_max);
    options.pair_adaptive_dist_ratio = node.GetDouble("pair_adaptive_dist_ratio", options.pair_adaptive_dist_ratio);
    options.pair_enable_facing_dist_relax = node.GetLogical("pair_enable_facing_dist_relax", options.pair_enable_facing_dist_relax);
    options.pair_facing_relax_start = node.GetDouble("pair_facing_relax_start", options.pair_facing_relax_start);
    options.pair_facing_dist_relax_max = node.GetDouble("pair_facing_dist_relax_max", options.pair_facing_dist_relax_max);
    options.pair_facing_dist_relax_power = node.GetDouble("pair_facing_dist_relax_power", options.pair_facing_dist_relax_power);
    options.pair_thinness_max = node.GetDouble("pair_thinness_max", options.pair_thinness_max);
    options.pair_min_score = node.GetDouble("pair_min_score", options.pair_min_score);
    options.pair_max_per_group = node.GetInt("pair_max_per_group", options.pair_max_per_group);
    options.pair_group_thickness_ratio_max = node.GetDouble("pair_group_thickness_ratio_max", options.pair_group_thickness_ratio_max);
    options.rib_area_ratio = node.GetDouble("rib_area_ratio", options.rib_area_ratio);
}

void ApplyStep4(const ConfigNode& node, Step4RelationOptions& options)
{
    options.build_pair_links = node.GetLogical("build_pair_links", options.build_pair_links);
    options.build_walls = node.GetLogical("build_walls", options.build_walls);
    options.emit_relation_events = node.GetLogical("emit_relation_events", options.emit_relation_events);
    options.enable_tiny_face_partner_patch = node.GetLogical("enable_tiny_face_partner_patch", options.enable_tiny_face_partner_patch);
    options.enable_orphan_wall_bridge = node.GetLogical("enable_orphan_wall_bridge", options.enable_orphan_wall_bridge);
    options.enable_mm2_angle_gate = node.GetLogical("enable_mm2_angle_gate", options.enable_mm2_angle_gate);
    options.enable_mm2_sweep_wall = node.GetLogical("enable_mm2_sweep_wall", options.enable_mm2_sweep_wall);
    options.mm2_use_all_hit_buckets = node.GetLogical("mm2_use_all_hit_buckets", options.mm2_use_all_hit_buckets);
    options.mm2_per_edge_sweep_only = node.GetLogical("mm2_per_edge_sweep_only", options.mm2_per_edge_sweep_only);
    options.mm2_enable_smooth_sampled_path = node.GetLogical("mm2_enable_smooth_sampled_path", options.mm2_enable_smooth_sampled_path);
    options.mm2_smooth_keep_closed = node.GetLogical("mm2_smooth_keep_closed", options.mm2_smooth_keep_closed);
    options.mm2_enable_connect_strategy = node.GetLogical("mm2_enable_connect_strategy", options.mm2_enable_connect_strategy);
    options.mm2_endpoint_single_use = node.GetLogical("mm2_endpoint_single_use", options.mm2_endpoint_single_use);
    options.mm2_sweep_owner1_or3_only = node.GetLogical("mm2_sweep_owner1_or3_only", options.mm2_sweep_owner1_or3_only);
    options.mm2_extend_only_open_ends = node.GetLogical("mm2_extend_only_open_ends", options.mm2_extend_only_open_ends);
    options.mm2_path_end_tbar_enable = node.GetLogical("mm2_path_end_tbar_enable", options.mm2_path_end_tbar_enable);
    options.mm2_fallback_edgewise_sweep = node.GetLogical("mm2_fallback_edgewise_sweep", options.mm2_fallback_edgewise_sweep);
    options.min_link_hits = node.GetInt("min_link_hits", options.min_link_hits);
    options.pair_mm2_angle_deg = node.GetDouble("pair_mm2_angle_deg", options.pair_mm2_angle_deg);
    options.mm2_smooth_sample_step_ratio = node.GetDouble("mm2_smooth_sample_step_ratio", options.mm2_smooth_sample_step_ratio);
    options.mm2_smooth_sample_step_abs_min = node.GetDouble("mm2_smooth_sample_step_abs_min", options.mm2_smooth_sample_step_abs_min);
    options.mm2_connect_gap_ratio = node.GetDouble("mm2_connect_gap_ratio", options.mm2_connect_gap_ratio);
    options.mm2_connect_gap_abs_min = node.GetDouble("mm2_connect_gap_abs_min", options.mm2_connect_gap_abs_min);
    options.mm2_sweep_thickness_scale = node.GetDouble("mm2_sweep_thickness_scale", options.mm2_sweep_thickness_scale);
    options.mm2_sweep_thickness_min = node.GetDouble("mm2_sweep_thickness_min", options.mm2_sweep_thickness_min);
    options.mm2_path_end_extend_ratio = node.GetDouble("mm2_path_end_extend_ratio", options.mm2_path_end_extend_ratio);
    options.mm2_path_end_extend_abs_min = node.GetDouble("mm2_path_end_extend_abs_min", options.mm2_path_end_extend_abs_min);
    options.mm2_path_end_tbar_ratio = node.GetDouble("mm2_path_end_tbar_ratio", options.mm2_path_end_tbar_ratio);
}

void ApplyStep5(const ConfigNode& node, Step5MidPatchOptions& options)
{
    options.build_sheet_body = node.GetLogical("build_sheet_body", options.build_sheet_body);
    options.emit_patch_events = node.GetLogical("emit_patch_events", options.emit_patch_events);
    options.min_component_pairs = node.GetInt("min_component_pairs", options.min_component_pairs);
    options.keep_isolated_pairs = node.GetLogical("keep_isolated_pairs", options.keep_isolated_pairs);
    options.enable_one_to_n_merge_units = node.GetLogical("enable_one_to_n_merge_units", options.enable_one_to_n_merge_units);
    options.spline_fit_only = node.GetLogical("spline_fit_only", options.spline_fit_only);
    options.spline_fit_tol_scale = node.GetDouble("spline_fit_tol_scale", options.spline_fit_tol_scale);
    options.spline_fit_tol_min = node.GetDouble("spline_fit_tol_min", options.spline_fit_tol_min);
    options.spline_fit_tol_max = node.GetDouble("spline_fit_tol_max", options.spline_fit_tol_max);
    options.spline_grid_min = node.GetInt("spline_grid_min", options.spline_grid_min);
    options.spline_grid_max = node.GetInt("spline_grid_max", options.spline_grid_max);
    options.freeform_uv_init_u = node.GetInt("freeform_uv_init_u", options.freeform_uv_init_u);
    options.freeform_uv_init_v = node.GetInt("freeform_uv_init_v", options.freeform_uv_init_v);
    options.freeform_uv_refine_rounds = node.GetInt("freeform_uv_refine_rounds", options.freeform_uv_refine_rounds);
    options.freeform_uv_refine_angle_deg = node.GetDouble("freeform_uv_refine_angle_deg", options.freeform_uv_refine_angle_deg);
    options.freeform_uv_max_u = node.GetInt("freeform_uv_max_u", options.freeform_uv_max_u);
    options.freeform_uv_max_v = node.GetInt("freeform_uv_max_v", options.freeform_uv_max_v);
    options.freeform_uv_fail_ratio_max = node.GetDouble("freeform_uv_fail_ratio_max", options.freeform_uv_fail_ratio_max);
    options.freeform_uv_min_valid_points = node.GetInt("freeform_uv_min_valid_points", options.freeform_uv_min_valid_points);
    options.analytic_patch_inflate = node.GetDouble("analytic_patch_inflate", options.analytic_patch_inflate);
    options.analytic_min_half_extent = node.GetDouble("analytic_min_half_extent", options.analytic_min_half_extent);
}

void ApplyStep6(const ConfigNode& node, Step6TrimSelectOptions& options)
{
    options.enable_trim = node.GetLogical("enable_trim", options.enable_trim);
    options.emit_selection_events = node.GetLogical("emit_selection_events", options.emit_selection_events);
    options.build_slice_adjacency = node.GetLogical("build_slice_adjacency", options.build_slice_adjacency);
    options.edge_match_sample_count = node.GetInt("edge_match_sample_count", options.edge_match_sample_count);
    options.edge_match_tolerance = node.GetDouble("edge_match_tolerance", options.edge_match_tolerance);
    options.edge_match_length_tolerance = node.GetDouble("edge_match_length_tolerance", options.edge_match_length_tolerance);
    options.adjacency_distance_units = node.GetDouble("adjacency_distance_units", options.adjacency_distance_units);
    options.adjacency_sample_count = node.GetInt("adjacency_sample_count", options.adjacency_sample_count);
    options.adjacency_min_pass_ratio = node.GetDouble("adjacency_min_pass_ratio", options.adjacency_min_pass_ratio);
    options.enable_source_trim = node.GetLogical("enable_source_trim", options.enable_source_trim);
    options.enable_body_prewall_extend = node.GetLogical("enable_body_prewall_extend", options.enable_body_prewall_extend);
    options.mid_extend_scale = node.GetDouble("mid_extend_scale", options.mid_extend_scale);
    options.mid_extend_min = node.GetDouble("mid_extend_min", options.mid_extend_min);
    options.wall_extend_scale = node.GetDouble("wall_extend_scale", options.wall_extend_scale);
    options.wall_extend_min = node.GetDouble("wall_extend_min", options.wall_extend_min);
    options.wall_sphere_radius_boost_ratio = node.GetDouble("wall_sphere_radius_boost_ratio", options.wall_sphere_radius_boost_ratio);
    options.use_api_extend_fail_fallback_only = node.GetLogical("use_api_extend_fail_fallback_only", options.use_api_extend_fail_fallback_only);
    options.api_extend_fail_fallback_use_uv_expand = node.GetLogical("api_extend_fail_fallback_use_uv_expand", options.api_extend_fail_fallback_use_uv_expand);
    options.cylinder_uv_expand_scale = node.GetDouble("cylinder_uv_expand_scale", options.cylinder_uv_expand_scale);
    options.sphere_uv_expand_scale = node.GetDouble("sphere_uv_expand_scale", options.sphere_uv_expand_scale);
    options.torus_uv_expand_scale = node.GetDouble("torus_uv_expand_scale", options.torus_uv_expand_scale);
    options.extend_fail_fallback_sample_count = node.GetInt("extend_fail_fallback_sample_count", options.extend_fail_fallback_sample_count);
    options.uniform_sample_count = node.GetInt("uniform_sample_count", options.uniform_sample_count);
    options.pass_ratio_required = node.GetDouble("pass_ratio_required", options.pass_ratio_required);
    options.facing_min_cos = node.GetDouble("facing_min_cos", options.facing_min_cos);
    options.area_min_ratio = node.GetDouble("area_min_ratio", options.area_min_ratio);
    options.use_between_score_pick = node.GetLogical("use_between_score_pick", options.use_between_score_pick);
    options.between_score_min = node.GetDouble("between_score_min", options.between_score_min);
    options.between_area_floor_ratio = node.GetDouble("between_area_floor_ratio", options.between_area_floor_ratio);
    options.between_sample_count = node.GetInt("between_sample_count", options.between_sample_count);
    options.select_sample_count = node.GetInt("select_sample_count", options.select_sample_count);
    options.min_sample_success_ratio = node.GetDouble("min_sample_success_ratio", options.min_sample_success_ratio);
    options.max_norm_std = node.GetDouble("max_norm_std", options.max_norm_std);
    options.max_mean_thickness_error = node.GetDouble("max_mean_thickness_error", options.max_mean_thickness_error);
    options.min_normal_support_ratio = node.GetDouble("min_normal_support_ratio", options.min_normal_support_ratio);
    options.nonfree_relax_factor = node.GetDouble("nonfree_relax_factor", options.nonfree_relax_factor);
    options.nonfree_min_pass_ratio = node.GetDouble("nonfree_min_pass_ratio", options.nonfree_min_pass_ratio);
    options.freeform_relax_factor = node.GetDouble("freeform_relax_factor", options.freeform_relax_factor);
    options.freeform_min_pass_ratio = node.GetDouble("freeform_min_pass_ratio", options.freeform_min_pass_ratio);
    options.small_face_ratio = node.GetDouble("small_face_ratio", options.small_face_ratio);
}

} // namespace

logical ApplyMidSurfaceConfigNode(const ConfigNode& root, MidSurfaceConfig& config)
{
    const ConfigNode* run_context = root.FindChild("run_context");
    if (run_context != nullptr)
        ApplyRunContext(*run_context, config.run_context);

    const ConfigNode* step1 = root.FindChild("step1");
    if (step1 != nullptr)
        ApplyStep1(*step1, config.step1);

    const ConfigNode* step2 = root.FindChild("step2");
    if (step2 != nullptr)
        ApplyStep2(*step2, config.step2);

    const ConfigNode* step3 = root.FindChild("step3");
    if (step3 != nullptr)
        ApplyStep3(*step3, config.step3);

    const ConfigNode* step4 = root.FindChild("step4");
    if (step4 != nullptr)
        ApplyStep4(*step4, config.step4);

    const ConfigNode* step5 = root.FindChild("step5");
    if (step5 != nullptr)
        ApplyStep5(*step5, config.step5);

    const ConfigNode* step6 = root.FindChild("step6");
    if (step6 != nullptr)
        ApplyStep6(*step6, config.step6);

    return TRUE;
}

logical LoadMidSurfaceConfigFromYaml(const char* yaml_path, MidSurfaceConfig& config)
{
    ConfigNode root;
    if (LoadYamlConfigFile(yaml_path, root) == FALSE)
        return FALSE;
    return ApplyMidSurfaceConfigNode(root, config);
}
} // namespace midsurface_new
