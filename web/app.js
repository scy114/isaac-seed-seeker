const $ = (selector) => document.querySelector(selector);
const number = new Intl.NumberFormat("zh-CN");
const sessionToken = new URLSearchParams(window.location.search).get("token") || "";
let pollTimer = null;
let pocketCatalogKind = null;
let catalogEntries = [];
const catalogByKey = new Map();
const catalogPickers = new Map();

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
      .filter((id) => Number.isInteger(id) && id > 0);
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
    this.selectedIds = [...new Set(ids.filter((id) => Number.isInteger(id) && id > 0))];
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
      option.append(copy, metadata);
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

const rangeFields = [
  ["red-hearts", "红心"],
  ["soul-hearts", "魂心"],
  ["damage", "伤害"],
  ["move-speed", "移速"],
  ["tears", "射速"],
  ["range", "射程"],
  ["shot-speed", "弹速"],
  ["luck", "幸运"],
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
};

const filterInputIds = [
  "pocket-ids", "pocket-exclude-ids",
  "active-ids", "active-exclude-ids",
  "passive-ids", "passive-exclude-ids",
  ...rangeFields.flatMap(([name]) => [`${name}-min`, `${name}-max`]),
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
    ["passive-ids", "passive", false],
    ["passive-exclude-ids", "passive", true],
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

function parseOptionalIds(value, label) {
  if (!value.trim()) return null;
  const ids = value.split(/[，,\s]+/).filter(Boolean).map(Number);
  if (ids.some((id) => !Number.isInteger(id) || id <= 0 || id > 2147483647)) {
    throw new Error(`${label}必须是用逗号分隔的正整数`);
  }
  return [...new Set(ids)];
}

function parseOptionalNumber(value, label) {
  if (!value.trim()) return null;
  const parsed = Number(value);
  if (!Number.isFinite(parsed)) throw new Error(`${label}不是有效数字`);
  return parsed;
}

function setOptionalIds(payload, key, selector, label) {
  const values = parseOptionalIds($(selector).value, label);
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
    pill: ["胶囊效果（任意一个）", "普通和大胶囊按各自效果 ID 匹配", "搜索胶囊效果名称、拼音或 ID"],
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
  const labels = [];
  const kind = $("#pocket-kind").value;
  const pocketNames = {none: "无口袋物", trinket: "饰品", card: "卡牌", pill: "胶囊"};
  if (kind) labels.push(pocketNames[kind]);
  else if ($("#pocket-ids").value.trim()) labels.push("口袋物");
  if ($("#active-ids").value.trim() || $("#active-exclude-ids").value.trim()) labels.push("主动");
  if ($("#passive-ids").value.trim() || $("#passive-exclude-ids").value.trim()) labels.push("被动");
  const statCount = rangeFields.filter(([name]) =>
    $(`#${name}-min`).value.trim() || $(`#${name}-max`).value.trim()
  ).length;
  if (statCount) labels.push(`${statCount} 项属性`);
  return labels;
}

function updateCriteriaSummary() {
  const labels = selectedCriteriaLabels();
  $("#criteria-summary").textContent = labels.length
    ? `当前：${labels.join(" + ")}`
    : "当前没有筛选条件";
}

function updateSortHelp() {
  const key = $("#sort-key").value;
  const direction = $("#sort-direction").value;
  const limit = Number($("#max-results").value);
  const directionLabel = direction === "asc" ? "从低到高" : "从高到低";
  let detail = "";
  if (key === "health") detail = "，红心相同再比较魂心";
  if (key === "active_quality") detail = "，品质相同按主动道具 ID 从低到高";
  if (key === "passive_quality") detail = "，品质相同按被动道具 ID 从低到高";
  if (key === "total_quality") detail = "，总品质相同先按主动 ID、再按被动 ID 从低到高";
  const shownLimit = Number.isInteger(limit) && limit > 0 ? number.format(limit) : "N";
  $("#sort-help").textContent = `完整扫描；按${sortLabels[key]}${directionLabel}${detail}，只保留最优的 ${shownLimit} 条。`;
}

function applyTargetPreset() {
  $("#pocket-kind").value = "trinket";
  updatePocketControls(false);
  setPickerIds("pocket-ids", [169]);
  setPickerIds("active-ids", [145, 133]);
  setPickerIds("passive-ids", [81, 134, 187, 212, 665]);
  ["pocket-exclude-ids", "active-exclude-ids", "passive-exclude-ids",
    ...rangeFields.flatMap(([name]) => [`${name}-min`, `${name}-max`])]
    .forEach((id) => {
      if (catalogPickers.has(id)) setPickerIds(id, []);
      else $(`#${id}`).value = "";
    });
  updateCriteriaSummary();
  $("#preset-target").classList.add("active");
}

function clearFilters() {
  $("#pocket-kind").value = "";
  filterInputIds.forEach((id) => {
    if (catalogPickers.has(id)) setPickerIds(id, []);
    else $(`#${id}`).value = "";
  });
  updatePocketControls(false);
  updateCriteriaSummary();
  $("#preset-target").classList.remove("active");
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
    setOptionalIds(payload, pocketKey, "#pocket-ids", "口袋物 ID");
    setOptionalIds(payload, "pocket_exclude_ids", "#pocket-exclude-ids", "排除口袋物 ID");
  }
  setOptionalIds(payload, "active_ids", "#active-ids", "主动道具 ID");
  setOptionalIds(payload, "active_exclude_ids", "#active-exclude-ids", "排除主动道具 ID");
  setOptionalIds(payload, "passive_ids", "#passive-ids", "被动道具 ID");
  setOptionalIds(payload, "passive_exclude_ids", "#passive-exclude-ids", "排除被动道具 ID");

  rangeFields.forEach(([name, label]) => {
    const minimum = parseOptionalNumber($(`#${name}-min`).value, `${label}最小值`);
    const maximum = parseOptionalNumber($(`#${name}-max`).value, `${label}最大值`);
    if (minimum !== null && maximum !== null && minimum > maximum) {
      throw new Error(`${label}的最小值不能大于最大值`);
    }
    if (minimum !== null) payload[`${name.replaceAll("-", "_")}_min`] = minimum;
    if (maximum !== null) payload[`${name.replaceAll("-", "_")}_max`] = maximum;
  });

  for (const name of ["red-hearts", "soul-hearts"]) {
    for (const bound of ["min", "max"]) {
      const input = $(`#${name}-${bound}`);
      if (input.value.trim() && !Number.isInteger(Number(input.value))) {
        throw new Error("红心和魂心必须填写整数颗数");
      }
    }
  }

  const criteriaKeys = Object.keys(payload);
  if (!criteriaKeys.length) throw new Error("请至少设置一个筛选条件");

  payload.sort_key = $("#sort-key").value;
  payload.sort_direction = $("#sort-direction").value;
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
  $("#profile-name").textContent = `${profile.game_build} · 全解锁`;
  $("#profile-detail").textContent = profile.game_version;
}

function stateLabel(state) {
  return ({idle: "等待开始", running: "正在扫描", completed: "扫描完成", cancelled: "已停止", failed: "搜索失败"})[state] || state;
}

function namedId(kind, id, fallback) {
  const entry = catalogByKey.get(catalogKey(kind, id));
  return entry ? `${entry.name_zh} · #${id}` : `${fallback} #${id}`;
}

function namedQualityItem(kind, id, quality, fallback) {
  const entry = catalogByKey.get(catalogKey(kind, id));
  const name = entry ? entry.name_zh : fallback;
  return `${name} · Q${quality} · #${id}`;
}

function pocketLabel(match) {
  return ({
    none: "无",
    trinket: namedId("trinket", match.pocket_id, "饰品"),
    card: namedId("card", match.pocket_id, "卡牌"),
    pill: namedId("pill", match.pocket_id, "胶囊"),
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
    $("#start-button").disabled = status.state === "running";
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
  const body = $("#results-body");
  if (!result.matches.length) {
    body.innerHTML = '<tr><td colspan="12" class="empty">没有命中当前条件</td></tr>';
  } else {
    body.replaceChildren(...result.matches.map((match) => {
      const row = document.createElement("tr");
      const seedCell = document.createElement("td");
      seedCell.className = "seed-cell";
      const seed = document.createElement("strong");
      seed.textContent = match.seed;
      const raw = document.createElement("small");
      raw.textContent = number.format(match.seed_u32);
      seedCell.append(seed, raw);
      row.appendChild(seedCell);
      appendCell(row, pocketLabel(match), "id-value");
      appendCell(row, namedQualityItem("active", match.active_id, match.active_quality, "主动"), "id-value");
      appendCell(row, namedQualityItem("passive", match.passive_id, match.passive_quality, "被动"), "id-value");
      appendCell(row, `Q${match.total_quality}`, "quality-value");
      appendCell(row, `${match.red_hearts} 红 / ${match.soul_hearts} 魂`);
      appendStatCell(row, match.damage);
      appendStatCell(row, match.move_speed);
      appendStatCell(row, match.tears);
      appendStatCell(row, match.range);
      appendStatCell(row, match.shot_speed);
      appendStatCell(row, match.luck);
      return row;
    }));
  }
  const order = `${sortLabels[result.sort_key] || result.sort_key}${result.sort_direction === "desc" ? "从高到低" : "从低到高"}`;
  $("#results-summary").textContent = result.truncated
    ? `总命中 ${number.format(result.total_count)} 条，按${order}保留最优的 ${number.format(result.count)} 条。`
    : `共命中 ${number.format(result.total_count)} 条，已按${order}排序。`;
  $("#download-link").classList.toggle("disabled", !result.matches.length);
}

$("#search-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  try {
    const payload = buildSearchPayload();
    await request("/api/v1/search", {
      method: "POST",
      headers: {"Content-Type": "application/json"},
      body: JSON.stringify(payload),
    });
    $("#results-body").innerHTML = '<tr><td colspan="12" class="empty">扫描进行中…</td></tr>';
    $("#results-summary").textContent = "正在计算候选种子。";
    $("#download-link").classList.add("disabled");
    $("#error-message").hidden = true;
    if (pollTimer) clearInterval(pollTimer);
    pollTimer = setInterval(updateStatus, 250);
    await updateStatus();
  } catch (error) {
    $("#error-message").hidden = false;
    $("#error-message").textContent = error.message;
    $("#error-message").scrollIntoView({behavior: "smooth", block: "nearest"});
  }
});

$("#pocket-kind").addEventListener("change", () => {
  updatePocketControls(true);
  updateCriteriaSummary();
  $("#preset-target").classList.remove("active");
});

$("#search-form").addEventListener("input", (event) => {
  if (!event.target.closest(".search-settings")) $("#preset-target").classList.remove("active");
  updateCriteriaSummary();
  updateSortHelp();
});

$("#preset-target").addEventListener("click", applyTargetPreset);
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
updateCriteriaSummary();
updateSortHelp();
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
