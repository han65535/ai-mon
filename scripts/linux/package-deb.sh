#!/usr/bin/env bash
set -euo pipefail
umask 022
root=$(cd "$(dirname "$0")/../.." && pwd)
bin="$root/out/linux-release/ai-mon"
[[ -f "$bin" ]] || { echo 'Run scripts/linux/build.sh first' >&2; exit 1; }
[[ "$(dpkg --print-architecture)" == amd64 ]] || { echo 'This package supports amd64 only' >&2; exit 1; }
command -v dpkg-shlibdeps >/dev/null || { echo 'dpkg-dev is required' >&2; exit 1; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/debian" "$tmp/debian/ai-mon-linux/DEBIAN" "$tmp/debian/ai-mon-linux/usr/bin" "$tmp/debian/ai-mon-linux/usr/share/applications" "$tmp/debian/ai-mon-linux/usr/share/icons/hicolor/scalable/apps" "$tmp/debian/ai-mon-linux/usr/share/doc/ai-mon-linux"
printf 'Source: ai-mon-linux\nSection: utils\nPriority: optional\nMaintainer: AI Mon contributors\nStandards-Version: 4.6.2\n\nPackage: ai-mon-linux\nArchitecture: amd64\nDescription: Claude and Codex usage monitor\n' > "$tmp/debian/control"
install -m 755 "$bin" "$tmp/debian/ai-mon-linux/usr/bin/ai-mon"
install -m 644 "$root/resources/linux/ai-mon.desktop" "$tmp/debian/ai-mon-linux/usr/share/applications/ai-mon.desktop"
install -m 644 "$root/resources/linux/ai-mon.svg" "$tmp/debian/ai-mon-linux/usr/share/icons/hicolor/scalable/apps/ai-mon.svg"
install -m 644 "$root/LICENSE" "$tmp/debian/ai-mon-linux/usr/share/doc/ai-mon-linux/copyright"
install -m 644 "$root/resources/linux/FLTK-LICENSE.txt" "$tmp/debian/ai-mon-linux/usr/share/doc/ai-mon-linux/FLTK-LICENSE"
printf 'AI Mon is based in part on the work of the FLTK project (https://www.fltk.org).\n' > "$tmp/debian/ai-mon-linux/usr/share/doc/ai-mon-linux/FLTK-NOTICE"
install -m 644 "$root/third_party/cjson/LICENSE" "$tmp/debian/ai-mon-linux/usr/share/doc/ai-mon-linux/cjson-LICENSE"
deps=$(cd "$tmp" && dpkg-shlibdeps -O -e debian/ai-mon-linux/usr/bin/ai-mon | sed 's/^shlibs:Depends=//')
[[ -n "$deps" ]] || { echo 'Dependency discovery failed' >&2; exit 1; }
[[ "$deps" == *"libsqlite3-0"* ]] || { echo 'SQLite dependency missing' >&2; exit 1; }
size=$(du -sk "$tmp/debian/ai-mon-linux/usr" | cut -f1)
cat > "$tmp/debian/ai-mon-linux/DEBIAN/control" <<CONTROL
Package: ai-mon-linux
Version: 0.4.1~beta1
Section: utils
Priority: optional
Architecture: amd64
Maintainer: AI Mon contributors
Installed-Size: $size
Depends: $deps
Suggests: fonts-noto-cjk
Description: Local Claude and Codex usage monitor
 A native Linux beta for local token usage and reported account limits.
CONTROL
mkdir -p "$root/out/linux-release"
dpkg-deb --build --root-owner-group "$tmp/debian/ai-mon-linux" "$root/out/linux-release/ai-mon-linux_0.4.1~beta1_amd64.deb"
