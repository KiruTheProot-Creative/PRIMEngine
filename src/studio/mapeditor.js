/* PRIMEngine Studio: the DOOM-style map editor. A top-down grid. Rooms (sectors) are closed loops of connected nodes.
   Click a node or a wall to select it; the Inspector assigns its texture. Geometry lives in the project's map object;
   every other value (heights, textures, light...) is a tag. */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), Schema = PRIM.Schema;
  const css = () => "";
  const segDist = (px, py, a, b) => { const dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy; let t = l2 ? ((px - a.x) * dx + (py - a.y) * dy) / l2 : 0; t = Math.max(0, Math.min(1, t)); const x = a.x + t * dx, y = a.y + t * dy; return { d: Math.hypot(px - x, py - y), t, x, y }; };
  const inPoly = (px, py, pts) => { let c = false; for (let i = 0, j = pts.length - 1; i < pts.length; j = i++) { if ((pts[i].y > py) !== (pts[j].y > py) && px < (pts[j].x - pts[i].x) * (py - pts[i].y) / (pts[j].y - pts[i].y) + pts[i].x) c = !c; } return c; };

  const ME = (PRIM.MapEditor = function (o) {
    this.o = o; this.m = o.map; this.T = o.tags; this.tool = "select"; this.sel = null; this.draft = []; this.mouse = null; this.v = { cx: 0, cy: 0, z: 1.2 }; this.imgs = {}; this.dpr = Math.min(2, window.devicePixelRatio || 1); this.drag = null; this.pan = null; this.space = false;
    const h = (t, a, ...k) => { const e = document.createElement(t); for (const n in a || {}) { if (n === "text") e.textContent = a[n]; else if (n.startsWith("on")) e[n] = a[n]; else e.setAttribute(n, a[n]); } for (const c of k.flat()) if (c) e.append(c); return e; };
    this.h = h; this.wrap = h("div", { class: "med", tabindex: "0" }); this.bar = h("div", { class: "medbar" }); this.cv = h("canvas", { class: "medcv" }); this.wrap.append(this.bar, this.cv); o.host.replaceChildren(this.wrap);
    this.cx = this.cv.getContext("2d"); this.buildBar();
    this.ro = new ResizeObserver(() => this.redraw()); this.ro.observe(this.cv);
    this.unsub = this.T.on(() => this.redraw());
    const cv = this.cv; cv.addEventListener("pointerdown", e => this.down(e)); cv.addEventListener("pointermove", e => this.move(e)); cv.addEventListener("pointerup", e => this.up(e)); cv.addEventListener("pointerleave", () => { this.mouse = null; this.redraw(); });
    cv.addEventListener("wheel", e => this.wheel(e), { passive: false }); cv.addEventListener("dblclick", e => this.dbl(e)); cv.addEventListener("contextmenu", e => e.preventDefault());
    this.wrap.addEventListener("keydown", e => this.key(e)); this.wrap.addEventListener("keyup", e => { if (e.key === " ") this.space = false; });
    this.fit(); setTimeout(() => this.fit(), 0);
  });
  const P = ME.prototype;

  P.buildBar = function () {
    const h = this.h, mk = (id, label, tip) => h("button", { class: "tbtn sm" + (this.tool === id ? " on" : ""), title: tip, text: label, onclick: () => this.setTool(id) });
    this.bar.replaceChildren(mk("select", "Select / Move", "V: click nodes, walls and rooms. Drag a node to move it. Double-click a wall to add a node."), mk("draw", "Draw Room", "D: click to place nodes, click the first node (or press Enter) to close the room."),
      mk("start", "Player Start", "P: click to put the player start point."), h("span", { class: "sp" }), h("button", { class: "tbtn sm", text: "Zoom to fit", title: "F", onclick: () => this.fit() }),
      h("span", { class: "hint", text: this.tool === "draw" ? "Click to place nodes · click the first node or press Enter to close · Backspace undoes · Esc cancels" : this.tool === "start" ? "Click where the player starts" : "Click a node or wall to select it · drag a node to move it · double-click a wall to add a node · drag empty space or hold the right mouse button to pan · scroll to zoom" }));
  };
  P.setTool = function (t) { this.tool = t; this.draft = []; this.buildBar(); this.redraw(); };
  P.destroy = function () { this.ro.disconnect(); this.unsub(); };
  P.grid = function () { return Math.max(1, +this.T.get(this.m.id + ".gridSize") || 32); };
  P.vmap = function () { const o = {}; for (const v of this.m.verts) o[v.id] = v; return o; };
  P.sx = function (x) { return (x - this.v.cx) * this.v.z + this.cv.width / 2 / this.dpr; };
  P.sy = function (y) { return (y - this.v.cy) * this.v.z + this.cv.height / 2 / this.dpr; };
  P.wx = function (sx) { return (sx - this.cv.width / 2 / this.dpr) / this.v.z + this.v.cx; };
  P.wy = function (sy) { return (sy - this.cv.height / 2 / this.dpr) / this.v.z + this.v.cy; };
  P.fit = function () {
    const vs = this.m.verts, w = this.cv.clientWidth || 600, hh = this.cv.clientHeight || 400; if (!vs.length) { this.v = { cx: 0, cy: 0, z: 1.2 }; return this.redraw(); }
    const x0 = Math.min(...vs.map(v => v.x)), x1 = Math.max(...vs.map(v => v.x)), y0 = Math.min(...vs.map(v => v.y)), y1 = Math.max(...vs.map(v => v.y));
    this.v = { cx: (x0 + x1) / 2, cy: (y0 + y1) / 2, z: Math.max(0.05, Math.min(4, 0.8 * Math.min(w / Math.max(64, x1 - x0), hh / Math.max(64, y1 - y0)))) }; this.redraw();
  };
  P.img = function (id) { const urls = this.o.getUrls ? this.o.getUrls() : this.o.urls || {}; if (!id || !urls[id]) return null; let e = this.imgs[id]; if (!e || e.__u !== urls[id]) { e = this.imgs[id] = new Image(); e.__u = urls[id]; e.onload = () => this.redraw(); e.src = urls[id]; } return e.complete && e.naturalWidth ? e : null; };

  // ---- picking
  P.pick = function (sx, sy) {
    const vs = this.vmap(), px = this.wx(sx), py = this.wy(sy), r = 9 / this.v.z, secs = this.m.sectors;
    if (this.sel && this.sel.kind === "start") { /* start handled below */ }
    const sx0 = +this.T.get(this.m.id + ".startX"), sy0 = +this.T.get(this.m.id + ".startY"); if (Math.hypot(px - sx0, py - sy0) < r * 1.3) return { kind: "start" };
    let best = null;
    for (const s of secs) for (const id of s.v) { const v = vs[id]; if (!v) continue; const d = Math.hypot(v.x - px, v.y - py); if (d < r && (!best || d < best.d)) best = { d, kind: "vert", id }; }
    if (best) return best;
    for (const s of secs) for (let i = 0; i < s.v.length; i++) { const a = vs[s.v[i]], b = vs[s.v[(i + 1) % s.v.length]]; if (!a || !b) continue; const r2 = segDist(px, py, a, b); if (r2.d < 6 / this.v.z && (!best || r2.d < best.d)) best = { d: r2.d, kind: "vert", id: a.id, edge: true }; }
    if (best) return best;
    for (let i = secs.length - 1; i >= 0; i--) { const pts = secs[i].v.map(id => vs[id]).filter(Boolean); if (pts.length > 2 && inPoly(px, py, pts)) return { kind: "sector", id: secs[i].id }; }
    return null;
  };
  P.snapPt = function (sx, sy, noVert) {
    const g = this.grid(), px = this.wx(sx), py = this.wy(sy);
    if (!noVert) { let best = null; for (const v of this.m.verts) { const d = Math.hypot(v.x - px, v.y - py) * this.v.z; if (d < 10 && (!best || d < best.d)) best = { d, x: v.x, y: v.y }; } if (best) return { x: best.x, y: best.y, vert: true }; }
    return { x: Math.round(px / g) * g, y: Math.round(py / g) * g };
  };
  P.select = function (sel, quiet) { this.sel = sel; if (!quiet && this.o.onSelect) this.o.onSelect(sel); this.redraw(); };
  P.rel = function (e) { const r = this.cv.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; };

  // ---- pointer
  P.down = function (e) {
    this.wrap.focus(); const [sx, sy] = this.rel(e); this.cv.setPointerCapture(e.pointerId);
    if (e.button === 1 || e.button === 2 || this.space) { this.pan = { x: e.clientX, y: e.clientY, cx: this.v.cx, cy: this.v.cy }; return; }
    if (e.button !== 0) return;
    if (this.tool === "draw") {
      const p = this.snapPt(sx, sy, e.altKey), f = this.draft[0], last = this.draft[this.draft.length - 1];
      if (f && this.draft.length >= 3 && Math.hypot(f.x - p.x, f.y - p.y) * this.v.z < 10) return this.closeDraft();
      if (last && last.x === p.x && last.y === p.y) return; this.draft.push({ x: p.x, y: p.y }); return this.redraw();
    }
    if (this.tool === "start") { this.T.setMany({ [this.m.id + ".startX"]: Math.round(this.wx(sx)), [this.m.id + ".startY"]: Math.round(this.wy(sy)) }); if (this.o.onChange) this.o.onChange(); this.select({ kind: "start" }); return; }
    const hit = this.pick(sx, sy);
    if (hit && hit.kind === "vert" && !hit.edge) { const v = this.vmap()[hit.id]; this.select({ kind: "vert", id: hit.id }); this.drag = { ids: this.m.verts.filter(o => o.x === v.x && o.y === v.y).map(o => o.id), moved: false }; }
    else if (hit && hit.kind === "start") { this.select({ kind: "start" }); this.drag = { start: true, moved: false }; }
    else if (hit) { this.select(hit.kind === "vert" ? { kind: "vert", id: hit.id } : hit); }
    else { this.select(null); this.pan = { x: e.clientX, y: e.clientY, cx: this.v.cx, cy: this.v.cy }; }
  };
  P.move = function (e) {
    const [sx, sy] = this.rel(e); this.mouse = { sx, sy };
    if (this.pan) { this.v.cx = this.pan.cx - (e.clientX - this.pan.x) / this.v.z; this.v.cy = this.pan.cy - (e.clientY - this.pan.y) / this.v.z; }
    else if (this.drag) {
      const p = this.snapPt(sx, sy, true), vs = this.vmap();
      if (this.drag.start) { this.T.setMany({ [this.m.id + ".startX"]: Math.round(this.wx(sx)), [this.m.id + ".startY"]: Math.round(this.wy(sy)) }); this.drag.moved = true; }
      else for (const id of this.drag.ids) { if (vs[id].x !== p.x || vs[id].y !== p.y) { vs[id].x = p.x; vs[id].y = p.y; this.drag.moved = true; } }
    }
    this.redraw();
  };
  P.up = function () { const d = this.drag; this.pan = null; this.drag = null; if (d && d.moved && this.o.onChange) this.o.onChange(true); };
  P.wheel = function (e) {
    e.preventDefault(); const [sx, sy] = this.rel(e), bx = this.wx(sx), by = this.wy(sy); this.v.z = Math.max(0.04, Math.min(12, this.v.z * (e.deltaY < 0 ? 1.15 : 1 / 1.15)));
    this.v.cx = bx - (sx - this.cv.width / 2 / this.dpr) / this.v.z; this.v.cy = by - (sy - this.cv.height / 2 / this.dpr) / this.v.z; this.redraw();
  };
  P.dbl = function (e) {
    if (this.tool === "draw") return this.closeDraft(); if (this.tool !== "select") return;
    const [sx, sy] = this.rel(e), hit = this.pick(sx, sy); if (!hit || !hit.edge) return;
    const vs = this.vmap(), sec = this.m.sectors.find(s => s.v.includes(hit.id)); if (!sec) return; const i = sec.v.indexOf(hit.id), a = vs[hit.id], b = vs[sec.v[(i + 1) % sec.v.length]];
    const r = segDist(this.wx(sx), this.wy(sy), a, b), g = this.grid(); let x = Math.round(r.x / g) * g, y = Math.round(r.y / g) * g; if (segDist(x, y, a, b).d > g) { x = Math.round(r.x); y = Math.round(r.y); }
    const over = {}; for (const p of Schema.VERT) over[p.k] = this.T.get(this.m.id + "." + a.id + "." + p.k);
    const nv = Schema.addVert(this.o.project, this.m, x, y, over); sec.v.splice(i + 1, 0, nv.id); this.T.emit([]); this.select({ kind: "vert", id: nv.id }); if (this.o.onChange) this.o.onChange(true, true);
  };
  P.closeDraft = function () {
    const d = this.draft; this.draft = []; if (d.length < 3) return this.redraw();
    const ids = d.map(p => Schema.addVert(this.o.project, this.m, p.x, p.y).id), sec = Schema.addSector(this.o.project, this.m, ids);
    this.T.emit([]); this.tool = "select"; this.buildBar(); this.select({ kind: "sector", id: sec.id }); if (this.o.onChange) this.o.onChange(true, true); if (this.o.log) this.o.log("Created room “" + sec.id + "” with " + d.length + " nodes");
  };
  P.deleteSelection = function () {
    if (this.draft.length) { this.draft.pop(); return this.redraw(); } const s = this.sel; if (!s) return;
    if (s.kind === "sector") { const sec = this.m.sectors.find(x => x.id === s.id); if (sec) Schema.removeSector(this.o.project, this.m, sec); }
    else if (s.kind === "vert") { const sec = this.m.sectors.find(x => x.v.includes(s.id)); if (!sec) return;
      if (sec.v.length <= 3) { this.o.log && this.o.log("A room needs at least 3 nodes. Select the room itself and press Delete to remove it.", "warn"); return; }
      sec.v.splice(sec.v.indexOf(s.id), 1); this.m.verts.splice(this.m.verts.findIndex(v => v.id === s.id), 1); this.T.removePrefix(this.m.id + "." + s.id); }
    else return;
    this.sel = null; this.T.emit([]); if (this.o.onSelect) this.o.onSelect(null); if (this.o.onChange) this.o.onChange(true, true); this.redraw();
  };
  P.key = function (e) {
    const k = e.key.toLowerCase(); if (/INPUT|SELECT|TEXTAREA/.test(e.target.tagName)) return;
    if (k === " ") { this.space = true; e.preventDefault(); } else if (k === "v") this.setTool("select"); else if (k === "d" && !e.ctrlKey && !e.metaKey) this.setTool("draw"); else if (k === "p") this.setTool("start"); else if (k === "f") this.fit();
    else if (k === "enter" && this.tool === "draw") this.closeDraft(); else if (k === "escape") { if (this.draft.length) { this.draft = []; this.redraw(); } else this.select(null); }
  };

  // ---- drawing
  let raf = 0;
  P.redraw = function () { if (this.pending) return; this.pending = true; requestAnimationFrame(() => { this.pending = false; this.paint(); }); };
  P.paint = function () {
    const cv = this.cv, dpr = (this.dpr = Math.min(2, window.devicePixelRatio || 1)), W = cv.clientWidth, H = cv.clientHeight; if (W < 2 || H < 2) return;
    if (cv.width !== Math.floor(W * dpr) || cv.height !== Math.floor(H * dpr)) { cv.width = Math.floor(W * dpr); cv.height = Math.floor(H * dpr); }
    const c = this.cx, vs = this.vmap(), T = this.T, mid = this.m.id; c.setTransform(dpr, 0, 0, dpr, 0, 0);
    c.fillStyle = css("--mapbg") || "#10161d"; c.fillRect(0, 0, W, H);
    // grid
    const g = this.grid(), gp = g * this.v.z, x0 = this.wx(0), x1 = this.wx(W), y0 = this.wy(0), y1 = this.wy(H);
    for (const [step, col] of [[g, css("--mapgrid") || "#1c2733"], [g * 2, css("--mapgrid2") || "#2a3a4c"]]) { if (step * this.v.z < 7) continue; c.strokeStyle = col; c.lineWidth = 1; c.beginPath();
      for (let x = Math.floor(x0 / step) * step; x <= x1; x += step) { const s = Math.round(this.sx(x)) + 0.5; c.moveTo(s, 0); c.lineTo(s, H); } for (let y = Math.floor(y0 / step) * step; y <= y1; y += step) { const s = Math.round(this.sy(y)) + 0.5; c.moveTo(0, s); c.lineTo(W, s); } c.stroke(); }
    c.strokeStyle = "#3d6a8c"; c.lineWidth = 1.5; c.beginPath(); c.moveTo(this.sx(0), 0); c.lineTo(this.sx(0), H); c.moveTo(0, this.sy(0)); c.lineTo(W, this.sy(0)); c.stroke();
    const selSec = this.sel && this.sel.kind === "sector" ? this.sel.id : null;
    // rooms
    for (const s of this.m.sectors) {
      const pts = s.v.map(id => vs[id]).filter(Boolean); if (pts.length < 3) continue; const pre = mid + "." + s.id + ".", img = this.img(T.get(pre + "floorTex"));
      c.beginPath(); pts.forEach((p, i) => i ? c.lineTo(this.sx(p.x), this.sy(p.y)) : c.moveTo(this.sx(p.x), this.sy(p.y))); c.closePath();
      c.fillStyle = T.get(pre + "floorColor"); c.globalAlpha = 0.42; c.fill(); c.globalAlpha = 1;
      if (img) { try { c.save(); c.clip(); c.globalAlpha = 0.55; c.imageSmoothingEnabled = false; const ts = 64 * this.v.z; if (ts > 3) { const ox = this.sx(0), oy = this.sy(0); for (let x = Math.floor((this.sx(Math.min(...pts.map(p => p.x))) - ox) / ts) * ts + ox; x < this.sx(Math.max(...pts.map(p => p.x))); x += ts) for (let y = Math.floor((this.sy(Math.min(...pts.map(p => p.y))) - oy) / ts) * ts + oy; y < this.sy(Math.max(...pts.map(p => p.y))); y += ts) c.drawImage(img, x, y, ts + 0.5, ts + 0.5); } c.restore(); } catch (x) { c.restore(); } }
      if (s.id === selSec) { c.fillStyle = "rgba(255,210,90,.18)"; c.fill(); }
    }
    // walls
    for (const s of this.m.sectors) {
      const n = s.v.length, selV = this.sel && this.sel.kind === "vert" ? this.sel.id : null;
      for (let i = 0; i < n; i++) {
        const a = vs[s.v[i]], b = vs[s.v[(i + 1) % n]]; if (!a || !b) continue; const vp = mid + "." + a.id + ".", open = T.get(vp + "open"), isSel = selV === a.id || s.id === selSec;
        c.beginPath(); c.moveTo(this.sx(a.x), this.sy(a.y)); c.lineTo(this.sx(b.x), this.sy(b.y)); c.setLineDash(open ? [5, 5] : []);
        c.strokeStyle = isSel ? (selV === a.id ? "#ffd25a" : "#ffe9a8") : open ? "#6c7a8a" : T.get(vp + "color"); c.lineWidth = selV === a.id ? 4 : isSel ? 3 : 2.4; c.stroke(); c.setLineDash([]);
        const mx = (this.sx(a.x) + this.sx(b.x)) / 2, my = (this.sy(a.y) + this.sy(b.y)) / 2;
        if (!open && Math.hypot(this.sx(b.x) - this.sx(a.x), this.sy(b.y) - this.sy(a.y)) > 38) { const ti = this.img(T.get(vp + "tex")); c.fillStyle = T.get(vp + "color"); c.fillRect(mx - 6, my - 6, 12, 12); if (ti) { c.imageSmoothingEnabled = false; c.drawImage(ti, mx - 6, my - 6, 12, 12); } c.strokeStyle = "#0008"; c.lineWidth = 1; c.strokeRect(mx - 6.5, my - 6.5, 13, 13); }
      }
    }
    // nodes
    for (const s of this.m.sectors) for (const id of s.v) { const v = vs[id]; if (!v) continue; const on = this.sel && this.sel.kind === "vert" && this.sel.id === id; c.fillStyle = on ? "#ffd25a" : "#e7eef6"; c.strokeStyle = "#000a"; c.lineWidth = 1; const r = on ? 5 : 3.5; c.fillRect(this.sx(v.x) - r, this.sy(v.y) - r, 2 * r, 2 * r); c.strokeRect(this.sx(v.x) - r - 0.5, this.sy(v.y) - r - 0.5, 2 * r + 1, 2 * r + 1); }
    // labels
    c.font = "11px ui-monospace,Consolas,monospace"; c.textAlign = "center";
    for (const s of this.m.sectors) { const pts = s.v.map(id => vs[id]).filter(Boolean); if (pts.length < 3 || this.v.z < 0.35) continue; const cx = pts.reduce((q, p) => q + p.x, 0) / pts.length, cy = pts.reduce((q, p) => q + p.y, 0) / pts.length, pre = mid + "." + s.id + ".";
      c.fillStyle = "#cfe0f0"; c.fillText(s.id, this.sx(cx), this.sy(cy) - 2); c.fillStyle = "#8fa6bd"; c.fillText(T.get(pre + "floor") + " … " + T.get(pre + "ceil"), this.sx(cx), this.sy(cy) + 11); }
    // player start
    const px = this.sx(+T.get(mid + ".startX")), py = this.sy(+T.get(mid + ".startY")), ang = (+T.get(mid + ".startAngle") || 0) * Math.PI / 180, st = this.sel && this.sel.kind === "start";
    c.save(); c.translate(px, py); c.rotate(ang); c.fillStyle = st ? "#ffd25a" : "#5fe0a0"; c.strokeStyle = "#000b"; c.lineWidth = 1.5; c.beginPath(); c.moveTo(13, 0); c.lineTo(-8, 8); c.lineTo(-4, 0); c.lineTo(-8, -8); c.closePath(); c.fill(); c.stroke(); c.restore();
    // draft
    if (this.draft.length) {
      const m = this.mouse ? this.snapPt(this.mouse.sx, this.mouse.sy) : null; c.strokeStyle = "#7fd0ff"; c.lineWidth = 2; c.beginPath(); this.draft.forEach((p, i) => i ? c.lineTo(this.sx(p.x), this.sy(p.y)) : c.moveTo(this.sx(p.x), this.sy(p.y))); if (m) c.lineTo(this.sx(m.x), this.sy(m.y)); c.stroke();
      c.fillStyle = "#7fd0ff"; for (const p of this.draft) c.fillRect(this.sx(p.x) - 3, this.sy(p.y) - 3, 6, 6); const f = this.draft[0]; if (this.draft.length >= 3) { c.strokeStyle = "#ffd25a"; c.strokeRect(this.sx(f.x) - 7, this.sy(f.y) - 7, 14, 14); }
    } else if (this.tool === "draw" && this.mouse) { const m = this.snapPt(this.mouse.sx, this.mouse.sy); c.strokeStyle = "#7fd0ff"; c.strokeRect(this.sx(m.x) - 4, this.sy(m.y) - 4, 8, 8); }
    // readout
    c.textAlign = "left"; c.fillStyle = "#8fa6bd"; const m2 = this.mouse ? this.snapPt(this.mouse.sx, this.mouse.sy, true) : null;
    const mt = n => (n / 64).toFixed(2).replace(/\.?0+$/, "") + " m"; c.fillText("tile " + mt(g) + " (" + g + " units)" + (m2 ? "   x " + mt(m2.x) + "  y " + mt(m2.y) : "") + "   zoom " + Math.round(this.v.z * 100) + "%   " + this.m.sectors.length + " room(s)", 8, H - 8);
  };
})();
