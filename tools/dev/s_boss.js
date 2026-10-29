// Boss with phases + boss script, keys and locked doors, a shot door that first needs a certain weapon, HUD minimap / radar / boss bar / keys. Frames stepped by hand.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/Arm_canon.fbx']); await p.waitForTimeout(2500);
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(500);
  await p.click('button:has-text("New weapon")'); await p.click('.pitem:has-text("Arm_canon")'); await p.waitForTimeout(1500);
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, T = S.tags, m = pr.maps[0], arm = pr.assets.find(a => /arm/i.test(a.id + a.name)).id, sc = 60 / ((PRIM.FBX.cache[arm] || { bounds: { size: 60 } }).bounds.size || 60), C = c => (c + 0.5) * 128;
    // two weapons: a blaster (att1) and a "missile" launcher (att9)
    const a1 = pr.player.attachments[0]; T.setMany({ ['player.' + a1.id + '.projType']: 'hitscan', ['player.' + a1.id + '.name']: 'Blaster' });
    const att = { id: 'att9' }; pr.player.attachments.push(att); Sc.defineAttach(pr, att, { type: 'weapon', name: 'Missile', asset: arm }); att.script = { nodes: [], links: [] }; Sc.ensurePlayer(pr); T.set('player.att9.projType', 'hitscan');
    pr.tags.bossDead = { type: 'bool', value: false, label: 'Boss beaten', group: 'custom', custom: true };
    // boss: 300 health, phase 1 at 100 %, "Rage" at 50 %
    const b = Sc.addProp(pr, { name: 'Warden', type: 'boss', asset: arm, scale: sc * 1.5, health: 300, radius: 30, height: 90, speed: 120, sight: 4000, attack: 'projectile', attackRange: 3000, damage: 1, attackRate: 1, attackDelay: 0, bossName: 'THE WARDEN', bossIntroTime: 0.3, bossIntroMsg: 'The Warden awakens', bossWinTag: 'bossDead', bossWinVal: 'yes', bossWinMsg: 'Warden defeated' });
    const ph1 = Sc.addPhase(pr, b, { name: 'Calm', startAt: 100, enterTime: 0 }), ph2 = Sc.addPhase(pr, b, { name: 'Rage', startAt: 50, enterTime: 0.2, enterMsg: 'It is angry!', bossMove: 'circle', keepDist: 300, speed: 260, pattern: 'spread', patCount: 5, patSpread: 50, armor: 0.5 });
    b.script = { nodes: [], links: [] }; const n1 = Sc.addSNode(pr, b.id, b.script, 'onPhase', 20, 20, { phase: 'Rage' }), n2 = Sc.addSNode(pr, b.id, b.script, 'bossFire', 260, 20, { pattern: 'ring', count: 12, wait: false }), n3 = Sc.addSNode(pr, b.id, b.script, 'bossShield', 500, 20, { seconds: 0.5 });
    b.script.links.push({ a: n1.id, o: 'next', b: n2.id }, { a: n2.id, o: 'next', b: n3.id });
    // key + a door locked with it; a shot door that first needs the missile
    const key = Sc.addProp(pr, { name: 'Red key', type: 'key', asset: arm, scale: sc * 0.4, keyName: 'Red key', keyColor: '#ff3030' }); Sc.ensureKeys(pr);
    const d1 = Sc.addProp(pr, { name: 'Red door', type: 'door', asset: arm, lockKey: key.id, openMode: 'interact', autoClose: false });
    const d2 = Sc.addProp(pr, { name: 'Missile door', type: 'door', asset: arm, openMode: 'shot', shotFirst: 'weapon', shotWeapon: 'att9', shotMsg: 'Too strong for that weapon', autoClose: true, closeAfter: 0.5 });
    window.__ids = { boss: b.id, key: key.id, d1: d1.id, d2: d2.id, ph2: ph2.id };
    m.things = [{ id: 'tb', prop: b.id, x: C(26), y: C(26), ang: 0, layer: 0 }, { id: 'tk', prop: key.id, x: C(21), y: C(26), ang: 0, layer: 0 }, { id: 'td1', prop: d1.id, x: C(22), y: C(21), ang: 0, layer: 0 }, { id: 'td2', prop: d2.id, x: C(25), y: C(21), ang: 0, layer: 0 }]; m.n.t = 9;
    // HUD: minimap, boss bar, keys
    const hud = pr.screens.find(s => s.kind === 'hud'); for (const [id, type] of [['mini', 'minimap'], ['bbar', 'bossbar'], ['keyz', 'keys']]) { const n = { id, type }; hud.nodes.push(n); Sc.defineNode(pr, hud.id, n, type === 'keys' ? { missing: true } : {}); }
    T.emit([]); });
  // Object Editor: the boss inspector
  await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(300); await p.evaluate(() => { __studio.S.propSel = window.__ids.boss; __studio.S.phSel = window.__ids.ph2; }); await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(500);
  console.log('boss inspector:', await p.evaluate(() => [...document.querySelectorAll('.insp .fold')].map(f => f.innerText.replace(/\s+/g, ' ').slice(0, 26)).join(' | ')));
  await p.screenshot({ path: out + '_boss_insp.png' });
  await p.evaluate(() => { __studio.S.propSel = window.__ids.d2; }); await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(400);
  console.log('door fields:', await p.evaluate(() => [...document.querySelectorAll('.insp .fold')].map(f => f.innerText.replace(/\s+/g, ' ')).filter(t => /First shot/.test(t)).map(t => t.match(/Opens.{0,400}/)[0]).join('')));
  // boss script in the Node Coder
  await p.evaluate(() => { __studio.S.coderT = { kind: 'boss', id: window.__ids.boss }; }); await p.click('.tab:has-text("Node Coder")'); await p.waitForTimeout(500); await p.screenshot({ path: out + '_coder.png' });
  console.log('coder nodes:', await p.evaluate(() => [...document.querySelectorAll('.ncn .nch')].map(e => e.innerText).join(' / ')));
  await p.click('.tab:has-text("Map")'); await p.waitForTimeout(400); await p.screenshot({ path: out + '_map.png' });
  // play
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(300); await p.evaluate(() => { __studio.S.objView = false; }); await p.click('.tab:has-text("Map")'); await p.waitForTimeout(200); await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(400);
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.things && __studio.S.play.things.length >= 4, null, { timeout: 15000 }); await p.waitForTimeout(400);
  const go = (n, fn, px, py, yaw) => p.evaluate(([n, fn, px, py, yaw]) => { const q = __studio.S.play; q.msg.style.display = 'none'; if (px !== null) { q.x = px; q.y = py; q.body = null; q.z = 0; } if (yaw !== null) { q.yaw = yaw; q.pitch = 0; } q.T.set('player.health', 100);
    let t = q.last || performance.now(); for (let i = 0; i < n; i++) { t += 16; q.frame(t); cancelAnimationFrame(q.raf); } q.raf = requestAnimationFrame(z => q.frame(z)); const r = fn ? (new Function('q', fn))(q) : null; const T = q.T, B = q.things.find(o => o.boss);
    return JSON.stringify({ r, toast: q.toastEl && q.toastEl.style.opacity !== '0' ? q.toastEl.textContent : '', boss: B && { st: B.st, hp: Math.round(B.hp), fight: !!B.fight, phase: B.phName }, tags: { active: T.get('player.bossActive'), hp: T.get('player.bossHealth'), name: T.get('player.bossName'), phase: T.get('player.bossPhase'), dead: T.get('bossDead') }, hostile: (q.fx || []).filter(f => f.hostile).length }); }, [n, fn || '', px === undefined ? null : px, py === undefined ? null : py, yaw === undefined ? null : yaw]);
  const C = c => (c + 0.5) * 128;
  // keys + locked door: E in front of it without the key, then with it
  const door = (id) => `const o = q.things.find(x => x.th.id === '${id}'); return { st: o.st, unlocked: !!o.unlocked, shotOk: !!o.shotOk };`;
  console.log('locked door, no key:', await go(5, "q.interact(); " + door('td1'), C(22), C(21) + 70, -Math.PI / 2));
  console.log('walk onto the key:', await go(10, `return q.T.get('${'X'}'.replace('X', window.__ids.key) + '.have');`, C(21), C(26)));
  console.log('locked door, with key:', await go(30, "q.interact(); " + door('td1'), C(22), C(21) + 70, -Math.PI / 2));
  // shot door: blaster does nothing, missile opens it, afterwards the blaster works
  const shoot = (w) => `const a = q.curWeapon(); q.fireProj(a); const dd = (() => { ` + door('td2') + ` })(); dd.weapon = a && a.id; dd.wids = q.wids(); dd.atts = q.atts.map(x => x.id); return dd;`;
  await go(1, 'q.setWeapon(0)'); console.log('shot door, blaster:', await go(3, shoot(0), C(25), C(21) + 200, -Math.PI / 2));
  await go(1, 'q.setWeapon(1)'); console.log('shot door, missile:', await go(3, shoot(1), C(25), C(21) + 200, -Math.PI / 2));
  console.log('closes again:', await go(160, door('td2')));
  await go(1, 'q.setWeapon(0)'); console.log('shot door, blaster after:', await go(3, shoot(0)));
  if (process.env.ONLY_DOOR) return;
  // the boss: it sees you, the fight starts
  console.log('boss sees you:', await go(40, null, C(23), C(24), 0.8));
  console.log('fight on:', await go(60));
  console.log('hit to 49 %:', await go(5, 'const B = q.things.find(o => o.boss); q.damageEnemy(B, 154); return { ringShots: (q.fx || []).filter(f => f.hostile).length };'));
  console.log('rage phase:', await go(40, 'const B = q.things.find(o => o.boss); return { move: q.ov(B, "bossMove"), armor: q.ov(B, "armor"), marks: q.bossMarks };'));
  { const url = await p.evaluate(() => { const q = __studio.S.play; cancelAnimationFrame(q.raf); q.frame(q.last + 16); cancelAnimationFrame(q.raf); const u = q.view.gl.canvas.toDataURL('image/png'); q.raf = requestAnimationFrame(z => q.frame(z)); return u; }); require('fs').writeFileSync(out + '_game.png', Buffer.from(url.split(',')[1], 'base64')); }
  await p.screenshot({ path: out + '_hud.png', timeout: 90000 }).catch(() => console.log('(skip hud shot)'));
  console.log('minimap drawn:', await p.evaluate(() => { const c = document.querySelector('.hud-root canvas'); if (!c) return 'no canvas'; const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; let n = 0; for (let i = 3; i < d.length; i += 4) if (d[i] > 0) n++; return c.width + 'x' + c.height + ' painted ' + Math.round(n / (d.length / 4) * 100) + '%'; }));
  console.log('keys HUD:', await p.evaluate(() => [...document.querySelectorAll('.hud-root .hud-el')].filter(e => e.querySelector('svg')).map(e => e.children.length + ' icons, opacities ' + [...e.children].map(c => c.style.opacity).join(',')).join(' ')));
  console.log('defeat:', await go(10, 'const B = q.things.find(o => o.boss); B.shield = 0; q.damageEnemy(B, 9999); return B.st;'));
};
