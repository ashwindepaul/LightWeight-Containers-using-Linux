const { app, BrowserWindow, ipcMain, dialog, shell } = require('electron');
const { spawn, execFile } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');

const PROJECT_ROOT = path.resolve(__dirname, '..');
const CLI = path.join(PROJECT_ROOT, 'minirun');
const CONTAINERS = path.join(PROJECT_ROOT, 'containers');
let mainWindow;
const activeRuns = new Map();

function send(channel, payload) {
  if (mainWindow && !mainWindow.isDestroyed()) mainWindow.webContents.send(channel, payload);
}
function isLinux() { return process.platform === 'linux'; }
function isWSL() { return isLinux() && /microsoft/i.test(os.release()); }
async function privilegeLauncher() {
  if (isWSL()) {
    const sudoReady = await new Promise(resolve => execFile('sudo', ['-n', 'true'], { timeout: 4000 }, error => resolve(!error)));
    if (sudoReady) return { command: 'sudo', prefix: ['-n'] };
    return { error: 'WSL2 may not have a graphical Polkit authentication agent. Open an Ubuntu terminal, run `sudo -v`, then retry in MiniRun Desktop.' };
  }
  const pkexec = await new Promise(resolve => execFile('sh', ['-lc', 'command -v pkexec'], (_e, stdout) => resolve(stdout.trim())));
  if (!pkexec) return { error: 'pkexec is not installed. Install it or run the shown MiniRun command manually with sudo in a terminal.' };
  return { command: pkexec, prefix: [] };
}
function runUnprivileged(args, timeout = 12000) {
  return new Promise((resolve) => {
    execFile('python3', [CLI, ...args], { cwd: PROJECT_ROOT, timeout, maxBuffer: 2 * 1024 * 1024 }, (error, stdout, stderr) => {
      resolve({ ok: !error, code: error?.code ?? 0, stdout: stdout || '', stderr: stderr || error?.message || '' });
    });
  });
}
function readContainers() {
  try {
    fs.mkdirSync(CONTAINERS, { recursive: true });
    return fs.readdirSync(CONTAINERS).filter(n => n.endsWith('.json')).map(n => {
      try {
        const c = JSON.parse(fs.readFileSync(path.join(CONTAINERS, n), 'utf8'));
        return { name: c.name || n.slice(0, -5), status: activeRuns.has(c.name) ? 'running' : (c.status || 'created'), memory_mb: c.memory_mb || 1024, cpu_pct: c.cpu_pct || 100, host_runtime: !!c.host_runtime, project: c.binds?.find(b => b.dst === '/workspace')?.src || '', command: c.command || '', network: c.network || 'host', created_at: c.created_at || '' };
      } catch { return null; }
    }).filter(Boolean);
  } catch { return []; }
}
function makeWindow() {
  mainWindow = new BrowserWindow({
    width: 1420, height: 920, minWidth: 1040, minHeight: 700,
    backgroundColor: '#0b1020', title: 'MiniRun Desktop',
    webPreferences: { preload: path.join(__dirname, 'preload.js'), contextIsolation: true, nodeIntegration: false, sandbox: true }
  });
  mainWindow.loadFile(path.join(__dirname, 'index.html'));
}
app.whenReady().then(() => {
  makeWindow();
  app.on('activate', () => { if (BrowserWindow.getAllWindows().length === 0) makeWindow(); });
});
app.on('window-all-closed', () => { for (const run of activeRuns.values()) { try { process.kill(-run.child.pid, 'SIGTERM'); } catch { try { run.child.kill('SIGTERM'); } catch {} } } if (process.platform !== 'darwin') app.quit(); });

ipcMain.handle('app:info', async () => ({
  appVersion: app.getVersion(), platform: process.platform, arch: process.arch,
  projectRoot: PROJECT_ROOT, supported: isLinux(), wsl: /microsoft/i.test(os.release()),
  cliExists: fs.existsSync(CLI), runtimeBuilt: fs.existsSync(path.join(PROJECT_ROOT, 'bin', 'container_runtime')),
  rootfsExists: fs.existsSync(path.join(PROJECT_ROOT, 'myroot')),
  containers: readContainers()
}));
ipcMain.handle('containers:list', async () => readContainers());
ipcMain.handle('project:choose', async () => {
  const result = await dialog.showOpenDialog(mainWindow, { title: 'Choose project folder', properties: ['openDirectory'] });
  return result.canceled ? null : result.filePaths[0];
});
ipcMain.handle('project:inspect', async (_e, projectPath) => {
  if (typeof projectPath !== 'string' || !path.isAbsolute(projectPath)) throw new Error('Choose an absolute project path.');
  let entries;
  try { entries = fs.readdirSync(projectPath, { withFileTypes: true }); } catch { throw new Error('That folder could not be read.'); }
  const names = entries.map(e => e.name);
  const kind = names.includes('pom.xml') ? 'Maven / Java' : (names.includes('package.json') ? 'Node.js' : (names.some(n => n === 'requirements.txt' || n === 'pyproject.toml') ? 'Python' : (names.includes('go.mod') ? 'Go' : 'Generic')));
  return { name: path.basename(projectPath), path: projectPath, kind, entries: names.slice(0, 12), count: entries.length };
});
ipcMain.handle('runtime:build', async () => {
  const result = await runUnprivileged(['build'], 60000);
  send('runtime:log', { kind: result.ok ? 'success' : 'error', text: result.stdout + result.stderr });
  return result;
});
ipcMain.handle('runtime:setup', async () => {
  if (!isLinux()) return { ok: false, stderr: 'MiniRun requires Linux. On Windows, use Ubuntu in WSL2 and run the desktop app from that Linux environment.' };
  const launcher = await privilegeLauncher();
  if (launcher.error) return { ok: false, stderr: launcher.error };
  return new Promise(resolve => {
    const child = spawn(launcher.command, [...launcher.prefix, 'python3', CLI, 'setup'], { cwd: PROJECT_ROOT, env: { ...process.env, DISPLAY: process.env.DISPLAY || '', WAYLAND_DISPLAY: process.env.WAYLAND_DISPLAY || '' } });
    let stdout = '', stderr = '';
    child.stdout.on('data', d => { stdout += d.toString(); send('runtime:log', { kind: 'info', text: d.toString() }); });
    child.stderr.on('data', d => { stderr += d.toString(); send('runtime:log', { kind: 'error', text: d.toString() }); });
    child.on('error', e => resolve({ ok: false, stderr: e.message }));
    child.on('close', code => resolve({ ok: code === 0, code, stdout, stderr }));
  });
});
ipcMain.handle('container:run', async (_e, input) => {
  if (!isLinux()) return { ok: false, error: 'MiniRun runtime requires Linux/WSL2. Launch this app inside Ubuntu, not as a native Windows process.' };
  const name = String(input?.name || '').trim();
  const project = String(input?.project || '').trim();
  const command = String(input?.command || '').trim();
  const memory = Number(input?.memory || 4096), cpu = Number(input?.cpu || 200);
  if (!/^[A-Za-z0-9][A-Za-z0-9_.-]{0,62}$/.test(name)) return { ok: false, error: 'Use a container name of 1–63 letters, digits, dots, underscores or hyphens; start with a letter or digit.' };
  if (!path.isAbsolute(project) || !fs.existsSync(project) || !fs.statSync(project).isDirectory()) return { ok: false, error: 'Choose an existing project folder using an absolute Linux path.' };
  if (!command) return { ok: false, error: 'Enter a command to run.' };
  if (!Number.isInteger(memory) || memory < 64 || memory > 1048576) return { ok: false, error: 'Memory must be between 64 and 1,048,576 MB.' };
  if (!Number.isInteger(cpu) || cpu < 1 || cpu > 800) return { ok: false, error: 'CPU must be between 1 and 800 percent.' };
  if (activeRuns.has(name)) return { ok: false, error: `Container ${name} is already running in this session.` };
  const args = ['run', name, '--project', project, '--memory', String(memory), '--cpu', String(cpu), '--network', input.network === 'isolated' ? 'isolated' : 'host', '--workdir', '/workspace'];
  if (input.hostRuntime !== false) args.push('--host-runtime');
  for (const env of (Array.isArray(input.env) ? input.env : [])) {
    if (typeof env !== 'string' || !/^[A-Za-z_][A-Za-z0-9_]*=.*/s.test(env)) return { ok: false, error: `Invalid environment entry: ${String(env)}` };
    args.push('--env', env);
  }
  args.push('--command', command);
  const launcher = await privilegeLauncher();
  if (launcher.error) return { ok: false, error: launcher.error };
  return new Promise(resolve => {
    const child = spawn(launcher.command, [...launcher.prefix, 'python3', CLI, ...args], { cwd: PROJECT_ROOT, env: process.env, stdio: ['ignore', 'pipe', 'pipe'], detached: true });
    activeRuns.set(name, { child, startedAt: Date.now(), project, command });
    send('containers:changed', readContainers());
    send('container:log', { name, kind: 'info', text: `$ minirun ${args.join(' ')}\n` });
    child.stdout.on('data', d => send('container:log', { name, kind: 'stdout', text: d.toString() }));
    child.stderr.on('data', d => send('container:log', { name, kind: 'stderr', text: d.toString() }));
    child.on('error', e => { activeRuns.delete(name); send('containers:changed', readContainers()); send('container:log', { name, kind: 'error', text: e.message }); resolve({ ok: false, error: e.message }); });
    child.on('close', code => {
      activeRuns.delete(name); send('containers:changed', readContainers());
      send('container:log', { name, kind: code === 0 ? 'success' : 'error', text: `\nMiniRun process exited with code ${code ?? 'unknown'}.\n` });
      resolve({ ok: code === 0, code });
    });
    resolve({ ok: true, started: true });
  });
});
ipcMain.handle('container:stop', async (_e, name) => {
  const active = activeRuns.get(String(name));
  if (!active) return { ok: false, error: 'No active MiniRun process with that name in this app session.' };
  try { process.kill(-active.child.pid, 'SIGTERM'); return { ok: true }; } catch (e) { try { active.child.kill('SIGTERM'); return { ok: true }; } catch (inner) { return { ok: false, error: inner.message || e.message }; } }
});
ipcMain.handle('container:delete', async (_e, name) => {
  if (!/^[A-Za-z0-9][A-Za-z0-9_.-]{0,62}$/.test(String(name))) return { ok: false, error: 'Invalid container name.' };
  if (activeRuns.has(name)) return { ok: false, error: 'Stop the active process before deleting its saved configuration.' };
  return runUnprivileged(['delete', name]);
});
ipcMain.handle('system:openPath', async (_e, target) => {
  if (typeof target !== 'string' || !path.isAbsolute(target)) return { ok: false, error: 'Invalid path.' };
  return { ok: !(await shell.openPath(target)) };
});
ipcMain.handle('cli:command', async (_e, action) => {
  const allowed = new Set(['list', 'doctor']);
  if (!allowed.has(action)) return { ok: false, stderr: 'That CLI action is not allowed from the dashboard.' };
  return runUnprivileged([action]);
});
