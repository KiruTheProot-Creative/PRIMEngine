const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles(SP + '/cube_bin.fbx'); await p.waitForTimeout(1200);
  await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(400);
  await p.click('button:has-text("New object")'); await p.click('.pitem:has-text("cube_bin")'); await p.waitForTimeout(800);
  await p.evaluate(() => { const T = __studio.S.tags, id = __studio.S.project.props[0].id; T.set(id + '.type', 'enemy'); T.set(id + '.speed', 600); T.set(id + '.damage', 15); T.set(id + '.attackRate', 0.5); T.set(id + '.attackDelay', 0.1); T.set(id + '.radius', 20); T.set(id + '.height', 100); });
  await p.waitForTimeout(400); await p.screenshot({ path: out + '_oe.png' });
  // a second map and an exit object + a weapon with ammo
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, m = pr.maps[0];
    // make map1 bigger: a second room east, opened into the first
    const G = PRIM.Grid; for (let y = 19; y <= 28; y++) for (let x = 28; x <= 36; x++) { const c = G.edit(m, 0, x, y); for (const k of Object.keys(c)) delete c[k]; if (y === 19 || y === 28 || x === 36) c.w = ''; else { c.f = ''; c.c = ''; } }
    Sc.newMap(pr, 'map2'); Sc.ensureWalls(pr);
    const ex = Sc.addProp(pr, { name: 'Exit', type: 'exit', asset: pr.props[0] && S.tags.get(pr.props[0].id + '.asset') });
    m.things = [{ id: 't1', prop: pr.props[0].id, x: 3900, y: 3072, ang: 0 }, { id: 't2', prop: ex.id, x: 2700, y: 3400, ang: 0 }]; m.n.t = 2; S.tags.emit([]); });
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(400); await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.hud, null, { timeout: 8000 }); await p.waitForTimeout(600);
  await p.evaluate(() => { const q = __studio.S.play; q.msg.style.display = 'none'; q.look = () => {}; q.body = null; q.x = 3072; q.y = 3072; q.yaw = 0; q.pitch = 0; });
  const st = () => p.evaluate(() => { const q = __studio.S.play, e = q.things.find(o => o.type === 'enemy'); return JSON.stringify({ hp: q.T.get('player.health'), dead: !!q.pdead, map: q.map.id, enemy: e && { st: e.st, x: Math.round(e.x), hp: e.hp, awake: !!e.awake } }); });
  console.log('start', await st()); await p.waitForFunction(() => +__studio.S.play.T.get('player.health') < 100, null, { timeout: 15000 }).catch(() => console.log('NO DAMAGE')); console.log('after it reached me', await st()); await p.screenshot({ path: out + '_chase.png' });
  // shoot it with a hitscan ray from the eye
  await p.evaluate(() => { const q = __studio.S.play, e = q.things.find(o => o.type === 'enemy'), O = q.pEye(), D = PRIM.M4.norm(PRIM.M4.sub([e.x, e.floor + 50, e.y], O)), h = q.hitEnemy(O, D, 5000); window.__hit = !!h; for (let i = 0; i < 4; i++) if (h) q.damageEnemy(h.o, 30); });
  console.log('ray hit enemy', await p.evaluate(() => window.__hit), await st()); await p.waitForFunction(() => __studio.S.play.things.find(o => o.type === 'enemy').st === 'dead', null, { timeout: 8000 }).catch(() => {}); console.log('after kill', await st()); await p.screenshot({ path: out + '_dead.png' });
  // death and restart
  await p.evaluate(() => { const q = __studio.S.play; q.T.set('player.deathDelay', 0.5); q.T.set('player.health', 0); }); await p.waitForFunction(() => __studio.S.play.pdead, null, { timeout: 5000 }); console.log('dying', await st()); await p.waitForFunction(() => !__studio.S.play.pdead, null, { timeout: 8000 }); console.log('restarted', await st());
  // level exit -> map2
  await p.evaluate(() => { const q = __studio.S.play; q.body = null; q.x = 2700; q.y = 3400; }); await p.waitForFunction(() => __studio.S.play.map.id === 'map2', null, { timeout: 5000 }).catch(() => {}); console.log('after exit', await st());
};
