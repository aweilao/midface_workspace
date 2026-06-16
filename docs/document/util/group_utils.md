# GroupUtils 工程文档

源码事实源：

- `midface/utils/GroupUtils.hpp`
- `midface/utils/GroupUtils.cpp`
- `midface/utils/SamplingUtils.hpp`

## 作用

`GroupUtils` 提供基于 Step2 group 的常用计算：

- group 面积和尺度。
- face/group 宽度尺度。
- 从 Step2 state 中按 face id 找 `FaceRecord`。
- 收集 group 内部采样点。
- 查询点到 group 的最近点。

当前主要调用方是 Step3 pairing。

## 数据结构

### `GroupClosestPointResult`

```cpp
struct GroupClosestPointResult
{
    logical found;
    SPAposition point;
    FACE* face;
    int face_id;
    double distance;
};
```

- `found`：是否找到最近点。
- `point`：最近点位置。
- `face`：最近点所在 face。
- `face_id`：最近点所在 Step1 face id。
- `distance`：查询点到最近点距离。

## 函数说明

### `GroupAreaSafe`

```cpp
double GroupAreaSafe(const GroupRecord& group);
```

返回：

```text
max(group.area_sum, 1.0e-12)
```

### `GroupScale`

```cpp
double GroupScale(const GroupRecord& group);
```

返回：

```text
sqrt(GroupAreaSafe(group))
```

### `FaceWidthScale`

```cpp
double FaceWidthScale(const FaceRecord& record);
```

估算 face 局部宽度：

1. 如果有 edge 长度且 `edge_max > 1e-12`，优先返回 `area_proxy / edge_max`。
2. 否则如果有 `edge_min`，返回 `edge_min`。
3. 否则如果有面积代理，返回 `sqrt(area_proxy)`。
4. 否则返回 `1.0e-6`。

### `FindFaceRecord`

```cpp
const FaceRecord* FindFaceRecord(const Step2GroupState& step2, int face_id);
```

从 `step2.input_step1->faces.records` 中线性查找指定 `face_id`。

如果 `step2.input_step1 == nullptr` 或找不到，返回 `nullptr`。

### `GroupWidthScale`

```cpp
double GroupWidthScale(const GroupRecord& group, const Step2GroupState& step2);
```

流程：

1. 遍历 `group.face_ids`。
2. 对每个有效 face 调用 `FaceWidthScale`。
3. 返回这些宽度的 0.50 分位数。
4. 如果没有可用宽度，返回 `GroupScale(group)`。

### `CollectGroupInteriorSamples`

```cpp
void CollectGroupInteriorSamples(
    const GroupRecord& group,
    const Step2GroupState& step2,
    const InteriorSampleOptions& sample_options,
    std::vector<std::pair<const FaceRecord*, SPAposition> >& out_samples);
```

流程：

1. 遍历 group 内 face ids。
2. 调用 `FindFaceRecord`。
3. 跳过无效 face。
4. 对每个 face 调用 `BuildInteriorPositionSamples`。
5. 输出 `(FaceRecord*, SPAposition)`，保留 sample 属于哪个 face。

### `FindClosestPointOnGroup`

```cpp
logical FindClosestPointOnGroup(
    const GroupRecord& group,
    const Step2GroupState& step2,
    const SPAposition& point,
    GroupClosestPointResult& out_result);
```

流程：

1. 遍历 group 内 face ids。
2. 查找 `FaceRecord`。
3. 调用 `api_entity_point_distance(record->face, query, cp, d)`。
4. 保留距离最小的结果。

返回值为 `out_result.found`。

## 当前边界

- `FindFaceRecord` 是线性查找，没有使用 Step2 的 `face_to_group` 或额外索引。
- `FindClosestPointOnGroup` 遍历 group 内所有 face，group 很大时成本较高。
- `GroupWidthScale` 使用 face width 中位数，是启发式尺度。
