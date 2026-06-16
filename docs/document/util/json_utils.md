# JsonUtils 工程文档

源码事实源：

- `midface/utils/JsonUtils.hpp`
- `midface/utils/JsonUtils.cpp`
- `midface/third_party/nlohmann/json.hpp`

## 作用

`JsonUtils` 是包内写 JSON 字符串和 JSON value 的小工具。主要用于：

- 结构化事件 raw JSON property。
- result JSON writer。
- sample 点坐标输出。

## 类型

```cpp
typedef nlohmann::json JsonValue;
```

## 函数说明

### `JsonString`

```cpp
std::string JsonString(const std::string& value);
```

返回 JSON string literal，例如输入 `abc` 输出 `"abc"`。

`StructuredEventWriter` 用它转义 tag、property key、property value。

### `JsonBool`

```cpp
std::string JsonBool(logical value);
```

把 ACIS `logical` 转成 JSON bool 字符串：

- `TRUE` -> `true`
- `FALSE` -> `false`

### `JsonPoint`

```cpp
JsonValue JsonPoint(const SPAposition& point);
```

输出三元素数组：

```json
[x, y, z]
```

### `JsonDump`

```cpp
std::string JsonDump(const JsonValue& value);
```

调用 `value.dump()` 输出紧凑 JSON。

### `IsLikelyJsonValue`

```cpp
logical IsLikelyJsonValue(const std::string& value);
```

调用 `JsonValue::accept(value)` 判断字符串是否为合法 JSON。

`StructuredEventWriter` 写 `json_properties` 时会用它检查；不合法的 raw JSON property 会被跳过。

## 当前边界

- 没有 pretty print 参数；pretty JSON 由 `ResultJsonWriter` 直接调用 `dump(2)`。
- `JsonBool` 返回字符串，`JsonBoolValue` 这种返回 JsonValue 的函数在 `ResultJsonWriter.cpp` 内部单独定义。
