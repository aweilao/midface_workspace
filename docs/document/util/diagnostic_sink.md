# DiagnosticSink / RunContext 工程文档

源码事实源：

- `midface/core/DiagnosticSink.hpp`
- `midface/core/DiagnosticSink.cpp`
- `midface/core/RunContext.hpp`
- `midface/core/RunContext.cpp`

## 作用

`RunContext` 管理一次 pipeline 运行的输出目录、日志 writer、debug SAT 开关、checkpoint store 和 trace filter。

`DiagnosticSink` 是 Step 代码使用的输出入口。Step 不直接管理文件路径，而是通过 `DiagnosticSink` 写：

- 结构化事件。
- debug SAT。
- face set SAT。
- color-map 事件。
- trace 判断。

## `RunContextOptions`

```cpp
struct RunContextOptions
{
    std::string run_id;
    std::string workspace_root;
    std::string start_method;
    std::string input_sat_path;
    std::string output_root;
    std::string output_sat_dir;
    std::string output_log_dir;
    std::string checkpoint_root;
    int stop_after_step;
    std::vector<std::string> debug_sat_outputs;
    std::vector<std::string> checkpoint_outputs;
    std::string resume_from_checkpoint;
    logical enable_diagnostics;
    logical append_diagnostics;
    logical enable_checkpoints;
    TraceFilterOptions trace;
};
```

默认值：

- `workspace_root = "."`
- `start_method = "body_pipeline"`
- `output_root = "test/out/midface_new"`
- `checkpoint_root = "test/out/midface_new/checkpoints"`
- `stop_after_step = PIPELINE_STEP7_FINAL_EXPORT`
- `enable_diagnostics = TRUE`
- `append_diagnostics = FALSE`
- `enable_checkpoints = FALSE`

注意：默认路径中仍有 `midface_new` 字样，这是当前代码默认值；实际 YAML 可覆盖。

## `RunContext` 字段

- `options_`：配置快照。
- `run_id_`：运行 id。未显式配置时由 `MakeRunId()` 生成。
- `output_root_`：输出根目录。
- `output_sat_dir_`：SAT 输出目录。未配置时为 `<output_root>/sat`。
- `output_log_dir_`：日志输出目录。未配置时为 `<output_root>/logs`。
- `debug_sat_output_set_`：debug SAT role 集合。
- `trace_filter_`：trace 过滤器。
- `checkpoint_store_`：checkpoint 存储入口。
- `event_writer_`：全局 events writer，写 `<output_log_dir>/events.jsonl`。
- `body_event_writers_`：按 body index 写 `body_<index>_events.jsonl`。

## `RunContext` 函数

### `Configure`

```cpp
logical Configure(const RunContextOptions& options);
```

流程：

1. 清理已有 body event writers。
2. 保存 options。
3. 生成或使用 `run_id`。
4. 规范化 output root、sat dir、log dir。
5. 构建 debug SAT role set。
6. 配置 trace filter 和 checkpoint store。
7. 创建 output root 和 sat dir。
8. 如启用 checkpoint，创建 checkpoint root。
9. 如启用 diagnostics，创建 log dir 并打开 `events.jsonl`。

### `ShouldOutputDebugSat`

```cpp
logical ShouldOutputDebugSat(const char* artifact_role) const;
```

检查 `artifact_role` 是否在 `debug_sat_outputs` 集合中。

### `DebugSatPath`

```cpp
std::string DebugSatPath(const char* artifact_role, int body_index) const;
```

生成 debug SAT 路径：

```text
<output_sat_dir>/<sanitized_artifact_role>_body_<body_index>.sat
```

`SanitizeArtifactRole` 会把非字母、数字、`-`、`_` 的字符替换成 `_`，所以 `step2.group_colored_body` 会变成类似：

```text
step2_group_colored_body_body_0.sat
```

### `Emit` / `EmitForBody`

`Emit` 写全局 `events.jsonl`。

`EmitForBody` 写 `body_<index>_events.jsonl`。当前 pipeline 的 `DiagnosticSink::EmitEvent` 使用 `EmitForBody(current_body_index_, event)`。

如果 `enable_diagnostics == FALSE`，这两个函数直接返回 `TRUE`，不写文件。

### `EmitSimple` / `EmitSimpleForBody`

复制 base event，追加 phase tag 和 message。`phase_tag` 若以 `event:` 开头，会去掉该前缀。

## `DiagnosticSink` 字段

```cpp
class DiagnosticSink
{
private:
    RunContext* context_;
    int current_body_index_;
};
```

- `context_`：当前运行上下文。
- `current_body_index_`：当前 body 编号。

## `DiagnosticSink` 函数

### `Configure`

保存 `RunContext*`。

### `SetCurrentBodyIndex`

设置当前 body index。`RunMidSurfaceBodyWithContext` 每个 body 开始时会调用。

### `EmitEvent`

```cpp
logical EmitEvent(const StructuredEvent& event);
```

如果没有 context，返回 `TRUE`。否则调用：

```cpp
context_->EmitForBody(current_body_index_, event)
```

### `EmitPointSet` / `EmitCrossMarkerSet`

当前只写简单事件：

- tags 包含 `points` 或 `cross-markers`
- tags 包含 `stepN`
- properties 只有 `count`

没有输出实际点坐标。

### `EmitBodySatIfEnabled`

```cpp
logical EmitBodySatIfEnabled(const char* artifact_role, BODY* body);
```

流程：

1. context 或 body 为空时返回 `TRUE`。
2. 若 `ShouldOutputDebugSat(artifact_role) == FALSE`，返回 `TRUE`。
3. 构造 `ENTITY_LIST`，加入 body。
4. 调用 `DebugSatPath` 生成路径。
5. 调用 `EmitEntityListSat`。

### `EmitEntityListSat`

使用 `AcisSatWriter::Save` 写 SAT。无论成功失败，都会输出一个事件：

tags：

```text
output
sat
ok|fail
```

properties：

- `role`：归一化后的 role。
- `path`：SAT 文件路径。

`NormalizeRole` 当前只去掉 `step1.`、`step2.`、`step3.` 前缀。Step5/Step6 role 不会被去掉前缀。

### `EmitFaceSetSat`

用 `DebugSatBuilder` 把 faces 加入 `ENTITY_LIST`，再调用 `EmitEntityListSat`。

注意：这里直接加入 `FACE*`，不是复制成 sheet body。

### `EmitColorIdMapEntryIfEnabled`

只有当对应 `artifact_role` 在 `debug_sat_outputs` 中时才输出 color-map event。

输入：

- `tags`：调用方提供基础 tags。
- `artifact_role`：对应 debug SAT role。
- `id_type`：当前函数忽略该参数。
- `properties`：普通字符串属性。
- `json_properties`：raw JSON 属性。

输出 event 会追加 tag：

```text
color-map
```

### `ShouldTraceFacePair`

调用 `context_->trace_filter().ShouldTraceFacePair(face_a, face_b)`。

## 当前调用关系

- `MidSurface.cpp` 创建 `RunContext` 并 `Configure`。
- `DiagnosticSink diagnostics; diagnostics.Configure(&context);`
- 每个 body 开始时 `diagnostics.SetCurrentBodyIndex(body_index)`。
- Step1-Step6 通过 `DiagnosticSink*` 写日志和 debug SAT。
- Step7 当前不接收 `DiagnosticSink*`。

## 当前边界

- `DiagnosticLevel level` 在 `EmitSimple`、`EmitStep*Event` 等多数地方只保留参数，当前未写入日志。
- `EmitPointSet` / `EmitCrossMarkerSet` 不输出点明细。
- `EmitColorIdMapEntryIfEnabled` 忽略 `id_type` 参数。
- Step5/Step6 的 SAT role 在 `NormalizeRole` 中不会去掉 `step5.`、`step6.` 前缀。
