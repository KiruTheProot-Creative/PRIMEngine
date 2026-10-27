/* PRIMEngine 3D play mode: the player walking through a map. Reads every setting from the player.* tags
   (speed, gravity, jumping, sprinting, health, crosshair, HUD ...). mode "play" is real play (settings are
   copied so the project is not changed); mode "preview" is a still camera used by the Player Editor. */
(function () {
  "use strict";
  const mouseId = b => b === 2 ? "mouse2" : b === 1 ? "mouse3" : "mouse1";
  const keyId = e => { const n = e.key.toLowerCase(); return n === " " ? "space" : n === "control" ? "ctrl" : n; };
  const PRIM = (window.PRIM = window.PRIM || {}), R3D = PRIM.R3D, M4 = PRIM.M4, clamp = (v, a, b) => Math.max(a, Math.min(b, v));
  const el = (cls, css) => { const d = document.createElement("div"); d.className = cls; if (css) d.style.cssText = css; return d; };
  const segDist = (px, py, w) => { const dx = w.bx - w.ax, dy = w.by - w.ay, l2 = dx * dx + dy * dy; let t = l2 ? ((px - w.ax) * dx + (py - w.ay) * dy) / l2 : 0; t = clamp(t, 0, 1); const x = w.ax + t * dx, y = w.ay + t * dy; return { d: Math.hypot(px - x, py - y), x, y }; };

  PRIM.Play3D = class Play3D {
    constructor(o) {
      this.o = o; this.mode = o.mode || "play"; this.project = o.project; this.map = o.map || null; this.urls = o.urls || {};
      this.T = this.mode === "play" ? o.tags.fork() : o.tags; if (this.mode === "play") this.T.setMany({ "player.health": +this.T.get("player.maxHealth") || 100 });
      this.keys = {}; this.aimYaw = 0; this.aimPitch = 0; this.dead = false; this.drag = false; this.vz = 0; this.ground = false; this.hint = null; this.shot = 0;
      const host = this.host = o.host; this.root = el("p3root"); this.cv = document.createElement("canvas"); this.cv.className = "v3cv p3cv"; this.xh = el("p3x"); this.hudHost = el("p3hud"); this.msg = el("p3msg");
      this.root.append(this.cv, this.hudHost, this.xh, this.msg); host.append(this.root);
      this.view = new R3D.View(this.cv); if (!this.view.ok) { this.view = null; this.msg.textContent = "WebGL could not start on this computer."; }
      if (this.view && this.map) { this.view.setMap(this.project, this.map, this.T, this.urls); this.walls = R3D.walls(this.map, this.T); } else if (!this.view) this.cv.style.background = "radial-gradient(ellipse at 50% 120%, #1c2b3d, #080b10)";
      const T = this.T, m = this.map; this.x = m ? +T.get(m.id + ".startX") : 0; this.y = m ? +T.get(m.id + ".startY") : 0; this.yaw = (m ? +T.get(m.id + ".startAngle") : 90) * Math.PI / 180; this.pitch = 0;
      const inf = m ? R3D.sectorInfo(m, T, this.x, this.y) : null; this.z = inf ? inf.floor : 0;
      this.hudRt = new PRIM.Runtime({ host: this.hudHost, project: this.project, tags: T, mode: "hud", urls: this.urls, screenId: T.get("player.hud") });
      this.unsub = T.on(() => { this.drawCross(); if (this.view && !this.pendingRb) { this.pendingRb = true; setTimeout(() => { this.pendingRb = false; if (!this.dead && this.view) { if (this.map) { this.view.rebuild(); this.walls = R3D.walls(this.map, T); } this.syncAtts(); } }, 80); } });
      this.drawCross(); this.gpu = {}; this.atts = [];
      this.sh = this.mode === "play" ? new PRIM.Script.Host({ tags: T, held: k => !!this.keys[k], orb: (k, c, a) => this.orbCmd(k, c, a), fire: key => this.fireProj(this.atts.find(a => "player." + a.id === key)), sfx: (id, v) => this.sfx(id, v), playAnim: (k, n, l) => this.playAnim(k, n, l), say: t => { if (this.o.say) this.o.say(t); else console.log(t); } }) : null;
      this.syncAtts(); this.trigger("onStart");
      if (this.mode === "play") this.bind(); else { this.msg.style.display = "none"; this.cv.style.cursor = "default"; }
      this.last = performance.now(); this.raf = requestAnimationFrame(t => this.frame(t));
    }
    destroy() {
      this.dead = true; if (this.sh) this.sh.stopAll(); cancelAnimationFrame(this.raf); this.unsub(); if (this.hudRt) this.hudRt.destroy(); if (this.unbind) this.unbind(); if (document.pointerLockElement === this.cv) document.exitPointerLock();
      if (this.view) { const e = this.view.gl.getExtension("WEBGL_lose_context"); if (e) e.loseContext(); } this.root.remove();
    }
    // ---- input
    bind() {
      const cv = this.cv, k = this.keys;
      // a visible Leave button, shown whenever the mouse is free (Esc first frees the mouse, so the menu is always reachable)
      const lv = document.createElement("button"); lv.className = "p3leave"; lv.textContent = "✕ Leave game  (Esc)"; lv.addEventListener("pointerdown", e => e.stopPropagation()); lv.addEventListener("click", e => { e.stopPropagation(); this.leave(); }); this.root.append(lv);
      const lock = () => { const on = document.pointerLockElement === cv; this.msg.style.display = on ? "none" : "grid"; lv.style.display = on ? "none" : "block"; };
      this.msg.textContent = "Click to play  ·  W A S D move  ·  Space jump  ·  Shift sprint  ·  Esc to leave" + (this.o.debug ? "  ·  H / J test damage" : ""); this.msg.style.display = "grid";
      const kd = e => {
        if (e.key === "Escape") { e.stopImmediatePropagation(); e.preventDefault(); this.leave(); return; }
        if (/INPUT|SELECT|TEXTAREA/.test((e.target && e.target.tagName) || "")) return;   // typing in a text field is never a game key
        const id = keyId(e), n = id; if (!e.repeat && !k[id]) { k[id] = true; this.trigger("onKey", id); } k[id] = true; if (/^[1-9]$/.test(n)) this.setWeapon(+n - 1); if (["w", "a", "s", "d", "space", "shift", "arrowup", "arrowdown", "arrowleft", "arrowright"].includes(id)) e.preventDefault();
        if (this.o.debug && n === "h") this.T.setMany({ "player.health": Math.max(0, this.T.get("player.health") - 10) }); if (this.o.debug && n === "j") this.T.setMany({ "player.health": Math.min(this.T.get("player.maxHealth"), this.T.get("player.health") + 10) });
      };
      const ku = e => this.release(keyId(e));
      const mm = e => { if (document.pointerLockElement === cv || this.drag) this.look(e.movementX, e.movementY); };
      const pd = e => { if (this.engaged && e && e.button === 0) this.fire(); if (this.engaged && e) this.press(mouseId(e.button)); this.engaged = true; this.msg.style.display = "none"; this.drag = true; try { const r = cv.requestPointerLock && cv.requestPointerLock(); if (r && r.catch) r.catch(() => {}); } catch (x) {} cv.focus(); };
      const pu = e => { this.drag = false; if (e && e.button !== undefined) this.release(mouseId(e.button)); }, pl = () => lock(), bl = () => { for (const id of Object.keys(k)) this.release(id); }, noCtx = e => e.preventDefault(), noMid = e => { if (e.button !== 0) e.preventDefault(); };
      addEventListener("keydown", kd, true); addEventListener("keyup", ku, true); addEventListener("mousemove", mm); addEventListener("pointerup", pu); document.addEventListener("pointerlockchange", pl); addEventListener("blur", bl); this.root.addEventListener("contextmenu", noCtx); this.root.addEventListener("mousedown", noMid);
      this.root.addEventListener("pointerdown", pd); cv.tabIndex = 0; const wh = e => { if (this.engaged) { e.preventDefault(); this.setWeapon((this.wi || 0) + (e.deltaY > 0 ? 1 : -1)); } }; this.root.addEventListener("wheel", wh, { passive: false });
      this.unbind = () => { removeEventListener("keydown", kd, true); removeEventListener("keyup", ku, true); removeEventListener("mousemove", mm); removeEventListener("pointerup", pu); document.removeEventListener("pointerlockchange", pl); removeEventListener("blur", bl); };
    }
    press(id) { if (this.keys[id]) return; this.keys[id] = true; this.trigger("onKey", id); }
    release(id) { if (!this.keys[id]) return; this.keys[id] = false; this.trigger("onKeyUp", id); }
    // "While a key is held": run again every few moments for as long as the key stays down
    hold(dt) {
      if (!this.sh) return; const cur = this.curWeaponId(), T = this.T, k = this.keys; this.holdT = this.holdT || {};
      this.ch = this.ch || {};
      for (const a of this.project.player.attachments) {
        if (!a.script) continue; if (T.get("player." + a.id + ".type") === "weapon" && a.id !== cur) { for (const q of Object.keys(this.ch)) if (q.startsWith("player." + a.id + ".")) { delete this.ch[q]; this.orb = null; this.T.set("player.charge", 0); } continue; }
        for (const n of a.script.nodes) {
          if (n.type === "onCharge") { this.charge(a, n, dt); continue; }
          if (n.type !== "onHold") continue; const pre = "player." + a.id + ".s." + n.id + ".", key = String(T.get(pre + "key") || "").toLowerCase();
          if (!k[key]) { delete this.holdT[pre]; continue; }
          const iv = Math.max(0, +T.get(pre + "interval") || 0), t = (this.holdT[pre] === undefined ? iv : this.holdT[pre]) + dt;
          if (t >= iv) { this.holdT[pre] = 0; this.sh.trigger({ key: "player." + a.id, script: a.script }, "onHold", key, n.id); } else this.holdT[pre] = t;
        }
      }
    }
    // Charge beam node: hold the key -> charging animation, then the charged loop; release -> fire animation (or cancel when released too early)
    charge(a, n, dt) {
      const T = this.T, pre = "player." + a.id + ".s." + n.id + ".", g = k => T.get(pre + k), key = String(g("key") || "").toLowerCase(), held = !!this.keys[key], own = { key: "player." + a.id, script: a.script }, ok = "player." + a.id, at = this.atts.find(x => x.id === a.id);
      const st = this.ch[pre], dur = nm => { const an = at && nm && at.model.anims[nm]; return an ? this.animLen(at.aid, nm, an.dur).len : 0; };
      if (held && !st) { this.ch[pre] = { t: 0, charged: false }; this.playAnim(ok, g("chargeAnim"), false); this.sh.runOut(own, n.id, "start"); T.set("player.charge", 0); }
      else if (held && st) {
        st.t += dt; const need = +g("chargeTime") > 0 ? +g("chargeTime") : (dur(g("chargeAnim")) || 1); this.chargeNeed = need;
        if (!st.charged) { T.set("player.charge", Math.min(1, st.t / need)); if (st.t >= need) { st.charged = true; this.stopAnim(ok); this.playAnim(ok, g("loopAnim"), true); T.set("player.charge", 1); this.sh.runOut(own, n.id, "charged"); } }
      } else if (!held && st) {
        delete this.ch[pre]; if (this.orb) { this.orbSize = this.orb.size; setTimeout(() => { this.orbSize = 0; }, 1500); this.orb = null; } this.stopAnim(ok); this.fireCharge = st.charged ? 1 : Math.min(1, T.get("player.charge")); setTimeout(() => { this.fireCharge = 0; }, 1500);
        if (st.charged) { this.playAnim(ok, g("fireAnim"), false); this.sh.runOut(own, n.id, "fire"); } else this.sh.runOut(own, n.id, "cancel");
        T.set("player.charge", 0);
      }
    }
    // the charge particle: a ball at the weapon's emitter point that can grow while you charge
    orbCmd(key, cmd, a) {
      const at = this.atts.find(x => "player." + x.id === key); if (!at) return;
      if (cmd === "show") this.orb = { id: at.id, color: a.color, size: a.size, size0: a.size, grow: null };
      else if (cmd === "grow" && this.orb && this.orb.id === at.id) this.orb.grow = { from: this.orb.size, to: a.size, secs: a.seconds > 0 ? a.seconds : (this.chargeNeed || 1), t: 0 };
    }
    hexCol(hex) { hex = String(hex || "#ffcc33"); return [parseInt(hex.slice(1, 3), 16) / 255, parseInt(hex.slice(3, 5), 16) / 255, parseInt(hex.slice(5, 7), 16) / 255]; }
    emitWorld(at) {
      const v = this.view, c = v.cam, B = R3D.camBasis(c.yaw, c.pitch), P = at.emit.pos, D0 = at.emit.dir;
      return { O: [c.x + B.s[0] * P[0] + B.u[0] * P[1] - B.f[0] * P[2], c.y + B.s[1] * P[0] + B.u[1] * P[1] - B.f[1] * P[2], c.z + B.s[2] * P[0] + B.u[2] * P[1] - B.f[2] * P[2]],
        D: M4.norm([B.s[0] * D0[0] + B.u[0] * D0[1] - B.f[0] * D0[2], B.s[1] * D0[0] + B.u[1] * D0[1] - B.f[1] * D0[2], B.s[2] * D0[0] + B.u[2] * D0[1] - B.f[2] * D0[2]]) };
    }
    stopAnim(key) { const at = this.atts.find(a => "player." + a.id === key); if (at) { at.fire = null; at.loopAnim = null; at.t = 0; } }
    // ---- projectiles: hitscan line, particle (sphere / laser) or a flying mesh, launched from the weapon's muzzle point along its arrow
    fireProj(at) {
      const v = this.view; if (!at || !at.emit || !v) return; const T = this.T, pre = "player." + at.id + ".", kind = T.get(pre + "projType"); if (!kind || kind === "none") return;
      const c = v.cam, B = R3D.camBasis(c.yaw, c.pitch), eye = [c.x, c.y, c.z], P = at.emit.pos, D0 = at.emit.dir;
      const O = [eye[0] + B.s[0] * P[0] + B.u[0] * P[1] - B.f[0] * P[2], eye[1] + B.s[1] * P[0] + B.u[1] * P[1] - B.f[1] * P[2], eye[2] + B.s[2] * P[0] + B.u[2] * P[1] - B.f[2] * P[2]];
      const D = M4.norm([B.s[0] * D0[0] + B.u[0] * D0[1] - B.f[0] * D0[2], B.s[1] * D0[0] + B.u[1] * D0[1] - B.f[1] * D0[2], B.s[2] * D0[0] + B.u[2] * D0[1] - B.f[2] * D0[2]]);
      const hex = String(T.get(pre + "projColor") || "#ffcc33"), col = [parseInt(hex.slice(1, 3), 16) / 255, parseInt(hex.slice(3, 5), 16) / 255, parseInt(hex.slice(5, 7), 16) / 255];
      const mul = T.get(pre + "projCharge") ? 1 + 1.5 * (this.fireCharge || 0) : 1, orbS = this.orbSize || 0; this.fx = this.fx || [];
      if (kind === "hitscan") { const hit = v.raycast(O, D, 6000), end = hit ? hit.p : M4.add(O, D.map(q => q * 3000)); this.fx.push({ kind: "line", a: O, b: end, c: col, w: +T.get(pre + "projWidth") || 0.6, life: +T.get(pre + "projLineTime") || 0.12, t: 0 }); }
      else if (kind === "particle") this.fx.push({ kind: T.get(pre + "projShape") === "laser" ? "laser" : "sphere", p: O, d: D, c: col, size: orbS > 0 ? orbS : (+T.get(pre + "projSize") || 3) * mul, speed: +T.get(pre + "projSpeed") || 1400, life: +T.get(pre + "projLife") || 2, t: 0 });
      else if (kind === "mesh") {
        const aid = T.get(pre + "projModel"), model = aid && PRIM.FBX.cache[aid]; if (!model) return; if (!this.gpu[aid]) this.gpu[aid] = v.loadModel(model);
        this.fx.push({ kind: "mesh", p: O, d: D, gm: this.gpu[aid], pose: PRIM.FBX.pose(model, null, 0), scale: (+T.get(pre + "projScale") || 1) * mul, rot: M4.rotXYZ([+T.get(pre + "projRx") || 0, +T.get(pre + "projRy") || 0, +T.get(pre + "projRz") || 0]), speed: +T.get(pre + "projSpeed") || 1400, life: +T.get(pre + "projLife") || 2, t: 0, M: M4.ident() });
      }
    }
    stepFx(dt) {
      const v = this.view, fx = this.fx; if (!fx || !fx.length || !v) return;
      this.fx = fx.filter(f => {
        f.t += dt; if (f.t > f.life) return false;
        if (f.kind !== "line") { const step = f.speed * dt, hit = v.raycast(f.p, f.d, step + (f.size || 1)); if (hit) return false; f.p = M4.add(f.p, f.d.map(q => q * step)); if (f.kind === "mesh") f.M = M4.mul(M4.translate(...f.p), M4.mul(M4.between([0, 0, -1], f.d, Math.PI), M4.mul(f.rot, M4.scale(f.scale, f.scale, f.scale)))); }
        return true;
      });
    }
    // footsteps, jump / landing / hurt / death sounds (player.* sound tags)
    psound(tag) { const id = this.T.get("player." + tag); if (id) this.sfx(id, +this.T.get("player.soundVolume")); }
    footstep() { const T = this.T, ids = ["footstep", "footstep2", "footstep3"].filter(t => T.get("player." + t)); if (!ids.length) return; let i = Math.floor(Math.random() * ids.length); if (ids.length > 1 && i === this.lastStep) i = (i + 1) % ids.length; this.lastStep = i; this.psound(ids[i]); }
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
    // ---- 3D objects attached to the camera (weapons, hands...)
    syncAtts() {
      if (!this.view) return; const F = PRIM.FBX, keep = [];
      for (const a of this.project.player.attachments) {
        { const pm = this.T.get("player." + a.id + ".projModel"); if (pm && !F.cache[pm] && this.urls[pm]) F.load(pm, this.urls[pm]).then(() => {}); }
        const type = this.T.get("player." + a.id + ".type"), aid = this.T.get("player." + a.id + (type === "image" ? ".image" : ".asset")), model = F.cache[aid]; if (!aid) continue;
        if (!model) { (type === "image" ? F.loadImage : F.load)(aid, this.urls[aid]).then(m => { if (m && !this.dead) this.syncAtts(); }); continue; }
        let at = this.atts.find(x => x.id === a.id && x.aid === aid) || { id: a.id, aid, t: 0, fire: null, cur: null }; at.model = model; at.type = type; if (!this.gpu[aid]) this.gpu[aid] = this.view.loadModel(model); at.gm = this.gpu[aid]; keep.push(at);
      }
      this.atts = keep; this.setWeapon(this.wi || 0);
    }
    // the weapon list: the player holds one weapon at a time (number keys 1-9 or the mouse wheel switch)
    wids() { return this.project.player.attachments.filter(a => this.T.get("player." + a.id + ".type") === "weapon").map(a => a.id); }
    curWeaponId() { const w = this.wids(); if (this.mode === "preview" && this.o.previewWeapon && w.includes(this.o.previewWeapon)) return this.o.previewWeapon; return w[this.wi || 0] || null; }
    curWeapon() { const id = this.curWeaponId(); return id ? this.atts.find(a => a.id === id) || null : null; }
    setWeapon(i) {
      const w = this.wids(); if (!w.length) { this.wi = 0; if (this.mode === "play" && this.T.get("player.weapon") !== 0) this.T.set("player.weapon", 0); return; }
      i = ((i % w.length) + w.length) % w.length; const changed = i !== this.wi, old = w[this.wi];
      if (changed) { for (const a of this.atts) { a.cur = null; a.fire = null; a.loopAnim = null; a.t = 0; } if (this.sh && old) this.sh.stopOwner("player." + old); }
      this.wi = i; if (this.mode === "play" && this.T.get("player.weapon") !== i + 1) this.T.set("player.weapon", i + 1);
      if (changed && this.sh) { const att = this.project.player.attachments.find(a => a.id === w[i]); if (att && att.script) this.sh.trigger({ key: "player." + att.id, script: att.script }, "onEquip"); }
    }
    // Node Coder events: every non-weapon object hears them, and the weapon that is currently in hand
    trigger(type, arg) {
      if (!this.sh) return; const cur = this.curWeaponId();
      for (const a of this.project.player.attachments) { if (!a.script) continue; if (this.T.get("player." + a.id + ".type") === "weapon" && a.id !== cur) continue; this.sh.trigger({ key: "player." + a.id, script: a.script }, type, arg); }
    }
    sfx(id, vol) { const u = this.urls[id]; if (!u) return; const a = new Audio(u), T = this.T; a.volume = Math.max(0, Math.min(1, (+T.get("audio.master") || 0) * (+T.get("audio.sfx") || 0) * (isNaN(vol) ? 1 : vol))); a.play().catch(() => {}); }
    playAnim(key, name, loop) { const at = this.atts.find(a => "player." + a.id === key); if (!at) return; if (!name) { if (loop) at.loopAnim = null; return; } if (!at.model.anims[name]) return; if (loop) at.loopAnim = name; else at.fire = name; at.t = 0; }
    fire() { this.trigger("onClick"); }
    // sound events set on the Timeline tab: play those that fall between t0 and t1 of the animation (only while really playing, not in the editor preview)
    ev(at, name, t0, t1) {
      if (this.mode !== "play") return; const T = this.T;
      for (const e of this.project.events || []) {
        if (e.asset !== at.aid || e.anim !== name) continue; const t = +T.get(e.id + ".time") || 0, sid = T.get(e.id + ".sound"); if (!(t >= t0 && t < t1) || !sid || !this.urls[sid]) continue;
        const a = new Audio(this.urls[sid]); a.volume = Math.max(0, Math.min(1, (+T.get("audio.master") || 0) * (+T.get("audio.sfx") || 0) * (+T.get(e.id + ".volume") || 0))); a.play().catch(() => {});
      }
    }
    animLen(aid, name, dur) { return PRIM.Schema.animLen(this.project, this.T, aid, name, dur); }
    // advance one attachment's animation; returns the name being played
    advance(at, dt) {
      const T = this.T, pre = "player." + at.id + "."; let name = at.fire || at.loopAnim || T.get(pre + "idleAnim"), a = name && at.model.anims[name]; if (!a) { at.t = 0; return ""; }
      const len = this.animLen(at.aid, name, a.dur).len, t0 = at.t; at.t += dt; this.ev(at, name, t0, Math.min(at.t, len));
      if (at.fire) { if (at.t >= len) { at.fire = null; at.t = 0; } } else if (len > 0 && at.t >= len) { this.ev(at, name, 0, at.t - len); at.t %= len; }
      return at.fire || name;
    }
    drawHand(v, dt) {
      if (!this.atts.length) return; const T = this.T, c = v.cam, items = [], segs = [], F = PRIM.FBX, eye = [c.x, c.y, c.z], B = R3D.camBasis(c.yaw, c.pitch);
      for (const at of this.atts) {
        const pre = "player." + at.id + "."; if (!T.get(pre + "visible")) continue; if (at.type === "weapon" && at !== this.curWeapon()) continue; const g = k => +T.get(pre + k) || 0, name = this.advance(at, dt), pose = F.pose(at.model, name || null, at.t * (name && at.model.anims[name] ? this.animLen(at.aid, name, at.model.anims[name].dur).speed : 1));
        const off = [g("x"), g("y"), g("z")], Rb = M4.rotXYZ([g("rx"), g("ry"), g("rz")]), sc = g("scale") || 1, Sm = M4.scale(sc, sc, sc); let Ra = M4.ident();
        if (at.type === "weapon") {
          const ay = g("arrowYaw") * Math.PI / 180, ap = g("arrowPitch") * Math.PI / 180, arrow = [-Math.cos(ap) * Math.sin(ay), Math.sin(ap), -Math.cos(ap) * Math.cos(ay)], mz = [(g("muzzleX") - g("pivotX")) * sc, (g("muzzleY") - g("pivotY")) * sc, (g("muzzleZ") - g("pivotZ")) * sc];
          const hit = v.raycast(eye, this.aimDir(), 6000), wp = hit ? hit.p : M4.add(eye, this.aimDir().map(q => q * 3000)), d0 = M4.sub(wp, eye), Hc = [M4.dot(d0, B.s), M4.dot(d0, B.u), -M4.dot(d0, B.f)];
          const a0 = M4.norm(M4.dir(Rb, ...arrow)), maxR = g("aimMax") * Math.PI / 180; let want = M4.norm(M4.sub(Hc, M4.add(off, M4.dir(Rb, ...mz))));
          Ra = M4.between(a0, want, maxR); want = M4.norm(M4.sub(Hc, M4.add(off, M4.dir(M4.mul(Ra, Rb), ...mz)))); at.cur = at.cur ? M4.norm(M4.add(at.cur.map(q => q * Math.exp(-14 * dt)), want.map(q => q * (1 - Math.exp(-14 * dt))))) : want; Ra = M4.between(a0, at.cur, maxR);
        } else at.cur = null;
        const M = M4.mul(M4.translate(...off), M4.mul(Ra, M4.mul(Rb, M4.mul(Sm, M4.translate(-g("pivotX"), -g("pivotY"), -g("pivotZ")))))); items.push({ gm: at.gm, pose, M });
        if (at.type === "weapon") { const ey = g("emitYaw") * Math.PI / 180, ep = g("emitPitch") * Math.PI / 180; at.emit = { pos: M4.point(M, g("emitX"), g("emitY"), g("emitZ")), dir: M4.norm(M4.dir(M, -Math.cos(ep) * Math.sin(ey), Math.sin(ep), -Math.cos(ep) * Math.cos(ey))) }; }
        if (at.type === "weapon") { const ay2 = g("arrowYaw") * Math.PI / 180, ap2 = g("arrowPitch") * Math.PI / 180; at.muz = { pos: M4.point(M, g("muzzleX"), g("muzzleY"), g("muzzleZ")), dir: M4.norm(M4.dir(M, -Math.cos(ap2) * Math.sin(ay2), Math.sin(ap2), -Math.cos(ap2) * Math.cos(ay2))) }; }
        if (this.mode === "preview" && at.type === "weapon") {   // the arrow and the muzzle point, drawn so they can be placed
          const mp = M4.point(M, g("muzzleX"), g("muzzleY"), g("muzzleZ")), ad = M4.norm(M4.dir(M, ...[-Math.cos(g("arrowPitch") * Math.PI / 180) * Math.sin(g("arrowYaw") * Math.PI / 180), Math.sin(g("arrowPitch") * Math.PI / 180), -Math.cos(g("arrowPitch") * Math.PI / 180) * Math.cos(g("arrowYaw") * Math.PI / 180)])), tip = M4.add(mp, ad.map(q => q * 26));
          const side = M4.norm(M4.cross(ad, [0, 1, 0])), r = [1, 0.25, 0.2], y = [1, 0.9, 0.2]; segs.push({ a: mp, b: tip, c: r }, { a: tip, b: M4.add(tip, M4.add(ad.map(q => -q * 5), side.map(q => q * 2.5))), c: r }, { a: tip, b: M4.add(tip, M4.sub(ad.map(q => -q * 5), side.map(q => q * 2.5))), c: r });
          for (const ax of [[1, 0, 0], [0, 1, 0], [0, 0, 1]]) segs.push({ a: M4.sub(mp, ax.map(q => q * 2)), b: M4.add(mp, ax.map(q => q * 2)), c: y });
        }
      }
      v.drawViewmodel(items, segs);
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
      const p = tryPos(nx, ny) || tryPos(nx, this.y) || tryPos(this.x, ny); if (p) { if (this.ground) { this.stepAcc = (this.stepAcc || 0) + Math.hypot(p.x - this.x, p.y - this.y); const sd2 = Math.max(10, +T.get("player.stepDistance") || 90); if (this.stepAcc >= sd2) { this.stepAcc %= sd2; this.footstep(); } } this.x = p.x; this.y = p.y; }
      const inf = this.info(this.x, this.y) || { floor: this.z, ceil: this.z + 9999 };
      if (T.get("player.jumpEnabled") && k.space && this.ground) { this.vz = Math.sqrt(2 * g * (+T.get("player.jumpHeight") || 0)); this.ground = false; this.psound("jumpSound"); }
      this.vz -= g * dt; this.z += this.vz * dt;
      if (this.z <= inf.floor) { if (!this.ground && this.vz < -250) this.psound("landSound"); this.z = inf.floor; this.vz = 0; this.ground = true; } else this.ground = false;
      if (this.z + head > inf.ceil) { this.z = Math.max(inf.floor, inf.ceil - head); if (this.vz > 0) this.vz = 0; }
    }
    frame(t) {
      if (this.dead) return; this.raf = requestAnimationFrame(q => this.frame(q)); const dt = Math.min(0.05, (t - this.last) / 1000); this.last = t;
      if (this.mode === "play" && this.map) this.step(dt);
      if (this.mode === "play") { this.hold(dt); const hp = +this.T.get("player.health"); if (this.lastHp === undefined) this.lastHp = hp; if (hp < this.lastHp) this.psound(hp <= 0 ? "deathSound" : "hurtSound"); this.lastHp = hp; }
      const v = this.view; if (!v) return; const T = this.T, c = v.cam; v.fov = (+T.get("player.fov") || 80) * Math.PI / 180;
      c.x = this.x; c.z = this.y; c.y = this.z + (+T.get("player.eyeHeight") || 56); c.yaw = this.yaw; c.pitch = this.pitch; v.draw(); if (this.mode === "play") { this.stepFx(dt); const list = (this.fx || []).slice(), ob = this.orb, oat = ob && this.atts.find(x => x.id === ob.id); if (oat && oat.emit) { if (ob.grow) { ob.grow.t += dt; ob.size = ob.grow.from + (ob.grow.to - ob.grow.from) * Math.min(1, ob.grow.t / ob.grow.secs); } list.push({ kind: "sphere", p: this.emitWorld(oat).O, c: this.hexCol(ob.color), size: ob.size }); } if (list.length) v.drawFx(list); } this.drawHand(v, dt); this.placeCross(v);
    }
  };
})();
