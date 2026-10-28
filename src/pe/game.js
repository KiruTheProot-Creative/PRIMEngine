/* PRIMEngine (Prime edition) game runtime.
   First person in real 3D rooms: walking on slopes and steps, jumping, lock-on (hold) with circle strafing and side dashes,
   four visors (combat, scan with logbook, thermal, x-ray), beams with charge shots, missiles and charge combos, upgrade
   pick-ups, doors locked to a beam or missiles, enemies with weaknesses, energy tanks, death and restart.
   new PRIM.PEGame({ host, project, tags, urls, start: { pos, yaw, pitch } (optional), onExit, say }) */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), D = PRIM.PE, W = PRIM.PEWorld, M4 = PRIM.M4, R3D = PRIM.R3D, SH = PRIM.PEShapes;
  const { sub, add, mul, dot, len, norm } = W.v, clamp = (v, a, b) => Math.max(a, Math.min(b, v)), hex = W.hex, DEG = Math.PI / 180;
  if (!document.getElementById("pe-game-style")) {
    const s = document.createElement("style"); s.id = "pe-game-style";
    s.textContent = `
.pg-root{position:absolute;inset:0;background:#000;overflow:hidden;font-family:"Share Tech Mono",ui-monospace,monospace;user-select:none}
.pg-root canvas.pg-cv{position:absolute;inset:0;width:100%;height:100%;display:block;outline:none;cursor:none}
.pg-hud{position:absolute;inset:0;pointer-events:none}
.pg-ov{position:absolute;inset:0;width:100%;height:100%;pointer-events:none}
.pg-msg{position:absolute;inset:0;display:grid;place-items:center;text-align:center;background:rgba(0,0,0,.5);color:#d8f6ff;font-size:16px;letter-spacing:.06em;cursor:pointer;line-height:1.9}
.pg-msg b{font-size:26px;letter-spacing:.2em;color:#7df3ff}
.pg-banner{position:absolute;left:50%;top:28%;transform:translateX(-50%);padding:12px 28px;border:1px solid #7df3ff;background:rgba(4,22,30,.82);color:#dffaff;font-size:20px;letter-spacing:.16em;text-align:center;pointer-events:none;opacity:0;transition:opacity .4s}
.pg-log{position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);width:min(620px,86vw);max-height:70vh;overflow:auto;border:1px solid #ffb04a;background:rgba(18,12,4,.9);color:#ffe9c9;padding:18px 22px;display:none;line-height:1.5;font-size:15px}
.pg-log h3{margin:0 0 4px;color:#ffc773;letter-spacing:.12em;font-size:18px}.pg-log small{color:#c79a62;letter-spacing:.1em}.pg-log img{max-width:100%;max-height:220px;display:block;margin:10px 0}.pg-log p{white-space:pre-wrap;margin:8px 0 0}
.pg-pause{position:absolute;inset:0;display:none;background:rgba(2,8,12,.86);color:#d8f6ff;padding:28px 36px;overflow:auto}
.pg-pause h2{margin:0 0 14px;letter-spacing:.3em;color:#7df3ff;font-size:22px}.pg-pause button{font:inherit;color:#d8f6ff;background:#0f2530;border:1px solid #2f6a7d;border-radius:3px;padding:6px 16px;margin:0 8px 8px 0;cursor:pointer}.pg-pause button:hover{background:#163746}
.pg-cols{display:grid;grid-template-columns:240px 1fr;gap:18px;margin-top:10px}.pg-cat{color:#7df3ff;margin:10px 0 4px;letter-spacing:.12em}.pg-ent{cursor:pointer;padding:3px 6px;color:#b8dde8}.pg-ent:hover,.pg-ent.on{background:#163746;color:#fff}
.pg-detail img{max-width:100%;max-height:260px;display:block;margin:8px 0}.pg-detail p{white-space:pre-wrap;line-height:1.5}
.pg-dead{position:absolute;inset:0;display:none;place-items:center;background:radial-gradient(ellipse at center,rgba(60,0,0,.35),rgba(0,0,0,.92));color:#ffb4a8;font-size:30px;letter-spacing:.3em}`;
    document.head.append(s);
  }
  const el = (tag, cls, txt) => { const e = document.createElement(tag); if (cls) e.className = cls; if (txt !== undefined) e.textContent = txt; return e; };
  const keyId = e => { const k = e.key; if (k === " ") return "space"; if (k === "Control") return "ctrl"; return k.length === 1 ? k.toLowerCase() : k.toLowerCase(); };
  const ITEM_TEXT = { energyTank: "Energy Tank acquired", missileExpansion: "Missile Expansion acquired", missileLauncher: "Missile Launcher acquired", charge: "Charge Beam acquired", combo: "Charge Combos acquired", energy: "", missiles: "", flag: "Item acquired" };

  PRIM.PEGame = class PEGame {
    constructor(o) {
      this.o = o; this.P = o.project; this.urls = o.urls || {}; const T = this.T = o.tags.fork(); this.keys = {}; this.dead = false; this.paused = false;
      const root = this.root = el("div", "pg-root"); this.cv = el("canvas", "pg-cv"); this.hudHost = el("div", "pg-hud"); this.ov = el("canvas", "pg-ov"); this.msg = el("div", "pg-msg"); this.banner = el("div", "pg-banner"); this.logEl = el("div", "pg-log"); this.pauseEl = el("div", "pg-pause"); this.deadEl = el("div", "pg-dead", "ENERGY DEPLETED");
      root.append(this.cv, this.hudHost, this.ov, this.banner, this.logEl, this.deadEl, this.pauseEl, this.msg); o.host.append(root);
      this.msg.innerHTML = "<div><b>CLICK TO PLAY</b><br>W A S D move · mouse look · Space jump<br>Hold right mouse: lock on (A / D circle, Space + A / D dash)<br>Left mouse fire (hold to charge) · F missile · 1-4 beams · Z X C V visors · Esc pause</div>";
      this.view = new R3D.View(this.cv); if (!this.view.ok) { this.msg.textContent = "WebGL could not start on this computer."; return; }
      this.view.fogD = +T.get("world.fogDistance") || 4000; this.view.sky = hex(T.get("world.sky"), "#0b1622"); this.view.fog = hex(T.get("world.fog"), "#0a121b");
      this.hud = new PRIM.HUD({ host: this.hudHost, project: this.P, tags: T, urls: this.urls, screenId: T.get("visor.combat.hud"), say: o.say });
      this.gpu = {}; this.models = {}; this.newGame();
      this.bind(); this.last = performance.now(); this.raf = requestAnimationFrame(t => this.frame(t));
    }
    destroy() { this.dead = true; cancelAnimationFrame(this.raf); if (this.unbind) this.unbind(); if (this.hud) this.hud.destroy(); if (this.chargeLoop) this.chargeLoop.stop(0.05); if (document.pointerLockElement === this.cv) document.exitPointerLock(); if (this.view && this.view.destroy) this.view.destroy(); this.root.remove(); }
    // ---------------------------------------------------------------- setup
    g(k) { return +this.T.get("player." + k) || 0; }
    newGame() {
      const T = this.T, P = this.P; this.inv = { beams: new Set(P.beams.filter(b => T.get(b.id + ".start")).map(b => b.id)), visors: new Set(D.VISORS.map(v => v[0]).filter(v => T.get("visor." + v + ".start"))), tanks: this.g("startTanks"), missilesMax: this.g("startMissiles"), charge: !!T.get("player.startCharge"), combo: !!T.get("player.startCombo"), flags: new Set() };
      if (!this.inv.beams.size && P.beams[0]) this.inv.beams.add(P.beams[0].id); this.inv.visors.add("combat");
      this.missiles = this.inv.missilesMax; this.hpMax = this.g("startEnergy") + this.inv.tanks * this.g("energyPerTank"); this.hp = this.hpMax; this.scanned = new Set(); this.collected = new Set();
      this.beam = [...this.inv.beams][0]; this.visor = "combat"; this.restart(true);
    }
    restart(first) {
      const T = this.T, st = this.P.rooms.flatMap(r => r.things).find(t => t.kind === "start"), s = !first || !this.o.start ? null : this.o.start;
      if (!first) { this.hp = this.hpMax; this.missiles = this.inv.missilesMax; }
      this.body = { pos: s ? s.pos.slice() : st ? st.p.slice() : [0, 0, 0], vel: [0, 0, 0], r: this.g("radius") || 22, h: this.g("height") || 150, step: this.g("step"), maxSlope: Math.cos((this.g("maxSlope") || 46) * DEG), ground: false };
      this.yaw = s ? s.yaw : st ? (st.yaw || 0) * DEG : 0; this.pitch = s ? s.pitch || 0 : 0; this.shots = []; this.fx = []; this.lock = null; this.dash = 0; this.charge = 0; this.fireHeld = 0; this.cool = 0; this.scanT = 0; this.dying = 0; this.deadEl.style.display = "none";
      this.buildWorld(); this.buildThings(); this.setVisor(this.visor, true); this.pushTags();
    }
    buildWorld() { this.world = W.build(this.P, this.T, { xray: this.visor === "xray" }); this.view.setWorld(this.world.batches, this.urls); }
    shapeGm(key, make) { if (!this.gpu[key]) { const m = make(); this.models[key] = m; this.gpu[key] = this.view.loadModel(m); } return this.gpu[key]; }
    fbx(aid) { const F = PRIM.FBX; if (!aid || !this.urls[aid]) return null; const m = F.cache[aid]; if (!m) { if (!this.loading) this.loading = {}; if (!this.loading[aid]) { this.loading[aid] = 1; F.load(aid, this.urls[aid]).then(() => {}); } return null; } if (!this.gpu["fbx:" + aid]) { this.gpu["fbx:" + aid] = this.view.loadModel(m); this.models["fbx:" + aid] = m; } return { gm: this.gpu["fbx:" + aid], model: m }; }
    buildThings() {
      const T = this.T; this.things = []; this.lights = [];
      for (const room of this.P.rooms) for (const t of room.things) {
        const g = k => T.get(t.id + "." + k), o = { t, id: t.id, kind: t.kind, pos: t.p.slice(), yaw: (t.yaw || 0) * DEG, g, n: k => +g(k) || 0 };
        if (t.kind === "light") { this.lights.push(o); continue; }
        if (t.kind === "start") continue;
        if (t.kind === "pickup" && this.collected.has(t.id)) continue;
        if (t.kind === "door") { o.open = g("lock") === "open" ? 1 : 0; o.state = o.open ? "open" : "closed"; o.timer = 0; }
        if (t.kind === "enemy") { o.hp = o.n("health") || 1; o.st = "idle"; o.cool = 0.5 + Math.random(); o.los = false; o.awake = false; o.losT = Math.random() * 0.2; o.at = 0; o.frozen = 0; o.burn = 0; o.flash = 0; o.home = t.p.slice();
          o.body = { pos: t.p.slice(), vel: [0, 0, 0], r: o.n("radius") || 28, h: o.n("height") || 96, step: 20, maxSlope: 0.7, ground: false }; if (g("flying")) o.body.pos[1] += o.n("hover"); }
        this.things.push(o);
      }
    }
    // ---------------------------------------------------------------- input
    bind() {
      const cv = this.cv, k = this.keys;
      const lockNow = () => document.pointerLockElement === cv;
      const pl = () => { if (lockNow()) { this.msg.style.display = "none"; this.closePause(); } else if (!this.dead && !this.logOpen && this.started) this.openPause(); };
      const kd = e => { if (/INPUT|SELECT|TEXTAREA/.test((e.target && e.target.tagName) || "")) return; const id = keyId(e);
        if (e.key === "Escape") { e.preventDefault(); if (this.logOpen) { this.closeLog(); return; } if (this.paused) { this.resume(); } return; }
        if (["space", "tab", "arrowup", "arrowdown"].includes(id)) e.preventDefault(); if (!e.repeat && !k[id]) this.press(id); k[id] = true; };
      const ku = e => { const id = keyId(e); k[id] = false; this.release(id); };
      const mid = b => "mouse" + (b + 1);
      const md = e => { if (this.paused) return; if (!lockNow()) { this.started = true; try { const r = cv.requestPointerLock(); if (r && r.catch) r.catch(() => {}); } catch (x) {} this.msg.style.display = "none"; return; } e.preventDefault(); const id = mid(e.button); if (!k[id]) { k[id] = true; this.press(id); } };
      const mu = e => { const id = mid(e.button); if (k[id]) { k[id] = false; this.release(id); } };
      const mm = e => { if (!lockNow() || this.paused) return; const s = 0.0022 * (this.g("mouseSens") || 1); if (this.lock && this.lock.target) return; this.yaw += e.movementX * s; this.pitch = clamp(this.pitch - e.movementY * s, -1.45, 1.45); };
      const ctx = e => e.preventDefault(), blur = () => { for (const id in k) if (k[id]) { k[id] = false; this.release(id); } };
      addEventListener("keydown", kd, true); addEventListener("keyup", ku, true); cv.addEventListener("mousedown", md); addEventListener("mouseup", mu); addEventListener("mousemove", mm); document.addEventListener("pointerlockchange", pl); this.root.addEventListener("contextmenu", ctx); addEventListener("blur", blur);
      this.msg.addEventListener("click", () => { this.started = true; try { const r = cv.requestPointerLock(); if (r && r.catch) r.catch(() => {}); } catch (x) {} this.msg.style.display = "none"; });
      this.unbind = () => { removeEventListener("keydown", kd, true); removeEventListener("keyup", ku, true); removeEventListener("mouseup", mu); removeEventListener("mousemove", mm); document.removeEventListener("pointerlockchange", pl); removeEventListener("blur", blur); };
    }
    key(name) { return String(this.T.get("player." + name) || "").toLowerCase(); }
    press(id) {
      if (this.paused || this.dying) return; const T = this.T;
      if (this.logOpen) { if (id === this.key("fireKey") || id === "enter" || id === "space") this.closeLog(); return; }
      if (id === this.key("lockKey")) this.startLock();
      if (id === this.key("jumpKey")) this.jumpPressed = true;
      if (id === this.key("fireKey")) { this.fireHeld = 0.0001; if (this.visor !== "scan") this.shoot(false); }
      if (id === this.key("missileKey") && this.visor !== "scan") this.missile();
      const bk = String(T.get("player.beamKeys") || "").split(",").map(s => s.trim().toLowerCase()), bi = bk.indexOf(id); if (bi >= 0 && this.P.beams[bi]) this.setBeam(this.P.beams[bi].id);
      for (const [v] of D.VISORS) if (id === this.key("visor" + v[0].toUpperCase() + v.slice(1))) this.setVisor(v);
    }
    release(id) {
      if (id === this.key("lockKey")) { this.lock = null; }
      if (id === this.key("fireKey")) { if (this.visor !== "scan" && this.charge >= 1) this.shoot(true); this.fireHeld = 0; this.charge = 0; this.scanT = 0; if (this.chargeLoop) { this.chargeLoop.stop(0.08); this.chargeLoop = null; } }
    }
    // ---------------------------------------------------------------- pause, logbook
    openPause() { this.paused = true; this.renderPause(); this.pauseEl.style.display = "block"; }
    closePause() { this.paused = false; this.pauseEl.style.display = "none"; }
    resume() { this.closePause(); try { const r = this.cv.requestPointerLock(); if (r && r.catch) r.catch(() => {}); } catch (x) {} }
    renderPause(sel) {
      const p = this.pauseEl, T = this.T; p.replaceChildren(); p.append(el("h2", "", "PAUSED"));
      const bar = el("div"); const b = (t, fn) => { const x = el("button", "", t); x.onclick = fn; bar.append(x); return x; };
      b("Resume", () => this.resume()); b("Restart", () => { this.closePause(); this.newGame(); this.resume(); }); b("Quit", () => { if (this.o.onExit) this.o.onExit(); else this.destroy(); }); p.append(bar);
      const ents = this.entries().filter(e => this.scanned.has(e.id)), cols = el("div", "pg-cols"), list = el("div"), det = el("div", "pg-detail"); p.append(el("div", "pg-cat", "LOGBOOK  ·  " + ents.length + " / " + this.entries().length + " scanned"), cols); cols.append(list, det);
      const cats = [...new Set(ents.map(e => e.cat))]; if (!ents.length) list.append(el("div", "", "Nothing scanned yet. Switch to the Scan visor and hold fire on things with a marker."));
      for (const c of cats) { list.append(el("div", "pg-cat", c.toUpperCase())); for (const e of ents.filter(x => x.cat === c)) { const r = el("div", "pg-ent" + (sel === e.id ? " on" : ""), e.title); r.onclick = () => this.renderPause(e.id); list.append(r); } }
      const e = ents.find(x => x.id === sel); if (e) { det.append(el("h3", "", e.title)); if (e.img && this.urls[e.img]) { const im = el("img"); im.src = this.urls[e.img]; det.append(im); } det.append(el("p", "", e.text)); }
      const inv = el("div", "pg-cat", "BEAMS: " + this.P.beams.filter(x => this.inv.beams.has(x.id)).map(x => T.get(x.id + ".name")).join(", ") + "   ·   VISORS: " + [...this.inv.visors].join(", ") + "   ·   MISSILES: " + this.missiles + " / " + this.inv.missilesMax + "   ·   ENERGY TANKS: " + this.inv.tanks); p.append(inv);
    }
    entries() { const T = this.T, out = []; for (const r of this.P.rooms) for (const t of r.things) { const title = T.get(t.id + ".scanTitle"); if (title) out.push({ id: t.id, title, text: T.get(t.id + ".scanText") || "", img: T.get(t.id + ".scanImage"), cat: T.get(t.id + ".scanCategory") || "Research" }); } return out; }
    showLog(t) { const T = this.T, x = this.logEl; x.replaceChildren(el("small", "", (T.get(t.id + ".scanCategory") || "Research").toUpperCase() + "  ·  LOGBOOK ENTRY ADDED"), el("h3", "", T.get(t.id + ".scanTitle")));
      const im = T.get(t.id + ".scanImage"); if (im && this.urls[im]) { const i = el("img"); i.src = this.urls[im]; x.append(i); } x.append(el("p", "", T.get(t.id + ".scanText") || ""), el("small", "", "fire / Esc to close")); x.style.display = "block"; this.logOpen = true; }
    closeLog() { this.logEl.style.display = "none"; this.logOpen = false; }
    bannerText(t) { this.banner.textContent = t; this.banner.style.opacity = 1; clearTimeout(this.bannerT); this.bannerT = setTimeout(() => { this.banner.style.opacity = 0; }, 2600); }
    sfx(id, v) { if (id && this.urls[id] && PRIM.Audio) PRIM.Audio.play(this.urls[id], clamp(v === undefined ? 0.8 : v, 0, 1), false); }
    // ---------------------------------------------------------------- beams, visors
    setBeam(id) { if (!this.inv.beams.has(id) || this.beam === id) return; this.beam = id; this.charge = 0; this.pushTags(); }
    setVisor(v, force) { if (!this.inv.visors.has(v) || (this.visor === v && !force)) return; const was = this.visor; this.visor = v; this.view.visor = { combat: 0, thermal: 1, xray: 2, scan: 3 }[v]; this.hud.setScreen(this.T.get("visor." + v + ".hud") || ""); if ((was === "xray") !== (v === "xray") || force) this.buildWorld(); if (!force) this.sfx(this.T.get("visor." + v + ".sound")); this.lock = null; this.pushTags(); }
    pushTags() {
      const T = this.T, per = this.g("energyPerTank") || 100, full = clamp(Math.floor(Math.max(0, this.hp) / per), 0, this.inv.tanks), now = Math.max(0, Math.round(this.hp - full * per)), bn = this.beam ? T.get(this.beam + ".name") : "";
      const v = { "player.energyNow": now, "player.energyMax": full < this.inv.tanks ? per : Math.max(1, this.hpMax - this.inv.tanks * per), "player.tanks": this.inv.tanks, "player.tanksFull": full, "player.missiles": this.missiles, "player.missilesMax": this.inv.missilesMax, "player.beamName": bn, "player.visorName": (D.VISORS.find(x => x[0] === this.visor) || [0, ""])[1].toUpperCase(), "player.charge": Math.round(this.charge * 100) / 100 };
      const ch = {}; for (const k in v) if (T.get(k) !== v[k]) ch[k] = v[k]; if (Object.keys(ch).length) T.setMany(ch);
    }
    // ---------------------------------------------------------------- frame
    frame(t) {
      if (this.dead) return; this.raf = requestAnimationFrame(q => this.frame(q)); const dt = Math.min(0.05, (t - this.last) / 1000); this.last = t; this.time = (this.time || 0) + dt;
      if (!this.paused && !this.logOpen && document.pointerLockElement === this.cv || this.o.autoplay) this.update(dt);
      this.render(dt);
    }
    camBasis() { return R3D.camBasis(this.yaw, this.pitch); }
    eye() { return [this.body.pos[0], this.body.pos[1] + (this.g("eye") || 128), this.body.pos[2]]; }
    update(dt) {
      const T = this.T, k = this.keys, b = this.body;
      if (this.dying) { this.dying += dt; if (this.dying > 2.6) { this.dying = 0; this.restart(false); } return; }
      // ---- lock-on: keep facing the target, drop it when it is gone
      if (this.lock && this.lock.target) { const tg = this.lock.target; if (!this.validTarget(tg) || len(sub(this.centerOf(tg), this.eye())) > (this.g("lockRange") || 1400) * 1.25) this.lock.target = null; else { const d = sub(this.centerOf(tg), this.eye()); const wy = Math.atan2(d[2], d[0]), wp = Math.atan2(d[1], Math.hypot(d[0], d[2])); this.yaw += Math.atan2(Math.sin(wy - this.yaw), Math.cos(wy - this.yaw)) * Math.min(1, dt * 14); this.pitch += (wp - this.pitch) * Math.min(1, dt * 14); } }
      // ---- moving: forward / back and strafing; with a target, strafing circles around it
      const fw = (k.w || k.arrowup ? 1 : 0) - (k.s || k.arrowdown ? 1 : 0), sd = (k.d ? 1 : 0) - (k.a ? 1 : 0), cy = Math.cos(this.yaw), sy = Math.sin(this.yaw), sp = this.g("speed") || 330;
      let wx = (cy * fw - sy * sd), wz = (sy * fw + cy * sd); const l = Math.hypot(wx, wz); if (l > 1) { wx /= l; wz /= l; }
      if (this.jumpPressed) { this.jumpPressed = false; if (this.lock && sd && b.ground) { this.dash = this.g("dashTime") || 0.28; const ds = this.g("dashSpeed") || 620; b.vel = [-sy * sd * ds, Math.sqrt(2 * (this.g("gravity") || 1700) * 28), cy * sd * ds]; b.ground = false; }
        else if (b.ground) { b.vel[1] = Math.sqrt(2 * (this.g("gravity") || 1700) * (this.g("jumpHeight") || 110)); b.ground = false; } }
      if (this.dash > 0) this.dash -= dt;
      else { const ax = b.ground ? 1 : clamp(this.g("airControl"), 0, 1) * dt * 6; b.vel[0] += (wx * sp - b.vel[0]) * Math.min(1, b.ground ? 1 : ax); b.vel[2] += (wz * sp - b.vel[2]) * Math.min(1, b.ground ? 1 : ax); }
      if (!b.ground) b.vel[1] -= (this.g("gravity") || 1700) * dt;
      W.move(b, dt, this.world.grid, this.dynTris());
      for (const o of this.things) if (o.kind === "enemy" && this.alive(o)) { const dx = b.pos[0] - o.body.pos[0], dz = b.pos[2] - o.body.pos[2], d = Math.hypot(dx, dz), rr = b.r + o.body.r; if (d < rr && d > 0.01 && b.pos[1] < o.body.pos[1] + o.body.h && b.pos[1] + b.h > o.body.pos[1]) { b.pos[0] += dx / d * (rr - d); b.pos[2] += dz / d * (rr - d); } }
      if (b.pos[1] < -6000) this.hurt(99999);
      // ---- weapons and scanning
      this.cool -= dt;
      if (this.fireHeld) { this.fireHeld += dt;
        if (this.visor === "scan") this.scanStep(dt);
        else { const bp = this.beam, pat = bp && T.get(bp + ".pattern"); if (pat === "rapid" && this.cool <= 0) this.shoot(false);
          if (this.inv.charge && this.fireHeld > 0.3 && pat !== "rapid") { const was = this.charge; this.charge = Math.min(1, this.charge + dt / (this.g("chargeTime") || 1)); if (!was && this.charge > 0) { const cs = bp && T.get(bp + ".chargeSound"); if (cs && this.urls[cs] && PRIM.Audio) this.chargeLoop = PRIM.Audio.play(this.urls[cs], 0.7, true); } } } }
      this.stepShots(dt); this.stepThings(dt); this.pushTags();
    }
    // ---------------------------------------------------------------- lock-on
    lockables() { const out = []; for (const o of this.things) { if (this.visor === "scan" ? this.scannable(o) : this.lockable(o)) out.push(o); } return out; }
    lockable(o) { return (o.kind === "enemy" && this.alive(o) && o.g("lockable") !== false && (!o.g("xrayOnly") || this.visor === "xray")) || (o.kind === "model" && o.g("lockable")); }
    scannable(o) { return !!o.g("scanTitle") && (o.kind !== "enemy" || this.alive(o)) && !(o.kind === "pickup" && o.taken); }
    validTarget(o) { return this.visor === "scan" ? this.scannable(o) : this.lockable(o); }
    centerOf(o) { if (o.kind === "enemy") return [o.body.pos[0], o.body.pos[1] + o.body.h * 0.55, o.body.pos[2]]; if (o.kind === "pickup") return [o.pos[0], o.pos[1] + 40, o.pos[2]]; if (o.kind === "door") return [o.pos[0], o.pos[1] + o.n("height") / 2, o.pos[2]]; if (o.kind === "model") return [o.pos[0], o.pos[1] + 32, o.pos[2]]; return o.pos.slice(); }
    see(p, from) { const e = from || this.eye(), d = sub(p, e), L = len(d); if (L < 1) return true; const D2 = mul(d, 1 / L), hit = W.raycast(this.world.grid, e, D2, L, this.dynTris()); return !hit || hit.t > L - 12; }
    startLock() {
      this.lock = { target: null }; const B = this.camBasis(), e = this.eye(), range = this.g("lockRange") || 1400, cone = Math.cos((this.g("lockAngle") || 32) * DEG); let best = null;
      for (const o of this.lockables()) { const d = sub(this.centerOf(o), e), L = len(d); if (L > range || L < 1) continue; const c = dot(mul(d, 1 / L), B.f); if (c < cone) continue; if (!best || c > best.c) { if (this.see(this.centerOf(o))) best = { o, c }; } }
      if (best) { this.lock.target = best.o; this.sfx(this.T.get("player.lockSound"), 0.6); }
    }
    // ---------------------------------------------------------------- shooting
    muzzle() { const B = this.camBasis(), e = this.eye(), x = this.g("cannonX"), y = this.g("cannonY"), z = this.g("cannonZ") + 26 * (this.g("cannonScale") || 1); return [e[0] + B.s[0] * x - B.u[0] * y + B.f[0] * z, e[1] + B.s[1] * x - B.u[1] * y + B.f[1] * z, e[2] + B.s[2] * x - B.u[2] * y + B.f[2] * z]; }
    aimPoint() { if (this.lock && this.lock.target) return this.centerOf(this.lock.target); const e = this.eye(), f = this.camBasis().f, h = W.raycast(this.world.grid, e, f, 6000, this.dynTris()); return h ? h.p : add(e, mul(f, 6000)); }
    shoot(charged) {
      const T = this.T, bid = this.beam; if (!bid) return; const pre = bid + ".", g = k => +T.get(pre + k) || 0; if (!charged && this.cool > 0) return; this.cool = 1 / Math.max(0.2, g("rate") || 4);
      const m = this.muzzle(), dir = norm(sub(this.aimPoint(), m)), col = hex(T.get(pre + "color"), "#ffd23a"), pat = T.get(pre + "pattern") || "single";
      const base = { beam: bid, kind: charged ? "charged" : "beam", c: col, life: 2.5, t: 0, freeze: g("freeze"), burn: g("burn") };
      if (charged) { this.shots.push(Object.assign({}, base, { p: m, d: dir, speed: (g("speed") || 1700) * 0.8, size: g("chargeSize") || 14, dmg: g("chargeDamage") || 50 })); this.kick = 1; }
      else if (pat === "spread") { const B = this.camBasis(); for (const a of [-0.1, 0, 0.1]) this.shots.push(Object.assign({}, base, { p: m, d: norm(add(dir, mul(B.s, a))), speed: g("speed") || 1700, size: g("size") || 5, dmg: g("damage") || 10 })); this.kick = 0.5; }
      else this.shots.push(Object.assign({}, base, { p: m, d: dir, speed: g("speed") || 1700, size: (g("size") || 5) * (pat === "rapid" ? 0.7 : 1), dmg: g("damage") || 10, wave: pat === "wave" ? Math.random() * 6 : null, side: this.camBasis().s }));
      this.kick = Math.max(this.kick || 0, 0.45); this.sfx(T.get(pre + "sound") || T.get("player.shotSound"), 0.7); this.firedAt = this.time;
    }
    missile() {
      const T = this.T; if (this.cool > 0) return;
      if (this.charge >= 1 && this.inv.combo && this.missiles >= this.g("comboMissiles")) { this.combo(); return; }
      if (this.missiles < 1) return; this.missiles--; this.cool = 0.45; const m = this.muzzle(), dir = norm(sub(this.aimPoint(), m));
      this.shots.push({ kind: "missile", p: m, d: dir, speed: this.g("missileSpeed") || 1300, size: 4, dmg: this.g("missileDamage") || 40, c: hex(T.get("player.missileColor"), "#ff9d3a"), life: 4, t: 0, home: this.lock && this.lock.target });
      this.kick = 0.8; this.sfx(T.get("player.missileSound"), 0.8);
    }
    combo() {
      const T = this.T, bid = this.beam, pre = bid + ".", kind = T.get(pre + "combo") || "blast", col = hex(T.get(pre + "color")), m = this.muzzle(), dir = norm(sub(this.aimPoint(), m)), dmg = +T.get(pre + "comboDamage") || 160;
      this.missiles -= this.g("comboMissiles"); this.charge = 0; this.cool = 1; if (this.chargeLoop) { this.chargeLoop.stop(0.05); this.chargeLoop = null; }
      if (kind === "volley") { for (let i = 0; i < 5; i++) this.shots.push({ kind: "combo", beam: bid, p: m, d: norm(add(dir, [Math.random() * 0.3 - 0.15, Math.random() * 0.2, Math.random() * 0.3 - 0.15])), speed: 900, size: 6, dmg: dmg / 5, c: col, life: 4, t: 0, home: this.lock && this.lock.target }); }
      else if (kind === "stream") { this.stream = { t: 1.6, dmg: dmg / 1.6, col, bid }; }
      else this.shots.push({ kind: "combo", beam: bid, p: m, d: dir, speed: 1100, size: 30, dmg, c: col, life: 3, t: 0, blast: 220 });
      this.kick = 1.2; this.sfx(T.get(pre + "sound") || T.get("player.missileSound"), 1);
    }
    stepShots(dt) {
      if (this.stream) { this.stream.t -= dt; const m = this.muzzle(), a = this.aimPoint(), d = norm(sub(a, m)), h = W.raycast(this.world.grid, m, d, 2500, this.dynTris()), end = h ? h.p : add(m, mul(d, 2500));
        this.fx.push({ kind: "line", a: m, b: end, c: this.stream.col, w: 3.5, life: 0.05, t: 0 }); const he = this.hitEnemy(m, d, h ? h.t : 2500); if (he) this.damage(he.o, this.stream.dmg * dt, { kind: "combo", beam: this.stream.bid }); if (this.stream.t <= 0) this.stream = null; }
      this.shots = this.shots.filter(s => {
        s.t += dt; if (s.t > s.life) return false;
        if (s.home && this.alive(s.home)) { const want = norm(sub(this.centerOf(s.home), s.p)); s.d = norm(add(s.d, mul(want, Math.min(1, dt * 6)))); }
        const step = s.speed * dt;
        if (s.hostile) { const q = add(s.p, mul(s.d, step)), b = this.body; if (Math.hypot(q[0] - b.pos[0], q[2] - b.pos[2]) < b.r + s.size && q[1] > b.pos[1] - 4 && q[1] < b.pos[1] + b.h + 4) { this.hurt(s.dmg); this.spark(q, s.c); return false; } }
        else { const he = this.hitEnemy(s.p, s.d, step + s.size); if (he) { this.damage(he.o, s.dmg, s); this.spark(add(s.p, mul(s.d, he.t)), s.c); if (s.blast) this.blast(add(s.p, mul(s.d, he.t)), s); return false; } }
        const dh = this.hitDoor(s.p, s.d, step + s.size); if (dh) { if (!s.hostile) this.shootDoor(dh.o, s); this.spark(add(s.p, mul(s.d, dh.t)), s.c); if (s.blast) this.blast(add(s.p, mul(s.d, dh.t)), s); return false; }
        const h = W.raycast(this.world.grid, s.p, s.d, step + s.size, this.solidModelTris()); if (h) { this.spark(h.p, s.c); if (s.blast) this.blast(h.p, s); return false; }
        s.p = add(s.p, mul(s.d, step)); if (s.wave !== null && s.wave !== undefined && s.side) { s.wave += dt * 22; s.off = Math.sin(s.wave) * 7; }
        return true;
      });
      this.fx = this.fx.filter(f => (f.t += dt) < f.life);
    }
    blast(p, s) { for (const o of this.things) if (o.kind === "enemy" && this.alive(o)) { const d = len(sub(this.centerOf(o), p)); if (d < s.blast) this.damage(o, s.dmg * (1 - d / s.blast) * 0.6, s); } for (let i = 0; i < 6; i++) this.fx.push({ kind: "sphere", p: add(p, [Math.random() * 60 - 30, Math.random() * 60 - 30, Math.random() * 60 - 30]), c: s.c, size: 18 + Math.random() * 20, life: 0.35, t: 0 }); }
    spark(p, c) { this.fx.push({ kind: "sphere", p, c: c.map(q => Math.min(1, q * 1.4)), size: 7, life: 0.12, t: 0, light: { c, k: 1.2, r: 160 } }); }
    // ---------------------------------------------------------------- energy
    hurt(d) { if (this.dying || !(d > 0)) return; this.hp -= d; this.hitFlash = 1; this.sfx(this.T.get("player.hurtSound"), 0.8); if (this.hp <= 0) { this.hp = 0; this.dying = 0.0001; this.deadEl.style.display = "grid"; this.lock = null; } this.pushTags(); }
    // ---------------------------------------------------------------- things: doors, pick-ups, enemies, scanning
    alive(o) { return o && o.kind === "enemy" && o.st !== "dead" && o.st !== "dying"; }
    doorBox(o) { const w = o.n("width") || 128, h = o.n("height") || 160, th = 14, alongX = Math.abs(Math.cos(o.yaw)) > 0.5, p = o.pos; return alongX ? { mn: [p[0] - w / 2, p[1], p[2] - th], mx: [p[0] + w / 2, p[1] + h, p[2] + th] } : { mn: [p[0] - th, p[1], p[2] - w / 2], mx: [p[0] + th, p[1] + h, p[2] + w / 2] }; }
    dynTris() { if (this.dynF === this.time && this.dynC) return this.dynC; const out = []; for (const o of this.things) { if (o.kind === "door" && o.open < 0.85) { const b = this.doorBox(o); out.push(...W.boxTris(b.mn, b.mx, o.id)); } } out.push(...this.solidModelTris()); this.dynF = this.time; this.dynC = out; return out; }
    solidModelTris() { if (this.smC) return this.smC; const out = []; for (const o of this.things || []) if (o.kind === "model" && o.g("solid")) { const s = 32 * (o.n("scale") || 1); out.push(...W.boxTris([o.pos[0] - s, o.pos[1], o.pos[2] - s], [o.pos[0] + s, o.pos[1] + 2 * s, o.pos[2] + s], o.id)); } return this.smC = out; }
    hitDoor(O, Dv, maxd) { let best = null; for (const o of this.things) { if (o.kind !== "door" || o.open > 0.5) continue; const b = this.doorBox(o), t = rayBox(O, Dv, b.mn, b.mx, maxd); if (t !== null && (!best || t < best.t)) best = { o, t }; } return best; }
    shootDoor(o, s) { const L = o.g("lock"), ok = L === "any" ? true : L === "missile" ? s.kind === "missile" || s.kind === "combo" : L === "beam" ? s.beam === o.g("lockBeam") : false; if (!ok || o.state === "open" || o.state === "opening") return; o.state = "opening"; this.sfx(o.g("openSound"), 0.8); }
    hitEnemy(O, Dv, maxd) { let best = null; for (const o of this.things) { if (!this.alive(o) || (o.g("xrayOnly") && this.visor !== "xray")) continue; const r = o.body.r, h = o.body.h, p = o.body.pos, fx = O[0] - p[0], fz = O[2] - p[2], a = Dv[0] * Dv[0] + Dv[2] * Dv[2], bb = 2 * (fx * Dv[0] + fz * Dv[2]), c = fx * fx + fz * fz - r * r; let t;
      if (c <= 0) t = 0; else { if (a < 1e-9) continue; const disc = bb * bb - 4 * a * c; if (disc < 0) continue; t = (-bb - Math.sqrt(disc)) / (2 * a); if (t < 0) continue; } if (t > maxd) continue; const y = O[1] + Dv[1] * t; if (y < p[1] || y > p[1] + h) continue; if (!best || t < best.t) best = { o, t }; } return best; }
    damage(o, d, s) {
      if (!this.alive(o)) return; const wt = o.g("weakTo"), ok = wt === "missile" ? (s.kind === "missile" || s.kind === "combo") : wt === "beam" ? s.beam === o.g("weakBeam") : true; o.awake = true;
      if (!ok) { o.flash = 0.06; return; }
      if (o.frozen > 0 && s.kind === "missile") d = o.hp + 1;   // a frozen enemy shatters when a missile hits it
      o.hp -= d; o.flash = 0.12; if (s.freeze) o.frozen = Math.max(o.frozen, s.freeze * (s.kind === "charged" ? 1.5 : 0.35)); if (s.burn) { o.burn = Math.max(o.burn, 3); o.burnRate = s.burn; }
      if (o.hp <= 0) this.kill(o); else this.sfx(o.g("hurtSound"), 0.6);
    }
    kill(o) { o.st = "dying"; o.at = 0; this.sfx(o.g("deathSound"), 0.8); if (this.lock && this.lock.target === o) this.lock.target = null;
      const drop = (item, amt, col) => this.things.push({ kind: "drop", item, amount: amt, pos: [o.body.pos[0], o.body.pos[1], o.body.pos[2]], life: 14, c: hex(col), g: () => undefined, n: () => 0 });
      if (Math.random() < o.n("dropEnergy")) drop("energy", 20, "#7df3ff"); else if (this.inv.missilesMax > 0 && Math.random() < o.n("dropMissiles")) drop("missiles", 5, "#ff9d3a"); }
    give(item, o) {
      const T = this.T, g = k => o.g(k), n = k => +o.g(k) || 0; let msg = ITEM_TEXT[item] || "";
      if (item === "energyTank") { this.inv.tanks++; this.hpMax += this.g("energyPerTank") || 100; this.hp = this.hpMax; }
      else if (item === "missileExpansion" || item === "missileLauncher") { const a = n("amount") || 5; this.inv.missilesMax += a; this.missiles = item === "missileLauncher" ? this.inv.missilesMax : this.missiles + a; }
      else if (item === "beam") { const b = g("beam") || (this.P.beams.find(x => !this.inv.beams.has(x.id)) || {}).id; if (b) { this.inv.beams.add(b); this.beam = b; msg = T.get(b + ".name") + " Beam acquired"; } }
      else if (item === "visor") { const v = g("visor") || "scan"; this.inv.visors.add(v); msg = (D.VISORS.find(x => x[0] === v) || [0, v])[1] + " Visor acquired"; }
      else if (item === "charge") this.inv.charge = true; else if (item === "combo") this.inv.combo = true;
      else if (item === "energy") this.hp = Math.min(this.hpMax, this.hp + (n("amount") || 20)); else if (item === "missiles") this.missiles = Math.min(this.inv.missilesMax, this.missiles + (n("amount") || 5));
      else if (item === "flag") { const f = g("flag"); this.inv.flags.add(f); if (T.has(f)) T.set(f, true); }
      return g("message") || msg;
    }
    stepThings(dt) {
      const T = this.T, b = this.body, e = this.eye();
      for (const o of this.things) {
        if (o.kind === "door") {
          if (o.state === "opening") { o.open = Math.min(1, o.open + dt * 2.2); if (o.open >= 1) { o.state = "open"; o.timer = 0; } }
          else if (o.state === "open" && o.g("lock") !== "open") { const ca = o.n("closeAfter"); if (ca > 0) { const near = len(sub([b.pos[0], o.pos[1], b.pos[2]], o.pos)) < (o.n("width") || 128) * 0.9; o.timer = near ? 0 : o.timer + dt; if (o.timer > ca) o.state = "closing"; } }
          else if (o.state === "closing") { o.open = Math.max(0, o.open - dt * 2.2); if (o.open <= 0) o.state = "closed"; }
        }
        else if (o.kind === "pickup" && !o.taken) { const c = this.centerOf(o); if (len(sub(c, add(b.pos, [0, 50, 0]))) < 64) { o.taken = true; this.collected.add(o.id); const msg = this.give(o.g("item"), o); if (msg) this.bannerText(msg.toUpperCase()); this.sfx(o.g("sound") || T.get("player.pickupSound"), 0.9); } }
        else if (o.kind === "drop") { o.life -= dt; if (o.life <= 0) o.gone = true; else if (len(sub(o.pos, b.pos)) < 70) { o.gone = true; this.give(o.item, { g: k => k === "amount" ? o.amount : undefined }); } }
        else if (o.kind === "enemy") this.enemyStep(o, dt);
      }
      this.things = this.things.filter(o => !o.gone);
    }
    enemyStep(o, dt) {
      if (o.st === "dead") return; o.at += dt; if (o.flash > 0) o.flash -= dt;
      if (o.st === "dying") { if (o.at > 0.6) { o.st = "dead"; } return; }
      if (o.burn > 0) { o.burn -= dt; o.hp -= (o.burnRate || 6) * dt; if (o.hp <= 0) return this.kill(o); }
      if (o.frozen > 0) { o.frozen -= dt; return; }
      const b = o.body, p = this.body.pos, d = sub([p[0], p[1] + this.body.h * 0.6, p[2]], [b.pos[0], b.pos[1] + b.h * 0.6, b.pos[2]]), dist = len(d), fly = !!o.g("flying");
      o.losT -= dt; if (o.losT <= 0) { o.losT = 0.2; o.los = !this.dying && dist < (o.n("sight") || 1100) && this.see([p[0], p[1] + (this.g("eye") || 128), p[2]], [b.pos[0], b.pos[1] + b.h * 0.8, b.pos[2]]); if (o.los) o.awake = true; }
      if (!o.awake || this.dying) { o.st = "idle"; if (!fly) { b.vel[0] = b.vel[2] = 0; if (!b.ground) b.vel[1] -= 1700 * dt; W.move(b, dt, this.world.grid, this.dynTris()); } return; }
      o.yaw = Math.atan2(d[0], d[2]); o.cool -= dt; const range = o.n("attackRange") || 90, melee = o.g("attack") !== "shot";
      if (o.los && o.cool <= 0 && dist <= range + (melee ? b.r + this.body.r : 0)) { o.cool = o.n("attackRate") || 1.4; o.st = "attack"; o.at = 0; this.sfx(o.g("attackSound"), 0.6);
        if (melee) this.hurt(o.n("damage")); else { const from = [b.pos[0], b.pos[1] + b.h * 0.6, b.pos[2]], to = [p[0], p[1] + (this.g("eye") || 128) * 0.75, p[2]]; this.shots.push({ hostile: true, kind: "enemy", p: from, d: norm(sub(to, from)), speed: o.n("shotSpeed") || 700, size: 7, dmg: o.n("damage"), c: hex(o.g("shotColor"), "#ff7a3a"), life: 5, t: 0, light: true }); } }
      else if (o.st === "attack" && o.at > 0.45) o.st = "chase";
      const want = melee ? b.r + this.body.r + 4 : Math.min(range * 0.6, 500), spd = o.n("speed"), hd = Math.hypot(d[0], d[2]) || 1;
      if (o.st !== "attack" && (hd > want || !o.los)) { o.st = "chase"; b.vel[0] = d[0] / hd * spd; b.vel[2] = d[2] / hd * spd; } else { b.vel[0] *= 0.5; b.vel[2] *= 0.5; }
      if (fly) { const ty = p[1] + o.n("hover") * 0.6; b.vel[1] = clamp((ty - b.pos[1]) * 2, -spd, spd); W.move(b, dt, this.world.grid, this.dynTris()); b.ground = false; }
      else { if (!b.ground) b.vel[1] -= 1700 * dt; W.move(b, dt, this.world.grid, this.dynTris()); }
    }
    scanStep(dt) {
      const T = this.T; let tg = this.lock && this.lock.target && this.scannable(this.lock.target) ? this.lock.target : null;
      if (!tg) { const B = this.camBasis(), e = this.eye(); let best = null; for (const o of this.things) { if (!this.scannable(o)) continue; const d = sub(this.centerOf(o), e), L = len(d); if (L > 1100) continue; const c = dot(mul(d, 1 / L), B.f); if (c > Math.cos(9 * DEG) && (!best || c > best.c) && this.see(this.centerOf(o))) best = { o, c }; } tg = best && best.o; }
      if (this.scanTarget !== tg) { this.scanTarget = tg; this.scanT = 0; }
      if (!tg) return; if (this.scanned.has(tg.id)) { if (this.fireHeld <= dt * 1.5) this.showLog(tg.t); return; }
      this.scanT += dt / (+tg.g("scanTime") || 1.2); if (this.scanT >= 1) { this.scanned.add(tg.id); this.scanT = 0; this.fireHeld = 0; this.sfx(T.get("player.scanSound"), 0.8); this.showLog(tg.t); }
    }
    // ---------------------------------------------------------------- drawing
    render(dt) {
      const v = this.view, T = this.T, c = v.cam, e = this.eye(); v.fov = (this.g("fov") || 78) * DEG; c.x = e[0]; c.y = e[1]; c.z = e[2]; c.yaw = this.yaw; c.pitch = this.pitch;
      if (this.dying) c.y -= Math.min(1, this.dying) * (this.g("eye") || 128) * 0.6;
      const lights = []; for (const o of this.lights) { const fl = o.n("flicker"), k = (o.n("intensity") || 1) * (fl ? 1 - fl * Math.random() * 0.6 : 1); lights.push({ p: o.pos, r: o.n("range") || 520, c: hex(o.g("color"), "#ffe8c4").map(q => q * k), type: 0 }); }
      for (const s of this.shots) lights.push({ p: s.p, r: s.kind === "charged" || s.kind === "combo" ? 360 : 180, c: s.c.map(q => q * 1.3), type: 0 });
      v.setLights(lights); v.draw();
      const items = []; this.thingItems(items); for (const s of this.shots) { const p = s.off && s.side ? add(s.p, mul(s.side, s.off)) : s.p; items.push(s.kind === "missile" ? { kind: "laser", p, d: s.d, size: 1.6, c: s.c } : { kind: "sphere", p, c: s.c.map(q => Math.min(1, q * 1.2)), size: s.size }); }
      for (const f of this.fx) items.push(f); if (this.charge > 0 && this.visor !== "scan") items.push({ kind: "sphere", p: this.muzzle(), c: hex(T.get((this.beam || "") + ".color")).map(q => Math.min(1, q * 1.3)), size: 2 + this.charge * ((+T.get(this.beam + ".chargeSize") || 14) * 0.5) });
      if (items.length) v.drawFx(items);
      this.drawCannon(v, dt); this.drawOverlay();
    }
    thingItems(items) {
      const T = this.T, xray = this.visor === "xray", scanV = this.visor === "scan", F = PRIM.FBX;
      const put = (gm, model, M, mat, anim, time, p, tex) => items.push({ kind: "mesh", gm, pose: F.pose(model, anim || null, time || 0), M, p, scale: 6, mat, tex });
      for (const o of this.things) {
        const g = o.g, mark = scanV && g("scanTitle") ? (this.scanned.has(o.id) ? 2 : 1) : 0;
        if (o.kind === "door") { const w = o.n("width") || 128, h = o.n("height") || 160, R = M4.mul(M4.translate(...o.pos), M4.rotY(-o.yaw / DEG + 0)), lk = g("lock"), col = lk === "missile" ? "#ff3a2a" : lk === "beam" ? (T.get(g("lockBeam") + ".color") || "#ffd23a") : lk === "locked" ? "#666a70" : "#3ea8ff";
          const fr = this.shapeGm("door:" + w + ":" + h, () => SH.doorFrame(w, h, 28)); put(fr, this.models["door:" + w + ":" + h], R, { tint: [0.55, 0.6, 0.66], pbr: true, rough: 0.5, metal: 0.6, mark }, null, 0, o.pos);
          if (o.open < 1) { const sh = this.shapeGm("shield:" + w + ":" + h, () => SH.doorShield(w, h)), k = 1 - o.open, cc = hex(col); put(sh, this.models["shield:" + w + ":" + h], M4.mul(M4.mul(R, M4.translate(0, h * (1 - k) / 2, 0)), M4.scale(1, Math.max(0.001, k), 1)), { tint: cc, emit: cc.map(q => q * 0.45), alpha: 0.85, mark }, null, 0, o.pos); } continue; }
        if (o.kind === "pickup" && !o.taken || o.kind === "drop") { const sp = this.time * 1.6, bob = Math.sin(this.time * 2.2) * 6, M = M4.mul(M4.translate(o.pos[0], o.pos[1] + 40 + bob, o.pos[2]), M4.rotY(sp / DEG)), f = o.kind === "pickup" && this.fbx(g("model"));
          if (f) put(f.gm, f.model, M4.mul(M, M4.scale(o.n("scale") || 1, o.n("scale") || 1, o.n("scale") || 1)), { mark }, null, 0, o.pos); else { const gm = this.shapeGm("gem", () => SH.gem(14)), cc = o.kind === "drop" ? o.c : hex(g("color"), "#4fe3ff"); put(gm, this.models.gem, o.kind === "drop" ? M4.mul(M, M4.scale(0.6, 0.6, 0.6)) : M, { tint: cc, emit: cc.map(q => q * 0.6), mark }, null, 0, o.pos); } continue; }
        if (o.kind === "enemy") { if (o.st === "dead" || (g("xrayOnly") && !xray)) continue; const bp = o.body.pos, k = o.st === "dying" ? Math.max(0.01, 1 - o.at / 0.6) : 1, f = this.fbx(g("model")), M = M4.mul(M4.translate(...bp), M4.rotY((o.yaw || 0) / DEG));
          const mat = { heat: o.n("heat"), xray: xray ? 1 : 0, mark, tint: o.frozen > 0 ? [0.6, 0.85, 1.2] : o.flash > 0 ? [1.6, 0.6, 0.5] : [1, 1, 1], emit: o.frozen > 0 ? [0.25, 0.4, 0.55] : o.flash > 0 ? [0.5, 0.1, 0.05] : [0, 0, 0] };
          if (f) { const an = o.st === "dying" ? g("deathAnim") : o.st === "attack" ? g("attackAnim") : o.st === "chase" ? g("walkAnim") : g("idleAnim"), a = an && f.model.anims[an], tm = a ? (o.st === "dying" ? Math.min(o.at, a.dur) : o.at % Math.max(0.05, a.dur)) : 0; put(f.gm, f.model, M4.mul(M, M4.scale((o.n("scale") || 1) * k, (o.n("scale") || 1) * k, (o.n("scale") || 1) * k)), mat, a ? an : null, tm, bp); }
          else { const gm = this.shapeGm("orb:" + o.body.r + ":" + o.body.h, () => SH.orb(o.body.r, o.body.h)), cc = hex(g("color"), "#d0553a"); put(gm, this.models["orb:" + o.body.r + ":" + o.body.h], M4.mul(M, M4.scale(k, k, k)), Object.assign(mat, { tint: mat.tint.map((q, i) => q * cc[i]) }), null, 0, bp); } continue; }
        if (o.kind === "model") { if (g("xrayOnly") && !xray) continue; const f = this.fbx(g("model")); if (!f) continue; const s = o.n("scale") || 1, an = g("anim"), a = an && f.model.anims[an], M = M4.mul(M4.translate(...o.pos), M4.mul(M4.rotXYZ([o.n("rx"), o.yaw / DEG, o.n("rz")]), M4.scale(s, s, s)));
          put(f.gm, f.model, M, { heat: o.n("heat"), xray: xray && g("xrayOnly") ? 1 : 0, mark }, a ? an : null, a ? this.time % Math.max(0.05, a.dur) : 0, o.pos); continue; }
        if (o.kind === "scan" && scanV) { const gm = this.shapeGm("gem", () => SH.gem(14)); put(gm, this.models.gem, M4.mul(M4.translate(o.pos[0], o.pos[1], o.pos[2]), M4.scale(0.4, 0.4, 0.4)), { tint: [1, 0.5, 0.15], emit: [0.4, 0.2, 0.05], mark }, null, 0, o.pos); }
      }
    }
    drawCannon(v, dt) {
      const T = this.T; this.kick = Math.max(0, (this.kick || 0) - dt * 4); const mv = this.body.ground ? Math.hypot(this.body.vel[0], this.body.vel[2]) / (this.g("speed") || 330) : 0; this.bobT = (this.bobT || 0) + dt * 9 * mv;
      const s = this.g("cannonScale") || 1, x = this.g("cannonX"), y = this.g("cannonY"), z = this.g("cannonZ"), bob = [Math.sin(this.bobT * 0.5) * 0.8 * mv, -Math.abs(Math.sin(this.bobT * 0.5)) * 0.9 * mv, 0];
      const M = M4.mul(M4.translate(x + bob[0], -y + bob[1], -z + this.kick * 3.5), M4.mul(M4.rotXYZ([this.g("cannonRx"), this.g("cannonRy"), this.g("cannonRz")]), M4.scale(s, s, s)));
      const f = this.fbx(T.get("player.cannon")), fireA = T.get("player.cannonFire"), cc = hex(T.get("player.cannonColor"), "#c9a24a"), glow = hex(T.get((this.beam || "") + ".color")).map(q => q * this.charge * 0.6);
      if (f) { const a = fireA && f.model.anims[fireA], tm = a && this.firedAt !== undefined ? Math.min(this.time - this.firedAt, a.dur) : 0; v.drawViewmodel([{ gm: f.gm, pose: PRIM.FBX.pose(f.model, a ? fireA : null, tm), M, mat: { emit: glow, pbr: true, rough: 0.45, metal: 0.4 } }], []); }
      else { const gm = this.shapeGm("cannon", () => SH.cannon()); v.drawViewmodel([{ gm, pose: PRIM.FBX.pose(this.models.cannon, null, 0), M, mat: { tint: cc, emit: glow, pbr: true, rough: 0.35, metal: 0.85 } }], []); }
    }
    toScreen(p) { const vp = this.view.vp; if (!vp) return null; const x = vp[0] * p[0] + vp[4] * p[1] + vp[8] * p[2] + vp[12], y = vp[1] * p[0] + vp[5] * p[1] + vp[9] * p[2] + vp[13], w = vp[3] * p[0] + vp[7] * p[1] + vp[11] * p[2] + vp[15]; if (w <= 0.01) return null; const r = this.ov.getBoundingClientRect(); return [(x / w * 0.5 + 0.5) * r.width, (1 - (y / w * 0.5 + 0.5)) * r.height]; }
    drawOverlay() {
      const cv = this.ov, dpr = Math.min(2, devicePixelRatio || 1), W2 = cv.clientWidth, H2 = cv.clientHeight; if (cv.width !== Math.round(W2 * dpr) || cv.height !== Math.round(H2 * dpr)) { cv.width = Math.round(W2 * dpr); cv.height = Math.round(H2 * dpr); }
      const g = cv.getContext("2d"); g.setTransform(dpr, 0, 0, dpr, 0, 0); g.clearRect(0, 0, W2, H2); const cx = W2 / 2, cy = H2 / 2, T = this.T, scanV = this.visor === "scan";
      const accent = scanV ? "#ffb04a" : this.visor === "thermal" ? "#ffd27a" : this.visor === "xray" ? "#e8f6ff" : "#7df3ff";
      // crosshair + charge ring
      g.strokeStyle = accent; g.lineWidth = 1.5; g.globalAlpha = 0.85; g.beginPath(); g.arc(cx, cy, 9, 0, Math.PI * 2); g.stroke(); g.fillStyle = accent; g.fillRect(cx - 1, cy - 1, 2, 2);
      if (this.charge > 0 && !scanV) { g.lineWidth = 3; g.beginPath(); g.arc(cx, cy, 16, -Math.PI / 2, -Math.PI / 2 + this.charge * Math.PI * 2); g.strokeStyle = this.charge >= 1 ? "#ffffff" : accent; g.stroke(); }
      // lock-on reticle
      const tg = this.lock && this.lock.target; if (tg) { const s = this.toScreen(this.centerOf(tg)); if (s) { const r = 26 + Math.sin(this.time * 6) * 3, a = this.time * 1.5; g.lineWidth = 2.5; g.strokeStyle = scanV ? "#ffb04a" : "#ff6a4a"; for (let i = 0; i < 4; i++) { const q = a + i * Math.PI / 2; g.beginPath(); g.arc(s[0], s[1], r, q, q + 0.7); g.stroke(); } } }
      else if (this.lock) { g.strokeStyle = accent; g.lineWidth = 1; g.strokeRect(cx - 26, cy - 26, 52, 52); }
      // scan markers and progress
      if (scanV) { for (const o of this.things) { if (!this.scannable(o)) continue; const c = this.centerOf(o); if (len(sub(c, this.eye())) > 1100) continue; const s = this.toScreen(c); if (!s) continue; const done = this.scanned.has(o.id); g.lineWidth = 2; g.strokeStyle = done ? "#5aa8ff" : "#ff8a2a"; g.strokeRect(s[0] - 9, s[1] - 9, 18, 18); if (!done) { g.fillStyle = "rgba(255,138,42,.3)"; g.fillRect(s[0] - 9, s[1] - 9, 18, 18); } }
        if (this.scanT > 0) { g.fillStyle = "rgba(20,10,0,.7)"; g.fillRect(cx - 110, cy + 44, 220, 14); g.fillStyle = "#ffb04a"; g.fillRect(cx - 108, cy + 46, 216 * this.scanT, 10); g.font = "12px 'Share Tech Mono',monospace"; g.textAlign = "center"; g.fillStyle = "#ffe0b0"; g.fillText("SCANNING", cx, cy + 74); } }
      // damage flash
      if (this.hitFlash > 0) { this.hitFlash = Math.max(0, this.hitFlash - 0.06); const gr = g.createRadialGradient(cx, cy, Math.min(W2, H2) * 0.25, cx, cy, Math.max(W2, H2) * 0.7); gr.addColorStop(0, "rgba(255,0,0,0)"); gr.addColorStop(1, "rgba(220,20,10," + 0.55 * this.hitFlash + ")"); g.globalAlpha = 1; g.fillStyle = gr; g.fillRect(0, 0, W2, H2); }
      g.globalAlpha = 1;
    }
  };
  // ray against an axis-aligned box: distance or null
  function rayBox(O, Dv, mn, mx, maxd) { let t0 = 0, t1 = maxd; for (let a = 0; a < 3; a++) { if (Math.abs(Dv[a]) < 1e-9) { if (O[a] < mn[a] || O[a] > mx[a]) return null; continue; } let q0 = (mn[a] - O[a]) / Dv[a], q1 = (mx[a] - O[a]) / Dv[a]; if (q0 > q1) [q0, q1] = [q1, q0]; t0 = Math.max(t0, q0); t1 = Math.min(t1, q1); if (t0 > t1) return null; } return t0; }
})();
