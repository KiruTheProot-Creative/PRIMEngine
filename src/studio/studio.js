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
    image: '<svg viewBox="0 0 32 32"><path d="M3 5h26v22H3z" style="fill:var(--filebg)" stroke="rgba(0,0,0,.5)"/><path d="M5 25l7-9 5 6 3-3 7 6z" fill="#5f9a5f"/><circle cx="22" cy="11" r="2.5" fill="#d9b441"/></svg>',
  };
  const ico = (k, cls) => h("span", { class: "ico " + (cls || ""), html: ICONS[k] || ICONS.file });

  // ---------------------------------------------------------------- log / toast
  function log(msg, kind) { S.log.push({ t: new Date(), msg, kind: kind || "info" }); if (S.log.length > 300) S.log.shift(); if (E.status) renderStatus(); if (E.console && S.btab === "console") renderProject(); if (kind === "error" || !S.project) toast(msg, kind); }
  function toast(msg, kind) { let t = document.getElementById("toast"); if (!t) { t = h("div", { id: "toast", role: "status" }); document.body.append(t); } t.textContent = msg; t.className = "show " + (kind || ""); clearTimeout(toast.t); toast.t = setTimeout(() => { t.className = ""; }, 4500); }

  // ---------------------------------------------------------------- popup menus
  function closePop() { if (E.pop) { E.pop.remove(); E.pop = null; E.popAnchor = null; } document.querySelectorAll(".mb.open").forEach(b => b.classList.remove("open")); }
  function popMenu(anchor, items) {
    closePop(); const r = anchor.getBoundingClientRect();
    const pop = h("div", { class: "pop", role: "menu" }, items.map(it => it === "-" ? h("div", { class: "sep" }) : h("button", { class: "pitem", role: "menuitem", disabled: !!it.disabled, onclick: () => { closePop(); if (it.fn) it.fn(); } }, h("span", { text: (it.check ? "✓  " : "") + it.t }), it.k ? h("span", { class: "k", text: it.k }) : null)));
    document.body.append(pop); pop.style.left = Math.max(4, Math.min(r.left, innerWidth - pop.offsetWidth - 4)) + "px"; pop.style.top = r.bottom + "px"; E.pop = pop; E.popAnchor = anchor; anchor.classList.add("open");
  }
  document.addEventListener("pointerdown", e => { if (E.pop && !E.pop.contains(e.target) && !(E.popAnchor && E.popAnchor.contains(e.target))) closePop(); });

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
    S.project = p; S.tags = new Tags(p.tags); S.urls = await Store.loadAssets(p); S.screenId = p.screens.find(s => s.id === "main") ? "main" : p.screens[0].id;
    Object.assign(S, { mapId: null, msel: null, selId: null, sel: null, folder: "", testing: false, paused: false, rtab: "inspector", btab: "project", search: "", renaming: null, collapsed: {}, treeClosed: {}, fileDirty: false, file: p.fileName ? { ref: null, name: p.fileName } : null });
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
      { label: "Screen", items: [{ t: "Create Screen", fn: addScreen }, { t: "Create Map…", fn: openNewMap }, "-", ...(sc.kind === "splash" ? [mk("splash")] : ["title", "button", "image", "slider", "toggle"].map(mk))] },
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
  function setTesting(v) { S.testing = v; if (!v) S.paused = false; renderToolbar(); renderCenter(); }

  // ---------------------------------------------------------------- hierarchy
  function renderHier() {
    const p = E.hier, sc = curScreen();
    p.tabs.replaceChildren(h("div", { class: "tab sel", text: "Hierarchy" }), h("span", { class: "sp" }), h("button", { class: "tbtn sm", text: "Create ▾", onclick: e => popMenu(e.currentTarget, [{ t: "Screen", fn: addScreen }, { t: "Map…", fn: openNewMap }, "-", ...(sc.kind === "splash" ? [{ t: "Splash", fn: () => addNode("splash") }] : ["title", "button", "image", "slider", "toggle"].map(t => ({ t: Schema.NODE[t].label, fn: () => addNode(t) })))]) }));
    const rows = [];
    for (const s of screens()) {
      const open = !S.collapsed[s.id], isSel = s.id === S.screenId && !S.selId && !S.sel && !S.mapId;
      rows.push(h("div", { class: "hrow" + (isSel ? " sel" : ""), style: "padding-left:4px", onclick: () => selectScreen(s.id) },
        h("button", { class: "tri", "aria-label": open ? "Collapse" : "Expand", text: s.nodes.length ? (open ? "▾" : "▸") : "", onclick: e => { e.stopPropagation(); S.collapsed[s.id] = open; renderHier(); } }),
        h("span", { class: "hic", text: s.kind === "splash" ? "✦" : "▭" }), h("span", { class: "hnm", text: s.id }), S.tags.get("project.startScreen") === s.id ? h("span", { class: "hmeta", text: "start" }) : null));
      if (open) for (const n of s.nodes) rows.push(h("div", { class: "hrow" + (s.id === S.screenId && n.id === S.selId && !S.sel && !S.mapId ? " sel" : ""), style: "padding-left:22px", onclick: () => selectNode(s.id, n.id) },
        h("span", { class: "hic", text: Schema.NODE[n.type].icon }), h("span", { class: "hnm", text: n.id }), h("span", { class: "hmeta", text: n.type })));
    }
    for (const m of S.project.maps) {
      const open = !S.collapsed["map:" + m.id], isSel = S.mapId === m.id && !(S.msel && S.msel.kind === "sector");
      rows.push(h("div", { class: "hrow" + (isSel ? " sel" : ""), style: "padding-left:4px", onclick: () => selectMap(m.id) },
        h("button", { class: "tri", "aria-label": open ? "Collapse" : "Expand", text: m.sectors.length ? (open ? "▾" : "▸") : "", onclick: e => { e.stopPropagation(); S.collapsed["map:" + m.id] = open; renderHier(); } }),
        h("span", { class: "hic", text: "▦" }), h("span", { class: "hnm", text: m.id }), h("span", { class: "hmeta", text: "map · " + m.style })));
      if (open) for (const sc2 of m.sectors) rows.push(h("div", { class: "hrow" + (S.mapId === m.id && S.msel && S.msel.kind === "sector" && S.msel.id === sc2.id ? " sel" : ""), style: "padding-left:22px", onclick: () => selectMap(m.id, { kind: "sector", id: sc2.id }) },
        h("span", { class: "hic", text: "▱" }), h("span", { class: "hnm", text: sc2.id }), h("span", { class: "hmeta", text: "room" })));
    }
    p.body.replaceChildren(...rows);
  }
  function selectScreen(id) { const was = S.screenId, wasMap = S.mapId; S.mapId = null; S.msel = null; S.screenId = id; S.selId = null; S.sel = null; renderHier(); renderInspector(); renderProject(); if (was !== id || wasMap) renderCenter(); else if (S.rt) { S.rt.selId = null; S.rt.rebuild(); } }
  function selectNode(sid, id) { const was = S.screenId, wasMap = S.mapId; S.mapId = null; S.msel = null; S.screenId = sid; S.selId = id; S.sel = null; renderHier(); renderInspector(); renderProject(); if (was !== sid || wasMap) renderCenter(); else if (S.rt) { S.rt.selId = id; S.rt.rebuild(); } }

  // ---------------------------------------------------------------- center: Scene / Game
  function renderCenter() {
    const sc = curScreen(), p = E.center; killMapViews(); if (S.rt) { S.rt.destroy(); S.rt = null; }
    if (S.mapId && curMap()) return mapCenter();
    E.host.className = "host"; E.host.replaceChildren();
    p.tabs.replaceChildren(h("button", { class: "tab" + (!S.testing ? " sel" : ""), text: "Scene", onclick: () => setTesting(false) }), h("button", { class: "tab" + (S.testing ? " sel" : ""), text: "Game", onclick: () => setTesting(true) }), h("span", { class: "sp" }), h("span", { class: "csize", text: "1280 × 720  ·  16:9" }));
    put(E.cbar, h("span", { class: "chip", text: sc.id }), !S.testing && sc.kind === "splash" ? h("button", { class: "tbtn sm", text: "▶ Preview sequence", onclick: () => { if (S.rt) S.rt.previewSplash(); } }) : null,
      h("span", { class: "hint", text: S.testing ? "Playing: hover and click buttons, drag sliders. Edits are disabled." : sc.kind === "splash" ? "Select a splash in the Hierarchy to see it." : "Click to select, drag to move (Shift = snap to 10). Drop images from the Project panel here." }));
    S.rt = new PRIM.Runtime({ host: E.host, project: S.project, tags: S.testing ? S.tags.fork() : S.tags, mode: S.testing ? "test" : "edit", urls: S.urls, screenId: S.screenId, selId: S.selId, onStartGame: startGame,
      onSelect: id => { S.selId = id; S.sel = null; renderHier(); renderInspector(); renderProject(); }, onChange: () => renderInspector(), onExit: () => setTesting(false) });
    S.rt.paused = S.paused;
  }

  // ---------------------------------------------------------------- structure edits (screens and elements)
  function addNode(type, over0) {
    const sc = curScreen(); if ((sc.kind === "splash") !== (type === "splash")) return log(type === "splash" ? "Splashes go on the splash screen." : "Elements go on menu screens, not the splash screen.", "warn");
    const id = Schema.uid(type, sc.nodes.map(n => n.id)), node = { id, type }; sc.nodes.push(node);
    const over = Object.assign(type === "splash" ? {} : { y: Math.min(680, 200 + (sc.nodes.length - 1) % 8 * 60) }, over0 || {}); if (type === "title" && !over.text) over.text = "Title"; if (type === "button" && !over.text) over.text = "Button";
    Schema.defineNode(S.project, sc.id, node, over); S.selId = id; S.sel = null; S.rtab = "inspector"; afterStructure(); log("Created " + type + " “" + id + "”");
  }
  function afterStructure() { markDirty(); S.tags.emit([]); renderHier(); renderInspector(); renderProject(); if (S.rt && !S.testing) { S.rt.selId = S.selId; S.rt.rebuild(); } }
  function deleteNode(n) { const sc = curScreen(); S.tags.removePrefix(sc.id + "." + n.id); sc.nodes.splice(sc.nodes.indexOf(n), 1); S.selId = null; afterStructure(); }
  function dupNode(n) {
    const sc = curScreen(), id = Schema.uid(n.type, sc.nodes.map(x => x.id)), pre = sc.id + "." + n.id + ".", np = sc.id + "." + id + ".";
    for (const k of Object.keys(S.project.tags)) if (k.startsWith(pre)) { const r = JSON.parse(JSON.stringify(S.project.tags[k])); r.owner = sc.id + "." + id; S.project.tags[np + k.slice(pre.length)] = r; }
    for (const ax of ["x", "y"]) { const r = S.project.tags[np + ax]; if (r) r.value = Math.min(ax === "x" ? 1280 : 720, r.value + 30); }
    sc.nodes.splice(sc.nodes.indexOf(n) + 1, 0, { id, type: n.type }); S.selId = id; afterStructure();
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
    if (sc.kind === "splash") addNode("splash", { kind: "image", asset: a.id }); else addNode("image", { asset: a.id, x: Math.max(0, Math.min(1280, x)), y: Math.max(0, Math.min(720, y)) });
  }
  function selectedNode() { return S.selId && !S.sel ? curScreen().nodes.find(n => n.id === S.selId) : null; }
  function deleteSelected() { if (S.mapId && S.med && !S.sel) return S.med.deleteSelection(); if (S.sel) return deleteAssetSel(); const n = selectedNode(); if (n) deleteNode(n); }
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
    for (const f of subs) grid.append(pItem("folder", f, baseName(f), null));
    for (const a of assets) grid.append(pItem("asset", a.id, a.name, a));
    if (!subs.length && !assets.length) grid.append(h("div", { class: "empty", text: q ? "No assets match “" + S.search + "”." : "This folder is empty. Use Import, or drop files and folders from your computer here." }));
    const content = h("div", { class: "pcontent" }, h("div", { class: "crumbs" }, ...crumbs, q ? h("span", { class: "sepc", text: "  search results" }) : null), grid);
    content.addEventListener("dragover", e => { if ([...e.dataTransfer.types].includes("Files")) { e.preventDefault(); content.classList.add("dropfiles"); } });
    content.addEventListener("dragleave", e => { if (!content.contains(e.relatedTarget)) content.classList.remove("dropfiles"); });
    content.addEventListener("drop", e => { content.classList.remove("dropfiles"); if ([...e.dataTransfer.types].includes("Files")) { e.preventDefault(); Host.fromDrop(e.dataTransfer).then(items => doIngest(items, "drop")); } });
    grid.addEventListener("click", e => { if (e.target === grid) { S.sel = null; renderProjectBody(); renderInspector(); } });
    b.replaceChildren(tree, content);
    if (S.renaming) { const i = b.querySelector(".rename"); if (i) { i.focus(); const d = i.value.lastIndexOf("."); i.setSelectionRange(0, d > 0 && S.renaming.kind === "asset" ? d : i.value.length); } }
  }
  function pItem(kind, key, name, a) {
    const sel = S.sel && S.sel.kind === kind && S.sel.key === key, renaming = S.renaming && S.renaming.kind === kind && S.renaming.key === key;
    let icon; if (kind === "folder") icon = ico("folder", "big"); else if (a.type === "image" && S.urls[a.id]) icon = h("img", { class: "thumbimg", src: S.urls[a.id], alt: "", draggable: false }); else if (a.type === "font") icon = h("span", { class: "ico big fontprev", style: "font-family:'prim-font-" + a.id + "',serif", text: "Aa" }); else icon = ico(a.type === "sound" ? "sound" : a.type === "image" ? "image" : "file", "big");
    const label = renaming ? h("input", { class: "rename", value: kind === "folder" ? baseName(key) : a.name, "aria-label": "New name", onkeydown: e => { if (e.key === "Enter") e.target.blur(); else if (e.key === "Escape") { S.renaming = null; renderProjectBody(); } e.stopPropagation(); }, onblur: e => { if (!S.renaming) return; const v = e.target.value; S.renaming = null; commitRename(kind, key, v); } }) : h("div", { class: "pname", title: name, text: name });
    const it = h("div", { class: "pit" + (sel ? " sel" : ""), draggable: !renaming, title: kind === "folder" ? key : (a.path ? a.path + "/" : "") + a.name, onclick: e => { e.stopPropagation(); S.sel = { kind, key }; renderProjectBody(); renderInspector(); },
      ondblclick: () => { if (kind === "folder") { S.folder = key; S.search = ""; S.sel = null; renderProject(); renderInspector(); } else if (a.type === "sound" && S.urls[a.id]) playSound(a.id); },
      ondragstart: e => { e.dataTransfer.setData("application/x-prime", JSON.stringify({ kind, key })); e.dataTransfer.effectAllowed = "move"; } }, h("div", { class: "pico" }, icon), label);
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
    if (d.kind === "asset") { const a = assetOf(d.key); if (!a || a.path === dest) return; a.name = Store.uniqueName(S.project.assets.filter(x => x !== a && x.path === dest).map(x => x.name), a.name); a.path = dest; if (dest) Store.ensureFolder(S.project, dest); log("Moved “" + a.name + "” to Assets" + (dest ? "/" + dest : "")); }
    else if (d.kind === "folder") { const key = d.key; if (dest === key || dest.startsWith(key + "/") || Store.parentOf(key) === dest) return; const np = dest ? dest + "/" + baseName(key) : baseName(key); if (foldersAll().includes(np)) return log("A folder named “" + baseName(key) + "” already exists there.", "warn"); retarget(key, np); S.sel = { kind: "folder", key: np }; log("Moved folder “" + baseName(key) + "”"); }
    markDirty(); renderProject(); renderInspector();
  }
  function removeAssetCore(a) {
    for (const k in S.project.tags) { const r = S.project.tags[k]; if ((r.type === "asset" || r.type === "font") && r.value === a.id) r.value = r.type === "font" ? "builtin:display" : ""; }
    S.project.assets.splice(S.project.assets.indexOf(a), 1); delete S.urls[a.id]; Store.delAsset(S.project.id, a.id);
  }
  let delArm = null;
  function deleteAssetSel() {
    const s = S.sel; if (!s) return; const key = s.kind + ":" + s.key;
    if (delArm !== key) { delArm = key; log("Press Delete again to remove “" + baseName(s.key) + "”" + (s.kind === "folder" ? " and everything in it" : "") + ".", "warn"); setTimeout(() => { if (delArm === key) delArm = null; }, 3000); return; }
    delArm = null;
    if (s.kind === "asset") { const a = assetOf(s.key); if (a) removeAssetCore(a); } else { for (const a of S.project.assets.filter(x => x.path === s.key || x.path.startsWith(s.key + "/"))) removeAssetCore(a); S.project.folders = S.project.folders.filter(f => f !== s.key && !f.startsWith(s.key + "/")); if (S.folder === s.key || S.folder.startsWith(s.key + "/")) S.folder = Store.parentOf(s.key); }
    S.sel = null; log("Deleted."); S.tags.emit([]); markDirty(); renderProject(); renderInspector(); if (S.rt) { S.rt.urls = S.urls; S.rt.rebuild(); }
  }

  // ================================================================ INSPECTOR
  const GROUP = { x: "Transform", y: "Transform", w: "Transform", h: "Transform", hoverSound: "Audio", clickSound: "Audio", action: "Behaviour", target: "Behaviour", bind: "Behaviour", duration: "Timing", fadeIn: "Timing", fadeInType: "Timing", fadeOut: "Timing", fadeOutType: "Timing" };
  const ORDER = ["Transform", "Appearance", "Timing", "Audio", "Behaviour"];
  function foldout(key, title, ...kids) { const open = S.fold[key] !== false; return h("div", { class: "fold" }, h("button", { class: "fh", "aria-expanded": open, onclick: () => { S.fold[key] = !open; renderInspector(); } }, h("span", { class: "tri", text: open ? "▾" : "▸" }), h("span", { text: title })), open ? h("div", { class: "fb" }, ...kids) : null); }
  function renderInspector() {
    const p = E.insp; p.tabs.replaceChildren(...[["inspector", "Inspector"], ["tags", "Tags"]].map(([k, l]) => h("button", { class: "tab" + (S.rtab === k ? " sel" : ""), text: l, onclick: () => { S.rtab = k; renderInspector(); } })));
    p.body.replaceChildren(S.rtab === "tags" ? tagsBody() : S.sel ? assetInspector() : propsBody());
  }
  function propsBody() {
    if (S.mapId && curMap()) return mapInspector();
    const sc = curScreen(), node = selectedNode(), box = h("div", { class: "insp" });
    if (node) {
      const pre = sc.id + "." + node.id + ".", def = Schema.NODE[node.type];
      box.append(h("div", { class: "ihead" }, h("span", { class: "bigic", text: def.icon }), h("div", { class: "ititle" }, h("input", { class: "idin", value: node.id, "aria-label": "Element name", onchange: e => renameNode(node, e.target.value) }), h("div", { class: "isub", text: def.label + " · screen “" + sc.id + "”" }))),
        h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: sc.kind === "splash" ? "◀ Earlier" : "▼ Back", onclick: () => moveNode(node, -1) }), h("button", { class: "btn sm", text: sc.kind === "splash" ? "Later ▶" : "▲ Front", onclick: () => moveNode(node, 1) }), h("button", { class: "btn sm", text: "Duplicate", onclick: () => dupNode(node) }), h("button", { class: "btn sm danger", text: "Delete", onclick: () => deleteNode(node) })));
      const groups = {}; for (const pr of def.props) { if (pr.showIf && S.tags.get(pre + pr.showIf[0]) !== pr.showIf[1]) continue; (groups[GROUP[pr.k] || "Appearance"] = groups[GROUP[pr.k] || "Appearance"] || []).push(field(pre + pr.k)); }
      for (const g of ORDER) if (groups[g]) box.append(foldout(g, g, ...groups[g]));
      box.append(h("div", { class: "note", text: "Tip: write {audio.master} in any text to show a tag's live value. Drag a field's name left/right to scrub numbers." }));
    } else {
      const isStart = S.tags.get("project.startScreen") === sc.id;
      box.append(h("div", { class: "ihead" }, h("span", { class: "bigic", text: sc.kind === "splash" ? "✦" : "▭" }), h("div", { class: "ititle" }, h("input", { class: "idin", value: sc.id, "aria-label": "Screen name", onchange: e => renameScreen(sc, e.target.value) }), h("div", { class: "isub", text: sc.kind === "splash" ? "Splash screens, played in order" : "Menu screen" }))),
        h("div", { class: "btnrow" }, h("button", { class: "btn sm", disabled: isStart, text: isStart ? "Start screen" : "Make start screen", onclick: () => { S.tags.set("project.startScreen", sc.id); renderHier(); renderInspector(); } }), sc.kind === "menu" ? h("button", { class: "btn sm danger", text: "Delete screen", onclick: () => deleteScreen(sc) }) : null),
        foldout("screen", "Screen", ...Schema.SCREEN[sc.kind].map(pr => field(sc.id + "." + pr.k))), h("div", { class: "note", text: "Select an element in the Hierarchy or the Scene view to edit it. Every value is a named tag: see the Tags tab." }));
    }
    return box;
  }
  function assetInspector() {
    const s = S.sel, box = h("div", { class: "insp" });
    if (s.kind === "folder") { const n = S.project.assets.filter(a => a.path === s.key || a.path.startsWith(s.key + "/")).length; box.append(h("div", { class: "ihead" }, ico("folder", "bigico"), h("div", { class: "ititle" }, h("div", { class: "idin ro", text: baseName(s.key) }), h("div", { class: "isub", text: "Folder · Assets/" + s.key }))), h("div", { class: "note", text: n + " file(s) inside. Double-click to open. Drag files onto it to move them. F2 renames, Delete removes." })); return box; }
    const a = assetOf(s.key); if (!a) return box;
    const used = []; for (const k in S.project.tags) { const r = S.project.tags[k]; if ((r.type === "asset" || r.type === "font") && r.value === a.id) used.push(k); }
    box.append(h("div", { class: "ihead" }, ico(a.type === "sound" ? "sound" : a.type === "font" ? "font" : a.type === "image" ? "image" : "file", "bigico"), h("div", { class: "ititle" }, h("div", { class: "idin ro", text: a.name }), h("div", { class: "isub", text: a.type + " · Assets/" + (a.path ? a.path + "/" : "") + a.name + " · " + fmtSize(a.size || 0) }))));
    const pv = h("div", { class: "preview" });
    if (a.type === "image" && S.urls[a.id]) { const im = h("img", { src: S.urls[a.id], alt: a.name }), dim = h("div", { class: "isub", text: "" }); im.onload = () => { dim.textContent = im.naturalWidth + " × " + im.naturalHeight + " px"; }; pv.append(im, dim); }
    else if (a.type === "sound" && S.urls[a.id]) { const dur = h("span", { class: "isub", text: "" }), au = new Audio(S.urls[a.id]); au.onloadedmetadata = () => { dur.textContent = au.duration.toFixed(2) + " s"; }; pv.append(h("button", { class: "btn", text: "▶ Play", onclick: () => playSound(a.id) }), h("button", { class: "btn", text: "■ Stop", onclick: stopSound }), dur); }
    else if (a.type === "font") pv.append(h("div", { class: "fsample", style: "font-family:'prim-font-" + a.id + "',serif;font-size:34px", text: "Aa Bb Cc 123" }), h("div", { class: "fsample", style: "font-family:'prim-font-" + a.id + "',serif;font-size:16px", text: "The quick brown fox jumps over the lazy dog." }));
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
        const commit = v => { if (!isFinite(v)) return; if (ranged) v = Math.max(t.min, Math.min(t.max, v)); v = Math.round(v / step) * step; set(v); inp.value = v.toFixed(dec); if (sl) sl.value = v; };
        inp.onchange = () => commit(parseFloat(inp.value)); if (sl) sl.oninput = () => commit(+sl.value);
        lab.classList.add("scrub");
        lab.addEventListener("pointerdown", e => { if (e.button !== 0) return; lab.setPointerCapture(e.pointerId); lab._s = { x: e.clientX, v: +S.tags.get(name), per: ranged ? (t.max - t.min) / 400 : step }; });
        lab.addEventListener("pointermove", e => { if (lab._s) commit(lab._s.v + (e.clientX - lab._s.x) * lab._s.per * (e.shiftKey ? 0.1 : 1)); }); lab.addEventListener("pointerup", () => { lab._s = null; });
        return row(h("div", { class: "numwrap" }, sl, inp));
      }
      case "bool": return row(h("input", { type: "checkbox", class: "chk", checked: !!S.tags.get(name), "aria-label": label, onchange: e => { set(e.target.checked); redraw(); } }));
      case "color": return row(h("input", { type: "color", class: "col", value: S.tags.get(name), "aria-label": label, oninput: e => set(e.target.value) }));
      case "string": return row(h("input", { class: "txt", type: "text", value: S.tags.get(name), "aria-label": label, oninput: e => set(e.target.value) }));
      case "asset": case "font": case "choice": case "screenRef": case "tagRef": {
        let opts = [];
        if (t.type === "asset") opts = [["", "None"], ...S.project.assets.filter(a => a.type === t.assetType).map(a => [a.id, (a.path ? a.path + "/" : "") + a.name])];
        else if (t.type === "font") opts = [...PRIM.BUILTIN_FONTS, ...S.project.assets.filter(a => a.type === "font").map(a => [a.id, a.name])];
        else if (t.type === "choice") opts = t.options; else if (t.type === "screenRef") opts = [["", "None"], ...screens().map(s => [s.id, s.id])];
        else opts = [["", "(pick a tag)"], ...S.tags.names().filter(n => S.tags.spec(n).type === t.refType).map(n => [n, (S.tags.spec(n).label || n) + "  ·  " + n])];
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
  function killMapViews() { if (S.med) { S.med.destroy(); S.med = null; } if (S.v3) { S.v3.dispose(); S.v3 = null; } }
  function selectMap(id, sel) { const was = S.mapId; S.mapId = id; S.selId = null; S.sel = null; S.msel = sel || null; renderHier(); renderInspector(); renderProject(); if (was !== id || !S.med) renderCenter(); else S.med.select(S.msel, true); }
  function openNewMap() {
    const ov = h("div", { class: "modal" }), close = () => ov.remove();
    const card = (id, title, desc, off) => h("button", { class: "mcard" + (off ? " off" : ""), disabled: off, onclick: () => { close(); createMap(id); } }, h("div", { class: "mt", text: title }), h("div", { class: "md", text: desc }), off ? h("div", { class: "mbadge", text: "Coming later" }) : null);
    ov.append(h("div", { class: "mwin", role: "dialog", "aria-label": "New map" }, h("div", { class: "wtitle", text: "New Map: choose an editor style" }),
      h("div", { class: "mbody" }, card("modern", "Modern", "A low-poly brush editor for terrain, placing buildings, and features that make map-making a lot easier.", true),
        card("doom", "DOOM", "The simplest map editor. Walls are made from connected nodes. Select a node or wall and give it a texture. A free-fly 3D view is built in.")),
      h("div", { class: "btnrow", style: "padding:6px 14px 12px" }, h("button", { class: "btn", text: "Cancel", onclick: close }))));
    ov.addEventListener("pointerdown", e => { if (e.target === ov) close(); }); document.body.append(ov);
  }
  const reserved = () => [...screens().map(s => s.id), ...S.project.maps.map(m => m.id), "audio", "gfx", "project"];
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
    p.tabs.replaceChildren(h("button", { class: "tab" + (!S.testing ? " sel" : ""), text: "Map", onclick: () => setTesting(false) }), h("button", { class: "tab" + (S.testing ? " sel" : ""), text: "3D Fly", onclick: () => setTesting(true) }), h("span", { class: "sp" }), h("span", { class: "csize", text: m.style.toUpperCase() + " editor  ·  " + m.sectors.length + " room(s)" }));
    put(E.cbar, h("span", { class: "chip", text: m.id }), h("span", { class: "hint", text: S.testing ? "Click the view, then W A S D to move, mouse to look, Q / E down and up, Shift = fast. Edit on the Map tab; changes show here live." : "Rooms are closed loops of nodes. Drop an image from the Project panel onto a wall to texture it (Shift = ceiling when dropped on a room)." }));
    E.host.className = "host maphost"; E.host.replaceChildren();
    if (S.testing) { S.v3 = makeView(E.host, m); if (S.v3) S.v3.focus = false; }
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
    fly3d(m, 150);
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
        h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "Open 3D Fly", onclick: () => setTesting(true) }), h("button", { class: "btn sm", text: "Fullscreen fly", onclick: () => fly3d(m) }), h("button", { class: "btn sm danger", text: "Delete map", onclick: () => deleteMap(m) })),
        foldout("mm", "Map", ...Schema.MAP.map(p => field(pre + p.k))), h("div", { class: "note", text: "Click a room, wall or node in the Map view to edit it. Every value is a named tag (see the Tags tab)." }));
    }
    return box;
  }

  // ================================================================ PLAY (fullscreen, remembers audio/graphics settings)
  function play() {
    if (S.mapId && curMap()) return fly3d(curMap());
    autosave(); const layer = h("div", { class: "play-layer" }); document.body.append(layer);
    const tg = S.tags.fork(), saved = Store.loadSettings(S.project.id); for (const k in saved) if (S.tags.has(k)) tg.ov[k] = saved[k];
    let start = S.tags.get("project.startScreen"); if (!screens().some(s => s.id === start)) start = screens()[0].id;
    if (S.rt) S.rt.stopMusic();
    const rt = new PRIM.Runtime({ host: layer, project: S.project, tags: tg, mode: "play", urls: S.urls, screenId: start, onPersist: ov => Store.saveSettings(S.project.id, ov), onExit: close, onStartGame: startGame });
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
    else if (e.key === "F2") { e.preventDefault(); startRename(); }
    else if (e.key === "Delete" || e.key === "Backspace") { if (document.querySelector(".play-layer")) return; e.preventDefault(); deleteSelected(); }
    else if (mod && e.key.toLowerCase() === "d") { e.preventDefault(); duplicateSelected(); }
  });
  addEventListener("beforeunload", () => { if (S.project) autosave(); });

  window.__studio = { S, Store, Host, openProject, renderManager, openBytes, saveProject };
  renderManager();
})();
