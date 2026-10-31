#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
fltk_config=${FLTK_CONFIG:-fltk-config}
command -v "$fltk_config" >/dev/null || { echo 'FLTK 1.4 fltk-config is required' >&2; exit 1; }
[[ "$("$fltk_config" --api-version)" == 1.4 ]] || { echo 'FLTK 1.4 with X11 and Wayland support is required' >&2; exit 1; }
pkg-config --exists gio-2.0 || { echo 'GIO development files are required' >&2; exit 1; }
out="$root/out/linux-release"
mkdir -p "$out"
cc -std=c99 -DCJSON_NESTING_LIMIT=64 -I"$root/third_party/cjson" -c "$root/third_party/cjson/cJSON.c" -o "$out/cjson.o"
read -r -a fltk_cflags <<< "$("$fltk_config" --cxxflags)"
read -r -a fltk_libs <<< "$("$fltk_config" --ldflags)"
read -r -a gio_cflags <<< "$(pkg-config --cflags gio-2.0)"
read -r -a gio_libs <<< "$(pkg-config --libs gio-2.0)"
c++ -std=c++17 -Wall -Wextra -Werror -Os -s -pthread "${fltk_cflags[@]}" "${gio_cflags[@]}" \
  -I"$root/src" -I"$root/third_party/cjson" \
  "$root/src/linux/main.cpp" "$root/src/linux/platform.cpp" "$root/src/linux/probes.cpp" \
  "$root/src/usage.cpp" "$root/src/quota.cpp" "$root/src/collector.cpp" "$out/cjson.o" \
  -o "$out/ai-mon" "${fltk_libs[@]}" "${gio_libs[@]}"
printf 'Built %s\n' "$out/ai-mon"
