# Native Backend API

原生后端由 C++20 实现，CLI 与本地 HTTP 服务共用同一个预测和筛选内核。默认使用内置的 `j460-full-unlock` Profile。

双击 EXE 即可使用覆盖全部下列条件的 WebUI；CLI 与 HTTP 接口主要用于自动化、复核和后续二次开发。

## 语义

- 不同类别之间使用 AND，同一个 `*_ids` 列表内部使用 OR。
- `*_exclude_ids` 在对应类别命中后排除指定 ID。
- `none`、`trinket`、`card`、`pill` 四种口袋状态互斥。
- `pill_effect_ids` 是胶囊效果 ID，不是胶囊颜色 ID。
- 红心、魂心和属性都是道具结算前的伊甸生成值。
- `damage`、`move_speed`、`tears`、`range`、`shot_speed`、`luck` 使用游戏 Found HUD 的真实数值口径。
- 伤害、移速、弹速和幸运由角色模板基准叠加随机修正；`tears` 按 Repentance 泪延迟公式换算为每秒泪弹数。
- JSON 结果仍返回 `damage_delta`、`move_speed_delta`、`tears_delta`、`shot_speed_delta`、`luck_delta`，旧版筛选参数也继续兼容，供脚本迁移和预测复核使用。
- `range` 是从内部距离修正换算后的基础显示值，范围约为 5.0 到 8.0。
- 所有区间边界均为闭区间。
- 主动与被动收藏品品质来自固定 Wiki 数据表，取值为 `0..4`；`total_quality` 是两件开局道具品质之和，取值为 `0..8`。
- 搜索始终扫描完整范围并统计真实命中数，但只保留按指定顺序最优的 Top-K，内存占用受 `max_results` 限制。

## CLI

```powershell
# 检查单个 uint32 种子
.\build\native\IsaacSeedSeeker.exe inspect --seed 10161220

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
- `damage`、`move-speed`、`tears`
- `range`、`shot-speed`、`luck`

旧版 `damage-delta`、`move-speed-delta`、`tears-delta`、`shot-speed-delta`、`luck-delta` 参数仍可使用，但新界面和新脚本应优先使用真实面板值。

排序参数：

- `--sort seed|health|damage|move-speed|tears|range|shot-speed|luck|active-quality|passive-quality|total-quality`
- `--direction asc|desc`
- `--max-results N`：CLI 默认 `1000`，内核允许 `1..100000`。

血量先比较红心，红心相同再比较魂心。主动/被动品质相同时按对应道具 ID 升序；总品质相同时依次按主动 ID、被动 ID、种子数值升序。其他属性相同则按种子数值升序。

## Local HTTP

不带参数运行 EXE 或执行 `IsaacSeedSeeker.exe serve` 后，程序只监听随机的 `127.0.0.1` 端口。启动日志会给出 URL 和会话 token。所有 POST 请求必须把 token 放入 `X-Isaac-Token` 请求头。

### 检查单个种子

`POST /api/v1/inspect`

```json
{"seed_u32": 2}
```

### 开始搜索

`POST /api/v1/search`

```json
{
  "pill_effect_ids": [12],
  "active_ids": [639],
  "passive_ids": [393],
  "red_hearts_min": 2,
  "red_hearts_max": 2,
  "damage_min": 4.05,
  "range_min": 7.42,
  "range_max": 7.43,
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

数值字段使用下划线形式：`red_hearts_min`、`red_hearts_max`，依此类推；可用字段与 CLI 的数值参数相同。

`sort_key` 可用值与 CLI 排序项一致，但使用下划线形式，例如 `move_speed`、`active_quality` 和 `total_quality`。`sort_direction` 为 `asc` 或 `desc`。本地 HTTP/WebUI 的 `max_results` 默认 `1000`、允许 `1..10000`。

### 状态与结果

- `GET /api/v1/search/status`
- `GET /api/v1/search/results`
- `GET /api/v1/search/results.txt`
- `POST /api/v1/search/cancel`
- `POST /api/v1/shutdown`

JSON 结果中的 `count` 是实际保留并返回的数量，`total_count` 是完整扫描的真实命中数，`truncated` 表示是否因 `max_results` 截断。`sort_key`、`sort_direction` 和 `result_limit` 记录本次 Top-K 契约；每条结果包含 `active_quality`、`passive_quality` 与 `total_quality`。页面、JSON 和 TXT 使用完全相同的顺序。

## 当前准确性边界

- 饰品、主动和被动道具基于内置 J460 全解锁 Profile；Profile 改变时需要重新采集并复核。
- 卡牌按 Repentance 的普通/特殊/逆位卡生成规则计算；伊甸不会从这一过程获得符文或魂石。
- 胶囊当前输出 RNG 选中的效果 ID；游戏内药丸颜色映射和内容 Mod 仍应使用 Lua 观察器做最终确认。
- 硬币、钥匙、炸弹，以及开局道具作用后的最终面板属性尚未进入原生筛选条件。
