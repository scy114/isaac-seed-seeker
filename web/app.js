const $ = (selector) => document.querySelector(selector);
const number = new Intl.NumberFormat("zh-CN");
const sessionToken = new URLSearchParams(window.location.search).get("token") || "";
let pollTimer = null;

function parseIds(value) {
  const ids = value.split(/[，,\s]+/).filter(Boolean).map(Number);
  if (!ids.length || ids.some((id) => !Number.isInteger(id) || id <= 0)) {
    throw new Error("道具 ID 必须是用逗号分隔的正整数");
  }
  return [...new Set(ids)];
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
  return ({
    idle: "等待开始",
    running: "正在扫描",
    completed: "扫描完成",
    cancelled: "已停止",
    failed: "搜索失败",
  })[state] || state;
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

    if (status.state === "running" && !pollTimer) {
      pollTimer = setInterval(updateStatus, 250);
    }
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
    body.innerHTML = '<tr><td colspan="5" class="empty">没有命中当前条件</td></tr>';
  } else {
    body.replaceChildren(...result.matches.map((match) => {
      const row = document.createElement("tr");
      [match.seed, number.format(match.seed_u32), match.trinket_id, match.active_id, match.passive_id]
        .forEach((value) => {
          const cell = document.createElement("td");
          cell.textContent = value;
          row.appendChild(cell);
        });
      return row;
    }));
  }
  $("#download-link").classList.toggle("disabled", !result.matches.length);
}

$("#search-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  try {
    const payload = {
      trinket_id: Number($("#trinket-id").value),
      active_ids: parseIds($("#active-ids").value),
      passive_ids: parseIds($("#passive-ids").value),
      start: Number($("#range-start").value),
      end: Number($("#range-end").value),
      threads: Number($("#threads").value),
    };
    if (!Number.isInteger(payload.trinket_id) || payload.trinket_id <= 0) throw new Error("饰品 ID 无效");
    if (![payload.start, payload.end, payload.threads].every(Number.isInteger)) throw new Error("范围和线程必须是整数");
    await request("/api/v1/search", {
      method: "POST",
      headers: {"Content-Type": "application/json"},
      body: JSON.stringify(payload),
    });
    $("#results-body").innerHTML = '<tr><td colspan="5" class="empty">扫描进行中…</td></tr>';
    $("#download-link").classList.add("disabled");
    if (pollTimer) clearInterval(pollTimer);
    pollTimer = setInterval(updateStatus, 250);
    await updateStatus();
  } catch (error) {
    $("#error-message").hidden = false;
    $("#error-message").textContent = error.message;
  }
});

$("#cancel-button").addEventListener("click", async () => {
  await request("/api/v1/search/cancel", {method: "POST"});
});

$("#shutdown-button").addEventListener("click", async () => {
  await request("/api/v1/shutdown", {method: "POST"});
  document.body.innerHTML = '<main class="shell"><section class="panel"><h2>本地程序已关闭</h2><p>现在可以关闭这个页面。</p></section></main>';
});

loadProfile().catch((error) => {
  $("#profile-name").textContent = "Profile 读取失败";
  $("#profile-detail").textContent = error.message;
});
updateStatus();
