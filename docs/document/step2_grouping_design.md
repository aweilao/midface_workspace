# Step2 Grouping 工程文档

本文记录当前 `midface` Step2 的实现。源码事实源：

- `midface/steps/Step2GroupBuild.hpp`
- `midface/steps/Step2GroupBuild.cpp`
- `midface/steps/Step1FaceAnalyze.hpp`
- `midface/core/MidSurfaceNewTypes.hpp`
- `midface/core/MidSurfaceNewTypes.cpp`
- `midface/utils/SamplingUtils.hpp`
- `midface/MidSurface.cpp`

## 目标和算法

Step2 输入 Step1 的 `Step1FaceAnalyzeState`，把有效 face 合并成几何语义相近的 group，输出 `Step2GroupState`。Step3 会基于这些 group 枚举厚度方向 pair。

Step2 做的事情：

- 从 Step1 的 `faces.records` 中收集有效 face。
- 枚举所有有效 face 两两组合，形成 group merge candidate。
- 用底层 surface 信息做初筛。
- 对初筛通过的 face pair 做边界采样精筛。
- 接受的 pair 用 union-find 合并。
- 从连通分量生成 `GroupRecord`。
- 建立 `face_id -> group_id` 映射。
- 按 group 重新染色，便于输出 `step2.group_colored_body` 调试 SAT。

Step2 不做的事情：

- 不生成中面。
- 不判断厚度方向 pair。
- 不要求两个 face 拓扑相邻。
- 不依赖输入 SAT 原始颜色做算法判断。

Step2 的主流程：

```text
RunMidSurfaceBodyWithContext
  -> RunStep2GroupBuild(result.step1.state, config.step2, diagnostics, result.step2)
    -> CollectValidFaces
    -> EmitRunEvent(start)
    -> ResolveTolerances
    -> enumerate all valid face pairs
      -> MakeCandidate
      -> EvaluateSurfacePrefilter
      -> EmitPrefilterEvent
      -> AddDecision(reject) or continue
      -> EvaluatePairBySamples
        -> ChooseSourceTarget
        -> BuildSourceSamples
        -> EvaluateOneHit for each sample
      -> EmitRefineEvent
      -> AddDecision
      -> UnionFind.Unite accepted pair
    -> BuildGroupsFromUnionFind
      -> BuildOneGroupRecord
      -> ApplyFaceColor
    -> EmitAllGroupEvents
    -> EmitStep2ColorMapEvents
    -> EmitBodySatIfEnabled("step2.group_colored_body")
    -> EmitStageSummaryEvents
    -> EmitRunEvent(finish)
```

## 参数设置

`Step2GroupOptions` 定义在 `Step2GroupBuild.hpp`，默认值在 `Step2GroupBuild.cpp`。

```cpp
struct Step2GroupOptions
{
    logical enable_surface_prefilter = TRUE;
    logical enable_sample_refine = TRUE;
    logical emit_prefilter_events = TRUE;
    logical emit_refine_events = TRUE;
    logical emit_group_events = TRUE;
    logical emit_group_artifacts = FALSE;
    logical emit_traced_pair_samples = TRUE;

    double normal_angle_deg = 15.0;
    double axis_angle_deg = 15.0;
    double plane_tol = -1.0;
    double radius_tol = -1.0;
    logical auto_tolerance = TRUE;

    double sample_density = 2.0;
    int sample_min_count = 9;
    int sample_max_count = 49;
    int edge_min_samples_per_edge = 8;
    double edge_sample_alpha = 0.5;
    int max_samples_per_face = 64;
    double edge_length_eps = -1.0;
    logical auto_edge_length_eps = TRUE;

    int local_nearest_count = 6;
    int local_min_pass_count = 2;
    double local_distance_gate_scale = 1.2;
    double local_normal_component_max = 0.1;
};
```

- `enable_surface_prefilter`：是否启用 surface 初筛。关闭后所有 candidate 直接通过初筛。
- `enable_sample_refine`：是否启用采样精筛。关闭后初筛通过的 candidate 直接接受。
- `emit_prefilter_events`：是否输出每个 candidate 的初筛日志。
- `emit_refine_events`：是否输出每个进入精筛 candidate 的 refine 日志。
- `emit_group_events`：是否输出最终 group 记录日志。
- `emit_group_artifacts`：预留字段。当前代码没有基于该字段输出额外 group artifact。
- `emit_traced_pair_samples`：预留字段。当前 refine sample 直接写入 refine event 的 JSON property。
- `normal_angle_deg`：plane 初筛和 sample normal gate 使用的法向角度阈值。
- `axis_angle_deg`：axis 比较阈值。当前 `ResolvedTolerances` 会计算 `axis_cos`，但 Step2 初筛尚未实际使用它。
- `plane_tol`：plane 法向距离阈值。小于等于 0 时按 body bbox 对角线自动推导。
- `radius_tol`：cylinder/sphere 半径差阈值。小于等于 0 时按 body bbox 对角线自动推导。
- `auto_tolerance`：自动容差语义字段。当前实际逻辑由 `plane_tol`、`radius_tol`、`edge_length_eps` 是否大于 0 决定。
- `sample_density`：边界采样目标密度，传给 `BoundarySampleOptions.target.density`。
- `sample_min_count`：边界采样目标最小点数。
- `sample_max_count`：边界采样目标最大点数。
- `edge_min_samples_per_edge`：每条 edge 的基础采样数。
- `edge_sample_alpha`：额外采样预算比例。
- `max_samples_per_face`：单个 source face 的最大采样点数。
- `edge_length_eps`：退化 edge 过滤阈值。小于等于 0 时按 body bbox 对角线自动推导。
- `auto_edge_length_eps`：自动 edge 长度阈值语义字段。当前实际逻辑由 `edge_length_eps` 是否大于 0 决定。
- `local_nearest_count`：精筛时只统计距离最近的 K 个 hit。
- `local_min_pass_count`：最近 K 个 hit 中至少需要通过的数量。
- `local_distance_gate_scale`：局部距离阈值放大系数。
- `local_normal_component_max`：tangent gate 中 source-to-target 向量在两侧法向上的最大归一化分量。

## Result 和 State

### `Step2GroupResult`

```cpp
struct Step2GroupResult
{
    logical ok;
    Step2GroupState state;
};
```

- `ok`：Step2 是否完成。当前实现中，只要存在有效 face 并完成 group 构建，就为 `TRUE`。
- `state`：Step2 产出的完整状态，后续 Step3 会读取其中的 group、decision、mapping 和统计信息。

### `Step2GroupState`

```cpp
struct Step2GroupState
{
    const Step1FaceAnalyzeState* input_step1;
    Step2GroupOptions options_snapshot;
    GroupCandidateTable candidates;
    GroupDecisionTable decisions;
    GroupTable groups;
    FaceToGroupMap face_to_group;
    std::map<std::string, int> reject_reason_stats;
    GroupBuildStats stats;
};
```

- `input_step1`：指向本次 Step2 使用的 Step1 state。Step2 会从这里读取 `faces.records` 和 `input_body`。
- `options_snapshot`：Step2 本次运行使用的参数快照。
- `candidates`：所有枚举出来的 face pair candidate。
- `decisions`：每个 candidate 的接受或拒绝记录。
- `groups`：Step2 最终生成的 group 列表。
- `face_to_group`：从 Step1 `face_id` 到 Step2 `group_id` 的映射。
- `reject_reason_stats`：按拒绝原因统计 candidate 数量。
- `stats`：Step2 统计信息。

### `GroupCandidateTable`

```cpp
struct GroupCandidateTable
{
    std::vector<GroupMergeCandidate> candidates;
};
```

- `candidates`：Step2 枚举的所有有效 face pair。当前枚举策略是所有有效 face 两两组合。

### `GroupMergeCandidate`

定义在 `MidSurfaceNewTypes.hpp`。

```cpp
struct GroupMergeCandidate
{
    int candidate_id;
    int face_a;
    int face_b;
    std::string source;
};
```

- `candidate_id`：Step2 内部递增编号。
- `face_a`：candidate 中较小的 face id。
- `face_b`：candidate 中较大的 face id。
- `source`：candidate 来源。当前固定为 `all_valid_pair`。

### `GroupDecisionTable`

```cpp
struct GroupDecisionTable
{
    std::vector<GroupMergeDecision> decisions;
};
```

- `decisions`：每个 candidate 的最终判断结果。接受和拒绝都会写入。

### `GroupMergeDecision`

定义在 `MidSurfaceNewTypes.hpp`。

```cpp
struct GroupMergeDecision
{
    int candidate_id;
    int face_a;
    int face_b;
    std::string decision;
    std::string reason;
    double score;
};
```

- `candidate_id`：对应的 `GroupMergeCandidate.candidate_id`。
- `face_a`：candidate 中较小的 face id。
- `face_b`：candidate 中较大的 face id。
- `decision`：`accept` 或 `reject`。
- `reason`：接受或拒绝原因，例如 `pass_plane`、`fail_type_mismatch`、`accept_refine`、`fail_refine`。
- `score`：判断分数。refine 阶段当前为 `pass_count / nearest_count`，prefilter 拒绝时为 `0.0`，关闭 refine 时为 `1.0`。

### `GroupTable`

```cpp
struct GroupTable
{
    std::vector<GroupRecord> groups;
};
```

- `groups`：Step2 输出的 group 列表。group id 按排序后的连通分量顺序生成。

### `GroupRecord`

定义在 `MidSurfaceNewTypes.hpp`。

```cpp
struct GroupRecord
{
    int group_id;
    std::vector<int> face_ids;
    std::vector<FACE*> faces;
    std::string type;
    std::vector<int> surface_ids;
    SPAposition seed_point;
    SPAunit_vector seed_normal;
    double area_sum;
    double local_scale;
    std::string source_rules;
    double confidence;
};
```

- `group_id`：Step2 分配的 group 编号，从 0 递增。
- `face_ids`：group 内包含的 Step1 face id，升序保存。
- `faces`：group 内 face 指针，和 `face_ids` 对应。
- `type`：group 类型。当前取 group 中第一个有效 face 的 `face_type`。
- `surface_ids`：预留字段。当前 Step2 没有填充。
- `seed_point`：group 代表点。当前用 face 面积代理做加权平均。
- `seed_normal`：group 代表法向。当前用 face 面积代理对代表法向加权，长度过小时使用 fallback normal。
- `area_sum`：group 内 face 的面积代理总和。
- `local_scale`：group 局部尺度，当前为 `sqrt(max(1.0e-12, area_sum))`。
- `source_rules`：group 生成规则说明。当前固定为 `surface_prefilter+three_gate_samples`。
- `confidence`：group 内 accepted decision 的平均 score；若 group 内没有 accepted pair，当前返回 `1.0`。

### `FaceToGroupMap`

```cpp
struct FaceToGroupMap
{
    std::map<int, int> face_to_group;
};
```

- `face_to_group`：key 是 Step1 `face_id`，value 是 Step2 `group_id`。

### `reject_reason_stats`

```cpp
std::map<std::string, int> reject_reason_stats;
```

- key：拒绝原因字符串。
- value：该原因出现次数。

拒绝原因来自 `AddDecision`。如果 reason 为空，会按 `reject_unknown` 统计。

### `GroupBuildStats`

```cpp
struct GroupBuildStats
{
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
```

- `candidate_count`：枚举出的 candidate 数量。
- `accepted_count`：最终接受的 candidate 数量。
- `rejected_count`：最终拒绝的 candidate 数量。
- `group_count`：最终生成的 group 数量。
- `valid_face_count`：从 Step1 收集到的有效 face 数。
- `surface_prefilter_pass_count`：surface 初筛通过数量。
- `surface_prefilter_reject_count`：surface 初筛拒绝数量。
- `sample_refine_pass_count`：采样精筛通过数量。
- `sample_refine_reject_count`：采样精筛拒绝数量。
- `single_face_group_count`：只包含一个 face 的 group 数量。

## 内部辅助结构

这些结构定义在 `Step2GroupBuild.cpp` 的匿名 namespace 中，只服务当前 Step2 实现。

### `ValidFaceIndex`

```cpp
struct ValidFaceIndex
{
    std::vector<const FaceRecord*> records;
    std::map<int, int> face_id_to_index;
};
```

- `records`：从 Step1 收集到的有效 face record 指针。
- `face_id_to_index`：`face_id -> records 下标`，用于通过 face id 查找 record。

### `ResolvedTolerances`

```cpp
struct ResolvedTolerances
{
    double normal_cos;
    double axis_cos;
    double plane_tol;
    double radius_tol;
    double edge_length_eps;
};
```

- `normal_cos`：`normal_angle_deg` 转成的 cos 阈值。
- `axis_cos`：`axis_angle_deg` 转成的 cos 阈值。当前尚未实际用于初筛。
- `plane_tol`：实际使用的 plane 距离容差。
- `radius_tol`：实际使用的半径差容差。
- `edge_length_eps`：实际使用的 edge 长度过滤阈值。

### `SurfacePrefilterResult`

```cpp
struct SurfacePrefilterResult
{
    logical passed;
    std::string result_text;
    std::string match_type;
    std::string matched_properties;
    std::string reason;
    double normal_dot;
    double normal_angle_deg;
    double plane_distance;
    double radius_delta;
};
```

- `passed`：初筛是否通过。
- `result_text`：预留文本字段。当前没有实际写入。
- `match_type`：匹配类型，例如 `plane`、`cylinder`、`sphere`、`disabled`。
- `matched_properties`：参与判断的属性摘要。
- `reason`：通过或失败原因。
- `normal_dot`：plane 法向点积。
- `normal_angle_deg`：plane 法向夹角。
- `plane_distance`：两个 plane 沿法向的距离。
- `radius_delta`：两个 radius surface 的半径差。

### `SourceTargetPair`

```cpp
struct SourceTargetPair
{
    const FaceRecord* source;
    const FaceRecord* target;
    logical swapped;
};
```

- `source`：精筛采样的源 face。当前选择面积较小者。
- `target`：被查询最近点的目标 face。
- `swapped`：是否相对输入顺序交换了 source 和 target。

### `FaceSampleSet`

```cpp
struct FaceSampleSet
{
    int face_id;
    std::vector<PointSample> samples;
    logical ok;
    std::string reason;
};
```

- `face_id`：采样所属 face id。
- `samples`：边界采样点。
- `ok`：采样是否成功。
- `reason`：采样失败原因。

### `ClosestHit`

```cpp
struct ClosestHit
{
    int source_face_id;
    int target_face_id;
    SPAposition source_point;
    SPAposition target_point;
    SPAunit_vector source_normal;
    SPAunit_vector target_normal;
    double distance;
    double source_normal_component;
    double target_normal_component;
    logical closest_ok;
    logical distance_ok;
    logical normal_ok;
    logical tangent_ok;
    logical pass;
    std::string fail_reason;
};
```

- `source_face_id`：source face id。
- `target_face_id`：target face id。
- `source_point`：source 采样点。
- `target_point`：target 上的最近点。
- `source_normal`：source 采样点法向。
- `target_normal`：target 最近点法向。
- `distance`：source 点到 target 最近点距离。
- `source_normal_component`：source-to-target 向量在 source 法向上的归一化分量绝对值。
- `target_normal_component`：source-to-target 向量在 target 法向上的归一化分量绝对值。
- `closest_ok`：最近点查询是否成功。
- `distance_ok`：距离 gate 是否通过。
- `normal_ok`：法向 gate 是否通过。
- `tangent_ok`：切向 gate 是否通过。
- `pass`：三个 gate 是否全部通过。
- `fail_reason`：失败原因，例如 `closest_point_failed`、`normal_unavailable`、`distance_gate`、`normal_gate`、`tangent_gate`。

### `PairRefineResult`

```cpp
struct PairRefineResult
{
    logical accepted;
    std::string reason;
    int sample_count;
    int hit_count;
    int nearest_count;
    int pass_count;
    double local_dist_gate;
    double score;
    std::vector<ClosestHit> nearest_hits;
};
```

- `accepted`：精筛是否接受该 face pair。
- `reason`：精筛结果原因，当前主要是 `accept_refine` 或 `fail_refine`。
- `sample_count`：source face 边界采样点数量。
- `hit_count`：最近点查询成功的 hit 数。
- `nearest_count`：参与最终判断的最近 hit 数。
- `pass_count`：最近 hit 中通过三个 gate 的数量。
- `local_dist_gate`：本次精筛使用的局部距离阈值。
- `score`：`pass_count / nearest_count`。
- `nearest_hits`：距离最近的 hit 列表，会写入 refine 日志的 `samples` JSON。

## 当前算法细节

### 有效 face 收集

`CollectValidFaces` 从 `step1.faces.records` 中收集：

```text
record.valid == TRUE && record.face != nullptr
```

有效 face 数写入 `stats.valid_face_count`。

### candidate 枚举

当前枚举所有有效 face 两两组合：

```text
for i in valid_faces:
  for j > i:
    candidate = all_valid_pair
```

Step1 adjacency 不参与 Step2 candidate 过滤。

### surface 初筛

`EvaluateSurfacePrefilter` 当前规则：

- face 指针缺失：拒绝，`fail_missing_face`。
- face type 不同：拒绝，`fail_type_mismatch`。
- `surface_geometry` 指针相同：通过，`pass_same_surface_pointer`。
- plane：调用 `EvaluatePlanePrefilter`。
- cylinder：调用 `EvaluateRadiusPrefilter`。
- sphere：调用 `EvaluateRadiusPrefilter`。
- cone/torus：拒绝，`fail_unverified_parameter_api`。
- 其他类型：拒绝，`fail_unsupported_surface_type`。

plane 初筛要求：

```text
normal_dot >= normal_cos
plane_distance <= plane_tol
```

cylinder/sphere 初筛要求：

```text
abs(radius_a - radius_b) <= radius_tol
```

### sample refine

初筛通过后，`EvaluatePairBySamples` 做采样精筛。

流程：

1. `ChooseSourceTarget` 选择面积较小的 face 作为 source，另一个作为 target。
2. `BuildSourceSamples` 调用 `BuildBoundaryPointSamples` 在 source face 边界上采样。
3. 对每个 source sample 调用 `EvaluateOneHit`。
4. `EvaluateOneHit` 调用 `api_entity_point_distance` 查询 target 最近点。
5. 检查 distance gate、normal gate、tangent gate。
6. 按距离排序所有成功 hit。
7. 只取最近 `local_nearest_count` 个 hit。
8. 若最近 hit 中通过数量大于等于 `local_min_pass_count`，接受该 pair。

local distance gate 计算：

```text
if source.edge_max > 0:
  base = source.area_proxy / source.edge_max
else if source.edge_min > 0:
  base = source.edge_min
else:
  base = sqrt(source.area_proxy)

local_dist_gate = base * local_distance_gate_scale
```

### group 构建

接受的 candidate 会调用 `UnionFind.Unite(face_a, face_b)`。所有 candidate 处理完后，`BuildGroupsFromUnionFind` 把连通分量转成 group。

`BuildOneGroupRecord` 会：

- 写入 `group_id` 和 `face_ids`。
- 收集 `FACE*`。
- 设置 `type`。
- 用面积代理加权计算 `seed_point`。
- 用面积代理加权计算 `seed_normal`。
- 累加 `area_sum`。
- 设置 `local_scale = sqrt(area_sum)`。
- 设置 `source_rules = "surface_prefilter+three_gate_samples"`。
- 设置 `confidence`。

## 函数说明

### `RunStep2GroupBuild`

Step2 外部入口。

```cpp
logical RunStep2GroupBuild(
    const Step1FaceAnalyzeState& step1,
    const Step2GroupOptions& options,
    DiagnosticSink* diagnostics,
    Step2GroupResult& result);
```

流程：

1. 重置 `result`。
2. 保存 `&step1` 到 `result.state.input_step1`。
3. 保存 `options` 到 `result.state.options_snapshot`。
4. 调用 `CollectValidFaces`。
5. 输出 start run event。
6. 若没有有效 face，输出 stage summary 和 finish run event，返回 `FALSE`。
7. 初始化 union-find。
8. 调用 `ResolveTolerances`。
9. 枚举所有有效 face pair。
10. 对每个 pair 创建 `GroupMergeCandidate`。
11. 执行 surface prefilter。
12. 若初筛失败，写 reject decision，继续下一个 candidate。
13. 执行 sample refine。
14. 若 refine 失败，写 reject decision，继续下一个 candidate。
15. refine 通过时写 accept decision，并 union 两个 face。
16. 调用 `BuildGroupsFromUnionFind`。
17. 可选输出 group events。
18. 输出 color map events 和 debug SAT。
19. 设置 `result.ok = TRUE`。
20. 输出 stage summary 和 finish run event。

### `CollectValidFaces`

从 Step1 face 表中收集有效 face，并建立 `face_id_to_index`。

### `FindFaceRecord`

通过 `face_id` 从 `ValidFaceIndex` 查找 `FaceRecord`。

### `MakeCandidate`

构造 `GroupMergeCandidate`，保证 `face_a < face_b`，并设置 `source = "all_valid_pair"`。

### `AddDecision`

写入 `GroupMergeDecision`。若 decision 是 `accept`，递增 `accepted_count`；否则递增 `rejected_count` 并更新 `reject_reason_stats`。

### `ResolveTolerances`

根据 options 和 body bbox 对角线推导实际容差。

规则：

- `normal_cos = cos(normal_angle_deg)`
- `axis_cos = cos(axis_angle_deg)`
- `plane_tol = options.plane_tol > 0 ? options.plane_tol : max(1e-6, bbox_diag * 1e-5)`
- `radius_tol = options.radius_tol > 0 ? options.radius_tol : max(1e-6, bbox_diag * 1e-4)`
- `edge_length_eps = options.edge_length_eps > 0 ? options.edge_length_eps : max(1e-9, bbox_diag * 1e-8)`

### `EvaluateSurfacePrefilter`

统一 surface 初筛入口，根据 face type 分派到 plane 或 radius 检查。

### `EvaluatePlanePrefilter`

调用 `get_face_plane` 获取两个 face 的 plane 点和法向，检查法向夹角和法向距离。

### `EvaluateRadiusPrefilter`

调用 `get_face_radius` 获取半径，检查半径差是否在 `radius_tol` 内。

### `ChooseSourceTarget`

选择面积较小的 face 作为 source。面积相同时，face id 较小者作为 source。

### `BuildSourceSamples`

把 `Step2GroupOptions` 转为 `BoundarySampleOptions`，调用 `BuildBoundaryPointSamples` 生成 source face 边界采样点。

### `QueryLocalFaceNormal`

查询 face 在某个点附近的局部法向。plane 优先调用 `get_face_normal(record.face, out_normal)`，其他情况调用 `sg_get_face_normal(record.face, point)`。

### `ComputeLocalDistanceGate`

根据 source face 的面积代理和 edge 长度估算局部距离阈值。

### `EvaluateOneHit`

对一个 source sample 查询 target 最近点，并检查三个 gate：

- distance gate：距离不超过 `local_dist_gate`。
- normal gate：source normal 和 target normal 点积不小于 `normal_cos`。
- tangent gate：source-to-target 向量在 source/target 法向上的分量不超过 `local_normal_component_max`。

### `EvaluatePairBySamples`

对一个 face pair 执行完整 sample refine，输出 `PairRefineResult`。

### `BuildOneGroupRecord`

根据一个 face id 连通分量构造 `GroupRecord`。

### `AverageAcceptedScoreForGroup`

统计 group 内 accepted decisions 的平均 score。如果没有 accepted decision，返回 `1.0`。

### `BuildGroupsFromUnionFind`

把 union-find 连通分量转成 group，填充 `face_to_group`，统计 single-face group，并按 group palette 给 face 染色。

### `EmitPrefilterEvent`

输出 prefilter detail event。

### `EmitRefineEvent`

输出 refine detail event，并把最近 hit 列表写入 JSON property `samples`。

### `EmitGroupEvent`

输出单个 group 的 summary event。

### `EmitStageSummaryEvents`

输出 prefilter、refine、group 三个阶段的 summary event。

### `EmitRunEvent`

输出 Step2 start/finish run event。

### `EmitStep2ColorMapEvents`

为 `step2.group_colored_body` 输出 group id 到颜色、face ids 的映射。

## 日志输出

所有 Step2 事件基础 tag：

```text
step2
grouping
```

### run event

触发位置：Step2 start 和 finish。

start tags：

```text
step2
grouping
start
```

finish tags：

```text
step2
grouping
finish
summary
all
```

properties：

- `valid_face_count`：有效 face 数。
- `candidate_count`：candidate 数。
- `accepted_count`：接受数量。
- `rejected_count`：拒绝数量。
- `group_count`：group 数。
- `single_face_group_count`：单 face group 数。
- `surface_prefilter_pass_count`：初筛通过数量。
- `surface_prefilter_reject_count`：初筛拒绝数量。
- `sample_refine_pass_count`：精筛通过数量。
- `sample_refine_reject_count`：精筛拒绝数量。
- `reject_reason_stats`：拒绝原因统计，格式为 `reason=count;reason=count`。

### prefilter detail event

触发位置：每个 candidate 完成 surface prefilter 后，且 `emit_prefilter_events == TRUE`。

tags：

```text
step2
grouping
prefilter
detail
```

properties：

- `face_a`：candidate 中较小的 face id。
- `face_b`：candidate 中较大的 face id。
- `face_a_type`：face_a 类型。
- `face_b_type`：face_b 类型。
- `result`：`pass` 或 `fail`。
- `match_type`：匹配类型，例如 `plane`、`cylinder`、`sphere`、`disabled`。
- `normal_angle_deg`：plane 法向夹角。
- `plane_distance`：plane 法向距离。
- `radius_delta`：半径差。
- `matched_properties`：参与判断的属性摘要。
- `reason`：通过或失败原因。

### refine detail event

触发位置：candidate 进入 sample refine 后，且 `emit_refine_events == TRUE`。

tags：

```text
step2
grouping
refine
detail
```

properties：

- `source_face`：source face id。
- `target_face`：target face id。
- `result`：`pass` 或 `fail`。
- `sample_count`：source 采样数量。
- `pass_count`：最近 hit 中通过数量。
- `local_dist_gate`：局部距离阈值。

json_properties：

- `samples`：最近 hit 列表。每项包含：
  - `i`：样本序号。
  - `p`：source 点坐标数组。
  - `cp`：target 最近点坐标数组。
  - `dist`：距离。
  - `distance_ok`：distance gate 是否通过。
  - `normal_ok`：normal gate 是否通过。
  - `tangent_ok`：tangent gate 是否通过。
  - `pass`：该 hit 是否通过全部 gate。

### group record event

触发位置：group 构建完成后，且 `emit_group_events == TRUE`。

tags：

```text
step2
grouping
group
record
summary
single
```

properties：

- `group_id`：group 编号。
- `face_count`：group 内 face 数量。
- `face_ids`：group 内 face id，逗号分隔。
- `type`：group 类型。
- `area_sum`：面积代理总和。
- `local_scale`：局部尺度。
- `confidence`：group confidence。
- `source_rules`：group 生成规则说明。

### stage summary events

触发位置：Step2 结束前。

prefilter stage tags：

```text
step2
grouping
prefilter
summary
stage
finish
```

properties：

- `candidate_count`：candidate 总数。
- `pass_count`：初筛通过数。
- `reject_count`：初筛拒绝数。

refine stage tags：

```text
step2
grouping
refine
summary
stage
finish
```

properties：

- `pass_count`：精筛通过数。
- `reject_count`：精筛拒绝数。
- `reject_reason_stats`：拒绝原因统计。

group stage tags：

```text
step2
grouping
group
summary
stage
finish
```

properties：

- `group_count`：group 数。
- `single_face_group_count`：单 face group 数。

### color-map

触发位置：Step2 group 构建后。

用途：记录 `step2.group_colored_body` 中颜色到 `group_id` 和 `face_ids` 的映射。具体公共字段由 `DiagnosticSink::EmitColorIdMapEntryIfEnabled` 生成。

Step2 传入的 tags：

```text
step2
grouping
```

传入的 artifact role：

```text
step2.group_colored_body
```

properties：

- `group_id`：group 编号。
- `face_count`：group 内 face 数量。
- `type`：group 类型。

json_properties：

- `rgb`：RGB 数组。
- `face_ids`：group 内 face id 数组。

### debug SAT

触发位置：Step2 group 构建后。

role：

```text
step2.group_colored_body
```

输出条件：`RunContextOptions.debug_sat_outputs` 包含该 role。

内容：输入 body 被按 Step2 group palette 重新染色后的 SAT。
