module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n').slice(1, 3).join(' | ')));
  await p.click('button:has-text("New project")'); await p.waitForSelector('.pe-app'); await p.waitForTimeout(800);
  await p.evaluate(() => { const S = __pe, D = PRIM.PE, P = S.project, r = P.rooms[0];
    D.addThing(P, r, 'enemy', [0, 0, 420], 180, { name: 'Crawler', health: 30, speed: 0, scanTitle: 'Crawler', scanText: 'A slow cave dweller.', scanCategory: 'Creatures' });
    D.addThing(P, r, 'scan', [-300, 120, 0], 0, { scanTitle: 'Old Console', scanText: 'Still humming.', scanCategory: 'Research' });
    D.addThing(P, r, 'pickup', [0, 0, 120], 0, { item: 'missileLauncher', amount: 5 });
    D.addThing(P, r, 'pickup', [0, 0, 200], 0, { item: 'visor', visor: 'thermal' });
    D.addThing(P, r, 'door', [0, 0, -560], 0, { lock: 'missile', closeAfter: 0 });
    S.tags.emit([]); });
  await p.evaluate(() => __pe.play(false)); await p.waitForTimeout(800);
  await p.evaluate(() => { const g = __pe.game; g.o.autoplay = false; g.msg.style.display = 'none'; g.yaw = Math.PI / 2; g.pitch = 0; window.sim = s => { for (let i = 0; i < s * 60; i++) g.update(1 / 60); }; });
  const sim = s => p.evaluate(s => window.sim(s), s);
  const st = () => p.evaluate(() => { const g = __pe.game, e = g.things.find(o => o.kind === 'enemy'), d = g.things.find(o => o.kind === 'door'); return JSON.stringify({ pos: g.body.pos.map(Math.round), ground: g.body.ground, hp: Math.round(g.hp), missiles: g.missiles, visors: [...g.inv.visors], enemy: e && [e.st, Math.round(e.hp)], door: d && [d.state, +d.open.toFixed(2)], lock: !!(g.lock && g.lock.target), scanned: [...g.scanned] }); });
  await p.waitForTimeout(500); console.log('start', await st());
  await p.evaluate(() => { __pe.game.keys.w = true; }); await sim(1.2); await p.evaluate(() => { __pe.game.keys.w = false; }); await sim(0.2); console.log('walked +z (picked up items?)', await st());
  await p.screenshot({ path: out + '_g1.png' });
  await p.evaluate(() => { const g = __pe.game; g.body.pos = [0, 0, 0]; g.yaw = Math.PI / 2; g.pitch = 0; g.press('mouse2'); }); await p.waitForTimeout(300); console.log('lock on', await st());
  for (let i = 0; i < 6; i++) { await p.evaluate(() => { const g = __pe.game; g.cool = 0; g.press('mouse1'); g.release('mouse1'); }); await sim(0.35); }
  console.log('after shots', await st()); await p.screenshot({ path: out + '_g2.png' });
  await p.evaluate(() => { const g = __pe.game; g.release('mouse2'); g.yaw = -Math.PI / 2; g.cool = 0; g.press('mouse1'); g.release('mouse1'); }); await sim(0.9); console.log('beam at missile door', await st());
  await p.evaluate(() => { const g = __pe.game; g.cool = 0; g.press('f'); }); await sim(0.9); console.log('missile at door', await st());
  await p.screenshot({ path: out + '_door.png' });
  // visors
  await p.evaluate(() => { const g = __pe.game; g.yaw = Math.PI; g.pitch = 0.1; g.press('x'); }); await p.waitForTimeout(300); await p.screenshot({ path: out + '_scan.png' });
  await p.evaluate(() => { const g = __pe.game; g.press('mouse1'); }); await sim(1.6); await p.waitForTimeout(300); console.log('scanning', await st(), 'log open', await p.evaluate(() => __pe.game.logOpen));
  await p.screenshot({ path: out + '_log.png' });
  await p.evaluate(() => { const g = __pe.game; g.release('mouse1'); g.closeLog(); g.yaw = Math.PI / 2; g.pitch = 0; g.press('c'); }); await p.waitForTimeout(400); await p.screenshot({ path: out + '_thermal.png' });
  console.log('visor now', await p.evaluate(() => __pe.game.visor + ' hud ' + __pe.game.hud.screenId));
};
