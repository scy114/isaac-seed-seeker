const $ = (selector) => document.querySelector(selector);
const sessionToken = new URLSearchParams(window.location.search).get("token") || "";
const dailyConfig = document.body.dataset.page === "daily-bad"
  ? {rulesVersion: "daily-bad-v2", endpoint: "/api/v1/daily-bad"}
  : {rulesVersion: "daily-good-v1", endpoint: "/api/v1/daily-good"};
const rulesVersion = dailyConfig.rulesVersion;
const catalogByKey = new Map();
let dailyState = null;
let requestInProgress = false;
let catalogPromise = null;

function withToken(path) {
  const url = new URL(path, window.location.origin);
  if (sessionToken) url.searchParams.set("token", sessionToken);
  return `${url.pathname}${url.search}`;
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

function loadBackdropImage(source) {
  return new Promise((resolve, reject) => {
    const image = new Image();
    image.decoding = "async";
    image.addEventListener("load", () => resolve(image), {once: true});
    image.addEventListener("error", () => reject(new Error(source)), {once: true});
    image.src = source;
  });
}

async function initializeBasementBackdrop() {
  const canvas = $("#basement-room");
  const atlas = await loadBackdropImage("/game-assets/ui/basement-walls.png");
  const context = canvas?.getContext("2d", {alpha: false});
  if (!canvas || !context) return;
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
    const drawCorner = (x, y, targetWidth, targetHeight, flipX, flipY) => {
      context.save();
      context.translate(x + (flipX ? targetWidth : 0), y + (flipY ? targetHeight : 0));
      context.scale(flipX ? -1 : 1, flipY ? -1 : 1);
      context.drawImage(atlas, 0, 0, 234, 156, 0, 0, targetWidth, targetHeight);
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

function utc8Date() {
  const parts = new Intl.DateTimeFormat("en-US", {
    timeZone: "Asia/Shanghai",
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
  }).formatToParts(new Date());
  const values = Object.fromEntries(parts.map((part) => [part.type, part.value]));
  return `${values.year}-${values.month}-${values.day}`;
}

function storageKey(date) {
  return `isaac-seed-seeker:${rulesVersion}:${date}`;
}

function loadSavedState(date) {
  try {
    const saved = JSON.parse(localStorage.getItem(storageKey(date)) || "null");
    if (!saved || saved.date !== date || saved.rules_version !== rulesVersion || !saved.seed) return null;
    return {
      ...saved,
      seen: Array.isArray(saved.seen) ? saved.seen.slice(-200) : [saved.seed],
    };
  } catch {
    return null;
  }
}

function saveState() {
  if (dailyState) localStorage.setItem(storageKey(dailyState.date), JSON.stringify(dailyState));
}

function randomVariant() {
  const values = new Uint32Array(1);
  crypto.getRandomValues(values);
  return values[0] || 1;
}

function catalogEntry(kind, id) {
  return catalogByKey.get(`${kind}:${Number(id)}`);
}

function itemName(kind, id, fallback) {
  return catalogEntry(kind, id)?.name_zh || fallback;
}

async function ensureCatalog() {
  if (!catalogPromise) {
    catalogPromise = request("/catalog.json").then((catalog) => {
      catalog.entries.forEach((entry) => catalogByKey.set(`${entry.kind}:${entry.search_id}`, entry));
      return catalog;
    });
  }
  return catalogPromise;
}

function setDailyIcon(selector, kind, id) {
  const image = $(selector);
  if (!image || !["active", "passive", "trinket"].includes(kind) || Number(id) <= 0) {
    if (image) image.hidden = true;
    return;
  }
  image.hidden = false;
  image.src = `/game-assets/${kind}/${Number(id)}.png`;
  image.onerror = () => { image.hidden = true; };
}

function pocketDescription(result) {
  if (result.pocket_kind === "none") return {name: "无", meta: "没有口袋物", iconKind: null};
  if (result.pocket_kind === "trinket") {
    return {name: itemName("trinket", result.pocket_id, "饰品"), meta: `#${result.pocket_id}`, iconKind: "trinket"};
  }
  if (result.pocket_kind === "card") {
    return {name: itemName("card", result.pocket_id, "卡牌"), meta: `卡牌 #${result.pocket_id}`, iconKind: null};
  }
  const color = Number(result.pill_color || 0);
  const baseColor = color & 2047;
  const horse = (color & 2048) !== 0 ? " · 马胶囊" : "";
  return {
    name: itemName("pill", result.pocket_id, "胶囊"),
    meta: `效果 #${result.pocket_id} · 颜色 #${baseColor}${horse}`,
    iconKind: null,
  };
}

function hideDailyDetails() {
  $("#daily-details").hidden = true;
  $("#reveal-daily-seed").textContent = "开辉眼";
}

function renderDailyDetails(result) {
  const pocket = pocketDescription(result);
  $("#daily-pocket").textContent = pocket.name;
  $("#daily-pocket-meta").textContent = pocket.meta;
  setDailyIcon("#daily-pocket-icon", pocket.iconKind, result.pocket_id);

  $("#daily-active").textContent = itemName("active", result.active_id, "主动道具");
  $("#daily-active-meta").textContent = `Q${result.active_quality} · #${result.active_id}`;
  setDailyIcon("#daily-active-icon", "active", result.active_id);
  $("#daily-passive").textContent = itemName("passive", result.passive_id, "被动道具");
  $("#daily-passive-meta").textContent = `Q${result.passive_quality} · #${result.passive_id}`;
  setDailyIcon("#daily-passive-icon", "passive", result.passive_id);

  ["red_hearts", "soul_hearts", "coins", "keys", "bombs"].forEach((field) => {
    $(`#daily-${field.replaceAll("_", "-")}`).textContent = result[field];
  });
  ["damage", "move_speed", "tears", "range", "shot_speed", "luck"].forEach((field) => {
    $(`#daily-${field.replaceAll("_", "-")}`).textContent = Number(result[field]).toFixed(4);
  });

  $("#daily-details").hidden = false;
  $("#reveal-daily-seed").textContent = "问心无愧";
  $("#daily-details").scrollIntoView({behavior: "smooth", block: "nearest"});
}

function renderSeed(message) {
  $("#daily-seed").textContent = dailyState.seed.replace(" ", "\n");
  $("#daily-status").textContent = message;
  $("#copy-daily-seed").disabled = false;
  $("#reroll-daily-seed").disabled = false;
  $("#reveal-daily-seed").disabled = false;
}

async function fetchDailySeed(variant) {
  return request(dailyConfig.endpoint, {
    method: "POST",
    headers: {"Content-Type": "application/json"},
    body: JSON.stringify({date: $("#daily-date").dataset.isoDate, variant}),
  });
}

async function initializeDailySeed() {
  const date = utc8Date();
  $("#daily-date").dataset.isoDate = date;
  $("#daily-date").textContent = date;
  dailyState = loadSavedState(date);
  if (dailyState) {
    renderSeed(dailyState.variant === 0 ? "今天大家拿到的都是这颗" : `已经换过 ${Math.max(1, dailyState.seen.length - 1)} 次`);
    return;
  }
  const result = await fetchDailySeed(0);
  dailyState = {
    date,
    rules_version: result.rules_version,
    seed: result.seed,
    seed_u32: result.seed_u32,
    variant: 0,
    seen: [result.seed],
  };
  saveState();
  renderSeed("今天大家拿到的都是这颗");
}

async function rerollDailySeed() {
  if (requestInProgress || !dailyState) return;
  requestInProgress = true;
  const button = $("#reroll-daily-seed");
  const error = $("#daily-error");
  hideDailyDetails();
  button.disabled = true;
  $("#reveal-daily-seed").disabled = true;
  error.hidden = true;
  $("#daily-status").textContent = "再抽一个……";
  try {
    let result = null;
    for (let attempt = 0; attempt < 5; attempt += 1) {
      result = await fetchDailySeed(randomVariant());
      if (!dailyState.seen.includes(result.seed)) break;
    }
    if (!result || dailyState.seen.includes(result.seed)) throw new Error("刚好抽到了看过的种子，请再试一次");
    dailyState.seed = result.seed;
    dailyState.seed_u32 = result.seed_u32;
    dailyState.variant = result.variant;
    dailyState.seen = [...dailyState.seen, result.seed].slice(-200);
    saveState();
    renderSeed(`第 ${dailyState.seen.length - 1} 次换种`);
  } catch (failure) {
    error.textContent = failure.message;
    error.hidden = false;
    $("#daily-status").textContent = "这次没翻出来";
  } finally {
    requestInProgress = false;
    button.disabled = false;
    $("#reveal-daily-seed").disabled = false;
  }
}

async function toggleDailyReveal() {
  if (!dailyState || requestInProgress) return;
  if (!$("#daily-details").hidden) {
    hideDailyDetails();
    return;
  }

  const button = $("#reveal-daily-seed");
  const error = $("#daily-error");
  button.disabled = true;
  button.textContent = "正在开辉眼……";
  error.hidden = true;
  try {
    await ensureCatalog();
    const result = await request("/api/v1/inspect", {
      method: "POST",
      headers: {"Content-Type": "application/json"},
      body: JSON.stringify({seed: dailyState.seed}),
    });
    renderDailyDetails(result);
  } catch (failure) {
    error.textContent = failure.message;
    error.hidden = false;
    button.textContent = "开辉眼";
  } finally {
    button.disabled = false;
  }
}

async function copyDailySeed() {
  if (!dailyState) return;
  try {
    await navigator.clipboard.writeText(dailyState.seed);
  } catch {
    const input = document.createElement("textarea");
    input.value = dailyState.seed;
    document.body.append(input);
    input.select();
    document.execCommand("copy");
    input.remove();
  }
  const button = $("#copy-daily-seed");
  const original = button.textContent;
  button.textContent = "已复制";
  setTimeout(() => { button.textContent = original; }, 1200);
}

async function initialize() {
  $("#generic-page-link").href = withToken("/");
  document.querySelectorAll("img[data-game-icon]").forEach((image) => {
    image.addEventListener("error", () => image.classList.add("is-missing"), {once: true});
  });
  await initializeDailySeed();
}

$("#copy-daily-seed").addEventListener("click", copyDailySeed);
$("#reroll-daily-seed").addEventListener("click", rerollDailySeed);
$("#reveal-daily-seed").addEventListener("click", toggleDailyReveal);
$("#shutdown-button").addEventListener("click", async () => {
  await request("/api/v1/shutdown", {method: "POST"});
  document.body.innerHTML = '<main class="shell"><section class="panel"><h2>本地程序已关闭</h2><p>现在可以关闭这个页面。</p></section></main>';
});

initializeBasementBackdrop().catch(() => document.body.classList.add("backdrop-fallback"));
initialize().catch((failure) => {
  $("#daily-error").textContent = failure.message;
  $("#daily-error").hidden = false;
  $("#daily-status").textContent = "今天的种子没有成功翻出来";
});
