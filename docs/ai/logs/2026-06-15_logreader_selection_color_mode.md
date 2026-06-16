# 2026-06-15 logReader selection color mode

## 背景

用户在 Selection JS 中用 `idColors` 高亮 Step6 selected-adjacency 时发现两个问题：

- 同一个 id 可能在 `id-colors` 展示块中重复出现，例如先灰色重置 `6`，再黄色高亮 `6`。
- 取消最后一个选中 id 后，自动选中查询不会执行，旧的脚本染色残留在模型上。

同时用户希望 Three.js viewer 支持单点击模式，避免每次点击都累积多重选中。

## 改动

- `scriptRunner.ts` 在归一化 `id-colors` block 时按 id 去重，保留最后一次颜色，展示层和实际应用语义一致。
- `App.tsx` 将脚本染色拆成 query 和 selection 两层：
  - query 脚本颜色仍按原逻辑合并。
  - selection 脚本颜色每次替换自己的颜色层。
  - 空选中时清空 selection 颜色层和 selection 结果块。
- `ThreeViewer.tsx` 新增单选/多选模式：
  - 默认单选。
  - 单选下点击新 id 会替换当前选中；点击当前唯一选中 id 会取消选中。
  - 多选保留原来的 toggle 累积行为。
- `App.tsx` 将传给 `ThreeViewer` 的选中回调用 `useCallback` 固定，避免每次点击后回调引用变化触发 Three.js 场景重新初始化并重置相机视角。
- `styles.css` 为两段式选择模式 segmented control 增加布局样式。

## 验证

- 在 `logReader/frontend` 执行：

```bash
env -i HOME=/root PATH=/root/.local/toolchains/node-v22.15.0-linux-x64/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin npm run build
```

- 结果：`tsc -b`、`vite build`、`scripts/inline-dist.mjs` 全部通过。
- Vite 仍提示主 chunk 大于 500 kB，这是当前离线打包的已知状态。

## 注意

- 当前工作区已有大量 logReader 迁移改动和未跟踪文件，本次只基于现有浏览器版 logReader 继续修改，没有回滚旧改动。
