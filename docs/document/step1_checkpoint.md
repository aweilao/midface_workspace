# Step1 Checkpoint

本文档记录 `midface_new/utils/Step1Checkpoint.hpp/.cpp` 的职责和数据格式。

## 1. 设计目的

Step2 调参时不希望每次都重新执行 Step1。Step1 checkpoint 提供两个能力：

- Step1 完成后将可持久化结果落盘。
- 后续从 Step1 checkpoint 恢复内存态 `Step1FaceAnalyzeResult`，直接进入 Step2。

ACIS 指针不能持久化，因此 checkpoint 拆成两部分：

- SAT：保存 Step1 染色后的 body。
- JSON：保存 Step1 face records、统计和 adjacency。

恢复时先 restore SAT，再按 face_id 顺序重新绑定 `FACE*` 和 `SURFACE*`。

## 2. 文件布局

以 `checkpoint_root = .../checkpoints` 为例：

```text
checkpoints/
  step1/
    body_0_colored.sat
    body_0_step1_result.json
```

## 3. API

### `SaveStep1Checkpoint`

```cpp
logical SaveStep1Checkpoint(
    const CheckpointStore& store,
    int body_index,
    const Step1FaceAnalyzeResult& result,
    DiagnosticSink* diagnostics);
```

职责：

- 写 `body_N_colored.sat`。
- 写 `body_N_step1_result.json`。
- 发出 checkpoint save 事件。

输入：

- `store`：已配置 root 的 `CheckpointStore`。
- `body_index`：当前 body 序号。
- `result`：Step1 内存结果。
- `diagnostics`：用于写 checkpoint 事件。

输出和副作用：

- 成功返回 `TRUE`。
- 写入 checkpoint 文件。
- 写入 `checkpoint + step1` 事件。

### `RestoreStep1Checkpoint`

```cpp
logical RestoreStep1Checkpoint(
    const CheckpointStore& store,
    int body_index,
    BODY*& out_body,
    Step1FaceAnalyzeResult& out_result,
    DiagnosticSink* diagnostics);
```

职责：

- 读取 Step1 JSON。
- restore Step1 colored SAT。
- 收集 restored body 中的 `FACE*`。
- 按 `face_id` 绑定 `FaceRecord.face` 和 `surface_geometry`。
- 发出 checkpoint restore 事件。

输出：

- `out_body`：从 SAT restore 得到的 body。
- `out_result`：可直接传给 `RunStep2GroupBuild` 的 Step1 result。

## 4. JSON 字段

顶层字段：

- `schema`
- `body_index`
- `ok`
- `stats`
- `faces`
- `adjacency`

`faces[]` 字段：

- `face_id`
- `face_type`
- `color`
- `edge_count`
- `adjacency_degree`
- `has_representative_point`
- `has_representative_normal`
- `has_area_proxy`
- `has_edge_lengths`
- `representative_point`
- `representative_normal`
- `area_proxy`
- `edge_min`
- `edge_max`
- `valid`
- `invalid_reason`
- `sample_summary`

`adjacency[]` 字段：

- `face_a`
- `face_b`
- `shared_edge_count`
- `relation`

## 5. 调用流程

写 checkpoint：

```text
RunStep1FaceAnalyze
  -> Step1FaceAnalyzeResult
  -> SaveStep1Checkpoint
    -> AcisSatWriter.Save(body_N_colored.sat)
    -> CheckpointStore.WriteTextFile(body_N_step1_result.json)
```

从 Step2 恢复：

```text
RunMidSurface(start_method = step2_from_step1_checkpoint)
  -> RestoreStep1Checkpoint
    -> CheckpointStore.ReadTextFile
    -> api_restore_entity_list(body_N_colored.sat)
    -> api_get_faces(restored_body)
    -> bind FaceRecord pointers
  -> RunStep2GroupBuild(restored_step1.state, ...)
```

## 6. 限制

- 当前只接了 body index 0 的 resume 入口。
- 依赖 restored SAT 的 face 遍历顺序与 Step1 写 checkpoint 时一致。
- JSON 不保存 ACIS 指针；所有指针都必须恢复后重新绑定。
