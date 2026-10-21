# polymarket-watcher

Alerts on large Polymarket trades (default >= $1,000) in markets that mention a
team in `teams.json`. Trades >= `FLAG_USD` (default $5,000) are marked 🚩.
Every hit is also appended to `alerts.jsonl`. **Read-only — it does not trade.**

```
cd polymarket-watcher
node watcher.js                                   # Node 18+, no dependencies
MIN_USD=1000 FLAG_USD=5000 POLL_MS=15000 DISCORD_WEBHOOK=https://discord.com/api/webhooks/... node watcher.js
```

Edit `teams.json` to add/remove teams. The list comes from unverified social
media posts; a big bet on one of these teams is not proof of anything.
