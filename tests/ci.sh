#!/usr/bin/env bash
# CI entry point: builds the compiler and runs the full test suite.
# Exits non-zero on any build or test failure (strict mode).
set -euo pipefail

cd "$(dirname "$0")/.."

echo "==> Cleaning previous build"
make clean

echo "==> Building compiler (make)"
make

echo "==> Running tests (make test)"
make test

echo "==> All checks passed"
