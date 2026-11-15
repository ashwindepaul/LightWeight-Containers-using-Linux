# MiniRun v3 — Linux Container Runtime

MiniRun is a learning-focused Linux container runtime built from Linux primitives rather than Docker. Its development mode mounts a host project's source and toolchain into a constrained runtime for larger builds.

## Features

- PID, mount and UTS namespaces
- cgroups v2: memory, CPU and PID limits
- isolated `/proc` and tmpfs `/tmp`
- read-only host runtime mounts (`/usr`, `/bin`, `/lib`, `/lib64`, `/etc`) for running real Java/Node/Python/Go tools
- bind mounts for source code and persistent data
- configurable working directory
- environment variables
- clean container environment with an explicit `PATH` and `HOME`; pass app settings with `--env`
- persistent per-container `/root` cache for Maven, npm, pip and Gradle downloads
- larger default resource limits suitable for development projects
- direct `minirun run` workflow
- explicit host or isolated network mode (host networking is the default so package managers can download dependencies)
- container name and resource limit validation before creating runtime state
- container definitions remain JSON and can be started again

> Linux is required. On Windows, use Ubuntu under WSL2 and open the project from the WSL filesystem. Namespace and mount operations require root. For large projects, use `--network host` (the default) so Maven, npm, pip and application services can access the host network.

## Desktop application (new)

MiniRun includes an Electron desktop control panel in `desktop/` with a project launcher, resource controls, container overview, and live process output. The UI controls the existing Linux runtime; it does not replace the native engine.

**Supported environment:** Linux desktop or Ubuntu under WSL2 with WSLg. The native container runtime requires Linux namespaces and elevated mount privileges. This is not a native Windows container engine.

Install prerequisites and build the desktop app (requires internet access to download Electron):

```bash
./install-desktop.sh
cd desktop
npm start
```

In the app, choose **Build runtime**, then **Setup runtime**, then **Launch a project**. Alternatively, use the CLI commands below. See [`desktop/README.md`](desktop/README.md) for security notes and limitations.

## VS Code setup

Extract the ZIP, open its folder in VS Code, then open an Ubuntu/WSL terminal in that folder. From Windows PowerShell run `wsl`, navigate to the extracted project directory and launch VS Code there with `code .`.

### 1. Install dependencies (Ubuntu/WSL2)

```bash
sudo apt update
sudo apt install -y build-essential python3
```

Optional runtimes for the projects you want to run:

```bash
sudo apt install -y openjdk-17-jdk maven
sudo apt install -y nodejs npm
sudo apt install -y python3 python3-pip
```

### 2. Build

```bash
chmod +x minirun setup_container.sh
./minirun build
sudo ./minirun setup
```

### 3. Smoke test

```bash
sudo ./minirun run hello \
  --project . \
  --host-runtime \
  --command 'echo "Hello from MiniRun"; uname -a; ps -ef; ls -la /workspace'
```

### 4. Run a real Java/Spring Boot project

From the MiniRun project directory, point `--project` at your application:

```bash
sudo ./minirun run springapp \
  --project /absolute/path/to/your/spring-project \
  --host-runtime \
  --memory 4096 \
  --cpu 200 \
  --network host \
  --workdir /workspace \
  --env SPRING_PROFILES_ACTIVE=dev \
  --command 'mvn spring-boot:run'
```

Or run an already-built JAR:

```bash
sudo ./minirun run springapp \
  --project /absolute/path/to/your/spring-project \
  --host-runtime \
  --memory 4096 \
  --cpu 200 \
  --network host \
  --command 'java -jar target/app.jar'
```

### 5. Run Node.js

```bash
sudo ./minirun run nodeapp \
  --project /absolute/path/to/node-project \
  --host-runtime \
  --memory 2048 \
  --cpu 200 \
  --network host \
  --command 'npm install && npm start'
```

### 6. Run Python

```bash
sudo ./minirun run pyapp \
  --project /absolute/path/to/python-project \
  --host-runtime \
  --memory 2048 \
  --cpu 200 \
  --network host \
  --command 'python3 app.py'
```

## Bind mounts

Add extra host directories with:

```bash
sudo ./minirun run app --project ./myapp --host-runtime \
  --bind ./data:/data \
  --bind ./config:/config:ro \
  --command '...'
```

The source code is automatically mounted at `/workspace` when `--project` is used.

## Environment variables

```bash
sudo ./minirun run app --project ./myapp --host-runtime \
  --env DB_HOST=127.0.0.1 \
  --env DB_PORT=5432 \
  --env APP_ENV=dev \
  --command '...'
```

## Saved containers

```bash
./minirun create myapp --host-runtime --network host --project /absolute/path/to/app --command 'mvn spring-boot:run'
sudo ./minirun start myapp
./minirun list
./minirun info myapp
./minirun delete myapp
```

## Architecture

```text
MiniRun CLI (Python)
        |
        v
 C runtime (C)
        |
        +-- clone()
        |     +-- PID namespace
        |     +-- Mount namespace
        |     +-- UTS namespace
        |
        +-- cgroups v2
        |     +-- memory.max
        |     +-- cpu.max
        |     +-- pids.max
        |
        +-- chroot(rootfs)
        |     +-- /proc
        |     +-- /tmp
        |     +-- host runtime (optional, read-only)
        |     +-- project bind mount
        |
        +-- exec /bin/bash -lc COMMAND
```

## Important limitation

`--host-runtime` is a **development/compatibility mode**: it makes the host's user-space runtime visible read-only inside the container so large applications can run without building a complete Linux distribution image. It is not equivalent to Docker's fully self-contained image model.

Host networking shares the host network namespace. `--network isolated` gives the container a private network namespace without a configured bridge, so it has no external network access. This is a development and learning runtime, not a hardened production container engine. Stronger isolation needs user namespaces/rootless mode, a real image/layer store, virtual networking with veth/bridge, port forwarding, DNS, seccomp and a proper init process.

## Useful commands

```bash
./minirun --help
./minirun build
sudo ./minirun setup
./minirun list
./minirun doctor
./minirun info NAME
./minirun delete NAME
```

Container names must be 1–63 characters, start with a letter or digit, and use only letters, digits, `.`, `_` or `-`. Memory limits range from 64 MB to 1 TB and CPU limits range from 1% to 800%.
