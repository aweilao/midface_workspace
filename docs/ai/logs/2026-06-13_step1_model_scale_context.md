# 2026-06-13 Step1 model scale context

## 背景

讨论 Step6 split adjacency 时发现未来需要一套稳定的相对距离单位，但当前不希望改动既有算法容差。

本次只在 Step1 建立尺度基础数据，不让 Step2-Step7 消费该值。

## 改动

- 新增 `ModelScaleContext`。
- 在 `Step1FaceAnalyzeState` 中新增 `model_scale`。
- Step1 枚举 face edge 时按 `EDGE*` 去重收集有效 edge 长度。
- 计算规则：
  - 有效 edge：`edge->length(TRUE) > 1.0e-9`。
  - `reference_length = average(top 10 longest unique valid edge lengths)`。
  - `distance_unit = reference_length / 1000.0`。
  - 有效 edge 不足 10 条时使用已有有效 edge。
- 新增 Step1 `model-scale` 结构化日志。
- result JSON 和 Step1 checkpoint 写入/恢复 `model_scale`。
- 更新 `docs/document/step1_face_analyze_design.md` 和 `docs/document/logging_events.md`。

## 行为边界

- 不修改 Step2-Step7 的现有距离公式。
- 不新增 YAML 参数。
- 不改变 face edge length、adjacency、grouping、pairing 等既有算法路径。

## 后续

未来若要重构距离参数，可以从 `Step1FaceAnalyzeState.model_scale.distance_unit` 读取统一相对单位，再逐步把部分绝对容差迁移为 `units * distance_unit`。
