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
  // ---- tag picker: tags grouped by what they belong to (Player > a weapon, a screen > an element, a map, an object, Compositor…), with a search bar
  function tagGroup(n) {
    const P = S.project, a = n.split("."), first = a[0], att = first === "player" && a.length > 2 && P.player.attachments.find(x => x.id === a[1]);
    const nm = id => S.tags.get(id + ".name") || id, sc = screens().find(x => x.id === first), mp = P.maps.find(x => x.id === first), pr = (P.props || []).find(x => x.id === first), fx = first === "comp" && a.length > 2 && ((P.compositor || {}).effects || []).find(e => e.id === a[1]);
    const g1 = first === "player" ? "Player" : first === "audio" ? "Audio" : first === "gfx" ? "Graphics settings" : first === "project" ? "Project" : first === "comp" ? "Compositor" : sc ? "Screen: " + first : mp ? "Map: " + first : pr ? "Object: " + nm(first) : first;
    const g2 = att ? (S.tags.get("player." + att.id + ".type") === "weapon" ? "Weapon: " : "Object: ") + (S.tags.get("player." + att.id + ".name") || att.id) : fx ? ((Schema.COMPFX[fx.type] || {}).label || fx.id) + " (" + fx.id + ")" : sc && a.length > 2 ? a[1] : null;
    return [g1, g2];
  }
  function tagPicker(anchor, names, cur, pick) {
    closePop(); const pop = h("div", { class: "pop tagpick", role: "dialog" }), q = h("input", { class: "tpq", placeholder: "Search tags (name or label)…", "aria-label": "Search tags" }), list = h("div", { class: "tpl" });
    S.tpOpen = S.tpOpen || {};
    const item = n => { const sp = S.tags.spec(n) || {}; return h("button", { class: "tpi" + (n === cur ? " sel" : ""), title: n, onclick: () => { closePop(); pick(n); } }, h("span", { class: "tpn", text: sp.label || n }), h("span", { class: "tpk", text: n })); };
    const draw = () => {
      const f = q.value.trim().toLowerCase(), hit = names.filter(n => !f || n.toLowerCase().includes(f) || String((S.tags.spec(n) || {}).label || "").toLowerCase().includes(f));
      const tree = new Map(); for (const n of hit) { const [g1, g2] = tagGroup(n); if (!tree.has(g1)) tree.set(g1, new Map()); const m = tree.get(g1), k = g2 || ""; if (!m.has(k)) m.set(k, []); m.get(k).push(n); }
      const rows = [h("button", { class: "tpi", onclick: () => { closePop(); pick(""); } }, h("span", { class: "tpn", text: "(no tag)" }))];
      for (const [g1, subs] of tree) {
        const k1 = g1, cnt = [...subs.values()].reduce((t, l) => t + l.length, 0), open1 = f || S.tpOpen[k1] || (cur && [...subs.values()].some(l => l.includes(cur)));
        rows.push(h("button", { class: "tpg", onclick: () => { S.tpOpen[k1] = !open1; draw(); } }, h("span", { text: (open1 ? "▾ " : "▸ ") + g1 }), h("span", { class: "tpc", text: String(cnt) })));
        if (!open1) continue;
        for (const [g2, l] of [...subs].sort((a, b) => (a[0] ? 0 : 1) - (b[0] ? 0 : 1))) {
          if (!g2) { rows.push(...l.map(item)); continue; }
          const k2 = g1 + "/" + g2, open2 = f || S.tpOpen[k2] || (cur && l.includes(cur));
          rows.push(h("button", { class: "tpg sub", onclick: () => { S.tpOpen[k2] = !open2; draw(); } }, h("span", { text: (open2 ? "▾ " : "▸ ") + g2 }), h("span", { class: "tpc", text: String(l.length) })));
          if (open2) rows.push(...l.map(n => { const b = item(n); b.classList.add("sub"); return b; }));
        }
      }
      if (!hit.length) rows.push(h("div", { class: "tpe", text: "No tag matches “" + q.value + "”." }));
      list.replaceChildren(...rows);
    };
    q.oninput = draw; q.onkeydown = e => { if (e.key === "Escape") closePop(); if (e.key === "Enter") { const b = list.querySelector(".tpi:not(:first-child)"); if (b) b.click(); } };
    pop.append(q, list); draw(); document.body.append(pop); const r = anchor.getBoundingClientRect();
    pop.style.width = Math.max(300, r.width) + "px"; pop.style.left = Math.max(4, Math.min(r.left, innerWidth - pop.offsetWidth - 4)) + "px"; pop.style.top = Math.max(4, Math.min(r.bottom, innerHeight - pop.offsetHeight - 4)) + "px";
    E.pop = pop; E.popAnchor = anchor; setTimeout(() => q.focus(), 0);
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
    S.unsub = S.tags.on(() => markDirty(true)); S.log = []; document.body.classList.add("in-editor"); renderEditor(); log("Opened project “" + p.name + "”"); if (p.droppedMaps) { log(p.droppedMaps + " old room-style map(s) were removed: maps are now grids of 2 m cells. Create a new map from the Create menu.", "warn"); delete p.droppedMaps; markDirty(); }
  }
  function stopPlay() { if (S.play) { S.play.destroy(); S.play = null; } }   // the running game (walk / playtest), so it never keeps playing in the background
  function closeEditorState() { stopPlay(); killMapViews(); if (S.rt) { S.rt.destroy(); S.rt = null; } if (S.unsub) { S.unsub(); S.unsub = null; } closePop(); S.project = null; E = {}; }
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
      { label: "File", items: [{ t: "New Project", fn: async () => { await backToManager(); const p = Store.create(); await openProject(p.id); } }, { t: "Open Project…", k: "Ctrl+O", fn: openFileDialog }, "-", { t: "Save", k: "Ctrl+S", fn: () => saveProject(false) }, { t: "Save As…", fn: () => saveProject(true) }, "-", { t: "Build Game…", fn: buildGame }, "-", { t: "Close Project", fn: backToManager }] },
      { label: "Edit", items: [{ t: "Rename", k: "F2", fn: startRename }, { t: "Duplicate", k: "Ctrl+D", fn: duplicateSelected }, { t: "Delete", k: "Del", fn: deleteSelected }] },
      { label: "Assets", items: [{ t: "Create Folder", fn: newFolder }, "-", { t: "Import Files…", fn: importFilesDlg }, { t: "Import Folder…", fn: importFolderDlg }, "-", { t: "Delete Selected…", k: "Del", fn: () => { if (S.sel) deleteAssetsAsk(); else log("Select an asset or folder in the Project panel first.", "warn"); } }, { t: "Remove Unused Assets…", fn: removeUnusedAssets }] },
      { label: "Screen", items: [{ t: "Create Screen", fn: addScreen }, { t: "Create Pause Screen", fn: () => newPause(false) }, { t: "Create Map…", fn: openNewMap }, "-", ...(sc.kind === "splash" ? [mk("splash")] : ["title", "button", "image", "slider", "toggle", "bar", "pips"].map(mk))] },
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
  function setTab(t) { S.fsel = null; S.fsl = []; S.fhov = null; if (t === "fly" && !S.mapId) t = "edit"; if (t === "pause") pausePick(); S.tab = t; S.testing = t === "game"; S.playerSel = t === "player"; if (S.playerSel || t === "timeline" || t === "coder" || t === "comp" || t === "tagmgr" || t === "dlg") S.sel = null; if (!S.testing) S.paused = false; renderToolbar(); renderHier(); renderInspector(); renderCenter(); }
  function setTesting(v) { setTab(v ? "game" : "edit"); }
  function leaveSpecial() { S.playerSel = false; if (["player", "timeline", "fly", "coder", "comp", "tagmgr", "dlg"].includes(S.tab)) { S.tab = "edit"; S.testing = false; return true; } return false; }
  function centerTabs() {
    const items = S.mapId && curMap() ? [["edit", "Map"], ["fly", "Fly Editor"]] : [["edit", "Scene"]]; items.push(["game", "Game"], ["player", "Player Editor"], ["props", "Object Editor"], ["timeline", "Timeline"], ["coder", "Node Coder"], ["comp", "Compositor"], ["tagmgr", "Tag Manager"], ["pause", "Pause Menu"], ["dlg", "Dialogue Editor"]);
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
    p.tabs.replaceChildren(h("div", { class: "tab sel", text: "Hierarchy" }), h("span", { class: "sp" }), h("button", { class: "tbtn sm", text: "Create ▾", onclick: e => popMenu(e.currentTarget, [{ t: "Screen", fn: addScreen }, { t: "Pause screen", fn: () => newPause(false) }, { t: "Map…", fn: openNewMap }, { t: "Folder", fn: () => newHFolder(null) }, ...(sc.kind !== "splash" ? [{ t: "Folder inside “" + sc.id + "”", fn: () => newHFolder(sc.id) }] : []), "-", ...(sc.kind === "splash" ? [{ t: "Splash", fn: () => addNode("splash") }] : ["title", "button", "image", "slider", "toggle", "bar", "pips"].map(t => ({ t: Schema.NODE[t].label, fn: () => addNode(t) })))]) }));
    const rows = [], tri = (open, has, fn) => h("button", { class: "tri", "aria-label": open ? "Collapse" : "Expand", text: has ? (open ? "▾" : "▸") : "", onclick: e => { e.stopPropagation(); fn(); } });
    const drag = (el, d) => { el.draggable = true; el.addEventListener("dragstart", e => { e.dataTransfer.setData("application/x-hier", JSON.stringify(d)); e.dataTransfer.effectAllowed = "move"; }); return el; };
    const drop = (el, t) => { el.addEventListener("dragover", e => { if (e.dataTransfer.types.includes("application/x-hier")) { e.preventDefault(); el.classList.add("dropt"); } }); el.addEventListener("dragleave", () => el.classList.remove("dropt")); el.addEventListener("drop", e => { e.preventDefault(); e.stopPropagation(); el.classList.remove("dropt"); let d = null; try { d = JSON.parse(e.dataTransfer.getData("application/x-hier")); } catch (x) {} if (d) moveToFolder(d, t); }); return el; };
    const folderRow = (f, lvl, count) => { const open = !S.collapsed["f:" + f.id]; rows.push(drop(h("div", { class: "hrow" + (S.fsel === f.id ? " sel" : ""), style: pad(lvl), onclick: () => { S.fsel = f.id; S.sel = null; S.selId = null; renderHier(); renderInspector(); } }, tri(open, count > 0, () => { S.collapsed["f:" + f.id] = open; renderHier(); }), h("span", { class: "hic", text: "▰" }), h("span", { class: "hnm", text: f.name }), h("span", { class: "hmeta", text: "folder" })), { folder: f.id })); return open; };
    const nodeRow = (s, n, lvl) => rows.push(drag(h("div", { class: "hrow" + (s.id === S.screenId && n.id === S.selId && !S.sel && !S.mapId && !S.playerSel && !S.fsel ? " sel" : ""), style: pad(lvl), onclick: () => selectNode(s.id, n.id) },
      h("span", { class: "hic", text: Schema.NODE[n.type].icon }), h("span", { class: "hnm", text: n.id }), h("span", { class: "hmeta", text: n.type })), { kind: "node", screen: s.id, id: n.id }));
    const screenRows = (s, lvl) => {
      const open = !S.collapsed[s.id]; rows.push(drag(drop(h("div", { class: "hrow" + (s.id === S.screenId && !S.selId && none() ? " sel" : ""), style: pad(lvl), onclick: () => selectScreen(s.id) },
        tri(open, s.nodes.length > 0, () => { S.collapsed[s.id] = open; renderHier(); }), h("span", { class: "hic", text: s.kind === "splash" ? "✦" : s.kind === "hud" ? "◫" : s.kind === "pause" ? "⏸" : "▭" }), h("span", { class: "hnm", text: s.id }), S.tags.get("project.startScreen") === s.id ? h("span", { class: "hmeta", text: "start" }) : null), { folder: null, screen: s.id }), { kind: "screen", id: s.id }));
      if (!open) return; const sf = F.filter(f => f.scope === s.id);
      for (const f of sf) { const kids = s.nodes.filter(n => n.folder === f.id); if (folderRow(f, lvl + 1, kids.length)) for (const n of kids) nodeRow(s, n, lvl + 2); }
      for (const n of s.nodes) if (!sf.some(f => f.id === n.folder)) nodeRow(s, n, lvl + 1);
    };
    const mapRows = (m, lvl) => {
      const isSel = S.mapId === m.id && !S.playerSel && !S.fsel;
      rows.push(drag(h("div", { class: "hrow" + (isSel ? " sel" : ""), style: pad(lvl), onclick: () => selectMap(m.id) }, h("span", { class: "tri" }),
        h("span", { class: "hic", text: "▦" }), h("span", { class: "hnm", text: m.id }), h("span", { class: "hmeta", text: "map · " + m.layers.length + " stor" + (m.layers.length === 1 ? "y" : "ies") })), { kind: "map", id: m.id }));
    };
    for (const f of F.filter(x => !x.scope)) { const ss = screens().filter(x => x.folder === f.id), mm = S.project.maps.filter(x => x.folder === f.id); if (folderRow(f, 0, ss.length + mm.length)) { for (const x of ss) screenRows(x, 1); for (const x of mm) mapRows(x, 1); } }
    const tops = F.filter(x => !x.scope); for (const x of screens()) if (!tops.some(f => f.id === x.folder)) screenRows(x, 0); for (const m of S.project.maps) if (!tops.some(f => f.id === m.folder)) mapRows(m, 0);
    rows.push(h("div", { class: "hrow" + (S.playerSel ? " sel" : ""), style: "padding-left:4px", onclick: () => { if (S.tab === "coder") { S.coderT = { kind: "player" }; S.cnode = null; renderHier(); renderInspector(); renderCenter(); } else setTab("player"); } }, h("span", { class: "tri" }), h("span", { class: "hic", text: "◉" }), h("span", { class: "hnm", text: "Player" }), h("span", { class: "hmeta", text: "entity" })));
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
    if (S.tab === "player") return S.objView && S.objSel && S.project.player.attachments.some(a => a.id === S.objSel) ? objectCenter() : playerCenter(); if (S.tab === "props") return propCenter(); if (S.tab === "timeline") return timelineCenter(); if (S.tab === "coder") return coderCenter(); if (S.tab === "comp") return compCenter(); if (S.tab === "tagmgr") return tagMgrCenter(); if (S.tab === "dlg") return dlgCenter();
    if (S.tab === "pause" && (S.mapId || curScreen().kind !== "pause")) { if (!pausePick()) return pauseEmpty(); S.tab = "pause"; }
    if (S.mapId && curMap() && S.tab !== "game") return mapCenter();
    const sc = S.testing && S.mapId ? (screens().find(x => x.id === S.tags.get("project.startScreen")) || screens()[0]) : curScreen();
    E.host.className = "host"; E.host.replaceChildren();
    p.tabs.replaceChildren(...centerTabs(), h("span", { class: "sp" }), h("span", { class: "csize", text: "1280 × 720  ·  16:9" }));
    if (S.tab === "pause") pauseBar(sc); else put(E.cbar, h("span", { class: "chip", text: sc.id }), !S.testing && sc.kind === "splash" ? h("button", { class: "tbtn sm", text: "▶ Preview sequence", onclick: () => { if (S.rt) S.rt.previewSplash(); } }) : null,
      h("span", { class: "hint", text: S.testing ? "Playing: hover and click buttons, drag sliders. Edits are disabled." : sc.kind === "splash" ? "Select a splash in the Hierarchy to see it." : "Click to select, drag to move (Shift = snap to 10), drag an orange corner to resize. x0,y0 is the middle of the screen. Drop images from the Project panel here." }));
    if (sc.kind === "hud") return hudCenter(sc);
    S.rt = new PRIM.Runtime({ host: E.host, project: S.project, tags: S.testing ? S.tags.fork() : S.tags, mode: S.testing ? "test" : "edit", urls: S.urls, screenId: sc.id, selId: S.selId, onStartGame: startGame, say: t => log(t),
      onSelect: id => { S.selId = id; S.sel = null; renderHier(); renderInspector(); renderProject(); }, onChange: () => renderInspector(), onExit: () => setTesting(false) });
    S.rt.paused = S.paused;
  }

  // ---------------------------------------------------------------- Pause Menu tab: the pause screens (kind "pause") edited like any screen; one of them is the pause menu Esc opens (tag player.pauseScreen)
  const pauseScreens = () => screens().filter(s => s.kind === "pause");
  // choose the pause screen to edit: the selected one, else the one Esc opens, else the first; false when there is none
  function pausePick() {
    const cur = !S.mapId && curScreen(); if (cur && cur.kind === "pause") return true; const L = pauseScreens(); if (!L.length) return false;
    const sc = L.find(x => x.id === S.tags.get("player.pauseScreen")) || L[0]; S.mapId = null; S.msel = null; S.screenId = sc.id; S.selId = null; S.sel = null; return true;
  }
  function newPause(full) {
    const sc = Schema.addPauseScreen(S.project, "pause", full); if (!pauseScreens().some(x => x.id === S.tags.get("player.pauseScreen") && x !== sc)) S.tags.set("player.pauseScreen", sc.id);
    S.mapId = null; S.screenId = sc.id; S.selId = null; S.sel = null; S.tab = "pause"; afterStructure(); renderCenter(); log("Created pause screen “" + sc.id + "”" + (full ? " (with an Options screen)" : ""));
  }
  function pauseEmpty() {
    E.host.className = "host maphost"; E.host.replaceChildren(); E.center.tabs.replaceChildren(...centerTabs());
    put(E.cbar, h("span", { class: "chip", text: "Pause menu" }), h("span", { class: "hint", text: "No pause screens yet." }));
    E.host.append(h("div", { class: "empty-c" }, h("div", { text: "A pause menu opens when the player presses Esc in the game. It freezes the game and shows a screen you design here (buttons: Resume, Restart level, Options, Quit to main menu…)." }),
      h("div", { class: "btnrow" }, h("button", { class: "btn", text: "+ New pause menu", onclick: () => newPause(false) }), h("button", { class: "btn", text: "+ New pause menu with an Options screen", onclick: () => newPause(true) }))));
  }
  function pauseBar(sc) {
    const isOn = S.tags.get("player.pauseScreen") === sc.id, L = pauseScreens();
    put(E.cbar, h("span", { class: "chip", text: "Pause menu" }),
      h("select", { class: "tsel", "aria-label": "Pause screen", onchange: e => { S.screenId = e.target.value; S.selId = null; renderHier(); renderInspector(); renderCenter(); } }, ...L.map(x => h("option", { value: x.id, text: x.id + (S.tags.get("player.pauseScreen") === x.id ? "  ★ Esc" : ""), selected: x.id === sc.id }))),
      h("button", { class: "tbtn sm", text: "+ New pause screen", onclick: e => popMenu(e.currentTarget, [{ t: "Pause menu (Resume, Restart, Quit)", fn: () => newPause(false) }, { t: "Pause menu + Options screen", fn: () => newPause(true) }]) }),
      h("button", { class: "tbtn sm" + (isOn ? " on" : ""), disabled: isOn, text: isOn ? "★ Esc opens this screen" : "Open this one with Esc", onclick: () => { S.tags.set("player.pauseScreen", sc.id); renderCenter(); renderInspector(); } }),
      h("span", { class: "hint", text: S.testing ? "Testing: the buttons work (Resume / Restart only do something in the game)." : "Designed over a stand-in game picture. Buttons: Resume, Restart level, Go to screen (more pause screens), Go back, Quit to main menu, Quit. Add elements with the Screen menu." }));
  }
  // the HUD screen: edited with PRIM.HudEditor, shown in the Game tab with PRIM.HUD (the same renderer the game uses)
  function hudCenter(sc) {
    E.host.style.position = "";   // the host is absolutely placed by the stylesheet; never override it
    put(E.cbar, h("span", { class: "chip", text: sc.id + " · HUD" }), h("span", { class: "hint", text: S.testing ? "The HUD as the game draws it (scripts run, values are live)." : "Click to select, drag to move (Shift = snap 10), orange corners resize. Wheel zooms, middle / right drag pans, F fits. The dashed frame is the standing-still view; elements may sit outside it. Drop images from the Project panel here." }));
    if (S.testing) { const box = h("div", { style: "position:absolute;inset:0;background:radial-gradient(ellipse at 50% 115%,#26384c,#0f1822 55%,#070b10)" }); E.host.append(box); const hud = new PRIM.HUD({ host: box, project: S.project, tags: S.tags.fork(), urls: S.urls, screenId: sc.id, say: t => log(t) }); S.rt = { paused: false, destroy: () => { hud.destroy(); box.remove(); }, rebuild: () => hud.build(), stopMusic() {}, previewSplash() {}, els: {} }; return; }
    S.rt = new PRIM.HudEditor({ host: E.host, project: S.project, tags: S.tags, urls: S.urls, screenId: sc.id, selId: S.selId,
      onSelect: id => { S.selId = id; S.sel = null; renderHier(); renderInspector(); renderProject(); }, onChange: () => renderInspector() });
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
    if (S.mapId) return S.tab === "fly" ? flyDrop(e, a) : dropOnMap(e, a);
    if (!S.rt) return;
    const sc = curScreen(); if (S.rt.dropPoint) { const q = S.rt.dropPoint(e); return addNode("image", { asset: a.id, x: q.x, y: q.y }); }
    const r = S.rt.stage.getBoundingClientRect(), x = Math.round((e.clientX - r.left) / S.rt.scale), y = Math.round((e.clientY - r.top) / S.rt.scale);
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
    const it = h("div", { class: "pit" + (sel ? " sel" : "") + (S.clip && S.clip.mode === "cut" && S.clip.items.some(i => i.kind === kind && i.key === key) ? " cut" : ""), "data-k": kind + ":" + key, draggable: !renaming, title: kind === "folder" ? key : (a.path ? a.path + "/" : "") + a.name, onclick: e => { e.stopPropagation(); clickPick(e, kind, key); renderProjectBody(); renderInspector(); },
      oncontextmenu: e => { e.preventDefault(); e.stopPropagation(); const inSel = pickedItems().some(i => i.kind === kind && i.key === key); if (!inSel) { clickPick({}, kind, key); renderProjectBody(); renderInspector(); }
        popMenu(null, [{ t: "Rename", k: "F2", fn: startRename, disabled: pickedItems().length > 1 }, ...(kind === "asset" && a.type === "sound" && S.urls[a.id] ? [{ t: "Play", fn: () => playSound(a.id) }] : []), "-", { t: "Cut", k: "Ctrl+X", fn: () => assetClip("cut") }, { t: "Copy", k: "Ctrl+C", fn: () => assetClip("copy") }, { t: "Paste here", k: "Ctrl+V", fn: () => { if (kind === "folder") S.folder = key; assetPaste(); }, disabled: !S.clip }, "-", { t: "Delete…", k: "Del", fn: deleteAssetsAsk }], { x: e.clientX, y: e.clientY }); },
      ondblclick: () => { if (kind === "folder") { S.folder = key; S.search = ""; S.sel = null; renderProject(); renderInspector(); } else if (a.type === "sound" && S.urls[a.id]) playSound(a.id); },
      ondragstart: e => { const ml = multiList(), many = ml && ml.some(i => i.kind === kind && i.key === key); e.dataTransfer.setData("application/x-prime", JSON.stringify(many ? { kind, key, items: topLevel(ml) } : { kind, key })); e.dataTransfer.effectAllowed = "move"; } }, h("div", { class: "pico" }, icon), label);
    if (kind === "folder") { it.addEventListener("dragover", e => { if ([...e.dataTransfer.types].includes("application/x-prime")) { e.preventDefault(); e.stopPropagation(); it.classList.add("drop"); } }); it.addEventListener("dragleave", () => it.classList.remove("drop")); it.addEventListener("drop", e => { it.classList.remove("drop"); e.stopPropagation(); dropOnFolder(e, key); }); }
    return it;
  }
  const soundPlayers = {}; function playSound(id) { stopSound(); soundPlayers.cur = PRIM.Audio.play(S.urls[id], 0.7, false); } function stopSound() { if (soundPlayers.cur) { soundPlayers.cur.stop(0.02); soundPlayers.cur = null; } }

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
  // every place that uses an asset: tags (images, sounds, models, fonts…), map cell textures (walls / floors / ceilings / diagonals are structure, not tags), timeline events
  function assetUses(a) {
    const P = S.project, out = [];
    for (const k in P.tags) { const r = P.tags[k]; if ((r.type === "asset" || r.type === "font") && r.value === a.id) out.push(k); }
    for (const m of P.maps || []) { let n = 0; for (const L of m.layers || []) for (const c of Object.values(L.cells || {})) { for (const q of ["w", "f", "c", "dt"]) if (c[q] === a.id) n++; if (c.ws) for (const v of Object.values(c.ws)) if (v === a.id) n++; } if (n) out.push(m.id + " (" + n + " texture" + (n > 1 ? "s" : "") + " in the map)"); }
    for (const e of P.events || []) if (e.asset === a.id) out.push("timeline sound event " + e.id); for (const e of P.animLen || []) if (e.asset === a.id) out.push("timeline length " + e.id);
    return out;
  }
  function removeAssetCore(a) {
    const P = S.project;
    for (const k in P.tags) { const r = P.tags[k]; if ((r.type === "asset" || r.type === "font") && r.value === a.id) r.value = r.type === "font" ? "builtin:display" : ""; }
    for (const m of P.maps || []) for (const L of m.layers || []) for (const c of Object.values(L.cells || {})) { for (const q of ["w", "f", "c", "dt"]) if (c[q] === a.id) c[q] = ""; if (c.ws) for (const sd of Object.keys(c.ws)) if (c.ws[sd] === a.id) delete c.ws[sd]; }   // the texture falls back to the default look
    P.events = (P.events || []).filter(e => e.asset !== a.id); P.animLen = (P.animLen || []).filter(e => e.asset !== a.id);
    if (PRIM.FBX && PRIM.FBX.cache) delete PRIM.FBX.cache[a.id];
    P.assets.splice(P.assets.indexOf(a), 1); delete S.urls[a.id]; Store.delAsset(P.id, a.id);
  }
  function assetsOfItems(items) { const set = new Set(); for (const s of items) { if (s.kind === "asset") { const a = assetOf(s.key); if (a) set.add(a); } else for (const a of S.project.assets.filter(x => x.path === s.key || x.path.startsWith(s.key + "/"))) set.add(a); } return [...set]; }
  // Delete button / right-click Delete: asks once (saying where things are used), then removes
  function deleteAssetsAsk() {
    const items = topLevel(pickedItems()); if (!items.length) return; const as = assetsOfItems(items), used = as.filter(a => assetUses(a).length);
    const one = items.length === 1 && items[0].kind === "asset" ? assetOf(items[0].key) : null;
    const what = items.length > 1 ? items.length + " items" : one ? "“" + one.name + "”" : "the folder “" + baseName(items[0].key) + "” (" + as.length + " file" + (as.length === 1 ? "" : "s") + ")";
    const usedTxt = one ? (used.length ? "\n\nIt is still used by: " + assetUses(one).slice(0, 6).join(", ") + ". Those places go back to empty / the default look." : "") : used.length ? "\n\n" + used.length + " of them " + (used.length === 1 ? "is" : "are") + " still used (" + used.slice(0, 4).map(a => a.name).join(", ") + (used.length > 4 ? "…" : "") + "). Those places go back to empty / the default look." : "";
    const msg = "Delete " + what + " from the project?" + usedTxt + "\n\nThis can't be undone (your original files on disk are not touched).";
    if (!confirm(msg)) return; delArm = items.map(i => i.kind + ":" + i.key).join("|"); deleteAssetSel();
  }
  function removeUnusedAssets() {
    const un = S.project.assets.filter(a => !assetUses(a).length); if (!un.length) { log("Every asset is used somewhere. Nothing to remove."); return; }
    if (!confirm("Remove " + un.length + " unused asset" + (un.length > 1 ? "s" : "") + "?\n\n" + un.slice(0, 12).map(a => "• " + (a.path ? a.path + "/" : "") + a.name).join("\n") + (un.length > 12 ? "\n…and " + (un.length - 12) + " more" : "") + "\n\nNothing in the project uses them (no screen, map, object, weapon, sound setting or timeline). Your original files on disk are not touched.")) return;
    for (const a of un) removeAssetCore(a); S.sel = null; S.multi = null; log("Removed " + un.length + " unused asset" + (un.length > 1 ? "s." : ".")); S.tags.emit([]); markDirty(); renderProject(); renderInspector(); if (S.rt) { S.rt.urls = S.urls; S.rt.rebuild(); }
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
    S.tags.emit([]); markDirty(); renderProject(); renderInspector(); if (S.rt) { S.rt.urls = S.urls; S.rt.rebuild(); } if (S.mapId) renderCenter();
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
    if (S.tab === "comp") return compInspector();
    if (S.tab === "tagmgr") return tagMgrInspector(); if (S.tab === "dlg") return coderInspector();
    if (S.tab === "props") return propInspector();
    if (S.playerSel) return playerInspector(); if (S.tab === "timeline") return timelineInspector(); if (S.tab === "coder") return coderInspector();
    if (S.mapId && curMap()) return S.tab === "fly" && (S.fsl || []).length ? flyInspector() : mapInspector();
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
      box.append(h("div", { class: "ihead" }, h("span", { class: "bigic", text: sc.kind === "splash" ? "✦" : sc.kind === "hud" ? "◫" : sc.kind === "pause" ? "⏸" : "▭" }), h("div", { class: "ititle" }, h("input", { class: "idin", value: sc.id, "aria-label": "Screen name", onchange: e => renameScreen(sc, e.target.value) }), h("div", { class: "isub", text: sc.kind === "splash" ? "Splash screens, played in order" : sc.kind === "hud" ? "HUD: drawn over the game" : sc.kind === "pause" ? "Pause screen: drawn over the frozen game" + (S.tags.get("player.pauseScreen") === sc.id ? " (Esc opens it)" : "") : "Menu screen" }))),
        h("div", { class: "btnrow" }, h("button", { class: "btn sm", disabled: isStart, text: isStart ? "Start screen" : "Make start screen", onclick: () => { S.tags.set("project.startScreen", sc.id); renderHier(); renderInspector(); } }), sc.kind === "pause" && S.tags.get("player.pauseScreen") !== sc.id ? h("button", { class: "btn sm", text: "Open with Esc", onclick: () => { S.tags.set("player.pauseScreen", sc.id); renderInspector(); if (S.tab === "pause") renderCenter(); } }) : null,
          sc.kind === "menu" || sc.kind === "pause" ? h("button", { class: "btn sm danger", text: "Delete screen", onclick: () => deleteScreen(sc) }) : null),
        foldout("screen", "Screen", ...Schema.SCREEN[sc.kind].map(pr => field(sc.id + "." + pr.k))), h("div", { class: "note", text: "Select an element in the Hierarchy or the Scene view to edit it. Every value is a named tag: see the Tags tab." }));
    }
    return box;
  }
  function assetInspector() {
    const box = assetInspectorBody(); if (!S.sel) return box; const ml = multiList(), a = !ml && S.sel.kind === "asset" ? assetOf(S.sel.key) : null, uses = a ? assetUses(a) : [];
    if (a) box.append(foldout("as:uses", "Used by (" + uses.length + ")", ...(uses.length ? uses.slice(0, 30).map(u => h("div", { class: "note mono", style: "margin:2px 8px", text: u })) : [h("div", { class: "note", text: "Nothing uses this asset yet." })])));
    box.append(h("div", { class: "btnrow" }, h("button", { class: "btn sm danger", text: ml ? "Delete " + ml.length + " items" : S.sel.kind === "folder" ? "Delete folder" : "Delete asset", onclick: deleteAssetsAsk })), h("div", { class: "note", text: "Tip: right-click an asset for Rename / Delete. Assets > Remove Unused Assets… cleans out everything nothing uses." }));
    return box;
  }
  function assetInspectorBody() {
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
      case "string": if (t.multi && !compact) { const ta = h("textarea", { class: "dlgtext", rows: 3, "aria-label": label, oninput: e => set(e.target.value) }); ta.value = S.tags.get(name) || ""; return row(ta); }
        return row(h("input", { class: "txt", type: "text", value: S.tags.get(name), "aria-label": label, oninput: e => set(e.target.value) }));
      case "asset": case "font": case "choice": case "screenRef": case "mapRef": case "dialogRef": case "tagRef": case "anim": {
        let opts = [];
        if (t.type === "asset") opts = [["", "None"], ...S.project.assets.filter(a => a.type === t.assetType).map(a => [a.id, (a.path ? a.path + "/" : "") + a.name])];
        else if (t.type === "font") opts = [...PRIM.BUILTIN_FONTS, ...S.project.assets.filter(a => a.type === "font").map(a => [a.id, a.name])];
        else if (t.type === "anim") { const mo = PRIM.FBX.cache[S.tags.get(t.modelTag)]; opts = [["", "(none)"], ...(mo ? Object.keys(mo.anims) : []).map(n => [n, n])]; }
        else if (t.type === "choice") opts = t.options; else if (t.type === "mapRef") opts = [["", "None"], ...S.project.maps.map(m => [m.id, m.id])]; else if (t.type === "screenRef") opts = [["", "None"], ...screens().map(s => [s.id, s.id])]; else if (t.type === "dialogRef") opts = [["", "None"], ...(S.project.dialogues || []).map(d => [d.id, d.name + "  (" + d.id + ")"])];
        else opts = [["", "(pick a tag)"], ...S.tags.names().filter(n => !S.tags.spec(n).group || S.tags.spec(n).group !== "script").filter(n => t.refType === "any" ? ["number", "bool", "string", "color"].includes(S.tags.spec(n).type) : S.tags.spec(n).type === t.refType).map(n => [n, (S.tags.spec(n).label || n) + "  ·  " + n])];
        if (t.type === "tagRef") { const cur = S.tags.get(name), cs = cur && S.tags.spec(cur), btn = h("button", { class: "dd tagbtn", "aria-label": label, title: cur || "", text: cur ? (cs ? (cs.label || cur) + "  ·  " + cur : cur + " (missing)") : "(pick a tag)" });
          btn.onclick = () => tagPicker(btn, opts.slice(1).map(o => o[0]), cur, v => { set(v); redraw(); }); return row(h("div", { class: "selrow" }, btn)); }
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
  // ---------------------------------------------------------------- Tag Manager tab: every tag of the project in one big table + your own tags
  const TAGNAME = /^[a-z][a-z0-9_]*(\.[a-z0-9_]+)*$/, TTYPES = [["number", "Number"], ["bool", "Yes / no"], ["string", "Text"], ["color", "Color"]];
  const tmLive = sp => !!(sp && (sp.live || /\(live\)/.test(sp.label || "")));
  const tmTypeName = t => ({ number: "Number", bool: "Yes / no", string: "Text", color: "Color", tagRef: "Tag link", asset: "File", choice: "Choice", font: "Font", anim: "Animation", screenRef: "Screen", mapRef: "Map" })[t] || t;
  function tmUses(n) {   // which tags point at n (tag links, or {n} inside a text)
    const out = []; for (const k of S.tags.names()) { const r = S.tags.spec(k); if (!r) continue; if (r.type === "tagRef" && r.value === n) out.push(k); else if (r.type === "string" && typeof r.value === "string" && r.value.includes("{" + n + "}")) out.push(k); } return out;
  }
  function tmList() {
    const f = (S.tmQ || "").trim().toLowerCase(), ty = S.tmType || "all", gsel = S.tmGroup || "all";
    return S.tags.names().filter(n => { const sp = S.tags.spec(n); if (!sp || sp.group === "script") return false;
      if (gsel === "mine" && !sp.custom) return false; if (gsel === "live" && !tmLive(sp)) return false; if (gsel !== "all" && gsel !== "mine" && gsel !== "live" && tagGroup(n)[0] !== gsel) return false;
      if (ty !== "all" && (ty === "other" ? ["number", "bool", "string", "color"].includes(sp.type) : sp.type !== ty)) return false;
      return !f || n.toLowerCase().includes(f) || String(sp.label || "").toLowerCase().includes(f); }).sort();
  }
  function tmCreate(o) {
    const name = String(o.name || "").trim().toLowerCase(); if (!TAGNAME.test(name)) return "Use lowercase letters, numbers, _ and dots, starting with a letter (like quest.step or keys_found).";
    if (S.tags.has(name)) return "A tag called “" + name + "” already exists."; const reserved = ["player", "audio", "gfx", "project", "comp"]; if (reserved.includes(name)) return "That name is used by the engine; add something after it, like player.coins.";
    const type = o.type, rec = { type, label: String(o.label || "").trim() || name, group: name.split(".")[0], custom: true };
    if (type === "number") { rec.min = isFinite(+o.min) ? +o.min : 0; rec.max = isFinite(+o.max) ? +o.max : 100; if (rec.max < rec.min) [rec.min, rec.max] = [rec.max, rec.min]; rec.step = Number.isInteger(+o.step) && +o.step > 0 ? +o.step : (rec.max - rec.min > 10 ? 1 : 0.01); rec.value = Math.max(rec.min, Math.min(rec.max, +o.value || 0)); }
    else if (type === "bool") rec.value = /^(1|true|on|yes)$/i.test(String(o.value)); else if (type === "color") rec.value = /^#[0-9a-f]{6}$/i.test(o.value) ? o.value : "#ffffff"; else rec.value = String(o.value || "");
    if (o.desc) rec.desc = String(o.desc); S.tags.define(name, rec); markDirty(); S.tags.emit([name]); S.tmSel = name; return null;
  }
  function tagMgrCenter() {
    const p = E.center; p.tabs.replaceChildren(...centerTabs(), h("span", { class: "sp" }), h("span", { class: "csize", text: "tag manager" }));
    E.host.className = "host maphost tmhost"; E.host.replaceChildren(); put(E.cbar, h("span", { class: "chip", text: "Tag Manager" }), h("button", { class: "tbtn sm", text: "+ New tag", onclick: () => { S.tmNew = true; renderCenter(); } }),
      h("span", { class: "hint", text: "Every value in the game is a tag. Make your own (coins, keys found, quest step…) and use them in the HUD, nodes, pick-ups and dialogue." }));
    const all = S.tags.names().filter(n => S.tags.spec(n) && S.tags.spec(n).group !== "script"), groups = new Map(); for (const n of all) { const g = tagGroup(n)[0]; groups.set(g, (groups.get(g) || 0) + 1); }
    const side = h("div", { class: "tmside" }), main = h("div", { class: "tmmain" }); E.host.append(h("div", { class: "tmwrap" }, side, main));
    const gbtn = (k, label, n) => h("button", { class: "tmg" + ((S.tmGroup || "all") === k ? " sel" : ""), onclick: () => { S.tmGroup = k; renderCenter(); } }, h("span", { text: label }), h("span", { class: "tpc", text: String(n) }));
    side.append(gbtn("all", "All tags", all.length), gbtn("mine", "★ My tags", all.filter(n => S.tags.spec(n).custom).length), gbtn("live", "⟳ Live (change while playing)", all.filter(n => tmLive(S.tags.spec(n))).length), h("div", { class: "tmsep" }), ...[...groups].sort((a, b) => a[0].localeCompare(b[0])).map(([g, c]) => gbtn(g, g, c)));
    // new tag form
    if (S.tmNew) {
      const v = S.tmDraft = S.tmDraft || { name: "", type: "number", label: "", value: "0", min: "0", max: "100", step: "1", desc: "" }, err = h("div", { class: "note warn" });
      const inp = (k, ph, w) => h("input", { class: "txt", value: v[k], placeholder: ph || "", style: w ? "width:" + w : "", "aria-label": k, oninput: e => { v[k] = e.target.value; } });
      const ty = h("select", { class: "dd", "aria-label": "Type", onchange: e => { v.type = e.target.value; v.value = v.type === "bool" ? "no" : v.type === "color" ? "#ffffff" : v.type === "number" ? "0" : ""; renderCenter(); } }, TTYPES.map(([a, b]) => h("option", { value: a, text: b, selected: v.type === a })));
      const valIn = v.type === "bool" ? h("select", { class: "dd", "aria-label": "Start value", onchange: e => { v.value = e.target.value; } }, [["no", "No"], ["yes", "Yes"]].map(([a, b]) => h("option", { value: a, text: b, selected: v.value === a }))) : v.type === "color" ? h("input", { type: "color", value: /^#[0-9a-f]{6}$/i.test(v.value) ? v.value : "#ffffff", "aria-label": "Start value", oninput: e => { v.value = e.target.value; } }) : inp("value", v.type === "number" ? "0" : "text");
      const go = () => { const e = tmCreate(v); if (e) { err.textContent = e; return; } S.tmNew = false; S.tmDraft = null; S.tmGroup = "mine"; renderCenter(); renderInspector(); };
      const row = (l, ...c) => h("div", { class: "tmf" }, h("label", { text: l }), h("div", { class: "tmfc" }, ...c));
      main.append(h("div", { class: "tmnew" }, h("div", { class: "tmnh", text: "New tag" }),
        row("Name", inp("name", "e.g. coins, keys_found, quest.step"), h("span", { class: "hint", text: "dots make folders: quest.step goes into “quest”" })), row("Type", ty), row("Label", inp("label", "shown in lists, e.g. Coins collected")),
        row(v.type === "number" ? "Start value" : "Value", valIn), v.type === "number" ? row("Lowest / highest", inp("min", "0", "90px"), inp("max", "100", "90px"), h("span", { class: "hint", text: "step" }), inp("step", "1", "70px")) : null,
        row("Note (optional)", inp("desc", "what it is for")), h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "Create tag", onclick: go }), h("button", { class: "btn sm", text: "Cancel", onclick: () => { S.tmNew = false; renderCenter(); } })), err));
    }
    // search + type filter
    const q = h("input", { class: "txt tmq", type: "search", placeholder: "Search by name or label…", value: S.tmQ || "", "aria-label": "Search tags" }), tyf = h("select", { class: "dd", "aria-label": "Type filter", onchange: e => { S.tmType = e.target.value; draw(); } }, [["all", "Every type"], ...TTYPES, ["other", "Links, files, choices"]].map(([a, b]) => h("option", { value: a, text: b, selected: (S.tmType || "all") === a })));
    const count = h("span", { class: "hint" }), table = h("div", { class: "tmtable" }); main.append(h("div", { class: "tmbar" }, q, tyf, count), table);
    q.oninput = () => { S.tmQ = q.value; draw(); };
    const draw = () => {
      const L = tmList(); count.textContent = L.length + " of " + all.length + " tags"; const MAX = 400;
      table.replaceChildren(h("div", { class: "tmr tmhd" }, h("span", { text: "Tag" }), h("span", { text: "Type" }), h("span", { text: "Value" }), h("span", { text: "" })),
        ...L.slice(0, MAX).map(n => { const sp = S.tags.spec(n), f = field(n, { compact: true });
          return h("div", { class: "tmr" + (S.tmSel === n ? " sel" : ""), onclick: e => { if (e.target.closest("input,select,button,textarea")) return; S.tmSel = n; table.querySelectorAll(".tmr.sel").forEach(x => x.classList.remove("sel")); e.currentTarget.classList.add("sel"); renderInspector(); } },
            h("span", { class: "tmn" }, h("b", { text: (sp.custom ? "★ " : "") + (sp.label || n) }), h("span", { class: "tpk", text: n })), h("span", { class: "tmt", text: tmTypeName(sp.type) + (tmLive(sp) ? " · live" : "") }), h("span", { class: "tmv" }, f),
            h("span", { class: "tmk" }, h("button", { class: "btn sm", title: "Copy {" + n + "} for texts", text: "{ }", onclick: () => { try { navigator.clipboard.writeText("{" + n + "}"); log("Copied {" + n + "}: paste it into any text to show this tag's value."); } catch (x) {} } }))); }),
        L.length > MAX ? h("div", { class: "note", text: "Showing the first " + MAX + ". Search or pick a group to narrow it down." }) : h("span"));
    };
    S.tmDraw = draw; table.addEventListener("change", () => setTimeout(() => { if (S.tab === "tagmgr") renderInspector(); }, 0));
    draw(); if (!S.tmNew) setTimeout(() => q.focus(), 0);
  }
  function tagMgrInspector() {
    const box = h("div", { class: "insp" }), n = S.tmSel, sp = n && S.tags.spec(n);
    if (!sp) { box.append(h("div", { class: "ihead" }, h("span", { class: "bigic", text: "#" }), h("div", { class: "ititle" }, h("div", { class: "idin", text: "Tag Manager" }), h("div", { class: "isub", text: S.tags.names().length + " tags" }))),
      h("div", { class: "note", text: "Click a tag in the table to see its details. ★ = your own tags (you can rename and delete those). Live tags are changed by the game while it runs (health, ammo, the player's state…)." }),
      h("div", { class: "note", text: "Tip: write {coins} in any text (a HUD title, a dialogue line, a message node) and it shows the tag's current value." }),
      h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "+ New tag", onclick: () => { S.tmNew = true; renderCenter(); } }))); return box; }
    const uses = tmUses(n), [g1, g2] = tagGroup(n); box.addEventListener("change", () => setTimeout(() => { if (S.tab === "tagmgr" && S.tmDraw) S.tmDraw(); }, 0));
    box.append(h("div", { class: "ihead" }, h("span", { class: "bigic", text: sp.custom ? "★" : "#" }), h("div", { class: "ititle" }, h("div", { class: "idin", text: sp.label || n }), h("div", { class: "isub", text: n + " · " + tmTypeName(sp.type) + (tmLive(sp) ? " · live" : "") + " · " + g1 + (g2 ? " › " + g2 : "") }))));
    if (sp.custom) {
      const nm = h("input", { class: "txt", value: n, "aria-label": "Tag name" }), lb = h("input", { class: "txt", value: sp.label || "", "aria-label": "Label" }), ds = h("input", { class: "txt", value: sp.desc || "", "aria-label": "Note" }), err = h("div", { class: "note warn" });
      nm.onchange = () => { const nv = nm.value.trim().toLowerCase(); if (nv === n) return; if (!TAGNAME.test(nv)) { err.textContent = "Use lowercase letters, numbers, _ and dots."; return; } if (!S.tags.rename(n, nv)) { err.textContent = "A tag called “" + nv + "” already exists."; return; } S.tags.spec(nv).group = nv.split(".")[0]; S.tmSel = nv; markDirty(); renderCenter(); renderInspector(); log("Renamed " + n + " to " + nv + " (everything that used it was updated)."); };
      lb.onchange = () => { sp.label = lb.value.trim() || n; markDirty(); S.tags.emit([n]); renderCenter(); }; ds.onchange = () => { sp.desc = ds.value.trim(); markDirty(); };
      const fl = (l, c) => h("div", { class: "fld" }, h("label", { class: "flab", text: l }), h("div", { class: "fctl" }, c));
      const extra = []; if (sp.type === "number") { const mn = h("input", { class: "num", type: "number", value: sp.min, "aria-label": "Lowest" }), mx = h("input", { class: "num", type: "number", value: sp.max, "aria-label": "Highest" }), st = h("input", { class: "num", type: "number", value: sp.step, "aria-label": "Step" });
        const upd = () => { let a = +mn.value, b = +mx.value; if (!isFinite(a) || !isFinite(b)) return; if (b < a) [a, b] = [b, a]; sp.min = a; sp.max = b; if (+st.value > 0) sp.step = +st.value; S.tags.set(n, Math.max(a, Math.min(b, +S.tags.get(n) || 0))); markDirty(); renderCenter(); renderInspector(); }; mn.onchange = mx.onchange = st.onchange = upd;
        extra.push(fl("Lowest / highest", h("div", { class: "fctl mm" }, mn, mx)), fl("Step", st)); }
      box.append(foldout("tm:def", "Your tag", fl("Name", nm), fl("Label", lb), fl("Note", ds), ...extra, err));
    } else if (sp.desc) box.append(h("div", { class: "note", text: sp.desc }));
    box.append(foldout("tm:val", tmLive(sp) ? "Value (the game changes it while playing)" : "Value", field(n)));
    box.append(foldout("tm:uses", "Used by (" + uses.length + ")", ...(uses.length ? uses.map(u => h("button", { class: "btn sm", style: "display:block;width:100%;text-align:left;margin-top:3px", title: u, text: (S.tags.spec(u).label || u) + "  ·  " + u, onclick: () => { S.tmSel = u; S.tmQ = ""; S.tmGroup = "all"; renderCenter(); renderInspector(); } }))
      : [h("div", { class: "note", text: "Nothing points at this tag yet (tag links and {" + n + "} in texts are counted)." })])));
    box.append(h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "Copy {" + n + "}", onclick: () => { try { navigator.clipboard.writeText("{" + n + "}"); } catch (x) {} log("Copied {" + n + "}."); } }),
      sp.custom ? h("button", { class: "btn sm", text: "Duplicate", onclick: () => { let i = 2; while (S.tags.has(n + "_" + i)) i++; S.tags.define(n + "_" + i, JSON.parse(JSON.stringify(sp))); S.tmSel = n + "_" + i; markDirty(); S.tags.emit([]); renderCenter(); renderInspector(); } }) : null,
      sp.custom ? h("button", { class: "btn sm danger", text: "Delete", onclick: () => { if (uses.length && !confirm("“" + n + "” is used by " + uses.length + " other value(s). Delete it anyway?")) return; delete S.project.tags[n]; S.tmSel = null; markDirty(); S.tags.emit([]); renderCenter(); renderInspector(); } }) : null));
    return box;
  }
  function tagRow(n) {
    const t = S.tags.spec(n);
    if (!t.custom) return field(n, { compact: true });
    return h("div", {}, h("div", { class: "thead" }, h("input", { class: "tname", value: n, "aria-label": "Tag name", onchange: e => { const nv = e.target.value.trim().toLowerCase(); if (/^[a-z][a-z0-9_]*(\.[a-z0-9_]+)*$/.test(nv) && S.tags.rename(n, nv)) S.tags.t[nv].group = nv.split(".")[0]; renderInspector(); } }),
      h("button", { class: "btn sm", "aria-label": "Delete tag", text: "✕", onclick: () => { delete S.project.tags[n]; S.tags.emit([]); renderInspector(); } })), field(n, { compact: true }));
  }

  // ================================================================ MAPS (DOOM-style editor + 3D fly view)
  const curMap = () => S.project.maps.find(m => m.id === S.mapId);
  function killMapViews() { if (S.med) { S.med.destroy(); S.med = null; } if (S.v3) { S.v3.dispose(); S.v3 = null; } if (S.p3) { S.p3.destroy(); S.p3 = null; } if (S.tlv) { S.tlv.destroy(); S.tlv = null; } if (S.coderStop) { S.coderStop(); S.coderStop = null; } if (S.oev) { S.oev.destroy(); S.oev = null; } if (S.wed) { S.wed.destroy(); S.wed = null; } if (S.pev) { S.pev.destroy(); S.pev = null; } }
  function selectMap(id, sel) { S.fsel = null; const was = S.mapId, tc = (S.playerSel || S.tab === "timeline") ? (S.tab = "edit", S.testing = false, true) : false; S.playerSel = false; S.mapId = id; S.selId = null; S.sel = null; S.msel = sel || null; renderHier(); renderInspector(); renderProject(); if (was !== id || !S.med || tc) renderCenter(); else S.med.select(S.msel, true); }
  function openNewMap() { createMap(); }
  const reserved = () => [...screens().map(s => s.id), ...S.project.maps.map(m => m.id), ...(S.project.events || []).map(e => e.id), ...(S.project.animLen || []).map(e => e.id), "audio", "gfx", "project", "player", "comp"];
  function createMap() {
    let n = 1; while (reserved().includes("map" + n)) n++; const id = "map" + n; Schema.newMap(S.project, id); S.tags.emit([]); markDirty();
    S.mapId = id; S.msel = null; S.selId = null; S.sel = null; S.testing = false; S.paused = false; S.rtab = "inspector"; S.tab = "edit"; renderToolbar(); renderHier(); renderCenter(); renderInspector(); renderProject(); log("Created map “" + id + "”: one room to start from. Pick a texture on the left, a tool above, and click cells.");
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
    const m = curMap(), p = E.center, G = PRIM.Grid;
    const fly = S.tab === "fly", nc = m.layers.reduce((t, l) => t + Object.keys(l.cells).length, 0);
    p.tabs.replaceChildren(...centerTabs(), h("span", { class: "sp" }), h("span", { class: "csize", text: "grid map · " + m.layers.length + " stor" + (m.layers.length === 1 ? "y" : "ies") + " · " + nc + " cells · 1 cell = 2 m" }));
    put(E.cbar, h("span", { class: "chip", text: m.id }), h("button", { class: "tbtn sm", text: "▶ Walk this map", title: "Try the map as the player (Esc to leave)", onclick: () => walkMap(m) }), h("span", { class: "hint", text: fly ? "Hold the RIGHT mouse button and use W A S D to fly (mouse looks, Q / E down and up, Shift = fast). Let go to stand still, then LEFT-click a wall side, floor or ceiling to select it (Shift adds more) and give it a texture in the Inspector, or drop an image from the Project panel onto it. Each side of a wall block can have its own texture." : "Pick a texture on the left and a tool above, then click or drag over cells. Right mouse takes things away. Drop an image from the Project panel onto a cell to texture it (Shift = its ceiling)." }));
    E.host.className = "host maphost"; E.host.replaceChildren();
    if (fly) { S.v3 = makeView(E.host, m); if (S.v3) { S.v3.focus = false; S.v3.editMode = true; S.v3.onPick = flyPick; S.v3.onHover = flyHover; flyOverlay(); } }
    else { S.med = new PRIM.MapEditor({ host: E.host, project: S.project, map: m, tags: S.tags, getUrls: () => S.urls, log, getProps: () => (S.project.props || []).map(p => ({ id: p.id, name: S.tags.get(p.id + ".name") || p.id, type: S.tags.get(p.id + ".type") })), getProp: () => { const ps = S.project.props || []; return ps.some(p => p.id === S.propSel) ? S.propSel : (ps[0] || {}).id; }, setProp: id => { S.propSel = id; }, onSelect: sel => { S.msel = sel; renderInspector(); },
      onChange: (geo, structure) => { markDirty(); if (structure) renderHier(); renderInspector(); } }); if (S.msel) S.med.select(S.msel, true); }
  }
  // ---- Fly Editor: pick faces (a side of a wall block, a floor or a ceiling) and texture them
  const faceKey = f => [f.kind, f.layer, f.x, f.y, f.side === undefined ? "" : f.side].join("|");
  const DIRN = ["north", "east", "south", "west", "underside"];
  function faceOutline(m, f, col, w) {
    const G = PRIM.Grid, C = G.CELL, B = f.layer * G.LAYER, X0 = f.x * C, Z0 = f.y * C, X1 = X0 + C, Z1 = Z0 + C, out = [], ln = (a, b) => out.push({ kind: "line", a, b, c: col, w, life: 1, t: 0 }), loop = ps => { for (let i = 0; i < ps.length; i++) ln(ps[i], ps[(i + 1) % ps.length]); ln(ps[0], ps[2]); };
    const c = G.cell(m, f.layer, f.x, f.y), e = 0.7;
    if (f.kind === "diag" && c && c.dg) { const pts = c.dg.map(([u, v]) => [X0 + u * C, Z0 + v * C]); for (let i = 0; i < pts.length; i++) { const a = pts[i], b = pts[(i + 1) % pts.length]; ln([a[0], B + 1, a[1]], [b[0], B + 1, b[1]]); ln([a[0], B + G.LAYER - 1, a[1]], [b[0], B + G.LAYER - 1, b[1]]); ln([a[0], B + 1, a[1]], [a[0], B + G.LAYER - 1, a[1]]); } return out; }
    if (f.kind === "wall" && f.side < 4) { const sd = f.side, X = sd === 1 ? X1 + e : sd === 3 ? X0 - e : null, Z = sd === 0 ? Z0 - e : sd === 2 ? Z1 + e : null;
      if (X !== null) loop([[X, B + 1, Z0], [X, B + 1, Z1], [X, B + G.LAYER - 1, Z1], [X, B + G.LAYER - 1, Z0]]); else loop([[X0, B + 1, Z], [X1, B + 1, Z], [X1, B + G.LAYER - 1, Z], [X0, B + G.LAYER - 1, Z]]); }
    else { const y = f.kind === "ceil" ? B + G.LAYER - 1 - e : f.kind === "walltop" ? B + G.LAYER + e : f.kind === "wall" ? B - e : null;
      if (y !== null) loop([[X0, y, Z0], [X1, y, Z0], [X1, y, Z1], [X0, y, Z1]]);
      else { const hy = (u, v) => B + (G.hAt(c, u, v) || 0) + e; loop([[X0, hy(0.001, 0.001), Z0], [X1, hy(0.999, 0.001), Z0], [X1, hy(0.999, 0.999), Z1], [X0, hy(0.001, 0.999), Z1]]); } }
    return out;
  }
  function flyOverlay() {
    const v = S.v3, m = curMap(); if (!v || !m) return; let list = [];
    for (const f of S.fsl || []) list.push(...faceOutline(m, f, [1, 0.85, 0.15], 1.1));
    if (S.fhov && !(S.fsl || []).some(f => faceKey(f) === faceKey(S.fhov))) list.push(...faceOutline(m, S.fhov, [0.75, 0.9, 1], 0.6)); v.overlay = list;
  }
  function flyHit(x, y) { const v = S.v3; if (!v) return null; const r = v.rayAt(x, y), hit = v.raycast(r.o, r.d, 1e5); if (!hit || !hit.info) return null; const f = Object.assign({}, hit.info); if (f.kind === "floor") delete f.side; return f; }
  function flyPick(p) {
    const f = flyHit(p.x, p.y), cur = S.fsl || []; if (!f) S.fsl = p.shift ? cur : []; else if (p.shift) S.fsl = cur.some(o => faceKey(o) === faceKey(f)) ? cur.filter(o => faceKey(o) !== faceKey(f)) : cur.concat([f]); else S.fsl = [f];
    flyOverlay(); renderInspector();
  }
  function flyHover(x, y) { if (S.fhovT) return; S.fhovT = requestAnimationFrame(() => { S.fhovT = 0; if (S.tab !== "fly" || !S.v3) return; const f = flyHit(x, y), a = S.fhov; if ((f && faceKey(f)) !== (a && faceKey(a))) { S.fhov = f; flyOverlay(); } }); }
  // the texture of one face: a wall side may have its own (ws), else the block's
  const faceTex = (m, f) => { const c = PRIM.Grid.cell(m, f.layer, f.x, f.y); if (!c) return ""; if (f.kind === "diag") return c.dt; if (f.kind === "wall" || f.kind === "walltop") return f.kind === "wall" && f.side < 4 && c.ws && c.ws[f.side] !== undefined ? c.ws[f.side] : c.w; return f.kind === "ceil" ? c.c : c.f; };
  function flyApply(value, whole) {   // a texture on every selected face (whole = the whole wall block)
    const m = curMap(), G = PRIM.Grid; for (const f of S.fsl || []) { const c = G.cell(m, f.layer, f.x, f.y); if (!c) continue;
      if (f.kind === "diag") c.dt = value; else if (f.kind === "wall" && f.side < 4 && !whole) { c.ws = c.ws || {}; c.ws[f.side] = value; } else if (f.kind === "wall" || f.kind === "walltop") { c.w = value; delete c.ws; } else if (f.kind === "ceil") c.c = value; else c.f = value; }
    markDirty(); if (S.v3) S.v3.rebuild();
  }
  function flyInspector() {
    const m = curMap(), box = h("div", { class: "insp" }), fs = S.fsl, imgs = S.project.assets.filter(a => a.type === "image");
    const kind = f => f.kind === "diag" ? "diagonal wall" : f.kind === "wall" ? (f.side < 4 ? DIRN[f.side] + " side of a wall" : "underside of a wall") : f.kind === "walltop" ? "top of a wall" : f.kind === "ceil" ? "ceiling" : "floor";
    const names = [...new Set(fs.map(kind))].join(" + "), vals = new Set(fs.map(f => faceTex(m, f))), tex = vals.size === 1 ? [...vals][0] : null, walls = fs.some(f => f.kind === "wall" && f.side < 4);
    box.append(h("div", { class: "ihead" }, h("span", { class: "bigic", text: "▦" }), h("div", { class: "ititle" }, h("div", { class: "idin ro", text: fs.length + " face" + (fs.length > 1 ? "s" : "") + " selected" }), h("div", { class: "isub", text: names }))),
      h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "Select nothing", onclick: () => { S.fsl = []; flyOverlay(); renderInspector(); } })),
      foldout("ff", "Texture",
        h("div", { class: "fld" }, h("label", { class: "flab" }, h("span", { text: "Texture" })), h("div", { class: "fctl" }, h("select", { class: "sel", "aria-label": "Texture of the selected faces", onchange: e => { flyApply(e.target.value); renderInspector(); } }, tex === null ? h("option", { value: "__mixed", text: "(different)", selected: true }) : "", h("option", { value: "", text: "Default", selected: tex === "" }), ...imgs.map(a => h("option", { value: a.id, text: a.name, selected: tex === a.id }))))),
        walls ? h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "Use it on every side of the block", onclick: () => { flyApply(tex || "", true); renderInspector(); } })) : ""),
      h("div", { class: "note", text: "Left-click selects, Shift-click adds or removes. Each side of a wall block can have its own texture. You can also drop an image from the Project panel onto a face." }));
    return box;
  }
  function flyDrop(e, a) {
    const v = S.v3; if (!v) return log("Switch to the Fly Editor tab to texture faces.", "warn"); const r = v.canvas.getBoundingClientRect(), f = flyHit(e.clientX - r.left, e.clientY - r.top); if (!f) return log("Drop the image onto a wall, floor or ceiling.", "warn");
    if (!(S.fsl || []).some(o => faceKey(o) === faceKey(f))) S.fsl = [f]; flyOverlay(); flyApply(a.id); renderInspector(); log("Texture set to " + a.name + " on " + S.fsl.length + " face(s)");
  }
  function makeView(host, m) {
    const cv = h("canvas", { class: "v3cv" }); host.append(cv); const v = new PRIM.R3D.View(cv), T = S.tags, G = PRIM.Grid;
    if (!v.ok) { cv.remove(); host.append(h("div", { class: "v3err", text: "WebGL could not start on this computer, so the 3D view is unavailable." })); return null; }
    v.setMap(S.project, m, T, S.urls);
    const sx = +T.get(m.id + ".startX"), sy = +T.get(m.id + ".startY"), L = +T.get(m.id + ".startLayer") || 0;
    v.placeCamera(sx, sy, +T.get(m.id + ".startAngle"), G.groundY(m, sx, sy, L * G.LAYER + G.LAYER - 8) + (+T.get("player.eyeHeight") || 102)); v.attach(); v.start();
    let t; const un = T.on(() => { clearTimeout(t); t = setTimeout(() => v.rebuild(), 60); });
    const hud = h("div", { class: "v3pos" }); host.append(hud); let n = 0; v.onFrame = () => { if (n++ % 10 === 0) hud.textContent = "x " + Math.round(v.cam.x) + "   y " + Math.round(v.cam.z) + "   height " + Math.round(v.cam.y) + "   story " + (G.layerOf(v.cam.y - 100) + 1) + "   " + v.triCount + " triangles"; };
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
    const sm = rt.tags.get("player.startMap"), m = S.project.maps.find(x => x.id === sm) || S.project.maps[0]; if (!m) { rt.game = true; rt.rebuild(); return; }
    rt.stopMusic(); const layer = h("div", { class: "p3layer" }); rt.host.append(layer);
    const pl = new PRIM.Play3D({ host: layer, project: S.project, map: m, tags: rt.tags, urls: S.urls, mode: "play", debug: true, say: t => log(t), onExit: () => { pl.destroy(); layer.remove(); },
      onMenu: () => { pl.destroy(); layer.remove(); const mm = S.project.screens.find(x => x.id === "main") || S.project.screens.find(x => x.kind === "menu"); if (mm) rt.show(mm.id); },
      onQuit: () => { pl.destroy(); layer.remove(); if (rt.o.onExit) rt.o.onExit(); },
      onScreen: id => { pl.destroy(); layer.remove(); if (S.project.screens.some(x => x.id === id)) rt.show(id); } }); S.play = pl;
  }
  function walkMap(m) {
    const layer = h("div", { class: "play-layer p3layer" }); document.body.append(layer);
    const pl = new PRIM.Play3D({ host: layer, project: S.project, map: m, tags: S.tags, urls: S.urls, mode: "play", debug: true, say: t => log(t), onExit: () => { pl.destroy(); layer.remove(); },
      onScreen: id => { pl.destroy(); layer.remove(); log("The game would now show the screen “" + id + "” (use ▶ Play or Maximize on Play to try the whole game)."); } }); S.play = pl;
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
    const type = T.get(pre + "type"), isW = type === "weapon", aid = T.get(pre + (type === "image" ? "image" : "asset")), mo = F.cache[aid]; if (!isW && ["muzzle", "arrow", "emitter", "earrow"].includes(OE.tool)) OE.tool = "orbit"; const lty = T.get(pre + "lightType"); if ((OE.tool === "lightpos" && (!lty || lty === "none")) || (OE.tool === "lightaim" && lty !== "area")) OE.tool = "orbit";
    p.tabs.replaceChildren(...centerTabs(), h("span", { class: "sp" }), h("span", { class: "csize", text: "object editor" }));
    const tb = (t, l, on, fn) => h("button", { class: "tbtn sm" + (on ? " on" : ""), text: l, onclick: fn }), tool = (k, l) => tb(k, l, OE.tool === k, () => { OE.tool = k; renderCenter(); }), view = (l, y, pt) => tb(l, l, false, () => { OE.yaw = y; OE.pitch = pt; });
    const hints = { orbit: "Drag to turn the object around, scroll to zoom.", pivot: "Click or drag: the colored cross is the pivot (origin) of this object. It turns, scales and sits where you put it.", muzzle: "Click or drag on the object: the yellow cross (where shots leave the weapon) follows your mouse.", arrow: "Drag: the red arrow points to your mouse, as seen in this view. Switch between Front / Side / Top to aim it in every direction.", emitter: "Click or drag on the object: the cyan cross is where projectiles start (the projectile emitter).", earrow: "Drag: the cyan arrow is the direction projectiles fly, as seen in this view. Switch between Front / Side / Top to aim it in every direction.", lightpos: "Click or drag on the object: the glowing cross is where the light sits.", lightaim: "Drag: the arrow shows which way the light panel shines, as seen in this view. Switch between Front / Side / Top." };
    put(E.cbar, h("span", { class: "chip", text: T.get(pre + "name") || att.id }), tb("pv", "◀ Player view", false, () => { S.objView = false; renderCenter(); }), tool("orbit", "Look around"), tool("pivot", "Set pivot"), isW ? [tool("muzzle", "Set muzzle point"), tool("arrow", "Aim arrow"), tool("emitter", "Set projectile emitter"), tool("earrow", "Aim emitter arrow")] : null, (lty && lty !== "none") ? tool("lightpos", "Set light point") : null, lty === "area" ? tool("lightaim", "Aim light") : null, tb("pc", "Pivot to middle", false, () => { const b = mo && mo.bounds; if (b) T.setMany({ [pre + "pivotX"]: +((b.lo[0] + b.hi[0]) / 2).toFixed(1), [pre + "pivotY"]: +((b.lo[1] + b.hi[1]) / 2).toFixed(1), [pre + "pivotZ"]: +((b.lo[2] + b.hi[2]) / 2).toFixed(1) }); renderInspector(); }), tb("po", "Pivot to origin", false, () => { T.setMany({ [pre + "pivotX"]: 0, [pre + "pivotY"]: 0, [pre + "pivotZ"]: 0 }); renderInspector(); }), view("Front", 0, 0), view("Side", 90, 0), view("Top", 0, 89),
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
      if (lty && lty !== "none") {   // the light: a cross in its color (and the panel with an arrow for an area light)
        const lp = M4.point(fr.M, g("lightX"), g("lightY"), g("lightZ")), lc = (h2 => [1, 3, 5].map(i => parseInt(h2.slice(i, i + 2), 16) / 255))(String(T.get(pre + "lightColor") || "#ffe2b0"));
        for (const d of [[1, 0, 0], [0, 1, 0], [0, 0, 1]]) o.push({ a: M4.sub(lp, d.map(q => q * 1.4)), b: M4.add(lp, d.map(q => q * 1.4)), c: lc });
        if (lty === "area") { const yw = g("lightYaw") * Math.PI / 180, pt = g("lightPitch") * Math.PI / 180, n = M4.norm(M4.dir(fr.M, -Math.cos(pt) * Math.sin(yw), Math.sin(pt), -Math.cos(pt) * Math.cos(yw))), ax = M4.norm(M4.cross(n, Math.abs(n[1]) > 0.95 ? [1, 0, 0] : [0, 1, 0])), ay = M4.cross(n, ax), hw = (g("lightWidth") || 40) / 2 * fr.k, hh = (g("lightHeight") || 20) / 2 * fr.k;
          const cr = [[1, 1], [-1, 1], [-1, -1], [1, -1]].map(([a, b]) => M4.add(lp, M4.add(ax.map(q => q * hw * a), ay.map(q => q * hh * b))));
          for (let i = 0; i < 4; i++) o.push({ a: cr[i], b: cr[(i + 1) % 4], c: lc }); const tip = M4.add(lp, n.map(q => q * 7)); o.push({ a: lp, b: tip, c: lc }, { a: tip, b: M4.add(tip, M4.add(n.map(q => -q * 1.6), ax.map(q => q * 0.9))), c: lc }, { a: tip, b: M4.add(tip, M4.sub(n.map(q => -q * 1.6), ax.map(q => q * 0.9))), c: lc }); } }
      return o; };
    const edLights = fr => {   // preview: the object's own light falls on it (the editor camera looks down +Z, see render3d useLights)
      const test = S.objTab === "material" ? [{ p: [-30, 56 + 35, 5], r: 600, c: [1.1, 1.05, 1], type: 0 }] : [];   // a test light while the Material tab is open
      if (!lty || lty === "none") return test; const w = q => [-q[0], 56 + q[1], -q[2]], wd = q => M4.norm([-q[0], q[1], -q[2]]), col = Schema.matOf({ get: () => undefined }, pre) && (() => { const h = String(T.get(pre + "lightColor") || "#ffe2b0"); return [1, 3, 5].map(i => parseInt(h.slice(i, i + 2), 16) / 255); })(), k = +T.get(pre + "lightIntensity") || 0;
      const L = { p: w(M4.point(fr.M, g("lightX"), g("lightY"), g("lightZ"))), r: (+T.get(pre + "lightRange") || 300) * fr.k, c: col.map(q => q * k), type: 0 };
      if (lty === "area") { const yw = g("lightYaw") * Math.PI / 180, pt = g("lightPitch") * Math.PI / 180, n = M4.norm(M4.dir(fr.M, -Math.cos(pt) * Math.sin(yw), Math.sin(pt), -Math.cos(pt) * Math.cos(yw))), ax = M4.norm(M4.cross(n, Math.abs(n[1]) > 0.95 ? [1, 0, 0] : [0, 1, 0])); L.type = 1; L.n = wd(n); L.u = wd(ax); L.hw = (g("lightWidth") || 40) / 2 * fr.k; L.hh = (g("lightHeight") || 20) / 2 * fr.k; }
      return [L].concat(test); };
    let raf = 0; const loop = () => { raf = requestAnimationFrame(loop); const fr = frame(); v.cam = { x: 0, y: 56, z: 0, yaw: Math.PI / 2, pitch: 0 }; v.draw(); v.setLights(edLights(fr)); const mt = matGL(v, pre); v.drawViewmodel([{ gm, pose: fr.pose, M: fr.M, mat: mt, tex: mt.texId ? v.texFor(mt.texId, S.urls) : null }], segs(fr)); }; raf = requestAnimationFrame(loop);
    // mouse -> ray in camera space -> point on the object (or on the plane through its middle when it misses)
    const ray = e => { const r = cv.getBoundingClientRect(), t = Math.tan(v.fov / 2), asp = r.width / r.height; return M4.norm([((e.clientX - r.left) / r.width * 2 - 1) * t * asp, (1 - (e.clientY - r.top) / r.height * 2) * t, -1]); };
    const toModel = (fr, pc) => { const q = M4.dir(fr.Rinv, pc[0], pc[1], pc[2] + D).map(x => x / fr.k); return [fr.c[0] + q[0], fr.c[1] + q[1], fr.c[2] + q[2]]; };
    const surface = (fr, d) => { let best = Infinity;
      mo.nodes.forEach((n, i) => { if (!n.mesh) return; const W = M4.mul(fr.M, fr.pose[i]), P = n.mesh.p, vt = j => M4.point(W, P[j * 3], P[j * 3 + 1], P[j * 3 + 2]);
        for (let j = 0; j + 2 < P.length / 3; j += 3) { const a = vt(j), b = vt(j + 1), c = vt(j + 2), e1 = M4.sub(b, a), e2 = M4.sub(c, a), pv = M4.cross(d, e2), det = M4.dot(e1, pv); if (Math.abs(det) < 1e-9) continue; const tv = M4.sub([0, 0, 0], a), u = M4.dot(tv, pv) / det; if (u < 0 || u > 1) continue; const qv = M4.cross(tv, e1), w = M4.dot(d, qv) / det; if (w < 0 || u + w > 1) continue; const t = M4.dot(e2, qv) / det; if (t > 0 && t < best) best = t; } });
      if (best < Infinity) return d.map(q => q * best); return d.map(q => q * (D / -d[2])); };
    const apply = e => {
      const fr = frame(), d = ray(e);
      if (OE.tool === "muzzle" || OE.tool === "emitter" || OE.tool === "lightpos") { const nm = OE.tool === "muzzle" ? "muzzle" : OE.tool === "lightpos" ? "light" : "emit", m = toModel(fr, surface(fr, d)).map(x => Math.round(x * 10) / 10); T.setMany({ [pre + nm + "X"]: m[0], [pre + nm + "Y"]: m[1], [pre + nm + "Z"]: m[2] }); }
      else if (OE.tool === "pivot") { const m = toModel(fr, surface(fr, d)).map(x => Math.round(x * 10) / 10); T.setMany({ [pre + "pivotX"]: m[0], [pre + "pivotY"]: m[1], [pre + "pivotZ"]: m[2] }); }
      else if (OE.tool === "arrow" || OE.tool === "earrow" || OE.tool === "lightaim") {
        const E_ = OE.tool === "arrow", LA = OE.tool === "lightaim", pn = LA ? "light" : E_ ? "muzzle" : "emit", yk = LA ? "lightYaw" : E_ ? "arrowYaw" : "emitYaw", pk = LA ? "lightPitch" : E_ ? "arrowPitch" : "emitPitch";
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
    return [h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "Open the Player's Node Coder (double jump, dash, landing…)", onclick: () => { S.coderT = { kind: "player" }; S.cnode = null; setTab("coder"); } })), foldout("pl:weapons", "Weapon list", ...(ws.length ? ws.map((a, i) => row(a, i + 1)) : [h("div", { class: "note", text: "No weapons yet. In game, number keys 1-9 and the mouse wheel switch between them." })]),
        h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "+ New weapon ▾", onclick: e => popMenu(e.currentTarget, models.length ? models.map(a => ({ t: a.name, fn: () => addObject("weapon", a) })) : nope("Import an .fbx file first (Assets > Import Files)")) }))),
      foldout("pl:objects", "Other objects (animations, images)", ...(os.length ? os.map(a => row(a, 0)) : [h("div", { class: "note", text: "3D models with animations, or flat pictures, fixed to the camera (hands, a visor...)." })]),
        h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "+ Animation object ▾", onclick: e => popMenu(e.currentTarget, models.length ? models.map(a => ({ t: a.name, fn: () => addObject("animation", a) })) : nope("Import an .fbx file first")) }), h("button", { class: "btn sm", text: "+ Image object ▾", onclick: e => popMenu(e.currentTarget, images.length ? images.map(a => ({ t: a.name, fn: () => addObject("image", a) })) : nope("Import a picture first")) })))];
  }
  function selectedObjectEditor() {
    const att = S.project.player.attachments.find(a => a.id === S.objSel); if (!att) return null; const pre = "player." + att.id + ".", T = S.tags, ty = T.get(pre + "type");
    const tab = S.objTab = S.objTab || "object", tabs = h("div", { class: "objtabs" }, ...[["object", "Object"], ["material", "Material"], ["light", "Light"]].map(([k, l]) => h("button", { class: "tbtn sm" + (tab === k ? " on" : ""), text: l, onclick: () => { S.objTab = k; renderInspector(); } })));
    const mine = pr => !pr.live && tab === (pr.tab || "object");
    const note = tab === "material" ? "Material: a base color map (replaces the model's picture) and an emission map + strength (what glows). That is all a material has." : tab === "light" ? "Light: an object can shine. A point light shines everywhere around it; an area light is a flat panel that shines one way. Use “Set light point” and “Aim light” in the object editor." : null;
    return foldout("pl:obj", "Selected: " + (T.get(pre + "name") || att.id), tabs, note ? h("div", { class: "note", text: note }) : null, ...Schema.ATTACH.filter(pr => mine(pr) && Schema.shown(T, pre, pr)).map(pr => field(pre + pr.k)),
      tab === "object" && T.get(pre + "type") === "weapon" ? h("div", { class: "note", text: "This weapon's own ammo tags: " + pre + "ammo (loaded, ready to fire" + (T.get(pre + "ammoUse") ? ", now " + T.get(pre + "ammo") : "") + ") and " + pre + "reserve (carried" + (T.get(pre + "ammoUse") ? ", now " + T.get(pre + "reserve") : "") + "). Pick them in a HUD bar or text ({" + pre + "ammo}), an ammo pick-up's “gives to” tag, or a node." + (T.get(pre + "ammoUse") ? "" : " Switch on “Uses ammo” to use them.") }) : null,
      h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "Open Node Coder", onclick: () => { S.coderT = { kind: "att", id: att.id }; setTab("coder"); } }), ty === "weapon" ? h("button", { class: "btn sm", text: "Edit muzzle, arrow & emitter", onclick: () => { S.objView = true; renderCenter(); } }) : null),
      h("div", { class: "note", text: ty === "weapon" ? "A weapon turns its arrow toward whatever the crosshair is aiming at. Place the muzzle point and the arrow in the object editor." : "This object stays fixed to the camera." }));
  }
  // ================================================================ NODE CODER (scripts made of connected nodes, one script per object)
  // ---------------------------------------------------------------- Compositor: post-processing chain (Blender style), live preview of a map
  function compList() { return (S.project.compositor = S.project.compositor || { effects: [] }).effects; }
  function compAdd(type) { const e = Schema.addComp(S.project, type); S.compSel = e.id; markDirty(); S.tags.emit([]); renderCenter(); renderInspector(); }
  function compMove(id, d) { const L = compList(), i = L.findIndex(e => e.id === id), j = i + d; if (i < 0 || j < 0 || j >= L.length) return; [L[i], L[j]] = [L[j], L[i]]; markDirty(); S.tags.emit([]); renderCenter(); renderInspector(); }
  function compDel(id) { Schema.removeComp(S.project, id); if (S.compSel === id) S.compSel = null; markDirty(); S.tags.emit([]); renderCenter(); renderInspector(); }
  function compAddMenu(btn) { popMenu(btn, Object.entries(Schema.COMPFX).map(([k, d]) => ({ t: d.icon + "  " + d.label, fn: () => compAdd(k) }))); }
  function compCenter() {
    const p = E.center; p.tabs.replaceChildren(...centerTabs(), h("span", { class: "sp" }), h("span", { class: "csize", text: "compositor" }));
    E.host.className = "host maphost"; E.host.replaceChildren(); const L = compList(), T = S.tags, m = curMap() || S.project.maps[0];
    const cmp = h("button", { class: "tbtn sm", text: "Hold to compare (effects off)" }); cmp.onpointerdown = () => { S.compOff = true; cmp.classList.add("on"); }; cmp.onpointerup = cmp.onpointerleave = () => { S.compOff = false; cmp.classList.remove("on"); };
    put(E.cbar, h("span", { class: "chip", text: "Compositor" }), h("button", { class: "tbtn sm", text: "+ Add effect ▾", onclick: e => compAddMenu(e.currentTarget) }), cmp,
      h("span", { class: "hint", text: m ? "Preview of “" + m.id + "”: hold the right mouse button + WASD to fly. Effects run from left to right on the finished picture." : "Create a map to see a live preview." }));
    const pv = h("div", { class: "comppv" }), strip = h("div", { class: "compstrip" }); E.host.append(pv, strip);
    if (m) { S.v3 = makeView(pv, m); if (S.v3) { S.v3.post = true; S.v3.focus = false; S.v3.editMode = true; S.v3.compOff = () => !!S.compOff; } }
    else pv.append(h("div", { class: "v3err", style: "color:var(--muted)", text: "No map yet: the effects still work in the game, but a preview needs a map (Screen > Create Map)." }));
    const box = (cls, title, sub, extra) => h("div", Object.assign({ class: "cfx " + cls }, extra || {}), h("div", { class: "cfh" }, title), h("div", { class: "cfb", text: sub }));
    const arrow = () => h("div", { class: "cfa", text: "→" });
    const items = [box("io", h("span", { text: "Render" }), "the game picture + the map's bloom and exposure", { title: "Bloom and exposure are set per map (map Inspector > Look)" })];
    for (const e of L) {
      const d = Schema.COMPFX[e.type]; if (!d) continue; const on = T.get("comp." + e.id + ".on") !== false;
      const head = [h("input", { type: "checkbox", checked: on, title: "Effect on / off", onclick: ev => ev.stopPropagation(), onchange: ev => { T.set("comp." + e.id + ".on", ev.target.checked); markDirty(); renderCenter(); renderInspector(); } }), h("span", { class: "cft", text: d.icon + " " + d.label })];
      const tools = h("div", { class: "cfk" }, h("button", { text: "◀", title: "Earlier in the chain", onclick: ev => { ev.stopPropagation(); compMove(e.id, -1); } }), h("button", { text: "▶", title: "Later in the chain", onclick: ev => { ev.stopPropagation(); compMove(e.id, 1); } }), h("button", { text: "✕", title: "Remove", onclick: ev => { ev.stopPropagation(); compDel(e.id); } }));
      const b = h("div", { class: "cfx fx" + (on ? "" : " off") + (S.compSel === e.id ? " sel" : ""), onclick: () => { S.compSel = e.id; renderCenter(); renderInspector(); } }, h("div", { class: "cfh" }, ...head), h("div", { class: "cfb", text: e.id + " · " + (T.get("comp." + e.id + ".layer") === "front" ? "▲ in front of the HUD" : "▼ behind the HUD") }), tools);
      items.push(arrow(), b);
    }
    items.push(arrow(), h("button", { class: "cfx add", text: "+ Add effect", onclick: e => compAddMenu(e.currentTarget) }), arrow(), box("io", h("span", { text: "Screen" }), "what the player sees"));
    strip.append(...items);
  }
  function compInspector() {
    const box = h("div", { class: "insp" }), L = compList(), e = L.find(x => x.id === S.compSel);
    if (!e) { box.append(h("div", { class: "ihead" }, h("span", { class: "bigic", text: "▤" }), h("div", { class: "ititle" }, h("div", { class: "idin", text: "Compositor" }), h("div", { class: "isub", text: L.length + " effect" + (L.length === 1 ? "" : "s") + " in the chain" }))),
        h("div", { class: "note", text: "Effects change the finished game picture, one after another from left to right (the order matters: pixelate before a CRT screen looks different than after). Click an effect below the preview to edit it. Each effect has a Layer: behind the HUD (only the 3D picture) or in front of it (the HUD and crosshair get it too, e.g. scanlines or a vignette over everything). The preview here has no HUD; play the game to see the HUD layering. Every value is a tag (comp.<effect>.…), so a node can switch an effect on, e.g. night vision when a key is pressed." }),
        h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "+ Add effect ▾", onclick: ev => compAddMenu(ev.currentTarget) })),
        ...L.map(x => h("button", { class: "btn sm", style: "display:block;width:100%;text-align:left;margin-top:4px", text: (Schema.COMPFX[x.type] || {}).icon + "  " + (Schema.COMPFX[x.type] || {}).label + "  (" + x.id + ")", onclick: () => { S.compSel = x.id; renderCenter(); renderInspector(); } })),
        h("div", { class: "note", text: "Bloom and exposure belong to each map (select a map > Look), because a dark cave and a bright desert need different values. The Compositor is the same for the whole game." })); return box; }
    const d = Schema.COMPFX[e.type], pre = "comp." + e.id + ".", i = L.indexOf(e);
    box.append(h("div", { class: "ihead" }, h("span", { class: "bigic", text: d.icon }), h("div", { class: "ititle" }, h("div", { class: "idin", text: d.label }), h("div", { class: "isub", text: e.id + " · step " + (i + 1) + " of " + L.length }))),
      h("div", { class: "note", text: d.desc }),
      foldout("comp:fx", "Settings", ...Schema.compKeys(e.type).map(q => field(pre + q.k))),
      h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "◀ Earlier", onclick: () => compMove(e.id, -1) }), h("button", { class: "btn sm", text: "Later ▶", onclick: () => compMove(e.id, 1) }), h("button", { class: "btn sm danger", text: "Remove", onclick: () => compDel(e.id) })),
      h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "← All effects", onclick: () => { S.compSel = null; renderCenter(); renderInspector(); } })));
    return box;
  }
  function coderTarget() {
    if (S.tab === "dlg") { const d = (S.project.dialogues || []).find(x => x.id === S.dlgSel); return d ? { script: d, key: d.id, ctx: ["dialogue"], modelTag: null, name: d.name, what: "Dialogue", dlg: d } : null; }
    const t = S.coderT; if (!t) return null;
    if (t.kind === "zone") { const m = S.project.maps.find(x => x.id === t.map), z = m && (m.zones || []).find(x => x.id === t.id); if (!z || z.type !== "custom") return null; z.script = z.script || { nodes: [], links: []}; return { script: z.script, key: m.id + ".z." + z.id, ctx: ["zone", "player"], modelTag: null, name: S.tags.get(m.id + ".z." + z.id + ".name") || z.id, what: "Custom zone in “" + m.id + "”", zone: z, map: m }; }
    if (t.kind === "player") { const pl = S.project.player; if (!pl.script) pl.script = { nodes: [], links: [] }; return { script: pl.script, key: "player", ctx: ["player"], modelTag: null, name: "Player", what: "player (jumping, landing, getting hurt…)" }; }
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
    switch (n.type) { case "chargeSuck": return g("rate") + "/s from " + g("radius") + " units"; case "chargeShow": return "size " + g("size") + " · " + g("color"); case "chargeGrow": return "to size " + g("size") + (+g("seconds") > 0 ? " over " + g("seconds") + " s" : " as it charges"); case "onState": case "ifState": return (Schema.STATES.find(x => x[0] === g("state")) || ["", g("state")])[1]; case "onCharge": return "hold " + kl(g("key")) + " to charge"; case "fireProjectile": return "uses the weapon’s Projectile settings"; case "onKey": case "onKeyUp": case "ifKey": return kl(g("key")); case "onHold": return kl(g("key")) + " · every " + g("interval") + " s"; case "setTag": return (g("tag") || "(pick a tag)") + " = " + g("value"); case "changeTag": return (g("tag") || "(pick a tag)") + " " + (+g("amount") >= 0 ? "+ " : "− ") + Math.abs(+g("amount"));
      case "playSound": case "loopSound": return asset(g("sound")); case "stopLoop": return g("sound") ? asset(g("sound")) : "all looping sounds"; case "playAnim": return (g("anim") || "(pick one)") + (g("loop") ? " · loops" : ""); case "gotoScreen": return g("screen") || "(pick a screen)"; case "say": return "“" + g("text") + "”";
      case "dLine": return (g("speaker") ? g("speaker") + ": " : "") + "“" + String(g("text") || "").replace(/\s+/g, " ").slice(0, 60) + "”"; case "dChoice": return g("text") ? "“" + String(g("text")).replace(/\s+/g, " ").slice(0, 50) + "”" : "(asks after the last line)";
      case "startDialogue": { const d = (S.project.dialogues || []).find(x => x.id === g("dialog")); return d ? d.name : "(pick a dialogue)"; } case "pauseGame": return g("screen") || "the normal pause menu"; case "onInside": return "every " + g("interval") + " s";
      case "wait": return g("seconds") + " s"; case "if": return (g("tag") || "(pick a tag)") + " " + g("op") + " " + g("value"); case "repeat": return g("times") + " times"; default: return ""; }
  }
  function coderCenter() {
    const p = E.center, tg = coderTarget(); p.tabs.replaceChildren(...centerTabs(), h("span", { class: "sp" }), h("span", { class: "csize", text: "node coder" }));
    E.host.className = "host maphost";
    if (!tg || tg.splash) { put(E.cbar, h("span", { class: "chip", text: "Node Coder" })); E.host.replaceChildren(h("div", { class: "v3err", style: "color:var(--muted);text-align:center;padding:20px", text: tg && tg.splash ? "Splash elements can't have scripts." : "Select an element (in the Hierarchy) or a weapon / object under Player, and its script opens here." })); return; }
    const V = S.cview = S.cview || { px: 30, py: 30 }, sc = tg.script, T = S.tags, ns = "http://www.w3.org/2000/svg";
    const allowed = Object.keys(Schema.SNODES).filter(k => { const d = Schema.SNODES[k]; return d.ctx.some(c => tg.ctx.includes(c)) || (tg.ctx.includes("player") && d.ctx.includes("object") && !d.objOnly && d.cat !== "weapon"); });
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
          h("div", { class: "nch", text: (S.tags.get(Schema.snodeKey(tg.key, n.id, "needTag")) ? "🔒 " : "") + def.label, title: S.tags.get(Schema.snodeKey(tg.key, n.id, "needTag")) ? "Only if " + S.tags.get(Schema.snodeKey(tg.key, n.id, "needTag")) + " " + S.tags.get(Schema.snodeKey(tg.key, n.id, "needOp")) + " " + S.tags.get(Schema.snodeKey(tg.key, n.id, "needVal")) : "" }), h("div", { class: "ncs", text: ncSummary(tg, n) }),
          ...def.outs.map((o, j) => h("div", { class: "nco" + (def.outText && !String(S.tags.get(Schema.snodeKey(tg.key, n.id, o)) || "").trim() ? " off" : ""), style: "top:" + (NCH + NCS + j * NCR) + "px" }, h("span", { text: o === "next" ? "" : def.outText ? (String(S.tags.get(Schema.snodeKey(tg.key, n.id, o)) || "").trim() || "(unused)").slice(0, 26) : o }), h("i", { class: "ncdot", "data-out": o, "data-id": n.id }))));
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
    if (!sc.nodes.length) view.append(h("div", { class: "ncempty", text: tg.dlg ? "Empty dialogue. Add “Dialogue starts” (Events), then lines and questions from the Dialogue menu." : "Empty script. Press “+ Add node” and start with a green event (like “When clicked”), then add actions and drag a line from its dot to the next node." }));
    const un = T.on(() => { if (S.cdraw) draw(); }); S.cdraw = true; S.coderStop = () => { un(); S.cdraw = false; }; draw();
  }
  // ---- Dialogue Editor: dialogues are node graphs (project.dialogues). NPCs pick one (Object Editor > NPC > Dialogue); zones and scripts can start one too.
  function dlgCenter() {
    const L = S.project.dialogues = S.project.dialogues || []; if (!L.some(d => d.id === S.dlgSel)) S.dlgSel = L[0] ? L[0].id : null;
    if (!S.dlgSel) { const p = E.center; p.tabs.replaceChildren(...centerTabs()); E.host.className = "host maphost"; E.host.replaceChildren(h("div", { class: "empty-c" }, h("div", { text: "Dialogues are drawn as nodes: “Dialogue starts” → lines → questions with up to 4 answers, each answer going its own way. Tag nodes (set, change, if…) work inside them too, so a talk can remember choices or depend on them. Give a dialogue to an NPC in the Object Editor, or start one from a zone or any script." }), h("div", { class: "btnrow" }, h("button", { class: "btn", text: "+ New dialogue", onclick: () => dlgNew() }))));
      put(E.cbar, h("span", { class: "chip", text: "Dialogue Editor" }), h("span", { class: "hint", text: "No dialogues yet." })); return; }
    coderCenter();
    E.cbar.prepend(h("select", { class: "tsel", "aria-label": "Dialogue", onchange: e => { S.dlgSel = e.target.value; S.cnode = null; renderCenter(); renderInspector(); } }, ...L.map(d => h("option", { value: d.id, text: d.name, selected: d.id === S.dlgSel }))), h("button", { class: "tbtn sm", text: "+ New dialogue", onclick: () => dlgNew() }));
  }
  function dlgNew() { const d = Schema.addDialogue(S.project); S.dlgSel = d.id; S.cnode = null; S.tab = "dlg"; markDirty(); S.tags.emit([]); renderCenter(); renderInspector(); log("Created dialogue “" + d.name + "”"); }
  function dlgUses(d) { const out = []; for (const k of S.tags.names()) { const r = S.tags.spec(k); if (r && r.type === "dialogRef" && r.value === d.id) { const pr = (S.project.props || []).find(p => k.startsWith(p.id + ".")); out.push(pr ? "NPC “" + (S.tags.get(pr.id + ".name") || pr.id) + "”" : k); } } return out; }
  function coderDelete() {
    const tg = coderTarget(); if (!tg || tg.splash) return; const sc = tg.script;
    if (S.clink != null && sc.links[S.clink]) { sc.links.splice(S.clink, 1); S.clink = null; markDirty(); return renderCenter(); }
    const n = sc.nodes.find(x => x.id === S.cnode); if (!n) return; Schema.removeSNode(S.project, tg.key, sc, n); S.cnode = null; S.tags.emit([]); markDirty(); renderInspector(); renderCenter();
  }
  function coderInspector() {
    const tg = coderTarget(), box = h("div", { class: "insp" }); if (!tg || tg.splash) { box.append(h("div", { class: "note", text: "Select an element or a player object to edit its script." })); return box; }
    if (tg.dlg) { const d = tg.dlg, uses = dlgUses(d);
      box.append(h("div", { class: "ihead" }, h("span", { class: "bigic", text: "💬" }), h("div", { class: "ititle" }, h("input", { class: "idin", value: d.name, "aria-label": "Dialogue name", onchange: e => { d.name = e.target.value.trim() || d.name; markDirty(); renderCenter(); renderInspector(); } }), h("div", { class: "isub", text: d.id + " · " + d.nodes.length + " nodes" }))),
        h("div", { class: "note", text: uses.length ? "Used by: " + uses.join(", ") : "Not used yet: pick it in an NPC (Object Editor > NPC > Dialogue), a trigger zone, or a “Start a dialogue” node." }),
        h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "Duplicate", onclick: () => { const c = Schema.addDialogue(S.project, d.name + " copy"); for (const n of c.nodes.slice()) Schema.removeSNode(S.project, c.id, c, n); c.links = []; const map = {}; for (const n of d.nodes) { const nn = Schema.addSNode(S.project, c.id, c, n.type, n.x, n.y); map[n.id] = nn.id; for (const pr of Schema.SNODES[n.type].props.concat(Schema.SCOND)) S.project.tags[Schema.snodeKey(c.id, nn.id, pr.k)].value = S.tags.get(Schema.snodeKey(d.id, n.id, pr.k)); } c.links = d.links.map(l => ({ a: map[l.a], o: l.o, b: map[l.b] })); S.dlgSel = c.id; markDirty(); S.tags.emit([]); renderCenter(); renderInspector(); } }),
          h("button", { class: "btn sm danger", text: "Delete dialogue", onclick: () => { if (uses.length && !confirm("“" + d.name + "” is used by " + uses.join(", ") + ". Delete it anyway?")) return; Schema.removeDialogue(S.project, d); S.dlgSel = null; S.cnode = null; markDirty(); S.tags.emit([]); renderCenter(); renderInspector(); } })));
    } else box.append(h("div", { class: "ihead" }, h("span", { class: "bigic", text: "⬡" }), h("div", { class: "ititle" }, h("div", { class: "idin ro", text: tg.name }), h("div", { class: "isub", text: "Script of the " + tg.what + " · " + tg.script.nodes.length + " nodes" }))));
    if (tg.zone) box.append(h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "← Back to the map", onclick: () => { S.tab = "edit"; renderToolbar(); renderHier(); renderCenter(); renderInspector(); } })));
    const n = tg.script.nodes.find(x => x.id === S.cnode);
    if (n) { const def = Schema.SNODES[n.type]; Schema.ensureSNode(S.project, tg.key, n, tg.key + ".asset"); box.append(foldout("nc:node", def.label, ...def.props.map(pr => field(Schema.snodeKey(tg.key, n.id, pr.k))), h("div", { class: "btnrow" }, h("button", { class: "btn sm danger", text: "Delete node", onclick: () => coderDelete() }))));
      box.append(foldout("nc:cond", "Only if… (condition)", ...Schema.SCOND.map(pr => field(Schema.snodeKey(tg.key, n.id, pr.k))), h("div", { class: "note", text: def.ins === false ? "Leave the tag empty and this event always happens. With a tag, it only happens while the tag matches, e.g. canChargeWeapon is equal to yes. Make your own tags in the Tags tab." : "Leave the tag empty and this node always runs. With a tag, the chain stops here unless the tag matches." })));
      if (!def.props.length) box.append(h("div", { class: "note", text: def.cat === "event" ? "This starts a chain. Connect the next node to its dot." : "Nothing to set here. Connect nodes to its dots." })); }
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
  // the length the timeline shows: the animation's own length unless it was changed (project.animLen)
  function tlLen(q) { if (!q.anim) return { len: 1, speed: 1, orig: 1 }; const r = Schema.animLen(S.project, S.tags, q.asset.id, q.name, q.anim.dur); return { len: Math.max(0.05, r.len), speed: r.speed, orig: q.anim.dur, entry: r.entry }; }
  function setAnimLength(q, len) {
    let e = (S.project.animLen = S.project.animLen || []).find(x => x.asset === q.asset.id && x.anim === q.name);
    if (!e) { const used = reserved(); let n = 1; while (used.includes("al" + n)) n++; e = { id: "al" + n, asset: q.asset.id, anim: q.name }; S.project.animLen.push(e); Schema.defineAnimLen(S.project, e, { length: 0 }); }
    S.tags.set(e.id + ".length", len <= 0 ? 0 : +Math.max(0.05, Math.min(600, len)).toFixed(2));
  }
  // sound waveforms for the sound bars (decoded once per sound)
  S.wave = S.wave || {};
  function waveOf(id) {
    if (S.wave[id]) return S.wave[id]; const w = S.wave[id] = { dur: 0, peaks: null, busy: true }; const u = S.urls[id]; if (!u) return w;
    fetch(u).then(r => r.arrayBuffer()).then(b => { const C = window.AudioContext || window.webkitAudioContext; if (!C) throw 0; const ctx = new C(); return ctx.decodeAudioData(b).finally(() => ctx.close && ctx.close()); }).then(buf => {
      const d = buf.getChannelData(0), N = 240, pk = new Float32Array(N), step = Math.max(1, Math.floor(d.length / N)); for (let i = 0; i < N; i++) { let m = 0; for (let j = i * step; j < Math.min(d.length, (i + 1) * step); j += Math.max(1, step >> 4)) m = Math.max(m, Math.abs(d[j])); pk[i] = m; }
      w.dur = buf.duration; w.peaks = pk; w.busy = false; if (S.tlGrid) tlGrid();
    }).catch(() => { w.busy = false; w.dur = 0.3; }); return w;
  }
  const tlEvents = () => { const q = tlSelection(); return q.asset && q.name ? S.project.events.filter(e => e.asset === q.asset.id && e.anim === q.name) : []; };
  function addSoundEvent(snd) {
    const q = tlSelection(); if (!q.anim) return; const ev = { id: (() => { const used = reserved(); let n = 1; while (used.includes("ev" + n)) n++; return "ev" + n; })(), asset: q.asset.id, anim: q.name };
    S.project.events.push(ev); Schema.defineEvent(S.project, ev, { time: +Math.min(q.TL.t, Math.max(0, tlLen(q).len - 0.01)).toFixed(2), sound: snd.id }); q.TL.evSel = ev.id; S.tags.emit([]); markDirty(); renderInspector(); tlGrid(); log("Added sound “" + snd.name + "” to " + q.name);
  }
  function tlGrid() {
    const g = S.tlGrid; if (!g) return; const q = tlSelection(), { TL, anim, mo } = q, L = tlLen(q), LW = 130, W = Math.max(200, g.clientWidth - LW - 20), dur = anim ? L.len : 1, view = Math.max(dur, L.orig || 1) * 1.12, pps = W / view, X = t => LW + t * pps, evs = tlEvents();
    const tick = view > 8 ? 1 : view > 3 ? 0.5 : view > 1 ? 0.25 : 0.1, ruler = h("div", { class: "tlrow tlrul" }, h("div", { class: "tllab", text: "seconds" }));
    for (let t = 0; t <= view + 1e-6; t += tick) ruler.append(h("div", { class: "tltick", style: "left:" + X(t) + "px", text: +t.toFixed(2) }));
    // the end of the animation: drag the orange handle to make it shorter or longer
    const endShade = h("div", { class: "tlend", style: "left:" + X(dur) + "px;right:0" });
    const endH = h("div", { class: "tlendh", title: "Drag to change the length of this animation", style: "left:" + X(dur) + "px", text: "⇹" });
    if (anim) endH.addEventListener("pointerdown", ev => {
      ev.stopPropagation(); ev.preventDefault(); endH.setPointerCapture(ev.pointerId); const x0 = ev.clientX, l0 = dur;
      endH.onpointermove = mv => { const l = Math.max(0.05, Math.min(600, l0 + (mv.clientX - x0) / pps)); endH.style.left = X(l) + "px"; endShade.style.left = X(l) + "px"; endH.textContent = l.toFixed(2) + " s"; endH.__l = l; };
      endH.onpointerup = () => { endH.onpointermove = endH.onpointerup = null; if (endH.__l) setAnimLength(q, Math.abs(endH.__l - L.orig) < 0.005 ? 0 : endH.__l); S.tags.emit([]); markDirty(); renderInspector(); renderCenter(); };
    });
    // sounds as bars: the bar is as long as the sound itself, with its waveform; stacked in lanes when they overlap
    const lanes = [], bars = [], items = evs.map(e => { const sid = S.tags.get(e.id + ".sound"), a = S.project.assets.find(x => x.id === sid), w = sid ? waveOf(sid) : { dur: 0 }, t = +S.tags.get(e.id + ".time") || 0; return { e, a, w, t, d: Math.max(0.08, w.dur || 0.3) }; }).sort((x, y) => x.t - y.t);
    for (const it of items) { let l = lanes.findIndex(end => end <= it.t + 1e-6); if (l < 0) { l = lanes.length; lanes.push(0); } lanes[l] = it.t + it.d; it.lane = l; }
    const LH = 32, sounds = h("div", { class: "tlrow tlsnd", style: "height:" + Math.max(1, lanes.length) * LH + "px" }, h("div", { class: "tllab", style: "line-height:26px", text: "♪ Sounds" }));
    for (const it of items) {
      const { e, a, w } = it, vol = Math.max(0, Math.min(1, +S.tags.get(e.id + ".volume")));
      const bar = h("div", { class: "tlbar" + (TL.evSel === e.id ? " sel" : ""), title: (a ? a.name : "no sound") + " @ " + it.t.toFixed(2) + " s · volume " + Math.round(vol * 100) + "%  (drag = move, Shift+drag or mouse wheel = volume)", style: "left:" + X(it.t) + "px;top:" + (it.lane * LH + 3) + "px;width:" + Math.max(18, it.d * pps) + "px" });
      const cv = h("canvas", { class: "tlwave" }); cv.width = Math.max(18, Math.round(it.d * pps)); cv.height = 24; bar.append(cv, h("span", { class: "tlbn", text: "♪ " + (a ? a.name : "no sound") }), h("span", { class: "tlbv", text: Math.round(vol * 100) + "%" }));
      const cx = cv.getContext("2d"); cx.fillStyle = "rgba(255,255,255," + (0.25 + 0.6 * vol) + ")"; if (w.peaks) { const n = w.peaks.length; for (let x = 0; x < cv.width; x++) { const p = w.peaks[Math.min(n - 1, Math.floor(x / cv.width * n))] * vol * 11; cx.fillRect(x, 12 - p, 1, Math.max(1, p * 2)); } } else cx.fillRect(0, 11, cv.width, 2);
      bar.addEventListener("pointerdown", ev => {
        ev.stopPropagation(); ev.preventDefault(); TL.evSel = e.id; bar.setPointerCapture(ev.pointerId); const x0 = ev.clientX, y0 = ev.clientY, t0 = it.t, v0 = vol, volMode = ev.shiftKey; let moved = false;
        bar.onpointermove = mv => { moved = true;
          if (volMode) { const v = Math.max(0, Math.min(1, v0 + (y0 - mv.clientY) / 80)); S.tags.set(e.id + ".volume", +v.toFixed(2)); bar.querySelector(".tlbv").textContent = Math.round(v * 100) + "%"; }
          else { const t = Math.max(0, Math.min(dur - 0.01, t0 + (mv.clientX - x0) / pps)); S.tags.set(e.id + ".time", +t.toFixed(2)); bar.style.left = X(t) + "px"; } };
        bar.onpointerup = () => { bar.onpointermove = bar.onpointerup = null; markDirty(); renderInspector(); tlGrid(); if (!moved && a && S.urls[a.id]) playSound(a.id); };
      });
      bar.addEventListener("wheel", ev => { ev.preventDefault(); const v = Math.max(0, Math.min(1, (+S.tags.get(e.id + ".volume") || 0) + (ev.deltaY < 0 ? 0.05 : -0.05))); S.tags.set(e.id + ".volume", +v.toFixed(2)); markDirty(); renderInspector(); tlGrid(); }, { passive: false });
      sounds.append(bar);
    }
    const rows = [], order = []; if (anim && mo) for (const tr of anim.tracks) { let r = order.find(o => o.n === tr.n); if (!r) order.push(r = { n: tr.n, ts: new Set() }); for (const c of tr.c) if (c) for (const t of c.t) r.ts.add(t); }
    for (const r of order) { const row = h("div", { class: "tlrow" }, h("div", { class: "tllab", title: mo.nodes[r.n].name, text: mo.nodes[r.n].name })); for (const t of r.ts) row.append(h("div", { class: "tlkey", style: "left:" + X(t / L.speed) + "px" })); rows.push(row); }
    const head = h("div", { class: "tlhead" }, ruler, sounds), body = h("div", { class: "tlbody" }, ...rows);
    if (!anim) body.append(h("div", { class: "hint", style: "padding:10px", text: mo ? "This model has no animations." : "Loading the model…" }));
    const ph = h("div", { class: "tlplayhead" }); S.tlPlayhead = ph; ph.style.left = X(TL.t) + "px"; S.tlX = X;
    const scrub = ev => { const r = g.getBoundingClientRect(); TL.t = Math.max(0, Math.min(dur, (ev.clientX - r.left - LW) / pps)); TL.playing = false; S.tlTick && S.tlTick(0); };
    for (const el of [ruler, sounds, body]) el.addEventListener("pointerdown", ev => { if (ev.target.closest(".tlbar,.tlendh")) return; scrub(ev); el.setPointerCapture(ev.pointerId); el.onpointermove = scrub; el.onpointerup = () => { el.onpointermove = el.onpointerup = null; }; });
    put(g, head, body, endShade, ph); if (anim) g.append(endH);
  }
  function timelineCenter() {
    const p = E.center; p.tabs.replaceChildren(...centerTabs(), h("span", { class: "sp" })); const q = tlSelection(), { TL, models } = q, F = PRIM.FBX;
    const list = h("div", { class: "tllist" });
    let c = 0; for (const a of models) { list.append(h("div", { class: "tlm", text: "▪ " + a.name })); const mo = F.cache[a.id]; if (!mo) list.append(h("div", { class: "hint", style: "padding:2px 14px", text: a.meta && a.meta.error ? "Could not read this file" : "Loading…" }));
      else for (const n of Object.keys(mo.anims)) list.append(h("div", { class: "tla" + (TL.asset === a.id && TL.anim === n ? " sel" : ""), onclick: () => { TL.asset = a.id; TL.anim = n; TL.t = 0; TL.evSel = null; TL.playing = false; renderInspector(); renderCenter(); } }, h("span", { text: n }), h("span", { class: "hint", text: Schema.animLen(S.project, S.tags, a.id, n, mo.anims[n].dur).len.toFixed(2) + " s" + (Schema.animLen(S.project, S.tags, a.id, n, mo.anims[n].dur).entry && Schema.animLen(S.project, S.tags, a.id, n, mo.anims[n].dur).len !== mo.anims[n].dur ? " ⇹" : "") + ((c = S.project.events.filter(e => e.asset === a.id && e.anim === n).length) ? "  ♪ " + c : "") }))); }
    if (!models.length) list.append(h("div", { class: "hint", style: "padding:10px", text: "Import an .fbx model (Assets > Import Files) and its animations appear here." }));
    const cv = h("canvas", { class: "tlcv" }), time = h("span", { class: "chip", text: "0.00 s" }), snds = S.project.assets.filter(a => a.type === "sound");
    const playBtn = h("button", { class: "tbtn sm", text: "▶ Play", onclick: () => { if (!q.anim) return; TL.playing = !TL.playing; if (TL.playing && TL.t >= tlLen(q).len - 0.01) TL.t = 0; playBtn.textContent = TL.playing ? "❚❚ Pause" : "▶ Play"; } });
    put(E.cbar, h("span", { class: "chip", text: "Timeline" }), playBtn, h("button", { class: "tbtn sm", text: "⏮", title: "Back to the start", onclick: () => { TL.t = 0; TL.playing = false; playBtn.textContent = "▶ Play"; if (S.tlTick) S.tlTick(0); } }), time,
      h("button", { class: "tbtn sm", disabled: !q.anim, text: "+ Add sound at playhead ▾", onclick: e => popMenu(e.currentTarget, snds.length ? snds.map(a => ({ t: a.name, fn: () => addSoundEvent(a) })) : [{ t: "Import a sound file first (Assets > Import Files)", disabled: true }]) }),
      h("span", { class: "hint", text: "Click or drag in the timeline to scrub · drag a ♪ to move it · click a ♪ to hear it" }));
    const grid = h("div", { class: "tlgrid" }); S.tlGrid = grid;
    E.host.className = "host maphost"; E.host.replaceChildren(h("div", { class: "tl" }, list, h("div", { class: "tlmain" }, h("div", { class: "tlprev" }, cv), grid)));
    let v = null, gm = null, raf = 0, last = performance.now(), ro = null, prevT = TL.t; const M4 = PRIM.M4;
    if (q.mo) { v = new PRIM.R3D.View(cv); if (v.ok) { gm = v.loadModel(q.mo); v.sky = [0.1, 0.12, 0.17]; v.fov = 0.8; } else v = null; }
    const tick = dt => {
      const qq = tlSelection(), an = qq.anim, LL = tlLen(qq); if (an && TL.playing) { const t0 = TL.t; TL.t += dt; if (TL.t >= LL.len) { TL.t = LL.len > 0 ? TL.t % LL.len : 0; } tlSound(t0, TL.t, LL.len); }
      time.textContent = TL.t.toFixed(2) + " / " + (an ? LL.len.toFixed(2) : "0.00") + " s"; if (S.tlPlayhead && S.tlX) S.tlPlayhead.style.left = S.tlX(TL.t) + "px";
      if (v && q.anim) { const b = q.mo.bounds, c = [0, 1, 2].map(i => (b.lo[i] + b.hi[i]) / 2), k = 22 / (b.size || 1);
        v.draw(); v.drawViewmodel([{ gm, pose: PRIM.FBX.pose(q.mo, q.name, TL.t * LL.speed), M: M4.mul(M4.translate(0, 0, -34), M4.mul(M4.rotX(-12), M4.mul(M4.rotY(TL.yaw), M4.mul(M4.scale(k, k, k), M4.translate(-c[0], -c[1], -c[2]))))) }], []); }
    };
    function tlSound(t0, t1, dur) {   // events passed since last frame (also across the loop point)
      for (const e of tlEvents()) { const t = +S.tags.get(e.id + ".time") || 0, hit = t1 >= t0 ? (t >= t0 && t < t1) : (t >= t0 || t < t1); const sid = S.tags.get(e.id + ".sound"); if (hit && sid && S.urls[sid]) PRIM.Audio.play(S.urls[sid], Math.max(0, Math.min(1, (+S.tags.get("audio.master") || 0) * (+S.tags.get("audio.sfx") || 0) * (+S.tags.get(e.id + ".volume") || 0))), false); }
    }
    S.tlTick = tick; const loop = t => { raf = requestAnimationFrame(loop); const dt = Math.min(0.05, (t - last) / 1000); last = t; tick(dt); playBtn.textContent = TL.playing ? "❚❚ Pause" : "▶ Play"; }; raf = requestAnimationFrame(loop);
    let drag = null; cv.addEventListener("pointerdown", e => { drag = e.clientX; cv.setPointerCapture(e.pointerId); }); cv.addEventListener("pointermove", e => { if (drag !== null) { TL.yaw += (e.clientX - drag) * 0.6; drag = e.clientX; } }); cv.addEventListener("pointerup", () => { drag = null; });
    ro = new ResizeObserver(() => tlGrid()); ro.observe(grid); tlGrid();
    S.tlv = { destroy() { cancelAnimationFrame(raf); if (ro) ro.disconnect(); if (v) v.destroy(); S.tlTick = null; S.tlGrid = null; S.tlPlayhead = null; } };
  }
  function timelineInspector() {
    const q = tlSelection(), box = h("div", { class: "insp" }), evs = tlEvents(), TL = q.TL;
    box.append(h("div", { class: "ihead" }, h("span", { class: "bigic", text: "♪" }), h("div", { class: "ititle" }, h("div", { class: "idin ro", text: q.name || "Timeline" }), h("div", { class: "isub", text: q.asset ? q.asset.name + (q.anim ? " · " + tlLen(q).len.toFixed(2) + " s · " + q.anim.tracks.length + " tracks" : "") : "No model yet" }))));
    if (q.anim) { const L = tlLen(q); box.append(foldout("tl:len", "Animation length", h("div", { class: "note", text: "Original length " + L.orig.toFixed(2) + " s. Make it shorter to play it faster, longer to play it slower. You can also drag the orange ⇹ handle at the end of the timeline." }),
      h("div", { class: "fld" }, h("div", { class: "flab", text: "Length (seconds)" }), h("input", { class: "num", type: "number", step: "0.05", min: "0.05", value: +L.len.toFixed(2), onchange: e => { const v = parseFloat(e.target.value); if (!(v > 0)) return; setAnimLength(q, Math.abs(v - L.orig) < 0.005 ? 0 : v); S.tags.emit([]); markDirty(); renderInspector(); renderCenter(); } })),
      h("div", { class: "note", text: "Speed: " + (L.speed * 100).toFixed(0) + "% of the original" }), h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "Back to original length", onclick: () => { setAnimLength(q, 0); S.tags.emit([]); markDirty(); renderInspector(); renderCenter(); } })))); }
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
    if (!S.med) return log("Switch to the Map tab to texture cells.", "warn"); const part = S.med.dropTexture(e, a.id); log(part[0].toUpperCase() + part.slice(1) + " texture set to " + a.name); renderInspector();
  }
  // ================================================================ OBJECT EDITOR (placeable objects: doors, pick-ups, decorations)
  function propBounds(pre) {   // size of the model after rotation and scale: { lo, hi } or null
    const T = S.tags, F = PRIM.FBX, M4 = PRIM.M4, mo = F.cache[T.get(pre + "asset")]; if (!mo || !mo.bounds) return null; const b = mo.bounds, R = M4.mul(M4.rotXYZ([+T.get(pre + "rx") || 0, +T.get(pre + "ry") || 0, +T.get(pre + "rz") || 0]), M4.scale(+T.get(pre + "scale") || 1, +T.get(pre + "scale") || 1, +T.get(pre + "scale") || 1)), lo = [1e9, 1e9, 1e9], hi = [-1e9, -1e9, -1e9];
    for (const x of [b.lo[0], b.hi[0]]) for (const y of [b.lo[1], b.hi[1]]) for (const z of [b.lo[2], b.hi[2]]) { const p = M4.point(R, x, y, z); for (let i = 0; i < 3; i++) { lo[i] = Math.min(lo[i], p[i]); hi[i] = Math.max(hi[i], p[i]); } }
    return { lo, hi };
  }
  function propCenterOnGrid(pre) { const b = propBounds(pre); if (!b) return; S.tags.setMany({ [pre + "ox"]: +(-(b.lo[0] + b.hi[0]) / 2 + (+S.tags.get(pre + "ox") || 0)).toFixed(1), [pre + "oy"]: +(-b.lo[1] + (+S.tags.get(pre + "oy") || 0)).toFixed(1), [pre + "oz"]: +(-(b.lo[2] + b.hi[2]) / 2 + (+S.tags.get(pre + "oz") || 0)).toFixed(1) }); }
  function propFitScale(pre) { const T = S.tags, mo = PRIM.FBX.cache[T.get(pre + "asset")]; if (!mo) return; T.set(pre + "scale", 1); const b = propBounds(pre); if (!b) return; const w = Math.max(b.hi[0] - b.lo[0], b.hi[2] - b.lo[2], 0.001); T.set(pre + "scale", +(128 / w).toFixed(4)); }
  // a flat stand-in model for a 2D sprite object in the editor view: its first frame, at its height, standing on the grid
  function spriteModel(pre) {
    const T = S.tags, id = T.get(pre + "spriteIdle"); if (!id || !S.urls[id]) return null; const d = (S.imgDims = S.imgDims || {}); if (!d[id]) { const im = new Image(); im.onload = () => { d[id] = [im.naturalWidth, im.naturalHeight]; if (S.tab === "props") renderCenter(); }; im.src = S.urls[id]; return null; }
    const fr = Math.max(1, +T.get(pre + "spriteIdleFrames") || 1), hh = +T.get(pre + "spriteHeight") || 110, w = hh * d[id][0] / fr / d[id][1] / 2, u1 = 1 / fr;
    return { nodes: [{ mesh: { p: [-w, 0, 0, w, 0, 0, w, hh, 0, -w, 0, 0, w, hh, 0, -w, hh, 0], n: [0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1], u: [0, 1, u1, 1, u1, 0, 0, 1, u1, 0, 0, 0] } }], anims: {} };
  }
  // the look of an object for the 3D view: base colour map + emission (map texture loaded on that view)
  function matGL(v, pre) { const m = Schema.matOf(S.tags, pre); if (m.emitTexId) m.emitTex = v.texFor(m.emitTexId, S.urls); return m; }
  function addProp(asset) {
    const P = S.project, mo = PRIM.FBX.cache[asset.id], names = mo ? Object.keys(mo.anims) : [], p = Schema.addProp(P, { name: asset.name.replace(/\.[^.]+$/, ""), asset: asset.id, idleAnim: names.find(n => /idle/i.test(n)) || "" }), pre = p.id + ".";
    propFitScale(pre); propCenterOnGrid(pre); S.propSel = p.id; S.tags.emit([]); markDirty(); renderCenter(); renderInspector(); log("Created object “" + S.tags.get(pre + "name") + "”");
  }
  function addSpriteProp() {
    const P = S.project, img = P.assets.find(a => a.type === "image"), p = Schema.addProp(P, { name: "Sprite " + (P.props.length + 1), look: "sprite", type: "enemy", spriteIdle: img ? img.id : "" });
    S.propSel = p.id; S.tags.emit([]); markDirty(); renderCenter(); renderInspector(); log("Created a 2D sprite object. Pick its pictures on the right (each picture = one animation, frames side by side).");
  }
  function propCenter() {
    const p = E.center, P = S.project, props = P.props = P.props || [], F = PRIM.FBX, M4 = PRIM.M4, T = S.tags;
    p.tabs.replaceChildren(...centerTabs(), h("span", { class: "sp" }), h("span", { class: "csize", text: "16 × 16 grid = one 2 m cell" })); E.host.className = "host maphost";
    const pr = props.find(x => x.id === S.propSel) || props[0]; S.propSel = pr ? pr.id : null; const models = P.assets.filter(a => a.type === "model");
    const newBtn = h("button", { class: "btn sm", text: "+ New object ▾", onclick: e => popMenu(e.currentTarget, [...(models.length ? models.map(a => ({ t: "3D: " + a.name, fn: () => addProp(a) })) : [{ t: "3D model: import an .fbx file first (Assets > Import Files)", disabled: true }]), "-", { t: "2D sprite object (pictures)", fn: () => addSpriteProp() }]) });
    if (!pr) { put(E.cbar, newBtn, h("span", { class: "hint", text: "Objects are doors, pick-ups and decorations you place in maps." })); E.host.replaceChildren(h("div", { class: "v3err", style: "color:var(--muted);text-align:center;padding:20px", text: "No objects yet. Click “+ New object” and pick an FBX model." })); return; }
    const pre = pr.id + ".", spr = T.get(pre + "type") !== "door" && T.get(pre + "look") === "sprite", aid = spr ? "" : T.get(pre + "asset"), mo = spr ? spriteModel(pre) : F.cache[aid], OE = S.pe = S.pe || { yaw: -30, pitch: 22, zoom: 1 };
    const tb = (l, fn, tip) => h("button", { class: "tbtn sm", text: l, title: tip || "", onclick: fn });
    put(E.cbar, h("select", { class: "sel", onchange: e => { S.propSel = e.target.value; renderCenter(); renderInspector(); } }, ...props.map(x => h("option", { value: x.id, text: T.get(x.id + ".name") || x.id, selected: x.id === pr.id }))), newBtn,
      tb("Scale to grid", () => { propFitScale(pre); propCenterOnGrid(pre); markDirty(); renderInspector(); }, "Scales the model so it fills the 3 x 3 tile grid"), tb("Center on grid", () => { propCenterOnGrid(pre); markDirty(); renderInspector(); }, "Puts the middle of the model on the grid, standing on the ground"),
      tb("Front", () => { OE.yaw = 0; OE.pitch = 0; }), tb("Side", () => { OE.yaw = 90; OE.pitch = 0; }), tb("Top", () => { OE.yaw = 0; OE.pitch = 89; }), tb("3D", () => { OE.yaw = -30; OE.pitch = 22; }),
      h("span", { class: "hint", text: "The grid is one map cell (2 m): 16 × 16 squares of 12.5 cm, the bright edge is the cell's border, the arrow points to the front (+Z). Drag to orbit, scroll to zoom." + (T.get(pre + "type") === "door" ? " Yellow = the doorway cut into the wall, blue = the wall's thickness." : T.get(pre + "type") === "enemy" ? " Blue cylinder = the enemy's body, red arrow = its front (+Z)." : "") }));
    const cv = h("canvas", { class: "v3cv", style: "cursor:grab" }); E.host.replaceChildren(cv);
    if (!mo) { E.host.append(h("div", { class: "v3err", style: "color:var(--muted)", text: spr ? "Choose its standing picture on the right." : aid ? "Loading the model…" : "Choose a model for this object on the right." })); if (aid) F.load(aid, S.urls[aid]).then(m => { if (m && S.tab === "props") renderCenter(); }); return; }
    const v = new PRIM.R3D.View(cv); if (!v.ok) return; v.sky = [0.1, 0.12, 0.17]; v.fov = 0.8; const gm = v.loadModel(mo), D = 34, g = k => +T.get(pre + k) || 0;
    const segs = V => { const o = [], L = (a, b, c) => o.push({ a: M4.point(V, ...a), b: M4.point(V, ...b), c });
      // Blockbench-style floor grid: one cell (128 units) in 16 x 16 squares of 8 units, a brighter edge, the middle lines, the neighbouring cells faint, an arrow to the front (+Z)
      const LW = (a, b, c, w) => o.push({ a: M4.point(V, ...a), b: M4.point(V, ...b), c, w });
      for (let i = -8; i <= 8; i++) { const e = Math.abs(i) === 8, c = e ? [0.2, 0.75, 0.85] : i === 0 ? [0.45, 0.53, 0.63] : i % 4 === 0 ? [0.33, 0.39, 0.48] : [0.25, 0.29, 0.35], w = e ? 0.16 : i % 4 === 0 ? 0.09 : 0.05; LW([i * 8, 0, -64], [i * 8, 0, 64], c, w); LW([-64, 0, i * 8], [64, 0, i * 8], c, w); }
      for (const q of [-192, 192]) { const c = [0.17, 0.2, 0.25]; LW([q, 0, -192], [q, 0, 192], c, 0.06); LW([-192, 0, q], [192, 0, q], c, 0.06); } for (const q of [-64, 64]) { const c = [0.17, 0.2, 0.25]; LW([q, 0, -192], [q, 0, -64], c, 0.06); LW([q, 0, 64], [q, 0, 192], c, 0.06); LW([-192, 0, q], [-64, 0, q], c, 0.06); LW([64, 0, q], [192, 0, q], c, 0.06); }
      L([0, 0.5, 64], [0, 0.5, 84], [1, 0.4, 0.3]); L([0, 0.5, 84], [-6, 0.5, 76], [1, 0.4, 0.3]); L([0, 0.5, 84], [6, 0.5, 76], [1, 0.4, 0.3]);
      if (T.get(pre + "type") === "door") { const w = 64 * (+T.get(pre + "doorCellsW") || 1), hh = 128 * (+T.get(pre + "doorCellsH") || 1), y = [1, 0.85, 0.25], bl = [0.35, 0.55, 1], th = 8;
        L([-w, 0, 0], [w, 0, 0], y); L([w, 0, 0], [w, hh, 0], y); L([w, hh, 0], [-w, hh, 0], y); L([-w, hh, 0], [-w, 0, 0], y);
        for (const z of [-th, th]) { L([-w - 8, 0, z], [w + 8, 0, z], bl); L([-w - 8, 0, z], [-w - 8, hh, z], bl); L([w + 8, 0, z], [w + 8, hh, z], bl); } }
      if (T.get(pre + "type") === "interactive") { const pr2 = (S.project.props || []).find(x => x.id + "." === pre); for (const b of (pr2 && pr2.hitboxes) || []) { const q = kk => +T.get(pre + "hb." + b.id + "." + kk) || 0, c = [q("x"), q("y"), q("z")], hh = [q("w") / 2, q("h") / 2, q("d") / 2], col = b.id === S.hbSel ? [1, 0.85, 0.2] : [1, 0.3, 0.9], P = i => [c[0] + (i & 1 ? 1 : -1) * hh[0], c[1] + (i & 2 ? 1 : -1) * hh[1], c[2] + (i & 4 ? 1 : -1) * hh[2]];
        for (const [i, j] of [[0, 1], [2, 3], [4, 5], [6, 7], [0, 2], [1, 3], [4, 6], [5, 7], [0, 4], [1, 5], [2, 6], [3, 7]]) LW(P(i), P(j), col, b.id === S.hbSel ? 0.22 : 0.12); } }
      if (T.get(pre + "type") === "enemy") { const r = +T.get(pre + "radius") || 22, hh = +T.get(pre + "height") || 110, bl = [0.35, 0.65, 1], N = 20; for (let i = 0; i < N; i++) { const a0 = i / N * Math.PI * 2, a1 = (i + 1) / N * Math.PI * 2, p = (a, y) => [Math.cos(a) * r, y, Math.sin(a) * r]; L(p(a0, 0), p(a1, 0), bl); L(p(a0, hh), p(a1, hh), bl); if (i % 5 === 0) L(p(a0, 0), p(a0, hh), bl); } L([0, 2, 0], [0, 2, r + 30], [1, 0.4, 0.3]); L([0, 2, r + 30], [-6, 2, r + 22], [1, 0.4, 0.3]); L([0, 2, r + 30], [6, 2, r + 22], [1, 0.4, 0.3]); }
      return o; };
    let raf = 0; const loop = () => { raf = requestAnimationFrame(loop); const k = 0.11 * OE.zoom, V = M4.mul(M4.translate(0, 0, -D), M4.mul(M4.rotX(OE.pitch), M4.mul(M4.rotY(OE.yaw), M4.mul(M4.scale(k, k, k), M4.translate(0, -50, 0))))), Mp = M4.mul(V, M4.mul(M4.translate(g("ox"), g("oy"), g("oz")), M4.mul(M4.rotXYZ([g("rx"), g("ry"), g("rz")]), M4.scale(g("scale") || 1, g("scale") || 1, g("scale") || 1)))), Ms = M4.mul(V, M4.rotY(-OE.yaw));
      if (spr) { if (!gm.tex || gm.tex === v.whiteTex || gm.tex === v.defTex) gm.tex = v.texFor(T.get(pre + "spriteIdle"), S.urls); v.setLights([]); v.draw(); v.drawViewmodel([{ gm, pose: [M4.ident()], M: Ms, mat: { lit: 0 } }], segs(V)); return; }
      const mt = matGL(v, pre); v.setLights([{ p: [60, 140, 120], r: 900, c: [1.1, 1.05, 1], type: 0 }]); v.draw(); v.drawViewmodel([{ gm, pose: F.pose(mo, T.get(pre + "idleAnim") || null, 0), M: Mp, mat: mt, tex: mt.texId ? v.texFor(mt.texId, S.urls) : null }], segs(V)); }; raf = requestAnimationFrame(loop);
    let drag = null; cv.addEventListener("pointerdown", e => { cv.setPointerCapture(e.pointerId); drag = { x: e.clientX, y: e.clientY }; }); cv.addEventListener("pointermove", e => { if (!drag) return; OE.yaw += (e.clientX - drag.x) * 0.6; OE.pitch = Math.max(-89, Math.min(89, OE.pitch + (e.clientY - drag.y) * 0.6)); drag = { x: e.clientX, y: e.clientY }; });
    cv.addEventListener("pointerup", () => { drag = null; }); cv.addEventListener("wheel", e => { e.preventDefault(); OE.zoom = Math.max(0.3, Math.min(4, OE.zoom * (e.deltaY > 0 ? 0.9 : 1.1))); }, { passive: false });
    S.pev = { destroy() { cancelAnimationFrame(raf); v.destroy(); } };
  }
  // the dialogue of a character: lines in order, each with up to 4 choices (a choice can jump to any line or end the talk, and change a tag)
  function dialogueBox(pr) {
    const P = S.project, T = S.tags, lines = (pr.dialog = pr.dialog || { lines: [] }).lines, redo = () => { markDirty(); renderInspector(); };
    Schema.syncDOpts(P, pr); const rows = [];
    lines.forEach((l, i) => {
      const pre = pr.id + ".d." + l.id + ".", f = k => field(pre + k), shownC = [1, 2, 3, 4].filter(n => String(T.get(pre + "c" + n + "text") || "").trim()), nextC = [1, 2, 3, 4].find(n => !shownC.includes(n));
      const ta = h("textarea", { class: "dlgtext", rows: 3, "aria-label": "Line " + (i + 1) + " text", onchange: e => { T.set(pre + "text", e.target.value); markDirty(); } }); ta.value = T.get(pre + "text") || "";
      rows.push(h("div", { class: "dlgline" }, h("div", { class: "dlghead" }, h("b", { text: "Line " + (i + 1) }), h("span", { class: "tn", text: l.id }), h("span", { class: "sp" }),
          i > 0 ? h("button", { class: "btn sm", title: "Move up", text: "↑", onclick: () => { lines.splice(i, 1); lines.splice(i - 1, 0, l); Schema.syncDOpts(P, pr); redo(); } }) : "",
          h("button", { class: "btn sm danger", title: "Delete this line", text: "✕", onclick: () => { Schema.removeDLine(P, pr, l); redo(); } })),
        f("speaker"), ta, f("sound"), shownC.length ? "" : f("next"),
        ...shownC.map(n => h("div", { class: "dlgchoice" }, f("c" + n + "text"), f("c" + n + "go"), f("c" + n + "tag"), T.get(pre + "c" + n + "tag") ? f("c" + n + "val") : "",
          h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "Remove choice " + n, onclick: () => { T.set(pre + "c" + n + "text", ""); T.set(pre + "c" + n + "tag", ""); redo(); } })))),
        nextC ? h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "+ Add a choice", onclick: () => { T.set(pre + "c" + nextC + "text", "Choice " + nextC); redo(); } })) : ""));
    });
    return foldout("pr:dlg", "Dialogue (" + lines.length + " line" + (lines.length === 1 ? "" : "s") + ")", ...rows,
      h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "+ Add a line", onclick: () => { Schema.addDLine(P, pr, { text: lines.length ? "..." : "Hello there." }); redo(); } })),
      h("div", { class: "note", text: "The talk starts at line 1. Without choices, Continue goes to “Then go to”. With choices, each one jumps to a line (or ends the talk) and can change a tag, e.g. set quest.step to 2, or +1 to a counter. Use those tags in the HUD or the Node Coder." }));
  }
  // hitboxes of an interactive object: invisible boxes in the object's own space (the Object Editor grid), each with what pressing it does
  function hitboxBox(pr) {
    const P = S.project, T = S.tags, L = pr.hitboxes = pr.hitboxes || []; if (!L.length) { Schema.addHitbox(P, pr); markDirty(); S.tags.emit([]); }
    const cur = L.find(b => b.id === S.hbSel) || L[0]; S.hbSel = cur.id; const k = pr.id + ".hb." + cur.id + ".";
    const list = h("div", { class: "btnrow", style: "flex-wrap:wrap" }, ...L.map((b, i) => h("button", { class: "btn sm" + (b === cur ? " on" : ""), style: b === cur ? "outline:2px solid #ffb53a" : "", text: (i + 1) + ". " + (T.get(pr.id + ".hb." + b.id + ".name") || b.id), onclick: () => { S.hbSel = b.id; renderInspector(); } })),
      h("button", { class: "btn sm", text: "+ Hitbox", onclick: () => { const c = cur ? +T.get(k + "x") || 0 : 0, b = Schema.addHitbox(P, pr, { name: "Button " + (L.length + 1), x: c + 32 }); S.hbSel = b.id; markDirty(); S.tags.emit([]); renderInspector(); } }));
    const f = q => field(k + q);
    return foldout("pr:hb", "Hitboxes (" + L.length + ")", list, h("div", { class: "note", text: "Yellow box in the view = the selected hitbox, pink = the others. Size and place them over the buttons of your model (X right, Y up, Z front; the grid is one 2 m cell, a square is 8 units)." }),
      ...["name", "x", "y", "z", "w", "h", "d"].map(f), h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "Fit to the model", onclick: () => { const b = propBounds(pr.id + "."); if (!b) return; const ox = +T.get(pr.id + ".ox") || 0, oy = +T.get(pr.id + ".oy") || 0, oz = +T.get(pr.id + ".oz") || 0; T.setMany({ [k + "x"]: Math.round((b.lo[0] + b.hi[0]) / 2 + ox), [k + "y"]: Math.round((b.lo[1] + b.hi[1]) / 2 + oy), [k + "z"]: Math.round((b.lo[2] + b.hi[2]) / 2 + oz), [k + "w"]: Math.round(b.hi[0] - b.lo[0]) + 2, [k + "h"]: Math.round(b.hi[1] - b.lo[1]) + 2, [k + "d"]: Math.round(b.hi[2] - b.lo[2]) + 2 }); markDirty(); renderInspector(); } }),
        h("button", { class: "btn sm danger", text: "Remove hitbox", disabled: L.length < 2, onclick: () => { Schema.removeHitbox(P, pr, cur); S.hbSel = null; markDirty(); S.tags.emit([]); renderInspector(); } })),
      h("div", { class: "fsub", text: "When pressed" }), ...["setTag", "setVal", "message", "sound", "anim", "cooldown", "once"].map(f),
      h("div", { class: "fsub", text: "Lock (puzzles)" }), ...["needTag", "needOp", "needVal", "lockedSound"].map(f),
      h("div", { class: "note", text: "Node Coder: “When a button is pressed” (any, or by name) and “When I receive a message” react to it. The live tag player.lastButton holds the name of the last pressed button." }));
  }
  function propInspector() {
    const P = S.project, pr = (P.props || []).find(x => x.id === S.propSel) || (P.props || [])[0], box = h("div", { class: "insp" }), T = S.tags;
    if (!pr) { box.append(h("div", { class: "note", text: "No object yet. Use “+ New object” above the view." })); return box; }
    const pre = pr.id + ".", ty = T.get(pre + "type"), spr = ty !== "door" && T.get(pre + "look") === "sprite", shown = Schema.PROP.filter(q => Schema.shown(T, pre, q) && !(spr && q.type === "anim")), grp = keys => shown.filter(q => keys.includes(q.k)).map(q => field(pre + q.k));
    box.append(h("div", { class: "ihead" }, h("span", { class: "bigic", text: ty === "door" ? "▯" : ty === "pickup" ? "◆" : ty === "enemy" ? "☠" : ty === "exit" ? "⇥" : ty === "speaker" ? "♪" : ty === "interactive" ? "◳" : ty === "checkpoint" ? "⚑" : "▣" }), h("div", { class: "ititle" }, h("input", { class: "idin", value: T.get(pre + "name") || pr.id, "aria-label": "Object name", onchange: e => { T.set(pre + "name", e.target.value.trim() || pr.id); renderCenter(); } }), h("div", { class: "isub", text: pr.id + " · " + ty }))),
      h("div", { class: "btnrow" }, h("button", { class: "btn sm danger", text: "Delete object", onclick: () => { Schema.removeProp(P, pr); S.propSel = null; S.tags.emit([]); markDirty(); renderCenter(); renderInspector(); } })),
      foldout("pr:type", "Type", ...grp(["type", "look", "asset"])),
      spr ? foldout("pr:spr", "Sprite pictures", ...grp(["spriteIdle", "spriteIdleFrames", "spriteWalk", "spriteWalkFrames", "spriteAttack", "spriteAttackFrames", "spriteHurt", "spriteHurtFrames", "spriteDeath", "spriteDeathFrames", "spriteFps", "spriteHeight"]),
        h("div", { class: "note", text: "Each picture is one animation: its frames side by side from left to right, all the same width. A single picture with 1 frame is a still sprite. Sprites always turn to face the player." })) : "",
      spr ? "" : foldout("pr:mat", "Material", ...grp(["matTexture", "matEmitMap", "matEmit"]), h("div", { class: "note", text: "A base color map replaces the model's own picture. The emission map says which parts glow (empty = the whole object glows); strength 0 = no glow." })), spr ? "" : foldout("pr:xf", "Model placement", ...grp(["scale", "rx", "ry", "rz", "ox", "oy", "oz", "idleAnim"]),
        h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "Scale to grid", onclick: () => { propFitScale(pre); propCenterOnGrid(pre); markDirty(); renderInspector(); } }), h("button", { class: "btn sm", text: "Center on grid", onclick: () => { propCenterOnGrid(pre); markDirty(); renderInspector(); } }))),
      ty === "door" ? foldout("pr:door", "Door", ...grp(["doorCellsW", "doorCellsH", "doorFit"]), h("div", { class: "note", text: "A door is a block: 1 × 1 fills one cell, 2 × 1 is two cells wide, 3 × 2 is three cells wide and two stories tall. With “fit” on, the model is stretched to fill it exactly." }), ...grp(["openMode", "openRange", "interactRange", "openAnim", "closeAnim", "autoClose", "closeAfter", "openSound", "closeSound"]),
        h("div", { class: "note", text: "Place doors in the Map view with the Object tool, in a cell between two walls: a door fills its cell (2 m wide). The model's width runs across the doorway, its front faces +Z." })) : "",
      ty === "pickup" ? foldout("pr:pick", "Pick-up", ...grp(["giveTag", "giveAmount", "pickRadius", "pickSound"]), h("div", { class: "note", text: "Energy tank (Lives system “Energy reserve”): gives to player.tanksMax, amount 1. It adds a tank and refills everything. Health pick-ups (player.health) fill empty tanks once your health is full." }),
        h("div", { class: "fsub", text: "Pop-up" }), ...grp(["popupOn", "popupTitle", "popupText", "popupImage", "popupFirst", "popupSound"]), h("div", { class: "note", text: "Tip: to make an ammo box, pick a weapon's “ammo” tag above (player.<weapon>.reserve or .ammo); for a health pack pick player.health." })) : "",
      ty === "enemy" ? foldout("pr:enemy", "Enemy", ...grp(["health", "speed", "radius", "height", "sight", "attack", "attackRange", "damage", "attackRate", "attackDelay", "projSpeed", "projSize", "projColor"])) : "",
      ty === "enemy" ? foldout("pr:enanim", "Enemy animations and sounds", ...grp(["walkAnim", "attackAnim", "hurtAnim", "deathAnim", "corpse", "sightSound", "attackSound", "hurtSound", "deathSound", "killTag", "killAmount"]),
        h("div", { class: "note", text: "The model's front must face +Z (toward you in the Front view). The blue cylinder shows its body (what bullets hit and what blocks the player). Place enemies in the Map view with the Place Object tool; Turn sets which way they face." })) : "",
      ty !== "door" && ty !== "speaker" ? foldout("pr:light", "Light", ...grp(["lightOn", "lightColor", "lightIntensity", "lightRange", "lightHeight", "lightFlicker", "lightShadows"]), h("div", { class: "note", text: "A lamp, a torch, a glowing crystal: the object lights up the room around it, and with shadows on, walls, objects and enemies cast real shadows from it." })) : "",
      ty === "npc" ? foldout("pr:npc", "Character", ...grp(["npcName", "talkRange", "radius", "talkAnim", "talkSprite", "talkSpriteFrames", "faceTalker"]), h("div", { class: "note", text: "Walk up to it, look at it and press the Interact key to talk. It blocks the way like a body (its width above)." })) : "",
      ty === "npc" ? foldout("pr:npcdlg", "Dialogue (node graph)", ...grp(["npcDialogue"]), h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: T.get(pre + "npcDialogue") ? "Edit in the Dialogue Editor" : "+ New dialogue for it", onclick: () => { let id = T.get(pre + "npcDialogue"); if (!(P.dialogues || []).some(d => d.id === id)) { const d = Schema.addDialogue(P, (T.get(pre + "name") || pr.id) + " talk"); id = d.id; T.set(pre + "npcDialogue", id); } S.dlgSel = id; S.cnode = null; markDirty(); setTab("dlg"); } })),
        h("div", { class: "note", text: "A dialogue from the Dialogue Editor (lines, questions, answers that go different ways, tags). When one is picked, the simple line list below is not used." })) : "",
      ty === "npc" && !T.get(pre + "npcDialogue") ? dialogueBox(pr) : "",
      ty === "checkpoint" ? foldout("pr:cp", "Checkpoint", ...grp(["cpRadius", "cpHeal", "cpMessage", "cpSound"]), h("div", { class: "note", text: "Touch it to make it your respawn point. With the Lives system “Classic”, dying costs a life and puts you back at the last checkpoint you touched (or the map start). A model is optional: without one it is invisible in the game." })) : "",
      ty === "interactive" ? foldout("pr:use", "Interaction", ...grp(["useMode", "useRange", "useHint", "showBoxes"]), h("div", { class: "note", text: T.get(pre + "useMode") === "cursor" ? "Screen mode: walk up to it and look at it; the mouse then moves a cursor over it instead of turning. Click a hitbox to press it. Move the cursor off the object to look around again." : T.get(pre + "useMode") === "aim" ? "Put the crosshair on a hitbox and left-click (the click does not fire your weapon)." : "Look at a hitbox and press the Interact key (E by default)." })) : "",
      ty === "interactive" ? hitboxBox(pr) : "",
      ty === "speaker" ? foldout("pr:spk", "Speaker", ...grp(["spkSound", "spkVolume", "spkRadius", "spkHeight", "spkLoop", "spkShow"]), h("div", { class: "note", text: "Place it in the Map view. You hear it from its direction (left, right, behind), loudest close by and silent outside its radius (the dashed circle on the map). A model is optional: by default it is invisible in the game." })) : "",
      ty === "exit" ? foldout("pr:exit", "Level exit", ...grp(["exitTo", "exitMap", "exitScreen", "exitRadius", "exitSound"]), h("div", { class: "note", text: "Touching it ends the level. Leave “Map” empty to go to the next map in the list. Any model works (a teleporter, a door frame…)." })) : "");
    return box;
  }
  function mapInspector() {
    const m = curMap(), box = h("div", { class: "insp" }), sel = S.msel, pre = m.id + ".", G = PRIM.Grid, med = S.med, imgs = S.project.assets.filter(a => a.type === "image");
    const head = (ic, title, sub) => h("div", { class: "ihead" }, h("span", { class: "bigic", text: ic }), h("div", { class: "ititle" }, title, h("div", { class: "isub", text: sub })));
    const row = (label, ctl) => h("div", { class: "fld" }, h("label", { class: "flab" }, h("span", { text: label })), h("div", { class: "fctl" }, ctl));
    const texSel = (label, val, fn) => row(label, h("select", { class: "sel", onchange: e => fn(e.target.value) }, val === null ? h("option", { value: "__m", text: "(different)", selected: true }) : "", h("option", { value: "", text: "Default", selected: val === "" }), ...imgs.map(a => h("option", { value: a.id, text: a.name, selected: val === a.id }))));
    const choice = (label, opts, val, fn) => row(label, h("select", { class: "sel", onchange: e => fn(e.target.value) }, val === null ? h("option", { value: "__m", text: "(different)", selected: true }) : "", ...opts.map(([v, l]) => h("option", { value: v, text: l, selected: String(val) === String(v) }))));
    const check = (label, val, fn) => row(label, h("input", Object.assign({ type: "checkbox", onchange: e => fn(e.target.checked) }, val ? { checked: "" } : {})));
    if (sel && sel.kind === "cells" && med) {
      const L = med.s.L, list = med.selCells(), cells = list.map(([x, y]) => G.cell(m, L, x, y) || {}), same = f => { const v = new Set(cells.map(f)); return v.size === 1 ? [...v][0] : null; };
      const apply = fn => { med.applyCells(fn); renderInspector(); }, walls = cells.filter(c => c.w !== undefined), floors = cells.filter(c => c.w === undefined && c.f !== undefined);
      box.append(head("▦", h("div", { class: "idin ro", text: list.length + " cell" + (list.length > 1 ? "s" : "") + " selected" }), "story " + (L + 1) + " · " + walls.length + " wall · " + floors.length + " floor"),
        h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "Copy (Ctrl+C)", onclick: () => med.copy() }), h("button", { class: "btn sm", text: "Duplicate (Ctrl+D)", onclick: () => med.duplicate() }), h("button", { class: "btn sm danger", text: "Clear cells", onclick: () => { med.deleteSelection(); renderInspector(); } })),
        foldout("gc:what", "What is in the cells",
          check("Wall block", same(c => c.w !== undefined), on => apply(c => { if (on) { c.w = c.w !== undefined ? c.w : (c.f || ""); } else { delete c.w; delete c.ws; } })),
          check("Floor", same(c => c.f !== undefined), on => apply(c => { if (on) { if (c.f === undefined) c.f = ""; } else { delete c.f; delete c.h; delete c.sh; delete c.sd; delete c.sr; } })),
          check("Ceiling", same(c => c.c !== undefined), on => apply(c => { if (on) { if (c.c === undefined) c.c = ""; } else delete c.c; }))),
        walls.length ? foldout("gc:wall", "Wall", texSel("Texture (all sides)", same(c => c.w === undefined ? undefined : c.w), v => apply(c => { if (c.w !== undefined) { c.w = v; delete c.ws; } })),
          ...[0, 1, 2, 3].map(d => texSel(["North", "East", "South", "West"][d] + " side", same(c => c.w === undefined ? undefined : c.ws && c.ws[d] !== undefined ? c.ws[d] : c.w), v => apply(c => { if (c.w === undefined) return; c.ws = c.ws || {}; c.ws[d] = v; }))),
          h("div", { class: "note", text: "A side can have its own texture (e.g. a switch panel on one face). You can also click faces in the Fly Editor." })) : "",
        floors.length ? foldout("gc:floor", "Floor", texSel("Texture", same(c => c.f), v => apply(c => { if (c.f !== undefined && c.w === undefined) c.f = v; })),
          row("Height (16ths of a story; for a slope or stairs: where it starts)", h("input", { type: "range", min: 0, max: 15, step: 1, value: same(c => c.h || 0) === null ? 0 : same(c => c.h || 0), onchange: e => apply(c => { if (c.f !== undefined) { if (+e.target.value) c.h = +e.target.value; else delete c.h; } }) })),
          choice("Shape", [["", "Flat"], ["slope", "Slope (ramp)"], ["stairs", "Stairs (8 steps)"]], same(c => c.sh || ""), v => apply(c => { if (c.f === undefined) return; if (v) { c.sh = v; if (c.sd === undefined) c.sd = 1; if (!c.sr) c.sr = 8; } else { delete c.sh; delete c.sd; delete c.sr; } })),
          same(c => c.sh || "") !== "" ? choice("Rises toward", [[0, "North ↑"], [1, "East →"], [2, "South ↓"], [3, "West ←"]], same(c => c.sd || 0), v => apply(c => { if (c.sh) c.sd = +v; })) : "",
          same(c => c.sh || "") !== "" ? choice("Rises by", [1, 2, 3, 4, 6, 8, 10, 12, 14, 16].map(r => [r, r + "/16 of a story" + (r === 8 ? " (half)" : r === 16 ? " (a whole story)" : "")]), same(c => c.sr || 8), v => apply(c => { if (c.sh) c.sr = +v; })) : "",
          h("div", { class: "note", text: "One story is 2 m, so each height step is 12.5 cm. A slope or stairs rising 16/16 reaches the next story." })) : "",
        cells.some(c => PRIM.Grid.isDiag(c)) ? foldout("gc:diag", "Diagonal wall", texSel("Texture", same(c => PRIM.Grid.isDiag(c) ? c.dt : undefined), v => apply(c => { if (PRIM.Grid.isDiag(c)) c.dt = v; })), h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "Remove diagonal walls", onclick: () => apply(c => { delete c.dg; delete c.dt; }) }))) : "",
        cells.some(c => c.c !== undefined) ? foldout("gc:ceil", "Ceiling", texSel("Texture", same(c => c.c), v => apply(c => { if (c.c !== undefined) c.c = v; }))) : "",
        foldout("gc:light", "Light", row("Brightness %", h("input", { type: "range", min: 0, max: 100, step: 5, value: same(c => c.li === undefined ? 100 : c.li) === null ? 100 : same(c => c.li === undefined ? 100 : c.li), onchange: e => apply(c => { if (+e.target.value >= 100) delete c.li; else c.li = +e.target.value; }) }))));
    } else if (sel && sel.kind === "zone" && (m.zones || []).some(z => z.id === sel.id)) {
      const z = m.zones.find(q => q.id === sel.id), zp = m.id + ".z." + z.id + ".", cu = z.type === "custom", f = k => field(zp + k), redo = () => { markDirty(); if (med) { med.redraw(); med.buildPal(); } renderInspector(); };
      const num = (label, k, min) => row(label, h("input", { class: "num", type: "number", step: 1, value: k === "layer" ? (z.layer || 0) + 1 : z[k], "aria-label": label, onchange: e => { const v = Math.round(+e.target.value) - (k === "layer" ? 1 : 0); if (!isFinite(v)) return; z[k] = Math.max(min === undefined ? -1e6 : min, v); if (k !== "layer") { const a = Math.min(z.x0, z.x1), b = Math.max(z.x0, z.x1), c = Math.min(z.y0, z.y1), d = Math.max(z.y0, z.y1); z.x0 = a; z.x1 = b; z.y0 = c; z.y1 = d; } redo(); } }));
      box.append(head(cu ? "⬡" : "⚡", h("input", { class: "idin", value: S.tags.get(zp + "name") || z.id, "aria-label": "Zone name", onchange: e => { S.tags.set(zp + "name", e.target.value.trim() || z.id); redo(); } }), (cu ? "Custom zone" : "Trigger zone") + " · " + z.id + " · " + (z.x1 - z.x0 + 1) + " × " + (z.y1 - z.y0 + 1) + " cells"),
        h("div", { class: "btnrow" }, cu ? h("button", { class: "btn sm", text: "Open its script in the Node Coder", onclick: () => { S.coderT = { kind: "zone", map: m.id, id: z.id }; S.cnode = null; setTab("coder"); } }) : null,
          h("button", { class: "btn sm", text: cu ? "Make it a trigger zone" : "Make it a custom zone", onclick: () => { z.type = cu ? "trigger" : "custom"; if (z.type === "custom") z.script = z.script || { nodes: [], links: [] }; redo(); } }),
          h("button", { class: "btn sm danger", text: "Delete zone", onclick: () => { if (med) { med.select({ kind: "zone", id: z.id }, true); med.deleteSelection(); } S.msel = null; renderInspector(); } })),
        foldout("zn:area", "Area", num("From cell X", "x0"), num("From cell Y", "y0"), num("To cell X", "x1"), num("To cell Y", "y1"), num("Story (1 = ground)", "layer", 0), f("height"), h("div", { class: "note", text: "Drag the zone in the map (Zone tool) to move it. Height 1 reaches one story up, for tall rooms and stairwells." })),
        foldout("zn:rule", "When it works", f("once"), f("needTag"), S.tags.get(zp + "needTag") ? f("needOp") : "", S.tags.get(zp + "needTag") ? f("needVal") : "", h("div", { class: "note", text: "With a tag, the zone only notices you while the tag matches (e.g. door_power is yes)." })),
        cu ? h("div", { class: "note", text: "A custom zone runs its own script: start it with “When the player enters the zone”, “…leaves the zone” or “While the player is inside”. It can use every player node (tags, sounds, messages, hurt, teleport, dialogues, pause…). Script nodes: " + ((z.script && z.script.nodes.length) || 0) + "." })
          : foldout("zn:act", "What it does", ...Schema.ZONE.filter(q => q.trig).map(q => f(q.k)), h("div", { class: "note", text: "Each action is optional; leave a field empty to skip it. Values for tags: a number, yes / no, text, +n / -n (adds) or toggle." })));
    } else if (sel && sel.kind === "thing" && (m.things || []).some(t => t.id === sel.id)) {
      const th = m.things.find(t => t.id === sel.id), ty = S.tags.get(th.prop + ".type"), pn = (S.project.props || []).filter(x => S.tags.get(x.id + ".type") === ty);
      const num = (label, k, step) => h("div", { class: "frow" }, h("label", { text: label }), h("input", { class: "num", type: "number", step, value: th[k] === undefined ? 0 : th[k], onchange: e => { const n = parseFloat(e.target.value); if (isFinite(n)) { th[k] = k === "layer" ? Math.max(0, Math.round(n) - 1) : n; markDirty(); if (S.med) S.med.redraw(); renderInspector(); } } }));
      box.append(head("▣", h("div", { class: "idin ro", text: (S.tags.get(th.prop + ".name") || th.prop) }), (ty || "") + " · story " + ((th.layer || 0) + 1)),
        h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "⟳ Turn 90°", onclick: () => { S.med && S.med.turn(); renderInspector(); } }), h("button", { class: "btn sm danger", text: "Delete", onclick: () => S.med && S.med.deleteSelection() })),
        foldout("mth", "Object", h("div", { class: "frow" }, h("label", { text: "Which object" }), h("select", { class: "sel", onchange: e => { th.prop = e.target.value; markDirty(); renderInspector(); S.med && S.med.redraw(); } }, ...pn.map(x => h("option", { value: x.id, text: S.tags.get(x.id + ".name") || x.id, selected: x.id === th.prop })))),
          num("X", "x", 16), num("Y", "y", 16), num("Turn (degrees)", "ang", 45)),
        h("div", { class: "note", text: ty === "door" ? "A door fills its cell. Put it between two walls; Turn picks which way it runs. Its behaviour is set in the Object Editor tab." : "Drag it in the Map view to move it (D turns it). Its behaviour is set in the Object Editor tab." }));
    } else if (sel && sel.kind === "start") {
      box.append(head("➤", h("div", { class: "idin ro", text: "Player start" }), "Where the player begins"), foldout("mst", "Start", ...["startX", "startY", "startLayer", "startAngle"].map(k => field(pre + k))));
    } else {
      const nc = m.layers.reduce((t, l) => t + Object.keys(l.cells).length, 0), T = S.tags, look = Schema.MAP.filter(p => !p.k.startsWith("start") && Schema.shown(T, pre, p));
      box.append(head("▦", h("input", { class: "idin", value: m.id, "aria-label": "Map name", onchange: e => renameMap(m, e.target.value) }), "grid map · " + m.layers.length + " stor" + (m.layers.length === 1 ? "y" : "ies") + " · " + nc + " cells"),
        h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "Open Fly Editor", onclick: () => setTab("fly") }), h("button", { class: "btn sm", text: "Fullscreen fly", onclick: () => fly3d(m) }), h("button", { class: "btn sm danger", text: "Delete map", onclick: () => deleteMap(m) })),
        foldout("mm", "Look: sky, fog, mood, bloom", ...look.map(p => field(pre + p.k))), foldout("mst", "Player start", ...["startX", "startY", "startLayer", "startAngle"].map(k => field(pre + k))),
        h("div", { class: "note", text: "Use the Select tool to click or box-select cells and edit them here. Every map setting is a named tag (see the Tags tab)." }));
    }
    return box;
  }

  // ================================================================ PLAY (fullscreen, remembers audio/graphics settings)
  // ---- Build Game: one playable .html file (engine + project + every asset inside it)
  async function gameHtml() {
    const toData = b => new Promise((ok, no) => { const r = new FileReader(); r.onload = () => ok(r.result); r.onerror = no; r.readAsDataURL(b); });
    const tags = [...document.querySelectorAll("script")].map(s => ({ s, src: s.getAttribute("src") || s.dataset.src || "" })).filter(x => /^src\/core\//.test(x.src)), code = [];
    for (const x of tags) code.push(x.s.getAttribute("src") ? await (await fetch(x.src)).text() : x.s.textContent);
    const assets = {}; let missing = 0; for (const a of S.project.assets) { const u = S.urls[a.id]; if (!u) { missing++; continue; } try { assets[a.id] = await toData(await (await fetch(u)).blob()); } catch (e) { missing++; } }
    const data = JSON.stringify({ project: S.project, assets }).replace(/</g, "\\u003c"), esc = t => String(t).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
    const html = '<!doctype html>\n<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>' + esc(S.project.name || "Game") + '</title>\n<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Chakra+Petch:wght@400;600;700&family=Share+Tech+Mono&display=swap">\n</head><body><div id="game"></div>\n<script id="game-data" type="application/json">' + data + '</script>\n'
      + code.map(c => "<script>\n" + c.replace(/<\/script/gi, "<\\/script") + "\n</script>").join("\n") + '\n<script>PRIM.GamePlayer.boot(JSON.parse(document.getElementById("game-data").textContent), document.getElementById("game"));</script>\n</body></html>\n';
    return { html, missing };
  }
  async function buildGame() {
    if (!S.project.maps.length) log("Note: the game has no map yet, so “Start game” will only show a placeholder.", "warn");
    log("Building the game…"); let r; try { r = await gameHtml(); } catch (e) { return log("Could not build the game: " + (e && e.message || e), "warn"); }
    const name = (S.project.name || "game").replace(/[\\/:*?"<>|]+/g, "_") + ".html", saved = await Host.saveFile(name, new TextEncoder().encode(r.html), null, true, { description: "Game (HTML)", accept: { "text/html": [".html"] } });
    if (saved) log("Built “" + saved.name + "” (" + Math.round(r.html.length / 1024) + " KB)" + (r.missing ? " – " + r.missing + " asset(s) could not be included" : "") + ". Open it in a browser to play.");
  }
  window.__gameHtml = gameHtml;
  function play() {
    autosave(); const layer = h("div", { class: "play-layer" }); document.body.append(layer);
    const tg = S.tags.fork(), saved = Store.loadSettings(S.project.id); for (const k in saved) if (S.tags.has(k)) tg.ov[k] = saved[k];
    let start = S.tags.get("project.startScreen"); if (!screens().some(s => s.id === start)) start = screens()[0].id;
    if (S.rt) S.rt.stopMusic();
    const rt = new PRIM.Runtime({ host: layer, project: S.project, tags: tg, mode: "play", urls: S.urls, screenId: start, onPersist: ov => Store.saveSettings(S.project.id, ov), onExit: close, onStartGame: startGame, say: t => log(t) });
    function close() { stopPlay(); rt.destroy(); layer.remove(); if (document.fullscreenElement) document.exitFullscreen().catch(() => {}); }
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
    else if (mod && S.pfocus && S.btab === "project" && ["c", "x", "v"].includes(e.key.toLowerCase())) { e.preventDefault(); const k = e.key.toLowerCase(); if (k === "v") assetPaste(); else assetClip(k === "x" ? "cut" : "copy"); }
  });
  // Ctrl+X / Ctrl+C / Ctrl+V in the Project panel (only after a click in it, so the map editor keeps its own copy / paste)
  addEventListener("pointerdown", e => { S.pfocus = !!(e.target && e.target.closest && e.target.closest("#proj")); }, true);
  function assetClip(mode) {
    const items = topLevel(pickedItems()); if (!items.length) return log("Select files or folders in the Project panel first.", "warn");
    S.clip = { mode, items: items.map(i => ({ kind: i.kind, key: i.key })) }; renderProjectBody();
    log((mode === "cut" ? "Cut " : "Copied ") + (items.length > 1 ? items.length + " items" : "“" + baseName(items[0].key) + "”") + ". Open a folder and press Ctrl+V to " + (mode === "cut" ? "move" : "paste") + " " + (items.length > 1 ? "them" : "it") + " there.");
  }
  async function assetPaste() {
    const c = S.clip, dest = S.folder || ""; if (!c || !c.items.length) return log("Nothing to paste: Ctrl+C or Ctrl+X something in the Project panel first.", "warn");
    if (c.mode === "cut") { let n = 0; for (const d of c.items) { if (d.kind === "folder" && (dest === d.key || dest.startsWith(d.key + "/"))) { log("A folder can't be moved into itself.", "warn"); continue; } dropOne(d, dest); n++; } S.clip = null; S.sel = null; S.multi = null; markDirty(); renderProject(); renderInspector(); if (n) log("Moved " + n + " item" + (n > 1 ? "s" : "") + " to Assets" + (dest ? "/" + dest : "") + "."); return; }
    // copy: real duplicates of the stored files (a folder keeps its inner folders)
    const files = [];
    for (const d of c.items) {
      if (d.kind === "asset") { const a = assetOf(d.key); if (a && S.urls[a.id]) files.push({ a, name: a.name, path: "" }); }
      else { const top = baseName(d.key); Store.ensureFolder(S.project, dest ? dest + "/" + top : top); for (const a of S.project.assets.filter(x => x.path === d.key || x.path.startsWith(d.key + "/"))) if (S.urls[a.id]) files.push({ a, name: a.name, path: top + a.path.slice(d.key.length) }); }
    }
    try { const items = []; for (const f of files) items.push({ name: f.name, path: f.path, file: await (await fetch(S.urls[f.a.id])).blob() });
      const added = await Store.ingest(S.project, S.urls, items, dest); markDirty(); log("Pasted " + added.length + " file" + (added.length === 1 ? "" : "s") + " into Assets" + (dest ? "/" + dest : "") + "."); if (S.rt) { S.rt.urls = S.urls; S.rt.rebuild(); } renderProject(); renderInspector(); }
    catch (e) { log("Paste failed: " + e.message, "error"); }
  }
  addEventListener("beforeunload", () => { if (S.project) autosave(); });

  window.__studio = { S, Store, Host, openProject, renderManager, openBytes, saveProject };
  renderManager();
})();
