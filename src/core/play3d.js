/* PRIMEngine 3D play mode: the player walking through a map. Reads every setting from the player.* tags
   (speed, gravity, jumping, sprinting, health, crosshair, HUD ...). mode "play" is real play (settings are
   copied so the project is not changed); mode "preview" is a still camera used by the Player Editor. */
(function () {
  "use strict";
  const SHEETS = { idleAnim: "spriteIdle", walkAnim: "spriteWalk", attackAnim: "spriteAttack", hurtAnim: "spriteHurt", deathAnim: "spriteDeath" };
  const mouseId = b => b === 2 ? "mouse2" : b === 1 ? "mouse3" : "mouse1";
  const keyId = e => { const n = e.key.toLowerCase(); return n === " " ? "space" : n === "control" ? "ctrl" : n; };
  const PRIM = (window.PRIM = window.PRIM || {}), R3D = PRIM.R3D, M4 = PRIM.M4, G = PRIM.Grid, clamp = (v, a, b) => Math.max(a, Math.min(b, v));
  const el = (cls, css) => { const d = document.createElement("div"); d.className = cls; if (css) d.style.cssText = css; return d; };

  PRIM.Play3D = class Play3D {
    constructor(o) {
      this.o = o; this.mode = o.mode || "play"; this.project = o.project; this.map = o.map || null; this.urls = o.urls || {};
      this.T = this.mode === "play" ? o.tags.fork() : o.tags; if (this.mode === "play") { this.T.setMany({ "player.health": +this.T.get("player.maxHealth") || 100 }); this.initAmmo(); this.initLives(); }
      this.keys = {}; this.aimYaw = 0; this.aimPitch = 0; this.dead = false; this.drag = false; this.vz = 0; this.ground = false; this.hint = null; this.shot = 0;
      const host = this.host = o.host; this.root = el("p3root"); this.cv = document.createElement("canvas"); this.cv.className = "v3cv p3cv"; this.xh = el("p3x"); this.hudHost = el("p3hud"); this.msg = el("p3msg");
      this.root.append(this.cv, this.hudHost, this.xh, this.msg); host.append(this.root);
      this.view = new R3D.View(this.cv); if (!this.view.ok) { this.view = null; this.msg.textContent = "WebGL could not start on this computer."; }
      if (this.view && this.mode === "play") this.view.post = !window.__noPost;   // HDR picture + bloom (the software-GL test harness turns it off for speed)
      if (this.view && this.map) this.view.setMap(this.project, this.map, this.T, this.urls); else if (!this.view) this.cv.style.background = "radial-gradient(ellipse at 50% 120%, #1c2b3d, #080b10)";
      const T = this.T; this.placeStart();
      this.hud = new PRIM.HUD({ host: this.hudHost, project: this.project, tags: T, urls: this.urls, screenId: T.get("player.hud"), edit: this.mode !== "play", say: t => { if (this.o.say) this.o.say(t); } });
      this.unsub = T.on(names => { this.drawCross(); if (this.mode === "play" && names && names.length && !names.some(n => (this.map && n.startsWith(this.map.id + ".")) || /^prop/.test(n) || n === "gfx.ao")) return; if (this.view && !this.pendingRb) { this.pendingRb = true; setTimeout(() => { this.pendingRb = false; if (!this.dead && this.view) { if (this.map) this.view.rebuild(); this.syncAtts(); this.syncThings(); } }, 80); } });
      this.drawCross(); this.gpu = {}; this.atts = [];
      this.sh = this.mode === "play" ? new PRIM.Script.Host({ tags: T, held: k => !!this.keys[k], isState: s => this.isState(s), orb: (k, c, a) => this.orbCmd(k, c, a), fire: key => this.fireProj(this.atts.find(a => "player." + a.id === key)), reload: key => this.reload(this.atts.find(a => "player." + a.id === key)), hasAmmo: key => { const pre = key + "."; return !this.T.get(pre + "ammoUse") || (!this.reloading && (+this.T.get(pre + "ammo") || 0) >= 1); }, sfx: (id, v) => this.sfx(id, v),
        dlgLine: l => this.dlgLine(l), dlgChoice: c => this.dlgChoice(c), pause: id => this.pauseOpen(id), dialogue: id => this.talkGraph(id, null), loop: (id, v) => this.loopSfx(id, v), stopLoop: id => this.stopLoops(id), playAnim: (k, n, l) => this.playAnim(k, n, l),
        isReloading: () => !!this.reloading, stopAnim: k => this.stopAnim(k), showHide: (k, m) => { const t = k + ".visible"; if (T.get(t) === undefined) return; T.set(t, m === "toggle" ? !T.get(t) : m === "show"); },
        shake: (a, sec) => { this.shakeA = Math.max(this.shakeA || 0, a); this.shakeT = sec; this.shakeT0 = sec; }, flash: (c, a) => this.flash(a, c, true),
        jump: (hgt, air) => { if (air || this.ground) this.jumpReq = hgt || -1; }, airJump: (max, hgt) => { if (!this.ground && (this.airJumps || 0) < max) { this.airJumps = (this.airJumps || 0) + 1; this.jumpReq = hgt || -1; } },
        launch: (f, sd, up) => { const cy = Math.cos(this.yaw), sy = Math.sin(this.yaw); this.extV = [(this.extV ? this.extV[0] : 0) + cy * f - sy * sd, (this.extV ? this.extV[1] : 0) + sy * f + cy * sd]; if (up) this.upReq = (this.upReq || 0) + up; },
        hurt: a => { if (a > 0) this.hurtPlayer(a); else if (a < 0 && !this.pdead) T.set("player.health", Math.min(+T.get("player.maxHealth") || 100, (+T.get("player.health") || 0) - a)); },
        teleport: () => { this.placeStart(); this.extV = null; }, nearEnemy: d => (this.things || []).some(o => this.eAlive(o) && Math.hypot(o.x - this.x, o.y - this.y) < d && Math.abs(o.floor - this.z) < 200),
        switchWeapon: (w, n) => { if (w === "next") this.setWeapon((this.wi || 0) + 1); else if (w === "prev") this.setWeapon((this.wi || 0) - 1); else if (n - 1 < this.wids().length) this.setWeapon(n - 1); },
        broadcast: name => this.trigger("onMessage", name), custom: (ow, n, g, tok) => this.custom(ow, n, g, tok), say: t => { if (this.o.say) this.o.say(t); else console.log(t); } }) : null;
      if (PRIM.Audio) PRIM.Audio.preload(this.project.assets.filter(x => x.type === "sound" && this.urls[x.id]).map(x => this.urls[x.id]));
      if (this.mode === "play") { if (PRIM.Schema && PRIM.Schema.ensureKeys) PRIM.Schema.ensureKeys(this.project); this.keysReset("all"); }
      this.slot = (this.o.save && this.o.save.slot) || +T.get("save.slot") || 1;
      this.syncAtts(); this.syncThings(); if (this.mode === "play" && this.o.save && this.o.save.load) this.loadGame(this.o.save.load); this.trigger("onStart"); if (this.sh) this.sh.watch(() => this.scriptOwners());
      if (this.mode === "play") this.bind(); else { this.msg.style.display = "none"; this.cv.style.cursor = "default"; }
      this.last = performance.now(); this.raf = requestAnimationFrame(t => this.frame(t));
    }
    destroy() {
      if (this.destroyed) return; this.destroyed = true; this.dead = true; this.stopLoops(""); this.stopSpeakers(); if (this.sh) this.sh.stopAll(); if (this.paused) this.pauseClose(true); cancelAnimationFrame(this.raf); this.unsub(); if (this.hud) this.hud.destroy(); if (this.unbind) this.unbind(); if (document.pointerLockElement === this.cv) document.exitPointerLock();
      if (this.view) { const e = this.view.gl.getExtension("WEBGL_lose_context"); if (e) e.loseContext(); } this.root.remove();
    }
    // ---- input
    bind() {
      const cv = this.cv, k = this.keys;
      // a visible Leave button, shown whenever the mouse is free (Esc first frees the mouse, so the menu is always reachable)
      const lv = document.createElement("button"); lv.className = "p3leave"; lv.textContent = "✕ Leave game  (Esc)"; lv.addEventListener("pointerdown", e => e.stopPropagation()); lv.addEventListener("click", e => { e.stopPropagation(); this.leave(); }); this.root.append(lv);
      this.lv = lv;
      const lock = () => { const on = document.pointerLockElement === cv; this.msg.style.display = on || this.paused ? "none" : "grid"; lv.style.display = on || (this.paused && !this.o.debug) ? "none" : "block";
        // the mouse was freed while playing (Esc in most browsers frees it before the page hears the key): open the pause menu
        if (!on && this.engaged && this.mode === "play" && !this.paused && !this.talk && !this.popup && !this.pdead && !this.dead && this.pauseId()) this.pauseOpen(); };
      this.msg.textContent = "Click to play  ·  W A S D move  ·  Space jump  ·  Shift sprint  ·  Esc " + (this.pauseId() ? "pause" : "to leave") + (this.o.debug ? "  ·  H / J test damage" : ""); this.msg.style.display = "grid";
      const kd = e => {
        if (!this.root.isConnected) return;
        if (this.paused) { if (e.key === "Escape") { e.stopImmediatePropagation(); e.preventDefault(); if (!e.repeat && performance.now() - this.paused.t0 > 250) this.pauseRt.back(); } return; }   // other keys go to the pause menu (its scripts)
        if (e.key === "Escape") { e.stopImmediatePropagation(); e.preventDefault(); if (this.mode === "play" && this.pauseId() && !this.pdead) this.pauseOpen(); else this.leave(); return; }
        if (/INPUT|SELECT|TEXTAREA/.test((e.target && e.target.tagName) || "")) return;   // typing in a text field is never a game key
        if (this.popup) { e.preventDefault(); if (!e.repeat) this.popupKey(keyId(e)); return; }
        if (this.talk) { e.preventDefault(); if (!e.repeat) this.talkKey(keyId(e)); return; }   // while talking, keys answer the dialogue
        const id = keyId(e), n = id; if (!e.repeat && !k[id]) { k[id] = true; this.trigger("onKey", id); this.aimInput(id, true); if (this.mode === "play" && id === (this.T.get("player.interactKey") || "e")) this.interact(); if (this.mode === "play" && id === (this.T.get("player.reloadKey") || "r")) this.reload(this.curWeapon()); } k[id] = true; if (/^[1-9]$/.test(n)) this.setWeapon(+n - 1); if (["w", "a", "s", "d", "space", "shift", "arrowup", "arrowdown", "arrowleft", "arrowright"].includes(id)) e.preventDefault();
        if (this.o.debug && n === "h") this.T.setMany({ "player.health": Math.max(0, this.T.get("player.health") - 10) }); if (this.o.debug && n === "j") this.T.setMany({ "player.health": Math.min(this.T.get("player.maxHealth"), this.T.get("player.health") + 10) });
      };
      const ku = e => this.release(keyId(e));
      const mm = e => { if (document.pointerLockElement === cv || this.drag) this.look(e.movementX, e.movementY); };
      const pd = e => { if (this.paused) return; if (this.popup) { this.popupClose(); return; } if (this.talk) return; if (this.engaged && e && e.button === 0 && this.mode === "play" && this.useClick()) return; if (this.engaged && e && e.button === 0) this.fire(); if (this.engaged && e) this.press(mouseId(e.button)); this.engaged = true; this.msg.style.display = "none"; this.drag = true; try { const r = cv.requestPointerLock && cv.requestPointerLock(); if (r && r.catch) r.catch(() => {}); } catch (x) {} cv.focus(); };
      const pu = e => { this.drag = false; if (e && e.button !== undefined) this.release(mouseId(e.button)); }, pl = () => lock(), bl = () => { for (const id of Object.keys(k)) this.release(id); }, noCtx = e => e.preventDefault(), noMid = e => { if (e.button !== 0) e.preventDefault(); };
      addEventListener("keydown", kd, true); addEventListener("keyup", ku, true); addEventListener("mousemove", mm); addEventListener("mouseup", pu); document.addEventListener("pointerlockchange", pl); addEventListener("blur", bl); this.root.addEventListener("contextmenu", noCtx); this.root.addEventListener("mousedown", noMid);
      this.root.addEventListener("mousedown", pd); cv.tabIndex = 0; const wh = e => { if (this.engaged) { e.preventDefault(); this.setWeapon((this.wi || 0) + (e.deltaY > 0 ? 1 : -1)); } }; this.root.addEventListener("wheel", wh, { passive: false });
      this.unbind = () => { removeEventListener("keydown", kd, true); removeEventListener("keyup", ku, true); removeEventListener("mousemove", mm); removeEventListener("mouseup", pu); document.removeEventListener("pointerlockchange", pl); removeEventListener("blur", bl); };
    }
    press(id) { if (this.keys[id]) return; this.keys[id] = true; this.trigger("onKey", id); this.aimInput(id, true); if (this.mode === "play" && id === (this.T.get("player.interactKey") || "e")) this.interact(); if (this.mode === "play" && id === (this.T.get("player.reloadKey") || "r")) this.reload(this.curWeapon()); }
    release(id) { if (!this.keys[id]) return; this.keys[id] = false; this.trigger("onKeyUp", id); this.aimInput(id, false); }
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
      if (held && !st && !this.sh.ok(own, n)) return;   // its "Only if" condition is not met: the weapon can not charge
      if (held && !st) { this.ch[pre] = { t: 0, charged: false, wait: 0, started: delay <= 0 }; if (delay <= 0) { this.playAnim(ok, g("chargeAnim"), false); this.sh.runOut(own, n.id, "start"); T.set("player.charge", 0); } }
      else if (held && st && !st.started) { st.wait += dt; if (st.wait >= delay) { st.started = true; this.playAnim(ok, g("chargeAnim"), false); this.sh.runOut(own, n.id, "start"); T.set("player.charge", 0); } }
      else if (!held && st && !st.started) delete this.ch[pre];   // let go before the delay: nothing was charged
      else if (held && st) {
        st.t += dt; const need = +g("chargeTime") > 0 ? +g("chargeTime") : (dur(g("chargeAnim")) || 1); this.chargeNeed = need;
        if (!st.charged) { T.set("player.charge", Math.min(1, st.t / need)); if (st.t >= need) { st.charged = true; this.stopAnim(ok); this.playAnim(ok, g("loopAnim"), true); T.set("player.charge", 1); this.sh.runOut(own, n.id, "charged"); } }
      } else if (!held && st) {
        delete this.ch[pre]; // the released charge belongs to the NEXT shot only (it used to stay for 1.5 s, so quick normal shots after it came out big and charged)
        this.pendingShot = { size: this.orb ? this.orb.size : 0, ch: st.charged ? 1 : Math.min(1, +T.get("player.charge") || 0), until: performance.now() + 1500 }; this.orb = null; this.stopAnim(ok);
        if (st.charged) { this.playAnim(ok, g("fireAnim"), false); this.sh.runOut(own, n.id, "fire"); } else this.sh.runOut(own, n.id, "cancel");
        T.set("player.charge", 0);
      }
    }
    // the charge particle: a ball at the weapon's emitter point that can grow while you charge
    orbCmd(key, cmd, a) {
      const at = this.atts.find(x => "player." + x.id === key); if (!at) return;
      if (cmd === "show") this.orb = { id: at.id, color: a.color, size: a.size, size0: a.size, grow: null };
      else if (cmd === "suck" && this.orb && this.orb.id === at.id) { this.orb.suck = a; this.orb.sp = this.orb.sp || []; }
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
      const v = this.view; if (!at || !at.emit || !v) return; const T = this.T, pre = "player." + at.id + ".", kind = T.get(pre + "projType"); if (!kind || kind === "none" || this.pdead) return; const fr = +T.get(pre + "fireRate") || 0, nowS = performance.now() / 1000; if (fr > 0 && nowS - (at.lastShot || -1e9) < 1 / fr - 0.001) return; if (!this.useAmmo(at)) return; at.lastShot = nowS; this.triggerAtt(at.id, "onFire"); const dmg = (T.get(pre + "projDamage") === undefined ? 25 : +T.get(pre + "projDamage"));
      const c = v.cam, B = R3D.camBasis(c.yaw, c.pitch), eye = [c.x, c.y, c.z], P = at.emit.pos, D0 = at.emit.dir;
      const O = [eye[0] + B.s[0] * P[0] + B.u[0] * P[1] - B.f[0] * P[2], eye[1] + B.s[1] * P[0] + B.u[1] * P[1] - B.f[1] * P[2], eye[2] + B.s[2] * P[0] + B.u[2] * P[1] - B.f[2] * P[2]];
      let D1 = M4.norm([B.s[0] * D0[0] + B.u[0] * D0[1] - B.f[0] * D0[2], B.s[1] * D0[0] + B.u[1] * D0[1] - B.f[1] * D0[2], B.s[2] * D0[0] + B.u[2] * D0[1] - B.f[2] * D0[2]]);
      const lockT = this.lock && this.lock.o && this.eAlive(this.lock.o) && this.aimW() && this.aimW().att === at ? this.lock.o : null; if (lockT) D1 = M4.norm(M4.sub(this.eCenter(lockT), O));   // locked on: straight at the target
      const hex = String(T.get(pre + "projColor") || "#ffcc33"), col = [parseInt(hex.slice(1, 3), 16) / 255, parseInt(hex.slice(3, 5), 16) / 255, parseInt(hex.slice(5, 7), 16) / 255];
      const ps = this.pendingShot && performance.now() < this.pendingShot.until ? this.pendingShot : null; this.pendingShot = null;
      const ch = Math.max(0, Math.min(1, ps ? ps.ch : 0)), mul = T.get(pre + "projCharge") ? 1 + 1.5 * ch : 1, orbS = ps ? ps.size : 0; this.fx = this.fx || [];
      const em = T.get(pre + "projEmit") === undefined ? 1 : +T.get(pre + "projEmit"), ecol = col.map(q => q * em), lt = T.get(pre + "projLight") && kind !== "hitscan" ? { c: col, k: +T.get(pre + "projLightPower") || 1.5, r: +T.get(pre + "projLightRange") || 220, shadow: T.get(pre + "projLightShadow") !== false } : null;
      const spr = (+T.get(pre + "projSpread") || 0) * (this.aimK > 0 ? 1 - this.aimK * (1 - (T.get(pre + "aimSpread") === undefined ? 0.3 : +T.get(pre + "aimSpread"))) : 1) * Math.PI / 180, cnt = Math.max(1, Math.min(30, Math.round(+T.get(pre + "projCount") || 1)));
      for (let pel = 0; pel < cnt; pel++) { const D = spr > 0 ? this.coneDir(D1, spr) : D1;
      if (kind === "hitscan") { let hit = v.raycast(O, D, 6000); const far = hit ? hit.t || M4.len(M4.sub(hit.p, O)) : 6000, dh = this.hitDoor(O, D, far), he = this.hitEnemy(O, D, dh ? dh.t : far); if (hit && !he && !dh) this.impact(pre, hit, ch); if (he) { this.damageEnemy(he.o, dmg * mul); hit = { p: M4.add(O, D.map(q => q * he.t)), n: D.map(q => -q) }; } else if (dh) { const sr = this.shootDoor(dh.th, pre, ch), od = (this.things || []).find(x => x.th === dh.th); hit = { p: M4.add(O, D.map(q => q * dh.t)), n: D.map(q => -q) }; if (sr === "wrong" && od && od.shA > 0.1) this.shieldHit(od, hit.p, D, true); } if (hit) this.explode(pre, hit.p, hit.n, he ? "enemy" : dh ? "door" : "wall", ch); else this.explode(pre, null, null, "end", ch); const end = hit ? hit.p : M4.add(O, D.map(q => q * 3000)); this.fx.push({ kind: "line", a: O, b: end, c: ecol, w: +T.get(pre + "projWidth") || 0.6, life: +T.get(pre + "projLineTime") || 0.12, t: 0 }); }
      else if (kind === "particle") this.fx.push({ pre, ch, home: lockT && T.get(pre + "lockHoming") !== false ? lockT : null, homeK: +T.get(pre + "lockHomingTurn") || 4, kind: T.get(pre + "projShape") === "laser" ? "laser" : "sphere", flat: T.get(pre + "projShade") === "flat", dmg: dmg * mul, p: O, d: D, c: ecol, light: lt, size: orbS > 0 ? orbS : (+T.get(pre + "projSize") || 3) * mul, speed: +T.get(pre + "projSpeed") || 1400, life: +T.get(pre + "projLife") || 2, t: 0 });
      else if (kind === "mesh") {
        const aid = T.get(pre + "projModel"), model = aid && PRIM.FBX.cache[aid]; if (!model) return; if (!this.gpu[aid]) this.gpu[aid] = v.loadModel(model);
        this.fx.push({ pre, ch, home: lockT && T.get(pre + "lockHoming") !== false ? lockT : null, homeK: +T.get(pre + "lockHomingTurn") || 4, kind: "mesh", dmg: dmg * mul, p: O, d: D, gm: this.gpu[aid], pose: PRIM.FBX.pose(model, null, 0), scale: (+T.get(pre + "projScale") || 1) * mul, rot: M4.rotXYZ([+T.get(pre + "projRx") || 0, +T.get(pre + "projRy") || 0, +T.get(pre + "projRz") || 0]), speed: +T.get(pre + "projSpeed") || 1400, life: +T.get(pre + "projLife") || 2, t: 0, M: M4.ident(), light: lt, mat: { emit: [1, 1, 1].map(q => q * Math.max(0, em - 1) * 0.4) } });
      }
      }   // (one pass per pellet)
    }
    // a shot hit the level: the weapon's wall hit sound (quieter far away) and its mark / animation stuck to the surface
    isCharged(pre, ch) { return (ch || 0) >= Math.min(1, +this.T.get(pre + "chargedFrom") || 1) - 1e-6 && (ch || 0) > 0; }
    impact(pre, hit, ch) {
      const T = this.T, snd = (this.isCharged(pre, ch) && T.get(pre + "impactSoundCharged")) || T.get(pre + "impactSound"), img = T.get(pre + "impactImage"), c = this.view && this.view.cam;
      if (snd && c) this.sfxAt(snd, 1, hit.p);
      if (!img || !hit.n) return; const g = k => +T.get(pre + k) || 0, n = hit.n, frames = Math.max(1, Math.round(g("impactFrames") || 1));
      (this.marks = this.marks || []).push({ kind: "decal", texId: img, p: [hit.p[0] + n[0] * 0.5, hit.p[1] + n[1] * 0.5, hit.p[2] + n[2] * 0.5], n, size: g("impactSize") || 16, frames, fps: g("impactFps") || 15, stay: T.get(pre + "impactStay") === undefined ? 4 : g("impactStay"), t: 0, frame: 0, lit: T.get(pre + "impactGlow") ? 0 : 1 });
      if (this.marks.length > 80) this.marks.shift();
    }
    stepMarks(dt) {
      if (!this.marks || !this.marks.length) return []; const cam = this.view && this.view.cam; this.marks = this.marks.filter(m => { m.t += dt; if (m.bill && cam) { const d = [cam.x - m.p[0], cam.y - m.p[1], cam.z - m.p[2]], L = Math.hypot(...d) || 1; m.n = d.map(q => q / L); } const anim = m.frames > 1 ? m.frames / m.fps : 0; m.frame = Math.min(m.frames - 1, Math.floor(m.t * m.fps)); const end = anim + m.stay; if (m.t > end) return false; m.alpha = Math.min(1, (end - m.t) / 0.5); return true; });
      return this.marks;
    }
    stepFx(dt) {
      const v = this.view, fx = this.fx; if (!fx || !fx.length || !v) return; this.fx = [];   // explosions made while stepping land in the fresh list
      let deco = 0; const kept = fx.filter(f => { if (f.piece) return this.pieceStep(f, dt); if (f.deco && f.kind === "sphere") deco++;
        f.t += dt; const boom = (p, n, what) => { if (f.pre && !f.hostile) this.explode(f.pre, p, n, what, f.ch || 0); };
        if (f.deco) { if (f.t > f.life) return false; const a = f.t / f.life; if (f.light) f.light.k = f.light.k0 * (1 - a) * (1 - a); if (f.speed) { f.p = M4.add(f.p, f.d.map(q => q * f.speed * dt)); f.d[1] -= (f.grav || 0) * dt / Math.max(1, f.speed); f.speed *= Math.max(0, 1 - 2.5 * dt); f.size = f.size0 * (1 - a * 0.85); } return true; }
        if (f.t > f.life) { if (f.kind !== "line") boom(f.p, null, "end"); return false; }
        if (f.home && f.d) { if (this.eAlive(f.home)) { const to = M4.norm(M4.sub(this.eCenter(f.home), f.p)), a = Math.min(1, (f.homeK || 4) * dt); f.d = M4.norm(f.d.map((q, i) => q + (to[i] - q) * a)); } else f.home = null; }   // locked-on shots curve toward the target
        if (f.kind !== "line") { const step = f.speed * dt; if (f.hostile) { const q = M4.add(f.p, f.d.map(z => z * step)); if (!this.pdead && Math.hypot(q[0] - this.x, q[2] - this.y) < (+this.T.get("player.radius") || 16) + (f.size || 4) && q[1] > this.z - 4 && q[1] < this.z + (+this.T.get("player.eyeHeight") || 102) + 12) { this.hurtPlayer(f.dmg); return false; } }
        else { const he = this.hitEnemy(f.p, f.d, step + (f.size || 1)); if (he) { this.damageEnemy(he.o, f.dmg || 0); boom(M4.add(f.p, f.d.map(q => q * he.t)), f.d.map(q => -q), "enemy"); return false; } }
        const dh = this.hitDoor(f.p, f.d, step + (f.size || 1)); if (dh) { if (!f.hostile) { const sr = this.shootDoor(dh.th, f.pre, f.ch || 0), od = (this.things || []).find(x => x.th === dh.th), hp = M4.add(f.p, f.d.map(q => q * dh.t));
            if (sr === "wrong" && od && od.shA > 0.1) { this.shieldHit(od, hp); if (!f.bounced && this.ov(od, "ricochet") !== false) { const n = this.doorN(od, f.d); f.d = M4.norm(M4.sub(f.d, n.map(q => q * 2 * M4.dot(f.d, n)))); f.p = M4.add(hp, n.map(q => q * ((f.size || 1) + 2))); f.bounced = true; return true; } } } boom(M4.add(f.p, f.d.map(q => q * dh.t)), f.d.map(q => -q), "door"); return false; } const hit = v.raycast(f.p, f.d, step + (f.size || 1)); if (hit) { if (f.pre && !f.hostile) this.impact(f.pre, hit, f.ch || 0); boom(hit.p, hit.n, "wall"); return false; } f.p = M4.add(f.p, f.d.map(q => q * step)); if (f.kind === "mesh") f.M = M4.mul(M4.translate(...f.p), M4.mul(M4.between([0, 0, -1], f.d, Math.PI), M4.mul(f.rot, M4.scale(f.scale, f.scale, f.scale)))); }
        return true;
      });
      this.fx = kept.concat(this.fx); this.decoN = deco;
    }
    // ---- placed objects (map.things): doors in walls, pick-ups, decorations
    syncThings() {
      const m = this.map, T = this.T, F = PRIM.FBX; if (!m || !this.view) { this.things = []; return; } const old = this.things || [], out = [], gone = new Set(old.map(o => o.th.id));
      for (const th of (m.things || []).concat(this.spawnTh || [])) {
        const prev = old.find(x => x.th.id === th.id), pid = prev && prev.morph && (this.project.props || []).some(q => q.id === prev.morph) ? prev.morph : th.prop, pre = pid + ".", aid = T.get(pre + "asset"), type0 = T.get(pre + "type"), type = type0 === "boss" ? "enemy" : type0, boss = type0 === "boss";
        if (type !== "door" && T.get(pre + "look") === "sprite") { gone.delete(th.id); const o = old.find(x => x.th.id === th.id) || { th, st: "closed", p: 0, t: 0, timer: 0, taken: false, spin: 0 }; Object.assign(o, { th, pre, aid: null, model: null, gm: null, sprite: true, type, boss, fr: G.thingFrame(m, T, th) }); if (!o.init) o.floor = o.fr.fy; out.push(o); continue; }
        if ((type === "speaker" || type === "interactive" || type === "checkpoint" || type === "spawner" || type === "save" || (type === "decoration" && T.get(pre + "solid") === "box")) && (!aid || !this.urls[aid])) { gone.delete(th.id); const o = old.find(x => x.th.id === th.id) || { th, st: "closed", p: 0, t: 0, timer: 0, taken: false, spin: 0 }; Object.assign(o, { th, pre, aid: null, model: null, gm: null, sprite: false, type, fr: G.thingFrame(m, T, th) }); o.floor = o.fr.fy; out.push(o); continue; }
        if (!aid || !this.urls[aid]) continue; const model = F.cache[aid];
        if (!model) { F.load(aid, this.urls[aid]).then(mm => { if (mm && !this.dead) this.syncThings(); }); continue; }
        if (!this.gpu[aid]) this.gpu[aid] = this.view.loadModel(model); gone.delete(th.id); { const q = old.find(x => x.th.id === th.id); if (q) q.sprite = false; }
        const o = old.find(x => x.th.id === th.id) || { th, st: "closed", p: 0, t: 0, timer: 0, taken: false, spin: 0 }; o.th = th; o.pre = pre; o.aid = aid; o.model = model; o.gm = this.gpu[aid]; o.type = type; o.boss = boss; o.fr = G.thingFrame(m, T, th); if (!o.init) o.floor = o.fr.fy; out.push(o);
      }
      for (const o of out) { const tp = m.id + ".th." + o.th.id + "."; o.tp = o.type === "door" && T.has(tp + "openMode") ? tp : null; }
      this.things = out; this.doorOpen = this.doorOpen || {};
    }
    thingHit(o, O, D, maxd) {   // ray against a door's box in its own space; returns distance or null
      const f = o.fr, w = f.w || 128, h = f.h || 128, th = f.thick || 16, dx = O[0] - f.cx, dz = O[2] - f.cy;
      const lo = [dx * f.ux + dz * f.uy, O[1] - o.floor, dx * f.nx + dz * f.ny], ld = [D[0] * f.ux + D[2] * f.uy, D[1], D[0] * f.nx + D[2] * f.ny], mn = [-w / 2, 0, -th / 2], mx = [w / 2, h, th / 2]; let t0 = 0, t1 = maxd;
      for (let a = 0; a < 3; a++) { if (Math.abs(ld[a]) < 1e-6) { if (lo[a] < mn[a] || lo[a] > mx[a]) return null; } else { let q0 = (mn[a] - lo[a]) / ld[a], q1 = (mx[a] - lo[a]) / ld[a]; if (q0 > q1) [q0, q1] = [q1, q0]; t0 = Math.max(t0, q0); t1 = Math.min(t1, q1); if (t0 > t1) return null; } }
      return t0;
    }
    hitDoor(O, D, maxd) { let best = null; for (const o of this.things || []) { if (o.type !== "door" || o.st !== "closed") continue; const t = this.thingHit(o, O, D, maxd); if (t !== null && (!best || t < best.t)) best = { th: o.th, t }; } return best; }
    // closed doors are solid blocks for walking, shots and sight (open ones step aside)
    doorTris() { const out = [], shot = []; for (const o of this.things || []) { if (o.type === "door" && !(this.doorOpen && this.doorOpen[o.th.id])) { const t = G.boxTris(...G.doorBox(o.fr), { kind: "door", id: o.th.id }); out.push(...t); shot.push(...t); continue; }
        const st = this.solidTris(o); if (st) { out.push(...st); if (this.ov(o, "solidShots") !== false) shot.push(...st); } }
      this.dtris = out; if (this.view) this.view.extraTris = shot; return out; }
    // a solid object's collision box (prop tag solid: auto = around the model, box = solidX/Y/Z + solidW/H/D), in world triangles; cached until it moves or its settings change
    solidBox(o) { const mode = this.ov(o, "solid"); if (!mode || mode === "none" || o.taken) return null; const g = k => +this.ov(o, k) || 0;
      if (mode === "box") { const c = [g("solidX"), g("solidY"), g("solidZ")], h = [Math.max(1, g("solidW")) / 2, Math.max(1, g("solidH")) / 2, Math.max(1, g("solidD")) / 2]; return { mn: [c[0] - h[0], c[1] - h[1], c[2] - h[2]], mx: [c[0] + h[0], c[1] + h[1], c[2] + h[2]] }; }
      if (o.model && o.model.bounds) { const b = o.model.bounds, sc = g("scale") || 1, R = M4.mul(M4.rotXYZ([g("rx"), g("ry"), g("rz")]), M4.scale(sc, sc, sc)), mn = [1e9, 1e9, 1e9], mx = [-1e9, -1e9, -1e9];
        for (const x of [b.lo[0], b.hi[0]]) for (const y of [b.lo[1], b.hi[1]]) for (const z of [b.lo[2], b.hi[2]]) { const p = M4.point(R, x, y, z); for (let i = 0; i < 3; i++) { mn[i] = Math.min(mn[i], p[i]); mx[i] = Math.max(mx[i], p[i]); } }
        const off = [g("ox"), g("oy"), g("oz")]; return { mn: mn.map((v, i) => v + off[i]), mx: mx.map((v, i) => v + off[i]) }; }
      if (o.sprite) { const hh = g("spriteHeight") || 110, w = Math.min(64, hh / 2); return { mn: [-w / 2, 0, -w / 2], mx: [w / 2, hh, w / 2] }; }
      return null; }
    solidTris(o) { const b = this.solidBox(o); if (!b) { o.solT = null; return null; } const f = o.fr, key = [f.cx, f.cy, o.floor, f.ux, f.uy, b.mn, b.mx].join(",");
      if (o.solK !== key) { o.solK = key; o.solT = G.boxTrisF(b.mn, b.mx, p => this.hbToWorld(o, p), { kind: "solid", id: o.th.id }); } return o.solT; }
    // ---- talking to characters (NPCs): lines with up to 4 choices; a choice can change a tag and jump to another line
    npcInFront() {
      const T = this.T, fx = Math.cos(this.yaw), fy = Math.sin(this.yaw); let best = null, bd = 1e9;
      for (const o of this.things || []) { if (o.type !== "npc" || o.taken || !((o.th.prop && this.project.props.find(p => p.id === o.th.prop) || {}).dialog || {}).lines) continue; const dx = o.fr.cx - this.x, dy = o.fr.cy - this.y, d = Math.hypot(dx, dy);
        if (d > (+this.ov(o, "talkRange") || 140) || Math.abs(o.floor - this.z) > 100) continue; if (d > 20 && (dx * fx + dy * fy) / d < 0.6) continue; if (d < bd) { bd = d; best = o; } }
      return best;
    }
    talkHint() {
      if (!this.hintEl) { this.hintEl = el("", "position:absolute;left:50%;top:62%;transform:translateX(-50%);padding:6px 14px;background:rgba(10,14,20,.7);border:1px solid rgba(255,255,255,.25);border-radius:4px;color:#e8f2fb;font:600 16px 'Chakra Petch',system-ui,sans-serif;pointer-events:none;z-index:6;display:none"); this.root.append(this.hintEl); }
      { const st = !this.talk && !this.pdead ? this.stationNear() : null; if (st) { this.hintEl.style.display = "block"; this.hintEl.textContent = "[" + String(this.T.get("player.interactKey") || "e").toUpperCase() + "] Save the game"; return; } }
      const n = !this.talk && !this.pdead ? this.npcInFront() : null, ky = "[" + String(this.T.get("player.interactKey") || "e").toUpperCase() + "] ";
      if (n) { this.hintEl.style.display = "block"; this.hintEl.textContent = ky + "Talk to " + (this.T.get(n.pre + "npcName") || this.T.get(n.pre + "name") || "them"); return; }
      const hb = !this.talk && !this.pdead ? (this.ui ? this.ui.hover : this.hbAim(["key", "aim"])) : null; this.hintEl.style.display = hb ? "block" : "none";
      if (hb) { const T = this.T, k = hb.o.pre + "hb." + hb.b.id + ".", mode = T.get(hb.o.pre + "useMode") || "key", nm = T.get(hb.o.pre + "useHint") || T.get(k + "name") || "Use"; this.hintEl.textContent = (mode === "key" ? ky : mode === "aim" ? "[Click] " : "") + nm + (this.hbLocked(hb.o, hb.b) ? "  (locked)" : ""); }
    }
    talkStart(o) {
      const p = this.project.props.find(x => x.id === o.th.prop), dg = this.ov(o, "npcDialogue"); if (dg && (this.project.dialogues || []).some(d => d.id === dg)) { this.talkGraph(dg, o); return; }
      if (!p || !p.dialog || !p.dialog.lines.length) return;
      this.talk = { o, p, i: 0 }; this.talkOpen(); this.talkLine();
    }
    talkOpen() {
      for (const k of Object.keys(this.keys)) this.release(k); try { if (document.pointerLockElement === this.cv) document.exitPointerLock(); } catch (e) {}
      if (!this.talkEl) { this.talkEl = el("p3talk", "position:absolute;left:50%;bottom:6%;transform:translateX(-50%);width:min(820px,90%);padding:16px 20px;background:rgba(8,12,18,.88);border:1px solid rgba(160,200,230,.45);border-radius:6px;color:#e8f2fb;font:17px/1.45 'Chakra Petch',system-ui,sans-serif;z-index:8;box-shadow:0 8px 30px rgba(0,0,0,.5)"); this.talkEl.addEventListener("pointerdown", e => e.stopPropagation()); this.root.append(this.talkEl); }
      this.talkEl.style.display = "block"; this.msg.style.display = "none";
    }
    // the talk box: speaker, text and either choices (keys 1-4 / click) or Continue (Interact / Space / Enter)
    talkBox(who, text, picks, cont, sound) {
      const t = this.talk, T = this.T; if (!t) return;
      if (sound) { if (this.voice && this.voice.playing) this.voice.stop(0.05); this.voice = this.sfx(sound, 1); }
      const box = this.talkEl; box.replaceChildren(); if (who) { box.append(el("", "color:#7df3ff;font-weight:700;margin-bottom:4px;letter-spacing:.04em")); box.lastChild.textContent = who; }
      const tx = el("", "white-space:pre-wrap"); tx.textContent = text || ""; box.append(tx); const opts = el("", "display:flex;flex-direction:column;gap:6px;margin-top:12px"); box.append(opts);
      const btn = (label, fn) => { const b = document.createElement("button"); b.textContent = label; b.style.cssText = "text-align:left;font:inherit;font-size:16px;color:#e8f2fb;background:rgba(40,60,80,.55);border:1px solid rgba(160,200,230,.35);border-radius:4px;padding:6px 12px;cursor:pointer"; b.onmouseenter = () => { b.style.background = "rgba(60,100,130,.75)"; }; b.onmouseleave = () => { b.style.background = "rgba(40,60,80,.55)"; }; b.onclick = e => { e.stopPropagation(); fn(); }; opts.append(b); };
      t.pick = picks && picks.length ? picks.map(p => p.fn) : null; t.cont = t.pick ? null : cont;
      if (t.pick) picks.forEach((p, n) => btn((n + 1) + ".  " + p.label, p.fn)); else if (cont) btn("Continue  ▸   (" + String(T.get("player.interactKey") || "e").toUpperCase() + " / Space)", cont);
    }
    // a dialogue made in the Dialogue Editor (a node graph). o = the NPC talking (or null: a zone / script started it). Returns a promise that ends with the talk.
    talkGraph(id, o) {
      const d = (this.project.dialogues || []).find(x => x.id === id); if (!d || !this.sh || this.dead) return null; if (this.talk) this.talkEnd();
      const t = this.talk = { o, graph: d }; this.talkOpen(); this.talkBox("", "…", null, null);
      const pr = this.sh.play({ key: d.id, script: d }); t.done = pr; pr.then(() => { if (this.talk === t) this.talkEnd(); }); return pr;
    }
    talkWho(s) { const o = this.talk && this.talk.o; return s || (o ? this.ov(o, "npcName") || this.ov(o, "name") || "" : ""); }
    dlgLine(l) {
      const t = this.talk; if (!t || !t.graph) return Promise.resolve(); t.lastText = l.text; t.lastWho = this.talkWho(l.speaker);
      return new Promise(res => { let timer = 0; const go = () => { if (timer) clearTimeout(timer); if (t.res === go) t.res = null; res(); }; t.res = go; this.talkBox(t.lastWho, l.text, null, go, l.sound); if (l.auto > 0) timer = setTimeout(go, l.auto * 1000); });
    }
    dlgChoice(c) {
      const t = this.talk; if (!t || !t.graph) return Promise.resolve(null);
      return new Promise(res => { const done = v => { if (t.res === stop) t.res = null; res(v); }, stop = () => done(null); t.res = stop; this.talkBox(c.speaker || c.text ? this.talkWho(c.speaker) : t.lastWho, c.text || t.lastText || "", c.opts.map(q => ({ label: q.label, fn: () => done(q.out) })), null); });
    }
    talkLine() {
      const t = this.talk, T = this.T, l = t.p.dialog.lines[t.i]; if (!l) return this.talkEnd(); const pre = t.p.id + ".d." + l.id + ".", g = k => T.get(pre + k);
      const who = g("speaker") || T.get(t.o.pre + "npcName") || T.get(t.o.pre + "name") || "", ch = [1, 2, 3, 4].filter(i => String(g("c" + i + "text") || "").trim());
      t.choices = ch; this.talkBox(who, g("text") || "", ch.map(i => ({ label: g("c" + i + "text"), fn: () => this.talkChoose(i) })), () => this.talkGo(g("next")), g("sound"));
    }
    talkKey(id) {
      const t = this.talk; if (!t) return; if (t.pick) { const n = +id; if (n >= 1 && n <= t.pick.length) t.pick[n - 1](); return; }
      if (t.cont && (id === "space" || id === "enter" || id === (this.T.get("player.interactKey") || "e"))) t.cont();
    }
    talkChoose(i) {
      const t = this.talk, T = this.T, l = t.p.dialog.lines[t.i], pre = t.p.id + ".d." + l.id + ".", tag = T.get(pre + "c" + i + "tag"), raw = String(T.get(pre + "c" + i + "val") || "").trim();
      if (tag && T.has(tag) && raw !== "") { const cur = T.get(tag); let v = raw; if (/^[+-]\d+(\.\d+)?$/.test(raw) && typeof cur === "number") v = cur + parseFloat(raw); else if (/^-?\d+(\.\d+)?$/.test(raw)) v = parseFloat(raw); else if (raw === "true" || raw === "false") v = raw === "true"; T.set(tag, v); }
      this.talkGo(T.get(pre + "c" + i + "go"));
    }
    talkGo(go) { const t = this.talk; if (!t) return; if (!go || go === "next") t.i++; else if (go === "end") return this.talkEnd(); else t.i = t.p.dialog.lines.findIndex(l => l.id === go); this.talkLine(); }
    talkEnd() { const t = this.talk; this.talk = null; if (t && t.graph) { if (this.sh) this.sh.stopOwner(t.graph.id); if (t.res) t.res(); } if (this.talkEl) this.talkEl.style.display = "none"; this.msg.style.display = document.pointerLockElement === this.cv ? "none" : "grid"; }
    // a shot-open door: the first opening shot can be required to come from a certain weapon (a missile…) or be charged; after that any shot opens it (shotAfter)
    shootDoor(th, pre, ch) { const o = (this.things || []).find(x => x.th === th); if (!o) return "none"; const s0 = this.shieldState(o); if (this.ov(o, "openMode") !== "shot") return s0 ? "wrong" : "none"; if (s0 && s0.sealed) return "wrong"; const need = this.ov(o, "shotFirst") || "any";
      if (need !== "any" && !o.shotOk) { const ok = need === "weapon" ? !!pre && !!this.ov(o, "shotWeapon") && pre === "player." + this.ov(o, "shotWeapon") + "." : !!pre && this.isCharged(pre, ch || 0);
        if (!ok) { const ms = this.ov(o, "shotMsg"); if (ms) { const now = performance.now(); if (!(now - (o.lockT || 0) < 1500)) { o.lockT = now; this.toast(this.T.interpolate(String(ms))); } } return "wrong"; }
        if (this.ov(o, "shotAfter") !== false) { o.shotOk = true; this.remember("shot", o); } }
      if (s0 && o.shA > 0.1) this.shieldBreak(o); this.openDoor(o); return "open"; }
    // ---- door energy shields: a weapon door glows in the needed weapon's Door colour (charged-shot doors and doors sealed by their tag in shieldColor) until it is opened
    shieldState(o) { if (o.type !== "door" || this.ov(o, "shieldOn") === "off") return null;
      const need = this.ov(o, "openMode") === "shot" ? this.ov(o, "shotFirst") || "any" : "any", left = !o.shotOk && !o.shieldGone, wpn = need === "weapon" && left, chg = need === "charged" && left;
      const nt = this.ov(o, "lockNeedTag"), sealed = !!nt && !!this.sh && !this.sh.test(nt, this.ov(o, "lockNeedOp") || "=", this.ov(o, "lockNeedVal")); if (!wpn && !chg && !sealed) return null;
      const col = wpn && !sealed ? this.T.get("player." + this.ov(o, "shotWeapon") + ".doorColor") || this.ov(o, "shieldColor") : this.ov(o, "shieldColor"); return { col: this.hexCol(col || "#ff3a5a"), wpn, chg, sealed }; }
    shieldItems(list, dt) { for (const o of this.things || []) { if (o.type !== "door") continue; const s = this.shieldState(o), goal = s ? 1 : 0;
        if (s && s.sealed && (o.st === "open" || o.st === "opening")) this.closeDoor(o);   // a sealed door shuts behind you (boss rooms)
        o.shA = o.shA === undefined ? goal : goal > o.shA ? Math.min(1, o.shA + dt * 3) : Math.max(0, o.shA - dt * 1.5); if (s) o.shCol = s.col; if (o.shHit) o.shHit[3] += dt;
        if (o.shA <= 0.005 || !o.shCol) continue; const [mn, mx] = G.doorBox(o.fr), e = 3;
        list.push({ kind: "shield", mn: mn.map(v => v - e), mx: mx.map(v => v + e), c: o.shCol, a: (+this.ov(o, "shieldAlpha") || 0.5) * o.shA, s: +this.ov(o, "shieldSize") || 1, sp: this.ov(o, "shieldSpeed") === undefined ? 1 : +this.ov(o, "shieldSpeed"), g: this.ov(o, "shieldGlow") === undefined ? 1.4 : +this.ov(o, "shieldGlow"), hit: o.shHit || [0, 0, 0, -1] }); } }
    doorN(o, d) { const n = [o.fr.nx, 0, o.fr.ny]; return M4.dot(n, d) > 0 ? n.map(q => -q) : n; }
    // a wrong shot: a ripple from where it hit, a clank; hitscan shots also bounce off as a short tracer
    shieldHit(o, p, D, scan) { o.shHit = [p[0], p[1], p[2], 0]; const s = this.ov(o, "shieldClank"); if (s) this.sfxAt(s, 1, p);
      if (scan && D && this.ov(o, "ricochet") !== false) { const n = this.doorN(o, D), R = M4.norm(M4.sub(D, n.map(q => q * 2 * M4.dot(D, n)))), a = M4.add(p, n.map(q => q * 2)), h = this.view && this.view.raycast(a, R, 1500); (this.fx = this.fx || []).push({ kind: "line", a, b: h ? h.p : M4.add(a, R.map(q => q * 1500)), c: o.shCol || [1, 1, 1], w: 0.5, life: 0.15, t: 0 }); } }
    shieldBreak(o) { o.shieldGone = true; this.remember("shields", o); const f = o.fr, c = [f.cx, o.floor + (f.h || 128) / 2, f.cy], col = o.shCol || [1, 0.4, 0.4]; o.shA = 0;
      this.burst(c, col, 60, 320, 0.7, 3); (this.fx = this.fx || []).push({ kind: "sphere", deco: true, p: c, d: [0, 1, 0], c: [0, 0, 0], size0: 0.01, size: 0.01, speed: 0, life: 0.4, t: 0, light: { c: col, k: 3, k0: 3, r: 420, shadow: false } });
      const s = this.ov(o, "shieldBreak"); if (s) this.sfxAt(s, 1, c); }
    // locked doors: a key (lockKey; used up once when lockUse) and / or a tag condition. Returns true while locked (and says so, at most every 2 s)
    doorLocked(o, quiet) { const T = this.T, k = this.ov(o, "lockKey"), hasK = !!(k && T.has(k + ".have")), keyOk = !hasK || o.unlocked || !!T.get(k + ".have"), nt = this.ov(o, "lockNeedTag"), tagOk = !nt || !this.sh || this.sh.test(nt, this.ov(o, "lockNeedOp") || "=", this.ov(o, "lockNeedVal"));
      if (keyOk && tagOk) { if (hasK && !o.unlocked) { o.unlocked = true; this.remember("unlocked", o); if (this.ov(o, "lockUse")) T.set(k + ".have", false); } return false; }
      if (!quiet) { const now = performance.now(); if (!(now - (o.lockT || 0) < 2000)) { o.lockT = now; const nm = hasK && !keyOk ? String(T.get(k + ".keyName") || "key") : "", ms = String(this.ov(o, "lockMsg") || "Locked."); this.toast(T.interpolate(nm || !/\{key\}/.test(ms) ? ms.split("{key}").join(nm) : "Locked.")); const s = this.ov(o, "lockSound"); if (s) this.sfxAt(s, 1, this.thingPos(o)); } }
      return true; }
    openDoor(o, quiet) { if (o.st === "open" || o.st === "opening") return; if (this.doorLocked(o, quiet)) return; o.st = "opening"; o.t = o.p * this.doorLen(o, "openAnim"); o.timer = 0; const s = this.ov(o, "openSound"); if (s) this.sfxAt(s, 1, this.thingPos(o)); }
    closeDoor(o) { if (o.st === "closed" || o.st === "closing") return; o.st = "closing"; o.timer = 0; const s = this.ov(o, "closeSound"); if (s) this.sfxAt(s, 1, this.thingPos(o)); }
    doorLen(o, which) { const nm = this.ov(o, (which === "closeAnim" && this.ov(o, "closeAnim") ? "closeAnim" : "openAnim")), a = nm && o.model.anims[nm]; return a ? Math.max(0.05, this.animLen(o.aid, nm, a.dur).len) : 0.8; }
    // ---- interactive objects (prop type "interactive"): invisible hitboxes in the object's own space (Object Editor grid: X right, Y up, Z front).
    // Used three ways (prop tag useMode): "key" = look at it + Interact key, "aim" = crosshair + left click, "cursor" = a screen: stepping up to it
    // turns mouse movement into an on-screen cursor (the view stops turning) until the cursor leaves the object's box on screen.
    hbOf(o) { const p = (this.project.props || []).find(x => x.id === o.th.prop); return (p && p.hitboxes) || []; }
    hbFrame(o) { const f = o.fr; return { O: [f.cx, o.floor, f.cy], X: [f.ux, 0, f.uy], Z: [f.nx, 0, f.ny] }; }
    hbBox(o, b) { const g = k => +this.ov(o, "hb." + b.id + "." + k) || 0; return { c: [g("x"), g("y"), g("z")], h: [Math.max(0.5, g("w")) / 2, Math.max(0.5, g("h")) / 2, Math.max(0.5, g("d")) / 2] }; }
    hbToWorld(o, p) { const F = this.hbFrame(o); return [0, 1, 2].map(k => F.O[k] + F.X[k] * p[0] + (k === 1 ? p[1] : 0) + F.Z[k] * p[2]); }
    // nearest hitbox along a world ray (only objects in `modes`, within their range, not behind a wall); only = one object
    hbRay(O, D, modes, only) {
      const T = this.T, v = this.view; let best = null;
      for (const o of this.things || []) { if (o.type !== "interactive" || (only && o !== only)) continue; const mode = this.ov(o, "useMode") || "key"; if (modes && !modes.includes(mode)) continue; const range = +this.ov(o, "useRange") || 120, F = this.hbFrame(o), rel = M4.sub(O, F.O);
        const lo = [M4.dot(rel, F.X), rel[1], M4.dot(rel, F.Z)], ld = [M4.dot(D, F.X), D[1], M4.dot(D, F.Z)];
        for (const b of this.hbOf(o)) { const bx = this.hbBox(o, b); let t0 = 0, t1 = range + 1e-3, ok = true;
          for (let a = 0; a < 3 && ok; a++) { const mn = bx.c[a] - bx.h[a], mx = bx.c[a] + bx.h[a]; if (Math.abs(ld[a]) < 1e-9) { if (lo[a] < mn || lo[a] > mx) ok = false; continue; } let ta = (mn - lo[a]) / ld[a], tb = (mx - lo[a]) / ld[a]; if (ta > tb) [ta, tb] = [tb, ta]; t0 = Math.max(t0, ta); t1 = Math.min(t1, tb); if (t0 > t1) ok = false; }
          if (ok && (!best || t0 < best.t)) best = { o, b, t: t0 }; } }
      if (best && v) { const w = v.raycast(O, D, best.t); if (w && (w.t || 0) < best.t - 2) return null; } return best;
    }
    hbAim(modes) { const v = this.view; if (!v) return null; const c = v.cam, B = R3D.camBasis(c.yaw, c.pitch); return this.hbRay([c.x, c.y, c.z], B.f, modes); }
    hbLocked(o, b) { const T = this.T, k = o.pre + "hb." + b.id + ".", t = T.get(k + "needTag"); return !!(t && this.sh && !this.sh.test(t, T.get(k + "needOp") || "=", T.get(k + "needVal"))); }
    hbPress(o, b) {
      const T = this.T, k = o.pre + "hb." + b.id + ".", st = (o.hbs = o.hbs || {})[b.id] = (o.hbs || {})[b.id] || { last: -1e9, used: false }, now = performance.now() / 1000, P = this.hbToWorld(o, this.hbBox(o, b).c);
      if (st.used || now - st.last < (+T.get(k + "cooldown") || 0)) return false;
      if (this.hbLocked(o, b)) { const ls = T.get(k + "lockedSound"); if (ls) this.sfxAt(ls, 1, P); return false; }
      st.last = now; if (T.get(k + "once")) st.used = true;
      const tag = T.get(k + "setTag"), raw = String(T.get(k + "setVal") === undefined ? "" : T.get(k + "setVal")).trim();
      if (tag && T.has(tag) && raw !== "") { const cur = T.get(tag), sp = T.spec(tag); if (/^toggle$/i.test(raw)) T.set(tag, sp && sp.type === "bool" ? !cur : !/^(1|true|on|yes)$/i.test(String(cur))); else if (/^[+-]\d+(\.\d+)?$/.test(raw) && typeof cur === "number") T.set(tag, cur + parseFloat(raw)); else if (this.sh) this.sh.setTag(tag, raw); }
      const snd = T.get(k + "sound"); if (snd) this.sfxAt(snd, 1, P); const an = T.get(k + "anim"); if (an && o.model && o.model.anims[an]) o.pressAnim = { name: an, t: 0 };
      const nm = String(T.get(k + "name") || ""); if (T.has("player.lastButton")) T.set("player.lastButton", nm); this.trigger("onButton", nm); const msg = String(T.get(k + "message") || "").trim(); if (msg) this.trigger("onMessage", msg);
      return true;
    }
    // left click: a cursor screen, or an "aim" hitbox under the crosshair, takes the click (the weapon does not fire)
    useClick() { if (this.ui) { if (this.ui.hover) this.hbPress(this.ui.hover.o, this.ui.hover.b); return true; } const hb = this.hbAim(["aim"]); if (hb) { this.hbPress(hb.o, hb.b); return true; } return false; }
    // the screen rectangle (canvas pixels) around all hitboxes of an object
    hbRect(o) { const v = this.view; if (!v || !v.vp) return null; const r = v.canvas.getBoundingClientRect(), m = v.vp; let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
      for (const b of this.hbOf(o)) { const bx = this.hbBox(o, b); for (let i = 0; i < 8; i++) { const p = this.hbToWorld(o, [bx.c[0] + (i & 1 ? 1 : -1) * bx.h[0], bx.c[1] + (i & 2 ? 1 : -1) * bx.h[1], bx.c[2] + (i & 4 ? 1 : -1) * bx.h[2]]), w = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15]; if (w <= 0.01) return null;
        const sx = ((m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]) / w * 0.5 + 0.5) * r.width, sy = (1 - ((m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]) / w * 0.5 + 0.5)) * r.height; x0 = Math.min(x0, sx); y0 = Math.min(y0, sy); x1 = Math.max(x1, sx); y1 = Math.max(y1, sy); } }
      return x0 < x1 ? { x0, y0, x1, y1 } : null; }
    uiStep() {
      if (this.mode !== "play" || !this.view) return; const T = this.T;
      if (this.ui) { const u = this.ui, o = u.o, d = Math.hypot(o.fr.cx - this.x, o.fr.cy - this.y); if (this.talk || this.pdead || !this.things.includes(o) || d > (+this.ov(o, "useRange") || 120) + 40) return this.uiEnd(false);
        const ray = this.view.rayAt(u.x, u.y); u.hover = this.hbRay(ray.o, ray.d, ["cursor"], o); this.curEl.style.left = u.x + "px"; this.curEl.style.top = u.y + "px"; this.curEl.classList.toggle("on", !!u.hover); return; }
      const hb = this.pdead || this.talk ? null : this.hbAim(["cursor"]); if (!hb) { this.uiBlock = null; return; } if (this.uiBlock === hb.o) return;
      const r = this.view.canvas.getBoundingClientRect(); this.ui = { o: hb.o, x: r.width / 2, y: r.height / 2, hover: hb };
      if (!this.curEl) { this.curEl = document.createElement("div"); this.curEl.className = "p3cur"; this.curEl.style.cssText = "position:absolute;width:0;height:0;pointer-events:none;z-index:7"; this.curEl.innerHTML = '<svg width="22" height="26" viewBox="0 0 22 26" style="position:absolute;left:-2px;top:-2px;filter:drop-shadow(0 1px 2px #000)"><path d="M2 2 L2 21 L7 16 L11 24 L14 22.5 L10 15 L17 15 Z" fill="#eaf6ff" stroke="#10151c" stroke-width="1.5"/></svg>'; this.root.append(this.curEl); }
      this.curEl.style.display = "block"; this.xh.style.visibility = "hidden";
    }
    uiEnd(block) { if (!this.ui) return; if (block) this.uiBlock = this.ui.o; this.ui = null; if (this.curEl) this.curEl.style.display = "none"; this.xh.style.visibility = ""; }
    uiMove(dx, dy) { const u = this.ui, r = this.view.canvas.getBoundingClientRect(), k = +this.T.get("player.mouseSens") || 1; u.x = Math.max(0, Math.min(r.width, u.x + dx * k)); u.y = Math.max(0, Math.min(r.height, u.y + dy * k));
      const R = this.hbRect(u.o), m = 40; if (!R || u.x < R.x0 - m || u.x > R.x1 + m || u.y < R.y0 - m || u.y > R.y1 + m || u.x <= 0 || u.y <= 0 || u.x >= r.width || u.y >= r.height) this.uiEnd(true); }
    // "Show the hitboxes while playing": their edges as thin lines (yellow = the one you point at)
    hbLines() { const out = [], T = this.T, hov = this.ui ? this.ui.hover : this.hbAim(null);
      for (const o of this.things || []) { if (o.type !== "interactive" || !this.ov(o, "showBoxes")) continue; for (const b of this.hbOf(o)) { const bx = this.hbBox(o, b), P = (i) => this.hbToWorld(o, [bx.c[0] + (i & 1 ? 1 : -1) * bx.h[0], bx.c[1] + (i & 2 ? 1 : -1) * bx.h[1], bx.c[2] + (i & 4 ? 1 : -1) * bx.h[2]]), c = hov && hov.b === b && hov.o === o ? [1, 0.85, 0.2] : [1, 0.3, 0.9];
        for (const [i, j] of [[0, 1], [2, 3], [4, 5], [6, 7], [0, 2], [1, 3], [4, 6], [5, 7], [0, 4], [1, 5], [2, 6], [3, 7]]) out.push({ kind: "line", a: P(i), b: P(j), c, w: 0.35, t: 0, life: 1e9 }); } }
      return out; }
    interact() {
      const v = this.view; if (!v) return; { const st = this.stationNear(); if (st) { this.saveAsk(st); return; } } { const n = this.npcInFront(); if (n) { this.talkStart(n); return; } } { const hb = this.ui ? this.ui.hover : this.hbAim(["key"]); if (hb) { this.hbPress(hb.o, hb.b); return; } } const c = v.cam, B = R3D.camBasis(c.yaw, c.pitch), O = [c.x, c.y, c.z], D = B.f;
      for (const o of this.things || []) { if (o.type !== "door" || this.ov(o, "openMode") !== "interact") continue; const t = this.thingHit(o, O, D, +this.ov(o, "interactRange") || 90); if (t === null) continue; if (o.st === "closed" || o.st === "closing") this.openDoor(o); else this.closeDoor(o); return; }
    }
    thingStep(dt) {
      if (this.talk) return; const T = this.T; for (const o of this.things || []) {
        if (o.taken) continue; if (o.type === "enemy") { this.enemyStep(o, dt); continue; } if (o.type === "spawner") { this.spawnerStep(o, dt); continue; } const g = k => +this.ov(o, k) || 0, f = o.fr, dist = Math.hypot(this.x - f.cx, this.y - f.cy);
        if (o.type === "exit") { if (!this.pdead && dist < (g("exitRadius") || 40) && Math.abs(this.z - o.floor) < 90) this.levelExit(o); continue; }
        if (o.type === "pickup" || o.type === "key") { if (dist < (g("pickRadius") || 28) && Math.abs(this.z - o.floor) < 70) { o.taken = true; this.remember("taken", o); const tg = o.type === "key" ? "" : this.ov(o, "giveTag"), amt = g("giveAmount"); if (o.type === "key") this.keyPick(o);
          if (tg === "player.health" && this.livesMode() === "reserve") this.giveHealth(amt);   // overflow fills empty energy tanks
          else if (tg === "player.tanksMax") { const own = Math.min(+T.get("player.tanksLimit") || 14, (+T.get(tg) || 0) + amt); T.setMany({ "player.tanksMax": own, "player.tanks": own, "player.health": +T.get("player.maxHealth") || 100 }); }   // a new energy tank refills everything
          else if (tg && T.get(tg) !== undefined) T.set(tg, (+T.get(tg) || 0) + amt);
          const s = this.ov(o, "pickSound"); if (s) this.sfx(s);
          if (this.ov(o, "popupOn")) { this.seenPop = this.seenPop || new Set(); const kind = o.th.prop; if (!(this.ov(o, "popupFirst") !== false && this.seenPop.has(kind))) { this.seenPop.add(kind); this.popupShow(o); } } } else o.spin += dt * 80; continue; }
        if (o.type === "checkpoint") { if (dist < (g("cpRadius") || 64) && Math.abs(this.z - o.floor) < 90 && !this.pdead && (!this.cp || this.cp.id !== o.th.id)) { this.cp = { id: o.th.id, map: this.map && this.map.id, x: this.x, y: this.y, z: this.z, yaw: this.yaw };
          if (this.ov(o, "cpHeal") !== false) T.set("player.health", +T.get("player.maxHealth") || 100); const s = this.ov(o, "cpSound"); if (s) this.sfxAt(s, 1, this.thingPos(o)); if (this.ov(o, "cpMessage")) this.toast(this.ov(o, "cpMessage")); } continue; }
        if (o.type !== "door") continue; const mode = this.ov(o, "openMode"), near = dist < (mode === "proximity" ? g("openRange") || 110 : 50);
        if (mode === "proximity" && near && (o.st === "closed" || o.st === "closing")) this.openDoor(o);
        const on = this.ov(o, "autoClose") && (mode === "proximity" || g("closeAfter") > 0);
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
        if (o.taken) continue; if (!o.gm && !o.sprite) continue; if (o.type === "speaker" && this.mode === "play" && !this.ov(o, "spkShow")) continue; if (o.type === "spawner" && this.mode === "play" && !this.ov(o, "spawnShow")) continue; if (o.sprite) { const it = this.spriteItem(o, v); if (it) items.push(it); continue; } const g = k => +this.ov(o, k) || 0; let f = o.fr, name = null, tm = 0;
        if (o.type === "enemy" && o.init) { const a = o.yaw * Math.PI / 180; f = { cx: o.x, cy: o.y, ux: Math.cos(a), uy: -Math.sin(a), nx: Math.sin(a), ny: Math.cos(a), door: false }; }
        const talking = o.type === "npc" && this.talk && this.talk.o === o; if (talking && this.ov(o, "faceTalker") !== false) { const a = Math.atan2(this.x - o.fr.cx, this.y - o.fr.cy); f = { cx: o.fr.cx, cy: o.fr.cy, ux: Math.cos(a), uy: -Math.sin(a), nx: Math.sin(a), ny: Math.cos(a), door: false }; }
        const an = k => { const nm = this.ov(o, k); return nm && o.model.anims[nm] ? nm : null; };
        if (o.type === "door") { const oa = an("openAnim"), ca = an("closeAnim"); if (oa) { const L = this.animLen(o.aid, oa, o.model.anims[oa].dur); if (o.st === "closing" && ca) { name = ca; tm = (1 - o.p) * this.animLen(o.aid, ca, o.model.anims[ca].dur).len * 1; } else { name = oa; tm = o.p * L.len; } tm *= L.speed; } else { name = an("idleAnim"); tm = 0; } }
        else if (o.type === "enemy" && o.init) { name = o.anim; if (name) { const L = this.animLen(o.aid, name, o.model.anims[name].dur), loop = o.akey === "idleAnim" || o.akey === "walkAnim"; tm = (loop ? o.at % Math.max(0.05, L.len) : Math.min(o.at, L.len - 1e-3)) * L.speed; } }
        else if (o.pressAnim && o.model.anims[o.pressAnim.name]) { const pa = o.pressAnim, L = this.animLen(o.aid, pa.name, o.model.anims[pa.name].dur); pa.t += dt; if (pa.t >= L.len) { o.pressAnim = null; name = an("idleAnim"); tm = 0; } else { name = pa.name; tm = pa.t * L.speed; } }
        else { name = (talking && an("talkAnim")) || an("idleAnim"); if (name) { const L = this.animLen(o.aid, name, o.model.anims[name].dur); tm = ((performance.now() / 1000) % Math.max(0.05, L.len)) * L.speed; } }
        const pose = this.mixPose(o, o.model, name, tm, dt), ang = o.type === "pickup" ? o.spin : 0;
        let z = [f.ux * 0 + 0, 0, 0]; const X = [f.ux, 0, f.uy], Z = [f.nx, 0, f.ny]; let cr = [X[1] * 0 - 0 * 0, 0, 0];
        const sgn = (-f.uy * f.nx + f.ux * f.ny) >= 0 ? 1 : -1, XX = f.door ? X.map(q => q * sgn) : X, ZZ = f.door ? Z.map(q => q * sgn) : Z;
        const slide = o.type === "door" && !an("openAnim") ? o.p * (f.h || 128) : 0;
        const Fm = o.type === "enemy" && o.surf ? this.crawlFrame(o) : [XX[0], XX[1], XX[2], 0, 0, 1, 0, 0, ZZ[0], ZZ[1], ZZ[2], 0, f.cx, o.floor + slide, f.cy, 1];
        const sc = g("scale") || 1; let loc = M4.mul(M4.translate(g("ox"), g("oy"), g("oz")), M4.mul(M4.rotXYZ([g("rx"), g("ry") + ang, g("rz")]), M4.scale(sc, sc, sc)));
        if (o.mount === "ceiling" && this.ov(o, "ceilFlip") !== false) loc = M4.mul(M4.translate(0, g("height") || 110, 0), M4.mul(M4.rotXYZ([0, 0, 180]), loc));   // hanging: turned over, its feet on the ceiling
        if (o.type === "door" && this.ov(o, "doorFit") !== false) loc = this.doorFit(o, f);
        if (o.type === "enemy") { o.lastM = M4.mul(Fm, loc); o.lastPose = pose; }
        items.push({ kind: "mesh", owner: o.th.id, gm: o.gm, pose, M: M4.mul(Fm, loc), clip: o.type === "door" ? o.floor + (f.h || 128) : undefined, p: [f.cx, o.floor + 40, f.cy], scale: 6, mat: Object.assign(this.matGL(o.pre), o.flash > 0 ? { tint: [1, 0.45, 0.45], emit: [0.5, 0.05, 0.05] } : o.shGlow ? { emit: o.shGlow } : o.st === "dead" ? { tint: [0.75, 0.7, 0.7] } : {}), tex: this.ov(o, "matTexture") ? v.texFor(this.ov(o, "matTexture"), this.urls) : null }); if (items[items.length - 1].clip !== undefined) items[items.length - 1].mat.clipY = items[items.length - 1].clip;
      }
      // objects that give off light: a point light above them (flicker = a torch), maybe casting shadows; their own model never shadows their own light
      for (const o of this.things) { if (o.taken || !this.ov(o, "lightOn")) continue; const g = k => +this.ov(o, k) || 0, x = o.type === "enemy" && o.init ? o.x : o.fr.cx, y = o.type === "enemy" && o.init ? o.y : o.fr.cy, fl = g("lightFlicker"), tm = performance.now() / 1000 + (o.th.id.length * 1.7), k = g("lightIntensity") * (1 - fl * (0.5 + 0.5 * Math.sin(tm * 17 + Math.sin(tm * 7.3) * 4)) * 0.8);
        (this.lightNow = this.lightNow || []).push({ p: [x, o.floor + g("lightHeight"), y], r: g("lightRange") || 420, c: this.hexCol(this.ov(o, "lightColor") || "#ffd9a0").map(q => q * k), type: 0, shadow: this.ov(o, "lightShadows") !== false, owner: o.th.id }); }
      for (const it of items) if (it.kind === "mesh") it.owner = it.owner || null; v.casters = items.filter(it => it.kind === "mesh");
      if (items.length) v.drawFx(items);
    }
    // a door model stretched to exactly fill its block: as wide as the cell, as tall as the door, at most 16 units thick (so it never sticks into the walls beside it)
    doorFit(o, f) {
      const T = this.T, g = k => +this.ov(o, k) || 0, b = o.model.bounds, R = M4.rotXYZ([g("rx"), g("ry"), g("rz")]), lo = [1e9, 1e9, 1e9], hi = [-1e9, -1e9, -1e9]; if (!b) return M4.ident();
      for (const x of [b.lo[0], b.hi[0]]) for (const y of [b.lo[1], b.hi[1]]) for (const z of [b.lo[2], b.hi[2]]) { const p = M4.point(R, x, y, z); for (let i = 0; i < 3; i++) { lo[i] = Math.min(lo[i], p[i]); hi[i] = Math.max(hi[i], p[i]); } }
      const W = Math.max(1e-3, hi[0] - lo[0]), H = Math.max(1e-3, hi[1] - lo[1]), D = Math.max(1e-3, hi[2] - lo[2]), sx = (f.w || 128) / W, sy = (f.h || 128) / H, sz = Math.min(16, D * Math.min(sx, sy)) / D;
      return M4.mul(M4.scale(sx, sy, sz), M4.mul(M4.translate(-(lo[0] + hi[0]) / 2, -lo[1], -(lo[2] + hi[2]) / 2), R));
    }
    // ---- enemies: idle until they see you, then chase, attack, get hurt, die
    eAnim(o, key) { if (o.sprite) { const sh = SHEETS[key]; return sh && this.ov(o, sh) ? key : null; } const nm = this.ov(o, key); if (!(o.model && nm && o.model.anims[nm])) return null; return nm; }
    eLen(o, key, fb) { const nm = this.eAnim(o, key); if (nm && o.sprite) return Math.max(0.05, (+this.ov(o, SHEETS[key] + "Frames") || 1) / (+this.ov(o, "spriteFps") || 8)); return nm ? Math.max(0.05, this.animLen(o.aid, nm, o.model.anims[nm].dur).len) : fb; }
    // a 2D sprite thing: the right strip for what it is doing, the frame for the time, standing on the floor and turned to face the camera
    spriteItem(o, v) {
      const T = this.T, key = o.type === "enemy" && o.init ? (o.anim || "idleAnim") : "idleAnim", sh = o.type === "npc" && this.talk && this.talk.o === o && this.ov(o, "talkSprite") ? "talkSprite" : SHEETS[key] || "spriteIdle", id = this.ov(o, sh) || this.ov(o, "spriteIdle"); if (!id) return null;
      const frames = Math.max(1, Math.round(+this.ov(o, (this.ov(o, sh) ? sh : "spriteIdle") + "Frames") || 1)), fps = +this.ov(o, "spriteFps") || 8, t = o.type === "enemy" && o.init ? o.at : performance.now() / 1000;
      const loop = key === "idleAnim" || key === "walkAnim", frame = loop ? Math.floor(t * fps) % frames : Math.min(frames - 1, Math.floor(t * fps)), dim = this.imgDim(id), hh = +this.ov(o, "spriteHeight") || 110, ww = hh * (dim ? dim[0] / frames / dim[1] : 1);
      const x = o.type === "enemy" && o.init ? o.x : o.fr.cx, y = o.type === "enemy" && o.init ? o.y : o.fr.cy, c = v.cam, n = M4.norm([c.x - x, 0, c.z - y]);
      return { kind: "decal", sprite: true, texId: id, p: [x, o.floor + hh / 2 + (o.type === "pickup" ? Math.sin(performance.now() / 400) * 4 : 0), y], n, w: ww, h: hh, frames, frame, tint: o.flash > 0 ? [1, 0.45, 0.45] : o.st === "dead" ? [0.8, 0.75, 0.75] : undefined, emit: o.flash > 0 ? [0.5, 0.05, 0.05] : o.shGlow || undefined, add: this.ov(o, "spriteBlend") === "glow" };
    }
    imgDim(id) { const c = this.dims = this.dims || {}; if (c[id] !== undefined) return c[id]; c[id] = null; const im = new Image(); im.onload = () => { c[id] = [im.naturalWidth, im.naturalHeight]; }; im.src = this.urls[id] || ""; return null; }
    eSet(o, key) { const nm = this.eAnim(o, key) || this.eAnim(o, "idleAnim"); if (o.anim !== nm || o.akey !== key) { o.anim = nm; o.akey = key; o.at = 0; } }
    eAlive(o) { return o.type === "enemy" && !o.taken && o.init && o.st !== "dying" && o.st !== "dead"; }
    eEye(o) { return [o.x, o.floor + (+this.ov(o, "height") || 110) * (o.mount === "ceiling" ? 0.15 : 0.8), o.y]; }
    pEye() { return [this.x, this.z + (+this.T.get("player.eyeHeight") || 102), this.y]; }
    canSee(o) {
      const v = this.view; if (!v) return false; const a = this.eEye(o), b = this.pEye(), d = M4.sub(b, a), L = M4.len(d); if (L < 1) return true; const D = d.map(q => q / L);
      const hit = v.raycast(a, D, L); if (hit && hit.t < L - 8) return false; const dh = this.hitDoor(a, D, L); return !(dh && dh.t < L - 8);
    }
    // a body pushed aside (by enemies / other enemies): moved in small steps with the walls checked after each, at most its radius per frame,
    // so a crowd can never shove it through a wall (the player used to be pushed into walls and fall out of the level)
    shove(b, dx, dy, world, dtr) { let L = Math.hypot(dx, dy); if (L < 1e-6) return; if (L > b.r) { dx *= b.r / L; dy *= b.r / L; L = b.r; } const n = Math.ceil(L / Math.max(1, b.r * 0.25));
      for (let i = 0; i < n; i++) { b.pos[0] += dx / n; b.pos[2] += dy / n; G.push(b, world, dtr); } }
    ePush(p, r, self) {   // keep enemies apart from each other
      for (const e of this.things || []) { if (e === self || !this.eAlive(e) || Math.abs(e.floor - self.floor) > 100) continue; const er = +this.T.get(e.pre + "radius") || 22, dx = p.x - e.x, dy = p.y - e.y, d = Math.hypot(dx, dy); if (d < r + er && d > 1e-4) { p.x += dx / d * (r + er - d) * 0.5; p.y += dy / d * (r + er - d) * 0.5; } }
    }
    // an enemy walks with the same body physics as the player: walls, steps, slopes and stairs, gravity
    eMove(o, vx, vy, dt) {
      const g = k => +this.ov(o, k) || 0, b = o.body || (o.body = { pos: [o.x, o.floor, o.y], vel: [0, 0, 0], r: 0, h: 0, step: 26, maxSlope: 0.69 }); b.r = Math.min(g("radius") || 22, 58); b.h = Math.min(Math.max(g("height") || 110, 2 * b.r + 4), G.LAYER - 4);   
      b.vel[0] = vx; b.vel[2] = vy; b.vel[1] -= (+this.T.get("player.gravity") || 1100) * dt; G.move(b, dt, this.view && this.view.world, this.dtris);
      const p = { x: b.pos[0], y: b.pos[2] }; this.ePush(p, b.r, o); this.shove(b, p.x - b.pos[0], p.y - b.pos[2], this.view && this.view.world, this.dtris); o.x = b.pos[0]; o.y = b.pos[2]; o.floor = b.pos[1];
    }
    enemyStep(o, dt) {
      const T = this.T, g = k => +this.ov(o, k) || 0;
      const beh = this.ov(o, "behavior") || "chase";
      if (!o.init) { o.init = true; o.x = o.fr.cx; o.y = o.fr.cy; o.yaw = o.th.ang || 0; o.hp = g("health") || 100; o.st = "idle"; o.cool = 0; o.losT = Math.random() * 0.2; o.at = 0; o.flash = 0; o.pi = 0; o.pdir = 1; o.pwait = 0; o.surf = null; this.eSet(o, "idleAnim"); if (o.th.wake) { o.awake = true; o.st = "chase"; }
        if (o.keep) { Object.assign(o, o.keep); o.keep = null; o.awake = true; o.st = "chase"; o.body = null; }   // it dropped and turned into this enemy: same place, already hunting
        o.mount = beh === "stationary" && this.ov(o, "mount") === "ceiling" && !o.dropped ? "ceiling" : null; if (o.mount) this.eHang(o); }
      o.at += dt; if (o.flash > 0) o.flash -= dt;
      if (o.st === "dead") return;
      if (o.st === "dying") { if (o.at >= this.eLen(o, "deathAnim", 0.6)) { o.st = "dead"; if (!this.ov(o, "corpse")) o.taken = true; } return; }
      if (o.st === "falling") { this.eMove(o, 0, 0, dt); if (o.body && o.body.ground && o.at > 0.05) this.eLand(o); return; }
      if (o.st === "fuse") { o.fuseT -= dt; o.flash = Math.floor(o.fuseT * 12) % 2 ? 0.06 : 0; if (o.fuseT <= 0) this.eBlast(o, true); return; }
      const r = g("radius") || 22, dx = this.x - o.x, dy = this.y - o.y, dist = Math.hypot(dx, dy) || 1;
      // walks a path: Patrol behaviour, or a Normal enemy that was given a path on the map (it patrols until it notices you)
      const pathOn = (beh === "path" || beh === "chase") && !!o.th.path && (this.map.paths || []).some(q => q.id === o.th.path && q.pts.length);
      if (o.boss) { this.bossStep(o, dt, dist, dx, dy); return; }
      if (this.ov(o, "detect") === "blind") { this.eBlind(o, dt, dist, pathOn ? "path" : beh); return; }
      o.losT -= dt; if (o.losT <= 0) { o.losT = 0.2; o.los = !this.pdead && (o.mount ? this.inCone(o) : dist < (g("sight") || 900) && this.canSee(o)); if (o.los && !o.awake && pathOn && !this.eFront(o, dx, dy, dist)) o.los = false; if (o.los && !o.awake) { o.awake = true; o.lostT = 0; const s = this.ov(o, "sightSound"); if (s) this.sfxAt(s, 1, this.thingPos(o)); } }
      if (o.st === "pain") { if (o.at >= this.eLen(o, "hurtAnim", 0.25)) { o.st = "chase"; this.eSet(o, "walkAnim"); } return; }
      // hanging from the ceiling: waits for you to step into its cone, then shoots, drops or blows up
      if (o.mount === "ceiling") { o.cool -= dt;
        if (o.st === "attack") { if (!o.hit && o.at >= g("attackDelay")) { o.hit = true; this.eAttack(o, dist, true); } if (o.at >= Math.max(this.eLen(o, "attackAnim", 0), g("attackDelay") + 0.25)) { o.st = "idle"; o.cool = g("attackRate") || 1.2; this.eSet(o, "idleAnim"); } return; }
        if (!o.los || this.pdead) { o.st = "idle"; this.eSet(o, "idleAnim"); return; }
        if (this.ov(o, "turn") !== false) o.yaw = Math.atan2(dx, dy) * 180 / Math.PI;
        const act = this.ov(o, "ceilAction") || "shoot", ds = this.ov(o, "dropSound");
        if (act === "shoot") { if (o.cool <= 0) { o.st = "attack"; o.hit = false; this.eSet(o, "attackAnim"); o.at = 0; const s = this.ov(o, "attackSound"); if (s) this.sfxAt(s, 1, this.thingPos(o)); } return; }
        if (ds) this.sfxAt(ds, 1, this.thingPos(o));
        if (act === "selfdestruct" && !this.ov(o, "sdFall")) { o.st = "fuse"; o.fuseT = g("fuse"); return; }   // stays up there while the fuse burns
        o.mount = null; o.dropped = true; o.st = "falling"; o.at = 0; o.body = null; this.eSet(o, "idleAnim"); return; }
      // a patroller that lost you for a while goes back to its path (the point nearest to it)
      if (pathOn && o.awake && this.ov(o, "pathChase") !== false && !this.pdead && o.st !== "attack") { o.lostT = o.los ? 0 : (o.lostT || 0) + dt;
        if (o.lostT > (+this.ov(o, "pathGiveUp") || 5)) { o.awake = false; o.lostT = 0; o.pwait = 0; o.pstuck = 0; const pa = this.map.paths.find(q => q.id === o.th.path); let bi = 0, bd = 1e18; pa.pts.forEach((q, i) => { const d = (q[0] - o.x) ** 2 + (q[1] - o.y) ** 2; if (d < bd) { bd = d; bi = i; } }); o.pi = bi; } }
      if (!o.awake || this.pdead) { if (pathOn && !this.pdead) this.ePatrol(o, dt); else { o.st = "idle"; this.eSet(o, "idleAnim"); } return; }
      if (pathOn && this.ov(o, "pathChase") === false && o.st !== "attack") {   // stays on its path; still attacks when it can
        o.cool -= dt; const rg = g("attackRange") || 70;
        if (dist <= rg + (this.ov(o, "attack") === "melee" ? r + 16 : 0) && o.los && o.cool <= 0) { o.yaw = Math.atan2(dx, dy) * 180 / Math.PI; o.st = "attack"; o.hit = false; this.eSet(o, "attackAnim"); o.at = 0; const s = this.ov(o, "attackSound"); if (s) this.sfxAt(s, 1, this.thingPos(o)); return; }
        this.ePatrol(o, dt); return; }
      if (beh !== "stationary" || this.ov(o, "turn") !== false) o.yaw = Math.atan2(dx, dy) * 180 / Math.PI;
      if (o.st === "attack") {
        if (!o.hit && o.at >= g("attackDelay")) { o.hit = true; this.eAttack(o, dist); }
        if (o.at >= Math.max(this.eLen(o, "attackAnim", 0), g("attackDelay") + 0.25)) { o.st = "chase"; o.cool = g("attackRate") || 1.2; this.eSet(o, "walkAnim"); } return;
      }
      if (beh === "stationary") { o.cool -= dt; const rg = g("attackRange") || 70; this.eMove(o, 0, 0, dt); o.st = "chase"; this.eSet(o, "idleAnim");   // a turret: never walks
        if (dist <= rg + (this.ov(o, "attack") === "melee" ? r + 16 : 0) && o.los && o.cool <= 0) { o.st = "attack"; o.hit = false; this.eSet(o, "attackAnim"); o.at = 0; const s = this.ov(o, "attackSound"); if (s) this.sfxAt(s, 1, this.thingPos(o)); } return; }
      o.cool -= dt; const range = g("attackRange") || 70;
      if (dist <= range + (this.ov(o, "attack") === "melee" ? r + 16 : 0) && o.los && o.cool <= 0 && !(o.surf && this.ov(o, "attack") === "melee")) { o.st = "attack"; o.hit = false; this.eSet(o, "attackAnim"); o.at = 0; const s = this.ov(o, "attackSound"); if (s) this.sfxAt(s, 1, this.thingPos(o)); return; }
      if (beh === "chase" && this.ov(o, "crawl") && this.eCrawl(o, dt, dist)) return;
      const want = this.ov(o, "attack") === "melee" ? r + (+T.get("player.radius") || 16) + 4 : Math.min(range * 0.7, 400);
      if (dist > want || !o.los) { const sp = g("speed") || 0; this.eMove(o, dx / dist * sp, dy / dist * sp, dt); o.st = "chase"; this.eSet(o, "walkAnim"); }
      else { this.eMove(o, 0, 0, dt); o.st = "chase"; this.eSet(o, "idleAnim"); }
    }
    eAttack(o, dist, ranged, dir) {
      const T = this.T, g = k => +this.ov(o, k) || 0, kind = ranged && this.ov(o, "attack") === "melee" ? "projectile" : this.ov(o, "attack"), dmg = g("damage"), col = this.hexCol(this.ov(o, "projColor") || "#ff6a2a");
      if (kind === "melee") { if (dist <= (g("attackRange") || 70) + (g("radius") || 22) + 24 && Math.abs(this.z - o.floor) < 90) this.hurtPlayer(dmg); return; }
      if (dir) { const a = this.eEye(o), hit = this.view && this.view.raycast(a, dir, 6000), far = hit ? hit.t : 6000;   // straight ahead, aimed at nothing
        if (kind === "hitscan") { const pc = [this.x, this.z + 50, this.y], rel = M4.sub(pc, a), t = M4.dot(rel, dir), off = M4.len(M4.sub(rel, dir.map(q => q * t))); (this.fx = this.fx || []).push({ kind: "line", a, b: M4.add(a, dir.map(q => q * far)), c: col, w: 0.6, life: 0.12, t: 0 }); if (t > 0 && t < far && off < (+T.get("player.radius") || 16) + 40) this.hurtPlayer(dmg); return; }
        (this.fx = this.fx || []).push({ kind: "sphere", hostile: true, dmg, p: a, d: dir.slice(), c: col, size: g("projSize") || 6, speed: g("projSpeed") || 600, life: 6, t: 0, light: { c: col, k: 1.2, r: 160 } }); return; }
      const a = this.eEye(o), b = this.pEye(); b[1] -= 14; const d = M4.sub(b, a), L = M4.len(d) || 1, D = d.map(q => q / L);
      if (kind === "hitscan") { const ok = this.canSee(o); (this.fx = this.fx || []).push({ kind: "line", a, b: ok ? b : M4.add(a, D.map(q => q * L * 0.9)), c: col, w: 0.6, life: 0.12, t: 0 }); if (ok) this.hurtPlayer(dmg); return; }
      (this.fx = this.fx || []).push({ kind: "sphere", hostile: true, dmg, p: a, d: D, c: col, size: g("projSize") || 6, speed: g("projSpeed") || 600, life: 6, t: 0, light: { c: col, k: 1.2, r: 160 } });
    }
    hurtPlayer(d) { if (this.pdead || !(d > 0) || this.popup) return; const T = this.T, hp = (+T.get("player.health") || 0) - d;
      if (hp <= 0 && d < 1e8 && this.livesMode() === "reserve" && (+T.get("player.tanks") || 0) > 0) { let h = hp, tk = +T.get("player.tanks") || 0; const mx = +T.get("player.maxHealth") || 100; while (h <= 0 && tk > 0) { tk--; h += mx; } T.setMany({ "player.health": Math.min(mx, h), "player.tanks": tk }); this.flash(0.5); this.psound("hurtSound"); this.toast("Energy tank used"); return; }
      T.set("player.health", Math.max(0, hp)); }
    hitEnemy(O, D, maxd) {   // ray against enemy bodies (upright cylinders)
      let best = null; for (const o of this.things || []) { if (!this.eAlive(o)) continue; const r = +this.ov(o, "radius") || 22, h = +this.ov(o, "height") || 110, fx = O[0] - o.x, fz = O[2] - o.y, a = D[0] * D[0] + D[2] * D[2], b = 2 * (fx * D[0] + fz * D[2]), c = fx * fx + fz * fz - r * r;
        let t; if (c <= 0) t = 0; else { if (a < 1e-9) continue; const disc = b * b - 4 * a * c; if (disc < 0) continue; t = (-b - Math.sqrt(disc)) / (2 * a); if (t < 0) continue; }
        if (t > maxd) continue; const y = O[1] + D[1] * t; if (y < o.floor || y > o.floor + h) continue; if (!best || t < best.t) best = { o, t }; }
      return best;
    }
    damageEnemy(o, d) {
      if (!this.eAlive(o)) return; const T = this.T;
      if (o.boss) { if (!o.fight) { o.hitOnce = true; return; } if (o.pause > 0 || (o.shield && o.bt < o.shield)) { o.flash = 0.03; return; } d *= Math.max(0, +this.ov(o, "armor")); if (!(d > 0)) return; }
      o.hp -= d; o.awake = true; o.flash = 0.12;
      if (o.hp <= 0 && o.boss) this.bossEnd(o);
      if (o.hp <= 0) { this.eDrop(o); this.trigger("onKill"); if (this.ov(o, "deathStyle") === "slice") { const s = this.ov(o, "deathSound"); if (s) this.sfxAt(s, 1, this.thingPos(o)); const kt = this.ov(o, "killTag"); if (kt && T.get(kt) !== undefined) T.set(kt, (+T.get(kt) || 0) + (+this.ov(o, "killAmount") || 0)); this.eSlice(o); return; }
        o.st = "dying"; this.eSet(o, "deathAnim"); o.at = 0; const s = this.ov(o, "deathSound"); if (s) this.sfxAt(s, 1, this.thingPos(o)); const kt = this.ov(o, "killTag"); if (kt && T.get(kt) !== undefined) T.set(kt, (+T.get(kt) || 0) + (+this.ov(o, "killAmount") || 0)); return; }
      if (o.boss) { this.bossTags(o); this.bossTrig(o, "onBossHurt"); this.bossHealthTrig(o); }
      const s = this.ov(o, "hurtSound"); if (s) this.sfxAt(s, 1, this.thingPos(o)); if (this.eAnim(o, "hurtAnim") && o.st !== "attack" && (!o.boss || (this.ov(o, "bossFlinch") && !o.dash))) { o.st = "pain"; this.eSet(o, "hurtAnim"); o.at = 0; }
    }
    // ---- per-object values: a boss reads its current phase's value for the keys a phase can change (Schema.PHASE_KEYS); boss nodes can override some for a while (o.tov)
    ov(o, k) { if (o.tov && o.tov[k] !== undefined) return o.tov[k]; if (o.tp) { const v = this.T.get(o.tp + k); if (v !== undefined) return v; } if (o.php) { const S = this.phk || (this.phk = new Set((PRIM.Schema && PRIM.Schema.PHASE_KEYS) || [])); if (S.has(k)) { const v = this.T.get(o.php + k); if (v !== undefined) return v; } } return this.T.get(o.pre + k); }
    // ---- the world remembers (per map): pick-ups / keys taken, doors unlocked, shot doors opened, shields broken, bosses beaten. Saved with the game.
    wmem() { const id = this.map && this.map.id; if (!id) return null; this.world = this.world || {}; return (this.world[id] = this.world[id] || { taken: [], unlocked: [], shot: [], shields: [], bosses: [] }); }
    remember(kind, o) { if (!o || !o.th || o.th.spawned) return; const w = this.wmem(); if (w && !(w[kind] = w[kind] || []).includes(o.th.id)) w[kind].push(o.th.id); }
    applyWorld() { const w = this.world && this.map && this.world[this.map.id]; if (!w) return; const has = (k, id) => (w[k] || []).includes(id);
      for (const o of this.things || []) { const id = o.th.id; if (has("taken", id) || has("bosses", id)) o.taken = true; if (has("unlocked", id)) o.unlocked = true; if (has("shot", id)) o.shotOk = true; if (has("shields", id)) o.shieldGone = true; } }
    // ---- saving: save stations (prop type "save") and the "Save the game" node write to the current slot (PRIM.Save, src/core/save.js)
    stationNear() { for (const o of this.things || []) { if (o.type !== "save" || o.taken) continue; if (Math.hypot(this.x - o.fr.cx, this.y - o.fr.cy) < (+this.ov(o, "saveRadius") || 96) && Math.abs(this.z - o.floor) < 90) return o; } return null; }
    saveAsk(o) { if (this.ov(o, "saveAsk") === false) { this.stationSave(o); return; }
      this.talk = { o, ask: true }; this.talkOpen(); this.talkBox("Save station", "Save the game?", [{ label: "Yes", fn: () => { this.talkEnd(); this.stationSave(o); } }, { label: "No", fn: () => this.talkEnd() }], null); }
    stationSave(o) { const T = this.T;
      if (this.ov(o, "saveHeal") !== false) { const set = { "player.health": +T.get("player.maxHealth") || 100 }; if (T.has("player.tanks")) set["player.tanks"] = +T.get("player.tanksMax") || 0; T.setMany(set); this.lastHp = undefined; }
      if (this.ov(o, "saveAmmo") !== false) { const set = {}; for (const a of this.weapons()) { const pre = "player." + a.id + "."; if (!T.get(pre + "ammoUse") || !T.has(pre + "ammo")) continue; const mx = +T.get(pre + "ammoMax") || +T.get(pre + "ammoStart") || 0, clip = +T.get(pre + "clipSize") || 0, now = clip > 0 ? Math.min(clip, mx) : mx; set[pre + "ammo"] = now; set[pre + "reserve"] = Math.max(0, mx - now); } if (Object.keys(set).length) T.setMany(set); }
      this.cp = { id: o.th.id, map: this.map && this.map.id, x: this.x, y: this.y, z: this.z, yaw: this.yaw };
      const ok = this.saveGame(o); const s = this.ov(o, "saveSound"); if (s) this.sfxAt(s, 1, this.thingPos(o)); this.toast(ok ? String(this.ov(o, "saveMsg") || "Game saved") : "Could not save");
      this.flash(0.35, "#7df3ff", true); }
    saveGame(st) {
      if (!PRIM.Save || this.mode !== "play" || !this.map) return false; const T = this.T, m = this.map, tags = {};
      if (T.ov) { for (const k in T.ov) if (!/^save\./.test(k)) tags[k] = T.ov[k]; } else for (const k of T.names()) { const r = T.spec(k); if (r && !/^save\./.test(k) && (r.custom || /^player\./.test(k) || /\.have$/.test(k))) tags[k] = T.get(k); }
      for (const k of ["player.bossActive", "player.aiming", "player.locked", "player.reloading", "player.charge"]) delete tags[k];
      const d = { v: 1, time: Date.now(), map: m.id, station: st ? st.th.id : null, name: (st && this.ov(st, "saveName")) || m.id, pos: { x: this.x, y: this.y, z: this.z, yaw: this.yaw }, tags, world: JSON.parse(JSON.stringify(this.world || {})) };
      const ok = PRIM.Save.write(this.project.id, this.slot, d); if (ok) { PRIM.Save.refreshTags(T, this.project.id); this.trigger("onSave"); } return ok; }
    loadGame(d) {
      if (!d || !this.view) return false; const T = this.T, m = this.project.maps.find(x => x.id === d.map) || this.map, set = {};
      for (const k in d.tags || {}) if (T.has(k)) set[k] = d.tags[k]; if (Object.keys(set).length) T.setMany(set);
      this.world = JSON.parse(JSON.stringify(d.world || {})); this.cp = null; this.pdead = false; this.deadT = 0; this.lastHp = undefined; this.say2(""); this.exiting = false;
      if (m && m !== this.map) { this.map = m; this.view.setMap(this.project, m, T, this.urls); }
      this.resetPlayer(); const p = d.pos; if (p) { this.x = p.x; this.y = p.y; this.z = p.z; this.yaw = p.yaw || 0; this.pitch = 0; this.body = null; this.vz = 0; this.ground = true; }
      this.keySnap = new Set(Object.keys(set).filter(k => /\.have$/.test(k) && set[k])); this.trigger("onLoad"); return true; }
    // ---- keys: key objects set their live tag <key>.have; doors check it (doorLocked)
    keyPick(o) { const T = this.T, id = o.pre.slice(0, -1); if (T.has(id + ".have")) T.set(id + ".have", true); if (!this.ov(o, "popupOn")) this.toast("Picked up the " + (this.ov(o, "keyName") || "key")); this.keyGot(id); }
    keyGot(id) { if (!this.sh) return; for (const ow of this.scriptOwners()) for (const n of ow.script.nodes) { if (n.type !== "onKeyGot") continue; const f = this.T.get(ow.key + ".s." + n.id + ".key"); if (!f || f === id) this.sh.trigger(ow, "onKeyGot", null, n.id); } }
    // "all" = no keys (game start), "level" = keep only keys marked keyKeep (next level), "snap" = back to the keys you had when the level began (restart)
    keysReset(mode) { const T = this.T, names = (this.project.props || []).filter(q => T.get(q.id + ".type") === "key").map(q => q.id + ".have").filter(n => T.has(n)), o = {};
      for (const n of names) o[n] = mode === "snap" ? !!(this.keySnap && this.keySnap.has(n)) : mode === "level" ? !!T.get(n) && !!T.get(n.slice(0, -5) + ".keyKeep") : false;
      if (names.length) T.setMany(o); if (mode !== "snap") this.keySnap = new Set(names.filter(n => o[n])); }
    // ---- bosses: an enemy with a fight (health bar, intro, music), phases (prop.phases, tags <prop>.ph.<id>.*) and its own script (prop.script, owner key = the prop id)
    bossProp(o) { const id = o.pre.slice(0, -1); return (this.project.props || []).find(q => q.id === id) || null; }
    bossOwner(o) { const p = this.bossProp(o); return p && p.script ? { key: p.id, script: p.script } : null; }
    bossOwners() { const seen = new Set(), out = []; for (const o of this.things || []) if (o.boss && o.fight && this.eAlive(o) && !seen.has(o.pre)) { seen.add(o.pre); const ow = this.bossOwner(o); if (ow) out.push(ow); } return out; }
    bossTrig(o, type, filter) { const ow = this.bossOwner(o); if (!this.sh || !ow) return; for (const n of ow.script.nodes) if (n.type === type && (!filter || filter(k => this.T.get(ow.key + ".s." + n.id + "." + k), n))) this.sh.trigger(ow, type, null, n.id); }
    bossOf(key) { const L = (this.things || []).filter(o => o.boss && o.pre === key + "." && this.eAlive(o)); return L.find(o => o.fight) || L[0] || null; }
    phases(o) { const p = this.bossProp(o), T = this.T, at = ph => +T.get(o.pre + "ph." + ph.id + ".startAt"); return ((p && p.phases) || []).slice().sort((a, b) => at(b) - at(a)); }
    bossPct(o) { return Math.max(0, o.hp) / (+this.T.get(o.pre + "health") || 100) * 100; }
    bossTags(o) { const T = this.T, mx = +T.get(o.pre + "health") || 100; this.bossMarks = this.phases(o).map(ph => +T.get(o.pre + "ph." + ph.id + ".startAt") / 100).filter(v => v > 0 && v < 1); if (this.hud) this.hud.bossMarks = this.bossMarks;
      if (T.get(o.pre + "bossBar") === false) { if (T.get("player.bossActive")) T.set("player.bossActive", false); return; }
      T.setMany({ "player.bossActive": !!o.fight && this.eAlive(o), "player.bossHealth": Math.max(0, Math.round(o.hp)), "player.bossMax": mx, "player.bossName": String(T.get(o.pre + "bossName") || T.get(o.pre + "name") || ""), "player.bossPhase": o.phName || "" }); }
    bossMusic(id, v) { if (!id || this.bossMu === id) return; if (this.bossMu) this.stopLoops(this.bossMu); this.bossMu = id; this.loopSfx(id, isFinite(v) ? v : 0.8); }
    bossStop() { if (this.bossMu) { this.stopLoops(this.bossMu); this.bossMu = null; } if (this.mode === "play" && this.T.get("player.bossActive")) this.T.set("player.bossActive", false); }
    bossBegin(o) {
      const T = this.T; Object.assign(o, { fight: true, awake: true, bt: 0, ph: null, php: null, phName: "", phLock: -1, tov: {}, tovT: {}, vols: [], hpFired: new Set(), shield: 0, hold: 0, dash: null, cool: 0.4 }); o.home = o.home || [o.x, o.y];
      o.pause = +T.get(o.pre + "bossIntroTime") || 0; const s = T.get(o.pre + "bossIntroSound"); if (s) this.sfxAt(s, 1, this.thingPos(o)); const ms = T.get(o.pre + "bossIntroMsg"); if (ms) this.toast(T.interpolate(String(ms)));
      this.bossMusic(T.get(o.pre + "bossMusic"), +T.get(o.pre + "bossMusicVol")); this.bossPhaseCheck(o, true); this.bossTags(o); this.bossTrig(o, "onBossStart");
    }
    // the phase follows the health: the deepest phase whose "starts at" the health has reached (never back up by itself)
    bossPhaseCheck(o, quiet) { const P = this.phases(o); if (!P.length) return; const pct = this.bossPct(o); let j = -1; P.forEach((ph, i) => { if (pct <= +this.T.get(o.pre + "ph." + ph.id + ".startAt") + 1e-6) j = i; });
      const cur = o.ph ? P.findIndex(q => q.id === o.ph) : -1; if (j > Math.max(cur, o.phLock === undefined ? -1 : o.phLock)) { o.phLock = j; this.bossEnter(o, P[j], quiet); } }
    bossEnter(o, ph, quiet) {
      const T = this.T, k = o.pre + "ph." + ph.id + ".", g = q => T.get(k + q), mx = +T.get(o.pre + "health") || 100; o.ph = ph.id; o.php = k; o.phName = String(g("name") || ""); o.vols = []; this.bossDashEnd(o);
      if (!quiet) { o.pause = Math.max(o.pause || 0, +g("enterTime") || 0); o.st = "chase"; const an = g("enterAnim"); if (an && o.model && o.model.anims[an]) { o.anim = an; o.akey = "enterAnim"; o.at = 0; o.animLock = this.animLen(o.aid, an, o.model.anims[an].dur).len; }
        const s = g("enterSound"); if (s) this.sfxAt(s, 1, this.thingPos(o)); if (g("enterMsg")) this.toast(T.interpolate(String(g("enterMsg")))); if (g("enterFlash") !== false) this.flash(0.6, g("enterColor") || "#ffffff", true);
        const sh = +g("enterShake") || 0; if (sh > 0) { this.shakeA = Math.max(this.shakeA || 0, sh); this.shakeT = 0.6; this.shakeT0 = 0.6; } }
      const heal = +g("enterHeal") || 0; if (heal) o.hp = Math.min(mx, o.hp + heal / 100 * mx);
      const sp = g("enterSpawn"), sn = Math.round(+g("enterSpawnCount") || 0); if (sp && sn > 0) this.bossSpawn(o, sp, sn, +g("enterSpawnRadius") || 300);
      this.applyVal(g("enterTag"), g("enterVal")); const msg = String(g("enterMessage") || "").trim(); if (msg) this.trigger("onMessage", msg);
      if (g("music")) this.bossMusic(g("music"), +T.get(o.pre + "bossMusicVol"));
      this.bossTags(o); this.bossTrig(o, "onPhase", q => { const f = String(q("phase") || "").trim().toLowerCase(); return !f || f === o.phName.trim().toLowerCase(); });
    }
    bossHealthTrig(o) { const pct = this.bossPct(o); this.bossTrig(o, "onBossHealth", (q, n) => { if (o.hpFired.has(n.id) || pct > +q("percent")) return false; o.hpFired.add(n.id); return true; }); }
    bossEnd(o) {
      const T = this.T, ow = this.bossOwner(o); if (this.sh && ow) this.sh.stopOwner(ow.key); for (const v of o.vols || []) if (v.done) v.done(); o.vols = []; this.bossDashEnd(o); o.shGlow = null; o.tov = {};
      if (this.bossMu) { this.stopLoops(this.bossMu); this.bossMu = null; } T.setMany({ "player.bossActive": false, "player.bossHealth": 0 });
      this.applyVal(T.get(o.pre + "bossWinTag"), T.get(o.pre + "bossWinVal")); const ms = T.get(o.pre + "bossWinMsg"); if (ms) this.toast(T.interpolate(String(ms)));
      this.bossTrig(o, "onBossDeath"); o.fight = false; this.remember("bosses", o);
    }
    bossSpawn(o, prop, n, R) {
      if (!(this.project.props || []).some(q => q.id === prop)) return; const L = G.layerOf(o.floor + 4), col = this.hexCol("#c07dff");
      for (let i = 0; i < n; i++) { let X = o.x, Z = o.y; for (let t = 0; t < 12; t++) { const a = Math.random() * Math.PI * 2, rr = (0.35 + 0.65 * Math.sqrt(Math.random())) * R, x = o.x + Math.cos(a) * rr, z = o.y + Math.sin(a) * rr; if (this.floorAt(L, x, z)) { X = x; Z = z; break; } }
        this.spawnN = (this.spawnN || 0) + 1; (this.spawnTh = this.spawnTh || []).push({ id: o.th.id + "~" + this.spawnN, prop, x: X, y: Z, ang: 0, layer: L, spawned: true, wake: true }); this.burst([X, o.floor + 40, Z], col, 18, 200, 0.5, 2.5); }
      this.syncThings();
    }
    // "When killed, it drops…": each row rolls its chance; things appear around the body (runtime things, like a spawner's). Not called when an enemy blows itself up.
    eDrop(o) { const p = this.bossProp(o); if (!p || !p.drops || !p.drops.length) return; const T = this.T, L = G.layerOf(o.floor + 4), R = +this.ov(o, "dropSpread") || 0; let made = 0;
      for (const d of p.drops) { const k = p.id + ".dr." + d.id + ".", what = T.get(k + "what"); if (!what || !(this.project.props || []).some(q => q.id === what) || Math.random() * 100 >= (+T.get(k + "chance") || 0)) continue;
        for (let i = 0; i < Math.max(1, Math.round(+T.get(k + "count") || 1)); i++) { let X = o.x, Z = o.y; for (let t = 0; t < 8 && R > 0; t++) { const a = Math.random() * Math.PI * 2, rr = Math.sqrt(Math.random()) * R, x = o.x + Math.cos(a) * rr, z = o.y + Math.sin(a) * rr; if (this.floorAt(L, x, z)) { X = x; Z = z; break; } }
          this.spawnN = (this.spawnN || 0) + 1; (this.spawnTh = this.spawnTh || []).push({ id: o.th.id + "~d" + this.spawnN, prop: what, x: X, y: Z, ang: 0, layer: L, spawned: true, wake: true }); made++; } }
      if (made) this.syncThings(); }
    floorAt(L, x, z) { const c = G.cell(this.map, L, Math.floor(x / 128), Math.floor(z / 128)); return !!c && !G.isWall(c) && !c.dg && (c.f !== undefined || !!c.sh); }
    bossTeleport(o, where, R, fx, color) {
      const L = G.layerOf(o.floor + 4), home = o.home || [o.x, o.y]; let P = null;
      for (let i = 0; i < 16 && !P; i++) { let x, z; const j = i ? (Math.random() - 0.5) * 1.2 : 0, rr = R || 400;
        if (where === "start") { x = home[0]; z = home[1]; } else if (where === "behind") { const a = this.yaw + Math.PI + j; x = this.x + Math.cos(a) * rr; z = this.y + Math.sin(a) * rr; }
        else if (where === "far") { const a = Math.atan2(this.y - o.y, this.x - o.x) + j; x = this.x + Math.cos(a) * rr; z = this.y + Math.sin(a) * rr; }
        else { const a = Math.random() * Math.PI * 2, q = Math.sqrt(Math.random()) * rr; x = home[0] + Math.cos(a) * q; z = home[1] + Math.sin(a) * q; }
        if (this.floorAt(L, x, z) || where === "start") P = [x, z]; }
      if (!P) return; const col = this.hexCol(color || "#c07dff"); if (fx) this.burst([o.x, o.floor + 50, o.y], col, 20, 240, 0.45, 2.5);
      o.x = P[0]; o.y = P[1]; o.body = null; const gy = G.groundY(this.map, o.x, o.y, L * G.LAYER + G.LAYER - 8); if (gy !== null && gy !== undefined) o.floor = gy; this.bossDashEnd(o);
      if (fx) { this.burst([o.x, o.floor + 50, o.y], col, 26, 260, 0.5, 2.5); this.fx.push({ kind: "sphere", deco: true, p: [o.x, o.floor + 50, o.y], d: [0, 1, 0], c: [0, 0, 0], size0: 0.01, size: 0.01, speed: 0, life: 0.3, t: 0, light: { c: col, k: 2.5, k0: 2.5, r: 300, shadow: false } }); }
    }
    bossDashStart(o, speed, time, dmg, done) { const dx = this.x - o.x, dy = this.y - o.y, d = Math.hypot(dx, dy) || 1; this.bossDashEnd(o); o.dash = { ux: dx / d, uy: dy / d, speed, t: time, dmg, hit: false, done }; o.yaw = Math.atan2(dx, dy) * 180 / Math.PI; this.eSet(o, "walkAnim"); }
    bossDashStep(o, dt, dist) { const q = o.dash, bx = o.x, by = o.y; q.t -= dt; this.eMove(o, q.ux * q.speed, q.uy * q.speed, dt); const r = +this.ov(o, "radius") || 22;
      if (!q.hit && dist < r + (+this.T.get("player.radius") || 16) + 14 && Math.abs(this.z - o.floor) < 100) { q.hit = true; this.hurtPlayer(q.dmg); this.extV = [q.ux * 520, q.uy * 520]; }
      if (q.t <= 0 || (q.t < q.t0 - 0.1 && Math.hypot(o.x - bx, o.y - by) < q.speed * dt * 0.25)) this.bossDashEnd(o); if (q.t0 === undefined) q.t0 = q.t + dt; }
    bossDashEnd(o) { const q = o.dash; o.dash = null; if (q && q.done) q.done(); }
    // a volley: the boss's attack (pattern settings) or a "Fire a pattern" node (g = the node's values)
    bossVolleyOf(o, n) { const g = k => this.ov(o, k), hs = g("attack") === "hitscan";
      return { atk: !n, pattern: (n ? n("pattern") : g("pattern")) || "aimed", count: Math.max(1, Math.round(+(n ? n("count") : g("patCount")) || 1)), spread: +(n ? n("spread") : g("patSpread")) || 40, left: Math.max(1, Math.round(+(n ? n("volleys") : g("volleys")) || 1)),
        gap: Math.max(0.02, +(n ? n("gap") : g("volleyGap")) || 0.25), turn: +(n ? n("turn") : g("patTurn")) || 0, t: 0, kind: hs ? "hitscan" : "projectile",
        speed: (n && +n("speed")) || +g("projSpeed") || 600, size: (n && +n("size")) || +g("projSize") || 6, dmg: (n && +n("damage")) || +g("damage") || 0, col: this.hexCol((n && n("own") === false ? n("color") : g("projColor")) || "#ff6a2a") }; }
    bossVolley(o, v) {
      const a = this.eEye(o), b = this.pEye(); b[1] -= 14; const d0 = M4.sub(b, a), L0 = M4.len(d0) || 1, D = d0.map(q => q / L0), n = v.count, h = Math.atan2(D[2], D[0]), dirs = [];
      const at = (yaw, py) => { const c = Math.sqrt(Math.max(0, 1 - py * py)); return [Math.cos(yaw) * c, py, Math.sin(yaw) * c]; };
      if (v.pattern === "rain") { const R = v.spread > 5 ? v.spread : 200, L = G.layerOf(this.z + 4);
        for (let i = 0; i < n; i++) { const an = Math.random() * Math.PI * 2, rr = Math.sqrt(Math.random()) * R, x = this.x + Math.cos(an) * rr, z = this.y + Math.sin(an) * rr; if (!this.floorAt(L, x, z)) continue; const base = [x, this.z + 40, z], up = this.view && this.view.raycast(base, [0, 1, 0], 600), top = up ? Math.max(20, up.t - 10) : 360;
          this.bossShot([x, this.z + 40 + top, z], [0, -1, 0], Object.assign({}, v, { kind: "projectile" })); } return; }
      if (v.pattern === "aimed") dirs.push(D);
      else if (v.pattern === "spread") { const w = v.spread * Math.PI / 180; for (let i = 0; i < n; i++) dirs.push(at(h + (n > 1 ? (i / (n - 1) - 0.5) * w : 0), D[1])); }
      else { if (v.ang === undefined) v.ang = h; for (let i = 0; i < n; i++) dirs.push(at(v.ang + i / n * Math.PI * 2, D[1] * 0.5)); if (v.pattern === "spiral") v.ang += v.turn * Math.PI / 180; }
      dirs.forEach((dir, i) => this.bossShot(a, dir, v, i));
    }
    bossShot(p, dir, v, i) {
      if (v.kind === "hitscan") { const hit = this.view && this.view.raycast(p, dir, 6000), far = hit ? hit.t : 6000, pc = [this.x, this.z + 50, this.y], rel = M4.sub(pc, p), t = M4.dot(rel, dir), off = M4.len(M4.sub(rel, dir.map(q => q * t)));
        (this.fx = this.fx || []).push({ kind: "line", a: p, b: M4.add(p, dir.map(q => q * far)), c: v.col, w: 0.6, life: 0.12, t: 0 }); if (t > 0 && t < far && off < (+this.T.get("player.radius") || 16) + 30) this.hurtPlayer(v.dmg); return; }
      (this.fx = this.fx || []).push({ kind: "sphere", hostile: true, dmg: v.dmg, p: p.slice(), d: dir.slice(), c: v.col, size: v.size, speed: v.speed, life: 6, t: 0, light: (i || 0) < 3 ? { c: v.col, k: 1.2, r: 160 } : undefined });
    }
    bossAttack(o, dist) { if (this.ov(o, "attack") === "melee") this.eAttack(o, dist); else o.vols.push(this.bossVolleyOf(o, null)); }
    bossMove(o, dt, dist, dx, dy, set) {
      const mv = this.ov(o, "bossMove") || "chase", sp = +this.ov(o, "speed") || 0, ux = dx / dist, uy = dy / dist, kd = +this.ov(o, "keepDist") || 500, r = +this.ov(o, "radius") || 22, melee = this.ov(o, "attack") === "melee",
        want = melee ? r + (+this.T.get("player.radius") || 16) + 4 : Math.min((+this.ov(o, "attackRange") || 70) * 0.7, 400); let vx = 0, vy = 0;
      if (mv === "chase") { if (dist > want || !o.los) { vx = ux * sp; vy = uy * sp; } }
      else if (mv === "keep") { if (dist > kd + 60 || !o.los) { vx = ux * sp; vy = uy * sp; } else if (dist < kd - 60) { vx = -ux * sp; vy = -uy * sp; } }
      else if (mv === "flee") { vx = -ux * sp; vy = -uy * sp; }
      else if (mv === "circle") { o.cdir = o.cdir || 1; const rad = Math.max(-1, Math.min(1, (dist - kd) / 200)), tg = 1 - Math.abs(rad) * 0.5; vx = (ux * rad - uy * o.cdir * tg) * sp; vy = (uy * rad + ux * o.cdir * tg) * sp; }
      else if (mv === "dash") { o.dashT = (o.dashT === undefined ? +this.ov(o, "dashEvery") || 3 : o.dashT) - dt; if (o.dashT <= 0 && o.los) { o.dashT = +this.ov(o, "dashEvery") || 3; this.bossDashStart(o, +this.ov(o, "dashSpeed") || 900, +this.ov(o, "dashTime") || 0.6, +this.ov(o, "damage") || 0); return; } if (dist > want * 1.5) { vx = ux * sp * 0.6; vy = uy * sp * 0.6; } }
      const bx = o.x, by = o.y, moving = !!(vx || vy); this.eMove(o, vx, vy, dt);
      if (mv === "circle" && moving && Math.hypot(o.x - bx, o.y - by) < sp * dt * 0.3) { o.cblk = (o.cblk || 0) + dt; if (o.cblk > 0.25) { o.cdir = -o.cdir; o.cblk = 0; } } else o.cblk = 0;
      o.st = "chase"; set(moving ? "walkAnim" : "idleAnim");
    }
    bossStep(o, dt, dist, dx, dy) {
      const T = this.T, g = k => +this.ov(o, k) || 0, r = g("radius") || 22;
      o.losT -= dt; if (o.losT <= 0) { o.losT = 0.2; o.los = !this.pdead && dist < (g("sight") || 900) && this.canSee(o); }
      if (o.animLock > 0) o.animLock -= dt; const set = key => { if (!(o.animLock > 0)) this.eSet(o, key); };
      if (!o.fight) { this.eMove(o, 0, 0, dt); o.st = "idle"; set("idleAnim"); const st = T.get(o.pre + "bossStart"), tg = T.get(o.pre + "bossStartTag");
        const go = st === "tag" ? !!(this.sh && tg && this.sh.test(tg, T.get(o.pre + "bossStartOp") || "=", T.get(o.pre + "bossStartVal"))) : o.los || o.hitOnce; if (go && !this.pdead) this.bossBegin(o); return; }
      o.bt += dt; for (const k in o.tovT) if (o.bt >= o.tovT[k]) { delete o.tov[k]; delete o.tovT[k]; }
      const shOn = o.shield && o.bt < o.shield; if (!shOn) o.shield = 0; if (shOn) { const c = o.shCol || [0.5, 0.95, 1], k = 0.55 + 0.35 * Math.sin(o.bt * 9); o.shGlow = c.map(q => q * k); } else o.shGlow = null;
      this.bossPhaseCheck(o, false);
      for (const v of o.vols) { v.t -= dt; if (v.t <= 0 && v.left > 0) { this.bossVolley(o, v); v.left--; v.t = v.gap; } } o.vols = o.vols.filter(v => { if (v.left > 0) return true; if (v.done) v.done(); return false; });
      if (this.pdead) { this.eMove(o, 0, 0, dt); o.st = "idle"; set("idleAnim"); return; }
      if (o.pause > 0) { o.pause -= dt; this.eMove(o, 0, 0, dt); o.yaw = Math.atan2(dx, dy) * 180 / Math.PI; set("idleAnim"); return; }
      if (o.st === "pain") { if (o.at < this.eLen(o, "hurtAnim", 0.25)) { this.eMove(o, 0, 0, dt); return; } o.st = "chase"; }
      if (o.dash) { this.bossDashStep(o, dt, dist); return; }
      o.yaw = Math.atan2(dx, dy) * 180 / Math.PI;
      if (o.st === "attack") { if (!o.hit && o.at >= g("attackDelay")) { o.hit = true; this.bossAttack(o, dist); } if (o.at >= Math.max(this.eLen(o, "attackAnim", 0), g("attackDelay") + 0.25)) { o.st = "chase"; o.cool = g("attackRate") || 1.2; } this.eMove(o, 0, 0, dt); return; }
      o.cool -= dt; const melee = this.ov(o, "attack") === "melee", range = g("attackRange") || 70;
      if (!(o.hold > o.bt) && o.cool <= 0 && o.los && dist <= range + (melee ? r + 16 : 0) && !o.vols.some(v => v.atk)) { o.st = "attack"; o.hit = false; this.eSet(o, "attackAnim"); o.at = 0; const s = this.ov(o, "attackSound"); if (s) this.sfxAt(s, 1, this.thingPos(o)); this.bossTrig(o, "onBossAttack"); return; }
      this.bossMove(o, dt, dist, dx, dy, set);
    }
    // keys + boss nodes (script.js hands every node it does not know to o.custom)
    async custom(owner, n, g, tok) {
      const T = this.T, ty = n.type, until = fn => new Promise(res => { fn(res); tok.cancel = res; });
      if (ty === "giveKey" || ty === "takeKey") { const k = g("key"); if (k && T.has(k + ".have")) { T.set(k + ".have", ty === "giveKey"); if (ty === "giveKey") this.keyGot(k); } return; }
      if (ty === "saveGame") { this.saveGame(null); return; }
      if (ty === "ifHasKey") { const k = g("key"); return k && T.get(k + ".have") ? "then" : "else"; }
      const o = this.bossOf(owner.key); if (!o || !o.fight) return /^if/.test(ty) ? "else" : undefined;
      switch (ty) {
        case "setPhase": { const P = this.phases(o), nm = String(g("phase") || "").trim().toLowerCase(), cur = P.findIndex(q => q.id === o.ph), ph = nm ? P.find(q => String(T.get(o.pre + "ph." + q.id + ".name") || "").trim().toLowerCase() === nm) : P[cur + 1];
          if (ph) { const pct = this.bossPct(o); let j = -1; P.forEach((q, i) => { if (pct <= +T.get(o.pre + "ph." + q.id + ".startAt")) j = i; }); o.phLock = Math.max(j, P.indexOf(ph)); this.bossEnter(o, ph, false); } return; }
        case "bossFire": { const v = this.bossVolleyOf(o, g); o.vols.push(v); if (g("wait") !== false) await until(res => { v.done = res; }); return; }
        case "bossMove": { const sec = +g("seconds") || 0, u = sec > 0 ? o.bt + sec : Infinity; o.tov.bossMove = g("move"); o.tovT.bossMove = u; if (+g("speed") > 0) { o.tov.speed = +g("speed"); o.tovT.speed = u; } else { delete o.tov.speed; delete o.tovT.speed; } return; }
        case "bossDash": { let fin; const pr = until(res => { fin = res; }); this.bossDashStart(o, +g("speed") || 1000, +g("seconds") || 0.6, +g("damage") || 0, fin); if (g("wait") !== false) await pr; return; }
        case "bossTeleport": this.bossTeleport(o, g("where"), +g("radius") || 0, g("fx") !== false, g("color")); return;
        case "bossSpawn": if (g("what")) this.bossSpawn(o, g("what"), Math.max(1, Math.round(+g("count") || 1)), +g("radius") || 0); return;
        case "bossHeal": { const mx = +T.get(o.pre + "health") || 100; o.hp = Math.max(1, Math.min(mx, o.hp + (+g("percent") || 0) / 100 * mx)); this.bossTags(o); return; }
        case "bossShield": o.shield = g("on") === "off" ? 0 : (+g("seconds") > 0 ? o.bt + +g("seconds") : Infinity); o.shCol = this.hexCol(g("color") || "#7df3ff"); return;
        case "bossHold": o.hold = o.bt + (+g("seconds") || 0); return;
        case "bossAnim": { const an = g("anim"); if (!an || !o.model || !o.model.anims[an]) return; const len = this.animLen(o.aid, an, o.model.anims[an].dur).len; o.anim = an; o.akey = "nodeAnim"; o.at = 0; o.animLock = len;
          if (g("wait") !== false) await until(res => { setTimeout(res, len * 1000); }); return; }
        case "ifPhase": return String(o.phName || "").trim().toLowerCase() === String(g("phase") || "").trim().toLowerCase() ? "then" : "else";
        case "ifBossHealth": { const a = this.bossPct(o), b = +g("percent") || 0, r = { "=": Math.abs(a - b) < 0.5, "!=": Math.abs(a - b) >= 0.5, ">": a > b, "<": a < b, ">=": a >= b, "<=": a <= b }[g("op") || "<="]; return r ? "then" : "else"; }
        case "ifPlayerDist": { const d = Math.hypot(this.x - o.x, this.y - o.y); return (g("op") === ">" ? d > +g("dist") : d < +g("dist")) ? "then" : "else"; }
      }
    }
    // what the minimap needs, every frame
    hudWorld() { const T = this.T, items = [], doors = [];
      for (const o of this.things || []) { if (o.taken) continue; const ty = o.boss ? "boss" : o.type;
        if (ty === "door") { const k = this.ov(o, "lockKey"); doors.push(this.hud.doorSeg(o.fr, k && !o.unlocked ? T.get(k + ".keyColor") : o.shA > 0.1 && o.shCol ? "rgb(" + o.shCol.map(q => Math.round(Math.min(1, q) * 255)).join(",") + ")" : null, o.st === "open")); continue; }
        if (ty === "enemy" || ty === "boss") { if (this.eAlive(o)) items.push({ id: o.th.id, x: o.x, y: o.y, k: ty, L: G.layerOf(o.floor + 4) }); continue; }
        if (ty === "key" || ty === "pickup" || ty === "exit" || ty === "npc" || ty === "save") items.push({ id: o.th.id, x: o.fr.cx, y: o.fr.cy, k: ty === "pickup" ? "item" : ty, c: ty === "key" ? this.ov(o, "keyColor") : null, L: G.layerOf((o.floor || 0) + 4) }); }
      return { map: this.map, L: G.layerOf(this.z + 4), x: this.x, y: this.y, yaw: this.yaw, t: this.clock || 0, items, doors }; }
    // ---- aiming (weapon tag aimType): "lock" = lock onto the enemy nearest the crosshair, "sights" = look down the sights with a zoom
    aimW() { const at = this.curWeapon && this.curWeapon(); if (!at) return null; const pre = "player." + at.id + ".", type = this.T.get(pre + "aimType"); return type === "lock" || type === "sights" ? { att: at, pre, type } : null; }
    aimInput(id, down) {
      const w = this.aimW(); if (!w || this.mode !== "play" || id !== String(this.T.get(w.pre + "aimKey") || "mouse2")) return;
      if (this.T.get(w.pre + "aimHold") === "toggle") { if (down) this.aimSet(!this.aimOn); } else this.aimSet(down);
    }
    aimSet(on) { if (!!this.aimOn === !!on) return; this.aimOn = !!on; if (!on) this.lockOff(); this.trigger(on ? "onAim" : "onAimEnd"); }
    eCenter(o) { return [o.x, o.floor + (+this.ov(o, "height") || 110) * 0.5, o.y]; }
    lockOff() { if (this.lock) { this.lock = null; if (this.T.get("player.locked")) this.T.set("player.locked", false); } }
    // the best enemy to lock onto: in reach, near the crosshair (within lockAngle), visible; the one nearest the middle wins
    lockFind(w) {
      const T = this.T, v = this.view, c = v.cam, eye = [c.x, c.y, c.z], f = R3D.camBasis(c.yaw, c.pitch).f, rng = +T.get(w.pre + "lockRange") || 1500, maxA = (+T.get(w.pre + "lockAngle") || 20) * Math.PI / 180; let best = null, ba = 1e9;
      for (const o of this.things || []) { if (!this.eAlive(o)) continue; const d = M4.sub(this.eCenter(o), eye), L = M4.len(d); if (L > rng || L < 1) continue; const a = Math.acos(Math.max(-1, Math.min(1, M4.dot(d, f) / L))); if (a > maxA || a > ba) continue;
        const D = d.map(q => q / L), hit = v.raycast(eye, D, L); if (hit && hit.t < L - 20) continue; best = o; ba = a; }
      return best;
    }
    aimStep(dt) {
      const T = this.T, w = this.aimW(); if (!w || this.pdead || this.talk) { if (this.aimOn) this.aimSet(false); }
      const tt = w ? +T.get(w.pre + "aimTime") : 0.15, goal = this.aimOn && w ? 1 : 0, k0 = this.aimK || 0; this.aimK = tt > 0 ? (goal > k0 ? Math.min(goal, k0 + dt / tt) : Math.max(goal, k0 - dt / tt)) : goal;
      if (!!T.get("player.aiming") !== !!this.aimOn) T.set("player.aiming", !!this.aimOn);
      // crosshair + scope picture
      const sights = w && w.type === "sights"; if (this.xh) this.xh.style.opacity = sights && T.get(w.pre + "aimHideCross") ? String(1 - this.aimK) : "";
      const ov = sights && T.get(w.pre + "aimOverlay"); if (ov && this.urls[ov]) { if (!this.scopeEl) { this.scopeEl = document.createElement("img"); this.scopeEl.style.cssText = "position:absolute;inset:0;width:100%;height:100%;object-fit:cover;pointer-events:none;z-index:4"; this.root.append(this.scopeEl); } if (this.scopeEl.src !== this.urls[ov]) this.scopeEl.src = this.urls[ov]; this.scopeEl.style.display = this.aimK > 0.01 ? "block" : "none"; this.scopeEl.style.opacity = String(Math.min(1, this.aimK * 1.5)); }
      else if (this.scopeEl) this.scopeEl.style.display = "none";
      // lock on: find a target, keep it while it lives, stays in reach and in sight; turn the view toward it; a marker on screen
      if (w && w.type === "lock" && this.aimOn) {
        const L = this.lock = this.lock || { o: null, t: 0, lost: 0 }; L.t -= dt;
        if (L.o && (!this.eAlive(L.o) || M4.len(M4.sub(this.eCenter(L.o), [this.view.cam.x, this.view.cam.y, this.view.cam.z])) > (+T.get(w.pre + "lockRange") || 1500) * 1.2)) { L.o = null; T.set("player.locked", false); }
        if (L.o && L.t <= 0) { L.t = 0.2; const c = this.view.cam, e = [c.x, c.y, c.z], d = M4.sub(this.eCenter(L.o), e), Ld = M4.len(d), hit = this.view.raycast(e, d.map(q => q / Ld), Ld); L.lost = hit && hit.t < Ld - 20 ? L.lost + 0.2 : 0; if (L.lost > 0.6) { L.o = null; T.set("player.locked", false); } }
        if (!L.o && L.t <= 0) { L.t = 0.2; const o = this.lockFind(w); if (o) { L.o = o; L.lost = 0; T.set("player.locked", true); const s = T.get(w.pre + "lockSound"); if (s) this.sfx(s); } }
        const turn = +T.get(w.pre + "lockTurn") || 0; if (L.o && turn > 0) { const c = this.view.cam, d = M4.sub(this.eCenter(L.o), [c.x, c.y, c.z]), Ld = M4.len(d) || 1, ty = Math.atan2(d[2], d[0]), tp = Math.asin(Math.max(-1, Math.min(1, d[1] / Ld))), a = Math.min(1, turn * dt);
          let dy = ty - this.yaw; dy = Math.atan2(Math.sin(dy), Math.cos(dy)); this.yaw += dy * a; this.pitch = clamp(this.pitch + (tp - this.pitch) * a, -1.45, 1.45); }
      } else if (this.lock) this.lockOff();
      this.lockMark(w);
    }
    lockMark(w) {
      const o = this.lock && this.lock.o, v = this.view; if (!o || !v || !v.vp) { if (this.lockEl) this.lockEl.style.display = "none"; return; }
      if (!this.lockEl) { this.lockEl = document.createElement("div"); this.lockEl.className = "p3lock"; this.lockEl.style.cssText = "position:absolute;left:0;top:0;pointer-events:none;z-index:5;box-sizing:border-box;transform:translate(-50%,-50%) rotate(45deg)"; this.root.append(this.lockEl); }
      const p = this.eCenter(o), m = v.vp, X = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], Y = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13], W = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
      if (W <= 0) { this.lockEl.style.display = "none"; return; } const sz = +this.T.get(w.pre + "lockSize") || 46, col = this.T.get(w.pre + "lockColor") || "#ffb030", pulse = 1 + Math.sin(performance.now() / 120) * 0.06;
      Object.assign(this.lockEl.style, { display: "block", left: ((X / W) * 0.5 + 0.5) * this.cv.clientWidth + "px", top: (0.5 - (Y / W) * 0.5) * this.cv.clientHeight + "px", width: sz * pulse + "px", height: sz * pulse + "px", border: "3px solid " + col, boxShadow: "0 0 10px " + col });
    }
    // a random direction inside a cone of half-angle `a` around D (spread)
    coneDir(D, a) { const t = Math.abs(D[1]) < 0.9 ? M4.norm(M4.cross(D, [0, 1, 0])) : M4.norm(M4.cross(D, [1, 0, 0])), b = M4.cross(D, t), r = Math.sqrt(Math.random()) * Math.tan(a), ph = Math.random() * Math.PI * 2;
      return M4.norm([D[0] + (t[0] * Math.cos(ph) + b[0] * Math.sin(ph)) * r, D[1] + (t[1] * Math.cos(ph) + b[1] * Math.sin(ph)) * r, D[2] + (t[2] * Math.cos(ph) + b[2] * Math.sin(ph)) * r]); }
    // ---- patrols, ceiling enemies, blasts, slicing
    // walk the path picked for this placed enemy (map.paths, thing.path): point to point, round in a loop or back and forth
    // is the player in front of this patrolling enemy (tag pathFov, degrees; 360 = sees all around)? Always true when you are right next to it.
    eFront(o, dx, dy, dist) { const fov = +this.ov(o, "pathFov"); if (!(fov > 0) || fov >= 360 || dist < (+this.ov(o, "radius") || 22) + 70) return true;
      const a = o.yaw * Math.PI / 180, c = (Math.sin(a) * dx + Math.cos(a) * dy) / (dist || 1); return c >= Math.cos(fov * Math.PI / 360); }
    ePatrol(o, dt) {
      const pa = o.th.path && (this.map.paths || []).find(q => q.id === o.th.path), sp = +this.ov(o, "speed") || 0;
      if (!pa || pa.pts.length < 1 || sp <= 0) { this.eMove(o, 0, 0, dt); o.st = "idle"; this.eSet(o, "idleAnim"); return; }
      if (o.pwait > 0) { o.pwait -= dt; this.eMove(o, 0, 0, dt); o.st = "idle"; this.eSet(o, "idleAnim"); return; }
      o.pi = Math.max(0, Math.min(pa.pts.length - 1, o.pi || 0)); const q = pa.pts[o.pi], dx = q[0] - o.x, dy = q[1] - o.y, d = Math.hypot(dx, dy);
      if (d < 10 || (o.pstuck || 0) > 2.5) { o.pstuck = 0; const n = pa.pts.length; if (n > 1) { if (pa.loop !== false) o.pi = (o.pi + 1) % n; else { if (o.pi + o.pdir < 0 || o.pi + o.pdir >= n) o.pdir = -o.pdir; o.pi += o.pdir; } } o.pwait = +this.ov(o, "pathWait") || 0; this.eMove(o, 0, 0, dt); return; }
      const bx = o.x, by = o.y; this.eMove(o, dx / d * sp, dy / d * sp, dt); o.pstuck = Math.hypot(o.x - bx, o.y - by) < sp * dt * 0.2 ? (o.pstuck || 0) + dt : 0;   // blocked: give up on this point after a while
      o.yaw = Math.atan2(dx, dy) * 180 / Math.PI; o.st = "patrol"; this.eSet(o, "walkAnim");
    }
    // an enemy that never notices you: fires on a timer in the direction it faces and / or hurts you when you touch it; still patrols its path
    eBlind(o, dt, dist, beh) {
      const T = this.T, g = k => +this.ov(o, k) || 0, mode = this.ov(o, "blindMode") || "shoot", r = g("radius") || 22, h = g("height") || 110, rate = g("attackRate") || 1.2; o.awake = false; o.los = false;
      if (mode !== "shoot") { o.tcool = (o.tcool || 0) - dt; const pz = this.z, ph = +T.get("player.eyeHeight") || 102;
        if (o.tcool <= 0 && !this.pdead && dist < r + (+T.get("player.radius") || 16) + 4 && pz < o.floor + h && pz + ph > o.floor) { this.hurtPlayer(g("damage")); o.tcool = rate; o.flash = 0.06; } }
      if (mode !== "touch") { o.bt = (o.bt === undefined ? Math.random() * rate : o.bt) - dt;
        if (o.bt <= 0 && !this.pdead) { o.bt = rate; this.eSet(o, "attackAnim"); o.at = 0; o.bAnim = this.eLen(o, "attackAnim", 0.3); const s = this.ov(o, "attackSound"); if (s) this.sfxAt(s, 1, this.thingPos(o));
          const a = o.yaw * Math.PI / 180, dir = o.mount === "ceiling" ? [0, -1, 0] : [Math.sin(a), 0, Math.cos(a)]; this.eAttack(o, dist, true, this.ov(o, "attack") === "hitscan" ? dir : dir); } }
      if (o.bAnim > 0) { o.bAnim -= dt; if (o.bAnim > 0) { if (!o.mount) this.eMove(o, 0, 0, dt); return; } }
      if (beh === "path") { this.ePatrol(o, dt); return; }
      if (!o.mount) this.eMove(o, 0, 0, dt); o.st = "idle"; this.eSet(o, "idleAnim");
    }
    // walking on walls and ceilings. o.surf = { n: surface normal, p: feet point } while on a wall / ceiling; null = on the floor (normal walking physics).
    // On the floor it climbs a wall that blocks its way; on a wall it climbs up (and toward you); on a ceiling it crawls over you and drops. Returns true when it moved itself.
    eCrawl(o, dt, dist) {
      const T = this.T, g = k => +this.ov(o, k) || 0, v = this.view; if (!v) return false; const sp = g("speed") || 0, r = g("radius") || 22, h = g("height") || 110, P = [this.x, this.z + 40, this.y];
      if (!o.surf) {   // floor: only take over when a wall blocks the way to you
        if (!o.body || !o.body.ground || sp <= 0) return false; const L = Math.hypot(P[0] - o.x, P[2] - o.y) || 1, dir = [(P[0] - o.x) / L, 0, (P[2] - o.y) / L], hit = v.raycast([o.x, o.floor + Math.min(h * 0.5, 40), o.y], dir, r + 14);
        const meleeNear = this.ov(o, "attack") === "melee" && dist < r + 40 && Math.abs(this.z - o.floor) < 60;
        if (hit && hit.n && Math.abs(hit.n[1]) < 0.4 && !meleeNear && Math.abs(P[1] - o.floor) < 400) { o.cBlock = (o.cBlock || 0) + dt; if (o.cBlock > 0.2) { o.cBlock = 0; o.surf = { n: hit.n.slice(), p: M4.add(hit.p, hit.n.map(q => q * 1)) }; o.cstuck = 0; } }
        else o.cBlock = 0;
        if (!o.surf) return false;
      }
      const s = o.surf, n = s.n, F = s.p; let D;
      if (n[1] < -0.5) { const L = Math.hypot(P[0] - F[0], P[2] - F[2]); D = [P[0] - F[0], 0, P[2] - F[2]];   // ceiling: over you, then drop
        if (L < r + 26 || (this.ov(o, "attack") !== "melee" && o.los && dist < (g("attackRange") || 70))) { if (L < r + 26) return this.eCrawlOff(o, true); D = [0, 0, 0]; } }
      else { const L = Math.hypot(P[0] - F[0], P[2] - F[2]) || 1; D = [(P[0] - F[0]) / L * 0.6, 1, (P[2] - F[2]) / L * 0.6]; }   // wall: up and toward you
      const dn = M4.dot(D, n); D = M4.sub(D, n.map(q => q * dn)); const DL = M4.len(D);
      if (DL < 1e-3) { this.eCrawlPlace(o, h); this.eSet(o, "idleAnim"); return true; } D = D.map(q => q / DL); s.fwd = D;
      const step = sp * dt, C = M4.add(F, n.map(q => q * r * 0.5)), ahead = v.raycast(C, D, step + r);
      if (ahead && ahead.n && ahead.t < step + r * 0.8 && M4.dot(ahead.n, n) < 0.7) { s.n = ahead.n.slice(); s.p = M4.add(ahead.p, ahead.n.map(q => q * 1)); }   // inside corner: onto the surface in front
      else { const F2 = M4.add(F, D.map(q => q * step)), down = v.raycast(M4.add(F2, n.map(q => q * r * 0.5)), n.map(q => -q), r * 0.5 + 12);
        if (down && down.n) { s.p = M4.add(down.p, down.n.map(q => q * 1)); s.n = down.n.slice(); }
        else { const wrap = v.raycast(M4.sub(F2, n.map(q => q * 4)), D.map(q => -q), r + 12);   // outside corner: around the edge
          if (wrap && wrap.n) { s.n = wrap.n.slice(); s.p = M4.add(wrap.p, wrap.n.map(q => q * 1)); } else return this.eCrawlOff(o, true); } }
      const moved = M4.len(M4.sub(s.p, F)); o.cstuck = moved < step * 0.2 ? (o.cstuck || 0) + dt : 0; if (o.cstuck > 1.5) return this.eCrawlOff(o, true);
      o.cwin = (o.cwin || 0) + dt; if (!o.cfrom) o.cfrom = s.p.slice(); if (o.cwin > 0.6) { const prog = M4.len(M4.sub(s.p, o.cfrom)); o.cwin = 0; o.cfrom = s.p.slice(); if (prog < sp * 0.08) return this.eCrawlOff(o, true); }   // flipping in a tight corner without getting anywhere: let go
      if (s.n[1] > 0.5) return this.eCrawlOff(o, false);   // on top of something: walk normally again
      this.eCrawlPlace(o, h); o.st = "chase"; this.eSet(o, "walkAnim"); return true;
    }
    eCrawlPlace(o, h) { const s = o.surf, c = M4.add(s.p, s.n.map(q => q * h * 0.5)); o.x = c[0]; o.y = c[2]; o.floor = c[1] - h * 0.5; }
    eCrawlOff(o, fall) { const s = o.surf; o.surf = null; o.body = null; o.cfrom = null; o.cwin = 0; if (s) { o.x = s.p[0] + s.n[0] * 4; o.y = s.p[2] + s.n[2] * 4; o.floor = s.p[1] + (s.n[1] < 0 ? -(+this.ov(o, "height") || 110) : 0); }
      if (fall) { o.st = "falling"; o.at = 0; o.crawlDrop = true; } return true; }
    // the model standing on its surface: up = the surface normal, front = the way it crawls
    crawlFrame(o) { const s = o.surf, Y = s.n, f0 = s.fwd || (Math.abs(Y[1]) > 0.9 ? [0, 0, 1] : [0, 1, 0]), Z = M4.norm(M4.sub(f0, Y.map(q => q * M4.dot(f0, Y)))), X = M4.cross(Y, Z), F = s.p;
      return [X[0], X[1], X[2], 0, Y[0], Y[1], Y[2], 0, Z[0], Z[1], Z[2], 0, F[0], F[1], F[2], 1]; }
    // a spawner: every `spawnEvery` s a new copy of `spawnWhat` appears around it (runtime only: this.spawnTh, cleared on restart), while fewer than `spawnMax` of its copies are alive
    spawnerStep(o, dt) {
      const T = this.T, g = k => +this.ov(o, k) || 0, what = this.ov(o, "spawnWhat"), st = o.sp = o.sp || { t: g("spawnFirst"), made: 0, kids: [] };
      if (!what || !(this.project.props || []).some(q => q.id === what) || this.pdead) return;
      const need = this.ov(o, "spawnNeedTag"); if (need && this.sh && !this.sh.test(need, this.ov(o, "spawnNeedOp") || "=", this.ov(o, "spawnNeedVal"))) return;
      if (g("spawnNear") > 0 && Math.hypot(this.x - o.fr.cx, this.y - o.fr.cy) > g("spawnNear")) return;
      const alive = id => { const k = (this.things || []).find(x => x.th.id === id); if (!k) return true; return !k.taken && !(k.type === "enemy" && (k.st === "dead" || k.st === "dying")); };
      st.kids = st.kids.filter(alive); this.spawnTh = (this.spawnTh || []).filter(th => { const k = (this.things || []).find(x => x.th.id === th.id); return !(k && k.taken); });
      if (g("spawnMax") > 0 && st.kids.length >= g("spawnMax")) return; if (g("spawnTotal") > 0 && st.made >= g("spawnTotal")) return;
      st.t -= dt; if (st.t > 0) return; st.t = g("spawnEvery") || 4;
      const L = o.th.layer || 0, R = g("spawnRadius"); let X = o.fr.cx, Z = o.fr.cy;
      for (let i = 0; i < 8; i++) { const a = Math.random() * Math.PI * 2, rr = Math.sqrt(Math.random()) * R, x = o.fr.cx + Math.cos(a) * rr, z = o.fr.cy + Math.sin(a) * rr, c = G.cell(this.map, L, Math.floor(x / 128), Math.floor(z / 128)); if (c && !G.isWall(c) && !(c.dg)) { X = x; Z = z; break; } }
      this.spawnN = (this.spawnN || 0) + 1; st.made++; const th = { id: o.th.id + "~" + this.spawnN, prop: what, x: X, y: Z, ang: Math.floor(Math.random() * 4) * 90, layer: L, spawned: true, wake: this.ov(o, "spawnAwake") !== false };
      (this.spawnTh = this.spawnTh || []).push(th); st.kids.push(th.id); this.syncThings();
      const P = [X, o.fr.fy + 40, Z], snd = this.ov(o, "spawnSound"); if (snd) this.sfxAt(snd, 1, P);
      if (this.ov(o, "spawnFx") !== false) { const col = this.hexCol(this.ov(o, "spawnColor") || "#7df3ff"); this.burst(P, col, 24, 220, 0.5, 2.5); this.fx.push({ kind: "sphere", deco: true, p: P.slice(), d: [0, 1, 0], c: [0, 0, 0], size0: 0.01, size: 0.01, speed: 0, life: 0.3, t: 0, light: { c: col, k: 2.5, k0: 2.5, r: 260, shadow: false } }); }
    }
    // stick to the nearest ceiling straight above the placed spot (its top touches the ceiling)
    eHang(o) {
      const h = +this.ov(o, "height") || 110, v = this.view, from = [o.x, o.floor + 4, o.y], hit = v && v.raycast(from, [0, 1, 0], 4000);
      const top = hit && hit.p ? hit.p[1] : o.floor + G.LAYER - 1; o.floor = top - h; o.ceilY = top;
    }
    // you are inside its detection cone (straight down, `coneAngle` wide, `coneRange` long) and it can see you
    inCone(o) {
      const T = this.T, B = [o.x, o.floor, o.y], P = [this.x, this.z + (+T.get("player.eyeHeight") || 102) * 0.5, this.y], d = M4.sub(P, B), L = M4.len(d); if (L < 1) return true;
      if (L > (+this.ov(o, "coneRange") || 400) + 60 || -d[1] / L < Math.cos((+this.ov(o, "coneAngle") || 25) * Math.PI / 180)) return false; return this.canSee(o);
    }
    // a dropped enemy hits the floor: it turns into its "becomes" enemy, explodes, or simply hunts you
    eLand(o) {
      if (o.crawlDrop) { o.crawlDrop = false; o.st = "chase"; o.awake = true; this.eSet(o, "walkAnim"); return; }
      const T = this.T, into = this.ov(o, "dropInto"), act = this.ov(o, "ceilAction");
      if (act === "selfdestruct") { o.st = "fuse"; o.fuseT = +this.ov(o, "fuse") || 0; return; }
      if (into && into !== o.morph && (this.project.props || []).some(q => q.id === into) && T.get(into + ".type") === "enemy") { o.morph = into; o.keep = { x: o.x, y: o.y, floor: o.floor, yaw: o.yaw, dropped: true }; o.init = false; o.mount = null; this.syncThings(); return; }
      o.st = "chase"; o.awake = true; this.eSet(o, "walkAnim");
    }
    // particles flying out of a point (decoration; respects the particle budget)
    burst(c, col, count, speed, life, size) {
      const room = Math.max(0, (this.fxBudget || 1500) - (this.decoN || 0)), cnt = Math.min(count, room); this.decoN = (this.decoN || 0) + cnt; this.fx = this.fx || [];
      for (let i = 0; i < cnt; i++) { let d = [Math.random() * 2 - 1, Math.random() * 2 - 1, Math.random() * 2 - 1]; const L = M4.len(d) || 1; d = d.map(q => q / L); const sz = size * (0.6 + Math.random() * 0.8);
        this.fx.push({ kind: "sphere", deco: true, p: c.slice(), d, c: col.map(q => q * 1.6), size0: sz, size: sz, speed: speed * (0.4 + Math.random() * 0.8), grav: 500, life: life * (0.6 + Math.random() * 0.6), t: 0 }); }
    }
    // an enemy explosion: hurts you and other enemies (less toward the edge, walls block it), particles, light, shock sphere, sound
    eBlast(o, kill, at) {
      const T = this.T, g = k => +this.ov(o, k) || 0, h = g("height") || 110, c = at || [o.x, o.floor + h * 0.5, o.y], R = g("blastRadius"), dmg = g("blastDamage"), col = this.hexCol(this.ov(o, "blastColor") || "#ff8a2a");
      const clear = (a, b) => { const d = M4.sub(b, a), L = M4.len(d); if (L < 1) return true; const hit = this.view && this.view.raycast(a, d.map(q => q / L), L); return !(hit && hit.t < L - 8); };
      if (R > 0 && dmg > 0) { const pc = [this.x, this.z + 50, this.y], dp = M4.len(M4.sub(pc, c)); if (dp < R && !this.pdead && clear(c, pc)) this.hurtPlayer(Math.round(dmg * (1 - 0.75 * dp / R)));
        for (const e of this.things || []) { if (e === o || !this.eAlive(e)) continue; const ec = [e.x, e.floor + 50, e.y], de = M4.len(M4.sub(ec, c)); if (de < R && clear(c, ec)) this.damageEnemy(e, dmg * (1 - 0.75 * de / R)); } }
      this.burst(c, col, Math.round(g("blastCount")), 320, 0.6, 3); this.fx.push({ kind: "sphere", deco: true, p: c.slice(), d: [0, 1, 0], c: [0, 0, 0], size0: 0.01, size: 0.01, speed: 0, life: 0.35, t: 0, light: { c: col, k: 3, k0: 3, r: Math.max(200, R * 1.5), shadow: false } });
      if (this.ov(o, "blastSphere") !== false && R > 0) this.fx.push({ kind: "shock", deco: true, p: c.slice(), R, life: g("blastSphereTime") || 0.45, t: 0, c1: this.hexCol(this.ov(o, "blastSphereColor1") || "#ffe08a"), c2: this.hexCol(this.ov(o, "blastSphereColor2") || "#ff4a1a"), anim: this.ov(o, "blastSphereAnim") || "bands", aspd: this.ov(o, "blastSphereSpeed") === undefined ? 2 : g("blastSphereSpeed"), glow: this.ov(o, "blastSphereGlow") === undefined ? 1.2 : g("blastSphereGlow") });
      const s = this.ov(o, "blastSound"); if (s) this.sfxAt(s, 1, c, 3000);
      if (kill) { o.hp = 0; o.flash = 0; if (this.ov(o, "deathStyle") === "slice" && !o.sliced) this.eSlice(o, true); else { o.st = "dead"; o.taken = true; } }
    }
    // cut the model (as it looks right now) into pieces with random planes, each cut gets a cap in the inside colour; the pieces fly apart and spin
    eSlice(o, noBlast) {
      const T = this.T, g = k => +this.ov(o, k) || 0, v = this.view; o.st = "dead"; o.taken = true; o.sliced = true; const ss = this.ov(o, "sliceSound"); if (ss) this.sfxAt(ss, 1, this.thingPos(o));
      if (!v || o.sprite || !o.model || !o.lastM || !o.lastPose) { this.eBlast(o, false); return; }
      // every triangle in world space: [x,y,z,u,v] x 3
      let tris = []; for (const ms of o.gm.meshes) { const mesh = o.model.nodes[ms.node] && o.model.nodes[ms.node].mesh; if (!mesh) continue; const W = M4.mul(o.lastM, o.lastPose[ms.node]), cnt = mesh.p.length / 3;
        for (let t = 0; t + 2 < cnt; t += 3) { const tri = []; for (let j = 0; j < 3; j++) { const i = t + j, p = M4.point(W, mesh.p[i * 3], mesh.p[i * 3 + 1], mesh.p[i * 3 + 2]); tri.push([p[0], p[1], p[2], mesh.u[i * 2], mesh.u[i * 2 + 1]]); } tris.push(tri); } }
      if (!tris.length) { this.eBlast(o, false); return; }
      if (tris.length > 6000) tris = tris.filter((_, i) => i % Math.ceil(tris.length / 6000) === 0);
      const center = list => { const c = [0, 0, 0]; let n = 0; for (const t of list) for (const q of t) { c[0] += q[0]; c[1] += q[1]; c[2] += q[2]; n++; } return c.map(q => q / Math.max(1, n)); };
      const lerp = (a, b, k) => a.map((q, i) => q + (b[i] - q) * k);
      // split triangles by a plane: returns [front, back, cut segments]
      const cut = (list, n, d) => { const A = [], B = [], seg = [];
        for (const t of list) { const sd = t.map(q => q[0] * n[0] + q[1] * n[1] + q[2] * n[2] - d), pos = sd.map(x => x >= 0); if (pos.every(Boolean)) { A.push(t); continue; } if (!pos.some(Boolean)) { B.push(t); continue; }
          const lone = pos.filter(Boolean).length === 1 ? pos.indexOf(true) : pos.indexOf(false), a = t[lone], b = t[(lone + 1) % 3], c2 = t[(lone + 2) % 3], ab = lerp(a, b, sd[lone] / (sd[lone] - sd[(lone + 1) % 3])), ac = lerp(a, c2, sd[lone] / (sd[lone] - sd[(lone + 2) % 3]));
          const one = [[a, ab, ac]], two = [[ab, b, c2], [ab, c2, ac]]; if (pos[lone]) { A.push(...one); B.push(...two); } else { B.push(...one); A.push(...two); } seg.push([ab, ac]); }
        return [A, B, seg]; };
      const N = Math.max(2, Math.min(8, Math.round(g("slicePieces") || 4))); let pieces = [{ tris, caps: [] }];
      while (pieces.length < N) { pieces.sort((a, b) => b.tris.length - a.tris.length); const pc = pieces.shift(); if (pc.tris.length < 2) { pieces.push(pc); break; }
        const c = center(pc.tris); let n = [Math.random() * 2 - 1, (Math.random() * 2 - 1) * 0.6, Math.random() * 2 - 1]; n = M4.norm(n); const d = n[0] * c[0] + n[1] * c[1] + n[2] * c[2], [A, B, seg] = cut(pc.tris, n, d);
        if (!A.length || !B.length) { pieces.push(pc); if (pieces.length >= N || pc.tris.length < 4) break; continue; }
        const cap = seg.length ? { seg, n } : null, keepCaps = (side, sgn) => pc.caps.map(cp => ({ seg: cut(cp.tris || [], n, d)[sgn ? 0 : 1], n: cp.n, tris: cut(cp.tris || [], n, d)[sgn ? 0 : 1] })).filter(cp => cp.tris.length);
        const capTris = (s, nn) => { const ctr = [0, 0, 0]; for (const [a, b] of s) for (let i = 0; i < 3; i++) ctr[i] += (a[i] + b[i]) / (2 * s.length); return s.map(([a, b]) => { const tri = [[...ctr, 0, 0], a, b], f = M4.cross(M4.sub(b, a), M4.sub(tri[0], a)); return M4.dot(f, nn) < 0 ? tri : [tri[0], b, a]; }); };
        const cA = cap ? { tris: capTris(seg, n.map(q => -q)) } : null, cB = cap ? { tris: capTris(seg, n) } : null;
        pieces.push({ tris: A, caps: keepCaps(A, 1).concat(cA ? [cA] : []) }, { tris: B, caps: keepCaps(B, 0).concat(cB ? [cB] : []) }); }
      // turn every piece into a little model that flies off from the middle
      const all = center(tris), force = g("sliceForce") || 260, inside = this.hexCol(this.ov(o, "sliceInside") || "#7a1414"), Ld = M4.norm([0.3, 0.8, 0.5]), life = g("sliceTime") || 0.7, tex = o.gm.tex, mat = this.matGL(o.pre);
      for (const pc of pieces) { if (!pc.tris.length) continue; const c = center(pc.tris), lo = [1e9, 1e9, 1e9], hi = [-1e9, -1e9, -1e9];
        const pack = (list, col) => { const a = new Float32Array(list.length * 24); let k = 0; for (const t of list) { const fn = M4.norm(M4.cross(M4.sub(t[1], t[0]), M4.sub(t[2], t[0]))), lit = 0.55 + 0.45 * Math.max(0, M4.dot(fn, Ld)) || 0.55;
          for (const q of t) { const x = q[0] - c[0], y = q[1] - c[1], z = q[2] - c[2]; lo[0] = Math.min(lo[0], x); lo[1] = Math.min(lo[1], y); lo[2] = Math.min(lo[2], z); hi[0] = Math.max(hi[0], x); hi[1] = Math.max(hi[1], y); hi[2] = Math.max(hi[2], z);
            a.set([x, y, z, q[3] || 0, q[4] || 0, (col ? col[0] : 1) * lit, (col ? col[1] : 1) * lit, (col ? col[2] : 1) * lit], k); k += 8; } } return a; };
        const meshes = [v.meshFrom(pack(pc.tris, null), tex)]; const capList = pc.caps.flatMap(cp => cp.tris || []); if (capList.length) meshes.push(v.meshFrom(pack(capList, inside), null));
        let dir = M4.sub(c, all); const dl = M4.len(dir); dir = dl > 1 ? dir.map(q => q / dl) : M4.norm([Math.random() - 0.5, 0.5, Math.random() - 0.5]); dir[1] += 0.6; dir = M4.norm(dir);
        const sp = force * (0.6 + Math.random() * 0.8), ax = M4.norm([Math.random() - 0.5, Math.random() - 0.5, Math.random() - 0.5]);
        this.fx = this.fx || []; this.fx.push({ kind: "mesh", piece: true, deco: true, owner: o, gm: { model: { bounds: { lo, hi } }, meshes, tex }, pose: [M4.ident()], M: M4.translate(c[0], c[1], c[2]), p: c.slice(), vel: dir.map(q => q * sp), ax, spin: (Math.random() * 2 - 1) * 9, ang: 0, t: 0, life, scale: 3, mat, blast: !noBlast && this.ov(o, "sliceBlast") !== false, rest: false }); }
    }
    // one flying piece: gravity, bounce on the floor, spin; at the end it explodes (or just disappears)
    pieceStep(f, dt) {
      f.t += dt; const o = f.owner, rad = Math.max(4, Math.max(f.gm.model.bounds.hi[1], -f.gm.model.bounds.lo[1]) * 0.6);
      if (!f.rest) { f.vel[1] -= 900 * dt; f.p = M4.add(f.p, f.vel.map(q => q * dt)); f.ang += f.spin * dt;
        const gy = this.map ? G.groundY(this.map, f.p[0], f.p[2], f.p[1] + 20) : 0; if (gy !== null && gy !== undefined && f.p[1] - rad < gy) { f.p[1] = gy + rad; if (f.vel[1] < 0) f.vel[1] *= -0.3; f.vel[0] *= 0.6; f.vel[2] *= 0.6; f.spin *= 0.6; if (Math.abs(f.vel[1]) < 40 && Math.hypot(f.vel[0], f.vel[2]) < 30) f.rest = true; }
        const hv = Math.hypot(f.vel[0], f.vel[2]), hit = hv > 1 && this.view && this.view.raycast(f.p, [f.vel[0] / hv, 0, f.vel[2] / hv], rad); if (hit && hit.n) { const n = hit.n, vn = M4.dot(f.vel, n); if (vn < 0) f.vel = M4.sub(f.vel, n.map(q => q * vn * 1.4)); } }
      const R = M4.rotAxis(f.ax, f.ang); f.M = M4.mul(M4.translate(f.p[0], f.p[1], f.p[2]), R);
      if (f.t < f.life) return true;
      if (f.blast && o) this.eBlast(o, false, f.p.slice()); for (const ms of f.gm.meshes) this.view.freeMesh(ms); return false;
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
      if (an) this.playAnim("player." + at.id, nm, false); const s = T.get(pre + "reloadSound"); if (s) this.sfx(s); this.reloading = { at, t: len }; T.set("player.reloading", true); this.triggerAtt(at.id, "onReload"); return true;
    }
    reloadStep(dt) { const r = this.reloading; if (!r) return; r.t -= dt; if (r.t > 0) return; this.reloading = null; this.T.set("player.reloading", false); this.triggerAtt(r.at.id, "onReloadDone"); const T = this.T, pre = "player." + r.at.id + ".", clip = +T.get(pre + "clipSize") || 0, am = +T.get(pre + "ammo") || 0, rs = +T.get(pre + "reserve") || 0, mv = Math.max(0, Math.min(clip - am, rs)); T.setMany({ [pre + "ammo"]: am + mv, [pre + "reserve"]: rs - mv }); }
    useAmmo(at) {   // true = the shot may go out
      const T = this.T, pre = "player." + at.id + "."; if (!T.get(pre + "ammoUse")) return true; if (this.reloading) return false;
      const am = +T.get(pre + "ammo") || 0; if (am >= 1) { T.set(pre + "ammo", am - 1); return true; }
      if (!this.reload(at)) { const s = T.get(pre + "emptySound"); if (s) this.sfx(s); } return false;
    }
    // ---- damage flash, death, level exits, changing maps
    flash(a, color, force) {   // red damage flash (setting player.damageFlash), or any colour from the "Flash the screen" node
      if (!force && !this.T.get("player.damageFlash")) return; if (!this.flashEl) { this.flashEl = document.createElement("div"); this.flashEl.style.cssText = "position:absolute;inset:0;pointer-events:none;opacity:0;z-index:4"; this.root.append(this.flashEl); }
      const e = this.flashEl, c = this.hexCol(color || "#c80000").map(q => Math.round(q * 255)).join(","); e.style.background = color ? "rgb(" + c + ")" : "radial-gradient(ellipse at center,rgba(255,0,0,0) 35%,rgba(200,0,0,.85) 100%)";
      e.style.transition = "none"; e.style.opacity = a; requestAnimationFrame(() => { e.style.transition = "opacity .45s ease-out"; e.style.opacity = 0; }); }
    // ---- Lives system (player tag livesMode): "classic" = lives + respawn at the last checkpoint, "reserve" = energy tanks (Metroid style), "off"
    livesMode() { return this.T.get("player.livesMode") || "off"; }
    initLives() { const T = this.T, set = { "player.lives": +T.get("player.livesStart") || 3 }, tm = Math.min(+T.get("player.tanksLimit") || 14, +T.get("player.tanksStart") || 0); set["player.tanksMax"] = tm; set["player.tanks"] = tm; for (const k of Object.keys(set)) if (!T.has(k)) delete set[k]; T.setMany(set); }
    // energy tanks: when health would drop to 0, a full tank refills it (the leftover damage still counts)
    useTanks() { const T = this.T; if (this.livesMode() !== "reserve" || this.pdead) return; let hp = +T.get("player.health") || 0, tk = +T.get("player.tanks") || 0; const mx = +T.get("player.maxHealth") || 100; if (hp > 0 || tk <= 0) return; while (hp <= 0 && tk > 0) { tk--; hp += mx; } T.setMany({ "player.health": Math.min(mx, hp), "player.tanks": tk }); this.lastHp = Math.min(mx, hp); this.toast("Energy tank used"); }
    // health pick-ups in reserve mode: once health is full the rest fills empty tanks
    giveHealth(amt) { const T = this.T, mx = +T.get("player.maxHealth") || 100, hp = (+T.get("player.health") || 0) + amt, own = +T.get("player.tanksMax") || 0; let tk = +T.get("player.tanks") || 0;
      if (hp > mx && tk < own) { this.tankFill = (this.tankFill || 0) + hp - mx; while (this.tankFill >= mx && tk < own) { this.tankFill -= mx; tk++; } if (tk >= own) this.tankFill = 0; }   // the extra collects until a whole tank is full
      T.setMany({ "player.health": Math.min(mx, hp), "player.tanks": tk }); }
    respawn() {
      const T = this.T, c = this.cp; T.set("player.lives", Math.max(0, (+T.get("player.lives") || 0) - 1)); T.set("player.health", +T.get("player.maxHealth") || 100); this.lastHp = undefined;
      this.fx = []; this.orb = null; this.reloading = null; this.extV = null; this.jumpReq = undefined; this.shakeT = 0; this.pdead = false; this.deadT = 0; this.say2(""); this.body = null;
      if (c && this.map && c.map === this.map.id) { this.x = c.x; this.y = c.y; this.z = c.z; this.yaw = c.yaw; this.pitch = 0; this.vz = 0; this.ground = true; } else this.placeStart();
      this.trigger("onRespawn"); this.toast((+T.get("player.lives") || 0) + (+T.get("player.lives") === 1 ? " life left" : " lives left"));
    }
    toast(t) { if (!this.toastEl) { this.toastEl = document.createElement("div"); this.toastEl.style.cssText = "position:absolute;left:50%;top:14%;transform:translateX(-50%);padding:8px 18px;background:rgba(10,14,20,.75);border:1px solid rgba(255,255,255,.25);border-radius:4px;color:#e8f2fb;font:600 18px 'Chakra Petch',system-ui,sans-serif;letter-spacing:.06em;pointer-events:none;z-index:6;transition:opacity .4s;opacity:0"; this.root.append(this.toastEl); }
      this.toastEl.textContent = t; this.toastEl.style.opacity = 1; clearTimeout(this.toastT); this.toastT = setTimeout(() => { if (this.toastEl) this.toastEl.style.opacity = 0; }, 1800); }
    // ---- pick-up pop-ups: pause everything and say what you got (closed with the Interact key, Space, Enter or a click)
    popupShow(o) {
      const T = this.T, pre = o.pre, g = k => T.get(pre + k); this.popup = { t0: performance.now() }; for (const k of Object.keys(this.keys)) this.release(k);
      if (!this.popEl) { this.popEl = document.createElement("div"); this.popEl.className = "p3pop"; this.popEl.style.cssText = "position:absolute;inset:0;display:grid;place-items:center;background:rgba(0,0,0,.45);z-index:8"; this.popEl.addEventListener("pointerdown", e => { e.stopPropagation(); this.popupClose(); }); this.root.append(this.popEl); }
      const box = document.createElement("div"); box.style.cssText = "min-width:320px;max-width:min(560px,86%);padding:22px 28px;background:rgba(8,14,22,.93);border:2px solid rgba(125,243,255,.7);border-radius:6px;color:#e8f2fb;text-align:center;font:17px/1.45 'Chakra Petch',system-ui,sans-serif;box-shadow:0 0 30px rgba(125,243,255,.25)";
      const img = g("popupImage") && this.urls[g("popupImage")]; if (img) { const i = document.createElement("img"); i.src = img; i.alt = ""; i.style.cssText = "display:block;margin:0 auto 12px;max-width:160px;max-height:160px;image-rendering:pixelated"; box.append(i); }
      const h = document.createElement("div"); h.textContent = g("popupTitle") || g("name") || "Item"; h.style.cssText = "font-size:26px;font-weight:700;letter-spacing:.1em;text-transform:uppercase;color:#7df3ff;margin-bottom:6px"; box.append(h);
      if (g("popupText")) { const tx = document.createElement("div"); tx.textContent = T.interpolate(String(g("popupText"))); box.append(tx); }
      const f = document.createElement("div"); f.textContent = "[" + String(T.get("player.interactKey") || "e").toUpperCase() + "]  Continue"; f.style.cssText = "margin-top:16px;font-size:13px;opacity:.7;letter-spacing:.1em"; box.append(f);
      this.popEl.replaceChildren(box); this.popEl.style.display = "grid"; const snd = g("popupSound"); if (snd) this.sfx(snd, 1);
    }
    popupKey(id) { if (!this.popup || performance.now() - this.popup.t0 < 350) return; if (id === "space" || id === "enter" || id === "escape" || id === (this.T.get("player.interactKey") || "e")) this.popupClose(); }
    popupClose() { if (!this.popup || performance.now() - this.popup.t0 < 350) return; this.popup = null; if (this.popEl) this.popEl.style.display = "none"; }
    deathStep(dt) {
      const T = this.T; if (!this.pdead) { this.useTanks(); if ((+T.get("player.health") || 0) <= 0) { this.pdead = true; this.deadT = 0; this.reloading = null; this.say2("You died"); } return; }
      this.deadT += dt; if (this.deadT < (+T.get("player.deathDelay") || 0)) return;
      if (this.livesMode() === "classic" && (+T.get("player.lives") || 0) > 0) { this.respawn(); return; }   // lives left: back at the last checkpoint, the level stays as it is
      if (T.get("player.deathAction") === "save" && PRIM.Save) { const d = PRIM.Save.read(this.project.id, this.slot); if (d) { this.loadGame(d); return; } }
      const scr = T.get("player.deathScreen"); if (T.get("player.deathAction") === "screen" && scr && this.o.onScreen) { this.pdead = false; this.o.onScreen(scr); return; } this.restartMap();
    }
    say2(t) { if (!this.bigMsg) { this.bigMsg = document.createElement("div"); this.bigMsg.style.cssText = "position:absolute;left:0;right:0;top:38%;text-align:center;font:600 46px 'Chakra Petch',system-ui,sans-serif;letter-spacing:.12em;color:#ffdede;text-shadow:0 2px 10px #000;pointer-events:none;z-index:6;transition:opacity .6s"; this.root.append(this.bigMsg); } this.bigMsg.textContent = t; this.bigMsg.style.opacity = t ? 1 : 0; }
    // the map's player start (tags startX / startY / startLayer / startAngle), standing on the floor there
    placeStart() {
      const T = this.T, m = this.map; this.x = m ? +T.get(m.id + ".startX") || 0 : 0; this.y = m ? +T.get(m.id + ".startY") || 0 : 0; this.yaw = (m ? +T.get(m.id + ".startAngle") : 90) * Math.PI / 180; this.pitch = 0;
      const L = m ? +T.get(m.id + ".startLayer") || 0 : 0; this.z = m && G ? G.groundY(m, this.x, this.y, L * G.LAYER + G.LAYER - 8) : 0; this.vz = 0; this.body = null; this.ground = true;
    }
    resetPlayer() { this.placeStart(); this.zst = null; this.fx = []; this.orb = null; this.reloading = null; if (this.mode === "play" && this.T.get("player.reloading")) this.T.set("player.reloading", false); this.extV = null; this.jumpReq = undefined; this.airJumps = 0; this.shakeT = 0; this.pdead = false; this.deadT = 0; this.say2(""); this.doorOpen = {}; this.bossStop(); this.things = []; this.spawnTh = []; this.syncThings(); this.applyWorld(); }
    restartMap() { const T = this.T; T.set("player.health", +T.get("player.maxHealth") || 100); this.lastHp = undefined; this.initAmmo(); this.initLives(); this.cp = null; this.keysReset("snap"); this.resetPlayer(); }
    loadMap(id) { const m = this.project.maps.find(x => x.id === id); if (!m || !this.view) return false; this.map = m; this.cp = null; this.view.setMap(this.project, m, this.T, this.urls); this.resetPlayer(); return true; }
    levelExit(o) {
      if (this.exiting) return; const T = this.T, s = this.ov(o, "exitSound"); if (s) this.sfx(s);
      if (this.ov(o, "exitTo") === "screen") { const sc = this.ov(o, "exitScreen"); if (sc && this.o.onScreen) { this.exiting = true; this.o.onScreen(sc); return; } this.say2("Level finished"); this.exiting = true; return; }
      const maps = this.project.maps, cur = maps.indexOf(this.map), id = this.ov(o, "exitMap") || (maps[cur + 1] && maps[cur + 1].id);
      if (!id) { this.say2("Level finished"); this.exiting = true; return; } this.keysReset("level"); this.loadMap(id);
    }
    // footsteps, jump / landing / hurt / death sounds (player.* sound tags)
    psound(tag) { const id = this.T.get("player." + tag); return id ? this.sfx(id, +this.T.get("player.soundVolume")) : null; }
    // one footstep at a time: never two within 0.2 s (or right after landing), and a new step cuts the tail of the last one so they never pile up
    footstep() {
      const T = this.T, ids = ["footstep", "footstep2", "footstep3"].filter(t => T.get("player." + t)), now = performance.now(); if (!ids.length) return; if (now - (this.lastStepT || 0) < 200 || now - (this.landT || 0) < 250) return; this.lastStepT = now;
      let i = Math.floor(Math.random() * ids.length); if (ids.length > 1 && i === this.lastStep) i = (i + 1) % ids.length; this.lastStep = i;
      if (this.stepSnd && this.stepSnd.playing) this.stepSnd.stop(0.06); this.stepSnd = this.psound(ids[i]);
    }
    leave() { if (this.o.onExit) this.o.onExit(); else this.destroy(); }
    // ---- pause menu: Esc freezes the game (like a pop-up) and shows the pause screen (tag player.pauseScreen) through a Runtime on top
    pauseId() { const id = this.T.get("player.pauseScreen"); return id && this.project.screens.some(s => s.id === id) ? id : ""; }
    pauseOpen(screenId) {
      const id = screenId || this.pauseId(); if (!id || this.paused || this.dead) return false;
      for (const k of Object.keys(this.keys)) this.release(k); this.drag = false;
      this.paused = { t0: performance.now() }; this.msg.style.display = "none"; if (this.lv) { this.lv.style.display = this.o.debug ? "block" : "none"; this.lv.style.zIndex = 30; } if (this.talkEl) this.talkEl.style.visibility = "hidden";
      if (document.pointerLockElement === this.cv) document.exitPointerLock();
      PRIM.Audio.setWorld(+this.T.get("player.pauseVolume") >= 0 ? +this.T.get("player.pauseVolume") : 0.25);
      const el = this.pauseEl = document.createElement("div"); el.className = "p3pause"; el.style.cssText = "position:absolute;inset:0;z-index:20"; this.root.append(el);
      this.pauseRt = new PRIM.Runtime({ host: el, project: this.project, tags: this.T, urls: this.urls, mode: "play", screenId: id, noEscExit: true, noHint: true, transparent: true, say: t => { if (this.o.say) this.o.say(t); },
        onResume: () => this.pauseClose(), onRestart: () => { this.pauseClose(); this.restartMap(); }, onMenu: () => { this.pauseClose(true); if (this.o.onMenu) this.o.onMenu(); else this.leave(); },
        onQuit: () => { this.pauseClose(true); if (this.o.onQuit) this.o.onQuit(); else this.leave(); }, onExit: () => { this.pauseClose(true); this.leave(); }, onStartGame: () => this.pauseClose() });
      this.trigger("onPause"); return true;
    }
    pauseClose(leaving) {
      if (!this.paused) return; this.paused = null; PRIM.Audio.setWorld(1); if (this.pauseRt) { this.pauseRt.destroy(); this.pauseRt = null; } if (this.pauseEl) { this.pauseEl.remove(); this.pauseEl = null; }
      this.last = performance.now(); if (this.talkEl) this.talkEl.style.visibility = ""; if (leaving || this.dead) return; this.trigger("onResume"); if (this.talk) return;   // still talking: the mouse stays free for the talk box
      try { const r = this.cv.requestPointerLock && this.cv.requestPointerLock(); if (r && r.catch) r.catch(() => { this.msg.style.display = "grid"; }); } catch (x) {} this.cv.focus();
      if (document.pointerLockElement !== this.cv) { this.msg.style.display = "grid"; if (this.lv) this.lv.style.display = "block"; }
    }
    // Classic: the camera looks where the mouse points and the crosshair stays in the middle.
    // Free aim (bodycam style): the mouse moves the aim (and crosshair) inside an area of the screen; the camera only turns once the aim reaches the edge of that area.
    look(dx, dy) {
      if (this.ui) return this.uiMove(dx, dy); const T = this.T, aw = this.aimW(), s = 0.0022 * (+T.get("player.mouseSens") || 1) * (aw && aw.type === "sights" ? 1 - (this.aimK || 0) * (1 - (+T.get(aw.pre + "aimSens") || 1)) : 1);
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
      i = ((i % w.length) + w.length) % w.length; const changed = i !== this.wi, old = w[this.wi]; if (changed) this.aimSet(false);
      if (changed) { for (const a of this.atts) { a.cur = null; a.fire = null; a.loopAnim = null; a.t = 0; } if (this.sh && old) this.sh.stopOwner("player." + old); }
      this.wi = i; if (this.mode === "play" && this.T.get("player.weapon") !== i + 1) this.T.set("player.weapon", i + 1);
      if (changed && this.sh) { const att = this.project.player.attachments.find(a => a.id === w[i]); if (att && att.script) this.sh.trigger({ key: "player." + att.id, script: att.script }, "onEquip"); }
    }
    // Node Coder events: every non-weapon object hears them, and the weapon that is currently in hand
    scriptOwners() { const cur = this.curWeaponId(), out = this.project.player.attachments.filter(a => a.script && !(this.T.get("player." + a.id + ".type") === "weapon" && a.id !== cur)).map(a => ({ key: "player." + a.id, script: a.script })); if (this.project.player.script) out.push({ key: "player", script: this.project.player.script }); for (const z of (this.map && this.map.zones) || []) if (z.type === "custom" && z.script) out.push({ key: this.map.id + ".z." + z.id, script: z.script }); return out.concat(this.bossOwners()); }
    triggerAtt(id, type, arg) { const a = this.project.player.attachments.find(x => x.id === id); if (this.sh && a && a.script) this.sh.trigger({ key: "player." + a.id, script: a.script }, type, arg); }
    stopAnim(key) { const at = this.atts.find(a => "player." + a.id === key); if (!at) return; at.fire = null; at.loopAnim = null; at.t = 0; at.restart = true; }
    trigger(type, arg) {
      if (!this.sh) return; if (this.project.player.script) this.sh.trigger({ key: "player", script: this.project.player.script }, type, arg); const cur = this.curWeaponId();
      for (const a of this.project.player.attachments) { if (!a.script) continue; if (this.T.get("player." + a.id + ".type") === "weapon" && a.id !== cur) continue; this.sh.trigger({ key: "player." + a.id, script: a.script }, type, arg); }
      for (const z of (this.map && this.map.zones) || []) if (z.type === "custom" && z.script) this.sh.trigger({ key: this.map.id + ".z." + z.id, script: z.script }, type, arg);
      for (const ow of this.bossOwners()) this.sh.trigger(ow, type, arg);
    }
    // set a tag from a text value: "toggle", "+n" / "-n" adds, anything else is set (numbers, yes / no, text)
    applyVal(tag, raw) { const T = this.T; raw = String(raw === undefined ? "" : raw).trim(); if (!tag || !T.has(tag) || raw === "") return; const cur = T.get(tag), sp = T.spec(tag);
      if (/^toggle$/i.test(raw)) T.set(tag, sp && sp.type === "bool" ? !cur : !/^(1|true|on|yes)$/i.test(String(cur))); else if (/^[+-]\d+(\.\d+)?$/.test(raw) && typeof cur === "number") this.sh ? this.sh.setTag(tag, cur + parseFloat(raw)) : T.set(tag, cur + parseFloat(raw)); else if (this.sh) this.sh.setTag(tag, raw); else T.set(tag, raw); }
    // ---- zones (map editor > Zones): boxes of cells on a story. Trigger zones run their ready-made actions, custom zones run their own script (onEnter / onLeave / onInside).
    zoneIn(z) { const C = 128, cx = Math.floor(this.x / C), cy = Math.floor(this.y / C), st = Math.floor((this.z + 2) / C), T = this.T, k = this.map.id + ".z." + z.id + ".", ht = Math.max(0, +T.get(k + "height") || 0), L = z.layer || 0;
      return cx >= Math.min(z.x0, z.x1) && cx <= Math.max(z.x0, z.x1) && cy >= Math.min(z.y0, z.y1) && cy <= Math.max(z.y0, z.y1) && st >= L && st <= L + ht; }
    zoneStep(dt) {
      const m = this.map; if (!m || !m.zones || !m.zones.length || this.pdead) return; const T = this.T, st = this.zst = this.zst && this.zst.map === m ? this.zst : { map: m, z: {} };
      for (const z of m.zones) {
        const k = m.id + ".z." + z.id + ".", g = q => T.get(k + q), s = st.z[z.id] = st.z[z.id] || { in: false, done: false, t: 0, nt: {} }, need = g("needTag"), okc = !need || !this.sh || this.sh.test(need, g("needOp") || "=", g("needVal"));
        const inside = this.zoneIn(z) && okc && !(s.done && !s.in), owner = z.type === "custom" && z.script ? { key: m.id + ".z." + z.id, script: z.script } : null;
        if (inside && !s.in) { s.in = true; s.t = 0; s.nt = {}; if (g("once")) s.done = true;
          if (owner) { if (this.sh) this.sh.trigger(owner, "onEnter"); }
          else { this.applyVal(g("setTag"), g("setVal")); const ms = String(g("message") || "").trim(); if (ms) this.trigger("onMessage", ms); if (g("sound")) this.sfx(g("sound")); if (g("say")) this.toast(T.interpolate(String(g("say"))));
            const tp = String(g("teleport") || "").split(",").map(q => parseFloat(q)); if (tp.length >= 2 && isFinite(tp[0]) && isFinite(tp[1])) { this.x = (tp[0] + 0.5) * 128; this.y = (tp[1] + 0.5) * 128; const L = isFinite(tp[2]) ? tp[2] : 0; this.z = G ? G.groundY(m, this.x, this.y, L * G.LAYER + G.LAYER - 8) : L * 128; this.vz = 0; this.body = null; this.extV = null; }
            if (g("dialog")) this.talkGraph(g("dialog"), null); const sc = g("screen"); if (sc && this.o.onScreen && this.project.screens.some(x => x.id === sc)) { this.o.onScreen(sc); return; } } }
        else if (!inside && s.in) { s.in = false; if (owner) { if (this.sh) this.sh.trigger(owner, "onLeave"); } else { this.applyVal(g("leaveTag"), g("leaveVal")); const ms = String(g("leaveMessage") || "").trim(); if (ms) this.trigger("onMessage", ms); } }
        if (!s.in) continue; s.t += dt;
        if (owner && this.sh) for (const n of z.script.nodes) { if (n.type !== "onInside") continue; const iv = Math.max(0.05, +T.get(k + "s." + n.id + ".interval") || 1); s.nt[n.id] = (s.nt[n.id] || 0) + dt; if (s.nt[n.id] >= iv) { s.nt[n.id] -= iv; this.sh.trigger(owner, "onInside", null, n.id); } }
        const hu = owner ? 0 : +g("hurt") || 0; if (hu) { s.ht = (s.ht || 0) + dt; if (s.ht >= 0.5) { s.ht -= 0.5; if (hu > 0) this.hurtPlayer(hu * 0.5); else T.set("player.health", Math.min(+T.get("player.maxHealth") || 100, (+T.get("player.health") || 0) - hu * 0.5)); } }
      }
    }
    vol3(mult) { const T = this.T; return Math.max(0, Math.min(1, (+T.get("audio.master") || 0) * (+T.get("audio.sfx") || 0) * (isNaN(mult) ? 1 : mult))); }
    loopSfx(id, vol) { const u = this.urls[id]; if (!u) return; this.loops = this.loops || {}; const v = this.vol3(vol); if (this.loops[id] && this.loops[id].playing) { this.loops[id].setVolume(v); return; } this.loops[id] = PRIM.Audio.play(u, v, true); }
    stopLoops(id) { const L = this.loops || {}; for (const k of Object.keys(L)) if (!id || k === id) { L[k].stop(0.03); delete L[k]; } }
    sfx(id, vol) { const u = this.urls[id]; return u ? PRIM.Audio.play(u, this.vol3(vol), false) : null; }
    // a sound at a place in the world: heard from its direction, fading to silence at `max` units
    sfxAt(id, vol, p, max, loop) { const u = this.urls[id]; return u ? PRIM.Audio.play(u, this.vol3(vol), !!loop, { p, ref: Math.min(96, (max || 2500) * 0.2), max: max || 2500 }) : null; }
    thingPos(o, up) { const f = o.fr || {}; return [o.x !== undefined && o.type === "enemy" ? o.x : f.cx, (o.floor !== undefined ? o.floor : f.fy || 0) + (up === undefined ? 56 : up), o.y !== undefined && o.type === "enemy" ? o.y : f.cy]; }
    // speakers: a looping sound placed in the world (or a one-shot each time you walk into its radius)
    speakerStep() {
      const T = this.T; this.spk = this.spk || {}; const live = new Set();
      for (const o of this.things || []) {
        if (o.type !== "speaker") continue; const id = o.th.id, snd = this.ov(o, "spkSound"), r = +this.ov(o, "spkRadius") || 512, vol = this.ov(o, "spkVolume") === undefined ? 1 : +this.ov(o, "spkVolume"), p = this.thingPos(o, +this.ov(o, "spkHeight") || 0), d = Math.hypot(p[0] - this.x, p[2] - this.y, p[1] - this.z - 56);
        let k = this.spk[id]; if (k && k.snd !== snd) { if (k.h) k.h.stop(0.1); k = this.spk[id] = null; } if (!snd) continue; live.add(id);
        if (this.ov(o, "spkLoop") !== false) { if (!k) k = this.spk[id] = { snd, h: this.sfxAt(snd, vol, p, r, true) }; else if (k.h) { k.h.setVolume(this.vol3(vol)); k.h.setPos(p); } }
        else { if (!k) k = this.spk[id] = { snd, h: null, inside: false }; const inside = d < r; if (inside && !k.inside) k.h = this.sfxAt(snd, vol, p, r); k.inside = inside; }
      }
      for (const id of Object.keys(this.spk)) if (!live.has(id)) { const k = this.spk[id]; if (k && k.h) k.h.stop(0.1); delete this.spk[id]; }
    }
    stopSpeakers() { for (const k of Object.values(this.spk || {})) if (k && k.h) k.h.stop(0.05); this.spk = {}; }
    // where a player shot lands: splash damage around it and the explosion (particles or an animation), its flash and sound
    explode(pre, p, n, what, ch) {
      const T = this.T, v = this.view, g = k => +T.get(pre + k) || 0, n0 = n || [0, 1, 0], chg = this.isCharged(pre, ch);
      { const id = pre.split(".")[1], arg = (what === "enemy" ? "enemy" : what === "end" ? "end" : "level") + "|" + (chg ? 1 : 0); this.triggerAtt(id, "onImpact", arg); if (this.sh && this.project.player.script) this.sh.trigger({ key: "player", script: this.project.player.script }, "onImpact", arg); }
      if (!p) return; const growR = 1 + (Math.max(1, g("splashCharge") || 1) - 1) * Math.max(0, Math.min(1, ch || 0));
      if (T.get(pre + "projSplash")) {
        const R = (g("splashRadius") || 160) * growR, dmg = T.get(pre + "splashDamage") === undefined ? 50 : g("splashDamage"), from = [p[0] + n0[0] * 2, p[1] + n0[1] * 2, p[2] + n0[2] * 2];
        const clear = q => { const d = M4.sub(q, from), L = M4.len(d); if (L < 1) return true; const hh = v && v.raycast(from, d.map(z => z / L), L - 2); return !hh; };
        for (const o of this.things || []) { if (!this.eAlive(o)) continue; const r = +this.ov(o, "radius") || 22, hgt = +this.ov(o, "height") || 110, cy = Math.max(o.floor, Math.min(o.floor + hgt, p[1])), dist = Math.max(0, Math.hypot(o.x - p[0], o.y - p[2]) - r), d = Math.hypot(dist, cy - p[1]);
          if (d < R && clear([o.x, cy, o.y])) this.damageEnemy(o, dmg * (1 - 0.75 * d / R)); }
        if (T.get(pre + "splashSelf") && !this.pdead) { const r = +T.get("player.radius") || 16, cy = Math.max(this.z, Math.min(this.z + 102, p[1])), d = Math.hypot(Math.max(0, Math.hypot(this.x - p[0], this.y - p[2]) - r), cy - p[1]); if (d < R && clear([this.x, cy, this.y])) this.hurtPlayer(dmg * (1 - 0.75 * d / R)); }
      }
      if (T.get(pre + "projSplash") && T.get(pre + "splashSphere")) { this.fx = this.fx || []; this.fx.push({ kind: "shock", deco: true, p: p.slice(), R: (g("splashRadius") || 160) * growR, life: g("splashSphereTime") || 0.45, t: 0, c1: this.hexCol(T.get(pre + "splashColor1") || "#ffe08a"), c2: this.hexCol(T.get(pre + "splashColor2") || "#ff4a1a"), anim: T.get(pre + "splashAnim") || "none", aspd: T.get(pre + "splashAnimSpeed") === undefined ? 2 : g("splashAnimSpeed"), glow: T.get(pre + "splashGlow") === undefined ? 1.4 : g("splashGlow") }); }
      const kind = T.get(pre + "explodeType"), snd = (chg && T.get(pre + "explodeSoundCharged")) || T.get(pre + "explodeSound"); if (snd) this.sfxAt(snd, 1, p);
      if (!kind || kind === "none") return; const size = kind === "particles" ? g("explodeBit") || 4 : g("explodeSize") || 96, hex = String(T.get(pre + "explodeColor") || "#ff9933"), col = this.hexCol(hex); this.fx = this.fx || [];
      if (kind === "particles") { const want = Math.min(200, Math.max(1, Math.round(g("explodeCount") || 24))), room = Math.max(0, (this.fxBudget || 1500) - (this.decoN || 0)), cnt = Math.min(want, room > 600 ? want : Math.ceil(want * room / 600)); this.decoN = (this.decoN || 0) + cnt; const sp = g("explodeSpeed") || 320, life = g("explodeTime") || 0.6;   // particle budget: a busy screen gets fewer bits per explosion, never more than the budget
        for (let i = 0; i < cnt; i++) { let d = [Math.random() * 2 - 1, Math.random() * 2 - 1, Math.random() * 2 - 1]; const L = M4.len(d) || 1; d = d.map(z => z / L); if (M4.dot(d, n0) < 0) d = d.map((z, j) => z - 2 * M4.dot(d, n0) * n0[j]);
          this.fx.push({ kind: "sphere", deco: true, flat: T.get(pre + "projShade") === "flat", p: [p[0] + n0[0] * 3, p[1] + n0[1] * 3, p[2] + n0[2] * 3], d, c: col.map(q => q * 1.6), size0: size * (0.6 + Math.random() * 0.8), size, speed: sp * (0.4 + Math.random() * 0.8), grav: 500, life: life * (0.6 + Math.random() * 0.6), t: 0 }); } }
      else if (T.get(pre + "explodeImage")) { const fr = Math.max(1, Math.round(g("explodeFrames") || 8)), fps = g("explodeFps") || 20;
        (this.marks = this.marks || []).push({ kind: "decal", bill: true, texId: T.get(pre + "explodeImage"), p: [p[0] + n0[0] * size * 0.3, p[1] + n0[1] * size * 0.3, p[2] + n0[2] * size * 0.3], n: [0, 0, 1], size, frames: fr, fps, stay: 0, t: 0, frame: 0, lit: 0, glow: 1 }); }
      if (T.get(pre + "explodeLight") !== false) { const life = kind === "particles" ? Math.min(0.5, g("explodeTime") || 0.6) : Math.min(0.6, Math.max(1, Math.round(g("explodeFrames") || 8)) / (g("explodeFps") || 20)); this.fx.push({ kind: "flash", deco: true, p: [p[0] + n0[0] * 16, p[1] + n0[1] * 16, p[2] + n0[2] * 16], d: [0, 0, 0], speed: 0, life, t: 0, light: { shadow: false, c: kind === "particles" ? col : [1, 0.75, 0.45], k: 3, k0: 3, r: Math.max(200, (T.get(pre + "projSplash") ? g("splashRadius") * growR : 0) * 2, size * 3) } }); }
    }
    playAnim(key, name, loop) { const at = this.atts.find(a => "player." + a.id === key); if (!at) return; if (!name) { if (loop) at.loopAnim = null; return; } if (!at.model.anims[name]) return; if (loop) at.loopAnim = name; else at.fire = name; at.t = 0; at.restart = true; }
    // smooth changes between animations: when the animation of a model changes (or one restarts), the pose eases from what was shown
    // into the new animation over player.animBlend seconds (0 = instant). h = anything that keeps the state (a weapon, a placed object)
    mixPose(h, model, name, time, dt) {
      const F = PRIM.FBX, b = Math.max(0, +this.T.get("player.animBlend") || 0), cur = F.local(model, name, time);
      if (h.mModel !== model) { h.mModel = model; h.mName = name; h.mSnap = null; }
      else if ((name !== h.mName || h.restart) && b > 0 && h.mLast) { h.mSnap = h.mLast; h.mK = 0; }
      h.mName = name; h.restart = false; let shown = cur;
      if (h.mSnap && b > 0) { h.mK += dt / b; if (h.mK >= 1) h.mSnap = null; else { const w = h.mK * h.mK * (3 - 2 * h.mK); shown = F.mixLocal(h.mSnap, cur, w); } }
      h.mLast = shown; return F.world(model, shown);
    }
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
        const pre = "player." + at.id + "."; if (!T.get(pre + "visible")) continue; if (at.type === "weapon" && at !== this.curWeapon()) continue; const g = k => +T.get(pre + k) || 0, name = this.advance(at, dt), pose = this.mixPose(at, at.model, name || null, at.t * (name && at.model.anims[name] ? this.animLen(at.aid, name, at.model.anims[name].dur).speed : 1), dt);
        const ak = at.type === "weapon" && this.aimK > 0 && this.aimW() && this.aimW().type === "sights" ? this.aimK : 0; if (ak > 0.9 && T.get(pre + "aimOverlay")) continue;   // a scope picture covers the screen: no weapon model
        const vo = (this.mode === "play" && this.vmOff ? this.vmOff : [0, 0, 0]).map(q => q * (1 - 0.8 * ak)), off = [g("x") + vo[0] + g("adsX") * ak, g("y") + vo[1] + g("adsY") * ak, g("z") + vo[2] + g("adsZ") * ak], Rb = M4.rotXYZ([g("rx"), g("ry"), g("rz")]), sc = g("scale") || 1, Sm = M4.scale(sc, sc, sc); let Ra = M4.ident();
        if (at.type === "weapon") {
          const ay = g("arrowYaw") * Math.PI / 180, ap = g("arrowPitch") * Math.PI / 180, arrow = [-Math.cos(ap) * Math.sin(ay), Math.sin(ap), -Math.cos(ap) * Math.cos(ay)], mz = [(g("muzzleX") - g("pivotX")) * sc, (g("muzzleY") - g("pivotY")) * sc, (g("muzzleZ") - g("pivotZ")) * sc];
          const hit = v.raycast(eye, this.aimDir(), 6000), wp = hit ? hit.p : M4.add(eye, this.aimDir().map(q => q * 3000)), d0 = M4.sub(wp, eye), Hc = [M4.dot(d0, B.s), M4.dot(d0, B.u), -M4.dot(d0, B.f)];
          const a0 = M4.norm(M4.dir(Rb, ...arrow)), maxR = g("aimMax") * Math.PI / 180; let want = M4.norm(M4.sub(Hc, M4.add(off, M4.dir(Rb, ...mz))));
          Ra = M4.between(a0, want, maxR); want = M4.norm(M4.sub(Hc, M4.add(off, M4.dir(M4.mul(Ra, Rb), ...mz)))); at.cur = at.cur ? M4.norm(M4.add(at.cur.map(q => q * Math.exp(-14 * dt)), want.map(q => q * (1 - Math.exp(-14 * dt))))) : want; Ra = M4.between(a0, at.cur, maxR);
        } else at.cur = null;
        const M = M4.mul(M4.translate(...off), M4.mul(Ra, M4.mul(Rb, M4.mul(Sm, M4.translate(-g("pivotX"), -g("pivotY"), -g("pivotZ")))))); const mt = this.matGL(pre); items.push({ gm: at.gm, pose, M, mat: mt, tex: mt.texId ? v.texFor(mt.texId, this.urls) : null }); this.objLight(at, pre, M, g, sc, eye, B);
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
    matGL(pre) { const m = this.matOf(pre); if (m.emitTexId && this.view) m.emitTex = this.view.texFor(m.emitTexId, this.urls); return m; }
    // the light an object carries (Light tab), placed in the world for this frame (camera space -> world)
    objLight(at, pre, M, g, sc, eye, B) {
      const T = this.T, ty = T.get(pre + "lightType"); if (!ty || ty === "none") return; const w = q => [eye[0] + B.s[0] * q[0] + B.u[0] * q[1] - B.f[0] * q[2], eye[1] + B.s[1] * q[0] + B.u[1] * q[1] - B.f[1] * q[2], eye[2] + B.s[2] * q[0] + B.u[2] * q[1] - B.f[2] * q[2]];
      const wd = q => M4.norm([B.s[0] * q[0] + B.u[0] * q[1] - B.f[0] * q[2], B.s[1] * q[0] + B.u[1] * q[1] - B.f[1] * q[2], B.s[2] * q[0] + B.u[2] * q[1] - B.f[2] * q[2]]);
      const fl = +T.get(pre + "lightFlicker") || 0, tm = performance.now() / 1000, k = (+T.get(pre + "lightIntensity") || 0) * (1 - fl * (0.5 + 0.5 * Math.sin(tm * 37 + Math.sin(tm * 11) * 5)) * 0.9), col = this.hexCol(T.get(pre + "lightColor") || "#ffe2b0");
      const L = { p: w(M4.point(M, g("lightX"), g("lightY"), g("lightZ"))), r: +T.get(pre + "lightRange") || 300, c: col.map(q => q * k), type: 0, shadow: false };   // a light you carry sits at the camera: its shadows would hardly show, so it casts none
      if (ty === "area") { const yw = g("lightYaw") * Math.PI / 180, pt = g("lightPitch") * Math.PI / 180, n = M4.norm(M4.dir(M, -Math.cos(pt) * Math.sin(yw), Math.sin(pt), -Math.cos(pt) * Math.cos(yw))), ax = M4.norm(M4.cross(n, Math.abs(n[1]) > 0.95 ? [1, 0, 0] : [0, 1, 0]));
        L.type = 1; L.n = wd(n); L.u = wd(ax); L.hw = (+T.get(pre + "lightWidth") || 40) * sc / 2; L.hh = (+T.get(pre + "lightHeight") || 20) * sc / 2; }
      (this.lightNow = this.lightNow || []).push(L);
    }
    // Compositor effects set "in front of the HUD": overlays (vignette, grain, letterbox, CRT lines + mask) become page layers above the HUD and the
    // crosshair; picture effects (colour grading, fewer colours, sharpen, chromatic aberration, pixelate, night vision) reach the HUD through an SVG filter.
    // The 3D picture gets the picture effects from the renderer (render3d present()), which skips the overlays here (view.domFx).
    compDom() {
      const v = this.view; if (!v || this.mode !== "play") return; v.domFx = true; const Sc = PRIM.Schema;
      const list = Sc && Sc.compActive ? Sc.compActive(this.project, this.T).filter(f => f.layer === "front") : [], sig = JSON.stringify(list);
      if (sig !== this.compSig) { this.compSig = sig; this.buildCompDom(list); }
      for (const g of this.grainEls || []) g.style.backgroundPosition = Math.floor(Math.random() * 256) + "px " + Math.floor(Math.random() * 256) + "px";
    }
    buildCompDom(list) {
      if (!this.fxo) { this.fxo = document.createElement("div"); this.fxo.style.cssText = "position:absolute;inset:0;pointer-events:none;overflow:hidden"; this.xh.after(this.fxo); }
      this.fxo.replaceChildren(); this.grainEls = []; const ns = "http://www.w3.org/2000/svg", prims = [], id = "pcf" + Math.random().toString(36).slice(2, 8); let n = 0, last = "SourceGraphic";
      const rgba = (c, a) => "rgba(" + this.hexCol(c || "#000000").map(q => Math.round(q * 255)).join(",") + "," + a + ")", num = (f, k, d) => { const x = f.v[k]; return x && x.val !== undefined && x.val !== "" && !isNaN(+x.val) ? +x.val : d; };
      const div = css => { const d = document.createElement("div"); d.style.cssText = "position:absolute;inset:0;pointer-events:none;" + css; this.fxo.append(d); return d; }, R = () => "r" + (++n), F = x => (+x).toFixed(4);
      for (const f of list) {
        const V = k => f.v[k] ? f.v[k].val : undefined;
        if (f.type === "vignette") { const st = num(f, "strength", 0.5), sz = num(f, "size", 0.75), so = num(f, "soft", 0.45); div("background:radial-gradient(circle farthest-corner at 50% 50%," + rgba(V("color"), 0) + " " + Math.max(0, sz - so) * 93 + "%," + rgba(V("color"), st) + " " + sz * 93 + "%)"); }
        else if (f.type === "letterbox") { const h = num(f, "size", 0.12) * 100; div("bottom:auto;height:" + h + "%;background:" + rgba(V("color"), 1)); div("top:auto;height:" + h + "%;background:" + rgba(V("color"), 1)); }
        else if (f.type === "grain") {
          const col = !!V("color"), key = col ? "c" : "m"; this.noise = this.noise || {};
          if (!this.noise[key]) { const c = document.createElement("canvas"); c.width = c.height = 128; const x = c.getContext("2d"), im = x.createImageData(128, 128); for (let i = 0; i < im.data.length; i += 4) { const g = Math.random() * 255; im.data[i] = col ? Math.random() * 255 : g; im.data[i + 1] = col ? Math.random() * 255 : g; im.data[i + 2] = col ? Math.random() * 255 : g; im.data[i + 3] = 255; } x.putImageData(im, 0, 0); this.noise[key] = c.toDataURL(); }
          this.grainEls.push(div("background-image:url(" + this.noise[key] + ");background-size:" + Math.round(128 * num(f, "size", 1.5)) + "px;image-rendering:pixelated;mix-blend-mode:overlay;opacity:" + Math.min(1, num(f, "amount", 0.08) * 4)));
        }
        else if (f.type === "crt") { const L = num(f, "lines", 0.35), per = 100 / Math.max(1, num(f, "count", 360)), M = num(f, "mask", 0.2);
          if (L > 0) div("background:repeating-linear-gradient(to bottom,rgba(0,0,0,0) 0%,rgba(0,0,0," + L + ") " + F(per / 2) + "%,rgba(0,0,0,0) " + F(per) + "%)");
          if (M > 0) div("background:repeating-linear-gradient(to right,#ff4040 0 1px,#40ff40 1px 2px,#4040ff 2px 3px);mix-blend-mode:multiply;opacity:" + Math.min(1, M * 0.6)); }
        else if (f.type === "grade") { const b = num(f, "brightness", 1), c = num(f, "contrast", 1), sat = num(f, "saturation", 1), g = num(f, "gamma", 1), w = num(f, "warmth", 0), t = this.hexCol(V("tint") || "#ffffff"), r1 = R(), r2 = R(), r3 = R();
          prims.push('<feColorMatrix in="' + last + '" type="saturate" values="' + F(sat) + '" result="' + r1 + '"/>', '<feComponentTransfer in="' + r1 + '" result="' + r2 + '">' + [0, 1, 2].map(i => '<feFunc' + "RGB"[i] + ' type="linear" slope="' + F(b * t[i] * c) + '" intercept="' + F(0.5 - 0.5 * c + [0.04, 0.01, -0.04][i] * w) + '"/>').join("") + '</feComponentTransfer>',
            '<feComponentTransfer in="' + r2 + '" result="' + r3 + '">' + "RGB".split("").map(ch => '<feFunc' + ch + ' type="gamma" amplitude="1" exponent="' + F(1 / Math.max(0.01, g)) + '" offset="0"/>').join("") + '</feComponentTransfer>'); last = r3; }
        else if (f.type === "posterize") { const L = Math.max(2, Math.round(num(f, "levels", 16))), tv = Array.from({ length: L }, (_, i) => F(i / (L - 1))).join(" "), r = R(); prims.push('<feComponentTransfer in="' + last + '" result="' + r + '">' + "RGB".split("").map(ch => '<feFunc' + ch + ' type="discrete" tableValues="' + tv + '"/>').join("") + '</feComponentTransfer>'); last = r; }
        else if (f.type === "sharpen") { const a = num(f, "amount", 0.4), r = R(); prims.push('<feConvolveMatrix in="' + last + '" order="3" preserveAlpha="true" kernelMatrix="0 ' + F(-a) + ' 0 ' + F(-a) + ' ' + F(1 + 4 * a) + ' ' + F(-a) + ' 0 ' + F(-a) + ' 0" result="' + r + '"/>'); last = r; }
        else if (f.type === "chroma") { const a = num(f, "amount", 2), o1 = R(), cr = R(), cg = R(), o2 = R(), cb = R(), x = R(), r = R(), M = (ri, gi, bi) => [ri, gi, bi].map(k => [0, 1, 2, 3].map(j => j === k ? 1 : 0).concat(0).join(" ")).join(" ") + " 0 0 0 1 0";
          prims.push('<feOffset in="' + last + '" dx="' + F(a) + '" dy="0" result="' + o1 + '"/>', '<feColorMatrix in="' + o1 + '" type="matrix" values="1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0" result="' + cr + '"/>', '<feColorMatrix in="' + last + '" type="matrix" values="0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 1 0" result="' + cg + '"/>',
            '<feOffset in="' + last + '" dx="' + F(-a) + '" dy="0" result="' + o2 + '"/>', '<feColorMatrix in="' + o2 + '" type="matrix" values="0 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 1 0" result="' + cb + '"/>', '<feBlend in="' + cr + '" in2="' + cg + '" mode="screen" result="' + x + '"/>', '<feBlend in="' + x + '" in2="' + cb + '" mode="screen" result="' + r + '"/>'); last = r; }
        else if (f.type === "pixelate") { const z = Math.max(1, Math.round(num(f, "size", 3))), fl = R(), cp = R(), tl = R(), pk = R(), r = R();
          prims.push('<feFlood x="' + Math.floor(z / 2) + '" y="' + Math.floor(z / 2) + '" width="1" height="1" flood-color="#000" result="' + fl + '"/>', '<feComposite in="' + fl + '" x="0" y="0" width="' + z + '" height="' + z + '" result="' + cp + '"/>', '<feTile in="' + cp + '" result="' + tl + '"/>', '<feComposite in="' + last + '" in2="' + tl + '" operator="in" result="' + pk + '"/>', '<feMorphology in="' + pk + '" operator="dilate" radius="' + F(z / 2) + '" result="' + r + '"/>'); last = r; }
        else if (f.type === "night") { const st = num(f, "strength", 1), gn = num(f, "gain", 2.5), c = this.hexCol(V("color") || "#5cff7a"), L = [0.299, 0.587, 0.114], r = R(), row = i => [0, 1, 2].map(j => F((1 - st) * (i === j ? 1 : 0) + st * c[i] * gn * L[j])).join(" ") + " 0 0";
          prims.push('<feColorMatrix in="' + last + '" type="matrix" values="' + row(0) + " " + row(1) + " " + row(2) + ' 0 0 0 1 0" result="' + r + '"/>'); last = r; }
      }
      const filt = prims.length ? "url(#" + id + ")" : ""; this.hudHost.style.filter = filt; this.xh.style.filter = filt;
      if (prims.length) { const svg = document.createElementNS(ns, "svg"); svg.setAttribute("width", "0"); svg.setAttribute("height", "0"); svg.style.position = "absolute";
        svg.innerHTML = '<filter id="' + id + '" x="-20%" y="-20%" width="140%" height="140%" primitiveUnits="userSpaceOnUse" color-interpolation-filters="sRGB">' + prims.join("") + '</filter>'; this.fxo.append(svg); }
    }
    // small performance readout (shown with "Show FPS counter")
    stats3d(v) {
      if (!this.statEl) { this.statEl = document.createElement("div"); this.statEl.style.cssText = "position:absolute;left:14px;top:46px;font:13px monospace;color:#7df3ff;text-shadow:0 1px 2px #000;pointer-events:none;white-space:pre;z-index:5"; if (v.canvas.parentNode) v.canvas.parentNode.append(this.statEl); }
      const on = this.mode === "play" && this.T.get("gfx.showFps") && v.stats; this.statEl.style.display = on ? "block" : "none"; if (!on) return; const s = v.stats;
      if (!this.statN || performance.now() - this.statN > 400) { this.statN = performance.now(); this.statEl.textContent = "triangles " + Math.round(s.tris) + " / " + Math.round(s.totalTris) + "\nchunks " + s.chunks + " / " + s.totalChunks + "\nlights " + s.lights + "\nLOD level " + (v.lodLevel || 0) + "\nparticles " + ((v.fxStats && v.fxStats.balls) || 0) + " drawn, " + ((v.fxStats && v.fxStats.culled) || 0) + " skipped (off screen)" + (v.inst ? "" : "  [no instancing]") + "\nshots / bits alive " + (this.fx || []).length; }
    }
    // ---- walking: a capsule body (grid.js) slides along walls, climbs steps and stairs, walks slopes; enemies push it away
    push(p, r) { for (const e of this.things || []) { const npc = e.type === "npc" && !e.taken; if (!(this.eAlive(e) || npc) || Math.abs(e.floor - this.z) > 100) continue; const er = +this.T.get(e.pre + "radius") || 22, dx = p.x - (npc ? e.fr.cx : e.x), dy = p.y - (npc ? e.fr.cy : e.y), d = Math.hypot(dx, dy); if (d < r + er && d > 1e-4) { p.x += dx / d * (r + er - d); p.y += dy / d * (r + er - d); } } }
    step(dt) {
      const T = this.T, k = this.keys, g = +T.get("player.gravity"), r = +T.get("player.radius") || 16, head = Math.min((+T.get("player.eyeHeight") || 102) + 8, G.LAYER - 6), stepH = +T.get("player.stepHeight") || 24;
      let fw = (k.w || k.arrowup ? 1 : 0) - (k.s || k.arrowdown ? 1 : 0), sd = (k.d ? 1 : 0) - (k.a ? 1 : 0); if (k.arrowleft) this.yaw -= 2 * dt; if (k.arrowright) this.yaw += 2 * dt;
      const len = Math.hypot(fw, sd) || 1; fw /= len; sd /= len; let spd = +T.get("player.speed"); if (T.get("player.sprintEnabled") && k.shift && fw > 0 && !(this.aimK > 0.5)) spd *= 1 + (+T.get("player.sprintPercent") || 0) / 100; { const aw = this.aimW(); if (aw && this.aimK > 0) spd *= 1 - this.aimK * (1 - (T.get(aw.pre + "aimMove") === undefined ? 1 : +T.get(aw.pre + "aimMove"))); }
      const cy = Math.cos(this.yaw), sy = Math.sin(this.yaw), b = this.body || (this.body = { pos: [this.x, this.z, this.y], vel: [0, 0, 0], ground: true });
      Object.assign(b, { r, h: head, step: stepH, maxSlope: 0.69 }); b.vel[0] = (cy * fw - sy * sd) * spd; b.vel[2] = (sy * fw + cy * sd) * spd;
      const was = this.ground; if (T.get("player.jumpEnabled") && k.space && was && this.jumpReq === undefined) this.jumpReq = -1;
      if (this.jumpReq !== undefined) { const hj = this.jumpReq > 0 ? this.jumpReq : +T.get("player.jumpHeight") || 0; this.jumpReq = undefined; b.vel[1] = Math.sqrt(2 * g * hj); b.ground = false; this.psound("jumpSound"); this.trigger("onJump"); }
      if (this.upReq) { b.vel[1] = Math.max(b.vel[1], 0) + this.upReq; this.upReq = 0; b.ground = false; }
      if (this.extV) { b.vel[0] += this.extV[0]; b.vel[2] += this.extV[1]; const f = Math.exp(-dt * (was ? 7 : 1.2)); this.extV = [this.extV[0] * f, this.extV[1] * f]; if (Math.hypot(...this.extV) < 5) this.extV = null; }
      b.vel[1] -= g * dt; const vy = b.vel[1], x0 = b.pos[0], z0 = b.pos[2], world = this.view && this.view.world, dtr = this.doorTris();
      G.move(b, dt, world, dtr); const p = { x: b.pos[0], y: b.pos[2] }; this.push(p, r); this.shove(b, p.x - b.pos[0], p.y - b.pos[2], world, dtr);
      this.mv = Math.hypot(b.pos[0] - x0, b.pos[2] - z0); this.sprinting = !!(T.get("player.sprintEnabled") && k.shift && fw > 0); this.spdNow = spd;
      if (b.ground && was) { this.stepAcc = (this.stepAcc || 0) + this.mv; const sd2 = Math.max(10, +T.get("player.stepDistance") || 90); if (this.stepAcc >= sd2) { this.stepAcc %= sd2; this.footstep(); } }
      if (b.ground && !was && vy < -250) { this.psound("landSound"); this.landT = performance.now(); this.stepAcc = 0; }
      if (b.ground && !was) { this.airJumps = 0; this.trigger("onLand"); }
      this.x = b.pos[0]; this.y = b.pos[2]; this.z = b.pos[1]; this.ground = b.ground; this.vz = b.vel[1];
      if (this.z < -4000 && !this.pdead) this.hurtPlayer(1e9);   // fell out of the world
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
      f.bt += dt * (+T.get("player.breatheSpeed") || 0.25) * Math.PI * 2; if (T.get("player.bobSync")) f.ph = Math.PI * 2 * ((this.stepAcc || 0) / Math.max(10, +T.get("player.stepDistance") || 90)) + Math.PI;   // one bob per footstep, the step sound at the lowest point
      else f.ph += dt * (+T.get("player.bobSpeed") || 1.8) * Math.PI * 2 * (st === "running" ? 1.45 : 1);
      const ba = T.get("player.breathe") ? (+T.get("player.breatheAmount") || 0) * f.idle : 0, wa = T.get("player.bob") ? (+T.get("player.bobAmount") || 0) * f.walk * (st === "running" ? 1.4 : 1) : 0;
      this.vmOff = [Math.sin(f.ph * 0.5) * wa * 0.8, Math.sin(f.bt) * ba * 0.7 - Math.abs(Math.sin(f.ph * 0.5)) * wa, Math.sin(f.bt) * ba * 0.4];
      // Hud Movement: turning kicks the HUD the other way, then it eases back
      let dy = this.yaw - f.ly; dy = Math.atan2(Math.sin(dy), Math.cos(dy)); const dp = this.pitch - f.lp; f.ly = this.yaw; f.lp = this.pitch;
      if (T.get("player.hudMove")) { const A = +T.get("player.hudMoveAmount") || 1; f.hx = Math.max(-70 * A, Math.min(70 * A, f.hx - dy * 260 * A)); f.hy = Math.max(-50 * A, Math.min(50 * A, f.hy + dp * 260 * A)); const dk = Math.exp(-7 * dt); f.hx *= dk; f.hy *= dk; if (this.hud) this.hud.sway(f.hx, f.hy); }
      else if (f.hx || f.hy) { f.hx = f.hy = 0; if (this.hud) this.hud.sway(0, 0); }
    }
    frame(t) {
      if (this.dead) return;
      // draw order (play): level, then solid things (doors, enemies) BEFORE the effects. See-through effects (shock spheres, marks) write no depth, so anything drawn after them covered them.
      // removed from the page (the playtest was stopped, another project was opened...): shut down for good, so no copy keeps listening to keys and playing sounds
      if (this.root.isConnected) this.wasOn = true; else if (this.wasOn) { this.destroy(); return; }
      this.raf = requestAnimationFrame(q => this.frame(q)); const dt = this.popup || this.paused ? 0 : Math.max(0, Math.min(0.05, (t - this.last) / 1000)); this.last = Math.max(t, this.last || 0);   // never a negative step (gravity would run backwards)   // a pick-up pop-up pauses everything
      if (this.mode === "play" && this.map && !this.pdead && !this.talk && !this.popup && !this.paused) { this.step(dt); this.zoneStep(dt); }
      if (this.mode === "play") this.talkHint();
      if (this.mode === "play" && this.hud && this.hud.wants && this.hud.wants()) { this.clock = (this.clock || 0) + dt; this.hud.tick(this.hudWorld(), dt); }
      if (this.mode === "play") this.feel(dt); if (this.mode === "play" && this.view) this.aimStep(this.paused ? 0 : dt);
      if (this.mode === "play") { this.hold(dt); const hp = +this.T.get("player.health"); if (this.lastHp === undefined) this.lastHp = hp; if (hp < this.lastHp) { this.psound(hp <= 0 ? "deathSound" : "hurtSound"); this.flash(Math.min(0.6, 0.15 + (this.lastHp - hp) / 60)); this.trigger(hp <= 0 ? "onDeath" : "onHurt"); } this.lastHp = hp; this.reloadStep(dt); this.ammoClamp(); this.deathStep(dt); }
      const v = this.view; if (!v) return; const T = this.T, c = v.cam; v.fov = (+T.get("player.fov") || 80) * Math.PI / 180; v.vmFov = v.fov; /* the weapon in your hand keeps the normal view angle while the world zooms */ { const aw = this.aimW(); if (aw && aw.type === "sights" && this.aimK > 0) { const z = 1 + ((+T.get(aw.pre + "aimZoom") || 1) - 1) * this.aimK; v.fov = 2 * Math.atan(Math.tan(v.fov / 2) / z); } }
      c.x = this.x; c.z = this.y; c.y = this.z + (+T.get("player.eyeHeight") || 56) - (this.pdead ? Math.min(1, (this.deadT || 0) * 2) * (+T.get("player.eyeHeight") || 56) * 0.7 : 0); c.yaw = this.yaw; c.pitch = this.pitch; if (this.shakeT > 0) { this.shakeT -= dt; const a = (this.shakeA || 0) * Math.max(0, this.shakeT / (this.shakeT0 || 1)) * 0.02; c.yaw += (Math.random() * 2 - 1) * a; c.pitch += (Math.random() * 2 - 1) * a; if (this.shakeT <= 0) this.shakeA = 0; } { const B = R3D.camBasis(c.yaw, c.pitch); PRIM.Audio.listen([c.x, c.y, c.z], B.f, B.u, B.s); } if (this.mode === "play") this.speakerStep(); v.setLights(this.lightPrev || []); v.draw(); this.lightNow = []; if (this.mode === "play") { this.stepFx(dt); const list = (this.fx || []).slice(), ob = this.orb, oat = ob && this.atts.find(x => x.id === ob.id); for (const f of list) if (f.light) this.lightNow.push({ p: f.p, r: f.light.r, c: f.light.c.map(q => q * f.light.k), type: 0, shadow: f.light.shadow !== false && !f.hostile }); if (oat && oat.emit) { if (ob.grow) { ob.grow.t += dt; ob.size = ob.grow.from + (ob.grow.to - ob.grow.from) * Math.min(1, ob.grow.t / ob.grow.secs); } const op = this.emitWorld(oat).O, opre = "player." + oat.id + ".", oem = T.get(opre + "projEmit") === undefined ? 1 : +T.get(opre + "projEmit"); const oflat = T.get(opre + "projShade") === "flat", ocol = this.hexCol(ob.color).map(q => q * oem); list.push({ kind: "sphere", p: op, c: ocol, size: ob.size, flat: oflat });
        if (ob.suck) { const S = ob.suck, pc = S.color ? this.hexCol(S.color).map(q => q * oem) : ocol; ob.sp = ob.sp || []; ob.acc = (ob.acc || 0) + dt * S.rate;   // bits spawn on a ball around the charge and speed up into it
          while (ob.acc >= 1 && ob.sp.length < 160) { ob.acc -= 1; let d = [Math.random() * 2 - 1, Math.random() * 2 - 1, Math.random() * 2 - 1]; const L = Math.hypot(...d) || 1, R = S.radius * (0.65 + 0.35 * Math.random()) + ob.size; ob.sp.push({ off: d.map(q => q / L * R), t: 0, T: S.time * (0.7 + 0.6 * Math.random()), s: S.size * (0.6 + 0.8 * Math.random()) }); } if (ob.acc > 1) ob.acc = 1;
          ob.sp = ob.sp.filter(q => (q.t += dt) < q.T); for (const q of ob.sp) { const a = q.t / q.T, k = 1 - a * a; list.push({ kind: "sphere", p: [op[0] + q.off[0] * k, op[1] + q.off[1] * k, op[2] + q.off[2] * k], c: pc.map(v => v * (0.55 + 0.9 * a)), size: q.s * (0.35 + 0.65 * Math.min(1, a * 3)), flat: oflat }); } } if (T.get(opre + "projLight")) this.lightNow.push({ p: op, r: +T.get(opre + "projLightRange") || 220, c: this.hexCol(ob.color).map(q => q * (+T.get(opre + "projLightPower") || 1.5)), type: 0, shadow: true }); } v.setLights(this.lightNow); list.push(...this.stepMarks(dt), ...this.hbLines()); this.uiStep(); this.drawThings(v, dt); this.shieldItems(list, dt); if (list.length) v.drawFx(list); } else this.drawThings(v, dt); this.drawHand(v, dt); v.present(); this.lightPrev = this.lightNow; this.compDom(); this.stats3d(v); this.placeCross(v);
    }
  };
})();
