# midface 工程文档入口

本目录记录当前 `/root/midface_package/midface` callable 包的长期工程事实。文档以当前源码为准，旧 `midface_new/` 只作为历史参考，不再作为默认事实源。

## 当前事实源

- workspace：`/root/midface_package`
- 当前源码：`/root/midface_package/midface`
- 当前包内构建入口：`/root/midface_package/midface/Makefile`
- 当前流水线入口：`midface/MidSurface.cpp`
- Step1-Step7 源码：`midface/steps/`
- 公共数据结构：`midface/core/MidSurfaceNewTypes.hpp`

## Step 文档

- `step1_face_analyze_design.md`：Step1 face analyze，记录 face 枚举、几何摘要、邻接构建、state 字段、函数流程和日志输出。
- `step2_grouping_design.md`：Step2 grouping，记录 face group 合并算法、参数、state 字段、函数流程和日志输出。
- `step3_pairing_design.md`：Step3 pairing，记录 group 厚度配对、wall candidate、state 字段、函数流程和日志输出。
- `step4_relation_design.md`：Step4 relation，记录 pair-wall/pair-pair 关系、virtual wall、state 字段、函数流程和日志输出。
- `step5_mid_patch_design.md`：Step5 mid patch，记录中面 patch 生成、state 字段、函数流程和日志输出。
- `step6_trim_select_design.md`：Step6 trim select，记录 trim/split/selection、Step7 输入构造、state 字段、函数流程和日志输出。
- `step7_final_export_design.md`：Step7 stitch，占位接口，记录 Step7 输入结构、当前 SAT 输出和实现边界。

## 公共文档

- `util/`：当前 `midface/utils/` 和部分 `midface/core/` 公共工具文档。
- `midsurface_config.md`：`MidSurfaceConfig`、`MidSurfaceResult`、`RunMidSurface` 和主线启动流程。
- `yaml_config.md`：YAML 配置文件启动方式、可覆盖字段和示例配置。
- `logging_events.md`：结构化日志的 tag/property 约定和历史事件字典。
- `step1_checkpoint.md`：Step1 checkpoint 保存、恢复、文件布局和 JSON 字段。
- `sampling_utils.md`：`SamplingUtils` 公共采样 API。
- `group_utils.md`：`GroupUtils` 公共 group API。
- `color_utils.md`：颜色 palette 分配和 ACIS face/entity 染色 API。
- `json_utils.md`：`nlohmann/json.hpp` 的包内使用方式和 raw JSON property 输出。

## 记录规则

- 写 Step 文档时先对照 `midface/Makefile` 和 `midface/steps/Step*.hpp/.cpp`。
- 每个 Step 文档按算法、参数、state 字段、函数流程、日志输出组织。
- state 字段要逐个解释；字段若是自定义结构，也继续解释其字段。
- 一次性任务过程、验证命令和临时限制写到 `docs/ai/logs/`，不要替代长期工程文档。
