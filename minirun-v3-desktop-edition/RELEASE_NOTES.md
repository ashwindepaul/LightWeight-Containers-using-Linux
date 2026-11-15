# MiniRun v3.1 Desktop Edition — release notes

## Added
- Electron desktop shell with a dark, responsive developer dashboard.
- Project folder picker and basic Java/Maven, Node.js, Python and Go project detection.
- Configurable container name, start command, environment variables, CPU quota, memory limit and network mode.
- Runtime build/setup actions, saved-container overview, project folder opener, stop/delete controls and streamed process output.
- Hardened Electron renderer boundary (`contextIsolation`, no Node integration, preload IPC allowlist, Content Security Policy).
- CLI `doctor` diagnostics command and more explicit warnings when cgroup controls cannot be applied.
- Linux setup launcher and desktop menu entry script.

## Validation performed in the build environment
- JavaScript syntax checks passed for Electron main/preload/renderer files.
- Python syntax check passed for the CLI.
- C runtime compiled successfully with GCC.
- CLI help, `doctor`, `list`, and invalid container-name validation passed.
- Full container execution could not be validated in the build environment because Linux namespace creation is denied (`clone: Operation not permitted`) and the cgroup filesystem is read-only. Validate full execution on the target Ubuntu/WSL2 machine.
- Electron dependency installation could not be completed in the build environment because the package download timed out. The installer downloads Electron through npm and therefore requires internet access.

## Known limitations
- Requires Linux/Ubuntu under WSL2 with WSLg for a graphical app on Windows; it is not a native Windows container engine.
- Runtime setup uses elevated privileges; the GUI run/setup path expects `pkexec` and a working graphical authentication agent.
- Host-runtime mode mounts host tool directories read-only and the project directory writable. Host networking shares the host network namespace.
- Isolated networking has no bridge, DNS or port forwarding.
- The dashboard shows configured resource ceilings, not live CPU/memory telemetry.
- This is a development runtime and must not be used as a hardened sandbox for untrusted code or represented as a Docker replacement.
