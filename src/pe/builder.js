/* PRIMEngine (Prime edition) 3D room builder.
   Camera: hold the RIGHT mouse button + W A S D to fly (Q / E down / up, Shift fast), wheel moves forward / back, middle drag pans.
   Tools: Select (V) · Box (B) · Ramp (N) · Place object (O, the kind chosen in the toolbar).
     Box / Ramp: drag a rectangle on the floor (or on top of anything) -> a block with the height set in the toolbar; a ramp rises in the direction you drag.
     Select: click a block / object; drag it to move it on the grid (Shift = up / down); drag a yellow handle to resize; R turns it.
   Keys: Delete removes, Ctrl+D duplicates, Ctrl+Z / Ctrl+Y undo / redo, [ ] move the work plane down / up, F frames the selection.
   Options: { host, project, tags, urls, log, onSelect(sel), onChange(structure) } */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), D = PRIM.PE, W = PRIM.PEWorld, M4 = PRIM.M4, R3D = PRIM.R3D, SH = PRIM.PEShapes;
  const { sub, add, mul, dot, len, norm, cross } = W.v, clamp = (v, a, b) => Math.max(a, Math.min(b, v)), DEG = Math.PI / 180;
  const mk = (tag, cls, txt) => { const e = document.createElement(tag); if (cls) e.className = cls; if (txt !== undefined) e.textContent = txt; return e; };
  if (!document.getElementById("pb-style")) {
    const s = mk("style"); s.id = "pb-style";
    s.textContent = `.pb{position:absolute;inset:0;outline:none;background:#0b1118}.pb canvas{position:absolute;inset:0;width:100%;height:100%;display:block}
.pb-hint{position:absolute;left:8px;bottom:6px;font:12px "Share Tech Mono",ui-monospace,monospace;color:#9fb4c8;text-shadow:0 1px 2px #000;pointer-events:none}
.pb-ov{position:absolute;inset:0;pointer-events:none}`;
    document.head.append(s);
  }
  const KIND_COL = { start: [0.3, 1, 0.6], light: [1, 0.9, 0.5], door: [0.3, 0.65, 1], pickup: [0.3, 0.9, 1], enemy: [1, 0.35, 0.25], scan: [1, 0.55, 0.15], model: [0.8, 0.8, 0.9] };

  PRIM.PEBuilder = class PEBuilder {
    constructor(o) {
      this.o = o; this.P = o.project; this.T = o.tags; this.urls = o.urls || {}; this.sel = o.sel || null; this.tool = "select"; this.place = "pickup"; this.boxH = 128; this.planeY = 0; this.keys = {}; this.gpu = {}; this.models = {};
      this.cam = o.cam || { x: 0, y: 520, z: -820, yaw: Math.PI / 2, pitch: -0.55 };
      this.root = mk("div", "pb"); this.root.tabIndex = 0; this.cv = mk("canvas"); this.ovc = mk("canvas", "pb-ov"); this.hint = mk("div", "pb-hint"); this.root.append(this.cv, this.ovc, this.hint); o.host.replaceChildren(this.root);
      this.view = new R3D.View(this.cv); if (!this.view.ok) { this.root.append(mk("div", "v3err", "WebGL could not start on this computer.")); return; } this.view.fov = 70 * DEG; this.view.fogD = 1e6;
      this.undoS = []; this.redoS = []; this.cur = this.ser(); this.rebuild();
      this.unsub = this.T.on(names => { if (this.restoring) return; clearTimeout(this.rt); this.rt = setTimeout(() => { this.rebuild(); this.commit(); }, 60); });
      this.bind(); this.last = performance.now(); this.raf = requestAnimationFrame(t => this.frame(t)); this.setTool(this.tool);
    }
    destroy() { this.dead = true; cancelAnimationFrame(this.raf); this.unsub && this.unsub(); clearTimeout(this.rt); if (this.unbind) this.unbind(); if (this.view && this.view.destroy) this.view.destroy(); }
    grid() { return Math.max(4, +this.T.get("world.grid") || 32); }
    snap(v) { const g = this.grid(); return Math.round(v / g) * g; }
    room() { const id = this.o.getRoom && this.o.getRoom(); return this.P.rooms.find(r => r.id === id) || this.P.rooms[0]; }
    setTool(t) { this.tool = t; this.hint.textContent = { select: "Select: click a block or object · drag to move (Shift = up/down) · yellow handles resize · R turns · Delete · Ctrl+D duplicate · Ctrl+Z undo · hold RIGHT mouse + WASD to fly", box: "Box: drag a rectangle on the floor or on top of a block · height " + this.boxH + " units (" + (this.boxH / 64).toFixed(2) + " m) · [ ] work plane", ramp: "Ramp: drag a rectangle; it rises in the direction you drag", place: "Place: click on a surface to put a " + D.THING_KINDS[this.place].label.toLowerCase() }[t] || ""; if (this.o.onTool) this.o.onTool(t); }
    // ---------------------------------------------------------------- undo / redo (the whole level and every tag)
    ser() { return JSON.stringify({ rooms: this.P.rooms, n: this.P.n, tags: this.P.tags }); }
    commit() { const s = this.ser(); if (s === this.cur) return; this.undoS.push(this.cur); if (this.undoS.length > 60) this.undoS.shift(); this.redoS = []; this.cur = s; }
    history(back) { this.commit(); const from = back ? this.undoS : this.redoS, to = back ? this.redoS : this.undoS, s = from.pop(); if (!s) { this.o.log && this.o.log(back ? "Nothing to undo." : "Nothing to redo."); return; }
      to.push(this.cur); this.cur = s; const d = JSON.parse(s); this.restoring = true; this.P.rooms = d.rooms; this.P.n = d.n; for (const k of Object.keys(this.P.tags)) delete this.P.tags[k]; Object.assign(this.P.tags, d.tags); this.T.emit([]); this.restoring = false;
      this.select(null); this.rebuild(); if (this.o.onChange) this.o.onChange(true); }
    changed(structure) { this.rebuild(); this.commit(); if (this.o.onChange) this.o.onChange(structure); }
    // ---------------------------------------------------------------- geometry
    rebuild() { if (!this.view.ok) return; this.world = W.build(this.P, this.T, { editor: true }); this.view.setWorld(this.world.batches, this.urls); this.view.sky = W.hex(this.T.get("world.sky"), "#0b1622").map(q => q * 0.8 + 0.03); }
    find(id) { for (const r of this.P.rooms) { const b = r.brushes.find(x => x.id === id); if (b) return { room: r, brush: b }; const t = r.things.find(x => x.id === id); if (t) return { room: r, thing: t }; } return null; }
    select(sel) { this.sel = sel; if (this.o.onSelect) this.o.onSelect(sel); }
    // ---------------------------------------------------------------- picking
    rel(e) { const r = this.cv.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; }
    ray(sx, sy) { this.syncCam(); return this.view.rayAt(sx, sy); }
    thingCenter(t) { return [t.p[0], t.p[1] + (t.kind === "light" ? 0 : t.kind === "door" ? (+this.T.get(t.id + ".height") || 160) / 2 : t.kind === "start" ? 75 : 30), t.p[2]]; }
    thingRadius(t) { return t.kind === "door" ? (+this.T.get(t.id + ".width") || 128) / 2 : t.kind === "enemy" ? Math.max(30, +this.T.get(t.id + ".radius") || 28) : t.kind === "start" ? 40 : 26; }
    pick(sx, sy) {
      const r = this.ray(sx, sy), h = W.raycast(this.world.pickGrid, r.o, r.d, 1e6); let best = h ? { kind: "brush", id: h.tri.id, face: h.tri.face, t: h.t, p: h.p, n: h.tri.n } : null;
      for (const room of this.P.rooms) for (const t of room.things) { const c = this.thingCenter(t), rr = this.thingRadius(t), oc = sub(r.o, c), b = dot(oc, r.d), cc = dot(oc, oc) - rr * rr, disc = b * b - cc; if (disc < 0) continue; const tt = -b - Math.sqrt(disc); if (tt > 0 && (!best || tt < best.t)) best = { kind: "thing", id: t.id, t: tt, p: add(r.o, mul(r.d, tt)) }; }
      return best;
    }
    planeHit(r, y) { if (Math.abs(r.d[1]) < 1e-5) return null; const t = (y - r.o[1]) / r.d[1]; return t > 0 ? add(r.o, mul(r.d, t)) : null; }
    toScreen(p) { const vp = this.view.vp; if (!vp) return null; const x = vp[0] * p[0] + vp[4] * p[1] + vp[8] * p[2] + vp[12], y = vp[1] * p[0] + vp[5] * p[1] + vp[9] * p[2] + vp[13], w = vp[3] * p[0] + vp[7] * p[1] + vp[11] * p[2] + vp[15]; if (w <= 0.01) return null; const r = this.cv.getBoundingClientRect(); return [(x / w * 0.5 + 0.5) * r.width, (1 - (y / w * 0.5 + 0.5)) * r.height]; }
    handles() {   // resize handles of the selected block: [axis, side, point]
      const f = this.sel && this.sel.kind === "brush" && this.find(this.sel.id); if (!f) return []; const b = f.brush, c = add(b.p, mul(b.s, 0.5)), out = [];
      for (let a = 0; a < 3; a++) for (const sd of [-1, 1]) { const p = c.slice(); p[a] = sd < 0 ? b.p[a] : b.p[a] + b.s[a]; out.push([a, sd, p]); } return out;
    }
    // ---------------------------------------------------------------- input
    bind() {
      const cv = this.cv, k = this.keys; let drag = null;
      const down = e => { this.root.focus(); cv.setPointerCapture(e.pointerId); const [sx, sy] = this.rel(e);
        if (e.button === 2) { this.look = true; try { const r = cv.requestPointerLock && cv.requestPointerLock(); if (r && r.catch) r.catch(() => {}); } catch (x) {} return; }
        if (e.button === 1) { drag = { pan: true, x: e.clientX, y: e.clientY }; return; } if (e.button !== 0) return;
        if (this.tool === "box" || this.tool === "ramp") { const r = this.ray(sx, sy), h = W.raycast(this.world.pickGrid, r.o, r.d, 1e6); let y = this.planeY; if (h && h.tri.n[1] > 0.7) y = Math.round(h.p[1]); const p = this.planeHit(r, y); if (!p) return; drag = this.drawing = { draw: true, y, a: [this.snap(p[0]), y, this.snap(p[2])], b: [this.snap(p[0]), y, this.snap(p[2])] }; return; }
        if (this.tool === "place") { const r = this.ray(sx, sy), h = W.raycast(this.world.pickGrid, r.o, r.d, 1e6), p = h && h.tri.n[1] > 0.5 ? h.p : this.planeHit(r, this.planeY); if (!p) return; this.placeAt([this.snap(p[0]), Math.round(p[1]), this.snap(p[2])]); return; }
        // select tool: a handle, or the thing under the mouse
        for (const [a, sd, p] of this.handles()) { const s = this.toScreen(p); if (s && Math.hypot(s[0] - sx, s[1] - sy) < 11) { const f = this.find(this.sel.id); drag = { handle: true, a, sd, brush: f.brush, p0: f.brush.p.slice(), s0: f.brush.s.slice(), axisP: p.slice(), moved: false }; return; } }
        const hit = this.pick(sx, sy); if (!hit) { this.select(null); drag = { pan: true, x: e.clientX, y: e.clientY }; return; }
        this.select({ kind: hit.kind, id: hit.id, face: hit.face || null }); const f = this.find(hit.id), obj = f.brush || f.thing; drag = { move: true, obj, p0: obj.p.slice(), grab: hit.p.slice(), vert: e.shiftKey, moved: false };
      };
      const move = e => {
        if (this.look && (document.pointerLockElement === cv || e.buttons & 2)) { this.cam.yaw += e.movementX * 0.0035; this.cam.pitch = clamp(this.cam.pitch - e.movementY * 0.0035, -1.5, 1.5); return; }
        if (!drag) return; const [sx, sy] = this.rel(e), r = this.ray(sx, sy);
        if (drag.pan) { const B = R3D.camBasis(this.cam.yaw, this.cam.pitch), sp = 1.4; this.cam.x -= (B.s[0] * (e.clientX - drag.x) - B.u[0] * (e.clientY - drag.y)) * sp; this.cam.y -= (B.s[1] * (e.clientX - drag.x) - B.u[1] * (e.clientY - drag.y)) * sp; this.cam.z -= (B.s[2] * (e.clientX - drag.x) - B.u[2] * (e.clientY - drag.y)) * sp; drag.x = e.clientX; drag.y = e.clientY; return; }
        if (drag.draw) { const p = this.planeHit(r, drag.y); if (p) drag.b = [this.snap(p[0]), drag.y, this.snap(p[2])]; return; }
        if (drag.handle) { const a = drag.a, axis = [0, 0, 0]; axis[a] = 1; const w = closestOnAxis(r, drag.axisP, axis), v = this.snap(w[a]), b = drag.brush, g = this.grid();
          if (drag.sd > 0) b.s[a] = Math.max(g, v - drag.p0[a]); else { const end = drag.p0[a] + drag.s0[a], nv = Math.min(v, end - g); b.p[a] = nv; b.s[a] = end - nv; } drag.moved = true; this.rebuild(); return; }
        if (drag.move) { if (drag.vert || e.shiftKey) { const B = R3D.camBasis(this.cam.yaw, this.cam.pitch), nrm = norm([B.f[0], 0, B.f[2]]), t = dot(sub(drag.grab, r.o), nrm) / (dot(r.d, nrm) || 1e-6); if (t > 0) { const p = add(r.o, mul(r.d, t)), dy = this.snap(p[1] - drag.grab[1]); if (drag.obj.p[1] !== drag.p0[1] + dy) { drag.obj.p[1] = drag.p0[1] + dy; drag.moved = true; } } }
          else { const p = this.planeHit(r, drag.grab[1]); if (p) { const dx = this.snap(p[0] - drag.grab[0]), dz = this.snap(p[2] - drag.grab[2]); if (drag.obj.p[0] !== drag.p0[0] + dx || drag.obj.p[2] !== drag.p0[2] + dz) { drag.obj.p[0] = drag.p0[0] + dx; drag.obj.p[2] = drag.p0[2] + dz; drag.moved = true; } } }
          if (drag.moved && drag.obj.shape) this.rebuild(); }
      };
      const up = e => {
        if (e.button === 2) { this.look = false; for (const id of "wasdqe") k[id] = false; try { if (document.pointerLockElement === cv) document.exitPointerLock(); } catch (x) {} return; }
        const d = drag; drag = null; this.drawing = null; if (!d) return;
        if (d.draw) { const x0 = Math.min(d.a[0], d.b[0]), x1 = Math.max(d.a[0], d.b[0]), z0 = Math.min(d.a[2], d.b[2]), z1 = Math.max(d.a[2], d.b[2]); if (x1 - x0 < this.grid() || z1 - z0 < this.grid()) return;
          const dir = this.tool === "ramp" ? (Math.abs(d.b[0] - d.a[0]) >= Math.abs(d.b[2] - d.a[2]) ? (d.b[0] >= d.a[0] ? 0 : 2) : (d.b[2] >= d.a[2] ? 1 : 3)) : 0;
          const b = D.addBrush(this.P, this.room(), this.tool, [x0, d.y, z0], [x1 - x0, this.boxH, z1 - z0], dir, { color: this.tool === "ramp" ? "#a3a07c" : "#8d98a3" }); this.select({ kind: "brush", id: b.id }); this.changed(true); return; }
        if ((d.move || d.handle) && d.moved) this.changed(false);
      };
      const wheel = e => { e.preventDefault(); const B = R3D.camBasis(this.cam.yaw, this.cam.pitch), sp = (e.deltaY < 0 ? 1 : -1) * 64 * (e.shiftKey ? 4 : 1); this.cam.x += B.f[0] * sp; this.cam.y += B.f[1] * sp; this.cam.z += B.f[2] * sp; };
      const kd = e => { if (/INPUT|SELECT|TEXTAREA/.test((e.target && e.target.tagName) || "")) return; const id = e.key.toLowerCase();
        if (this.look && "wasdqe".includes(id) && id.length === 1 || id === "shift") { k[id] = true; if (this.look) { e.preventDefault(); return; } }
        if (e.target !== this.root) return;
        if (e.ctrlKey || e.metaKey) { if (id === "z") { e.preventDefault(); this.history(!e.shiftKey); } else if (id === "y") { e.preventDefault(); this.history(false); } else if (id === "d") { e.preventDefault(); this.duplicate(); } return; }
        if (id === "delete" || id === "backspace") { e.preventDefault(); e.stopPropagation(); this.deleteSel(); }
        else if (id === "v") this.setTool("select"); else if (id === "b") this.setTool("box"); else if (id === "n") this.setTool("ramp"); else if (id === "o") this.setTool("place");
        else if (id === "r") this.turn(); else if (id === "f") this.frameSel(); else if (id === "[") { this.planeY -= this.grid(); this.o.log && this.o.log("Work plane at " + this.planeY + " units"); } else if (id === "]") { this.planeY += this.grid(); this.o.log && this.o.log("Work plane at " + this.planeY + " units"); }
        else if (id === "escape") this.select(null); };
      const ku = e => { const id = e.key.toLowerCase(); k[id] = false; };
      cv.addEventListener("pointerdown", down); cv.addEventListener("pointerup", up); cv.addEventListener("wheel", wheel, { passive: false }); cv.addEventListener("contextmenu", e => e.preventDefault());
      addEventListener("mousemove", move); addEventListener("keydown", kd); addEventListener("keyup", ku);
      cv.addEventListener("dragover", e => { if ([...e.dataTransfer.types].includes("application/x-pe-asset")) e.preventDefault(); }); cv.addEventListener("drop", e => this.dropAsset(e));
      this.unbind = () => { removeEventListener("mousemove", move); removeEventListener("keydown", kd); removeEventListener("keyup", ku); };
    }
    // a texture dropped on a face (Shift = the whole block); a model dropped anywhere becomes a Model object
    dropAsset(e) {
      e.preventDefault(); let id = ""; try { id = e.dataTransfer.getData("application/x-pe-asset"); } catch (x) {} const a = this.P.assets.find(x => x.id === id); if (!a) return; const [sx, sy] = this.rel(e), hit = this.pick(sx, sy);
      if (a.type === "image") { if (!hit || hit.kind !== "brush") return this.o.log && this.o.log("Drop a picture onto a block face.", "warn"); this.T.set(hit.id + (e.shiftKey ? "" : "." + hit.face) + ".tex", a.id); this.select({ kind: "brush", id: hit.id, face: hit.face }); this.o.log && this.o.log("Texture " + a.name + " on " + (e.shiftKey ? "the whole block" : "the " + D.FACE_LABEL[hit.face] + " face")); return; }
      if (a.type === "model") { const r = this.ray(sx, sy), p = hit ? hit.p : this.planeHit(r, this.planeY); if (!p) return; const t = D.addThing(this.P, this.room(), "model", [this.snap(p[0]), Math.round(p[1]), this.snap(p[2])], 0, { model: a.id, name: a.name.replace(/\.[^.]+$/, "") }); this.select({ kind: "thing", id: t.id }); this.changed(true); }
    }
    placeAt(p) { const kind = this.place, room = this.room();
      if (kind === "start") { for (const r of this.P.rooms) for (const t of r.things.slice()) if (t.kind === "start") D.removeThing(this.P, r, t); }
      const t = D.addThing(this.P, room, kind, kind === "light" ? [p[0], p[1] + 160, p[2]] : p, 0); this.select({ kind: "thing", id: t.id }); this.changed(true); }
    deleteSel() { const f = this.sel && this.find(this.sel.id); if (!f) return; if (f.brush) D.removeBrush(this.P, f.room, f.brush); else D.removeThing(this.P, f.room, f.thing); this.select(null); this.changed(true); }
    duplicate() { const f = this.sel && this.find(this.sel.id); if (!f) return; const pre = this.sel.id + ".", over = {}; for (const k in this.P.tags) if (k.startsWith(pre)) over[k.slice(pre.length)] = this.P.tags[k].value;
      if (f.brush) { const b = f.brush, nb = D.addBrush(this.P, f.room, b.shape, [b.p[0] + b.s[0], b.p[1], b.p[2]], b.s, b.dir, {}); for (const k in over) if (this.P.tags[nb.id + "." + k]) this.P.tags[nb.id + "." + k].value = over[k]; this.select({ kind: "brush", id: nb.id }); }
      else { const t = f.thing; if (t.kind === "start") return; const nt = D.addThing(this.P, f.room, t.kind, [t.p[0] + this.grid() * 2, t.p[1], t.p[2]], t.yaw, {}); for (const k in over) if (this.P.tags[nt.id + "." + k]) this.P.tags[nt.id + "." + k].value = over[k]; this.select({ kind: "thing", id: nt.id }); }
      this.T.emit([]); this.changed(true); }
    turn() { const f = this.sel && this.find(this.sel.id); if (!f) return; if (f.brush) { const b = f.brush; if (b.shape === "ramp") b.dir = ((b.dir || 0) + 1) % 4; else { const c = [b.p[0] + b.s[0] / 2, b.p[2] + b.s[2] / 2]; [b.s[0], b.s[2]] = [b.s[2], b.s[0]]; b.p[0] = this.snap(c[0] - b.s[0] / 2); b.p[2] = this.snap(c[1] - b.s[2] / 2); } }
      else f.thing.yaw = ((f.thing.yaw || 0) + (f.thing.kind === "door" ? 90 : 45)) % 360; this.changed(false); }
    frameSel() { const f = this.sel && this.find(this.sel.id); let c = [0, 0, 0], r = 600; if (f && f.brush) { c = add(f.brush.p, mul(f.brush.s, 0.5)); r = len(f.brush.s) * 1.2 + 200; } else if (f && f.thing) { c = this.thingCenter(f.thing); r = 400; } const B = R3D.camBasis(this.cam.yaw, this.cam.pitch); this.cam.x = c[0] - B.f[0] * r; this.cam.y = c[1] - B.f[1] * r; this.cam.z = c[2] - B.f[2] * r; }
    // ---------------------------------------------------------------- frame
    syncCam() { const c = this.view.cam; c.x = this.cam.x; c.y = this.cam.y; c.z = this.cam.z; c.yaw = this.cam.yaw; c.pitch = this.cam.pitch; }
    frame(t) {
      if (this.dead) return; this.raf = requestAnimationFrame(q => this.frame(q)); const dt = Math.min(0.05, (t - this.last) / 1000); this.last = t; const k = this.keys; if (this.paused) return;
      if (this.look) { const B = R3D.camBasis(this.cam.yaw, this.cam.pitch), sp = (k.shift ? 1800 : 600) * dt, f = (k.w ? 1 : 0) - (k.s ? 1 : 0), s = (k.d ? 1 : 0) - (k.a ? 1 : 0), u = (k.e ? 1 : 0) - (k.q ? 1 : 0); this.cam.x += (B.f[0] * f + B.s[0] * s) * sp; this.cam.y += (B.f[1] * f + B.s[1] * s + u) * sp; this.cam.z += (B.f[2] * f + B.s[2] * s) * sp; }
      this.syncCam(); const v = this.view, T = this.T; const lights = []; for (const r of this.P.rooms) for (const th of r.things) if (th.kind === "light") lights.push({ p: th.p, r: +T.get(th.id + ".range") || 520, c: W.hex(T.get(th.id + ".color"), "#ffe8c4").map(q => q * (+T.get(th.id + ".intensity") || 1)), type: 0 });
      v.setLights(lights.length ? lights : [{ p: [this.cam.x, this.cam.y + 200, this.cam.z], r: 2400, c: [0.5, 0.5, 0.5], type: 0 }]); v.draw();
      const items = []; this.gridItems(items); this.thingItems(items); this.selItems(items); if (items.length) v.drawFx(items); this.overlay();
    }
    gridItems(items) {
      const g = this.grid(), y = this.planeY + 0.6, cx = this.snap(this.cam.x), cz = this.snap(this.cam.z), n = 24, col = [0.22, 0.32, 0.42], col2 = [0.3, 0.5, 0.62];
      for (let i = -n; i <= n; i++) { const c = (cx / g + i) % 4 === 0 ? col2 : col, c2 = (cz / g + i) % 4 === 0 ? col2 : col; items.push({ kind: "line", a: [cx + i * g, y, cz - n * g], b: [cx + i * g, y, cz + n * g], c, w: 0.35, life: 1, t: 0 }, { kind: "line", a: [cx - n * g, y, cz + i * g], b: [cx + n * g, y, cz + i * g], c: c2, w: 0.35, life: 1, t: 0 }); }
    }
    gm(key, make) { if (!this.gpu[key]) { this.models[key] = make(); this.gpu[key] = this.view.loadModel(this.models[key]); } return this.gpu[key]; }
    fbx(aid) { const F = PRIM.FBX; if (!aid || !this.urls[aid]) return null; const m = F.cache[aid]; if (!m) { F.load(aid, this.urls[aid]); return null; } if (!this.gpu["fbx:" + aid]) { this.gpu["fbx:" + aid] = this.view.loadModel(m); this.models["fbx:" + aid] = m; } return { gm: this.gpu["fbx:" + aid], model: m }; }
    thingItems(items) {
      const T = this.T, F = PRIM.FBX, L = (a, b, c, w) => items.push({ kind: "line", a, b, c, w: w || 0.8, life: 1, t: 0 });
      for (const r of this.P.rooms) for (const t of r.things) {
        const g = k => T.get(t.id + "." + k), n = k => +g(k) || 0, p = t.p, col = KIND_COL[t.kind], on = this.sel && this.sel.id === t.id, cc = on ? [1, 0.85, 0.2] : col, yaw = (t.yaw || 0) * DEG, put = (gm, model, M, mat, anim) => items.push({ kind: "mesh", gm, pose: F.pose(model, anim || null, 0), M, p, scale: 6, mat });
        if (t.kind === "start") { const h = 150, rr = 22; for (let i = 0; i < 12; i++) { const a0 = i / 12 * Math.PI * 2, a1 = (i + 1) / 12 * Math.PI * 2; L([p[0] + Math.cos(a0) * rr, p[1], p[2] + Math.sin(a0) * rr], [p[0] + Math.cos(a1) * rr, p[1], p[2] + Math.sin(a1) * rr], cc); L([p[0] + Math.cos(a0) * rr, p[1] + h, p[2] + Math.sin(a0) * rr], [p[0] + Math.cos(a1) * rr, p[1] + h, p[2] + Math.sin(a1) * rr], cc); }
          const f = [Math.cos(yaw), 0, Math.sin(yaw)]; L([p[0], p[1] + 128, p[2]], [p[0] + f[0] * 70, p[1] + 128, p[2] + f[2] * 70], cc, 1.4); L([p[0], p[1], p[2]], [p[0], p[1] + h, p[2]], cc); continue; }
        if (t.kind === "light") { items.push({ kind: "sphere", p, c: on ? cc : W.hex(g("color"), "#ffe8c4"), size: 9 }); const rr = n("range") * 0.15; L([p[0] - rr, p[1], p[2]], [p[0] + rr, p[1], p[2]], cc, 0.4); L([p[0], p[1] - rr, p[2]], [p[0], p[1] + rr, p[2]], cc, 0.4); L([p[0], p[1], p[2] - rr], [p[0], p[1], p[2] + rr], cc, 0.4); continue; }
        if (t.kind === "door") { const w = n("width") || 128, h = n("height") || 160, R = M4.mul(M4.translate(...p), M4.rotY(-(t.yaw || 0))), lk = g("lock"), sc = W.hex(lk === "missile" ? "#ff3a2a" : lk === "beam" ? (T.get(g("lockBeam") + ".color") || "#ffd23a") : lk === "locked" ? "#666a70" : "#3ea8ff");
          put(this.gm("door:" + w + ":" + h, () => SH.doorFrame(w, h, 28)), this.models["door:" + w + ":" + h], R, { tint: on ? [1, 0.85, 0.3] : [0.55, 0.6, 0.66] }); if (lk !== "open") put(this.gm("shield:" + w + ":" + h, () => SH.doorShield(w, h)), this.models["shield:" + w + ":" + h], R, { tint: sc, emit: sc.map(q => q * 0.4), alpha: 0.75 }); continue; }
        if (t.kind === "pickup" || t.kind === "scan") { const f = t.kind === "pickup" && this.fbx(g("model")), M = M4.mul(M4.translate(p[0], p[1] + 30, p[2]), M4.rotY(this.last / 20 % 360)); if (f) put(f.gm, f.model, M4.mul(M, M4.scale(n("scale") || 1, n("scale") || 1, n("scale") || 1)), { tint: on ? [1, 0.85, 0.3] : [1, 1, 1] });
          else { const c2 = t.kind === "scan" ? [1, 0.55, 0.15] : W.hex(g("color"), "#4fe3ff"); put(this.gm("gem", () => SH.gem(14)), this.models.gem, t.kind === "scan" ? M4.mul(M, M4.scale(0.6, 0.6, 0.6)) : M, { tint: on ? [1, 0.85, 0.3] : c2, emit: c2.map(q => q * 0.4) }); } continue; }
        if (t.kind === "enemy") { const f = this.fbx(g("model")), M = M4.mul(M4.translate(...p), M4.rotY(t.yaw || 0)), rr = n("radius") || 28, hh = n("height") || 96; if (f) put(f.gm, f.model, M4.mul(M, M4.scale(n("scale") || 1, n("scale") || 1, n("scale") || 1)), { tint: on ? [1, 0.85, 0.3] : [1, 1, 1] }, g("idleAnim"));
          else { const c2 = W.hex(g("color"), "#d0553a"); put(this.gm("orb:" + rr + ":" + hh, () => SH.orb(rr, hh)), this.models["orb:" + rr + ":" + hh], M, { tint: on ? [1, 0.85, 0.3] : c2 }); } const f2 = [Math.sin(yaw), 0, Math.cos(yaw)]; L([p[0], p[1] + 4, p[2]], [p[0] + f2[0] * (rr + 30), p[1] + 4, p[2] + f2[2] * (rr + 30)], [1, 0.4, 0.3], 1.2); continue; }
        if (t.kind === "model") { const f = this.fbx(g("model")), s = n("scale") || 1; if (f) put(f.gm, f.model, M4.mul(M4.translate(...p), M4.mul(M4.rotXYZ([n("rx"), t.yaw || 0, n("rz")]), M4.scale(s, s, s))), { tint: on ? [1, 0.85, 0.3] : [1, 1, 1] }, g("anim")); else items.push({ kind: "sphere", p: [p[0], p[1] + 20, p[2]], c: cc, size: 12 }); }
      }
    }
    selItems(items) {
      const L = (a, b, c, w) => items.push({ kind: "line", a, b, c, w: w || 1, life: 1, t: 0 }), f = this.sel && this.find(this.sel.id);
      if (f && f.brush) { const b = f.brush, mn = b.p, mx = add(b.p, b.s), Y = [1, 0.82, 0.2]; const P = (i, j, k) => [i ? mx[0] : mn[0], j ? mx[1] : mn[1], k ? mx[2] : mn[2]];
        for (const [a, c] of [[[0, 0, 0], [1, 0, 0]], [[0, 0, 1], [1, 0, 1]], [[0, 1, 0], [1, 1, 0]], [[0, 1, 1], [1, 1, 1]], [[0, 0, 0], [0, 1, 0]], [[1, 0, 0], [1, 1, 0]], [[0, 0, 1], [0, 1, 1]], [[1, 0, 1], [1, 1, 1]], [[0, 0, 0], [0, 0, 1]], [[1, 0, 0], [1, 0, 1]], [[0, 1, 0], [0, 1, 1]], [[1, 1, 0], [1, 1, 1]]]) L(P(...a), P(...c), Y, 0.9);
        if (this.sel.face) { const fc = W.faces(b).find(x => x[0] === this.sel.face); if (fc) { const v = fc[1], nn = fc[2], o = q => add(q, mul(nn, 1.2)); for (let i = 0; i < v.length; i++) L(o(v[i]), o(v[(i + 1) % v.length]), [1, 0.5, 0.15], 1.4); } }
        for (const [, , p] of this.handles()) items.push({ kind: "sphere", p, c: [1, 0.85, 0.2], size: 5 }); }
      if (this.drawing) { const d = this.drawing, x0 = Math.min(d.a[0], d.b[0]), x1 = Math.max(d.a[0], d.b[0]), z0 = Math.min(d.a[2], d.b[2]), z1 = Math.max(d.a[2], d.b[2]), C = [0.5, 0.85, 1];
        for (const y of [d.y + 0.8, d.y + this.boxH]) { L([x0, y, z0], [x1, y, z0], C); L([x1, y, z0], [x1, y, z1], C); L([x1, y, z1], [x0, y, z1], C); L([x0, y, z1], [x0, y, z0], C); } for (const [x, z] of [[x0, z0], [x1, z0], [x1, z1], [x0, z1]]) L([x, d.y, z], [x, d.y + this.boxH, z], C); }
    }
    overlay() {
      const c = this.ovc, dpr = Math.min(2, devicePixelRatio || 1), w = c.clientWidth, h = c.clientHeight; if (c.width !== Math.round(w * dpr) || c.height !== Math.round(h * dpr)) { c.width = Math.round(w * dpr); c.height = Math.round(h * dpr); } const g = c.getContext("2d"); g.setTransform(dpr, 0, 0, dpr, 0, 0); g.clearRect(0, 0, w, h);
      g.font = "12px 'Share Tech Mono',monospace"; g.fillStyle = "#9fb4c8"; g.fillText("plane " + (this.planeY / 64).toFixed(2) + " m · grid " + (this.grid() / 64).toFixed(2) + " m · camera " + [this.cam.x, this.cam.y, this.cam.z].map(q => Math.round(q)).join(", "), 8, 16);
      const f = this.sel && this.find(this.sel.id); if (f && f.brush) { const b = f.brush, s = this.toScreen(add(b.p, [b.s[0] / 2, b.s[1] + 8, b.s[2] / 2])); if (s) { g.fillStyle = "#ffd25a"; g.textAlign = "center"; g.fillText((b.s[0] / 64).toFixed(2) + " × " + (b.s[1] / 64).toFixed(2) + " × " + (b.s[2] / 64).toFixed(2) + " m", s[0], s[1] - 6); g.textAlign = "left"; } }
    }
  };
  // closest point on the line (p + t*axis) to a ray
  function closestOnAxis(r, p, axis) { const w0 = sub(p, r.o), a = dot(axis, axis), b = dot(axis, r.d), c = dot(r.d, r.d), d = dot(axis, w0), e = dot(r.d, w0), den = a * c - b * b; const t = Math.abs(den) < 1e-9 ? 0 : (b * e - c * d) / den; return add(p, mul(axis, t)); }
})();
