#!/bin/sh
# Build WaveForge with the plain Makefile from the repository root.
set -e

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

make clean
make all selftest
