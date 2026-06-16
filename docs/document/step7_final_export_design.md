# Step7 Stitch 工程文档

本文记录当前 `midface` Step7 的实现。源码事实源：

- `midface/steps/Step7Stitch.hpp`
- `midface/steps/Step7Stitch.cpp`
- `midface/steps/Step7StitchInput.hpp`
- `midface/utils/FaceSheetBodySatWriter.hpp`
- `midface/MidSurface.cpp`

## 目标和算法

Step7 当前是 stitch 阶段的接口占位。Step6 已经把 selected split face、raw face relation 和上游 pair/group/face 引用封装成 `Step7StitchInput`。Step7 当前接收该输入，保存到 result state，并输出两个调试 SAT。

Step7 当前做的事情：

- 接收 `Step7StitchInput`。
- 输出所有 selected split faces 的 SAT。
- 输出一个 selected split face 连同其上游原始 group faces 的 SAT。
- 将输入复制到 `Step7StitchResult.state.input`。
- 设置 `result.ok = TRUE`。

Step7 当前不做的事情：

- 不计算 split-level adjacency。
- 不调用 ACIS stitch。
- 不生成最终 stitched body。
- 不写结构化日志。
- 不接收 `DiagnosticSink*`。

Step7 主流程：

```text
RunMidSurfaceBodyWithContext
  -> RunStep7Stitch(result.step6.state.step7_stitch_input, result.step7)
    -> WriteStep7InputSplitFacesSat(
         input.split_to_raw_faces,
         "output/step7_input_split_faces_body_0.sat")
    -> WriteFirstSplitWithUpstreamFacesSat(input)
    -> result.state.input = input
    -> result.ok = TRUE
```

## 参数设置

Step7 当前没有 `Step7Options`。配置只来自 Step6 构造好的 `Step7StitchInput`。

## Result 和 State

### `Step7StitchResult`

```cpp
struct Step7StitchResult
{
    logical ok = FALSE;
    Step7StitchState state;
};
```

- `ok`：Step7 是否完成。当前只要 `WriteStep7InputSplitFacesSat` 成功，最后就为 `TRUE`。
- `state`：Step7 输出状态。

### `Step7StitchState`

```cpp
struct Step7StitchState
{
    Step7StitchInput input;
};
```

- `input`：Step7 接收到的输入副本。当前 Step7 没有生成新的几何拓扑结果。

### `Step7StitchInput`

定义在 `Step7StitchInput.hpp`。

```cpp
struct Step7StitchInput
{
    std::vector<Step7SplitRawFaceRecord> split_to_raw_faces;
    std::vector<Step7RawFaceRelationRecord> raw_face_relations;
    std::vector<Step7RawFaceRef> raw_faces;
};
```

- `split_to_raw_faces`：Step6 selected split face 到 Step5 raw patch 的映射。
- `raw_face_relations`：Step5 raw patch 之间的关系。
- `raw_faces`：raw patch 的上游 pair/group/face 引用。

### `Step7SplitRawFaceRecord`

```cpp
struct Step7SplitRawFaceRecord
{
    int split_id = -1;
    int raw_face_id = -1;
    FACE* split_face = nullptr;
};
```

- `split_id`：Step6 `TrimSelectionRecord.selection_id`。
- `raw_face_id`：Step5 `MidPatchRecord.patch_id`，由 Step6 `source_patch_id` 传入。
- `split_face`：Step6 选中的 detached face。

### `Step7RawFaceRelationRecord`

```cpp
struct Step7RawFaceRelationRecord
{
    int relation_id = -1;
    int raw_face_a = -1;
    int raw_face_b = -1;
};
```

- `relation_id`：Step6 构造 raw face relation 时分配的编号。
- `raw_face_a`：关系一侧 raw face id。
- `raw_face_b`：关系另一侧 raw face id。

### `Step7FaceRef`

```cpp
struct Step7FaceRef
{
    int face_id = -1;
    FACE* face = nullptr;
};
```

- `face_id`：Step1 face id。
- `face`：对应 ACIS `FACE*`。

### `Step7GroupRef`

```cpp
struct Step7GroupRef
{
    int group_id = -1;
    std::vector<Step7FaceRef> faces;
};
```

- `group_id`：Step2 group id。
- `faces`：该 group 内的 Step1 face 引用。

### `Step7PairRef`

```cpp
struct Step7PairRef
{
    int pair_id = -1;
    Step7GroupRef group_a;
    Step7GroupRef group_b;
};
```

- `pair_id`：Step3 pair id。
- `group_a`：pair A 侧 group。
- `group_b`：pair B 侧 group。

### `Step7RawFaceRef`

```cpp
struct Step7RawFaceRef
{
    int raw_face_id = -1;
    FACE* raw_face = nullptr;
    Step7PairRef pair;
};
```

- `raw_face_id`：Step5 patch id。
- `raw_face`：Step5 `MidPatchRecord.face`。
- `pair`：该 raw face 来源 pair 及其两侧 group 信息。

## 函数说明

### `RunStep7Stitch`

Step7 外部入口。

```cpp
logical RunStep7Stitch(
    const Step7StitchInput& input,
    Step7StitchResult& result);
```

流程：

1. 调用 `WriteStep7InputSplitFacesSat` 输出所有 split faces。
2. 若输出失败，设置 `result.ok = FALSE` 并返回 `FALSE`。
3. 调用 `WriteFirstSplitWithUpstreamFacesSat` 输出一个 split face 及其上游 group faces。
4. 保存 `input` 到 `result.state.input`。
5. 设置 `result.ok = TRUE`。

注意：当前 `RunStep7Stitch` 没有先重置 `result`，调用方在 `MidSurfaceResult` 初始化时已构造默认 result。

### `WriteStep7InputSplitFacesSat`

```cpp
logical WriteStep7InputSplitFacesSat(
    const std::vector<Step7SplitRawFaceRecord>& split_to_raw_faces,
    const char* output_sat_path);
```

流程：

1. 若 `output_sat_path == nullptr`，返回 `FALSE`。
2. 遍历 `split_to_raw_faces`，收集每条记录的 `split_face`。
3. 调用 `SaveFacesAsSheetBodiesSat(faces, output_sat_path)`。

当前输出路径：

```text
output/step7_input_split_faces_body_0.sat
```

### `WriteFirstSplitWithUpstreamFacesSat`

定义在 `Step7Stitch.cpp` 匿名 namespace。

流程：

1. 若 `input.split_to_raw_faces.empty()`，返回 `TRUE`。
2. 读取 `input.split_to_raw_faces[5]`。
3. 收集该 split face。
4. 根据 `split.raw_face_id` 查找 `Step7RawFaceRef`。
5. 追加该 raw face 所属 pair 的 group_a/group_b 中所有 faces。
6. 调用 `SaveFacesAsSheetBodiesSat` 输出 SAT。

当前输出路径：

```text
output/step7_first_split_with_origin_faces_body_0.sat
```

注意：当前代码固定访问 `split_to_raw_faces[5]`，只检查了 empty，没有检查 size 是否大于 5。这是当前实现的边界风险。

### `FindRawFaceRef`

按 `raw_face_id` 在线性遍历 `input.raw_faces` 中查找 `Step7RawFaceRef`。

### `AppendGroupFaces`

把 `Step7GroupRef.faces` 中非空 `FACE*` 追加到输出 vector。

## Step6 输入封装

Step7 输入由 Step6 的 `BuildStep7StitchInputFromSelections` 构造。

封装规则：

- 每个 `TrimSelectionRecord` 生成一个 `Step7SplitRawFaceRecord`。
- `split_id = selection.selection_id`。
- `raw_face_id = selection.source_patch_id`。
- `split_face = selection.selected_face`。
- Step5 junctions 被转换成 `Step7RawFaceRelationRecord`。
- `raw_faces` 保存 raw patch 对应的 Step5 patch face、Step3 pair、Step2 group 和 Step1 face 引用。

## 日志输出

Step7 当前不写结构化日志，也不接收 `DiagnosticSink*`。

Step7 相关输入追踪日志是在 Step6 中输出的，tag 包含：

```text
step6
trim-select
step7-input
```

## Debug SAT / 文件输出

Step7 当前直接写固定路径 SAT，不通过 `RunContextOptions.debug_sat_outputs` 控制。

输出文件：

```text
output/step7_input_split_faces_body_0.sat
output/step7_first_split_with_origin_faces_body_0.sat
```

第一个文件包含 Step6 selected split faces。第二个文件包含固定下标 split face 以及该 raw face 上游 pair 两侧 group 的原始 faces。

## Result JSON

Step7 自身不写 JSON。pipeline result JSON 由 `SaveMidSurfaceResultJson` 统一输出。

Step7 result 中主要内容来自：

```cpp
result.state.input = input;
```

也就是说 result JSON 中 Step7 的 split/raw relation/raw face 信息来自 Step6 封装的 `Step7StitchInput`。
