#include "qo/html_reporter.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace qo {

// ── htmlEscape ────────────────────────────────────────────────────────────────

std::string HtmlReporter::htmlEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
        case '&':  out += "&amp;";  break;
        case '<':  out += "&lt;";   break;
        case '>':  out += "&gt;";   break;
        case '"':  out += "&quot;"; break;
        case '\'': out += "&#39;";  break;
        default:   out += c;        break;
        }
    }
    return out;
}

// ── jsEscape ─────────────────────────────────────────────────────────────────

std::string HtmlReporter::jsEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '`':  out += "\\`";  break;
        case '$':  out += "\\$";  break;
        default:   out += c;      break;
        }
    }
    return out;
}

// ── generateDashboard ─────────────────────────────────────────────────────────

void HtmlReporter::generateDashboard(const std::string&               filename,
                                     const std::vector<QueryProfile>&  profiles) {
    if (profiles.empty()) {
        throw std::runtime_error("HtmlReporter: profiles vector must not be empty");
    }

    std::ofstream out(filename);
    if (!out.is_open()) {
        throw std::runtime_error(
            "HtmlReporter: failed to open output file '" + filename + "'");
    }

    // ── Static HTML + CSS ─────────────────────────────────────────────────────
    out << R"(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <meta name="description" content="SQL Query Optimiser dashboard." />
  <title>SQL Query Optimiser</title>
  <link rel="preconnect" href="https://fonts.googleapis.com" />
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin />
  <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700;800&display=swap" rel="stylesheet" />
  <style>
    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

    :root {
      --bg:            #090d16;
      --surface:       #111827;
      --surface-mid:   #161f2e;
      --surface-hi:    #1f2937;
      --border:        rgba(51, 65, 85, 0.4);
      --border-solid:  #1e2d3d;
      --text-base:     #e2e8f0;
      --text-muted:    #94a3b8;
      --text-faint:    #475569;
      --green:         #34d399;
      --green-dim:     rgba(52, 211, 153, 0.12);
      --green-glow:    rgba(52, 211, 153, 0.18);
      --green-btn:     rgba(52, 211, 153, 0.08);
      --blue:          #60a5fa;
      --blue-dim:      rgba(96, 165, 250, 0.12);
      --amber:         #fbbf24;
      --amber-dim:     rgba(251, 191, 36, 0.10);
      --rose:          #f87171;
      --sans:  'Inter', system-ui, -apple-system, sans-serif;
      --mono:  'Cascadia Code', 'Fira Code', 'Consolas', 'Courier New', monospace;
      --radius:    10px;
      --radius-sm: 6px;
    }

    html { font-size: 16px; scroll-behavior: smooth; }

    body {
      background: var(--bg);
      color: var(--text-base);
      font-family: var(--sans);
      line-height: 1.6;
      min-height: 100vh;
      -webkit-font-smoothing: antialiased;
    }
    ::-webkit-scrollbar              { width: 5px; height: 5px; }
    ::-webkit-scrollbar-track        { background: transparent; }
    ::-webkit-scrollbar-thumb        { background: #1e2d3d; border-radius: 99px; }
    ::-webkit-scrollbar-thumb:hover  { background: #2d3f52; }
    .navbar {
      display: flex;
      align-items: center;
      gap: 16px;
      padding: 0 32px;
      height: 58px;
      background: rgba(9, 13, 22, 0.88);
      backdrop-filter: blur(16px);
      border-bottom: 1px solid var(--border);
      position: sticky;
      top: 0;
      z-index: 200;
    }

    .logo { display: flex; align-items: center; gap: 10px; text-decoration: none; }

    .logo-icon {
      width: 30px; height: 30px;
      background: linear-gradient(135deg, #0f2027, #203a43, #2c5364);
      border-radius: 8px;
      display: flex; align-items: center; justify-content: center;
      flex-shrink: 0;
      border: 1px solid rgba(96,165,250,0.2);
    }

    .logo-text {
      font-size: 1.05rem;
      font-weight: 700;
      letter-spacing: -0.02em;
      background: linear-gradient(to right, #34d399, #60a5fa);
      -webkit-background-clip: text;
      -webkit-text-fill-color: transparent;
      background-clip: text;
    }

    .navbar-divider { width: 1px; height: 20px; background: var(--border-solid); }
    .selector-wrap { display: flex; align-items: center; gap: 0; }

    .selector-label {
      font-size: 0.72rem;
      font-weight: 600;
      letter-spacing: 0.05em;
      text-transform: uppercase;
      color: var(--text-faint);
      margin-right: 10px;
      white-space: nowrap;
    }

    #profile-select {
      appearance: none;
      -webkit-appearance: none;
      background-color: var(--surface-hi);
      background-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 12 12'%3E%3Cpath fill='%2360a5fa' d='M6 8L1 3h10z'/%3E%3C/svg%3E");
      background-repeat: no-repeat;
      background-position: right 12px center;
      border: 1px solid var(--border-solid);
      border-radius: var(--radius-sm);
      color: var(--text-base);
      font-family: var(--sans);
      font-size: 0.82rem;
      font-weight: 500;
      padding: 6px 36px 6px 12px;
      cursor: pointer;
      outline: none;
      transition: border-color 0.2s, box-shadow 0.2s;
      min-width: 240px;
    }

    #profile-select:focus {
      border-color: var(--blue);
      box-shadow: 0 0 0 3px rgba(96, 165, 250, 0.15);
    }

    #profile-select:hover { border-color: #2d3f52; }

    .navbar-badge {
      margin-left: auto;
      font-size: 0.68rem;
      font-weight: 600;
      letter-spacing: 0.06em;
      text-transform: uppercase;
      color: var(--green);
      background: var(--green-dim);
      border: 1px solid rgba(52, 211, 153, 0.25);
      border-radius: 99px;
      padding: 3px 11px;
    }
    .page { max-width: 1440px; margin: 0 auto; padding: 36px 28px 72px; }
    #dashboard-content { transition: opacity 0.22s ease; }
    #dashboard-content.fading { opacity: 0; }
    .section-row {
      display: flex; align-items: center; gap: 14px; margin-bottom: 20px;
    }
    .section-row h2 {
      font-size: 0.8rem; font-weight: 600; letter-spacing: 0.05em;
      text-transform: uppercase; color: var(--text-faint); white-space: nowrap;
    }
    .section-row hr { flex: 1; border: none; border-top: 1px solid var(--border); }
    .console-panel {
      background: var(--surface);
      border: 1px solid var(--border);
      border-radius: var(--radius);
      padding: 20px 22px;
      margin-bottom: 24px;
    }

    .console-header {
      display: flex;
      align-items: center;
      gap: 10px;
      margin-bottom: 12px;
    }

    .console-dot {
      width: 9px; height: 9px; border-radius: 50%; flex-shrink: 0;
    }
    .console-dot--red    { background: #f87171; }
    .console-dot--amber  { background: #fbbf24; }
    .console-dot--green  { background: #34d399; }

    .console-title {
      font-size: 0.75rem;
      font-weight: 600;
      letter-spacing: 0.05em;
      text-transform: uppercase;
      color: var(--text-faint);
      margin-left: 4px;
    }

    #custom-sql-input {
      width: 100%;
      background: #0b1120;
      border: 1px solid var(--border-solid);
      border-radius: var(--radius-sm);
      color: #93c5fd;
      font-family: var(--mono);
      font-size: 0.875rem;
      line-height: 1.65;
      padding: 14px 16px;
      resize: vertical;
      outline: none;
      transition: border-color 0.2s, box-shadow 0.2s;
      min-height: 96px;
    }

    #custom-sql-input::placeholder { color: #2d4060; }

    #custom-sql-input:focus {
      border-color: var(--green);
      box-shadow: 0 0 0 3px rgba(52, 211, 153, 0.12),
                  0 0 16px rgba(52, 211, 153, 0.06);
    }

    .console-actions {
      display: flex;
      align-items: center;
      gap: 12px;
      margin-top: 12px;
    }
    #btn-optimize-live {
      display: inline-flex;
      align-items: center;
      gap: 7px;
      background: linear-gradient(135deg, #059669, #34d399);
      color: #021a10;
      font-family: var(--sans);
      font-size: 0.84rem;
      font-weight: 700;
      letter-spacing: 0.02em;
      border: none;
      border-radius: var(--radius-sm);
      padding: 9px 20px;
      cursor: pointer;
      outline: none;
      transition: opacity 0.18s, transform 0.15s, box-shadow 0.2s;
      box-shadow: 0 0 20px rgba(52, 211, 153, 0.2);
      user-select: none;
    }

    #btn-optimize-live:hover {
      opacity: 0.92;
      box-shadow: 0 0 28px rgba(52, 211, 153, 0.35);
    }

    #btn-optimize-live:active {
      transform: scale(0.97);
      box-shadow: 0 0 12px rgba(52, 211, 153, 0.2);
    }

    #btn-optimize-live:disabled {
      opacity: 0.45;
      cursor: not-allowed;
      transform: none;
    }
    #live-status {
      font-size: 0.76rem;
      color: var(--text-faint);
      min-width: 200px;
      font-style: italic;
      transition: color 0.2s;
    }

    #live-status.status-running { color: var(--amber); }
    #live-status.status-ok      { color: var(--green); }
    #live-status.status-error   { color: var(--rose);  }
    .query-banner {
      background: var(--surface);
      border: 1px solid var(--border);
      border-left: 3px solid var(--blue);
      border-radius: var(--radius);
      padding: 16px 20px;
      margin-bottom: 28px;
    }

    .query-badge {
      display: inline-block;
      font-size: 0.67rem; font-weight: 700;
      letter-spacing: 0.08em; text-transform: uppercase;
      color: var(--blue); background: var(--blue-dim);
      border: 1px solid rgba(96,165,250,0.2);
      border-radius: 4px; padding: 2px 8px; margin-bottom: 8px;
    }

    #query-display {
      font-family: var(--mono);
      font-size: 0.875rem;
      color: #93c5fd;
      white-space: pre-wrap;
      word-break: break-all;
      line-height: 1.65;
    }
    .metrics-grid {
      display: grid;
      grid-template-columns: repeat(3, 1fr);
      gap: 16px;
      margin-bottom: 32px;
    }

    @media (max-width: 780px) { .metrics-grid { grid-template-columns: 1fr; } }

    .metric-card {
      background: var(--surface);
      border: 1px solid var(--border);
      border-radius: var(--radius);
      padding: 22px 24px 20px;
      display: flex; flex-direction: column; gap: 6px;
      transition: border-color 0.2s, box-shadow 0.2s;
    }
    .metric-card:hover { border-color: #2d3f52; }
    .metric-card--amber { border-top: 2px solid var(--amber); }
    .metric-card--blue  { border-top: 2px solid var(--blue); }
    .metric-card--green { border-top: 2px solid var(--green); }

    .metric-label {
      font-size: 0.72rem; font-weight: 600;
      letter-spacing: 0.05em; text-transform: uppercase; color: var(--text-faint);
    }

    .metric-value-wrap { display: inline-block; padding: 4px 0; }

    .metric-value {
      font-size: 2.2rem; font-weight: 800;
      letter-spacing: -0.04em; font-family: var(--mono); line-height: 1.1;
    }

    .metric-value--amber { color: var(--amber); }
    .metric-value--blue  { color: var(--blue); }
    .metric-value--green { color: var(--green); }

    .metric-tint {
      display: inline-block; background: var(--amber-dim);
      border-radius: var(--radius-sm); padding: 2px 10px 2px 8px;
    }
    .metric-tint--blue  { background: var(--blue-dim); }
    .metric-tint--green { background: var(--green-dim); }

    .metric-sub { font-size: 0.74rem; color: var(--text-faint); line-height: 1.4; }
    .plans-grid {
      display: grid;
      grid-template-columns: repeat(3, 1fr);
      gap: 16px;
    }

    @media (max-width: 1060px) { .plans-grid { grid-template-columns: 1fr; } }

    .plan-card {
      background: var(--surface);
      border: 1px solid var(--border);
      border-radius: var(--radius);
      overflow: hidden; display: flex; flex-direction: column;
      transition: border-color 0.2s, box-shadow 0.2s;
    }

    .plan-card:hover { border-color: #2d3f52; }

    .plan-card--hero {
      border-top: 2px solid var(--green);
      box-shadow: 0 0 25px var(--green-glow), 0 0 60px rgba(52, 211, 153, 0.05);
    }

    .plan-card--hero:hover {
      box-shadow: 0 0 35px var(--green-glow), 0 0 80px rgba(52, 211, 153, 0.08);
      border-color: rgba(52, 211, 153, 0.5);
    }

    .plan-header {
      display: flex; align-items: center; gap: 10px;
      padding: 13px 16px;
      border-bottom: 1px solid var(--border);
      background: var(--surface-mid);
    }

    .plan-badge {
      font-size: 0.65rem; font-weight: 700;
      letter-spacing: 0.08em; text-transform: uppercase;
      border-radius: 4px; padding: 2px 7px; flex-shrink: 0;
    }

    .plan-badge--raw { background: var(--amber-dim); color: var(--amber); border: 1px solid rgba(251,191,36,0.25); }
    .plan-badge--rbo { background: var(--blue-dim);  color: var(--blue);  border: 1px solid rgba(96,165,250,0.25); }
    .plan-badge--cbo { background: var(--green-dim); color: var(--green); border: 1px solid rgba(52,211,153,0.25); }

    .plan-title { font-size: 0.85rem; font-weight: 600; color: var(--text-base); }

    .plan-subtitle {
      padding: 7px 16px; font-size: 0.72rem; font-style: italic;
      color: var(--text-faint);
      border-bottom: 1px solid rgba(30, 45, 61, 0.5);
      background: var(--surface-mid);
    }

    .plan-body { flex: 1; overflow: auto; }

    .plan-body pre {
      font-family: var(--mono);
      font-size: 0.78rem; line-height: 1.7; color: #8899aa;
      padding: 16px; margin: 0; background: #0b1120;
      white-space: pre; min-height: 180px;
    }
    .kw-scan    { color: #60a5fa; }
    .kw-filter  { color: #f87171; }
    .kw-project { color: #c084fc; }
    .kw-join    { color: #fbbf24; }
    .kw-bracket { color: #475569; }
    .kw-num     { color: var(--green); }
    .footer {
      margin-top: 60px; text-align: center;
      font-size: 0.72rem; color: var(--text-faint); letter-spacing: 0.02em;
    }
    .footer strong { color: var(--green); font-weight: 600; }

    @keyframes fadeUp {
      from { opacity: 0; transform: translateY(8px); }
      to   { opacity: 1; transform: translateY(0); }
    }
    .animate { animation: fadeUp 0.35s ease both; }
    .navbar, .console-panel, .query-banner, .metric-card, .plan-card { box-shadow: none; }
    :root {
      --bg: #f7f8fb;
      --surface: #ffffff;
      --surface-mid: #f1f5f9;
      --surface-hi: #eef2f7;
      --border: #d8dee8;
      --border-solid: #cbd5e1;
      --text-base: #172033;
      --text-muted: #64748b;
      --text-faint: #64748b;
      --green: #0f766e;
      --green-dim: #d9f3ee;
      --green-glow: transparent;
      --blue: #2563eb;
      --blue-dim: #dbeafe;
      --amber: #b45309;
      --amber-dim: #fef3c7;
      --rose: #dc2626;
      --radius: 8px;
    }

    body { background: var(--bg); color: var(--text-base); }
    .navbar {
      height: auto;
      min-height: 60px;
      padding: 12px 28px;
      background: #ffffff;
      backdrop-filter: none;
      flex-wrap: wrap;
    }
    .logo-icon {
      background: #111827;
      border-color: #111827;
      border-radius: 6px;
    }
    .logo-icon svg path, .logo-icon svg line { stroke: #ffffff; }
    .logo-text {
      color: #111827;
      background: none;
      -webkit-text-fill-color: currentColor;
      font-size: 1rem;
      letter-spacing: 0;
    }
    #profile-select, #custom-sql-input, .plan-body pre {
      background: #ffffff;
      color: #172033;
    }
    #custom-sql-input:focus {
      border-color: var(--blue);
      box-shadow: 0 0 0 3px rgba(37, 99, 235, 0.12);
    }
    #query-display { color: #172033; }
    #optimized-query-display { color: var(--green) !important; }
    #btn-optimize-live {
      background: #111827;
      color: #ffffff;
      box-shadow: none;
      letter-spacing: 0;
    }
    #btn-optimize-live:hover { box-shadow: none; opacity: 0.9; }
    .navbar-badge {
      color: #0f766e;
      background: #d9f3ee;
      border-color: #99d8cc;
    }
    .console-dot { display: none; }
    .section-row { margin-bottom: 12px; }
    .page { max-width: 1180px; padding-top: 28px; }
    .metrics-grid, .plans-grid { gap: 12px; }
    .metric-card { padding: 18px; }
    .metric-value { font-size: 1.65rem; letter-spacing: 0; }
    .plan-card--hero { box-shadow: none; }
  </style>
</head>
<body>
<nav class="navbar" id="navbar">
  <a class="logo" href="#" aria-label="SQL Query Optimiser">
    <div class="logo-icon">
      <svg width="16" height="16" viewBox="0 0 16 16" fill="none" aria-hidden="true">
        <path d="M8 2L14 13H2L8 2Z" stroke="#34d399" stroke-width="1.5" stroke-linejoin="round"/>
        <line x1="5" y1="9.5" x2="11" y2="9.5" stroke="#60a5fa" stroke-width="1.2" stroke-linecap="round"/>
      </svg>
    </div>
    <span class="logo-text">SQL Query Optimiser</span>
  </a>

  <div class="navbar-divider" aria-hidden="true"></div>

  <div class="selector-wrap">
    <span class="selector-label">Templates</span>
    <select id="profile-select" aria-label="Select preset query template">
    </select>
  </div>

  <span class="navbar-badge">Ready</span>
</nav>
<main class="page" id="main-content">
  <div class="section-row">
    <h2>SQL Console</h2><hr />
  </div>

  <div class="console-panel" id="console-panel">
    <div class="console-header">
      <span class="console-dot console-dot--red"  aria-hidden="true"></span>
      <span class="console-dot console-dot--amber" aria-hidden="true"></span>
      <span class="console-dot console-dot--green" aria-hidden="true"></span>
      <span class="console-title">SQL Query</span>
    </div>
    <textarea
      id="custom-sql-input"
      rows="4"
      spellcheck="false"
      autocomplete="off"
      placeholder="Type a SQL query here (e.g., SELECT * FROM products WHERE price < 100)..."
      aria-label="SQL query input"
    ></textarea>
    <div class="console-actions">
      <button id="btn-optimize-live" type="button">
        Optimise Query
      </button>
      <span id="live-status" role="status" aria-live="polite"></span>
    </div>
  </div>
  <div id="dashboard-content">
    <div class="section-row">
      <h2>Active Query</h2><hr />
    </div>
    <div class="query-banner" id="query-banner">
      <span class="query-badge">SQL Input</span>
      <pre id="query-display"></pre>
    </div>
    <div class="section-row">
      <h2>Optimised Query</h2><hr />
    </div>
    <div class="query-banner" style="border: 1px solid #34d399;" id="optimized-query-banner">
      <div id="badges-container" style="margin-bottom: 12px; display: flex; gap: 8px;">
      </div>
      <pre id="optimized-query-display" style="color: #34d399; font-weight: bold;"></pre>
    </div>
    <div class="section-row">
      <h2>Cost Metrics</h2><hr />
    </div>

    <div class="metrics-grid" id="metrics-grid">
      <div class="metric-card metric-card--amber">
        <span class="metric-label">Initial Cost</span>
        <div class="metric-value-wrap">
          <span class="metric-tint">
            <span class="metric-value metric-value--amber" id="val-initial"></span>
          </span>
        </div>
        <span class="metric-sub">Original logical plan (row-read units)</span>
      </div>
      <div class="metric-card metric-card--blue">
        <span class="metric-label">Optimised Cost</span>
        <div class="metric-value-wrap">
          <span class="metric-tint metric-tint--blue">
            <span class="metric-value metric-value--blue" id="val-final"></span>
          </span>
        </div>
        <span class="metric-sub">After RBO &amp; CBO passes (row-read units)</span>
      </div>
      <div class="metric-card metric-card--green">
        <span class="metric-label">Cost Reduction</span>
        <div class="metric-value-wrap">
          <span class="metric-tint metric-tint--green">
            <span class="metric-value metric-value--green" id="val-boost"></span>
          </span>
        </div>
        <span class="metric-sub">((initial &minus; final) / initial) &times; 100</span>
      </div>
    </div>
    <div class="section-row" style="margin-top:4px">
      <h2>Plan Comparison</h2><hr />
    </div>

    <div class="plans-grid" id="plans-grid">
      <div class="plan-card" id="card-raw">
        <div class="plan-header">
          <span class="plan-badge plan-badge--raw">Raw</span>
          <span class="plan-title">Original Logical Plan</span>
        </div>
        <p class="plan-subtitle">Step 0 &mdash; direct LogicalPlanner output, no rewrites applied</p>
        <div class="plan-body"><pre id="pre-initial"></pre></div>
      </div>
      <div class="plan-card" id="card-rbo">
        <div class="plan-header">
          <span class="plan-badge plan-badge--rbo">RBO</span>
          <span class="plan-title">Heuristic Plan</span>
        </div>
        <p class="plan-subtitle">Step 1 &mdash; predicate push-down (filters moved below joins)</p>
        <div class="plan-body"><pre id="pre-rbo"></pre></div>
      </div>
      <div class="plan-card plan-card--hero" id="card-cbo">
        <div class="plan-header">
          <span class="plan-badge plan-badge--cbo">CBO</span>
          <span class="plan-title">Optimal Physical Plan</span>
        </div>
        <p class="plan-subtitle">Step 2 &mdash; System&nbsp;R DP join reordering for global minimum cost</p>
        <div class="plan-body"><pre id="pre-cbo"></pre></div>
      </div>
    </div>

  </div>

  <footer class="footer" id="page-footer">
    <strong>SQL Query Optimiser</strong> &bull; Plan cost comparison
  </footer>
</main>

<script>
const queryDatabase = [
)";

    // ── Emit JS profile array ─────────────────────────────────────────────────
    for (std::size_t i = 0; i < profiles.size(); ++i) {
        const auto& p = profiles[i];

        double boost = 0.0;
        if (p.initialCost > 0) {
            boost = ((static_cast<double>(p.initialCost) - static_cast<double>(p.finalCost))
                     / static_cast<double>(p.initialCost)) * 100.0;
        }

        std::ostringstream boostStream;
        boostStream << std::fixed << std::setprecision(2) << boost;

        out << "  {\n"
            << "    title:       `" << jsEscape(p.title)                        << "`,\n"
            << "    sqlQuery:    `" << jsEscape(p.sqlQuery)                     << "`,\n"
            << "    initialPlan: `" << jsEscape(htmlEscape(p.initialPlan))      << "`,\n"
            << "    rboPlan:     `" << jsEscape(htmlEscape(p.rboPlan))          << "`,\n"
            << "    cboPlan:     `" << jsEscape(htmlEscape(p.cboPlan))          << "`,\n"
            << "    optimizedQuery: `" << jsEscape(p.optimizedQuery)            << "`,\n"
            << "    appliedPredicatePushdown: " << (p.appliedPredicatePushdown ? "true" : "false") << ",\n"
            << "    appliedJoinReordering: "    << (p.appliedJoinReordering ? "true" : "false")    << ",\n"
            << "    appliedOrUnionExpansion: "  << (p.appliedOrUnionExpansion ? "true" : "false")  << ",\n"
            << "    cardinalityReductionAchieved: " << (p.cardinalityReductionAchieved ? "true" : "false") << ",\n"
            << "    initialCost: " << p.initialCost                              << ",\n"
            << "    finalCost:   " << p.finalCost                                << ",\n"
            << "    boost:       \"" << boostStream.str()                        << "\"\n"
            << "  }" << (i + 1 < profiles.size() ? "," : "") << "\n";
    }

    out << R"(];

const kwRules = [
  { re: /\bLogicalScan\b/g,      cls: 'kw-scan'    },
  { re: /\bLogicalFilter\b/g,    cls: 'kw-filter'  },
  { re: /\bLogicalProject\b/g,   cls: 'kw-project' },
  { re: /\bLogicalJoin\b/g,      cls: 'kw-join'    },
  { re: /\bHashJoin\b/g,         cls: 'kw-join'    },
  { re: /\bNestedLoopJoin\b/g,   cls: 'kw-join'    },
  { re: /\bLogicalUnion\b/g,     cls: 'kw-join'    },
  { re: /([[\]])/g,              cls: 'kw-bracket' },
  { re: /\b(\d[\d,]*)\b/g,       cls: 'kw-num'     },
];

function highlight(html) {
  return kwRules.reduce(
    (acc, rule) => acc.replace(rule.re, m => `<span class="${rule.cls}">${m}</span>`),
    html
  );
}

function applyHighlight() {
  ['pre-initial', 'pre-rbo', 'pre-cbo'].forEach(id => {
    const el = document.getElementById(id);
    if (el) el.innerHTML = highlight(el.innerHTML);
  });
}

const content   = document.getElementById('dashboard-content');
const sqlInput  = document.getElementById('custom-sql-input');
const liveBtn   = document.getElementById('btn-optimize-live');
const liveStatus = document.getElementById('live-status');
const select    = document.getElementById('profile-select');
const FADE_MS   = 220;

function setStatus(msg, cls) {
  liveStatus.textContent = msg;
  liveStatus.className   = cls ? `status-${cls}` : '';
}

function renderData(d) {
  document.getElementById('query-display').textContent = d.sqlQuery;
  const fmt = n => typeof n === 'number' ? n.toLocaleString() : String(n);
  document.getElementById('val-initial').textContent = fmt(d.initialCost);
  document.getElementById('val-final').textContent   = fmt(d.finalCost ?? d.optimized_cost);
  document.getElementById('val-boost').textContent   =
    (d.boost ?? d.performance_boost ?? '-') + (String(d.boost ?? '').includes('%') ? '' : '%');
  document.getElementById('pre-initial').innerHTML = d.initialPlan ?? d.initial_plan ?? '';
  document.getElementById('pre-rbo').innerHTML     = d.rboPlan     ?? d.rbo_plan     ?? '';
  document.getElementById('pre-cbo').innerHTML     = d.cboPlan     ?? d.cbo_plan     ?? '';
  document.getElementById('optimized-query-display').textContent = d.optimizedQuery ?? d.optimized_query ?? '';
  const badges = document.getElementById('badges-container');
  badges.innerHTML = '';
  const pushdown = d.appliedPredicatePushdown ?? d.applied_predicate_pushdown;
  const reordering = d.appliedJoinReordering ?? d.applied_join_reordering;
  const reduction = d.cardinalityReductionAchieved ?? d.cardinality_reduction_achieved;
  const unionExp = d.appliedOrUnionExpansion ?? d.applied_or_union_expansion;
  
  if (pushdown)   badges.innerHTML += '<span class="plan-badge plan-badge--rbo" style="display:inline-block;">Predicate Pushdown</span>';
  if (reordering) badges.innerHTML += '<span class="plan-badge plan-badge--cbo" style="display:inline-block;">Join Reordered</span>';
  if (reduction)  badges.innerHTML += '<span class="plan-badge plan-badge--raw" style="display:inline-block; background-color:#0f766e; color:#fff;">Cost Reduced</span>';
  if (unionExp)   badges.innerHTML += '<span class="plan-badge" style="display:inline-block; background: var(--blue-dim); color: var(--blue); border-color: rgba(96,165,250,0.2);">OR-to-UNION Expanded</span>';

  applyHighlight();
}
function switchToData(dataObj) {
  content.classList.add('fading');
  setTimeout(() => {
    renderData(dataObj);
    content.classList.remove('fading');
  }, FADE_MS);
}
queryDatabase.forEach((profile, idx) => {
  const opt = document.createElement('option');
  opt.value = idx;
  opt.textContent = profile.title;
  select.appendChild(opt);
});
select.addEventListener('change', () => {
  const idx     = parseInt(select.value, 10);
  const profile = queryDatabase[idx];
  sqlInput.value = profile.sqlQuery;

  switchToData(profile);
});

liveBtn.addEventListener('click', async () => {
  const userQuery = sqlInput.value.trim();
  if (!userQuery) {
    setStatus('Enter a SQL query first.', 'error');
    return;
  }
  liveBtn.disabled = true;
  setStatus('Running optimiser...', 'running');

  try {
    const response = await fetch('http://localhost:8765/optimize', {
      method:  'POST',
      headers: { 'Content-Type': 'application/json' },
      body:    JSON.stringify({ query: userQuery })
    });

    if (!response.ok) {
      const errText = await response.text();
      throw new Error(`Server returned ${response.status}: ${errText}`);
    }

    const data = await response.json();
    const normalized = {
      sqlQuery:     userQuery,
      initialCost:  data.initial_cost,
      finalCost:    data.optimized_cost,
      boost:        (data.performance_boost ?? 0).toFixed(2),
      initialPlan:  escapeHtmlForPre(data.initial_plan ?? ''),
      rboPlan:      escapeHtmlForPre(data.rbo_plan     ?? ''),
      cboPlan:      escapeHtmlForPre(data.cbo_plan     ?? ''),
      optimizedQuery: data.optimized_query ?? '',
      appliedPredicatePushdown: data.applied_predicate_pushdown ?? false,
      appliedJoinReordering: data.applied_join_reordering ?? false,
      cardinalityReductionAchieved: data.cardinality_reduction_achieved ?? false,
      appliedOrUnionExpansion: data.applied_or_union_expansion ?? false,
    };

    switchToData(normalized);
    setStatus('Optimisation complete.', 'ok');

  } catch (err) {
    if (err instanceof TypeError) {
      setStatus('Start server.py on port 8765 to run custom queries.', 'error');
    } else {
      setStatus(err.message, 'error');
    }
  } finally {
    liveBtn.disabled = false;
  }
});
function escapeHtmlForPre(str) {
  return str
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

sqlInput.value = queryDatabase[0].sqlQuery;
renderData(queryDatabase[0]);
</script>

</body>
</html>
)";

    out.flush();
    if (!out.good()) {
        throw std::runtime_error(
            "HtmlReporter: write error for file '" + filename + "'");
    }
}

} // namespace qo
