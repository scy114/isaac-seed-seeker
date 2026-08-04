# Architecture

## 目标

第一阶段只服务伊甸：筛选随机开局属性、血量、资源、主动/被动道具以及口袋物品。其他角色、房间布局和深层楼层通过接口预留，但不进入 MVP 的完成定义。

## Truth-first pipeline

```text
SearchJob JSON
    -> candidate generation / future predictive kernels
    -> generated_job.lua
    -> Repentance+ Lua observer
    -> tagged JSON lines in log.txt
    -> observations.jsonl
    -> deterministic offline filtering
```

### Why game truth comes first

本机游戏为 Repentance+ `v1.9.7.17.J460`。现有开源实现覆盖的版本和算法并不完全一致，且伊甸道具候选还受解锁状态和运行时物品表影响。因此 MVP 以当前游戏进程产生的观测作为真值。

离线预测器必须实现 `Predictor` 边界，并通过同一批 Profile 的 golden observations 后，才能被标记为 `verified` 并参与淘汰候选。未经验证的预测器只能用于排序或实验输出。

## Data contracts

- `GameProfile`: 游戏版本、构建、资源快照和道具数量。
- `SearchJob`: 候选种子来源、伊甸过滤条件、输出限制。
- `EdenObservation`: 游戏内实际观测；数值单位保持原始 Lua API 单位并显式命名。
- `Predictor`: `predict(seed, profile) -> EdenObservationLike`，未来高速内核的稳定接口。

## Components

### Python controller

- 解析和校验任务。
- 生成带校验和的候选种子。
- 编译 Lua job。
- 导入游戏日志、去重并离线筛选。
- 检测 Profile。

### Lua observer

- 只接受普通伊甸角色。
- 对每个候选启动自定义种子。
- 在 `MC_POST_GAME_STARTED` 读取玩家状态。
- 通过带固定前缀的单行 JSON 写入 `log.txt`。
- `T` 开始，`Y` 停止；不主动修改存档文件或游戏资源。

### Future predictive kernels

优先顺序：

1. 种子编解码与范围裁剪。
2. J460 伊甸基础属性。
3. J460 伊甸起始道具与口袋物品。
4. 可选 REPENTOGON 房间/楼层观察器。

每一步都必须用实际游戏观测做正反例回归。

## Extension boundaries

- 角色通过 observation adapter 扩展，不污染 Eden 数据模型。
- 房间和楼层条件放入独立的 `RunObservation`，不塞入 Eden start filter。
- GUI 只消费 CLI/JSON API，不直接实现 RNG。
- 第三方内核通过 adapter 接入，保留来源、版本和许可证信息。
