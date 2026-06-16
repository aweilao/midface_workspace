# Step1 Face Analyze 工程文档

本文记录当前 `midface` Step1 的实现。源码事实源：

- `midface/steps/Step1FaceAnalyze.hpp`
- `midface/steps/Step1FaceAnalyze.cpp`
- `midface/core/MidSurfaceNewTypes.hpp`
- `midface/core/MidSurfaceNewTypes.cpp`
- `midface/MidSurface.cpp`

## 目标和算法

Step1 接收一个 ACIS `BODY*`，枚举 body 上的所有 `FACE*`，为每个 face 建立稳定的 `FaceRecord`。这些记录是 Step2 grouping 的输入。

Step1 做的事情：

- 读取 body 中的 face。
- 给每个 face 分配 `face_id`。
- 获取 face 的底层 surface、face type、代表点、代表法向、面积代理、边数量和边长范围。
- 按 face id 给 face 上色，便于输出 `step1.colored_body` 调试 SAT。
- 可选构建 face 邻接关系。
- 可选输出每个 face 的结构化日志。

Step1 不做的事情：

- 不判断厚度方向。
- 不合并 face。
- 不生成中面。
- 不修复拓扑。

Step1 的主流程：

```text
RunMidSurfaceBodyWithContext
  -> RunStep1FaceAnalyze(body, config.step1, diagnostics, result.step1)
    -> CollectFaces
    -> for each FACE:
      -> EntityToFace
      -> DistinctColorByIndex
      -> ApplyFaceColor
      -> FillRepresentativePoint
      -> FillRepresentativeNormal
      -> FillAreaProxy
      -> CollectFaceEdges
      -> FillEdgeLengthRange
      -> BuildSampleSummary
      -> EmitStep1Event(face_recorded)
    -> BuildAdjacencyFromEdges
    -> Emit finish event
    -> EmitStep1ColorMapEvents
    -> EmitBodySatIfEnabled("step1.colored_body")
```

## 参数设置

`Step1FaceAnalyzeOptions` 定义在 `Step1FaceAnalyze.hpp`，默认值在 `Step1FaceAnalyze.cpp`。

```cpp
struct Step1FaceAnalyzeOptions
{
    logical build_adjacency = TRUE;
    logical build_sample_summary = TRUE;
    logical emit_face_events = TRUE;
};
```

- `build_adjacency`：是否根据共享 edge 构建 face 邻接关系。开启后会填充 `Step1FaceAnalyzeState.adjacency`，并更新每个 `FaceRecord.adjacency_degree`。
- `build_sample_summary`：是否构建 face 的摘要文本。当前实现里 `BuildSampleSummary` 总会被调用，字段保留为配置语义。
- `emit_face_events`：是否为每个 face 输出 `face_recorded` 事件。关闭后仍会构建 state，只是不输出逐 face 日志。

## Result 和 State

### `Step1FaceAnalyzeResult`

```cpp
struct Step1FaceAnalyzeResult
{
    logical ok;
    Step1FaceAnalyzeState state;
};
```

- `ok`：Step1 是否成功。当前实现中，只要最终 `face_count > 0` 就为 `TRUE`。
- `state`：Step1 产出的完整状态，后续 Step2 会读取其中的 face 表、邻接和统计信息。

### `Step1FaceAnalyzeState`

```cpp
struct Step1FaceAnalyzeState
{
    BODY* input_body;
    Step1FaceAnalyzeOptions options_snapshot;
    FaceTable faces;
    FaceAdjacencyGraph adjacency;
    std::vector<PointSample> sample_summary;
    ModelScaleContext model_scale;
    FaceAnalyzeStats stats;
};
```

- `input_body`：传入 Step1 的原始 `BODY*`。Step2 输出 debug SAT 时会通过它输出当前染色后的 body。
- `options_snapshot`：Step1 本次运行使用的参数快照。
- `faces`：所有枚举到的 face 记录表。Step2 从这里收集 `valid == TRUE` 且 `face != nullptr` 的 face。
- `adjacency`：face 邻接图。当前由共享 edge 统计而来。
- `sample_summary`：预留的采样摘要列表。当前 Step1 代码没有向该 vector 写入数据，face 摘要文本写在每个 `FaceRecord.sample_summary` 中。
- `model_scale`：模型尺度上下文。当前只在 Step1 计算、写入日志、result JSON 和 Step1 checkpoint，后续算法暂不消费它；它是未来把距离参数改成相对单位时的基础。
- `stats`：Step1 统计信息，包括 face 数、有效 face 数、无效 face 数和邻接边数。

### `FaceTable`

```cpp
struct FaceTable
{
    std::vector<FaceRecord> records;
};
```

- `records`：按 Step1 枚举顺序保存所有 `FaceRecord`。`face_id` 当前就是记录创建时的递增编号。

### `FaceRecord`

定义在 `MidSurfaceNewTypes.hpp`。

```cpp
struct FaceRecord
{
    int face_id;
    FACE* face;
    SURFACE* surface_geometry;
    std::string face_type;
    int color_r;
    int color_g;
    int color_b;
    int edge_count;
    int adjacency_degree;
    logical has_representative_point;
    logical has_representative_normal;
    logical has_area_proxy;
    logical has_edge_lengths;
    SPAposition representative_point;
    SPAunit_vector representative_normal;
    double area_proxy;
    double edge_min;
    double edge_max;
    logical valid;
    std::string invalid_reason;
    std::string sample_summary;
};
```

- `face_id`：Step1 分配的 face 编号，从 0 递增。
- `face`：对应的 ACIS `FACE*`。
- `surface_geometry`：`face->geometry()` 返回的底层 `SURFACE*`。
- `face_type`：face 类型文本，来自 `get_face_type`，当前可能是 `plane`、`cylinder`、`cone`、`sphere`、`torus`、`spline`、`unknown` 或 `missing_surface`。
- `color_r` / `color_g` / `color_b`：Step1 分配给该 face 的 RGB 颜色，取值 0-255。
- `edge_count`：该 face 上枚举到的 edge 数量。
- `adjacency_degree`：共享 edge 邻接的 face 数统计。`BuildAdjacencyFromEdges` 会递增它。
- `has_representative_point`：`representative_point` 是否可用。
- `has_representative_normal`：`representative_normal` 是否可用。
- `has_area_proxy`：`area_proxy` 是否可用。
- `has_edge_lengths`：`edge_min` 和 `edge_max` 是否可用。
- `representative_point`：face 代表点。优先使用 `find_interior_point`，失败时用 face bbox 中心点。
- `representative_normal`：face 代表法向。优先在代表点调用 `get_face_normal`，失败时尝试平面法向。
- `area_proxy`：face 面积代理，来自 `api_ent_area`。
- `edge_min`：有效 edge 长度的最小值，忽略长度小于等于 `1.0e-9` 的 edge。
- `edge_max`：有效 edge 长度的最大值。
- `valid`：face 是否可作为后续输入。当前主要取决于 `surface_geometry != nullptr`。
- `invalid_reason`：无效或信息缺失的原因，多个原因用 `;` 拼接。
- `sample_summary`：文本摘要，包含 type、edge_count、代表点可用性、normal、area、edge_lengths 和 invalid_reason。

### `FaceAdjacencyGraph`

```cpp
struct FaceAdjacencyGraph
{
    std::vector<FaceAdjacency> edges;
};
```

- `edges`：face 之间的邻接边列表。每条记录表示两个 face 共享至少一条 ACIS edge。

### `FaceAdjacency`

```cpp
struct FaceAdjacency
{
    int face_a;
    int face_b;
    int shared_edge_count;
    std::string relation;
};
```

- `face_a`：邻接关系中较小的 face id。
- `face_b`：邻接关系中较大的 face id。
- `shared_edge_count`：两个 face 共享的 edge 数。
- `relation`：当前固定为 `shared_edge`。

### `PointSample`

```cpp
struct PointSample
{
    int face_id;
    SPAposition point;
    SPAunit_vector normal;
    std::string role;
};
```

- `face_id`：sample 所属 face id。
- `point`：采样点位置。
- `normal`：采样点法向。
- `role`：sample 的来源或用途。Step1 当前没有填充 `Step1FaceAnalyzeState.sample_summary`，Step2 会使用该结构保存 boundary sample。

### `ModelScaleContext`

定义在 `MidSurfaceNewTypes.hpp`。

```cpp
struct ModelScaleContext
{
    logical valid;
    double reference_length;
    double distance_unit;
    double normalized_reference;
    int top_edge_count_requested;
    int top_edge_count_used;
    int valid_edge_count;
    double min_valid_edge_length;
    double longest_edge_length;
    double shortest_used_edge_length;
};
```

- `valid`：尺度上下文是否可用。只要有有效 edge、`reference_length > 0` 且 `distance_unit > 0` 就为 `TRUE`。
- `reference_length`：模型参考长度。当前算法为“唯一有效 edge 长度按降序排列后，取最长 `top_edge_count_requested` 条的平均值”。
- `distance_unit`：归一化距离单位，当前为 `reference_length / normalized_reference`。
- `normalized_reference`：归一化参考值，当前默认 `1000.0`。含义是把模型参考长度视为约 1000 个内部单位。
- `top_edge_count_requested`：期望参与参考长度计算的最长 edge 数，当前默认 10。
- `top_edge_count_used`：实际参与计算的 edge 数。有效 edge 不足 10 条时使用已有有效 edge。
- `valid_edge_count`：全模型参与尺度候选的唯一有效 edge 数。共享 edge 按 `EDGE*` 去重，只统计一次。
- `min_valid_edge_length`：有效 edge 的最小长度阈值，当前为 `1.0e-9`。
- `longest_edge_length`：候选有效 edge 中的最大长度。
- `shortest_used_edge_length`：参与 top edge 平均值计算的最短那条 edge 长度。

### `FaceAnalyzeStats`

```cpp
struct FaceAnalyzeStats
{
    int face_count;
    int valid_face_count;
    int invalid_face_count;
    int adjacency_count;
};
```

- `face_count`：Step1 成功记录的 face 数量。
- `valid_face_count`：`FaceRecord.valid == TRUE` 的 face 数。
- `invalid_face_count`：`FaceRecord.valid == FALSE` 的 face 数。
- `adjacency_count`：邻接关系数量，即 `adjacency.edges.size()`。

## 函数说明

### `RunStep1FaceAnalyze`

Step1 外部入口。

```cpp
logical RunStep1FaceAnalyze(
    BODY* body,
    const Step1FaceAnalyzeOptions& options,
    DiagnosticSink* diagnostics,
    Step1FaceAnalyzeResult& result);
```

流程：

1. 重置 `result`。
2. 保存 `body` 到 `result.state.input_body`。
3. 保存 `options` 到 `result.state.options_snapshot`。
4. 若 `body == nullptr`，输出 `invalid_input` 事件并返回 `FALSE`。
5. 输出 `start` 事件。
6. 调用 `CollectFaces` 枚举 body 中的 face。
7. 遍历 face，生成 `FaceRecord`，计算几何摘要并上色。
8. 可选输出每个 face 的 `face_recorded` 事件。
9. 若 `build_adjacency` 开启，调用 `BuildAdjacencyFromEdges` 构建邻接图。
10. 调用 `BuildModelScaleContext` 计算 `state.model_scale`，并输出 `model-scale` 事件。
11. 根据 `face_count > 0` 设置 `result.ok`。
12. 输出 finish 事件、color map 事件和可选 debug SAT。

### `CollectFaces`

调用 `api_get_faces((ENTITY*)body, out_faces)` 收集 body 上的 face entity。失败时 Step1 直接返回 `FALSE`。

### `EntityToFace`

检查 `ENTITY*` 是否为 `FACE`，是则转换为 `FACE*`，否则返回 `nullptr`。

### `CollectFaceEdges`

调用 `api_get_edges((ENTITY*)face, edge_entities)` 收集 face 上的 edge，并把 `EDGE*` 放入输出 vector。

### `FillRepresentativePoint`

为 `FaceRecord` 填写代表点。

流程：

1. 优先调用 `find_interior_point(face, interior_point)`。
2. 若失败，调用 `api_get_entity_box` 获取 face bbox。
3. 使用 bbox 中心作为代表点。
4. 成功后设置 `has_representative_point = TRUE`。

### `FillRepresentativeNormal`

为 `FaceRecord` 填写代表法向。

流程：

1. 若代表点可用，在代表点调用 `get_face_normal(face, point, normal_position, normal)`。
2. 若失败，尝试调用 `get_face_normal((FACE const*)face, planar_normal)`。
3. 成功后设置 `has_representative_normal = TRUE`。

### `FillAreaProxy`

调用 `api_ent_area((ENTITY*)face, 1.0e-6, area, achieved_accuracy)` 计算 face 面积代理，成功后写入 `area_proxy`。

### `FillEdgeLengthRange`

遍历 face edges，调用 `edge->length(TRUE)`，统计大于 `1.0e-9` 的 edge 长度最小值和最大值。

### `CollectUniqueValidEdgeLengths`

遍历当前 face 的 edge，按 `EDGE*` 对全模型 edge 去重，调用 `edge->length(TRUE)`，把大于 `1.0e-9` 的长度加入模型尺度候选列表。该函数只为 `ModelScaleContext` 收集数据，不影响 `FaceRecord.edge_min` / `edge_max`。

### `BuildModelScaleContext`

从全模型唯一有效 edge 长度列表计算尺度上下文。

流程：

1. 复制 edge 长度列表并按降序排序。
2. 取最长 `top_edge_count_requested` 条；不足 10 条时取全部有效 edge。
3. 计算这些 edge 的平均值，写入 `reference_length`。
4. 计算 `distance_unit = reference_length / normalized_reference`。
5. 写入 `valid_edge_count`、`top_edge_count_used`、`longest_edge_length` 和 `shortest_used_edge_length`。
6. 若参考长度和单位均大于 0，设置 `valid = TRUE`。

这个尺度当前是未来重构用的基础数据，不改变 Step2-Step7 的现有距离容差。

### `BuildAdjacencyFromEdges`

输入 `edge_faces`，其中 key 是 `EDGE*`，value 是使用该 edge 的 face id 列表。

流程：

1. 对每条 edge 上的 face 两两组合。
2. 统计每个 face pair 的共享 edge 数。
3. 调用 `AddAdjacencyEdge` 写入 `state.adjacency.edges`。
4. 更新 `state.stats.adjacency_count`。

### `AddAdjacencyEdge`

构造一条 `FaceAdjacency`，确保 `face_a < face_b`，设置 `relation = "shared_edge"`，并递增两个 face 的 `adjacency_degree`。

### `BuildSampleSummary`

把 face type、edge_count、代表点、法向、面积、边长和 invalid reason 拼成一段摘要字符串，写入 `FaceRecord.sample_summary`。

### `EmitStep1Event`

输出 Step1 结构化事件。对 `face_recorded` 事件会额外添加 `summary` 和 `single` tag，并把 `FaceRecord` 的主要字段写入 properties。

### `EmitModelScaleEvent`

输出 Step1 模型尺度摘要事件。该事件记录 `ModelScaleContext` 的每个字段，便于未来调试相对单位参数。

### `EmitStep1ColorMapEvents`

为 `step1.colored_body` 输出 face id 到颜色的映射，供日志查看器按颜色反查 face id。

## 日志输出

所有 Step1 事件基础 tag：

```text
step1
face-analyze
```

### start

触发位置：Step1 开始。

tags：

```text
step1
face-analyze
start
```

properties：当前无额外字段。

### invalid_input

触发位置：`body == nullptr`。

tags：

```text
step1
face-analyze
invalid_input
```

properties：

- `note`：错误说明，当前为 `input body is null`。

### collect_faces_failed

触发位置：`api_get_faces` 失败。

tags：

```text
step1
face-analyze
collect_faces_failed
```

properties：

- `note`：错误说明，当前为 `api_get_faces failed`。

### face_recorded

触发位置：每个 face 记录完成后，且 `emit_face_events == TRUE`。

tags：

```text
step1
face-analyze
face_recorded
summary
single
```

properties：

- `face_id`：face 编号。
- `face_type`：face 类型。
- `valid`：是否有效。
- `edge_count`：edge 数量。
- `adjacency_degree`：邻接度。注意该事件输出时邻接还未构建，通常仍是 0。
- `has_representative_point`：是否有代表点。
- `has_representative_normal`：是否有代表法向。
- `has_area_proxy`：是否有面积代理。
- `has_edge_lengths`：是否有边长范围。
- `representative_point`：代表点，格式为 `x,y,z`，仅可用时输出。
- `representative_normal`：代表法向，格式为 `x,y,z`，仅可用时输出。
- `area_proxy`：面积代理，仅可用时输出。
- `edge_min`：最小 edge 长度，仅可用时输出。
- `edge_max`：最大 edge 长度，仅可用时输出。
- `invalid_reason`：无效或信息缺失原因，仅非空时输出。
- `sample_summary`：face 摘要文本，仅非空时输出。

json_properties：

- `color_rgb`：Step1 给 face 分配的 RGB 数组。

### adjacency_built

触发位置：`build_adjacency == TRUE` 且邻接构建完成。

tags：

```text
step1
face-analyze
adjacency
adjacency_built
summary
stage
```

properties：

- `adjacency_count`：邻接边数量。

### model-scale

触发位置：face 枚举和 adjacency 构建完成后，finish 事件前。

tags：

```text
step1
face-analyze
model-scale
summary
stage
```

properties：

- `valid`：尺度上下文是否可用。
- `reference_length`：最长若干有效 edge 的平均参考长度。
- `distance_unit`：`reference_length / normalized_reference`。
- `normalized_reference`：归一化参考值，当前为 `1000`。
- `top_edge_count_requested`：期望使用的最长 edge 数，当前为 `10`。
- `top_edge_count_used`：实际使用的 edge 数。
- `valid_edge_count`：全模型唯一有效 edge 数。
- `min_valid_edge_length`：有效 edge 长度阈值，当前为 `1.0e-9`。
- `longest_edge_length`：候选 edge 中的最大长度。
- `shortest_used_edge_length`：参与参考平均值计算的最短 edge 长度。

### finish / finish_without_faces

触发位置：Step1 结束。

tags：

```text
step1
face-analyze
finish
summary
all
```

或：

```text
step1
face-analyze
finish_without_faces
summary
all
```

properties：

- `face_count`：face 总数。
- `valid_face_count`：有效 face 数。
- `invalid_face_count`：无效 face 数。
- `adjacency_count`：邻接边数量。

### color-map

触发位置：Step1 finish 后的 `EmitStep1ColorMapEvents`。

用途：记录 `step1.colored_body` 中颜色到 `face_id` 的映射。具体公共字段由 `DiagnosticSink::EmitColorIdMapEntryIfEnabled` 生成。

Step1 传入的 tags：

```text
step1
face-analyze
```

传入的 artifact role：

```text
step1.colored_body
```

properties：

- `face_id`：face 编号。
- `type`：face 类型。

json_properties：

- `rgb`：RGB 数组。

### debug SAT

触发位置：Step1 finish 后。

role：

```text
step1.colored_body
```

输出条件：`RunContextOptions.debug_sat_outputs` 包含该 role。

内容：输入 body 被按 Step1 face id palette 染色后的 SAT。
