# 2026-06-13 Step6 distance adjacency

## 背景

Step6 selected slice adjacency 原先主要依赖拓扑共享 edge、edge-edge 几何匹配和 edge-on-face。讨论后决定第一版先简化：不区分 MM1/MM2，只按“有关系候选 + 采样距离 refine”判断 selected face 联通性。

## 改动

- 新增 Step6 YAML/option 参数：
  - `adjacency_distance_units`，默认 `10.0`。
  - `adjacency_sample_count`，默认 `24`。
  - `adjacency_min_pass_ratio`，默认 `0.60`。
- `BuildSelectedSliceAdjacency` 保留同 seed body 共享 ACIS `EDGE*` 的 `source=topology` 强证据。
- 其余候选统一走关系 prefilter：
  - 同 `source_seed_rep_pair`
  - 同 `source_patch_id`
  - 同 `source_pair_id`
  - 或两个 `source_pair_id` 在 Step4 pair relation 中有关系
- 对候选做双向 face distance refine：
  - A face 采样点到 B face 最近距离。
  - B face 采样点到 A face 最近距离。
  - 任一方向 `pass_ratio >= adjacency_min_pass_ratio` 即建立 `source=distance` adjacency。
- 距离阈值：
  - `tol = adjacency_distance_units * Step1.model_scale.distance_unit`
  - 若 Step1 scale 不可用，回退到 `edge_match_tolerance`。
- selected adjacency 事件输出双向采样统计、实际阈值、归一化阈值和 `accepted_direction`。
- 更新 Step6 文档、YAML 文档和日志字典。

## 行为边界

- 不再在 selected adjacency 主路径里区分 MM1/MM2。
- 不删除旧 edge-edge / edge-on-face helper 函数，当前只是主循环不再使用它们。
- `Step6SliceAdjacency` 结构暂未扩展，详细证据先写入日志。
