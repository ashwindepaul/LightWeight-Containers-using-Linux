#!/usr/bin/env bash
set -euo pipefail
ROOTFS="$(cd "$(dirname "$0")" && pwd)/myroot"
mkdir -p "$ROOTFS"/{bin,usr,sbin,lib,lib64,etc,proc,sys,dev,tmp,run,workspace,root,var,tmp}
chmod 1777 "$ROOTFS/tmp"
# A small native rootfs remains available for educational isolation demos.
# Large-project mode uses --host-runtime to bind the host runtime directories read-only.
if command -v bash >/dev/null; then
  cp -f "$(command -v bash)" "$ROOTFS/bin/bash" || true
fi
ln -sfn bash "$ROOTFS/bin/sh"
cat > "$ROOTFS/etc/hostname" <<EOF2
minirun
EOF2
cat > "$ROOTFS/etc/passwd" <<EOF2
root:x:0:0:root:/root:/bin/bash
EOF2
cat > "$ROOTFS/etc/group" <<EOF2
root:x:0:
EOF2
if [ -e /etc/resolv.conf ]; then cp -L /etc/resolv.conf "$ROOTFS/etc/resolv.conf" 2>/dev/null || true; fi
echo "MiniRun rootfs prepared: $ROOTFS"
echo "For real Java/Node/Python projects use: --host-runtime"
