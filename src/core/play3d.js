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
      this.T = this.mode === "play" ? o.tags.fork() : o.tags; if (this.mode === "play") { this.T.setMany({ "player.health": +this.T.get("player.maxHealth") || 100 }); this.initAmmo(); }
      this.keys = {}; this.aimYaw = 0; this.aimPitch = 0; this.dead = false; this.drag = false; this.vz = 0; this.ground = false; this.hint = null; this.shot = 0;
      const host = this.host = o.host; this.root = el("p3root"); this.cv = document.createElement("canvas"); this.cv.className = "v3cv p3cv"; this.xh = el("p3x"); this.hudHost = el("p3hud"); this.msg = el("p3msg");
      this.root.append(this.cv, this.hudHost, this.xh, this.msg); host.append(this.root);
      this.view = new R3D.View(this.cv); if (!this.view.ok) { this.view = null; this.msg.textContent = "WebGL could not start on this computer."; }
      if (this.view && this.map) { this.view.setMap(this.project, this.map, this.T, this.urls); this.walls = R3D.walls(this.map, this.T); } else if (!this.view) this.cv.style.background = "radial-gradient(ellipse at 50% 120%, #1c2b3d, #080b10)";
      const T = this.T, m = this.map; this.x = m ? +T.get(m.id + ".startX") : 0; this.y = m ? +T.get(m.id + ".startY") : 0; this.yaw = (m ? +T.get(m.id + ".startAngle") : 90) * Math.PI / 180; this.pitch = 0;
      const inf = m ? R3D.sectorInfo(m, T, this.x, this.y) : null; this.z = inf ? inf.floor : 0;
      this.hud = new PRIM.HUD({ host: this.hudHost, project: this.project, tags: T, urls: this.urls, screenId: T.get("player.hud"), edit: this.mode !== "play", say: t => { if (this.o.say) this.o.say(t); } });
      this.unsub = T.on(names => { this.drawCross(); if (this.mode === "play" && names && names.length && !names.some(n => (this.map && n.startsWith(this.map.id + ".")) || /^(wall|prop)/.test(n))) return; if (this.view && !this.pendingRb) { this.pendingRb = true; setTimeout(() => { this.pendingRb = false; if (!this.dead && this.view) { if (this.map) { this.view.rebuild(); this.walls = R3D.walls(this.map, T); } this.syncAtts(); this.syncThings(); } }, 80); } });
      this.drawCross(); this.gpu = {}; this.atts = [];
      this.sh = this.mode === "play" ? new PRIM.Script.Host({ tags: T, held: k => !!this.keys[k], isState: s => this.isState(s), orb: (k, c, a) => this.orbCmd(k, c, a), fire: key => this.fireProj(this.atts.find(a => "player." + a.id === key)), reload: key => this.reload(this.atts.find(a => "player." + a.id === key)), hasAmmo: key => { const pre = key + "."; return !this.T.get(pre + "ammoUse") || (!this.reloading && (+this.T.get(pre + "ammo") || 0) >= 1); }, sfx: (id, v) => this.sfx(id, v), loop: (id, v) => this.loopSfx(id, v), stopLoop: id => this.stopLoops(id), playAnim: (k, n, l) => this.playAnim(k, n, l), say: t => { if (this.o.say) this.o.say(t); else console.log(t); } }) : null;
      if (PRIM.Audio) PRIM.Audio.preload(this.project.assets.filter(x => x.type === "sound" && this.urls[x.id]).map(x => this.urls[x.id]));
      this.syncAtts(); this.syncThings(); this.trigger("onStart");
      if (this.mode === "play") this.bind(); else { this.msg.style.display = "none"; this.cv.style.cursor = "default"; }
      this.last = performance.now(); this.raf = requestAnimationFrame(t => this.frame(t));
    }
    destroy() {
      this.dead = true; this.stopLoops(""); if (this.sh) this.sh.stopAll(); cancelAnimationFrame(this.raf); this.unsub(); if (this.hud) this.hud.destroy(); if (this.unbind) this.unbind(); if (document.pointerLockElement === this.cv) document.exitPointerLock();
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
        const id = keyId(e), n = id; if (!e.repeat && !k[id]) { k[id] = true; this.trigger("onKey", id); if (this.mode === "play" && id === (this.T.get("player.interactKey") || "e")) this.interact(); if (this.mode === "play" && id === (this.T.get("player.reloadKey") || "r")) this.reload(this.curWeapon()); } k[id] = true; if (/^[1-9]$/.test(n)) this.setWeapon(+n - 1); if (["w", "a", "s", "d", "space", "shift", "arrowup", "arrowdown", "arrowleft", "arrowright"].includes(id)) e.preventDefault();
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
    press(id) { if (this.keys[id]) return; this.keys[id] = true; this.trigger("onKey", id); if (this.mode === "play" && id === (this.T.get("player.interactKey") || "e")) this.interact(); if (this.mode === "play" && id === (this.T.get("player.reloadKey") || "r")) this.reload(this.curWeapon()); }
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
      const delay = Math.max(0, +g("startDelay") || 0);
      if (held && !st) { this.ch[pre] = { t: 0, charged: false, wait: 0, started: delay <= 0 }; if (delay <= 0) { this.playAnim(ok, g("chargeAnim"), false); this.sh.runOut(own, n.id, "start"); T.set("player.charge", 0); } }
      else if (held && st && !st.started) { st.wait += dt; if (st.wait >= delay) { st.started = true; this.playAnim(ok, g("chargeAnim"), false); this.sh.runOut(own, n.id, "start"); T.set("player.charge", 0); } }
      else if (!held && st && !st.started) delete this.ch[pre];   // let go before the delay: nothing was charged
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
      const v = this.view; if (!at || !at.emit || !v) return; const T = this.T, pre = "player." + at.id + ".", kind = T.get(pre + "projType"); if (!kind || kind === "none" || this.pdead) return; if (!this.useAmmo(at)) return; const dmg = (T.get(pre + "projDamage") === undefined ? 25 : +T.get(pre + "projDamage"));
      const c = v.cam, B = R3D.camBasis(c.yaw, c.pitch), eye = [c.x, c.y, c.z], P = at.emit.pos, D0 = at.emit.dir;
      const O = [eye[0] + B.s[0] * P[0] + B.u[0] * P[1] - B.f[0] * P[2], eye[1] + B.s[1] * P[0] + B.u[1] * P[1] - B.f[1] * P[2], eye[2] + B.s[2] * P[0] + B.u[2] * P[1] - B.f[2] * P[2]];
      const D = M4.norm([B.s[0] * D0[0] + B.u[0] * D0[1] - B.f[0] * D0[2], B.s[1] * D0[0] + B.u[1] * D0[1] - B.f[1] * D0[2], B.s[2] * D0[0] + B.u[2] * D0[1] - B.f[2] * D0[2]]);
      const hex = String(T.get(pre + "projColor") || "#ffcc33"), col = [parseInt(hex.slice(1, 3), 16) / 255, parseInt(hex.slice(3, 5), 16) / 255, parseInt(hex.slice(5, 7), 16) / 255];
      const mul = T.get(pre + "projCharge") ? 1 + 1.5 * (this.fireCharge || 0) : 1, orbS = this.orbSize || 0; this.fx = this.fx || [];
      const em = T.get(pre + "projEmit") === undefined ? 1 : +T.get(pre + "projEmit"), ecol = col.map(q => q * em), lt = T.get(pre + "projLight") && kind !== "hitscan" ? { c: col, k: +T.get(pre + "projLightPower") || 1.5, r: +T.get(pre + "projLightRange") || 220 } : null;
      if (kind === "hitscan") { let hit = v.raycast(O, D, 6000); const far = hit ? hit.t || M4.len(M4.sub(hit.p, O)) : 6000, dh = this.hitDoor(O, D, far), he = this.hitEnemy(O, D, dh ? dh.t : far); if (he) { this.damageEnemy(he.o, dmg * mul); hit = { p: M4.add(O, D.map(q => q * he.t)) }; } else if (dh) { this.shootDoor(dh.th); hit = { p: M4.add(O, D.map(q => q * dh.t)) }; } const end = hit ? hit.p : M4.add(O, D.map(q => q * 3000)); this.fx.push({ kind: "line", a: O, b: end, c: ecol, w: +T.get(pre + "projWidth") || 0.6, life: +T.get(pre + "projLineTime") || 0.12, t: 0 }); }
      else if (kind === "particle") this.fx.push({ kind: T.get(pre + "projShape") === "laser" ? "laser" : "sphere", dmg: dmg * mul, p: O, d: D, c: ecol, light: lt, size: orbS > 0 ? orbS : (+T.get(pre + "projSize") || 3) * mul, speed: +T.get(pre + "projSpeed") || 1400, life: +T.get(pre + "projLife") || 2, t: 0 });
      else if (kind === "mesh") {
        const aid = T.get(pre + "projModel"), model = aid && PRIM.FBX.cache[aid]; if (!model) return; if (!this.gpu[aid]) this.gpu[aid] = v.loadModel(model);
        this.fx.push({ kind: "mesh", dmg: dmg * mul, p: O, d: D, gm: this.gpu[aid], pose: PRIM.FBX.pose(model, null, 0), scale: (+T.get(pre + "projScale") || 1) * mul, rot: M4.rotXYZ([+T.get(pre + "projRx") || 0, +T.get(pre + "projRy") || 0, +T.get(pre + "projRz") || 0]), speed: +T.get(pre + "projSpeed") || 1400, life: +T.get(pre + "projLife") || 2, t: 0, M: M4.ident(), light: lt, mat: { emit: [1, 1, 1].map(q => q * Math.max(0, em - 1) * 0.4) } });
      }
    }
    stepFx(dt) {
      const v = this.view, fx = this.fx; if (!fx || !fx.length || !v) return;
      this.fx = fx.filter(f => {
        f.t += dt; if (f.t > f.life) return false;
        if (f.kind !== "line") { const step = f.speed * dt; if (f.hostile) { const q = M4.add(f.p, f.d.map(z => z * step)); if (!this.pdead && Math.hypot(q[0] - this.x, q[2] - this.y) < (+this.T.get("player.radius") || 16) + (f.size || 4) && q[1] > this.z - 4 && q[1] < this.z + (+this.T.get("player.eyeHeight") || 102) + 12) { this.hurtPlayer(f.dmg); return false; } }
        else { const he = this.hitEnemy(f.p, f.d, step + (f.size || 1)); if (he) { this.damageEnemy(he.o, f.dmg || 0); return false; } }
        const dh = this.hitDoor(f.p, f.d, step + (f.size || 1)); if (dh) { if (!f.hostile) this.shootDoor(dh.th); return false; } const hit = v.raycast(f.p, f.d, step + (f.size || 1)); if (hit) return false; f.p = M4.add(f.p, f.d.map(q => q * step)); if (f.kind === "mesh") f.M = M4.mul(M4.translate(...f.p), M4.mul(M4.between([0, 0, -1], f.d, Math.PI), M4.mul(f.rot, M4.scale(f.scale, f.scale, f.scale)))); }
        return true;
      });
    }
    // ---- placed objects (map.things): doors in walls, pick-ups, decorations
    syncThings() {
      const m = this.map, T = this.T, F = PRIM.FBX; if (!m || !this.view) { this.things = []; return; } const old = this.things || [], out = [], gone = new Set(old.map(o => o.th.id));
      for (const th of m.things || []) {
        const pre = th.prop + ".", aid = T.get(pre + "asset"), type = T.get(pre + "type"); if (!aid || !this.urls[aid]) continue; const model = F.cache[aid];
        if (!model) { F.load(aid, this.urls[aid]).then(mm => { if (mm && !this.dead) this.syncThings(); }); continue; }
        if (!this.gpu[aid]) this.gpu[aid] = this.view.loadModel(model); gone.delete(th.id);
        const o = old.find(x => x.th.id === th.id) || { th, st: "closed", p: 0, t: 0, timer: 0, taken: false, spin: 0 }; o.th = th; o.pre = pre; o.aid = aid; o.model = model; o.gm = this.gpu[aid]; o.type = type; o.fr = R3D.thingFrame(m, T, th); if (!o.fr) continue;
        if (!o.init) { const inf = R3D.sectorInfo(m, T, o.fr.cx, o.fr.cy) || R3D.sectorInfo(m, T, o.fr.cx + o.fr.nx * 10, o.fr.cy + o.fr.ny * 10); o.floor = inf ? inf.floor : 0; } out.push(o);
      }
      this.things = out; this.doorOpen = this.doorOpen || {};
    }
    thingHit(o, O, D, maxd) {   // ray against a door's box in its own space; returns distance or null
      const f = o.fr, g = k => +this.T.get(o.pre + k) || 0, w = g("doorWidth") || 96, h = g("doorHeight") || 144, th = f.thick || 16, dx = O[0] - f.cx, dz = O[2] - f.cy;
      const lo = [dx * f.ux + dz * f.uy, O[1] - o.floor, dx * f.nx + dz * f.ny], ld = [D[0] * f.ux + D[2] * f.uy, D[1], D[0] * f.nx + D[2] * f.ny], mn = [-w / 2, 0, -th / 2], mx = [w / 2, h, th / 2]; let t0 = 0, t1 = maxd;
      for (let a = 0; a < 3; a++) { if (Math.abs(ld[a]) < 1e-6) { if (lo[a] < mn[a] || lo[a] > mx[a]) return null; } else { let q0 = (mn[a] - lo[a]) / ld[a], q1 = (mx[a] - lo[a]) / ld[a]; if (q0 > q1) [q0, q1] = [q1, q0]; t0 = Math.max(t0, q0); t1 = Math.min(t1, q1); if (t0 > t1) return null; } }
      return t0;
    }
    hitDoor(O, D, maxd) { let best = null; for (const o of this.things || []) { if (o.type !== "door" || o.st !== "closed") continue; const t = this.thingHit(o, O, D, maxd); if (t !== null && (!best || t < best.t)) best = { th: o.th, t }; } return best; }
    shootDoor(th) { const o = (this.things || []).find(x => x.th === th); if (o && this.T.get(o.pre + "openMode") === "shot") this.openDoor(o); }
    openDoor(o) { if (o.st === "open" || o.st === "opening") return; o.st = "opening"; o.t = o.p * this.doorLen(o, "openAnim"); o.timer = 0; const s = this.T.get(o.pre + "openSound"); if (s) this.sfx(s); }
    closeDoor(o) { if (o.st === "closed" || o.st === "closing") return; o.st = "closing"; o.timer = 0; const s = this.T.get(o.pre + "closeSound"); if (s) this.sfx(s); }
    doorLen(o, which) { const nm = this.T.get(o.pre + (which === "closeAnim" && this.T.get(o.pre + "closeAnim") ? "closeAnim" : "openAnim")), a = nm && o.model.anims[nm]; return a ? Math.max(0.05, this.animLen(o.aid, nm, a.dur).len) : 0.8; }
    interact() {
      const v = this.view; if (!v) return; const c = v.cam, B = R3D.camBasis(c.yaw, c.pitch), O = [c.x, c.y, c.z], D = B.f;
      for (const o of this.things || []) { if (o.type !== "door" || this.T.get(o.pre + "openMode") !== "interact") continue; const t = this.thingHit(o, O, D, +this.T.get(o.pre + "interactRange") || 90); if (t === null) continue; if (o.st === "closed" || o.st === "closing") this.openDoor(o); else this.closeDoor(o); return; }
    }
    thingStep(dt) {
      const T = this.T; for (const o of this.things || []) {
        if (o.taken) continue; if (o.type === "enemy") { this.enemyStep(o, dt); continue; } const g = k => +T.get(o.pre + k) || 0, f = o.fr, dist = Math.hypot(this.x - f.cx, this.y - f.cy);
        if (o.type === "exit") { if (!this.pdead && dist < (g("exitRadius") || 40) && Math.abs(this.z - o.floor) < 90) this.levelExit(o); continue; }
        if (o.type === "pickup") { if (dist < (g("pickRadius") || 28) && Math.abs(this.z - o.floor) < 70) { o.taken = true; const tg = T.get(o.pre + "giveTag"); if (tg && T.get(tg) !== undefined) T.set(tg, (+T.get(tg) || 0) + g("giveAmount")); const s = T.get(o.pre + "pickSound"); if (s) this.sfx(s); } else o.spin += dt * 80; continue; }
        if (o.type !== "door") continue; const mode = T.get(o.pre + "openMode"), near = dist < (mode === "proximity" ? g("openRange") || 110 : 50);
        if (mode === "proximity" && near && (o.st === "closed" || o.st === "closing")) this.openDoor(o);
        const on = T.get(o.pre + "autoClose") && (mode === "proximity" || g("closeAfter") > 0);
        if (o.st === "open") { if (on && !(dist < 50)) { if (mode === "proximity") { if (!near) this.closeDoor(o); } else { o.timer += dt; if (o.timer >= g("closeAfter")) this.closeDoor(o); } } }
        const len = this.doorLen(o, o.st === "closing" ? "closeAnim" : "openAnim");
        if (o.st === "opening") { o.t += dt; o.p = Math.min(1, o.t / len); if (o.p >= 1) o.st = "open"; }
        else if (o.st === "closing") { o.p = Math.max(0, o.p - dt / len); if (o.p <= 0) { o.st = "closed"; o.t = 0; } }
        this.doorOpen[o.th.id] = o.st === "open" || o.p > 0.7;
      }
    }
    drawThings(v, dt) {
      if (!this.things || !this.things.length) return; if (this.mode === "play") this.thingStep(dt); const F = PRIM.FBX, T = this.T, items = [];
      for (const o of this.things) {
        if (o.taken) continue; const g = k => +T.get(o.pre + k) || 0; let f = o.fr, name = null, tm = 0;
        if (o.type === "enemy" && o.init) { const a = o.yaw * Math.PI / 180; f = { cx: o.x, cy: o.y, ux: Math.cos(a), uy: -Math.sin(a), nx: Math.sin(a), ny: Math.cos(a), door: false }; }
        const an = k => { const nm = T.get(o.pre + k); return nm && o.model.anims[nm] ? nm : null; };
        if (o.type === "door") { const oa = an("openAnim"), ca = an("closeAnim"); if (oa) { const L = this.animLen(o.aid, oa, o.model.anims[oa].dur); if (o.st === "closing" && ca) { name = ca; tm = (1 - o.p) * this.animLen(o.aid, ca, o.model.anims[ca].dur).len * 1; } else { name = oa; tm = o.p * L.len; } tm *= L.speed; } else { name = an("idleAnim"); tm = 0; } }
        else if (o.type === "enemy" && o.init) { name = o.anim; if (name) { const L = this.animLen(o.aid, name, o.model.anims[name].dur), loop = o.akey === "idleAnim" || o.akey === "walkAnim"; tm = (loop ? o.at % Math.max(0.05, L.len) : Math.min(o.at, L.len - 1e-3)) * L.speed; } }
        else { name = an("idleAnim"); if (name) { const L = this.animLen(o.aid, name, o.model.anims[name].dur); tm = ((performance.now() / 1000) % Math.max(0.05, L.len)) * L.speed; } }
        const pose = F.pose(o.model, name, tm), ang = o.type === "pickup" ? o.spin : 0;
        let z = [f.ux * 0 + 0, 0, 0]; const X = [f.ux, 0, f.uy], Z = [f.nx, 0, f.ny]; let cr = [X[1] * 0 - 0 * 0, 0, 0];
        const sgn = (-f.uy * f.nx + f.ux * f.ny) >= 0 ? 1 : -1, XX = f.door ? X.map(q => q * sgn) : X, ZZ = f.door ? Z.map(q => q * sgn) : Z;
        const slide = o.type === "door" && !an("openAnim") ? o.p * (g("doorHeight") || 144) : 0;
        const Fm = [XX[0], XX[1], XX[2], 0, 0, 1, 0, 0, ZZ[0], ZZ[1], ZZ[2], 0, f.cx, o.floor + slide, f.cy, 1];
        const sc = g("scale") || 1, loc = M4.mul(M4.translate(g("ox"), g("oy"), g("oz")), M4.mul(M4.rotXYZ([g("rx"), g("ry") + ang, g("rz")]), M4.scale(sc, sc, sc)));
        items.push({ kind: "mesh", gm: o.gm, pose, M: M4.mul(Fm, loc), p: [f.cx, o.floor + 40, f.cy], scale: 6, mat: o.flash > 0 ? { tint: [1, 0.45, 0.45], emit: [0.5, 0.05, 0.05] } : o.st === "dead" ? { tint: [0.75, 0.7, 0.7] } : null });
      }
      if (items.length) v.drawFx(items);
    }
    // ---- enemies: idle until they see you, then chase, attack, get hurt, die
    eAnim(o, key) { const nm = this.T.get(o.pre + key); if (!(nm && o.model.anims[nm])) return null; return nm; }
    eLen(o, key, fb) { const nm = this.eAnim(o, key); return nm ? Math.max(0.05, this.animLen(o.aid, nm, o.model.anims[nm].dur).len) : fb; }
    eSet(o, key) { const nm = this.eAnim(o, key) || this.eAnim(o, "idleAnim"); if (o.anim !== nm || o.akey !== key) { o.anim = nm; o.akey = key; o.at = 0; } }
    eAlive(o) { return o.type === "enemy" && !o.taken && o.init && o.st !== "dying" && o.st !== "dead"; }
    eEye(o) { return [o.x, o.floor + (+this.T.get(o.pre + "height") || 110) * 0.8, o.y]; }
    pEye() { return [this.x, this.z + (+this.T.get("player.eyeHeight") || 102), this.y]; }
    canSee(o) {
      const v = this.view; if (!v) return false; const a = this.eEye(o), b = this.pEye(), d = M4.sub(b, a), L = M4.len(d); if (L < 1) return true; const D = d.map(q => q / L);
      const hit = v.raycast(a, D, L); if (hit && hit.t < L - 8) return false; const dh = this.hitDoor(a, D, L); return !(dh && dh.t < L - 8);
    }
    ePush(p, r, self) {   // keep a moving enemy out of walls (open doors excluded) and away from other enemies
      for (let it = 0; it < 2; it++) { for (const w of this.walls) { if (w.door && this.doorOpen && this.doorOpen[w.door]) continue; const q = segDist(p.x, p.y, w); if (q.d < r && q.d > 1e-4) { p.x += (p.x - q.x) / q.d * (r - q.d); p.y += (p.y - q.y) / q.d * (r - q.d); } }
        for (const e of this.things || []) { if (e === self || !this.eAlive(e)) continue; const er = +this.T.get(e.pre + "radius") || 22, dx = p.x - e.x, dy = p.y - e.y, d = Math.hypot(dx, dy); if (d < r + er && d > 1e-4) { p.x += dx / d * (r + er - d) * 0.5; p.y += dy / d * (r + er - d) * 0.5; } } }
    }
    enemyStep(o, dt) {
      const T = this.T, g = k => +T.get(o.pre + k) || 0;
      if (!o.init) { o.init = true; o.x = o.fr.cx; o.y = o.fr.cy; o.yaw = o.th.ang || 0; o.hp = g("health") || 100; o.st = "idle"; o.cool = 0; o.losT = Math.random() * 0.2; o.at = 0; o.flash = 0; this.eSet(o, "idleAnim"); }
      o.at += dt; if (o.flash > 0) o.flash -= dt;
      if (o.st === "dead") return;
      if (o.st === "dying") { if (o.at >= this.eLen(o, "deathAnim", 0.6)) { o.st = "dead"; if (!T.get(o.pre + "corpse")) o.taken = true; } return; }
      const r = g("radius") || 22, dx = this.x - o.x, dy = this.y - o.y, dist = Math.hypot(dx, dy) || 1;
      o.losT -= dt; if (o.losT <= 0) { o.losT = 0.2; o.los = !this.pdead && dist < (g("sight") || 900) && this.canSee(o); if (o.los && !o.awake) { o.awake = true; const s = T.get(o.pre + "sightSound"); if (s) this.sfx(s); } }
      if (o.st === "pain") { if (o.at >= this.eLen(o, "hurtAnim", 0.25)) { o.st = "chase"; this.eSet(o, "walkAnim"); } return; }
      if (!o.awake || this.pdead) { o.st = "idle"; this.eSet(o, "idleAnim"); return; }
      o.yaw = Math.atan2(dx, dy) * 180 / Math.PI;
      if (o.st === "attack") {
        if (!o.hit && o.at >= g("attackDelay")) { o.hit = true; this.eAttack(o, dist); }
        if (o.at >= Math.max(this.eLen(o, "attackAnim", 0), g("attackDelay") + 0.25)) { o.st = "chase"; o.cool = g("attackRate") || 1.2; this.eSet(o, "walkAnim"); } return;
      }
      o.cool -= dt; const range = g("attackRange") || 70;
      if (dist <= range + (T.get(o.pre + "attack") === "melee" ? r + 16 : 0) && o.los && o.cool <= 0) { o.st = "attack"; o.hit = false; this.eSet(o, "attackAnim"); o.at = 0; const s = T.get(o.pre + "attackSound"); if (s) this.sfx(s); return; }
      const want = T.get(o.pre + "attack") === "melee" ? r + (+T.get("player.radius") || 16) + 4 : Math.min(range * 0.7, 400);
      if (dist > want || !o.los) {
        const sp = (g("speed") || 0) * dt, p = { x: o.x + dx / dist * sp, y: o.y + dy / dist * sp }; this.ePush(p, r, o);
        const inf = this.info(p.x, p.y); if (inf && inf.floor - o.floor <= 26 && inf.ceil - inf.floor >= (g("height") || 110) * 0.6) { o.x = p.x; o.y = p.y; o.floor = inf.floor; }
        o.st = "chase"; this.eSet(o, "walkAnim");
      } else { o.st = "chase"; this.eSet(o, "idleAnim"); }
    }
    eAttack(o, dist) {
      const T = this.T, g = k => +T.get(o.pre + k) || 0, kind = T.get(o.pre + "attack"), dmg = g("damage"), col = this.hexCol(T.get(o.pre + "projColor") || "#ff6a2a");
      if (kind === "melee") { if (dist <= (g("attackRange") || 70) + (g("radius") || 22) + 24) this.hurtPlayer(dmg); return; }
      const a = this.eEye(o), b = this.pEye(); b[1] -= 14; const d = M4.sub(b, a), L = M4.len(d) || 1, D = d.map(q => q / L);
      if (kind === "hitscan") { const ok = this.canSee(o); (this.fx = this.fx || []).push({ kind: "line", a, b: ok ? b : M4.add(a, D.map(q => q * L * 0.9)), c: col, w: 0.6, life: 0.12, t: 0 }); if (ok) this.hurtPlayer(dmg); return; }
      (this.fx = this.fx || []).push({ kind: "sphere", hostile: true, dmg, p: a, d: D, c: col, size: g("projSize") || 6, speed: g("projSpeed") || 600, life: 6, t: 0, light: { c: col, k: 1.2, r: 160 } });
    }
    hurtPlayer(d) { if (this.pdead || !(d > 0)) return; const T = this.T; T.set("player.health", Math.max(0, (+T.get("player.health") || 0) - d)); }
    hitEnemy(O, D, maxd) {   // ray against enemy bodies (upright cylinders)
      let best = null; for (const o of this.things || []) { if (!this.eAlive(o)) continue; const r = +this.T.get(o.pre + "radius") || 22, h = +this.T.get(o.pre + "height") || 110, fx = O[0] - o.x, fz = O[2] - o.y, a = D[0] * D[0] + D[2] * D[2], b = 2 * (fx * D[0] + fz * D[2]), c = fx * fx + fz * fz - r * r;
        let t; if (c <= 0) t = 0; else { if (a < 1e-9) continue; const disc = b * b - 4 * a * c; if (disc < 0) continue; t = (-b - Math.sqrt(disc)) / (2 * a); if (t < 0) continue; }
        if (t > maxd) continue; const y = O[1] + D[1] * t; if (y < o.floor || y > o.floor + h) continue; if (!best || t < best.t) best = { o, t }; }
      return best;
    }
    damageEnemy(o, d) {
      if (!this.eAlive(o)) return; const T = this.T; o.hp -= d; o.awake = true; o.flash = 0.12;
      if (o.hp <= 0) { o.st = "dying"; this.eSet(o, "deathAnim"); o.at = 0; const s = T.get(o.pre + "deathSound"); if (s) this.sfx(s); const kt = T.get(o.pre + "killTag"); if (kt && T.get(kt) !== undefined) T.set(kt, (+T.get(kt) || 0) + (+T.get(o.pre + "killAmount") || 0)); return; }
      const s = T.get(o.pre + "hurtSound"); if (s) this.sfx(s); if (this.eAnim(o, "hurtAnim") && o.st !== "attack") { o.st = "pain"; this.eSet(o, "hurtAnim"); o.at = 0; }
    }
    // ---- ammo: "ammo" = ready to fire, "reserve" = carried. Reload moves rounds from reserve into the magazine
    weapons() { return (this.project.player && this.project.player.attachments || []).filter(a => this.T.get("player." + a.id + ".type") === "weapon"); }
    initAmmo() { const T = this.T, set = {}; for (const a of this.weapons()) { const pre = "player." + a.id + "."; if (!T.get(pre + "ammoUse") || !T.has(pre + "ammo")) continue; const st = +T.get(pre + "ammoStart") || 0, clip = +T.get(pre + "clipSize") || 0, now = clip > 0 ? Math.min(clip, st) : st; set[pre + "ammo"] = now; set[pre + "reserve"] = st - now; } if (Object.keys(set).length) T.setMany(set); }
    ammoClamp() { const T = this.T, set = {}; for (const a of this.weapons()) { const pre = "player." + a.id + "."; if (!T.get(pre + "ammoUse")) continue; const mx = +T.get(pre + "ammoMax") || 0, clip = +T.get(pre + "clipSize") || 0; let am = Math.max(0, Math.floor(+T.get(pre + "ammo") || 0)), rs = Math.max(0, Math.floor(+T.get(pre + "reserve") || 0));
      if (clip <= 0 && rs > 0) { am += rs; rs = 0; } if (clip > 0 && am > clip) { rs += am - clip; am = clip; } if (mx > 0 && am + rs > mx) { rs = Math.max(0, mx - am); if (am > mx) am = mx; }
      if (am !== +T.get(pre + "ammo")) set[pre + "ammo"] = am; if (rs !== +T.get(pre + "reserve")) set[pre + "reserve"] = rs; } if (Object.keys(set).length) T.setMany(set); }
    reload(at) {
      if (!at || this.reloading) return false; const T = this.T, pre = "player." + at.id + ".", clip = +T.get(pre + "clipSize") || 0; if (!T.get(pre + "ammoUse") || clip <= 0) return false;
      if ((+T.get(pre + "reserve") || 0) <= 0 || (+T.get(pre + "ammo") || 0) >= clip) return false;
      const nm = T.get(pre + "reloadAnim"), an = nm && at.model && at.model.anims[nm], len = an ? this.animLen(at.aid, nm, an.dur).len : (+T.get(pre + "reloadTime") || 1.2);
      if (an) this.playAnim("player." + at.id, nm, false); const s = T.get(pre + "reloadSound"); if (s) this.sfx(s); this.reloading = { at, t: len }; return true;
    }
    reloadStep(dt) { const r = this.reloading; if (!r) return; r.t -= dt; if (r.t > 0) return; this.reloading = null; const T = this.T, pre = "player." + r.at.id + ".", clip = +T.get(pre + "clipSize") || 0, am = +T.get(pre + "ammo") || 0, rs = +T.get(pre + "reserve") || 0, mv = Math.max(0, Math.min(clip - am, rs)); T.setMany({ [pre + "ammo"]: am + mv, [pre + "reserve"]: rs - mv }); }
    useAmmo(at) {   // true = the shot may go out
      const T = this.T, pre = "player." + at.id + "."; if (!T.get(pre + "ammoUse")) return true; if (this.reloading) return false;
      const am = +T.get(pre + "ammo") || 0; if (am >= 1) { T.set(pre + "ammo", am - 1); return true; }
      if (!this.reload(at)) { const s = T.get(pre + "emptySound"); if (s) this.sfx(s); } return false;
    }
    // ---- damage flash, death, level exits, changing maps
    flash(a) { if (!this.T.get("player.damageFlash")) return; if (!this.flashEl) { this.flashEl = document.createElement("div"); this.flashEl.style.cssText = "position:absolute;inset:0;pointer-events:none;background:radial-gradient(ellipse at center,rgba(255,0,0,0) 35%,rgba(200,0,0,.85) 100%);opacity:0;transition:opacity .45s ease-out;z-index:4"; this.root.append(this.flashEl); } const e = this.flashEl; e.style.transition = "none"; e.style.opacity = a; requestAnimationFrame(() => { e.style.transition = "opacity .45s ease-out"; e.style.opacity = 0; }); }
    deathStep(dt) {
      const T = this.T; if (!this.pdead) { if ((+T.get("player.health") || 0) <= 0) { this.pdead = true; this.deadT = 0; this.reloading = null; this.say2("You died"); } return; }
      this.deadT += dt; if (this.deadT < (+T.get("player.deathDelay") || 0)) return;
      const scr = T.get("player.deathScreen"); if (T.get("player.deathAction") === "screen" && scr && this.o.onScreen) { this.pdead = false; this.o.onScreen(scr); return; } this.restartMap();
    }
    say2(t) { if (!this.bigMsg) { this.bigMsg = document.createElement("div"); this.bigMsg.style.cssText = "position:absolute;left:0;right:0;top:38%;text-align:center;font:600 46px 'Chakra Petch',system-ui,sans-serif;letter-spacing:.12em;color:#ffdede;text-shadow:0 2px 10px #000;pointer-events:none;z-index:6;transition:opacity .6s"; this.root.append(this.bigMsg); } this.bigMsg.textContent = t; this.bigMsg.style.opacity = t ? 1 : 0; }
    resetPlayer() { const T = this.T, m = this.map; this.x = +T.get(m.id + ".startX"); this.y = +T.get(m.id + ".startY"); this.yaw = (+T.get(m.id + ".startAngle")) * Math.PI / 180; this.pitch = 0; const inf = R3D.sectorInfo(m, T, this.x, this.y); this.z = inf ? inf.floor : 0; this.vz = 0; this.fx = []; this.orb = null; this.reloading = null; this.pdead = false; this.deadT = 0; this.say2(""); this.doorOpen = {}; this.things = []; this.syncThings(); }
    restartMap() { const T = this.T; T.set("player.health", +T.get("player.maxHealth") || 100); this.lastHp = undefined; this.initAmmo(); this.resetPlayer(); }
    loadMap(id) { const m = this.project.maps.find(x => x.id === id); if (!m || !this.view) return false; this.map = m; this.view.setMap(this.project, m, this.T, this.urls); this.walls = R3D.walls(m, this.T); this.resetPlayer(); return true; }
    levelExit(o) {
      if (this.exiting) return; const T = this.T, s = T.get(o.pre + "exitSound"); if (s) this.sfx(s);
      if (T.get(o.pre + "exitTo") === "screen") { const sc = T.get(o.pre + "exitScreen"); if (sc && this.o.onScreen) { this.exiting = true; this.o.onScreen(sc); return; } this.say2("Level finished"); this.exiting = true; return; }
      const maps = this.project.maps, cur = maps.indexOf(this.map), id = T.get(o.pre + "exitMap") || (maps[cur + 1] && maps[cur + 1].id);
      if (!id) { this.say2("Level finished"); this.exiting = true; return; } this.loadMap(id);
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
    vol3(mult) { const T = this.T; return Math.max(0, Math.min(1, (+T.get("audio.master") || 0) * (+T.get("audio.sfx") || 0) * (isNaN(mult) ? 1 : mult))); }
    loopSfx(id, vol) { const u = this.urls[id]; if (!u) return; this.loops = this.loops || {}; const v = this.vol3(vol); if (this.loops[id] && this.loops[id].playing) { this.loops[id].setVolume(v); return; } this.loops[id] = PRIM.Audio.play(u, v, true); }
    stopLoops(id) { const L = this.loops || {}; for (const k of Object.keys(L)) if (!id || k === id) { L[k].stop(0.03); delete L[k]; } }
    sfx(id, vol) { const u = this.urls[id]; if (u) PRIM.Audio.play(u, this.vol3(vol), false); }
    playAnim(key, name, loop) { const at = this.atts.find(a => "player." + a.id === key); if (!at) return; if (!name) { if (loop) at.loopAnim = null; return; } if (!at.model.anims[name]) return; if (loop) at.loopAnim = name; else at.fire = name; at.t = 0; }
    fire() { this.trigger("onClick"); }
    // sound events set on the Timeline tab: play those that fall between t0 and t1 of the animation (only while really playing, not in the editor preview)
    ev(at, name, t0, t1) {
      if (this.mode !== "play") return; const T = this.T;
      for (const e of this.project.events || []) {
        if (e.asset !== at.aid || e.anim !== name) continue; const t = +T.get(e.id + ".time") || 0, sid = T.get(e.id + ".sound"); if (!(t >= t0 && t < t1) || !sid || !this.urls[sid]) continue;
        PRIM.Audio.play(this.urls[sid], this.vol3(+T.get(e.id + ".volume") || 0), false);
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
        const vo = this.mode === "play" && this.vmOff ? this.vmOff : [0, 0, 0], off = [g("x") + vo[0], g("y") + vo[1], g("z") + vo[2]], Rb = M4.rotXYZ([g("rx"), g("ry"), g("rz")]), sc = g("scale") || 1, Sm = M4.scale(sc, sc, sc); let Ra = M4.ident();
        if (at.type === "weapon") {
          const ay = g("arrowYaw") * Math.PI / 180, ap = g("arrowPitch") * Math.PI / 180, arrow = [-Math.cos(ap) * Math.sin(ay), Math.sin(ap), -Math.cos(ap) * Math.cos(ay)], mz = [(g("muzzleX") - g("pivotX")) * sc, (g("muzzleY") - g("pivotY")) * sc, (g("muzzleZ") - g("pivotZ")) * sc];
          const hit = v.raycast(eye, this.aimDir(), 6000), wp = hit ? hit.p : M4.add(eye, this.aimDir().map(q => q * 3000)), d0 = M4.sub(wp, eye), Hc = [M4.dot(d0, B.s), M4.dot(d0, B.u), -M4.dot(d0, B.f)];
          const a0 = M4.norm(M4.dir(Rb, ...arrow)), maxR = g("aimMax") * Math.PI / 180; let want = M4.norm(M4.sub(Hc, M4.add(off, M4.dir(Rb, ...mz))));
          Ra = M4.between(a0, want, maxR); want = M4.norm(M4.sub(Hc, M4.add(off, M4.dir(M4.mul(Ra, Rb), ...mz)))); at.cur = at.cur ? M4.norm(M4.add(at.cur.map(q => q * Math.exp(-14 * dt)), want.map(q => q * (1 - Math.exp(-14 * dt))))) : want; Ra = M4.between(a0, at.cur, maxR);
        } else at.cur = null;
        const M = M4.mul(M4.translate(...off), M4.mul(Ra, M4.mul(Rb, M4.mul(Sm, M4.translate(-g("pivotX"), -g("pivotY"), -g("pivotZ")))))); items.push({ gm: at.gm, pose, M, mat: this.matOf(pre) }); this.objLight(at, pre, M, g, sc, eye, B);
        if (at.type === "weapon") { const ey = g("emitYaw") * Math.PI / 180, ep = g("emitPitch") * Math.PI / 180; at.emit = { pos: M4.point(M, g("emitX"), g("emitY"), g("emitZ")), dir: M4.norm(M4.dir(M, -Math.cos(ep) * Math.sin(ey), Math.sin(ep), -Math.cos(ep) * Math.cos(ey))) }; }
        if (at.type === "weapon") { const ay2 = g("arrowYaw") * Math.PI / 180, ap2 = g("arrowPitch") * Math.PI / 180; at.muz = { pos: M4.point(M, g("muzzleX"), g("muzzleY"), g("muzzleZ")), dir: M4.norm(M4.dir(M, -Math.cos(ap2) * Math.sin(ay2), Math.sin(ap2), -Math.cos(ap2) * Math.cos(ay2))) }; }
        if (this.mode === "preview" && at.type === "weapon") {   // the arrow and the muzzle point, drawn so they can be placed
          const mp = M4.point(M, g("muzzleX"), g("muzzleY"), g("muzzleZ")), ad = M4.norm(M4.dir(M, ...[-Math.cos(g("arrowPitch") * Math.PI / 180) * Math.sin(g("arrowYaw") * Math.PI / 180), Math.sin(g("arrowPitch") * Math.PI / 180), -Math.cos(g("arrowPitch") * Math.PI / 180) * Math.cos(g("arrowYaw") * Math.PI / 180)])), tip = M4.add(mp, ad.map(q => q * 26));
          const side = M4.norm(M4.cross(ad, [0, 1, 0])), r = [1, 0.25, 0.2], y = [1, 0.9, 0.2]; segs.push({ a: mp, b: tip, c: r }, { a: tip, b: M4.add(tip, M4.add(ad.map(q => -q * 5), side.map(q => q * 2.5))), c: r }, { a: tip, b: M4.add(tip, M4.sub(ad.map(q => -q * 5), side.map(q => q * 2.5))), c: r });
          for (const ax of [[1, 0, 0], [0, 1, 0], [0, 0, 1]]) segs.push({ a: M4.sub(mp, ax.map(q => q * 2)), b: M4.add(mp, ax.map(q => q * 2)), c: y });
        }
      }
      v.setLights(this.lightNow || []); v.drawViewmodel(items, segs);
    }
    // material of an object (Material tab)
    matOf(pre) { return PRIM.Schema.matOf(this.T, pre); }
    // the light an object carries (Light tab), placed in the world for this frame (camera space -> world)
    objLight(at, pre, M, g, sc, eye, B) {
      const T = this.T, ty = T.get(pre + "lightType"); if (!ty || ty === "none") return; const w = q => [eye[0] + B.s[0] * q[0] + B.u[0] * q[1] - B.f[0] * q[2], eye[1] + B.s[1] * q[0] + B.u[1] * q[1] - B.f[1] * q[2], eye[2] + B.s[2] * q[0] + B.u[2] * q[1] - B.f[2] * q[2]];
      const wd = q => M4.norm([B.s[0] * q[0] + B.u[0] * q[1] - B.f[0] * q[2], B.s[1] * q[0] + B.u[1] * q[1] - B.f[1] * q[2], B.s[2] * q[0] + B.u[2] * q[1] - B.f[2] * q[2]]);
      const fl = +T.get(pre + "lightFlicker") || 0, tm = performance.now() / 1000, k = (+T.get(pre + "lightIntensity") || 0) * (1 - fl * (0.5 + 0.5 * Math.sin(tm * 37 + Math.sin(tm * 11) * 5)) * 0.9), col = this.hexCol(T.get(pre + "lightColor") || "#ffe2b0");
      const L = { p: w(M4.point(M, g("lightX"), g("lightY"), g("lightZ"))), r: +T.get(pre + "lightRange") || 300, c: col.map(q => q * k), type: 0 };
      if (ty === "area") { const yw = g("lightYaw") * Math.PI / 180, pt = g("lightPitch") * Math.PI / 180, n = M4.norm(M4.dir(M, -Math.cos(pt) * Math.sin(yw), Math.sin(pt), -Math.cos(pt) * Math.cos(yw))), ax = M4.norm(M4.cross(n, Math.abs(n[1]) > 0.95 ? [1, 0, 0] : [0, 1, 0]));
        L.type = 1; L.n = wd(n); L.u = wd(ax); L.hw = (+T.get(pre + "lightWidth") || 40) * sc / 2; L.hh = (+T.get(pre + "lightHeight") || 20) * sc / 2; }
      (this.lightNow = this.lightNow || []).push(L);
    }
    // small performance readout (shown with "Show FPS counter")
    stats3d(v) {
      if (!this.statEl) { this.statEl = document.createElement("div"); this.statEl.style.cssText = "position:absolute;left:14px;top:46px;font:13px monospace;color:#7df3ff;text-shadow:0 1px 2px #000;pointer-events:none;white-space:pre;z-index:5"; if (v.canvas.parentNode) v.canvas.parentNode.append(this.statEl); }
      const on = this.mode === "play" && this.T.get("gfx.showFps") && v.stats; this.statEl.style.display = on ? "block" : "none"; if (!on) return; const s = v.stats;
      if (!this.statN || performance.now() - this.statN > 400) { this.statN = performance.now(); this.statEl.textContent = "triangles " + Math.round(s.tris) + " / " + Math.round(s.totalTris) + "\nchunks " + s.chunks + " / " + s.totalChunks + "\nlights " + s.lights + "\nLOD level " + (v.lodLevel || 0); }
    }
    // ---- walking
    info(x, y) { return R3D.sectorInfo(this.map, this.T, x, y); }
    push(p, r) { for (const e of this.things || []) { if (!this.eAlive(e)) continue; const er = +this.T.get(e.pre + "radius") || 22, dx = p.x - e.x, dy = p.y - e.y, d = Math.hypot(dx, dy); if (d < r + er && d > 1e-4) { p.x += dx / d * (r + er - d); p.y += dy / d * (r + er - d); } } for (let it = 0; it < 3; it++) for (const w of this.walls) { if (w.door && this.doorOpen && this.doorOpen[w.door]) continue; const q = segDist(p.x, p.y, w); if (q.d < r) { let nx = p.x - q.x, ny = p.y - q.y, d = q.d; if (d < 1e-4) { const dx = w.bx - w.ax, dy = w.by - w.ay, l = Math.hypot(dx, dy) || 1; nx = -dy / l; ny = dx / l; d = 0; } else { nx /= d; ny /= d; } p.x += nx * (r - d); p.y += ny * (r - d); } } }
    step(dt) {
      const T = this.T, k = this.keys, g = +T.get("player.gravity"), r = +T.get("player.radius"), head = +T.get("player.eyeHeight") + 8, stepH = +T.get("player.stepHeight");
      let fw = (k.w || k.arrowup ? 1 : 0) - (k.s || k.arrowdown ? 1 : 0), sd = (k.d ? 1 : 0) - (k.a ? 1 : 0); if (k.arrowleft) this.yaw -= 2 * dt; if (k.arrowright) this.yaw += 2 * dt;
      const len = Math.hypot(fw, sd) || 1; fw /= len; sd /= len; let spd = +T.get("player.speed"); if (T.get("player.sprintEnabled") && k.shift && fw > 0) spd *= 1 + (+T.get("player.sprintPercent") || 0) / 100;
      const cy = Math.cos(this.yaw), sy = Math.sin(this.yaw), nx = this.x + (cy * fw - sy * sd) * spd * dt, ny = this.y + (sy * fw + cy * sd) * spd * dt;
      const tryPos = (x, y) => { const p = { x, y }; this.push(p, r); const inf = this.info(p.x, p.y); if (!inf) return null; if (inf.floor - this.z > stepH) return null; if (inf.ceil - Math.max(inf.floor, this.z) < head) return null; return p; };
      const p = tryPos(nx, ny) || tryPos(nx, this.y) || tryPos(this.x, ny); this.mv = p ? Math.hypot(p.x - this.x, p.y - this.y) : 0; this.sprinting = !!(T.get("player.sprintEnabled") && k.shift && fw > 0); this.spdNow = spd; if (p) { if (this.ground) { this.stepAcc = (this.stepAcc || 0) + Math.hypot(p.x - this.x, p.y - this.y); const sd2 = Math.max(10, +T.get("player.stepDistance") || 90); if (this.stepAcc >= sd2) { this.stepAcc %= sd2; this.footstep(); } } this.x = p.x; this.y = p.y; }
      const inf = this.info(this.x, this.y) || { floor: this.z, ceil: this.z + 9999 };
      if (T.get("player.jumpEnabled") && k.space && this.ground) { this.vz = Math.sqrt(2 * g * (+T.get("player.jumpHeight") || 0)); this.ground = false; this.psound("jumpSound"); }
      this.vz -= g * dt; this.z += this.vz * dt;
      if (this.z <= inf.floor) { if (!this.ground && this.vz < -250) this.psound("landSound"); this.z = inf.floor; this.vz = 0; this.ground = true; } else this.ground = false;
      if (this.z + head > inf.ceil) { this.z = Math.max(inf.floor, inf.ceil - head); if (this.vz > 0) this.vz = 0; }
      this.airT = this.ground ? 0 : (this.airT || 0) + dt;
      this.setState(!this.ground && (this.vz > 0 || this.airT > 0.12) ? (this.vz > 0 ? "jumping" : "falling") : this.mv > this.spdNow * dt * 0.3 ? (this.sprinting ? "running" : "walking") : "standing");
    }
    // what the player is doing now (standing, walking, running, jumping, falling) -> live tag player.state + "When the player is…" events
    isState(s) { const c = this.pstate; return s === c || (s === "moving" && (c === "walking" || c === "running")) || (s === "inair" && (c === "jumping" || c === "falling")); }
    setState(st) {
      const prev = this.pstate; if (st === prev) return; this.pstate = st; this.T.set("player.state", st); this.trigger("onState", st);
      const grp = s => ({ moving: s === "walking" || s === "running", inair: s === "jumping" || s === "falling" });
      const a = grp(prev), b = grp(st); for (const k of ["moving", "inair"]) if (b[k] && !a[k]) this.trigger("onState", k);
    }
    // breathing, walking bob and the swaying HUD
    feel(dt) {
      const T = this.T, st = this.pstate, f = this.fl = this.fl || { idle: 0, walk: 0, ph: 0, bt: 0, hx: 0, hy: 0, ly: this.yaw, lp: this.pitch };
      const idleT = st === "standing" ? 1 : 0, walkT = st === "walking" || st === "running" ? 1 : 0, k = 1 - Math.exp(-6 * dt); f.idle += (idleT - f.idle) * k; f.walk += (walkT - f.walk) * k;
      f.bt += dt * (+T.get("player.breatheSpeed") || 0.25) * Math.PI * 2; f.ph += dt * (+T.get("player.bobSpeed") || 1.8) * Math.PI * 2 * (st === "running" ? 1.45 : 1);
      const ba = T.get("player.breathe") ? (+T.get("player.breatheAmount") || 0) * f.idle : 0, wa = T.get("player.bob") ? (+T.get("player.bobAmount") || 0) * f.walk * (st === "running" ? 1.4 : 1) : 0;
      this.vmOff = [Math.sin(f.ph * 0.5) * wa * 0.8, Math.sin(f.bt) * ba * 0.7 - Math.abs(Math.sin(f.ph * 0.5)) * wa, Math.sin(f.bt) * ba * 0.4];
      // Hud Movement: turning kicks the HUD the other way, then it eases back
      let dy = this.yaw - f.ly; dy = Math.atan2(Math.sin(dy), Math.cos(dy)); const dp = this.pitch - f.lp; f.ly = this.yaw; f.lp = this.pitch;
      if (T.get("player.hudMove")) { const A = +T.get("player.hudMoveAmount") || 1; f.hx = Math.max(-70 * A, Math.min(70 * A, f.hx - dy * 260 * A)); f.hy = Math.max(-50 * A, Math.min(50 * A, f.hy + dp * 260 * A)); const dk = Math.exp(-7 * dt); f.hx *= dk; f.hy *= dk; if (this.hud) this.hud.sway(f.hx, f.hy); }
      else if (f.hx || f.hy) { f.hx = f.hy = 0; if (this.hud) this.hud.sway(0, 0); }
    }
    frame(t) {
      if (this.dead) return; this.raf = requestAnimationFrame(q => this.frame(q)); const dt = Math.min(0.05, (t - this.last) / 1000); this.last = t;
      if (this.mode === "play" && this.map && !this.pdead) this.step(dt);
      if (this.mode === "play") this.feel(dt);
      if (this.mode === "play") { this.hold(dt); const hp = +this.T.get("player.health"); if (this.lastHp === undefined) this.lastHp = hp; if (hp < this.lastHp) { this.psound(hp <= 0 ? "deathSound" : "hurtSound"); this.flash(Math.min(0.6, 0.15 + (this.lastHp - hp) / 60)); } this.lastHp = hp; this.reloadStep(dt); this.ammoClamp(); this.deathStep(dt); }
      const v = this.view; if (!v) return; const T = this.T, c = v.cam; v.fov = (+T.get("player.fov") || 80) * Math.PI / 180;
      c.x = this.x; c.z = this.y; c.y = this.z + (+T.get("player.eyeHeight") || 56) - (this.pdead ? Math.min(1, (this.deadT || 0) * 2) * (+T.get("player.eyeHeight") || 56) * 0.7 : 0); c.yaw = this.yaw; c.pitch = this.pitch; v.setLights(this.lightPrev || []); v.draw(); this.lightNow = []; if (this.mode === "play") { this.stepFx(dt); const list = (this.fx || []).slice(), ob = this.orb, oat = ob && this.atts.find(x => x.id === ob.id); for (const f of list) if (f.light) this.lightNow.push({ p: f.p, r: f.light.r, c: f.light.c.map(q => q * f.light.k), type: 0 }); if (oat && oat.emit) { if (ob.grow) { ob.grow.t += dt; ob.size = ob.grow.from + (ob.grow.to - ob.grow.from) * Math.min(1, ob.grow.t / ob.grow.secs); } const op = this.emitWorld(oat).O, opre = "player." + oat.id + ".", oem = T.get(opre + "projEmit") === undefined ? 1 : +T.get(opre + "projEmit"); list.push({ kind: "sphere", p: op, c: this.hexCol(ob.color).map(q => q * oem), size: ob.size }); if (T.get(opre + "projLight")) this.lightNow.push({ p: op, r: +T.get(opre + "projLightRange") || 220, c: this.hexCol(ob.color).map(q => q * (+T.get(opre + "projLightPower") || 1.5)), type: 0 }); } v.setLights(this.lightNow); if (list.length) v.drawFx(list); } this.drawThings(v, dt); this.drawHand(v, dt); this.lightPrev = this.lightNow; this.stats3d(v); this.placeCross(v);
    }
  };
})();
