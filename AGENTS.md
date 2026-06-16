# AGENTS.md

workspace：当前目录 `./`

## 目录
- `midface/`：当前 C++ 源码，以这里为准。
- `midface/Makefile`：构建入口。
- `midface/MidSurface.cpp`：pipeline 入口。
- `midface/steps/`：Step1-Step7。
- `docs/document/`：长期工程文档。
- `docs/ai/logs/`：任务日志。
- `logReader/`：日志和模型调试工具。
- `example/`：示例输入。
- `acisR24-linux/`：ACIS R24 Linux 依赖。
- 不默认改外层旧 workspace、`midface_new/` 或 Windows 工程。

## 开工
先读 `AGENTS.md` 和 `docs/document/README.md`。

按任务补读：
- Step：`docs/document/step*_design.md`
- util：`docs/document/util/README.md`
- logReader：`logReader/README.md`
- 查询脚本：`logReader/SCRIPTING_FOR_CHATBOT.md`
- 最近背景：`docs/ai/logs/`

## 命令
```bash
make -C midface
make -C midface run
make -C midface print-paths
make -C midface clean
```

```bash
cd logReader/frontend
npm run build
npm run dev
```

## 规则
- 改 `midface/` 后跑 `make -C midface`。
- 改 `logReader/` 后跑 `npm run build`。
- 实质修改后，在 `docs/ai/logs/` 写任务日志。
- 长期事实变化，同步更新 `docs/document/`。
- 文档和代码冲突时，以当前 `midface/` 代码为准。
- ACIS 行为不确定时先做最小实验。
