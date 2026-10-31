#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
pkg-config --exists sqlite3 || { echo 'SQLite development files are required' >&2; exit 1; }
cc -std=c99 -DCJSON_NESTING_LIMIT=64 -I"$root/third_party/cjson" -c "$root/third_party/cjson/cJSON.c" -o "$tmp/cjson.o"
c++ -std=c++17 -Wall -Wextra -Werror -I"$root/src" -I"$root/third_party/cjson" \
    "$root/src/linux/platform.cpp" "$root/src/usage.cpp" "$root/src/quota.cpp" "$root/src/history.cpp" \
    "$root/tests/linux/history_test.cpp" "$tmp/cjson.o" -lsqlite3 -o "$tmp/test"
TZ=UTC "$tmp/test" "$tmp/data"
