# Step6 Trim Select 工程文档

本文记录当前 `midface` Step6 的实现。源码事实源：

- `midface/steps/Step6TrimSelect.hpp`
- `midface/steps/Step6TrimSelect.cpp`
- `midface/steps/Step5MidPatchBuild.hpp`
- `midface/steps/Step7StitchInput.hpp`
- `midface/core/MidSurfaceNewTypes.hpp`
- `midface/core/MidSurfaceNewTypes.cpp`
- `midface/MidSurface.cpp`

## 目标和算法

Step6 输入 Step5 的 `Step5MidPatchState`，把 Step5 生成的中面 patch face 扩展成 seed body，用相邻中面和 wall 作为工具进行 imprint/split，然后从 split faces 中选择最终进入 Step7 的 face。

Step6 做的事情：

- 从 Step5、Step4、Step3、Step2、Step1 读取完整上游 state。
- 为每个 mid patch face 构造扩展 seed body。
- 为 Step3 wall group 和 Step4 virtual wall 构造/复制 wall tool body。
- 根据 Step4 relation 建 trim graph。
- 用 MM1 邻接 pair 和 wall tool 对 mid seed body 做 imprint。
- 收集 split 后的候选 face。
- 按几何规则选择每个 pair 的最终 face。
- 构造 `Step7StitchInput`。
- 可选构建 selected slice adjacency。
- 输出 Step6 debug SAT 和 color map。

Step6 不做的事情：

- 不真正执行 Step7 stitch。
- 当前 MM2 relation 在 trim graph 中计数后跳过，主要由 Step4 virtual wall 支持。

Step6 主流程：

```text
RunMidSurfaceBodyWithContext
  -> RunStep6TrimSelect(result.step5.state, config.step6, diagnostics, result.step6)
    -> check upstream step4/step3/step2/step1/source_body
    -> build extended mid seed bodies from Step5 patches
    -> build/copy wall seed bodies from Step3 walls and Step4 virtual walls
    -> build trim graph from Step4 pair relations and pair-wall relations
    -> group pairs by shared seed body
    -> copy seed body to blank body
    -> imprint MM1 neighbor tools / wall tools / optional source body
    -> collect split faces into Step6SliceRecord
    -> PickFacesByGeometryRule for each pair
    -> create TrimSelectionRecord
    -> BuildStep7StitchInputFromSelections
    -> optional BuildSelectedSliceAdjacency
    -> ColorSelectedFacesAndEmitMap
    -> Emit debug SAT
    -> EmitFinishEvent
```

## 参数设置

`Step6TrimSelectOptions` 定义在 `Step6TrimSelect.hpp`，默认值在 `Step6TrimSelect.cpp`。

```cpp
struct Step6TrimSelectOptions
{
    logical enable_trim = TRUE;
    logical emit_selection_events = TRUE;
    logical build_slice_adjacency = FALSE;
    int edge_match_sample_count = 8;
    double edge_match_tolerance = 1.0e-5;
    double edge_match_length_tolerance = 1.0e-5;
    double adjacency_distance_units = 10.0;
    int adjacency_sample_count = 24;
    double adjacency_min_pass_ratio = 0.60;

    logical enable_source_trim = FALSE;
    logical enable_body_prewall_extend = TRUE;
    double mid_extend_scale = 1.50;
    double mid_extend_min = 1e-4;
    double wall_extend_scale = 3.0;
    double wall_extend_min = 1e-4;
    double wall_sphere_radius_boost_ratio = 0.05;
    logical use_api_extend_fail_fallback_only = TRUE;
    logical api_extend_fail_fallback_use_uv_expand = TRUE;
    double cylinder_uv_expand_scale = 3.0;
    double sphere_uv_expand_scale = 3.0;
    double torus_uv_expand_scale = 3.0;
    int extend_fail_fallback_sample_count = 64;
    int uniform_sample_count = 24;
    double pass_ratio_required = 0.70;
    double facing_min_cos = 0.80;
    double area_min_ratio = 0.06;
    logical use_between_score_pick = TRUE;
    double between_score_min = 1.60;
    double between_area_floor_ratio = 0.0;
    int between_sample_count = 24;
    int select_sample_count = 24;
    double min_sample_success_ratio = 0.2;
    double max_norm_std = 0.28;
    double max_mean_thickness_error = 0.45;
    double min_normal_support_ratio = 0.0;
    double nonfree_relax_factor = 4.0;
    double nonfree_min_pass_ratio = 0.20;
    double freeform_relax_factor = 2.20;
    double freeform_min_pass_ratio = 0.55;
    double small_face_ratio = 0.06;
};
```

- `enable_trim`：是否启用 trim 语义。当前主流程仍执行 seed/split/select。
- `emit_selection_events`：是否输出 selection 和 Step7 input 相关日志。
- `build_slice_adjacency`：是否构建 selected slice adjacency。
- `edge_match_sample_count`：边几何匹配采样数。
- `edge_match_tolerance`：边几何匹配距离容差。
- `edge_match_length_tolerance`：边长匹配容差。
- `adjacency_distance_units`：selected slice 距离邻接判断的归一化距离阈值。实际阈值为 `adjacency_distance_units * Step1.model_scale.distance_unit`；若 Step1 scale 不可用，则回退到 `edge_match_tolerance`。
- `adjacency_sample_count`：selected slice 距离邻接判断中每条 edge 的采样目标数量；实际 edge 采样数至少取 `max(edge_match_sample_count, adjacency_sample_count)`。
- `adjacency_min_pass_ratio`：单向距离 refine 的最小通过比例。A 到 B 或 B 到 A 任一方向达到该比例即可建立距离邻接。
- `enable_source_trim`：是否用原始 source body 作为额外 trim tool。
- `enable_body_prewall_extend`：构建 seed body 时是否尝试扩展。
- `mid_extend_scale`：mid face 扩展距离比例。
- `mid_extend_min`：mid face 扩展距离下限。
- `wall_extend_scale`：wall face 扩展距离比例。
- `wall_extend_min`：wall face 扩展距离下限。
- `wall_sphere_radius_boost_ratio`：sphere wall 半径扩展辅助比例。
- `use_api_extend_fail_fallback_only`：是否仅在 API extend 失败时使用 fallback。
- `api_extend_fail_fallback_use_uv_expand`：API extend 失败 fallback 是否用 UV expand。
- `cylinder_uv_expand_scale`：cylinder UV expand 比例。
- `sphere_uv_expand_scale`：sphere UV expand 比例。
- `torus_uv_expand_scale`：torus UV expand 比例。
- `extend_fail_fallback_sample_count`：extend fallback 采样数量。
- `uniform_sample_count`：通用均匀采样数量。
- `pass_ratio_required`：选择规则中的通过比例要求。
- `facing_min_cos`：选择规则中的朝向余弦下限。
- `area_min_ratio`：面积选择规则下限比例。
- `use_between_score_pick`：是否启用 between score 选择策略。
- `between_score_min`：between score 选择阈值。
- `between_area_floor_ratio`：between score 面积下限比例。
- `between_sample_count`：between score 采样数量。
- `select_sample_count`：选择阶段采样数量。
- `min_sample_success_ratio`：最小采样成功比例。
- `max_norm_std`：法向统计标准差上限。
- `max_mean_thickness_error`：平均厚度误差上限。
- `min_normal_support_ratio`：法向支持比例下限。
- `nonfree_relax_factor`：非 freeform 放宽系数。
- `nonfree_min_pass_ratio`：非 freeform 最小通过比例。
- `freeform_relax_factor`：freeform 放宽系数。
- `freeform_min_pass_ratio`：freeform 最小通过比例。
- `small_face_ratio`：小 face 过滤比例。

## Result 和 State

### `Step6TrimSelectResult`

```cpp
struct Step6TrimSelectResult
{
    logical ok;
    Step6TrimSelectState state;
};
```

- `ok`：Step6 是否选出至少一个 face。当前 `selected_count > 0` 时为 `TRUE`。
- `state`：Step6 完整输出状态。

### `Step6TrimSelectState`

```cpp
struct Step6TrimSelectState
{
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
```

- `input_step5`：指向本次 Step6 使用的 Step5 state。
- `options_snapshot`：Step6 本次运行使用的参数快照。
- `selections`：最终选中的 split face 列表。
- `slices`：trim/split 后的候选 face 列表。
- `raw_imprint_edges`：raw imprint edge 配对表。当前主流程没有系统填充该表。
- `slice_edge_uses`：slice edge 到 imprint edge 的使用记录。当前主流程没有系统填充该表。
- `slice_adjacencies`：selected slice 之间的邻接关系。仅 `build_slice_adjacency == TRUE` 时构建。
- `step7_stitch_input`：Step6 为 Step7 准备的输入。
- `stats`：Step6 统计信息。

### `TrimSelectionTable`

```cpp
struct TrimSelectionTable
{
    std::vector<TrimSelectionRecord> selections;
};
```

- `selections`：最终选中的 face 记录。

### `TrimSelectionRecord`

```cpp
struct TrimSelectionRecord
{
    int selection_id;
    int source_patch_id;
    int source_pair_id;
    int source_seed_rep_pair;
    int source_split_index;
    FACE* source_split_face;
    FACE* selected_face;
    double area;
    std::string reason;
};
```

- `selection_id`：Step6 分配的 selection 编号。
- `source_patch_id`：该 selection 来源 patch id。
- `source_pair_id`：该 selection 来源 pair id。
- `source_seed_rep_pair`：共享 seed body 的代表 pair id。
- `source_split_index`：在该 pair 选择结果中的下标。
- `source_split_face`：blank body 上被选中的原始 split face。
- `selected_face`：复制出来的 detached face，供 Step7 使用。
- `area`：source split face 面积估计。
- `reason`：选择原因或选择规则说明。

### `Step6SliceTable`

```cpp
struct Step6SliceTable
{
    std::vector<Step6SliceRecord> slices;
};
```

- `slices`：预选 split face 列表。

### `Step6SliceRecord`

```cpp
struct Step6SliceRecord
{
    int slice_id;
    FACE* face;
    int selected_index;
    int source_patch_id;
    int source_pair_id;
    int source_seed_rep_pair;
    double area;
};
```

- `slice_id`：候选 slice 编号。
- `face`：候选 split face 的 detached copy。
- `selected_index`：预留字段。当前构造 slice 时保持默认 `-1`。
- `source_patch_id`：来源 patch id。
- `source_pair_id`：来源 pair id。
- `source_seed_rep_pair`：共享 seed body 的代表 pair id。
- `area`：source split face 面积估计。

### `Step6EdgeGeom`

```cpp
struct Step6EdgeGeom
{
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
```

- `edge`：edge 指针。
- `coedge`：coedge 指针。
- `p0`：edge 起点。
- `p1`：edge 终点。
- `mid`：edge 中点。
- `bbox`：edge bbox。
- `length`：edge 长度。
- `curve_type`：curve 类型编号。
- `valid`：edge 几何信息是否有效。

### `Step6RawImprintEdgeTable`

```cpp
struct Step6RawImprintEdgeTable
{
    std::vector<Step6RawImprintSideEdge> side_edges;
    std::vector<Step6RawImprintEdgePair> pairs;
};
```

- `side_edges`：raw imprint 单侧 edge 列表。
- `pairs`：raw imprint edge 配对列表。

### `Step6RawImprintSideEdge`

- `side_edge_id`：单侧 edge 编号。
- `raw_id`：raw imprint id。
- `side`：A/B 侧。
- `geom`：edge 几何信息。

### `Step6RawImprintEdgePair`

- `imprint_edge_id`：imprint edge pair 编号。
- `raw_a`：A 侧 raw id。
- `raw_b`：B 侧 raw id。
- `side_edge_a_id`：A 侧 edge id。
- `side_edge_b_id`：B 侧 edge id。
- `edge_a`：A 侧 edge 指针。
- `edge_b`：B 侧 edge 指针。
- `reversed`：两边方向是否反向。
- `endpoint_error`：端点误差。
- `sample_error`：采样误差。
- `length_error`：长度误差。
- `ambiguous`：是否存在歧义匹配。

### `Step6SliceEdgeUseTable`

- `uses`：slice edge 使用 imprint edge 的记录。

### `Step6SliceEdgeUse`

- `use_id`：使用记录编号。
- `slice_id`：slice id。
- `slice_edge`：slice 上的 edge。
- `imprint_edge_id`：对应 imprint edge id。
- `side`：A/B 侧。
- `coverage_ratio`：覆盖比例。

### `Step6SliceAdjacencyTable`

- `adjacencies`：selected slice 邻接记录。

### `Step6SliceAdjacency`

- `adjacency_id`：邻接记录编号。
- `slice_a`：一侧 selection id。
- `slice_b`：另一侧 selection id。
- `imprint_edge_id`：对应 imprint edge id。几何/拓扑直接匹配时可能为 `-1`。
- `shared_length`：共享长度估计。

### `Step7StitchInput`

定义在 `Step7StitchInput.hpp`。

```cpp
struct Step7StitchInput
{
    std::vector<Step7SplitRawFaceRecord> split_to_raw_faces;
    std::vector<Step7RawFaceRelationRecord> raw_face_relations;
    std::vector<Step7RawFaceRef> raw_faces;
};
```

- `split_to_raw_faces`：Step6 selected split face 到 Step5 raw patch 的映射。
- `raw_face_relations`：Step5 junction 转换出的 raw face 关系。
- `raw_faces`：Step7 需要的 raw face 上游引用信息。

### `TrimSelectStats`

```cpp
struct TrimSelectStats
{
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
```

字段含义按名字对应 Step6 阶段统计：

- `input_patch_count`：输入 patch 数。
- `input_pair_count`：输入 pair 数。
- `mid_seed_try_count` / `mid_seed_ok_count` / `mid_seed_reused_count`：mid seed body 构建尝试、成功和复用数量。
- `wall_seed_try_count` / `wall_seed_ok_count`：Step3 wall seed 构建尝试和成功数量。
- `virtual_wall_seed_count`：Step4 virtual wall body 复制成功数量。
- `virtual_wall_raw_count`：预留统计，当前主流程未明显递增。
- `mm1_bidirectional_edge_count`：MM1 双向邻接 edge 计数。
- `mm1_directional_edge_count`：MM1 方向性邻接 edge 计数。
- `mm2_skipped_count`：trim graph 中跳过的 MM2 relation 数。
- `trim_seed_group_count`：按 seed body 分组后的 trim seed 组数。
- `trim_tool_try_count` / `trim_tool_ok_count`：imprint tool 尝试和成功数量。
- `preselect_split_face_count`：预选 split face 数。
- `selected_count`：最终 selected face 数。
- `rejected_count`：预选但未选中的数量。
- `selected_copy_fail_count`：selected face detached copy 失败数。
- `selected_dedup_skip_count`：重复 selected source face 跳过数。
- `slice_count`：slice 数。
- `side_edge_count`：raw imprint side edge 数。
- `raw_imprint_edge_pair_count`：raw imprint edge pair 数。
- `ambiguous_edge_pair_count`：歧义 edge pair 数。
- `slice_edge_use_count`：slice edge use 数。
- `slice_adjacency_count`：selected slice adjacency 数。

## 函数说明

### `RunStep6TrimSelect`

Step6 外部入口。

流程：

1. 重置 `result`。
2. 保存 `&step5` 和 options 快照。
3. 从 Step5 向上解析 Step4、Step3、Step2、Step1 和 source body。
4. 构建 mid seed body，并记录 patch 到 pair 的映射。
5. 构建 wall seed body 和复制 Step4 virtual wall body。
6. 从 Step4 pair relation 构建 MM1/MM2 trim graph。
7. 从 Step4 pair-wall relation 建 wall tool bucket。
8. 按共享 seed body 对 pair 分组。
9. 对 blank seed body 执行工具 imprint。
10. 收集 split faces 到 `slices`。
11. 调用 `PickFacesByGeometryRule` 为每个 pair 选择 face。
12. 生成 `TrimSelectionRecord`。
13. 调用 `BuildStep7StitchInputFromSelections`。
14. 可选调用 `BuildSelectedSliceAdjacency`。
15. 上色 selected faces，输出 color map 和 debug SAT。
16. 设置 `result.ok = selected_count > 0`。

### `BuildSeedBodyFromFace`

从 face 构造 seed body，并按配置尝试扩展。用于 mid face 和 wall face。

### `ImprintToolSeedCopyOnBlankBody`

复制 tool seed 并对 blank body 执行 selective imprint。

### `PickFacesByGeometryRule`

从 split faces 中选择最终 face。内部会结合面积、between score、采样成功比例、法向、厚度误差等规则。

### `BuildStep7StitchInputFromSelections`

把 Step6 selections 转成 Step7 输入。

生成：

- `split_to_raw_faces`：每个 selected face 对应一个 split/raw 映射。
- `raw_face_relations`：由 Step5 junctions 转换而来。
- `raw_faces`：raw face 对应的 patch、pair、group、face 上游引用。

### `BuildSelectedSliceAdjacency`

当 `build_slice_adjacency == TRUE` 时，对 selected faces 构建邻接关系，填充 `slice_adjacencies`。

当前规则：

1. 若两个 selection 来自同一个 `source_seed_rep_pair`，并且 `source_split_face` 共享同一个 ACIS `EDGE*`，直接建立 `source = topology` 邻接。
2. 否则先做关系候选过滤。候选条件包括同 `source_seed_rep_pair`、同 `source_patch_id`、同 `source_pair_id`，或两个 `source_pair_id` 在 Step4 pair relation 中有关系。
3. 对关系候选做双向 edge 距离 refine：
   - 遍历 A 的 `source_split_face` 每条有效边，在单条 edge 上采样点，计算这些点到 B 的有限 `source_split_face` 的最近距离。
   - 遍历 B 的每条有效边，对 A 重复一次。
   - 实际阈值 `tol = adjacency_distance_units * Step1.model_scale.distance_unit`；若 Step1 scale 不可用，使用 `edge_match_tolerance`。
   - 每条 edge 单独统计 `sample_count`、`valid_count`、`pass_count`、`pass_ratio`、平均距离和归一化平均距离。
   - 每个方向保留通过或最优的 edge 作为该方向日志结果。
4. 只要 A 到 B 或 B 到 A 任一方向有任一 edge 的 `pass_ratio >= adjacency_min_pass_ratio`，就建立 `source = distance` 邻接。

该规则不区分 MM1/MM2。Step4 relation 只负责缩小候选范围，最终是否邻接由距离采样证据决定。

### `ColorSelectedFacesAndEmitMap`

为 selected faces 分配颜色并输出 color map。

## 日志输出

所有 Step6 事件基础 tag：

```text
step6
trim-select
```

### start event

tags：

```text
step6
trim-select
start
```

properties：

- `input_patch_count`
- `input_pair_count`

### invalid_input

tags：

```text
step6
trim-select
invalid_input
```

properties：

- `reason`：当前为 `missing_upstream_state`。

### extend detail event

mid face 扩展时输出：

```text
step6
trim-select
detail
extend
mid
```

properties：

- `source_patch_id`
- `area`
- `extend_distance`
- `ok`
- `detach_used`
- `extend_ok`
- `edge_count`

### stage summary

tags：

```text
step6
trim-select
summary
stage
<stage>
<optional-substage>
finish
```

当前主要 stage：

- `extend mid`：`try_count`、`ok_count`、`reused_count`。
- `extend wall`：`try_count`、`ok_count`、`virtual_wall_seed_count`。
- `trim-graph`：`mm1_bidirectional_edge_count`、`mm1_directional_edge_count`、`mm1_split_tool_edge_count`、`mm2_skipped_count`、`raw_pair_candidate_count`、`pair_wall_relation_count`。
- `split`：`trim_seed_group_count`、`trim_tool_try_count`、`trim_tool_ok_count`、`preselect_split_face_count`。
- `select`：`selected_count`、`selected_face_count`、`rejected_count`、`selected_copy_fail_count`、`selected_dedup_skip_count`。
- `adjacency`：仅开启 `build_slice_adjacency` 时输出，包含 `selected_count`、`slice_adjacency_count` 等。

### selection event

tags：

```text
step6
trim-select
summary
single
selection
```

properties：

- `selection_id`
- `source_patch_id`
- `source_pair_id`
- `source_seed_rep_pair`
- `area`
- `reason`

### Step7 input events

Step6 在构造 `step7_stitch_input` 时输出，都会额外带：

```text
step7-input
```

包括：

- split/raw face 映射事件。
- raw face relation 事件。
- raw face patch/pair/group/face 上游追踪事件。
- Step7 input stage summary。

### selected adjacency event

当 `build_slice_adjacency == TRUE` 时输出。

tags：

```text
step6
trim-select
selected-adjacency
```

properties：

- `adjacency_id`
- `selection_a`
- `selection_b`
- `imprint_edge_id`
- `shared_length`
- `source`

当 `source = distance` 时还会输出：

- `source_pair_a` / `source_pair_b`
- `source_patch_a` / `source_patch_b`
- `source_seed_rep_pair_a` / `source_seed_rep_pair_b`
- `refine_mode`：当前为 `edge`。
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
- `accepted_direction`：`a_to_b`、`b_to_a` 或 `both`。

### finish event

tags：

```text
step6
trim-select
summary
all
finish
```

properties：

- `ok`
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
- `selected_face_count`
- `rejected_count`
- `selected_copy_fail_count`
- `selected_dedup_skip_count`
- `slice_adjacency_count`

### color-map

artifact role：

```text
step6.selected_faces
```

properties：

- `selection_id`
- `selected_body_index`
- `source_patch_id`
- `source_pair_id`
- `source_seed_rep_pair`
- `area`
- `has_selected_face`

json_properties：

- `rgb`

### debug SAT

Step6 相关 role：

```text
step6.extended_mid_faces
step6.extended_wall_faces
step6.preselect_split_faces
step6.selected_faces
```

输出条件：`RunContextOptions.debug_sat_outputs` 包含对应 role。
