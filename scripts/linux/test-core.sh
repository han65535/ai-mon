#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=c99 -DCJSON_NESTING_LIMIT=64 -I"$root/third_party/cjson" -c "$root/third_party/cjson/cJSON.c" -o "$tmp/cjson.o"
c++ -std=c++17 -Wall -Wextra -Werror -I"$root/src" -I"$root/third_party/cjson" \
    "$root/src/linux/platform.cpp" "$root/src/usage.cpp" "$root/src/quota.cpp" "$root/src/collector.cpp" \
    "$root/tests/linux/collector_test.cpp" "$tmp/cjson.o" -o "$tmp/test"
"$tmp/test" "$tmp/data"

mkdir -p "$tmp/mocks" "$tmp/quota"
for name in codex claude claude-unavailable hang bad; do cp "$root/tests/linux/mock_cli.py" "$tmp/mocks/$name"; chmod +x "$tmp/mocks/$name"; done
c++ -std=c++17 -pthread -Wall -Wextra -Werror -I"$root/src" -I"$root/third_party/cjson" \
    "$root/src/linux/platform.cpp" "$root/src/quota.cpp" "$root/src/linux/probes.cpp" \
    "$root/tests/linux/probes_test.cpp" "$tmp/cjson.o" -o "$tmp/probes-test"
"$tmp/probes-test" "$tmp/quota" "$tmp/mocks"
