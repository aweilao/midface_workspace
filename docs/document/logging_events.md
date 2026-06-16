# midface_new 算法 debug 日志字典

本文档记录 `midface_new` 当前真实输出的 JSONL 日志结构。日志不是后端审计系统，只服务算法 debug 和人工查询。

每行是一个 JSON object：

```json
{"tags":["step2","grouping","refine","detail"],"properties":{"source_face":"3","target_face":"7","result":"pass"}}
```

## 总体约定

- `tags` 只做路由：表示 step、阶段、对象类型和日志粒度。
- `properties` 只放算法强相关字段，例如 face/group/pair/wall id、阈值、计数、采样证据、输出路径。
- 不写系统级字段：`schema`、`run_id`、`time`、`level`。
- 不写系统级 tag：`diagnostic`、`artifact:sat`、`result:ok`、`result:fail`。
- 大型数组或对象使用 JSON property，例如 `samples`、`rgb`、`face_ids`、`hit_delta`。
- 全局 pipeline 事件写入 `events.jsonl`。
- 每个 body 的算法、summary、debug output 事件写入 `body_<index>_events.jsonl`。
- 受 YAML 开关控制的 detail/object 日志，即使文档列出，关闭开关时也不会出现在输出里。

## 日志层级

| tag 组合 | 含义 |
|---|---|
| `detail` | 最细操作记录，例如 face-face 比较、sample gate、pair-pair hit |
| `summary` + `single` | 单个对象总结，例如一个 face、group、pair、wall、relation |
| `summary` + `stage` | 阶段总结，例如 prefilter/refine/group-role/relation 子阶段 |
| `summary` + `all` | 整个 Step 总结 |

后续 Step 新实现前，先写清楚：有哪些阶段、哪些 detail、哪些 single summary、哪些 stage summary、Step finish 输出哪些 all summary。

## 公共 Tags

| tag | 含义 |
|---|---|
| `pipeline` | 主线流程 |
| `step1` / `step2` / `step3` / `step4` | 当前已实现 Step |
| `face-analyze` | Step1 面分析 |
| `grouping` | Step2 分组 |
| `pairing` | Step3 配对 |
| `relation` | Step4 关系构建 |
| `prefilter` / `refine` | 初筛 / 精筛 |
| `candidate` | 候选记录 |
| `face` / `group` / `pair` / `wall` | 对象类型 |
| `group-role` | Step3 group 的 pair/wall 身份 |
| `group-adjacency` / `tiny-face-patch` / `pair-wall` / `orphan-bridge` / `pair-link` / `virtual-wall` | Step4 子阶段 |
| `color-map` | id 到颜色的映射，一条 id 一条日志 |
| `output` / `sat` / `result-json` | debug 输出 |
| `start` / `finish` / `record` / `ok` / `fail` | 动作或结果 |

## Pipeline

全局文件 `events.jsonl`：

| tags | properties |
|---|---|
| `pipeline` + `finish` | 当前无固定 property |

body 文件 `body_<index>_events.jsonl`：

| tags | properties |
|---|---|
| `pipeline` + `start` | `body_index` |
| `pipeline` + `result-json` + `output` + `ok/fail` | `body_index`、`path`、`completed_step` |

## Debug SAT 输出

tags：

- `output`
- `sat`
- `ok` 或 `fail`

properties：

| property | 含义 |
|---|---|
| `role` | 输出角色短名，例如 `colored_body`、`group_colored_body`、`pair_colored_body` |
| `path` | SAT 输出路径 |

当前支持的 debug SAT role：

- `step1.colored_body`
- `step2.group_colored_body`
- `step3.pair_colored_body`

## Color Map

`color-map` 是颜色查询表，一条 id 一条日志，不写 SAT 路径。SAT 路径看对应的 `output + sat` 日志。

| 阶段 | tags | properties |
|---|---|---|
| Step1 face color | `step1` + `face-analyze` + `color-map` | `face_id`、`type`、`rgb` |
| Step2 group color | `step2` + `grouping` + `color-map` | `group_id`、`type`、`face_count`、`face_ids`、`rgb` |
| Step3 pair color | `step3` + `pairing` + `color-map` | `pair_id`、`group_a`、`group_b`、`face_count`、`face_ids`、`rgb` |
| Step3 wall color | `step3` + `pairing` + `wall` + `color-map` | `wall_id`、`group_id`、`face_count`、`face_ids`、`rgb` |

## Step1 Face Analyze

| 层级 | tags | 输出条件 |
|---|---|---|
| start | `step1` + `face-analyze` + `start` | 总是输出 |
| 单 face 总结 | `step1` + `face-analyze` + `face_recorded` + `summary` + `single` | `step1.emit_face_events=true` |
| adjacency 阶段总结 | `step1` + `face-analyze` + `adjacency` + `adjacency_built` + `summary` + `stage` | `step1.build_adjacency=true` |
| model scale 阶段总结 | `step1` + `face-analyze` + `model-scale` + `summary` + `stage` | 总是输出 |
| Step1 总结 | `step1` + `face-analyze` + `finish/finish_without_faces` + `summary` + `all` | 总是输出 |
| color map | `step1` + `face-analyze` + `color-map` | `step1.colored_body` role 启用 |

单 face properties：

| property | 含义 |
|---|---|
| `face_id` | Step1 分配的 face id |
| `color_rgb` | 写入 face 的 RGB JSON 数组，格式 `[r,g,b]` |
| `face_type` | face 类型 |
| `valid` | face record 是否有效 |
| `edge_count` / `adjacency_degree` | 边数量 / 邻接数量 |
| `has_representative_point` / `has_representative_normal` / `has_area_proxy` / `has_edge_lengths` | 对应摘要是否成功 |
| `representative_point` / `representative_normal` | 代表点 / 代表法向 |
| `area_proxy` | 面积代理值 |
| `edge_min` / `edge_max` | 边长最小 / 最大值 |
| `invalid_reason` | 无效原因 |
| `sample_summary` | Step1 采样摘要 |

Step1 总结 properties：

- `face_count`
- `valid_face_count`
- `invalid_face_count`
- `adjacency_count`

model scale properties：

- `valid`
- `reference_length`
- `distance_unit`
- `normalized_reference`
- `top_edge_count_requested`
- `top_edge_count_used`
- `valid_edge_count`
- `min_valid_edge_length`
- `longest_edge_length`
- `shortest_used_edge_length`

## Step2 Grouping

| 层级 | tags | 输出条件 |
|---|---|---|
| start | `step2` + `grouping` + `start` | 总是输出 |
| face-face surface prefilter detail | `step2` + `grouping` + `prefilter` + `detail` | `step2.emit_prefilter_events=true` |
| face-face sample refine detail | `step2` + `grouping` + `refine` + `detail` | `step2.emit_refine_events=true` |
| 单 group 总结 | `step2` + `grouping` + `group` + `record` + `summary` + `single` | `step2.emit_group_events=true` |
| prefilter 阶段总结 | `step2` + `grouping` + `prefilter` + `summary` + `stage` + `finish` | 总是输出 |
| refine 阶段总结 | `step2` + `grouping` + `refine` + `summary` + `stage` + `finish` | 总是输出 |
| group build 阶段总结 | `step2` + `grouping` + `group` + `summary` + `stage` + `finish` | 总是输出 |
| Step2 总结 | `step2` + `grouping` + `finish` + `summary` + `all` | 总是输出 |
| color map | `step2` + `grouping` + `color-map` | `step2.group_colored_body` role 启用 |

prefilter detail properties：

- `face_a`
- `face_b`
- `face_a_type`
- `face_b_type`
- `result`
- `match_type`
- `normal_angle_deg`
- `plane_distance`
- `radius_delta`
- `matched_properties`
- `reason`

refine detail properties：

- `source_face`
- `target_face`
- `result`
- `sample_count`
- `pass_count`
- `local_dist_gate`
- `samples`

group summary properties：

- `group_id`
- `face_count`
- `face_ids`
- `type`
- `area_sum`
- `local_scale`
- `confidence`
- `source_rules`

Step2 run/finish properties：

- `valid_face_count`
- `candidate_count`
- `accepted_count`
- `rejected_count`
- `group_count`
- `single_face_group_count`
- `surface_prefilter_pass_count`
- `surface_prefilter_reject_count`
- `sample_refine_pass_count`
- `sample_refine_reject_count`
- `reject_reason_stats`

## Step3 Pairing

| 层级 | tags | 输出条件 |
|---|---|---|
| start | `step3` + `pairing` + `start` | 总是输出 |
| candidate detail | `step3` + `pairing` + `candidate` + `coarse_normal/surface` + `detail` | `step3.emit_candidate_events=true` |
| refine detail | `step3` + `pairing` + `refine` + `detail` | `step3.emit_refine_events=true` |
| thickness filter detail | `step3` + `pairing` + `thickness-filter` + `detail` | 有 pair 被每组厚度倍数后筛丢弃时输出 |
| 单 pair 总结 | `step3` + `pairing` + `pair` + `record` + `summary` + `single` | `step3.emit_pair_events=true` |
| 单 wall 总结 | `step3` + `pairing` + `wall` + `summary` + `single` | `step3.emit_pair_events=true` |
| 单 group role 总结 | `step3` + `pairing` + `group-role` + `summary` + `single` | `step3.emit_pair_events=true` |
| candidate 阶段总结 | `step3` + `pairing` + `candidate` + `summary` + `stage` + `finish` | 总是输出 |
| refine 阶段总结 | `step3` + `pairing` + `refine` + `summary` + `stage` + `finish` | 总是输出 |
| group-role 阶段总结 | `step3` + `pairing` + `group-role` + `summary` + `stage` + `finish` | 总是输出 |
| Step3 总结 | `step3` + `pairing` + `finish` + `summary` + `all` | 总是输出 |
| pair color map | `step3` + `pairing` + `color-map` | `step3.pair_colored_body` role 启用 |
| wall color map | `step3` + `pairing` + `wall` + `color-map` | `step3.pair_colored_body` role 启用 |

candidate detail properties：

- `candidate_id`
- `group_a`
- `group_b`
- `result`
- `source`
- `reason`

refine detail properties：

- `candidate_id`
- `group_a`
- `group_b`
- `result`
- `reason`
- `thickness`
- `score`
- `coverage`
- `pass_ab`
- `total_ab`
- `pass_ba`
- `total_ba`
- `gate_pass`
- `facing_score`
- `width_a`
- `width_b`
- `width_min`
- `adaptive_max`
- `adaptive_relax`
- `samples`

thickness filter detail properties：

- `result`
- `reason`
- `pair_id_before_filter`
- `group_a`
- `group_b`
- `thickness`
- `ratio_max`
- `group_a_pair_count`
- `group_b_pair_count`
- `group_a_min_thickness`
- `group_b_min_thickness`
- `group_a_limit`
- `group_b_limit`
- `drop_by_group_a`
- `drop_by_group_b`

pair summary properties：

- `pair_id`
- `group_a`
- `group_b`
- `thickness`
- `coverage`
- `score`
- `facing_score`
- `pass_ratio`
- `pass_balance`
- `uncertain`
- `rib_candidate`
- `reason`

wall summary properties：

- `wall_id`
- `group_id`
- `face_count`
- `source`
- `face_ids`

group-role properties：

- `group_id`
- `role`
- `pair_count`
- `pair_ids`
- `wall_id`

Step3 run/finish properties：

- `candidate_count`
- `accepted_count`
- `rejected_count`
- `pair_count`
- `wall_count`
- `pair_group_count`
- `wall_group_count`
- `coarse_prefilter_pass_count`
- `coarse_prefilter_reject_count`
- `surface_prefilter_pass_count`
- `surface_prefilter_reject_count`
- `refine_pass_count`
- `refine_reject_count`
- `thickness_filter_dropped_count`
- `rib_candidate_count`
- `uncertain_count`

## Step4 Relation

Step4 记录 group 邻接、tiny face patch、MW1 pair-wall、orphan bridge、pair-pair MM1/MM2 和 MM2 virtual wall。

| 层级 | tags | 输出条件 |
|---|---|---|
| start | `step4` + `relation` + `start` | 总是输出 |
| group adjacency hit detail | `step4` + `relation` + `detail` + `group-adjacency` | `step4.emit_relation_events=true` |
| tiny face patch detail | `step4` + `relation` + `detail` + `tiny-face-patch` | `step4.emit_relation_events=true` 且有触发 |
| pair-link hit detail | `step4` + `relation` + `detail` + `pair-link` | `step4.emit_relation_events=true` |
| orphan bridge detail | `step4` + `relation` + `detail` + `orphan-bridge` | `step4.emit_relation_events=true` 且有触发 |
| virtual-wall fail detail | `step4` + `relation` + `detail` + `virtual-wall` + `fail` | `step4.emit_relation_events=true` 且有失败 |
| pair-wall single summary | `step4` + `relation` + `summary` + `single` + `pair-wall` | `step4.emit_relation_events=true` |
| pair-link single summary | `step4` + `relation` + `summary` + `single` + `pair-link` | `step4.emit_relation_events=true` |
| virtual-wall ok single summary | `step4` + `relation` + `summary` + `single` + `virtual-wall` + `ok` | `step4.emit_relation_events=true` 且生成成功 |
| group adjacency stage summary | `step4` + `relation` + `summary` + `stage` + `group-adjacency` + `finish` | `step4.emit_relation_events=true` |
| pair-wall stage summary | `step4` + `relation` + `summary` + `stage` + `pair-wall` + `finish` | `step4.emit_relation_events=true` |
| orphan bridge stage summary | `step4` + `relation` + `summary` + `stage` + `orphan-bridge` + `finish` | `step4.emit_relation_events=true` |
| pair-link stage summary | `step4` + `relation` + `summary` + `stage` + `pair-link` + `finish` | `step4.emit_relation_events=true` |
| virtual-wall stage summary | `step4` + `relation` + `summary` + `stage` + `virtual-wall` + `finish` | `step4.emit_relation_events=true` |
| Step4 总结 | `step4` + `relation` + `summary` + `all` + `finish` | 总是输出 |

group adjacency detail properties：

- `group_a`
- `group_b`
- `source`
- `ring_size`
- `hit_delta`

group adjacency stage properties：

- `group_adjacency_count`
- `coedge_total`
- `coedge_with_partner`
- `coedge_partner_ring_gt2`
- `unique_nonmanifold_edges`
- `partner_ring_max`
- `tiny_faces_found`
- `tiny_partner_coedges`
- `tiny_patch_applied`

pair-wall single properties：

- `relation_id`
- `pair_id`
- `wall_group_id`
- `hit_group_a`
- `hit_group_b`
- `wall_mode`

pair-wall stage properties：

- `checked_count`
- `accepted_count`
- `rejected_count`
- `wall_group_count`

orphan bridge detail properties：

- `wall_group_id`
- `wall_face_index`
- `pair_a`
- `pair_b`
- `side_a`
- `side_b`
- `edge_sum`
- `mode`
- `hit_delta`

orphan bridge stage properties：

- `orphan_wall_group_count`
- `wall_faces_total`
- `face_dedup_skip`
- `injected_link_count`

pair-link detail properties：

- `pair_a`
- `pair_b`
- `group_a`
- `group_b`
- `side_a`
- `side_b`
- `hit_delta`
- `hits_aa`
- `hits_ab`
- `hits_ba`
- `hits_bb`
- `source`

pair-link single properties：

- `relation_id`
- `pair_a`
- `pair_b`
- `pair_mode`
- `classify_source`
- `source`
- `hits_aa`
- `hits_ab`
- `hits_ba`
- `hits_bb`
- `total_hits`
- `hit_normal_cos_abs`
- `hit_normal_angle_deg`

pair-link stage properties：

- `candidate_link_count`
- `accepted_link_count`
- `rejected_link_count`
- `direct_hit_count`
- `orphan_hit_count`
- `mm1_count`
- `mm2_count`

virtual-wall single/detail properties：

- `wall_id`
- `pair_a`
- `pair_b`
- `group_a`
- `group_b`
- `bucket_slot`
- `source`
- `coedge_len`
- `half_profile`
- `hit_normal_cos_abs`

virtual-wall stage properties：

- `try_count`
- `ok_count`
- `fail_count`

Step4 finish properties：

- `relation_count`
- `pair_wall_relation_count`
- `wall_count`
- `group_adjacency_count`
- `tiny_faces_found`
- `tiny_patch_applied`
- `orphan_wall_group_count`
- `orphan_bridge_injected_count`
- `direct_pair_link_count`
- `mm1_count`
- `mm2_count`
- `virtual_wall_try_count`
- `virtual_wall_ok_count`

## Step5 Mid Patch

Step5 记录 old Step3 中面生成逻辑在 `midface_new` 中的结果：pair component、1:n patch unit、junction、raw midface geometry。

| 层级 | tags | 输出条件 |
|---|---|---|
| component stage summary | `step5` + `mid-patch` + `summary` + `stage` + `component` + `finish` | `step5.emit_patch_events=true` |
| patch unit stage summary | `step5` + `mid-patch` + `summary` + `stage` + `patch-unit` + `finish` | `step5.emit_patch_events=true` |
| junction stage summary | `step5` + `mid-patch` + `summary` + `stage` + `junction` + `finish` | `step5.emit_patch_events=true` |
| geometry stage summary | `step5` + `mid-patch` + `summary` + `stage` + `geometry` + `finish` | `step5.emit_patch_events=true` |
| patch single summary | `step5` + `mid-patch` + `summary` + `single` + `patch` + `ok/fail` | `step5.emit_patch_events=true` |
| junction single summary | `step5` + `mid-patch` + `summary` + `single` + `junction` | `step5.emit_patch_events=true` |
| Step5 summary | `step5` + `mid-patch` + `summary` + `all` + `finish` | 总是输出 |

patch single properties：

- `patch_id`
- `source_pair_id`
- `group_a`
- `group_b`
- `anchor_group`
- `component_id`
- `merged_unit`
- `member_pairs`
- `kind`
- `type_a`
- `type_b`
- `side_type_a`
- `side_type_b`
- `valid`
- `has_face`
- `build_method`
- `fail_reason`
- `thickness`
- `point_mid`
- `normal_mid`

junction single properties：

- `junction_id`
- `pair_i`
- `pair_j`
- `link_type`
- `link_type_id`
- `conn_class`
- `conn_class_id`
- `pair_mode`
- `pair_mode_id`
- `total_hits`

Step5 summary properties：

- `input_pair_count`
- `component_count`
- `requested_patch_count`
- `built_patch_count`
- `failed_patch_count`
- `junction_count`
- `merged_unit_count`
- `merged_pair_count`

Debug SAT role：

- `step5.mid_patch_faces`

## Step6 Trim Select

Step6 记录 old Step4 extend / trim / split / select 搬运后的中面裁剪选片流程。

| 层级 | tags | 输出条件 |
|---|---|---|
| start | `step6` + `trim-select` + `start` | 总是输出 |
| mid seed extend detail | `step6` + `trim-select` + `detail` + `extend` + `mid` | `step6.emit_selection_events=true` |
| wall seed extend detail | `step6` + `trim-select` + `detail` + `extend` + `wall` | `step6.emit_selection_events=true` |
| trim tool detail | `step6` + `trim-select` + `detail` + `trim-tool` | `step6.emit_selection_events=true` |
| geometry select detail | `step6` + `trim-select` + `detail` + `select` + `geometry` | `step6.emit_selection_events=true` |
| selected face single summary | `step6` + `trim-select` + `summary` + `single` + `selection` | `step6.emit_selection_events=true` |
| selected face color map | `step6` + `trim-select` + `selection` + `color-map` | debug SAT role `step6.selected_faces` 开启 |
| selected adjacency single summary | `step6` + `trim-select` + `summary` + `single` + `adjacency` + `selected-adjacency` | `step6.build_slice_adjacency=true` |
| mid extend stage summary | `step6` + `trim-select` + `summary` + `stage` + `extend` + `mid` + `finish` | 总是输出 |
| wall extend stage summary | `step6` + `trim-select` + `summary` + `stage` + `extend` + `wall` + `finish` | 总是输出 |
| trim graph stage summary | `step6` + `trim-select` + `summary` + `stage` + `trim-graph` + `finish` | 总是输出 |
| trim/split stage summary | `step6` + `trim-select` + `summary` + `stage` + `split` + `finish` | 总是输出 |
| select stage summary | `step6` + `trim-select` + `summary` + `stage` + `select` + `finish` | 总是输出 |
| selected adjacency stage summary | `step6` + `trim-select` + `summary` + `stage` + `adjacency` + `finish` | `step6.build_slice_adjacency=true` |
| Step6 summary | `step6` + `trim-select` + `summary` + `all` + `finish` | 总是输出 |

Step6 finish/stat properties：

- `input_patch_count`
- `input_pair_count`
- `mid_seed_try_count`
- `mid_seed_ok_count`
- `wall_seed_try_count`
- `wall_seed_ok_count`
- `virtual_wall_seed_count`
- `mm1_bidirectional_edge_count`
- `mm1_directional_edge_count`
- `mm2_skipped_count`
- `trim_tool_try_count`
- `trim_tool_ok_count`
- `preselect_split_face_count`
- `selected_count`
- `rejected_count`
- `selected_copy_fail_count`
- `selected_dedup_skip_count`

Step6 trim graph stage summary properties：

- `mm1_bidirectional_edge_count`：按 Step4 side hit 判为双向 MM1 的有向统计边数。
- `mm1_directional_edge_count`：按 Step4 side hit 判为偏置/单向 T 型 MM1 的关系数。
- `mm1_split_tool_edge_count`：Step6 实际用于 split imprint 的 MM1 双向 tool 边数；每条 MM1 relation 计 2。
- `mm2_skipped_count`
- `raw_pair_candidate_count`
- `pair_wall_relation_count`

Debug SAT role：

- `step6.preselect_split_faces`
- `step6.selected_faces`
- `step6.extended_mid_faces`
- `step6.extended_wall_faces`

`step6.selected_faces` 会按 selected `selection_id` 染色，并在 color-map 事件中记录：

- `selection_id`
- `selected_body_index`
- `source_patch_id`
- `source_pair_id`
- `source_seed_rep_pair`
- `source_split_index`
- `area`
- `reason`
- `rgb`

selected adjacency single summary properties：

- `adjacency_id`
- `a`
- `b`
- `selection_a`
- `selection_b`
- `imprint_edge_id`
- `shared_length`
- `source`：`topology` 或 `distance`

当 `source=distance` 时还会输出：

- `source_pair_a` / `source_pair_b`
- `source_patch_a` / `source_patch_b`
- `source_seed_rep_pair_a` / `source_seed_rep_pair_b`
- `refine_mode`：当前为 `edge`，表示按单条 edge 采样到有限 target face 的距离。
- `scale`
- `tolerance_units`
- `tolerance`
- `min_pass_ratio`
- `a_to_b_edge_count` / `b_to_a_edge_count`
- `a_to_b_checked_edge_count` / `b_to_a_checked_edge_count`
- `a_to_b_best_edge_index` / `b_to_a_best_edge_index`
- `a_to_b_best_edge_length` / `b_to_a_best_edge_length`
- `a_to_b_sample_count` / `b_to_a_sample_count`
- `a_to_b_valid_count` / `b_to_a_valid_count`
- `a_to_b_pass_count` / `b_to_a_pass_count`
- `a_to_b_pass_ratio` / `b_to_a_pass_ratio`
- `a_to_b_mean_distance` / `b_to_a_mean_distance`
- `a_to_b_mean_distance_units` / `b_to_a_mean_distance_units`
- `a_to_b_mean_pass_distance` / `b_to_a_mean_pass_distance`
- `a_to_b_mean_pass_distance_units` / `b_to_a_mean_pass_distance_units`
- `a_to_b_max_pass_distance` / `b_to_a_max_pass_distance`
- `accepted_direction`

selected adjacency stage summary properties：

- `selected_count`
- `raw_pair_candidate_count`
- `raw_prefilter_pair_count`
- `topology_hit_count`
- `distance_checked_pair_count`
- `distance_hit_count`
- `distance_one_way_hit_count`
- `distance_both_way_hit_count`
- `adjacency_scale`
- `adjacency_tolerance_units`
- `adjacency_tolerance`
- `adjacency_min_pass_ratio`
- `adjacency_sample_count`
- `slice_adjacency_count`

Step7 input single summary tags：

- `step6` + `trim-select` + `summary` + `single` + `step7-input` + `split-raw-face`
- `step6` + `trim-select` + `summary` + `single` + `step7-input` + `raw-face-relation`
- `step6` + `trim-select` + `summary` + `single` + `step7-input` + `raw-face-patch`
- `step6` + `trim-select` + `summary` + `single` + `step7-input` + `raw-face-pair`
- `step6` + `trim-select` + `summary` + `single` + `step7-input` + `raw-face-group`
- `step6` + `trim-select` + `summary` + `single` + `step7-input` + `raw-face-group-face`

`split-raw-face` properties：

- `split_id`
- `raw_face_id`
- `has_split_face`：只表示 `FACE*` 是否为空，不输出指针值

`raw-face-relation` properties：

- `relation_id`
- `raw_face_a`
- `raw_face_b`

`raw-face-patch` properties：

- `raw_face_id`
- `patch_id`
- `source_pair_id`
- `member_pairs`
- `group_a`
- `group_b`
- `component_id`
- `merged_unit`
- `kind_id`
- `type_a`
- `type_b`
- `valid`
- `has_raw_mid_face`
- `raw_mid_face_ptr`

`raw-face-pair` properties：

- `raw_face_id`
- `pair_id`
- `group_a`
- `group_b`
- `thickness`
- `coverage`
- `score`
- `facing_score`
- `pass_ratio`
- `pass_balance`
- `uncertain`
- `rib_candidate`
- `reason`

`raw-face-group` properties：

- `raw_face_id`
- `pair_id`
- `pair_side`：`a` 或 `b`
- `group_id`
- `group_type`
- `face_count`
- `face_ids`
- `area_sum`
- `local_scale`
- `confidence`
- `source_rules`

`raw-face-group-face` properties：

- `raw_face_id`
- `pair_id`
- `pair_side`：`a` 或 `b`
- `group_id`
- `face_index`
- `face_id`
- `has_face`
- `face_ptr`：Step2 group 内对应 `FACE*` 的地址字符串，用于人工对照 ACIS 实体，不写入 Step7 输入结构

Step7 input stage summary tags：

- `step6` + `trim-select` + `summary` + `stage` + `step7-input` + `finish`

Step7 input stage summary properties：

- `split_to_raw_face_count`
- `raw_face_relation_count`
- `raw_face_count`

## Step7

Step7 当前是纯 `RunStep7Stitch` 直通函数，不接收 `DiagnosticSink*`，不输出 Step7 专属结构化日志或 debug SAT。Step7 数据通过 pipeline result JSON 的 `step7.raw_relation_input` 查看；Step6 封装 Step7 输入时会按 `step7-input` tag 输出两张输入表，并额外展开每个 raw face 对应的 Step5 patch、Step3 pair、Step2 group 和 group face 指针。接口数据本身也包含精简后的 `raw_faces` 链路：`raw_face -> pair -> group_a/group_b -> faces[]`。

## 查询建议

- 看全局流程：筛 `pipeline`。
- 看某个 body 的 Step 总结：筛 `summary` + `all`。
- 看阶段总结：筛 `summary` + `stage`。
- 看单对象总结：筛 `summary` + `single`。
- 看 Step2 精筛明细：筛 `step2` + `grouping` + `refine` + `detail`。
- 看 Step3 某个 pair：筛 `step3` + `pairing` + `pair` + `summary` + `single`。
- 看 Step3 group 是 pair 还是 wall：筛 `step3` + `pairing` + `group-role`。
- 看 Step4 pair-pair 关系：筛 `step4` + `relation` + `pair-link`。
- 看颜色映射：筛 `color-map`，再按 `face_id/group_id/pair_id/wall_id` 查。
- 看 SAT 输出路径：筛 `output` + `sat`。
