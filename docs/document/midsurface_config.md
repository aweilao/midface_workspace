# MidSurface 配置入口

本文档记录 `midface_new/MidSurface.h/.cpp` 和 `core/RunContext` 中当前稳定的主线入口、配置结构和 debug 输出职责。

## 1. 主线定位

`MidSurface` 是 `midface_new` 的 Step1 到 Step7 主线入口。旧的 `MidSurfaceNewPipeline` 命名已经收敛为更短的主线命名：

- `MidSurfaceConfig`：一次运行的总配置。
- `MidSurfaceResult`：一次运行的总结果。
- `RunMidSurface`：主线启动函数。

`midface_new/MidFace.*` 仍然是最外层适配入口，负责承接 SAT path、`ENTITY_LIST` 或 `BODY*` 调用，再转入 `RunMidSurface`。

## 2. 数据结构

### `MidSurfaceConfig`

```cpp
struct MidSurfaceConfig
{
    void EnableDebugSatOutput(const char* artifact_role);
    void EnableDefaultDebugSatOutputs();
    void EnableStep3PairColoredBodyDebugSat();

    RunContextOptions run_context;
    Step1FaceAnalyzeOptions step1;
    Step2GroupOptions step2;
    Step3PairOptions step3;
    Step4RelationOptions step4;
    Step5MidPatchOptions step5;
    Step6TrimSelectOptions step6;
};
```

字段含义：

- `run_context`：运行入口、输入输出目录、日志、debug SAT 输出选择、停止 step 等跨 step 配置。
- `step1` 到 `step6`：每一步自己的 options，主线只负责按顺序传递。Step7 当前是纯 `RunStep7Stitch` 直通函数，不需要 options。

字段来源：

- 调用方直接构造或由 `MidFaceBuildOptions.pipeline` 持有。

后续消费：

- `RunMidSurface` 读取 `run_context` 创建 `RunContext`。
- 每个 Step 只消费自己的 options。

函数职责：

- `EnableDebugSatOutput`：向 `run_context.debug_sat_outputs` 追加一个 debug SAT role；若 role 已存在则不重复追加。
- `EnableDefaultDebugSatOutputs`：启用当前默认观察输出，即 `step1.colored_body` 和 `step2.group_colored_body`。
- `EnableStep3PairColoredBodyDebugSat`：启用 Step3 pair 染色后的 body SAT 输出，即 `step3.pair_colored_body`。

### `MidSurfaceResult`

```cpp
struct MidSurfaceResult
{
    logical ok;
    int completed_step;
    Step1FaceAnalyzeResult step1;
    Step2GroupResult step2;
    Step3PairResult step3;
    Step4RelationResult step4;
    Step5MidPatchResult step5;
    Step6TrimSelectResult step6;
    Step7StitchResult step7;
};
```

字段含义：

- `ok`：主线本次运行是否按当前停止点得到成功结果。
- `completed_step`：最后完成的 step 编号。
- `step1` 到 `step7`：对应 step 的结果快照。

## 3. `RunContextOptions`

当前跨 step 运行参数包括：

- `start_method`：配置入口使用的启动函数选择。当前支持 `sat_pipeline`；`BODY*` 入口直接由重载函数启动。
- `input_sat_path`：SAT 文件入口的输入文件。
- `output_root`：默认输出根目录。
- `output_sat_dir`：debug SAT 输出目录；为空时使用 `output_root/sat`。
- `output_log_dir`：结构化日志输出目录；为空时使用 `output_root/logs`。
- `stop_after_step`：跑到指定 step 后直接停止。
- `debug_sat_outputs`：需要输出的 debug SAT role 列表。
- `checkpoint_outputs`：需要持久化的 checkpoint role 列表。当前支持 `step1.result`。
- `resume_from_checkpoint`：预留字段，当前恢复逻辑主要由 `start_method` 选择。

`RunContext::Configure` 会把 `debug_sat_outputs` 转成一次运行内的 set。判断输出时使用：

当前已定义的 debug SAT role：

- `step1.colored_body`
- `step2.group_colored_body`
- `step3.pair_colored_body`

判断输出时使用：

```cpp
context.ShouldOutputDebugSat("step3.pair_colored_body");
```

路径生成使用：

```cpp
context.DebugSatPath("step2.group_colored_body", body_index);
```

输出文件名会把 role 中的非字母数字字符替换为 `_`。

日志文件布局：

- `<output_log_dir>/events.jsonl`：全局 pipeline 事件，例如整次运行 finish。
- `<output_log_dir>/body_<index>_events.jsonl`：某个 body 的 Step1/2/3/4 算法事件、debug SAT 输出事件、result JSON 输出事件。

这和 debug SAT 的 body index 后缀保持一致，方便多 BODY 调试时只看一个 body。

## 4. 入口函数

### `RunMidSurface(BODY*, const MidSurfaceConfig&, MidSurfaceResult&)`

输入：

- `body`：单个 ACIS `BODY*`。
- `config`：主线配置。
- `result`：输出结果。

流程：

1. 创建并配置 `RunContext`。
2. 创建 `DiagnosticSink`。
3. 按 Step1 到 Step7 顺序调用。
4. 每个 Step 完成后检查 `stop_after_step`。
5. debug SAT 由具体业务 Step 在合适位置通过 `DiagnosticSink` 输出；主线不在 Step 尾部统一补输出。

副作用：

- 可能写 `events.jsonl`。
- 多 BODY 时，body 内算法日志写到 `body_<index>_events.jsonl`。
- 可能写 debug SAT。
- Step1 和 Step2 会修改输入 body 上 face 的颜色。

### `RunMidSurface(const MidSurfaceConfig&, MidSurfaceResult&)`

输入：

- `config.run_context.start_method` 必须为 `sat_pipeline`。
- `config.run_context.input_sat_path` 必须指向 SAT 文件。

流程：

1. restore SAT 到 `ENTITY_LIST`。
2. 收集其中的 `BODY*`。
3. 对每个 body 调用共享主线逻辑。
4. 多 body 情况下，debug SAT 文件名带 `body_index`。

当前限制：

- 只接入 `sat_pipeline`。
- 不在该入口写最终业务 SAT；`output_sat_dir` 只用于 debug SAT。

## 5. Debug SAT Role

当前支持：

- `step1.colored_body`：Step1 统一染色完成后的 body。
- `step2.group_colored_body`：Step2 grouping 后按 group 染色的 body。
- `step3.pair_colored_body`：Step3 后按 pair/wall 染色的 body。

这些输出只用于调试，不改变主线结果。

## 6. Result JSON 快照

主线每个 body 运行到当前停止点或最终 Step 后，会写一次 `MidSurfaceResult` JSON 快照：

```text
<output_root>/results/body_<body_index>_result.json
```

例如：

```text
run_midface_new_test11_step4/results/body_0_result.json
```

触发时机：

- Step1 到 Step7 任意一步因 `stop_after_step` 停止。
- Step7 正常结束。
- 某一步失败返回前，会写入当前已完成 step 的快照。

内容范围：

- `ok`
- `completed_step`
- Step1 到已完成 Step 的 stats。
- face/group/pair/wall/relation 等轻量 record。
- `group_to_pairs`、`group_to_walls`、`face_to_group` 等 map。

不写入：

- `BODY*`
- `FACE*`
- `SURFACE*`
- `ENTITY*`
- 其他 ACIS 指针。

定位：

- Result JSON 是 debug/审阅快照，不是完整恢复 checkpoint。
- 需要恢复继续运行时仍然使用专门 checkpoint，例如当前已有的 `step1.result`。
- 写出成功或失败会在 `events.jsonl` 中记录 `pipeline + result-json + output + ok/fail`。

## 7. 启动调用流程总览

`midface_new` 当前按三层入口理解：

```text
MidFace.*
  -> MidSurface.*
    -> Step1 ... Step7
      -> RunContext / DiagnosticSink
```

职责分层：

- `MidFace.*`：最外层适配层，保留 SAT path、`ENTITY_LIST`、`BODY*` 等调用形式。
- `MidSurface.*`：主线层，负责按 `MidSurfaceConfig` 调度 Step1 到 Step7。
- `RunContext`：保存一次运行的入口参数、输出目录、日志目录、debug SAT role set。
- `DiagnosticSink`：业务代码使用的诊断输出门面，实际写结构化事件、SAT artifact 事件和 debug SAT。

### 7.1 配置 SAT 入口

调用示例：

```cpp
midsurface_new::MidSurfaceConfig config;
config.run_context.start_method = "sat_pipeline";
config.run_context.input_sat_path = ".../input.sat";
config.run_context.output_sat_dir = ".../debug_sat";
config.run_context.output_log_dir = ".../logs";
config.run_context.stop_after_step = 3;
config.EnableDefaultDebugSatOutputs();
config.EnableStep3PairColoredBodyDebugSat();

midsurface_new::MidSurfaceResult result;
midsurface_new::RunMidSurface(config, result);
```

完整流程：

```text
RunMidSurface(config)
  -> check start_method == "sat_pipeline"
  -> check input_sat_path
  -> RunContext.Configure(config.run_context)
    -> create output_sat_dir
    -> create output_log_dir
    -> open output_log_dir/events.jsonl
    -> convert debug_sat_outputs to run-scoped set
  -> DiagnosticSink.Configure(&context)
  -> restore SAT to ENTITY_LIST
  -> collect BODY*
  -> for each BODY:
    -> RunMidSurfaceBodyWithContext
      -> stop/final/failure:
        -> write output_root/results/body_<index>_result.json
```

### 6.2 单 BODY 主线入口

调用示例：

```cpp
midsurface_new::RunMidSurface(body, config, result);
```

完整流程：

```text
RunMidSurface(body, config, result)
  -> RunContext.Configure(config.run_context)
  -> DiagnosticSink.Configure(&context)
  -> RunMidSurfaceBodyWithContext(body_index = 0)
```

### 6.3 单个 BODY 内部 Step 流程

```text
RunMidSurfaceBodyWithContext
  -> DiagnosticSink.SetCurrentBodyIndex(body_index)
  -> emit pipeline start event

  -> RunStep1FaceAnalyze
    -> analyze face records
    -> apply Step1 face colors
    -> if debug_sat_outputs contains "step1.colored_body":
      -> diagnostics.EmitBodySatIfEnabled("step1.colored_body", body)
    -> fill Step1FaceAnalyzeResult
  -> completed_step = 1
  -> if stop_after_step <= 1:
    -> finish this body

  -> RunStep2GroupBuild
    -> surface prefilter
    -> sample refine with three gates
    -> build groups by union-find
    -> apply group colors
    -> if debug_sat_outputs contains "step2.group_colored_body":
      -> diagnostics.EmitBodySatIfEnabled("step2.group_colored_body", input_body)
    -> fill Step2GroupResult
  -> completed_step = 2
  -> if stop_after_step <= 2:
    -> finish this body

  -> RunStep3PairBuild
    -> build candidate group pairs
    -> refine samples by opposite-normal and direction gates
    -> select pairs and filter per-group thickness outliers
    -> apply pair colors
    -> if debug_sat_outputs contains "step3.pair_colored_body":
      -> diagnostics.EmitBodySatIfEnabled("step3.pair_colored_body", input_body)
    -> fill Step3PairResult
  -> completed_step = 3
  -> if stop_after_step <= 3:
    -> finish this body

  -> RunStep4RelationBuild
  -> RunStep5MidPatchBuild
  -> RunStep6TrimSelect
  -> RunStep7Stitch

  -> result.ok = final step ok
  -> emit pipeline finish event
```

### 6.4 外层 `MidFace.*` 适配流程

`MidFace.*` 仍然可以作为兼容外层调用的入口：

```text
BuildMidFaceSatFileEx
  -> RestoreSatFileToEntityList
  -> BuildMidFaceEntitiesFromEntityList
    -> collect BODY*
    -> RunMidSurface(body, options.pipeline, pipeline_result)
    -> read step7 final exports
  -> AcisSatWriter.Save(output_sat_path)
```

注意：

- `RunMidSurface(config)` 的 `output_sat_dir` 只用于 debug SAT，不是最终业务 SAT 文件。
- 当前 Step3 以后仍是后续实现区间；调试 Step1/Step2 时建议设置 `stop_after_step = 2`。
- 临时 debug 输出应优先写在业务代码附近，通过 `DiagnosticSink` 统一判断 role 和生成路径；不要在 pipeline 末尾集中补输出。

## 7. DiagnosticSink 业务输出接口

`DiagnosticSink` 是 Step 代码面向日志和 debug artifact 的统一出口。Step 不需要直接拼 output path，也不需要直接读取 `RunContextOptions`。

### `SetCurrentBodyIndex`

```cpp
void SetCurrentBodyIndex(int body_index);
```

职责：

- 由 `RunMidSurfaceBodyWithContext` 在进入单 body 处理前设置。
- 后续业务 Step 输出 SAT artifact 时，文件名使用该 body index。

### `EmitBodySatIfEnabled`

```cpp
logical EmitBodySatIfEnabled(const char* artifact_role, BODY* body);
```

职责：

- 检查 `debug_sat_outputs` 是否包含 `artifact_role`。
- 若未启用该 role，直接返回 `TRUE`，不写文件。
- 若启用，则调用 `RunContext::DebugSatPath` 生成路径。
- 写出 body SAT。
- 同时发出 `diagnostic + artifact:sat` 结构化事件。

典型调用位置：

```cpp
// Step1 完成统一染色后
diagnostics->EmitBodySatIfEnabled("step1.colored_body", body);

// Step2 完成 group 染色后
diagnostics->EmitBodySatIfEnabled("step2.group_colored_body", step1.input_body);

// Step3 完成 pair 染色后
diagnostics->EmitBodySatIfEnabled("step3.pair_colored_body", step2.input_step1->input_body);
```

设计约定：

- pipeline 只负责调度和 stop 判断。
- 业务 Step 自己决定“什么时候的状态值得输出”。
- 后续如果要临时输出采样点、局部 face set、某个 candidate 的中间 SAT，也应优先在对应业务函数附近通过 `DiagnosticSink` 增加输出。

## 8. Step checkpoint 与从 Step2 启动

checkpoint 配置也统一放在 `RunContextOptions` 中。

### 8.1 写 Step1 checkpoint

配置示例：

```cpp
config.run_context.start_method = "sat_pipeline";
config.run_context.enable_checkpoints = TRUE;
config.run_context.checkpoint_root = ".../checkpoints";
config.run_context.checkpoint_outputs.push_back("step1.result");
config.run_context.stop_after_step = 1;
```

运行流程：

```text
RunMidSurface(config)
  -> restore input SAT
  -> RunStep1FaceAnalyze
  -> SaveStep1Checkpoint
    -> checkpoints/step1/body_0_colored.sat
    -> checkpoints/step1/body_0_step1_result.json
  -> stop_after_step = 1
```

`body_0_colored.sat` 保存 Step1 染色后的 ACIS body。`body_0_step1_result.json` 保存可持久化的 Step1 face records 和 adjacency。

### 8.2 从 Step1 checkpoint 直接跑 Step2

配置示例：

```cpp
config.run_context.start_method = "step2_from_step1_checkpoint";
config.run_context.enable_checkpoints = TRUE;
config.run_context.checkpoint_root = ".../checkpoints";
config.run_context.stop_after_step = 2;
```

运行流程：

```text
RunMidSurface(config)
  -> RestoreStep1Checkpoint
    -> restore checkpoints/step1/body_0_colored.sat
    -> read checkpoints/step1/body_0_step1_result.json
    -> collect FACE* from restored body
    -> bind FaceRecord.face / surface_geometry by face_id
  -> RunStep2GroupBuild(restored_step1.state, ...)
```

### 8.3 当前 checkpoint 内容

Step1 JSON checkpoint 保存：

- `schema`
- `body_index`
- `ok`
- `stats.face_count / valid_face_count / invalid_face_count / adjacency_count`
- `faces[]`
  - `face_id`
  - `face_type`
  - `color`
  - `representative_point`
  - `representative_normal`
  - `area_proxy`
  - `edge_min / edge_max`
  - `edge_count`
  - `adjacency_degree`
  - `valid / invalid_reason`
  - `sample_summary`
- `adjacency[]`
  - `face_a`
  - `face_b`
  - `shared_edge_count`
  - `relation`

不会直接保存 ACIS 指针。恢复时通过 restored SAT 重新获得 `BODY*` / `FACE*` / `SURFACE*`。

当前限制：

- 先支持 body index 0 的恢复入口。
- Step2 resume 依赖 Step1 checkpoint 中 face 顺序和 restored body face 顺序一致。
- 若要多 body resume，后续需要让配置指定 body index 列表或批量恢复策略。
