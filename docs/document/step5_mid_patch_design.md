# Step5 Mid Patch 工程文档

本文记录当前 `midface` Step5 的实现。源码事实源：

- `midface/steps/Step5MidPatchBuild.hpp`
- `midface/steps/Step5MidPatchBuild.cpp`
- `midface/steps/Step4RelationBuild.hpp`
- `midface/core/MidSurfaceNewTypes.hpp`
- `midface/core/MidSurfaceNewTypes.cpp`
- `midface/MidSurface.cpp`

## 目标和算法

Step5 输入 Step4 的 `Step4RelationState`，根据 Step3 pair 和 Step4 pair-pair relation 生成中面 patch 记录，并尝试构造每个 patch 的 ACIS `FACE*`。

Step5 做的事情：

- 从 Step4 读取 Step3 和 Step2 上游 state。
- 将 Step2 group 转成 Step5 内部 group view。
- 根据 Step4 pair relation 建 pair link 和 pair 邻接图。
- 对 pair 邻接图做连通分量。
- 为每个 Step3 pair 创建 `MidPatchRecord`。
- 可选把一对多关系合并成 merged unit patch。
- 根据 pair relation 生成 `MidPatchJunctionRecord`。
- 对每个 patch 尝试构造几何 face。
- 输出 Step5 patch face debug SAT。

Step5 不做的事情：

- 不做 trim。
- 不选择最终保留 face。
- 不做 stitch。

Step5 主流程：

```text
RunMidSurfaceBodyWithContext
  -> RunStep5MidPatchBuild(result.step4.state, config.step5, diagnostics, result.step5)
    -> check step4.input_step3 and step3.input_step2
    -> copy options into runtime clamps
    -> BuildGroupViews
    -> BuildPairLinks
    -> BuildConnectedComponents
    -> optional one-to-n merged units
    -> BuildOnePatch for remaining pairs
    -> build MidPatchJunctionRecord from pair links
    -> BuildAnalyticMidFacePure or BuildFreeformMidFacePure
    -> EmitPatchEvent
    -> EmitFaceSetSat("step5.mid_patch_faces")
    -> EmitFinishEvent
```

## 参数设置

`Step5MidPatchOptions` 定义在 `Step5MidPatchBuild.hpp`，默认值在 `Step5MidPatchBuild.cpp`。

```cpp
struct Step5MidPatchOptions
{
    logical build_sheet_body = TRUE;
    logical emit_patch_events = TRUE;
    int min_component_pairs = 1;
    logical keep_isolated_pairs = TRUE;
    logical enable_one_to_n_merge_units = FALSE;
    logical spline_fit_only = FALSE;
    double spline_fit_tol_scale = 1.0;
    double spline_fit_tol_min = 1e-6;
    double spline_fit_tol_max = 5e-2;
    int spline_grid_min = 4;
    int spline_grid_max = 9;
    int freeform_uv_init_u = 6;
    int freeform_uv_init_v = 6;
    int freeform_uv_refine_rounds = 3;
    double freeform_uv_refine_angle_deg = 20.0;
    int freeform_uv_max_u = 28;
    int freeform_uv_max_v = 28;
    double freeform_uv_fail_ratio_max = 0.45;
    int freeform_uv_min_valid_points = 16;
    double analytic_patch_inflate = 1.10;
    double analytic_min_half_extent = 1e-3;
};
```

- `build_sheet_body`：保留配置字段。当前 Step5 主要生成 patch face，未直接由该字段分支。
- `emit_patch_events`：是否输出 patch、component、junction、geometry 日志。
- `min_component_pairs`：pair 连通分量最小 pair 数。
- `keep_isolated_pairs`：是否保留单 pair 分量。
- `enable_one_to_n_merge_units`：是否启用一对多 pair 合并成 merged unit patch。
- `spline_fit_only`：是否强制使用 spline/freeform 构造路径。
- `spline_fit_tol_scale`：spline fitting 容差比例。
- `spline_fit_tol_min`：spline fitting 容差下限。
- `spline_fit_tol_max`：spline fitting 容差上限。
- `spline_grid_min`：spline 网格最小尺寸。
- `spline_grid_max`：spline 网格最大尺寸。
- `freeform_uv_init_u`：freeform UV 初始 U 方向采样数。
- `freeform_uv_init_v`：freeform UV 初始 V 方向采样数。
- `freeform_uv_refine_rounds`：freeform UV 细化轮数。
- `freeform_uv_refine_angle_deg`：freeform UV 细化角度阈值。
- `freeform_uv_max_u`：freeform UV U 方向最大采样数。
- `freeform_uv_max_v`：freeform UV V 方向最大采样数。
- `freeform_uv_fail_ratio_max`：freeform 采样失败比例上限。
- `freeform_uv_min_valid_points`：freeform fitting 最少有效点数。
- `analytic_patch_inflate`：analytic patch 边界放大系数，运行时 clamp 到 `[1.0, 3.0]`。
- `analytic_min_half_extent`：analytic patch 半尺寸下限，运行时至少为 `1e-6`。

## Result 和 State

### `Step5MidPatchResult`

```cpp
struct Step5MidPatchResult
{
    logical ok;
    Step5MidPatchState state;
};
```

- `ok`：Step5 是否完成。当前输入有效后，即使没有 pair 也会返回 `TRUE`。
- `state`：Step5 完整输出状态。

### `Step5MidPatchState`

```cpp
struct Step5MidPatchState
{
    const Step4RelationState* input_step4;
    Step5MidPatchOptions options_snapshot;
    MidPatchTable patches;
    MidPatchJunctionTable junctions;
    MidPatchComponentTable components;
    MidPatchBuildStats stats;
};
```

- `input_step4`：指向本次 Step5 使用的 Step4 state。
- `options_snapshot`：Step5 本次运行使用的参数快照。
- `patches`：Step5 生成的 patch 列表。
- `junctions`：patch/pair 之间的 junction 关系。
- `components`：pair 邻接图的连通分量，每个分量是 pair id 列表。
- `stats`：Step5 统计信息。

### `MidPatchTable`

```cpp
struct MidPatchTable
{
    std::vector<MidPatchRecord> patches;
};
```

- `patches`：中面 patch 记录列表。

### `MidPatchRecord`

```cpp
struct MidPatchRecord
{
    int patch_id;
    int source_pair_id;
    int group_a;
    int group_b;
    int anchor_group;
    int component_id;
    logical merged_unit;
    std::vector<int> member_pairs;
    MidPatchKind kind;
    std::string type_a;
    std::string type_b;
    MidPatchSideGroupType side_type_a;
    MidPatchSideGroupType side_type_b;
    logical valid;
    logical is_rib;
    logical is_uncertain;
    double thickness;
    SPAposition point_a;
    SPAposition point_b;
    SPAposition point_mid;
    SPAunit_vector normal_mid;
    FACE* face;
    BODY* sheet_body;
    std::string build_method;
    std::string fail_reason;
};
```

- `patch_id`：Step5 分配的 patch 编号。
- `source_pair_id`：代表该 patch 的 Step3 pair id。
- `group_a`：source pair 的 group_a。
- `group_b`：source pair 的 group_b。
- `anchor_group`：一对多合并时的公共 group；普通 patch 为 `-1`。
- `component_id`：所属 pair 连通分量 id。
- `merged_unit`：是否是一对多合并 patch。
- `member_pairs`：该 patch 覆盖的 pair id 列表。
- `kind`：patch 几何类型，可能是 plane、cylinder、sphere、analytic、spline、mixed 等。
- `type_a`：A 侧 group type。
- `type_b`：B 侧 group type。
- `side_type_a`：A 侧是 single 还是 mixed。
- `side_type_b`：B 侧是 single 还是 mixed。
- `valid`：patch record 是否有效。
- `is_rib`：是否来自 rib candidate pair。
- `is_uncertain`：是否来自 uncertain pair。
- `thickness`：来源 pair 的厚度。
- `point_a`：来源 pair 的 A 端代表点。
- `point_b`：来源 pair 的 B 端代表点。
- `point_mid`：`point_a` 和 `point_b` 的中点。
- `normal_mid`：从 `point_a` 指向 `point_b` 的方向，或 fallback group normal。
- `face`：构造出的中面 `FACE*`。
- `sheet_body`：预留 sheet body 字段。当前 Step5 主要填 `face`。
- `build_method`：几何构造方法名。
- `fail_reason`：构造失败原因。

### `MidPatchJunctionTable`

```cpp
struct MidPatchJunctionTable
{
    std::vector<MidPatchJunctionRecord> junctions;
};
```

- `junctions`：来自 Step4 pair relation 的 patch/pair 连接关系。

### `MidPatchJunctionRecord`

```cpp
struct MidPatchJunctionRecord
{
    int junction_id;
    int pair_i;
    int pair_j;
    MidPatchLinkType link_type;
    MidPatchConnectClass conn_class;
    MidPatchPairMode pair_mode;
    int total_hits;
};
```

- `junction_id`：junction 编号。
- `pair_i`：一侧代表 pair id。
- `pair_j`：另一侧代表 pair id。
- `link_type`：连接类型，例如 same side、cross side、mixed、rib T。
- `conn_class`：连接类别，当前由 MM1/MM2 映射而来。
- `pair_mode`：Step4 relation 的 pair mode，MM1 或 MM2。
- `total_hits`：该连接累积 hit 数。

### `MidPatchComponentTable`

```cpp
struct MidPatchComponentTable
{
    std::vector< std::vector<int> > components;
};
```

- `components`：pair 邻接图连通分量。每个子 vector 保存一个分量中的 pair id。

### `MidPatchBuildStats`

```cpp
struct MidPatchBuildStats
{
    int input_pair_count;
    int component_count;
    int requested_patch_count;
    int built_patch_count;
    int failed_patch_count;
    int junction_count;
    int merged_unit_count;
    int merged_pair_count;
};
```

- `input_pair_count`：Step3 pair 数。
- `component_count`：保留下来的 pair 连通分量数。
- `requested_patch_count`：请求构造的 patch 数。
- `built_patch_count`：成功构造出 `FACE*` 的 patch 数。
- `failed_patch_count`：构造失败的 patch 数。
- `junction_count`：junction 数。
- `merged_unit_count`：一对多合并 patch 数。
- `merged_pair_count`：被 merged unit 覆盖的 pair 数。

## 函数说明

### `RunStep5MidPatchBuild`

Step5 外部入口。

流程：

1. 重置 `result`。
2. 保存 `&step4` 和 options 快照。
3. 检查 `step4.input_step3` 和 `step3.input_step2`。
4. 将 options 写入运行时参数并做 clamp。
5. 调用 `BuildGroupViews`。
6. 读取 pair_count；若没有 pair，返回 `TRUE`。
7. 调用 `BuildPairLinks` 和 `BuildConnectedComponents`。
8. 根据 `min_component_pairs`、`keep_isolated_pairs` 过滤分量。
9. 可选构造 one-to-n merged unit patch。
10. 为剩余 pair 调用 `BuildOnePatch`。
11. 根据 pair links 构造 junctions。
12. 遍历 patch，调用 analytic 或 freeform 构造函数生成 face。
13. 输出 patch events、stage summary、debug SAT 和 finish event。

### `BuildGroupViews`

从 Step2 `GroupRecord` 提取 faces、seed point、seed normal 和 type，构造 Step5 内部 group view。

### `BuildPairLinks`

读取 Step4 `pair_relations`，生成 Step5 pair link view，并构建 pair 邻接表。

### `BuildConnectedComponents`

对 pair 邻接表做 BFS，输出 pair 连通分量。

### `BuildOnePatch`

根据一个 Step3 `PairRecord` 构造 `MidPatchRecord` 的基础字段。

主要逻辑：

- 保存 source pair、group、member pairs、rib/uncertain、thickness。
- 根据 group type 推导 patch kind。
- 用 pair 两端点计算 `point_mid` 和 `normal_mid`。
- 若 pair 端点退化，则使用 group seed point 兜底。

### `BuildAnalyticMidFacePure`

对非 freeform 类型尝试 analytic 中面构造。内部会根据 patch kind 尝试 plane、cylinder、sphere 或 dominant offset 等构造路径。

### `BuildFreeformMidFacePure`

对 freeform 类型构造中面 face。内部使用 UV 中点采样和 spline fitting。

### `EmitPatchEvent`

输出单个 patch 的详细 summary。

### `EmitStageSummary`

输出 component、patch-unit、junction、geometry 阶段 summary。

### `EmitFinishEvent`

输出 Step5 finish summary。

## 日志输出

所有 Step5 事件基础 tag：

```text
step5
mid-patch
```

### invalid input finish

输入缺失时输出：

```text
step5
mid-patch
summary
all
finish
fail
```

properties：

- `reason`：当前为 `missing_step3_or_step2_input`。

### patch event

tags：

```text
step5
mid-patch
summary
single
patch
ok|fail
```

properties：

- `patch_id`
- `source_pair_id`
- `group_a`
- `group_b`
- `anchor_group`
- `component_id`
- `merged_unit`
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

json_properties：

- `member_pairs`
- `point_mid`
- `normal_mid`

### stage summary

tags：

```text
step5
mid-patch
summary
stage
<stage>
finish
```

当前 stage：

- `component`：`input_pair_count`、`component_count`、`raw_component_count`。
- `patch-unit`：`requested_patch_count`、`merged_unit_count`、`merged_pair_count`。
- `junction`：`input_link_count`、`junction_count`。
- `geometry`：`requested_patch_count`、`built_patch_count`、`failed_patch_count`、`built_face_count`。

### finish event

tags：

```text
step5
mid-patch
summary
all
finish
```

properties：

- `input_pair_count`
- `component_count`
- `requested_patch_count`
- `built_patch_count`
- `failed_patch_count`
- `junction_count`
- `merged_unit_count`
- `merged_pair_count`

### debug SAT

role：

```text
step5.mid_patch_faces
```

输出条件：`RunContextOptions.debug_sat_outputs` 包含该 role。

内容：Step5 成功构造的 mid patch faces。
