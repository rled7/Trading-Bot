/**
 * AlgoForge Dashboard — app.js
 * Pure ES module, no bundler required.
 * Depends on Chart.js loaded globally via CDN before this module executes.
 */

// ============================================================
// Constants
// ============================================================

const POLL_INTERVAL_MS   = 5_000;
const CONN_TIMEOUT_MS    = 10_000;

const ORDER_TYPE_NAMES = {
  0: "BUY",
  1: "SELL",
  2: "BUY_LIMIT",
  3: "SELL_LIMIT",
  4: "BUY_STOP",
  5: "SELL_STOP",
};

// ============================================================
// State
// ============================================================

let token           = "";
let pollTimer       = null;
let sseSource       = null;
let priceChart      = null;
let lastSuccessMs   = 0;
let chartSymbol     = "";
let chartTf         = "M5";
let chartCount      = 200;
let symbolList      = [];

// ============================================================
// DOM references
// ============================================================

const tokenInput    = document.getElementById("token-input");
const tokenSave     = document.getElementById("token-save");
const connDot       = document.getElementById("conn-dot");
const connLabel     = document.getElementById("conn-label");
const bannerUnauth  = document.getElementById("banner-unauth");
const bannerConn    = document.getElementById("banner-conn");

const hBroker       = document.getElementById("h-broker");
const hConnected    = document.getElementById("h-connected");
const hVersion      = document.getElementById("h-version");
const hUptime       = document.getElementById("h-uptime");

const aBalance      = document.getElementById("a-balance");
const aEquity       = document.getElementById("a-equity");
const aMargin       = document.getElementById("a-margin");
const aFreeMargin   = document.getElementById("a-free-margin");
const aProfit       = document.getElementById("a-profit");
const aLeverage     = document.getElementById("a-leverage");

const ticksBody     = document.getElementById("ticks-body");
const posBody       = document.getElementById("positions-body");
const ordBody       = document.getElementById("orders-body");
const logsPre       = document.getElementById("logs-pre");
const logCount      = document.getElementById("log-count");

const chartSymbolEl = document.getElementById("chart-symbol");
const chartTfEl     = document.getElementById("chart-tf");
const chartCountEl  = document.getElementById("chart-count");
const chartLoadBtn  = document.getElementById("chart-load");

// ============================================================
// Token management
// ============================================================

function loadToken() {
  const stored = localStorage.getItem("dashboard_token") || "";
  token = stored;
  tokenInput.value = stored;
}

function saveToken() {
  const val = tokenInput.value.trim();
  token = val;
  localStorage.setItem("dashboard_token", val);
  reconnect();
}

tokenSave.addEventListener("click", saveToken);
tokenInput.addEventListener("keydown", (e) => {
  if (e.key === "Enter") saveToken();
});

// ============================================================
// HTTP helper
// ============================================================

async function fetchApi(path) {
  const res = await fetch(path, {
    headers: { Authorization: `Bearer ${token}` },
  });
  if (res.status === 401) {
    showBanner("unauth");
    throw Object.assign(new Error("Unauthorized"), { status: 401 });
  }
  if (!res.ok) {
    showBanner("conn");
    throw Object.assign(new Error(`HTTP ${res.status}`), { status: res.status });
  }
  clearBanners();
  return res.json();
}

// ============================================================
// Banner helpers
// ============================================================

function showBanner(type) {
  if (type === "unauth") {
    bannerUnauth.hidden = false;
    bannerConn.hidden   = true;
  } else {
    bannerConn.hidden   = false;
    bannerUnauth.hidden = true;
  }
}

function clearBanners() {
  bannerUnauth.hidden = true;
  bannerConn.hidden   = true;
}

// ============================================================
// Connection status indicator
// ============================================================

function markSuccess() {
  lastSuccessMs = Date.now();
  connDot.className  = "conn-dot conn-ok";
  connLabel.textContent = "Connected";
}

function markFailure() {
  const elapsed = Date.now() - lastSuccessMs;
  if (lastSuccessMs === 0 || elapsed > CONN_TIMEOUT_MS) {
    connDot.className  = "conn-dot conn-err";
    connLabel.textContent = "Disconnected";
  }
}

// ============================================================
// Formatting utilities
// ============================================================

const currFmt = new Intl.NumberFormat("en-US", {
  minimumFractionDigits: 2,
  maximumFractionDigits: 2,
});

function fmtCurrency(val, currency) {
  if (val == null) return "--";
  const n = Number(val);
  if (!isFinite(n)) return "--";
  const s = currency
    ? new Intl.NumberFormat("en-US", {
        style: "currency",
        currency,
        minimumFractionDigits: 2,
      }).format(n)
    : currFmt.format(n);
  return s;
}

function fmtNum(val, digits = 5) {
  if (val == null) return "--";
  const n = Number(val);
  if (!isFinite(n)) return "--";
  return n.toFixed(digits);
}

function fmtUptime(secs) {
  if (secs == null) return "--";
  const s = Math.floor(Number(secs));
  const d = Math.floor(s / 86400);
  const h = Math.floor((s % 86400) / 3600);
  const m = Math.floor((s % 3600) / 60);
  const parts = [];
  if (d) parts.push(`${d}d`);
  if (h || d) parts.push(`${h}h`);
  parts.push(`${m}m`);
  return parts.join(" ");
}

/**
 * Return the appropriate decimal-digit count for price display.
 * JPY pairs and XAU/USD use 3 digits; everything else defaults to 5.
 */
function pricePrecision(symbol) {
  if (!symbol) return 5;
  const s = symbol.toUpperCase();
  if (s.endsWith("JPY") || s === "XAUUSD" || s === "GOLD") return 3;
  return 5;
}

function fmtPrice(val, symbol) {
  return fmtNum(val, pricePrecision(symbol));
}

function fmtSpread(bid, ask, symbol) {
  if (bid == null || ask == null) return "--";
  return fmtNum(ask - bid, pricePrecision(symbol));
}

function profitClass(val) {
  const n = Number(val);
  if (n > 0)  return "profit-pos";
  if (n < 0)  return "profit-neg";
  return "profit-zero";
}

function fmtTimestamp(unix) {
  const d = new Date(unix * 1000);
  const hh = String(d.getHours()).padStart(2, "0");
  const mm = String(d.getMinutes()).padStart(2, "0");
  return `${hh}:${mm}`;
}

function fmtTime(unix) {
  const d = new Date(unix * 1000);
  return d.toLocaleTimeString("en-US", { hour12: false });
}

// ============================================================
// Render: Health
// ============================================================

function renderHealth(data) {
  hBroker.textContent     = data.broker_name ?? "--";
  hConnected.textContent  = data.is_connected ? "Yes" : "No";
  hConnected.className    = data.is_connected ? "profit-pos" : "profit-neg";
  hVersion.textContent    = data.version ?? "--";
  hUptime.textContent     = fmtUptime(data.uptime_seconds);
}

// ============================================================
// Render: Account
// ============================================================

function renderAccount(data) {
  const cur = data.currency || "USD";
  aBalance.textContent    = fmtCurrency(data.balance,     cur);
  aEquity.textContent     = fmtCurrency(data.equity,      cur);
  aMargin.textContent     = fmtCurrency(data.margin,      cur);
  aFreeMargin.textContent = fmtCurrency(data.free_margin, cur);
  const profitNum = Number(data.profit);
  aProfit.textContent     = fmtCurrency(data.profit, cur);
  aProfit.className       = profitClass(profitNum);
  aLeverage.textContent   = data.leverage ? `1:${data.leverage}` : "--";
}

// ============================================================
// Render: Positions
// ============================================================

function renderPositions(rows) {
  if (!rows || rows.length === 0) {
    posBody.innerHTML = `<tr><td colspan="9" class="empty-row">No open positions</td></tr>`;
    return;
  }
  posBody.innerHTML = rows.map((p) => {
    const side      = p.side === 1 ? "LONG" : (p.side === -1 ? "SHORT" : String(p.side));
    const sideClass = p.side === 1 ? "side-long" : "side-short";
    const digits    = pricePrecision(p.symbol);
    const pc        = profitClass(p.profit);
    return `<tr>
      <td>${p.ticket}</td>
      <td>${p.symbol ?? "--"}</td>
      <td class="${sideClass}">${side}</td>
      <td class="num">${fmtNum(p.lots, 2)}</td>
      <td class="num">${fmtNum(p.open_price, digits)}</td>
      <td class="num">${fmtNum(p.current_price, digits)}</td>
      <td class="num">${p.sl ? fmtNum(p.sl, digits) : "--"}</td>
      <td class="num">${p.tp ? fmtNum(p.tp, digits) : "--"}</td>
      <td class="num ${pc}">${fmtNum(p.profit, 2)}</td>
    </tr>`;
  }).join("");
}

// ============================================================
// Render: Orders
// ============================================================

function renderOrders(rows) {
  if (!rows || rows.length === 0) {
    ordBody.innerHTML = `<tr><td colspan="7" class="empty-row">No pending orders</td></tr>`;
    return;
  }
  ordBody.innerHTML = rows.map((o) => {
    const typeName  = ORDER_TYPE_NAMES[o.type] ?? `Type(${o.type})`;
    const digits    = pricePrecision(o.symbol);
    return `<tr>
      <td>${o.ticket}</td>
      <td>${o.symbol ?? "--"}</td>
      <td>${typeName}</td>
      <td class="num">${fmtNum(o.lots, 2)}</td>
      <td class="num">${fmtNum(o.price, digits)}</td>
      <td class="num">${o.sl ? fmtNum(o.sl, digits) : "--"}</td>
      <td class="num">${o.tp ? fmtNum(o.tp, digits) : "--"}</td>
    </tr>`;
  }).join("");
}

// ============================================================
// Render: Logs
// ============================================================

function renderLogs(entries) {
  if (!entries || entries.length === 0) {
    logsPre.textContent = "(no logs)";
    logCount.textContent = "";
    return;
  }
  logCount.textContent = `(${entries.length})`;
  logsPre.innerHTML = entries.map((e) => {
    const lvl    = (e.level || "INFO").toUpperCase();
    const cls    = `log-${lvl.toLowerCase()}`;
    const ts     = e.ts ? new Date(e.ts * 1000).toISOString().replace("T", " ").slice(0, 19) : "--";
    const comp   = e.component ? `[${e.component}]` : "";
    const msg    = escapeHtml(String(e.msg ?? ""));
    return `<span class="${cls}">${ts} ${lvl.padEnd(8)} ${comp.padEnd(14)} ${msg}</span>`;
  }).join("\n");
  // Scroll to bottom (newest logs)
  logsPre.scrollTop = logsPre.scrollHeight;
}

function escapeHtml(s) {
  return s
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;");
}

// ============================================================
// Live Ticks — SSE
// ============================================================

/**
 * Seed the ticks table with rows for every symbol in symbolList.
 * Each row gets id="tick-row-SYMBOL" so SSE updates can find it.
 */
function seedTicksTable(symbols) {
  ticksBody.innerHTML = symbols.map((sym) =>
    `<tr id="tick-row-${sym}">
      <td>${sym}</td>
      <td class="num" id="tick-bid-${sym}">--</td>
      <td class="num" id="tick-ask-${sym}">--</td>
      <td class="num" id="tick-spread-${sym}">--</td>
      <td class="num" id="tick-time-${sym}">--</td>
    </tr>`
  ).join("");
}

function openSse() {
  if (sseSource) {
    sseSource.close();
    sseSource = null;
  }
  if (!token) return;

  const url = `/api/stream/ticks?token=${encodeURIComponent(token)}`;
  sseSource = new EventSource(url);

  sseSource.addEventListener("message", (ev) => {
    try {
      const d = JSON.parse(ev.data);
      updateTickRow(d);
    } catch (_) {
      // ignore malformed frames
    }
  });

  sseSource.addEventListener("error", () => {
    // EventSource reconnects automatically; nothing to do here
  });
}

function updateTickRow(d) {
  const sym = d.symbol;
  if (!sym) return;

  // If the symbol isn't in the table yet (symbols loaded async), add a row
  if (!document.getElementById(`tick-row-${sym}`)) {
    const tr = document.createElement("tr");
    tr.id = `tick-row-${sym}`;
    tr.innerHTML = `
      <td>${sym}</td>
      <td class="num" id="tick-bid-${sym}">--</td>
      <td class="num" id="tick-ask-${sym}">--</td>
      <td class="num" id="tick-spread-${sym}">--</td>
      <td class="num" id="tick-time-${sym}">--</td>`;
    ticksBody.appendChild(tr);
  }

  const digits   = pricePrecision(sym);
  const bidEl    = document.getElementById(`tick-bid-${sym}`);
  const askEl    = document.getElementById(`tick-ask-${sym}`);
  const spreadEl = document.getElementById(`tick-spread-${sym}`);
  const timeEl   = document.getElementById(`tick-time-${sym}`);

  if (bidEl)    bidEl.textContent    = fmtNum(d.bid, digits);
  if (askEl)    askEl.textContent    = fmtNum(d.ask, digits);
  if (spreadEl) spreadEl.textContent = fmtSpread(d.bid, d.ask, sym);
  if (timeEl)   timeEl.textContent   = d.time ? fmtTime(d.time) : "--";
}

// ============================================================
// Chart
// ============================================================

function initChart() {
  const ctx = document.getElementById("price-chart").getContext("2d");
  priceChart = new Chart(ctx, {
    type: "line",
    data: {
      labels: [],
      datasets: [{
        label: "Close",
        data: [],
        borderColor: "#58a6ff",
        backgroundColor: "rgba(88,166,255,0.08)",
        borderWidth: 1.5,
        pointRadius: 0,
        tension: 0,
        fill: true,
      }],
    },
    options: {
      animation: false,
      responsive: true,
      maintainAspectRatio: false,
      interaction: { mode: "index", intersect: false },
      scales: {
        x: {
          ticks: {
            color: "#8b949e",
            font: { family: "Courier New, Courier, monospace", size: 10 },
            maxRotation: 0,
            autoSkip: true,
            maxTicksLimit: 12,
          },
          grid: { color: "#21262d" },
        },
        y: {
          position: "right",
          ticks: {
            color: "#8b949e",
            font: { family: "Courier New, Courier, monospace", size: 10 },
          },
          grid: { color: "#21262d" },
        },
      },
      plugins: {
        legend: { display: false },
        tooltip: {
          backgroundColor: "#161b22",
          borderColor: "#30363d",
          borderWidth: 1,
          titleColor: "#e6edf3",
          bodyColor: "#c9d1d9",
          titleFont: { family: "Courier New, Courier, monospace", size: 11 },
          bodyFont:  { family: "Courier New, Courier, monospace", size: 11 },
        },
      },
    },
  });
}

async function loadChart() {
  const sym   = chartSymbolEl.value;
  const tf    = chartTfEl.value;
  const count = Math.max(10, Math.min(2000, parseInt(chartCountEl.value, 10) || 200));

  if (!sym) return;

  let bars;
  try {
    bars = await fetchApi(`/api/bars/${encodeURIComponent(sym)}?tf=${tf}&count=${count}`);
  } catch (_) {
    return;
  }

  if (!bars || bars.length === 0) return;

  const digits = pricePrecision(sym);
  const labels = bars.map((b) => fmtTimestamp(b.timestamp));
  const prices = bars.map((b) => parseFloat(Number(b.close).toFixed(digits)));

  priceChart.data.labels              = labels;
  priceChart.data.datasets[0].data    = prices;
  priceChart.data.datasets[0].label   = `${sym} ${tf} — Close`;
  priceChart.update("none");
}

chartLoadBtn.addEventListener("click", loadChart);

// Reload on dropdown/count change
chartSymbolEl.addEventListener("change", loadChart);
chartTfEl.addEventListener("change", loadChart);
chartCountEl.addEventListener("change", () => {
  // sanitize
  let v = parseInt(chartCountEl.value, 10);
  if (!isFinite(v) || v < 10)  v = 10;
  if (v > 2000)                 v = 2000;
  chartCountEl.value = v;
  loadChart();
});

// ============================================================
// Symbol list initialisation
// ============================================================

async function loadSymbols() {
  let syms;
  try {
    syms = await fetchApi("/api/symbols");
  } catch (_) {
    return;
  }
  if (!Array.isArray(syms) || syms.length === 0) return;
  symbolList = syms;

  // Seed ticks table
  seedTicksTable(syms);

  // Populate chart symbol dropdown
  chartSymbolEl.innerHTML = syms.map((s) =>
    `<option value="${s}"${s === "EURUSD" ? " selected" : ""}>${s}</option>`
  ).join("");

  chartSymbol = chartSymbolEl.value || syms[0];
  loadChart();
}

// ============================================================
// Polling cycle
// ============================================================

async function pollOnce() {
  try {
    const [health, account, positions, orders, logs] = await Promise.all([
      fetchApi("/api/health"),
      fetchApi("/api/account"),
      fetchApi("/api/positions"),
      fetchApi("/api/orders"),
      fetchApi("/api/logs?n=50"),
    ]);

    renderHealth(health);
    renderAccount(account);
    renderPositions(positions);
    renderOrders(orders);
    renderLogs(logs);
    markSuccess();
    clearBanners();
  } catch (err) {
    markFailure();
    if (err.status !== 401) showBanner("conn");
  }
}

function startPolling() {
  if (pollTimer) clearInterval(pollTimer);
  pollOnce();
  pollTimer = setInterval(pollOnce, POLL_INTERVAL_MS);
}

// ============================================================
// Reconnect (called after token change)
// ============================================================

function reconnect() {
  openSse();
  startPolling();
}

// ============================================================
// Boot
// ============================================================

function boot() {
  loadToken();
  initChart();
  loadSymbols().then(() => {
    startPolling();
    openSse();
  });
}

boot();
