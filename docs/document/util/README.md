# util 工程文档入口

本目录记录当前 `midface/utils/` 和少量 `midface/core/` 公共工具的代码事实。文档以 `/root/midface_package/midface` 当前源码为准。

## 文档列表

- `color_utils.md`：颜色 palette、按 id 取色、ACIS entity/face 染色。
- `diagnostic_sink.md`：Step 日志、debug SAT、color-map 输出的统一入口。
- `structured_event_writer.md`：结构化事件 JSONL writer。
- `sat_writers.md`：`AcisSatWriter`、`DebugSatBuilder`、`FaceSheetBodySatWriter`。
- `sampling_utils.md`：face 边界/内部采样工具。
- `group_utils.md`：group 面积、尺度、采样、最近点工具。
- `json_utils.md`：JSON 字符串、点、raw JSON property 辅助函数。
- `result_json_writer.md`：pipeline result JSON 快照输出。

## 写作规则

- 先读 `midface/utils/*.hpp/.cpp` 或 `midface/core/*.hpp/.cpp`。
- 不照搬旧文档；旧文档只作底稿。
- 每篇说明谁调用、输入输出、字段/参数含义、当前限制。
