# 2026-06-13 util 工程文档首批整理

## 任务

用户要求在 `docs/document` 下开一个 `util` 文件夹，开始写公共工具类文档。旧文档可以参考，但必须按当前 `midface/` 代码验证。

同时用户询问当前输出是否可以自动染成不同颜色，已通过 `ColorUtils` 和 Step 调用点核对。

## 本次改动

新增 `docs/document/util/`：

- `README.md`
- `color_utils.md`
- `diagnostic_sink.md`
- `structured_event_writer.md`
- `sat_writers.md`
- `sampling_utils.md`
- `group_utils.md`
- `json_utils.md`
- `result_json_writer.md`

更新：

- `docs/document/README.md`：增加 `util/` 入口。

## 代码事实源

主要对照：

- `midface/utils/ColorUtils.hpp/.cpp`
- `midface/core/DiagnosticSink.hpp/.cpp`
- `midface/core/RunContext.hpp/.cpp`
- `midface/utils/StructuredEventWriter.hpp/.cpp`
- `midface/utils/AcisSatWriter.hpp/.cpp`
- `midface/utils/DebugSatBuilder.hpp/.cpp`
- `midface/utils/FaceSheetBodySatWriter.hpp/.cpp`
- `midface/utils/SamplingUtils.hpp/.cpp`
- `midface/utils/GroupUtils.hpp/.cpp`
- `midface/utils/JsonUtils.hpp/.cpp`
- `midface/utils/ResultJsonWriter.hpp/.cpp`

## 染色结论

- Step1 `step1.colored_body`：自动按 face 染色。
- Step2 `step2.group_colored_body`：自动按 group 染色。
- Step3 `step3.pair_colored_body`：自动按 pair/wall 染色。
- Step6 `step6.selected_faces`：自动按 selected face 染色。
- Step5 `step5.mid_patch_faces`：当前没有主动 palette 染色。
- Step6 `extended_mid_faces`、`extended_wall_faces`、`preselect_split_faces`：当前没有统一 palette 染色。
- Step7 当前没有主动染色。

## 未做

- 未写 `PathUtils`、`CheckpointStore`、`Step1Checkpoint`、`TraceFilter`、`IdUtils`、config loader 等文档。
- 未修改代码。
