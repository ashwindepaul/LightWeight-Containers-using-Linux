#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
node --check desktop/main.js
node --check desktop/preload.js
node --check desktop/renderer.js
python3 -m py_compile minirun
make build
./minirun doctor >/tmp/minirun-doctor.log
if ./minirun run 'bad/name' --project /tmp --command 'echo no' >/tmp/minirun-invalid.log 2>&1; then
  echo "Expected invalid container name to be rejected." >&2
  exit 1
fi
grep -q 'Container name must start' /tmp/minirun-invalid.log
echo "MiniRun static checks passed. Full namespace execution must be tested on the target Linux/WSL2 host."
