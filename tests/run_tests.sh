#!/usr/bin/env bash
# run_tests.sh — test Fermi compiler pipeline through LLVM IR generation.
# Pass: fermi exits 0, no [parse error]/[error] lines in stderr.
# Usage: ./run_tests.sh [--clang] to also compile+run binaries if clang is available.
set -uo pipefail

FERMI="$(cd "$(dirname "$0")/.." && pwd)/fermi"
TESTS_DIR="$(cd "$(dirname "$0")" && pwd)"
USE_CLANG="${1:-}"
PASS=0; FAIL=0; SKIP=0

for fe in "$TESTS_DIR"/t*.fe "$TESTS_DIR"/fermi_file.fe; do
    [ -f "$fe" ] || continue
    name="$(basename "$fe" .fe)"
    stderr_out="$(2>&1 "$FERMI" "$fe" --llvm >/dev/null)"
    rc=$?
    has_errors=$(echo "$stderr_out" | grep -cE '^\[(parse |type )?error\]' || true)
    if [ "$rc" -eq 0 ] && [ "$has_errors" -eq 0 ]; then
        if [ "$USE_CLANG" = "--clang" ] && command -v clang >/dev/null 2>&1; then
            bin="/tmp/fermi_test_$$_${name}"
            if "$FERMI" "$fe" -o "$bin" 2>/dev/null && "$bin" >/dev/null 2>&1; then
                echo "  PASS  $name (compiled + ran)"
                PASS=$((PASS+1))
            else
                echo "  FAIL  $name (IR ok, binary failed)"
                FAIL=$((FAIL+1))
            fi
            rm -f "$bin"
        else
            echo "  PASS  $name"
            PASS=$((PASS+1))
        fi
    else
        echo "  FAIL  $name"
        if [ -n "$stderr_out" ]; then echo "        $stderr_out" | head -3; fi
        FAIL=$((FAIL+1))
    fi
done

echo ""
echo "Results: $PASS passed, $FAIL failed, $SKIP skipped"
[ "$FAIL" -eq 0 ]
