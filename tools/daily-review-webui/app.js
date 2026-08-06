const state = {
  payload: null,
  ratings: new Map(),
  filter: "all",
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
