# logReader 开发者说明

`logReader` 是 `midface` 的浏览器侧日志/模型调试工具。当前版本已经退役 Go 后端，active runtime 只剩 React + Vite + Three.js 前端。

本文档面向维护者和 coding agent：先记录运维命令，再说明项目结构和关键实现约定。给外部网页 chatbot / code generator 生成查询脚本时，请看 `SCRIPTING_FOR_CHATBOT.md`。

## 运维命令

所有命令默认在 WSL `/root/midface_package` 内执行。

### 准备 Node 环境

桌面环境里建议显式指定 Node.js 路径：

```bash
export PATH=/root/.local/toolchains/node-v22.15.0-linux-x64/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
```

首次安装依赖：

```bash
cd /root/midface_package/logReader/frontend
npm install
```

### 开发运行

```bash
cd /root/midface_package/logReader/frontend
npm run dev
```

访问地址：

```text
http://127.0.0.1:5173/
```

从 Windows PowerShell 直接启动 WSL 内的开发服务：

```powershell
wsl -d Ubuntu -- bash -lc 'cd /root/midface_package/logReader/frontend && PATH=/root/.local/toolchains/node-v22.15.0-linux-x64/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin npm run dev'
```

### 构建静态产物

```bash
cd /root/midface_package/logReader/frontend
env -i HOME=/root PATH=/root/.local/toolchains/node-v22.15.0-linux-x64/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin npm run build
```

`npm run build` 会依次执行：

```text
tsc -b
vite build
node scripts/inline-dist.mjs
```

`vite build` 生成 `dist/index.html` 和 `dist/assets/index-*.js/css`，随后 `inline-dist.mjs` 把 JS/CSS 内联回 `dist/index.html`。最终入口是：

```text
/root/midface_package/logReader/frontend/dist/index.html
```

Windows 侧可打开：

```text
\\wsl.localhost\Ubuntu\root\midface_package\logReader\frontend\dist\index.html
```

当前目标是 `file://` 直接打开 `index.html` 可用，所以构建后不需要启动服务。

### 预览构建结果

静态文件也可以用 Vite preview 检查：

```bash
cd /root/midface_package/logReader/frontend
npm run preview
```

默认访问：

```text
http://127.0.0.1:4173/
```

注意：preview 只是辅助验证；正式交付仍以直接打开 `dist/index.html` 为准。

### 常用检查

查看 logReader 改动：

```bash
cd /root/midface_package
git status --short logReader docs/ai/logs
```

确认脚本文档里没有旧 `ctx` 接口：

```bash
cd /root/midface_package
/usr/bin/grep -RInF --exclude-dir=node_modules --exclude-dir=dist --exclude-dir=.vite "ctx" logReader/frontend/src logReader/*.md
```

## 项目定位

`logReader` 做两件事：

- 日志查询：导入 JSONL，在浏览器内解析、建索引，用用户 JS 脚本返回展示块。
- 模型阅读：导入 OBJ/MTL/GLB/GLTF，用日志 color-map 把模型颜色反查为业务 id，并支持按 id 整体选中和染色。

当前不再依赖：

- Go server
- `/api`
- `workspace/scripts.json`
- CDN Three.js

脚本保存使用浏览器 `localStorage`。脚本库导入/导出使用 JSON 文件。

## 目录结构

```text
logReader/
  README.md                    开发者说明
  RUNNING.md                   当前运行笔记和维护记录
  SCRIPTING_FOR_CHATBOT.md     给外部 chatbot/code generator 的脚本协议
  frontend/
    package.json               npm 命令和依赖
    vite.config.ts             Vite 配置，base="./"
    scripts/inline-dist.mjs    构建后内联 JS/CSS
    src/
      App.tsx                  页面状态、导入栏、两种工作区和脚本执行入口
      ThreeViewer.tsx          Three.js 模型加载、颜色索引、选中和染色
      colorMaps.ts             color-map profile 和 RGB/id 解析
      logStore.ts              JSONL 解析、tag/property 索引、search/logsBySeq
      scriptRunner.ts          Worker 调用、超时、RenderBlock 归一化
      scriptWorker.ts          用户脚本运行时和全局 API 注入
      scriptStore.ts           默认脚本、localStorage、脚本导入导出
      types.ts                 共享类型
      styles.css               页面样式
```

## 前端架构

### 数据导入

顶部导入栏支持：

- `.jsonl`
- `.obj`
- `.mtl`
- `.glb`
- `.gltf`
- 多文件一键导入

JSONL 会在浏览器内解析成：

```ts
type LogEntry = {
  seq: number;
  tags: string[];
  properties: Record<string, unknown>;
};
```

`seq` 当前就是 `allLogs` 的数组下标。

### 日志索引

`logStore.ts` 建两类索引：

- tag 索引：`tag -> seq[]`
- property 索引：`propertyName + normalizedValue -> seq[]`

`search(query)` 使用这些索引求交集后分页。用户脚本侧也暴露同类原语：`tagSeqs`、`propSeqs`、`log`、`logs`。

### 脚本执行

`scriptRunner.ts` 每次执行创建一个 inline Web Worker，并设置默认 5 秒超时。`scriptWorker.ts` 注入全局 API 后用 `new Function(...)` 取出用户定义的 `run` 函数。

当前脚本入口固定为：

```js
async function run() {}
async function run(ids) {}
```

不再向用户脚本暴露 `ctx`。脚本 API 细节以 `SCRIPTING_FOR_CHATBOT.md` 为准。

Worker 的作用是隔离 UI 卡顿和超时，不是安全沙箱。

### 渲染协议

脚本返回 `RenderBlock` 或 `RenderBlock[]`。主线程负责归一化和渲染。

当前类型：

```text
text
json
table
log-table
id-list
id-colors
```

`id-colors` 会被 `App.tsx` 提取出来，作为 Three.js 模型的脚本底色覆盖。

### Three.js 模型服务

`ThreeViewer.tsx` 负责：

- 加载 OBJ/MTL/GLB/GLTF。
- 缓存三角面原始颜色。
- 根据 color-map profile 建立 `id -> triangle[]` 和 triangle -> id 映射。
- 处理点击选中：选中以 id 为单位，同 id 的所有面一起高亮。
- 应用脚本底色：`id-colors` 改底色，手动选中高亮是独立 overlay。

内置 profile：

```text
step1-face       face_id
step2-group      group_id
step3-pair       pair_id
step3-wall       wall_id
step6-selection  selection_id
```

## 构建约定

`vite.config.ts` 必须保持：

```ts
base: "./"
```

否则静态产物直接用 `file://` 打开时容易找不到资源。

`scripts/inline-dist.mjs` 是当前“直接打开 HTML”能力的关键步骤。修改 Vite 输出结构、入口文件名或 HTML 模板后，需要确认它仍能找到并内联当前 JS/CSS。

全量 `lodash-es` 和 Three.js 会进入离线包，所以 Vite 可能提示主 chunk 超过 500 kB。当前这是可接受状态，优先级低于离线可打开。

## 文档分工

- `README.md`：开发者维护入口。
- `RUNNING.md`：当前运行状态、命令笔记和迁移记录。
- `SCRIPTING_FOR_CHATBOT.md`：脚本生成协议，给网页 chatbot / code generator 看。
- `docs/shared/log/`：日志 tag、字段和 result JSON 的长期字段字典。
- `docs/ai/logs/`：每次实质修改后的任务日志。

修改 active runtime 后，记得同步 `docs/ai/logs/`。
