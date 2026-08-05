# 下一阶段调研：开局资源、实验性疗法与一层内容

- 检索日期：2026-08-05
- 目标版本：The Binding of Isaac: Repentance+ `v1.9.7.17.J460`，普通伊甸，全解锁 Profile
- 工作分支：`codex/research-floor1-and-post-item-stats`

## 结论

建议把四类需求分成三个彼此独立的里程碑，不要一次并入同一个“大楼层模拟器”。

| 功能 | 能否纯离线精确实现 | 当前证据 | 预计难度 | 本分支建议 |
| --- | --- | --- | --- | --- |
| 开局硬币、钥匙、炸弹 | 可以 | RNG 分支已在当前 C++ 内核中执行；上游有明确取值规则 | 低 | 下一次优先实现，可单独合并 |
| 实验性疗法（黄针，ID 240）结算 | 可以，先限定为开局被动 240 | 上游已有 7 属性洗牌算法；游戏 API 和 Wiki 给出效果语义 | 中 | 做成首个“道具专用结算器”，单独验证后合并 |
| 一层 Boss 身份 | 游戏内可精确读取；纯离线尚无现代通用实现 | Lua API 可直接读取 Boss 房描述；离线需复现楼层生成 | 中高 | 先做研究观察器，不进正式 WebUI |
| 一层宝箱房初始道具 | 游戏内可精确读取；纯离线需要房间与物品池模拟 | 房间二进制、房间 SpawnSeed、物品池规则都参与 | 高 | 暂不承诺合并，先建立真值集 |
| 一层 Boss 奖励道具 | 常见路径可近似；当前不能保证全种子正确 | 上游实现明确注明只覆盖“地图一次生成成功”的高概率路径 | 高 | 不以近似结果提供公开筛选 |

因此，用户问题的直接答案是：**炸弹、钥匙、钱可以筛，并且是目前最适合马上补上的功能。**

## 1. 开局硬币、钥匙、炸弹

### 已验证事实

当前 `roll_base_start` 已经为了保持后续属性 RNG 对齐而执行了资源分支，但只计算了分支推进，没有把结果写入 `EdenStart`。旧 Python 数据契约和 `schemas/search-job.schema.json` 已经预留 `coins`、`keys`、`bombs`，所以缺口主要在原生结果结构、筛选参数、排序/展示和测试。

上游 `eden.hpp` 的取值规则为：

1. 先判断是否没有开局资源；
2. 若有资源，只会从硬币、钥匙、炸弹三类中选择一类；
3. 硬币为 1–5，钥匙固定为 1，炸弹为 1–2；
4. 三类不会同时非零。

对应上游固定提交：[`eden.hpp` 资源分支](https://github.com/2o181o28/eden-seed-finder/blob/f8d46aac86505193a3906bd9cea486bbcbabdc63/eden.hpp#L189-L211)。当前仓库的 C++ 已经使用相同的 RNG 路径，只是没有保存值。

### 建议接口

- `EdenStart` 增加整数 `coins`、`keys`、`bombs`；
- `EdenCriteria` 增加三个整数闭区间；
- CLI/HTTP/WebUI 使用 0–5、0–1、0–2 的数值范围；
- 结果表展示三列，并允许页面内排序；
- 仍称为“开局资源”，不与道具额外给予的资源混在一起。

### 合并门槛

- 至少 20 个覆盖所有可能取值的实机观测；
- 覆盖“全为 0”、硬币 1 和 5、钥匙 1、炸弹 1 和 2；
- 全域扫描不增加未配置该条件时的明显耗时；
- 旧 HTTP 请求与已有 golden fixture 不破坏。

这项修改不依赖楼层模拟，也不需要重新提取物品池。

## 2. 实验性疗法（黄针，ID 240）

### 功能边界

这里的“黄针”按本地 J460 目录和灰机 Wiki 确认为 **实验性疗法 / Experimental Treatment，收藏品 ID 240**。当前数据快照记录其效果为“随机增加四项并减少两项角色的属性”；灰机条目也给出了 Repentance 的固定增减值。[灰机 Wiki：C240](https://isaac.huijiwiki.com/wiki/C240)

首版只处理：

- 普通伊甸开局被动道具恰好为 240；
- 玩家尚未进行任何操作；
- 只结算实验性疗法本身，不声称支持任意道具组合；
- 同时保留“道具前属性”和“黄针结算后属性”，避免改变现有字段含义。

### 上游已经做到什么

上游较新的 `generator_dz_exp_long.cpp` 不是逐种子进游戏试出来的。它从玩家初始化 RNG 派生 ID 240 的 RNG，然后对 7 项属性做 Fisher–Yates 洗牌：排列中的 4 项上升、2 项下降、1 项不变。源码中的核心见 [`generator_dz_exp_long.cpp` 第 23–38 行](https://github.com/2o181o28/eden-seed-finder/blob/f8d46aac86505193a3906bd9cea486bbcbabdc63/generator_dz_exp_long.cpp#L23-L38)。

这说明“黄针降低伤害和射速”等条件可以在全域扫描中低成本判断。它比通用道具属性结算简单得多，因为随机选择本身可以做成一个针对 ID 240 的确定性后处理器。

### 仍需验证的部分

- 上游选择算法在 J460 的 RNG 调用序列是否仍完全相同；
- 7 项索引与生命、移速、射速、伤害、射程、弹速、幸运的准确映射；
- 心之容器增减的边界行为，以及魂心不应被误当成同一属性；
- Repentance 的射速修正应叠加到当前项目保存的原始 tears modifier，再经过现有泪延迟公式，而不是直接对 Found HUD 数字加减；
- 移速、弹速、射程等上下限造成的显示截断；
- 开局同时拥有会改变最终面板的主动道具时，未使用的主动道具通常不应参与结算。

Isaac Lua API 公开了 `EntityPlayer:GetCollectibleRNG(ID)` 和最终玩家属性，可作为真值观察接口；`TemporaryEffects` 文档还特别注明实验性疗法的属性变化按房间 seed 决定。[EntityPlayer API](https://wofsauge.github.io/IsaacDocs/rep/EntityPlayer.html)；[TemporaryEffects API](https://wofsauge.github.io/IsaacDocs/rep/TemporaryEffects.html#addcollectibleeffect)

### 推荐设计

不要立刻写一个“所有道具最终属性引擎”。增加窄接口：

```text
Base Eden start
    -> ItemPostProcessor(passive_id, seed context)
    -> optional PostItemStats + provenance
```

首个处理器只注册 ID 240，并输出：

- `post_item_stats`：结算后真实面板；
- `experimental_treatment.ups`：4 个属性名；
- `experimental_treatment.downs`：2 个属性名；
- `experimental_treatment.unchanged`：1 个属性名；
- `post_item_stats_status = verified|experimental`。

筛选条件应明确分组为“基础属性”和“道具后属性”，不能把现有 `damage`、`tears` 等字段静默改义。

### 合并门槛

- 先用观察器采集至少 50 个开局黄针种子；
- 7 个属性都至少出现一次上升、下降和不变；
- 对比属性选择、红心和六项 Found HUD 数值；
- 混沌等与黄针结算无关的开局道具不得改变黄针结果；
- 未持有 240 时不运行后处理器，不影响现有扫描速度。

### 许可证注意

上游仓库为 AGPL-3.0。当前建议先把其实现作为行为参考和交叉验证器；若直接复制实现，应先确定本项目的 AGPL 兼容发行方案。上游仓库和固定提交见[项目主页](https://github.com/2o181o28/eden-seed-finder)与[提交 `f8d46aa`](https://github.com/2o181o28/eden-seed-finder/commit/f8d46aac86505193a3906bd9cea486bbcbabdc63)。

## 3. 一层 Boss：先区分“Boss 身份”和“Boss 奖励”

### Boss 身份

游戏内读取并不难。官方 Lua API 提供：

```lua
local level = Game():GetLevel()
local index = level:QueryRoomTypeIndex(RoomType.ROOM_BOSS, false, RNG())
local descriptor = level:GetRoomByIdx(index)
local roomName = descriptor.Data.Name
```

`QueryRoomTypeIndex` 和 `GetRoomByIdx` 的接口见 [Level API](https://wofsauge.github.io/IsaacDocs/rep/Level.html#queryroomtypeindex)。原作者的 `main_dz.lua`/`main_dz_exp.lua` 也采用这一路径，在游戏中筛 Rag Man 等 Boss。

难点是**离线生成同一个 `RoomDescriptor`**。需要复现：

- start seed 到 stage seed；
- 普通/困难、楼层变体、诅咒和 XL 等条件；
- 13×13 地图布局生成及失败重试；
- `bosspools.xml` 的权重选择；
- Boss 房形状和 `.stb` 房间模板选择；
- Headless Horseman、Book of Revelations 等会改变 Boss 的运行内行为。

所以 Boss 身份适合先做批量 Lua 真值采集，再判断是否值得逆向纯离线布局生成。

### Boss 奖励道具

`RoomDescriptor.AwardSeed` 用于普通房、迷你 Boss 房和 Boss 房的清理奖励；API 文档直接说明了这一用途。[RoomDescriptor API](https://wofsauge.github.io/IsaacDocs/rep/RoomDescriptor.html#awardseed)

原作者已有一个离线捷径：从 stage seed 推 Boss 房 `AwardSeed`，再从 Boss 物品池取道具；但源码明确标注它**只在 “Map Generated in 1 Loop” 时成立**，只是高概率，不是完整正确性保证。[上游 Boss 奖励近似](https://github.com/2o181o28/eden-seed-finder/blob/f8d46aac86505193a3906bd9cea486bbcbabdc63/generator_dz_exp_long.cpp#L44-L52)

此外，Boss 奖励还可能受以下因素影响：

- Boss 身份及 Boss 专属掉落；
- 物品池的顺序、权重衰减、已抽取/黑名单状态；
- 伊甸开局的混沌、NO! 等改变物品池选择的效果；
- 玩家在击杀 Boss 前使用主动、进入其他房间或生成额外道具；
- 多道具选项和特殊房间布局。

官方 `ItemPool:GetCollectible` 文档明确指出其结果会处理 Chaos、NO!、Sacred Orb、人物/挑战限制等规则。这正是简单“seed % 物品数量”无法代替的部分。[ItemPool API](https://wofsauge.github.io/IsaacDocs/rep/ItemPool.html#getcollectible)

结论：Boss 身份和 Boss 奖励必须分开设计；身份可以较早进入实验功能，奖励不能用当前高概率近似直接面向玩家。

## 4. 一层宝箱房初始道具

### 为什么比开局道具难很多

目标若定义为“未进行任何操作时，第一次进入 Basement I 宝箱房看到的初始收藏品 ID”，至少要得到：

1. 一层 stage seed；
2. 完整楼层布局与宝箱房 `RoomDescriptor`；
3. 被选中的 `.stb` 房间模板；
4. 房间 `SpawnSeed`，它用于加载房间实体；
5. 模板中收藏品底座的实体 seed 与初始 subtype；
6. Treasure 物品池在该局的洗牌、权重和黑名单状态；
7. 开局混沌、NO! 等对 `GetCollectible` 的修正；
8. 多底座、More Options、固定道具房等特殊布局。

`RoomDescriptor.SpawnSeed` 的官方说明是“用于在房间加载时生成实体并初始化敌人掉落 seed”；`Data` 则保存选中的房间配置。[RoomDescriptor API](https://wofsauge.github.io/IsaacDocs/rep/RoomDescriptor.html#spawnseed)

本机 ResourceExtractor 已提供实现所需的静态数据：

- `extracted_resources/resources/rooms/01.basement.stb`
- `extracted_resources/resources/00.special rooms.stb`
- `extracted_resources/resources/stages.xml`
- `extracted_resources/resources/bosspools.xml`
- `extracted_resources/resources/itempools.xml`

`.stb` 是二进制房间格式。开源的 [Basement Renovator](https://github.com/Basement-Renovator/basement-renovator) 可以解析和编辑该格式，因此静态房间解析不必从零开始；但它不提供 Isaac 当前版本的楼层随机生成器。

### 现有轮子的真实能力

原作者的新脚本仍然通过游戏命令进入宝箱房，再遍历实际生成的收藏品实体；它没有离线计算宝箱房道具。[`main_exp_long.lua` 的房间验证](https://github.com/2o181o28/eden-seed-finder/blob/f8d46aac86505193a3906bd9cea486bbcbabdc63/main_exp_long.lua#L29-L48)

2021 年的 [Isaac Repentance Seed Calculator](https://github.com/mzmmmm/Isaac_Repentance_Seed_Calculator) 尝试计算各楼层道具，许可证为 MIT，但 README 明确说它针对 2021 年旧版本，后续更新可能已失准。它可以作为 RNG 调用序列线索，不能直接成为 J460 后端。

本次检索没有发现一个已验证支持 J460、可离线输出一层布局和宝箱房道具的成熟通用库。

### 推荐分阶段路线

#### 阶段 A：真值观察器

只做开发工具，不安装为常驻玩家 Mod：

- 从候选种子列表启动普通伊甸；
- 记录 stage seed、难度、诅咒、房间列表；
- 记录宝箱房的 ListIndex、GridIndex、Data.Name/Variant、SpawnSeed、AwardSeed；
- 进入房间后记录所有收藏品实体的 InitSeed、DropSeed、SubType；
- 同时记录 Boss 房描述和最终 Boss 实体 ID；
- 输出 JSONL，运行结束恢复到普通状态。

#### 阶段 B：静态数据层

- 解析 `stages.xml`、`bosspools.xml`、`itempools.xml`；
- 评估复用 Basement Renovator 的 STB 解析规范，避免把 Python 运行时带入发行包；
- 生成 Profile 专属的压缩 C++ 数据表。

#### 阶段 C：离线 floor-1 predictor

- 先只支持普通路线 Basement I、普通伊甸、无内容 Mod；
- 先输出房间描述与 seed，不急着输出道具；
- 对地图生成重试次数建模，并用观察器数据验证；
- Boss 身份验证通过后，再接宝箱房实体和物品池；
- 最后处理混沌、NO!、多底座和 Boss 奖励。

这一预测器应是独立的 `Floor1Observation`/`RunObservation`，不能把字段塞进 `EdenStart`，与现有 `docs/architecture.md` 的扩展边界保持一致。

## 5. 推荐实施顺序与是否合并

### 可以较快进入主线

1. **开局资源筛选**：最小、确定、与楼层无关；完成实机向量后合并。
2. **实验性疗法专用结算器**：独立功能开关，达到 50 个实机向量后再合并。

### 继续留在研究分支

3. **一层真值观察器和数据集**：只服务逆向与回归，不随玩家包安装。
4. **Boss 身份离线预测原型**：只有全量验证无错报/漏报后才讨论 WebUI。
5. **宝箱房与 Boss 奖励**：在楼层布局、房间 seed、物品池三层都通过验证前不合并。

不建议把“进入游戏自动跑种子”的观察器重新放进正式安装包。它应是仓库内可显式构建/临时安装/卸载的开发工具，避免再次改变玩家出生楼层或普通游戏行为。

## 6. 预计工作量

这里的估计包含实现、WebUI 接口、回归测试和实机验证，不包含等待人工试玩的时间。

| 里程碑 | 估计 |
| --- | ---: |
| 开局资源完整功能 | 0.5–1 天 |
| 实验性疗法属性选择 + 结算 + 50 种子验证 | 1–3 天 |
| 一层真值观察器与首批数据集 | 1–2 天 |
| Boss 身份纯离线原型 | 3–7 天研究，结果不保证能立即产品化 |
| 一层宝箱房初始道具纯离线 MVP | 1–3 周 |
| Boss 奖励与混沌/特殊掉落完整化 | 在宝箱房 MVP 之后另计 |

## 7. 来源与证据等级

### 一级：本地版本与游戏资源

- 当前仓库 `native/src/core.cpp`、`native/include/isaac_seed_seeker/core.hpp`；
- 当前仓库 `schemas/search-job.schema.json`、`src/isaac_seed_seeker/domain.py`；
- 本机 J460 ResourceExtractor 输出的 `rooms/*.stb`、`stages.xml`、`bosspools.xml`、`itempools.xml`；
- 当前仓库固定的灰机数据修订 `data/catalog/sources/huiji-item.169621.json`（CC0-1.0）。

### 二级：API 文档与上游实现

- [BoI Lua API：Seeds](https://wofsauge.github.io/IsaacDocs/rep/Seeds.html)
- [BoI Lua API：Level](https://wofsauge.github.io/IsaacDocs/rep/Level.html)
- [BoI Lua API：RoomDescriptor](https://wofsauge.github.io/IsaacDocs/rep/RoomDescriptor.html)
- [BoI Lua API：ItemPool](https://wofsauge.github.io/IsaacDocs/rep/ItemPool.html)
- [BoI Lua API：EntityPlayer](https://wofsauge.github.io/IsaacDocs/rep/EntityPlayer.html)
- [2o181o28/eden-seed-finder，固定提交 `f8d46aa`](https://github.com/2o181o28/eden-seed-finder/tree/f8d46aac86505193a3906bd9cea486bbcbabdc63)
- [Basement Renovator](https://github.com/Basement-Renovator/basement-renovator)

### 推断，尚未验证

- 黄针上游 RNG 在 J460 保持不变；
- 常见的一次生成地图路径可覆盖绝大多数一层 Boss 奖励；
- 仅解析 `.stb` 和 XML 就足以复现 J460 的楼层生成；
- 普通与困难模式可共享完全相同的一层预测调用序列。

这些推断都不能直接变成对外功能声明，必须先由本机 J460 观察数据验证。
