# GroupUtils 工程文档

本文记录 `midface_new/utils/GroupUtils.hpp/.cpp` 的公共 group 操作。它只放和 group 数据访问、group 尺度、group 采样、group 最近点相关的基础能力，不包含 Step3 pair 接受/拒绝逻辑。

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

字段含义：

- `found`：是否找到最近点。
- `point`：目标 group 上的最近点。
- `face`：最近点所在的 `FACE*`。
- `face_id`：最近点所在 face 的 Step1 face id。
- `distance`：query point 到最近点的距离。

## 函数

### `GroupAreaSafe`

```cpp
double GroupAreaSafe(const GroupRecord& group);
```

返回 `max(group.area_sum, 1e-12)`，避免后续尺度计算除零。

### `GroupScale`

```cpp
double GroupScale(const GroupRecord& group);
```

返回 `sqrt(GroupAreaSafe(group))`，表示 group 的粗尺度。

### `FaceWidthScale`

```cpp
double FaceWidthScale(const FaceRecord& record);
```

当前逻辑：

```text
if edge_max valid:
  width = area_proxy / edge_max
else if edge_min valid:
  width = edge_min
else:
  width = sqrt(area_proxy)
```

含义：

- 主要用于估计一个 face 的短方向局部宽度。
- 对长方形面，`area / max_edge` 近似等于短边宽度。

### `FindFaceRecord`

```cpp
const FaceRecord* FindFaceRecord(const Step2GroupState& step2, int face_id);
```

从 `step2.input_step1->faces.records` 中按 `face_id` 查找 Step1 的 `FaceRecord`。

### `GroupWidthScale`

```cpp
double GroupWidthScale(const GroupRecord& group, const Step2GroupState& step2);
```

流程：

1. 遍历 group 内 face id。
2. 通过 `FindFaceRecord` 找到 `FaceRecord`。
3. 对每个有效 face 调用 `FaceWidthScale`。
4. 返回这些 width 的中位数。
5. 若没有有效 width，则返回 `GroupScale(group)`。

当前消费者：

- Step3 adaptive thickness gate：

```text
dist_rep <= pair_adaptive_dist_ratio * min(GroupWidthScale(A), GroupWidthScale(B)) * relax
```

### `CollectGroupInteriorSamples`

```cpp
void CollectGroupInteriorSamples(
    const GroupRecord& group,
    const Step2GroupState& step2,
    const InteriorSampleOptions& sample_options,
    std::vector<std::pair<const FaceRecord*, SPAposition> >& out_samples);
```

职责：

- 对 group 内每个有效 face 调用 `SamplingUtils::BuildInteriorPositionSamples`。
- 输出 `(FaceRecord*, sample_point)`，让调用方知道 sample 来自哪个 face。

当前消费者：

- Step3 `EvalDirectionalGroup`。

### `FindClosestPointOnGroup`

```cpp
logical FindClosestPointOnGroup(
    const GroupRecord& group,
    const Step2GroupState& step2,
    const SPAposition& point,
    GroupClosestPointResult& out_result);
```

职责：

- 遍历 group 内所有有效 face。
- 对每个 face 调用 `api_entity_point_distance`。
- 返回距离最小的 closest point。

当前消费者：

- Step3 `EvalDirectionalGroup`。

后续用途：

- 可用于 Step3 二阶段最近距离竞争后筛：比较一个 group 到多个候选 group 的最近距离，辅助剔除多余 pair。

## 边界

- `GroupUtils` 不写日志。
- `GroupUtils` 不判断 pair 是否接受。
- `GroupUtils` 不修改 ACIS model。
- 如果某个函数需要 Step3 特定参数，应由 Step3 先转成通用 options，再调用 `GroupUtils`。
