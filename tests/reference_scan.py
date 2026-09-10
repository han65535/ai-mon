"""Optional real-log cross-check; emits aggregate numbers, never conversation text.

Uses Python's JSON parser independently of the C++ parser. It checks Codex's
per-request last_token_usage against the app's cumulative-counter deltas.
Python is not required for the product, build, or normal test suite.
"""
import datetime as dt
import json
import os
from pathlib import Path

today = dt.datetime.now().astimezone().date()
roots = [Path(os.environ.get("CLAUDE_CONFIG_DIR", Path.home() / ".claude")) / "projects",
         Path(os.environ.get("CODEX_HOME", Path.home() / ".codex")) / "sessions"]
claude = {}
codex = {}
large = 0
invalid = 0
for provider, root in enumerate(roots):
    for path in root.rglob("*.jsonl"):
        session = str(path)
        previous = None
        epoch = 0
        with path.open("rb") as stream:
            for raw in stream:
                if not raw.endswith(b"\n"):
                    continue
                if len(raw) > 2 * 1024 * 1024:
                    large += 1
                try:
                    event = json.loads(raw)
                except (ValueError, UnicodeError):
                    invalid += 1
                    continue
                if provider == 0 and event.get("type") == "assistant":
                    message = event.get("message") or {}
                    usage = message.get("usage")
                    if not usage or not message.get("id"):
                        continue
                    time = dt.datetime.fromisoformat(event["timestamp"].replace("Z", "+00:00"))
                    key = (event.get("sessionId", str(path)), event.get("requestId", ""), message["id"])
                    old = claude.get(key)
                    if old is None:
                        claude[key] = (time, time, usage)
                    elif time >= old[1]:
                        claude[key] = (min(time, old[0]), time, usage)
                elif provider == 1:
                    payload = event.get("payload") or {}
                    if event.get("type") == "session_meta":
                        session = payload.get("id", session)
                    elif event.get("type") == "event_msg" and payload.get("type") == "token_count":
                        info = payload.get("info") or {}
                        total = info.get("total_token_usage")
                        last = info.get("last_token_usage")
                        if not total or not last:
                            continue
                        counts = tuple(total.get(k, 0) for k in ("input_tokens", "output_tokens", "cached_input_tokens", "cache_write_input_tokens"))
                        if counts == previous:
                            continue
                        if previous and any(a < b for a, b in zip(counts, previous)):
                            epoch += 1
                        previous = counts
                        time = dt.datetime.fromisoformat(event["timestamp"].replace("Z", "+00:00"))
                        codex.setdefault((session, epoch, counts), (time, last))

totals = [{"i": 0, "o": 0, "r": 0, "w": 0, "events": 0} for _ in range(2)]
for first, _, usage in claude.values():
    if first.astimezone().date() == today:
        row = totals[0]
        row["r"] += usage.get("cache_read_input_tokens", 0)
        row["w"] += usage.get("cache_creation_input_tokens", 0)
        row["i"] += usage["input_tokens"] + usage.get("cache_read_input_tokens", 0) + usage.get("cache_creation_input_tokens", 0)
        row["o"] += usage["output_tokens"]
        row["events"] += 1
for time, usage in codex.values():
    if time.astimezone().date() == today:
        row = totals[1]
        for output, key in [("i", "input_tokens"), ("o", "output_tokens"), ("r", "cached_input_tokens"), ("w", "cache_write_input_tokens")]:
            row[output] += usage.get(key, 0)
        row["events"] += 1
print(json.dumps({"providers": totals, "large_records": large, "invalid_json": invalid}))
