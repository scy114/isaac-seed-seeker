const $ = (selector) => document.querySelector(selector);
const number = new Intl.NumberFormat("zh-CN");
const pageMode = document.body.dataset.page || "generic";
const treatmentMode = pageMode === "treatment";
const sessionToken = new URLSearchParams(window.location.search).get("token") || "";
let pollTimer = null;
let pocketCatalogKind = null;
let catalogEntries = [];
let currentResult = null;
let currentSortKey = "seed";
let currentSortDirection = "asc";
let currentPage = 1;
let lastSearchPayload = null;
const catalogByKey = new Map();
const catalogPickers = new Map();

function loadBackdropImage(source) {
  return new Promise((resolve, reject) => {
    const image = new Image();
    image.decoding = "async";
    image.addEventListener("load", () => resolve(image), {once: true});
    image.addEventListener("error", () => reject(new Error(`背景素材读取失败：${source}`)), {once: true});
    image.src = source;
  });
}

async function initializeBasementBackdrop() {
  const canvas = $("#basement-room");
  if (!canvas) return;
  const atlas = await loadBackdropImage("/game-assets/ui/basement-walls.png");
  const context = canvas.getContext("2d", {alpha: false});
  if (!context) return;
  let resizeFrame = null;

  const draw = () => {
    const width = window.innerWidth;
    const height = window.innerHeight;
    const scale = Math.min(window.devicePixelRatio || 1, 2);
    canvas.width = Math.max(1, Math.round(width * scale));
    canvas.height = Math.max(1, Math.round(height * scale));
    context.setTransform(scale, 0, 0, scale, 0, 0);
    context.imageSmoothingEnabled = false;

    const halfWidth = Math.ceil(width / 2);
    const halfHeight = Math.ceil(height / 2);
    const sourceWidth = 234;
    const sourceHeight = 156;
    const drawCorner = (x, y, targetWidth, targetHeight, flipX, flipY) => {
      context.save();
      context.translate(x + (flipX ? targetWidth : 0), y + (flipY ? targetHeight : 0));
      context.scale(flipX ? -1 : 1, flipY ? -1 : 1);
      context.drawImage(
        atlas,
        0,
        0,
        sourceWidth,
        sourceHeight,
        0,
        0,
        targetWidth,
        targetHeight,
      );
      context.restore();
    };

    drawCorner(0, 0, halfWidth, halfHeight, false, false);
    drawCorner(halfWidth - 1, 0, width - halfWidth + 1, halfHeight, true, false);
    drawCorner(0, halfHeight - 1, halfWidth, height - halfHeight + 1, false, true);
    drawCorner(halfWidth - 1, halfHeight - 1, width - halfWidth + 1, height - halfHeight + 1, true, true);
    context.fillStyle = "rgba(19, 10, 8, 0.56)";
    context.fillRect(0, 0, width, height);
  };

  draw();
  window.addEventListener("resize", () => {
    if (resizeFrame !== null) cancelAnimationFrame(resizeFrame);
    resizeFrame = requestAnimationFrame(() => {
      resizeFrame = null;
      draw();
    });
  });
}

function createItemIcon(kind, id) {
  if (!["active", "passive", "trinket"].includes(kind) || !Number.isInteger(Number(id)) || Number(id) <= 0) {
    return null;
  }
  const image = document.createElement("img");
  image.className = "item-icon";
  image.src = `/game-assets/${kind}/${Number(id)}.png`;
  image.alt = "";
  image.loading = "lazy";
  image.decoding = "async";
  image.setAttribute("aria-hidden", "true");
  image.addEventListener("error", () => image.remove(), {once: true});
  return image;
}

function normalizeCatalogText(value) {
  return String(value || "").normalize("NFKC").toLocaleLowerCase("zh-CN").trim().replace(/\s+/g, " ");
}

function compactCatalogText(value) {
  return normalizeCatalogText(value).replace(/[\s'’"“”·._\-—–:：/\\,，()（）\[\]【】]+/g, "");
}

function catalogKey(kind, id) {
  return `${kind}:${id}`;
}

function prepareCatalogEntry(entry) {
  return {
    ...entry,
    _names: [entry.name_zh, entry.name_en].map((value) => [normalizeCatalogText(value), compactCatalogText(value)]),
    _aliases: entry.aliases.map((value) => [normalizeCatalogText(value), compactCatalogText(value)]),
    _pinyin: entry.pinyin.map(normalizeCatalogText),
  };
}

function bestTextScore(values, query, compactQuery, scores) {
  let best = 0;
  let exact = false;
  for (const [normal, compact] of values) {
    if (normal === query || (compactQuery && compact === compactQuery)) {
      best = Math.max(best, scores.exact);
      exact = true;
    } else if (normal.startsWith(query) || (compactQuery && compact.startsWith(compactQuery))) {
      best = Math.max(best, scores.prefix);
    } else if (normal.includes(query) || (compactQuery && compact.includes(compactQuery))) {
      best = Math.max(best, scores.contains);
    }
  }
  return {score: best, exact};
}

function scoreCatalogEntry(entry, rawQuery) {
  const query = normalizeCatalogText(rawQuery);
  const numeric = query.match(/^#?(\d+)$/);
  if (numeric) {
    return Number(numeric[1]) === entry.search_id ? {score: 1200, exact: true} : null;
  }

  const compactQuery = compactCatalogText(query);
  if (!compactQuery) return null;
  const name = bestTextScore(entry._names, query, compactQuery, {exact: 1000, prefix: 800, contains: 560});
  const alias = bestTextScore(entry._aliases, query, compactQuery, {exact: 940, prefix: 740, contains: 520});
  let pinyinScore = 0;
  for (const value of entry._pinyin) {
    if (value === compactQuery) pinyinScore = Math.max(pinyinScore, 700);
    else if (value.startsWith(compactQuery)) pinyinScore = Math.max(pinyinScore, 620);
    else if (compactQuery.length >= 3 && value.includes(compactQuery)) pinyinScore = Math.max(pinyinScore, 420);
  }
  const score = Math.max(name.score, alias.score, pinyinScore);
  return score ? {score, exact: name.exact || alias.exact} : null;
}

function searchCatalog(kind, query, selectedIds) {
  const selected = new Set(selectedIds);
  return catalogEntries
    .filter((entry) => entry.kind === kind && !selected.has(entry.search_id))
    .map((entry) => ({entry, match: scoreCatalogEntry(entry, query)}))
    .filter(({entry, match}) => match && (entry.available_for_eden || match.exact))
    .sort((left, right) =>
      right.match.score - left.match.score
      || Number(right.entry.available_for_eden) - Number(left.entry.available_for_eden)
      || left.entry.name_zh.localeCompare(right.entry.name_zh, "zh-CN")
      || left.entry.search_id - right.entry.search_id
    )
    .slice(0, 12)
    .map(({entry}) => entry);
}

class CatalogPicker {
  constructor(valueInput, {kind, exclude = false}) {
    this.valueInput = valueInput;
    this.kind = kind;
    this.exclude = exclude;
    this.selectedIds = [];
    this.activeIndex = -1;

    this.root = document.createElement("div");
    this.root.className = `catalog-picker${exclude ? " is-exclude" : ""}`;
    this.tokens = document.createElement("div");
    this.tokens.className = "catalog-tokens";
    this.query = document.createElement("input");
    this.query.id = `${valueInput.id}-query`;
    this.query.className = "catalog-query";
    this.query.type = "text";
    this.query.placeholder = valueInput.placeholder;
    this.query.autocomplete = "off";
    this.query.spellcheck = false;
    this.query.setAttribute("role", "combobox");
    this.query.setAttribute("aria-autocomplete", "list");
    this.query.setAttribute("aria-expanded", "false");
    this.options = document.createElement("div");
    this.options.id = `${valueInput.id}-options`;
    this.options.className = "catalog-options";
    this.options.setAttribute("role", "listbox");
    this.options.hidden = true;
    this.query.setAttribute("aria-controls", this.options.id);
    this.root.append(this.tokens, this.query, this.options);

    const initialIds = valueInput.value.split(/[，,\s]+/).filter(Boolean).map(Number)
      .filter((id) => Number.isInteger(id) && (this.kind === "pill" ? id >= 0 : id > 0));
    valueInput.type = "hidden";
    valueInput.insertAdjacentElement("afterend", this.root);
    this.setIds(initialIds, false);

    this.query.addEventListener("input", () => this.renderOptions());
    this.query.addEventListener("focus", () => this.renderOptions());
    this.query.addEventListener("keydown", (event) => this.handleKeydown(event));
    this.query.addEventListener("blur", () => window.setTimeout(() => this.close(), 100));
  }

  setKind(kind, disabled) {
    this.kind = kind;
    this.query.disabled = disabled;
    this.root.classList.toggle("is-disabled", disabled);
    if (disabled) this.close();
    this.renderTokens();
  }

  setPlaceholder(value) {
    this.query.placeholder = value;
  }

  setIds(ids, notify = true) {
    this.selectedIds = [...new Set(ids.filter((id) =>
      Number.isInteger(id) && (this.kind === "pill" ? id >= 0 : id > 0)
    ))];
    this.valueInput.value = this.selectedIds.join(", ");
    this.renderTokens();
    this.renderOptions();
    if (notify) this.valueInput.dispatchEvent(new Event("input", {bubbles: true}));
  }

  remove(id) {
    this.setIds(this.selectedIds.filter((selected) => selected !== id));
    this.query.focus();
  }

  select(entry) {
    if (!entry.available_for_eden) return;
    this.setIds([...this.selectedIds, entry.search_id]);
    this.query.value = "";
    this.close();
    this.query.focus();
  }

  renderTokens() {
    const fragment = document.createDocumentFragment();
    for (const id of this.selectedIds) {
      const entry = catalogByKey.get(catalogKey(this.kind, id));
      const token = document.createElement("span");
      token.className = "catalog-token";
      const name = document.createElement("span");
      name.className = "catalog-token-name";
      name.textContent = entry ? entry.name_zh : "未知条目";
      const identifier = document.createElement("span");
      identifier.className = "catalog-token-id";
      identifier.textContent = `#${id}`;
      const icon = createItemIcon(this.kind, id);
      if (icon) token.appendChild(icon);
      if (entry && Number.isInteger(entry.quality)) {
        const quality = document.createElement("span");
        quality.className = "catalog-token-quality";
        quality.textContent = `Q${entry.quality}`;
        token.append(name, quality, identifier);
      } else {
        token.append(name, identifier);
      }
      const remove = document.createElement("button");
      remove.type = "button";
      remove.textContent = "×";
      remove.title = `移除 ${entry ? entry.name_zh : `#${id}`}`;
      remove.setAttribute("aria-label", remove.title);
      remove.addEventListener("click", (event) => {
        event.preventDefault();
        this.remove(id);
      });
      token.appendChild(remove);
      fragment.appendChild(token);
    }
    this.tokens.replaceChildren(fragment);
  }

  renderOptions() {
    const query = this.query.value.trim();
    if (this.query.disabled || !this.kind || !query) {
      this.close();
      return;
    }

    const entries = searchCatalog(this.kind, query, this.selectedIds);
    if (!entries.length) {
      const empty = document.createElement("div");
      empty.className = "catalog-empty";
      empty.textContent = "没有匹配的当前版本条目";
      this.options.replaceChildren(empty);
      this.activeIndex = -1;
      this.open();
      return;
    }

    const fragment = document.createDocumentFragment();
    entries.forEach((entry, index) => {
      const option = document.createElement("button");
      option.type = "button";
      option.id = `${this.valueInput.id}-option-${index}`;
      option.className = "catalog-option";
      option.setAttribute("role", "option");
      option.disabled = !entry.available_for_eden;
      option.setAttribute("aria-disabled", String(!entry.available_for_eden));
      const copy = document.createElement("span");
      copy.className = "catalog-option-copy";
      const name = document.createElement("strong");
      name.textContent = entry.name_zh;
      const english = document.createElement("small");
      english.textContent = entry.name_en;
      copy.append(name, english);
      const metadata = document.createElement("span");
      metadata.className = "catalog-option-meta";
      metadata.textContent = Number.isInteger(entry.quality)
        ? `Q${entry.quality} · #${entry.search_id}`
        : `#${entry.search_id}`;
      if (!entry.available_for_eden) {
        const unavailable = document.createElement("small");
        unavailable.className = "catalog-option-unavailable";
        unavailable.textContent = "当前 J460 伊甸池不可用";
        copy.appendChild(unavailable);
      }
      const icon = createItemIcon(entry.kind, entry.search_id);
      if (icon) option.append(icon, copy, metadata);
      else option.append(copy, metadata);
      option.addEventListener("pointerdown", (event) => event.preventDefault());
      option.addEventListener("click", () => this.select(entry));
      fragment.appendChild(option);
    });
    this.options.replaceChildren(fragment);
    this.activeIndex = -1;
    this.open();
  }

  open() {
    this.options.hidden = false;
    this.query.setAttribute("aria-expanded", "true");
  }

  close() {
    this.options.hidden = true;
    this.query.setAttribute("aria-expanded", "false");
    this.query.removeAttribute("aria-activedescendant");
    this.activeIndex = -1;
  }

  moveActive(direction) {
    const options = [...this.options.querySelectorAll(".catalog-option:not(:disabled)")];
    if (!options.length) return;
    this.activeIndex = this.activeIndex < 0
      ? (direction > 0 ? 0 : options.length - 1)
      : (this.activeIndex + direction + options.length) % options.length;
    options.forEach((option, index) => option.classList.toggle("is-active", index === this.activeIndex));
    const active = options[this.activeIndex];
    this.query.setAttribute("aria-activedescendant", active.id);
    active.scrollIntoView({block: "nearest"});
  }

  handleKeydown(event) {
    if (event.key === "ArrowDown" || event.key === "ArrowUp") {
      event.preventDefault();
      if (this.options.hidden) this.renderOptions();
      this.moveActive(event.key === "ArrowDown" ? 1 : -1);
    } else if (event.key === "Enter" && !this.options.hidden) {
      const options = [...this.options.querySelectorAll(".catalog-option:not(:disabled)")];
      const selectedIndex = this.activeIndex >= 0 ? this.activeIndex : 0;
      if (options[selectedIndex]) {
        event.preventDefault();
        options[selectedIndex].click();
      }
    } else if (event.key === "Escape") {
      this.close();
    } else if (event.key === "Backspace" && !this.query.value && this.selectedIds.length) {
      this.remove(this.selectedIds[this.selectedIds.length - 1]);
    }
  }
}

const baseRangeFields = [
  ["red-hearts", "红心"],
  ["soul-hearts", "魂心"],
  ["coins", "钱"],
  ["keys", "钥匙"],
  ["bombs", "炸弹"],
  ["damage", "伤害"],
  ["move-speed", "移速"],
  ["tears", "射速"],
  ["range", "射程"],
  ["shot-speed", "弹速"],
  ["luck", "幸运"],
];

const postTreatmentRangeFields = [
  ["post-damage", "黄针结算后伤害"],
  ["post-move-speed", "黄针结算后移速"],
  ["post-tears", "黄针结算后射速"],
  ["post-range", "黄针结算后射程"],
  ["post-shot-speed", "黄针结算后弹速"],
  ["post-luck", "黄针结算后幸运"],
];

const rangeFields = [
  ...baseRangeFields,
  ...(treatmentMode ? postTreatmentRangeFields : []),
];

const treatmentDirectionFields = [
  ["health", "心容器"],
  ["move-speed", "移速"],
  ["tears", "射速"],
  ["damage", "伤害"],
  ["range", "射程"],
  ["shot-speed", "弹速"],
  ["luck", "幸运"],
];

const treatmentStatModels = [
  {name: "damage", label: "伤害", base: [2.5, 4.5], post: [1.5, 5.5], delta: 1},
  {name: "move-speed", label: "移速", base: [0.85, 1.15], post: [0.65, 1.35], delta: 0.2},
  {name: "tears", label: "射速", base: [1.802826069, 3.501433905], post: [1.302826069, 4.001433905], delta: 0.5},
  {name: "range", label: "射程", base: [5, 8], post: [2.5, 10.5], delta: 2.5},
  {name: "shot-speed", label: "弹速", base: [0.75, 1.25], post: [0.6, 1.45], delta: 0.2, floor: 0.6},
  {name: "luck", label: "幸运", base: [-1, 1], post: [-2, 2], delta: 1},
];

const sortLabels = {
  seed: "种子数值",
  health: "血量",
  damage: "伤害",
  move_speed: "移速",
  tears: "射速",
  range: "射程",
  shot_speed: "弹速",
  luck: "幸运",
  active_quality: "主动道具品质",
  passive_quality: "被动道具品质",
  total_quality: "开局道具总品质",
  coins: "钱",
  keys: "钥匙",
  bombs: "炸弹",
};

const resultColumnCount = treatmentMode ? 13 : 15;

const filterInputIds = [
  "pocket-ids", "pocket-exclude-ids",
  "active-ids", "active-exclude-ids",
  ...(treatmentMode ? [] : ["passive-ids", "passive-exclude-ids"]),
  ...rangeFields.flatMap(([name]) => [`${name}-min`, `${name}-max`]),
  ...(treatmentMode
    ? treatmentDirectionFields.map(([name]) => `experimental-${name}`)
    : []),
];

function setPickerIds(inputId, ids, notify = false) {
  const picker = catalogPickers.get(inputId);
  if (picker) picker.setIds(ids, notify);
  else $(`#${inputId}`).value = ids.join(", ");
}

function mountCatalogPickers() {
  const pocketKind = ["trinket", "card", "pill"].includes($("#pocket-kind").value)
    ? $("#pocket-kind").value
    : "";
  const configurations = [
    ["pocket-ids", pocketKind, false],
    ["pocket-exclude-ids", pocketKind, true],
    ["active-ids", "active", false],
    ["active-exclude-ids", "active", true],
    ...(treatmentMode ? [] : [
      ["passive-ids", "passive", false],
      ["passive-exclude-ids", "passive", true],
    ]),
  ];
  for (const [inputId, kind, exclude] of configurations) {
    if (!catalogPickers.has(inputId)) {
      catalogPickers.set(inputId, new CatalogPicker($(`#${inputId}`), {kind, exclude}));
    }
  }
}

async function loadCatalog() {
  const catalog = await request("/catalog.json");
  if (catalog.schema_version !== 1 || !Array.isArray(catalog.entries)) {
    throw new Error("离线名称目录格式不受支持");
  }
  catalogEntries = catalog.entries.map(prepareCatalogEntry);
  catalogByKey.clear();
  catalogEntries.forEach((entry) => catalogByKey.set(catalogKey(entry.kind, entry.search_id), entry));
  mountCatalogPickers();
  updatePocketControls(false);
  const available = Object.values(catalog.counts.available_for_eden).reduce((sum, value) => sum + value, 0);
  const state = $("#catalog-state");
  state.textContent = `离线目录已就绪：${number.format(available)} 个当前伊甸可用条目`;
  state.className = "catalog-state ready";
}

function parseOptionalIds(value, label, allowZero = false) {
  if (!value.trim()) return null;
  const ids = value.split(/[，,\s]+/).filter(Boolean).map(Number);
  if (ids.some((id) => !Number.isInteger(id) || id < (allowZero ? 0 : 1) || id > 2147483647)) {
    throw new Error(`${label}必须是用逗号分隔的${allowZero ? "非负整数" : "正整数"}`);
  }
  return [...new Set(ids)];
}

function parseOptionalNumber(value, label) {
  if (!value.trim()) return null;
  const parsed = Number(value);
  if (!Number.isFinite(parsed)) throw new Error(`${label}不是有效数字`);
  return parsed;
}

function setOptionalIds(payload, key, selector, label, allowZero = false) {
  const values = parseOptionalIds($(selector).value, label, allowZero);
  if (values) payload[key] = values;
}

function updatePocketControls(clearOnKindChange = false) {
  const kind = $("#pocket-kind").value;
  const input = $("#pocket-ids");
  const excluded = $("#pocket-exclude-ids");
  const labels = {
    "": ["先选择口袋物类型", "选择类型后可按名称、俗称、拼音或 ID 搜索", "先选择类型"],
    trinket: ["饰品（任意一个）", "普通与金色饰品按基础 ID 匹配", "搜索饰品名称、俗称、拼音或 ID"],
    card: ["卡牌（任意一个）", "包含普通、特殊与逆位卡牌", "搜索卡牌名称、拼音或 ID"],
    pill: ["胶囊效果（任意一个）", "按本局颜色映射后的原始效果 ID 匹配；马胶囊使用相同基础效果", "搜索胶囊效果名称、拼音或 ID"],
    none: ["无需选择条目", "只筛选没有口袋物的开局", "没有口袋物"],
  };
  const [label, help, placeholder] = labels[kind];
  $("#pocket-ids-label").textContent = label;
  $("#pocket-ids-help").textContent = help;
  input.placeholder = placeholder;
  const nextCatalogKind = ["trinket", "card", "pill"].includes(kind) ? kind : "";
  if (clearOnKindChange && pocketCatalogKind !== null && pocketCatalogKind !== nextCatalogKind) {
    setPickerIds("pocket-ids", []);
    setPickerIds("pocket-exclude-ids", []);
  }
  pocketCatalogKind = nextCatalogKind;
  const disabled = !nextCatalogKind;
  input.disabled = disabled;
  excluded.disabled = disabled;
  for (const inputId of ["pocket-ids", "pocket-exclude-ids"]) {
    const picker = catalogPickers.get(inputId);
    if (picker) {
      picker.setKind(nextCatalogKind, disabled);
      picker.setPlaceholder(placeholder);
    }
  }
}

function selectedCriteriaLabels() {
  const labels = treatmentMode ? ["实验性疗法"] : [];
  const kind = $("#pocket-kind").value;
  const pocketNames = {none: "无口袋物", trinket: "饰品", card: "卡牌", pill: "胶囊"};
  if (kind) labels.push(pocketNames[kind]);
  else if ($("#pocket-ids").value.trim()) labels.push("口袋物");
  if ($("#active-ids").value.trim() || $("#active-exclude-ids").value.trim()) labels.push("主动");
  if (!treatmentMode
      && ($("#passive-ids").value.trim() || $("#passive-exclude-ids").value.trim())) {
    labels.push("被动");
  }
  const statCount = rangeFields.filter(([name]) =>
    $(`#${name}-min`).value.trim() || $(`#${name}-max`).value.trim()
  ).length;
  if (statCount) labels.push(`${statCount} 项数值`);
  const directionCount = treatmentMode
    ? treatmentDirectionFields.filter(([name]) => $(`#experimental-${name}`).value).length
    : 0;
  if (directionCount) labels.push(`${directionCount} 项黄针方向`);
  return labels;
}

function updateCriteriaSummary() {
  const labels = selectedCriteriaLabels();
  $("#criteria-summary").textContent = labels.length
    ? `当前：${labels.join(" + ")}`
    : "当前没有筛选条件";
}

function syncDirectionButtons() {
  if (!treatmentMode) return;
  document.querySelectorAll(".direction-field[data-direction-field]").forEach((field) => {
    const input = $(`#experimental-${field.dataset.directionField}`);
    field.querySelectorAll("button[data-value]").forEach((button) => {
      const selected = button.dataset.value === input.value;
      button.setAttribute("aria-pressed", String(selected));
    });
  });
}

function formatConstraintNumber(value) {
  return value.toFixed(3).replace(/\.?0+$/, "");
}

function treatmentBound(inputId) {
  const value = $(`#${inputId}`).value.trim();
  if (!value) return null;
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : null;
}

function treatmentPostValue(model, baseValue, direction) {
  const multiplier = direction === "up" ? 1 : (direction === "down" ? -1 : 0);
  let value = baseValue + model.delta * multiplier;
  if (model.floor !== undefined) value = Math.max(model.floor, value);
  return value;
}

function updateTreatmentFeasibility() {
  if (!treatmentMode) return [];
  const issues = [];
  const directionCounts = {up: 0, down: 0, unchanged: 0};
  treatmentDirectionFields.forEach(([name]) => {
    const value = $(`#experimental-${name}`).value;
    if (value) directionCounts[value] += 1;
  });
  const quotas = {up: 4, down: 2, unchanged: 1};
  const quotaLabels = {up: "上升", down: "下降", unchanged: "不变"};
  Object.entries(quotas).forEach(([direction, maximum]) => {
    if (directionCounts[direction] > maximum) {
      issues.push(`${quotaLabels[direction]}最多指定 ${maximum} 项，当前为 ${directionCounts[direction]} 项`);
    }
  });

  treatmentStatModels.forEach((model) => {
    const card = document.querySelector(`[data-treatment-stat="${model.name}"]`);
    const feedback = card.querySelector("[data-constraint-feedback]");
    card.classList.remove("is-impossible");
    feedback.textContent = "";

    const baseMinimumInput = treatmentBound(`${model.name}-min`);
    const baseMaximumInput = treatmentBound(`${model.name}-max`);
    const postMinimumInput = treatmentBound(`post-${model.name}-min`);
    const postMaximumInput = treatmentBound(`post-${model.name}-max`);
    const baseMinimum = Math.max(baseMinimumInput ?? model.base[0], model.base[0]);
    const baseMaximum = Math.min(baseMaximumInput ?? model.base[1], model.base[1]);
    const postMinimum = Math.max(postMinimumInput ?? model.post[0], model.post[0]);
    const postMaximum = Math.min(postMaximumInput ?? model.post[1], model.post[1]);
    let message = "";
    if (baseMinimumInput !== null && baseMaximumInput !== null
        && baseMinimumInput > baseMaximumInput) {
      message = `无解：针前${model.label}最小值大于最大值`;
    } else if (postMinimumInput !== null && postMaximumInput !== null
        && postMinimumInput > postMaximumInput) {
      message = `无解：针后${model.label}最小值大于最大值`;
    } else if (baseMinimum > baseMaximum) {
      message = `无解：针前${model.label}超出模型范围 ${formatConstraintNumber(model.base[0])}～${formatConstraintNumber(model.base[1])}`;
    } else if (postMinimum > postMaximum) {
      message = `无解：针后${model.label}超出模型范围 ${formatConstraintNumber(model.post[0])}～${formatConstraintNumber(model.post[1])}`;
    } else {
      const selected = $(`#experimental-${model.name}`).value;
      const directions = selected ? [selected] : ["up", "down", "unchanged"];
      const intervals = directions.map((direction) => {
        const left = treatmentPostValue(model, baseMinimum, direction);
        const right = treatmentPostValue(model, baseMaximum, direction);
        return [Math.min(left, right), Math.max(left, right)];
      });
      const achievableMinimum = Math.min(...intervals.map(([minimum]) => minimum));
      const achievableMaximum = Math.max(...intervals.map(([, maximum]) => maximum));
      const feasible = intervals.some(([minimum, maximum]) =>
        maximum + 1.0e-9 >= postMinimum && minimum - 1.0e-9 <= postMaximum
      );
      const baseRestricted = baseMinimumInput !== null || baseMaximumInput !== null;
      if (!feasible) {
        message = `无解：当前针前条件与方向只能得到 ${formatConstraintNumber(achievableMinimum)}～${formatConstraintNumber(achievableMaximum)}`;
      } else if (selected || baseRestricted) {
        feedback.textContent = `按当前针前条件，可得到 ${formatConstraintNumber(achievableMinimum)}～${formatConstraintNumber(achievableMaximum)}`;
      }
    }
    if (message) {
      card.classList.add("is-impossible");
      feedback.textContent = message;
      issues.push(message);
    }
  });

  const state = $("#treatment-constraint-state");
  if (issues.length) {
    state.textContent = `发现 ${issues.length} 处无解，调整后再扫描`;
    state.classList.add("is-impossible");
  } else {
    const selectedCount = directionCounts.up + directionCounts.down + directionCounts.unchanged;
    state.textContent = selectedCount
      ? `已指定：${directionCounts.up} 升 · ${directionCounts.down} 降 · ${directionCounts.unchanged} 不变`
      : "当前条件可以组合";
    state.classList.remove("is-impossible");
  }
  const startButton = $("#start-button");
  startButton.dataset.constraintBlocked = issues.length ? "true" : "false";
  startButton.disabled = issues.length > 0 || startButton.dataset.searchRunning === "true";
  return issues;
}

function initializePageLinks() {
  const withToken = (path) => {
    const url = new URL(path, window.location.origin);
    if (sessionToken) url.searchParams.set("token", sessionToken);
    return `${url.pathname}${url.search}`;
  };
  const treatmentLink = $("#treatment-page-link");
  if (treatmentLink) treatmentLink.href = withToken("/experimental-treatment.html");
  const dailyGoodLink = $("#daily-good-page-link");
  if (dailyGoodLink) dailyGoodLink.href = withToken("/daily-good.html");
  const dailyBadLink = $("#daily-bad-page-link");
  if (dailyBadLink) dailyBadLink.href = withToken("/daily-bad.html");
  const inspectorLink = $("#seed-inspector-link");
  if (inspectorLink) inspectorLink.href = withToken("/seed-inspector.html");
  const genericLink = $("#generic-page-link");
  if (genericLink) genericLink.href = withToken("/");
  document.querySelectorAll("img[data-game-icon]").forEach((image) => {
    image.addEventListener("error", () => image.classList.add("is-missing"), {once: true});
  });
}

function clearFilters() {
  $("#pocket-kind").value = "";
  filterInputIds.forEach((id) => {
    if (catalogPickers.has(id)) setPickerIds(id, []);
    else $(`#${id}`).value = "";
  });
  syncDirectionButtons();
  updatePocketControls(false);
  updateCriteriaSummary();
  updateTreatmentFeasibility();
}

function buildSearchPayload() {
  const pendingPicker = [...catalogPickers.values()].find((picker) =>
    !picker.query.disabled && picker.query.value.trim()
  );
  if (pendingPicker) {
    pendingPicker.query.focus();
    throw new Error("请先从名称推荐中选择条目，再开始扫描");
  }
  const payload = {};
  const kind = $("#pocket-kind").value;
  if (kind) payload.pocket_kind = kind;
  if (kind !== "none") {
    const pocketKey = ({trinket: "trinket_ids", card: "card_ids", pill: "pill_effect_ids"})[kind]
      || "pocket_ids";
    setOptionalIds(payload, pocketKey, "#pocket-ids", "口袋物 ID", kind === "pill");
    setOptionalIds(payload, "pocket_exclude_ids", "#pocket-exclude-ids", "排除口袋物 ID", kind === "pill");
  }
  setOptionalIds(payload, "active_ids", "#active-ids", "主动道具 ID");
  setOptionalIds(payload, "active_exclude_ids", "#active-exclude-ids", "排除主动道具 ID");
  if (treatmentMode) {
    payload.passive_ids = [240];
  } else {
    setOptionalIds(payload, "passive_ids", "#passive-ids", "被动道具 ID");
    setOptionalIds(payload, "passive_exclude_ids", "#passive-exclude-ids", "排除被动道具 ID");
  }

  rangeFields.forEach(([name, label]) => {
    const minimum = parseOptionalNumber($(`#${name}-min`).value, `${label}最小值`);
    const maximum = parseOptionalNumber($(`#${name}-max`).value, `${label}最大值`);
    if (minimum !== null && maximum !== null && minimum > maximum) {
      throw new Error(`${label}的最小值不能大于最大值`);
    }
    if (minimum !== null) payload[`${name.replaceAll("-", "_")}_min`] = minimum;
    if (maximum !== null) payload[`${name.replaceAll("-", "_")}_max`] = maximum;
  });

  if (treatmentMode) {
    treatmentDirectionFields.forEach(([name]) => {
      const value = $(`#experimental-${name}`).value;
      if (value) payload[`experimental_${name.replaceAll("-", "_")}`] = value;
    });
    const treatmentIssues = updateTreatmentFeasibility();
    if (treatmentIssues.length) throw new Error(treatmentIssues[0]);
  }

  const integerRangeLimits = {
    "red-hearts": [0, 3],
    "soul-hearts": [0, 3],
    coins: [0, 5],
    keys: [0, 1],
    bombs: [0, 2],
  };
  for (const [name, [minimum, maximum]] of Object.entries(integerRangeLimits)) {
    for (const bound of ["min", "max"]) {
      const input = $(`#${name}-${bound}`);
      if (input.value.trim()) {
        const value = Number(input.value);
        if (!Number.isInteger(value) || value < minimum || value > maximum) {
          const label = rangeFields.find(([field]) => field === name)[1];
          throw new Error(`${label}必须填写 ${minimum}～${maximum} 的整数`);
        }
      }
    }
  }

  const criteriaKeys = Object.keys(payload);
  if (!criteriaKeys.length) throw new Error("请至少设置一个筛选条件");

  payload.sort_key = currentSortKey;
  payload.sort_direction = currentSortDirection;
  payload.start = Number($("#range-start").value);
  payload.end = Number($("#range-end").value);
  payload.threads = Number($("#threads").value);
  payload.max_results = Number($("#max-results").value);
  if (![payload.start, payload.end, payload.threads, payload.max_results].every(Number.isInteger)) {
    throw new Error("扫描范围、线程和结果上限必须是整数");
  }
  if (payload.start < 1 || payload.end > 4294967295 || payload.start > payload.end) {
    throw new Error("扫描范围必须位于 1～4294967295，且起点不能大于终点");
  }
  if (payload.threads < 0 || payload.threads > 64) throw new Error("线程必须位于 0～64");
  if (payload.max_results < 1 || payload.max_results > 10000) {
    throw new Error("最多保留结果必须位于 1～10000");
  }
  return payload;
}

async function request(path, options) {
  const config = {...(options || {})};
  config.headers = {...(config.headers || {})};
  if ((config.method || "GET").toUpperCase() === "POST") {
    config.headers["X-Isaac-Token"] = sessionToken;
  }
  const response = await fetch(path, config);
  const payload = await response.json();
  if (!response.ok) throw new Error(payload.error || `请求失败：${response.status}`);
  return payload;
}

async function loadProfile() {
  const profile = await request("/api/v1/profile");
  document.body.classList.toggle("has-local-art", profile.local_game_icons);
  $("#profile-name").textContent = `${profile.game_build} · 全解锁`;
  $("#profile-detail").textContent = profile.local_game_icons
    ? `${profile.game_version} · 已读取游戏图标`
    : profile.game_version;
}

function stateLabel(state) {
  return ({idle: "等待开始", running: "正在扫描", completed: "扫描完成", cancelled: "已停止", failed: "搜索失败"})[state] || state;
}

function namedId(kind, id, fallback) {
  const entry = catalogByKey.get(catalogKey(kind, id));
  return entry ? `${entry.name_zh} · #${id}` : `${fallback} #${id}`;
}

function pocketLabel(match) {
  const pillColor = Number(match.pill_color || 0);
  const basePillColor = pillColor & 2047;
  const horseSuffix = (pillColor & 2048) !== 0 ? " · 马胶囊" : "";
  const pillLabel = basePillColor === 14
    ? `金色胶囊${horseSuffix}`
    : `${namedId("pill", match.pocket_id, "胶囊")} · 颜色 #${basePillColor}${horseSuffix}`;
  return ({
    none: "无",
    trinket: namedId("trinket", match.pocket_id, "饰品"),
    card: namedId("card", match.pocket_id, "卡牌"),
    pill: pillLabel,
  })[match.pocket_kind] || match.pocket_kind;
}

function formatStat(value) {
  return Number(value).toFixed(4);
}

function appendCell(row, value, className) {
  const cell = document.createElement("td");
  cell.textContent = value;
  if (className) cell.className = className;
  row.appendChild(cell);
  return cell;
}

function appendStatCell(row, value) {
  return appendCell(row, formatStat(value));
}

function appendNamedItemCell(row, kind, id, metadata, fallback) {
  const entry = catalogByKey.get(catalogKey(kind, id));
  const cell = document.createElement("td");
  cell.className = "id-value";
  const wrapper = document.createElement("div");
  wrapper.className = "item-cell";
  const icon = createItemIcon(kind, id);
  if (icon) wrapper.appendChild(icon);
  const copy = document.createElement("span");
  copy.className = "item-cell-copy";
  const name = document.createElement("strong");
  name.textContent = entry ? entry.name_zh : fallback;
  const detail = document.createElement("small");
  detail.textContent = metadata;
  copy.append(name, detail);
  wrapper.appendChild(copy);
  cell.appendChild(wrapper);
  row.appendChild(cell);
  return cell;
}

function appendPocketCell(row, match) {
  if (match.pocket_kind === "trinket") {
    return appendNamedItemCell(row, "trinket", match.pocket_id, `#${match.pocket_id}`, "饰品");
  }
  return appendCell(row, pocketLabel(match), "id-value");
}

function treatmentDirections(match) {
  const up = [];
  const down = [];
  const unchanged = [];
  treatmentDirectionFields.forEach(([, label], index) => {
    const bit = 1 << index;
    if ((match.experimental_treatment_up_mask & bit) !== 0) up.push(label);
    else if ((match.experimental_treatment_down_mask & bit) !== 0) down.push(label);
    else unchanged.push(label);
  });
  return {up, down, unchanged};
}

function appendDirectionCell(row, values, kind) {
  const cell = document.createElement("td");
  cell.className = `direction-result direction-result-${kind}`;
  if (!values.length) {
    cell.textContent = "—";
  } else {
    values.forEach((value) => {
      const badge = document.createElement("span");
      badge.textContent = value;
      cell.appendChild(badge);
    });
  }
  row.appendChild(cell);
  return cell;
}

function appendTransitionStatCell(row, before, after) {
  const cell = document.createElement("td");
  cell.className = "transition-stat";
  const wrapper = document.createElement("span");
  wrapper.className = "transition-stat-copy";
  const beforeCopy = document.createElement("small");
  beforeCopy.textContent = formatStat(before);
  const arrow = document.createElement("strong");
  const difference = Number(after) - Number(before);
  const direction = difference > 1.0e-9 ? "up" : (difference < -1.0e-9 ? "down" : "same");
  arrow.className = `transition-${direction}`;
  arrow.textContent = direction === "up" ? "↑" : (direction === "down" ? "↓" : "＝");
  const afterCopy = document.createElement("b");
  afterCopy.textContent = formatStat(after);
  wrapper.append(beforeCopy, arrow, afterCopy);
  cell.appendChild(wrapper);
  row.appendChild(cell);
  return cell;
}

function appendSeedCell(row, match) {
  const seedCell = document.createElement("td");
  seedCell.className = "seed-cell";
  const seed = document.createElement("strong");
  seed.textContent = match.seed;
  const raw = document.createElement("small");
  raw.textContent = number.format(match.seed_u32);
  seedCell.append(seed, raw);
  row.appendChild(seedCell);
}

function compareAscending(left, right) {
  if (left === right) return 0;
  return left < right ? -1 : 1;
}

function compareDirected(left, right, direction) {
  const result = compareAscending(left, right);
  return direction === "asc" ? result : -result;
}

function compareMatches(left, right, key, direction) {
  let compared = 0;
  if (key === "health") {
    compared = compareDirected(left.red_hearts, right.red_hearts, direction)
      || compareDirected(left.soul_hearts, right.soul_hearts, direction);
  } else if (key === "active_quality") {
    compared = compareDirected(left.active_quality, right.active_quality, direction)
      || compareAscending(left.active_id, right.active_id);
  } else if (key === "passive_quality") {
    compared = compareDirected(left.passive_quality, right.passive_quality, direction)
      || compareAscending(left.passive_id, right.passive_id);
  } else if (key === "total_quality") {
    compared = compareDirected(left.total_quality, right.total_quality, direction)
      || compareAscending(left.active_id, right.active_id)
      || compareAscending(left.passive_id, right.passive_id);
  } else {
    const field = key === "seed" ? "seed_u32" : key;
    compared = compareDirected(left[field], right[field], direction);
  }
  return compared || compareAscending(left.seed_u32, right.seed_u32);
}

function currentOrderLabel() {
  const direction = currentSortDirection === "asc" ? "从低到高" : "从高到低";
  return `${sortLabels[currentSortKey] || currentSortKey}${direction}`;
}

function backendOrderIsCurrent() {
  return currentResult
    && currentResult.sort_key === currentSortKey
    && currentResult.sort_direction === currentSortDirection;
}

function updateSortHeaders() {
  document.querySelectorAll("th[data-sort-key]").forEach((header) => {
    const selected = header.dataset.sortKey === currentSortKey;
    header.setAttribute(
      "aria-sort",
      selected ? (currentSortDirection === "asc" ? "ascending" : "descending") : "none",
    );
    header.querySelector(".sort-indicator").textContent = selected
      ? (currentSortDirection === "asc" ? "↑" : "↓")
      : "↕";
  });
  $("#view-sort-status").textContent = `${currentOrderLabel()} ${currentSortDirection === "asc" ? "↑" : "↓"}`;
}

function createGenericResultRow(match) {
  const row = document.createElement("tr");
  appendSeedCell(row, match);
  appendPocketCell(row, match);
  appendNamedItemCell(row, "active", match.active_id, `Q${match.active_quality} · #${match.active_id}`, "主动");
  appendNamedItemCell(row, "passive", match.passive_id, `Q${match.passive_quality} · #${match.passive_id}`, "被动");
  appendCell(row, `Q${match.total_quality}`, "quality-value");
  appendCell(row, `${match.red_hearts} 红 / ${match.soul_hearts} 魂`);
  appendCell(row, match.coins);
  appendCell(row, match.keys);
  appendCell(row, match.bombs);
  appendStatCell(row, match.damage);
  appendStatCell(row, match.move_speed);
  appendStatCell(row, match.tears);
  appendStatCell(row, match.range);
  appendStatCell(row, match.shot_speed);
  appendStatCell(row, match.luck);
  return row;
}

function createTreatmentResultRow(match) {
  const row = document.createElement("tr");
  appendSeedCell(row, match);
  appendPocketCell(row, match);
  appendNamedItemCell(row, "active", match.active_id, `Q${match.active_quality} · #${match.active_id}`, "主动");
  const directions = treatmentDirections(match);
  appendDirectionCell(row, directions.up, "up");
  appendDirectionCell(row, directions.down, "down");
  appendDirectionCell(row, directions.unchanged, "same");
  appendCell(row, `${match.coins}¢ · ${match.keys}钥 · ${match.bombs}弹`, "resource-result");
  appendTransitionStatCell(row, match.damage, match.post_damage);
  appendTransitionStatCell(row, match.move_speed, match.post_move_speed);
  appendTransitionStatCell(row, match.tears, match.post_tears);
  appendTransitionStatCell(row, match.range, match.post_range);
  appendTransitionStatCell(row, match.shot_speed, match.post_shot_speed);
  appendTransitionStatCell(row, match.luck, match.post_luck);
  return row;
}

function createResultRow(match) {
  return treatmentMode ? createTreatmentResultRow(match) : createGenericResultRow(match);
}

function renderResults() {
  updateSortHeaders();
  const matches = currentResult?.matches || [];
  const pageSize = Number($("#page-size").value);
  const pageCount = matches.length ? Math.ceil(matches.length / pageSize) : 0;
  currentPage = pageCount ? Math.max(1, Math.min(currentPage, pageCount)) : 1;
  const firstIndex = (currentPage - 1) * pageSize;
  const pageMatches = matches.slice(firstIndex, firstIndex + pageSize);
  const body = $("#results-body");
  if (!currentResult) {
    body.innerHTML = `<tr><td colspan="${resultColumnCount}" class="empty">还没有搜索结果</td></tr>`;
  } else if (!matches.length) {
    body.innerHTML = `<tr><td colspan="${resultColumnCount}" class="empty">没有命中当前条件</td></tr>`;
  } else {
    body.replaceChildren(...pageMatches.map(createResultRow));
  }

  $("#page-status").textContent = pageCount
    ? `第 ${number.format(currentPage)} / ${number.format(pageCount)} 页 · ${number.format(firstIndex + 1)}–${number.format(firstIndex + pageMatches.length)} / ${number.format(matches.length)}`
    : "第 0 / 0 页";
  $("#previous-page").disabled = pageCount === 0 || currentPage <= 1;
  $("#next-page").disabled = pageCount === 0 || currentPage >= pageCount;

  if (!currentResult) {
    $("#results-summary").textContent = "完成搜索后在这里显示结果。";
    $("#view-sort-detail").textContent = treatmentMode
      ? "黄针前后属性会并排显示，表格只渲染当前页。"
      : "结果会保存在浏览器内存中，表格只渲染当前页。";
    $("#global-resort-button").hidden = true;
    $("#download-link").classList.add("disabled");
    return;
  }

  const order = currentOrderLabel();
  const exactGlobalOrder = backendOrderIsCurrent();
  if (!currentResult.truncated) {
    $("#results-summary").textContent = `共命中并载入 ${number.format(currentResult.total_count)} 条，已按${order}排序。`;
    $("#view-sort-detail").textContent = treatmentMode
      ? "当前内存中包含全部命中；每项面板显示黄针结算前 → 结算后。"
      : "当前内存中包含全部命中，点击任意列都会立即进行精确全量排序。";
  } else if (exactGlobalOrder) {
    $("#results-summary").textContent = `总命中 ${number.format(currentResult.total_count)} 条，已载入按${order}选出的全局前 ${number.format(matches.length)} 条。`;
    $("#view-sort-detail").textContent = "当前载入的是完整扫描得到的全局 Top‑N；点击其他列会先即时重排这批结果。";
  } else {
    $("#results-summary").textContent = `总命中 ${number.format(currentResult.total_count)} 条；当前按${order}重排已载入的 ${number.format(matches.length)} 条。`;
    $("#view-sort-detail").textContent = "当前表头排序只作用于已载入结果；要取得此顺序的全局 Top‑N，请重新扫描。";
  }
  $("#global-resort-button").hidden = !currentResult.truncated || exactGlobalOrder;
  $("#download-link").classList.toggle("disabled", !matches.length);
}

function sortCurrentResults() {
  if (currentResult?.matches.length) {
    currentResult.matches.sort((left, right) =>
      compareMatches(left, right, currentSortKey, currentSortDirection)
    );
  }
  currentPage = 1;
  renderResults();
  $(".table-wrap").scrollTop = 0;
}

function resultText(matches) {
  const header = [
    "seed", "seed_u32", "pocket_kind", "pocket_id", "pill_color", "active_id", "passive_id",
    "active_quality", "passive_quality", "total_quality", "red_hearts", "soul_hearts",
    "coins", "keys", "bombs",
    "damage", "move_speed", "tears", "range", "shot_speed", "luck",
    "damage_delta", "move_speed_delta", "tears_delta", "shot_speed_delta", "luck_delta",
    "post_item_stats_available", "experimental_treatment_up_mask", "experimental_treatment_down_mask",
    "post_damage", "post_move_speed", "post_tears", "post_range", "post_shot_speed", "post_luck",
  ];
  const lines = matches.map((match) => header.map((field) => match[field]).join("\t"));
  return `${header.join("\t")}\r\n${lines.join("\r\n")}\r\n`;
}

async function updateStatus() {
  try {
    const status = await request("/api/v1/search/status");
    const percent = Math.max(0, Math.min(100, status.progress || 0));
    $("#state-label").textContent = stateLabel(status.state);
    $("#percent-label").textContent = `${percent.toFixed(2)}%`;
    $("#progress-bar").style.width = `${percent}%`;
    $("#scanned-value").textContent = `${number.format(status.scanned)} / ${number.format(status.total)}`;
    $("#matches-value").textContent = number.format(status.matches);
    $("#elapsed-value").textContent = `${status.elapsed_seconds.toFixed(1)} 秒`;
    const speed = status.elapsed_seconds > 0 ? status.scanned / status.elapsed_seconds : 0;
    $("#speed-value").textContent = speed ? `${number.format(Math.round(speed))}/秒` : "—";
    $("#start-button").dataset.searchRunning = String(status.state === "running");
    $("#start-button").disabled = status.state === "running"
      || $("#start-button").dataset.constraintBlocked === "true";
    $("#cancel-button").disabled = status.state !== "running";
    $("#error-message").hidden = !status.error;
    $("#error-message").textContent = status.error || "";

    if (status.state === "running" && !pollTimer) pollTimer = setInterval(updateStatus, 250);
    if (["completed", "cancelled", "failed"].includes(status.state)) {
      clearInterval(pollTimer);
      pollTimer = null;
      if (status.state !== "failed") await loadResults();
    }
  } catch (error) {
    $("#error-message").hidden = false;
    $("#error-message").textContent = error.message;
  }
}

async function loadResults() {
  const result = await request("/api/v1/search/results");
  currentResult = result;
  currentSortKey = result.sort_key;
  currentSortDirection = result.sort_direction;
  sortCurrentResults();
}

async function beginSearch(payload) {
  lastSearchPayload = JSON.parse(JSON.stringify(payload));
  await request("/api/v1/search", {
    method: "POST",
    headers: {"Content-Type": "application/json"},
    body: JSON.stringify(payload),
  });
  currentResult = null;
  currentPage = 1;
  renderResults();
  $("#results-body").innerHTML = `<tr><td colspan="${resultColumnCount}" class="empty">扫描进行中…</td></tr>`;
  $("#results-summary").textContent = "正在计算候选种子。";
  $("#view-sort-detail").textContent = `扫描完成后将按${currentOrderLabel()}载入结果。`;
  $("#download-link").classList.add("disabled");
  $("#error-message").hidden = true;
  if (pollTimer) clearInterval(pollTimer);
  pollTimer = setInterval(updateStatus, 250);
  await updateStatus();
}

$("#search-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  try {
    const payload = buildSearchPayload();
    await beginSearch(payload);
  } catch (error) {
    $("#error-message").hidden = false;
    $("#error-message").textContent = error.message;
    $("#error-message").scrollIntoView({behavior: "smooth", block: "nearest"});
  }
});

$("#pocket-kind").addEventListener("change", () => {
  updatePocketControls(true);
  updateCriteriaSummary();
});

$("#search-form").addEventListener("input", () => {
  updateCriteriaSummary();
  updateTreatmentFeasibility();
});

if (treatmentMode) {
  document.querySelectorAll(".direction-field[data-direction-field]").forEach((field) => {
    const input = $(`#experimental-${field.dataset.directionField}`);
    field.querySelectorAll("button[data-value]").forEach((button) => {
      button.addEventListener("click", () => {
        input.value = button.dataset.value;
        syncDirectionButtons();
        input.dispatchEvent(new Event("input", {bubbles: true}));
      });
    });
  });
}

document.querySelectorAll("th[data-sort-key]").forEach((header) => {
  header.querySelector(".table-sort").addEventListener("click", () => {
    if (header.dataset.sortKey === currentSortKey) {
      currentSortDirection = currentSortDirection === "asc" ? "desc" : "asc";
    } else {
      currentSortKey = header.dataset.sortKey;
      currentSortDirection = "asc";
    }
    sortCurrentResults();
  });
});

$("#page-size").addEventListener("change", () => {
  currentPage = 1;
  renderResults();
  $(".table-wrap").scrollTop = 0;
});

$("#previous-page").addEventListener("click", () => {
  currentPage -= 1;
  renderResults();
  $(".table-wrap").scrollTop = 0;
});

$("#next-page").addEventListener("click", () => {
  currentPage += 1;
  renderResults();
  $(".table-wrap").scrollTop = 0;
});

$("#global-resort-button").addEventListener("click", async () => {
  try {
    if (!lastSearchPayload) throw new Error("没有可重新扫描的搜索条件");
    const limit = Number($("#max-results").value);
    if (!Number.isInteger(limit) || limit < 1 || limit > 10000) {
      throw new Error("内存载入上限必须位于 1～10000");
    }
    await beginSearch({
      ...lastSearchPayload,
      sort_key: currentSortKey,
      sort_direction: currentSortDirection,
      max_results: limit,
    });
  } catch (error) {
    $("#error-message").hidden = false;
    $("#error-message").textContent = error.message;
    $("#error-message").scrollIntoView({behavior: "smooth", block: "nearest"});
  }
});

$("#download-link").addEventListener("click", (event) => {
  event.preventDefault();
  if (!currentResult?.matches.length) return;
  const blob = new Blob([resultText(currentResult.matches)], {type: "text/tab-separated-values;charset=utf-8"});
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = treatmentMode ? "experimental-treatment-seeds.txt" : "isaac-seeds.txt";
  document.body.appendChild(anchor);
  anchor.click();
  anchor.remove();
  setTimeout(() => URL.revokeObjectURL(url), 0);
});

$("#clear-filters").addEventListener("click", clearFilters);
$("#cancel-button").addEventListener("click", () => request("/api/v1/search/cancel", {method: "POST"}));
$("#shutdown-button").addEventListener("click", async () => {
  await request("/api/v1/shutdown", {method: "POST"});
  document.body.innerHTML = '<main class="shell"><section class="panel"><h2>本地程序已关闭</h2><p>现在可以关闭这个页面。</p></section></main>';
});

document.addEventListener("pointerdown", (event) => {
  catalogPickers.forEach((picker) => {
    if (!picker.root.contains(event.target)) picker.close();
  });
});

updatePocketControls(false);
initializePageLinks();
syncDirectionButtons();
updateCriteriaSummary();
updateTreatmentFeasibility();
renderResults();
initializeBasementBackdrop().catch(() => {
  document.body.classList.add("backdrop-fallback");
});
loadCatalog().catch((error) => {
  const state = $("#catalog-state");
  state.textContent = `名称目录载入失败，仍可直接输入 ID：${error.message}`;
  state.className = "catalog-state failed";
});
loadProfile().catch((error) => {
  $("#profile-name").textContent = "Profile 读取失败";
  $("#profile-detail").textContent = error.message;
});
updateStatus();
