/* PRIMEngine: the game runtime (player, arm cannon, enemies, shots, HUD).
   Everything tweakable lives in `project` (see the project.json files under projects), so the editor changes
   the game live just by editing that object.  Modes: "edit" (viewport preview), "play", "pause", "end". */
(function () {
  "use strict";
  const PRIM = window.PRIM, M4 = PRIM.M4, D2R = PRIM.D2R, clamp = PRIM.clamp, angDiff = PRIM.angDiff;
  const G = PRIM.gfx, A = PRIM.Audio, L = PRIM.Level;
  const Game = (PRIM.Game = { mode: "edit", manual: false, onExit: null });
  const $ = id => document.getElementById(id);

  let gl, project, CFG, arm = null, MESH = {}, canvas, stage, W = 1, H = 1, aspect = 1, VP = null, camR = [1, 0, 0], camU = [0, 1, 0], lastT = 0;
  const keys = {};
  const S = (Game.S = { mode: "edit", P: null, enemies: [], shots: [], fx: [], pickups: [], stats: null, time: 0, lock: null, lockT: 0, lockLost: 0, rmb: false, firing: false,
    charge: { on: false, t: 0, full: false, snd: null, loop: null, wait: 0 }, cool: 0, shake: 0, noLockSupport: false, locked: false, flash: 0, hit: 0, hurt: 0, toastT: 0,
    sway: { x: 0, y: 0 }, bob: 0, recoil: 0, won: 0 });
  Game.keys = keys;

  const snd = (key, extra) => A.playSlot(project, key, extra);
  const armObj = () => project.objects.armCannon;
  const ST = () => S.stats;
  const P0 = () => ({ x: 0, y: CFG.player.eye, z: 0, yaw: -Math.PI / 2, pitch: 0, energy: CFG.player.maxEnergy, vx: 0, vz: 0 });

  // =====================================================================
  //  SETUP
  // =====================================================================
  Game.init = function (o) {
    project = o.project; CFG = project.settings; arm = o.arm || null; canvas = o.canvas; stage = o.stage; gl = G.gl;
    MESH = { body: G.boxMesh(0.62, 0.62, 0.62), wing: G.boxMesh(1.25, 0.07, 0.42), spine: G.boxMesh(0.1, 0.1, 0.9), slab: G.boxMesh(L.TILE, L.WALL_H, 0.34) };
    const visor = $("visor"); if (o.visorUrl) visor.src = o.visorUrl; else visor.style.display = "none";
    const cells = $("cells"); cells.innerHTML = ""; for (let i = 0; i < 20; i++) cells.appendChild(document.createElement("i"));
    resetLevel(); Game.resize(); lastT = performance.now(); requestAnimationFrame(frame);
  };
  Game.setProject = p => { project = p; CFG = p.settings; };
  Game.resize = function () {
    if (!stage) return;
    const px = Math.max(1, CFG.display.pixel | 0), w = Math.max(2, stage.clientWidth), h = Math.max(2, stage.clientHeight);
    W = Math.max(1, Math.floor(w / px)); H = Math.max(1, Math.floor(h / px)); canvas.width = W; canvas.height = H; aspect = w / h;
    canvas.classList.toggle("pixel", px > 1);
  };

  function resetLevel() {
    S.enemies = []; S.shots = []; S.fx = []; S.pickups = []; S.lock = null; S.won = 0; L.resetDoors(); S.P = P0();
    for (let z = 0; z < L.MH; z++) for (let x = 0; x < L.MW; x++) {
      const c = L.cellCh(x, z), cx = (x + 0.5) * L.TILE, cz = (z + 0.5) * L.TILE;
      if (c === "P") { S.P.x = cx; S.P.z = cz; }
      if (c === "E") S.enemies.push({ x: cx, y: 1.7, by: 1.7, z: cz, hp: CFG.enemy.hp, awake: false, cool: 1.5 + Math.random(), wind: 0, flash: 0, dead: false, yaw: 0, ph: Math.random() * 6, strafe: Math.random() < 0.5 ? 1 : -1, strafeT: 1 + Math.random() * 2 });
      if (c === "H") S.pickups.push({ x: cx, y: 0.7, z: cz, kind: "energy", t: Math.random() * 6 });
    }
    S.stats = { time: 0, shots: 0, hits: 0, charged: 0 };
    chargeStop(0.01); S.charge = { on: false, t: 0, full: false, snd: null, loop: null, wait: 0 }; S.cool = 0; S.firing = false; S.rmb = false;
    S.recoil = 0; S.flash = 0; S.hurt = 0; S.hit = 0; S.shake = 0;
    if (arm) arm.play("IDLE", true, 0.01);
  }
  Game.resetLevel = resetLevel;

  // =====================================================================
  //  COLLISION + QUERIES
  // =====================================================================
  const blocked = (x, z, r) => L.solidAt(x - r, z - r) || L.solidAt(x + r, z - r) || L.solidAt(x - r, z + r) || L.solidAt(x + r, z + r);
  function moveXZ(o, dx, dz, r) { if (!blocked(o.x + dx, o.z, r)) o.x += dx; if (!blocked(o.x, o.z + dz, r)) o.z += dz; }
  function los(ax, ay, az, bx, by, bz) {
    const dx = bx - ax, dy = by - ay, dz = bz - az, d = Math.hypot(dx, dy, dz), n = Math.ceil(d / 0.3);
    for (let i = 1; i < n; i++) { const t = i / n, x = ax + dx * t, z = az + dz * t, y = ay + dy * t; if (y < 0 || y > L.WALL_H || L.solidAt(x, z)) return false; }
    return true;
  }
  const eyePos = () => [S.P.x, S.P.y + Math.sin(S.bob) * 0.03 * Math.min(1, Math.hypot(S.P.vx, S.P.vz) / 3), S.P.z];
  const fwdVec = (yaw, pitch) => [-Math.sin(yaw) * Math.cos(pitch), Math.sin(pitch), -Math.cos(yaw) * Math.cos(pitch)];

  // =====================================================================
  //  EFFECTS
  // =====================================================================
  function spark(x, y, z, n, col, speed, life) {
    for (let i = 0; i < n; i++) { const a = Math.random() * 6.28, e = Math.random() * 2 - 0.4, s = speed * (0.4 + Math.random() * 0.8);
      S.fx.push({ k: "spark", x, y, z, vx: Math.cos(a) * s, vy: e * s, vz: Math.sin(a) * s, life: life * (0.5 + Math.random() * 0.7), max: life, col, size: 0.05 }); }
  }
  const flashFx = (x, y, z, size, col, life, light = true) => S.fx.push({ k: "flash", x, y, z, life, max: life, size, col, light });
  function impact(x, y, z, big, sound = true) {
    flashFx(x, y, z, big ? 1.5 : 0.7, big ? [1, 0.8, 0.5] : [1, 0.75, 0.4], big ? 0.28 : 0.16);
    spark(x, y, z, big ? 16 : 7, big ? [1, 0.8, 0.45] : [1, 0.7, 0.3], big ? 5 : 3.5, 0.5);
    if (sound) snd("world.impact", { pos: [x, y, z], volMul: big ? 1 : 0.55, rateMul: big ? 0.88 : 1 + Math.random() * 0.1 });
  }
  function toast(msg, t = 1.8) { $("toast").textContent = msg; $("toast").style.opacity = 1; S.toastT = t; }

  // =====================================================================
  //  LOCK-ON + FIRING
  // =====================================================================
  function acquireLock() {
    const P = S.P, f = fwdVec(P.yaw, P.pitch); let best = null, bs = -9;
    for (const e of S.enemies) { if (e.dead) continue;
      const dx = e.x - P.x, dy = e.y - P.y, dz = e.z - P.z, d = Math.hypot(dx, dy, dz); if (d > 50) continue;
      const dot = (dx * f[0] + dy * f[1] + dz * f[2]) / d; if (dot < Math.cos(70 * D2R)) continue;
      if (!los(P.x, P.y, P.z, e.x, e.y, e.z)) continue;
      const sc = dot - d / 160; if (sc > bs) { bs = sc; best = e; } }
    S.lock = best; if (best) toast("TARGET LOCKED", 0.8);
  }
  // the arm cannon's pose in view space, from the project's transform (the editor edits these numbers)
  function armPlace(bx = 0, by = 0, swx = 0, swy = 0, recoil = 0, lean = 0) {
    const a = armObj(), t = a.transform, mi = a.modelInfo, s = mi.unitScale * t.scale, c = mi.center;
    return M4.mul(M4.trans(t.position[0] + bx - swx * 0.03 - lean, t.position[1] + by + swy * 0.02, t.position[2] + recoil * 0.05),
      M4.mul(M4.rotY(t.rotation[1] * D2R), M4.mul(M4.rotX(t.rotation[0] * D2R + recoil * 0.035), M4.mul(M4.rotZ(t.rotation[2] * D2R), M4.mul(M4.scale(s, s, s), M4.trans(-c[0], -c[1], -c[2]))))));
  }
  function muzzleView() { const m = armPlace(), q = armObj().modelInfo.muzzle; return [m[0]*q[0] + m[4]*q[1] + m[8]*q[2] + m[12], m[1]*q[0] + m[5]*q[1] + m[9]*q[2] + m[13], m[2]*q[0] + m[6]*q[1] + m[10]*q[2] + m[14]]; }
  function muzzleWorld() {
    const P = S.P, r = [Math.cos(P.yaw), 0, -Math.sin(P.yaw)], f = fwdVec(P.yaw, P.pitch), u = [Math.sin(P.yaw) * Math.sin(P.pitch), Math.cos(P.pitch), Math.cos(P.yaw) * Math.sin(P.pitch)];
    const v = muzzleView(), mx = v[0], my = v[1], mf = -v[2];
    return [P.x + r[0] * mx + u[0] * my + f[0] * mf, P.y + r[1] * mx + u[1] * my + f[1] * mf, P.z + r[2] * mx + u[2] * my + f[2] * mf];
  }
  function shoot(charged) {
    const P = S.P, f = fwdVec(P.yaw, P.pitch), m = muzzleWorld(); let ax, ay, az;
    if (S.lock && !S.lock.dead) { ax = S.lock.x; ay = S.lock.y; az = S.lock.z; } else { ax = P.x + f[0] * 40; ay = P.y + f[1] * 40; az = P.z + f[2] * 40; }
    let dx = ax - m[0], dy = ay - m[1], dz = az - m[2]; const d = Math.hypot(dx, dy, dz) || 1; dx /= d; dy /= d; dz /= d;
    const sp = CFG.beam.speed;
    S.shots.push({ x: m[0], y: m[1], z: m[2], vx: dx * sp, vy: dy * sp, vz: dz * sp, charged, life: 2.5, mine: true, trail: [] });
    ST().shots++; if (charged) ST().charged++;
    S.recoil = charged ? 1.6 : 1; S.shake = charged ? 0.35 : 0.06; S.flash = charged ? 0.14 : 0.07;
  }
  function fireDown() {
    if (S.mode !== "play" || S.cool > 0) return;
    shoot(false); S.cool = CFG.beam.fireDelay; snd("armCannon.shot"); if (arm) arm.play("shot", false, 0.04);
    S.charge = { on: false, t: 0, full: false, snd: null, loop: null, wait: 0.34 }; // charging starts once the shot animation ends
  }
  function chargeStop(fade = 0.1) { const c = S.charge; if (!c) return; if (c.snd) c.snd.stop(fade); if (c.loop) c.loop.stop(fade); c.snd = null; c.loop = null; }
  function fireUp() {
    const c = S.charge;
    if (c.on && c.full) { chargeStop(0.05); snd("armCannon.chargedShot"); shoot(true); if (arm) arm.play("CHarged Shot", false, 0.04); S.cool = 0.5; }
    else if (c.on) { chargeStop(0.12); if (arm) arm.play("IDLE", true, 0.15); }
    S.charge = { on: false, t: 0, full: false, snd: null, loop: null, wait: 0 };
  }
  Game.fireDown = fireDown; Game.fireUp = fireUp;

  // =====================================================================
  //  UPDATE
  // =====================================================================
  function updateCharge(dt) {
    const c = S.charge;
    if (S.firing && !c.on) {
      if (c.wait > 0) c.wait -= dt;
      else if (S.cool <= 0) { c.on = true; c.t = 0; c.full = false; if (arm) arm.play("charge", false, 0.1); c.snd = snd("armCannon.chargeStart"); }
    }
    if (c.on) {
      c.t += dt;
      if (arm && arm.done && arm.clip === "charge") arm.play("Charge-loop", true, 0.05);
      if (!c.full && c.t >= CFG.beam.chargeTime) { c.full = true; if (!c.loop) c.loop = snd("armCannon.chargeLoop"); }
    }
    if (arm && !c.on && arm.done && (arm.clip === "shot" || arm.clip === "CHarged Shot")) arm.play("IDLE", true, 0.12);
  }
  function damagePlayer(n) {
    S.P.energy -= n; S.hurt = 0.5; S.shake = Math.max(S.shake, 0.18); snd("player.hurt");
    if (S.P.energy <= 0) { S.P.energy = 0; endGame(false); }
  }
  function updateEnemies(dt) {
    const P = S.P;
    for (const e of S.enemies) {
      if (e.dead) continue;
      e.ph += dt; e.flash = Math.max(0, e.flash - dt);
      const dx = P.x - e.x, dz = P.z - e.z, d = Math.hypot(dx, dz);
      const sees = d < CFG.enemy.sight && los(e.x, e.y, e.z, P.x, P.y - 0.2, P.z);
      if (sees) e.awake = true;
      e.yaw = Math.atan2(-dx, -dz); e.by = 1.7 + Math.sin(e.ph * 2) * 0.12;
      if (!e.awake) continue;
      e.strafeT -= dt; if (e.strafeT <= 0) { e.strafe = -e.strafe; e.strafeT = 1.2 + Math.random() * 2; }
      const want = d > 11 ? 1 : d < 6 ? -1 : 0, nx = dx / (d || 1), nz = dz / (d || 1);
      moveXZ(e, (nx * want - nz * e.strafe * 0.7) * CFG.enemy.speed * dt, (nz * want + nx * e.strafe * 0.7) * CFG.enemy.speed * dt, 0.5);
      e.y = e.by; e.cool -= dt;
      if (e.wind > 0) { e.wind -= dt; if (e.wind <= 0) {
          const tx = P.x, ty = P.y - 0.3, tz = P.z, l = Math.hypot(tx - e.x, ty - e.y, tz - e.z) || 1, sp = 12;
          S.shots.push({ x: e.x, y: e.y, z: e.z, vx: (tx - e.x) / l * sp, vy: (ty - e.y) / l * sp, vz: (tz - e.z) / l * sp, mine: false, life: 4, charged: false, trail: [] });
          snd("drone.shot", { pos: [e.x, e.y, e.z] }); } }
      else if (sees && e.cool <= 0 && d < 24) { e.wind = 0.55; e.cool = 2.2 + Math.random() * 1.6; }
    }
    for (let i = 0; i < S.enemies.length; i++) for (let j = i + 1; j < S.enemies.length; j++) {
      const a = S.enemies[i], b = S.enemies[j]; if (a.dead || b.dead) continue;
      const dx = b.x - a.x, dz = b.z - a.z, d = Math.hypot(dx, dz);
      if (d < 1.3 && d > 0.001) { const k = (1.3 - d) / 2; moveXZ(a, -dx / d * k, -dz / d * k, 0.5); moveXZ(b, dx / d * k, dz / d * k, 0.5); }
    }
  }
  function killEnemy(e) {
    e.dead = true; flashFx(e.x, e.y, e.z, 2.4, [1, 0.6, 0.3], 0.5); spark(e.x, e.y, e.z, 26, [1, 0.6, 0.3], 6, 0.9);
    snd("drone.death", { pos: [e.x, e.y, e.z] });
    if (Math.random() < 0.7) S.pickups.push({ x: e.x, y: 0.7, z: e.z, kind: "energy", t: 0, small: true });
    if (S.lock === e) S.lock = null;
    if (S.enemies.every(x => x.dead)) { S.won = 1.4; toast("AREA SECURED", 3); }
  }
  function updateShots(dt) {
    const P = S.P;
    for (let i = S.shots.length - 1; i >= 0; i--) {
      const s = S.shots[i]; let gone = false;
      for (let k = 0; k < 3 && !gone; k++) {
        const h = dt / 3; s.x += s.vx * h; s.y += s.vy * h; s.z += s.vz * h; s.life -= h;
        if (s.life <= 0) { gone = true; break; }
        const cx = Math.floor(s.x / L.TILE), cz = Math.floor(s.z / L.TILE);
        if (s.y < 0.02 || s.y > L.WALL_H - 0.02 || L.solidCell(cx, cz)) {
          const d = L.doorAt(cx, cz);
          if (d && s.mine && d.open < 0.1 && s.y >= 0 && s.y <= L.WALL_H) { d.target = 1; d.hit = 0.25; snd("door.open"); toast("DOOR UNLOCKED", 1.4); }
          impact(s.x - s.vx * 0.01, clamp(s.y, 0.05, L.WALL_H - 0.05), s.z - s.vz * 0.01, s.charged, s.mine); gone = true; break; }
        if (s.mine) { for (const e of S.enemies) { if (e.dead) continue;
            if (Math.hypot(s.x - e.x, s.y - e.y, s.z - e.z) < 0.7) { e.hp -= s.charged ? CFG.beam.chargedDamage : CFG.beam.damage; e.flash = 0.12; e.awake = true; S.hit = 0.14; ST().hits++;
              impact(s.x, s.y, s.z, s.charged, false); snd("drone.hit", { pos: [e.x, e.y, e.z] }); if (e.hp <= 0) killEnemy(e); gone = true; break; } } }
        else if (Math.hypot(s.x - P.x, s.z - P.z) < 0.55 && s.y > 0 && s.y < 2.0) { damagePlayer(CFG.enemy.damage); impact(s.x, s.y, s.z, false, false); gone = true; }
      }
      s.trail.unshift([s.x, s.y, s.z]); if (s.trail.length > 6) s.trail.pop();
      if (gone) S.shots.splice(i, 1);
    }
  }
  function updateFx(dt) {
    for (let i = S.fx.length - 1; i >= 0; i--) { const f = S.fx[i]; f.life -= dt;
      if (f.k === "spark") { f.x += f.vx * dt; f.y += f.vy * dt; f.z += f.vz * dt; f.vy -= 9 * dt; if (f.y < 0.02) { f.y = 0.02; f.vy *= -0.3; f.vx *= 0.6; f.vz *= 0.6; } }
      if (f.life <= 0) S.fx.splice(i, 1); }
    for (const d of L.doors) { d.open = clamp(d.open + (d.target - d.open) * Math.min(1, dt * 3.2) + (d.target > d.open ? dt * 0.2 : 0), 0, 1); d.hit = Math.max(0, d.hit - dt); }
  }
  function updatePickups(dt) {
    for (let i = S.pickups.length - 1; i >= 0; i--) { const p = S.pickups[i]; p.t += dt;
      if (Math.hypot(p.x - S.P.x, p.z - S.P.z) < 0.9 && S.P.energy < CFG.player.maxEnergy) {
        const add = p.small ? 10 : 30; S.P.energy = Math.min(CFG.player.maxEnergy, S.P.energy + add); toast("ENERGY +" + add, 1.1); snd("world.pickup"); S.pickups.splice(i, 1); } }
  }
  function update(dt) {
    const P = S.P; S.time += dt; ST().time += dt; S.cool = Math.max(0, S.cool - dt);
    S.flash = Math.max(0, S.flash - dt); S.hit = Math.max(0, S.hit - dt); S.hurt = Math.max(0, S.hurt - dt); S.shake = Math.max(0, S.shake - dt * 1.4);
    const wantLock = !!(keys.ShiftLeft || keys.ShiftRight || S.rmb);
    if (wantLock && !S.lock) { S.lockT += dt; if (S.lockT === dt || S.lockT > 0.25) { acquireLock(); S.lockT = 0.0001; } }
    if (!wantLock) { S.lock = null; S.lockT = 0; }
    if (S.lock && (S.lock.dead || !los(P.x, P.y, P.z, S.lock.x, S.lock.y, S.lock.z))) { if (S.lock.dead) S.lock = null; else { S.lockLost += dt; if (S.lockLost > 0.8) { S.lock = null; S.lockLost = 0; } } } else S.lockLost = 0;
    if (S.lock) {
      const e = S.lock, dx = e.x - P.x, dz = e.z - P.z, dy = e.y - P.y, ty = Math.atan2(-dx, -dz), tp = Math.atan2(dy, Math.hypot(dx, dz)), k = Math.min(1, dt * 11);
      P.yaw += angDiff(P.yaw, ty) * k; P.pitch += (tp - P.pitch) * k;
    }
    const ls = 2.0 * dt * CFG.player.sensitivity;
    if (!S.lock) { if (keys.ArrowLeft) P.yaw += ls; if (keys.ArrowRight) P.yaw -= ls; if (keys.ArrowUp) P.pitch = clamp(P.pitch + ls, -1.3, 1.3); if (keys.ArrowDown) P.pitch = clamp(P.pitch - ls, -1.3, 1.3); }
    let fx = 0, fz = 0; if (keys.KeyW) fz += 1; if (keys.KeyS) fz -= 1; if (keys.KeyA) fx -= 1; if (keys.KeyD) fx += 1;
    const len = Math.hypot(fx, fz) || 1, sp = CFG.player.speed, sy = Math.sin(P.yaw), cy = Math.cos(P.yaw), mv = (fx || fz) ? 1 : 0, a = Math.min(1, dt * 12);
    P.vx += ((-sy * fz + cy * fx) / len * sp * mv - P.vx) * a; P.vz += ((-cy * fz - sy * fx) / len * sp * mv - P.vz) * a;
    moveXZ(P, P.vx * dt, P.vz * dt, 0.35);
    S.bob += dt * Math.hypot(P.vx, P.vz) * 1.9;
    updateCharge(dt); updateEnemies(dt); updateShots(dt); updateFx(dt); updatePickups(dt);
    if (arm) arm.update(dt);
    S.recoil = Math.max(0, S.recoil - dt * 5); S.sway.x *= Math.pow(0.003, dt); S.sway.y *= Math.pow(0.003, dt);
    if (S.toastT > 0) { S.toastT -= dt; if (S.toastT <= 0) $("toast").style.opacity = 0; }
    if (S.won > 0) { S.won -= dt; if (S.won <= 0) endGame(true); }
  }
  function updateEdit(dt) {
    S.time += dt; if (arm) arm.update(dt);
    for (const e of S.enemies) { if (e.dead) continue; e.ph += dt; e.by = 1.7 + Math.sin(e.ph * 2) * 0.12; e.y = e.by; }
    S.recoil = Math.max(0, S.recoil - dt * 5); updateFx(dt);
  }
  Game.update = update;

  // =====================================================================
  //  DRAW
  // =====================================================================
  function camMatrices(yaw, pitch, pos, sh) {
    const view = M4.mul(M4.rotX(-pitch - sh[1]), M4.mul(M4.rotY(-yaw - sh[0]), M4.trans(-pos[0], -pos[1], -pos[2])));
    VP = M4.mul(M4.persp(CFG.display.fov, aspect, 0.08, 120), view);
    camR = [Math.cos(yaw), 0, -Math.sin(yaw)]; camU = [Math.sin(yaw) * Math.sin(pitch), Math.cos(pitch), Math.cos(yaw) * Math.sin(pitch)];
  }
  function setLights(cam) {
    const arr = new Float32Array(32), col = new Float32Array(24); let n = 0;
    const add = (x, y, z, r, c, k) => { if (n >= 8) return; arr.set([x, y, z, r], n * 4); col.set([c[0] * k, c[1] * k, c[2] * k], n * 3); n++; };
    for (const f of S.fx) if (f.k === "flash" && f.light) add(f.x, f.y, f.z, 9, f.col, 1.8 * (f.life / f.max));
    for (const s of S.shots) add(s.x, s.y, s.z, s.charged ? 11 : 6, s.mine ? [1, 0.7, 0.3] : [1, 0.3, 0.2], s.charged ? 1.6 : 0.9);
    if (S.flash > 0) add(cam[0], cam[1], cam[2], 8, [1, 0.8, 0.5], 1.6 * S.flash / 0.1);
    const near = L.LIGHTS.map(l => ({ l, d: Math.hypot(l.x * L.TILE - cam[0], l.z * L.TILE - cam[2]) })).sort((a, b) => a.d - b.d);
    for (const { l } of near) add(l.x * L.TILE, L.WALL_H - 0.7, l.z * L.TILE, l.r * 1.3, l.col, 1.25);
    while (n < 8) { arr.set([0, 0, 0, 0.0001], n * 4); n++; }
    return [arr, col];
  }
  function render(camPos, yaw, pitch, shk) {
    const FOG = G.FOG_COL, PR = G.PROG;
    gl.viewport(0, 0, W, H); gl.clearColor(FOG[0], FOG[1], FOG[2], 1); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.enable(gl.DEPTH_TEST); gl.depthFunc(gl.LEQUAL); gl.disable(gl.CULL_FACE); gl.disable(gl.BLEND);
    camMatrices(yaw, pitch, camPos, shk);
    const [lp, lc] = setLights(camPos);
    let p = PR.world; gl.useProgram(p); G.attribs(3);
    gl.uniformMatrix4fv(p.u("uVP"), false, VP); gl.uniform3fv(p.u("uCam"), camPos); gl.uniform3fv(p.u("uFogCol"), FOG); gl.uniform2f(p.u("uFog"), 6, 46);
    gl.uniform3f(p.u("uAmb"), 0.21, 0.26, 0.33); gl.uniform4fv(p.u("uLP"), lp); gl.uniform3fv(p.u("uLC"), lc);
    for (const k in L.batches) { const b = L.batches[k]; gl.bindTexture(gl.TEXTURE_2D, b.tex); gl.bindBuffer(gl.ARRAY_BUFFER, b.buf);
      gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 24, 0); gl.vertexAttribPointer(1, 2, gl.FLOAT, false, 24, 12); gl.vertexAttribPointer(2, 1, gl.FLOAT, false, 24, 20); gl.drawArrays(gl.TRIANGLES, 0, b.n); }
    p = PR.model; gl.useProgram(p); G.attribs(3);
    gl.uniform3f(p.u("uAmb"), 0.55, 0.65, 0.8); gl.uniform3f(p.u("uLDir"), 0.4, 0.9, 0.3); gl.uniform3f(p.u("uLCol"), 0.7, 0.7, 0.7);
    gl.uniform3fv(p.u("uCam"), camPos); gl.uniform3fv(p.u("uFogCol"), FOG); gl.uniform2f(p.u("uFog"), 6, 46); gl.uniform1f(p.u("uFogOn"), 1);
    for (const d of L.doors) { const cx = (d.cx + 0.5) * L.TILE, cz = (d.cz + 0.5) * L.TILE, lift = d.open * (L.WALL_H + 0.2);
      const model = M4.mul(M4.trans(cx, L.WALL_H / 2 + lift, cz), d.axis === "x" ? M4.ident() : M4.rotY(Math.PI / 2));
      G.drawModelMesh(MESH.slab, M4.mul(VP, model), model, L.TEX.door, [1, 1, 1], [0.05 + d.hit * 1.5, 0.12 + d.hit, 0.3]); }
    for (const e of S.enemies) { if (e.dead) continue;
      const base = M4.mul(M4.trans(e.x, e.by, e.z), M4.mul(M4.rotY(e.yaw), M4.rotZ(Math.sin(e.ph * 1.3) * 0.12)));
      const fl = e.flash > 0 ? 0.7 : 0, wd = e.wind > 0 ? 0.6 : 0, bodyM = M4.mul(base, M4.rotX(e.ph * 0.9)), spM = M4.mul(base, M4.rotY(Math.PI / 2));
      G.drawModelMesh(MESH.body, M4.mul(VP, bodyM), bodyM, L.TEX.metal, [0.5, 0.58, 0.68], [fl, fl, fl]);
      G.drawModelMesh(MESH.wing, M4.mul(VP, base), base, L.TEX.metal, [0.3, 0.36, 0.45], [fl, fl, fl]);
      G.drawModelMesh(MESH.spine, M4.mul(VP, spM), spM, L.TEX.metal, [0.25, 0.3, 0.38], [wd * 0.6 + fl, wd * 0.2 + fl, fl]); }
    p = PR.sprite; gl.useProgram(p); G.attribs(1); gl.bindBuffer(gl.ARRAY_BUFFER, G.QUAD); gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
    gl.uniformMatrix4fv(p.u("uVP"), false, VP); gl.uniform3fv(p.u("uR"), camR); gl.uniform3fv(p.u("uU"), camU);
    gl.enable(gl.BLEND); gl.blendFunc(gl.ONE, gl.ONE); gl.depthMask(false);
    for (const l of L.LIGHTS) G.drawSprite(l.x * L.TILE, L.WALL_H - 0.05, l.z * L.TILE, 2.6, [l.col[0] * 0.5, l.col[1] * 0.5, l.col[2] * 0.5], 2.5);
    for (const e of S.enemies) { if (e.dead) continue; const f = fwdVec(e.yaw, 0), g = 0.5 + (e.wind > 0 ? 1.1 : 0) + Math.sin(e.ph * 4) * 0.1;
      G.drawSprite(e.x + f[0] * 0.34, e.by, e.z + f[2] * 0.34, 0.4 + (e.wind > 0 ? 0.35 : 0), [1, 0.25 + (e.wind > 0 ? 0.4 : 0), 0.12, g], 1.6); }
    for (const pk of S.pickups) G.drawSprite(pk.x, 0.7 + Math.sin(pk.t * 3) * 0.1, pk.z, pk.small ? 0.35 : 0.6, [0.3, 0.8, 1, 0.9], 1.4);
    for (const s of S.shots) { const c = s.mine ? (s.charged ? [1, 0.82, 0.5] : [1, 0.7, 0.25]) : [1, 0.28, 0.2], sz = s.mine ? (s.charged ? 0.7 : 0.32) : 0.4;
      G.drawSprite(s.x, s.y, s.z, sz * 2.4, [c[0], c[1], c[2], 0.55], 2.2); G.drawSprite(s.x, s.y, s.z, sz * 0.9, [1, 1, 0.9, 1], 1.3);
      for (let i = 1; i < s.trail.length; i += 2) { const t = s.trail[i], k = 1 - i / s.trail.length; G.drawSprite(t[0], t[1], t[2], sz * 1.1 * k, [c[0], c[1], c[2], 0.5 * k], 1.8); } }
    for (const f of S.fx) { const k = f.life / f.max;
      if (f.k === "flash") G.drawSprite(f.x, f.y, f.z, f.size * (1.4 - k * 0.6), [f.col[0], f.col[1], f.col[2], k], 1.8);
      else G.drawSprite(f.x, f.y, f.z, f.size, [f.col[0], f.col[1], f.col[2], Math.min(1, k * 2)], 1.2); }
    gl.depthMask(true); gl.disable(gl.BLEND);
  }
  // the arm cannon is drawn last on a fresh depth buffer so it never clips into walls
  function renderArm() {
    if (!arm) return;
    gl.clear(gl.DEPTH_BUFFER_BIT);
    const sw = S.sway, spd = Math.hypot(S.P.vx, S.P.vz), bobA = Math.min(1, spd / 4);
    const place = armPlace(Math.sin(S.bob * 0.5) * 0.012 * bobA, Math.abs(Math.cos(S.bob * 0.5)) * 0.014 * bobA, clamp(sw.x, -1, 1), clamp(sw.y, -1, 1), S.recoil, S.lock ? 0.03 : 0);
    const proj = M4.persp(62, aspect, 0.02, 10), c = S.charge, k = c.on ? Math.min(1, c.t / CFG.beam.chargeTime) : 0, f = S.flash > 0 ? S.flash : 0;
    let p = G.PROG.model; gl.useProgram(p); G.attribs(3);
    gl.uniform3f(p.u("uAmb"), 0.75, 0.82, 0.95); gl.uniform3f(p.u("uLDir"), 0.35, 0.8, 0.6); gl.uniform3f(p.u("uLCol"), 0.95, 0.92, 0.85); gl.uniform1f(p.u("uFogOn"), 0);
    arm.draw(place, proj, [f * 3 + k * 0.17, f * 2 + k * 0.12, f + k * 0.04]);
    if (k > 0.05 || f > 0) {
      p = G.PROG.sprite; gl.useProgram(p); G.attribs(1); gl.bindBuffer(gl.ARRAY_BUFFER, G.QUAD); gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
      gl.uniformMatrix4fv(p.u("uVP"), false, proj); gl.uniform3f(p.u("uR"), 1, 0, 0); gl.uniform3f(p.u("uU"), 0, 1, 0);
      gl.enable(gl.BLEND); gl.blendFunc(gl.ONE, gl.ONE); gl.depthMask(false);
      const mp = muzzleView(), pulse = c.full ? 1 + Math.sin(S.time * 30) * 0.12 : 1;
      if (k > 0.05) { G.drawSprite(mp[0], mp[1], mp[2], (0.05 + k * 0.17) * pulse, [1, 0.72, 0.28, 0.9], 1.8); G.drawSprite(mp[0], mp[1], mp[2], (0.02 + k * 0.06) * pulse, [1, 1, 0.9, 1], 1.4); }
      if (f > 0) G.drawSprite(mp[0], mp[1], mp[2], 0.28 * (f / 0.14 + 0.4), [1, 0.85, 0.5, 1], 1.5);
      gl.depthMask(true); gl.disable(gl.BLEND);
    }
  }

  // =====================================================================
  //  HUD + SCREENS
  // =====================================================================
  let lastE = -1, lastH = -1, swayX = 0, swayY = 0, prevYaw = 0, prevPitch = 0;
  function hudStats() {
    const e = Math.ceil(S.P.energy);
    if (e !== lastE) { lastE = e; $("eNum").textContent = String(e).padStart(3, "0");
      const on = Math.ceil(e / CFG.player.maxEnergy * 20), cells = $("cells"); [...cells.children].forEach((c, i) => c.classList.toggle("on", i < on));
      cells.className = e < CFG.player.maxEnergy * 0.25 ? "crit" : e < CFG.player.maxEnergy * 0.5 ? "low" : ""; }
    const h = S.enemies.filter(x => !x.dead).length; if (h !== lastH) { lastH = h; $("hNum").textContent = h; }
  }
  function hudFrame(dt) {
    const c = S.charge, k = c.on ? Math.min(1, c.t / CFG.beam.chargeTime) : 0, playing = S.mode === "play";
    $("chg").setAttribute("stroke-dasharray", (k * 188.5).toFixed(1) + " 189"); $("chg").classList.toggle("full", !!c.full);
    $("reticle").classList.toggle("hit", S.hit > 0); $("vignette").style.opacity = S.hurt > 0 ? Math.min(1, S.hurt / 0.5) : 0;
    const hint = $("hint"); hint.style.opacity = (playing && S.time < 14) ? 0.9 : 0;
    hint.textContent = S.time < 5 ? "HOLD FIRE TO CHARGE  //  SHOOT THE DOOR" : "HOLD RIGHT CLICK TO LOCK ON";
    const lockEl = $("lock");
    if (playing && S.lock && !S.lock.dead) {
      const e = S.lock, v = [e.x, e.y, e.z], m = VP, x = m[0]*v[0] + m[4]*v[1] + m[8]*v[2] + m[12], y = m[1]*v[0] + m[5]*v[1] + m[9]*v[2] + m[13], w = m[3]*v[0] + m[7]*v[1] + m[11]*v[2] + m[15];
      if (w > 0.05) { lockEl.style.display = "block"; lockEl.style.left = ((x / w * 0.5 + 0.5) * 100) + "%"; lockEl.style.top = ((1 - (y / w * 0.5 + 0.5)) * 100) + "%";
        lockEl.style.transform = "rotate(" + ((S.time * 90) % 360).toFixed(0) + "deg)"; $("lockHp").style.width = (e.hp / CFG.enemy.hp * 100) + "%"; } else lockEl.style.display = "none";
    } else lockEl.style.display = "none";
    const dy = angDiff(prevYaw, S.P.yaw), dp = S.P.pitch - prevPitch; prevYaw = S.P.yaw; prevPitch = S.P.pitch;
    if (CFG.display.hudSway && playing) { swayX += (clamp(-dy * 260, -14, 14) - swayX) * Math.min(1, dt * 8); swayY += (clamp(dp * 260, -10, 10) + Math.sin(S.bob) * 2.4 * Math.min(1, Math.hypot(S.P.vx, S.P.vz) / 4) - swayY) * Math.min(1, dt * 8);
      $("sway").style.transform = `translate(${swayX.toFixed(1)}px, ${swayY.toFixed(1)}px) scale(1.025)`; } else $("sway").style.transform = "none";
    hudStats();
  }
  function show(id) { for (const s of ["pause", "end"]) $(s).hidden = s !== id; }
  function lockPointer() { try { const r = canvas.requestPointerLock(); if (r && r.catch) r.catch(() => { S.noLockSupport = true; }); } catch (e) { S.noLockSupport = true; } }
  Game.startPlay = function () {
    A.init(CFG.display.volume); A.resume(); resetLevel(); show(null); S.mode = Game.mode = "play"; S.time = 0; $("hud").classList.remove("paused"); lockPointer(); lastT = performance.now();
    document.getElementById("app").classList.add("playing"); Game.resize();
  };
  Game.pause = function () { if (S.mode !== "play") return; S.mode = Game.mode = "pause"; S.firing = false; S.rmb = false; chargeStop(0.05); $("hud").classList.add("paused"); show("pause"); if (document.pointerLockElement) document.exitPointerLock(); };
  Game.resume = function () { A.resume(); $("hud").classList.remove("paused"); show(null); S.mode = Game.mode = "play"; lockPointer(); lastT = performance.now(); };
  Game.exitToEdit = function () {
    chargeStop(0.05); if (document.pointerLockElement) document.exitPointerLock();
    S.mode = Game.mode = "edit"; show(null); $("hud").classList.remove("paused"); document.getElementById("app").classList.remove("playing");
    resetLevel(); S.P.yaw = -Math.PI / 2 + 0.28; lastE = -1; lastH = -1; Game.resize(); if (Game.onExit) Game.onExit();
  };
  function endGame(won) {
    if (S.mode !== "play") return; S.mode = Game.mode = "end"; S.firing = false; chargeStop(0.05); if (document.pointerLockElement) document.exitPointerLock();
    $("endEyebrow").textContent = won ? "AREA SECURED" : "ENERGY DEPLETED"; $("endTitle").textContent = won ? "Chamber cleared" : "Signal lost";
    const m = Math.floor(ST().time / 60), s = Math.floor(ST().time % 60);
    $("sTime").textContent = `${m}:${String(s).padStart(2, "0")}`; $("sAcc").textContent = (ST().shots ? Math.round(ST().hits / ST().shots * 100) : 0) + "%"; $("sChg").textContent = ST().charged;
    show("end");
  }
  Game.damagePlayer = damagePlayer; Game.endGame = endGame;
  Game.previewClip = name => { if (arm && S.mode === "edit") arm.play(name, true, 0.1); };
  Game.look = (dx, dy) => { if (S.mode !== "edit") return; S.P.yaw -= dx * 0.004; S.P.pitch = clamp(S.P.pitch - dy * 0.004, -0.9, 0.9); };
  Game.setVisorVisible = v => { $("visor").style.visibility = v ? "visible" : "hidden"; };
  Game.setHudVisible = v => { $("hud").hidden = !v; };

  // =====================================================================
  //  INPUT
  // =====================================================================
  const wasd = ["Space", "ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight", "ShiftLeft", "ShiftRight"];
  addEventListener("keydown", e => {
    if (S.mode === "play" && !(e.target && /INPUT|SELECT|TEXTAREA/.test(e.target.tagName))) { keys[e.code] = true; if (wasd.includes(e.code)) e.preventDefault(); }
    if (S.mode === "play" && e.code === "Escape") Game.pause();
  });
  addEventListener("keyup", e => { keys[e.code] = false; });
  addEventListener("blur", () => { for (const k in keys) keys[k] = false; if (S.firing) { S.firing = false; if (S.mode === "play") fireUp(); } S.rmb = false; });
  document.addEventListener("mousemove", e => {
    if (S.mode !== "play" || (!S.locked && !S.noLockSupport) || S.lock) return;
    const s = 0.0022 * CFG.player.sensitivity;
    S.P.yaw -= e.movementX * s; S.P.pitch = clamp(S.P.pitch - e.movementY * s, -1.3, 1.3); S.sway.x += e.movementX * 0.03; S.sway.y += e.movementY * 0.03;
  });
  function bindCanvas() {
    canvas.addEventListener("mousedown", e => {
      if (S.mode !== "play") return;
      if (!S.locked && !S.noLockSupport) { lockPointer(); return; }
      if (e.button === 0) { S.firing = true; fireDown(); } if (e.button === 2) S.rmb = true;
    });
    canvas.addEventListener("contextmenu", e => e.preventDefault());
  }
  addEventListener("mouseup", e => { if (e.button === 0 && S.firing) { S.firing = false; if (S.mode === "play") fireUp(); } if (e.button === 2) S.rmb = false; });
  document.addEventListener("pointerlockchange", () => { const was = S.locked; S.locked = document.pointerLockElement === canvas; if (was && !S.locked && S.mode === "play") Game.pause(); });
  document.addEventListener("pointerlockerror", () => { S.noLockSupport = true; });
  document.addEventListener("visibilitychange", () => { if (document.hidden) Game.pause(); });
  $("resumeBtn").onclick = () => Game.resume();
  $("restartBtn").onclick = () => { resetLevel(); Game.resume(); };
  $("exitBtn").onclick = () => Game.exitToEdit(); $("exitBtn2").onclick = () => Game.exitToEdit();
  $("againBtn").onclick = () => Game.startPlay();
  Game.bindCanvas = bindCanvas;

  // =====================================================================
  //  LOOP
  // =====================================================================
  function frame(now) {
    requestAnimationFrame(frame);
    const dt = Math.min(0.05, (now - lastT) / 1000); lastT = now;
    if (S.mode === "play") { if (!Game.manual) update(dt); } else if (S.mode === "edit") updateEdit(dt);
    const shk = [Math.sin(S.time * 63) * S.shake * 0.01, Math.cos(S.time * 71) * S.shake * 0.01];
    render(eyePos(), S.P.yaw, S.P.pitch, shk); renderArm();
    hudFrame(dt); A.setListener(S.P, S.P.yaw);
  }
})();
