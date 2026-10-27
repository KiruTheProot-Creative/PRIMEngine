/* PRIMEngine Studio: project wizard + editor in the style of classic Unity.
   Panels: Hierarchy (screens and their elements), Scene / Game views, Inspector (+ Tags), Project (asset explorer) / Console. */
(function () {
  "use strict";
  const PRIM = window.PRIM, Schema = PRIM.Schema, Store = PRIM.Store, Tags = PRIM.Tags, Host = PRIM.Host, root = document.getElementById("root");
  const lsGet = (k, d) => { try { return localStorage.getItem(k) || d; } catch (e) { return d; } }, lsSet = (k, v) => { try { localStorage.setItem(k, v); } catch (e) {} };
  const S = { project: null, tags: null, urls: {}, screenId: null, selId: null, sel: null, folder: "", testing: false, paused: false, rtab: "inspector", btab: "project", filter: "", search: "", icon: 64, collapsed: {}, treeClosed: {}, fold: {},
    log: [], file: null, fileDirty: false, dirtyT: 0, unsub: null, renaming: null, rt: null };
  let E = {};
  document.documentElement.dataset.skin = lsGet("prime.studio.skin", "dark");

  function h(tag, props, ...kids) {
    const n = document.createElement(tag);
    for (const k in (props || {})) { const v = props[k]; if (k === "class") n.className = v; else if (k === "text") n.textContent = v; else if (k === "html") n.innerHTML = v; else if (k.startsWith("on")) n.addEventListener(k.slice(2), v); else if (v !== false && v != null) n.setAttribute(k, v === true ? "" : v); }
    for (const c of kids.flat()) if (c != null && c !== false) n.append(c.nodeType ? c : document.createTextNode(c));
    return n;
  }
  const screens = () => S.project.screens, curScreen = () => screens().find(s => s.id === S.screenId) || screens()[0];
  const fmtDate = t => { const d = new Date(t); return d.toLocaleDateString(undefined, { month: "short", day: "numeric", year: "numeric" }); };
  const fmtSize = n => n < 1024 ? n + " B" : n < 1048576 ? (n / 1024).toFixed(1) + " KB" : (n / 1048576).toFixed(1) + " MB";
  const baseName = p => p.slice(p.lastIndexOf("/") + 1);
  const put = (el, ...k) => el.replaceChildren(...k.flat(Infinity).filter(x => x != null && x !== false));

  // ---------------------------------------------------------------- icons (inline SVG, colored by the skin)
  const ICONS = {
    folder: '<svg viewBox="0 0 32 32"><path d="M2 7h10l3 3h15v17H2z" style="fill:var(--folder)" stroke="rgba(0,0,0,.5)"/><path d="M2 12h28v15H2z" style="fill:var(--folder)" opacity=".75"/></svg>',
    file: '<svg viewBox="0 0 32 32"><path d="M7 2h13l6 6v22H7z" style="fill:var(--filebg)" stroke="rgba(0,0,0,.5)"/><path d="M20 2v6h6" fill="none" stroke="rgba(0,0,0,.4)"/></svg>',
    sound: '<svg viewBox="0 0 32 32"><path d="M7 2h13l6 6v22H7z" style="fill:var(--filebg)" stroke="rgba(0,0,0,.5)"/><path d="M11 16h3l4-3v10l-4-3h-3z" fill="#4f7fbf"/><path d="M20 14q2 2 0 4" fill="none" stroke="#4f7fbf" stroke-width="1.6"/></svg>',
    font: '<svg viewBox="0 0 32 32"><path d="M7 2h13l6 6v22H7z" style="fill:var(--filebg)" stroke="rgba(0,0,0,.5)"/><text x="16" y="23" text-anchor="middle" font-family="serif" font-weight="700" font-size="12" fill="#b0603a">Aa</text></svg>',
    model: '<svg viewBox="0 0 32 32"><path d="M7 2h13l6 6v22H7z" style="fill:var(--filebg)" stroke="rgba(0,0,0,.5)"/><path d="M16 11l6 3v7l-6 3-6-3v-7z" fill="#6aa7d8" stroke="#1b3c5a"/><path d="M10 14l6 3 6-3M16 17v7" fill="none" stroke="#1b3c5a"/></svg>',
    image: '<svg viewBox="0 0 32 32"><path d="M3 5h26v22H3z" style="fill:var(--filebg)" stroke="rgba(0,0,0,.5)"/><path d="M5 25l7-9 5 6 3-3 7 6z" fill="#5f9a5f"/><circle cx="22" cy="11" r="2.5" fill="#d9b441"/></svg>',
  };
  const ico = (k, cls) => h("span", { class: "ico " + (cls || ""), html: ICONS[k] || ICONS.file });

  // ---------------------------------------------------------------- log / toast
  function log(msg, kind) { S.log.push({ t: new Date(), msg, kind: kind || "info" }); if (S.log.length > 300) S.log.shift(); if (E.status) renderStatus(); if (E.console && S.btab === "console") renderProject(); if (kind === "error" || !S.project) toast(msg, kind); }
  function toast(msg, kind) { let t = document.getElementById("toast"); if (!t) { t = h("div", { id: "toast", role: "status" }); document.body.append(t); } t.textContent = msg; t.className = "show " + (kind || ""); clearTimeout(toast.t); toast.t = setTimeout(() => { t.className = ""; }, 4500); }

  // ---------------------------------------------------------------- popup menus
  function closePop() { document.querySelectorAll(".pop").forEach(p => p.remove()); E.pop = null; E.popAnchor = null; document.querySelectorAll(".mb.open").forEach(b => b.classList.remove("open")); }
  // a menu item with `sub: [...]` opens a second menu beside it (used for the Node Coder categories)
  function menuEl(items) {
    const pop = h("div", { class: "pop", role: "menu" });
    const openSub = (btn, sub) => { if (pop.__sub) { pop.__sub.remove(); pop.__sub = null; } const el = menuEl(sub), r = btn.getBoundingClientRect(); document.body.append(el); el.style.left = Math.min(r.right - 2, innerWidth - el.offsetWidth - 4) + "px"; el.style.top = Math.max(4, Math.min(r.top, innerHeight - el.offsetHeight - 4)) + "px"; pop.__sub = el; };
    for (const it of items) {
      if (it === "-") { pop.append(h("div", { class: "sep" })); continue; }
      const b = h("button", { class: "pitem", role: "menuitem", disabled: !!it.disabled, onclick: e => { if (it.sub) { openSub(e.currentTarget, it.sub); return; } closePop(); if (it.fn) it.fn(); } }, h("span", { text: (it.check ? "✓  " : "") + it.t }), it.sub ? h("span", { class: "k", text: "▸" }) : it.k ? h("span", { class: "k", text: it.k }) : null);
      b.addEventListener("mouseenter", () => { if (it.sub) openSub(b, it.sub); else if (pop.__sub) { pop.__sub.remove(); pop.__sub = null; } });
      pop.append(b);
    }
    return pop;
  }
  function popMenu(anchor, items, at) {
    closePop(); const r = at ? { left: at.x, bottom: at.y } : anchor.getBoundingClientRect(), pop = menuEl(items);
    document.body.append(pop); pop.style.left = Math.max(4, Math.min(r.left, innerWidth - pop.offsetWidth - 4)) + "px"; pop.style.top = Math.max(4, Math.min(r.bottom, innerHeight - pop.offsetHeight - 4)) + "px"; E.pop = pop; E.popAnchor = anchor || null; if (anchor) anchor.classList.add("open");
  }
  document.addEventListener("pointerdown", e => { if (E.pop && !e.target.closest(".pop") && !(E.popAnchor && E.popAnchor.contains(e.target))) closePop(); });
  document.addEventListener("pointermove", e => { S.mouse = { x: e.clientX, y: e.clientY }; });

  // ================================================================ PROJECT WIZARD
  function renderManager() {
    closeEditorState(); document.title = "PRIMEngine Studio"; document.body.classList.remove("in-editor");
    const list = Store.list(), pending = renderManager.pending;
    const tiles = list.map(p => h("div", { class: "tile" },
      h("button", { class: "tile-open", onclick: () => openProject(p.id), "aria-label": "Open " + p.name }, h("div", { class: "thumb", text: (p.name[0] || "?").toUpperCase() }), h("div", { class: "tname", text: p.name }), h("div", { class: "tdate", text: fmtDate(p.modified) + (p.file ? "  ·  " + p.file : "") })),
      h("button", { class: "tdel" + (pending === p.id ? " arm" : ""), "aria-label": "Delete " + p.name, text: pending === p.id ? "Delete?" : "✕", onclick: async () => {
        if (renderManager.pending === p.id) { renderManager.pending = null; await Store.remove(p.id); renderManager(); } else { renderManager.pending = p.id; renderManager(); setTimeout(() => { if (renderManager.pending === p.id && !S.project) { renderManager.pending = null; renderManager(); } }, 3000); } } })));
    const plus = h("button", { class: "tile plus", "aria-label": "New project", onclick: async () => { const p = Store.create(); await openProject(p.id); } }, h("div", { class: "plus-sign", text: "+" }), h("div", { class: "tname", text: "New project" }));
    const skin = h("select", { class: "dd", "aria-label": "Skin", onchange: e => setSkin(e.target.value) }, [["dark", "Dark skin"], ["light", "Classic light skin"]].map(([v, l]) => h("option", { value: v, text: l, selected: document.documentElement.dataset.skin === v })));
    root.replaceChildren(h("div", { class: "wiz" }, h("div", { class: "wwin" },
      h("div", { class: "wtitle" }, h("span", { text: "PRIMEngine — Project Wizard" })),
      h("div", { class: "wbar" }, h("button", { class: "btn", text: "Open .prime file…", onclick: openFileDialog }), h("span", { class: "sp" }), skin),
      h("div", { class: "wbody" }, h("div", { class: "brandrow" }, h("div", { class: "brand" }, "PRIM", h("span", { text: "Engine" })), h("div", { class: "motto", text: "Design your game." })),
        h("div", { class: "wh", text: "Projects" }), h("div", { class: "tiles" }, plus, ...tiles)),
      h("div", { class: "wfoot", text: Host.desktop ? "Desktop app. Double-click a .prime file to open it." : "Tip: drop a .prime file anywhere on this window to open it. Projects are also autosaved in this browser." }))));
  }
  document.addEventListener("dragover", e => { if (!S.project && [...(e.dataTransfer.types || [])].includes("Files")) e.preventDefault(); });
  document.addEventListener("drop", async e => { if (S.project) return; const f = [...(e.dataTransfer.files || [])].find(x => x.name.toLowerCase().endsWith(".prime")); if (f) { e.preventDefault(); openBytes(await Host.readFile(f)); } });
  function setSkin(v) { document.documentElement.dataset.skin = v; lsSet("prime.studio.skin", v); if (!S.project) renderManager(); else renderMenubar(); }

  async function openProject(id) {
    const p = Store.load(id); if (!p) return renderManager();
    S.project = p; S.tags = new Tags(p.tags); S.urls = await Store.loadAssets(p); loadModels(p); S.screenId = p.screens.find(s => s.id === "main") ? "main" : p.screens[0].id;
    Object.assign(S, { tab: "edit", playerSel: false, mapId: null, msel: null, selId: null, sel: null, folder: "", testing: false, paused: false, rtab: "inspector", btab: "project", search: "", renaming: null, collapsed: {}, treeClosed: {}, fileDirty: false, file: p.fileName ? { ref: null, name: p.fileName } : null });
    S.unsub = S.tags.on(() => markDirty(true)); S.log = []; document.body.classList.add("in-editor"); renderEditor(); log("Opened project “" + p.name + "”");
  }
  function closeEditorState() { killMapViews(); if (S.rt) { S.rt.destroy(); S.rt = null; } if (S.unsub) { S.unsub(); S.unsub = null; } closePop(); S.project = null; E = {}; }
  async function backToManager() { clearTimeout(S.dirtyT); if (S.project) Store.save(S.project); closeEditorState(); renderManager(); }

  // ================================================================ .prime FILES + browser autosave
  function markDirty(fromTags) { S.fileDirty = true; clearTimeout(S.dirtyT); S.dirtyT = setTimeout(autosave, 400); updateTitle(); if (E.status) renderStatus(); }
  function autosave() { clearTimeout(S.dirtyT); if (!S.project) return; Store.save(S.project); }
  function updateTitle() { if (S.project) document.title = (S.file ? S.file.name : S.project.name) + (S.fileDirty ? " *" : "") + " - PRIMEngine Studio"; if (E.pname) E.pname.textContent = S.project.name + (S.fileDirty ? " *" : ""); }
  async function saveProject(as) {
    autosave(); log("Saving…");
    try {
      const { bytes, missing } = await Store.exportPrime(S.project), r = await Host.saveFile((S.project.name || "project") + ".prime", bytes, S.file && S.file.ref, as);
      if (!r) return log("Save cancelled");
      S.file = { ref: r.ref, name: r.name }; S.project.fileName = r.name; S.fileDirty = false; autosave(); updateTitle();
      log(r.downloaded ? "Saved " + r.name + " to your Downloads folder. (The browser can't overwrite files: use the desktop app, or Chrome/Edge, for a real Save.)" : "Saved " + r.name + (missing ? " (" + missing + " asset file(s) were missing)" : ""));
    } catch (e) { log("Could not save: " + e.message, "error"); }
  }
  async function openFileDialog() { try { const r = await Host.openFile(); if (r) await openBytes(r); } catch (e) { log("Could not open: " + e.message, "error"); } }
  async function openBytes(r) {
    try { const { project, missing } = await Store.importPrime(r.bytes); project.fileName = r.name; Store.save(project); await openProject(project.id); S.file = { ref: r.ref || null, name: r.name }; updateTitle(); log("Opened " + r.name + (missing.length ? " — " + missing.length + " asset file(s) missing" : ""), missing.length ? "warn" : "info"); }
    catch (e) { log("Could not open " + r.name + ": " + e.message, "error"); }
  }
  if (Host.desktop && window.primeDesktop.onOpen) window.primeDesktop.onOpen(d => openBytes({ bytes: new Uint8Array(d.bytes), name: d.path.split(/[\\/]/).pop(), ref: d.path }));

  // ================================================================ EDITOR SHELL
  function panel(id, title) { const body = h("div", { class: "pbody" }), tabs = h("div", { class: "tabbar" }); return { el: h("section", { class: "panel", id }, tabs, body), tabs, body }; }
  function renderEditor() {
    E = { status: h("div", { id: "statusbar" }), menubar: h("div", { id: "menubar", role: "menubar" }), toolbar: h("div", { id: "toolbar" }) };
    const hier = panel("hier"), center = panel("center"), insp = panel("insp"), proj = panel("proj");
    Object.assign(E, { hier, center, insp, proj, pname: null });
    E.host = h("div", { class: "host" }); E.cbar = h("div", { class: "cbar" }); center.body.append(E.cbar, h("div", { class: "hostwrap" }, E.host));
    for (const [p, cls] of [[hier, "h"], [insp, "i"], [proj, "p"]]) p.el.append(h("div", { class: "split " + cls }));
    root.replaceChildren(h("div", { id: "app" }, E.menubar, E.toolbar, hier.el, center.el, insp.el, proj.el, E.status));
    splitter(hier.el.querySelector(".split"), "--hw", "x", 160, 520, 1); splitter(insp.el.querySelector(".split"), "--iw", "x", 240, 640, -1); splitter(proj.el.querySelector(".split"), "--ph", "y", 120, 640, -1);
    E.host.addEventListener("dragover", e => { if ([...e.dataTransfer.types].includes("application/x-prime")) e.preventDefault(); }); E.host.addEventListener("drop", stageDrop);
    renderAll();
  }
  function splitter(handle, prop, axis, min, max, sign) {
    handle.addEventListener("pointerdown", e => { handle.setPointerCapture(e.pointerId); const app = document.getElementById("app"), start = axis === "x" ? e.clientX : e.clientY, cur = parseFloat(getComputedStyle(app).getPropertyValue(prop)) || 240; handle._d = { start, cur, app }; e.preventDefault(); });
    handle.addEventListener("pointermove", e => { const d = handle._d; if (!d) return; const v = Math.max(min, Math.min(max, d.cur + sign * ((axis === "x" ? e.clientX : e.clientY) - d.start))); d.app.style.setProperty(prop, v + "px"); });
    handle.addEventListener("pointerup", () => { handle._d = null; });
  }
  function renderAll() { renderMenubar(); renderToolbar(); renderHier(); renderCenter(); renderInspector(); renderProject(); renderStatus(); updateTitle(); }

  // ---------------------------------------------------------------- menu bar + toolbar + status
  function menus() {
    const sc = curScreen(), mk = t => ({ t: "Create " + Schema.NODE[t].label, fn: () => addNode(t) });
    return [
      { label: "File", items: [{ t: "New Project", fn: async () => { await backToManager(); const p = Store.create(); await openProject(p.id); } }, { t: "Open Project…", k: "Ctrl+O", fn: openFileDialog }, "-", { t: "Save", k: "Ctrl+S", fn: () => saveProject(false) }, { t: "Save As…", fn: () => saveProject(true) }, "-", { t: "Close Project", fn: backToManager }] },
      { label: "Edit", items: [{ t: "Rename", k: "F2", fn: startRename }, { t: "Duplicate", k: "Ctrl+D", fn: duplicateSelected }, { t: "Delete", k: "Del", fn: deleteSelected }] },
      { label: "Assets", items: [{ t: "Create Folder", fn: newFolder }, "-", { t: "Import Files…", fn: importFilesDlg }, { t: "Import Folder…", fn: importFolderDlg }] },
      { label: "Screen", items: [{ t: "Create Screen", fn: addScreen }, { t: "Create Map…", fn: openNewMap }, "-", ...(sc.kind === "splash" ? [mk("splash")] : ["title", "button", "image", "slider", "toggle", "bar"].map(mk))] },
      { label: "Window", items: [{ t: "Skin: Dark", check: document.documentElement.dataset.skin === "dark", fn: () => setSkin("dark") }, { t: "Skin: Classic light", check: document.documentElement.dataset.skin === "light", fn: () => setSkin("light") }, "-", { t: "Reset layout", fn: () => { const a = document.getElementById("app"); for (const p of ["--hw", "--iw", "--ph"]) a.style.removeProperty(p); } }, { t: "Maximize on Play", fn: play }] },
      { label: "Help", items: [{ t: Host.desktop ? "PRIMEngine Studio (desktop)" : "PRIMEngine Studio (web)", disabled: true }, { t: "Design your game.", disabled: true }] },
    ];
  }
  function renderMenubar() {
    E.menubar.replaceChildren(...menus().map(m => { const b = h("button", { class: "mb", role: "menuitem", text: m.label }); const open = () => popMenu(b, m.items); b.onclick = () => (E.popAnchor === b ? closePop() : open()); b.onpointerenter = () => { if (E.pop && E.popAnchor !== b) open(); }; return b; }));
  }
  function renderToolbar() {
    const app = document.getElementById("app"); app.classList.toggle("playing", S.testing);
    E.pname = h("span", { class: "pname", text: S.project.name + (S.fileDirty ? " *" : "") });
    const name = h("input", { class: "projname", value: S.project.name, "aria-label": "Project name", onchange: e => { S.project.name = e.target.value.trim() || "Untitled"; markDirty(); } });
    put(E.toolbar, h("div", { class: "tgroup" }, h("button", { class: "tbtn", text: "← Projects", onclick: backToManager }), name),
      h("div", { class: "tcenter" }, h("button", { class: "tbtn play" + (S.testing ? " on" : ""), "aria-pressed": S.testing, "aria-label": "Play", text: "▶", onclick: () => setTesting(!S.testing) }),
        h("button", { class: "tbtn" + (S.paused ? " on" : ""), "aria-pressed": S.paused, "aria-label": "Pause", disabled: !S.testing, text: "❚❚", onclick: () => { S.paused = !S.paused; if (S.rt) S.rt.paused = S.paused; renderToolbar(); } })),
      h("div", { class: "tgroup right" }, h("button", { class: "tbtn", text: "Maximize on Play", onclick: play }), h("button", { class: "tbtn", text: "Save", title: "Save as a .prime file (Ctrl+S)", onclick: () => saveProject(false) })));
  }
  function renderStatus() { const l = S.log[S.log.length - 1]; E.status.className = l && l.kind === "error" ? "err" : l && l.kind === "warn" ? "warn" : ""; E.status.textContent = l ? l.msg : "Ready"; E.status.append(h("span", { class: "fileinfo", text: (S.file ? S.file.name : "not saved to a file yet") + (S.fileDirty ? "  ·  unsaved changes" : "") })); }
  function setTab(t) { S.fsel = null; if (t === "fly" && !S.mapId) t = "edit"; S.tab = t; S.testing = t === "game"; S.playerSel = t === "player"; if (S.playerSel || t === "timeline" || t === "coder") S.sel = null; if (!S.testing) S.paused = false; renderToolbar(); renderHier(); renderInspector(); renderCenter(); }
  function setTesting(v) { setTab(v ? "game" : "edit"); }
  function leaveSpecial() { S.playerSel = false; if (["player", "timeline", "fly", "coder"].includes(S.tab)) { S.tab = "edit"; S.testing = false; return true; } return false; }
  function centerTabs() {
    const items = S.mapId && curMap() ? [["edit", "Map"], ["fly", "Fly Editor"]] : [["edit", "Scene"]]; items.push(["game", "Game"], ["player", "Player Editor"], ["timeline", "Timeline"], ["coder", "Node Coder"]);
    return items.map(([k, l]) => h("button", { class: "tab" + (S.tab === k ? " sel" : ""), text: l, onclick: () => setTab(k) }));
  }

  // ---------------------------------------------------------------- hierarchy
  // ---- hierarchy folders: project.hfolders = [{id, name, scope?}]. A folder with no scope holds screens and maps; a folder with scope = a screen id holds that screen's elements. Items point to their folder with `.folder`.
  function newHFolder(scope) { const F = S.project.hfolders = S.project.hfolders || [], f = { id: Schema.uid("folder", F.map(x => x.id)) }; f.name = f.id; if (scope) f.scope = scope; F.push(f); S.fsel = f.id; S.sel = null; S.selId = null; markDirty(); renderHier(); renderInspector(); log("Created folder “" + f.name + "”"); }
  function deleteHFolder(f) { for (const sc of screens()) { if (sc.folder === f.id) delete sc.folder; for (const n of sc.nodes) if (n.folder === f.id) delete n.folder; } for (const m of S.project.maps) if (m.folder === f.id) delete m.folder; S.project.hfolders.splice(S.project.hfolders.indexOf(f), 1); S.fsel = null; markDirty(); renderHier(); renderInspector(); }
  function moveToFolder(d, t) {
    const F = S.project.hfolders || [], f = t.folder ? F.find(x => x.id === t.folder) : null; let it = null;
    if (d.kind === "screen") it = screens().find(x => x.id === d.id); else if (d.kind === "map") it = S.project.maps.find(x => x.id === d.id); else if (d.kind === "node") { const sc = screens().find(x => x.id === d.screen); it = sc && sc.nodes.find(x => x.id === d.id); if (f && f.scope !== d.screen) return; if (!f && t.screen && t.screen !== d.screen) return; }
    if (!it) return; if (d.kind !== "node" && f && f.scope) return; if (f) it.folder = f.id; else delete it.folder; markDirty(); renderHier();
  }
  function folderInspector() {
    const f = (S.project.hfolders || []).find(x => x.id === S.fsel), box = h("div", { class: "insp" }); if (!f) return box;
    const n = screens().filter(x => x.folder === f.id).length + S.project.maps.filter(x => x.folder === f.id).length + screens().reduce((a, sc) => a + sc.nodes.filter(q => q.folder === f.id).length, 0);
    box.append(h("div", { class: "ihead" }, h("span", { class: "bigic", text: "▰" }), h("div", { class: "ititle" }, h("input", { class: "idin", value: f.name, "aria-label": "Folder name", onchange: e => { f.name = e.target.value.trim() || f.id; markDirty(); renderHier(); } }), h("div", { class: "isub", text: "Folder · " + (f.scope ? "inside screen “" + f.scope + "”" : "holds screens and maps") + " · " + n + " item(s)" }))),
      h("div", { class: "btnrow" }, h("button", { class: "btn sm danger", text: "Delete folder (keeps the items)", onclick: () => deleteHFolder(f) })), h("div", { class: "note", text: "Drag items in the Hierarchy onto this folder to file them. Drag them onto a screen name, or the empty space below, to take them out again." }));
    return box;
  }
  function renderHier() {
    const p = E.hier, sc = curScreen(), F = S.project.hfolders || (S.project.hfolders = []), none = () => !S.selId && !S.sel && !S.mapId && !S.playerSel && !S.fsel, pad = l => "padding-left:" + (4 + l * 18) + "px";
    p.tabs.replaceChildren(h("div", { class: "tab sel", text: "Hierarchy" }), h("span", { class: "sp" }), h("button", { class: "tbtn sm", text: "Create ▾", onclick: e => popMenu(e.currentTarget, [{ t: "Screen", fn: addScreen }, { t: "Map…", fn: openNewMap }, { t: "Folder", fn: () => newHFolder(null) }, ...(sc.kind !== "splash" ? [{ t: "Folder inside “" + sc.id + "”", fn: () => newHFolder(sc.id) }] : []), "-", ...(sc.kind === "splash" ? [{ t: "Splash", fn: () => addNode("splash") }] : ["title", "button", "image", "slider", "toggle", "bar"].map(t => ({ t: Schema.NODE[t].label, fn: () => addNode(t) })))]) }));
    const rows = [], tri = (open, has, fn) => h("button", { class: "tri", "aria-label": open ? "Collapse" : "Expand", text: has ? (open ? "▾" : "▸") : "", onclick: e => { e.stopPropagation(); fn(); } });
    const drag = (el, d) => { el.draggable = true; el.addEventListener("dragstart", e => { e.dataTransfer.setData("application/x-hier", JSON.stringify(d)); e.dataTransfer.effectAllowed = "move"; }); return el; };
    const drop = (el, t) => { el.addEventListener("dragover", e => { if (e.dataTransfer.types.includes("application/x-hier")) { e.preventDefault(); el.classList.add("dropt"); } }); el.addEventListener("dragleave", () => el.classList.remove("dropt")); el.addEventListener("drop", e => { e.preventDefault(); e.stopPropagation(); el.classList.remove("dropt"); let d = null; try { d = JSON.parse(e.dataTransfer.getData("application/x-hier")); } catch (x) {} if (d) moveToFolder(d, t); }); return el; };
    const folderRow = (f, lvl, count) => { const open = !S.collapsed["f:" + f.id]; rows.push(drop(h("div", { class: "hrow" + (S.fsel === f.id ? " sel" : ""), style: pad(lvl), onclick: () => { S.fsel = f.id; S.sel = null; S.selId = null; renderHier(); renderInspector(); } }, tri(open, count > 0, () => { S.collapsed["f:" + f.id] = open; renderHier(); }), h("span", { class: "hic", text: "▰" }), h("span", { class: "hnm", text: f.name }), h("span", { class: "hmeta", text: "folder" })), { folder: f.id })); return open; };
    const nodeRow = (s, n, lvl) => rows.push(drag(h("div", { class: "hrow" + (s.id === S.screenId && n.id === S.selId && !S.sel && !S.mapId && !S.playerSel && !S.fsel ? " sel" : ""), style: pad(lvl), onclick: () => selectNode(s.id, n.id) },
      h("span", { class: "hic", text: Schema.NODE[n.type].icon }), h("span", { class: "hnm", text: n.id }), h("span", { class: "hmeta", text: n.type })), { kind: "node", screen: s.id, id: n.id }));
    const screenRows = (s, lvl) => {
      const open = !S.collapsed[s.id]; rows.push(drag(drop(h("div", { class: "hrow" + (s.id === S.screenId && !S.selId && none() ? " sel" : ""), style: pad(lvl), onclick: () => selectScreen(s.id) },
        tri(open, s.nodes.length > 0, () => { S.collapsed[s.id] = open; renderHier(); }), h("span", { class: "hic", text: s.kind === "splash" ? "✦" : s.kind === "hud" ? "◫" : "▭" }), h("span", { class: "hnm", text: s.id }), S.tags.get("project.startScreen") === s.id ? h("span", { class: "hmeta", text: "start" }) : null), { folder: null, screen: s.id }), { kind: "screen", id: s.id }));
      if (!open) return; const sf = F.filter(f => f.scope === s.id);
      for (const f of sf) { const kids = s.nodes.filter(n => n.folder === f.id); if (folderRow(f, lvl + 1, kids.length)) for (const n of kids) nodeRow(s, n, lvl + 2); }
      for (const n of s.nodes) if (!sf.some(f => f.id === n.folder)) nodeRow(s, n, lvl + 1);
    };
    const mapRows = (m, lvl) => {
      const open = !S.collapsed["map:" + m.id], isSel = S.mapId === m.id && !S.playerSel && !S.fsel && !(S.msel && S.msel.kind === "sector");
      rows.push(drag(h("div", { class: "hrow" + (isSel ? " sel" : ""), style: pad(lvl), onclick: () => selectMap(m.id) }, tri(open, m.sectors.length > 0, () => { S.collapsed["map:" + m.id] = open; renderHier(); }),
        h("span", { class: "hic", text: "▦" }), h("span", { class: "hnm", text: m.id }), h("span", { class: "hmeta", text: "map · " + m.style })), { kind: "map", id: m.id }));
      if (open) for (const sc2 of m.sectors) rows.push(h("div", { class: "hrow" + (S.mapId === m.id && !S.playerSel && !S.fsel && S.msel && S.msel.kind === "sector" && S.msel.id === sc2.id ? " sel" : ""), style: pad(lvl + 1), onclick: () => selectMap(m.id, { kind: "sector", id: sc2.id }) },
        h("span", { class: "hic", text: "▱" }), h("span", { class: "hnm", text: sc2.id }), h("span", { class: "hmeta", text: "room" })));
    };
    for (const f of F.filter(x => !x.scope)) { const ss = screens().filter(x => x.folder === f.id), mm = S.project.maps.filter(x => x.folder === f.id); if (folderRow(f, 0, ss.length + mm.length)) { for (const x of ss) screenRows(x, 1); for (const x of mm) mapRows(x, 1); } }
    const tops = F.filter(x => !x.scope); for (const x of screens()) if (!tops.some(f => f.id === x.folder)) screenRows(x, 0); for (const m of S.project.maps) if (!tops.some(f => f.id === m.folder)) mapRows(m, 0);
    rows.push(h("div", { class: "hrow" + (S.playerSel ? " sel" : ""), style: "padding-left:4px", onclick: () => setTab("player") }, h("span", { class: "tri" }), h("span", { class: "hic", text: "◉" }), h("span", { class: "hnm", text: "Player" }), h("span", { class: "hmeta", text: "entity" })));
    for (const att of S.project.player.attachments) { const ty = S.tags.get("player." + att.id + ".type"); rows.push(h("div", { class: "hrow" + (S.playerSel && S.objSel === att.id ? " sel" : ""), style: "padding-left:22px", onclick: () => { S.objSel = att.id; S.objView = true; S.coderT = { kind: "att", id: att.id }; S.cnode = null; if (S.tab === "player" || S.tab === "coder") { renderHier(); renderInspector(); renderCenter(); } else setTab("player"); } },
      h("span", { class: "hic", text: ty === "weapon" ? "✚" : ty === "image" ? "▨" : "▣" }), h("span", { class: "hnm", text: S.tags.get("player." + att.id + ".name") || att.id }), h("span", { class: "hmeta", text: ty }))); }
    p.body.replaceChildren(...rows); p.body.ondragover = e => { if (e.dataTransfer.types.includes("application/x-hier")) e.preventDefault(); }; p.body.ondrop = e => { e.preventDefault(); let d = null; try { d = JSON.parse(e.dataTransfer.getData("application/x-hier")); } catch (x) {} if (d) moveToFolder(d, { folder: null }); };
  }
  function selectScreen(id) { S.fsel = null; const was = S.screenId, wasMap = S.mapId, tc = leaveSpecial(); S.mapId = null; S.msel = null; S.screenId = id; S.selId = null; S.sel = null; renderHier(); renderInspector(); renderProject(); if (was !== id || wasMap || tc) renderCenter(); else if (S.rt) { S.rt.selId = null; S.rt.rebuild(); } }
  function selectNode(sid, id) { S.fsel = null; S.coderT = { kind: "node", screen: sid, id }; if (S.tab === "coder") S.cnode = null; const was = S.screenId, wasMap = S.mapId, tc = leaveSpecial(); S.mapId = null; S.msel = null; S.screenId = sid; S.selId = id; S.sel = null; renderHier(); renderInspector(); renderProject(); if (was !== sid || wasMap || tc || S.tab === "coder") renderCenter(); else if (S.rt) { S.rt.selId = id; S.rt.rebuild(); } }

  // ---------------------------------------------------------------- center: Scene / Game
  function renderCenter() {
    const p = E.center; killMapViews(); if (S.rt) { S.rt.destroy(); S.rt = null; }
    if (S.tab === "fly" && !S.mapId) S.tab = "edit"; S.testing = S.tab === "game";
    if (S.tab === "player") return S.objView && S.objSel && S.project.player.attachments.some(a => a.id === S.objSel) ? objectCenter() : playerCenter(); if (S.tab === "timeline") return timelineCenter(); if (S.tab === "coder") return coderCenter(); if (S.mapId && curMap() && S.tab !== "game") return mapCenter();
    const sc = S.testing && S.mapId ? (screens().find(x => x.id === S.tags.get("project.startScreen")) || screens()[0]) : curScreen();
    E.host.className = "host"; E.host.replaceChildren();
    p.tabs.replaceChildren(...centerTabs(), h("span", { class: "sp" }), h("span", { class: "csize", text: "1280 × 720  ·  16:9" }));
    put(E.cbar, h("span", { class: "chip", text: sc.id }), !S.testing && sc.kind === "splash" ? h("button", { class: "tbtn sm", text: "▶ Preview sequence", onclick: () => { if (S.rt) S.rt.previewSplash(); } }) : null,
      h("span", { class: "hint", text: S.testing ? "Playing: hover and click buttons, drag sliders. Edits are disabled." : sc.kind === "splash" ? "Select a splash in the Hierarchy to see it." : "Click to select, drag to move (Shift = snap to 10), drag an orange corner to resize. x0,y0 is the middle of the screen. Drop images from the Project panel here." }));
    S.rt = new PRIM.Runtime({ host: E.host, project: S.project, tags: S.testing ? S.tags.fork() : S.tags, mode: S.testing ? "test" : "edit", urls: S.urls, screenId: sc.id, selId: S.selId, onStartGame: startGame, say: t => log(t),
      onSelect: id => { S.selId = id; S.sel = null; renderHier(); renderInspector(); renderProject(); }, onChange: () => renderInspector(), onExit: () => setTesting(false) });
    S.rt.paused = S.paused;
  }

  // ---------------------------------------------------------------- structure edits (screens and elements)
  function addNode(type, over0) {
    const sc = curScreen(); if ((sc.kind === "splash") !== (type === "splash")) return log(type === "splash" ? "Splashes go on the splash screen." : "Elements go on menu screens, not the splash screen.", "warn");
    const id = Schema.uid(type, sc.nodes.map(n => n.id)), node = { id, type }; sc.nodes.push(node);
    const over = Object.assign(type === "splash" ? {} : { y: type === "image" ? 0 : 160 - (sc.nodes.length - 1) % 8 * 60 }, over0 || {}); if (type === "title" && !over.text) over.text = "Title"; if (type === "button" && !over.text) over.text = "Button";
    Schema.defineNode(S.project, sc.id, node, over); S.selId = id; S.sel = null; S.rtab = "inspector"; afterStructure(); log("Created " + type + " “" + id + "”");
  }
  function afterStructure() { markDirty(); S.tags.emit([]); renderHier(); renderInspector(); renderProject(); if (S.rt && !S.testing) { S.rt.selId = S.selId; S.rt.rebuild(); } }
  function deleteNode(n) { const sc = curScreen(); S.tags.removePrefix(sc.id + "." + n.id); sc.nodes.splice(sc.nodes.indexOf(n), 1); S.selId = null; afterStructure(); }
  function dupNode(n) {
    const sc = curScreen(), id = Schema.uid(n.type, sc.nodes.map(x => x.id)), pre = sc.id + "." + n.id + ".", np = sc.id + "." + id + ".";
    for (const k of Object.keys(S.project.tags)) if (k.startsWith(pre)) { const r = JSON.parse(JSON.stringify(S.project.tags[k])); r.owner = sc.id + "." + id; S.project.tags[np + k.slice(pre.length)] = r; }
    for (const ax of ["x", "y"]) { const r = S.project.tags[np + ax]; if (r) r.value = ax === "x" ? Math.min(640, r.value + 30) : Math.max(-360, r.value - 30); }
    sc.nodes.splice(sc.nodes.indexOf(n) + 1, 0, Object.assign({ id, type: n.type }, n.script ? { script: JSON.parse(JSON.stringify(n.script)) } : {}, n.folder ? { folder: n.folder } : {})); S.selId = id; afterStructure();
  }
  function moveNode(n, d) { const a = curScreen().nodes, i = a.indexOf(n), j = i + d; if (j < 0 || j >= a.length) return; [a[i], a[j]] = [a[j], a[i]]; afterStructure(); }
  function renameNode(n, raw) { const sc = curScreen(), id = Schema.clean(raw); if (!id || id === n.id || sc.nodes.some(x => x.id === id)) return renderInspector(); S.tags.renamePrefix(sc.id + "." + n.id, sc.id + "." + id); n.id = id; S.selId = id; afterStructure(); }
  function addScreen() {
    const id = Schema.uid("screen", screens().map(s => s.id)), sc = { id, kind: "menu", nodes: [] }; S.project.screens.push(sc); Schema.defineScreen(S.project, sc);
    const n = { id: "title", type: "title" }; sc.nodes.push(n); Schema.defineNode(S.project, id, n, { text: id, y: 120 }); S.screenId = id; S.selId = null; S.sel = null; afterStructure(); renderCenter(); log("Created screen “" + id + "”");
  }
  function deleteScreen(sc) {
    const was = sc.id; S.tags.removePrefix(was); S.project.screens.splice(S.project.screens.indexOf(sc), 1);
    for (const k in S.project.tags) { const r = S.project.tags[k]; if (r.type === "screenRef" && r.value === was) r.value = ""; }
    S.screenId = (screens().find(s => s.id === "main") || screens()[0]).id; S.selId = null; afterStructure(); renderCenter();
  }
  function renameScreen(sc, raw) {
    const id = Schema.clean(raw); if (!id || id === sc.id || screens().some(s => s.id === id)) return renderInspector();
    const old = sc.id; S.tags.renamePrefix(old, id); for (const k in S.project.tags) { const r = S.project.tags[k]; if (r.group === old) r.group = id; if (r.type === "screenRef" && r.value === old) r.value = id; }
    sc.id = id; S.screenId = id; afterStructure(); renderCenter();
  }
  function stageDrop(e) {
    e.preventDefault(); let d; try { d = JSON.parse(e.dataTransfer.getData("application/x-prime")); } catch (x) { return; } if (S.testing || !d || d.kind !== "asset") return;
    const a = S.project.assets.find(x => x.id === d.key); if (!a || a.type !== "image") return log("Only images can be dropped on the screen.", "warn");
    if (S.mapId) return dropOnMap(e, a);
    if (!S.rt) return;
    const sc = curScreen(), r = S.rt.stage.getBoundingClientRect(), x = Math.round((e.clientX - r.left) / S.rt.scale), y = Math.round((e.clientY - r.top) / S.rt.scale);
    if (sc.kind === "splash") addNode("splash", { kind: "image", asset: a.id }); else addNode("image", { asset: a.id, x: Math.max(-640, Math.min(640, x - 640)), y: Math.max(-360, Math.min(360, 360 - y)) });
  }
  function selectedNode() { return S.selId && !S.sel ? curScreen().nodes.find(n => n.id === S.selId) : null; }
  function deleteSelected() { if (S.tab === "coder") return coderDelete(); if (S.playerSel || S.tab === "timeline") return; if (S.mapId && S.med && !S.sel) return S.med.deleteSelection(); if (S.sel) return deleteAssetSel(); const n = selectedNode(); if (n) deleteNode(n); }
  function duplicateSelected() { const n = selectedNode(); if (n) dupNode(n); }
  function startRename() { if (S.sel) { S.renaming = { kind: S.sel.kind, key: S.sel.key }; renderProject(); } else { const i = document.querySelector(".idin"); if (i) { i.focus(); i.select(); } } }

  // ================================================================ PROJECT PANEL (asset explorer)
  const assetOf = id => S.project.assets.find(a => a.id === id);
  const foldersAll = () => Store.folders(S.project);
  function renderProject() {
    const p = E.proj;
    put(p.tabs, h("button", { class: "tab" + (S.btab === "project" ? " sel" : ""), text: "Project", onclick: () => { S.btab = "project"; renderProject(); } }), h("button", { class: "tab" + (S.btab === "console" ? " sel" : ""), text: "Console", onclick: () => { S.btab = "console"; renderProject(); } }), h("span", { class: "sp" }),
      S.btab === "project" ? [h("input", { class: "txt search", type: "search", placeholder: "Search", value: S.search, "aria-label": "Search assets", oninput: e => { S.search = e.target.value; renderProjectBody(); } }),
        h("input", { type: "range", class: "zoom", min: 32, max: 110, value: S.icon, "aria-label": "Icon size", oninput: e => { S.icon = +e.target.value; renderProjectBody(); } }),
        h("button", { class: "tbtn sm", text: "Import ▾", onclick: e => popMenu(e.currentTarget, [{ t: "Import Files…", fn: importFilesDlg }, { t: "Import Folder…", fn: importFolderDlg }]) }),
        h("button", { class: "tbtn sm", text: "Create ▾", onclick: e => popMenu(e.currentTarget, [{ t: "Folder", fn: newFolder }]) })] : h("button", { class: "tbtn sm", text: "Clear", onclick: () => { S.log = []; renderProject(); renderStatus(); } }));
    renderProjectBody();
  }
  // ---- multi-selection in the asset explorer (S.sel stays the "primary" item)
  function multiList() { const m = S.multi; return m && S.sel && m.primary === S.sel && m.items.length > 1 ? m.items : null; }
  function isPicked(kind, key) { const m = multiList(); return m ? m.some(i => i.kind === kind && i.key === key) : !!(S.sel && S.sel.kind === kind && S.sel.key === key); }
  function pickSet(items, primary) { const list = items.filter((i, n) => items.findIndex(j => j.kind === i.kind && j.key === i.key) === n); if (!list.length) { S.sel = null; S.multi = null; return; } S.sel = primary || list[list.length - 1]; S.multi = list.length > 1 ? { primary: S.sel, items: list } : null; if (S.multi) S.multi.items = list.map(i => i === primary ? S.sel : i); }
  function pickedItems() { return multiList() || (S.sel ? [S.sel] : []); }
  function topLevel(items) { const folders = items.filter(i => i.kind === "folder").map(i => i.key); return items.filter(i => { const path = i.kind === "folder" ? Store.parentOf(i.key) : (assetOf(i.key) || { path: "" }).path; return !folders.some(f => f !== i.key && (path === f || path.startsWith(f + "/"))); }); }
  function clickPick(e, kind, key) {
    const cur = { kind, key }, order = S.porder || [], here = order.findIndex(i => i.kind === kind && i.key === key);
    if (e.shiftKey && S.panchor && here >= 0) { const a = order.findIndex(i => i.kind === S.panchor.kind && i.key === S.panchor.key); if (a >= 0) { const lo = Math.min(a, here), hi = Math.max(a, here); pickSet(order.slice(lo, hi + 1).map(i => ({ kind: i.kind, key: i.key })), cur); return; } }
    if (e.ctrlKey || e.metaKey) { const items = pickedItems().slice(), at = items.findIndex(i => i.kind === kind && i.key === key); if (at >= 0) items.splice(at, 1); else items.push(cur); S.panchor = cur; pickSet(items, at >= 0 ? items[items.length - 1] : cur); return; }
    S.panchor = cur; pickSet([cur], cur);
  }
  function marquee(e, grid) {
    if (e.button !== 0 || e.target !== grid) return;
    const x0 = e.clientX, y0 = e.clientY, base = (e.ctrlKey || e.metaKey || e.shiftKey) ? pickedItems().slice() : [];
    let box = null, moved = false;
    const move = ev => {
      if (!moved && Math.abs(ev.clientX - x0) + Math.abs(ev.clientY - y0) < 5) return; moved = true;
      if (!box) { box = h("div", { class: "marq" }); document.body.append(box); }
      const l = Math.min(x0, ev.clientX), t = Math.min(y0, ev.clientY), r = Math.max(x0, ev.clientX), b = Math.max(y0, ev.clientY);
      box.style.cssText = "left:" + l + "px;top:" + t + "px;width:" + (r - l) + "px;height:" + (b - t) + "px";
      const hit = [];
      grid.querySelectorAll(".pit").forEach(el => { const q = el.getBoundingClientRect(), on = q.right > l && q.left < r && q.bottom > t && q.top < b; el.classList.toggle("sel", on || base.some(i => i.kind + ":" + i.key === el.dataset.k)); if (on) { const at = el.dataset.k.indexOf(":"); hit.push({ kind: el.dataset.k.slice(0, at), key: el.dataset.k.slice(at + 1) }); } });
      S.mhit = hit;
    };
    const up = () => {
      document.removeEventListener("mousemove", move); document.removeEventListener("mouseup", up); if (box) box.remove();
      if (!moved) return; S.noclick = true; setTimeout(() => { S.noclick = false; }, 0);
      pickSet(base.concat(S.mhit || []), null); S.panchor = S.sel; renderProjectBody(); renderInspector();
    };
    document.addEventListener("mousemove", move); document.addEventListener("mouseup", up);
  }
  function renderProjectBody() {
    const b = E.proj.body; if (S.btab === "console") {
      b.className = "pbody console"; b.replaceChildren(...(S.log.length ? S.log.slice().reverse().map(l => h("div", { class: "crow " + l.kind }, h("span", { class: "ct", text: l.t.toLocaleTimeString() }), h("span", { text: l.msg }))) : [h("div", { class: "empty", text: "Nothing logged yet." })])); return; }
    b.className = "pbody explorer"; const folders = foldersAll();
    const tree = h("div", { class: "ptree", role: "tree" });
    const addRow = (path, name, depth, hasKids) => tree.append(h("div", { class: "trow" + (S.folder === path ? " sel" : ""), role: "treeitem", style: "padding-left:" + (6 + depth * 14) + "px", onclick: () => { S.folder = path; S.search = ""; S.sel = null; renderProject(); renderInspector(); },
      ondragover: e => { if ([...e.dataTransfer.types].includes("application/x-prime")) { e.preventDefault(); e.currentTarget.classList.add("drop"); } }, ondragleave: e => e.currentTarget.classList.remove("drop"), ondrop: e => { e.currentTarget.classList.remove("drop"); dropOnFolder(e, path); } },
      h("button", { class: "tri", "aria-label": "Toggle", text: hasKids ? (S.treeClosed[path] ? "▸" : "▾") : "", onclick: e => { e.stopPropagation(); S.treeClosed[path] = !S.treeClosed[path]; renderProjectBody(); } }), ico("folder", "sm"), h("span", { class: "hnm", text: name })));
    const walk = (parent, depth) => { for (const f of folders.filter(x => Store.parentOf(x) === parent)) { const kids = folders.some(c => Store.parentOf(c) === f); addRow(f, baseName(f), depth, kids); if (kids && !S.treeClosed[f]) walk(f, depth + 1); } };
    addRow("", "Assets", 0, folders.some(f => !Store.parentOf(f))); if (!S.treeClosed[""]) walk("", 1);

    const q = S.search.trim().toLowerCase(), subs = q ? [] : folders.filter(f => Store.parentOf(f) === S.folder);
    const assets = (q ? S.project.assets.filter(a => a.name.toLowerCase().includes(q)) : S.project.assets.filter(a => a.path === S.folder)).slice().sort((a, b) => a.name.localeCompare(b.name));
    const crumbs = [h("button", { class: "crumb", text: "Assets", onclick: () => { S.folder = ""; S.search = ""; renderProject(); } })]; let acc = "";
    for (const part of S.folder ? S.folder.split("/") : []) { acc = acc ? acc + "/" + part : part; const pth = acc; crumbs.push(h("span", { class: "sepc", text: "›" }), h("button", { class: "crumb", text: part, onclick: () => { S.folder = pth; renderProject(); } })); }
    const cw = S.icon + 18, grid = h("div", { class: "pgrid", style: "--cw:" + cw + "px;--ci:" + S.icon + "px" });
    S.porder = [...subs.map(f => ({ kind: "folder", key: f })), ...assets.map(a => ({ kind: "asset", key: a.id }))];
    for (const f of subs) grid.append(pItem("folder", f, baseName(f), null));
    for (const a of assets) grid.append(pItem("asset", a.id, a.name, a));
    if (!subs.length && !assets.length) grid.append(h("div", { class: "empty", text: q ? "No assets match “" + S.search + "”." : "This folder is empty. Use Import, or drop files and folders from your computer here." }));
    const content = h("div", { class: "pcontent" }, h("div", { class: "crumbs" }, ...crumbs, q ? h("span", { class: "sepc", text: "  search results" }) : null), grid);
    content.addEventListener("dragover", e => { if ([...e.dataTransfer.types].includes("Files")) { e.preventDefault(); content.classList.add("dropfiles"); } });
    content.addEventListener("dragleave", e => { if (!content.contains(e.relatedTarget)) content.classList.remove("dropfiles"); });
    content.addEventListener("drop", e => { content.classList.remove("dropfiles"); if ([...e.dataTransfer.types].includes("Files")) { e.preventDefault(); Host.fromDrop(e.dataTransfer).then(items => doIngest(items, "drop")); } });
    grid.addEventListener("mousedown", e => marquee(e, grid));
    grid.addEventListener("click", e => { if (S.noclick) return; if (e.target === grid && !(e.ctrlKey || e.metaKey || e.shiftKey)) { S.sel = null; S.multi = null; renderProjectBody(); renderInspector(); } });
    b.replaceChildren(tree, content);
    if (S.renaming) { const i = b.querySelector(".rename"); if (i) { i.focus(); const d = i.value.lastIndexOf("."); i.setSelectionRange(0, d > 0 && S.renaming.kind === "asset" ? d : i.value.length); } }
  }
  function pItem(kind, key, name, a) {
    const sel = isPicked(kind, key), renaming = S.renaming && S.renaming.kind === kind && S.renaming.key === key;
    let icon; if (kind === "folder") icon = ico("folder", "big"); else if (a.type === "image" && S.urls[a.id]) icon = h("img", { class: "thumbimg", src: S.urls[a.id], alt: "", draggable: false }); else if (a.type === "font") icon = h("span", { class: "ico big fontprev", style: "font-family:'prim-font-" + a.id + "',serif", text: "Aa" }); else icon = ico(a.type === "sound" ? "sound" : a.type === "image" ? "image" : a.type === "model" ? "model" : "file", "big");
    const label = renaming ? h("input", { class: "rename", value: kind === "folder" ? baseName(key) : a.name, "aria-label": "New name", onkeydown: e => { if (e.key === "Enter") e.target.blur(); else if (e.key === "Escape") { S.renaming = null; renderProjectBody(); } e.stopPropagation(); }, onblur: e => { if (!S.renaming) return; const v = e.target.value; S.renaming = null; commitRename(kind, key, v); } }) : h("div", { class: "pname", title: name, text: name });
    const it = h("div", { class: "pit" + (sel ? " sel" : ""), "data-k": kind + ":" + key, draggable: !renaming, title: kind === "folder" ? key : (a.path ? a.path + "/" : "") + a.name, onclick: e => { e.stopPropagation(); clickPick(e, kind, key); renderProjectBody(); renderInspector(); },
      ondblclick: () => { if (kind === "folder") { S.folder = key; S.search = ""; S.sel = null; renderProject(); renderInspector(); } else if (a.type === "sound" && S.urls[a.id]) playSound(a.id); },
      ondragstart: e => { const ml = multiList(), many = ml && ml.some(i => i.kind === kind && i.key === key); e.dataTransfer.setData("application/x-prime", JSON.stringify(many ? { kind, key, items: topLevel(ml) } : { kind, key })); e.dataTransfer.effectAllowed = "move"; } }, h("div", { class: "pico" }, icon), label);
    if (kind === "folder") { it.addEventListener("dragover", e => { if ([...e.dataTransfer.types].includes("application/x-prime")) { e.preventDefault(); e.stopPropagation(); it.classList.add("drop"); } }); it.addEventListener("dragleave", () => it.classList.remove("drop")); it.addEventListener("drop", e => { it.classList.remove("drop"); e.stopPropagation(); dropOnFolder(e, key); }); }
    return it;
  }
  const soundPlayers = {}; function playSound(id) { stopSound(); const u = new Audio(S.urls[id]); u.volume = 0.7; soundPlayers.cur = u; u.play().catch(() => {}); } function stopSound() { if (soundPlayers.cur) { soundPlayers.cur.pause(); soundPlayers.cur = null; } }

  // ---- asset operations
  async function doIngest(items, label) {
    if (!items.length) return log("Nothing to import.", "warn");
    try { const added = await Store.ingest(S.project, S.urls, items, S.folder); markDirty(); log("Imported " + added.length + " file(s)" + (label && label !== "drop" ? " from " + label : "") + " into Assets" + (S.folder ? "/" + S.folder : "")); if (S.rt) { S.rt.urls = S.urls; S.rt.rebuild(); } renderProject(); renderInspector(); }
    catch (e) { log("Import failed: " + e.message, "error"); }
  }
  async function importFilesDlg() { try { await doIngest(await Host.pickFiles(), "files"); } catch (e) { log("Import failed: " + e.message, "error"); } }
  async function importFolderDlg() { try { await doIngest(await Host.pickFolder(), "folder"); } catch (e) { log("Import failed: " + e.message, "error"); } }
  function newFolder() {
    const names = foldersAll().filter(f => Store.parentOf(f) === S.folder).map(baseName), n = Store.uniqueName(names, "New Folder"), path = S.folder ? S.folder + "/" + n : n;
    Store.ensureFolder(S.project, path); S.sel = { kind: "folder", key: path }; S.renaming = { kind: "folder", key: path }; markDirty(); renderProject(); renderInspector();
  }
  function retarget(op, np) {
    const P = S.project; P.folders = P.folders.map(f => f === op || f.startsWith(op + "/") ? np + f.slice(op.length) : f);
    for (const a of P.assets) if (a.path === op || a.path.startsWith(op + "/")) a.path = np + a.path.slice(op.length);
    if (S.folder === op || S.folder.startsWith(op + "/")) S.folder = np + S.folder.slice(op.length);
  }
  function commitRename(kind, key, raw) {
    const v = raw.replace(/[\\/]+/g, "").trim(); if (!v) return renderProjectBody();
    if (kind === "folder") { const parent = Store.parentOf(key), np = parent ? parent + "/" + v : v; if (np !== key) { if (foldersAll().includes(np)) { log("A folder named “" + v + "” already exists here.", "warn"); return renderProjectBody(); } retarget(key, np); S.sel = { kind, key: np }; } }
    else { const a = assetOf(key); if (!a || v === a.name) return renderProjectBody(); if (S.project.assets.some(x => x !== a && x.path === a.path && x.name === v)) { log("“" + v + "” already exists in this folder.", "warn"); return renderProjectBody(); } a.name = v; }
    markDirty(); renderProject(); renderInspector(); log("Renamed to “" + v + "”");
  }
  function dropOnFolder(e, dest) {
    e.preventDefault(); let d; try { d = JSON.parse(e.dataTransfer.getData("application/x-prime")); } catch (x) { return; } if (!d) return;
    if (d.items && d.items.length > 1) { for (const it of d.items) dropOne(it, dest); S.multi = null; markDirty(); renderProject(); renderInspector(); return; }
    dropOne(d, dest); markDirty(); renderProject(); renderInspector();
  }
  function dropOne(d, dest) {
    if (d.kind === "asset") { const a = assetOf(d.key); if (!a || a.path === dest) return; a.name = Store.uniqueName(S.project.assets.filter(x => x !== a && x.path === dest).map(x => x.name), a.name); a.path = dest; if (dest) Store.ensureFolder(S.project, dest); log("Moved “" + a.name + "” to Assets" + (dest ? "/" + dest : "")); }
    else if (d.kind === "folder") { const key = d.key; if (dest === key || dest.startsWith(key + "/") || Store.parentOf(key) === dest) return; const np = dest ? dest + "/" + baseName(key) : baseName(key); if (foldersAll().includes(np)) return log("A folder named “" + baseName(key) + "” already exists there.", "warn"); retarget(key, np); S.sel = { kind: "folder", key: np }; log("Moved folder “" + baseName(key) + "”"); }
  }
  function removeAssetCore(a) {
    for (const k in S.project.tags) { const r = S.project.tags[k]; if ((r.type === "asset" || r.type === "font") && r.value === a.id) r.value = r.type === "font" ? "builtin:display" : ""; }
    S.project.assets.splice(S.project.assets.indexOf(a), 1); delete S.urls[a.id]; Store.delAsset(S.project.id, a.id);
  }
  let delArm = null;
  function deleteAssetSel() {
    const items = topLevel(pickedItems()); if (!items.length) return; const key = items.map(i => i.kind + ":" + i.key).join("|");
    if (delArm !== key) { delArm = key; log("Press Delete again to remove " + (items.length > 1 ? items.length + " items" : "“" + baseName(items[0].key) + "”") + (items.some(i => i.kind === "folder") ? " (folders with everything in them)" : "") + ".", "warn"); setTimeout(() => { if (delArm === key) delArm = null; }, 3000); return; }
    delArm = null;
    for (const s of items) {
      if (s.kind === "asset") { const a = assetOf(s.key); if (a) removeAssetCore(a); }
      else { for (const a of S.project.assets.filter(x => x.path === s.key || x.path.startsWith(s.key + "/"))) removeAssetCore(a); S.project.folders = S.project.folders.filter(f => f !== s.key && !f.startsWith(s.key + "/")); if (S.folder === s.key || S.folder.startsWith(s.key + "/")) S.folder = Store.parentOf(s.key); }
    }
    S.sel = null; S.multi = null; log("Deleted " + (items.length > 1 ? items.length + " items." : "."));
    S.tags.emit([]); markDirty(); renderProject(); renderInspector(); if (S.rt) { S.rt.urls = S.urls; S.rt.rebuild(); }
  }

  // ================================================================ INSPECTOR
  const GROUP = { x: "Transform", y: "Transform", w: "Transform", h: "Transform", lock: "Transform", hoverSound: "Audio", clickSound: "Audio", action: "Behaviour", target: "Behaviour", bind: "Behaviour", duration: "Timing", fadeIn: "Timing", fadeInType: "Timing", fadeOut: "Timing", fadeOutType: "Timing" };
  const ORDER = ["Transform", "Appearance", "Timing", "Audio", "Behaviour"];
  function foldout(key, title, ...kids) { const open = S.fold[key] !== false; return h("div", { class: "fold" }, h("button", { class: "fh", "aria-expanded": open, onclick: () => { S.fold[key] = !open; renderInspector(); } }, h("span", { class: "tri", text: open ? "▾" : "▸" }), h("span", { text: title })), open ? h("div", { class: "fb" }, ...kids) : null); }
  function renderInspector() {
    const p = E.insp; p.tabs.replaceChildren(...[["inspector", "Inspector"], ["tags", "Tags"]].map(([k, l]) => h("button", { class: "tab" + (S.rtab === k ? " sel" : ""), text: l, onclick: () => { S.rtab = k; renderInspector(); } })));
    p.body.replaceChildren(S.rtab === "tags" ? tagsBody() : S.sel ? assetInspector() : propsBody());
  }
  function propsBody() {
    if (S.fsel) return folderInspector();
    if (S.playerSel) return playerInspector(); if (S.tab === "timeline") return timelineInspector(); if (S.tab === "coder") return coderInspector();
    if (S.mapId && curMap()) return mapInspector();
    const sc = curScreen(), node = selectedNode(), box = h("div", { class: "insp" });
    if (node) {
      const pre = sc.id + "." + node.id + ".", def = Schema.NODE[node.type];
      box.append(h("div", { class: "ihead" }, h("span", { class: "bigic", text: def.icon }), h("div", { class: "ititle" }, h("input", { class: "idin", value: node.id, "aria-label": "Element name", onchange: e => renameNode(node, e.target.value) }), h("div", { class: "isub", text: def.label + " · screen “" + sc.id + "”" }))),
        h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: sc.kind === "splash" ? "◀ Earlier" : "▼ Back", onclick: () => moveNode(node, -1) }), h("button", { class: "btn sm", text: sc.kind === "splash" ? "Later ▶" : "▲ Front", onclick: () => moveNode(node, 1) }), h("button", { class: "btn sm", text: "Duplicate", onclick: () => dupNode(node) }), h("button", { class: "btn sm danger", text: "Delete", onclick: () => deleteNode(node) })));
      const groups = {}; for (const pr of def.props) { if (!Schema.shown(S.tags, pre, pr)) continue; (groups[GROUP[pr.k] || "Appearance"] = groups[GROUP[pr.k] || "Appearance"] || []).push(field(pre + pr.k)); }
      if (sc.kind !== "splash") box.append(h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "Open Node Coder for this element", onclick: () => { S.coderT = { kind: "node", screen: sc.id, id: node.id }; setTab("coder"); } })));
      if (groups.Transform && sc.kind !== "splash") groups.Transform.push(h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "Center horizontally", onclick: () => { S.tags.set(pre + "x", 0); renderInspector(); } }), h("button", { class: "btn sm", text: "Center vertically", onclick: () => { S.tags.set(pre + "y", 0); renderInspector(); } }), h("button", { class: "btn sm", text: "Center both", onclick: () => { S.tags.setMany({ [pre + "x"]: 0, [pre + "y"]: 0 }); renderInspector(); } })));
      for (const g of ORDER) if (groups[g]) box.append(foldout(g, g, ...groups[g]));
      box.append(h("div", { class: "note", text: "Tip: write {audio.master} in any text to show a tag's live value. Drag a field's name left/right to scrub numbers." }));
    } else {
      const isStart = S.tags.get("project.startScreen") === sc.id;
      box.append(h("div", { class: "ihead" }, h("span", { class: "bigic", text: sc.kind === "splash" ? "✦" : sc.kind === "hud" ? "◫" : "▭" }), h("div", { class: "ititle" }, h("input", { class: "idin", value: sc.id, "aria-label": "Screen name", onchange: e => renameScreen(sc, e.target.value) }), h("div", { class: "isub", text: sc.kind === "splash" ? "Splash screens, played in order" : sc.kind === "hud" ? "HUD: drawn over the game" : "Menu screen" }))),
        h("div", { class: "btnrow" }, h("button", { class: "btn sm", disabled: isStart, text: isStart ? "Start screen" : "Make start screen", onclick: () => { S.tags.set("project.startScreen", sc.id); renderHier(); renderInspector(); } }), sc.kind === "menu" ? h("button", { class: "btn sm danger", text: "Delete screen", onclick: () => deleteScreen(sc) }) : null),
        foldout("screen", "Screen", ...Schema.SCREEN[sc.kind].map(pr => field(sc.id + "." + pr.k))), h("div", { class: "note", text: "Select an element in the Hierarchy or the Scene view to edit it. Every value is a named tag: see the Tags tab." }));
    }
    return box;
  }
  function assetInspector() {
    const s = S.sel, box = h("div", { class: "insp" }), ml = multiList();
    if (ml) { const nf = ml.filter(i => i.kind === "folder").length, na = ml.length - nf; box.append(h("div", { class: "ihead" }, ico("folder", "bigico"), h("div", { class: "ititle" }, h("div", { class: "idin ro", text: ml.length + " items selected" }), h("div", { class: "isub", text: na + " file(s), " + nf + " folder(s)" }))), h("div", { class: "note", text: "Drag any of them onto a folder to move them all, or press Delete twice to remove them. Click one item to go back to a single selection." })); return box; }
    if (s.kind === "folder") { const n = S.project.assets.filter(a => a.path === s.key || a.path.startsWith(s.key + "/")).length; box.append(h("div", { class: "ihead" }, ico("folder", "bigico"), h("div", { class: "ititle" }, h("div", { class: "idin ro", text: baseName(s.key) }), h("div", { class: "isub", text: "Folder · Assets/" + s.key }))), h("div", { class: "note", text: n + " file(s) inside. Double-click to open. Drag files onto it to move them. F2 renames, Delete removes." })); return box; }
    const a = assetOf(s.key); if (!a) return box;
    const used = []; for (const k in S.project.tags) { const r = S.project.tags[k]; if ((r.type === "asset" || r.type === "font") && r.value === a.id) used.push(k); }
    box.append(h("div", { class: "ihead" }, ico(a.type === "sound" ? "sound" : a.type === "font" ? "font" : a.type === "image" ? "image" : a.type === "model" ? "model" : "file", "bigico"), h("div", { class: "ititle" }, h("div", { class: "idin ro", text: a.name }), h("div", { class: "isub", text: a.type + " · Assets/" + (a.path ? a.path + "/" : "") + a.name + " · " + fmtSize(a.size || 0) }))));
    const pv = h("div", { class: "preview" });
    if (a.type === "image" && S.urls[a.id]) { const im = h("img", { src: S.urls[a.id], alt: a.name }), dim = h("div", { class: "isub", text: "" }); im.onload = () => { dim.textContent = im.naturalWidth + " × " + im.naturalHeight + " px"; }; pv.append(im, dim); }
    else if (a.type === "sound" && S.urls[a.id]) { const dur = h("span", { class: "isub", text: "" }), au = new Audio(S.urls[a.id]); au.onloadedmetadata = () => { dur.textContent = au.duration.toFixed(2) + " s"; }; pv.append(h("button", { class: "btn", text: "▶ Play", onclick: () => playSound(a.id) }), h("button", { class: "btn", text: "■ Stop", onclick: stopSound }), dur); }
    else if (a.type === "font") pv.append(h("div", { class: "fsample", style: "font-family:'prim-font-" + a.id + "',serif;font-size:34px", text: "Aa Bb Cc 123" }), h("div", { class: "fsample", style: "font-family:'prim-font-" + a.id + "',serif;font-size:16px", text: "The quick brown fox jumps over the lazy dog." }));
    else if (a.type === "model") { const mt = a.meta || {}; pv.append(h("div", { class: "isub", text: mt.error ? "Could not read this FBX: " + mt.error : (mt.meshes || 0) + " mesh piece(s), " + (mt.nodes || 0) + " bones/nodes" }), h("div", { class: "note", text: (mt.anims && mt.anims.length) ? "Animations found:\n" + mt.anims.map(an => "• " + an.name + "  (" + an.dur.toFixed(2) + " s)").join("\n") + "\n\nSee them in the Timeline tab." : "No animations in this file." })); }
    else pv.append(h("div", { class: "isub", text: "No preview for this file type yet." }));
    box.append(foldout("preview", "Preview", pv), foldout("used", "Used by", h("div", { class: "note", text: used.length ? used.join("\n") : "Not used by any tag yet. Drag it onto the Scene view or an asset field." })));
    return box;
  }

  // ---------------------------------------------------------------- one field for any tag
  function field(name, o) {
    o = o || {}; const t = S.tags.spec(name); if (!t) return h("div");
    const compact = !!o.compact, label = o.label || t.label || name, redraw = () => { if (!compact) renderInspector(); }, set = v => S.tags.set(name, v);
    const lab = h("label", { class: "flab", title: name }, compact ? h("span", { class: "mono", text: name }) : [h("span", { text: label }), h("span", { class: "tn", text: name })]);
    const row = ctl => h("div", { class: "fld" + (compact ? " compact" : "") }, lab, h("div", { class: "fctl" }, ctl));
    switch (t.type) {
      case "number": {
        const step = t.step || 1, dec = step >= 1 ? 0 : Math.min(3, Math.ceil(-Math.log10(step))), ranged = isFinite(t.min) && isFinite(t.max), slider = ranged && !compact && (t.max - t.min) <= 10;
        const inp = h("input", { class: "num", type: "number", step, value: (+S.tags.get(name)).toFixed(dec), "aria-label": label }), sl = slider ? h("input", { type: "range", min: t.min, max: t.max, step, value: S.tags.get(name), "aria-label": label + " slider" }) : null;
        const commit = v => { if (!isFinite(v)) return; if (ranged) v = Math.max(t.min, Math.min(t.max, v)); v = Math.round(v / step) * step;
          const m = /^(.+)\.(w|h)$/.exec(name), lk = m && S.tags.get(m[1] + ".lock") && S.tags.spec(m[1] + ".w") && S.tags.spec(m[1] + ".h"); let extra = null;   // "keep proportions": the other side follows
          if (lk) { let rw = +S.tags.get(m[1] + ".w"), rh = +S.tags.get(m[1] + ".h"); if (!rw || !rh) { const el = S.rt && S.rt.els[m[1].split(".")[1]]; if (el) { rw = el.offsetWidth; rh = el.offsetHeight; } } if (rw > 0 && rh > 0) extra = m[2] === "w" ? [m[1] + ".h", Math.round(v * rh / rw)] : [m[1] + ".w", Math.round(v * rw / rh)]; }
          set(v); inp.value = v.toFixed(dec); if (sl) sl.value = v; if (extra) { const sp = S.tags.spec(extra[0]); S.tags.set(extra[0], Math.max(sp.min, Math.min(sp.max, extra[1]))); if (!compact) setTimeout(renderInspector, 0); } };
        inp.onchange = () => commit(parseFloat(inp.value)); if (sl) sl.oninput = () => commit(+sl.value);
        lab.classList.add("scrub");
        lab.addEventListener("pointerdown", e => { if (e.button !== 0) return; lab.setPointerCapture(e.pointerId); lab._s = { x: e.clientX, v: +S.tags.get(name), per: ranged ? (t.max - t.min) / 400 : step }; });
        lab.addEventListener("pointermove", e => { if (lab._s) commit(lab._s.v + (e.clientX - lab._s.x) * lab._s.per * (e.shiftKey ? 0.1 : 1)); }); lab.addEventListener("pointerup", () => { lab._s = null; });
        return row(h("div", { class: "numwrap" }, sl, inp));
      }
      case "bool": return row(h("input", { type: "checkbox", class: "chk", checked: !!S.tags.get(name), "aria-label": label, onchange: e => { set(e.target.checked); redraw(); } }));
      case "color": return row(h("input", { type: "color", class: "col", value: S.tags.get(name), "aria-label": label, oninput: e => set(e.target.value) }));
      case "string": return row(h("input", { class: "txt", type: "text", value: S.tags.get(name), "aria-label": label, oninput: e => set(e.target.value) }));
      case "asset": case "font": case "choice": case "screenRef": case "tagRef": case "anim": {
        let opts = [];
        if (t.type === "asset") opts = [["", "None"], ...S.project.assets.filter(a => a.type === t.assetType).map(a => [a.id, (a.path ? a.path + "/" : "") + a.name])];
        else if (t.type === "font") opts = [...PRIM.BUILTIN_FONTS, ...S.project.assets.filter(a => a.type === "font").map(a => [a.id, a.name])];
        else if (t.type === "anim") { const mo = PRIM.FBX.cache[S.tags.get(t.modelTag)]; opts = [["", "(none)"], ...(mo ? Object.keys(mo.anims) : []).map(n => [n, n])]; }
        else if (t.type === "choice") opts = t.options; else if (t.type === "screenRef") opts = [["", "None"], ...screens().map(s => [s.id, s.id])];
        else opts = [["", "(pick a tag)"], ...S.tags.names().filter(n => !S.tags.spec(n).group || S.tags.spec(n).group !== "script").filter(n => t.refType === "any" ? ["number", "bool", "string", "color"].includes(S.tags.spec(n).type) : S.tags.spec(n).type === t.refType).map(n => [n, (S.tags.spec(n).label || n) + "  ·  " + n])];
        const sel = h("select", { class: "dd", "aria-label": label }, opts.map(([v, l]) => h("option", { value: v, text: l }))); sel.value = S.tags.get(name); if (sel.value !== String(S.tags.get(name))) sel.value = "";
        sel.onchange = () => { set(sel.value); redraw(); };
        const accepts = t.type === "asset" ? t.assetType : t.type === "font" ? "font" : null;
        if (accepts) { sel.addEventListener("dragover", e => { if ([...e.dataTransfer.types].includes("application/x-prime")) { e.preventDefault(); sel.classList.add("drop"); } }); sel.addEventListener("dragleave", () => sel.classList.remove("drop"));
          sel.addEventListener("drop", e => { e.preventDefault(); sel.classList.remove("drop"); try { const d = JSON.parse(e.dataTransfer.getData("application/x-prime")), a = d.kind === "asset" && assetOf(d.key); if (a && a.type === accepts) { set(a.id); redraw(); } else log("That asset is not a " + accepts + ".", "warn"); } catch (x) {} }); }
        const play = t.type === "asset" && t.assetType === "sound" && S.tags.get(name) && S.urls[S.tags.get(name)] ? h("button", { class: "btn sm", "aria-label": "Play sound", text: "▶", onclick: () => playSound(S.tags.get(name)) }) : null;
        return row(h("div", { class: "selrow" }, sel, play));
      }
    }
    return row(null);
  }

  // ---------------------------------------------------------------- tags tab
  function tagsBody() {
    const box = h("div", { class: "insp" }), names = S.tags.names();
    const nn = h("input", { class: "txt", placeholder: "name, e.g. player.speed", "aria-label": "New tag name" }), ty = h("select", { class: "dd", "aria-label": "Type" }, [["number", "Number"], ["bool", "On / Off"], ["string", "Text"], ["color", "Color"]].map(([v, l]) => h("option", { value: v, text: l })));
    const mn = h("input", { class: "num", type: "number", value: 0, "aria-label": "Minimum" }), mx = h("input", { class: "num", type: "number", value: 1, "aria-label": "Maximum" }), err = h("div", { class: "note warn" });
    const add = () => {
      const name = nn.value.trim().toLowerCase(); err.textContent = "";
      if (!/^[a-z][a-z0-9_]*(\.[a-z0-9_]+)*$/.test(name)) { err.textContent = "Use lowercase letters, numbers, _ and dots (like player.speed)."; return; } if (S.tags.has(name)) { err.textContent = "That tag already exists."; return; }
      const type = ty.value, rec = { type, value: type === "number" ? 0 : type === "bool" ? false : type === "color" ? "#ffffff" : "", label: name, group: name.split(".")[0], custom: true };
      if (type === "number") { rec.min = +mn.value; rec.max = +mx.value; rec.step = (rec.max - rec.min) > 10 ? 1 : 0.01; rec.value = rec.min; } S.tags.define(name, rec); S.tags.emit([name]); renderInspector();
    };
    box.append(foldout("newtag", "New tag", h("div", { class: "fld" }, h("label", { class: "flab", text: "Name" }), h("div", { class: "fctl" }, nn)), h("div", { class: "fld" }, h("label", { class: "flab", text: "Type" }), h("div", { class: "fctl" }, ty)),
      h("div", { class: "fld" }, h("label", { class: "flab", text: "Min / Max" }), h("div", { class: "fctl mm" }, mn, mx)), h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "+ Add tag", onclick: add })), err));
    const listBox = h("div"), filter = h("input", { class: "txt", type: "search", placeholder: "Filter tags…", value: S.filter, "aria-label": "Filter tags", oninput: e => { S.filter = e.target.value; list(); } });
    box.append(h("div", { class: "fpad" }, filter), listBox);
    const list = () => {
      const f = S.filter.trim().toLowerCase(), groups = {}; for (const n of names.filter(n => !f || n.includes(f) || (S.tags.spec(n).label || "").toLowerCase().includes(f))) { const g = S.tags.spec(n).group || n.split(".")[0]; (groups[g] = groups[g] || []).push(n); }
      listBox.replaceChildren(h("div", { class: "note", text: names.length + " tags" }), ...Object.keys(groups).sort().map(g => foldout("tg:" + g, g, ...groups[g].map(tagRow))));
    };
    list(); return box;
  }
  function tagRow(n) {
    const t = S.tags.spec(n);
    if (!t.custom) return field(n, { compact: true });
    return h("div", {}, h("div", { class: "thead" }, h("input", { class: "tname", value: n, "aria-label": "Tag name", onchange: e => { const nv = e.target.value.trim().toLowerCase(); if (/^[a-z][a-z0-9_]*(\.[a-z0-9_]+)*$/.test(nv) && S.tags.rename(n, nv)) S.tags.t[nv].group = nv.split(".")[0]; renderInspector(); } }),
      h("button", { class: "btn sm", "aria-label": "Delete tag", text: "✕", onclick: () => { delete S.project.tags[n]; S.tags.emit([]); renderInspector(); } })), field(n, { compact: true }));
  }

  // ================================================================ MAPS (DOOM-style editor + 3D fly view)
  const curMap = () => S.project.maps.find(m => m.id === S.mapId);
  function killMapViews() { if (S.med) { S.med.destroy(); S.med = null; } if (S.v3) { S.v3.dispose(); S.v3 = null; } if (S.p3) { S.p3.destroy(); S.p3 = null; } if (S.tlv) { S.tlv.destroy(); S.tlv = null; } if (S.coderStop) { S.coderStop(); S.coderStop = null; } if (S.oev) { S.oev.destroy(); S.oev = null; } }
  function selectMap(id, sel) { S.fsel = null; const was = S.mapId, tc = (S.playerSel || S.tab === "timeline") ? (S.tab = "edit", S.testing = false, true) : false; S.playerSel = false; S.mapId = id; S.selId = null; S.sel = null; S.msel = sel || null; renderHier(); renderInspector(); renderProject(); if (was !== id || !S.med || tc) renderCenter(); else S.med.select(S.msel, true); }
  function openNewMap() {
    const ov = h("div", { class: "modal" }), close = () => ov.remove();
    const card = (id, title, desc, off) => h("button", { class: "mcard" + (off ? " off" : ""), disabled: off, onclick: () => { close(); createMap(id); } }, h("div", { class: "mt", text: title }), h("div", { class: "md", text: desc }), off ? h("div", { class: "mbadge", text: "Coming later" }) : null);
    ov.append(h("div", { class: "mwin", role: "dialog", "aria-label": "New map" }, h("div", { class: "wtitle", text: "New Map: choose an editor style" }),
      h("div", { class: "mbody" }, card("modern", "Modern", "A low-poly brush editor for terrain, placing buildings, and features that make map-making a lot easier.", true),
        card("doom", "DOOM", "The simplest map editor. Walls are made from connected nodes. Select a node or wall and give it a texture. A free-fly 3D view is built in.")),
      h("div", { class: "btnrow", style: "padding:6px 14px 12px" }, h("button", { class: "btn", text: "Cancel", onclick: close }))));
    ov.addEventListener("pointerdown", e => { if (e.target === ov) close(); }); document.body.append(ov);
  }
  const reserved = () => [...screens().map(s => s.id), ...S.project.maps.map(m => m.id), ...(S.project.events || []).map(e => e.id), "audio", "gfx", "project", "player"];
  function createMap(style) {
    let n = 1; while (reserved().includes("map" + n)) n++; const id = "map" + n, m = Schema.newMap(S.project, id, style); S.tags.emit([]); markDirty();
    S.mapId = id; S.msel = { kind: "sector", id: "s1" }; S.selId = null; S.sel = null; S.testing = false; S.paused = false; S.rtab = "inspector"; renderToolbar(); renderHier(); renderCenter(); renderInspector(); renderProject(); log("Created map “" + id + "” (" + style.toUpperCase() + " style)");
  }
  function deleteMap(m) {
    S.tags.removePrefix(m.id); S.project.maps.splice(S.project.maps.indexOf(m), 1); S.mapId = null; S.msel = null; S.testing = false; renderToolbar(); renderHier(); renderCenter(); renderInspector(); markDirty(); log("Deleted map “" + m.id + "”");
  }
  function renameMap(m, raw) {
    const id = Schema.clean(raw); if (!id || id === m.id || reserved().includes(id)) return renderInspector(); const old = m.id; S.tags.renamePrefix(old, id);
    for (const k in S.project.tags) { const r = S.project.tags[k]; if (r.group === old) r.group = id; if (r.owner === old || (r.owner && r.owner.startsWith(old + "."))) r.owner = id + r.owner.slice(old.length); }
    m.id = id; S.mapId = id; markDirty(); renderHier(); renderInspector(); renderCenter();
  }
  function mapCenter() {
    const m = curMap(), p = E.center;
    const fly = S.tab === "fly";
    p.tabs.replaceChildren(...centerTabs(), h("span", { class: "sp" }), h("span", { class: "csize", text: m.style.toUpperCase() + " editor  ·  " + m.sectors.length + " room(s)" }));
    put(E.cbar, h("span", { class: "chip", text: m.id }), h("button", { class: "tbtn sm", text: "▶ Walk this map", title: "Try the map as the player (Esc to leave)", onclick: () => walkMap(m) }), h("span", { class: "hint", text: fly ? "Editor camera: click the view, then W A S D to move, mouse to look, Q / E down and up, Shift = fast. This is for placing things; the player is set up in the Player Editor tab." : "Rooms are closed loops of nodes. Drop an image from the Project panel onto a wall to texture it (Shift = ceiling when dropped on a room)." }));
    E.host.className = "host maphost"; E.host.replaceChildren();
    if (fly) { S.v3 = makeView(E.host, m); if (S.v3) S.v3.focus = false; }
    else { S.med = new PRIM.MapEditor({ host: E.host, project: S.project, map: m, tags: S.tags, getUrls: () => S.urls, log, onSelect: sel => { S.msel = sel; renderInspector(); renderHier(); },
      onChange: (geo, structure) => { markDirty(); if (structure) { renderHier(); renderInspector(); } else renderInspector(); } }); S.med.select(S.msel, true); }
  }
  function makeView(host, m) {
    const cv = h("canvas", { class: "v3cv" }); host.append(cv); const v = new PRIM.R3D.View(cv), T = S.tags;
    if (!v.ok) { cv.remove(); host.append(h("div", { class: "v3err", text: "WebGL could not start on this computer, so the 3D view is unavailable." })); return null; }
    v.setMap(S.project, m, T, S.urls);
    const sx = +T.get(m.id + ".startX"), sy = +T.get(m.id + ".startY"), sec = PRIM.R3D.sectorAt(m, sx, sy);
    v.placeCamera(sx, sy, +T.get(m.id + ".startAngle"), (sec ? +T.get(m.id + "." + sec.id + ".floor") : 0) + 56); v.attach(); v.start();
    let t; const un = T.on(() => { clearTimeout(t); t = setTimeout(() => v.rebuild(), 60); });
    const hud = h("div", { class: "v3pos" }); host.append(hud); let n = 0; v.onFrame = () => { if (n++ % 10 === 0) hud.textContent = "x " + Math.round(v.cam.x) + "   y " + Math.round(v.cam.z) + "   height " + Math.round(v.cam.y) + "   " + v.triCount + " triangles"; };
    v.dispose = () => { clearTimeout(t); un(); v.destroy(); }; return v;
  }
  // fullscreen fly-through (Maximize on Play for a map, and the Play button of a game)
  function fly3d(m, z) {
    const layer = h("div", { class: "play-layer fly", style: "z-index:" + (z || 100) }); document.body.append(layer); const v = makeView(layer, m); if (v) v.focus = true;
    const hud = h("div", { class: "flyhud", text: "W A S D move  ·  mouse look  ·  Q / E down, up  ·  Shift fast  ·  Esc leave" }); layer.append(hud); layer.append(h("button", { class: "tbtn flyx", text: "✕ Leave", onclick: () => close() })); setTimeout(() => hud.classList.add("fade"), 4000);
    const onKey = e => { if (e.key === "Escape") { e.stopImmediatePropagation(); e.preventDefault(); close(); } };
    function close() { removeEventListener("keydown", onKey, true); if (v) v.dispose(); layer.remove(); } addEventListener("keydown", onKey, true); layer.__close = close;
  }
  function startGame(rt) {
    const m = S.project.maps[0]; if (!m) { rt.game = true; rt.rebuild(); return; }
    rt.stopMusic(); const layer = h("div", { class: "p3layer" }); rt.host.append(layer);
    const pl = new PRIM.Play3D({ host: layer, project: S.project, map: m, tags: rt.tags, urls: S.urls, mode: "play", debug: true, say: t => log(t), onExit: () => { pl.destroy(); layer.remove(); } }); S.play = pl;
  }
  function walkMap(m) {
    const layer = h("div", { class: "play-layer p3layer" }); document.body.append(layer);
    const pl = new PRIM.Play3D({ host: layer, project: S.project, map: m, tags: S.tags, urls: S.urls, mode: "play", debug: true, say: t => log(t), onExit: () => { pl.destroy(); layer.remove(); } }); S.play = pl;
  }
  // ---------------------------------------------------------------- Player Editor
  function playerCenter() {
    const p = E.center, m = curMap() || S.project.maps[0] || null;
    p.tabs.replaceChildren(...centerTabs(), h("span", { class: "sp" }), h("span", { class: "csize", text: "what the player sees" }));
    put(E.cbar, h("span", { class: "chip", text: "Player view" }), S.objSel ? h("button", { class: "tbtn sm", text: "Object editor", onclick: () => { S.objView = true; renderCenter(); } }) : null, m ? h("button", { class: "tbtn sm", text: "▶ Walk " + m.id, onclick: () => walkMap(m) }) : null,
      h("span", { class: "hint", text: m ? "The player's view from the start point of “" + m.id + "”, with the crosshair and HUD drawn over it. Change values on the right; this updates live." : "Make a map (Screen > Create Map…) to see the player's view here. The crosshair and HUD preview below still work." }));
    E.host.className = "host maphost"; E.host.replaceChildren();
    S.p3 = new PRIM.Play3D({ host: E.host, project: S.project, map: m, tags: S.tags, urls: S.urls, mode: "preview", previewWeapon: S.objSel });
  }
  // ---- 3D objects attached to the player camera
  function loadModels(p) { PRIM.FBX.cache = {}; PRIM.FBX.pending = {}; for (const a of p.assets) if (a.type === "model") PRIM.FBX.load(a.id, S.urls[a.id]).then(m => { if (m && S.project === p) { if (!a.meta || a.meta.error) a.meta = PRIM.FBX.summary(m); renderInspector(); if (S.tab === "timeline") renderCenter(); } }); }
  // ---- Object editor: the selected object on its own, to turn around and to place the muzzle point and the aim arrow by clicking
  function objectCenter() {
    const p = E.center, att = S.project.player.attachments.find(a => a.id === S.objSel), pre = "player." + att.id + ".", T = S.tags, F = PRIM.FBX, M4 = PRIM.M4, OE = S.oe = S.oe || { yaw: -30, pitch: 15, zoom: 1, tool: "orbit" };
    const type = T.get(pre + "type"), isW = type === "weapon", aid = T.get(pre + (type === "image" ? "image" : "asset")), mo = F.cache[aid]; if (!isW && ["muzzle", "arrow", "emitter", "earrow"].includes(OE.tool)) OE.tool = "orbit";
    p.tabs.replaceChildren(...centerTabs(), h("span", { class: "sp" }), h("span", { class: "csize", text: "object editor" }));
    const tb = (t, l, on, fn) => h("button", { class: "tbtn sm" + (on ? " on" : ""), text: l, onclick: fn }), tool = (k, l) => tb(k, l, OE.tool === k, () => { OE.tool = k; renderCenter(); }), view = (l, y, pt) => tb(l, l, false, () => { OE.yaw = y; OE.pitch = pt; });
    const hints = { orbit: "Drag to turn the object around, scroll to zoom.", pivot: "Click or drag: the colored cross is the pivot (origin) of this object. It turns, scales and sits where you put it.", muzzle: "Click or drag on the object: the yellow cross (where shots leave the weapon) follows your mouse.", arrow: "Drag: the red arrow points to your mouse, as seen in this view. Switch between Front / Side / Top to aim it in every direction.", emitter: "Click or drag on the object: the cyan cross is where projectiles start (the projectile emitter).", earrow: "Drag: the cyan arrow is the direction projectiles fly, as seen in this view. Switch between Front / Side / Top to aim it in every direction." };
    put(E.cbar, h("span", { class: "chip", text: T.get(pre + "name") || att.id }), tb("pv", "◀ Player view", false, () => { S.objView = false; renderCenter(); }), tool("orbit", "Look around"), tool("pivot", "Set pivot"), isW ? [tool("muzzle", "Set muzzle point"), tool("arrow", "Aim arrow"), tool("emitter", "Set projectile emitter"), tool("earrow", "Aim emitter arrow")] : null, tb("pc", "Pivot to middle", false, () => { const b = mo && mo.bounds; if (b) T.setMany({ [pre + "pivotX"]: +((b.lo[0] + b.hi[0]) / 2).toFixed(1), [pre + "pivotY"]: +((b.lo[1] + b.hi[1]) / 2).toFixed(1), [pre + "pivotZ"]: +((b.lo[2] + b.hi[2]) / 2).toFixed(1) }); renderInspector(); }), tb("po", "Pivot to origin", false, () => { T.setMany({ [pre + "pivotX"]: 0, [pre + "pivotY"]: 0, [pre + "pivotZ"]: 0 }); renderInspector(); }), view("Front", 0, 0), view("Side", 90, 0), view("Top", 0, 89),
      isW ? tb("fw", "Arrow forward", false, () => { T.setMany({ [pre + "arrowYaw"]: 0, [pre + "arrowPitch"]: 0 }); }) : null, isW ? tb("em", "Emitter = muzzle", false, () => { T.setMany({ [pre + "emitX"]: g("muzzleX"), [pre + "emitY"]: g("muzzleY"), [pre + "emitZ"]: g("muzzleZ"), [pre + "emitYaw"]: g("arrowYaw"), [pre + "emitPitch"]: g("arrowPitch") }); }) : null, h("span", { class: "hint", text: hints[OE.tool] }));
    const cv = h("canvas", { class: "v3cv", style: "cursor:" + (OE.tool === "orbit" ? "grab" : "crosshair") }); E.host.className = "host maphost"; E.host.replaceChildren(cv);
    if (!mo) { E.host.append(h("div", { class: "v3err", style: "color:var(--muted)", text: aid ? "Loading…" : "Choose a model or image for this object on the right." })); if (aid) (type === "image" ? F.loadImage : F.load)(aid, S.urls[aid]).then(m => { if (m && S.objView && S.tab === "player") renderCenter(); }); return; }
    const v = new PRIM.R3D.View(cv); if (!v.ok) return; v.sky = [0.12, 0.14, 0.19]; v.fov = 0.8; const gm = v.loadModel(mo), D = 34, g = k => +T.get(pre + k) || 0;
    const frame = () => { const b = mo.bounds, c = [0, 1, 2].map(i => (b.lo[i] + b.hi[i]) / 2), k = 22 / (b.size || 1) * OE.zoom, R = M4.mul(M4.rotX(OE.pitch), M4.rotY(OE.yaw)), M = M4.mul(M4.translate(0, 0, -D), M4.mul(R, M4.mul(M4.scale(k, k, k), M4.translate(-c[0], -c[1], -c[2])))), Rinv = M4.mul(M4.rotY(-OE.yaw), M4.rotX(-OE.pitch));
      return { c, k, M, Rinv, pose: F.pose(mo, T.get(pre + "idleAnim") || null, 0) }; };
    const segs = fr => { const o = [], ax = [[[1, 0, 0], [1, 0.3, 0.3]], [[0, 1, 0], [0.4, 1, 0.4]], [[0, 0, 1], [0.4, 0.6, 1]]], O = M4.point(fr.M, g("pivotX"), g("pivotY"), g("pivotZ"));
      for (const [d, c] of ax) o.push({ a: O, b: M4.add(O, M4.dir(M4.mul(M4.rotX(OE.pitch), M4.rotY(OE.yaw)), ...d.map(q => q * 3))), c });
      if (isW) { const mp = M4.point(fr.M, g("muzzleX"), g("muzzleY"), g("muzzleZ")), ay = g("arrowYaw") * Math.PI / 180, ap = g("arrowPitch") * Math.PI / 180, ad = M4.norm(M4.dir(fr.M, -Math.cos(ap) * Math.sin(ay), Math.sin(ap), -Math.cos(ap) * Math.cos(ay))), tip = M4.add(mp, ad.map(q => q * 9)), side = M4.norm(M4.cross(ad, [0.3, 0.9, 0.2])), red = [1, 0.25, 0.2], yel = [1, 0.9, 0.2];
        o.push({ a: mp, b: tip, c: red }, { a: tip, b: M4.add(tip, M4.add(ad.map(q => -q * 1.8), side.map(q => q * 1))), c: red }, { a: tip, b: M4.add(tip, M4.sub(ad.map(q => -q * 1.8), side.map(q => q * 1))), c: red });
        for (const d of [[1, 0, 0], [0, 1, 0], [0, 0, 1]]) o.push({ a: M4.sub(mp, d.map(q => q * 1.1)), b: M4.add(mp, d.map(q => q * 1.1)), c: yel });
        if (T.get(pre + "projType") !== "none") {   // the projectile emitter: a cyan point with its own arrow
          const ep = M4.point(fr.M, g("emitX"), g("emitY"), g("emitZ")), ey = g("emitYaw") * Math.PI / 180, epi = g("emitPitch") * Math.PI / 180, ed = M4.norm(M4.dir(fr.M, -Math.cos(epi) * Math.sin(ey), Math.sin(epi), -Math.cos(epi) * Math.cos(ey))), et = M4.add(ep, ed.map(q => q * 9)), es = M4.norm(M4.cross(ed, [0.3, 0.9, 0.2])), cy = [0.2, 0.9, 1];
          o.push({ a: ep, b: et, c: cy }, { a: et, b: M4.add(et, M4.add(ed.map(q => -q * 1.8), es.map(q => q * 1))), c: cy }, { a: et, b: M4.add(et, M4.sub(ed.map(q => -q * 1.8), es.map(q => q * 1))), c: cy });
          for (const d of [[1, 0, 0], [0, 1, 0], [0, 0, 1]]) o.push({ a: M4.sub(ep, d.map(q => q * 1.1)), b: M4.add(ep, d.map(q => q * 1.1)), c: cy }); } }
      return o; };
    let raf = 0; const loop = () => { raf = requestAnimationFrame(loop); const fr = frame(); v.draw(); v.drawViewmodel([{ gm, pose: fr.pose, M: fr.M }], segs(fr)); }; raf = requestAnimationFrame(loop);
    // mouse -> ray in camera space -> point on the object (or on the plane through its middle when it misses)
    const ray = e => { const r = cv.getBoundingClientRect(), t = Math.tan(v.fov / 2), asp = r.width / r.height; return M4.norm([((e.clientX - r.left) / r.width * 2 - 1) * t * asp, (1 - (e.clientY - r.top) / r.height * 2) * t, -1]); };
    const toModel = (fr, pc) => { const q = M4.dir(fr.Rinv, pc[0], pc[1], pc[2] + D).map(x => x / fr.k); return [fr.c[0] + q[0], fr.c[1] + q[1], fr.c[2] + q[2]]; };
    const surface = (fr, d) => { let best = Infinity;
      mo.nodes.forEach((n, i) => { if (!n.mesh) return; const W = M4.mul(fr.M, fr.pose[i]), P = n.mesh.p, vt = j => M4.point(W, P[j * 3], P[j * 3 + 1], P[j * 3 + 2]);
        for (let j = 0; j + 2 < P.length / 3; j += 3) { const a = vt(j), b = vt(j + 1), c = vt(j + 2), e1 = M4.sub(b, a), e2 = M4.sub(c, a), pv = M4.cross(d, e2), det = M4.dot(e1, pv); if (Math.abs(det) < 1e-9) continue; const tv = M4.sub([0, 0, 0], a), u = M4.dot(tv, pv) / det; if (u < 0 || u > 1) continue; const qv = M4.cross(tv, e1), w = M4.dot(d, qv) / det; if (w < 0 || u + w > 1) continue; const t = M4.dot(e2, qv) / det; if (t > 0 && t < best) best = t; } });
      if (best < Infinity) return d.map(q => q * best); return d.map(q => q * (D / -d[2])); };
    const apply = e => {
      const fr = frame(), d = ray(e);
      if (OE.tool === "muzzle" || OE.tool === "emitter") { const nm = OE.tool === "muzzle" ? "muzzle" : "emit", m = toModel(fr, surface(fr, d)).map(x => Math.round(x * 10) / 10); T.setMany({ [pre + nm + "X"]: m[0], [pre + nm + "Y"]: m[1], [pre + nm + "Z"]: m[2] }); }
      else if (OE.tool === "pivot") { const m = toModel(fr, surface(fr, d)).map(x => Math.round(x * 10) / 10); T.setMany({ [pre + "pivotX"]: m[0], [pre + "pivotY"]: m[1], [pre + "pivotZ"]: m[2] }); }
      else if (OE.tool === "arrow" || OE.tool === "earrow") {
        const E_ = OE.tool === "arrow", pn = E_ ? "muzzle" : "emit", yk = E_ ? "arrowYaw" : "emitYaw", pk = E_ ? "arrowPitch" : "emitPitch";
        const mp = M4.point(fr.M, g(pn + "X"), g(pn + "Y"), g(pn + "Z")), pc = d.map(q => q * (mp[2] / d[2])), vx = pc[0] - mp[0], vy = pc[1] - mp[1], l = Math.hypot(vx, vy); if (l < 1e-4) return;
        const ay = g(yk) * Math.PI / 180, ap = g(pk) * Math.PI / 180, cur = M4.norm(M4.dir(fr.M, -Math.cos(ap) * Math.sin(ay), Math.sin(ap), -Math.cos(ap) * Math.cos(ay)));
        let cz = cur[2], s = Math.sqrt(Math.max(0, 1 - cz * cz)); if (s < 0.2) { cz = 0; s = 1; }
        const dm = M4.norm(M4.dir(fr.Rinv, vx / l * s, vy / l * s, cz)); T.setMany({ [pre + yk]: Math.round(Math.atan2(-dm[0], -dm[2]) * 1800 / Math.PI) / 10, [pre + pk]: Math.round(Math.asin(Math.max(-1, Math.min(1, dm[1]))) * 1800 / Math.PI) / 10 });
      }
    };
    let drag = null; cv.addEventListener("pointerdown", e => { cv.setPointerCapture(e.pointerId); drag = { x: e.clientX, y: e.clientY }; if (OE.tool !== "orbit") apply(e); });
    cv.addEventListener("pointermove", e => { if (!drag) return; if (OE.tool === "orbit") { OE.yaw += (e.clientX - drag.x) * 0.6; OE.pitch = Math.max(-89, Math.min(89, OE.pitch + (e.clientY - drag.y) * 0.6)); drag = { x: e.clientX, y: e.clientY }; } else apply(e); });
    cv.addEventListener("pointerup", () => { if (drag && OE.tool !== "orbit") { markDirty(); renderInspector(); } drag = null; });
    cv.addEventListener("wheel", e => { e.preventDefault(); OE.zoom = Math.max(0.3, Math.min(4, OE.zoom * (e.deltaY > 0 ? 0.9 : 1.1))); }, { passive: false });
    S.oev = { destroy() { cancelAnimationFrame(raf); v.destroy(); } };
  }
  // type: "weapon" | "animation" (3D model) | "image" (flat picture)
  function addObject(type, asset) {
    const P = S.project.player, F = PRIM.FBX, id = Schema.uid("att", P.attachments.map(a => a.id)), att = { id }, img = type === "image", mo = !img && F.cache[asset.id], names = mo ? Object.keys(mo.anims) : [];
    const over = { type, name: asset.name.replace(/\.[^.]+$/, ""), scale: img ? 12 : mo ? Math.max(0.001, +(22 / (mo.bounds.size || 1)).toFixed(3)) : 1, idleAnim: names.find(n => /idle/i.test(n)) || "" }, fireName = type === "weapon" ? names.find(n => /shot|fire|shoot/i.test(n)) || "" : "";
    over[img ? "image" : "asset"] = asset.id; if (img) { over.x = 0; over.y = 0; }
    P.attachments.push(att); Schema.defineAttach(S.project, att, over); att.script = { nodes: [], links: [] };
    if (fireName) { const a = Schema.addSNode(S.project, "player." + id, att.script, "onClick", 30, 30), b = Schema.addSNode(S.project, "player." + id, att.script, "playAnim", 290, 30, { anim: fireName }, "player." + id + ".asset"); att.script.links.push({ a: a.id, o: "next", b: b.id }); } S.objSel = id; S.objView = true; S.coderT = { kind: "att", id }; S.tags.emit([]); markDirty(); renderHier(); renderInspector(); renderCenter(); log("Added " + type + " “" + over.name + "” to the player");
  }
  function removeObject(att) { Schema.removeAttach(S.project, att); if (S.objSel === att.id) S.objSel = null; S.tags.emit([]); markDirty(); renderHier(); renderInspector(); renderCenter(); }
  function moveObject(att, dir) {   // reorder inside its own list (weapons are numbered by this order)
    const A = S.project.player.attachments, ty = S.tags.get("player." + att.id + ".type") === "weapon", same = A.filter(a => (S.tags.get("player." + a.id + ".type") === "weapon") === ty), i = same.indexOf(att), o = same[i + dir]; if (!o) return;
    const x = A.indexOf(att), y = A.indexOf(o); A[x] = o; A[y] = att; markDirty(); S.tags.emit([]); renderHier(); renderInspector();
  }
  function objectLists() {
    const P = S.project.player, T = S.tags, models = S.project.assets.filter(a => a.type === "model"), images = S.project.assets.filter(a => a.type === "image"), nope = t => [{ t, disabled: true }];
    const row = (att, n) => { const ty = T.get("player." + att.id + ".type"), nm = T.get("player." + att.id + ".name") || att.id;
      return h("div", { class: "tla" + (S.objSel === att.id ? " sel" : ""), onclick: () => { S.objSel = att.id; S.objView = true; S.coderT = { kind: "att", id: att.id }; S.cnode = null; renderHier(); renderInspector(); renderCenter(); } },
        h("span", { text: (n ? n + "  " : "") + (ty === "weapon" ? "✚ " : ty === "image" ? "▨ " : "▣ ") + nm }),
        h("span", { class: "orow" }, h("button", { class: "mini", title: "Move up", text: "▲", onclick: e => { e.stopPropagation(); moveObject(att, -1); } }), h("button", { class: "mini", title: "Move down", text: "▼", onclick: e => { e.stopPropagation(); moveObject(att, 1); } }), h("button", { class: "mini", title: "Remove", text: "✕", onclick: e => { e.stopPropagation(); removeObject(att); } }))); };
    const ws = P.attachments.filter(a => T.get("player." + a.id + ".type") === "weapon"), os = P.attachments.filter(a => T.get("player." + a.id + ".type") !== "weapon");
    return [foldout("pl:weapons", "Weapon list", ...(ws.length ? ws.map((a, i) => row(a, i + 1)) : [h("div", { class: "note", text: "No weapons yet. In game, number keys 1-9 and the mouse wheel switch between them." })]),
        h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "+ New weapon ▾", onclick: e => popMenu(e.currentTarget, models.length ? models.map(a => ({ t: a.name, fn: () => addObject("weapon", a) })) : nope("Import an .fbx file first (Assets > Import Files)")) }))),
      foldout("pl:objects", "Other objects (animations, images)", ...(os.length ? os.map(a => row(a, 0)) : [h("div", { class: "note", text: "3D models with animations, or flat pictures, fixed to the camera (hands, a visor...)." })]),
        h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "+ Animation object ▾", onclick: e => popMenu(e.currentTarget, models.length ? models.map(a => ({ t: a.name, fn: () => addObject("animation", a) })) : nope("Import an .fbx file first")) }), h("button", { class: "btn sm", text: "+ Image object ▾", onclick: e => popMenu(e.currentTarget, images.length ? images.map(a => ({ t: a.name, fn: () => addObject("image", a) })) : nope("Import a picture first")) })))];
  }
  function selectedObjectEditor() {
    const att = S.project.player.attachments.find(a => a.id === S.objSel); if (!att) return null; const pre = "player." + att.id + ".", T = S.tags, ty = T.get(pre + "type");
    return foldout("pl:obj", "Selected: " + (T.get(pre + "name") || att.id), ...Schema.ATTACH.filter(pr => Schema.shown(T, pre, pr)).map(pr => field(pre + pr.k)),
      h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "Open Node Coder", onclick: () => { S.coderT = { kind: "att", id: att.id }; setTab("coder"); } }), ty === "weapon" ? h("button", { class: "btn sm", text: "Edit muzzle, arrow & emitter", onclick: () => { S.objView = true; renderCenter(); } }) : null),
      h("div", { class: "note", text: ty === "weapon" ? "A weapon turns its arrow toward whatever the crosshair is aiming at. Place the muzzle point and the arrow in the object editor." : "This object stays fixed to the camera." }));
  }
  // ================================================================ NODE CODER (scripts made of connected nodes, one script per object)
  function coderTarget() {
    const t = S.coderT; if (!t) return null;
    if (t.kind === "att") { const a = S.project.player.attachments.find(x => x.id === t.id); if (!a) return null; if (!a.script) a.script = { nodes: [], links: [] }; const ty = S.tags.get("player." + a.id + ".type");
      return { script: a.script, key: "player." + a.id, ctx: ty === "weapon" ? ["object", "weapon"] : ["object"], modelTag: "player." + a.id + ".asset", name: S.tags.get("player." + a.id + ".name") || a.id, what: ty === "weapon" ? "Weapon" : "Object" }; }
    const sc = screens().find(x => x.id === t.screen), n = sc && sc.nodes.find(x => x.id === t.id); if (!n) return null; if (sc.kind === "splash") return { splash: true, name: n.id };
    if (!n.script) n.script = { nodes: [], links: [] }; return { script: n.script, key: sc.id + "." + n.id, ctx: ["screen"], modelTag: null, name: n.id, what: Schema.NODE[n.type].label + " in “" + sc.id + "”" };
  }
  const NCW = 210, NCH = 24, NCS = 18, NCR = 22;
  const ncOutY = (n, j) => n.y + NCH + NCS + 11 + j * NCR, ncHeight = n => NCH + NCS + Math.max(1, Schema.SNODES[n.type].outs.length) * NCR + 4;
  function ncSummary(tg, n) {
    const T = S.tags, g = k => T.get(Schema.snodeKey(tg.key, n.id, k)), asset = id => { const a = S.project.assets.find(x => x.id === id); return a ? a.name : "(none)"; };
    const kl = v => { const o = Schema.KEYS.find(x => x[0] === String(v).toLowerCase()); return o ? o[1] : v; };
    switch (n.type) { case "onCharge": return "hold " + kl(g("key")) + " to charge"; case "fireProjectile": return "uses the weapon’s Projectile settings"; case "onKey": case "onKeyUp": case "ifKey": return kl(g("key")); case "onHold": return kl(g("key")) + " · every " + g("interval") + " s"; case "setTag": return (g("tag") || "(pick a tag)") + " = " + g("value"); case "changeTag": return (g("tag") || "(pick a tag)") + " " + (+g("amount") >= 0 ? "+ " : "− ") + Math.abs(+g("amount"));
      case "playSound": return asset(g("sound")); case "playAnim": return (g("anim") || "(pick one)") + (g("loop") ? " · loops" : ""); case "gotoScreen": return g("screen") || "(pick a screen)"; case "say": return "“" + g("text") + "”";
      case "wait": return g("seconds") + " s"; case "if": return (g("tag") || "(pick a tag)") + " " + g("op") + " " + g("value"); case "repeat": return g("times") + " times"; default: return ""; }
  }
  function coderCenter() {
    const p = E.center, tg = coderTarget(); p.tabs.replaceChildren(...centerTabs(), h("span", { class: "sp" }), h("span", { class: "csize", text: "node coder" }));
    E.host.className = "host maphost";
    if (!tg || tg.splash) { put(E.cbar, h("span", { class: "chip", text: "Node Coder" })); E.host.replaceChildren(h("div", { class: "v3err", style: "color:var(--muted);text-align:center;padding:20px", text: tg && tg.splash ? "Splash elements can't have scripts." : "Select an element (in the Hierarchy) or a weapon / object under Player, and its script opens here." })); return; }
    const V = S.cview = S.cview || { px: 30, py: 30 }, sc = tg.script, T = S.tags, ns = "http://www.w3.org/2000/svg";
    const allowed = Object.keys(Schema.SNODES).filter(k => Schema.SNODES[k].ctx.some(c => tg.ctx.includes(c)));
    const add = type => {   // a new node goes to the right of the selected one and is connected to it when that has a free first output
      const base = sc.nodes.find(n => n.id === S.cnode) || (sc.nodes.length && !Schema.SNODES[type].cat.startsWith("event") ? sc.nodes[sc.nodes.length - 1] : null), ev = Schema.SNODES[type].ins === false;
      const x = base && !ev ? base.x + NCW + 50 : ev ? 40 - V.px + 20 * (sc.nodes.filter(n => Schema.SNODES[n.type].ins === false).length % 5) : 40 - V.px, y = base && !ev ? base.y : ev ? 40 - V.py + 150 * sc.nodes.filter(n => Schema.SNODES[n.type].ins === false).length : 40 - V.py;
      const node = Schema.addSNode(S.project, tg.key, sc, type, x, y, null, tg.modelTag);
      if (base && !ev) { const o = Schema.SNODES[base.type].outs[0]; if (!sc.links.some(l => l.a === base.id && l.o === o)) sc.links.push({ a: base.id, o, b: node.id }); }
      S.cnode = node.id; S.clink = null; markDirty(); S.tags.emit([]); renderInspector(); draw();
    };
    const menu = []; for (const [cat, label] of Object.entries(Schema.SCAT)) { const subs = allowed.filter(k => Schema.SNODES[k].cat === cat).map(k => ({ t: Schema.SNODES[k].label, fn: () => add(k) })); if (subs.length) menu.push({ t: label, sub: subs }); }
    S.coderMenu = menu;
    put(E.cbar, h("span", { class: "chip", text: tg.what + " · " + tg.name }), h("button", { class: "tbtn sm", text: "+ Add node ▾ (Shift+A)", onclick: e => popMenu(e.currentTarget, menu) }), h("button", { class: "tbtn sm", text: "Delete selected", onclick: () => coderDelete() }),
      h("span", { class: "hint", text: "Drag a node by its title · drag from a node's round dot to another node's left dot to connect · drag the background to move around · Delete removes the selected node or line" }));
    const world = h("div", { class: "ncworld" }), svg = document.createElementNS(ns, "svg"); svg.setAttribute("class", "nclinks"); const view = h("div", { class: "nc", tabindex: "0" }, world);
    E.host.replaceChildren(view);
    const path = (x1, y1, x2, y2) => "M" + x1 + " " + y1 + " C" + (x1 + 70) + " " + y1 + " " + (x2 - 70) + " " + y2 + " " + x2 + " " + y2;
    const mk = (cls, d) => { const e = document.createElementNS(ns, "path"); e.setAttribute("d", d); e.setAttribute("class", cls); return e; };
    function draw() {
      world.style.transform = "translate(" + V.px + "px," + V.py + "px)"; const els = [svg];
      svg.replaceChildren(...sc.links.map((l, i) => { const a = sc.nodes.find(n => n.id === l.a), b = sc.nodes.find(n => n.id === l.b); if (!a || !b) return null; const j = Schema.SNODES[a.type].outs.indexOf(l.o), d = path(a.x + NCW, ncOutY(a, j), b.x, b.y + 12);
        const hit = mk("ncl-hit", d), vis = mk("ncl" + (S.clink === i ? " sel" : ""), d); hit.addEventListener("pointerdown", e => { e.stopPropagation(); S.clink = i; S.cnode = null; renderInspector(); draw(); }); return [vis, hit]; }).flat().filter(Boolean));
      for (const n of sc.nodes) { const def = Schema.SNODES[n.type], card = h("div", { class: "ncn " + def.cat + (S.cnode === n.id ? " sel" : ""), style: "left:" + n.x + "px;top:" + n.y + "px;width:" + NCW + "px;height:" + ncHeight(n) + "px" },
          h("div", { class: "nch", text: def.label }), h("div", { class: "ncs", text: ncSummary(tg, n) }),
          ...def.outs.map((o, j) => h("div", { class: "nco", style: "top:" + (NCH + NCS + j * NCR) + "px" }, h("span", { text: o === "next" ? "" : o }), h("i", { class: "ncdot", "data-out": o, "data-id": n.id }))));
        if (def.ins !== false) card.append(h("i", { class: "ncin", "data-id": n.id }));
        card.addEventListener("pointerdown", e => {
          const dot = e.target.closest && e.target.closest(".ncdot"); e.stopPropagation(); view.focus();
          if (dot) { const o = dot.dataset.out, wr = world.getBoundingClientRect(), x1 = n.x + NCW, y1 = ncOutY(n, def.outs.indexOf(o)); const tmp = mk("ncl tmp", ""); svg.append(tmp); card.setPointerCapture(e.pointerId);
            card.onpointermove = m => tmp.setAttribute("d", path(x1, y1, m.clientX - wr.left, m.clientY - wr.top));
            card.onpointerup = u => { card.onpointermove = card.onpointerup = null; const t = document.elementFromPoint(u.clientX, u.clientY), tin = t && t.closest && t.closest(".ncin");
              sc.links = sc.links.filter(l => !(l.a === n.id && l.o === o)); if (tin && tin.dataset.id !== n.id) sc.links.push({ a: n.id, o, b: tin.dataset.id }); markDirty(); draw(); }; return; }
          S.cnode = n.id; S.clink = null; renderInspector(); const x0 = e.clientX, y0 = e.clientY, nx = n.x, ny = n.y; card.setPointerCapture(e.pointerId); let moved = false;
          card.onpointermove = m => { moved = true; n.x = Math.round(nx + m.clientX - x0); n.y = Math.round(ny + m.clientY - y0); card.style.left = n.x + "px"; card.style.top = n.y + "px"; svgLinks(); };
          card.onpointerup = () => { card.onpointermove = card.onpointerup = null; if (moved) markDirty(); draw(); };
          for (const c of world.querySelectorAll(".ncn.sel")) c.classList.remove("sel"); card.classList.add("sel");
        }); els.push(card); }
      world.replaceChildren(...els); const em = view.querySelector(".ncempty"); if (em) em.style.display = sc.nodes.length ? "none" : "";
    }
    function svgLinks() { const keep = svg.querySelector(".tmp"); svg.replaceChildren(...sc.links.map((l, i) => { const a = sc.nodes.find(n => n.id === l.a), b = sc.nodes.find(n => n.id === l.b); if (!a || !b) return null; const j = Schema.SNODES[a.type].outs.indexOf(l.o), d = path(a.x + NCW, ncOutY(a, j), b.x, b.y + 12), vis = mk("ncl" + (S.clink === i ? " sel" : ""), d); return vis; }).filter(Boolean)); if (keep) svg.append(keep); }
    view.addEventListener("pointerdown", e => { if (e.target !== view && e.target !== world) return; S.cnode = null; S.clink = null; view.focus(); const x0 = e.clientX, y0 = e.clientY, px = V.px, py = V.py; view.setPointerCapture(e.pointerId); view.style.cursor = "grabbing";
      view.onpointermove = m => { V.px = px + m.clientX - x0; V.py = py + m.clientY - y0; world.style.transform = "translate(" + V.px + "px," + V.py + "px)"; }; view.onpointerup = () => { view.onpointermove = view.onpointerup = null; view.style.cursor = ""; renderInspector(); draw(); }; });
    view.addEventListener("keydown", e => { if ((e.key === "Delete" || e.key === "Backspace") && !/INPUT|SELECT|TEXTAREA/.test(e.target.tagName)) { e.preventDefault(); coderDelete(); } });
    if (!sc.nodes.length) view.append(h("div", { class: "ncempty", text: "Empty script. Press “+ Add node” and start with a green event (like “When clicked”), then add actions and drag a line from its dot to the next node." }));
    const un = T.on(() => { if (S.cdraw) draw(); }); S.cdraw = true; S.coderStop = () => { un(); S.cdraw = false; }; draw();
  }
  function coderDelete() {
    const tg = coderTarget(); if (!tg || tg.splash) return; const sc = tg.script;
    if (S.clink != null && sc.links[S.clink]) { sc.links.splice(S.clink, 1); S.clink = null; markDirty(); return renderCenter(); }
    const n = sc.nodes.find(x => x.id === S.cnode); if (!n) return; Schema.removeSNode(S.project, tg.key, sc, n); S.cnode = null; S.tags.emit([]); markDirty(); renderInspector(); renderCenter();
  }
  function coderInspector() {
    const tg = coderTarget(), box = h("div", { class: "insp" }); if (!tg || tg.splash) { box.append(h("div", { class: "note", text: "Select an element or a player object to edit its script." })); return box; }
    box.append(h("div", { class: "ihead" }, h("span", { class: "bigic", text: "⬡" }), h("div", { class: "ititle" }, h("div", { class: "idin ro", text: tg.name }), h("div", { class: "isub", text: "Script of the " + tg.what + " · " + tg.script.nodes.length + " nodes" }))));
    const n = tg.script.nodes.find(x => x.id === S.cnode);
    if (n) { const def = Schema.SNODES[n.type]; box.append(foldout("nc:node", def.label, ...def.props.map(pr => field(Schema.snodeKey(tg.key, n.id, pr.k))), h("div", { class: "btnrow" }, h("button", { class: "btn sm danger", text: "Delete node", onclick: () => coderDelete() })))); if (!def.props.length) box.append(h("div", { class: "note", text: def.cat === "event" ? "This starts a chain. Connect the next node to its dot." : "Nothing to set here. Connect nodes to its dots." })); }
    else box.append(h("div", { class: "note", text: "Click a node to change its values here. Events (green) start a script, actions (blue) do something, logic (orange) decides or repeats." }));
    box.append(foldout("nc:help", "How scripts run", h("div", { class: "note", text: "Green events start a chain. The nodes connected after it run one by one. “If” follows the then or else line. “Repeat” runs its body line several times, then continues with next. Weapons only listen while they are the weapon in your hand." })));
    return box;
  }
  // ---- Timeline: pick a model animation, scrub it with a live 3D preview, and put sounds on it (project.events + ev<N>.* tags)
  const tlState = () => S.tl = S.tl || { t: 0, playing: false, yaw: -35 };
  function tlSelection() {
    const TL = tlState(), models = S.project.assets.filter(a => a.type === "model"); let asset = models.find(a => a.id === TL.asset) || models[0];
    const mo = asset && PRIM.FBX.cache[asset.id], names = mo ? Object.keys(mo.anims) : [];
    if (asset && TL.asset !== asset.id) { TL.asset = asset.id; TL.anim = null; } if (mo && !names.includes(TL.anim)) { TL.anim = names[0] || null; TL.t = 0; }
    return { TL, models, asset, mo, anim: mo && TL.anim ? mo.anims[TL.anim] : null, name: TL.anim };
  }
  const tlEvents = () => { const q = tlSelection(); return q.asset && q.name ? S.project.events.filter(e => e.asset === q.asset.id && e.anim === q.name) : []; };
  function addSoundEvent(snd) {
    const q = tlSelection(); if (!q.anim) return; const ev = { id: (() => { const used = reserved(); let n = 1; while (used.includes("ev" + n)) n++; return "ev" + n; })(), asset: q.asset.id, anim: q.name };
    S.project.events.push(ev); Schema.defineEvent(S.project, ev, { time: +Math.min(q.TL.t, Math.max(0, q.anim.dur - 0.01)).toFixed(2), sound: snd.id }); q.TL.evSel = ev.id; S.tags.emit([]); markDirty(); renderInspector(); tlGrid(); log("Added sound “" + snd.name + "” to " + q.name);
  }
  function tlGrid() {
    const g = S.tlGrid; if (!g) return; const { TL, anim, mo } = tlSelection(), LW = 130, W = Math.max(200, g.clientWidth - LW - 20), dur = anim ? Math.max(anim.dur, 0.05) : 1, pps = W / dur, X = t => LW + t * pps, evs = tlEvents();
    const tick = dur > 8 ? 1 : dur > 3 ? 0.5 : dur > 1 ? 0.25 : 0.1, ruler = h("div", { class: "tlrow tlrul" }, h("div", { class: "tllab", text: "seconds" }));
    for (let t = 0; t <= dur + 1e-6; t += tick) ruler.append(h("div", { class: "tltick", style: "left:" + X(t) + "px", text: +t.toFixed(2) }));
    const sounds = h("div", { class: "tlrow tlsnd" }, h("div", { class: "tllab", text: "♪ Sounds" }));
    for (const e of evs) {
      const sid = S.tags.get(e.id + ".sound"), a = S.project.assets.find(x => x.id === sid), m = h("div", { class: "tlmark" + (TL.evSel === e.id ? " sel" : ""), title: (a ? a.name : "no sound") + " @ " + (+S.tags.get(e.id + ".time")).toFixed(2) + " s", style: "left:" + X(+S.tags.get(e.id + ".time") || 0) + "px", text: "♪" });
      m.addEventListener("pointerdown", ev => {
        ev.stopPropagation(); ev.preventDefault(); TL.evSel = e.id; m.setPointerCapture(ev.pointerId); const x0 = ev.clientX, t0 = +S.tags.get(e.id + ".time") || 0; let moved = false;
        m.onpointermove = mv => { moved = true; const t = Math.max(0, Math.min(dur - 0.01, t0 + (mv.clientX - x0) / pps)); S.tags.set(e.id + ".time", +t.toFixed(2)); m.style.left = X(t) + "px"; };
        m.onpointerup = () => { m.onpointermove = m.onpointerup = null; markDirty(); renderInspector(); tlGrid(); if (!moved && a && S.urls[a.id]) playSound(a.id); };
      }); sounds.append(m);
    }
    const rows = [], order = []; if (anim && mo) for (const tr of anim.tracks) { let r = order.find(o => o.n === tr.n); if (!r) order.push(r = { n: tr.n, ts: new Set() }); for (const c of tr.c) if (c) for (const t of c.t) r.ts.add(t); }
    for (const r of order) { const row = h("div", { class: "tlrow" }, h("div", { class: "tllab", title: mo.nodes[r.n].name, text: mo.nodes[r.n].name })); for (const t of r.ts) row.append(h("div", { class: "tlkey", style: "left:" + X(t) + "px" })); rows.push(row); }
    const head = h("div", { class: "tlhead" }, ruler, sounds), body = h("div", { class: "tlbody" }, ...rows);
    if (!anim) body.append(h("div", { class: "hint", style: "padding:10px", text: mo ? "This model has no animations." : "Loading the model…" }));
    const ph = h("div", { class: "tlplayhead" }); S.tlPlayhead = ph; ph.style.left = X(TL.t) + "px"; S.tlX = X;
    const scrub = ev => { const r = g.getBoundingClientRect(); TL.t = Math.max(0, Math.min(dur, (ev.clientX - r.left - LW) / pps)); TL.playing = false; S.tlTick && S.tlTick(0); };
    for (const el of [ruler, sounds, body]) el.addEventListener("pointerdown", ev => { if (ev.target.closest(".tlmark")) return; scrub(ev); el.setPointerCapture(ev.pointerId); el.onpointermove = scrub; el.onpointerup = () => { el.onpointermove = el.onpointerup = null; }; });
    put(g, head, body, ph);
  }
  function timelineCenter() {
    const p = E.center; p.tabs.replaceChildren(...centerTabs(), h("span", { class: "sp" })); const q = tlSelection(), { TL, models } = q, F = PRIM.FBX;
    const list = h("div", { class: "tllist" });
    let c = 0; for (const a of models) { list.append(h("div", { class: "tlm", text: "▪ " + a.name })); const mo = F.cache[a.id]; if (!mo) list.append(h("div", { class: "hint", style: "padding:2px 14px", text: a.meta && a.meta.error ? "Could not read this file" : "Loading…" }));
      else for (const n of Object.keys(mo.anims)) list.append(h("div", { class: "tla" + (TL.asset === a.id && TL.anim === n ? " sel" : ""), onclick: () => { TL.asset = a.id; TL.anim = n; TL.t = 0; TL.evSel = null; TL.playing = false; renderInspector(); renderCenter(); } }, h("span", { text: n }), h("span", { class: "hint", text: mo.anims[n].dur.toFixed(2) + " s" + ((c = S.project.events.filter(e => e.asset === a.id && e.anim === n).length) ? "  ♪ " + c : "") }))); }
    if (!models.length) list.append(h("div", { class: "hint", style: "padding:10px", text: "Import an .fbx model (Assets > Import Files) and its animations appear here." }));
    const cv = h("canvas", { class: "tlcv" }), time = h("span", { class: "chip", text: "0.00 s" }), snds = S.project.assets.filter(a => a.type === "sound");
    const playBtn = h("button", { class: "tbtn sm", text: "▶ Play", onclick: () => { if (!q.anim) return; TL.playing = !TL.playing; if (TL.playing && TL.t >= q.anim.dur - 0.01) TL.t = 0; playBtn.textContent = TL.playing ? "❚❚ Pause" : "▶ Play"; } });
    put(E.cbar, h("span", { class: "chip", text: "Timeline" }), playBtn, h("button", { class: "tbtn sm", text: "⏮", title: "Back to the start", onclick: () => { TL.t = 0; TL.playing = false; playBtn.textContent = "▶ Play"; if (S.tlTick) S.tlTick(0); } }), time,
      h("button", { class: "tbtn sm", disabled: !q.anim, text: "+ Add sound at playhead ▾", onclick: e => popMenu(e.currentTarget, snds.length ? snds.map(a => ({ t: a.name, fn: () => addSoundEvent(a) })) : [{ t: "Import a sound file first (Assets > Import Files)", disabled: true }]) }),
      h("span", { class: "hint", text: "Click or drag in the timeline to scrub · drag a ♪ to move it · click a ♪ to hear it" }));
    const grid = h("div", { class: "tlgrid" }); S.tlGrid = grid;
    E.host.className = "host maphost"; E.host.replaceChildren(h("div", { class: "tl" }, list, h("div", { class: "tlmain" }, h("div", { class: "tlprev" }, cv), grid)));
    let v = null, gm = null, raf = 0, last = performance.now(), ro = null, prevT = TL.t; const M4 = PRIM.M4;
    if (q.mo) { v = new PRIM.R3D.View(cv); if (v.ok) { gm = v.loadModel(q.mo); v.sky = [0.1, 0.12, 0.17]; v.fov = 0.8; } else v = null; }
    const tick = dt => {
      const an = tlSelection().anim; if (an && TL.playing) { const t0 = TL.t; TL.t += dt; if (TL.t >= an.dur) { TL.t = an.dur > 0 ? TL.t % an.dur : 0; } tlSound(t0, TL.t, an.dur); }
      time.textContent = TL.t.toFixed(2) + " / " + (an ? an.dur.toFixed(2) : "0.00") + " s"; if (S.tlPlayhead && S.tlX) S.tlPlayhead.style.left = S.tlX(TL.t) + "px";
      if (v && q.anim) { const b = q.mo.bounds, c = [0, 1, 2].map(i => (b.lo[i] + b.hi[i]) / 2), k = 22 / (b.size || 1);
        v.draw(); v.drawViewmodel([{ gm, pose: PRIM.FBX.pose(q.mo, q.name, TL.t), M: M4.mul(M4.translate(0, 0, -34), M4.mul(M4.rotX(-12), M4.mul(M4.rotY(TL.yaw), M4.mul(M4.scale(k, k, k), M4.translate(-c[0], -c[1], -c[2]))))) }], []); }
    };
    function tlSound(t0, t1, dur) {   // events passed since last frame (also across the loop point)
      for (const e of tlEvents()) { const t = +S.tags.get(e.id + ".time") || 0, hit = t1 >= t0 ? (t >= t0 && t < t1) : (t >= t0 || t < t1); const sid = S.tags.get(e.id + ".sound"); if (hit && sid && S.urls[sid]) { const a = new Audio(S.urls[sid]); a.volume = Math.max(0, Math.min(1, (+S.tags.get("audio.master") || 0) * (+S.tags.get("audio.sfx") || 0) * (+S.tags.get(e.id + ".volume") || 0))); a.play().catch(() => {}); } }
    }
    S.tlTick = tick; const loop = t => { raf = requestAnimationFrame(loop); const dt = Math.min(0.05, (t - last) / 1000); last = t; tick(dt); playBtn.textContent = TL.playing ? "❚❚ Pause" : "▶ Play"; }; raf = requestAnimationFrame(loop);
    let drag = null; cv.addEventListener("pointerdown", e => { drag = e.clientX; cv.setPointerCapture(e.pointerId); }); cv.addEventListener("pointermove", e => { if (drag !== null) { TL.yaw += (e.clientX - drag) * 0.6; drag = e.clientX; } }); cv.addEventListener("pointerup", () => { drag = null; });
    ro = new ResizeObserver(() => tlGrid()); ro.observe(grid); tlGrid();
    S.tlv = { destroy() { cancelAnimationFrame(raf); if (ro) ro.disconnect(); if (v) v.destroy(); S.tlTick = null; S.tlGrid = null; S.tlPlayhead = null; } };
  }
  function timelineInspector() {
    const q = tlSelection(), box = h("div", { class: "insp" }), evs = tlEvents(), TL = q.TL;
    box.append(h("div", { class: "ihead" }, h("span", { class: "bigic", text: "♪" }), h("div", { class: "ititle" }, h("div", { class: "idin ro", text: q.name || "Timeline" }), h("div", { class: "isub", text: q.asset ? q.asset.name + (q.anim ? " · " + q.anim.dur.toFixed(2) + " s · " + q.anim.tracks.length + " tracks" : "") : "No model yet" }))));
    const items = evs.length ? evs.map(e => { const a = S.project.assets.find(x => x.id === S.tags.get(e.id + ".sound")); return h("div", { class: "tla" + (TL.evSel === e.id ? " sel" : ""), onclick: () => { TL.evSel = e.id; renderInspector(); tlGrid(); } }, h("span", { text: "♪ " + (a ? a.name : "no sound") }), h("span", { class: "hint", text: (+S.tags.get(e.id + ".time")).toFixed(2) + " s" })); }) : [h("div", { class: "note", text: "No sounds on this animation yet. Press “+ Add sound at playhead” above." })];
    box.append(foldout("tl:list", "Sounds on this animation", ...items));
    const ev = evs.find(e => e.id === TL.evSel);
    if (ev) box.append(foldout("tl:ev", "Selected sound · " + ev.id, ...Schema.EVENT.map(pr => field(ev.id + "." + pr.k)), h("div", { class: "btnrow" }, h("button", { class: "btn sm danger", text: "Remove", onclick: () => { Schema.removeEvent(S.project, ev); TL.evSel = null; S.tags.emit([]); markDirty(); renderInspector(); tlGrid(); } }))));
    box.append(h("div", { class: "note", text: "While the game runs, each sound plays at its time whenever this animation plays (idle loop or click animation). Volume = master × SFX × the sound's own volume." }));
    return box;
  }
  function playerInspector() {
    const T = S.tags, box = h("div", { class: "insp" }), hudId = T.get("player.hud");
    box.append(h("div", { class: "ihead" }, h("span", { class: "bigic", text: "◉" }), h("div", { class: "ititle" }, h("div", { class: "idin ro", text: "Player" }), h("div", { class: "isub", text: "The one player entity · every value is a player.* tag" }))));
    box.append(...objectLists()); const sel = selectedObjectEditor(); if (sel) box.append(sel);
    for (const [title, keys] of Schema.PLAYER_GROUPS) {
      const rows = keys.filter(k => { const r = T.spec("player." + k); return r && Schema.shown(T, "player.", r); }).map(k => field("player." + k));
      if (title === "HUD") { const sc = screens().find(x => x.id === hudId); rows.push(h("div", { class: "btnrow" }, h("button", { class: "btn sm", disabled: !sc, text: "Edit HUD screen", onclick: () => sc && selectScreen(sc.id) }))); rows.push(h("div", { class: "note", text: "The HUD is a screen like any other: add Bars, Titles and Images to it. Write {player.health} in a text to show a live value." })); }
      box.append(foldout("pl:" + title, title, ...rows));
    }
    box.append(h("div", { class: "note", text: "Test keys while walking: H hurts 10, J heals 10. Click fires the current weapon; 1-9 or the wheel switch weapons." }));
    return box;
  }
  function dropOnMap(e, a) {
    if (!S.med) return log("Switch to the Map tab to texture walls.", "warn"); const r = S.med.cv.getBoundingClientRect(), hit = S.med.pick(e.clientX - r.left, e.clientY - r.top), m = curMap();
    if (!hit || hit.kind === "start") return log("Drop the image onto a wall (to texture it) or onto a room (to texture its floor, or Shift for the ceiling).", "warn");
    if (hit.kind === "vert") { S.tags.set(m.id + "." + hit.id + ".tex", a.id); S.med.select({ kind: "vert", id: hit.id }); log("Wall texture set to " + a.name); }
    else { S.tags.set(m.id + "." + hit.id + "." + (e.shiftKey ? "ceilTex" : "floorTex"), a.id); S.med.select({ kind: "sector", id: hit.id }); log((e.shiftKey ? "Ceiling" : "Floor") + " texture set to " + a.name); }
    renderInspector();
  }
  function mapInspector() {
    const m = curMap(), box = h("div", { class: "insp" }), sel = S.msel, pre = m.id + ".", vert = id => m.verts.find(v => v.id === id);
    const head = (ic, title, sub) => h("div", { class: "ihead" }, h("span", { class: "bigic", text: ic }), h("div", { class: "ititle" }, title, h("div", { class: "isub", text: sub })));
    const coord = (v, k) => { const inp = h("input", { class: "num", type: "number", step: 1, value: v[k], "aria-label": "Node " + k.toUpperCase(), onchange: e => { const n = Math.round(parseFloat(e.target.value)); if (isFinite(n)) { v[k] = n; markDirty(); if (S.med) S.med.redraw(); } } }); return h("div", { class: "fld" }, h("label", { class: "flab", text: k.toUpperCase() }), h("div", { class: "fctl" }, inp)); };
    if (sel && sel.kind === "vert" && vert(sel.id)) {
      const v = vert(sel.id), sec = m.sectors.find(s => s.v.includes(v.id)), nxt = sec ? vert(sec.v[(sec.v.indexOf(v.id) + 1) % sec.v.length]) : null;
      box.append(head("◆", h("div", { class: "idin ro", text: "Node " + v.id }), "Its wall runs to node " + (nxt ? nxt.id : "?") + (sec ? " · room “" + sec.id + "”" : "")),
        h("div", { class: "btnrow" }, h("button", { class: "btn sm danger", text: "Delete node", onclick: () => S.med && S.med.deleteSelection() })),
        foldout("mv", "Wall (this node to the next)", ...Schema.VERT.map(p => field(pre + v.id + "." + p.k))), foldout("mpos", "Position", coord(v, "x"), coord(v, "y")),
        h("div", { class: "note", text: "Tip: drag an image from the Project panel onto the wall in the Map view to texture it." }));
    } else if (sel && sel.kind === "sector" && m.sectors.some(s => s.id === sel.id)) {
      box.append(head("▱", h("div", { class: "idin ro", text: "Room " + sel.id }), m.sectors.find(s => s.id === sel.id).v.length + " nodes"),
        h("div", { class: "btnrow" }, h("button", { class: "btn sm danger", text: "Delete room", onclick: () => S.med && S.med.deleteSelection() })),
        foldout("ms", "Room", ...Schema.SECTOR.map(p => field(pre + sel.id + "." + p.k))), h("div", { class: "note", text: "Click one of the room's walls or nodes to texture that wall. Tip: drop an image on the room to set its floor (Shift = ceiling)." }));
    } else if (sel && sel.kind === "start") {
      box.append(head("➤", h("div", { class: "idin ro", text: "Player start" }), "Where the player begins"), foldout("mst", "Start", ...["startX", "startY", "startAngle"].map(k => field(pre + k))));
    } else {
      box.append(head("▦", h("input", { class: "idin", value: m.id, "aria-label": "Map name", onchange: e => renameMap(m, e.target.value) }), m.style.toUpperCase() + " style · " + m.sectors.length + " room(s) · " + m.verts.length + " nodes"),
        h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "Open Fly Editor", onclick: () => setTab("fly") }), h("button", { class: "btn sm", text: "Fullscreen fly", onclick: () => fly3d(m) }), h("button", { class: "btn sm danger", text: "Delete map", onclick: () => deleteMap(m) })),
        foldout("mm", "Map", ...Schema.MAP.map(p => field(pre + p.k))), h("div", { class: "note", text: "Click a room, wall or node in the Map view to edit it. Every value is a named tag (see the Tags tab)." }));
    }
    return box;
  }

  // ================================================================ PLAY (fullscreen, remembers audio/graphics settings)
  function play() {
    autosave(); const layer = h("div", { class: "play-layer" }); document.body.append(layer);
    const tg = S.tags.fork(), saved = Store.loadSettings(S.project.id); for (const k in saved) if (S.tags.has(k)) tg.ov[k] = saved[k];
    let start = S.tags.get("project.startScreen"); if (!screens().some(s => s.id === start)) start = screens()[0].id;
    if (S.rt) S.rt.stopMusic();
    const rt = new PRIM.Runtime({ host: layer, project: S.project, tags: tg, mode: "play", urls: S.urls, screenId: start, onPersist: ov => Store.saveSettings(S.project.id, ov), onExit: close, onStartGame: startGame, say: t => log(t) });
    function close() { rt.destroy(); layer.remove(); if (document.fullscreenElement) document.exitFullscreen().catch(() => {}); }
    layer.__rt = rt;
  }

  // ================================================================ keyboard
  addEventListener("keydown", e => {
    if (!S.project) return; const typing = /INPUT|SELECT|TEXTAREA/.test((e.target && e.target.tagName) || ""), mod = e.ctrlKey || e.metaKey;
    if (mod && e.key.toLowerCase() === "s") { e.preventDefault(); saveProject(e.shiftKey); }
    else if (mod && e.key.toLowerCase() === "o") { e.preventDefault(); openFileDialog(); }
    else if (e.key === "Escape") { closePop(); const md = document.querySelector(".modal"); if (md) md.remove(); }
    else if (typing) return;
    else if (document.querySelector(".p3layer,.play-layer")) return;
    else if (e.key === "F2") { e.preventDefault(); startRename(); }
    else if (e.shiftKey && !mod && e.key.toLowerCase() === "a" && S.tab === "coder" && S.coderMenu && S.coderMenu.length) { e.preventDefault(); const m = S.mouse || { x: 300, y: 200 }; popMenu(null, S.coderMenu, { x: m.x, y: m.y }); }
    else if (e.key === "Delete" || e.key === "Backspace") { if (document.querySelector(".play-layer")) return; e.preventDefault(); deleteSelected(); }
    else if (mod && e.key.toLowerCase() === "d") { e.preventDefault(); duplicateSelected(); }
  });
  addEventListener("beforeunload", () => { if (S.project) autosave(); });

  window.__studio = { S, Store, Host, openProject, renderManager, openBytes, saveProject };
  renderManager();
})();
