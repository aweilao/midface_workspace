# ResultJsonWriter 工程文档

源码事实源：

- `midface/utils/ResultJsonWriter.hpp`
- `midface/utils/ResultJsonWriter.cpp`
- `midface/MidSurface.h`

## 作用

`ResultJsonWriter` 在每个 body 运行结束或 stop-after 时保存 pipeline result 快照。输出位置：

```text
<output_root>/results/body_<body_index>_result.json
```

`MidSurface.cpp` 通过 `SaveResultJsonForBody` 调用它。

## 对外函数

### `ResultJsonPath`

```cpp
std::string ResultJsonPath(const RunContext& context, int body_index);
```

返回：

```text
<context.output_root()>/results/body_<body_index>_result.json
```

### `SaveMidSurfaceResultJson`

```cpp
logical SaveMidSurfaceResultJson(
    const RunContext& context,
    int body_index,
    const MidSurfaceResult& result);
```

流程：

1. 调用 `ResultJsonPath`。
2. 调用 `EnsureParentDirectoryForFile`。
3. 打开文件。
4. 调用内部 `ResultJson(body_index, result).dump(2)`。
5. 写入 pretty JSON，并追加换行。

## 输出结构

顶层字段：

- `body_index`
- `ok`
- `completed_step`
- `step1`
- `step2`
- `step3`
- `step4`
- `step5`
- `step6`
- `step7`

Step 字段只在 `completed_step >= 对应 step` 时输出。

## 各 Step 输出

### Step1

输出：

- `ok`
- `stats`
- `faces`
- `adjacency`

`faces` 来自 `FaceRecordJson`，包含 face id、type、color、edge、valid、代表点/法向、area、edge lengths 和 sample summary。

### Step2

输出：

- `ok`
- `stats`
- `groups`
- `face_to_group`
- `reject_reason_stats`

注意：Step2 result JSON 当前不输出 candidates 和 decisions。

### Step3

输出：

- `ok`
- `stats`
- `pairs`
- `walls`
- `group_to_pairs`
- `group_to_walls`

注意：Step3 result JSON 当前不输出 candidates 和 decisions。

### Step4

输出：

- `ok`
- `stats`
- `pair_relations`
- `pair_wall_relations`
- `virtual_walls`

### Step5

输出：

- `ok`
- `stats`
- `patches`
- `components`
- `junctions`

### Step6

输出：

- `ok`
- `stats`
- `slices`
- `selections`
- `slice_adjacencies`
- `selected_adjacency`

注意：Step6 result JSON 当前不输出完整 `step7_stitch_input`；Step7 result 中会输出 Step7 input 副本。

### Step7

输出：

- `ok`
- `raw_relation_input`

`raw_relation_input` 包含：

- `split_to_raw_faces`
- `raw_face_relations`
- `raw_faces`

这些来自 `Step7StitchResult.state.input`。

## 当前边界

- JSON 中只记录 ACIS 指针是否存在，例如 `has_face`、`has_body`，不保存指针地址。
- Step2/Step3 的 candidate 和 decision 细节主要在 JSONL 日志中，不在 result JSON 中。
- Step6 的 raw imprint edge tables、slice edge uses 当前没有写入 result JSON。
