# Architecture

## 目标

第一阶段只服务伊甸：筛选随机开局属性、血量、资源、主动/被动道具以及口袋物品。其他角色、房间布局和深层楼层通过接口预留，但不进入 MVP 的完成定义。

## Native product pipeline

```text
Embedded WebUI
    -> localhost HTTP API
    -> C++20 SearchSession + built-in or imported GameProfile
    -> parallel Eden RNG kernel
    -> per-worker bounded Top-K + deterministic global merge
    -> browser in-memory sorting + paginated rendering
    -> downloadable results
```

玩家发行物不包含 Python/Node 运行时。HTML、CSS、JavaScript 作为 Windows `RCDATA` 嵌入 EXE；原生程序只监听 `127.0.0.1` 的动态端口。

## Truth-first validation pipeline

```text
SearchJob JSON
    -> candidate generation / external predictive adapter
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
- `ItemCatalog`: Profile 专属的离线名称、别名、拼音与 `search_id` 映射；只供 UI 解析输入，不进入 RNG 内核。

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

### Native predictive kernel

优先顺序：

当前已实现种子编解码、J460 起始主动/被动/饰品、卡牌、胶囊、基础血量、钱/钥匙/炸弹与随机属性，以及金色基础 ID 归一化和并行全域扫描。道具后处理器目前只注册实验性疗法（ID 240），输出七项变化方向和六项结算后面板；它与基础属性字段分离。后续仍需处理用户 Profile 提取与版本兼容引导，不进入房间/楼层模拟。

每一步都必须用实际游戏观测做正反例回归。

### External J460 adapter

- 第三方目录保持在仓库之外或 Git 忽略的 `build/` 下，项目不复制其 RNG 实现。
- adapter 先用饰品条件扫描一次，再对饰品命中项判断主动/被动 OR 集合，避免为笛卡尔积重复扫描。
- `proc.json` 和 `trinket_pool.json` 属于 Profile 快照；全解锁只在提取快照的游戏状态成立。
- adapter 输出必须经过同 Profile 的 Lua 观察器复核，不能直接标记为 verified。

## Extension boundaries

- 角色通过 observation adapter 扩展，不污染 Eden 数据模型。
- 房间和楼层条件放入独立的 `RunObservation`，不塞入 Eden start filter。
- 道具后属性通过按收藏品 ID 注册的窄后处理器扩展；不能把基础属性字段静默改成道具结算值。
- GUI 只消费 CLI/JSON API，不直接实现 RNG。
- 名称搜索只在 GUI 侧将目录条目投影为整数 ID；HTTP API 不接受名称。
- 第三方内核通过 adapter 接入，保留来源、版本和许可证信息。
