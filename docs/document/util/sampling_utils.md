# SamplingUtils 工程文档

源码事实源：

- `midface/utils/SamplingUtils.hpp`
- `midface/utils/SamplingUtils.cpp`

## 作用

`SamplingUtils` 提供 face 边界采样和内部采样。当前主要调用方：

- Step2：对 source face 做边界采样，用于 grouping refine。
- Step3：对 group 内 face 做内部采样，用于 pair refine。
- `GroupUtils`：调用内部采样生成 group samples。

## 参数结构

### `SampleTargetOptions`

```cpp
struct SampleTargetOptions
{
    double density;
    int min_count;
    int max_count;
};
```

默认值：

- `density = 1.0`
- `min_count = 1`
- `max_count = 1`

含义：

- `density`：按 `sqrt(area_proxy)` 计算目标点数的密度。
- `min_count`：目标点数下限。
- `max_count`：目标点数上限。

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

默认值：

- `min_samples_per_edge = 1`
- `extra_sample_alpha = 0.0`
- `max_samples_per_face = 1`
- `edge_length_eps = 1.0e-9`
- `dedup_tol2 = 1.0e-12`
- `role = "boundary"`

字段含义：

- `target`：目标总采样数。
- `min_samples_per_edge`：每条 edge 至少采几个点。
- `extra_sample_alpha`：按目标采样数追加的额外预算比例。
- `max_samples_per_face`：单 face 最大采样数。
- `edge_length_eps`：小于该长度的 edge 不参与长度权重。
- `dedup_tol2`：采样点去重距离平方阈值。
- `role`：写入 `PointSample.role` 的文本。

### `InteriorSampleOptions`

```cpp
struct InteriorSampleOptions
{
    SampleTargetOptions target;
    int candidate_multiplier;
    int candidate_max;
};
```

默认值：

- `candidate_multiplier = 4`
- `candidate_max = 240`

含义：

- `target`：最终目标采样数。
- `candidate_multiplier`：候选采样数量倍数。
- `candidate_max`：候选采样数量上限。

## 函数说明

### `ComputeSampleTarget`

```cpp
int ComputeSampleTarget(double area_proxy, const SampleTargetOptions& options);
```

公式：

```text
n = ceil(density * sqrt(max(area_proxy, 1e-12)))
return clamp(n, min_count, max_count)
```

### `CollectFaceEdges`

调用 `api_get_edges((ENTITY*)face, edge_entities)` 收集 `EDGE*`。

### `ProjectPointToFace`

调用 `api_find_cls_ptto_face(guess, face, out_point)`，把点投影到 face。

### `IsNearAnyPoint` / `IsNearAnyPointSample`

按距离平方阈值检查是否接近已有点，用于去重。

### `AddPointSample`

构造 `PointSample` 并追加到输出列表。

字段：

- `face_id`
- `point`
- `normal`
- `role`

### `BuildBoundaryPointSamples`

```cpp
logical BuildBoundaryPointSamples(
    const FaceRecord& source,
    const BoundarySampleOptions& options,
    std::vector<PointSample>& out_samples,
    std::string& out_reason);
```

流程：

1. 检查 `source.face`。
2. 调用 `CollectFaceEdges`。
3. 如果没有 edge 且有代表点，输出一个 `fallback_representative` sample。
4. 用 `ComputeSampleTarget` 计算目标采样数。
5. 根据 edge 数和 `min_samples_per_edge` 计算基础采样数。
6. 根据 `extra_sample_alpha` 和 edge 长度分配额外采样预算。
7. 每条 edge 优先调用 `api_arc_len_samples_from_edges` 做弧长采样。
8. 弧长采样失败时，用端点线性 guess + `api_entity_point_distance` 兜底。
9. 每个点投影回 face，并按 `dedup_tol2` 去重。
10. 如果最终没有 sample 且有代表点，再 fallback。

`out_reason` 当前可能是：

- `missing_source_face`
- `fallback_representative`
- `no_edges`
- `no_samples`
- `ok`

### `BuildInteriorPositionSamples`

```cpp
logical BuildInteriorPositionSamples(
    const FaceRecord& record,
    const InteriorSampleOptions& options,
    std::vector<SPAposition>& out_samples,
    std::string& out_reason);
```

流程：

1. 检查 `record.face`。
2. 读取 face bbox。
3. 优先把 bbox 中心投影到 face。
4. 用 2/3/5 低差异序列在 bbox 内生成候选点。
5. 候选点投影到 face。
6. 去重后收集到 `out_samples`。
7. 若采样数超过目标数，按 stride 降采样。
8. 如果没有 sample 且有代表点，使用代表点 fallback。

## 当前边界

- 边界采样中的 normal 使用 `source.representative_normal`，不是每个采样点局部 normal。
- 内部采样基于 bbox 投影，不保证均匀覆盖 trimmed face。
- `BuildInteriorPositionSamples` 只输出位置，不输出 normal。
