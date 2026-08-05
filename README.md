# Isaac Seed Seeker

一个给伊甸玩家用的离线种子筛选器。

这个项目起因很简单：我想找一个开局就能凑出嗝屁猫套装的伊甸种子。手动重开显然不现实，已有工具虽然能计算伊甸开局，但要搜饰品、中文名称和复杂组合仍然很麻烦，于是有了这个双击就能用的本地 WebUI。

![Isaac Seed Seeker 的搜索页面](docs/isaac-seed-seeker-webui.png)

## 下载与使用

仓库刚刚公开，首个 Windows x64 Release 还在整理。发布后会放在 [Releases](https://github.com/scy114/isaac-seed-seeker/releases)；如果你拿到的是朋友发来的测试包，使用方法如下：

1. 完整解压 ZIP，不要直接在压缩包预览窗口中运行；
2. 双击 `IsaacSeedSeeker.exe`；
3. 在自动打开的页面中填写条件，点击“开始扫描”；
4. 把命中的八位种子复制进游戏。

程序会在本机启动一个 `127.0.0.1` 页面，搜索也在本地完成。用完后点击页面底部的“关闭本地程序”即可。

更细的说明见 [中文快速上手](docs/quick-start.zh-CN.txt)。

## 现在能搜什么

- 饰品、卡牌、胶囊，以及没有口袋物的开局；
- 主动和被动道具；
- 红心、魂心、钱、钥匙和炸弹；
- 伤害、移速、射速、射程、弹速和幸运；
- 指定、排除、区间限制，以及多个条件的组合；
- 按属性、资源和道具品质排序，分页查看或导出 TXT。

道具既可以输 ID，也可以按中文名、英文名、常用俗称或拼音搜索。

不同栏之间是“并且”，同一栏里的多个候选是“任意一个”。比如最初的一次搜索就是：

```text
饰品 169
并且 主动 145 / 133 中任意一个
并且 被动 81 / 134 / 187 / 212 / 665 中任意一个
```

结果太多时，程序仍会扫完整个范围并记录真实命中数，只把当前排序下最靠前的一批载入页面。这样不会因为浏览器内存上限而把扫描提前截断。

## 黄针专页

实验性疗法有单独的搜索页面。可以指定哪些属性上升、下降或不变，也可以直接填写扎针后的最终属性范围。

目前能精确筛选伤害、移速、射速、射程、弹速和幸运；生命值暂时只判断上升、下降或不变。互相矛盾的条件会在扫描前提示，不会拿一个无解组合跑完整个种子空间。

## 目前的边界

当前内置 Profile 对应：

- 《以撒的结合：忏悔+》`v1.9.7.17.J460`；
- 全解锁存档；
- 未启用会改变角色、道具池或开局生成的大型内容 Mod。

普通页面显示的是伊甸生成时、开局道具生效前的基础属性；黄针专页会额外计算实验性疗法结算后的面板。当前还不能搜索一层宝箱房、Boss 或 Boss 奖励，也没有模拟所有道具对最终面板的影响。

便利性和界面类 Mod 通常不影响开局生成，但最终仍以种子在游戏里的实际表现为准。

## 这个项目从哪里来

这不是一次从零开始的逆向。最关键的路，前人已经走过了：

- [2o181o28/eden-seed-finder](https://github.com/2o181o28/eden-seed-finder) 提供了早期的伊甸全域搜索实现、游戏内验证方法和实验性疗法 RNG 研究；
- [KamiMisuzu/isaac-repentance-eden-seed-decoder](https://github.com/KamiMisuzu/isaac-repentance-eden-seed-decoder) 提供了 J460 伊甸生成、运行时表和道具池的重要参考；
- [falsidge/isaac_rng](https://github.com/falsidge/isaac_rng) 提供了忏悔+胶囊池与颜色映射研究；
- [Eden Generation Wiki](https://bindingofisaacrebirth.wiki.gg/wiki/Eden_Generation) 整理了伊甸生成规则。

这个仓库在这些工作的基础上，补上了饰品与口袋物搜索、中文名称目录、资源和属性筛选、复杂条件组合、排序与导出、黄针最终属性搜索，以及一个可以直接交给普通玩家使用的原生 Windows WebUI。

仓库没有直接收录上述项目的源码文件。参考版本、验证种子和实现边界记录在 [研究文档](docs/research.md) 中。

## 自己构建

<details>
<summary>开发环境与命令</summary>

需要 Windows、CMake 和支持 C++20 的 MinGW-w64 工具链。

```powershell
.\scripts\build-native.ps1 -RunTests
.\build\native\IsaacSeedSeeker.exe
```

生成完整的 Windows 压缩包并执行打包自检：

```powershell
.\scripts\package-windows.ps1
```

开发资料：

- [原生 CLI 与本地 API](docs/native-api.md)
- [项目结构](docs/architecture.md)
- [道具名称目录](docs/item-catalog.md)
- [RNG 研究与验证记录](docs/research.md)
- [后续功能调研](docs/research-floor1-and-post-item-stats.md)

</details>

## 反馈

发现对不上的种子，可以在 [Issues](https://github.com/scy114/isaac-seed-seeker/issues) 留下程序版本、游戏版本、筛选条件、种子，以及是否启用了内容 Mod。像卡牌、胶囊和会生成掉落物的道具这类问题，实际游戏反馈尤其有用。

当前仓库尚未附加开源许可证；在许可证明确前，请不要复制或重新分发仓库代码。
