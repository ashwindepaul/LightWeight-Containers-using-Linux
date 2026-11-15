#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ "$(uname -s)" != Linux ]]; then
  echo "MiniRun Desktop currently needs Linux (Ubuntu/WSL2 with WSLg)." >&2
  exit 1
fi
if [[ ! -d "$ROOT/desktop/node_modules/electron" ]]; then
  echo "Electron is not installed. Run ./install-desktop.sh first." >&2
  exit 1
fi
cd "$ROOT/desktop"
exec npm start
