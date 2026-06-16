# logReader 脚本编写说明

本文档给外部网页 chatbot / 代码生成器使用，用来生成可直接粘贴到 `logReader` 的查询脚本。

`logReader` 是纯浏览器工具。用户通过页面上传日志、模型和材质文件，脚本运行时只能访问当前页面已经导入的数据，不能读取本地路径，也不能 `import` 外部模块。

## 运行环境

- 语言：浏览器 JavaScript，支持 ES6+ 语法。
- 执行位置：Web Worker。
- 模块导入：不支持 `import` / `require`。
- 超时：脚本执行超时会被终止，并在页面上显示错误。
- 安全边界：Web Worker 主要用于隔离 UI 卡顿，不是安全沙箱。脚本仍应只写查询和渲染逻辑。

## 入口函数

普通日志查询脚本：

```js
async function run() {
  return [];
}
```

选中查询脚本：

```js
async function run(ids) {
  return [];
}
```

`ids` 是当前 Three.js 模型里被选中的 id 列表，类型是 `string[]`。

脚本返回一个展示块，或者展示块数组。推荐始终返回数组。

## 日志对象

导入 JSONL 后，每一行会被解析成一个 `LogEntry`：

```ts
type LogEntry = {
  seq: number;
  tags: string[];
  properties: Record<string, unknown>;
};
```

`seq` 就是数组下标：

```js
allLogs[seq] === log(seq)
```

注意：`seq` 只对当前导入的日志有效，不要跨文件复用。

## 全局数据

脚本可以直接访问这些全局变量：

```js
allLogs      // LogEntry[]，当前导入的完整日志数组
selectedIds  // string[]，当前模型选中的 id 列表
colorMaps    // ColorMapRecord[]，从日志 color-map 事件解析出的颜色映射
```

`colorMaps` 的结构：

```ts
type ColorMapRecord = {
  profileId: string;
  id: string;
  rgb: [number, number, number];
  hex: string;
  log: LogEntry;
};
```

当前内置的 `profileId`：

```text
step1-face
step2-group
step3-pair
step3-wall
step6-selection
```

## 查询原语

系统只内置少量 seq 数组查询原语。复杂逻辑建议用 `lodash` 或普通 JS 自己组合。

### `tagSeqs(...tags)`

返回同时包含这些 tag 的日志 seq 数组。

```js
tagSeqs("step1")
tagSeqs("step1", "color-map")
```

多个 tag 会自动取交集。

### `propSeqs(name, value)`

返回 `properties[name]` 精确等于 `value` 的日志 seq 数组。

```js
propSeqs("face_id", "12")
propSeqs("rgb", [255, 0, 0])
```

也可以传对象，多个字段会自动取交集：

```js
propSeqs({ face_id: "12", level: "debug" })
```

### `log(seq)`

返回单条日志：

```js
const item = log(12);
```

等价于：

```js
const item = allLogs[12];
```

### `logs(seqs)`

把 seq 数组转换为日志对象数组，会去重并过滤无效 seq：

```js
const items = logs([1, 2, 2, 999999]);
```

### `search(query)`

保留的便捷查询函数，适合简单脚本：

```js
const result = search({
  tags: ["step1", "color-map"],
  properties: { face_id: "12" },
  offset: 0,
  limit: 20
});
```

返回结构：

```ts
type SearchResponse = {
  file_id: string;
  total: number;
  offset: number;
  limit: number;
  indices: number[];
  logs: LogEntry[];
};
```

### `logsBySeq(seqs)`

兼容别名，等价于：

```js
logs(seqs)
```

## Lodash

运行时全量暴露 `lodash-es`：

```js
lodash
```

同时保留 Lodash 习惯别名：

```js
_
```

两者指向同一套工具。给 chatbot 生成脚本时推荐写 `lodash`，手写短脚本可以写 `_`。

常用函数：

```js
lodash.intersection(a, b)  // 多个数组取交集
lodash.union(a, b)         // 多个数组取并集
lodash.difference(a, b)    // 从 a 中排除 b
lodash.uniq(a)             // 去重
lodash.groupBy(items, fn)  // 分组
lodash.countBy(items, fn)  // 分组计数
lodash.sortBy(items, fn)   // 排序
lodash.keyBy(items, fn)    // 按 key 转对象
```

例子：

```js
const seqs = lodash.intersection(
  tagSeqs("step1"),
  tagSeqs("color-map"),
  propSeqs("face_id", "12")
);
```

## 渲染输出

脚本返回 RenderBlock。推荐使用下面的包装函数，不要手写对象。

### `text(title, value)`

显示一段文本。

```js
text("匹配数量", 42)
```

结构：

```ts
{ type: "text"; title?: string; text: string }
```

### `json(title, value)`

显示任意 JSON 值。

```js
json("第一条日志", allLogs[0])
```

结构：

```ts
{ type: "json"; title?: string; value: unknown }
```

### `table(title, rows, columns?)`

显示普通表格。

```js
table("统计", [{ id: "12", count: 3 }], ["id", "count"])
```

结构：

```ts
{
  type: "table";
  title?: string;
  columns?: string[];
  rows: Record<string, unknown>[];
}
```

### `logTable(title, logs)`

显示日志表格。

```js
logTable("Step1 日志", logs(tagSeqs("step1").slice(0, 20)))
```

结构：

```ts
{ type: "log-table"; title?: string; logs: LogEntry[] }
```

### `idList(title, ids)`

显示 id 列表。

```js
idList("当前选中", ids)
```

结构：

```ts
{ type: "id-list"; title?: string; ids: string[] }
```

### `idColors(items, title?)`

给模型 id 设置底色，同时显示颜色列表。

```js
idColors([
  { id: "12", color: "#ffcc00" },
  { id: "18", color: "#48d6ff" }
])
```

结构：

```ts
{
  type: "id-colors";
  title?: string;
  items: Array<{ id: string | number; color: string }>;
}
```

`idColors` 修改的是 Three.js 模型底色。鼠标点击产生的选中高亮是另一层覆盖，不会和脚本底色混在一起。

## 示例

查询某个 face 的日志：

```js
async function run() {
  const seqs = lodash.intersection(
    tagSeqs("step1"),
    propSeqs("face_id", "12")
  );

  return [
    text("匹配数量", seqs.length),
    logTable("face 12", logs(seqs))
  ];
}
```

用 `log(seq)` 自己写过滤：

```js
async function run() {
  const seqs = tagSeqs("step1").filter((seq) => {
    const item = log(seq);
    return item && item.properties.face_id === "12";
  });

  return [
    logTable("手写过滤结果", logs(seqs))
  ];
}
```

按第一标签统计：

```js
async function run() {
  const rows = Object.entries(
    lodash.countBy(allLogs, (item) => item.tags[0] || "(none)")
  ).map(([tag, count]) => ({ tag, count }));

  return [
    table("第一标签统计", lodash.sortBy(rows, (row) => -row.count))
  ];
}
```

选中查询：

```js
async function run(ids) {
  const seqs = lodash.uniq(ids.flatMap((id) => propSeqs("face_id", id)));

  return [
    idList("当前选中", ids),
    logTable("匹配日志", logs(seqs))
  ];
}
```

给选中 id 染色：

```js
async function run(ids) {
  return [
    idColors(ids.map((id) => ({ id, color: "#ffcc00" })))
  ];
}
```

## 字段说明

脚本 API 只负责“怎么查、怎么渲染”。具体有哪些 tag、`properties` 字段、字段含义和所属 Step，应配合项目的日志字段说明使用。

这些字段说明在项目文档的 `docs/shared/log/` 目录中维护。网页脚本不读取这些文件；它们只是给人或 chatbot 生成脚本时参考。
