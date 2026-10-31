#!/usr/bin/env bash
set -euo pipefail
: "${AIMON_PROTOTYPE:?Set AIMON_PROTOTYPE to the built prototype}"
: "${AIMON_KIND:?Set AIMON_KIND to fltk or gtk}"
tmp=$(mktemp -d)
export HOME="$tmp" XDG_STATE_HOME="$tmp/state" DISPLAY=:98
cleanup() { kill "${app:-}" "${compositor:-}" "${wm:-}" "${xvfb:-}" 2>/dev/null || true; wait 2>/dev/null || true; rm -rf "$tmp"; }
trap cleanup EXIT
Xvfb "$DISPLAY" -screen 0 1024x768x24 >"$tmp/xvfb.log" 2>&1 & xvfb=$!
for i in {1..50}; do xdpyinfo >/dev/null 2>&1 && break; sleep .1; done
openbox >"$tmp/wm.log" 2>&1 & wm=$!
xcompmgr -n >"$tmp/compositor.log" 2>&1 & compositor=$!
"$AIMON_PROTOTYPE" >"$tmp/app.log" 2>&1 & app=$!
if [[ "$AIMON_KIND" == fltk ]]; then title='^AI Mon - X11 prototype$'; mini_title='^AI Mon mini$'; button_y=185; else title='^AI Mon - GTK X11 prototype$'; mini_title='^AI Mon GTK mini$'; button_y=130; fi
for i in {1..80}; do detail=$(xdotool search --onlyvisible --name "$title" 2>/dev/null | head -1 || true); [[ -n "$detail" ]] && break; sleep .1; done
[[ -n "${detail:-}" ]]
sleep 1
xdotool mousemove --window "$detail" 70 "$button_y" click 1
for i in {1..80}; do mini=$(xdotool search --onlyvisible --name "$mini_title" 2>/dev/null | head -1 || true); [[ -n "$mini" ]] && break; sleep .1; done
[[ -n "${mini:-}" ]]
sleep 2
eval "$(xdotool getwindowgeometry --shell "$mini")"
scrot "$tmp/translucent.png"
xprop -id "$mini" -f _NET_WM_WINDOW_OPACITY 32c -set _NET_WM_WINDOW_OPACITY 4294967295
sleep 2
scrot "$tmp/opaque.png"
python3 - "$tmp/translucent.png" "$tmp/opaque.png" "$X" "$Y" "$WIDTH" "$HEIGHT" <<'PY'
from PIL import Image, ImageChops
import sys
a,b = [Image.open(p).convert('RGB') for p in sys.argv[1:3]]
x,y,w,h = map(int,sys.argv[3:])
a,b = a.crop((x,y,x+w,y+h)),b.crop((x,y,x+w,y+h))
d = ImageChops.difference(a,b)
changed = sum(1 for pixel in d.getdata() if max(pixel) >= 3)
if changed < 100:
    raise SystemExit(f'FAIL: only {changed} changed pixels in mini window')
print(f'PASS: {changed} mini-window pixels changed when opacity changed')
PY
