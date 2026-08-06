const state = {
  payload: null,
  ratings: new Map(),
  filter: "all",
  q3Items: [],
  q3Ratings: new Map(),
  q3Loaded: false,
  q3SelectedKey: null,
};

const $ = (selector) => document.querySelector(selector);
const resultsElement = $("#results");
const statusElement = $("#status");

function escapeHtml(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

function itemMarkup(row, kind) {
  const id = row[`${kind}_id`];
  const quality = row[`${kind}_quality`];
  const name = row[`${kind}_name_zh`];
  const english = row[`${kind}_name_en`];
  const visual = row[`${kind}_has_icon`]
    ? `<img src="/icon/${id}.png" alt="">`
    : `<span class="fallback">#${id}</span>`;
  return `<div class="item">
    ${visual}
    <div class="item-copy">
      <strong>${escapeHtml(name)}</strong>
      <small>${kind === "active" ? "主动" : "被动"} #${id} · Q${quality}</small>
      <small title="${escapeHtml(english)}">${escapeHtml(english)}</small>
    </div>
  </div>`;
}

function bonusText(row) {
  const labels = [
    [row.active_q4_bonus, "主动 Q4"],
    [row.passive_q4_bonus, "被动 Q4"],
    [row.death_certificate_bonus, "死亡证明"],
    [row.damage_bonus, "伤害"],
    [row.tears_bonus, "射速"],
    [row.move_speed_bonus, "移速"],
  ];
  return ["基础 100", ...labels.filter(([value]) => value > 0).map(([value, label]) => `${label} +${value}`)].join(" · ");
}

function renderResults() {
  if (!state.payload) return;
  const cards = state.payload.results.map((row) => {
    const rating = state.ratings.get(row.seed) || "pending";
    const hidden = state.filter !== "all" && state.filter !== rating;
    return `<article class="seed-card" data-seed="${row.seed}" data-rating="${rating}" ${hidden ? "hidden" : ""}>
      <div class="card-head">
        <span class="date">${row.date}</span>
        <strong class="seed">${row.seed}</strong>
        <span class="weight">${row.weight} 分</span>
      </div>
      <div class="items">${itemMarkup(row, "active")}${itemMarkup(row, "passive")}</div>
      <div class="stats">
        <div class="stat"><span>伤害</span><strong>${row.damage.toFixed(3)}</strong></div>
        <div class="stat"><span>射速</span><strong>${row.tears.toFixed(3)}</strong></div>
        <div class="stat"><span>移速</span><strong>${row.move_speed.toFixed(3)}</strong></div>
      </div>
      <div class="bonuses">${bonusText(row)}</div>
      <div class="rating">
        <button data-rating="good" class="${rating === "good" ? "selected" : ""}">爽</button>
        <button data-rating="neutral" class="${rating === "neutral" ? "selected" : ""}">一般</button>
        <button data-rating="bad" class="${rating === "bad" ? "selected" : ""}">不爽</button>
      </div>
    </article>`;
  });
  resultsElement.innerHTML = cards.join("") || '<div class="empty">没有样本</div>';
  updateSummary();
}

function updateSummary() {
  if (!state.payload) return;
  const rows = state.payload.results;
  const counts = { good: 0, neutral: 0, bad: 0 };
  state.ratings.forEach((rating) => { counts[rating] += 1; });
  $("#summary-total").textContent = rows.length;
  $("#summary-good").textContent = counts.good;
  $("#summary-neutral").textContent = counts.neutral;
  $("#summary-bad").textContent = counts.bad;
  $("#summary-pending").textContent = rows.length - state.ratings.size;
  const meanWeight = rows.reduce((sum, row) => sum + row.weight, 0) / Math.max(1, rows.length);
  $("#summary-weight").textContent = meanWeight.toFixed(1);
}

const q3Scale = ["不推荐", "一般", "不错", "很爽", "核心"];
let q3SaveTimer = null;

async function loadQ3Items() {
  if (state.q3Loaded) return;
  $("#q3-save-status").textContent = "正在读取评分……";
  try {
    const [itemsResponse, ratingsResponse] = await Promise.all([
      fetch("/api/q3-items"),
      fetch("/api/q3-ratings"),
    ]);
    const itemsPayload = await itemsResponse.json();
    const ratingsPayload = await ratingsResponse.json();
    if (!itemsResponse.ok) throw new Error(itemsPayload.error || "读取道具失败");
    if (!ratingsResponse.ok) throw new Error(ratingsPayload.error || "读取评分失败");
    state.q3Items = itemsPayload.items;
    state.q3Ratings = new Map(
      Object.entries(ratingsPayload.ratings || {}).map(([key, score]) => [key, Number(score)]),
    );
    state.q3Loaded = true;
    $("#q3-save-status").textContent = state.q3Ratings.size ? "已恢复上次评分" : "尚未开始评分";
    renderQ3Items();
  } catch (error) {
    $("#q3-save-status").textContent = `读取失败：${error.message}`;
  }
}

function visibleQ3Items() {
  const kind = $("#q3-kind").value;
  const status = $("#q3-status-filter").value;
  const query = $("#q3-search").value.trim().toLocaleLowerCase("zh-CN");
  return state.q3Items.filter((item) => {
    const rated = state.q3Ratings.has(item.key);
    if (kind !== "all" && item.kind !== kind) return false;
    if (status === "pending" && rated) return false;
    if (status === "rated" && !rated) return false;
    if (!query) return true;
    return `${item.id} ${item.name_zh} ${item.name_en}`.toLocaleLowerCase("zh-CN").includes(query);
  });
}

function renderQ3Items(nextFocusKey = null) {
  if (!state.q3Loaded) return;
  const items = visibleQ3Items();
  $("#q3-list").innerHTML = items.map((item) => {
    const currentScore = state.q3Ratings.get(item.key);
    const visual = item.has_icon
      ? `<img src="/icon/${item.id}.png" alt="">`
      : `<span class="fallback">#${item.id}</span>`;
    const buttons = q3Scale.map((label, score) => (
      `<button class="q3-score ${currentScore === score ? "selected" : ""}" data-score="${score}" title="${score}：${label}"><b>${score}</b>${label}</button>`
    )).join("");
    return `<article class="q3-row ${state.q3SelectedKey === item.key ? "selected" : ""}" tabindex="0" data-key="${item.key}">
      ${visual}
      <div class="q3-item-copy">
        <strong>${escapeHtml(item.name_zh)}</strong>
        <small>${item.kind === "active" ? "主动" : "被动"} #${item.id} · ${escapeHtml(item.name_en)}</small>
      </div>
      ${buttons}
    </article>`;
  }).join("") || '<div class="empty">当前筛选下没有道具</div>';

  $("#q3-rated").textContent = state.q3Ratings.size;
  $("#q3-pending").textContent = Math.max(0, state.q3Items.length - state.q3Ratings.size);
  $("#q3-visible").textContent = items.length;
  if (nextFocusKey) {
    requestAnimationFrame(() => {
      const row = [...document.querySelectorAll(".q3-row")].find((element) => element.dataset.key === nextFocusKey);
      row?.focus({ preventScroll: false });
    });
  }
}

function rateQ3Item(key, score) {
  const before = visibleQ3Items();
  const index = before.findIndex((item) => item.key === key);
  const next = [...before.slice(index + 1), ...before.slice(0, Math.max(index, 0))]
    .find((item) => item.key !== key);
  state.q3Ratings.set(key, score);
  state.q3SelectedKey = next?.key || key;
  renderQ3Items(next?.key);
  scheduleQ3Save();
}

function scheduleQ3Save() {
  clearTimeout(q3SaveTimer);
  $("#q3-save-status").textContent = "正在保存……";
  q3SaveTimer = setTimeout(saveQ3Ratings, 250);
}

async function saveQ3Ratings() {
  try {
    const response = await fetch("/api/q3-ratings", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ ratings: Object.fromEntries(state.q3Ratings) }),
    });
    const payload = await response.json();
    if (!response.ok) throw new Error(payload.error || "保存失败");
    $("#q3-save-status").textContent = `已自动保存 ${payload.saved} 项`;
  } catch (error) {
    $("#q3-save-status").textContent = `保存失败：${error.message}`;
  }
}

async function generateSamples() {
  const button = $("#generate");
  button.disabled = true;
  statusElement.textContent = "正在扫描，30 天通常数秒，100 天约十几秒……";
  try {
    const response = await fetch("/api/generate", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        start_date: $("#start-date").value,
        days: Number($("#days").value),
        candidates: 10_000_000,
      }),
    });
    const payload = await response.json();
    if (!response.ok) throw new Error(payload.error || "生成失败");
    state.payload = payload;
    state.ratings.clear();
    state.filter = "all";
    $("#filters").querySelectorAll("button").forEach((element) => {
      element.classList.toggle("active", element.dataset.filter === "all");
    });
    $("#summary").hidden = false;
    $("#review-tools").hidden = false;
    renderResults();
    statusElement.textContent = `已生成 ${payload.results.length} 天 · ${payload.icons_available ? "已读取游戏图标" : "未找到游戏图标"}`;
  } catch (error) {
    statusElement.textContent = `失败：${error.message}`;
  } finally {
    button.disabled = false;
  }
}

resultsElement.addEventListener("click", (event) => {
  const button = event.target.closest("button[data-rating]");
  if (!button) return;
  const card = button.closest(".seed-card");
  const seed = card.dataset.seed;
  const rating = button.dataset.rating;
  if (state.ratings.get(seed) === rating) state.ratings.delete(seed);
  else state.ratings.set(seed, rating);
  renderResults();
});

$("#filters").addEventListener("click", (event) => {
  const button = event.target.closest("button[data-filter]");
  if (!button) return;
  state.filter = button.dataset.filter;
  $("#filters").querySelectorAll("button").forEach((element) => element.classList.toggle("active", element === button));
  renderResults();
});

$("#export").addEventListener("click", () => {
  if (!state.payload) return;
  const exportPayload = {
    rules_version: state.payload.rules_version,
    start_date: state.payload.start_date,
    days: state.payload.days,
    candidates_per_day: state.payload.candidates_per_day,
    reviews: state.payload.results.map((row) => ({
      ...row,
      rating: state.ratings.get(row.seed) || "pending",
    })),
  };
  const blob = new Blob([JSON.stringify(exportPayload, null, 2)], { type: "application/json" });
  const link = document.createElement("a");
  link.href = URL.createObjectURL(blob);
  link.download = `daily-good-review-${state.payload.start_date}.json`;
  link.click();
  URL.revokeObjectURL(link.href);
});

$("#generate").addEventListener("click", generateSamples);

document.querySelector(".page-tabs").addEventListener("click", (event) => {
  const button = event.target.closest("button[data-page]");
  if (!button) return;
  const page = button.dataset.page;
  $("#daily-page").hidden = page !== "daily";
  $("#q3-page").hidden = page !== "q3";
  document.querySelectorAll(".page-tabs button").forEach((element) => {
    element.classList.toggle("active", element === button);
  });
  if (page === "q3") loadQ3Items();
});

$("#q3-list").addEventListener("click", (event) => {
  const row = event.target.closest(".q3-row");
  if (!row) return;
  state.q3SelectedKey = row.dataset.key;
  const button = event.target.closest("button[data-score]");
  if (button) rateQ3Item(row.dataset.key, Number(button.dataset.score));
  else {
    document.querySelectorAll(".q3-row.selected").forEach((element) => element.classList.remove("selected"));
    row.classList.add("selected");
    row.focus();
  }
});

$("#q3-list").addEventListener("keydown", (event) => {
  if (!/^[0-4]$/.test(event.key)) return;
  const row = event.target.closest(".q3-row");
  if (!row) return;
  event.preventDefault();
  rateQ3Item(row.dataset.key, Number(event.key));
});

for (const selector of ["#q3-kind", "#q3-status-filter"]) {
  $(selector).addEventListener("change", () => renderQ3Items());
}
$("#q3-search").addEventListener("input", () => renderQ3Items());

$("#q3-export").addEventListener("click", () => {
  const payload = {
    schema_version: 1,
    rules_target: "daily-good-v1",
    scale: Object.fromEntries(q3Scale.map((label, score) => [score, label])),
    ratings: Object.fromEntries(state.q3Ratings),
  };
  const blob = new Blob([JSON.stringify(payload, null, 2)], { type: "application/json" });
  const link = document.createElement("a");
  link.href = URL.createObjectURL(blob);
  link.download = "daily-good-q3-ratings.json";
  link.click();
  URL.revokeObjectURL(link.href);
});
