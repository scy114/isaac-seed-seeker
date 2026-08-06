const $ = (selector) => document.querySelector(selector);
const sessionToken = new URLSearchParams(window.location.search).get("token") || "";
const rulesVersion = "daily-good-v1";
let dailyState = null;
let requestInProgress = false;

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

function renderSeed(message) {
  $("#daily-seed").textContent = dailyState.seed.replace(" ", "\n");
  $("#daily-status").textContent = message;
  $("#copy-daily-seed").disabled = false;
  $("#reroll-daily-seed").disabled = false;
}

async function fetchDailySeed(variant) {
  return request("/api/v1/daily-good", {
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
    renderSeed(dailyState.variant === 0 ? "今天的第一颗种子" : `今天已经换过 ${Math.max(1, dailyState.seen.length - 1)} 次`);
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
  renderSeed("这是今天所有玩家共同看到的第一颗种子");
}

async function rerollDailySeed() {
  if (requestInProgress || !dailyState) return;
  requestInProgress = true;
  const button = $("#reroll-daily-seed");
  const error = $("#daily-error");
  button.disabled = true;
  error.hidden = true;
  $("#daily-status").textContent = "再翻一颗……";
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
    renderSeed(`已换第 ${dailyState.seen.length - 1} 次；开局依然保密`);
  } catch (failure) {
    error.textContent = failure.message;
    error.hidden = false;
    $("#daily-status").textContent = "这次没翻出来";
  } finally {
    requestInProgress = false;
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
