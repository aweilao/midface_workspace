# 2026-06-13 Step7 工程文档重写

## 任务

继续按当前 `midface/` 源码重写 Step7 文档。

## 本次改动

- 重写 `docs/document/step7_final_export_design.md`。
- 更新 `docs/document/README.md` 的 Step7 索引说明。

## 代码事实源

主要对照：

- `midface/steps/Step7Stitch.hpp`
- `midface/steps/Step7Stitch.cpp`
- `midface/steps/Step7StitchInput.hpp`
- `midface/utils/FaceSheetBodySatWriter.hpp`

## 重要结论

- Step7 当前仍是 stitch 占位接口。
- Step7 输入来自 `Step6TrimSelectState.step7_stitch_input`。
- Step7 当前不接收 `DiagnosticSink*`，不写结构化日志。
- Step7 当前直接写固定路径 SAT：
  - `output/step7_input_split_faces_body_0.sat`
  - `output/step7_first_split_with_origin_faces_body_0.sat`
- `WriteFirstSplitWithUpstreamFacesSat` 当前固定访问 `split_to_raw_faces[5]`，没有检查 size 大于 5，这是实现边界风险。

## 未做

- 未修改 Step7 代码。
