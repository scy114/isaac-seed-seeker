const $ = (selector) => document.querySelector(selector);
const sessionToken = new URLSearchParams(window.location.search).get("token") || "";
const seedAlphabet = "ABCDEFGHJKLMNPQRSTWXYZ01234V6789";
const catalogByKey = new Map();

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

function normalizeSeed(value) {
  const compact = String(value || "").toUpperCase().replace(/\s+/g, "");
  if (compact.length !== 8) throw new Error("种子必须包含八个字符");
  const invalid = [...new Set([...compact].filter((character) => !seedAlphabet.includes(character)))];
  if (invalid.length) throw new Error(`种子包含无效字符：${invalid.join("")}`);
  return `${compact.slice(0, 4)} ${compact.slice(4)}`;
}

function catalogEntry(kind, id) {
  return catalogByKey.get(`${kind}:${Number(id)}`);
}

function itemName(kind, id, fallback) {
  return catalogEntry(kind, id)?.name_zh || fallback;
}

function setIcon(selector, kind, id) {
  const image = $(selector);
  if (!image || !["active", "passive", "trinket"].includes(kind) || Number(id) <= 0) {
    if (image) image.hidden = true;
    return;
  }
  image.hidden = false;
  image.src = `/game-assets/${kind}/${Number(id)}.png`;
  image.addEventListener("error", () => { image.hidden = true; }, {once: true});
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

function renderResult(result) {
  $("#result-seed").textContent = result.seed;
  $("#result-seed-u32").textContent = result.seed_u32.toLocaleString("zh-CN");

  const pocket = pocketDescription(result);
  $("#result-pocket").textContent = pocket.name;
  $("#result-pocket-meta").textContent = pocket.meta;
  setIcon("#result-pocket-icon", pocket.iconKind, result.pocket_id);

  $("#result-active").textContent = itemName("active", result.active_id, "主动道具");
  $("#result-active-meta").textContent = `Q${result.active_quality} · #${result.active_id}`;
  setIcon("#result-active-icon", "active", result.active_id);
  $("#result-passive").textContent = itemName("passive", result.passive_id, "被动道具");
  $("#result-passive-meta").textContent = `Q${result.passive_quality} · #${result.passive_id}`;
  setIcon("#result-passive-icon", "passive", result.passive_id);

  ["red_hearts", "soul_hearts", "coins", "keys", "bombs"].forEach((field) => {
    $(`#result-${field.replaceAll("_", "-")}`).textContent = result[field];
  });
  ["damage", "move_speed", "tears", "range", "shot_speed", "luck"].forEach((field) => {
    $(`#result-${field.replaceAll("_", "-")}`).textContent = Number(result[field]).toFixed(4);
  });
  $("#inspector-result").hidden = false;
  $("#inspector-result").scrollIntoView({behavior: "smooth", block: "start"});
}

async function inspectSeed(rawValue) {
  const seed = normalizeSeed(rawValue);
  $("#seed-input").value = seed;
  const button = $("#inspect-button");
  const error = $("#inspect-error");
  button.disabled = true;
  error.hidden = true;
  try {
    const result = await request("/api/v1/inspect", {
      method: "POST",
      headers: {"Content-Type": "application/json"},
      body: JSON.stringify({seed}),
    });
    renderResult(result);
  } catch (failure) {
    error.textContent = failure.message === "seed checksum is invalid"
      ? "种子校验不正确，请检查输入"
      : failure.message;
    error.hidden = false;
    $("#inspector-result").hidden = true;
  } finally {
    button.disabled = false;
  }
}

async function initialize() {
  $("#generic-page-link").href = withToken("/");
  const [profile, catalog] = await Promise.all([
    request("/api/v1/profile"),
    request("/catalog.json"),
  ]);
  document.body.classList.toggle("has-local-art", profile.local_game_icons);
  $("#profile-name").textContent = `${profile.game_build} · 全解锁`;
  $("#profile-detail").textContent = profile.local_game_icons
    ? `${profile.game_version} · 已读取游戏图标`
    : profile.game_version;
  catalog.entries.forEach((entry) => catalogByKey.set(`${entry.kind}:${entry.search_id}`, entry));
}

$("#seed-input").addEventListener("input", (event) => {
  const compact = event.target.value.toUpperCase().replace(/\s+/g, "").slice(0, 8);
  event.target.value = compact.length > 4 ? `${compact.slice(0, 4)} ${compact.slice(4)}` : compact;
});
$("#seed-inspector-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  try {
    await inspectSeed($("#seed-input").value);
  } catch (failure) {
    $("#inspect-error").textContent = failure.message;
    $("#inspect-error").hidden = false;
  }
});
$("#shutdown-button").addEventListener("click", async () => {
  await request("/api/v1/shutdown", {method: "POST"});
  document.body.innerHTML = '<main class="shell"><section class="panel"><h2>本地程序已关闭</h2><p>现在可以关闭这个页面。</p></section></main>';
});

initializeBasementBackdrop().catch(() => document.body.classList.add("backdrop-fallback"));
initialize().catch((failure) => {
  $("#profile-name").textContent = "页面初始化失败";
  $("#profile-detail").textContent = failure.message;
});
