# ColorUtils 颜色分配与染色 API

本文档记录 `midface_new/utils/ColorUtils.hpp/.cpp` 的稳定职责。该工具只服务 `midface_new`，不影响旧 `midface`。

## 1. 设计分层

`ColorUtils` 拆成两类 API：

- 颜色分配 API：根据数量生成区分度较高的 RGB 颜色。
- 染色 API：把指定 RGB 颜色写到 ACIS `ENTITY*` 或 `FACE*`。

颜色分配不直接修改 ACIS 实体；染色 API 不决定颜色如何分配。

## 2. 数据结构

### `ColorRgb255`

```cpp
struct ColorRgb255
{
    int r;
    int g;
    int b;
};
```

字段含义：

- `r`：红色通道，范围 `[0,255]`。
- `g`：绿色通道，范围 `[0,255]`。
- `b`：蓝色通道，范围 `[0,255]`。

字段来源：

- `BuildDistinctColorPalette` 或 `DistinctColorByIndex` 生成。
- 也可以由调用方手动构造后传给染色 API。

后续消费：

- Step1 写入 `FaceRecord.color_r/g/b`。
- Step2 group 染色时按 group id 分配颜色。
- 调试 SAT 输出继承 face/entity 上的 ACIS 颜色。

## 3. 颜色分配 API

### `BuildDistinctColorPalette`

```cpp
std::vector<ColorRgb255> BuildDistinctColorPalette(int count);
```

输入：

- `count`：需要生成的颜色数量。

输出：

- `count > 0`：返回长度为 `count` 的颜色列表。
- `count <= 0`：返回空列表。

规则：

- 内部使用 HSV 色相均分。
- 固定 `saturation = 0.75`。
- 固定 `value = 0.95`。
- 最终转成 `ColorRgb255` 返回。

职责：

- 为同一批 face 或 group 生成区分度更高的颜色。
- 不修改任何 ACIS 实体。

### `DistinctColorByIndex`

```cpp
ColorRgb255 DistinctColorByIndex(int index, int count);
```

输入：

- `index`：当前对象在该批对象中的下标。
- `count`：该批对象总数。

输出：

- `count <= 0`：返回默认黑色 `ColorRgb255()`。
- `index < 0`：按 `index = 0` 处理。
- `index >= count`：按 `index % count` 处理。

规则：

```text
hue = index / count
saturation = 0.75
value = 0.95
```

职责：

- 在不需要完整 palette 的场景中按下标生成稳定颜色。
- Step1 当前使用该 API 按 `face_id / face_count` 分配颜色。

### `StableColorByIndex`

```cpp
ColorRgb255 StableColorByIndex(int index);
```

状态：

- 保留为兼容 API。
- 当前 Step1 不再使用它。

规则：

- 使用旧的 RGB 取模方式。
- 不知道同一批对象总数，因此区分度不如 `DistinctColorByIndex(index, count)`。

## 4. 染色 API

### `ApplyEntityColor`

```cpp
logical ApplyEntityColor(ENTITY* entity, const ColorRgb255& color);
```

输入：

- `entity`：要染色的 ACIS entity。
- `color`：RGB 颜色。

输出：

- `TRUE`：`entity != nullptr`，已经调用 `set_entity_color`。
- `FALSE`：`entity == nullptr`。

职责：

- 将 `[0,255]` 的 RGB 通道转换为 ACIS `rgb_color` 使用的 `[0,1]` double。
- 调用 `set_entity_color`。
- 不决定颜色来源。

### `ApplyFaceColor`

```cpp
logical ApplyFaceColor(FACE* face, const ColorRgb255& color);
```

输入：

- `face`：要染色的 ACIS face。
- `color`：RGB 颜色。

输出：

- 透传 `ApplyEntityColor((ENTITY*)face, color)` 的结果。

职责：

- 为 face 调用方提供更明确的 API。
- Step1 统一 face 染色使用它。
- Step2 group 染色时也复用它。

## 5. 调用流程

Step1 当前流程：

```text
CollectFaces
  -> face_count = face_entities.count()
  -> for each FACE:
       color = DistinctColorByIndex(face_id, face_count)
       ApplyFaceColor(face, color)
       FaceRecord.color_r/g/b = color
```

Step2 后续 group 染色预留流程：

```text
BuildGroupsFromUnionFind
  -> palette = BuildDistinctColorPalette(group_count)
  -> for each GroupRecord:
       color = palette[group_id]
       for each FACE* in group.faces:
           ApplyFaceColor(face, color)
```

## 6. 注意事项

- HSV 只是内部实现细节，对外仍然只暴露 RGB。
- Step1 仍然只在 face analyze 阶段染色一次。
- Step2 group 染色尚未实现，本次只提供可复用 API。
- ACIS `set_entity_color` 返回 `rgb_color`，不是 `outcome`；当前 API 只以 entity 是否为空作为失败条件。
- ACIS `set_entity_color` 的行为按现有使用方式处理，本次未做额外 SAT 样本验证。
