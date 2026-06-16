# SamplingUtils 工程文档

本文记录 `midface_new/utils/SamplingUtils.hpp/.cpp` 的公共采样工具。它的目的不是定义某个 Step 的业务算法，而是把“从 ACIS face 上生成或投影采样点”的底层动作收拢到一处，避免 Step2、Step3、后续 Step4 重复手写。

## 设计边界

- `SamplingUtils` 只生成采样点和做基础去重、投影、edge 收集。
- 它不判断 group 是否合并，不判断 pair 是否接受，也不写日志。
- Step 级日志仍由调用方负责，因为只有调用方知道 candidate、source/target、gate、score 等业务语义。
- 采样失败时尽量返回明确 reason；有代表点时会用 representative point 兜底。

## Options

### `SampleTargetOptions`

```cpp
struct SampleTargetOptions
{
    double density;
    int min_count;
    int max_count;
};
```

职责：

- 描述“面积代理值 -> 目标采样数”的公共规则。
- 当前公式由 `ComputeSampleTarget` 执行：`ceil(density * sqrt(area_proxy))`，再 clamp 到 `[min_count, max_count]`。

### `BoundarySampleOptions`

```cpp
struct BoundarySampleOptions
{
    SampleTargetOptions target;
    int min_samples_per_edge;
    double extra_sample_alpha;
    int max_samples_per_face;
    double edge_length_eps;
    double dedup_tol2;
    const char* role;
};
```

职责：

- 控制沿 face edge 的 boundary samples。
- `min_samples_per_edge` 保证每条有效 edge 至少有若干点。
- `extra_sample_alpha` 根据目标采样数分配额外预算，按 `EDGE::length(TRUE)` 返回的 edge length 加权。
- `max_samples_per_face` 是单 face 上限。
- `edge_length_eps` 用于过滤退化 edge。
- `dedup_tol2` 是平方距离去重阈值。
- `role` 写入 `PointSample.role`，用于调用方后续诊断或 JSON 输出。

当前消费者：

- Step2 `BuildSourceSamples`：生成小面 source face 的 boundary samples，用于 source-to-target 三 gate 精筛。

### `InteriorSampleOptions`

```cpp
struct InteriorSampleOptions
{
    SampleTargetOptions target;
    int candidate_multiplier;
    int candidate_max;
};
```

职责：

- 控制 bbox + Halton-like candidate 的 interior samples。
- `candidate_multiplier` 表示先生成目标采样数的若干倍候选，再抽稀。
- `candidate_max` 限制候选数量，避免大面采样爆炸。

当前消费者：

- Step3 `CollectGroupSamples`：为 group 内每个 face 临时生成 interior samples，用于 group-to-group 双向 pairing refine。

## 公共函数

### `ComputeSampleTarget`

```cpp
int ComputeSampleTarget(double area_proxy, const SampleTargetOptions& options);
```

输入：

- `area_proxy`：face 或 group 的面积代理值。
- `options`：采样密度和上下限。

输出：

- 返回至少为 1 的目标采样数。

副作用：

- 无。

### `CollectFaceEdges`

```cpp
logical CollectFaceEdges(FACE* face, std::vector<EDGE*>& out_edges);
```

输入：

- `FACE* face`：ACIS face。

输出：

- `out_edges`：通过 `api_get_edges` 收集出的 `EDGE*` 列表。

失败：

- `face == nullptr` 或 ACIS API 失败时返回 `FALSE`。

### `ProjectPointToFace`

```cpp
logical ProjectPointToFace(FACE* face, const SPAposition& guess, SPAposition& out_point);
```

职责：

- 调用 `api_find_cls_ptto_face` 把一个 guess 点投影到 trimmed face 上的最近点。

说明：

- 只负责投影，不负责判断这个点是否满足 Step2/Step3 gate。

### `IsNearAnyPoint` / `IsNearAnyPointSample`

```cpp
logical IsNearAnyPoint(const SPAposition& p, const std::vector<SPAposition>& samples, double tol2);
logical IsNearAnyPointSample(const SPAposition& p, const std::vector<PointSample>& samples, double tol2);
```

职责：

- 按平方距离做简单去重。
- `IsNearAnyPoint` 面向 `SPAposition` 列表。
- `IsNearAnyPointSample` 面向带 `face_id/normal/role` 的 `PointSample` 列表。

### `AddPointSample`

```cpp
void AddPointSample(
    int face_id,
    const SPAposition& point,
    const SPAunit_vector& normal,
    const char* role,
    std::vector<PointSample>& out_samples);
```

职责：

- 统一构造 `PointSample`。
- `normal` 当前由调用方传入，通常是 Step1 的 representative normal。

### `BuildBoundaryPointSamples`

```cpp
logical BuildBoundaryPointSamples(
    const FaceRecord& source,
    const BoundarySampleOptions& options,
    std::vector<PointSample>& out_samples,
    std::string& out_reason);
```

流程：

1. 收集 `source.face` 的 edge。
2. 按 `SampleTargetOptions` 得到目标采样数。
3. 每条有效 edge 至少采样 `min_samples_per_edge` 个点。
4. 额外采样预算按 `EDGE::length(TRUE)` 返回的 edge length 分配。
5. 每条 edge 优先调用 `api_arc_len_samples_from_edges` 生成等弧长点。
6. 弧长采样失败时，才回退到旧的 `get_curve_ends` + `api_entity_point_distance` 投影采样。
7. 采样点统一通过 `ProjectPointToFace` 投影到 trimmed face。
8. 按 `dedup_tol2` 去重后加入 `out_samples`。
9. 若没有 edge 或没有生成样本，但 Step1 有 representative point，则返回 representative fallback。

输出：

- `out_samples`：带 face id、点、normal、role 的样本。
- `out_reason`：`ok`、`fallback_representative`、`missing_source_face`、`no_edges`、`no_samples` 等。

当前调用：

- Step2 `BuildSourceSamples`。

### `BuildInteriorPositionSamples`

```cpp
logical BuildInteriorPositionSamples(
    const FaceRecord& record,
    const InteriorSampleOptions& options,
    std::vector<SPAposition>& out_samples,
    std::string& out_reason);
```

流程：

1. 读取 face bbox。
2. 先尝试 bbox center 投影到 face。
3. 使用 2/3/5 基底的 Halton-like 序列在 bbox 内生成 guess 点。
4. 每个 guess 点通过 `ProjectPointToFace` 投影到 trimmed face。
5. 去重后收集候选。
6. 候选多于目标数时按 stride 抽稀。
7. 若无样本但有 representative point，则 fallback。

输出：

- `out_samples`：只包含 `SPAposition`，不包含 face id。调用方需要自己保留样本来自哪个 face。
- `out_reason`：`ok`、`fallback_representative`、`missing_face`、`no_samples` 等。

当前调用：

- Step3 `CollectGroupSamples`。

## ACIS API

当前使用的 ACIS API：

- `api_get_edges`：收集 face 的 edge。
- `EDGE::length(TRUE)`：读取 edge length，用于 extra sample 权重。
- `api_arc_len_samples_from_edges`：按 edge 弧长生成等距 boundary samples。
- `get_curve_ends`：弧长采样失败时读取 edge 两端点，用于 fallback。
- `api_entity_point_distance`：弧长采样失败时把 guess 点贴到 edge。
- `api_find_cls_ptto_face`：把点投影到 trimmed face。
- `get_face_box`：生成 interior sample 的 bbox。

这些 API 的行为如果后续发现对某类曲面不稳定，应在 `SamplingUtils` 内统一兜底，而不是在每个 Step 单独补丁。

## 后续约定

- Step4 如果需要临时采样，优先复用 `SamplingUtils`。
- 如果需要输出 sample JSON，不在 `SamplingUtils` 内写日志，由业务 Step 根据自己的 candidate id、face/group/pair 语义组织事件。
- 如果未来 Step1 checkpoint 要持久化 samples，也应在 Step1 或 checkpoint 工具层处理，不把持久化塞进 `SamplingUtils`。
