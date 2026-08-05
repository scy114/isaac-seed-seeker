# 离线名称目录与输入设计

## 目标

让玩家在主动、被动、饰品、卡牌和胶囊输入框中直接键入中文名、英文名、俗称、拼音或 ID，界面推荐候选项，最终仍向现有搜索内核提交整数 ID。

第一版只解决“找到并选择正确 ID”。图标、道具效果全文、道具结算后的真实属性和跨版本自动合池不进入本阶段。

## 数据流水线

```text
固定 revision 的灰机 Wiki 数据表（名称、别名、拼音）
    + J460 Profile（主动/被动分类、伊甸池可用性）
    + 当前 RNG 实现（卡牌、普通/大胶囊可生成集合）
    -> eden-start-catalog.j460.json
    -> 后续作为 RCDATA/静态资源嵌入原生 EXE
    -> WebUI 本地检索
    -> 只把 search_id 传给 C++ HTTP API
```

原生内核不认识名称，Wiki 也不会在程序运行时被访问。这样名称体验和 RNG correctness 分层：换翻译表不会改变扫描结果，离线 EXE 也没有网络依赖。

## 来源与版本固定

主数据源是灰机 Wiki 的 [`Data:Item.tabx`](https://isaac.huijiwiki.com/wiki/Data%3AItem.tabx) 与 [`Data:ItemKeywords.tabx`](https://isaac.huijiwiki.com/wiki/Data%3AItemKeywords.tabx)。两张数据页逐表标注为 CC0；仓库仍保留来源、revision、时间戳和内容 SHA-1，方便复查与更新。

`data/catalog/sources.lock.json` 是唯一的上游版本入口。`scripts/fetch-item-catalog.ps1` 通过 MediaWiki revisions API 拉取指定 revision，并在写入前校验标题、revision、时间戳和 SHA-1。当前 J460 Profile 的 721 个收藏品 ID 与 Wiki 表逐 ID 对齐；分类与“伊甸能否抽到”以本地 Profile 为准。

## 条目模型

每项的稳定身份是 `(kind, search_id)`：

- `kind`：`active`、`passive`、`trinket`、`card` 或 `pill`；
- `search_id`：传给现有搜索 API 的整数；
- `source_id`：Wiki 的原始 ID，大胶囊仍指向其基础胶囊效果；
- `name_zh` / `name_en`：主显示名；
- `aliases` / `pinyin`：补充搜索键；
- `variant`：当前目录均为 `normal`；马胶囊通过结果的 `pill_color` 标志区分，不创建第二套效果 ID；
- `quality`：主动/被动收藏品的 `0..4` 品质，其他类型为 `null`；
- `available_for_eden`：当前内置全解锁 J460 Profile 是否可能作为伊甸开局出现。

完整机器契约见 `schemas/item-catalog.schema.json`。

## ID 归一化

### 饰品

金色饰品不建立第二份条目。普通和金色版本都使用基础饰品 ID；这与现有 C++ 内核的归一化保持一致。

### 胶囊

胶囊使用 Wiki 原始效果 ID `0..49`。全解锁 J460 会为每局洗牌 13 色胶囊池，因此 50 种效果都可能成为伊甸开局胶囊的原始效果。

普通与马胶囊提交相同的基础效果 ID；是否为马胶囊记录在结果的 `pill_color & 2048`。目录不会再生成 `基础效果 ID + 55` 的重复条目，但会保留“大胶囊/马胶囊”名称与拼音别名。金色胶囊没有单一固定效果，不进入具体效果条目。

### 卡牌

Wiki 中保留全部卡牌，当前 RNG 可生成集合则由内核规则标记：`1..22`、`42..54`、`56..78` 和 `80`。其余卡牌仍可被查到，但在当前 Profile 选择器中禁用并说明原因。

## 本地搜索规则

每个输入框先按自己的类型过滤，避免主动与被动同 ID 或相似名称混在一起。文本索引在加载目录时一次性生成：

1. 对输入和索引字段做 Unicode NFKC、大小写折叠、首尾空白清理；
2. 另建去空格、连接号、撇号和常见标点的紧凑键，兼容 `Guppys Head` / `Guppy's Head`；
3. `#169` 和纯数字优先解释为当前类型的精确 ID；
4. 依次按精确主名称、精确别名、主名称前缀、别名前缀、拼音前缀、名称/别名包含匹配排序；
5. 同分时依次优先当前 Profile 可用项、中文主名称、较小 ID；最多显示 12 条。

输入框使用可多选 combobox：选中后变成 `名称 · #ID` 芯片，同一类别的多个芯片保持现有 OR 语义，不同类别仍为 AND。排除条件复用同一个选择器，但显示为排除色。删除芯片不会改写其他条件。

不可用项默认不占普通推荐结果；精确输入 ID 或完整名称时仍显示为禁用项，并标注“当前 J460 伊甸池不可用”。这样既不误导玩家，也方便发现 Profile 或版本差异。

## 更新流程

```powershell
# 普通复现：已有固定快照时全程离线，只重新生成目录
.\scripts\fetch-item-catalog.ps1

# 上游表更新：调研并修改锁文件后，拉取新 revision
.\scripts\fetch-item-catalog.ps1 -Force

# 本地游戏 Profile 更新：从受控原始快照刷新精简索引，再生成目录
python .\scripts\generate-builtin-profile.py
python .\scripts\generate-item-catalog.py

# 校验生成物没有漂移并运行回归
python .\scripts\generate-item-catalog.py --check
python -m pytest
```

更新时必须审阅各类型总数、可用数量、名称改动和 Profile 交集。revision 变化不自动等于游戏算法变化；若 J460 之后的游戏版本出现池或 ID 差异，应生成新的 Profile 专属目录，而不是覆盖旧目录。

## WebUI 接入状态

目录已经作为 `RCDATA` 嵌入原生 EXE，由只监听回环地址的 `/catalog.json` 提供给页面。浏览器端索引器和多选 combobox 不依赖框架；主动、被动、饰品、卡牌、胶囊及其排除栏均复用同一组件。结果表也使用目录显示名称。

C++ API、搜索任务 JSON 和 RNG 内核保持不变；页面在提交任务前完成 `CatalogEntry -> search_id` 投影。若目录加载失败，页面退回原始 ID 输入，不阻断既有搜索能力。
