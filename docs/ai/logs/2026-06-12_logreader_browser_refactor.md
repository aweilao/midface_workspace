# 2026-06-12 logReader 纯浏览器重构

## 目标

把 `logReader` 从 React + Go 本地 API 重构为纯浏览器工具。开发仍使用 Vite React，
构建产物为 `logReader/frontend/dist/`，可直接通过 `file://` 打开。

## 关键修改

- 前端移除 `/api`、Go server、`workspace/scripts.json`、CDN Three.js 依赖。
- 新增浏览器侧日志索引：`parseJsonlFile` 解析 JSONL，建立 tag/property 索引，提供 `search` 和 `logsBySeq`。
- 新增脚本协议：查询脚本与选中脚本在 Web Worker 中执行，标准返回 `RenderBlock[]`。
- 新增脚本库：脚本保存到 `localStorage`，支持 JSON 导入/导出。
- 新增 Three Viewer：本地 npm `three` 加载 OBJ/MTL/GLB，按 color-map profile 建立 `id -> triangle[]` 索引。
- Three 交互：点击面按 id 整体选中/取消；脚本返回 `id-colors` 时修改模型底色，选中高亮使用独立 overlay。
- Vite 设置 `base: "./"`，构建产物使用相对路径。
- `npm run build` 后执行 `scripts/inline-dist.mjs`，将 JS/CSS 内联进 `dist/index.html`，避免浏览器用 `file://` 打开 module asset 时白屏。

## 主要文件职责

- `logReader/frontend/src/logStore.ts`：JSONL 解析、tag/property 索引、浏览器内查询。
- `logReader/frontend/src/scriptRunner.ts`：Web Worker 脚本执行、超时、RenderBlock 归一化、`id-colors` 提取。
- `logReader/frontend/src/scriptStore.ts`：默认脚本、localStorage 持久化、脚本导入/导出。
- `logReader/frontend/src/colorMaps.ts`：内置 color-map profile、RGB 读取与 id 映射辅助。
- `logReader/frontend/src/ThreeViewer.tsx`：Three 模型加载、原始颜色缓存、id 映射、底色覆盖、选中 overlay。
- `logReader/frontend/src/App.tsx`：导入栏、日志查询工作区、Three 工作区、选中查询联动。

## RenderBlock 协议

普通查询脚本：

```js
async function run() {
  const seqs = _.intersection(tagSeqs("step1"), tagSeqs("color-map"));
  return [logTable("step1 color maps", logs(seqs))];
}
```

选中查询脚本：

```js
async function run(ids) {
  const seqs = _.uniq(ids.flatMap((id) => propSeqs("face_id", id)));
  return [idList("selected", ids), logTable("matched", logs(seqs))];
}
```

脚本运行时新增 seq 原语：

- `allLogs` / `log(seq)` / `logs(seqs)`
- `tagSeqs(tag)`
- `propSeqs(name, value)`
- `_`：内置轻量集合工具，覆盖 `intersection`、`union`、`difference`、`uniq`、`groupBy`、`countBy`、`sortBy`、`keyBy`

`seq` 当前就是 `allLogs` 数组下标，不使用 Map。

当前支持：

- `text`
- `json`
- `table`
- `log-table`
- `id-list`
- `id-colors`

## 验证

执行：

```bash
cd /root/midface_package/logReader/frontend
env -i HOME=/root PATH=/root/.local/toolchains/node-v22.15.0-linux-x64/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin npm run build
```

结果：

```text
tsc -b && vite build && node scripts/inline-dist.mjs 通过
dist/index.html 已内联 JS/CSS，可直接 file:// 打开
```

Vite 提示主 chunk 超过 500 kB，原因是 Three.js 被打进离线静态产物；当前符合“直接打开”目标。

## 注意

- Go 后端源码仍保留在 `logReader/`，但不再是当前 active runtime。
- 浏览器脚本由用户本地编写执行，Worker 只隔离 UI 卡顿与超时，不提供安全沙箱边界。
- color-map 匹配以 JSONL 中的 `rgb` / `color_rgb` 和模型原始材质/顶点色为准，带小容差处理颜色转换误差。

## 2026-06-13 脚本接口收敛

- `scriptWorker.ts` 不再向用户脚本暴露 `ctx`，普通查询入口固定为 `run()`，选中查询入口固定为 `run(ids)`。
- `scriptRunner.ts` 和 `App.tsx` 内部传输对象从 `ctx` 改名为 `snapshot`，避免和用户脚本 API 混淆。
- `types.ts` 中 `ScriptContextSnapshot` 改名为 `ScriptRuntimeSnapshot`，只表示 Worker 执行前的可序列化数据快照。
- 运行时仍暴露全局数据与函数：`allLogs`、`selectedIds`、`colorMaps`、`tagSeqs`、`propSeqs`、`log`、`logs`、`search`、`logsBySeq`、`lodash`、`_` 和 RenderBlock 构造函数。
- `logReader/SCRIPTING_FOR_CHATBOT.md` 改为中文说明，面向网页上传后的脚本生成场景；删除本地 `Useful files` 列表，只说明字段字典由 `docs/shared/log/` 维护。
- `logReader/RUNNING.md` 删除旧 `ctx.search(...)` / `ctx.logsBySeq(...)` 兼容说明，并说明 `lodash` 与 `_` 指向同一对象。

验证：

```bash
cd /root/midface_package/logReader/frontend
env -i HOME=/root PATH=/root/.local/toolchains/node-v22.15.0-linux-x64/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin npm run build
```

结果：`tsc -b && vite build && node scripts/inline-dist.mjs` 通过，`dist/index.html` 已重新内联 JS/CSS。Vite 仍提示主 chunk 超过 500 kB，原因为 Three.js 与全量 Lodash 进入离线静态包。

## 2026-06-13 README 受众调整

- `logReader/README.md` 从终端用户使用说明改为开发者/维护者入口。
- README 开头先记录运维命令：Node 路径、`npm install`、`npm run dev`、`npm run build`、`npm run preview`、静态产物路径和常用检查命令。
- README 后半部分介绍项目定位、目录结构、日志索引、脚本执行、RenderBlock、Three.js 模型服务、构建约定和文档分工。
- 详细脚本 API 不再在 README 展开，统一指向 `SCRIPTING_FOR_CHATBOT.md`，避免 README 和 chatbot 协议重复漂移。

验证：

```bash
cd /root/midface_package/logReader/frontend
env -i HOME=/root PATH=/root/.local/toolchains/node-v22.15.0-linux-x64/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin npm run build
```

结果：构建通过，`dist/index.html` 已重新内联 JS/CSS；仍只有 Vite 主 chunk 大小提示。
