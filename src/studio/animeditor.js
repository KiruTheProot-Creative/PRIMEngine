/* PRIMEngine Animation Editor: keyframe the parts of an FBX model on a timeline.
   The model is shown on top, the timeline below (one row per part, a diamond per key).
   Viewport: drag = orbit, wheel = zoom, click a part to select it. Tools (buttons or keys): Q orbit, G move, R rotate (X / Y / Z picks
   the axis), S scale. Dragging with a tool changes the selected part and sets a key at the playhead.
   Timeline: click / drag the ruler to scrub, click a diamond to select it, drag it to move it in time (Shift = no snapping),
   double-click a row to add a key there, Delete removes the selected key. Space plays, K keys the selected part, Ctrl+Z undoes.
   Options: { host, model, gm (loaded GPU model), anim (the project.anims entry), state (kept between openings), onChange(kind), onSelect() } */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), M4 = PRIM.M4, L = PRIM.AnimLib, clamp = (v, a, b) => Math.max(a, Math.min(b, v));
  const mk = (tag, cls, txt) => { const e = document.createElement(tag); if (cls) e.className = cls; if (txt) e.textContent = txt; return e; };
  if (!document.getElementById("ae-style")) {
    const s = mk("style"); s.id = "ae-style";
    s.textContent = `
.ae{position:absolute;inset:0;display:grid;grid-template-rows:1fr 170px;background:#0d131a;outline:none}
.ae-top{position:relative;min-height:0}
.ae-top canvas{position:absolute;inset:0;width:100%;height:100%;display:block}
.ae-tools{position:absolute;left:8px;top:8px;display:flex;gap:4px;flex-wrap:wrap;font:12px "Share Tech Mono",ui-monospace,monospace}
.ae-tools button{font:inherit;color:#d7e6f2;background:#1b2733cc;border:1px solid #3a4b5c;border-radius:4px;padding:3px 9px;cursor:pointer}
.ae-tools button.on{background:#ffb53a;color:#111;border-color:#ffb53a}
.ae-tools .sep{width:8px}
.ae-hint{position:absolute;left:8px;bottom:6px;font:12px "Share Tech Mono",ui-monospace,monospace;color:#9fb4c8;text-shadow:0 1px 2px #000;pointer-events:none}
.ae-tl{position:relative;border-top:1px solid #2a3846;background:#111a23}
.ae-tl canvas{position:absolute;inset:0;width:100%;height:100%;display:block}`;
    document.head.append(s);
  }
  const NAMEW = 150, RULER = 22, ROW = 18, FPS = 30;

  PRIM.AnimEditor = class AnimEditor {
    constructor(o) {
      this.o = o; this.model = o.model; this.gm = o.gm; this.a = o.anim; const st = this.st = o.state;
      st.yaw = st.yaw === undefined ? -30 : st.yaw; st.pitch = st.pitch === undefined ? 15 : st.pitch; st.zoom = st.zoom || 1; st.time = clamp(st.time || 0, 0, this.a.dur); st.tool = st.tool || "orbit"; st.axis = st.axis || 1; st.scroll = st.scroll || 0;
      if (st.node === undefined || !this.model.nodes.some(n => n.name === st.node)) { const first = this.model.nodes.find(n => n.mesh) || this.model.nodes[0]; st.node = first ? first.name : null; }
      this.undo = []; this.redo = [];
      this.root = mk("div", "ae"); this.root.tabIndex = 0; const top = mk("div", "ae-top"); this.cv = mk("canvas"); top.append(this.cv); this.tools = mk("div", "ae-tools"); this.hint = mk("div", "ae-hint"); top.append(this.tools, this.hint);
      const tl = mk("div", "ae-tl"); this.tc = mk("canvas"); tl.append(this.tc); this.root.append(top, tl); o.host.replaceChildren(this.root);
      this.view = new PRIM.R3D.View(this.cv); if (!this.view.ok) { top.append(mk("div", "v3err", "WebGL could not start.")); return; } this.view.sky = [0.07, 0.09, 0.12]; this.view.fov = 0.8;
      if (!this.gm) this.gm = this.view.loadModel(this.model);
      this.bounds = this.nodeBounds(); this.buildTools();
      this.bind(); this.last = performance.now(); this.raf = requestAnimationFrame(t => this.frame(t));
    }
    destroy() { this.dead = true; cancelAnimationFrame(this.raf); if (this.unbind) this.unbind(); if (this.view && this.view.destroy) this.view.destroy(); }
    // ---- data helpers
    rows() { return this.model.nodes.map((n, i) => ({ n, i })); }
    snap() { this.undo.push(JSON.stringify({ keys: this.a.keys, dur: this.a.dur })); if (this.undo.length > 100) this.undo.shift(); this.redo = []; }
    restore(from, to) { const s = from.pop(); if (!s) return; to.push(JSON.stringify({ keys: this.a.keys, dur: this.a.dur })); const d = JSON.parse(s); this.a.keys = d.keys; this.a.dur = d.dur; this.changed("keys"); }
    changed(kind) { L.apply(this.o.project, this.model, this.a.asset); if (this.o.onChange) this.o.onChange(kind || "keys"); }
    value(name) { return L.valueAt(this.a, this.model, name || this.st.node, this.st.time); }
    keyNow(name) { name = name || this.st.node; if (!name) return; this.snap(); L.setKey(this.a, name, this.st.time, this.value(name)); this.st.key = { node: name, t: Math.round(this.st.time * 1000) / 1000 }; this.changed(); }
    setValue(name, v) { L.setKey(this.a, name, this.st.time, v); this.st.key = { node: name, t: Math.round(this.st.time * 1000) / 1000 }; this.changed(); }
    deleteKey() { const k = this.st.key; if (!k) return; this.snap(); L.removeKey(this.a, k.node, k.t); this.st.key = null; this.changed(); }
    setTime(t) { this.st.time = clamp(t, 0, this.a.dur); if (this.o.onChange) this.o.onChange("time"); }
    // local size of every part's mesh (for the selection box and for clicking parts)
    nodeBounds() { return this.model.nodes.map(n => { if (!n.mesh) return null; const p = n.mesh.p, lo = [1e9, 1e9, 1e9], hi = [-1e9, -1e9, -1e9]; for (let i = 0; i < p.length; i += 3) for (let a = 0; a < 3; a++) { lo[a] = Math.min(lo[a], p[i + a]); hi[a] = Math.max(hi[a], p[i + a]); } return { lo, hi }; }); }
    // ---- camera: the model is fitted into view and turned around its middle
    viewM() { const st = this.st, b = this.model.bounds, c = [0, 1, 2].map(i => (b.lo[i] + b.hi[i]) / 2), D = 34, k = D * 0.75 / (b.size || 1) * st.zoom;
      return M4.mul(M4.translate(0, 0, -D), M4.mul(M4.rotX(st.pitch), M4.mul(M4.rotY(st.yaw), M4.mul(M4.scale(k, k, k), M4.translate(-c[0], -c[1], -c[2]))))); }
    pose() { return PRIM.FBX.pose(this.model, this.a.name in this.model.anims ? this.a.name : null, this.st.time * this.speed()); }
    speed() { return 1; }
    project(p) { const w = this.cv.clientWidth, h = this.cv.clientHeight, th = Math.tan(this.view.fov / 2); if (p[2] > -0.01) return null; return [w / 2 + (p[0] / -p[2]) / (th * w / h) * w / 2, h / 2 - (p[1] / -p[2]) / th * h / 2]; }
    boxOf(i, V, pose) { const b = this.bounds[i]; if (!b) return null; const M = M4.mul(V, pose[i]), pts = []; for (const x of [b.lo[0], b.hi[0]]) for (const y of [b.lo[1], b.hi[1]]) for (const z of [b.lo[2], b.hi[2]]) pts.push(M4.point(M, x, y, z)); return pts; }
    pick(sx, sy) { const V = this.viewM(), pose = this.pose(); let best = null;
      this.model.nodes.forEach((n, i) => { const pts = this.boxOf(i, V, pose); if (!pts) return; const sp = pts.map(p => this.project(p)).filter(Boolean); if (sp.length < 8) return; const x0 = Math.min(...sp.map(q => q[0])), x1 = Math.max(...sp.map(q => q[0])), y0 = Math.min(...sp.map(q => q[1])), y1 = Math.max(...sp.map(q => q[1]));
        if (sx < x0 || sx > x1 || sy < y0 || sy > y1) return; const area = (x1 - x0) * (y1 - y0); if (!best || area < best.area) best = { i, area }; });
      return best ? this.model.nodes[best.i].name : null; }
    // ---- the toolbar over the viewport
    buildTools() {
      const st = this.st, b = (txt, on, fn, tip) => { const e = mk("button", on ? "on" : "", txt); e.title = tip || ""; e.onclick = () => { fn(); this.buildTools(); this.root.focus(); }; return e; };
      this.tools.replaceChildren(b("Orbit (Q)", st.tool === "orbit", () => st.tool = "orbit"), b("Move (G)", st.tool === "move", () => st.tool = "move"), b("Rotate (R)", st.tool === "rotate", () => st.tool = "rotate"), b("Scale (S)", st.tool === "scale", () => st.tool = "scale"),
        mk("span", "sep"), ...["X", "Y", "Z"].map((n, i) => b(n, st.axis === i && st.tool === "rotate", () => { st.axis = i; st.tool = "rotate"; }, "Rotate around " + n)), mk("span", "sep"),
        b(st.playing ? "■ Stop" : "▶ Play", !!st.playing, () => { st.playing = !st.playing; }, "Space"), b("Key part (K)", false, () => this.keyNow(), "Set a key for the selected part at the playhead"));
      this.hint.textContent = st.tool === "orbit" ? "Drag to turn the view · wheel zooms · click a part to select it" : st.tool === "move" ? "Drag to move the selected part (keys it at the playhead)" : st.tool === "rotate" ? "Drag left / right to turn the part around " + "XYZ"[st.axis] : "Drag left / right to scale the part";
    }
    // ---- input
    bind() {
      const cv = this.cv, tc = this.tc, st = this.st; let drag = null;
      cv.addEventListener("pointerdown", e => { this.root.focus(); cv.setPointerCapture(e.pointerId); const r = cv.getBoundingClientRect(); drag = { x: e.clientX, y: e.clientY, x0: e.clientX, y0: e.clientY, moved: false, tool: e.button === 0 ? st.tool : "orbit", snapped: false }; });
      cv.addEventListener("pointermove", e => { if (!drag) return; const dx = e.clientX - drag.x, dy = e.clientY - drag.y; if (Math.hypot(e.clientX - drag.x0, e.clientY - drag.y0) > 3) drag.moved = true; if (!drag.moved) return; drag.x = e.clientX; drag.y = e.clientY;
        if (drag.tool === "orbit" || !st.node) { st.yaw += dx * 0.6; st.pitch = clamp(st.pitch + dy * 0.6, -89, 89); return; }
        if (!drag.snapped) { this.snap(); drag.snapped = true; } const v = this.value(), node = this.model.nodes.findIndex(n => n.name === st.node);
        if (drag.tool === "move") { const pose = this.pose(), par = this.model.nodes[node].parent, PM = M4.mul(this.viewM(), par >= 0 ? pose[par] : M4.ident()), here = M4.point(M4.mul(this.viewM(), pose[node]), 0, 0, 0), depth = Math.max(1, -here[2]), u = 2 * depth * Math.tan(this.view.fov / 2) / (cv.clientHeight || 1), d = M4.dir(M4.inv(PM), [dx * u, -dy * u, 0]); v.p = v.p.map((q, i) => q + d[i]); }
        else if (drag.tool === "rotate") v.r[st.axis] += dx * 0.5;
        else if (drag.tool === "scale") { const f = Math.exp(dx * 0.01); v.s = v.s.map(q => q * f); }
        this.setValue(st.node, v); });
      cv.addEventListener("pointerup", e => { if (drag && !drag.moved && e.button === 0) { const r = cv.getBoundingClientRect(), n = this.pick(e.clientX - r.left, e.clientY - r.top); if (n) { st.node = n; st.key = null; if (this.o.onSelect) this.o.onSelect(); } } drag = null; });
      cv.addEventListener("wheel", e => { e.preventDefault(); st.zoom = clamp(st.zoom * (e.deltaY > 0 ? 0.9 : 1.1), 0.2, 8); }, { passive: false });
      cv.addEventListener("contextmenu", e => e.preventDefault());
      // timeline
      let td = null; const tpos = e => { const r = tc.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; }, tOf = x => (x - NAMEW) / this.pxs(), rowAt = y => Math.floor((y - RULER) / ROW) + st.scroll;
      tc.addEventListener("pointerdown", e => { this.root.focus(); tc.setPointerCapture(e.pointerId); const [x, y] = tpos(e);
        if (y < RULER && x > NAMEW) { td = { scrub: true }; this.setTime(this.snapT(tOf(x), e.shiftKey)); return; }
        const ri = rowAt(y), row = this.rows()[ri]; if (!row) return; if (x < NAMEW) { st.node = row.n.name; st.key = null; if (this.o.onSelect) this.o.onSelect(); return; }
        const k = L.keysOf(this.a, row.n.name).find(k => Math.abs(NAMEW + k.t * this.pxs() - x) < 7); st.node = row.n.name;
        if (k) { st.key = { node: k.node, t: k.t }; this.setTime(k.t); this.snap(); td = { key: k, moved: false }; } else { st.key = null; this.setTime(this.snapT(tOf(x), e.shiftKey)); td = { scrub: true }; }
        if (this.o.onSelect) this.o.onSelect(); });
      tc.addEventListener("pointermove", e => { if (!td) return; const [x] = tpos(e), t = this.snapT(tOf(x), e.shiftKey);
        if (td.scrub) this.setTime(t); else if (td.key) { if (Math.abs(t - td.key.t) > 1e-4 && !this.a.keys.some(k => k !== td.key && k.node === td.key.node && Math.abs(k.t - t) < 0.0005)) { td.key.t = t; st.key = { node: td.key.node, t }; td.moved = true; this.setTime(t); this.changed(); } } });
      tc.addEventListener("pointerup", () => { if (td && td.key && !td.moved) this.undo.pop(); td = null; });
      tc.addEventListener("dblclick", e => { const [x, y] = tpos(e), row = this.rows()[rowAt(y)]; if (!row || x < NAMEW) return; st.node = row.n.name; this.setTime(this.snapT(tOf(x), e.shiftKey)); this.keyNow(row.n.name); if (this.o.onSelect) this.o.onSelect(); });
      tc.addEventListener("wheel", e => { e.preventDefault(); st.scroll = clamp(st.scroll + (e.deltaY > 0 ? 2 : -2), 0, Math.max(0, this.rows().length - 3)); }, { passive: false });
      const key = e => { if (/INPUT|SELECT|TEXTAREA/.test((e.target && e.target.tagName) || "")) return; const k = e.key.toLowerCase();
        if ((e.ctrlKey || e.metaKey) && k === "z") { e.preventDefault(); e.shiftKey ? this.restore(this.redo, this.undo) : this.restore(this.undo, this.redo); return; } if ((e.ctrlKey || e.metaKey) && k === "y") { e.preventDefault(); this.restore(this.redo, this.undo); return; }
        if (e.ctrlKey || e.metaKey || e.altKey) return;
        if (k === " ") { e.preventDefault(); st.playing = !st.playing; } else if (k === "q") st.tool = "orbit"; else if (k === "g") st.tool = "move"; else if (k === "r") st.tool = "rotate"; else if (k === "s") st.tool = "scale";
        else if (k === "x" || k === "y" || k === "z") { if (st.tool === "rotate") st.axis = "xyz".indexOf(k); else if (k === "x") { this.deleteKey(); return; } }
        else if (k === "k") { this.keyNow(); return; } else if (k === "delete" || k === "backspace") { e.preventDefault(); e.stopPropagation(); this.deleteKey(); return; } else return; this.buildTools(); };
      this.root.addEventListener("keydown", key); this.unbind = () => {};
    }
    pxs() { return Math.max(10, (this.tc.clientWidth - NAMEW - 14) / (this.a.dur || 1)); }
    snapT(t, free) { t = clamp(t, 0, this.a.dur); return free ? Math.round(t * 1000) / 1000 : Math.round(t * FPS) / FPS; }
    // ---- drawing
    frame(now) {
      if (this.dead) return; this.raf = requestAnimationFrame(t => this.frame(t)); const dt = Math.min(0.1, (now - this.last) / 1000); this.last = now; const st = this.st;
      if (st.playing) { st.time += dt; if (st.time > this.a.dur) st.time = 0; if (!this.tick || now - this.tick > 120) { this.tick = now; if (this.o.onChange) this.o.onChange("time"); } }
      const V = this.viewM(), pose = this.pose(), segs = [], line = (a, b, c) => segs.push({ a, b, c });
      for (let i = -4; i <= 4; i++) { const b = this.model.bounds, y = b.lo[1], s = (b.size || 1) / 4, c = [0.24, 0.3, 0.38]; line(M4.point(V, i * s, y, -4 * s), M4.point(V, i * s, y, 4 * s), c); line(M4.point(V, -4 * s, y, i * s), M4.point(V, 4 * s, y, i * s), c); }
      const si = this.model.nodes.findIndex(n => n.name === st.node), box = si >= 0 ? this.boxOf(si, V, pose) : null;
      if (box) { const E = [[0, 1], [2, 3], [4, 5], [6, 7], [0, 2], [1, 3], [4, 6], [5, 7], [0, 4], [1, 5], [2, 6], [3, 7]], c = [1, 0.75, 0.2]; for (const [a, b] of E) line(box[a], box[b], c); }
      this.view.draw(); this.view.drawViewmodel([{ gm: this.gm, pose, M: V }], segs); this.drawTimeline();
    }
    drawTimeline() {
      const c = this.tc, dpr = devicePixelRatio || 1, w = c.clientWidth, h = c.clientHeight; if (c.width !== Math.round(w * dpr) || c.height !== Math.round(h * dpr)) { c.width = Math.round(w * dpr); c.height = Math.round(h * dpr); }
      const g = c.getContext("2d"), st = this.st, px = this.pxs(), rows = this.rows(), dur = this.a.dur; g.setTransform(dpr, 0, 0, dpr, 0, 0); g.clearRect(0, 0, w, h); g.font = "11px ui-monospace,Consolas,monospace";
      g.fillStyle = "#0b1118"; g.fillRect(0, 0, w, RULER); g.fillStyle = "#16212b"; g.fillRect(0, 0, NAMEW, h);
      const step = dur > 6 ? 1 : dur > 2 ? 0.5 : 0.1; for (let t = 0; t <= dur + 1e-6; t += step) { const x = NAMEW + t * px; g.strokeStyle = "#2a3846"; g.beginPath(); g.moveTo(x, RULER - 6); g.lineTo(x, h); g.stroke(); g.fillStyle = "#8aa0b4"; g.fillText((Math.round(t * 10) / 10) + "s", x + 2, 13); }
      g.fillStyle = "#ff6a3a"; g.fillRect(NAMEW + dur * px, 0, 2, h);
      for (let r = 0; r * ROW < h - RULER; r++) { const ri = r + st.scroll, row = rows[ri]; if (!row) break; const y = RULER + r * ROW, sel = row.n.name === st.node;
        g.fillStyle = sel ? "#2a3c50" : r % 2 ? "#121b24" : "#0f1720"; g.fillRect(0, y, w, ROW); g.fillStyle = sel ? "#ffd27a" : row.n.mesh ? "#cfe0ee" : "#7d91a4"; g.fillText((row.n.parent >= 0 ? "  " : "") + (row.n.name || "part " + row.i).slice(0, 20), 6, y + 13);
        const ks = L.keysOf(this.a, row.n.name); if (ks.length > 1) { g.strokeStyle = "#4b6175"; g.beginPath(); g.moveTo(NAMEW + ks[0].t * px, y + ROW / 2); g.lineTo(NAMEW + ks[ks.length - 1].t * px, y + ROW / 2); g.stroke(); }
        for (const k of ks) { const x = NAMEW + k.t * px, on = st.key && st.key.node === k.node && Math.abs(st.key.t - k.t) < 0.0005; g.fillStyle = on ? "#ffd25a" : "#7df3ff"; g.strokeStyle = "#000"; g.beginPath(); g.moveTo(x, y + 3); g.lineTo(x + 5, y + ROW / 2); g.lineTo(x, y + ROW - 3); g.lineTo(x - 5, y + ROW / 2); g.closePath(); g.fill(); g.stroke(); } }
      const x = NAMEW + st.time * px; g.fillStyle = "#ff4a4a"; g.fillRect(x - 1, 0, 2, h); g.fillStyle = "#ff4a4a"; g.fillText(st.time.toFixed(2) + "s", Math.min(w - 44, x + 4), RULER - 4);
    }
  };
})();
