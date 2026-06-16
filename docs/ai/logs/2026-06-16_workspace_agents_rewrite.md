# 2026-06-16 workspace AGENTS rewrite

## 背景

新增 `midface_workspace/` 后，需要把 AI coding agent 的入口说明切换到新 workspace，避免继续把外层旧目录当作默认事实源。

## 改动

- 重写 `AGENTS.md`。
- 明确当前 workspace 是 `/root/midface_package/midface_workspace`。
- 说明主要目录：`midface/`、`docs/document/`、`docs/ai/logs/`、`logReader/`、`example/`、`acisR24-linux/`。
- 写入修改后的验证规则：
  - 改 `midface/` 后运行 `make -C midface`。
  - 改 `logReader/` 后运行 `npm run build`。
  - 实质修改后补 `docs/ai/logs/`，长期事实变化同步 `docs/document/`。
- 增加 logReader 指路，入口为 `logReader/README.md` 和 `logReader/SCRIPTING_FOR_CHATBOT.md`。
- 按用户反馈压缩 `AGENTS.md` 到 50 行内，只保留必要入口、命令和规则。
- 将 `AGENTS.md` 中的 workspace 和命令改为相对路径写法。
- 按用户要求将 `midface_workspace/.gitignore` 简化为 build、对象/依赖文件和 ACIS SDK。

## 验证

- `make -C midface` 通过。

## 注意

- 本次只改 workspace 入口说明和任务日志，没有改 C++ 算法或 logReader 源码。
