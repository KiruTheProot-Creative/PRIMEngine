// Ambient occlusion (baked into the level's vertex colours) and the spawner object. Frames stepped by hand.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/Arm_canon.fbx']); await p.waitForTimeout(2500);
  // AO: floor corners next to walls are darker than the middle of the room; a pillar darkens the floor around it
  console.log('ao', await p.evaluate(() => { const S = __studio.S, m = S.project.maps[0], G = PRIM.Grid; G.edit(m, 0, 24, 24).w = ''; S.tags.emit([]);
    const v = new PRIM.R3D.View(document.createElement('canvas')), r = G.build(m, { doors: [] }); v.tags = S.tags; const t0 = performance.now(); v.bakeAO(r, 0.6, 96); const ms = performance.now() - t0;
    const floorAt = (x, z) => { let best = null; for (const k in r.by) { const a = r.by[k]; for (let i = 0; i < a.length; i += 8) if (Math.abs(a[i + 1]) < 0.5 && Math.abs(a[i] - x) < 1 && Math.abs(a[i + 2] - z) < 1) best = Math.min(best === null ? 9 : best, a[i + 5]); } return best === null ? null : +best.toFixed(3); };
    let fx0 = 1e9, fz0 = 1e9; for (const k in m.layers[0].cells) { const c = m.layers[0].cells[k], [x, z] = k.split(',').map(Number); if (!G.isWall(c) && c.f !== undefined) { fx0 = Math.min(fx0, x); fz0 = Math.min(fz0, z); } }
    return JSON.stringify({ firstFloorCell: [fx0, fz0], realCorner: floorAt(fx0 * 128, fz0 * 128), edgeMid: floorAt(fx0 * 128, (fz0 + 3) * 128), ms: Math.round(ms), corners: v.aoCount, roomCorner: floorAt(21 * 128, 21 * 128), roomMiddle: floorAt(22 * 128, 22 * 128), besidePillar: floorAt(24 * 128, 25 * 128) }); }));
  // a bigger map: 40 x 40 cells with pillars every 4 cells, to see how long baking takes
  console.log('ao big map', await p.evaluate(() => { const G = PRIM.Grid, m = { id: 'big', style: 'grid', layers: [{ cells: {} }], things: [] }; for (let y = 0; y < 40; y++) for (let x = 0; x < 40; x++) { const c = G.edit(m, 0, x, y); if (x === 0 || y === 0 || x === 39 || y === 39 || (x % 4 === 0 && y % 4 === 0)) c.w = ''; else { c.f = ''; c.c = ''; } }
    const v = new PRIM.R3D.View(document.createElement('canvas')), r = G.build(m, { doors: [] }); let n = 0; for (const k in r.by) n += r.by[k].length / 8; const t0 = performance.now(); v.bakeAO(r, 0.6, 96); return JSON.stringify({ vertices: n, corners: v.aoCount, ms: Math.round(performance.now() - t0) }); }));
  // spawner: an enemy every 0.5 s, at most 3 alive
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, T = S.tags, arm = pr.assets.find(a => /arm/i.test(a.id + a.name)).id, sc = 40 / ((PRIM.FBX.cache[arm] || { bounds: { size: 40 } }).bounds.size || 40);
    const en = Sc.addProp(pr, { name: 'Bug', type: 'enemy', asset: arm, scale: sc, height: 40, radius: 14, speed: 0, sight: 10, attack: 'melee', attackRange: 10, damage: 0 }).id;
    const sp = Sc.addProp(pr, { name: 'Nest', type: 'spawner', spawnWhat: en, spawnEvery: 0.5, spawnFirst: 0.2, spawnMax: 3, spawnRadius: 100 }).id; window.__ids = { en, sp };
    const m = pr.maps[0]; m.things.push({ id: 'nest', prop: sp, x: 22.5 * 128, y: 26.5 * 128, ang: 0, layer: 0 }); m.n.t = 9; S.tags.emit([]); });
  await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(300); await p.evaluate(() => { __studio.S.propSel = window.__ids.sp; }); await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(400);
  console.log('spawner fields:', await p.evaluate(() => [...document.querySelectorAll('.insp .fold')].filter(f => /Spawner/.test(f.innerText)).map(f => f.innerText.replace(/\s+/g, ' ').slice(0, 200)).join('')));
  await p.click('.tab:has-text("Map")'); await p.waitForTimeout(400); await p.screenshot({ path: out + '_map.png' });
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(300);
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.things && __studio.S.play.things.length >= 1, null, { timeout: 12000 }); await p.waitForTimeout(400);
  const go = (n, fn) => p.evaluate(([n, fn]) => { const q = __studio.S.play; q.msg.style.display = 'none'; let t = q.last || performance.now(); for (let i = 0; i < n; i++) { t += 16; q.frame(t); cancelAnimationFrame(q.raf); } q.raf = requestAnimationFrame(z => q.frame(z)); const r = fn ? (new Function('q', fn))(q) : null;
    const kids = q.things.filter(o => o.th.spawned); return JSON.stringify({ r, spawned: kids.length, alive: kids.filter(o => q.eAlive(o)).length, awake: kids.filter(o => o.awake).length, ao: q.view.aoMs + ' ms' }); }, [n, fn || '']);
  console.log('after 0.5 s:', await go(30));
  console.log('after 3 s:', await go(160));
  console.log('kill one:', await go(1, 'const o = q.things.find(x => x.th.spawned && q.eAlive(x)); q.damageEnemy(o, 9999); return "killed";'));
  console.log('0.6 s later:', await go(40));
  { const url = await p.evaluate(() => { const q = __studio.S.play; cancelAnimationFrame(q.raf); q.x = 21.4 * 128; q.y = 26.6 * 128; q.yaw = 0.3; q.pitch = -0.25; q.body = null; q.frame(q.last + 16); cancelAnimationFrame(q.raf); const u = q.view.gl.canvas.toDataURL('image/png'); q.raf = requestAnimationFrame(z => q.frame(z)); return u; }); require('fs').writeFileSync(out + '_game.png', Buffer.from(url.split(',')[1], 'base64')); }
};
