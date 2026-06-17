#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
make -j"$(nproc)" 2>&1
echo "Build complete: build/fermi"
