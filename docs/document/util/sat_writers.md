# SAT Writer 工程文档

源码事实源：

- `midface/utils/AcisSatWriter.hpp`
- `midface/utils/AcisSatWriter.cpp`
- `midface/utils/DebugSatBuilder.hpp`
- `midface/utils/DebugSatBuilder.cpp`
- `midface/utils/FaceSheetBodySatWriter.hpp`
- `midface/utils/FaceSheetBodySatWriter.cpp`

## 作用

当前 SAT 输出工具分三层：

- `AcisSatWriter`：最底层，把 `ENTITY_LIST` 写成 SAT 文件。
- `DebugSatBuilder`：收集 entity 到 `ENTITY_LIST`，再用 `AcisSatWriter` 保存。
- `FaceSheetBodySatWriter`：复制 `FACE*`，包成单面 sheet body，再保存 SAT。

## `AcisSatWriter`

### `Save`

```cpp
logical Save(const char* file_path, ENTITY_LIST& entities) const;
```

流程：

1. `file_path == nullptr` 返回 `FALSE`。
2. `entities.count() <= 0` 返回 `FALSE`。
3. 调用 `EnsureParentDirectoryForFile(file_path)`。
4. 设置 ACIS `FileInfo`：
   - `units = 1.0`
   - `product_id = "ACIS (c) SPATIAL"`
5. 调用 `api_set_file_info(3, fileinfo)`。
6. `fopen(file_path, "w")`。
7. 调用 `api_save_entity_list(fp, TRUE, entities)`。

## `DebugSatBuilder`

字段：

```cpp
ENTITY_LIST entities_;
```

函数：

- `Clear()`：清空 entity list。
- `Add(ENTITY*)`：非空时加入。
- `Add(BODY*)` / `Add(FACE*)` / `Add(EDGE*)` / `Add(APOINT*)`：转成 `ENTITY*` 后加入。
- `Count()`：返回 `entities_.count()`。
- `entities()`：返回内部 `ENTITY_LIST&`。
- `Save(file_path)`：调用 `AcisSatWriter::Save`。

当前调用：

- `DiagnosticSink::EmitFaceSetSat` 用它收集 face。

## `FaceSheetBodySatWriter`

### `BuildSheetBodyFromFaceCopy`

```cpp
logical BuildSheetBodyFromFaceCopy(FACE* src_face, BODY*& out_body);
```

流程：

1. 调用内部 `CopyFaceDetached` 复制 `FACE*`。
2. 调用 `api_sheet_from_ff(1, one, body)` 把复制出的 face 包成单面 sheet body。
3. 输出 `BODY*`。

### `SaveFacesAsSheetBodiesSat`

```cpp
logical SaveFacesAsSheetBodiesSat(
    const std::vector<FACE*>& faces,
    const char* output_sat_path);
```

流程：

1. 遍历 faces。
2. 对每个 face 调用 `BuildSheetBodyFromFaceCopy`。
3. 成功则把 body 加入 `ENTITY_LIST`。
4. 调用 `AcisSatWriter::Save`。

当前调用：

- Step7 `WriteStep7InputSplitFacesSat`。
- Step7 `WriteFirstSplitWithUpstreamFacesSat`。

## 三者区别

- `EmitBodySatIfEnabled` 输出原 body/entity list，保留当前染色情况。
- `EmitFaceSetSat` 直接把 `FACE*` 加入 entity list，不复制成 sheet body。
- `SaveFacesAsSheetBodiesSat` 会复制 face 并包 sheet body，适合把一组裸 face 作为独立 sheet bodies 打开查看。

## 当前边界

- `AcisSatWriter::Save` 遇到空 entity list 会返回 `FALSE`。
- `FaceSheetBodySatWriter` 没有主动给 face/sheet body 染色。
- `DebugSatBuilder` 不拥有 entity 生命周期，只收集指针。
