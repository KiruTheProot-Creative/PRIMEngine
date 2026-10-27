/* PRIMEngine Studio: the Wall Editor. A small 3D mesh editor for one wall type (a 96 x 96 panel = 1.5 m x 1.5 m).
   Tools: Select, Move, Extrude, Inset; Cut and Delete are buttons. Face mode / Vertex mode. Right mouse = orbit, wheel = zoom, Shift+right = pan. */
(function () {
  const PRIM = (window.PRIM = window.PRIM || {}), M4 = PRIM.M4, R = PRIM.R3D, W = PRIM.WallMesh, S = PRIM.Schema;
  const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2], add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]], sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]], mul = (a, k) => [a[0] * k, a[1] * k, a[2] * k];
  const norm = a => { const l = Math.hypot(a[0], a[1], a[2]) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };
  const AX = [[1, 0, 0], [0, 1, 0], [0, 0, 1]];
  class WallEditor {
    constructor(o) {
      this.o = o; this.project = o.project; this.T = o.tags; this.urls = o.urls; this.w = o.wall; this.mode = "face"; this.tool = "select"; this.axis = -1; this.cutT = 0.5; this.sel = { faces: new Set(), verts: new Set() };
      this.undos = []; this.orbit = { yaw: -Math.PI / 2 + 0.55, pitch: 0.38, dist: 230, target: [48, 48, 4] }; this.hint = "";
      const cv = this.cv = document.createElement("canvas"); cv.className = "v3cv"; cv.style.cursor = "crosshair"; o.host.replaceChildren(cv); this.v = new R.View(cv); if (!this.v.ok) return;
      this.v.sky = [0.09, 0.11, 0.15]; this.v.fog = [0.09, 0.11, 0.15]; this.v.fogD = 1e6; this.v.fov = 0.8; this.v.bright = 1;
      this.rebuild(); this.bind(); const loop = () => { this.raf = requestAnimationFrame(loop); this.draw(); }; this.raf = requestAnimationFrame(loop);
    }
    destroy() { cancelAnimationFrame(this.raf); document.removeEventListener("keydown", this.kd); if (this.v && this.v.destroy) this.v.destroy(); }
    // ---- drawing
    rebuild() {
      const by = {}, hi = [1, 0.62, 0.34];
      for (const t of W.tris(this.w, this.T)) { const sel = this.sel.faces.has(t.face), n = norm(cross3(sub(t.p[1], t.p[0]), sub(t.p[2], t.p[0]))), sh = 0.72 + 0.28 * Math.abs(dot(n, norm([0.3, 0.6, 0.7]))), a = by[t.tex] = by[t.tex] || [];
        for (let k = 0; k < 3; k++) a.push(t.p[k][0], t.p[k][1], t.p[k][2], t.uv[k][0], t.uv[k][1], t.tint[0] * sh * (sel ? hi[0] : 1), t.tint[1] * sh * (sel ? hi[1] : 1), t.tint[2] * sh * (sel ? hi[2] : 1)); }
      this.v.setBatches(by, this.urls); this.wire = null;
    }
    cam() { const o = this.orbit, c = this.v.cam, cp = Math.cos(o.pitch), f = [Math.cos(o.yaw) * cp, Math.sin(o.pitch), Math.sin(o.yaw) * cp]; c.x = o.target[0] - f[0] * o.dist; c.y = o.target[1] - f[1] * o.dist; c.z = o.target[2] - f[2] * o.dist; c.yaw = o.yaw; c.pitch = o.pitch; return f; }
    wireList() {
      if (this.wire) return this.wire; const l = [], w = this.w, seen = new Set(), cEdge = [0.55, 0.65, 0.8], cSel = [1, 0.6, 0.25], cGuide = [0.2, 0.75, 0.85], P = id => W.pos(w, id);
      const line = (a, b, c, th) => l.push({ kind: "line", a, b, c, w: th, life: 1e9, t: 0 });
      for (const f of w.faces) for (let i = 0; i < f.v.length; i++) { const a = f.v[i], b = f.v[(i + 1) % f.v.length], k = a < b ? a + "|" + b : b + "|" + a; if (seen.has(k)) continue; seen.add(k);
        const on = this.sel.faces.has(f.id) || (this.mode === "vert" && this.sel.verts.has(a) && this.sel.verts.has(b)) || w.faces.some(g => g !== f && this.sel.faces.has(g.id) && g.v.includes(a) && g.v.includes(b)); line(P(a), P(b), on ? cSel : cEdge, on ? 0.45 : 0.28); }
      if (this.mode === "vert") for (const v of w.verts) l.push({ kind: "sphere", p: [v.x, v.y, v.z], c: this.sel.verts.has(v.id) ? cSel : [0.8, 0.85, 0.95], size: this.sel.verts.has(v.id) ? 1.9 : 1.3 });
      const S_ = W.SIZE; for (const [a, b] of [[[0, 0, 0], [S_, 0, 0]], [[S_, 0, 0], [S_, S_, 0]], [[S_, S_, 0], [0, S_, 0]], [[0, S_, 0], [0, 0, 0]]]) line(a, b, cGuide, 0.22);
      for (let i = 1; i < 3; i++) { line([i * 32, 0, 0], [i * 32, S_, 0], [0.18, 0.26, 0.32], 0.1); line([0, i * 32, 0], [S_, i * 32, 0], [0.18, 0.26, 0.32], 0.1); }
      return (this.wire = l);
    }
    draw() { if (!this.v.ok) return; if (this.o.hintEl) this.o.hintEl.textContent = this.hint || ""; this.cam(); this.v.draw(); this.v.drawFx(this.wireList()); }
    // ---- picking
    basis() { const o = this.orbit; return R.camBasis(o.yaw, o.pitch); }
    ray(e) { const r = this.cv.getBoundingClientRect(), t = Math.tan(this.v.fov / 2), asp = r.width / r.height, nx = ((e.clientX - r.left) / r.width * 2 - 1) * t * asp, ny = (1 - (e.clientY - r.top) / r.height * 2) * t, B = this.basis(), c = this.v.cam;
      return { o: [c.x, c.y, c.z], d: norm([B.f[0] + B.s[0] * nx + B.u[0] * ny, B.f[1] + B.s[1] * nx + B.u[1] * ny, B.f[2] + B.s[2] * nx + B.u[2] * ny]) }; }
    screen(p) { const m = this.v.vp, w = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15]; if (w <= 0.01) return null; const x = (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]) / w, y = (m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]) / w, r = this.cv.getBoundingClientRect(); return [r.left + (x * 0.5 + 0.5) * r.width, r.top + (0.5 - y * 0.5) * r.height]; }
    pickFace(e) {
      const { o, d } = this.ray(e); let best = 1e9, hit = null;
      for (const f of this.w.faces) { const L = f.v.map(id => W.pos(this.w, id)); for (let i = 1; i + 1 < L.length; i++) { const t = rayTri(o, d, L[0], L[i], L[i + 1]); if (t !== null && t < best) { best = t; hit = f.id; } } }
      return hit;
    }
    pickVert(e) { let best = 14, hit = null; for (const v of this.w.verts) { const s = this.screen([v.x, v.y, v.z]); if (!s) continue; const dd = Math.hypot(s[0] - e.clientX, s[1] - e.clientY); if (dd < best) { best = dd; hit = v.id; } } return hit; }
    // ---- selection
    selVerts() { if (this.mode === "vert") return [...this.sel.verts]; const s = new Set(); for (const f of this.w.faces) if (this.sel.faces.has(f.id)) f.v.forEach(i => s.add(i)); return [...s]; }
    hasSel() { return this.mode === "vert" ? this.sel.verts.size > 0 : this.sel.faces.size > 0; }
    clearSel() { this.sel.faces.clear(); this.sel.verts.clear(); }
    changeSel() { this.wire = null; this.rebuild(); this.o.onSelect && this.o.onSelect(); }
    pickInto(e, shift) {
      if (this.mode === "face") { const f = this.pickFace(e); if (!shift) this.sel.faces.clear(); if (f) { if (shift && this.sel.faces.has(f)) this.sel.faces.delete(f); else this.sel.faces.add(f); } }
      else { const v = this.pickVert(e); if (!shift) this.sel.verts.clear(); if (v) { if (shift && this.sel.verts.has(v)) this.sel.verts.delete(v); else this.sel.verts.add(v); } }
      this.changeSel();
    }
    boxSelect(x0, y0, x1, y1, shift) {
      const lo = [Math.min(x0, x1), Math.min(y0, y1)], hi = [Math.max(x0, x1), Math.max(y0, y1)], inside = p => { const s = this.screen(p); return s && s[0] >= lo[0] && s[0] <= hi[0] && s[1] >= lo[1] && s[1] <= hi[1]; };
      if (!shift) this.clearSel(); if (this.mode === "face") { for (const f of this.w.faces) if (inside(W.centre(this.w, f))) this.sel.faces.add(f.id); } else for (const v of this.w.verts) if (inside([v.x, v.y, v.z])) this.sel.verts.add(v.id);
      this.changeSel();
    }
    selectAll() { if (this.mode === "face") this.w.faces.forEach(f => this.sel.faces.add(f.id)); else this.w.verts.forEach(v => this.sel.verts.add(v.id)); this.changeSel(); }
    setMode(m) { this.mode = m; this.clearSel(); this.changeSel(); }
    setTool(t) { this.tool = t; this.axis = -1; this.o.onTool && this.o.onTool(); }
    // ---- editing (every change keeps an undo snapshot)
    push() { this.undos.push(W.snap(this.project, this.w)); if (this.undos.length > 60) this.undos.shift(); }
    undo() { const s = this.undos.pop(); if (!s) return; W.restore(this.project, this.w, s); const ok = new Set(this.w.faces.map(f => f.id)), okv = new Set(this.w.verts.map(v => v.id)); for (const f of [...this.sel.faces]) if (!ok.has(f)) this.sel.faces.delete(f); for (const v of [...this.sel.verts]) if (!okv.has(v)) this.sel.verts.delete(v); this.committed(); }
    committed() { this.wire = null; this.rebuild(); this.o.onChange && this.o.onChange(); this.o.onSelect && this.o.onSelect(); }
    cut(axis) { if (this.mode !== "face" || !this.sel.faces.size) return 0; this.push(); const n = W.cut(this.project, this.w, [...this.sel.faces], axis, this.cutT); if (!n) this.undos.pop(); else this.committed(); return n; }
    deleteSel() {
      if (!this.hasSel()) return; this.push(); const del = this.mode === "face" ? [...this.sel.faces] : this.w.faces.filter(f => f.v.some(i => this.sel.verts.has(i))).map(f => f.id);
      W.deleteFaces(this.project, this.w, del); this.clearSel(); this.committed();
    }
    setTexture(id) { for (const f of this.sel.faces) this.T.set(this.w.id + "." + f + ".tex", id || ""); this.rebuild(); this.o.onChange && this.o.onChange(); }
    setColor(c) { for (const f of this.sel.faces) this.T.set(this.w.id + "." + f + ".color", c); this.rebuild(); this.o.onChange && this.o.onChange(); }
    view(name) { const o = this.orbit; o.target = [48, 48, 4]; if (name === "front") { o.yaw = -Math.PI / 2; o.pitch = 0; } else if (name === "side") { o.yaw = Math.PI; o.pitch = 0; } else if (name === "top") { o.yaw = -Math.PI / 2; o.pitch = -1.55; } else { o.yaw = -Math.PI / 2 + 0.55; o.pitch = 0.38; } o.dist = 230; }
    // ---- mouse and keys
    bind() {
      const cv = this.cv; cv.addEventListener("contextmenu", e => e.preventDefault());
      cv.addEventListener("wheel", e => { e.preventDefault(); this.orbit.dist = Math.max(30, Math.min(900, this.orbit.dist * (e.deltaY > 0 ? 1.1 : 0.9))); }, { passive: false });
      cv.addEventListener("pointerdown", e => { cv.setPointerCapture(e.pointerId); this.down(e); }); cv.addEventListener("pointermove", e => this.move(e)); cv.addEventListener("pointerup", e => this.up(e));
      this.kd = e => { if (!this.o.active() || /INPUT|SELECT|TEXTAREA/.test((e.target || {}).tagName || "")) return; const k = e.key.toLowerCase();
        if ((e.ctrlKey || e.metaKey) && k === "z") { this.undo(); e.preventDefault(); return; } if (e.ctrlKey || e.metaKey) return;
        if (this.drag && "xyz".includes(k) && this.drag.kind === "move") { this.axis = this.axis === "xyz".indexOf(k) ? -1 : "xyz".indexOf(k); this.drag.moved && this.applyDrag(this.drag.last); this.o.onTool && this.o.onTool(); return; }
        if (k === "escape" && this.drag) { W.restore(this.project, this.w, this.drag.undo); this.drag = null; this.committed(); return; }
        const m = { q: () => this.setTool("select"), g: () => this.setTool("move"), e: () => this.setTool("extrude"), i: () => this.setTool("inset"), "1": () => this.setMode("face"), "2": () => this.setMode("vert"), a: () => this.selectAll(), x: () => this.deleteSel(), delete: () => this.deleteSel(), f: () => this.view("iso") }[k]; if (m) { m(); e.preventDefault(); } };
      document.addEventListener("keydown", this.kd);
    }
    down(e) {
      this.last = [e.clientX, e.clientY];
      if (e.button === 2 || e.button === 1) { this.drag = { kind: e.shiftKey ? "pan" : "orbit" }; return; }
      if (e.button !== 0) return;
      if (this.tool === "select" || !this.hasSel()) { this.drag = { kind: "box", x: e.clientX, y: e.clientY, shift: e.shiftKey }; return; }
      this.begin(e);
    }
    begin(e) {
      const kind = this.tool, ids = this.mode === "face" ? [...this.sel.faces] : [], undo = W.snap(this.project, this.w); this.drag = { kind, undo, start: [e.clientX, e.clientY], last: e, moved: false, ids };
      if (kind === "extrude") { if (this.mode !== "face") { this.drag = null; return; } const r = W.extrude(this.project, this.w, ids); this.drag.moveIds = r.moved; this.drag.dir = r.dir; this.drag.kind = "move"; this.axisN = r.dir; this.axis = -2; this.drag.ex = true; }
      else if (kind === "move") { this.drag.moveIds = this.selVerts(); this.axisN = null; }
      this.drag.base = W.snap(this.project, this.w); const c = [0, 0, 0], ps = (this.drag.moveIds || []).map(id => W.pos(this.w, id)); for (const p of ps) { c[0] += p[0]; c[1] += p[1]; c[2] += p[2]; } this.drag.c0 = ps.length ? mul(c, 1 / ps.length) : c;
      const B = this.basis(), r = this.ray(e), t = dot(sub(this.drag.c0, r.o), B.f) / (dot(r.d, B.f) || 1); this.drag.p0 = add(r.o, mul(r.d, t)); this.drag.s0 = this.axisParam(e);
    }
    // where the mouse is along the locked axis (null when we look straight along it)
    axisParam(e) {
      const n = this.axisN || (this.axis >= 0 ? AX[this.axis] : null); if (!n || !this.drag) return null; const r = this.ray(e), w0 = sub(this.drag.c0, r.o), b = dot(n, r.d), den = 1 - b * b; if (den < 0.04) return null; return (b * dot(r.d, w0) - dot(n, w0)) / den;
    }
    applyDrag(e) {
      const d = this.drag; if (!d || !d.base) return; d.last = e; d.moved = true; W.restore(this.project, this.w, d.base); const shift = e.shiftKey, q = v => shift ? v : Math.round(v / 4) * 4;
      if (d.kind === "move") {
        let delta; const n = this.axisN || (this.axis >= 0 ? AX[this.axis] : null);
        if (n) { let s = this.axisParam(e); s = s === null ? (d.start[1] - e.clientY) * 0.5 : s - (d.s0 || 0); delta = mul(n, q(s)); }
        else { const B = this.basis(), r = this.ray(e), t = dot(sub(d.c0, r.o), B.f) / (dot(r.d, B.f) || 1), p = add(r.o, mul(r.d, t)); delta = sub(p, d.p0).map(q); }
        for (const id of d.moveIds) { const v = W.vert(this.w, id); if (v) { v.x += delta[0]; v.y += delta[1]; v.z += delta[2]; } }
        this.hint = (d.ex ? "Extrude " : "Move ") + delta.map(x => x.toFixed(0)).join(", ") + (this.axis >= 0 ? "   axis " + "XYZ"[this.axis] : "");
      } else if (d.kind === "inset") { const amt = Math.max(0, q((e.clientX - d.start[0]) * 0.3)); W.inset(this.project, this.w, d.ids, amt); this.hint = "Inset " + amt.toFixed(0); }
      this.wire = null; this.rebuild();
    }
    move(e) {
      const d = this.drag; if (!d) return;
      if (d.kind === "orbit" || d.kind === "pan") { const dx = e.clientX - this.last[0], dy = e.clientY - this.last[1]; this.last = [e.clientX, e.clientY]; const o = this.orbit;
        if (d.kind === "orbit") { o.yaw += dx * 0.008; o.pitch = Math.max(-1.55, Math.min(1.55, o.pitch + dy * 0.008)); } else { const B = this.basis(), k = o.dist * 0.0016; o.target = add(o.target, add(mul(B.s, -dx * k), mul(B.u, dy * k))); } return; }
      if (d.kind === "box") { d.cx = e.clientX; d.cy = e.clientY; return; }
      if (d.base) this.applyDrag(e);
    }
    up(e) {
      const d = this.drag; this.drag = null; this.hint = ""; if (!d) return;
      if (d.kind === "box") { if (Math.hypot(e.clientX - d.x, e.clientY - d.y) < 5) this.pickInto(e, d.shift); else this.boxSelect(d.x, d.y, e.clientX, e.clientY, d.shift); return; }
      if (d.base) { if (d.moved) { this.undos.push(d.undo); this.committed(); } else { W.restore(this.project, this.w, d.undo); this.committed(); } }
    }
  }
  function cross3(a, b) { return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]; }
  function rayTri(o, d, a, b, c) { const e1 = sub(b, a), e2 = sub(c, a), p = cross3(d, e2), det = dot(e1, p); if (Math.abs(det) < 1e-9) return null; const inv = 1 / det, t = sub(o, a), u = dot(t, p) * inv; if (u < 0 || u > 1) return null; const q = cross3(t, e1), v = dot(d, q) * inv; if (v < 0 || u + v > 1) return null; const s = dot(e2, q) * inv; return s > 0 ? s : null; }
  PRIM.WallEditor = WallEditor;
})();
