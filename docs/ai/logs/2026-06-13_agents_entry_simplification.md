# 2026-06-13 AGENTS 入口简化

## 任务

用户认为 `docs/ai/map.md`、`state.md`、`command.md`、`rules.md` 四件套没有必要继续维护，希望按当前文档体系简化 AI 开工入口。

## 本次改动

- 重写 `AGENTS.md`。
- 开工必读改为：
  - `AGENTS.md`
  - `docs/document/README.md`
- 明确 `docs/ai/` 当前只保留 `logs/`，不再维护四件套。
- 文档事实源改为当前 `docs/document/`。
- 明确当前源码是 `/root/midface_package/midface`，`midface_new/` 只作为历史参考。
- 增加常用包内构建命令：
  - `make -C midface`
  - `make -C midface run`
  - `make -C midface print-paths`
  - `make -C midface clean`

## 未做

- 未修改代码。
- 未删除 `docs/ai/logs/`。
