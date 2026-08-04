# Isaac Seed Seeker

面向《以撒的结合：忏悔+》伊甸开局的种子筛选器。

当前 MVP 采用“候选生成 → 游戏内真值观测 → 离线筛选”的闭环。这样可以先准确支持本机 Repentance+ J460，再逐步加入经过 golden tests 校准的高速离线预测器，而不会把旧版本 RNG 规律误当作当前版本事实。

## 当前能力

- 以撒种子字符串与 `uint32` 互转，并校验校验和。
- 从本机日志和资源目录生成游戏 Profile。
- 校验伊甸筛选任务，按范围或显式列表生成候选种子。
- 把任务编译成 Lua Mod 可读取的候选表。
- Lua Mod 自动切换候选种子并记录伊甸的属性、血量、资源、主动/被动道具与口袋物品。
- 从 `log.txt` 导入 JSONL 观测，并在游戏外重复筛选。

## 快速开始

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -e .

$saveDir = Join-Path $env:USERPROFILE "Documents\My Games\Binding of Isaac Repentance+"
$gameDir = "F:\steam\steamapps\common\The Binding of Isaac Rebirth"

python -m isaac_seed_seeker.cli detect-profile `
  --log (Join-Path $saveDir "log.txt") `
  --game-dir $gameDir `
  --output data\profiles\local-j460.json

python -m isaac_seed_seeker.cli validate examples\eden-job.json
python -m isaac_seed_seeker.cli compile-job examples\eden-job.json
```

把 `mod/isaac_seed_seeker` 复制到游戏的 Mods 目录后进入一局普通伊甸游戏：

- 按 `T` 开始候选验证。
- 按 `Y` 停止。
- Mod 会向游戏 `log.txt` 输出以 `ISAAC_SEED_SEEKER observation` 开头的 JSON 记录。

导入并筛选：

```powershell
python -m isaac_seed_seeker.cli import-log `
  (Join-Path $saveDir "log.txt") `
  --output data\observations.jsonl

python -m isaac_seed_seeker.cli query data\observations.jsonl `
  --job examples\eden-job.json `
  --output data\matches.jsonl
```

## 准确性边界

游戏版本、存档解锁状态、难度和启用的 Mod 都可能改变结果。每条观测都会保留 Profile ID 和游戏版本；跨 Profile 的结果默认不应混用。

当前没有复制第三方种子筛选器源码。第三方项目、许可证和版本差异记录在 [docs/research.md](docs/research.md)。
