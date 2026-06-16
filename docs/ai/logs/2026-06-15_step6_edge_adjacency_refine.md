# 2026-06-15 Step6 edge adjacency refine

## 背景

Step6 selected slice adjacency 原 distance refine 使用 face 面域采样：

- 在 source face 上采 `adjacency_sample_count` 个点。
- 计算点到 target face 的最近距离。
- 以方向级 pass ratio 决定是否建立 `source=distance` 邻接。

该策略不适合 split 邻接判断。split 邻接应发生在边界上，尤其大面/小面、T 型局部接触等情况不应让 face 面域采样稀释或误导判断。

## 改动

- Step6 distance refine 改为 edge-based：
  - 对每个 selected face 收集 edge samples 时，采样数至少使用 `adjacency_sample_count`。
  - A 到 B 方向遍历 A 的每条有效 edge，计算 edge sample 到 B 的有限 `FACE*` 最近距离。
  - 每条 edge 单独统计 sample/valid/pass/pass_ratio/mean distance。
  - 任一 edge 的 `pass_ratio >= adjacency_min_pass_ratio` 即该方向通过。
  - A 到 B 或 B 到 A 任一方向通过即建立 `source=distance` 邻接。
- 日志保留原 distance 字段，并增加：
  - `refine_mode=edge`
  - `a_to_b_edge_count` / `b_to_a_edge_count`
  - `a_to_b_checked_edge_count` / `b_to_a_checked_edge_count`
  - `a_to_b_best_edge_index` / `b_to_a_best_edge_index`
  - `a_to_b_best_edge_length` / `b_to_a_best_edge_length`
- 更新 `docs/document/step6_trim_select_design.md` 和 `docs/document/logging_events.md`。

## 验证

- `make -C midface` 通过。

## 注意

- 当前实现继续使用 `api_find_cls_ptto_face(sample, target_face, cp)`，目标是有限 `FACE*`，不是直接对无限 surface equation 求距离。
- 目前仍只对 accepted selected-adjacency 输出单条日志；未通过的候选 pair 没有 per-pair reject 日志。
