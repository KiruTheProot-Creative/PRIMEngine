// Drones (circle, stop, shoot from the muzzle / dash), enemy muzzle, pick-up hum sound, several solid boxes, weapon take-out conditions. Frames stepped by hand.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/Arm_canon.fbx', SP + '/beep.wav']); await p.waitForTimeout(2000);
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(500);
  await p.click('button:has-text("New weapon")'); await p.click('.pitem:has-text("Arm_canon")'); await p.waitForTimeout(1500);
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, T = S.tags, m = pr.maps[0], arm = pr.assets.find(a => /arm/i.test(a.id + a.name)).id, snd = pr.assets.find(a => a.type === 'sound').id;
    const att = { id: 'att9' }; pr.player.attachments.push(att); Sc.defineAttach(pr, att, { type: 'weapon', name: 'Missile', asset: arm }); att.script = { nodes: [], links: [] }; Sc.ensurePlayer(pr);
    pr.tags.hasMissile = { type: 'bool', value: false, label: 'Has missiles', group: 'custom', custom: true };
    T.setMany({ 'player.att9.useNeedTag': 'hasMissile', 'player.att9.useNeedVal': 'yes', 'player.att9.useLockMsg': 'No missiles yet', 'player.health': 100 });
    const dr = Sc.addProp(pr, { name: 'Drone', type: 'enemy', asset: arm, behavior: 'drone', attack: 'projectile', projSpeed: 700, damage: 5, speed: 300, sight: 4000, hover: 80, orbitDist: 260, droneWaitMin: 0.4, droneWaitMax: 0.5, droneStop: 0.2, droneAttack: 'shoot', droneShots: 2, droneShotGap: 0.15, muzzleOn: true, muzzleX: 30, muzzleY: 50, muzzleZ: 40 });
    const pk = Sc.addProp(pr, { name: 'Orb', type: 'pickup', asset: arm, giveTag: 'player.health', giveAmount: 5, humSound: snd, humRadius: 2000 });
    const dk = Sc.addProp(pr, { name: 'Desk', type: 'decoration', asset: arm, solid: 'box' }); Sc.ensureSolids(pr); const desk = pr.props.find(x => x.id === dk.id); Sc.addSolidBox(pr, desk, { x: 80, y: 20, z: 0, w: 40, h: 40, d: 40 });
    window.__ids = { dr: dr.id, pk: pk.id, dk: dk.id, boxes: desk.solids.length };
    const sx = Math.floor(+T.get(m.id + '.startX') / 128), sy = Math.floor(+T.get(m.id + '.startY') / 128); window.__start = [sx, sy];
    const C = c => (c + 0.5) * 128; m.things = [{ id: 'd1', prop: dr.id, x: C(sx + 3), y: C(sy), ang: 0, layer: 0 }, { id: 'p1', prop: pk.id, x: C(sx - 2), y: C(sy - 2), ang: 0, layer: 0 }, { id: 'k1', prop: dk.id, x: C(sx - 3), y: C(sy + 3), ang: 0, layer: 0 }]; m.n.t = 9; T.emit([]); });
  console.log('setup', JSON.stringify(await p.evaluate(() => [window.__ids, window.__start])));
  const calls = []; await p.exposeFunction('__snd', s => calls.push(s));
  await p.evaluate(() => { const o = PRIM.Audio.play; PRIM.Audio.play = function (u, v, loop, at) { window.__snd((loop ? 'loop ' : '') + (at ? 'at' : '')); return o.apply(this, arguments); }; });
  await p.evaluate(() => { __studio.S.objView = false; }); await p.click('.tab:has-text("Timeline")'); await p.waitForTimeout(200); await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(400);
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.things && __studio.S.play.things.length >= 3, null, { timeout: 15000 }); await p.waitForTimeout(500);
  const go = (n, fn) => p.evaluate(([n, fn]) => { const q = __studio.S.play; q.msg.style.display = 'none'; const r0 = fn ? (new Function('q', fn))(q) : null; let t = q.last || performance.now(); const modes = new Set(), d = q.things.find(x => x.th.id === 'd1'); let shot = null, minD = 1e9, maxD = 0;
    for (let i = 0; i < n; i++) { t += 16; q.frame(t); cancelAnimationFrame(q.raf); if (d.dr) modes.add(d.dr.m); const f = (q.fx || []).find(x => x.hostile && x.t < 0.02); if (f && !shot) { const mz = q.eMuzzle(d); shot = { gap: +Math.hypot(f.p[0] - mz.p[0], f.p[1] - mz.p[1], f.p[2] - mz.p[2]).toFixed(1), y: Math.round(f.p[1]) }; }
      if (i > n / 2) { const dd = Math.hypot(d.x - q.x, d.y - q.y); minD = Math.min(minD, dd); maxD = Math.max(maxD, dd); } } q.raf = requestAnimationFrame(z => q.frame(z));
    return JSON.stringify({ r0, modes: [...modes], lift: Math.round(d.lift || 0), dist: [Math.round(minD), Math.round(maxD)], shot, hp: q.T.get('player.health'), dtris: (q.dtris || []).length }); }, [n, fn || '']);
  console.log('drone shoots:', await go(240, 'const d = q.things.find(x => x.th.id === "d1"); d.awake = true; return null;'));
  console.log('drone dashes:', await go(260, 'q.T.set("' + 'x' + '", 0); const d = q.things.find(x => x.th.id === "d1"); q.T.set(d.pre + "droneAttack", "dash"); q.T.set(d.pre + "droneDashSpeed", 1400); q.T.set("player.health", 100); return null;'));
  console.log('hum sounds:', JSON.stringify(calls.filter(c => c.startsWith('loop')).slice(0, 3)));
  const w = await p.evaluate(() => { const q = __studio.S.play, T = q.T, r = {}; q.setWeapon(1); r.locked = q.curWeaponId(); r.toast = q.toastEl && q.toastEl.textContent; q.setWeapon(0); q.setWeapon(1, 1); r.wheel = q.curWeaponId();
    T.set('hasMissile', true); q.setWeapon(1); r.after = q.curWeaponId(); T.set('hasMissile', false); q.weaponCheck(); r.dropped = q.curWeaponId();
    T.set('player.' + q.wids()[0] + '.useNeedTag', 'hasMissile'); q.weaponCheck(); r.none = q.curWeaponId(); r.unarmed = !!q.unarmed; T.set('hasMissile', true); q.weaponCheck(); r.back = q.curWeaponId(); return r; });
  console.log('weapons:', JSON.stringify(w));
};
