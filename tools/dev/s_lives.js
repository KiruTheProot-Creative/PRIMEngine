// Lives system (classic: lives + checkpoints; reserve: energy tanks), pick-up pop-ups that pause the game, Counter boxes HUD element. Frames stepped by hand.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/cube_bin.fbx', SP + '/boom.png']); await p.waitForTimeout(1200);
  // Counter boxes on the HUD
  await p.click('.hrow:has-text("hud") >> nth=0'); await p.waitForTimeout(300); await p.click('#hier button:has-text("Create")').catch(() => p.click('button:has-text("Create ▾") >> nth=0')); await p.click('.pop .pitem:has-text("Counter boxes")'); await p.waitForTimeout(400);
  console.log('hud elements:', await p.evaluate(() => JSON.stringify(__studio.S.project.screens.find(s => s.kind === 'hud').nodes.map(n => n.type))));
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv'); await p.waitForTimeout(300);
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, T = S.tags, cube = pr.assets.find(a => /cube/.test(a.id)).id, img = pr.assets.find(a => a.type === 'image').id;
    T.define('missiles', { type: 'number', value: 0, min: 0, max: 250, step: 1, label: 'Missiles', group: 'missiles', custom: true });
    const mt = Sc.addProp(pr, { name: 'Missile Tank', type: 'pickup', asset: cube, giveTag: 'missiles', giveAmount: 5, popupOn: true, popupText: 'Missile capacity increased by 5. Missiles: {missiles}', popupImage: img });
    const cp = Sc.addProp(pr, { name: 'Save point', type: 'checkpoint', cpMessage: 'Checkpoint reached' });
    pr.maps[0].things = [{ id: 't1', prop: mt.id, x: 3072 + 128, y: 3072 }, { id: 't2', prop: mt.id, x: 3072 + 256, y: 3072 }, { id: 't3', prop: cp.id, x: 3072 - 256, y: 3072 }]; pr.maps[0].n.t = 3;
    T.setMany({ 'player.livesMode': 'classic', 'player.livesStart': 2, 'player.deathDelay': 0.1 }); T.emit([]); });
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(400);
  console.log('lives settings:', await p.evaluate(() => ['Lives system', 'Lives at the start', 'Energy tanks at the start'].map(k => document.querySelector('.insp').innerText.includes(k)).join(',')));
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.hud, null, { timeout: 8000 }); await p.waitForTimeout(800);
  const run = (n, fn) => p.evaluate(([n, fn]) => { const q = __studio.S.play; q.msg.style.display = 'none'; if (fn) (new Function('q', fn))(q); let t = q.last || performance.now(); for (let i = 0; i < n; i++) { t += 1000 / 60; q.frame(t); cancelAnimationFrame(q.raf); } q.raf = requestAnimationFrame(x => q.frame(x));
    const T = q.T; return JSON.stringify({ x: Math.round(q.x), z: Math.round(q.z), hp: T.get('player.health'), lives: T.get('player.lives'), tanks: T.get('player.tanks') + '/' + T.get('player.tanksMax'), missiles: T.get('missiles'), popup: !!q.popup, dead: !!q.pdead, cp: q.cp && Math.round(q.cp.x), toast: q.toastEl && +q.toastEl.style.opacity ? q.toastEl.textContent : '' }); }, [n, fn || '']);
  console.log('start                ', await run(2, 'q.body = null; q.x = 3072; q.y = 3072;'));
  console.log('walk onto missile tank', await run(3, 'q.body = null; q.x = 3072 + 128;'));
  await p.screenshot({ path: out + '_popup.png', timeout: 90000 }).catch(() => console.log('(skip)'));
  console.log('W held, paused       ', await run(20, 'q.keys.w = true;'), '(x must not change)');
  await p.waitForTimeout(400); console.log('E closes             ', await run(1, 'q.keys.w = false; q.popupKey("e")'));
  console.log('2nd tank (no pop-up) ', await run(3, 'q.body = null; q.x = 3072 + 256;'));
  console.log('touch checkpoint     ', await run(3, 'q.body = null; q.x = 3072 - 256;'));
  console.log('walk away, die       ', await run(20, 'q.body = null; q.x = 3072 + 400; q.T.set("player.health", 0)'));
  console.log('die again            ', await run(20, 'q.body = null; q.x = 3072 + 400; q.T.set("player.health", 0)'));
  console.log('die with 0 lives     ', await run(20, 'q.body = null; q.x = 3072 + 400; q.T.set("player.health", 0)'), '(game over: map restarts, lives back to 2)');
  // energy reserve
  console.log('reserve, 2 tanks     ', await run(1, 'q.T.setMany({"player.livesMode":"reserve","player.tanksStart":2}); q.initLives(); q.T.set("player.health", 100)'));
  console.log('hit for 150          ', await run(1, 'q.hurtPlayer(150)'));
  console.log('hit for 30           ', await run(1, 'q.hurtPlayer(30)'));
  console.log('health +130          ', await run(1, 'q.giveHealth(130)'), '(health full, the extra fills a tank: Metroid energy)');
  console.log('health +60           ', await run(1, 'q.giveHealth(60)'), '(more health on top)');
  console.log('energy tank pick-up  ', await run(1, 'q.hurtPlayer(70); const T = q.T; T.setMany({ "player.tanksMax": Math.min(14, T.get("player.tanksMax") + 1) }); T.setMany({ "player.tanks": T.get("player.tanksMax"), "player.health": 100 })'));
  await p.evaluate(() => __studio.S.play.hud.update());
  console.log('counter boxes in HUD :', await p.evaluate(() => { const H = __studio.S.play.hud, it = Object.values(H.items).find(i => i.n.type === 'pips'), e = it && it.el; return e ? e.children.length + ' boxes, filled ' + [...e.children].filter(c => c.style.boxShadow).length : 'none'; }));
  const zz = () => p.evaluate(() => { const q = __studio.S.play; return 'z=' + Math.round(q.z) + ' ground=' + q.ground + ' dead=' + q.pdead + ' popup=' + !!q.popup + ' talk=' + !!q.talk + ' vz=' + Math.round(q.vz || 0) + ' keys=' + JSON.stringify(q.keys) + ' frames=' + q.view.frames; });
  console.log('before shot', await zz()); await p.waitForTimeout(3000); console.log('3 s of real frames', await zz());
  await p.screenshot({ path: out + '_hud.png', timeout: 90000 }).catch(() => console.log('(skip)')); console.log('after shot', await zz());
  await p.screenshot({ path: out + '_hud2.png', timeout: 90000 }).catch(() => console.log('(skip)'));
  console.log('after restart: view', await p.evaluate(() => { const q = __studio.S.play, v = q.view; let t = q.last; for (let i = 0; i < 3; i++) { t += 16; q.frame(t); cancelAnimationFrame(q.raf); } q.raf = requestAnimationFrame(x => q.frame(x)); return JSON.stringify({ cam: [v.cam.x, v.cam.y, v.cam.z].map(Math.round), pitch: +q.pitch.toFixed(2), tris: v.triCount, z: Math.round(q.z), map: q.map && q.map.id }); }));
  await p.screenshot({ path: out + '_hud2.png', timeout: 90000 }).catch(() => console.log('(skip)'));
};
