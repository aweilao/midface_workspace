# StructuredEventWriter 工程文档

源码事实源：

- `midface/utils/StructuredEventWriter.hpp`
- `midface/utils/StructuredEventWriter.cpp`
- `midface/core/MidSurfaceNewTypes.hpp`

## 作用

`StructuredEventWriter` 把 `StructuredEvent` 写成 JSONL 文件。`RunContext` 使用它写：

- `<output_log_dir>/events.jsonl`
- `<output_log_dir>/body_<index>_events.jsonl`

## `StructuredEvent`

定义在 `MidSurfaceNewTypes.hpp`。

```cpp
struct StructuredEvent
{
    std::vector<std::string> tags;
    std::map<std::string, std::string> properties;
    std::map<std::string, std::string> json_properties;
};
```

- `tags`：事件标签数组。
- `properties`：普通字符串属性。写出时 key 和 value 都会 JSON string escape。
- `json_properties`：raw JSON 属性。value 必须是合法 JSON 字符串，否则会被跳过。

辅助函数：

- `Clone()`：返回自身副本。
- `AddTag(tag)`：tag 非空时加入 `tags`。
- `SetProperty(name, value)`：name 非空时写入 `properties`。
- `SetJsonProperty(name, json_value)`：name 非空时写入 `json_properties`。

## `StructuredEventWriter` 字段

```cpp
FILE* fp_;
```

- `fp_`：当前打开的日志文件指针。

## 函数说明

### `Open`

```cpp
logical Open(const char* file_path, logical append);
```

流程：

1. 先调用 `Close()` 关闭旧文件。
2. `file_path == nullptr` 时返回 `FALSE`。
3. 调用 `EnsureParentDirectoryForFile(file_path)` 创建父目录。
4. 根据 `append` 选择 `"a"` 或 `"w"` 打开文件。

### `Close`

关闭 `fp_` 并置空。析构函数也会调用。

### `IsOpen`

返回当前是否有文件打开。

### `Write`

```cpp
logical Write(const StructuredEvent& event);
```

输出格式：

```json
{"tags":["step1","face-analyze"],"properties":{"face_id":"0","valid":"true"}}
```

写出规则：

- `tags` 写为 JSON array。
- `properties` 写入 `properties` object。
- `json_properties` 也合并写入同一个 `properties` object。
- `json_properties` 的 value 只有通过 `IsLikelyJsonValue` 检查时才写出。
- 每个 event 一行，写完 `fflush(fp_)`。

## 当前边界

- 没有单独的 `json_properties` 顶层字段；raw JSON 也合并到 `properties`。
- `DiagnosticLevel` 不在 writer 层处理。
- 写失败没有细粒度错误码，`fp_ == nullptr` 时返回 `FALSE`。
