/* PRIMEngine Studio: the grid map editor (EasyFPS style). The map is a grid of 2 m cells in layers (stories).
   Pick a texture in the palette, pick a tool (Wall, Floor, Ceiling, Room...), click or drag over cells to place it.
   Right mouse button (or Shift with the Erase tool) takes it away again. Middle mouse / Space + drag pans, the wheel zooms.
   The cells are structure (map.layers, see grid.js); map settings (fog, mood, player start) are tags. */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), G = PRIM.Grid, C = G.CELL;
  const DIRS = ["North ↑", "East →", "South ↓", "West ←"], ARROW = ["↑", "→", "↓", "←"];
  const RISES = [1, 2, 4, 6, 8, 12, 16].map(r => [r, r + "/16" + (r === 16 ? " (a whole story)" : r === 8 ? " (half a story)" : r === 4 ? " (a quarter)" : r === 1 ? " (tiny)" : "")]);
  const TOOLS = [
    ["select", "Select", "V", "Click or drag a box to select cells (edit them on the right). Drag an object or the start to move it."],
    ["wall", "Wall", "W", "Click / drag to place wall blocks with the chosen texture. Right mouse removes walls."],
    ["diag", "Diagonal", "G", "Click to place the chosen diagonal wall (some shapes span 2 or 3 cells). D turns it, M mirrors it. Right mouse removes it."],
    ["floor", "Floor", "F", "Click / drag to lay floor with the chosen texture. Right mouse removes floor."],
    ["ceil", "Ceiling", "C", "Click / drag to put a ceiling (top of this story). Right mouse removes it."],
    ["room", "Room", "R", "Drag a box: floor and ceiling inside, walls around it (only where nothing is yet). Right-drag clears the box."],
    ["height", "Height", "H", "Click / drag to set the floor height (16 steps per story). Right mouse sets it back to 0."],
    ["slope", "Slope", "S", "Click / drag to make ramps that rise toward the chosen direction. Right mouse makes the floor flat again."],
    ["stairs", "Stairs", "T", "Click / drag to make stairs (8 steps) that rise toward the chosen direction. Right mouse makes it flat."],
    ["light", "Light", "L", "Click / drag to set how bright cells are. Right mouse sets them back to 100%."],
    ["erase", "Erase", "X", "Click / drag to empty cells completely (wall, floor, ceiling)."],
    ["pick", "Pick", "I", "Click a cell to take its texture (Alt + click does this with any tool)."],
    ["object", "Object", "O", "Click to place the chosen object (doors snap to the middle of a cell). Click an object to select it, drag to move. D turns it."],
    ["start", "Player start", "P", "Click to put the player start. D turns it."],
    ["path", "Path", "N", "Click to add points to a path (a new path starts when none is open). Enter or right-click finishes it. Drag a point to move it, Delete removes the selected path. Patrol enemies walk them."],
    ["zone", "Zone", "Z", "Drag a box over cells to make a zone (Zones tab: trigger or custom). Click a zone to select it, drag it to move it. Delete removes it."],
  ];
  const KEYS = {}; for (const t of TOOLS) KEYS[t[2].toLowerCase()] = t[0];

  const ME = (PRIM.MapEditor = function (o) {
    this.o = o; this.m = o.map; this.T = o.tags; this.dpr = Math.min(2, window.devicePixelRatio || 1); this.imgs = {};
    const st = ME.state = ME.state || { tool: "wall", tex: "", L: 0, hgt: 2, dir: 1, rise: 8, light: 70, below: true, ceilShow: true, shape: "d45", rot: 0, flip: false, panel: "rooms", base: -1, zkind: "trigger" };
    this.s = st; if (st.L >= this.m.layers.length + 1) st.L = 0; this.v = { cx: 0, cy: 0, z: 0.25 }; this.sel = null; this.hover = null;
    const h = (t, a, ...k) => { const e = document.createElement(t); for (const n in a || {}) { if (n === "text") e.textContent = a[n]; else if (n.startsWith("on")) e[n] = a[n]; else e.setAttribute(n, a[n]); } for (const c of k.flat()) if (c) e.append(c); return e; };
    this.h = h; this.wrap = h("div", { class: "med", tabindex: "0" }); this.bar = h("div", { class: "medbar" }); this.body = h("div", { class: "medbody" });
    this.pal = h("div", { class: "medpal" }); this.cv = h("canvas", { class: "medcv" }); this.body.append(this.pal, this.cv); this.wrap.append(this.bar, this.body); o.host.replaceChildren(this.wrap);
    this.cx = this.cv.getContext("2d"); this.buildBar(); this.buildPal();
    for (const el of [this.pal, this.bar]) el.addEventListener("click", e => { if (!/INPUT|SELECT|TEXTAREA/.test(e.target.tagName)) this.wrap.focus({ preventScroll: true }); });   // keys keep working after clicking a button
    this.ro = new ResizeObserver(() => this.redraw()); this.ro.observe(this.cv);
    this.unsub = this.T.on(() => this.redraw());
    const cv = this.cv; cv.addEventListener("pointerdown", e => this.down(e)); cv.addEventListener("pointermove", e => this.move(e)); cv.addEventListener("pointerup", e => this.up(e)); cv.addEventListener("pointercancel", e => this.up(e));
    cv.addEventListener("pointerleave", () => { this.hover = null; this.redraw(); }); cv.addEventListener("wheel", e => this.wheel(e), { passive: false }); cv.addEventListener("contextmenu", e => e.preventDefault()); cv.addEventListener("auxclick", e => e.preventDefault());
    this.wrap.addEventListener("keydown", e => this.key(e)); this.wrap.addEventListener("keyup", e => { if (e.key === " ") this.space = false; });
    this.undoS = []; this.redoS = []; this.fit(); setTimeout(() => this.fit(), 0);
  });
  const P = ME.prototype;

  // ---------------------------------------------------------------- toolbar + palette
  P.buildBar = function () {
    const h = this.h, s = this.s, tip = TOOLS.find(t => t[0] === s.tool);
    const tb = (label, title, on, fn) => h("button", { class: "tbtn sm" + (on ? " on" : ""), title, text: label, onclick: fn });
    const sel = (title, opts, val, fn) => h("select", { class: "sel", title, onchange: e => fn(e.target.value) }, ...opts.map(([v, l]) => h("option", { value: v, text: l, selected: String(v) === String(val) })));
    const opt = [];
    if (s.tool === "height") opt.push(h("span", { class: "mlab", text: "Height" }), h("input", { type: "range", min: 0, max: 15, step: 1, value: s.hgt, title: "Floor height in 16ths of a story (8 units each)", oninput: e => { s.hgt = +e.target.value; this.buildBar(); } }), h("span", { class: "chip", text: s.hgt + "/16" }));
    if (s.tool === "slope" || s.tool === "stairs") opt.push(sel("Rises toward", DIRS.map((d, i) => [i, "rises " + d]), s.dir, v => { s.dir = +v; this.redraw(); }), sel("How much it rises", RISES, s.rise, v => { s.rise = +v; }),
      sel("Where it starts (its low end)", [[-1, "starts: automatic (where the floor behind it ends)"]].concat(Array.from({ length: 16 }, (_, i) => [i, "starts at " + i + "/16" + (i === 0 ? " (the floor)" : i === 8 ? " (half a story up)" : "")])), s.base, v => { s.base = +v; }));
    if (s.tool === "light") opt.push(h("span", { class: "mlab", text: "Light" }), h("input", { type: "range", min: 0, max: 100, step: 5, value: s.light, oninput: e => { s.light = +e.target.value; this.buildBar(); } }), h("span", { class: "chip", text: s.light + "%" }));
    if (s.tool === "diag") opt.push(tb("⟳ Turn (D)", "Turn the diagonal by 90°", false, () => this.turn()), tb("⇋ Mirror (M)", "Mirror the diagonal", s.flip, () => this.mirror()));
    if (s.tool === "object" || s.tool === "start") opt.push(tb("⟳ Turn (D)", "Turn the selected object / the start by 90°", false, () => this.turn()));
    const L = s.L, nL = this.m.layers.length;
    this.bar.replaceChildren(
      tb("↶", "Undo (Ctrl+Z)", false, () => this.history(true)), tb("↷", "Redo (Ctrl+Y)", false, () => this.history(false)), h("span", { class: "msep" }),
      tb("Select", "Select (V): click or drag a box over cells; drag objects to move them", s.tool === "select", () => this.setTool("select")), tb("Pick texture", "Pick (I): click a cell to take its texture (Alt+click works with any tool)", s.tool === "pick", () => this.setTool("pick")), h("span", { class: "msep" }),
      h("span", { class: "mlab", text: "Story" }), tb("▼", "Go down a story (Page Down)", false, () => this.setLayer(L - 1)), h("span", { class: "chip", title: "Story (layer) you are editing. Each story is 2 m tall.", text: (L + 1) + " / " + Math.max(nL, L + 1) }), tb("▲", "Go up a story (Page Up)", false, () => this.setLayer(L + 1)),
      h("label", { class: "mlab", title: "Show the story below, faded" }, h("input", Object.assign({ type: "checkbox", onchange: e => { s.below = e.target.checked; this.redraw(); } }, s.below ? { checked: "" } : {})), " below"),
      h("label", { class: "mlab", title: "Mark cells that have a ceiling" }, h("input", Object.assign({ type: "checkbox", onchange: e => { s.ceilShow = e.target.checked; this.redraw(); } }, s.ceilShow ? { checked: "" } : {})), " ceilings"),
      tb("Fit", "Show the whole map (Home)", false, () => this.fit()), ...(opt.length ? [h("span", { class: "msep" }), ...opt] : []),
      h("span", { class: "hint", text: tip ? tip[1] + " (" + tip[2] + "): " + tip[3] : "" }));
  };
  // a small picture of a building piece / object type / diagonal shape
  P.icon = function (kind, shape) {
    const c = document.createElement("canvas"), d = 2, S = 26; c.width = c.height = S * d; c.className = "mpico"; const x = c.getContext("2d"); x.scale(d, d);
    x.fillStyle = "#1b222b"; x.fillRect(0, 0, S, S); const wall = "#9aa3b0", flo = "#4f6070", acc = "#ffd84a";
    const box = (a, b, w, hh, col) => { x.fillStyle = col; x.fillRect(a, b, w, hh); };
    if (kind === "diag") {
      const cells = G.shapeCells(shape || this.s.shape, this.s.rot, this.s.flip), xs = cells.map(q => q.dx), ys = cells.map(q => q.dy), n = Math.max(Math.max(...xs) - Math.min(...xs), Math.max(...ys) - Math.min(...ys)) + 1, cs = (S - 4) / n, ox = 2 - Math.min(...xs) * cs, oy = 2 - Math.min(...ys) * cs;
      x.strokeStyle = "rgba(255,255,255,.18)"; for (const q of cells) x.strokeRect(ox + q.dx * cs, oy + q.dy * cs, cs, cs);
      x.fillStyle = wall; for (const q of cells) { x.beginPath(); q.poly.forEach(([u, v], i) => (i ? x.lineTo : x.moveTo).call(x, ox + (q.dx + u) * cs, oy + (q.dy + v) * cs)); x.closePath(); x.fill(); }
      return c;
    }
    const draw = {
      wall: () => box(4, 4, 18, 18, wall), floor: () => box(3, 3, 20, 20, flo),
      ceil: () => { box(3, 3, 20, 20, "#2a3542"); x.fillStyle = "rgba(120,220,255,.8)"; x.beginPath(); x.moveTo(23, 3); x.lineTo(13, 3); x.lineTo(23, 13); x.fill(); },
      room: () => { box(2, 2, 22, 22, wall); box(6, 6, 14, 14, flo); }, height: () => { box(3, 15, 20, 8, flo); box(9, 9, 14, 6, "#7d93a6"); },
      slope: () => { x.fillStyle = flo; x.beginPath(); x.moveTo(3, 23); x.lineTo(23, 7); x.lineTo(23, 23); x.fill(); },
      stairs: () => { x.fillStyle = flo; for (let i = 0; i < 4; i++) x.fillRect(3 + i * 5, 23 - (i + 1) * 4, 5, (i + 1) * 4); },
      light: () => { x.fillStyle = acc; x.beginPath(); x.arc(13, 11, 6, 0, 7); x.fill(); box(10, 18, 6, 4, acc); },
      erase: () => { x.strokeStyle = "#ff6a5a"; x.lineWidth = 3; x.beginPath(); x.moveTo(6, 6); x.lineTo(20, 20); x.moveTo(20, 6); x.lineTo(6, 20); x.stroke(); },
      start: () => { x.fillStyle = "#3dff7a"; x.beginPath(); x.moveTo(13, 3); x.lineTo(22, 22); x.lineTo(4, 22); x.fill(); },
      door: () => box(3, 11, 20, 5, "#d9a35a"), enemy: () => { x.fillStyle = "#ff5a4a"; x.beginPath(); x.arc(13, 13, 8, 0, 7); x.fill(); },
      pickup: () => { x.fillStyle = acc; x.beginPath(); x.moveTo(13, 4); x.lineTo(22, 13); x.lineTo(13, 22); x.lineTo(4, 13); x.fill(); },
      exit: () => { box(5, 4, 16, 18, "#5aff8a"); box(9, 8, 8, 14, "#1b222b"); },
      path: () => { x.strokeStyle = "#5ad1ff"; x.lineWidth = 2; x.setLineDash([]); x.beginPath(); x.moveTo(5, 20); x.lineTo(12, 8); x.lineTo(21, 15); x.stroke(); x.fillStyle = "#5ad1ff"; for (const [a, b] of [[5, 20], [12, 8], [21, 15]]) { x.beginPath(); x.arc(a, b, 2.6, 0, 7); x.fill(); } },
      zonetrigger: () => { x.fillStyle = "rgba(255,190,60,.35)"; x.fillRect(3, 3, 20, 20); x.strokeStyle = "#ffbe3c"; x.setLineDash([3, 2]); x.lineWidth = 1.5; x.strokeRect(3.5, 3.5, 19, 19); x.fillStyle = "#ffbe3c"; x.beginPath(); x.moveTo(14, 6); x.lineTo(9, 14); x.lineTo(13, 14); x.lineTo(11, 20); x.lineTo(17, 11); x.lineTo(13, 11); x.fill(); },
      zonecustom: () => { x.fillStyle = "rgba(220,90,255,.3)"; x.fillRect(3, 3, 20, 20); x.strokeStyle = "#dc5aff"; x.setLineDash([3, 2]); x.lineWidth = 1.5; x.strokeRect(3.5, 3.5, 19, 19); x.setLineDash([]); x.fillStyle = "#dc5aff"; for (const [a, b] of [[8, 9], [17, 9], [12, 17]]) { x.beginPath(); x.arc(a, b, 2.6, 0, 7); x.fill(); } x.strokeStyle = "#dc5aff"; x.beginPath(); x.moveTo(8, 9); x.lineTo(17, 9); x.lineTo(12, 17); x.stroke(); }, npc: () => { x.fillStyle = "#b48cff"; x.beginPath(); x.arc(13, 9, 5, 0, 7); x.fill(); box(7, 15, 12, 8, "#b48cff"); },
    };
    (draw[kind] || (() => box(7, 7, 12, 12, "#b8c4d4")))();
    return c;
  };
  // the side panel: "Rooms" (building pieces + textures) and "Objects" (the player start + every placeable object)
  P.buildPal = function () {
    const h = this.h, s = this.s, imgs = (this.o.project.assets || []).filter(a => a.type === "image"), urls = this.urls();
    const tabs = h("div", { class: "mptabs" }, ...[["rooms", "Rooms"], ["objects", "Objects"], ["zones", "Zones"]].map(([k, l]) => h("button", { class: "mptab" + (s.panel === k ? " on" : ""), text: l, onclick: () => { s.panel = k; this.buildPal(); } })));
    const piece = (tool, label, key) => h("button", { class: "mpiece" + (s.tool === tool ? " on" : ""), title: label + " (" + key + ")", onclick: () => this.setTool(tool) }, this.icon(tool), h("span", { text: label }));
    const out = [tabs];
    if (s.panel === "zones") {
      const zp = (kind, label, tip) => h("button", { class: "mpiece wide" + (s.tool === "zone" && s.zkind === kind ? " on" : ""), title: tip, onclick: () => { s.zkind = kind; this.setTool("zone"); } }, this.icon("zone" + kind), h("span", { class: "msn", text: label }));
      const zs = (this.m.zones || []).slice().sort((a, b) => (a.layer || 0) - (b.layer || 0));
      out.push(zp("trigger", "Trigger zone", "Ready-made actions when the player walks in or out: set a tag, send a message, sound, text, damage, dialogue, teleport…"), zp("custom", "Custom zone", "Its own Node Coder script: “When the player enters / leaves / is inside” and any action."),
        h("div", { class: "note", text: "Pick a kind, then drag a box over cells in the map. Zones are invisible in the game." }), h("div", { class: "mph", text: "Zones in this map" }),
        ...zs.map(z => h("button", { class: "mpiece wide" + (this.sel && this.sel.kind === "zone" && this.sel.id === z.id ? " on" : ""), title: "Select (story " + ((z.layer || 0) + 1) + ")", onclick: () => { if ((z.layer || 0) !== s.L) this.setLayer(z.layer || 0); this.select({ kind: "zone", id: z.id }); this.buildPal(); } }, this.icon("zone" + z.type), h("span", { class: "msn", text: this.T.get(this.m.id + ".z." + z.id + ".name") || z.id }), h("span", { class: "mpty", text: "story " + ((z.layer || 0) + 1) }))),
        zs.length ? "" : h("div", { class: "note", text: "No zones yet." }));
    } else if (s.panel === "objects") {
      const props = (this.o.getProps && this.o.getProps()) || [], cur = this.o.getProp && this.o.getProp();
      out.push(h("button", { class: "mpiece wide" + (s.tool === "start" ? " on" : ""), title: "Player start (P)", onclick: () => this.setTool("start") }, this.icon("start"), h("span", { text: "Player start" })),
        h("button", { class: "mpiece wide" + (s.tool === "path" ? " on" : ""), title: "Path (N): patrol routes for enemies", onclick: () => this.setTool("path") }, this.icon("path"), h("span", { text: "Path (patrol route)" })), h("div", { class: "mph", text: "Objects" }),
        ...props.map(p => h("button", { class: "mpiece wide" + (s.tool === "object" && cur === p.id ? " on" : ""), title: p.name + " (" + p.type + "): click it, then click in the map to place it", onclick: () => { if (this.o.setProp) this.o.setProp(p.id); this.setTool("object"); } }, this.icon(p.type), h("span", { class: "msn", text: p.name }), h("span", { class: "mpty", text: p.type }))),
        props.length ? "" : h("div", { class: "note", text: "No objects yet. Make doors, enemies, pick-ups and more in the Object Editor tab." }));
    } else {
      out.push(h("div", { class: "mpgrid" }, piece("wall", "Wall", "W"), piece("diag", "Diagonal", "G"), piece("floor", "Floor", "F"), piece("ceil", "Ceiling", "C"), piece("room", "Room", "R"), piece("height", "Height", "H"), piece("slope", "Slope", "S"), piece("stairs", "Stairs", "T"), piece("light", "Light", "L"), piece("erase", "Erase", "X")));
      if (s.tool === "diag") out.push(h("div", { class: "mph", text: "Diagonal shape" }),
        h("div", { class: "mpshapes" }, ...G.SHAPES.map(sh => h("button", { class: "mpshape" + (s.shape === sh.id ? " on" : ""), title: sh.name, onclick: () => { s.shape = sh.id; this.buildPal(); this.redraw(); } }, this.icon("diag", sh.id)))),
        h("div", { class: "mpsub", text: (G.SHAPES.find(q => q.id === s.shape) || {}).name || "" }),
        h("div", { class: "btnrow" }, h("button", { class: "btn sm", text: "⟳ Turn", title: "D", onclick: () => this.turn() }), h("button", { class: "btn sm", text: "⇋ Mirror", title: "M", onclick: () => this.mirror() })));
      const sw = (id, name, url) => h("button", { class: "mswatch" + (s.tex === id ? " on" : ""), title: name, onclick: () => { s.tex = id; this.buildPal(); if (["select", "erase", "pick", "object", "start", "height", "light", "slope", "stairs"].includes(s.tool)) this.setTool("wall"); } },
        url ? h("img", { src: url, alt: "", draggable: "false" }) : h("span", { class: "msdef" }), h("span", { class: "msn", text: name }));
      out.push(h("div", { class: "mph", text: "Texture" }), sw("", "Default", ""), ...imgs.map(a => sw(a.id, a.name.replace(/\.[^.]+$/, ""), urls[a.id])),
        imgs.length ? "" : h("div", { class: "note", text: "Import images (Assets > Import Files) to use them as textures." }));
    }
    this.pal.replaceChildren(...out);
  };
  P.mirror = function () { if (this.s.tool !== "diag") return; this.s.flip = !this.s.flip; this.buildBar(); this.buildPal(); this.redraw(); };
  P.setTool = function (t) { this.s.tool = t; this.drag = null; if (t !== "path") this.pathOpen = null; if (t === "object" || t === "start" || t === "path") this.s.panel = "objects"; else if (t === "zone") this.s.panel = "zones"; else if (t !== "select" && t !== "pick") this.s.panel = "rooms"; this.buildBar(); this.buildPal(); this.redraw(); };
  P.setLayer = function (L) { L = Math.max(0, Math.min(63, L)); if (L > this.m.layers.length) L = this.m.layers.length; this.s.L = L; this.sel = this.sel && this.sel.kind === "cells" ? null : this.sel; this.buildBar(); this.redraw(); this.emitSel(); };
  P.destroy = function () { this.ro.disconnect(); this.unsub(); };
  P.urls = function () { return (this.o.getUrls ? this.o.getUrls() : this.o.urls) || {}; };
  P.img = function (id) { const u = this.urls()[id]; if (!id || !u) return null; let e = this.imgs[id]; if (!e || e.__u !== u) { e = this.imgs[id] = new Image(); e.__u = u; e.onload = () => this.redraw(); e.src = u; } return e.complete && e.naturalWidth ? e : null; };

  // ---------------------------------------------------------------- undo / redo (cells, objects and the start)
  const START = ["startX", "startY", "startLayer", "startAngle"];
  P.snap = function () { const m = this.m, o = {}; for (const k of START) o[k] = this.T.get(m.id + "." + k); return JSON.stringify({ layers: m.layers, things: m.things, n: m.n, zones: m.zones || [], paths: m.paths || [], start: o }); };
  P.save = function () { this.undoS.push(this.snap()); if (this.undoS.length > 100) this.undoS.shift(); this.redoS = []; };
  P.restore = function (s) { const d = JSON.parse(s), m = this.m; m.layers = d.layers; m.things = d.things; m.n = d.n; m.zones = d.zones || []; m.paths = d.paths || []; if (PRIM.Schema && this.o.project) PRIM.Schema.ensureZones(this.o.project); const o = {}; for (const k of START) if (d.start[k] !== undefined) o[m.id + "." + k] = d.start[k]; this.T.setMany(o); };
  P.history = function (back) {
    const from = back ? this.undoS : this.redoS, to = back ? this.redoS : this.undoS, s = from.pop(); if (!s) { if (this.o.log) this.o.log(back ? "Nothing to undo." : "Nothing to redo."); return; }
    to.push(this.snap()); this.restore(s); this.sel = null; this.changed(true); this.emitSel();
  };
  P.changed = function (structure) { this.redraw(); if (this.o.onChange) this.o.onChange(true, structure); };

  // ---------------------------------------------------------------- view
  P.W = function () { return this.cv.width / this.dpr; }; P.H = function () { return this.cv.height / this.dpr; };
  P.sx = function (X) { return (X - this.v.cx) * this.v.z + this.W() / 2; }; P.sy = function (Z) { return (Z - this.v.cy) * this.v.z + this.H() / 2; };
  P.wx = function (sx) { return (sx - this.W() / 2) / this.v.z + this.v.cx; }; P.wy = function (sy) { return (sy - this.H() / 2) / this.v.z + this.v.cy; };
  P.fit = function () {
    const b = G.bounds(this.m), w = this.cv.clientWidth || 600, hh = this.cv.clientHeight || 400;
    if (!b) { this.v = { cx: this.m.w * C / 2, cy: this.m.h * C / 2, z: 0.25 }; return this.redraw(); }
    const x0 = b.x0 * C, x1 = (b.x1 + 1) * C, y0 = b.y0 * C, y1 = (b.y1 + 1) * C;
    this.v = { cx: (x0 + x1) / 2, cy: (y0 + y1) / 2, z: Math.max(0.03, Math.min(1.2, 0.85 * Math.min(w / Math.max(C * 4, x1 - x0), hh / Math.max(C * 4, y1 - y0)))) }; this.redraw();
  };
  P.wheel = function (e) {
    e.preventDefault(); const [px, py] = this.rel(e), X = this.wx(px), Z = this.wy(py); this.v.z = Math.max(0.03, Math.min(2, this.v.z * (e.deltaY > 0 ? 0.87 : 1.15)));
    this.v.cx = X - (px - this.W() / 2) / this.v.z; this.v.cy = Z - (py - this.H() / 2) / this.v.z; this.redraw();
  };
  P.rel = function (e) { const r = this.cv.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; };
  P.cellAt = function (px, py) { return [Math.floor(this.wx(px) / C), Math.floor(this.wy(py) / C)]; };

  // ---------------------------------------------------------------- painting cells
  // apply one tool to one cell of the current story; erase = the right-mouse version
  P.paintCell = function (x, y, erase) {
    const s = this.s, L = s.L, m = this.m, t = s.tool, c = G.edit(m, L, x, y);
    if (t === "wall") { if (erase) { delete c.w; delete c.ws; } else { c.w = s.tex; delete c.ws; delete c.dg; delete c.dt; } }
    else if (t === "floor") { if (erase) { delete c.f; delete c.h; delete c.sh; delete c.sd; delete c.sr; } else { c.f = s.tex; delete c.w; delete c.ws; } }
    else if (t === "ceil") { if (erase) delete c.c; else { c.c = s.tex; delete c.w; delete c.ws; } }
    else if (t === "height") { if (c.f === undefined && !erase) c.f = s.tex; if (erase) delete c.h; else { c.h = s.hgt; delete c.w; } }
    else if (t === "slope" || t === "stairs") { if (erase) { delete c.sh; delete c.sd; delete c.sr; } else { if (c.f === undefined) c.f = s.tex; delete c.w; c.sh = t; c.sd = s.dir; c.sr = s.rise; const h0 = s.base >= 0 ? s.base : this.autoBase(x, y, s.dir); if (h0) c.h = h0; else delete c.h; } }
    else if (t === "light") { if (erase || s.light >= 100) delete c.li; else c.li = s.light; }
    else if (t === "erase") { for (const k of Object.keys(c)) delete c[k]; }
    else if (t === "diag") { if (erase) { delete c.dg; delete c.dt; } }
    G.tidy(m, L, x, y);
  };
  // where a ramp / stairs rising toward dir should start: the height of the floor behind it, at the shared edge (so ramps drawn one after another keep climbing)
  P.autoBase = function (x, y, dir) {
    const L = this.s.L, b = G.cell(this.m, L, x - G.DIRS[dir][0], y - G.DIRS[dir][1]); if (!G.hasFloor(b)) return 0;
    const e = [[0.5, 0.001], [0.999, 0.5], [0.5, 0.999], [0.001, 0.5]][dir], hgt = G.hAt(b, e[0], e[1]); return Math.max(0, Math.min(15, Math.round(hgt / G.STEP)));
  };
  // a diagonal shape with its first cell at x, y
  P.placeDiag = function (x, y) {
    const s = this.s; for (const q of G.shapeCells(s.shape, s.rot, s.flip)) { const c = G.edit(this.m, s.L, x + q.dx, y + q.dy); delete c.w; delete c.ws; c.dg = q.poly; c.dt = s.tex; }
  };
  P.room = function (a, b, erase) {
    const s = this.s, L = s.L, m = this.m, x0 = Math.min(a[0], b[0]), x1 = Math.max(a[0], b[0]), y0 = Math.min(a[1], b[1]), y1 = Math.max(a[1], b[1]);
    for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) {
      if (erase) { const c = G.cell(m, L, x, y); if (c) { for (const k of Object.keys(c)) delete c[k]; G.tidy(m, L, x, y); } continue; }
      const edgeC = x === x0 || x === x1 || y === y0 || y === y1, c = G.edit(m, L, x, y);
      if (edgeC && x1 - x0 >= 2 && y1 - y0 >= 2) { if (c.f === undefined && c.w === undefined) c.w = s.tex; }
      else { delete c.w; delete c.ws; if (c.f === undefined) c.f = s.tex; if (c.c === undefined) c.c = s.tex; }
    }
  };
  P.pickTex = function (x, y) {
    const c = G.cell(this.m, this.s.L, x, y); if (!c) return; const tex = c.w !== undefined ? c.w : G.isDiag(c) ? c.dt : c.f !== undefined ? c.f : c.c; if (tex === undefined) return;
    this.s.tex = tex; if (this.s.tool === "pick" || this.s.tool === "select") this.s.tool = c.w !== undefined ? "wall" : "floor"; this.buildPal(); this.buildBar(); this.redraw();
  };

  // ---------------------------------------------------------------- objects and the start
  P.things = function () { return (this.m.things || []).filter(t => (t.layer || 0) === this.s.L); };
  P.thingAt = function (px, py) {
    let best = null, bd = 1e9; const X = this.wx(px), Z = this.wy(py);
    for (const t of this.things()) {
      if (this.T.get(t.prop + ".type") === "door") { const f = G.thingFrame(this.m, this.T, t); if (f.cells.some(([cx, cy]) => Math.floor(X / C) === cx && Math.floor(Z / C) === cy)) { const d = Math.hypot(X - f.cx, Z - f.cy) * this.v.z; if (d < bd) { bd = d; best = t; } } continue; }
      const d = Math.hypot(this.sx(t.x) - px, this.sy(t.y) - py), r = Math.max(9, 0.18 * C * this.v.z); if (d < r && d < bd) { bd = d; best = t; }
    }
    return best;
  };
  P.startAt = function (px, py) { const T = this.T, m = this.m; if ((+T.get(m.id + ".startLayer") || 0) !== this.s.L) return false; return Math.hypot(this.sx(+T.get(m.id + ".startX")) - px, this.sy(+T.get(m.id + ".startY")) - py) < Math.max(10, 0.2 * C * this.v.z); };
  const snap16 = v => Math.round(v / 16) * 16;
  P.place = function (px, py) {
    const pid = this.o.getProp && this.o.getProp(); if (!pid) { if (this.o.log) this.o.log("Make an object in the Object Editor tab first, then pick it in the toolbar.", "warn"); return null; }
    this.save(); const m = this.m; m.things = m.things || []; m.n = m.n || { t: 0 }; const isDoor = this.T.get(pid + ".type") === "door", X = this.wx(px), Z = this.wy(py);
    const th = { id: "t" + (++m.n.t), prop: pid, x: isDoor ? (Math.floor(X / C) + 0.5) * C : snap16(X), y: isDoor ? (Math.floor(Z / C) + 0.5) * C : snap16(Z), ang: 0, layer: this.s.L };
    if (isDoor) { const L = this.s.L, cx = Math.floor(X / C), cy = Math.floor(Z / C); if (G.isWall(G.cell(m, L, cx - 1, cy)) || G.isWall(G.cell(m, L, cx + 1, cy))) th.ang = 0; else if (G.isWall(G.cell(m, L, cx, cy - 1)) || G.isWall(G.cell(m, L, cx, cy + 1))) th.ang = 90; }
    m.things.push(th); this.sel = { kind: "thing", id: th.id }; this.changed(true); this.emitSel(); return th;
  };
  P.turn = function () {
    const sel = this.sel, m = this.m, t = this.s.tool;
    // a building tool turns its piece, even when an object is still selected from placing it (owner bug report)
    if (t === "slope" || t === "stairs") { this.s.dir = (this.s.dir + 1) % 4; this.buildBar(); this.redraw(); return; }
    if (t === "diag") { this.s.rot = (this.s.rot + 1) % 4; this.buildPal(); this.redraw(); return; }
    if (sel && sel.kind === "thing" && (t === "select" || t === "object")) { const th = (m.things || []).find(t => t.id === sel.id); if (th) { this.save(); th.ang = ((th.ang || 0) + 90) % 360; this.changed(false); this.emitSel(); } return; }
    if (t === "start" || (sel && sel.kind === "start" && t === "select")) { this.save(); this.T.set(m.id + ".startAngle", ((+this.T.get(m.id + ".startAngle") || 0) + 90) % 360); this.changed(false); return; }
  };
  P.setStart = function (px, py) { this.save(); const m = this.m; this.T.setMany({ [m.id + ".startX"]: snap16(this.wx(px)), [m.id + ".startY"]: snap16(this.wy(py)), [m.id + ".startLayer"]: this.s.L }); this.sel = { kind: "start" }; this.changed(false); this.emitSel(); };

  // ---------------------------------------------------------------- selection (used by the Inspector)
  P.emitSel = function () { if (this.o.onSelect) this.o.onSelect(this.sel); };
  P.select = function (sel, quiet) { this.sel = sel; this.redraw(); if (!quiet) this.emitSel(); };
  P.selCells = function () { const s = this.sel; if (!s || s.kind !== "cells") return []; const out = []; for (let y = Math.min(s.y0, s.y1); y <= Math.max(s.y0, s.y1); y++) for (let x = Math.min(s.x0, s.x1); x <= Math.max(s.x0, s.x1); x++) out.push([x, y]); return out; };
  // change every selected cell: fn(cell, x, y) edits the cell (it is created when missing, and removed again when left empty)
  P.applyCells = function (fn) { const list = this.selCells(); if (!list.length) return; this.save(); for (const [x, y] of list) { fn(G.edit(this.m, this.s.L, x, y), x, y); G.tidy(this.m, this.s.L, x, y); } this.changed(false); };
  P.deleteSelection = function () {
    const s = this.sel, m = this.m; if (!s) return false;
    if (s.kind === "thing") { this.save(); m.things = (m.things || []).filter(t => t.id !== s.id); this.sel = null; this.changed(true); this.emitSel(); return true; }
    if (s.kind === "cells") { this.applyCells(c => { for (const k of Object.keys(c)) delete c[k]; }); return true; }
    if (s.kind === "path") { this.save(); m.paths = (m.paths || []).filter(q => q.id !== s.id); for (const t of m.things || []) if (t.path === s.id) delete t.path; this.pathOpen = null; this.sel = null; this.changed(true); this.emitSel(); return true; }
    if (s.kind === "zone") { const z = (m.zones || []).find(q => q.id === s.id); if (!z) return false; this.save(); PRIM.Schema.removeZone(this.o.project, m, z); this.sel = null; this.changed(true); this.emitSel(); this.buildPal(); return true; }
    return false;
  };
  // copy / paste a box of cells (Ctrl+C / Ctrl+V at the mouse)
  P.copy = function () { const list = this.selCells(); if (!list.length) return false; const s = this.sel, x0 = Math.min(s.x0, s.x1), y0 = Math.min(s.y0, s.y1); ME.clip = list.map(([x, y]) => [x - x0, y - y0, JSON.parse(JSON.stringify(G.cell(this.m, this.s.L, x, y) || {}))]); ME.clipW = Math.abs(s.x1 - s.x0); ME.clipH = Math.abs(s.y1 - s.y0); if (this.o.log) this.o.log("Copied " + list.length + " cell(s). Ctrl+V pastes them where the mouse is."); return true; };
  P.paste = function (cx, cy) {
    if (!ME.clip) return; this.save(); const L = this.s.L; for (const [dx, dy, c] of ME.clip) { const l = (G.edit(this.m, L, cx + dx, cy + dy), this.m.layers[L]); l.cells[(cx + dx) + "," + (cy + dy)] = JSON.parse(JSON.stringify(c)); G.tidy(this.m, L, cx + dx, cy + dy); }
    this.sel = { kind: "cells", x0: cx, y0: cy, x1: cx + ME.clipW, y1: cy + ME.clipH }; this.changed(false); this.emitSel();
  };
  P.duplicate = function () { if (!this.copy()) return; const s = this.sel; this.paste(Math.max(s.x0, s.x1) + 1, Math.min(s.y0, s.y1)); };

  // ---------------------------------------------------------------- mouse
  P.down = function (e) {
    this.wrap.focus({ preventScroll: true }); const [px, py] = this.rel(e), s = this.s, [cx, cy] = this.cellAt(px, py); this.cv.setPointerCapture(e.pointerId);
    if (e.button === 1 || (e.button === 0 && this.space)) { this.drag = { kind: "pan", px, py, cx: this.v.cx, cy: this.v.cy }; return; }
    if (e.button === 0 && e.altKey) { this.pickTex(cx, cy); return; }
    const erase = e.button === 2;
    if (s.tool === "pick") { this.pickTex(cx, cy); return; }
    if (s.tool === "select" || s.tool === "object") {
      const th = this.thingAt(px, py); if (th) { this.save(); this.sel = { kind: "thing", id: th.id }; this.drag = { kind: "thing", th, ox: th.x - this.wx(px), oy: th.y - this.wy(py), moved: false }; this.redraw(); this.emitSel(); return; }
      if (this.startAt(px, py)) { this.save(); this.sel = { kind: "start" }; this.drag = { kind: "start" }; this.redraw(); this.emitSel(); return; }
      if (s.tool === "object") { if (!erase) { const t = this.place(px, py); if (t) { this.drag = { kind: "thing", th: t, ox: t.x - this.wx(px), oy: t.y - this.wy(py), moved: true, fresh: true }; } } return; }
      this.sel = { kind: "cells", x0: cx, y0: cy, x1: cx, y1: cy }; this.drag = { kind: "box", a: [cx, cy] }; this.redraw(); return;
    }
    if (s.tool === "start") { this.setStart(px, py); this.drag = { kind: "start" }; return; }
    if (s.tool === "path") { const hp = this.pathPointAt(px, py);
      if (erase) { this.pathOpen = null; if (hp && hp.i >= 0 && hp.pa.pts.length > 2) { this.save(); hp.pa.pts.splice(hp.i, 1); this.changed(false); } this.redraw(); this.emitSel(); return; }
      if (hp) { this.save(); this.sel = { kind: "path", id: hp.pa.id }; this.drag = { kind: "pmove", pa: hp.pa, i: hp.i, moved: false }; this.redraw(); this.emitSel(); return; }
      const P0 = [(Math.floor(this.wx(px) / C) + 0.5) * C, (Math.floor(this.wy(py) / C) + 0.5) * C], m = this.m; this.save(); m.paths = m.paths || [];
      let pa = this.pathOpen && m.paths.find(q => q.id === this.pathOpen);
      if (!pa) { let n = 1; while (m.paths.some(q => q.id === "path" + n)) n++; pa = { id: "path" + n, pts: [], loop: true, layer: s.L }; m.paths.push(pa); this.pathOpen = pa.id; }
      pa.pts.push(P0);
      // a path started on top of an enemy that walks no path yet: that enemy walks it
      if (pa.pts.length === 1 && this.o.project) for (const th of m.things || []) { if (th.path || (th.layer || 0) !== s.L || Math.floor(th.x / C) !== Math.floor(P0[0] / C) || Math.floor(th.y / C) !== Math.floor(P0[1] / C)) continue; const pr = (this.o.project.props || []).find(q => q.id === th.prop); if (pr && this.T.get(pr.id + ".type") === "enemy") { th.path = pa.id; break; } }
      this.sel = { kind: "path", id: pa.id }; this.changed(pa.pts.length === 1); this.emitSel(); return; }
    if (s.tool === "zone") { const z = this.zoneAt(cx, cy); if (erase) { if (z) { this.sel = { kind: "zone", id: z.id }; this.deleteSelection(); } return; }
      if (z) { this.save(); this.sel = { kind: "zone", id: z.id }; this.drag = { kind: "zmove", z, a: [cx, cy], o: [z.x0, z.y0, z.x1, z.y1], moved: false }; this.redraw(); this.emitSel(); this.buildPal(); return; }
      this.drag = { kind: "zbox", a: [cx, cy], b: [cx, cy] }; this.redraw(); return; }
    if (s.tool === "room") { this.drag = { kind: "room", a: [cx, cy], b: [cx, cy], erase }; this.redraw(); return; }
    if (s.tool === "diag" && !erase) { this.save(); this.placeDiag(cx, cy); this.changed(false); return; }
    this.save(); this.drag = { kind: "paint", erase, done: new Set() }; this.paintAt(cx, cy);
  };
  P.paintAt = function (cx, cy) { const d = this.drag, k = cx + "," + cy; if (d.done.has(k)) return; d.done.add(k); this.paintCell(cx, cy, d.erase); this.redraw(); };
  P.move = function (e) {
    const [px, py] = this.rel(e), [cx, cy] = this.cellAt(px, py), d = this.drag; this.hover = [cx, cy]; this.mouse = [px, py];
    if (!d) return this.redraw();
    if (d.kind === "pan") { this.v.cx = d.cx - (px - d.px) / this.v.z; this.v.cy = d.cy - (py - d.py) / this.v.z; return this.redraw(); }
    if (d.kind === "paint") { const l = d.last || [cx, cy], n = Math.max(Math.abs(cx - l[0]), Math.abs(cy - l[1])); for (let i = 1; i <= n; i++) this.paintAt(Math.round(l[0] + (cx - l[0]) * i / n), Math.round(l[1] + (cy - l[1]) * i / n)); this.paintAt(cx, cy); d.last = [cx, cy]; return; }
    if (d.kind === "pmove") { const P0 = [(Math.floor(this.wx(px) / C) + 0.5) * C, (Math.floor(this.wy(py) / C) + 0.5) * C], q = d.pa.pts[d.i]; if (q[0] !== P0[0] || q[1] !== P0[1]) { d.pa.pts[d.i] = P0; d.moved = true; } return this.redraw(); }
    if (d.kind === "room" || d.kind === "zbox") { d.b = [cx, cy]; return this.redraw(); }
    if (d.kind === "zmove") { const dx = cx - d.a[0], dy = cy - d.a[1]; d.z.x0 = d.o[0] + dx; d.z.y0 = d.o[1] + dy; d.z.x1 = d.o[2] + dx; d.z.y1 = d.o[3] + dy; if (dx || dy) d.moved = true; return this.redraw(); }
    if (d.kind === "box") { this.sel.x1 = cx; this.sel.y1 = cy; return this.redraw(); }
    if (d.kind === "thing") { const isDoor = this.T.get(d.th.prop + ".type") === "door", X = this.wx(px) + d.ox, Z = this.wy(py) + d.oy; d.th.x = isDoor ? (Math.floor(X / C) + 0.5) * C : snap16(X); d.th.y = isDoor ? (Math.floor(Z / C) + 0.5) * C : snap16(Z); d.moved = true; return this.redraw(); }
    if (d.kind === "start") { const m = this.m; this.T.setMany({ [m.id + ".startX"]: snap16(this.wx(px)), [m.id + ".startY"]: snap16(this.wy(py)) }); d.moved = true; }
  };
  P.up = function (e) {
    const d = this.drag; this.drag = null; if (!d) return; try { this.cv.releasePointerCapture(e.pointerId); } catch (x) {}
    if (d.kind === "paint") { if (d.done.size) this.changed(false); else this.undoS.pop(); }
    else if (d.kind === "room") { this.save(); this.room(d.a, d.b, d.erase); this.changed(false); }
    else if (d.kind === "zbox") { this.save(); const z = PRIM.Schema.addZone(this.o.project, this.m, { type: this.s.zkind || "trigger", x0: Math.min(d.a[0], d.b[0]), y0: Math.min(d.a[1], d.b[1]), x1: Math.max(d.a[0], d.b[0]), y1: Math.max(d.a[1], d.b[1]), layer: this.s.L }); this.sel = { kind: "zone", id: z.id }; this.changed(true); this.emitSel(); this.buildPal(); }
    else if (d.kind === "pmove") { if (!d.moved) this.undoS.pop(); this.changed(false); this.emitSel(); }
    else if (d.kind === "zmove") { if (!d.moved) this.undoS.pop(); this.changed(false); this.emitSel(); }
    else if (d.kind === "box") this.emitSel();
    else if (d.kind === "thing") { if (!d.moved) this.undoS.pop(); this.changed(false); this.emitSel(); }
    else if (d.kind === "start") { this.changed(false); this.emitSel(); }
    this.redraw();
  };
  P.key = function (e) {
    if (/INPUT|SELECT|TEXTAREA/.test(e.target.tagName)) return; const k = e.key.toLowerCase(), mod = e.ctrlKey || e.metaKey;
    if (k === " ") { this.space = true; e.preventDefault(); return; }
    if (mod && k === "z") { e.preventDefault(); this.history(!e.shiftKey); return; } if (mod && k === "y") { e.preventDefault(); this.history(false); return; }
    if (mod && k === "c") { e.preventDefault(); this.copy(); return; } if (mod && k === "v") { e.preventDefault(); if (this.hover) this.paste(this.hover[0], this.hover[1]); return; }
    if (mod && k === "a") { e.preventDefault(); const b = G.bounds(this.m); if (b) { this.sel = { kind: "cells", x0: b.x0, y0: b.y0, x1: b.x1, y1: b.y1 }; this.redraw(); this.emitSel(); } return; }
    if (mod) return;
    if ((k === "enter" || k === "escape") && this.pathOpen) { this.pathOpen = null; this.redraw(); if (this.o.log) this.o.log("Path finished."); return; }
    if (k === "escape") { this.sel = null; this.redraw(); this.emitSel(); return; }
    if (k === "pageup") { e.preventDefault(); this.setLayer(this.s.L + 1); return; } if (k === "pagedown") { e.preventDefault(); this.setLayer(this.s.L - 1); return; }
    if (k === "home") { this.fit(); return; } if (k === "d") { this.turn(); return; } if (k === "m") { this.mirror(); return; }
    if (KEYS[k]) { e.preventDefault(); this.setTool(KEYS[k]); }
  };
  // a texture dropped from the Project panel: put it on the cell under the mouse (its wall, or else its floor; Shift = ceiling)
  // a point of a path on this story near the mouse: { pa, i }
  P.pathPointAt = function (px, py) { let best = null, bd = 10; for (const pa of this.m.paths || []) { if ((pa.layer || 0) !== this.s.L) continue; pa.pts.forEach((q, i) => { const d = Math.hypot(this.sx(q[0]) - px, this.sy(q[1]) - py); if (d < bd) { bd = d; best = { pa, i }; } }); } return best; };
  // the zone under a cell on the story being edited (zones reach up `height` stories); the smallest wins
  P.zoneAt = function (cx, cy) { const L = this.s.L; let best = null, ba = 1e9; for (const z of this.m.zones || []) { const ht = Math.max(0, +this.T.get(this.m.id + ".z." + z.id + ".height") || 0), zl = z.layer || 0; if (L < zl || L > zl + ht) continue; if (cx < z.x0 || cx > z.x1 || cy < z.y0 || cy > z.y1) continue; const a = (z.x1 - z.x0 + 1) * (z.y1 - z.y0 + 1); if (a < ba) { ba = a; best = z; } } return best; };
  P.dropTexture = function (e, assetId) {
    const [px, py] = this.rel(e), [x, y] = this.cellAt(px, py), c = G.cell(this.m, this.s.L, x, y); this.save(); const cc = G.edit(this.m, this.s.L, x, y);
    let part; if (e.shiftKey) { cc.c = assetId; part = "ceiling"; } else if (c && c.w !== undefined) { cc.w = assetId; delete cc.ws; part = "wall"; } else { cc.f = assetId; part = "floor"; }
    this.s.tex = assetId; this.buildPal(); this.changed(false); return part;
  };

  // ---------------------------------------------------------------- drawing
  P.redraw = function () { if (this.pending) return; this.pending = true; requestAnimationFrame(() => { this.pending = false; this.paint(); }); };
  P.paint = function () {
    const cv = this.cv, d = this.dpr, w = Math.max(2, Math.floor(cv.clientWidth * d)), hh = Math.max(2, Math.floor(cv.clientHeight * d)); if (cv.width !== w || cv.height !== hh) { cv.width = w; cv.height = hh; }
    const x = this.cx, m = this.m, s = this.s, T = this.T, z = this.v.z, cs = C * z, L = s.L; x.setTransform(d, 0, 0, d, 0, 0); x.imageSmoothingEnabled = false;
    x.fillStyle = "#0d1218"; x.fillRect(0, 0, this.W(), this.H());
    const gx0 = Math.floor(this.wx(0) / C) - 1, gx1 = Math.ceil(this.wx(this.W()) / C) + 1, gy0 = Math.floor(this.wy(0) / C) - 1, gy1 = Math.ceil(this.wy(this.H()) / C) + 1;
    const rect = (cx, cy) => [this.sx(cx * C), this.sy(cy * C), cs, cs];
    const each = (layer, fn) => { if (!layer) return; for (const k in layer.cells) { const [cx, cy] = k.split(",").map(Number); if (cx < gx0 || cx > gx1 || cy < gy0 || cy > gy1) continue; fn(layer.cells[k], cx, cy); } };
    const texFill = (tex, r, base) => { const im = this.img(tex); if (im) x.drawImage(im, r[0], r[1], r[2] + 0.5, r[3] + 0.5); else { x.fillStyle = base; x.fillRect(r[0], r[1], r[2] + 0.5, r[3] + 0.5); } };
    // the story below, faded
    if (s.below && L > 0) { x.globalAlpha = 0.28; each(m.layers[L - 1], (c, cx, cy) => { const r = rect(cx, cy); if (G.isWall(c)) { x.fillStyle = "#6c7380"; x.fillRect(...r); } else if (c.f !== undefined) { x.fillStyle = "#38404c"; x.fillRect(...r); } }); x.globalAlpha = 1; }
    // floors first, then walls on top
    each(m.layers[L], (c, cx, cy) => {
      if (G.isWall(c)) return; const r = rect(cx, cy);
      if (c.f !== undefined) {
        texFill(c.f, r, "#4a5260"); const hgt = c.h || 0; if (hgt) { x.fillStyle = "rgba(255,255,255," + (hgt / 15 * 0.3).toFixed(3) + ")"; x.fillRect(...r); }
        if (c.sh) { x.fillStyle = "rgba(20,30,50,.35)"; x.fillRect(...r); x.strokeStyle = "rgba(255,230,140,.9)"; x.lineWidth = Math.max(1, cs / 40);
          if (c.sh === "stairs") { x.beginPath(); for (let i = 1; i < 8; i++) { const t = i / 8; if (c.sd === 1 || c.sd === 3) { const X = r[0] + r[2] * t; x.moveTo(X, r[1]); x.lineTo(X, r[1] + r[3]); } else { const Y = r[1] + r[3] * t; x.moveTo(r[0], Y); x.lineTo(r[0] + r[2], Y); } } x.stroke(); }
          if (cs >= 14) { x.fillStyle = "#ffe68c"; x.font = "bold " + Math.round(cs * 0.45) + "px system-ui"; x.textAlign = "center"; x.textBaseline = "middle"; x.fillText(ARROW[c.sd || 0], r[0] + r[2] / 2, r[1] + r[3] / 2); } }
        if (cs >= 26 && (hgt || c.sh)) { x.fillStyle = "#fff"; x.font = Math.round(Math.min(13, cs * 0.24)) + "px system-ui"; x.textAlign = "left"; x.textBaseline = "top"; x.fillText(hgt + (c.sh ? "+" + (c.sr || 8) : ""), r[0] + 2, r[1] + 1); }
      } else if (c.c !== undefined) { x.fillStyle = "rgba(90,120,150,.25)"; x.fillRect(...r); }
      if (c.li !== undefined && c.li < 100) { x.fillStyle = "rgba(0,0,0," + ((1 - c.li / 100) * 0.65).toFixed(3) + ")"; x.fillRect(...r); }
      if (s.ceilShow && c.c !== undefined && cs >= 8) { x.fillStyle = "rgba(120,220,255,.75)"; x.beginPath(); x.moveTo(r[0] + r[2], r[1]); x.lineTo(r[0] + r[2] - cs * 0.22, r[1]); x.lineTo(r[0] + r[2], r[1] + cs * 0.22); x.closePath(); x.fill(); }
    });
    // grid
    x.lineWidth = 1; for (let gx = gx0; gx <= gx1; gx++) { x.strokeStyle = gx % 8 === 0 ? "rgba(140,170,200,.32)" : "rgba(140,170,200,.12)"; const X = Math.round(this.sx(gx * C)) + 0.5; x.beginPath(); x.moveTo(X, 0); x.lineTo(X, this.H()); x.stroke(); }
    for (let gy = gy0; gy <= gy1; gy++) { x.strokeStyle = gy % 8 === 0 ? "rgba(140,170,200,.32)" : "rgba(140,170,200,.12)"; const Y = Math.round(this.sy(gy * C)) + 0.5; x.beginPath(); x.moveTo(0, Y); x.lineTo(this.W(), Y); x.stroke(); }
    // walls
    each(m.layers[L], (c, cx, cy) => {
      if (!G.isWall(c)) return; const r = rect(cx, cy); texFill(c.w, r, "#8b919c"); x.fillStyle = "rgba(0,0,0,.18)"; x.fillRect(...r);
      x.strokeStyle = "rgba(255,255,255,.55)"; x.lineWidth = Math.max(1, cs / 26); x.strokeRect(r[0] + 1, r[1] + 1, r[2] - 2, r[3] - 2);
      if (c.ws && cs >= 18) { x.fillStyle = "#ffcf5a"; for (const sd in c.ws) { const q = +sd, t = cs * 0.12; x.fillRect(q === 3 ? r[0] : q === 1 ? r[0] + r[2] - t : r[0] + t, q === 0 ? r[1] : q === 2 ? r[1] + r[3] - t : r[1] + t, q === 0 || q === 2 ? r[2] - 2 * t : t, q === 1 || q === 3 ? r[3] - 2 * t : t); } }
    });
    // diagonal walls
    const polyPath = (cx, cy, poly) => { x.beginPath(); poly.forEach(([u, v], i) => (i ? x.lineTo : x.moveTo).call(x, this.sx((cx + u) * C), this.sy((cy + v) * C))); x.closePath(); };
    each(m.layers[L], (c, cx, cy) => {
      if (!G.isDiag(c)) return; const r = rect(cx, cy); x.save(); polyPath(cx, cy, c.dg); x.clip(); texFill(c.dt, r, "#8b919c"); x.fillStyle = "rgba(0,0,0,.18)"; x.fillRect(...r); x.restore();
      polyPath(cx, cy, c.dg); x.strokeStyle = "rgba(255,255,255,.6)"; x.lineWidth = Math.max(1, cs / 26); x.stroke();
    });
    if (s.tool === "diag" && this.hover && !this.drag) { x.fillStyle = "rgba(125,243,255,.35)"; x.strokeStyle = "#7df3ff"; x.lineWidth = 1.5; for (const q of G.shapeCells(s.shape, s.rot, s.flip)) { polyPath(this.hover[0] + q.dx, this.hover[1] + q.dy, q.poly); x.fill(); x.stroke(); } }
    // objects on this story
    for (const th of this.things()) {
      const ty = T.get(th.prop + ".type"), selT = this.sel && this.sel.kind === "thing" && this.sel.id === th.id, nm = T.get(th.prop + ".name") || th.prop;
      if (ty === "door") { const f = G.thingFrame(m, T, th), X = this.sx(f.cx), Y = this.sy(f.cy), ns = f.ux === 0, a = f.w * z / 2, b = Math.max(3, cs * 0.12);
        x.fillStyle = "rgba(217,163,90,.18)"; for (const [qx, qy] of f.cells) x.fillRect(this.sx(qx * C), this.sy(qy * C), cs, cs);
        const tp = m.id + ".th." + th.id + ".", own = T.has(tp + "openMode"), dv = k => own ? T.get(tp + k) : T.get(th.prop + "." + k), shOn = dv("shieldOn") !== "off", sf = dv("openMode") === "shot" ? dv("shotFirst") : "any";
        const lk = dv("lockKey"), sw = sf === "weapon" && shOn ? T.get("player." + dv("shotWeapon") + ".doorColor") : null, sc = shOn && (sf === "charged" || dv("lockNeedTag")) ? dv("shieldColor") : null; x.fillStyle = selT ? "#ffd84a" : lk && T.get(lk + ".keyColor") || sw || sc || "#d9a35a"; x.fillRect(ns ? X - b / 2 : X - a, ns ? Y - a : Y - b / 2, ns ? b : a * 2, ns ? a * 2 : b); x.strokeStyle = "#000"; x.lineWidth = 1; x.strokeRect(ns ? X - b / 2 : X - a, ns ? Y - a : Y - b / 2, ns ? b : a * 2, ns ? a * 2 : b);
        if (own && cs >= 10) { x.fillStyle = "#fff"; x.strokeStyle = "#000"; x.lineWidth = 1.5; x.beginPath(); x.arc(X, Y, Math.max(2.5, cs * 0.07), 0, Math.PI * 2); x.fill(); x.stroke(); }
        if (f.H > 1 && cs >= 16) { x.fillStyle = "#fff"; x.font = "bold 11px system-ui"; x.textAlign = "center"; x.textBaseline = "middle"; x.fillText(f.W + "×" + f.H, X, Y - b - 6); } }
      else { const X = this.sx(th.x), Y = this.sy(th.y), r = Math.max(5, cs * 0.16), col = { enemy: "#ff5a4a", pickup: "#ffd84a", exit: "#5aff8a", npc: "#b48cff", speaker: "#4ad8ff", interactive: "#ff5ad8", checkpoint: "#5affd0", spawner: "#7df3ff", boss: "#ff2a6a", save: "#7dffb0" }[ty] || (ty === "key" ? T.get(th.prop + ".keyColor") || "#ffffff" : "#b8c4d4"), a = (th.ang || 0) * Math.PI / 180;
        if (ty === "spawner") { const R = Math.max(6, (+T.get(th.prop + ".spawnRadius") || 0) * z); x.save(); x.setLineDash([3, 4]); x.strokeStyle = selT ? "rgba(125,243,255,.95)" : "rgba(125,243,255,.55)"; x.lineWidth = 1.5; x.beginPath(); x.arc(X, Y, R, 0, 7); x.stroke(); x.restore(); }
        if (ty === "speaker") { const R = (+T.get(th.prop + ".spkRadius") || 512) * z; x.save(); x.setLineDash([6, 5]); x.strokeStyle = selT ? "rgba(74,216,255,.9)" : "rgba(74,216,255,.45)"; x.fillStyle = "rgba(74,216,255,.06)"; x.lineWidth = 1.5; x.beginPath(); x.arc(X, Y, R, 0, Math.PI * 2); x.fill(); x.stroke(); x.restore(); }
        const rr = ty === "boss" ? r * 1.6 : r; x.fillStyle = col; x.beginPath(); x.arc(X, Y, rr, 0, Math.PI * 2); x.fill(); x.strokeStyle = selT ? "#fff" : "#000"; x.lineWidth = selT ? 2.5 : 1; x.stroke();
        x.strokeStyle = "#000"; x.lineWidth = 2; x.beginPath(); x.moveTo(X, Y); x.lineTo(X + Math.sin(a) * r * 1.2, Y + Math.cos(a) * r * 1.2); x.stroke();
        if (ty === "speaker" || ty === "boss" || ty === "key" || ty === "save") { x.fillStyle = "#000"; x.font = "bold " + Math.round(rr * 1.3) + "px system-ui"; x.textAlign = "center"; x.textBaseline = "middle"; x.fillText(ty === "boss" ? "♛" : ty === "key" ? "⚿" : ty === "save" ? "S" : "♪", X, Y + 1); } }
      if (cs >= 30) { x.fillStyle = "#e8f0f8"; x.font = "11px system-ui"; x.textAlign = "center"; x.textBaseline = "top"; { const f = ty === "door" ? G.thingFrame(m, T, th) : null; x.fillText(nm, this.sx(f ? f.cx : th.x), this.sy(f ? f.cy : th.y) + Math.max(7, cs * 0.2)); } }
    }
    // player start
    if ((+T.get(m.id + ".startLayer") || 0) === L) {
      const X = this.sx(+T.get(m.id + ".startX")), Y = this.sy(+T.get(m.id + ".startY")), a = (+T.get(m.id + ".startAngle") || 0) * Math.PI / 180, r = Math.max(8, cs * 0.22), fx = Math.cos(a), fy = Math.sin(a), selS = this.sel && this.sel.kind === "start";
      x.fillStyle = "#3dff7a"; x.strokeStyle = selS ? "#fff" : "#04220e"; x.lineWidth = selS ? 2.5 : 1.5; x.beginPath(); x.moveTo(X + fx * r * 1.3, Y + fy * r * 1.3); x.lineTo(X - fx * r * 0.8 - fy * r * 0.8, Y - fy * r * 0.8 + fx * r * 0.8); x.lineTo(X - fx * r * 0.8 + fy * r * 0.8, Y - fy * r * 0.8 - fx * r * 0.8); x.closePath(); x.fill(); x.stroke();
    }
    // selection, room box, hover
    const box = (a, b, col, dash) => { const x0 = Math.min(a[0], b[0]), x1 = Math.max(a[0], b[0]) + 1, y0 = Math.min(a[1], b[1]), y1 = Math.max(a[1], b[1]) + 1; x.setLineDash(dash || []); x.strokeStyle = col; x.lineWidth = 2; x.strokeRect(this.sx(x0 * C), this.sy(y0 * C), (x1 - x0) * cs, (y1 - y0) * cs); x.setLineDash([]); };
    // paths on this story: lines with numbered points and arrows; a dashed line from each enemy that walks one to its first point
    for (const pa of m.paths || []) { if ((pa.layer || 0) !== L || !pa.pts.length) continue; const sel = this.sel && this.sel.kind === "path" && this.sel.id === pa.id, open = this.pathOpen === pa.id, P = pa.pts.map(q => [this.sx(q[0]), this.sy(q[1])]);
      x.strokeStyle = sel || open ? "#9be6ff" : "rgba(90,209,255,.75)"; x.lineWidth = sel ? 3 : 2; x.setLineDash([]); x.beginPath(); P.forEach((q, i) => (i ? x.lineTo : x.moveTo).call(x, q[0], q[1])); x.stroke();
      if (pa.loop !== false && P.length > 2) { x.setLineDash([6, 5]); x.beginPath(); x.moveTo(P[P.length - 1][0], P[P.length - 1][1]); x.lineTo(P[0][0], P[0][1]); x.stroke(); x.setLineDash([]); }
      for (let i = 0; i + 1 < P.length; i++) { const a = P[i], b = P[i + 1], mx = (a[0] + b[0]) / 2, my = (a[1] + b[1]) / 2, an = Math.atan2(b[1] - a[1], b[0] - a[0]); x.fillStyle = x.strokeStyle; x.beginPath(); x.moveTo(mx + Math.cos(an) * 7, my + Math.sin(an) * 7); x.lineTo(mx + Math.cos(an + 2.5) * 6, my + Math.sin(an + 2.5) * 6); x.lineTo(mx + Math.cos(an - 2.5) * 6, my + Math.sin(an - 2.5) * 6); x.fill(); }
      P.forEach((q, i) => { x.fillStyle = i === 0 ? "#3dff9a" : "#5ad1ff"; x.beginPath(); x.arc(q[0], q[1], sel ? 6 : 5, 0, 7); x.fill(); x.strokeStyle = "#000"; x.lineWidth = 1; x.stroke(); if (cs >= 20) { x.fillStyle = "#000"; x.font = "bold 9px system-ui"; x.textAlign = "center"; x.textBaseline = "middle"; x.fillText(String(i + 1), q[0], q[1] + 0.5); } });
      if (cs >= 14) { x.fillStyle = "#cfefff"; x.font = "11px system-ui"; x.textAlign = "left"; x.textBaseline = "bottom"; x.fillText(pa.id + (open ? "  (click to add points, Enter to finish)" : ""), P[0][0] + 8, P[0][1] - 6); } }
    for (const th of this.things()) { if (!th.path) continue; const pa = (m.paths || []).find(q => q.id === th.path); if (!pa || !pa.pts.length) continue; x.setLineDash([3, 4]); x.strokeStyle = "rgba(90,209,255,.6)"; x.lineWidth = 1.5; x.beginPath(); x.moveTo(this.sx(th.x), this.sy(th.y)); x.lineTo(this.sx(pa.pts[0][0]), this.sy(pa.pts[0][1])); x.stroke(); x.setLineDash([]); }
    // ceiling enemies: their detection cone, seen from above (where it reaches the floor)
    for (const th of this.things()) { const pr = th.prop + "."; if (T.get(pr + "type") !== "enemy" || T.get(pr + "behavior") !== "stationary" || T.get(pr + "mount") !== "ceiling") continue; const R = Math.tan((+T.get(pr + "coneAngle") || 25) * Math.PI / 180) * Math.min(+T.get(pr + "coneRange") || 400, 128);
      x.setLineDash([4, 4]); x.strokeStyle = "rgba(255,90,74,.8)"; x.fillStyle = "rgba(255,90,74,.08)"; x.lineWidth = 1.5; x.beginPath(); x.arc(this.sx(th.x), this.sy(th.y), Math.max(4, R * z), 0, 7); x.fill(); x.stroke(); x.setLineDash([]); }
    // zones on this story: translucent boxes (amber = trigger, purple = custom), always shown, brighter with the zone tool
    for (const zn of m.zones || []) { const ht = Math.max(0, +T.get(m.id + ".z." + zn.id + ".height") || 0), zl = zn.layer || 0; if (L < zl || L > zl + ht) continue;
      const cu = zn.type === "custom", sel = this.sel && this.sel.kind === "zone" && this.sel.id === zn.id, on = s.tool === "zone" || sel, X0 = this.sx(zn.x0 * C), Y0 = this.sy(zn.y0 * C), W0 = (zn.x1 - zn.x0 + 1) * cs, H0 = (zn.y1 - zn.y0 + 1) * cs;
      x.fillStyle = cu ? "rgba(220,90,255," + (on ? 0.22 : 0.1) + ")" : "rgba(255,190,60," + (on ? 0.22 : 0.1) + ")"; x.fillRect(X0, Y0, W0, H0);
      x.setLineDash(sel ? [] : [7, 4]); x.strokeStyle = cu ? "#dc5aff" : "#ffbe3c"; x.lineWidth = sel ? 3 : 1.5; x.strokeRect(X0 + 1, Y0 + 1, W0 - 2, H0 - 2); x.setLineDash([]);
      if (cs * 0.8 >= 6) { x.fillStyle = cu ? "#f2c4ff" : "#ffe2a6"; x.font = "bold " + Math.round(Math.max(10, Math.min(13, cs * 0.22))) + "px system-ui"; x.textAlign = "left"; x.textBaseline = "top"; x.fillText((cu ? "⬡ " : "⚡ ") + (T.get(m.id + ".z." + zn.id + ".name") || zn.id) + (zl !== L ? "  (from story " + (zl + 1) + ")" : ""), X0 + 4, Y0 + 3); } }
    if (this.drag && this.drag.kind === "zbox") box(this.drag.a, this.drag.b, s.zkind === "custom" ? "#dc5aff" : "#ffbe3c", [5, 3]);
    if (this.sel && this.sel.kind === "cells") { box([this.sel.x0, this.sel.y0], [this.sel.x1, this.sel.y1], "#ffd84a", [6, 4]); }
    if (this.drag && this.drag.kind === "room") { box(this.drag.a, this.drag.b, this.drag.erase ? "#ff6a5a" : "#7df3ff", [5, 3]); const n = (Math.abs(this.drag.b[0] - this.drag.a[0]) + 1) + " × " + (Math.abs(this.drag.b[1] - this.drag.a[1]) + 1); x.fillStyle = "#7df3ff"; x.font = "12px system-ui"; x.textAlign = "left"; x.textBaseline = "bottom"; x.fillText(n + " cells (" + 2 * (Math.abs(this.drag.b[0] - this.drag.a[0]) + 1) + " m × " + 2 * (Math.abs(this.drag.b[1] - this.drag.a[1]) + 1) + " m)", this.sx(Math.min(this.drag.a[0], this.drag.b[0]) * C), this.sy(Math.min(this.drag.a[1], this.drag.b[1]) * C) - 3); }
    if (this.hover && !this.drag) { const [hx, hy] = this.hover; x.strokeStyle = "rgba(255,255,255,.8)"; x.lineWidth = 1.5; x.strokeRect(this.sx(hx * C) + 0.5, this.sy(hy * C) + 0.5, cs - 1, cs - 1);
      if ((s.tool === "slope" || s.tool === "stairs") && cs >= 14) { x.fillStyle = "rgba(255,230,140,.9)"; x.font = "bold " + Math.round(cs * 0.45) + "px system-ui"; x.textAlign = "center"; x.textBaseline = "middle"; x.fillText(ARROW[s.dir], this.sx((hx + 0.5) * C), this.sy((hy + 0.5) * C)); } }
    // status line
    x.fillStyle = "rgba(10,14,20,.75)"; x.fillRect(0, this.H() - 20, this.W(), 20); x.fillStyle = "#b9c8d8"; x.font = "11px system-ui"; x.textAlign = "left"; x.textBaseline = "middle";
    const hc = this.hover ? G.cell(m, L, this.hover[0], this.hover[1]) : null;
    x.fillText("Story " + (L + 1) + "  ·  " + (this.hover ? "cell " + this.hover[0] + ", " + this.hover[1] + (hc ? "  ·  " + (G.isWall(hc) ? "wall" : [hc.f !== undefined ? "floor h " + (hc.h || 0) + "/16" + (hc.sh ? " " + hc.sh + " " + ARROW[hc.sd || 0] + " +" + (hc.sr || 8) + "/16" : "") : "", hc.c !== undefined ? "ceiling" : "", hc.li !== undefined ? "light " + hc.li + "%" : ""].filter(Boolean).join(", ")) : "  ·  empty") : "") + "  ·  1 cell = 2 m  ·  middle mouse / Space+drag pans, wheel zooms, Alt+click picks a texture", 8, this.H() - 10);
  };
})();
