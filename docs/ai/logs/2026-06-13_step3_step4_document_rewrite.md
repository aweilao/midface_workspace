# 2026-06-13 Step3/Step4 工程文档重写

## 任务

用户要求继续按 Step1/Step2 的模板写后续 Step 文档。本次处理 Step3 和 Step4。

## 本次改动

- 重写 `docs/document/step3_pairing_design.md`。
- 重写 `docs/document/step4_relation_design.md`。

文档按以下结构组织：

- 当前源码事实源。
- 算法目标和主流程。
- 参数默认值和字段含义。
- Result/State 字段逐项说明。
- 自定义结构字段说明。
- 主要函数和 helper 流程。
- 日志 tag、properties、json_properties 和 debug SAT。

## 代码事实源

主要对照：

- `midface/steps/Step3PairBuild.hpp`
- `midface/steps/Step3PairBuild.cpp`
- `midface/steps/Step4RelationBuild.hpp`
- `midface/steps/Step4RelationBuild.cpp`
- `midface/core/MidSurfaceNewTypes.hpp`
- `midface/core/MidSurfaceNewTypes.cpp`
- `midface/MidSurface.cpp`

## 重要结论

- Step3 当前输入 `Step2GroupState`，输出 `Step3PairState`；Step4 输入 `Step3PairState`。
- Step3 的 wall 是未进入任何 pair 的 Step2 group candidate，`WallRecord` 只填部分字段。
- Step4 的 `walls` 是 MM2 virtual wall body，不是简单复制 Step3 wall。
- Step4 当前没有专属 debug SAT role。
- Step3 日志基础 tag 是 `step3`、`pairing`。
- Step4 日志基础 tag 是 `step4`、`relation`。

## 未做

- 未重写 Step5-Step7 文档。
- 未修改代码。
