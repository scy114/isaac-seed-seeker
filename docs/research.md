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
- Declared license: README/`pyproject.toml` 声明 MIT，但仓库没有独立 LICENSE 文件。
- Value: J460 命名实现、Python/Numba 搜索、Profile、Web UI、运行时表提取。
- Decision: 用于识别验证点和模块边界；许可证文件澄清前不复制代码。

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
- REPENTOGON 可以增强房间级观测，但基础伊甸筛选不应硬依赖它。
