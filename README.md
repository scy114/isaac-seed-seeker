# Isaac Seed Seeker

面向《以撒的结合：忏悔+》伊甸开局的离线种子筛选器。

项目当前已经有一个不依赖 Python、Node.js 或网络的 Windows 原生预览版：C++20 内核扫描种子，EXE 在 `127.0.0.1` 启动内嵌 WebUI。默认内置本机全解锁 J460 Profile，也保留 Python + Lua 真值验证链作为开发期 correctness oracle。

## 当前原生版能力

- 双击单个 EXE，自动打开本地 WebUI。
- 若游戏已通过官方 ResourceExtractor 解包，页面会自动显示本机的真实道具与饰品图标；没有解包时仍可正常使用文字界面。
- WebUI 可按中文名、英文名、俗称、拼音或 ID 多选口袋物、主动/被动道具，并组合基础血量和六项随机属性区间。
- 结果表头可直接按血量、属性、主动/被动品质或两件道具总品质升降序排列；已载入结果保存在浏览器内存并分页展示。命中超过载入上限时，可按当前表头顺序重新扫描精确的全局 Top-K。
- 不同筛选类别之间为 AND，同一 ID 列表内部为 OR；各 ID 类别也支持排除列表。
- 普通/金色饰品统一按基础 ID 匹配。
- 多线程扫描任意 `uint32` 区间或全部 `2^32 - 1` 个可搜索值。
- 实时进度、速度、命中数、停止按钮和 TXT 导出。
- 内置 `v1.9.7.17.J460` 全解锁 Profile，不需要随 EXE 分发原始 JSON。
- CLI 单种子检查与 JSON 搜索输出。
- 静态链接 MinGW 运行库；玩家只需要单个 EXE，不需要安装 Python、Node.js 或 MinGW。

WebUI、原生 CLI 与本地 HTTP API 使用同一套通用后端。页面默认从空白条件开始，不携带特定道具组合预设。资源、道具结算后的最终属性、用户 Profile 提取仍在后续范围内。本项目不扩展完整楼层、房间或掉落模拟。

道具名称层已经接入 WebUI：仓库内的 J460 离线目录包含中英文名、俗称、拼音、ID 和当前伊甸池可用性，并处理金色饰品与大胶囊 ID 归一化。选择结果只在页面内投影成 ID，现有 C++ 搜索 API 与 RNG 内核不接收名称；设计、来源与更新方式见 [`docs/item-catalog.md`](docs/item-catalog.md)。

这里的“属性”严格指伊甸生成阶段、两个开局道具结算前的真实面板值：伤害以 3.5、移速以 1.0、弹速以 1.0、幸运以 0 为模板基准，射程已经换算为显示值；射速则按 Repentance 的泪延迟公式换算成 Found HUD 的每秒泪弹数。血量同样不包含开局道具追加的心。胶囊使用效果 ID，不是颜色 ID。

## 回归验证样例：目标 169

项目早期用于验证内核的目标 169 查询保留在 `examples/eden-target-169.json`，但不作为 WebUI 默认条件或预设：

- 饰品基础 ID：`169`，普通版和金色版都匹配；
- 主动道具：`145` 或 `133`；
- 被动道具：`81`、`134`、`187`、`212`、`665` 中任意一个；
- 三组条件必须同时成立。

C++ 全域扫描在当前机器的 Release 检查中以 8 线程耗时约 18–19 秒，得到 **901** 条离线候选。早期 Python 快速路径得到的 890 条没有错报，但漏掉了 11 条：第三方表压缩把部分 `null` 槽位表示成全零合法条目，提前终止了被动物品抽取。11 条新增候选均已通过第三方慢速精确路径复核；完整候选仍应由游戏内 Lua 观察器分层验证。

固定 Profile、查询与候选摘要记录在 `tests/fixtures/j460-target-169-golden.json`。

## 给玩家的 Windows 测试包

玩家版是 Windows x64 便携 ZIP。完整解压后双击 `IsaacSeedSeeker.exe` 即可；普通玩家说明见 [`docs/quick-start.zh-CN.txt`](docs/quick-start.zh-CN.txt)。

维护者可用一条命令构建、测试、打包并验证解压后的 EXE：

```powershell
.\scripts\package-windows.ps1
```

输出位于 `dist/`：

- `IsaacSeedSeeker-v0.1.0-windows-x64.zip`
- `SHA256SUMS.txt`
- 同名展开目录，便于本地检查

ZIP 内包含 EXE、中文使用说明、发行 NOTICE、离线目录来源说明和字体许可证。正式公开发布前的剩余事项见 [`docs/release-checklist.md`](docs/release-checklist.md)，其中项目主许可证仍需由维护者明确选择。

## 构建并启动原生版

开发机需要支持 C++20 的 MinGW-w64 `g++` 与 `windres`。它们只用于构建，不是最终玩家运行时依赖。

```powershell
.\scripts\build-native.ps1 -RunTests
.\build\native\IsaacSeedSeeker.exe
```

双击 EXE 与不带参数运行效果相同。程序会选择一个空闲本地端口并打开浏览器；网页中的“关闭本地程序”会结束后台进程。

接口冒烟测试：

```powershell
.\scripts\smoke-webui.ps1
```

命令行检查：

```powershell
.\build\native\IsaacSeedSeeker.exe inspect --seed 10161220

.\build\native\IsaacSeedSeeker.exe search `
  --trinket 1,2 `
  --active 105 `
  --damage-min 4.0 `
  --start 1 `
  --end 4294967295 `
  --threads 8 `
  --output data\matches.json

# 通用组合示例：卡牌 10、指定主动/被动、2 红心、真实伤害至少 4.05
.\build\native\IsaacSeedSeeker.exe search `
  --card 10 `
  --active 639 `
  --passive 393 `
  --red-hearts-min 2 `
  --red-hearts-max 2 `
  --damage-min 4.05 `
  --range-min 7.42 `
  --range-max 7.43 `
  --end 100
```

CLI 完整参数和本地 HTTP 请求字段见 [`docs/native-api.md`](docs/native-api.md)。搜索结果默认按种子数值升序保留全局最优的 1,000 条，但仍会扫描完整范围并返回真实 `total_count`；WebUI 最多载入 10,000 条，可直接点击结果表头排序。若命中数未超过上限，排序覆盖全部命中；若已截断，页面会提供按当前顺序重新扫描全局 Top-K 的入口。

仓库也提供标准 `CMakeLists.txt`，用于后续 MSVC/GitHub Actions Release 构建。

## 默认 Profile

原生版内置的 Profile 来自本机 `v1.9.7.17.J460`、全解锁、未启用大型内容 Mod 的运行时快照：

- `proc.json` SHA-256：`02e26398a02f46ee0b581f52bee8ad5e7133d6417dc9a8f967df114c63481faf`
- `trinket_pool.json` SHA-256：`fe5d6a4c3ee6c04f68665cd6958426addf3f109d6ffffd12062ba33b283aba37`

兼容性判断使用排除进程地址、采集当局种子等瞬时字段后的语义摘要，而不是直接比较原始文件：

- 物品表语义 SHA-256：`5698cf5c80a103c04974026f2dc18eedcd1207e0420f7f33371df94f9aed0aae`
- 饰品池语义 SHA-256：`4c0513dd16c165ef9de01e4b425b7850b7b642e7a2a9256f677854a8f2a721d3`

搜索算法可能跨版本保持不变，但物品池、解锁状态和内容 Mod 仍可能改变输出。后续版本会自动比较游戏/Profile 指纹，并在不匹配时引导用户重新提取。

## Python 与游戏内验证工具

Python 版本继续承担以下开发用途，不会进入玩家原生发行包：

- `SearchJob`、Profile 和观察记录契约；
- 外部 J460 解码器交叉验证；
- Lua 候选任务生成；
- 从 `log.txt` 导入游戏真值并再次筛选。

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -e .

$env:PYTHONPATH = "src"
python -m isaac_seed_seeker.cli validate examples\eden-target-169.json
```

Lua Mod 位于 `mod/isaac_seed_seeker`：按 `T` 开始候选验证，按 `Y` 停止；观察结果以带固定前缀的 JSON 写入游戏 `log.txt`。

## 上游与研究来源

- [2o181o28/eden-seed-finder](https://github.com/2o181o28/eden-seed-finder)：早期 C++ 全域预筛和 Lua 游戏内验证思路，AGPL-3.0。
- [KamiMisuzu/isaac-repentance-eden-seed-decoder](https://github.com/KamiMisuzu/isaac-repentance-eden-seed-decoder)：J460 伊甸生成、饰品池和运行时表提取研究；README/项目元数据声明 MIT。
- [Eden Generation Wiki](https://bindingofisaacrebirth.wiki.gg/wiki/Eden_Generation)：伊甸生成规则和历史分析。

本仓库没有直接收录上述项目的源码文件。C++ 内核是依据已研究的 RNG 行为重新实现，并用固定向量、Python 慢路径和后续游戏观察结果交叉验证。更完整的版本、许可证与决策记录见 `docs/research.md`。
