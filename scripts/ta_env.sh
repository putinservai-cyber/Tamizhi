#!/bin/bash
# Tamizhi environment helper — source it:  source scripts/ta_env.sh

export LANG="${LANG:-en_US.UTF-8}"
export LC_ALL="${LC_ALL:-en_US.UTF-8}"

if [[ ":$PATH:" != *":$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/build:"* ]]; then
    export PATH="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/build:$PATH"
fi

echo "Tamizhi environment ready. LANG=$LANG"
command -v ta >/dev/null && ta version || echo "run 'make' first if 'ta' is not found"
