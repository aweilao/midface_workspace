# 2026-06-13 Step1/Step2 工程文档重写

## 任务

用户要求当前只专注 `docs/document`，先按当前 `midface/` 源码写 Step1 和 Step2 文档。

要求文档结构：

- 先介绍算法。
- 再介绍参数设置。
- 详细介绍本级 state，逐个解释字段；字段若是自定义结构，也解释其字段。
- 介绍每个函数的作用和内部简单流程，体现调用关系。
- 介绍日志输出的 tag、properties 和含义。

## 本次改动

- 更新 `docs/document/README.md`，把文档入口改为当前 `midface` 包事实源。
- 新增 `docs/document/step1_face_analyze_design.md`。
- 重写 `docs/document/step2_grouping_design.md`，移除旧 `midface_new` 口径，改为当前 `midface/steps/Step2GroupBuild.*` 事实。

## 代码事实源

本次主要对照：

- `midface/Makefile`
- `midface/MidSurface.cpp`
- `midface/steps/Step1FaceAnalyze.hpp`
- `midface/steps/Step1FaceAnalyze.cpp`
- `midface/steps/Step2GroupBuild.hpp`
- `midface/steps/Step2GroupBuild.cpp`
- `midface/core/MidSurfaceNewTypes.hpp`
- `midface/core/MidSurfaceNewTypes.cpp`
- `midface/utils/SamplingUtils.hpp`

## 重要结论

- 当前包内 Makefile 编译 `midface/steps/Step1FaceAnalyze.cpp` 和 `midface/steps/Step2GroupBuild.cpp`。
- Step1 输出 `Step1FaceAnalyzeResult.state`，Step2 通过 `RunStep2GroupBuild(result.step1.state, ...)` 读取上一级 state。
- Step2 输出 `Step2GroupResult.state`，后续 Step3 通过 `RunStep3PairBuild(result.step2.state, ...)` 继续读取。
- Step1 的 `sample_summary` vector 当前未被填充，摘要文本写在 `FaceRecord.sample_summary`。
- Step2 中 `axis_angle_deg`、`auto_tolerance`、`auto_edge_length_eps`、`emit_group_artifacts`、`emit_traced_pair_samples` 当前保留配置语义，但没有完整驱动独立行为。

## 未做

- 未重写 Step3-Step7 文档。
- 未把日志输出拆成独立日志字典文件。
- 未修改代码。
