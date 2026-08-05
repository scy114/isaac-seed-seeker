# Design QA

## 对照目标与证据

- 主视觉真值：`docs/design/visual-target.png`
- 字体与背景的二级真值：`output/design-audit/01-wiki-home.png`
- 同尺寸实现截图：`output/design-audit/15-lanapixel-basement-room.png`
- 最新桌面截图：`output/design-audit/19-font-background-desktop.png`
- 窄屏截图：`output/design-audit/18-font-background-mobile.png`
- Wiki / 实现并排证据：`output/design-audit/17-wiki-font-background-comparison.png`
- 分层字体桌面截图：`output/design-audit/21-tiered-fonts-desktop.png`
- 全像素字 / 分层字体同尺寸对照：`output/design-audit/22-font-tier-comparison.png`
- 分层字体窄屏截图：`output/design-audit/23-tiered-fonts-mobile.png`
- 存档纸条遮挡参考：`C:/Temp/Temp/codex-clipboard-2cc5f11b-fe60-4212-b265-7395f8cde612.png`
- 纸条下移后的桌面截图：`output/design-audit/24-profile-card-lowered-desktop.png`
- 中等宽度截图：`output/design-audit/25-profile-card-lowered-medium.png`
- 下移后的窄屏截图：`output/design-audit/26-profile-card-mobile.png`
- 遮挡前 / 下移后局部对照：`output/design-audit/27-profile-card-before-after.png`
- 双语副标题桌面截图：`output/design-audit/28-bilingual-tagline-desktop.png`
- 双语副标题中等宽度截图：`output/design-audit/29-bilingual-tagline-medium.png`
- 双语副标题窄屏截图：`output/design-audit/30-bilingual-tagline-mobile.png`
- 原文案 / 双语副标题同尺寸对照：`output/design-audit/31-tagline-before-after.png`
- 放大后副标题桌面截图：`output/design-audit/32-enlarged-tagline-desktop.png`
- 放大后副标题中等宽度截图：`output/design-audit/33-enlarged-tagline-medium.png`
- 放大后副标题窄屏截图：`output/design-audit/34-enlarged-tagline-mobile.png`
- 放大前 / 放大后同尺寸对照：`output/design-audit/35-tagline-size-before-after.png`
- 副标题比例审查截图：`output/design-audit/36-subtitle-proportion-audit.png`
- 比例修正后桌面截图：`output/design-audit/37-balanced-subtitle-desktop.png`
- 比例修正后中等宽度截图：`output/design-audit/38-balanced-subtitle-medium.png`
- 比例修正后窄屏截图：`output/design-audit/39-balanced-subtitle-mobile.png`
- 比例失调 / 修正后同尺寸对照：`output/design-audit/40-subtitle-proportion-before-after.png`
- 状态：默认“目标 169”条件，离线目录、游戏图标、LanaPixel 字体和地下室背景均已载入，页面位于顶部。

## 视口与归一化

- Wiki 参考截图与同尺寸实现截图均为 `1265 × 712 px`，直接逐边并排，没有缩放、裁切或密度插值。
- 对应桌面 CSS 视口为 `1280 × 720`，浏览器可用内容宽度 `1265 px`，`devicePixelRatio = 1.5`。
- 最新桌面复核使用同一 `1280 × 720` CSS 视口，截图为 `1265 × 720 px`，`devicePixelRatio ≈ 1`。
- 窄屏 CSS 视口为 `390 × 844`，页面内容宽 `375 px`；文档与 body 的 `scrollWidth` 均为 `375 px`，无横向溢出。
- 字体分层前后的桌面截图均为 `1265 × 720 px`，对照图直接左右拼接为 `2530 × 720 px`，没有缩放或裁切。
- 本轮桌面 CSS 视口为 `1280 × 720`，实现截图为 `1265 × 720 px`；用户给出的 `456 × 250 px` 参考正好对应桌面截图右上角 `x=809, y=0` 的同尺寸区域，因此局部对照未缩放。
- 中等宽度 CSS 视口为 `900 × 720`，内容宽 `885 px`；窄屏仍为 `390 × 844`，内容宽 `375 px`。两者均无横向溢出。
- 双语副标题前后的桌面截图均为 `1265 × 720 px`，对应 `1280 × 720` CSS 视口；`31-tagline-before-after.png` 直接左右拼接为 `2530 × 720 px`，没有缩放或裁切。中等宽度和窄屏复核分别使用 `900 × 720` 与 `390 × 844` CSS 视口，页面内容宽分别为 `885 px` 与 `375 px`。
- 副标题字号调整前后的桌面截图均为 `1265 × 720 px`，对应 `1280 × 720` CSS 视口；`35-tagline-size-before-after.png` 直接左右拼接为 `2530 × 720 px`，没有缩放、裁切或密度插值。
- 比例审查前后的桌面截图均为 `1265 × 720 px`，对应 `1280 × 720` CSS 视口；`40-subtitle-proportion-before-after.png` 直接左右拼接为 `2530 × 720 px`，没有缩放、裁切或密度插值。响应式复核继续使用 `900 × 720` 与 `390 × 844` CSS 视口。

## Findings

- 没有仍需处理的 P0 / P1 / P2 差异。
- Wiki 首页当前背景更像暗室/箱子场景，实现有意改用更明确的地下室砖墙，因为这是用户本轮点名的方向。

## 必查表面

- 字体与排版：艺术标题保持透明位图；大标题、纸片区块标题和本轮双语副标题使用 LanaPixel；表单、说明、按钮、道具名和表格使用 Segoe UI / Microsoft YaHei UI；种子、ID、品质和统计数值使用 Cascadia Mono / Consolas 等宽栈。双语副标题现以 `34 px` 中文主句和 `21 px` 英文副句建立层级，较上一版的 `22 / 14 px` 明显放大；在 `700 px` 宽艺术标题下，中文和英文宽度占比分别由约 `18.7% / 19.6%` 提升到 `28.9% / 29.4%`。继续使用 `1.15` 行高与 `.08em` 字距，三个断点均无换行、截断或异常回退。
- 间距与布局：地下室房间框住标题、存档纸条和主面板，首屏层级接近 Wiki 的“房间内嵌界面”关系；`390 px` 窄屏改为单列，纸条与标题不重叠。
- 存档纸条位置：桌面 Hero 保持标题原位，纸条下移到标题与说明之后；实测桌面标题底边 `166 px`、纸条顶边约 `174.5 px`，不再相交。`900 px` 下标题右边 `581.7 px`、纸条左边 `608.1 px`，水平与垂直均留有间隔；手机端继续使用自然文档流。
- 色彩与视觉令牌：砖褐地下室、近黑面板、干血红与脏纸粉保持一致；背景增加暗色遮罩，避免墙体抢走表单对比度。
- 图像质量与素材：背景使用玩家本机 `gfx/backdrop/01_basement.png` 的真实地下室图集，在 Canvas 中以最近邻采样镜像拼成四角房间；没有把游戏背景打包进仓库，也没有用 CSS 图形、SVG、emoji 或占位图替代。标题和纸片继续使用真实素材。
- 文案与内容：删除泛化的长句说明，改为仿照 D6 与合成宝袋道具说明格式的两行短句“窥见你的命运 / Glimpse your destiny”，不加引号与句号；没有加入“无需 Python”“本地运行”等工程宣传语，其他筛选说明、版本、道具名称与应用实际状态一致。

## 交互与运行检查

- `body`、按钮和输入框最终计算为 UI 字体栈，H2/H3 为 `LanaPixel, "Microsoft YaHei", sans-serif`，ID 与统计数值为等宽字体栈；`document.fonts.status = loaded`。
- 桌面背景 Canvas 覆盖视口；窄屏 Canvas 为 `390 × 844 CSS px`，随视口重绘。
- 本地目录、LanaPixel 字体、地下室图集及 2000 万种子搜索冒烟测试通过；命中数为 3，首个种子为 `B74H HQPR`。
- 桌面与窄屏浏览器控制台均无 error / warning。
- “清空条件”将 8 个筛选条目降为 0，“恢复目标 169”重新恢复 8 个条目。
- 本轮仅调整 Hero 与纸条位置；桌面、中等宽度和手机端控制台均无 error / warning。
- 双语副标题的中文和英文均计算为 LanaPixel；`900 px` 下两个文字 span 与右侧存档纸条没有相交，`390 px` 下无换行或横向溢出；三个断点控制台均无 error / warning。
- 字号放大后再次测量：`1280 px` 下中文宽约 `130.7 px`、英文宽约 `137.0 px`；`900 px` 下二者与存档纸条均无相交，`390 px` 下文档 `scrollWidth = clientWidth = 375 px`。三个断点控制台仍无 error / warning。
- 比例修正后测量：`1280 px` 下中文宽约 `202.0 px`、英文宽约 `205.6 px`，与 `700 px` 艺术标题形成约三成宽的稳定层级；`900 px` 下两行副标题与存档纸条仍无相交，`390 px` 下文档 `scrollWidth = clientWidth = 375 px`。三个断点控制台均无 error / warning。

## 对照迭代历史

1. 先前一轮已经关闭标题比例和存档纸条位置的 `[P2]` 差异，证据保存在 `output/design-audit/11-post-fix-comparison-full.png`。
2. 本轮初始对照显示，正文仍是普通系统字体，背景只平铺地下室地面，缺少 Wiki 参考中明显的“房间边界”；这两处均为 `[P2]` 游戏辨识度差异。
3. 修正：嵌入 LanaPixel 001.003 TTF；增加本地游戏地下室图集接口；从真实图集中裁取角落并水平/垂直镜像，组成完整砖墙房间；保留暗色遮罩和无素材时的纯色回退。
4. 修后证据：`17-wiki-font-background-comparison.png` 显示字体节奏和房间式背景已与参考方向一致；`18-font-background-mobile.png` 验证窄屏没有横向溢出，`19-font-background-desktop.png` 验证最终桌面状态。此前两项 P2 均已关闭。
5. 用户复核后指出，全局使用像素字体会增加小字号和操作文字的阅读负担；该问题按 `[P2]` 可用性差异处理。
6. 修正：建立 `display / ui / mono` 三层字体令牌，只让 H2/H3 和装饰纸片保留像素风格；表单与操作恢复 UI 字体，机器数据使用等宽字体。`22-font-tier-comparison.png` 的同尺寸对照显示布局、色彩和素材未漂移，文字密度明显降低；`23-tiered-fonts-mobile.png` 验证窄屏宽度仍为 `375 px`，此前 P2 已关闭。
7. 用户截图指出右上存档纸条遮住艺术标题右端；测量确认标题与纸条在桌面端实际交叠约 `38 px`，按 `[P2]` 首屏层级问题处理。
8. 修正：桌面 Hero 从 `178 px` 增高到 `270 px`，保持标题在顶部，将纸条的绝对定位从 `23 px` 下移到 `158 px`；`740 px` 以下继续使用静态自然排列。`27-profile-card-before-after.png` 显示标题右端完全露出，`24/25/26` 三张截图验证三个断点均无新遮挡，此前 P2 已关闭。
9. 用户要求删除 Hero 中偏通用的长句，并参考 D6、合成宝袋的道具说明形式改成中英文游戏内短句；原文案被视为 `[P2]` 内容与艺术方向偏差。
10. 修正：替换为“窥见你的命运 / Glimpse your destiny”，两行均使用 LanaPixel，以字号和明度区分主次。`31-tagline-before-after.png` 显示标题、纸条、背景和主面板均未漂移，`28/29/30` 三张截图验证三个断点没有换行、遮挡或溢出，此前 P2 已关闭。
11. 用户复核后认为双语副标题仍偏小；该问题按 `[P2]` 首屏可读性与层级差异处理。
12. 修正：中文从 `18 px` 放大到 `22 px`，英文从 `11 px` 放大到 `14 px`，不改变文案、颜色、位置或其他组件。`35-tagline-size-before-after.png` 显示副标题层级更加清楚，`32/33/34` 三张截图验证三个断点均无新遮挡、换行或溢出，此前 P2 已关闭。
13. 用户再次复核指出副标题与艺术标题的比例仍失调；当前运行态测量确认副标题宽度仅约为艺术标题的 `19%`，按 `[P2]` 首屏信息层级问题处理。
14. 修正：中文从 `22 px` 放大到 `34 px`，英文从 `14 px` 放大到 `21 px`，把两行宽度占比提升到约 `29%`。`40-subtitle-proportion-before-after.png` 显示副标题已经从脚注层级提升为明确的第二层标题，`37/38/39` 三张截图验证三个断点均无新遮挡、换行或溢出，此前 P2 已关闭。

## 局部对照说明

`22-font-tier-comparison.png` 本身是同视口、同状态、同像素密度的字体专项对照；筛选标签、按钮、说明和道具 token 在首屏中均可直接判读，因此无需再次裁切。背景与整体构图继续由 `17-wiki-font-background-comparison.png` 覆盖。

存档纸条专项使用 `27-profile-card-before-after.png`：左侧是用户的原始遮挡截图，右侧是实现截图完全相同的 `456 × 250 px` 右上角裁切，可直接判断标题与纸条已经分离。

双语副标题专项使用 `31-tagline-before-after.png`：左右两侧是相同视口、状态、像素尺寸的完整首屏，短句本身在全视图中清晰可读，因此无需额外裁切；同时用 `29-bilingual-tagline-medium.png` 和 `30-bilingual-tagline-mobile.png` 检查响应式边界。

字号调整专项使用 `35-tagline-size-before-after.png`：左右两侧为相同视口、默认目标 169、完整目录已载入的同状态首屏，足以直接判断副标题的字体层级和周边留白；重要文字清晰可读，无需额外局部裁切。

比例修正专项使用 `40-subtitle-proportion-before-after.png`：左右两侧为当前审查轮次中捕获的同视口、同状态首屏，可直接比较标题宽度、副标题宽度和首屏留白；文字与周边纸条均清晰可判读，无需额外局部裁切。

## Implementation Checklist

- [x] 游戏同款中文像素字体已嵌入并带 OFL 许可证。
- [x] 装饰标题、操作文字和机器数据已分别路由到 display / ui / mono 字体栈。
- [x] 存档纸条已移到艺术标题下方，并通过 1280 / 900 / 390 三个宽度验证。
- [x] Hero 长说明已替换为中英文 LanaPixel 短句，并通过 1280 / 900 / 390 三个宽度验证。
- [x] 双语副标题已按标题比例放大到 `34 / 21 px`，并通过 1280 / 900 / 390 三个宽度复核。
- [x] 后端提供本机地下室图集端点，前端完成响应式房间拼接。
- [x] 桌面、窄屏、预设清空/恢复、控制台和搜索冒烟测试通过。
- [x] 没有剩余 P0 / P1 / P2 问题。

## 最终结果

final result: passed
