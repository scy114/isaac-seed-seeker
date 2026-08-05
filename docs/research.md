# Research Notes

检索日期：2026-08-04。

## Local facts

- 本机日志报告 `The Binding of Isaac: Repentance+ v1.9.7.17.J460`。
- Steam manifest build ID 为 `22878971`。
- 当前已解包 `items.xml` 的最高 ID 为 732；这不能证明物品标签、物品池或 RNG 调用序列未改变。
- 本地 `eden-seed-finder-master` 是较旧快照，核心方案是 C++ 全域预筛后由 Lua Mod 在游戏内逐个验证。

## Primary references

### 2o181o28/eden-seed-finder

- URL: https://github.com/2o181o28/eden-seed-finder
- License: AGPL-3.0.
- Value: C++ RNG、Eden 起始状态、Home/道具池条件、Lua 真值验证。
- Decision: 复用架构思想，不复制实现；若未来直接派生，需要单独决定 AGPL 路线。

### KamiMisuzu/isaac-repentance-eden-seed-decoder

- URL: https://github.com/KamiMisuzu/isaac-repentance-eden-seed-decoder
- Reviewed commit: `3ad022d047114a921550070ae3708384ff648178`（2026-06-06）。
- Declared license: README/`pyproject.toml` 声明 MIT，但仓库没有独立 LICENSE 文件。
- Value: J460 命名实现、Python/Numba 搜索、Profile、Web UI、运行时表提取。
- Caveat: README 声明适配 `v1.9.1.17`，本机日志为 `v1.9.7.17.J460`；不能仅凭内部函数名里的 J460 视为版本已验证。
- Decision: Python 校验层继续通过外部目录动态加载并记录 commit；原生层依据已研究的行为编写独立 C++ 实现，不收录第三方源码文件。所有离线结果仍标记为需要同 Profile 游戏观察验证。

### HtheChemist/EdenGenerator

- URL: https://github.com/HtheChemist/EdenGenerator
- License: 未发现。
- Value: 逆向 Xorshift、并行反推思路。
- Decision: 仅作为研究线索。

### mzmmmm/Isaac_Repentance_Seed_Calculator

- URL: https://github.com/mzmmmm/Isaac_Repentance_Seed_Calculator
- License: MIT.
- Value: 楼层种子、房间和物品池计算的历史实现。
- Caveat: 作者明确说明基于 2021 年版本，当前版本可能不准确。

### Documentation

- Eden Generation: https://bindingofisaacrebirth.wiki.gg/wiki/Eden_Generation
- Seeds Lua API: https://wofsauge.github.io/IsaacDocs/rep/Seeds.html
- RNG Lua API: https://wofsauge.github.io/IsaacDocs/rep/RNG.html
- REPENTOGON: https://repentogon.com/

## Verified design consequences

- Profile 是搜索主键的一部分，不能只记录八字符种子。
- 游戏内验证不是临时脚本，而是长期 correctness oracle。
- 高速内核必须声明支持的 build，并用该 build 的 golden observations 验证。
- 金色饰品在运行时使用高位标记；筛选按 `raw & 0x7FFF` 与基础饰品 ID 比较。
- REPENTOGON 可以增强房间级观测，但基础伊甸筛选不应硬依赖它。

## Frontend asset research

2026-08-05 对 Wiki、社区工具和字体资源做了单独的可再分发性检查：

- [IsaacSans](https://www.fontspace.com/isaacsans-font-f18283) 是 Shrapnel 制作的以撒风格字体，来源页和下载包均标记为 Public Domain。仓库保留字体文件、原始许可文字和来源链接，用于英文短标签。
- [LanaPixel](https://opengameart.org/content/lanapixel-localization-friendly-pixel-font) 是 eishiya 制作、游戏 CJK 资源实际采用的像素字体；001.003 轮廓 TTF 依据 SIL OFL 1.1 随程序嵌入，用于离线中文界面。仓库保留完整许可、版本与校验值。
- [External Item Descriptions](https://github.com/wofsauge/External-Item-Descriptions) 是最成熟的界面参照之一，但当前仓库未发现 LICENSE 文件，因此只参考信息布局，不复制其字体、图片或代码。
- [Isaac Codex](https://github.com/ceressa/isaac-codex) 明确区分 MIT 代码与游戏像素图：道具图仍归 Nicalis / Edmund McMillen 所有。其他若干社区工具也会随代码收录游戏图标，但开源代码许可证并不会自动覆盖这些图像。
- 灰机 Wiki 的条目数据快照继续按源页面声明的 CC0 使用；这不等于 Wiki 展示的官方游戏图标也变成 CC0。

因此发行包不收录从 Wiki、Platinum God 或其他仓库复制的官方道具图。Windows WebUI 会在启动时定位玩家自己的 Steam 安装，并只从本机 `extracted_resources/resources/gfx/items` 提供道具和饰品图标。图标不会写回仓库，也不会随 EXE 再分发；未解包资源时界面自动退回纯文字。

## Native generic Eden backend

原生后端现已复现并输出以下道具结算前字段：红心、魂心、伤害/移速/射速/弹速/幸运随机修正、换算后的基础射程，以及互斥的饰品/卡牌/胶囊口袋物。实现依据 Eden Generation 技术页中的 Repentance 生成顺序，并以本机 J460 表、固定种子向量和外部慢路径交叉检查。

当前有意保留的边界：

- 属性不叠加主动或被动开局道具的效果；否则需要完整的道具效果结算模型。
- 胶囊会从运行种子重建全解锁 J460 的 13 色原始效果映射，并计算金色与马胶囊分支；结果同时输出原始效果 ID 与 `pill_color`。
- 硬币、钥匙、炸弹尚未进入原生条件。
- 游戏内 Lua 观察现已覆盖普通卡、特殊卡、逆位卡、普通胶囊、金色胶囊和马胶囊。2026-08-05 的 13 种子批量观测中，颜色分支与非金色胶囊的原始效果均和原生预测逐项一致；游戏仍是最终 oracle。

## Fast-path null-entry finding

固定提交的快速表压缩会把 `proc.json` 的 `null` 槽位留成 `item_id=0 / blocked=0 / type=0`。快速物品循环因此可能把空槽当作合法非主动道具并提前停止；后续慢路径只复核快速阳性，无法恢复假阴性。

对全域目标 169 做 A/B 后：

- 原快速路径：890 条；
- 将空槽显式标记为无效后：901 条；
- 原 890 条全部保留；
- 新增 11 条逐条通过第三方慢路径复核。

因此修正后的 decoder-exact 黄金基线是 901。该基线仍不等于 901 条全部经过游戏实机确认，状态与摘要记录在 `tests/fixtures/j460-target-169-golden.json`。
