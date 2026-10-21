// Polymarket large-trade watcher for a watchlist of CS2 teams.
// Polls the public Data API, prints/pushes an alert for every trade >= MIN_USD
// on a market whose title/outcome mentions a watched team, and marks trades
// >= FLAG_USD as "flagged". Read-only: it never places orders.
//
//   node watcher.js
//   MIN_USD=1000 FLAG_USD=5000 DISCORD_WEBHOOK=https://... node watcher.js

const fs = require("fs");
const path = require("path");

const MIN_USD = Number(process.env.MIN_USD || 1000);
const FLAG_USD = Number(process.env.FLAG_USD || 5000);
const POLL_MS = Number(process.env.POLL_MS || 15000);
const WEBHOOK = process.env.DISCORD_WEBHOOK || "";
const LOG_FILE = process.env.LOG_FILE || path.join(__dirname, "alerts.jsonl");
const API = "https://data-api.polymarket.com/trades";

const { teams } = JSON.parse(fs.readFileSync(path.join(__dirname, "teams.json"), "utf8"));
const escape = (s) => s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
// Word-boundary match so short names like "Misa" don't hit "misaligned".
const patterns = teams.map((t) => [t, new RegExp(`\\b${escape(t)}\\b`, "i")]);

const seen = new Set();

function matchTeams(trade) {
  const haystack = [trade.title, trade.outcome, trade.slug, trade.eventSlug].filter(Boolean).join(" | ");
  return patterns.filter(([, re]) => re.test(haystack)).map(([t]) => t);
}

async function fetchLargeTrades() {
  const url = `${API}?limit=500&takerOnly=true&filterType=CASH&filterAmount=${MIN_USD}`;
  const res = await fetch(url, { headers: { accept: "application/json" } });
  if (!res.ok) throw new Error(`Data API ${res.status}`);
  return res.json();
}

function format(trade, matched, usd) {
  const flag = usd >= FLAG_USD ? "🚩 FLAGGED" : "ℹ️ large";
  const when = new Date(trade.timestamp * 1000).toISOString();
  const who = trade.pseudonym || trade.name || trade.proxyWallet;
  return [
    `${flag} $${usd.toFixed(0)} ${trade.side} "${trade.outcome}" @ ${Number(trade.price).toFixed(3)}`,
    `  market: ${trade.title}`,
    `  teams:  ${matched.join(", ")}`,
    `  trader: ${who} (${trade.proxyWallet})`,
    `  time:   ${when}`,
    `  link:   https://polymarket.com/event/${trade.eventSlug || trade.slug}`,
    `  tx:     https://polygonscan.com/tx/${trade.transactionHash}`,
  ].join("\n");
}

async function notify(text) {
  console.log(text + "\n");
  if (!WEBHOOK) return;
  try {
    await fetch(WEBHOOK, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ content: "```\n" + text.slice(0, 1900) + "\n```" }),
    });
  } catch (err) {
    console.error("webhook failed:", err.message);
  }
}

async function tick(firstRun) {
  const trades = await fetchLargeTrades();
  for (const trade of trades.reverse()) {
    const id = `${trade.transactionHash}:${trade.asset}:${trade.proxyWallet}`;
    if (seen.has(id)) continue;
    seen.add(id);

    const usd = Number(trade.size) * Number(trade.price);
    if (usd < MIN_USD) continue;
    const matched = matchTeams(trade);
    if (!matched.length) continue;

    fs.appendFileSync(LOG_FILE, JSON.stringify({ ...trade, usd, matched }) + "\n");
    await notify((firstRun ? "[backfill] " : "") + format(trade, matched, usd));
  }
  if (seen.size > 50000) seen.clear();
}

(async () => {
  console.log(`Watching ${teams.length} teams | alert >= $${MIN_USD} | flag >= $${FLAG_USD} | every ${POLL_MS / 1000}s`);
  let first = true;
  for (;;) {
    try {
      await tick(first);
      first = false;
    } catch (err) {
      console.error(new Date().toISOString(), err.message);
    }
    await new Promise((r) => setTimeout(r, POLL_MS));
  }
})();
