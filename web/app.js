const $ = (selector) => document.querySelector(selector);
const number = new Intl.NumberFormat("zh-CN");
const sessionToken = new URLSearchParams(window.location.search).get("token") || "";
let pollTimer = null;

const rangeFields = [
  ["red-hearts", "红心"],
  ["soul-hearts", "魂心"],
  ["damage-delta", "伤害"],
  ["move-speed-delta", "移速"],
  ["tears-delta", "射速"],
  ["range", "射程"],
  ["shot-speed-delta", "弹速"],
  ["luck-delta", "幸运"],
];

const filterInputIds = [
  "pocket-ids", "pocket-exclude-ids",
  "active-ids", "active-exclude-ids",
  "passive-ids", "passive-exclude-ids",
  ...rangeFields.flatMap(([name]) => [`${name}-min`, `${name}-max`]),
];

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

function updatePocketControls() {
  const kind = $("#pocket-kind").value;
  const input = $("#pocket-ids");
  const excluded = $("#pocket-exclude-ids");
  const labels = {
    "": ["口袋物 ID（任意一个）", "建议先选择类型，避免卡牌与饰品同 ID 的歧义", "例如：169"],
    trinket: ["饰品 ID（任意一个）", "普通与金色饰品按基础 ID 匹配", "例如：169"],
    card: ["卡牌 ID（任意一个）", "包含普通、特殊与逆位卡牌", "例如：2"],
    pill: ["胶囊效果 ID（任意一个）", "填写效果 ID，不是胶囊颜色 ID", "例如：12"],
    none: ["无需填写 ID", "只筛选没有口袋物的开局", ""],
  };
  const [label, help, placeholder] = labels[kind];
  $("#pocket-ids-label").textContent = label;
  $("#pocket-ids-help").textContent = help;
  input.placeholder = placeholder;
  input.disabled = kind === "none";
  excluded.disabled = kind === "none";
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

function applyTargetPreset() {
  $("#pocket-kind").value = "trinket";
  $("#pocket-ids").value = "169";
  $("#active-ids").value = "145, 133";
  $("#passive-ids").value = "81, 134, 187, 212, 665";
  ["pocket-exclude-ids", "active-exclude-ids", "passive-exclude-ids",
    ...rangeFields.flatMap(([name]) => [`${name}-min`, `${name}-max`])]
    .forEach((id) => { $(`#${id}`).value = ""; });
  updatePocketControls();
  updateCriteriaSummary();
  $("#preset-target").classList.add("active");
}

function clearFilters() {
  $("#pocket-kind").value = "";
  filterInputIds.forEach((id) => { $(`#${id}`).value = ""; });
  updatePocketControls();
  updateCriteriaSummary();
  $("#preset-target").classList.remove("active");
}

function buildSearchPayload() {
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
  if (payload.max_results < 1 || payload.max_results > 100000) {
    throw new Error("最多显示结果必须位于 1～100000");
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

function pocketLabel(match) {
  return ({
    none: "无",
    trinket: `饰品 #${match.pocket_id}`,
    card: `卡牌 #${match.pocket_id}`,
    pill: `胶囊 #${match.pocket_id}`,
  })[match.pocket_kind] || match.pocket_kind;
}

function formatDelta(value) {
  const numeric = Number(value);
  return `${numeric > 0 ? "+" : ""}${numeric.toFixed(4)}`;
}

function appendCell(row, value, className) {
  const cell = document.createElement("td");
  cell.textContent = value;
  if (className) cell.className = className;
  row.appendChild(cell);
  return cell;
}

function appendDeltaCell(row, value) {
  const numeric = Number(value);
  return appendCell(row, formatDelta(numeric), numeric > 0 ? "delta-positive" : numeric < 0 ? "delta-negative" : "");
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
    body.innerHTML = '<tr><td colspan="11" class="empty">没有命中当前条件</td></tr>';
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
      appendCell(row, `#${match.active_id}`, "id-value");
      appendCell(row, `#${match.passive_id}`, "id-value");
      appendCell(row, `${match.red_hearts} 红 / ${match.soul_hearts} 魂`);
      appendDeltaCell(row, match.damage_delta);
      appendDeltaCell(row, match.move_speed_delta);
      appendDeltaCell(row, match.tears_delta);
      appendCell(row, Number(match.range).toFixed(4));
      appendDeltaCell(row, match.shot_speed_delta);
      appendDeltaCell(row, match.luck_delta);
      return row;
    }));
  }
  const suffix = result.truncated ? `，当前显示前 ${number.format(result.count)} 条` : "";
  $("#results-summary").textContent = `共命中 ${number.format(result.total_count)} 条${suffix}。`;
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
    $("#results-body").innerHTML = '<tr><td colspan="11" class="empty">扫描进行中…</td></tr>';
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
  updatePocketControls();
  updateCriteriaSummary();
  $("#preset-target").classList.remove("active");
});

$("#search-form").addEventListener("input", (event) => {
  if (!event.target.closest(".search-settings")) $("#preset-target").classList.remove("active");
  updateCriteriaSummary();
});

$("#preset-target").addEventListener("click", applyTargetPreset);
$("#clear-filters").addEventListener("click", clearFilters);
$("#cancel-button").addEventListener("click", () => request("/api/v1/search/cancel", {method: "POST"}));
$("#shutdown-button").addEventListener("click", async () => {
  await request("/api/v1/shutdown", {method: "POST"});
  document.body.innerHTML = '<main class="shell"><section class="panel"><h2>本地程序已关闭</h2><p>现在可以关闭这个页面。</p></section></main>';
});

updatePocketControls();
updateCriteriaSummary();
loadProfile().catch((error) => {
  $("#profile-name").textContent = "Profile 读取失败";
  $("#profile-detail").textContent = error.message;
});
updateStatus();
