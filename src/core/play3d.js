/* PRIMEngine 3D play mode: the player walking through a map. Reads every setting from the player.* tags
   (speed, gravity, jumping, sprinting, health, crosshair, HUD ...). mode "play" is real play (settings are
   copied so the project is not changed); mode "preview" is a still camera used by the Player Editor. */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), R3D = PRIM.R3D, clamp = (v, a, b) => Math.max(a, Math.min(b, v));
  const el = (cls, css) => { const d = document.createElement("div"); d.className = cls; if (css) d.style.cssText = css; return d; };
  const segDist = (px, py, w) => { const dx = w.bx - w.ax, dy = w.by - w.ay, l2 = dx * dx + dy * dy; let t = l2 ? ((px - w.ax) * dx + (py - w.ay) * dy) / l2 : 0; t = clamp(t, 0, 1); const x = w.ax + t * dx, y = w.ay + t * dy; return { d: Math.hypot(px - x, py - y), x, y }; };

  PRIM.Play3D = class Play3D {
    constructor(o) {
      this.o = o; this.mode = o.mode || "play"; this.project = o.project; this.map = o.map || null; this.urls = o.urls || {};
      this.T = this.mode === "play" ? o.tags.fork() : o.tags; if (this.mode === "play") this.T.setMany({ "player.health": +this.T.get("player.maxHealth") || 100 });
      this.keys = {}; this.aimYaw = 0; this.aimPitch = 0; this.dead = false; this.drag = false; this.vz = 0; this.ground = false; this.hint = null; this.shot = 0;
      const host = this.host = o.host; this.root = el("p3root"); this.cv = document.createElement("canvas"); this.cv.className = "v3cv p3cv"; this.xh = el("p3x"); this.hudHost = el("p3hud"); this.msg = el("p3msg");
      this.root.append(this.cv, this.hudHost, this.xh, this.msg); host.append(this.root);
      this.view = this.map ? new R3D.View(this.cv) : null; if (this.view && !this.view.ok) { this.view = null; this.msg.textContent = "WebGL could not start on this computer."; }
      if (this.view) { this.view.setMap(this.project, this.map, this.T, this.urls); this.walls = R3D.walls(this.map, this.T); } else this.cv.style.background = "radial-gradient(ellipse at 50% 120%, #1c2b3d, #080b10)";
      const T = this.T, m = this.map; this.x = m ? +T.get(m.id + ".startX") : 0; this.y = m ? +T.get(m.id + ".startY") : 0; this.yaw = (m ? +T.get(m.id + ".startAngle") : 90) * Math.PI / 180; this.pitch = 0;
      const inf = m ? R3D.sectorInfo(m, T, this.x, this.y) : null; this.z = inf ? inf.floor : 0;
      this.hudRt = new PRIM.Runtime({ host: this.hudHost, project: this.project, tags: T, mode: "hud", urls: this.urls, screenId: T.get("player.hud") });
      this.unsub = T.on(() => { this.drawCross(); if (this.view && !this.pendingRb) { this.pendingRb = true; setTimeout(() => { this.pendingRb = false; if (!this.dead && this.view) { this.view.rebuild(); this.walls = R3D.walls(this.map, T); } }, 80); } });
      this.drawCross();
      if (this.mode === "play") this.bind(); else { this.msg.style.display = "none"; this.cv.style.cursor = "default"; }
      this.last = performance.now(); this.raf = requestAnimationFrame(t => this.frame(t));
    }
    destroy() {
      this.dead = true; cancelAnimationFrame(this.raf); this.unsub(); if (this.hudRt) this.hudRt.destroy(); if (this.unbind) this.unbind(); if (document.pointerLockElement === this.cv) document.exitPointerLock();
      if (this.view) { const e = this.view.gl.getExtension("WEBGL_lose_context"); if (e) e.loseContext(); } this.root.remove();
    }
    // ---- input
    bind() {
      const cv = this.cv, k = this.keys, lock = () => { this.msg.style.display = document.pointerLockElement === cv ? "none" : "grid"; };
      this.msg.textContent = "Click to play  ·  W A S D move  ·  Space jump  ·  Shift sprint  ·  Esc to leave" + (this.o.debug ? "  ·  H / J test damage" : ""); this.msg.style.display = "grid";
      const kd = e => {
        if (e.key === "Escape") { e.stopImmediatePropagation(); e.preventDefault(); this.leave(); return; }
        const n = e.key.toLowerCase(), id = n === " " ? "space" : n; k[id] = true; if (["w", "a", "s", "d", "space", "shift", "arrowup", "arrowdown", "arrowleft", "arrowright"].includes(id)) e.preventDefault();
        if (this.o.debug && n === "h") this.T.setMany({ "player.health": Math.max(0, this.T.get("player.health") - 10) }); if (this.o.debug && n === "j") this.T.setMany({ "player.health": Math.min(this.T.get("player.maxHealth"), this.T.get("player.health") + 10) });
      };
      const ku = e => { const n = e.key.toLowerCase(); k[n === " " ? "space" : n] = false; };
      const mm = e => { if (document.pointerLockElement === cv || this.drag) this.look(e.movementX, e.movementY); };
      const pd = () => { this.msg.style.display = "none"; this.drag = true; try { const r = cv.requestPointerLock && cv.requestPointerLock(); if (r && r.catch) r.catch(() => {}); } catch (x) {} cv.focus(); };
      const pu = () => { this.drag = false; }, pl = () => lock();
      addEventListener("keydown", kd, true); addEventListener("keyup", ku, true); addEventListener("mousemove", mm); cv.addEventListener("pointerdown", pd); addEventListener("pointerup", pu); document.addEventListener("pointerlockchange", pl);
      this.root.addEventListener("pointerdown", pd); cv.tabIndex = 0;
      this.unbind = () => { removeEventListener("keydown", kd, true); removeEventListener("keyup", ku, true); removeEventListener("mousemove", mm); removeEventListener("pointerup", pu); document.removeEventListener("pointerlockchange", pl); };
    }
    leave() { if (this.o.onExit) this.o.onExit(); else this.destroy(); }
    // Classic: the camera looks where the mouse points and the crosshair stays in the middle.
    // Free aim (bodycam style): the mouse moves the aim (and crosshair) inside an area of the screen; the camera only turns once the aim reaches the edge of that area.
    look(dx, dy) {
      const T = this.T, s = 0.0022 * (+T.get("player.mouseSens") || 1);
      if (T.get("player.aimMode") !== "free") { this.aimYaw = this.aimPitch = 0; this.yaw += dx * s; this.pitch = clamp(this.pitch - dy * s, -1.45, 1.45); return; }
      const R = Math.max(0.05, (+T.get("player.freeAimRadius") || 14) * Math.PI / 180); let ay = this.aimYaw + dx * s, ap = this.aimPitch - dy * s; const d = Math.hypot(ay, ap);
      if (d > R) { const k = R / d, ex = ay - ay * k, ey = ap - ap * k; this.yaw += ex; this.pitch = clamp(this.pitch + ey, -1.45, 1.45); ay *= k; ap *= k; }
      this.aimYaw = ay; this.aimPitch = ap;
    }
    // world direction the player is aiming along (camera direction plus the free-aim offset)
    aimDir() { const yaw = this.yaw + this.aimYaw, pit = clamp(this.pitch + this.aimPitch, -1.5, 1.5), cp = Math.cos(pit); return [Math.cos(yaw) * cp, Math.sin(pit), Math.sin(yaw) * cp]; }
    // ---- crosshair (a plain cross)
    drawCross() {
      const T = this.T, on = T.get("player.crosshair"); this.xh.style.display = on ? "block" : "none"; if (!on) return;
      const s = +T.get("player.crosshairSize"), g = +T.get("player.crosshairGap"), t = +T.get("player.crosshairThickness"), c = T.get("player.crosshairColor"), bar = (x, y, w, h) => el("", `position:absolute;left:${x}px;top:${y}px;width:${w}px;height:${h}px;background:${c};box-shadow:0 0 0 1px rgba(0,0,0,.55)`);
      this.xh.replaceChildren(bar(-t / 2, -g - s, t, s), bar(-t / 2, g, t, s), bar(-g - s, -t / 2, s, t), bar(g, -t / 2, s, t));
    }
    // the crosshair sits where the aim points: the centre in classic mode, moving inside the aim area in free aim
    placeCross(v) {
      if (this.T.get("player.aimMode") !== "free" && !this.aimYaw && !this.aimPitch) { this.xh.style.transform = ""; return; }
      const t = Math.tan(v.fov / 2), w = this.cv.clientWidth, h = this.cv.clientHeight, asp = w / h;
      this.xh.style.transform = `translate(${Math.tan(this.aimYaw) / (t * asp) * w / 2}px, ${-Math.tan(this.aimPitch) / t * h / 2}px)`;
    }
    // ---- walking
    info(x, y) { return R3D.sectorInfo(this.map, this.T, x, y); }
    push(p, r) { for (let it = 0; it < 3; it++) for (const w of this.walls) { const q = segDist(p.x, p.y, w); if (q.d < r) { let nx = p.x - q.x, ny = p.y - q.y, d = q.d; if (d < 1e-4) { const dx = w.bx - w.ax, dy = w.by - w.ay, l = Math.hypot(dx, dy) || 1; nx = -dy / l; ny = dx / l; d = 0; } else { nx /= d; ny /= d; } p.x += nx * (r - d); p.y += ny * (r - d); } } }
    step(dt) {
      const T = this.T, k = this.keys, g = +T.get("player.gravity"), r = +T.get("player.radius"), head = +T.get("player.eyeHeight") + 8, stepH = +T.get("player.stepHeight");
      let fw = (k.w || k.arrowup ? 1 : 0) - (k.s || k.arrowdown ? 1 : 0), sd = (k.d ? 1 : 0) - (k.a ? 1 : 0); if (k.arrowleft) this.yaw -= 2 * dt; if (k.arrowright) this.yaw += 2 * dt;
      const len = Math.hypot(fw, sd) || 1; fw /= len; sd /= len; let spd = +T.get("player.speed"); if (T.get("player.sprintEnabled") && k.shift && fw > 0) spd *= 1 + (+T.get("player.sprintPercent") || 0) / 100;
      const cy = Math.cos(this.yaw), sy = Math.sin(this.yaw), nx = this.x + (cy * fw - sy * sd) * spd * dt, ny = this.y + (sy * fw + cy * sd) * spd * dt;
      const tryPos = (x, y) => { const p = { x, y }; this.push(p, r); const inf = this.info(p.x, p.y); if (!inf) return null; if (inf.floor - this.z > stepH) return null; if (inf.ceil - Math.max(inf.floor, this.z) < head) return null; return p; };
      const p = tryPos(nx, ny) || tryPos(nx, this.y) || tryPos(this.x, ny); if (p) { this.x = p.x; this.y = p.y; }
      const inf = this.info(this.x, this.y) || { floor: this.z, ceil: this.z + 9999 };
      if (T.get("player.jumpEnabled") && k.space && this.ground) { this.vz = Math.sqrt(2 * g * (+T.get("player.jumpHeight") || 0)); this.ground = false; }
      this.vz -= g * dt; this.z += this.vz * dt;
      if (this.z <= inf.floor) { this.z = inf.floor; this.vz = 0; this.ground = true; } else this.ground = false;
      if (this.z + head > inf.ceil) { this.z = Math.max(inf.floor, inf.ceil - head); if (this.vz > 0) this.vz = 0; }
    }
    frame(t) {
      if (this.dead) return; this.raf = requestAnimationFrame(q => this.frame(q)); const dt = Math.min(0.05, (t - this.last) / 1000); this.last = t;
      if (this.mode === "play" && this.map) this.step(dt);
      const v = this.view; if (!v) return; const T = this.T, c = v.cam; v.fov = (+T.get("player.fov") || 80) * Math.PI / 180;
      c.x = this.x; c.z = this.y; c.y = this.z + (+T.get("player.eyeHeight") || 56); c.yaw = this.yaw; c.pitch = this.pitch; v.draw(); this.placeCross(v);
    }
  };
})();
