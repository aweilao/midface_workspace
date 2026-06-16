# JsonUtils 与结构化 JSON 日志

本文档记录 `midface_new/utils/JsonUtils.hpp/.cpp` 和 `StructuredEvent` 中 JSON 字段输出的职责。

`JsonUtils` 当前是项目对 `nlohmann/json` 的薄封装。`midface_new` 自带官方 single header：

```text
midface_new/third_party/nlohmann/json.hpp
```

include 入口使用：

```cpp
#include "third_party/nlohmann/json.hpp"
```

Step 和 writer 代码优先通过 `JsonUtils` 使用 JSON 能力，避免在各个算法文件中散落第三方库 include。外层临时 `json/` 仓库只是来源，不作为 `midface_new` 的运行依赖。

## 1. 背景

`StructuredEvent.properties` 的基础类型是 `std::map<std::string, std::string>`，普通字段会被 `StructuredEventWriter` 写成 JSON string。

对于 Step2 refine 的 `samples` 这类天然是 list/object 的字段，如果直接放进 `properties`，会变成：

```json
"samples": "[{\"i\":0,...}]"
```

这会让 JSON 数组被二次字符串化，不方便阅读和工具解析。

## 2. 数据结构

### `StructuredEvent::json_properties`

```cpp
std::map<std::string, std::string> json_properties;
```

字段含义：

- key：property 名称。
- value：已经构造好的 JSON value 片段，例如数组、对象、数字、布尔值或 `null`。

输出规则：

- `properties` 仍然按 JSON string 写出。
- `json_properties` 直接作为 JSON value 写出。
- writer 会用 `IsLikelyJsonValue` 做轻量检查，避免明显不是 JSON value 的片段被原样写入。

### `SetJsonProperty`

```cpp
StructuredEvent& SetJsonProperty(const std::string& name, const std::string& json_value);
```

职责：

- 把一个字段写入 `json_properties`。
- 用于 `samples` 这类 list/object 字段。

## 3. JsonUtils API

### `JsonValue`

```cpp
typedef nlohmann::json JsonValue;
```

职责：

- 作为 `midface_new` 内部统一使用的 JSON value 类型别名。
- 当前用于 Step2 refine samples 的 array/object 构造。

### `JsonString`

```cpp
std::string JsonString(const std::string& value);
```

职责：

- 调用 `nlohmann::json(value).dump()` 输出 JSON string。
- 返回带双引号的 JSON string。

当前使用方：

- `StructuredEventWriter` 写 tags、property key、普通 string property value。

### `JsonBool`

```cpp
std::string JsonBool(logical value);
```

职责：

- 调用 `nlohmann::json(value != FALSE).dump()` 输出 JSON 布尔值文本：`true` 或 `false`。

当前使用方：

- Step2 refine samples 中的 `distance_ok`、`normal_ok`、`tangent_ok`、`pass`。

### `JsonPoint`

```cpp
JsonValue JsonPoint(const SPAposition& point);
```

职责：

- 把 ACIS `SPAposition` 转为 JSON 数组 `[x, y, z]`。

当前使用方：

- Step2 refine samples 的 `p` 和 `cp` 字段。

### `JsonDump`

```cpp
std::string JsonDump(const JsonValue& value);
```

职责：

- 调用 `value.dump()` 输出 JSON value 字符串。
- 用于 `SetJsonProperty` 的 raw JSON value。

### `IsLikelyJsonValue`

```cpp
logical IsLikelyJsonValue(const std::string& value);
```

职责：

- 调用 `nlohmann::json::accept(value)` 检查 raw JSON value 是否能被解析。

限制：

- 只做合法性检查，不返回解析后的值。
- `StructuredEventWriter` 仍直接写入原始字符串。

## 4. Step2 refine samples

Step2 refine 事件现在使用：

```cpp
event.SetJsonProperty("samples", SamplesJson(refine.nearest_hits));
```

`SamplesJson` 内部使用 `JsonValue::array()` 和 `JsonValue::object()` 构造数组/对象，不再手拼 JSON 字符串。

输出形态：

```json
"samples": [
  {
    "i": 0,
    "p": [1.31785, 0, 0.89],
    "cp": [1.31785, 0, -0.89],
    "dist": 1.78,
    "distance_ok": false,
    "normal_ok": true,
    "tangent_ok": true,
    "pass": false
  }
]
```

含义：

- `i`：采样点序号。
- `p`：source face 上的采样点坐标。
- `cp`：target face 上的最近点坐标。
- `dist`：采样点到 target face 的距离。
- `distance_ok`：距离 gate 是否通过。
- `normal_ok`：法向 gate 是否通过。
- `tangent_ok`：切向 gate 是否通过。
- `pass`：该采样点是否通过三 gate。

注意：

- JSON 字段顺序由 `nlohmann/json` 控制，不作为语义依据。
- double 输出使用库的默认 `dump()` 精度，可能比手写日志显示更多小数位。
