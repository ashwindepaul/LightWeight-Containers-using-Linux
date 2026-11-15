const { contextBridge, ipcRenderer } = require('electron');
contextBridge.exposeInMainWorld('minirun', {
  info: () => ipcRenderer.invoke('app:info'),
  list: () => ipcRenderer.invoke('containers:list'),
  chooseProject: () => ipcRenderer.invoke('project:choose'),
  inspectProject: p => ipcRenderer.invoke('project:inspect', p),
  build: () => ipcRenderer.invoke('runtime:build'),
  setup: () => ipcRenderer.invoke('runtime:setup'),
  run: input => ipcRenderer.invoke('container:run', input),
  stop: name => ipcRenderer.invoke('container:stop', name),
  remove: name => ipcRenderer.invoke('container:delete', name),
  cli: action => ipcRenderer.invoke('cli:command', action),
  openPath: p => ipcRenderer.invoke('system:openPath', p),
  onRuntimeLog: cb => ipcRenderer.on('runtime:log', (_e, v) => cb(v)),
  onContainerLog: cb => ipcRenderer.on('container:log', (_e, v) => cb(v)),
  onContainersChanged: cb => ipcRenderer.on('containers:changed', (_e, v) => cb(v))
});
