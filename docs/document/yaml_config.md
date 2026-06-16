# midface_new YAML 配置入口

`midface_new` 支持用一个 YAML 文件启动主线。目标是把输入 SAT、输出目录、停止 step、debug SAT role 和各 Step options 都放到配置文件里，运行时只指定配置路径。

## 运行方式

```bash
build/midface_new_run/run_midface_new midface_new/configs/cangduan_step3.yaml
```

旧的四参数临时 runner 入口仍保留：

```bash
build/midface_new_run/run_midface_new input.sat output_dir sat_pipeline 3
```

## 设计原则

- YAML 只覆盖写出的字段，未写字段继续使用 C++ Options 构造函数默认值。
- 未识别字段保留在 `ConfigNode` 原始树中，不报错。
- 每个 Step 只读取自己的 section。
- YAML 解析由包内 vendored `mini-yaml` 完成，再转换成 `ConfigNode`；后续配置覆盖仍然是宽松读取。

## YAML parser

当前使用包内第三方副本：

- `midface_new/third_party/mini-yaml/yaml/Yaml.hpp`
- `midface_new/third_party/mini-yaml/yaml/Yaml.cpp`
- `midface_new/third_party/mini-yaml/LICENSE`

来源库是 MIT License。外层 `mini-yaml/` 目录只作为本地临时 checkout，不作为 `midface_new` 实现依赖。

mini-yaml 支持当前配置需要的能力：

- `key: value`
- section：
  ```yaml
  step2:
    emit_group_events: true
  ```
- string/int/double/bool
- list：
  ```yaml
  debug_sat_outputs:
    - step1.colored_body
    - step2.group_colored_body
  ```
- `#` 注释

不作为当前目标或不建议依赖：

- anchor / alias
- inline object/list
- 复杂 YAML 语义

## 文件和入口

主要文件：

- `midface_new/config/ConfigNode.*`
- `midface_new/config/YamlConfigLoader.*`
- `midface_new/config/MidSurfaceConfigLoader.*`
- `midface_new/configs/cangduan_step3.yaml`
- `midface_new/third_party/mini-yaml/yaml/Yaml.*`

对外入口：

```cpp
logical LoadMidSurfaceConfigFromYaml(
    const char* yaml_path,
    MidSurfaceConfig& config);
```

调用流程：

```text
YAML file
  -> LoadYamlConfigFile
  -> mini-yaml Yaml::Node
  -> ConfigNode raw tree
  -> ApplyMidSurfaceConfigNode
  -> MidSurfaceConfig
  -> RunMidSurface(config)
```

## 当前可读 section

### `run_context`

常用字段：

- `run_id`
- `workspace_root`
- `start_method`
- `input_sat_path`
- `output_root`
- `output_sat_dir`
- `output_log_dir`
- `checkpoint_root`
- `stop_after_step`
- `debug_sat_outputs`
- `checkpoint_outputs`
- `resume_from_checkpoint`
- `enable_diagnostics`
- `append_diagnostics`
- `enable_checkpoints`

`trace` 子 section：

- `enabled`
- `face_ids`
- `group_ids`
- `pair_ids`
- `tags`

### `step1`

- `build_adjacency`
- `build_sample_summary`
- `emit_face_events`

### `step2`

- `enable_surface_prefilter`
- `enable_sample_refine`
- `emit_prefilter_events`
- `emit_refine_events`
- `emit_group_events`
- `emit_group_artifacts`
- `emit_traced_pair_samples`
- `normal_angle_deg`
- `axis_angle_deg`
- `plane_tol`
- `radius_tol`
- `auto_tolerance`
- `sample_density`
- `sample_min_count`
- `sample_max_count`
- `edge_min_samples_per_edge`
- `edge_sample_alpha`
- `max_samples_per_face`
- `edge_length_eps`
- `auto_edge_length_eps`
- `local_nearest_count`
- `local_min_pass_count`
- `local_distance_gate_scale`
- `local_normal_component_max`

### `step3`

- `enable_coarse_normal_prefilter`
- `enable_surface_prefilter`
- `enable_pair_refine`
- `allow_multi_pair_per_group`
- `emit_candidate_events`
- `emit_refine_events`
- `emit_pair_events`
- `pair_opposite_angle_deg`
- `pair_direction_angle_deg`
- `pair_min_pass_count`
- `pair_min_pass_ratio`
- `pair_min_small_coverage`
- `pair_same_type_only`
- `pair_interior_sample_density`
- `pair_interior_sample_min`
- `pair_interior_sample_max`
- `pair_distance_quantile`
- `pair_thickness_min`
- `pair_thickness_max`
- `pair_adaptive_dist_ratio`
- `pair_enable_facing_dist_relax`
- `pair_facing_relax_start`
- `pair_facing_dist_relax_max`
- `pair_facing_dist_relax_power`
- `pair_thinness_max`
- `pair_min_score`
- `pair_max_per_group`
- `pair_group_thickness_ratio_max`
- `rib_area_ratio`

### `step4` 到 `step5`

当前骨架字段也可以覆盖；后续实现时继续在 `MidSurfaceConfigLoader` 对应 `ApplyStepN` 中补字段。

### `step6`

- `enable_trim`
- `emit_selection_events`
- `build_slice_adjacency`
- `edge_match_sample_count`
- `edge_match_tolerance`
- `edge_match_length_tolerance`
- `adjacency_distance_units`
- `adjacency_sample_count`
- `adjacency_min_pass_ratio`
- `enable_source_trim`
- `enable_body_prewall_extend`
- `mid_extend_scale`
- `mid_extend_min`
- `wall_extend_scale`
- `wall_extend_min`
- `wall_sphere_radius_boost_ratio`
- `use_api_extend_fail_fallback_only`
- `api_extend_fail_fallback_use_uv_expand`
- `cylinder_uv_expand_scale`
- `sphere_uv_expand_scale`
- `torus_uv_expand_scale`
- `extend_fail_fallback_sample_count`
- `uniform_sample_count`
- `pass_ratio_required`
- `facing_min_cos`
- `area_min_ratio`
- `use_between_score_pick`
- `between_score_min`
- `between_area_floor_ratio`
- `between_sample_count`
- `select_sample_count`
- `min_sample_success_ratio`
- `max_norm_std`
- `max_mean_thickness_error`
- `min_normal_support_ratio`
- `nonfree_relax_factor`
- `nonfree_min_pass_ratio`
- `freeform_relax_factor`
- `freeform_min_pass_ratio`
- `small_face_ratio`

### `step7`

当前骨架字段也可以覆盖；后续实现时继续在 `MidSurfaceConfigLoader` 对应 `ApplyStepN` 中补字段。

## 示例

```yaml
run_context:
  # run_id:
  start_method: sat_pipeline
  input_sat_path: test/sat/cangduan.sat
  output_root: run_midface_new_cangduan_yaml
  checkpoint_root: run_midface_new_cangduan_yaml/checkpoints
  stop_after_step: 3
  enable_diagnostics: true
  enable_checkpoints: true
  debug_sat_outputs:
    - step1.colored_body
    - step2.group_colored_body
    - step3.pair_colored_body
  checkpoint_outputs:
    - step1.result

step2:
  emit_refine_events: true

step3:
  emit_pair_events: true
```

空字符串字段建议直接注释掉，保持“没写就用默认值”的语义。
