# 2026-06-12 shared 文档按 steps/utils/log/run 重组

## 任务

用户希望重新整理文档体系，但不要继续扩出过多目录。最终确定只搬运和新增四类：

- `docs/shared/steps`
- `docs/shared/utils`
- `docs/shared/log`
- `docs/shared/run`

要求：旧文档不一定正确，本次以 `/root/midface_package/midface` 当前代码为事实源。

## 本次文档改动

新增 `docs/shared/README.md`，说明当前 shared 文档分层和事实源规则。

新增 `docs/shared/steps/`：

- `README.md`
- `step1_face_analyze.md`
- `step2_grouping.md`
- `step3_pairing.md`
- `step4_relation.md`
- `step5_mid_patch.md`
- `step6_trim_select.md`
- `step7_stitch_input.md`

新增 `docs/shared/utils/`：

- `README.md`
- `diagnostic_sink.md`
- `structured_event_writer.md`
- `result_json_writer.md`
- `debug_sat_builder.md`
- `face_sheet_body_sat_writer.md`
- `color_utils.md`
- `group_utils.md`
- `sampling_utils.md`
- `json_utils.md`
- `yaml_config_loader.md`

新增 `docs/shared/log/`：

- `README.md`
- `common.md`
- `step1.md`
- `step2.md`
- `step3.md`
- `step4.md`
- `step5.md`
- `step6.md`
- `step7.md`
- `debug_sat.md`
- `color_map.md`
- `result_json.md`

新增 `docs/shared/run/`：

- `README.md`
- `quick_start.md`
- `build.md`
- `yaml_config.md`
- `run_context.md`
- `step_options.md`
- `output_layout.md`
- `cases.md`

更新：

- `docs/ai/map.md`：把当前事实源路由到 `midface/`、`steps/`、`utils/`、`log/`、`run/`。
- `docs/shared/document/README.md`：标记为旧工程文档索引，说明当前事实源已迁移。

## 代码事实源

本次读取并按代码整理：

- `midface/Makefile`
- `midface/run_midface.cpp`
- `midface/MidSurface.h/.cpp`
- `midface/config/MidSurfaceConfigLoader.cpp`
- `midface/configs/cangduan.yaml`
- `midface/core/RunContext.*`
- `midface/core/DiagnosticSink.*`
- `midface/core/MidSurfaceNewTypes.*`
- `midface/utils/StructuredEventWriter.*`
- `midface/utils/ResultJsonWriter.cpp`
- `midface/utils/FaceSheetBodySatWriter.*`
- `midface/utils/DebugSatBuilder.*`
- `midface/steps/Step1FaceAnalyze.*` 到 `Step7Stitch.*`

## 重要纠偏

- 当前有效源码目录按 `midface/` 记录，`midface_new/` 不再作为默认事实源。
- 当前构建入口按 `make -C midface` 记录。
- YAML 配置字段以 `MidSurfaceConfigLoader.cpp` 为准。
- 日志不是对象 `toString()`，而是 `StructuredEvent(tags/properties/json_properties)` 写 JSONL。
- `IntText` 等 helper 只是转 string；真正 raw JSON 字段通过 `SetJsonProperty` 写入。
- Step7 当前不接 `DiagnosticSink*`，不写专属 JSONL，且有两个硬编码 SAT 输出路径：
  - `output/step7_input_split_faces_body_0.sat`
  - `output/step7_first_split_with_origin_faces_body_0.sat`

## 未做

- 未删除旧 `docs/shared/document/`、`docs/shared/algorithm/` 文档。
- 未重写 `docs/ai/state.md` 和 `docs/ai/command.md` 的全部旧状态；本次只更新 `map.md` 路由。
- 未改代码和算法。

## 后续建议

- 下一步可以把 `docs/ai/state.md`、`docs/ai/command.md` 更新到当前 `midface` 独立包事实。
- 若继续清理旧文档，优先在旧文档顶部加迁移指向，不要一次性删除。
