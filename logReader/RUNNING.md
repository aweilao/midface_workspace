# logReader WSL 运行记录

更新时间：2026-06-12

## 当前定位

`logReader` 已重构为纯浏览器工具。开发模式使用 Vite React，最终产物是
`frontend/dist/` 静态目录，可以直接打开：

```text
/root/midface_package/logReader/frontend/dist/index.html
```

当前前端不再依赖 Go 后端、`/api` 代理、`workspace/scripts.json` 或 CDN Three.js。

使用说明见：

```text
/root/midface_package/logReader/README.md
```

给外部网页 chatbot/code generator 的脚本生成规范见：

```text
/root/midface_package/logReader/SCRIPTING_FOR_CHATBOT.md
```

## 工具链

WSL 内使用 Node.js / npm：

```bash
export PATH=/root/.local/bin:/root/.local/toolchains/node-v22.15.0-linux-x64/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
```

## 开发运行

前台启动：

```bash
cd /root/midface_package/logReader/frontend
npm run dev
```

从 Windows PowerShell 直接启动 WSL 内服务：

```powershell
wsl -d Ubuntu -- bash -lc 'cd /root/midface_package/logReader/frontend && PATH=/root/.local/toolchains/node-v22.15.0-linux-x64/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin npm run dev'
```

访问地址：

```text
http://127.0.0.1:5173/
```

## 静态构建

构建静态目录：

```bash
cd /root/midface_package/logReader/frontend
env -i HOME=/root PATH=/root/.local/toolchains/node-v22.15.0-linux-x64/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin npm run build
```

构建通过后直接打开：

```text
/root/midface_package/logReader/frontend/dist/index.html
```

构建完成后会执行 `scripts/inline-dist.mjs`，把 JS/CSS 内联进 `dist/index.html`。
因此最终入口是一个可以直接双击/`file://` 打开的 HTML 文件，不依赖本地服务。

Windows 侧可以直接打开：

```text
\\wsl.localhost\Ubuntu\root\midface_package\logReader\frontend\dist\index.html
```

## 目录清理建议

当前 active runtime 只需要：

- `frontend/`：React/Vite 源码、依赖声明和静态构建入口。
- `RUNNING.md`：当前运行笔记。
- `.gitignore`：忽略 `dist/`、`node_modules/`、日志、pid、临时 SAT 等产物。

`workspace/` 是旧 Go 后端的脚本存储目录；新版脚本已经保存到浏览器 `localStorage`，因此它不再是运行必需项。若还想保留旧脚本样例，可以暂时留着；若确认不需要兼容旧脚本库，也可以删除。

以下内容已经不是纯浏览器版运行必需项，可以清理或移到历史归档：

- Go 后端源码：`go.mod`、`main.go`、`server.go`、`store.go`、`scripts.go`、`*_test.go`
- 旧运行产物：`backend-dev.*`、`frontend-dev.*`、`events.jsonl`
- 临时模型/调试产物：`*.sat`、`*:Zone.Identifier`

清理前建议先确认 git 状态，避免误删未提交资料：

```bash
cd /root/midface_package
git status --short logReader
```

## 功能入口

- 顶部导入：`.jsonl`、`.obj/.glb/.gltf`、`.mtl`，以及多文件“一键导入”。
- 日志查询：只保留 JS 脚本查询；脚本在 Web Worker 中执行，返回 `RenderBlock[]`。
- 脚本保存：使用浏览器 `localStorage`；支持脚本 JSON 导入/导出。
- Three.js：使用 npm `three` 本地打包；根据 color-map profile 反查 id，点击按 id 整体选中。
- 脚本染色：`id-colors` RenderBlock 的 `{ id, color }` 会作为模型底色覆盖；选中高亮独立 overlay 显示。

## RenderBlock 协议

普通查询脚本可以直接使用日志运行时函数：

```js
async function run() {
  const seqs = _.intersection(
    tagSeqs("step1"),
    tagSeqs("color-map"),
    propSeqs("face_id", "12")
  );

  return [logTable("face 12", logs(seqs))];
}
```

选中查询脚本：

```js
async function run(ids) {
  const seqs = _.uniq(ids.flatMap((id) => propSeqs("face_id", id)));

  return [
    idList("selected", ids),
    logTable("matched", logs(seqs))
  ];
}
```

脚本运行时暴露：

- `allLogs`：完整日志数组，`seq` 就是数组下标。
- `log(seq)`：返回单条日志，等价于 `allLogs[seq]`。
- `logs(seqs)`：按 seq 数组返回日志数组，并去重/过滤无效 seq。
- `tagSeqs(tag)`：返回含有该 tag 的 seq 数组。
- `propSeqs(name, value)`：返回 properties 中该字段等于该值的 seq 数组。
- `lodash` / `_`：Lodash 工具集合，`_` 是习惯别名，两者指向同一对象。
- `text/json/table/logTable/idList/idColors`：RenderBlock 快捷构造函数。

支持类型：

- `text`
- `json`
- `table`
- `log-table`
- `id-list`
- `id-colors`

## 验证记录

2026-06-12：

```text
npm run build
```

结果：TypeScript 与 Vite production build 通过；产物为：

```text
dist/index.html
```

Vite 仍会生成 `dist/assets/index-*.css` 和 `dist/assets/index-*.js` 作为中间产物，
但 `dist/index.html` 已经内联这些内容；直接打开时只需要 `index.html`。
Vite 对主 JS chunk 大小有提示，原因是 Three.js 打包进离线静态产物；当前可接受。
