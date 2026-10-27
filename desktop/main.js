// PRIMEngine Studio desktop shell: one window that shows the Studio, plus native file dialogs.
const { app, BrowserWindow, ipcMain, dialog, Menu } = require("electron");
const fs = require("fs"), path = require("path");
let win = null, pending = null;

const findPrimeArg = argv => argv.slice(1).find(a => a.toLowerCase().endsWith(".prime") && fs.existsSync(a));
function sendOpen(p) { try { const bytes = fs.readFileSync(p); win.webContents.send("open-path", { path: p, bytes }); } catch (e) { /* unreadable file: ignore */ } }

function createWindow() {
  win = new BrowserWindow({ width: 1500, height: 920, minWidth: 900, minHeight: 600, backgroundColor: "#2b2b2b", title: "PRIMEngine Studio",
    webPreferences: { preload: path.join(__dirname, "preload.js"), contextIsolation: true, nodeIntegration: false } });
  if (process.platform !== "darwin") Menu.setApplicationMenu(null);   // the Studio draws its own menu bar
  win.loadFile(path.join(__dirname, "app", "index.html"));
  win.webContents.on("did-finish-load", () => { if (pending) { sendOpen(pending); pending = null; } });
  win.on("closed", () => { win = null; });
}

if (!app.requestSingleInstanceLock()) app.quit();
else {
  app.on("second-instance", (e, argv) => { if (win) { if (win.isMinimized()) win.restore(); win.focus(); const p = findPrimeArg(argv); if (p) sendOpen(p); } });
  app.on("open-file", (e, p) => { e.preventDefault(); if (win) sendOpen(p); else pending = p; });   // macOS double-click
  app.whenReady().then(() => { pending = findPrimeArg(process.argv); createWindow(); });
  app.on("window-all-closed", () => app.quit());
}

ipcMain.handle("save-file", (e, suggested, bytes, ref) => {
  let p = ref;
  if (!p) { p = dialog.showSaveDialogSync(win, { defaultPath: suggested, filters: [{ name: "PRIMEngine project", extensions: ["prime"] }] }); if (!p) return null; }
  fs.writeFileSync(p, Buffer.from(bytes)); return p;
});
ipcMain.handle("open-file", () => {
  const r = dialog.showOpenDialogSync(win, { properties: ["openFile"], filters: [{ name: "PRIMEngine project", extensions: ["prime"] }] }); if (!r || !r[0]) return null;
  return { path: r[0], bytes: fs.readFileSync(r[0]) };
});
ipcMain.handle("pick-files", () => {
  const r = dialog.showOpenDialogSync(win, { properties: ["openFile", "multiSelections"] }) || [];
  return r.map(p => ({ path: path.basename(p), bytes: fs.readFileSync(p) }));
});
ipcMain.handle("pick-folder", () => {
  const r = dialog.showOpenDialogSync(win, { properties: ["openDirectory"] }); if (!r || !r[0]) return [];
  const top = r[0], name = path.basename(top), out = [];
  (function walk(dir, rel) {
    for (const ent of fs.readdirSync(dir, { withFileTypes: true })) {
      if (ent.name.startsWith(".")) continue;
      const full = path.join(dir, ent.name), relp = rel + "/" + ent.name;
      if (ent.isDirectory()) walk(full, relp); else if (ent.isFile()) out.push({ path: relp, bytes: fs.readFileSync(full) });
    }
  })(top, name);
  return out;
});
