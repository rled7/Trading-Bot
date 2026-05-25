/**
 * AlgoForge Dashboard — algo_lab.js
 * Algo Lab page logic: generate, stream SSE, registry table.
 *
 * SSE consumer uses POST + fetch() + response.body.getReader()
 * (EventSource does not support POST).
 *
 * Token is read from sessionStorage["AF_DASHBOARD_TOKEN"] to
 * mirror the pattern used by other dashboard panels.
 */

// ============================================================
// Token helpers
// ============================================================

function getToken() {
  return sessionStorage.getItem("AF_DASHBOARD_TOKEN") || localStorage.getItem("dashboard_token") || "";
}

function authHeaders() {
  const tok = getToken();
  const h = { "Content-Type": "application/json" };
  if (tok) h["Authorization"] = `Bearer ${tok}`;
  return h;
}

// ============================================================
// Tier helpers
// ============================================================

const TIER_TABLE = [
  { max: 70,  label: "Reject", emoji: "\uD83D\uDD34", cls: "tier-reject" },  // 🔴
  { max: 80,  label: "Orange", emoji: "\uD83D\uDFE0", cls: "tier-orange" },  // 🟠
  { max: 90,  label: "Yellow", emoji: "\uD83D\uDFE1", cls: "tier-yellow" },  // 🟡
  { max: 95,  label: "Green",  emoji: "\uD83D\uDFE2", cls: "tier-green"  },  // 🟢
  { max: 101, label: "White",  emoji: "\u26AA",        cls: "tier-white"  },  // ⚪
];

function tierInfo(score) {
  const n = Number(score);
  for (const t of TIER_TABLE) {
    if (n < t.max) return t;
  }
  return TIER_TABLE[TIER_TABLE.length - 1];
}

function tierChipHtml(score) {
  if (score == null) return "--";
  const t = tierInfo(score);
  return `<span class="al-tier-chip ${t.cls}">${t.emoji} ${t.label} ${Number(score).toFixed(1)}</span>`;
}

// ============================================================
// State
// ============================================================

let activeController = null;  // AbortController for in-flight generate stream

// ============================================================
// DOM refs
// ============================================================

const briefEl        = document.getElementById("al-brief");
const effortEl       = document.getElementById("al-effort");
const seedEl         = document.getElementById("al-seed");
const generateBtn    = document.getElementById("al-generate-btn");
const disabledNotice = document.getElementById("al-disabled-notice");
const logList        = document.getElementById("al-log-list");
const registryTbody  = document.getElementById("al-registry-body");

// ============================================================
// Streaming log helpers
// ============================================================

function logClear() {
  logList.innerHTML = "";
}

function logAppend(iconHtml, textHtml, done = false) {
  const li = document.createElement("li");
  li.className = "al-log-item" + (done ? " done" : "");
  li.innerHTML = `<span class="al-log-icon">${iconHtml}</span><span class="al-log-text">${textHtml}</span>`;
  logList.appendChild(li);
  return li;
}

function logSpinner(text) {
  return logAppend('<span class="al-spinner"></span>', escapeHtml(text), false);
}

function logCheck(text) {
  return logAppend("\u2713", escapeHtml(text), true);  // ✓
}

function logError(text) {
  const li = document.createElement("li");
  li.className = "al-log-item done";
  li.innerHTML = `<span class="al-log-icon" style="color:var(--red)">&#x2717;</span><span class="al-log-text" style="color:var(--red)">${escapeHtml(text)}</span>`;
  logList.appendChild(li);
}

function escapeHtml(s) {
  return String(s)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;");
}

// ============================================================
// Stage label map
// ============================================================

const STAGE_LABELS = {
  prompt:     "Prompt sent",
  manifest:   null,  // filled dynamically with algo name
  validation: null,  // filled dynamically with validation info
  tier:       null,  // filled dynamically with score + tier
};

// ============================================================
// Generate — stream consumer
// ============================================================

async function startGenerate() {
  const brief = briefEl.value.trim();
  if (!brief) {
    briefEl.focus();
    return;
  }
  if (activeController) return;  // already streaming

  logClear();
  disabledNotice.classList.remove("visible");

  const payload = {
    brief,
    effort: effortEl.value,
  };
  const seedVal = seedEl.value.trim();
  if (seedVal !== "") {
    const n = parseInt(seedVal, 10);
    if (!isNaN(n)) payload.seed = n;
  }

  generateBtn.disabled = true;
  const controller = new AbortController();
  activeController = controller;

  // Show initial spinner
  let spinnerItem = logSpinner("Sending prompt...");

  try {
    const resp = await fetch("/api/algos/generate/stream", {
      method: "POST",
      headers: authHeaders(),
      body: JSON.stringify(payload),
      signal: controller.signal,
    });

    // Remove spinner
    if (spinnerItem && spinnerItem.parentNode) {
      spinnerItem.parentNode.removeChild(spinnerItem);
    }
    spinnerItem = null;

    // Handle 503 algo_gen_disabled
    if (resp.status === 503) {
      let errDetail = "algo_gen_disabled";
      try {
        const body = await resp.json();
        errDetail = body.detail?.error || body.detail || errDetail;
      } catch (_) { /* ignore */ }
      if (errDetail === "algo_gen_disabled" || String(errDetail).includes("algo_gen")) {
        disabledNotice.classList.add("visible");
      } else {
        logError(`Server error (503): ${errDetail}`);
      }
      return;
    }

    if (!resp.ok) {
      logError(`Server error (${resp.status})`);
      return;
    }

    // Consume SSE frames from ReadableStream
    const reader = resp.body.getReader();
    const decoder = new TextDecoder();
    let buf = "";

    // Track stages for log ordering
    let lastManifestName = null;

    // eslint-disable-next-line no-constant-condition
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;

      buf += decoder.decode(value, { stream: true });

      // Split on SSE line boundaries ("data: <json>\n\n")
      const lines = buf.split("\n");
      buf = lines.pop(); // keep trailing incomplete fragment

      for (const line of lines) {
        if (!line.startsWith("data: ")) continue;
        const raw = line.slice(6);
        if (raw === "[DONE]") break;

        let evt;
        try {
          evt = JSON.parse(raw);
        } catch (_) {
          continue;
        }

        handleStageEvent(evt);

        // After tier event, refresh registry
        if (evt.stage === "tier") {
          await loadRegistry();
        }
      }
    }

    // Handle any trailing buffer content
    if (buf.startsWith("data: ")) {
      const tail = buf.slice(6);
      if (tail && tail !== "[DONE]") {
        let evt;
        try {
          evt = JSON.parse(tail);
          handleStageEvent(evt);
          if (evt.stage === "tier") await loadRegistry();
        } catch (_) { /* ignore */ }
      }
    }

  } catch (err) {
    if (err.name === "AbortError") {
      logError("Generation aborted.");
    } else {
      logError(`Error: ${err.message}`);
    }
  } finally {
    activeController = null;
    generateBtn.disabled = false;
    if (spinnerItem && spinnerItem.parentNode) {
      spinnerItem.parentNode.removeChild(spinnerItem);
    }
  }
}

function handleStageEvent(evt) {
  const stage = evt.stage;
  if (!stage) return;

  if (stage === "prompt") {
    logCheck("Prompt sent");

  } else if (stage === "manifest") {
    const name = evt.manifest?.name || evt.name || "algo";
    logCheck(`Manifest received: "${escapeHtml(name)}"`);

  } else if (stage === "validation") {
    const v = evt.validation || {};
    const schemaOk  = v.schema_valid !== false;
    const wf        = v.walk_forward != null ? Number(v.walk_forward).toFixed(1) : null;
    const mc        = v.mc_bootstrap  != null ? Number(v.mc_bootstrap).toFixed(1)  : null;
    const rob       = v.robustness    != null ? Number(v.robustness).toFixed(1)   : null;

    if (schemaOk) logCheck("Schema valid");
    if (wf  != null) logCheck(`Walk-forward ${wf}`);
    if (mc  != null) logCheck(`MC bootstrap ${mc}`);
    if (rob != null) logCheck(`Robustness ${rob}`);

  } else if (stage === "tier") {
    const score   = evt.score != null ? Number(evt.score) : null;
    const tierStr = evt.tier  || (score != null ? null : "unknown");
    if (score != null) {
      const info = tierInfo(score);
      logAppend(
        info.emoji,
        `Robustness ${score.toFixed(1)}  &rarr; Tier: ${info.emoji} ${info.label} ${score.toFixed(1)}`,
        true
      );
    } else {
      logCheck(`Tier: ${escapeHtml(tierStr)}`);
    }
  }
}

// ============================================================
// Registry table
// ============================================================

async function loadRegistry() {
  let algos;
  try {
    const resp = await fetch("/api/algos", { headers: authHeaders() });
    if (!resp.ok) return;
    algos = await resp.json();
  } catch (_) {
    return;
  }

  if (!Array.isArray(algos) || algos.length === 0) {
    registryTbody.innerHTML = `<tr><td colspan="5" class="al-empty-row">No algos in registry</td></tr>`;
    return;
  }

  registryTbody.innerHTML = algos.map((a) => {
    const scoreNum = a.score != null ? Number(a.score) : null;
    const tierHtml = tierChipHtml(scoreNum);
    const statusCls = statusClass(a.status);
    const actionsHtml = buildActions(a);
    return `<tr>
      <td>${tierHtml}</td>
      <td>${escapeHtml(a.name || a.algo_id || "--")}</td>
      <td class="num">${scoreNum != null ? scoreNum.toFixed(1) : "--"}</td>
      <td><span class="al-status ${statusCls}">${escapeHtml(a.status || "--")}</span></td>
      <td>${actionsHtml}</td>
    </tr>`;
  }).join("");

  // Bind action buttons
  registryTbody.querySelectorAll("[data-action]").forEach((btn) => {
    btn.addEventListener("click", onActionClick);
  });
}

function statusClass(status) {
  const map = {
    active:   "al-status-active",
    ready:    "al-status-ready",
    rejected: "al-status-rejected",
    retired:  "al-status-retired",
  };
  return map[String(status).toLowerCase()] || "";
}

function buildActions(algo) {
  const id     = algo.algo_id || algo.id || "";
  const status = String(algo.status || "").toLowerCase();
  const parts  = [];

  if (status === "ready") {
    parts.push(`<button class="al-action-btn" data-action="promote" data-id="${escapeHtml(id)}">Promote</button>`);
    parts.push(`<button class="al-action-btn" data-action="backtest" data-id="${escapeHtml(id)}">Backtest</button>`);
  } else if (status === "active") {
    parts.push(`<button class="al-action-btn" data-action="retire" data-id="${escapeHtml(id)}">Retire</button>`);
  }
  return parts.join("") || "&mdash;";
}

async function onActionClick(e) {
  const btn    = e.currentTarget;
  const action = btn.dataset.action;
  const id     = btn.dataset.id;
  if (!action || !id) return;

  btn.disabled = true;

  try {
    let url, method;
    if (action === "promote") {
      url    = `/api/algos/${encodeURIComponent(id)}/promote`;
      method = "POST";
    } else if (action === "retire") {
      url    = `/api/algos/${encodeURIComponent(id)}/retire`;
      method = "POST";
    } else if (action === "backtest") {
      url    = `/api/algos/${encodeURIComponent(id)}/backtest`;
      method = "POST";
    } else {
      return;
    }

    const resp = await fetch(url, {
      method,
      headers: authHeaders(),
    });

    if (!resp.ok) {
      console.error(`[AlgoLab] ${action} failed: HTTP ${resp.status}`);
    }

    await loadRegistry();
  } catch (err) {
    console.error("[AlgoLab] action error:", err);
    btn.disabled = false;
  }
}

// ============================================================
// Boot
// ============================================================

generateBtn.addEventListener("click", startGenerate);

// Allow Ctrl+Enter in textarea to trigger generate
briefEl.addEventListener("keydown", (e) => {
  if (e.key === "Enter" && (e.ctrlKey || e.metaKey)) {
    e.preventDefault();
    startGenerate();
  }
});

// Initial registry load on page open
loadRegistry();
