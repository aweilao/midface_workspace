# Step3 Pairing 工程文档

本文记录当前 `midface` Step3 的实现。源码事实源：

- `midface/steps/Step3PairBuild.hpp`
- `midface/steps/Step3PairBuild.cpp`
- `midface/steps/Step2GroupBuild.hpp`
- `midface/core/MidSurfaceNewTypes.hpp`
- `midface/core/MidSurfaceNewTypes.cpp`
- `midface/utils/GroupUtils.hpp`
- `midface/utils/SamplingUtils.hpp`
- `midface/MidSurface.cpp`

## 目标和算法

Step3 输入 Step2 的 `Step2GroupState`，在 group 之间寻找厚度方向上的配对关系，输出 pair、wall candidate、group 到 pair/wall 的映射和统计信息。

Step3 做的事情：

- 枚举所有 Step2 group pair。
- 对 plane-plane 做粗法向反向初筛。
- 对 group type 和 seed point 距离做快速 surface 初筛。
- 对候选 group pair 做双向内部采样精筛。
- 根据通过样本数、通过比例、厚度、coverage、法向反向程度和评分接受 pair。
- 对候选 pair 排序，按每组最大配对数选择最终 pair。
- 按每个 group 的最小厚度做后筛，丢弃过厚 pair。
- 生成未配对 group 的 wall candidate。
- 给 pair group 和 wall group 染色，输出 `step3.pair_colored_body` 调试 SAT。

Step3 不做的事情：

- 不生成中面 patch。
- 不构建 pair-pair 关系。
- 不构建 MW/MM 关系。
- 不做 trim 或 stitch。

Step3 主流程：

```text
RunMidSurfaceBodyWithContext
  -> RunStep3PairBuild(result.step2.state, config.step3, diagnostics, result.step3)
    -> EmitRunEvent(start)
    -> enumerate all group pairs
      -> MakeCandidate
      -> CoarsePairNormalPrefilter
      -> FastPairSurfacePrefilter
      -> EvaluateRegionPair
        -> EvalDirectionalGroup(group A -> group B)
          -> CollectGroupInteriorSamples
          -> FindClosestPointOnGroup
          -> normal gate + direction gate
        -> EvalDirectionalGroup(group B -> group A)
        -> thickness/coverage/score gates
      -> EmitCandidateEvent / EmitRefineEvent
    -> sort accepted candidates by PairBetter
    -> select pairs with max-per-group and duplicate checks
    -> FilterPairsByPerGroupThickness
    -> ReassignPairIds
    -> BuildGroupToPairMap
    -> BuildWallsFromUnpairedGroups
    -> ColorPairs + ColorWalls
    -> EmitPairEvent / EmitWallEvent / EmitGroupRoleEvents
    -> EmitStep3ColorMapEvents
    -> EmitBodySatIfEnabled("step3.pair_colored_body")
    -> EmitStageSummaryEvents
    -> EmitRunEvent(finish)
```

## 参数设置

`Step3PairOptions` 定义在 `Step3PairBuild.hpp`，默认值在 `Step3PairBuild.cpp`。

```cpp
struct Step3PairOptions
{
    logical enable_coarse_normal_prefilter = TRUE;
    logical enable_surface_prefilter = TRUE;
    logical enable_pair_refine = TRUE;
    logical allow_multi_pair_per_group = TRUE;
    logical emit_candidate_events = TRUE;
    logical emit_refine_events = TRUE;
    logical emit_pair_events = TRUE;

    double pair_opposite_angle_deg = 20.0;
    double pair_direction_angle_deg = 14.0;
    int pair_min_pass_count = 3;
    double pair_min_pass_ratio = 0.4;
    double pair_min_small_coverage = 0.20;
    logical pair_same_type_only = FALSE;
    double pair_interior_sample_density = 2.0;
    int pair_interior_sample_min = 9;
    int pair_interior_sample_max = 36;
    double pair_distance_quantile = 0.50;
    double pair_thickness_min = -1.0;
    double pair_thickness_max = -1.0;
    double pair_adaptive_dist_ratio = 1.0;
    logical pair_enable_facing_dist_relax = TRUE;
    double pair_facing_relax_start = 0.70;
    double pair_facing_dist_relax_max = 0.80;
    double pair_facing_dist_relax_power = 2.0;
    double pair_thinness_max = -1.0;
    double pair_min_score = 0.35;
    int pair_max_per_group = -1;
    double pair_group_thickness_ratio_max = 2.0;
    double rib_area_ratio = 0.35;
};
```

- `enable_coarse_normal_prefilter`：是否启用 plane-plane 的粗法向反向初筛。
- `enable_surface_prefilter`：是否启用 group type 和 seed distance 快速初筛。
- `enable_pair_refine`：是否启用双向采样精筛。关闭后通过初筛的 candidate 直接作为候选接受。
- `allow_multi_pair_per_group`：是否允许一个 group 进入多个 pair。关闭时等价于 `pair_max_per_group = 1`。
- `emit_candidate_events`：是否输出 candidate 阶段日志。
- `emit_refine_events`：是否输出 refine 阶段日志。
- `emit_pair_events`：是否输出最终 pair、wall 和 group role 日志。
- `pair_opposite_angle_deg`：两侧法向近反向 gate 的角度阈值。
- `pair_direction_angle_deg`：source-to-target 方向与两侧法向一致性的角度阈值。
- `pair_min_pass_count`：双向采样中至少需要的通过样本数。
- `pair_min_pass_ratio`：双向通过比例中的最大值下限。
- `pair_min_small_coverage`：小面积侧 coverage 下限。
- `pair_same_type_only`：是否强制两侧 group type 完全相同。
- `pair_interior_sample_density`：内部采样密度。
- `pair_interior_sample_min`：内部采样最小点数。
- `pair_interior_sample_max`：内部采样最大点数。
- `pair_distance_quantile`：用通过样本距离的哪个分位数作为厚度代表值。
- `pair_thickness_min`：厚度下限；小于等于 0 表示不启用。
- `pair_thickness_max`：厚度上限；小于等于 0 表示不启用。
- `pair_adaptive_dist_ratio`：自适应厚度上限比例，按两组较小宽度计算。
- `pair_enable_facing_dist_relax`：是否按 facing score 放宽自适应厚度上限。
- `pair_facing_relax_start`：开始放宽的 facing score。
- `pair_facing_dist_relax_max`：最大放宽比例。
- `pair_facing_dist_relax_power`：放宽曲线指数。
- `pair_thinness_max`：厚度与 group scale 比值上限；小于等于 0 表示不启用。
- `pair_min_score`：综合评分下限。
- `pair_max_per_group`：每个 group 最多参与多少 pair；小于等于 0 表示不限制。
- `pair_group_thickness_ratio_max`：同一 group 多 pair 时，超过本组最小厚度该倍数的 pair 会被后筛丢弃。
- `rib_area_ratio`：两侧面积比低于该值时标记 `rib_candidate`。

## Result 和 State

### `Step3PairResult`

```cpp
struct Step3PairResult
{
    logical ok;
    Step3PairState state;
};
```

- `ok`：Step3 是否完成。当前实现中，只要 Step2 有 group 并完成处理，就为 `TRUE`。
- `state`：Step3 完整输出状态。

### `Step3PairState`

```cpp
struct Step3PairState
{
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
```

- `input_step2`：指向本次 Step3 使用的 Step2 state。Step3 从这里读取 groups、face 和上游 input body。
- `options_snapshot`：Step3 本次运行使用的参数快照。
- `candidates`：所有枚举出来的 group pair candidate。
- `decisions`：candidate 的接受或拒绝结果。包括初筛拒绝、refine 拒绝、选择阶段拒绝和最终接受。
- `pairs`：最终选中的厚度 pair。
- `walls`：未进入任何 pair 的 group 生成的 wall candidate。
- `group_to_pairs`：`group_id -> pair_id list`。
- `group_to_walls`：`group_id -> wall_id`。
- `stats`：Step3 统计信息。

### `PairCandidateTable`

```cpp
struct PairCandidateTable
{
    std::vector<PairCandidate> candidates;
};
```

- `candidates`：Step3 枚举出的所有 group pair。

### `PairCandidate`

```cpp
struct PairCandidate
{
    int candidate_id;
    int group_a;
    int group_b;
    std::string source;
};
```

- `candidate_id`：Step3 内部递增编号。
- `group_a`：candidate 中较小的 group id。
- `group_b`：candidate 中较大的 group id。
- `source`：candidate 来源。当前固定为 `all_group_pair`。

### `PairDecisionTable`

```cpp
struct PairDecisionTable
{
    std::vector<PairDecisionRecord> decisions;
};
```

- `decisions`：candidate 判断记录列表。

### `PairDecisionRecord`

```cpp
struct PairDecisionRecord
{
    int candidate_id;
    int group_a;
    int group_b;
    std::string decision;
    std::string reason;
    double score;
};
```

- `candidate_id`：对应的 candidate id。
- `group_a`：candidate 的 group_a。
- `group_b`：candidate 的 group_b。
- `decision`：`accept` 或 `reject`。
- `reason`：接受或拒绝原因，例如 `accept_region_pair`、`fail_min_pass_count`、`fail_surface_prefilter`、`fail_max_per_group`。
- `score`：该 candidate 的评分。

### `PairTable`

```cpp
struct PairTable
{
    std::vector<PairRecord> pairs;
};
```

- `pairs`：最终选中的 pair 列表。

### `PairRecord`

```cpp
struct PairRecord
{
    int pair_id;
    int group_a;
    int group_b;
    double thickness;
    double coverage;
    double score;
    double facing_score;
    double pass_ratio;
    double pass_balance;
    SPAposition point_a;
    SPAposition point_b;
    SPAunit_vector pair_direction;
    logical uncertain;
    logical rib_candidate;
    std::string reason;
};
```

- `pair_id`：最终 pair 编号，后筛后会重新从 0 分配。
- `group_a`：pair 一侧 group id。
- `group_b`：pair 另一侧 group id。
- `thickness`：通过样本距离分位数得到的厚度代表值。
- `coverage`：小面积侧的通过比例。
- `score`：综合评分。
- `facing_score`：两组 seed normal 的反向程度评分。
- `pass_ratio`：AB/BA 双向通过比例中的较大值。
- `pass_balance`：AB/BA 双向通过数量的平衡度。
- `point_a`：代表厚度线一端点。
- `point_b`：代表厚度线另一端点。
- `pair_direction`：从 `point_a` 指向 `point_b` 的单位向量。
- `uncertain`：证据偏弱的标记。当前当 `gate_pass < pair_min_pass_count` 或 `coverage < 0.25` 时为 `TRUE`。
- `rib_candidate`：薄肋候选标记。当前按两侧面积比是否低于 `rib_area_ratio` 判断。
- `reason`：pair 接受原因，通常为 `accept_region_pair`。

### `WallTable`

```cpp
struct WallTable
{
    std::vector<WallRecord> walls;
};
```

- `walls`：未配对 group 生成的 wall candidate。Step4 会继续读取这些 wall。

### `WallRecord`

```cpp
struct WallRecord
{
    int wall_id;
    std::vector<int> source_face_ids;
    BODY* body;
    std::string source;
    int pair_a;
    int pair_b;
    int group_a;
    int group_b;
    int bucket_slot;
    double coedge_len;
    double half_profile;
    double hit_normal_cos_abs;
};
```

Step3 只填充其中一部分字段：

- `wall_id`：wall candidate 编号。
- `source_face_ids`：该 wall group 中的 face id 列表。
- `body`：Step3 中为 `nullptr`。
- `source`：Step3 中固定为 `step3_unpaired_group`。
- `pair_a`：Step3 中保持默认 `-1`。
- `pair_b`：Step3 中保持默认 `-1`。
- `group_a`：未配对 group id。
- `group_b`：Step3 中保持默认 `-1`。
- `bucket_slot`：Step3 中保持默认 `-1`。
- `coedge_len`：Step3 中保持默认 `0.0`。
- `half_profile`：Step3 中保持默认 `0.0`。
- `hit_normal_cos_abs`：Step3 中保持默认 `-1.0`。

### `GroupToPairMap`

```cpp
struct GroupToPairMap
{
    std::map<int, std::vector<int> > group_to_pairs;
};
```

- `group_to_pairs`：key 是 group id，value 是该 group 参与的 pair id 列表。

### `GroupToWallMap`

```cpp
struct GroupToWallMap
{
    std::map<int, int> group_to_wall;
};
```

- `group_to_wall`：key 是未配对 group id，value 是对应 wall id。

### `PairBuildStats`

```cpp
struct PairBuildStats
{
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
```

- `candidate_count`：枚举出的 group pair candidate 数。
- `accepted_count`：最终接受 decision 数。
- `rejected_count`：最终拒绝 decision 数。
- `pair_count`：最终 pair 数。
- `coarse_prefilter_pass_count`：粗法向初筛通过数。
- `coarse_prefilter_reject_count`：粗法向初筛拒绝数。
- `surface_prefilter_pass_count`：surface 初筛通过数。
- `surface_prefilter_reject_count`：surface 初筛拒绝数。
- `refine_pass_count`：采样精筛通过数。
- `refine_reject_count`：采样精筛拒绝数。
- `thickness_filter_dropped_count`：厚度后筛丢弃 pair 数。
- `rib_candidate_count`：标记为 rib candidate 的 pair 数。
- `uncertain_count`：标记为 uncertain 的 pair 数。
- `wall_count`：Step3 生成的 wall 数。
- `pair_group_count`：参与 pair 的 group 数。
- `wall_group_count`：进入 wall 的 group 数。

## 内部辅助结构

### `PairEvalSample`

保存一次 source group 到 target group 最近点评估。

- `source_group`：source group id。
- `target_group`：target group id。
- `source_face`：source sample 所属 face id。
- `target_face`：最近点所在 target face id。
- `point`：source sample 点。
- `closest_point`：target group 最近点。
- `distance`：距离。
- `normal_dot`：source normal 与 target normal 点积。
- `u_dot_source_normal`：source-to-target 单位方向与 source normal 的点积。
- `u_dot_target_normal`：source-to-target 单位方向与 target normal 的点积。
- `normal_ok`：法向反向 gate 是否通过。
- `direction_ok`：厚度方向 gate 是否通过。
- `pass`：两个 gate 是否都通过。

### `DirectionEval`

保存一个方向的 group 到 group 采样评估结果。

- `total`：成功完成最近点评估的样本数。
- `pass`：通过样本数。
- `pass_distances`：通过样本的距离。
- `has_best`：是否有最佳通过样本。
- `best_distance`：最佳通过样本距离。
- `best_source_point`：最佳 source 点。
- `best_target_point`：最佳 target 点。
- `samples`：该方向所有评估样本。

### `RegionPairData`

保存一个 group pair 精筛后的完整数据。

- `ga` / `gb`：group id。
- `distance`：厚度代表值。
- `score`：综合评分。
- `coverage_small`：小面积侧 coverage。
- `pass_balance`：双向通过数量平衡度。
- `ratio_ab` / `ratio_ba`：AB/BA 方向通过比例。
- `pass_ab` / `pass_ba`：AB/BA 方向通过数量。
- `total_ab` / `total_ba`：AB/BA 方向总样本数。
- `gate_pass`：双向通过数量中的较大值。
- `facing_score`：法向反向程度评分。
- `adaptive_max`：自适应厚度上限。
- `adaptive_relax`：facing relax 后的放宽倍数。
- `width_a` / `width_b` / `width_min`：group 宽度尺度。
- `pa` / `pb`：代表厚度线端点。
- `reason`：接受或拒绝原因。
- `samples`：双向采样评估详情。

## 函数说明

### `RunStep3PairBuild`

Step3 外部入口。

流程：

1. 重置 `result`。
2. 保存 `&step2` 到 `result.state.input_step2`。
3. 保存 options 快照。
4. 输出 start run event。
5. 若 Step2 没有 group，输出 summary 和 finish 后返回 `FALSE`。
6. 枚举所有 group pair。
7. 执行粗法向初筛和 surface 初筛。
8. 执行 `EvaluateRegionPair`。
9. 保存通过 refine 的候选。
10. 按 `PairBetter` 排序。
11. 按 `pair_max_per_group`、重复 pair 等规则选择最终 pair。
12. 执行 `FilterPairsByPerGroupThickness`。
13. 重新分配 pair id。
14. 构建 group-to-pair 和 wall。
15. 染色并输出日志、color map 和 debug SAT。
16. 输出 stage summary 和 finish run event。

### `EvalDirectionalGroup`

执行一个方向的 group 到 group 最近点评估。

流程：

1. 调用 `CollectGroupInteriorSamples` 生成 source group 内部采样点。
2. 对每个 sample 调用 `FindClosestPointOnGroup` 找 target group 最近点。
3. 计算 source 和 target 法向。
4. 检查 normal gate：`normal_dot <= -cos_opp`。
5. 检查 direction gate：source-to-target 方向应沿 source 反法向、target 正法向。
6. 保存通过样本距离和最佳通过样本。

### `CoarsePairNormalPrefilter`

只对 plane-plane group 生效。要求两个 seed normal 近似反向；非 plane 直接通过。

### `FastPairSurfacePrefilter`

检查 group type 和 seed point 距离。

- `pair_same_type_only == TRUE` 时要求 type 完全相同。
- 否则，当两侧 type 都非空且都不是 `unknown` 时，也要求 type 相同。
- seed point 距离不能超过 `18.0 * max(GroupScale(a), GroupScale(b))`。

### `EvaluateRegionPair`

执行完整双向采样精筛。

主要 gate：

- `pair_min_pass_count`
- `pair_min_pass_ratio`
- `pair_thickness_min`
- `pair_thickness_max`
- `pair_adaptive_dist_ratio`
- `pair_min_small_coverage`
- `pair_thinness_max`
- `pair_min_score`

评分：

```text
score = 0.40 * count_score
      + 0.30 * coverage_score
      + 0.20 * normal_score
      + 0.10 * balance_score
```

### `PairBetter`

accepted candidates 排序规则：

1. score 高者优先。
2. coverage 高者优先。
3. gate_pass 高者优先。
4. distance 小者优先。

### `AddDecision`

写入 `PairDecisionRecord`，并更新 `accepted_count` 或 `rejected_count`。

### `MakeCandidate`

构造 `PairCandidate`，确保 group id 升序，并设置 `source = "all_group_pair"`。

### `MakePairRecord`

把 `RegionPairData` 转成最终 `PairRecord`，同时计算 `pair_direction`、`rib_candidate` 和 `uncertain`。

### `FilterPairsByPerGroupThickness`

对每个 group 统计已选 pair 的最小厚度。若某个 pair 的厚度超过该 group 最小厚度的 `pair_group_thickness_ratio_max` 倍，且该 group 有多个 pair，则丢弃该 pair。

### `ReassignPairIds`

厚度后筛后重新按 vector 顺序分配 pair id。

### `BuildGroupToPairMap`

根据最终 pair 表构建 `group_id -> pair_id list`。

### `BuildWallsFromUnpairedGroups`

遍历 Step2 groups，未出现在 `group_to_pairs` 中的 group 会生成一个 `WallRecord`。

### `ColorPairs`

按 pair palette 给 pair 两侧 group 的 face 染色。

### `ColorWalls`

按 wall palette 给 wall group 的 face 染色。当前在 `RunStep3PairBuild` 中先 `ColorPairs` 再 `ColorWalls`。

### `EmitStep3ColorMapEvents`

输出 `step3.pair_colored_body` 的 pair/wall 颜色映射。

## 日志输出

所有 Step3 事件基础 tag：

```text
step3
pairing
```

### run event

start tags：

```text
step3
pairing
start
```

finish tags：

```text
step3
pairing
finish
summary
all
```

properties：

- `candidate_count`
- `accepted_count`
- `rejected_count`
- `pair_count`
- `coarse_prefilter_pass_count`
- `coarse_prefilter_reject_count`
- `surface_prefilter_pass_count`
- `surface_prefilter_reject_count`
- `refine_pass_count`
- `refine_reject_count`
- `thickness_filter_dropped_count`
- `rib_candidate_count`
- `uncertain_count`
- `wall_count`
- `pair_group_count`
- `wall_group_count`

### candidate detail event

tags：

```text
step3
pairing
candidate
<stage>
detail
```

`<stage>` 当前可能是 `coarse_normal` 或 `surface`。

properties：

- `candidate_id`
- `group_a`
- `group_b`
- `result`
- `source`
- `reason`

### refine detail event

tags：

```text
step3
pairing
refine
detail
```

properties：

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

json_properties：

- `samples`：最多 80 个 sample，每项包含 `source_group`、`target_group`、`source_face`、`target_face`、`p`、`cp`、`dist`、`normal_dot`、`u_dot_source_normal`、`u_dot_target_normal`、`normal_ok`、`direction_ok`、`pass`。

### pair record event

tags：

```text
step3
pairing
pair
record
summary
single
```

properties：

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

### wall record event

tags：

```text
step3
pairing
wall
summary
single
```

properties：

- `wall_id`
- `group_id`
- `face_count`
- `source`

json_properties：

- `face_ids`

### group role event

tags：

```text
step3
pairing
group-role
summary
single
```

properties：

- `group_id`
- `role`：`pair` 或 `wall`。
- `pair_count`
- `wall_id`：仅 wall group 有意义。

json_properties：

- `pair_ids`：仅 pair group 输出。

### stage summary events

candidate stage tags：

```text
step3
pairing
candidate
summary
stage
finish
```

refine stage tags：

```text
step3
pairing
refine
summary
stage
finish
```

group role stage tags：

```text
step3
pairing
group-role
summary
stage
finish
```

### thickness-filter detail event

tags：

```text
step3
pairing
thickness-filter
detail
```

properties 记录被丢弃 pair 的厚度、两侧 group、每组最小厚度和限制值。

### color-map

artifact role：

```text
step3.pair_colored_body
```

pair color map properties：

- `pair_id`
- `group_a`
- `group_b`
- `face_count`

wall color map properties：

- `wall_id`
- `group_id`
- `face_count`

json_properties：

- `rgb`
- `face_ids`

### debug SAT

role：

```text
step3.pair_colored_body
```

输出条件：`RunContextOptions.debug_sat_outputs` 包含该 role。

内容：按 Step3 pair/wall 染色后的 input body。
