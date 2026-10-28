/* PRIMEngine (Prime edition) editor shell: projects, the world tree, the room builder / HUD editor, the inspector,
   the asset shelf and play-testing. All values are tags; the inspector draws a field for each one. */
(function () {
  "use strict";
  const PRIM = window.PRIM, D = PRIM.PE, St = PRIM.PEStore, Host = PRIM.Host, root = document.getElementById("pe");
  const h = (t, a, ...kids) => { const e = document.createElement(t); for (const k in a || {}) { const v = a[k]; if (v === undefined || v === null || v === false) continue; if (k === "text") e.textContent = v; else if (k === "value") e.value = v; else if (k === "class") e.className = v; else if (k.startsWith("on")) e[k] = v; else if (k === "style") e.style.cssText = v; else if (k in e && typeof v !== "string") e[k] = v; else e.setAttribute(k, v === true ? "" : v); } for (const c of kids.flat()) if (c !== null && c !== undefined && c !== false && c !== "") e.append(c); return e; };
  const S = window.__pe = { project: null, tags: null, urls: {}, sel: null, view: "builder", hudScreen: null, hudSel: null, tree: { rooms: {} }, fold: {} };
  let E = {};
  const log = (t, kind) => { if (E.status) { E.status.textContent = t; E.status.className = "pe-status" + (kind ? " " + kind : ""); } };
  const dirty = () => { clearTimeout(S.saveT); S.saveT = setTimeout(() => { if (S.project) St.save(S.project); }, 600); };

  // ================================================================ project list
  function home() {
    closeProject(); const list = St.list();
    root.replaceChildren(h("div", { class: "pe-home" },
      h("div", { class: "pe-logo" }, h("b", { text: "PRIMEngine" }), h("span", { text: "Prime edition · build first-person adventure games without code" })),
      h("div", { class: "pe-homebar" }, h("button", { class: "pe-btn hi", text: "+ New project", onclick: () => { const p = St.create(); open(p.id); } }), h("button", { class: "pe-btn", text: "Open a .prime file…", onclick: openFile }), h("a", { class: "pe-btn", href: "classic.html", text: "Classic Studio" })),
      h("div", { class: "pe-plist" }, list.length ? list.map(x => h("div", { class: "pe-prow" }, h("div", { class: "pe-pname", text: x.name, onclick: () => open(x.id) }), h("div", { class: "pe-pdate", text: new Date(x.modified).toLocaleString() }),
        h("button", { class: "pe-btn sm", text: "Delete", onclick: async () => { if (confirm("Delete “" + x.name + "” from this browser? (Saved .prime files are not touched.)")) { await St.remove(x.id); home(); } } }))) : h("div", { class: "pe-empty", text: "No projects yet. Make a new one: it starts with a room, a player start, a light, four beams and the visors." }))));
  }
  async function openFile() { const f = await Host.openFile(); if (!f) return; try { const r = await St.unpack(f.bytes); open(r.project.id, f); } catch (e) { alert("Could not open it: " + (e.message || e)); } }
  function closeProject() { if (S.builder) { S.builder.destroy(); S.builder = null; } if (S.hed) { S.hed.destroy(); S.hed = null; } if (S.game) { S.game.destroy(); S.game = null; } S.project = null; }

  // ================================================================ the editor
  async function open(id, file) {
    const p = St.load(id); if (!p) return home(); S.project = p; S.file = file || null; S.tags = new PRIM.Tags(p.tags); S.sel = null; S.room = p.rooms[0] && p.rooms[0].id; S.view = "builder"; S.inspect = null;
    PRIM.FBX.cache = {}; PRIM.FBX.pending = {}; PRIM.FBX.decorate = (aid, m) => { if (PRIM.AnimLib) PRIM.AnimLib.apply(S.project, m, aid); };
    S.urls = await St.loadAssets(p); for (const a of p.assets) if (a.type === "model") PRIM.FBX.load(a.id, S.urls[a.id]);
    S.tags.on(() => { dirty(); renderTreeSoon(); });
    layout(); renderAll();
  }
  function layout() {
    E = {}; root.replaceChildren(h("div", { class: "pe-app" },
      E.top = h("div", { class: "pe-top" }), E.left = h("div", { class: "pe-left" }), h("div", { class: "pe-center" }, E.ctabs = h("div", { class: "pe-ctabs" }), E.cbar = h("div", { class: "pe-cbar" }), E.host = h("div", { class: "pe-host" })),
      E.right = h("div", { class: "pe-right" }), h("div", { class: "pe-bottom" }, E.shelfBar = h("div", { class: "pe-shelfbar" }), E.shelf = h("div", { class: "pe-shelf" })), E.status = h("div", { class: "pe-status" })));
    E.shelf.addEventListener("dragover", e => { if ([...e.dataTransfer.types].includes("Files")) { e.preventDefault(); E.shelf.classList.add("drop"); } }); E.shelf.addEventListener("dragleave", () => E.shelf.classList.remove("drop"));
    E.shelf.addEventListener("drop", async e => { E.shelf.classList.remove("drop"); if (![...e.dataTransfer.types].includes("Files")) return; e.preventDefault(); importFiles(await Host.fromDrop(e.dataTransfer)); });
  }
  function renderAll() { renderTop(); renderTree(); renderCenter(); renderInspector(); renderShelf(); }
  function renderTop() {
    const p = S.project; E.top.replaceChildren(h("div", { class: "pe-brand", text: "PRIMEngine", onclick: () => { St.save(p); home(); }, title: "Back to the project list" }),
      h("input", { class: "pe-pn", value: p.name, onchange: e => { p.name = e.target.value.trim() || "Untitled"; dirty(); } }), h("span", { class: "sp" }),
      h("button", { class: "pe-btn play", text: "▶ Play", title: "Play from the player start", onclick: () => play(false) }), h("button", { class: "pe-btn", text: "▶ From camera", title: "Play from where the editor camera is", onclick: () => play(true) }), h("span", { class: "sp" }),
      h("button", { class: "pe-btn", text: "Save", onclick: () => save(false) }), h("button", { class: "pe-btn", text: "Save as…", onclick: () => save(true) }), h("button", { class: "pe-btn", text: "Open…", onclick: openFile }), h("button", { class: "pe-btn", text: "Projects", onclick: () => { St.save(p); home(); } }));
  }
  async function save(as) { const { bytes, missing } = await St.pack(S.project), r = await Host.saveFile((S.project.name || "game") + ".prime", bytes, as ? null : S.file && S.file.ref, as); St.save(S.project); if (r) { S.file = r; log("Saved " + r.name + (missing ? " (" + missing + " file(s) missing)" : "")); } }

  // ---------------- world tree (left)
  let treeT = 0; function renderTreeSoon() { if (treeT) return; treeT = requestAnimationFrame(() => { treeT = 0; if (S.project) renderTree(); }); }
  function renderTree() {
    const p = S.project, T = S.tags, rows = [], sel = S.sel, row = (ic, label, sub, on, click, cls) => h("div", { class: "pe-row" + (on ? " on" : "") + (cls ? " " + cls : ""), onclick: click }, h("span", { class: "ic", text: ic }), h("span", { class: "lb", text: label }), sub ? h("span", { class: "sub", text: sub }) : null);
    rows.push(h("div", { class: "pe-head" }, h("span", { text: "ROOMS" }), h("button", { class: "pe-btn sm", text: "+ Room", onclick: () => { const r = D.addRoom(p); S.room = r.id; select({ kind: "room", id: r.id }); changed(true); } })));
    for (const r of p.rooms) {
      const open = S.tree.rooms[r.id] !== false, cur = S.room === r.id;
      rows.push(row(open ? "▾" : "▸", T.get(r.id + ".name") || r.id, r.brushes.length + " blocks", sel && sel.kind === "room" && sel.id === r.id, e => { if (e.target.classList.contains("ic")) { S.tree.rooms[r.id] = !open; renderTree(); return; } S.room = r.id; select({ kind: "room", id: r.id }); }, cur ? "cur" : ""));
      if (!open) continue;
      for (const t of r.things) rows.push(row(D.THING_KINDS[t.kind].icon, T.get(t.id + ".name") || t.id, t.kind, sel && sel.id === t.id, () => { S.room = r.id; select({ kind: "thing", id: t.id }); }, "in"));
      for (const b of r.brushes) rows.push(row(b.shape === "ramp" ? "◢" : "▣", (b.shape === "ramp" ? "Ramp " : "Block ") + b.id, (b.s[0] / 64).toFixed(1) + "×" + (b.s[1] / 64).toFixed(1) + "×" + (b.s[2] / 64).toFixed(1) + " m", sel && sel.id === b.id, () => { S.room = r.id; select({ kind: "brush", id: b.id }); }, "in dim"));
    }
    rows.push(h("div", { class: "pe-head" }, h("span", { text: "GAME" })));
    const g = (ic, label, kind, id) => rows.push(row(ic, label, "", sel && sel.kind === kind && (!id || sel.id === id), () => select({ kind, id })));
    g("◉", "Player", "player"); g("⚙", "World settings", "world");
    rows.push(h("div", { class: "pe-head sub" }, h("span", { text: "BEAMS" }), h("button", { class: "pe-btn sm", text: "+ Beam", onclick: () => { const id = D.addBeam(p, { name: "New beam" }); select({ kind: "beam", id }); changed(true); } })));
    for (const b of p.beams) { const r = row("●", T.get(b.id + ".name") || b.id, T.get(b.id + ".start") ? "start" : "", sel && sel.kind === "beam" && sel.id === b.id, () => select({ kind: "beam", id: b.id }), "in"), ic = r.querySelector(".ic"); ic.style.color = T.get(b.id + ".color"); rows.push(r); }
    rows.push(h("div", { class: "pe-head sub" }, h("span", { text: "VISORS & HUD" })));
    for (const [v, name] of D.VISORS) rows.push(row("◐", name + " visor", T.get("visor." + v + ".start") ? "start" : "pick-up", sel && sel.kind === "visor" && sel.id === v, () => select({ kind: "visor", id: v }), "in"));
    const ents = p.rooms.flatMap(r => r.things).filter(t => T.get(t.id + ".scanTitle"));
    rows.push(h("div", { class: "pe-head sub" }, h("span", { text: "LOGBOOK (" + ents.length + ")" })));
    for (const t of ents) rows.push(row("◎", T.get(t.id + ".scanTitle"), T.get(t.id + ".scanCategory") || "", sel && sel.id === t.id, () => { const r = D.findRoomOf(p, t.id); S.room = r && r.id; select({ kind: "thing", id: t.id }); }, "in"));
    E.left.replaceChildren(...rows);
  }
  function select(sel, quiet) { S.sel = sel; if (sel && (sel.kind === "brush" || sel.kind === "thing") && S.builder && !quiet) S.builder.sel = sel; if (sel && sel.kind === "visor" && S.view === "hud") { S.hudScreen = S.tags.get("visor." + sel.id + ".hud"); renderCenter(); } renderTree(); renderInspector(); }
  function changed(structure) { dirty(); if (S.builder && structure) S.builder.rebuild(); if (structure) renderTree(); renderInspector(); }

  // ---------------- centre: room builder or HUD editor
  function renderCenter() {
    if (S.builder) { S.cam = S.builder.cam; S.builder.destroy(); S.builder = null; } if (S.hed) { S.hed.destroy(); S.hed = null; }
    E.ctabs.replaceChildren(...[["builder", "Room Builder"], ["hud", "HUD"]].map(([k, l]) => h("button", { class: "pe-tab" + (S.view === k ? " on" : ""), text: l, onclick: () => { S.view = k; renderCenter(); renderInspector(); } })));
    if (S.view === "hud") return hudCenter();
    const B = S.builder = new PRIM.PEBuilder({ host: E.host, project: S.project, tags: S.tags, urls: S.urls, sel: S.sel && (S.sel.kind === "brush" || S.sel.kind === "thing") ? S.sel : null, cam: S.cam, log, getRoom: () => S.room,
      onSelect: sel => { if (sel) { const r = D.findRoomOf(S.project, sel.id); if (r) S.room = r.id; } select(sel, true); }, onChange: st => changed(st), onTool: () => { if (S.builder) toolbar(); } });
    toolbar();
    function toolbar() {
      const tb = (k, label, tip) => h("button", { class: "pe-btn sm" + (B.tool === k ? " on" : ""), text: label, title: tip, onclick: () => { B.setTool(k); B.root.focus(); } });
      E.cbar.replaceChildren(tb("select", "Select (V)"), tb("box", "Box (B)"), tb("ramp", "Ramp (N)"), tb("place", "Place (O)"),
        h("select", { class: "pe-sel", title: "What Place puts down", onchange: e => { B.place = e.target.value; B.setTool("place"); } }, ...Object.keys(D.THING_KINDS).map(k => h("option", { value: k, text: D.THING_KINDS[k].icon + " " + D.THING_KINDS[k].label, selected: B.place === k }))),
        h("label", { class: "pe-lab", text: "Height" }), h("input", { class: "pe-num", type: "number", step: 32, value: B.boxH, title: "Height of new boxes and ramps (units, 64 = 1 m)", onchange: e => { B.boxH = Math.max(8, +e.target.value || 128); B.setTool(B.tool); } }),
        h("button", { class: "pe-btn sm", text: "↶", title: "Undo (Ctrl+Z)", onclick: () => B.history(true) }), h("button", { class: "pe-btn sm", text: "↷", title: "Redo (Ctrl+Y)", onclick: () => B.history(false) }),
        h("span", { class: "hint", text: "hold right mouse + WASD to fly · drag pictures onto faces" }));
    }
  }
  function hudCenter() {
    const p = S.project, T = S.tags; if (!S.hudScreen || !p.screens.some(s => s.id === S.hudScreen)) S.hudScreen = T.get("visor.combat.hud") || (p.screens[0] && p.screens[0].id);
    const sc = p.screens.find(s => s.id === S.hudScreen);
    E.cbar.replaceChildren(h("select", { class: "pe-sel", onchange: e => { S.hudScreen = e.target.value; S.hudSel = null; renderCenter(); renderInspector(); } }, ...p.screens.map(s => h("option", { value: s.id, text: s.id + (D.VISORS.filter(([v]) => T.get("visor." + v + ".hud") === s.id).map(([, n]) => " · " + n).join("")), selected: s.id === S.hudScreen }))),
      h("button", { class: "pe-btn sm", text: "+ Element ▾", onclick: e => menu(e.currentTarget, Object.keys(D.HUD_NODE).map(k => ({ t: D.HUD_NODE[k].label, fn: () => addHudNode(k) }))) }),
      h("button", { class: "pe-btn sm", text: "Copy this HUD to all visors", onclick: () => copyHud(sc) }), h("span", { class: "hint", text: "values like {player.missiles} show live numbers · wheel zoom, right drag pan" }));
    if (!sc) { E.host.replaceChildren(h("div", { class: "pe-empty", text: "No HUD screens." })); return; }
    S.hed = new PRIM.HudEditor({ host: E.host, project: p, tags: T, urls: S.urls, screenId: sc.id, selId: S.hudSel, onSelect: id => { S.hudSel = id; renderInspector(); }, onChange: () => { dirty(); renderInspector(); } });
  }
  function addHudNode(type) { const sc = S.project.screens.find(s => s.id === S.hudScreen); if (!sc) return; const id = D.uid(type, sc.nodes.map(n => n.id)); sc.nodes.push({ id, type }); D.define(S.project, sc.id + "." + id, D.HUD_NODE[type].props, {}); S.hudSel = id; S.tags.emit([]); dirty(); renderCenter(); renderInspector(); }
  function copyHud(src) { const p = S.project; for (const [v] of D.VISORS) { const id = S.tags.get("visor." + v + ".hud"); if (!id || id === src.id) continue; const dst = p.screens.find(s => s.id === id); if (!dst) continue; for (const n of dst.nodes) D.dropTags(p, dst.id + "." + n.id); dst.nodes = src.nodes.map(n => ({ id: n.id, type: n.type }));
      for (const n of src.nodes) for (const pr of D.HUD_NODE[n.type].props) { const k = src.id + "." + n.id + "." + pr.k, r = p.tags[k]; if (r) p.tags[dst.id + "." + n.id + "." + pr.k] = Object.assign({}, r, { owner: dst.id + "." + n.id }); } } S.tags.emit([]); dirty(); log("Copied the HUD to every visor"); }

  // ---------------- inspector (right)
  function field(name, o) {
    o = o || {}; const T = S.tags, t = T.spec(name); if (!t) return null; const label = o.label || t.label || name, set = v => T.set(name, v), redraw = () => renderInspector();
    const row = ctl => h("div", { class: "pe-fld" }, h("label", { text: label, title: name }), h("div", { class: "pe-ctl" }, ctl));
    if (t.type === "number") { const step = t.step || 1, inp = h("input", { class: "pe-num", type: "number", step, value: +(+T.get(name)).toFixed(step >= 1 ? 0 : 3), onchange: e => { let v = parseFloat(e.target.value); if (!isFinite(v)) return; if (isFinite(t.min)) v = Math.max(t.min, v); if (isFinite(t.max)) v = Math.min(t.max, v); set(v); } });
      const sl = isFinite(t.min) && isFinite(t.max) && t.max - t.min <= 10 ? h("input", { type: "range", min: t.min, max: t.max, step, value: T.get(name), oninput: e => { set(+e.target.value); inp.value = e.target.value; } }) : null; return row(h("div", { class: "pe-numw" }, sl, inp)); }
    if (t.type === "bool") return row(h("input", { type: "checkbox", checked: !!T.get(name), onchange: e => { set(e.target.checked); redraw(); } }));
    if (t.type === "color") return row(h("input", { type: "color", value: T.get(name) || "#ffffff", oninput: e => set(e.target.value) }));
    let opts = null;
    if (t.type === "choice") opts = t.options; else if (t.type === "asset") opts = [["", "None"], ...S.project.assets.filter(a => a.type === t.assetType).map(a => [a.id, a.name])];
    else if (t.type === "anim") { const mt = t.modelTag || (t.owner + ".model"), mo = PRIM.FBX.cache[T.get(mt)]; opts = [["", "(none)"], ...(mo ? Object.keys(mo.anims) : []).map(n => [n, n])]; }
    else if (t.ref === "beam") opts = [["", "(choose)"], ...S.project.beams.map(b => [b.id, T.get(b.id + ".name") || b.id])]; else if (t.ref === "screen") opts = [["", "None"], ...S.project.screens.map(s => [s.id, s.id])];
    if (opts) { const sel = h("select", { class: "pe-sel", onchange: e => { set(e.target.value); redraw(); } }, ...opts.map(([v, l]) => h("option", { value: v, text: l, selected: String(T.get(name)) === String(v) }))); return row(sel); }
    if (t.multiline) return row(h("textarea", { class: "pe-txt", rows: 4, value: T.get(name) || "", oninput: e => set(e.target.value) }));
    return row(h("input", { class: "pe-txt", type: "text", value: T.get(name) || "", oninput: e => set(e.target.value) }));
  }
  const fold = (key, title, ...kids) => { const open = S.fold[key] !== false; return h("div", { class: "pe-fold" }, h("div", { class: "pe-fh", onclick: () => { S.fold[key] = !open; renderInspector(); } }, h("span", { text: (open ? "▾ " : "▸ ") + title })), open ? h("div", { class: "pe-fb" }, ...kids) : null); };
  const fields = (pre, props) => props.filter(p => !p.hidden && D.shown(S.tags, pre, p)).map(p => field(pre + "." + p.k));
  const numStruct = (label, obj, key, i, after) => h("div", { class: "pe-fld" }, h("label", { text: label }), h("div", { class: "pe-ctl" }, h("input", { class: "pe-num", type: "number", step: 8, value: obj[key][i], onchange: e => { const v = Math.round(parseFloat(e.target.value)); if (!isFinite(v)) return; obj[key][i] = v; if (after) after(); if (S.builder) { S.builder.rebuild(); S.builder.commit(); } dirty(); renderTree(); } })));
  function renderInspector() {
    const T = S.tags, p = S.project, box = h("div", { class: "pe-insp" }), sel = S.sel, head = (ic, title, sub) => h("div", { class: "pe-ihead" }, h("span", { class: "big", text: ic }), h("div", {}, h("div", { class: "t", text: title }), h("div", { class: "s", text: sub || "" })));
    if (S.view === "hud" && S.hudSel) { const sc = p.screens.find(s => s.id === S.hudScreen), n = sc && sc.nodes.find(x => x.id === S.hudSel);
      if (n) { box.append(head(D.HUD_NODE[n.type].icon, n.id, D.HUD_NODE[n.type].label + " on " + sc.id), h("div", { class: "pe-btns" }, h("button", { class: "pe-btn sm danger", text: "Delete", onclick: () => { sc.nodes = sc.nodes.filter(x => x !== n); D.dropTags(p, sc.id + "." + n.id); S.hudSel = null; S.tags.emit([]); dirty(); renderCenter(); renderInspector(); } })), fold("hn", "Element", ...fields(sc.id + "." + n.id, D.HUD_NODE[n.type].props)));
        if (n.type === "title") box.append(h("div", { class: "pe-note", text: "Live values: {player.energyNow} {player.tanks} {player.missiles} {player.missilesMax} {player.beamName} {player.visorName} {player.charge}" })); E.right.replaceChildren(box); return; } }
    if (!sel) { box.append(h("div", { class: "pe-note", text: "Select something in the room or in the list on the left. Tip: the Place tool puts down power-ups, enemies, doors, scan points, lights and models." })); }
    else if (sel.kind === "brush") { const f = S.builder ? S.builder.find(sel.id) : null, b = f ? f.brush : D.findRoomOf(p, sel.id) && D.findRoomOf(p, sel.id).brushes.find(x => x.id === sel.id); if (!b) { E.right.replaceChildren(box); return; }
      box.append(head(b.shape === "ramp" ? "◢" : "▣", (b.shape === "ramp" ? "Ramp " : "Block ") + b.id, "Drag it in the view · yellow handles resize · R turns"),
        fold("bp", "Position and size (units, 64 = 1 m)", numStruct("X", b, "p", 0), numStruct("Y (bottom)", b, "p", 1), numStruct("Z", b, "p", 2), numStruct("Width (X)", b, "s", 0), numStruct("Height", b, "s", 1), numStruct("Depth (Z)", b, "s", 2),
          b.shape === "ramp" ? h("div", { class: "pe-fld" }, h("label", { text: "Rises toward" }), h("div", { class: "pe-ctl" }, h("select", { class: "pe-sel", onchange: e => { b.dir = +e.target.value; if (S.builder) { S.builder.rebuild(); S.builder.commit(); } dirty(); } }, ...[["0", "+X (east)"], ["1", "+Z (north)"], ["2", "-X (west)"], ["3", "-Z (south)"]].map(([v, l]) => h("option", { value: v, text: l, selected: String(b.dir || 0) === v }))))) : null),
        fold("bb", "Block", ...fields(b.id, D.BRUSH)), fold("bf", "Faces" + (sel.face ? " (clicked: " + D.FACE_LABEL[sel.face] + ")" : ""), ...D.FACES[b.shape].map(fc => h("div", { class: "pe-face" + (sel.face === fc ? " on" : "") }, h("div", { class: "pe-fn", text: D.FACE_LABEL[fc] }), field(b.id + "." + fc + ".tex", { label: "Texture" }), field(b.id + "." + fc + ".color", { label: "Colour (empty = block colour)" }))),
          h("div", { class: "pe-note", text: "Drop a picture from the shelf onto a face to texture it (hold Shift for the whole block)." }))); }
    else if (sel.kind === "thing") { const r = D.findRoomOf(p, sel.id), t = r && r.things.find(x => x.id === sel.id); if (!t) { E.right.replaceChildren(box); return; } const K = D.THING_KINDS[t.kind];
      box.append(head(K.icon, T.get(t.id + ".name") || t.id, K.label + " · " + t.id), h("div", { class: "pe-btns" }, h("button", { class: "pe-btn sm danger", text: "Delete", onclick: () => { S.builder && (S.builder.sel = sel, S.builder.deleteSel()); } }), h("button", { class: "pe-btn sm", text: "Duplicate", onclick: () => { S.builder && (S.builder.sel = sel, S.builder.duplicate()); } })),
        fold("tp", "Position", numStruct("X", t, "p", 0), numStruct("Y", t, "p", 1), numStruct("Z", t, "p", 2), h("div", { class: "pe-fld" }, h("label", { text: "Turn (degrees)" }), h("div", { class: "pe-ctl" }, h("input", { class: "pe-num", type: "number", step: 15, value: t.yaw || 0, onchange: e => { t.yaw = +e.target.value || 0; S.builder && (S.builder.rebuild(), S.builder.commit()); dirty(); } })))),
        fold("tk", K.label, ...fields(t.id, [D.S("name", "Name")].concat(K.props.filter(q => !/^scan/.test(q.k))))));
      if (K.props.some(q => q.k === "scanTitle")) box.append(fold("ts", "Logbook (Scan visor)", ...fields(t.id, K.props.filter(q => /^scan/.test(q.k))), h("div", { class: "pe-note", text: t.kind === "scan" ? "A scan point is invisible except in the Scan visor." : "Give it a logbook title to make it scannable." })));
      if (t.kind === "door") box.append(h("div", { class: "pe-note", text: "Shield colours: blue = any beam, a beam's colour = only that beam, red = missiles, grey = sealed. R turns the door 90°. Put it in a gap between two blocks." })); }
    else if (sel.kind === "room") { const r = p.rooms.find(x => x.id === sel.id); if (r) box.append(head("▢", T.get(r.id + ".name") || r.id, r.brushes.length + " blocks · " + r.things.length + " objects"), h("div", { class: "pe-btns" }, h("button", { class: "pe-btn sm danger", text: "Delete room", disabled: p.rooms.length < 2, onclick: () => { if (!confirm("Delete this room and everything in it?")) return; for (const b of r.brushes) D.dropTags(p, b.id); for (const t of r.things) D.dropTags(p, t.id); D.dropTags(p, r.id); p.rooms.splice(p.rooms.indexOf(r), 1); S.room = p.rooms[0].id; select(null); changed(true); } })), fold("rm", "Room", ...fields(r.id, D.ROOM)), h("div", { class: "pe-note", text: "New blocks and objects go into the room selected on the left (shown in bold). Rooms are parts of one world: build them next to each other and join them with doors." })); }
    else if (sel.kind === "world") box.append(head("⚙", "World settings", ""), fold("w", "World", ...fields("world", D.WORLD)));
    else if (sel.kind === "player") { box.append(head("◉", "Player", "every value is a player.* tag")); for (const [g, keys] of D.PLAYER_GROUPS) box.append(fold("pl:" + g, g, ...fields("player", D.PLAYER.filter(q => keys.includes(q.k))))); }
    else if (sel.kind === "beam") { const b = p.beams.find(x => x.id === sel.id); if (b) box.append(head("●", T.get(b.id + ".name") || b.id, b.id), h("div", { class: "pe-btns" }, h("button", { class: "pe-btn sm danger", text: "Delete beam", disabled: p.beams.length < 2, onclick: () => { p.beams.splice(p.beams.indexOf(b), 1); D.dropTags(p, b.id); select(null); changed(true); } }),
      h("button", { class: "pe-btn sm", text: "▲", title: "Earlier in the list (beam keys follow this order)", onclick: () => { const i = p.beams.indexOf(b); if (i > 0) { p.beams.splice(i, 1); p.beams.splice(i - 1, 0, b); changed(true); } } })), fold("bm", "Beam", ...fields(b.id, D.BEAM)), h("div", { class: "pe-note", text: "Beam keys pick beams in this order (1, 2, 3, 4). A beam that does not start available is given by a Power-up set to “A beam”." })); }
    else if (sel.kind === "visor") { const v = sel.id; box.append(head("◐", (D.VISORS.find(x => x[0] === v) || [0, v])[1] + " visor", "visor." + v), fold("vs", "Visor", ...fields("visor." + v, D.VISOR)), h("div", { class: "pe-btns" }, h("button", { class: "pe-btn sm", text: "Edit its HUD", onclick: () => { S.view = "hud"; S.hudScreen = T.get("visor." + v + ".hud"); S.hudSel = null; renderCenter(); renderInspector(); } })),
      h("div", { class: "pe-note", text: { combat: "The normal view.", scan: "Hold fire on things with a logbook entry to scan them; markers show what can be scanned.", thermal: "Shows heat: enemies and blocks with Heat glow, the rest goes dark.", xray: "Shows blocks and enemies marked “Only visible with the X-ray visor” (hidden platforms, invisible enemies)." }[v] })); }
    E.right.replaceChildren(box);
  }

  // ---------------- asset shelf (bottom)
  function renderShelf() {
    const p = S.project; E.shelfBar.replaceChildren(h("span", { class: "pe-headl", text: "ASSETS" }), h("button", { class: "pe-btn sm", text: "Import files…", onclick: async () => importFiles(await Host.pickFiles()) }), h("span", { class: "hint", text: "drop files here · drag pictures onto faces, models into the room" }));
    E.shelf.replaceChildren(...(p.assets.length ? p.assets.map(a => { const it = h("div", { class: "pe-asset", draggable: "true", title: a.name + " (" + a.type + ")" }, a.type === "image" && S.urls[a.id] ? h("img", { src: S.urls[a.id], alt: "" }) : h("div", { class: "pe-aic", text: { model: "▣", sound: "♪", font: "Aa" }[a.type] || "?" }), h("div", { class: "pe-an", text: a.name }),
      h("button", { class: "pe-x", text: "×", title: "Delete", onclick: async e => { e.stopPropagation(); if (!confirm("Delete " + a.name + "?")) return; p.assets.splice(p.assets.indexOf(a), 1); await St.delAsset(p.id, a.id); dirty(); renderShelf(); } }));
      it.addEventListener("dragstart", e => { e.dataTransfer.setData("application/x-pe-asset", a.id); e.dataTransfer.effectAllowed = "copy"; });
      if (a.type === "sound") it.addEventListener("click", () => PRIM.Audio && PRIM.Audio.play(S.urls[a.id], 0.7, false)); return it; }) : [h("div", { class: "pe-empty", text: "No files yet: import pictures (textures, HUD art, logbook images), FBX models and sounds." })]));
  }
  async function importFiles(items) { if (!items || !items.length) return; const added = await St.ingest(S.project, S.urls, items.map(i => ({ name: i.name, file: i.file }))); St.save(S.project); renderShelf(); renderInspector(); log("Imported " + added.length + " file(s)"); }

  // ---------------- play
  function play(fromCam) {
    St.save(S.project); const layer = h("div", { class: "pe-play" }); document.body.append(layer); const c = S.builder && S.builder.cam;
    const start = fromCam && c ? { pos: [c.x, c.y - (+S.tags.get("player.eye") || 128), c.z], yaw: c.yaw, pitch: Math.max(-0.6, Math.min(0.6, c.pitch)) } : null;
    if (S.builder) S.builder.paused = true;
    S.game = new PRIM.PEGame({ host: layer, project: S.project, tags: S.tags, urls: S.urls, start, say: t => log(t), onExit: () => { S.game.destroy(); S.game = null; layer.remove(); if (S.builder) S.builder.paused = false; } });
  }

  // ---------------- small pop-up menu
  function menu(anchor, items) { document.querySelectorAll(".pe-menu").forEach(m => m.remove()); const r = anchor.getBoundingClientRect(), m = h("div", { class: "pe-menu", style: "left:" + r.left + "px;top:" + r.bottom + "px" }, ...items.map(it => h("div", { class: "pe-mi", text: it.t, onclick: () => { m.remove(); it.fn(); } }))); document.body.append(m); setTimeout(() => addEventListener("pointerdown", function off(e) { if (!m.contains(e.target)) { m.remove(); removeEventListener("pointerdown", off, true); } }, true), 0); }

  addEventListener("keydown", e => { if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "s" && S.project) { e.preventDefault(); save(e.shiftKey); } });
  if (window.primeDesktop && window.primeDesktop.onOpen) window.primeDesktop.onOpen(async d => { try { const r = await St.unpack(d.bytes); open(r.project.id, { ref: d.path, name: d.path }); } catch (e) { alert(e.message || e); } });
  S.open = open; S.home = home; S.select = select; S.play = play;
  home();
})();
