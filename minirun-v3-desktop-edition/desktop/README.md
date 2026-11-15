# MiniRun Desktop

A desktop control panel for the MiniRun Linux runtime. It is a local-first Electron app: the UI is isolated from Node.js, and a small preload bridge exposes only the operations needed by the interface.

## Supported setup

- Linux desktop, or Ubuntu running under WSL2 with WSLg enabled.
- Python 3 and GCC for the existing CLI/runtime.
- `pkexec` with a working graphical authentication agent on Linux desktops. In WSL2, MiniRun uses a cached `sudo` authorization; run `sudo -v` in an Ubuntu terminal before launching projects if authorization has expired.
- The app currently operates the existing MiniRun engine; it does not provide a Docker-compatible image store, bridge networking, or production-grade isolation.

## Run from source

From the repository root in Ubuntu/WSL2:

```bash
sudo apt update
sudo apt install -y build-essential python3 pkexec
cd desktop
npm install
npm start
```

On WSL2, if a launch reports that sudo authorization is needed, run `sudo -v` in an Ubuntu terminal and retry. Keep your project inside the Linux filesystem (for example `~/projects/`) for better build performance.

In MiniRun Desktop:

1. Choose **Build runtime**.
2. Choose **Setup runtime** and approve the system authentication prompt.
3. Choose **Launch a project**, select a Linux project folder, review the detected command and resource limits, then launch.
4. View stdout/stderr in Activity Log. Long-running project processes stay attached to the app session; closing the app ends tracked child processes.

For first-time setup or troubleshooting, use `./minirun doctor` from the repository root.

## Security notes

- MiniRun currently uses elevated privileges for mount and namespace setup.
- Host-runtime mode exposes host tools read-only and mounts the selected project writable.
- Host networking shares the host network namespace. Isolated networking currently has no bridge or port-forwarding setup.
- Do not use this version to execute untrusted code or claim it is a hardened sandbox.
- Environment variables can contain secrets; they are passed to the runtime and may be visible in local process/configuration data. Prefer non-sensitive development values.

## Current boundaries

This is a functional desktop control surface around the current CLI, not a signed, one-click installer. `npm install` fetches Electron and its platform binaries. The application should be tested on the target Ubuntu/WSL2 environment before distributing it to others. Linux capabilities, cgroup mount policy, `pkexec` availability, and WSL version can affect runtime behavior.
