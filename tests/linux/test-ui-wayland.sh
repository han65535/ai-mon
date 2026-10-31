#!/usr/bin/env bash
set -euo pipefail
: "${AIMON_PROTOTYPE:?Set AIMON_PROTOTYPE to the built Wayland prototype}"
: "${AIMON_FAKE_WATCHER:?Set AIMON_FAKE_WATCHER to fake-watcher.py}"
: "${AIMON_MOCK_CLI:?Set AIMON_MOCK_CLI to tests/linux/mock_cli.py}"
: "${AIMON_ICON_CHECK:?Set AIMON_ICON_CHECK to tests/linux/check-tray-icon.py}"
[[ -n "${DBUS_SESSION_BUS_ADDRESS:-}" ]] || { echo 'Run under dbus-run-session' >&2; exit 1; }
tmp=$(mktemp -d)
export LC_ALL=C.UTF-8 HOME="$tmp" XDG_CONFIG_HOME="$tmp/config" XDG_STATE_HOME="$tmp/state" XDG_RUNTIME_DIR="$tmp/runtime" DISPLAY=:99 WAYLAND_DISPLAY=aimon-test FLTK_BACKEND=wayland
mkdir -m 700 "$XDG_RUNTIME_DIR"
mkdir -p "$tmp/bin"
for name in claude codex; do cp "$AIMON_MOCK_CLI" "$tmp/bin/$name"; chmod +x "$tmp/bin/$name"; done
export PATH="$tmp/bin:$PATH"
cleanup() { kill "${app:-}" "${watch:-}" "${weston:-}" "${xvfb:-}" 2>/dev/null || true; wait 2>/dev/null || true; rm -rf "$tmp"; }
trap cleanup EXIT
trap 'tail -n 12 "$tmp/app.log" "$tmp/weston.log" >&2 2>/dev/null || true' ERR
mkdir -p "$tmp/한글 폴더/사용량"
"$AIMON_PROTOTYPE" --path-roundtrip "$tmp/한글 폴더/사용량"
[[ "$(cat "$XDG_CONFIG_HOME/ai-mon/claude-root")" == "$tmp/한글 폴더/사용량" ]]
mkdir -p "$tmp/.codex/sessions"
now=$(date -u +%Y-%m-%dT%H:%M:%SZ)
printf '{"type":"assistant","sessionId":"test","timestamp":"%s","message":{"id":"a","model":"claude-fixture","usage":{"input_tokens":12,"output_tokens":3}}}\n' "$now" > "$tmp/한글 폴더/사용량/test.jsonl"
printf '{"type":"session_meta","payload":{"id":"test-session","originator":"codex_vscode"}}\n{"type":"turn_context","payload":{"model":"codex-fixture"}}\n{"type":"event_msg","timestamp":"%s","payload":{"type":"token_count","info":{"total_token_usage":{"input_tokens":20,"output_tokens":4,"cached_input_tokens":5}}}}\n' "$now" > "$tmp/.codex/sessions/test.jsonl"
printf '%s\n' 'PASS: Korean UTF-8 path roundtrip'
Xvfb "$DISPLAY" -screen 0 1024x768x24 >"$tmp/xvfb.log" 2>&1 & xvfb=$!
for i in {1..50}; do xdpyinfo >/dev/null 2>&1 && break; sleep .1; done
weston --backend=x11-backend.so --socket="$WAYLAND_DISPLAY" --idle-time=0 >"$tmp/weston.log" 2>&1 & weston=$!
for i in {1..80}; do [[ -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]] && break; sleep .1; done
[[ -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]]
python3 "$AIMON_FAKE_WATCHER" "$tmp/item" >"$tmp/watch.log" 2>&1 & watch=$!
sleep 1
WAYLAND_DEBUG=1 "$AIMON_PROTOTYPE" >"$tmp/app.log" 2>&1 & app=$!
for i in {1..100}; do [[ -s "$tmp/item" ]] && break; sleep .1; done
[[ -s "$tmp/item" ]]
if [[ -n "${AIMON_SCREENSHOT:-}" ]]; then scrot "$AIMON_SCREENSHOT"; fi
for i in {1..80}; do [[ -s "$tmp/state/ai-mon/state.json" ]] && break; sleep .1; done
python3 - "$tmp/state/ai-mon/state.json" <<'PYTEST'
import json, sys
with open(sys.argv[1], encoding='utf-8') as f:
    data = json.load(f)
assert sorted((event['p'], event['i'], event['o']) for event in data['events']) == [(0, 12, 3), (1, 20, 4)]
PYTEST
for i in {1..80}; do
    if python3 - "$tmp/state/ai-mon/state.json" <<'PYQUOTA' 2>/dev/null
import json, sys
with open(sys.argv[1], encoding='utf-8') as f:
    q = json.load(f)['quotas']
assert q[0]['latest']['short']['remaining'] == 6000 and q[1]['latest']['short']['remaining'] == 7500
PYQUOTA
    then break; fi
    sleep .1
done
python3 - "$tmp/state/ai-mon/state.json" <<'PYQUOTA'
import json, sys
with open(sys.argv[1], encoding='utf-8') as f:
    q = json.load(f)['quotas']
assert q[0]['latest']['short']['remaining'] == 6000 and q[1]['latest']['short']['remaining'] == 7500
PYQUOTA
printf '%s\n' 'PASS: Claude and Codex fixture logs and mock quota responses collected by UI process'
for i in {1..80}; do
    if python3 - "$tmp/state/ai-mon/history.sqlite3" <<'PYHISTORY' 2>/dev/null
import sqlite3, sys
con = sqlite3.connect('file:' + sys.argv[1] + '?mode=ro', uri=True)
rows = con.execute('SELECT provider,model,source,input,output FROM events ORDER BY provider').fetchall()
assert rows == [(0, 'claude-fixture', '', 12, 3), (1, 'codex-fixture', 'VS Code', 20, 4)]
PYHISTORY
    then break; fi
    sleep .1
done
python3 - "$tmp/state/ai-mon/history.sqlite3" <<'PYHISTORY'
import sqlite3, sys
con = sqlite3.connect('file:' + sys.argv[1] + '?mode=ro', uri=True)
assert con.execute('SELECT provider,model,source,input,output FROM events ORDER BY provider').fetchall() == [(0, 'claude-fixture', '', 12, 3), (1, 'codex-fixture', 'VS Code', 20, 4)]
PYHISTORY
printf '%s\n' 'PASS: persistent model and source history'
item=$(cat "$tmp/item")
printf '%s\n' "Registered: $item"
python3 "$AIMON_ICON_CHECK" "$item"
grep -q 'xdg_toplevel.*set_title.*AI Mon' "$tmp/app.log"
props=$(gdbus call --session --dest "$item" --object-path /StatusNotifierItem --method org.freedesktop.DBus.Properties.GetAll org.kde.StatusNotifierItem)
[[ "$props" == *"ai-mon-linux"* && "$props" == *"IconPixmap"* ]]
layout=$(gdbus call --session --dest "$item" --object-path /Menu --method com.canonical.dbusmenu.GetLayout -- 0 -1 '[]')
[[ "$layout" == *'미니 창'* && "$layout" == *'종료'* ]]
gdbus call --session --dest "$item" --object-path /Menu --method com.canonical.dbusmenu.Event 4 clicked '<0>' 0 >/dev/null
for i in {1..50}; do grep -q 'xdg_toplevel.*set_title.*AI Mon - 기록' "$tmp/app.log" && break; sleep .1; done
grep -q 'xdg_toplevel.*set_title.*AI Mon - 기록' "$tmp/app.log"
if [[ -n "${AIMON_HISTORY_SCREENSHOT:-}" ]]; then sleep 2; scrot "$AIMON_HISTORY_SCREENSHOT"; fi
printf '%s\n' 'PASS: history window opens from tray'
gdbus call --session --dest "$item" --object-path /Menu --method com.canonical.dbusmenu.Event 2 clicked '<0>' 0 >/dev/null
for i in {1..50}; do grep -q 'xdg_toplevel.*set_title.*AI Mon mini' "$tmp/app.log" && break; sleep .1; done
grep -q 'xdg_toplevel.*set_title.*AI Mon mini' "$tmp/app.log"
printf '%s\n' 'PASS: native Wayland windows, tray properties, menu layout and mini action'
for i in {1..50}; do grep -q 'tray host available' "$tmp/app.log" && break; sleep .1; done
grep -q 'tray host available' "$tmp/app.log"
kill "$watch"; wait "$watch" 2>/dev/null || true; watch=''
for i in {1..60}; do grep -q 'tray host unavailable' "$tmp/app.log" && break; sleep .1; done
grep -q 'tray host unavailable' "$tmp/app.log"
kill -0 "$app"
rm -f "$tmp/item"
python3 "$AIMON_FAKE_WATCHER" "$tmp/item" >"$tmp/watch2.log" 2>&1 & watch=$!
for i in {1..80}; do [[ -s "$tmp/item" ]] && break; sleep .1; done
[[ "$(cat "$tmp/item")" == "$item" ]]
printf '%s\n' 'PASS: tray watcher loss and re-registration'
gdbus call --session --dest "$item" --object-path /Menu --method com.canonical.dbusmenu.Event 3 clicked '<0>' 0 >/dev/null
for i in {1..50}; do kill -0 "$app" 2>/dev/null || break; sleep .1; done
if kill -0 "$app" 2>/dev/null; then echo 'FAIL: tray exit did not stop app'; exit 1; fi
wait "$app"; app=''
printf '%s\n' 'PASS: tray menu exit'
