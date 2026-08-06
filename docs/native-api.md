# Native Backend API

原生后端由 C++20 实现，CLI 与本地 HTTP 服务共用同一个预测和筛选内核。默认使用内置的 `j460-full-unlock` Profile。

双击 EXE 即可使用覆盖全部下列条件的 WebUI；CLI 与 HTTP 接口主要用于自动化、复核和后续二次开发。

## 语义

- 不同类别之间使用 AND，同一个 `*_ids` 列表内部使用 OR。
- `*_exclude_ids` 在对应类别命中后排除指定 ID。
- `none`、`trinket`、`card`、`pill` 四种口袋状态互斥。
- `pill_effect_ids` 是当前种子洗牌后的原始胶囊效果 ID（`0..49`），不是胶囊颜色 ID。
- 结果中的 `pill_color` 保留颜色值；马胶囊带 `2048` 标志。金色胶囊没有单一固定效果，因此 `pocket_id` 为 `-1`。
- 红心、魂心、钱、钥匙、炸弹和基础属性都是道具结算前的伊甸生成值；资源不包含开局道具额外给予的掉落物。
- `damage`、`move_speed`、`tears`、`range`、`shot_speed`、`luck` 使用游戏 Found HUD 的真实数值口径。
- 伤害、移速、弹速和幸运由角色模板基准叠加随机修正；`tears` 按 Repentance 泪延迟公式换算为每秒泪弹数。
- JSON 结果仍返回 `damage_delta`、`move_speed_delta`、`tears_delta`、`shot_speed_delta`、`luck_delta`，旧版筛选参数也继续兼容，供脚本迁移和预测复核使用。
- `range` 是从内部距离修正换算后的基础显示值，范围约为 5.0 到 8.0。
- 所有区间边界均为闭区间。
- 主动与被动收藏品品质来自固定 Wiki 数据表，取值为 `0..4`；`total_quality` 是两件开局道具品质之和，取值为 `0..8`。
- `post_*` 与 `experimental_*` 当前只适用于开局被动道具为实验性疗法（ID 240）的种子。七项方向固定为 4 项上升、2 项下降、1 项不变；基础字段不会被覆盖。
- 搜索始终扫描完整范围并统计真实命中数，但只保留按指定顺序最优的 Top-K，内存占用受 `max_results` 限制。

## CLI

```powershell
# 检查单个 uint32 种子
.\build\native\IsaacSeedSeeker.exe inspect --seed 10161220

# 解码八字符游戏种子并检查开局
.\build\native\IsaacSeedSeeker.exe inspect --seed-label "TEXZ WDS0"

# 搜索；示例中的三类 ID 条件必须同时成立
.\build\native\IsaacSeedSeeker.exe search `
  --trinket 169 `
  --active 145,133 `
  --passive 81,134,187,212,665 `
  --start 1 `
  --end 4294967295 `
  --threads 8 `
  --sort total-quality `
  --direction desc `
  --max-results 1000 `
  --output matches.json
```

口袋参数：

- `--pocket-kind none|trinket|card|pill`
- `--trinket ID[,ID]`、`--card ID[,ID]`、`--pill ID[,ID]` 会同时指定口袋类型。
- `--pocket ID[,ID]` 只筛 ID，通常应同时给 `--pocket-kind`。
- `--pocket-exclude ID[,ID]`

道具参数：

- `--active ID[,ID]`、`--active-exclude ID[,ID]`
- `--passive ID[,ID]`、`--passive-exclude ID[,ID]`

数值参数均使用 `--字段-min` / `--字段-max`：

- `red-hearts`、`soul-hearts`
- `coins`、`keys`、`bombs`
- `damage`、`move-speed`、`tears`
- `range`、`shot-speed`、`luck`

黄针专用参数：

- `--experimental-health|move-speed|tears|damage|range|shot-speed|luck up|down|unchanged`
- `--post-damage-min/max`、`--post-move-speed-min/max`、`--post-tears-min/max`
- `--post-range-min/max`、`--post-shot-speed-min/max`、`--post-luck-min/max`

填写任一黄针专用条件会自然排除被动不是 240 的开局。若同时给出 `--passive`，列表必须包含 240，且不能用 `--passive-exclude 240`。

旧版 `damage-delta`、`move-speed-delta`、`tears-delta`、`shot-speed-delta`、`luck-delta` 参数仍可使用，但新界面和新脚本应优先使用真实面板值。

排序参数：

- `--sort seed|health|coins|keys|bombs|damage|move-speed|tears|range|shot-speed|luck|active-quality|passive-quality|total-quality`
- `--direction asc|desc`
- `--max-results N`：CLI 默认 `1000`，内核允许 `1..100000`。

血量先比较红心，红心相同再比较魂心。主动/被动品质相同时按对应道具 ID 升序；总品质相同时依次按主动 ID、被动 ID、种子数值升序。资源和其他属性相同则按种子数值升序。

## Local HTTP

不带参数运行 EXE 或执行 `IsaacSeedSeeker.exe serve` 后，程序只监听随机的 `127.0.0.1` 端口。启动日志会给出 URL 和会话 token。所有 POST 请求必须把 token 放入 `X-Isaac-Token` 请求头。

### 检查单个种子

`POST /api/v1/inspect`

```json
{"seed_u32": 2}
```

也可以直接提交游戏显示的八字符种子：

```json
{"seed": "TEXZ WDS0"}
```

`seed` 与 `seed_u32` 必须且只能提供一个。八字符种子会先进行字母表和校验码验证，再用于预测伊甸开局。

### 开始搜索

`POST /api/v1/search`

```json
{
  "card_ids": [12],
  "active_ids": [639],
  "passive_ids": [393],
  "red_hearts_min": 2,
  "red_hearts_max": 2,
  "damage_min": 2.87,
  "bombs_min": 1,
  "range_min": 7.19,
  "range_max": 7.20,
  "sort_key": "damage",
  "sort_direction": "desc",
  "start": 1,
  "end": 100,
  "threads": 2,
  "max_results": 1000
}
```

支持的 ID 字段：

- `pocket_kind`: `none`、`trinket`、`card`、`pill`
- `trinket_id`: 兼容当前 WebUI 的单饰品字段
- `trinket_ids`、`card_ids`、`pill_effect_ids`、`pocket_ids`
- `pocket_exclude_ids`
- `active_ids`、`active_exclude_ids`
- `passive_ids`、`passive_exclude_ids`

数值字段使用下划线形式：`red_hearts_min`、`coins_max`、`post_damage_min`，依此类推；可用字段与 CLI 的数值参数相同。黄针方向字段为 `experimental_health`、`experimental_move_speed`、`experimental_tears`、`experimental_damage`、`experimental_range`、`experimental_shot_speed`、`experimental_luck`，值为 `up`、`down` 或 `unchanged`。

`sort_key` 可用值与 CLI 排序项一致，但使用下划线形式，例如 `move_speed`、`active_quality` 和 `total_quality`。`sort_direction` 为 `asc` 或 `desc`。本地 HTTP/WebUI 的 `max_results` 默认 `1000`、允许 `1..10000`。

### 状态与结果

- `GET /api/v1/search/status`
- `GET /api/v1/search/results`
- `GET /api/v1/search/results.txt`
- `POST /api/v1/search/cancel`
- `POST /api/v1/shutdown`

JSON 结果中的 `count` 是实际保留并返回的数量，`total_count` 是完整扫描的真实命中数，`truncated` 表示是否因 `max_results` 截断。`sort_key`、`sort_direction` 和 `result_limit` 记录本次 Top-K 契约；每条结果包含 `coins`、`keys`、`bombs`、道具品质与基础属性。黄针命中还会令 `post_item_stats_available=true`，并返回 `experimental_treatment_up_mask`、`experimental_treatment_down_mask` 和六个 `post_*` 数值。掩码位 0..6 依次表示心之容器、移速、射速、伤害、射程、弹速、幸运。HTTP JSON 与原生 `results.txt` 端点使用相同的后端顺序。

WebUI 会把这批结果复制到浏览器内存并分页渲染，点击表头可即时重排已载入的全部记录，“导出当前排序 TXT”也使用页面当前顺序。若 `truncated=false`，这就是全部命中的精确排序；若 `truncated=true` 且页面顺序与后端 Top-K 顺序不同，界面会明确标注当前只重排了已载入集合，并提供按当前顺序重新扫描全局 Top-K 的按钮。

## 当前准确性边界

- 饰品、主动和被动道具基于内置 J460 全解锁 Profile；Profile 改变时需要重新采集并复核。
- 卡牌按 Repentance 的普通/特殊/逆位卡生成规则计算；伊甸不会从这一过程获得符文或魂石。
- 胶囊会从运行种子重建 J460 全解锁的 13 色效果洗牌，并计算金色/马胶囊分支。`pocket_id` 是未受 PHD、假 PHD、幸运脚等角色修正的原始效果；`pill_color` 是实际颜色值。
- 胶囊效果 0（Bad Gas）是合法筛选值。金色胶囊会连续改变效果，不能用单个 `pill_effect_ids` 表示。
- 改变胶囊池、解锁状态或胶囊规则的内容 Mod 仍需重新采集 Profile 并用 Lua 观察器复核。
- 钱、钥匙、炸弹已按伊甸自身的开局资源 RNG 分支计算；同一局只会出现其中一类，范围依次是 `0..5`、`0..1`、`0..2`。
- 黄针方向 RNG 按固定上游提交的 `GetCollectibleRNG(240)` 推导和 Fisher–Yates 洗牌重新实现；数值变化使用 Repentance 固定增减量。当前没有把它扩展为通用道具属性结算器。
- 黄针心之容器目前只提供上升/下降/不变方向，不提供一个合并红心、空心容器边界后的 `post_red_hearts` 数值。
