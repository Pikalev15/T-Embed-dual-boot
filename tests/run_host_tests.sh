#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
TEST_BINARY="$(mktemp "${TMPDIR:-/tmp}/t-embed-host-tests.XXXXXX")"
trap 'rm -f "${TEST_BINARY}"' EXIT

"${CC:-cc}" \
    -std=c11 \
    -Wall \
    -Wextra \
    -Werror \
    -pedantic \
    -I"${REPO_ROOT}/buddy_firmware/main" \
    "${SCRIPT_DIR}/test_invariant_feat_capture_hs.c" \
    -o "${TEST_BINARY}"

"${TEST_BINARY}"
echo "Host tests passed."
