#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ "$(uname -s)" != Linux ]]; then
  echo "MiniRun Desktop currently needs Linux (Ubuntu/WSL2 with WSLg)." >&2
  exit 1
fi
sudo apt-get update
sudo apt-get install -y build-essential python3 pkexec nodejs npm
if ! command -v node >/dev/null 2>&1 || ! command -v npm >/dev/null 2>&1; then
  echo "Node.js and npm are required. Install them and rerun this script." >&2
  exit 1
fi
chmod +x "$ROOT/minirun" "$ROOT/setup_container.sh"
python3 "$ROOT/minirun" build
sudo python3 "$ROOT/minirun" setup
(cd "$ROOT/desktop" && npm install)
mkdir -p "$HOME/.local/share/applications"
cat > "$HOME/.local/share/applications/minirun-desktop.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=MiniRun Desktop
Comment=Local Linux project runtime manager
Exec=$ROOT/run-desktop.sh
Icon=$ROOT/desktop/icon.svg
Terminal=false
Categories=Development;Utility;
DESKTOP
chmod 644 "$HOME/.local/share/applications/minirun-desktop.desktop"
echo
echo "MiniRun Desktop setup complete. Open MiniRun Desktop from your application menu,"
echo "or launch it with: '$ROOT/run-desktop.sh'"
