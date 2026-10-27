const { contextBridge, ipcRenderer } = require("electron");
contextBridge.exposeInMainWorld("primeDesktop", {
  platform: process.platform,
  saveFile: (name, bytes, ref) => ipcRenderer.invoke("save-file", name, bytes, ref),
  openFile: () => ipcRenderer.invoke("open-file"),
  pickFiles: () => ipcRenderer.invoke("pick-files"),
  pickFolder: () => ipcRenderer.invoke("pick-folder"),
  onOpen: cb => ipcRenderer.on("open-path", (e, d) => cb(d)),
});
