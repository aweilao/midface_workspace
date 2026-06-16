# Step4 Relation 工程文档

本文记录当前 `midface` Step4 的实现。源码事实源：

- `midface/steps/Step4RelationBuild.hpp`
- `midface/steps/Step4RelationBuild.cpp`
- `midface/steps/Step3PairBuild.hpp`
- `midface/steps/Step2GroupBuild.hpp`
- `midface/core/MidSurfaceNewTypes.hpp`
- `midface/core/MidSurfaceNewTypes.cpp`
- `midface/utils/GroupUtils.hpp`
- `midface/MidSurface.cpp`

## 目标和算法

Step4 输入 Step3 的 `Step3PairState`，构建 pair-wall 关系、pair-pair 关系，并为 MM2 pair link 尝试生成 virtual wall body。

Step4 做的事情：

- 从 Step2 groups 建立 `FACE* -> group_id` 映射。
- 遍历 group face 的 coedge partner ring，统计 group adjacency hit。
- 识别 tiny face，并在邻接统计时尝试穿透 tiny face 找真实相邻 group。
- 基于 Step3 wall group 和 pair 两侧邻接，生成 MW1 pair-wall 关系。
- 基于 group adjacency 统计 pair-pair hit bucket。
- 对 orphan wall group 注入 synthetic pair-pair hit。
- 根据 hit normal 或 pair direction 把 pair-pair link 分类为 MM1/MM2。
- 对 MM2 link 尝试 sweep virtual wall body，并把结果保存到 `Step4RelationState.walls`。

Step4 不做的事情：

- 不生成中面 patch。
- 不执行 trim。
- 不输出 Step4 专属 debug SAT。

Step4 主流程：

```text
RunMidSurfaceBodyWithContext
  -> RunStep4RelationBuild(result.step3.state, config.step4, diagnostics, result.step4)
    -> EmitStartEvent
    -> check step3.input_step2
    -> BuildFaceToGroupMap
    -> BuildGroupAdjacencyHitsByPartner
      -> BuildTinyFaceSet
      -> traverse group face loop/coedge partner ring
      -> ResolveTinyPartnerHitGroup when needed
    -> EmitAdjacencySummary
    -> build MW1 pair-wall relations
    -> build direct pair-pair hit accumulators
    -> BuildSyntheticPairHitsFromOrphanWalls
    -> classify pair links as MM1/MM2
    -> EmitPairLinkEvent
    -> BuildVirtualWallBodiesFromMm2LinkSweep for MM2
    -> EmitVirtualWallEvent
    -> fill stats
    -> EmitFinishEvent
```

## 参数设置

`Step4RelationOptions` 定义在 `Step4RelationBuild.hpp`，默认值在 `Step4RelationBuild.cpp`。

```cpp
struct Step4RelationOptions
{
    logical build_pair_links = TRUE;
    logical build_walls = TRUE;
    logical emit_relation_events = TRUE;
    logical enable_tiny_face_partner_patch = TRUE;
    logical enable_orphan_wall_bridge = TRUE;
    logical enable_mm2_angle_gate = TRUE;
    logical enable_mm2_sweep_wall = TRUE;
    logical mm2_use_all_hit_buckets = FALSE;
    logical mm2_per_edge_sweep_only = FALSE;
    logical mm2_enable_smooth_sampled_path = FALSE;
    logical mm2_smooth_keep_closed = TRUE;
    logical mm2_enable_connect_strategy = TRUE;
    logical mm2_endpoint_single_use = TRUE;
    logical mm2_sweep_owner1_or3_only = FALSE;
    logical mm2_extend_only_open_ends = FALSE;
    logical mm2_path_end_tbar_enable = TRUE;
    logical mm2_fallback_edgewise_sweep = TRUE;
    int min_link_hits = 1;
    double pair_mm2_angle_deg = 5.0;
    double mm2_smooth_sample_step_ratio = 0.1;
    double mm2_smooth_sample_step_abs_min = 1.0e-4;
    double mm2_connect_gap_ratio = 0.2;
    double mm2_connect_gap_abs_min = 5.0e-4;
    double mm2_sweep_thickness_scale = 4.0;
    double mm2_sweep_thickness_min = 1.0e-3;
    double mm2_path_end_extend_ratio = 0.1;
    double mm2_path_end_extend_abs_min = 1.0e-4;
    double mm2_path_end_tbar_ratio = 0.1;
};
```

- `build_pair_links`：是否构建 pair-pair relation。
- `build_walls`：是否构建 pair-wall relation，并对 MM2 link 尝试 virtual wall。
- `emit_relation_events`：是否输出 Step4 日志。
- `enable_tiny_face_partner_patch`：是否启用 tiny face 穿透邻接修正。
- `enable_orphan_wall_bridge`：是否对 orphan wall group 注入 synthetic pair-pair hit。
- `enable_mm2_angle_gate`：是否保留 MM2 分类。关闭时 MM2 会降为 MM1。
- `enable_mm2_sweep_wall`：是否对 MM2 link 尝试 sweep virtual wall body。
- `mm2_use_all_hit_buckets`：是否对 MM2 的所有 hit bucket 都尝试建 wall；关闭时只选主 bucket。
- `mm2_per_edge_sweep_only`：是否只按单 edge sweep。当前参与 sweep path 策略。
- `mm2_enable_smooth_sampled_path`：是否启用 smooth sampled path。当前默认关闭。
- `mm2_smooth_keep_closed`：smooth path 是否保留闭合。
- `mm2_enable_connect_strategy`：是否启用 path 连接策略。
- `mm2_endpoint_single_use`：连接 endpoint 时是否单次使用。
- `mm2_sweep_owner1_or3_only`：收集 bucket edge 时是否只保留 owner mask 为 1 或 3 的 coedge。
- `mm2_extend_only_open_ends`：是否只延长开放端。
- `mm2_path_end_tbar_enable`：是否启用 path end T-bar 处理。
- `mm2_fallback_edgewise_sweep`：整体 sweep 失败时是否回退到 edgewise sweep。
- `min_link_hits`：pair-pair link 最少 hit 数。
- `pair_mm2_angle_deg`：MM2 分类角度阈值。
- `mm2_smooth_sample_step_ratio`：smooth sampled path 采样步长比例。
- `mm2_smooth_sample_step_abs_min`：smooth sampled path 绝对最小步长。
- `mm2_connect_gap_ratio`：连接 gap 的比例阈值。
- `mm2_connect_gap_abs_min`：连接 gap 的绝对最小阈值。
- `mm2_sweep_thickness_scale`：sweep wall profile 半宽相对 pair 厚度的比例。
- `mm2_sweep_thickness_min`：sweep wall profile 半宽下限。
- `mm2_path_end_extend_ratio`：path 端部延长比例。
- `mm2_path_end_extend_abs_min`：path 端部延长绝对最小值。
- `mm2_path_end_tbar_ratio`：T-bar 长度比例。

## Result 和 State

### `Step4RelationResult`

```cpp
struct Step4RelationResult
{
    logical ok;
    Step4RelationState state;
};
```

- `ok`：Step4 是否完成。当前只要输入有效并完成关系构建，就为 `TRUE`。
- `state`：Step4 完整输出状态。

### `Step4RelationState`

```cpp
struct Step4RelationState
{
    const Step3PairState* input_step3;
    Step4RelationOptions options_snapshot;
    PairRelationTable pair_relations;
    PairWallRelationTable pair_wall_relations;
    WallTable walls;
    RelationBuildStats stats;
};
```

- `input_step3`：指向本次 Step4 使用的 Step3 state。
- `options_snapshot`：Step4 本次运行使用的参数快照。
- `pair_relations`：pair-pair relation 列表。
- `pair_wall_relations`：pair-wall relation 列表。
- `walls`：Step4 生成的 virtual wall body 列表。注意 Step3 原有 wall candidate 不直接复制到这里。
- `stats`：Step4 统计信息。

### `PairRelationTable`

```cpp
struct PairRelationTable
{
    std::vector<PairRelationRecord> relations;
};
```

- `relations`：pair-pair 关系记录。

### `PairRelationRecord`

```cpp
struct PairRelationRecord
{
    int relation_id;
    int pair_a;
    int pair_b;
    std::string relation_type;
    std::string pair_mode;
    std::string classify_source;
    std::string source;
    std::string wall_mode;
    int hits_aa;
    int hits_ab;
    int hits_ba;
    int hits_bb;
    int total_hits;
    double hit_normal_cos_abs;
    double hit_normal_angle_deg;
};
```

- `relation_id`：Step4 分配的 relation 编号。
- `pair_a`：较小的 pair id。
- `pair_b`：较大的 pair id。
- `relation_type`：当前固定为 `pair-pair`。
- `pair_mode`：`MM1` 或 `MM2`。
- `classify_source`：分类依据，当前可能是 `hit_normal` 或 `pair_direction`。
- `source`：hit 来源，当前可能是 `group_adjacency`、`orphan_bridge` 或 `mixed`。
- `wall_mode`：预留字段。当前 pair-pair record 不主动设置。
- `hits_aa`：pair_a A 侧 group 与 pair_b A 侧 group 的 hit 数。
- `hits_ab`：pair_a A 侧 group 与 pair_b B 侧 group 的 hit 数。
- `hits_ba`：pair_a B 侧 group 与 pair_b A 侧 group 的 hit 数。
- `hits_bb`：pair_a B 侧 group 与 pair_b B 侧 group 的 hit 数。
- `total_hits`：四个 bucket 的 hit 总数。
- `hit_normal_cos_abs`：用于 MM 分类的 hit normal 绝对点积，不可用时为 `-1.0`。
- `hit_normal_angle_deg`：`acos(hit_normal_cos_abs)` 的角度值，不可用时为 `-1.0`。

### `PairWallRelationTable`

```cpp
struct PairWallRelationTable
{
    std::vector<PairWallRelationRecord> relations;
};
```

- `relations`：pair-wall 关系记录。

### `PairWallRelationRecord`

```cpp
struct PairWallRelationRecord
{
    int relation_id;
    int pair_id;
    int wall_group_id;
    int hit_group_a;
    int hit_group_b;
    std::string wall_mode;
};
```

- `relation_id`：Step4 分配的 relation 编号。
- `pair_id`：相关 pair id。
- `wall_group_id`：wall group id 或 Step4 virtual wall id。
- `hit_group_a`：wall group 与 pair.group_a 的 hit 数。Step4 为 MM2 virtual wall 补 relation 时该字段保持默认 0。
- `hit_group_b`：wall group 与 pair.group_b 的 hit 数。Step4 为 MM2 virtual wall 补 relation 时该字段保持默认 0。
- `wall_mode`：当前为 `MW1`。

### `WallTable`

```cpp
struct WallTable
{
    std::vector<WallRecord> walls;
};
```

- `walls`：Step4 为 MM2 pair link 生成的 virtual wall。

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

Step4 主要填充：

- `wall_id`：virtual wall id。当前从 Step2 group 数开始递增。
- `source_face_ids`：Step4 当前不填充。
- `body`：sweep 生成的 virtual wall sheet body。
- `source`：`mm2_sweep_partner`、`mm2_sweep_orphan` 或 `mm2_sweep_mixed`。
- `pair_a`：产生该 virtual wall 的 pair_a。
- `pair_b`：产生该 virtual wall 的 pair_b。
- `group_a`：sweep bucket 一侧 group。
- `group_b`：sweep bucket 另一侧 group。
- `bucket_slot`：MM2 hit bucket，`0=aa`、`1=ab`、`2=ba`、`3=bb`。
- `coedge_len`：bucket 中用于 sweep 的 coedge 长度统计。
- `half_profile`：sweep profile 半宽。
- `hit_normal_cos_abs`：bucket 两侧 hit normal 绝对点积。

### `RelationBuildStats`

```cpp
struct RelationBuildStats
{
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
```

- `relation_count`：pair-pair relation 数。
- `wall_count`：MW1 wall group 数加 Step4 virtual wall 数。
- `group_adjacency_count`：group adjacency pair 数。
- `coedge_total`：遍历的 coedge 总数。
- `coedge_with_partner`：有 partner 的 coedge 数。
- `coedge_partner_ring_gt2`：partner ring 大于 2 的 coedge 数。
- `unique_nonmanifold_edges`：非流形 edge 数。
- `partner_ring_max`：最大 partner ring 长度。
- `tiny_faces_found`：识别出的 tiny face 数。
- `tiny_partner_coedges`：partner ring 中遇到 tiny face 的 coedge 数。
- `tiny_patch_applied`：tiny face 穿透修正成功次数。
- `pair_wall_relation_count`：pair-wall relation 数。
- `orphan_wall_group_count`：参与 orphan bridge 检查的 wall group 数。
- `orphan_bridge_injected_count`：注入的 orphan bridge synthetic link 数。
- `direct_pair_link_count`：source 为纯 `group_adjacency` 的 pair link 数。
- `mm1_count`：MM1 pair link 数。
- `mm2_count`：MM2 pair link 数。
- `virtual_wall_try_count`：尝试构建 virtual wall 的 MM2 link 数。
- `virtual_wall_ok_count`：成功构建 virtual wall 数。

## 内部辅助结构

### `PairLinkAccum`

累积两个 pair 之间四个 side bucket 的 hit。

- `aa` / `ab` / `ba` / `bb`：四个 side bucket hit 数。
- `direct_hits`：来自 group adjacency 的 hit 数。
- `orphan_hits`：来自 orphan bridge 的 synthetic hit 数。

### `PairDirOverride`

保存 orphan bridge 推断出的 pair direction override。

- `dir_i` / `dir_j`：两个 pair 的方向。
- `has_i` / `has_j`：方向是否可用。

### `GroupAdjBuildStatsLocal`

group adjacency 构建阶段的局部统计，之后拷贝到 `RelationBuildStats`。

### `WallCoedgeTouch`

记录 wall coedge 触碰到的 pair side。

- `pair_id`：触碰到的 pair。
- `side`：触碰到 pair 的 A/B side。
- `edge_len`：coedge 长度。
- `src_coedge`：来源 coedge。
- `has_dir` / `dir`：coedge 方向是否可用及方向。

### `OrphanBridgeStats`

orphan bridge 阶段统计。

- `orphan_wall_groups`
- `wall_faces_total`
- `injected_links`
- `face_dedup_skip`

### `Mm2HitBucket`

保存 MM2 sweep 使用的 hit bucket。

- `slot`：bucket 编号，`0=aa`、`1=ab`、`2=ba`、`3=bb`。
- `group_i` / `group_j`：bucket 两侧 group。
- `from_orphan_recorded`：是否包含 orphan bridge 记录来源。
- `from_partner_collected`：是否包含 partner ring 收集来源。
- `hit_weight`：bucket hit 权重。
- `len_sum`：coedge 长度总和。
- `longest_len`：最长 coedge 长度。
- `coedges`：bucket coedge 列表。
- `coedge_owner_mask`：coedge 属于哪侧 group 的 owner mask。

### `Mm2SweepWallBuild`

保存一次 virtual wall 构建结果。

- `body`：生成的 sheet body。
- `group_i` / `group_j`：bucket 两侧 group。
- `bucket_slot`：bucket 编号。
- `from_orphan_recorded`
- `from_partner_collected`
- `coedge_len`
- `half_profile`
- `hit_normal_cos_abs`

## 函数说明

### `RunStep4RelationBuild`

Step4 外部入口。

流程：

1. 重置 `result`。
2. 保存 `&step3` 到 `result.state.input_step3`。
3. 保存 options 快照。
4. 输出 start event。
5. 检查 `step3.input_step2`，缺失则输出 `invalid_input` 并返回 `FALSE`。
6. 调用 `BuildFaceToGroupMap`。
7. 调用 `BuildGroupAdjacencyHitsByPartner` 构建 group adjacency。
8. 构建 MW1 pair-wall relation。
9. 从 group adjacency 构建 pair-pair hit accumulator。
10. 可选调用 `BuildSyntheticPairHitsFromOrphanWalls`。
11. 过滤 hit 数不足的 pair link。
12. 分类 MM1/MM2。
13. 对 MM2 调用 `BuildVirtualWallBodiesFromMm2LinkSweep`。
14. 写入 virtual wall 和补充 pair-wall relation。
15. 填充 stats，设置 `result.ok = TRUE`。
16. 输出 finish event。

### `BuildFaceToGroupMap`

从 Step2 groups 的 `faces` 字段建立 `FACE* -> group_id` 映射。

### `BuildGroupAdjacencyHitsByPartner`

遍历所有 group face 的 loop/coedge，通过 coedge partner ring 找相邻 face 所属 group，统计 group-to-group hit。

当 `enable_tiny_face_partner_patch` 开启时，会调用 tiny face 相关逻辑尝试穿透 tiny face 找真实 hit group。

### `BuildTinyFaceSet`

识别 tiny face。当前规则：

```text
longest_edge^2 >= 1000.0 * area
```

### `ResolveTinyPartnerHitGroup`

当 coedge partner 指向 tiny face 时，收集 tiny face 另一侧邻居 group，并按采样点到候选 group 的平均距离选择真实 hit group。

### `BuildSyntheticPairHitsFromOrphanWalls`

遍历没有形成 MW1 的 orphan wall group，从 wall face 的 coedge touch 中收集 pair side，向 pair link accumulator 注入 synthetic hit。

### `EmitOrphanBridgeEvent`

输出 orphan bridge detail event。

### `SelectDominantHitGroupPair`

从 pair link 的四个 bucket 中选择主导 hit group pair，用于 hit normal 分类。

### `ComputeHitNormalCosForGroupPair`

对两个相邻 group 的 hit coedge 采样，计算两侧 face normal 的绝对点积，作为 MM1/MM2 分类依据。

### `ClassifyPairModeByAngleFallback`

当 hit normal 不可用时，用 pair direction 夹角兜底分类 MM1/MM2。

### `CollectMm2HitBuckets`

根据 MM2 pair link 的四个 bucket 收集用于 sweep 的 hit coedge。

### `SelectPrimaryMm2Bucket`

在多个 MM2 bucket 中选择主 bucket。优先长度总和更大者，长度相同时选 hit weight 更大者。

### `BuildVirtualWallBodiesFromMm2LinkSweep`

MM2 virtual wall 构建入口。

流程：

1. 初始化 ACIS sweeping。
2. 调用 `CollectMm2HitBuckets`。
3. 按配置选择全部 bucket 或主 bucket。
4. 对每个 bucket 调用 `BuildSweepWallsFromBucket`。
5. 计算 `hit_normal_cos_abs` 并输出构建结果。

### `BuildSweepWallsFromBucket`

把 bucket coedge 转换为 sweep path，按配置尝试连接、平滑、逐 edge sweep 或 fallback sweep。

### `EmitVirtualWallEvent`

输出 virtual wall 成功或失败事件。

## 日志输出

所有 Step4 事件基础 tag：

```text
step4
relation
```

### start event

tags：

```text
step4
relation
start
```

properties：

- `pair_count`：Step3 pair 数。
- `group_count`：Step2 group 数。仅 `step3.input_step2` 存在时输出。

### invalid_input

tags：

```text
step4
relation
invalid_input
```

properties：

- `reason`：当前为 `missing_step2_state`。

### group-adjacency summary

tags：

```text
step4
relation
summary
stage
group-adjacency
finish
```

properties：

- `group_adjacency_count`
- `coedge_total`
- `coedge_with_partner`
- `coedge_partner_ring_gt2`
- `unique_nonmanifold_edges`
- `partner_ring_max`
- `tiny_faces_found`
- `tiny_partner_coedges`
- `tiny_patch_applied`

当 tiny face 相关统计非零时，还会输出：

```text
step4
relation
summary
stage
tiny-face-patch
```

### pair-wall event

tags：

```text
step4
relation
summary
single
pair-wall
```

properties：

- `relation_id`
- `pair_id`
- `wall_group_id`
- `hit_group_a`
- `hit_group_b`
- `wall_mode`

### pair-wall stage summary

tags：

```text
step4
relation
summary
stage
pair-wall
finish
```

properties：

- `checked_count`
- `accepted_count`
- `rejected_count`
- `wall_group_count`

### pair-link candidate detail

tags：

```text
step4
relation
detail
pair-link
```

properties：

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

### pair-link event

tags：

```text
step4
relation
summary
single
pair-link
```

properties：

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

### pair-link stage summary

tags：

```text
step4
relation
summary
stage
pair-link
finish
```

properties：

- `candidate_link_count`
- `accepted_link_count`
- `rejected_link_count`
- `direct_hit_count`
- `orphan_hit_count`
- `mm1_count`
- `mm2_count`

### orphan-bridge stage summary

tags：

```text
step4
relation
summary
stage
orphan-bridge
finish
```

properties：

- `orphan_wall_group_count`
- `wall_faces_total`
- `injected_link_count`
- `face_dedup_skip`

### virtual-wall event

成功时 tags：

```text
step4
relation
summary
single
virtual-wall
ok
```

失败时 tags：

```text
step4
relation
detail
virtual-wall
fail
```

properties：

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

### virtual-wall stage summary

tags：

```text
step4
relation
summary
stage
virtual-wall
finish
```

properties：

- `try_count`
- `ok_count`
- `fail_count`

### finish event

tags：

```text
step4
relation
summary
all
finish
```

properties：

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

## Debug SAT

Step4 当前没有专属 debug SAT role。MM2 virtual wall body 保存在 `Step4RelationState.walls` 中，供后续 Step5 读取。
