# ColorUtils 工程文档

源码事实源：

- `midface/utils/ColorUtils.hpp`
- `midface/utils/ColorUtils.cpp`

## 作用

`ColorUtils` 提供当前 pipeline 的调试染色能力：

- 根据 index 生成稳定颜色。
- 根据总数生成可区分 palette。
- 给 ACIS `ENTITY*` / `FACE*` 设置 RGB 颜色。

这些颜色主要用于 debug SAT：Step1 face、Step2 group、Step3 pair/wall、Step6 selected face。

## 数据结构

### `ColorRgb255`

```cpp
struct ColorRgb255
{
    int r;
    int g;
    int b;
};
```

- `r`：红色通道，0-255。
- `g`：绿色通道，0-255。
- `b`：蓝色通道，0-255。

默认构造时三个通道都是 0。

## 函数说明

### `StableColorByIndex`

```cpp
ColorRgb255 StableColorByIndex(int index);
```

按固定整数公式生成颜色：

```text
r = (53 * (index + 1)) % 256
g = (97 * (index + 1)) % 256
b = (193 * (index + 1)) % 256
```

如果 `index < 0`，会当作 0。

当前 Step 主线里更常用的是 `DistinctColorByIndex`。

### `BuildDistinctColorPalette`

```cpp
std::vector<ColorRgb255> BuildDistinctColorPalette(int count);
```

生成长度为 `count` 的颜色列表。内部逐个调用：

```cpp
DistinctColorByIndex(i, count)
```

如果 `count <= 0`，返回空列表。

### `DistinctColorByIndex`

```cpp
ColorRgb255 DistinctColorByIndex(int index, int count);
```

使用 HSV 均匀分布生成颜色。

规则：

- `hue = index / count`
- `saturation = 0.75`
- `value = 0.95`
- 再转换成 RGB 0-255。

如果 `index < 0`，按 0 处理。如果 `index >= count`，按 `index % count` 回绕。

### `ApplyEntityColor`

```cpp
logical ApplyEntityColor(ENTITY* entity, const ColorRgb255& color);
```

把 0-255 RGB 转成 ACIS `rgb_color` 的 0-1 浮点值，并调用：

```cpp
set_entity_color(entity, acis_color)
```

如果 `entity == nullptr`，返回 `FALSE`。

### `ApplyFaceColor`

```cpp
logical ApplyFaceColor(FACE* face, const ColorRgb255& color);
```

对 `FACE*` 调用 `ApplyEntityColor((ENTITY*)face, color)`。

## 当前调用点

### Step1

`Step1FaceAnalyze.cpp`：

- 每个 face 使用 `DistinctColorByIndex(face_id, face_count)`。
- 调用 `ApplyFaceColor(face, color)`。
- 输出 `step1.colored_body` 时这些颜色会进入 SAT。

### Step2

`Step2GroupBuild.cpp`：

- `BuildGroupsFromUnionFind` 中按 group 数生成 palette。
- group 内所有 faces 染同一个 group 色。
- 输出 `step2.group_colored_body`。

### Step3

`Step3PairBuild.cpp`：

- `ColorPairs`：pair 两侧 group 染同一个 pair 色。
- `ColorWalls`：wall candidate group 使用 wall palette。
- 输出 `step3.pair_colored_body`。

### Step6

`Step6TrimSelect.cpp`：

- `ColorSelectedFacesAndEmitMap` 给 selected faces 分配 palette。
- 输出 `step6.selected_faces` color-map 和 debug SAT。

## 当前边界

- Step5 `step5.mid_patch_faces` 当前没有在输出前主动调用 `ApplyFaceColor`。
- Step6 `extended_mid_faces`、`extended_wall_faces`、`preselect_split_faces` 当前没有统一 palette 染色。
- Step7 当前没有主动染色。
- `set_entity_color` 的返回值没有被检查，`ApplyEntityColor` 只要 entity 非空就返回 `TRUE`。
