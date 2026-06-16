# 2026-06-13 Step5/Step6 工程文档重写

## 任务

继续按当前 `midface/` 源码重写 Step 文档。本次处理 Step5 和 Step6。

## 本次改动

- 重写 `docs/document/step5_mid_patch_design.md`。
- 新增 `docs/document/step6_trim_select_design.md`。
- 更新 `docs/document/README.md` 的 Step 文档索引。

## 代码事实源

主要对照：

- `midface/steps/Step5MidPatchBuild.hpp`
- `midface/steps/Step5MidPatchBuild.cpp`
- `midface/steps/Step6TrimSelect.hpp`
- `midface/steps/Step6TrimSelect.cpp`
- `midface/steps/Step7StitchInput.hpp`
- `midface/core/MidSurfaceNewTypes.hpp`
- `midface/core/MidSurfaceNewTypes.cpp`

## 重要结论

- Step5 输入 `Step4RelationState`，输出 `Step5MidPatchState`。
- Step5 的 `patches` 保存中面 patch 记录，`junctions` 来自 Step4 pair relation。
- Step5 debug SAT role 是 `step5.mid_patch_faces`。
- Step6 输入 `Step5MidPatchState`，输出 `Step6TrimSelectState`。
- Step6 会构造 `Step7StitchInput`，Step7 实际读取的是 `result.step6.state.step7_stitch_input`。
- Step6 当前 state 中 `raw_imprint_edges` 和 `slice_edge_uses` 有结构定义，但主流程没有系统填充。

## 未做

- 未重写 Step7 文档。
- 未修改代码。
