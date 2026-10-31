#!/usr/bin/env bash
set -euo pipefail
# Run on an isolated X server, never the user's desktop.
: "${AIMON_PROTOTYPE:?Set AIMON_PROTOTYPE to the built X11 prototype}"
tmp=$(mktemp -d)
export HOME="$tmp" XDG_STATE_HOME="$tmp/state" DISPLAY=:92
cleanup() { kill "${app:-}" "${tray:-}" "${wm:-}" "${xvfb:-}" 2>/dev/null || true; wait 2>/dev/null || true; rm -rf "$tmp"; }
trap cleanup EXIT
trap 'cat "$tmp/app.log" >&2 2>/dev/null || true' ERR
Xvfb "$DISPLAY" -screen 0 1024x768x24 >"$tmp/xvfb.log" 2>&1 & xvfb=$!
for i in {1..50}; do xdpyinfo >/dev/null 2>&1 && break; sleep .1; done
xdpyinfo >/dev/null
openbox >"$tmp/wm.log" 2>&1 & wm=$!
stalonetray --geometry 1x1+0+0 >"$tmp/tray.log" 2>&1 & tray=$!
sleep 1
"$AIMON_PROTOTYPE" >"$tmp/app.log" 2>&1 & app=$!
for i in {1..50}; do detail=$(xdotool search --name '^AI Mon - GTK X11 prototype$' 2>/dev/null | head -1 || true); [[ -n "$detail" ]] && break; sleep .1; done
[[ -n "${detail:-}" ]]
xdotool mousemove --window "$detail" 70 130 click 1
for i in {1..50}; do mini=$(xdotool search --name '^AI Mon GTK mini$' 2>/dev/null | head -1 || true); [[ -n "$mini" ]] && break; sleep .1; done
[[ -n "${mini:-}" ]]
sleep 1
xprop -id "$mini" _NET_WM_WINDOW_OPACITY | grep -q '3865470566'
xdotool mousemove --window "$mini" 20 45 mousedown 1 mousemove_relative --sync -- 80 50 mouseup 1
sleep .3
[[ -s "$XDG_STATE_HOME/ai-mon-gtk-prototype/position" ]]
kill -0 "$app"
# Wait for XEmbed and the periodic embedded-state check.
sleep 3
icon=$(xdotool search --onlyvisible --name '^AI Mon GTK prototype tray$' | head -1)
[[ -n "$icon" ]]
xdotool mousemove --window "$icon" 10 10 click 3
sleep .5
menu=$(xdotool search --onlyvisible --name '^AI Mon GTK tray menu$' | head -1)
[[ -n "$menu" ]]
xdotool mousemove --window "$menu" 30 17 click 1
sleep .3
if xdotool search --onlyvisible --name '^AI Mon GTK tray menu$' >/dev/null; then echo 'FAIL: tray menu did not close'; exit 1; fi
printf '%s\n' 'PASS: tray menu opens and selection closes it'
xdotool windowactivate --sync "$detail" key alt+F4
sleep .3
kill -0 "$app"
xdotool mousemove --window "$icon" 10 10 click 1
sleep .3
xdotool search --onlyvisible --name '^AI Mon - GTK X11 prototype$' >/dev/null
printf '%s\n' 'PASS: tray icon embedded and click reopens detail'
# Losing the tray must not strand or terminate the app.
kill "$tray"; wait "$tray" 2>/dev/null || true; tray=''
sleep 3
kill -0 "$app"
xdotool search --onlyvisible --name '^AI Mon - GTK X11 prototype$' >/dev/null
printf '%s\n' 'PASS: tray loss leaves detail accessible'
# Restart and check the persisted coordinates are applied to the new mini window.
saved=$(cat "$XDG_STATE_HOME/ai-mon-gtk-prototype/position")
kill "$app"; wait "$app" 2>/dev/null || true; app=''
"$AIMON_PROTOTYPE" >"$tmp/app2.log" 2>&1 & app=$!
for i in {1..50}; do detail=$(xdotool search --onlyvisible --name '^AI Mon - GTK X11 prototype$' 2>/dev/null | head -1 || true); [[ -n "$detail" ]] && break; sleep .1; done
xdotool mousemove --window "$detail" 70 130 click 1
sleep .5
mini=$(xdotool search --onlyvisible --name '^AI Mon GTK mini$' | head -1)
eval "$(xdotool getwindowgeometry --shell "$mini")"
read -r saved_x saved_y <<< "$saved"
# Window decorations may add a small offset to the client geometry.
(( X >= saved_x && X - saved_x <= 10 && Y >= saved_y && Y - saved_y <= 40 ))
printf '%s\n' 'PASS: mini position restored after restart'
xdotool mousemove --window "$detail" 70 180 click 1
for i in {1..30}; do kill -0 "$app" 2>/dev/null || break; sleep .1; done
if kill -0 "$app" 2>/dev/null; then echo 'FAIL: exit button did not stop app'; exit 1; fi
wait "$app"; app=''
printf '%s\n' 'PASS: exit button works without a tray'
printf '%s\n' 'PASS: X11 detail, mini window, opacity property and drag state' 
