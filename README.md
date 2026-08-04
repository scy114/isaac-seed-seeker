# Isaac Seed Seeker

面向《以撒的结合：忏悔+》伊甸开局的离线种子筛选器。

项目当前已经有一个不依赖 Python、Node.js 或网络的 Windows 原生预览版：C++20 内核扫描种子，EXE 在 `127.0.0.1` 启动内嵌 WebUI。默认内置本机全解锁 J460 Profile，也保留 Python + Lua 真值验证链作为开发期 correctness oracle。

## 当前原生版能力

- 双击单个 EXE，自动打开本地 WebUI。
- 输入任意饰品 ID、主动道具 OR 集合和被动道具 OR 集合。
- 普通/金色饰品统一按基础 ID 匹配。
- 多线程扫描任意 `uint32` 区间或全部 `2^32 - 1` 个可搜索值。
- 实时进度、速度、命中数、停止按钮和 TXT 导出。
- 内置 `v1.9.7.17.J460` 全解锁 Profile，不需要随 EXE 分发原始 JSON。
- CLI 单种子检查与 JSON 搜索输出。
- 静态链接 MinGW 运行库；当前 EXE 约 3.2 MB，只依赖 Windows 系统 DLL。

WebUI 当前聚焦“饰品 AND 主动组 AND 被动组”。属性、血量、资源、卡牌、胶囊以及用户 Profile 提取仍在后续通用伊甸搜索范围内；不扩展完整楼层、房间或掉落模拟。

## 当前目标与修正结果

默认条件固化在 `examples/eden-target-169.json`：

- 饰品基础 ID：`169`，普通版和金色版都匹配；
- 主动道具：`145` 或 `133`；
- 被动道具：`81`、`134`、`187`、`212`、`665` 中任意一个；
- 三组条件必须同时成立。

C++ 全域扫描在当前机器的 Release 检查中以 8 线程耗时约 18–19 秒，得到 **901** 条离线候选。早期 Python 快速路径得到的 890 条没有错报，但漏掉了 11 条：第三方表压缩把部分 `null` 槽位表示成全零合法条目，提前终止了被动物品抽取。11 条新增候选均已通过第三方慢速精确路径复核；完整候选仍应由游戏内 Lua 观察器分层验证。

固定 Profile、查询与候选摘要记录在 `tests/fixtures/j460-target-169-golden.json`。

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
  --trinket 169 `
  --active 145,133 `
  --passive 81,134,187,212,665 `
  --start 1 `
  --end 4294967295 `
  --threads 8 `
  --output data\target-169-native.json
```

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
