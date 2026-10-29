// Patrol paths: does a patrolling enemy really walk its points? (loop, back and forth, after seeing you, chase behaviour + path). Frames stepped by hand.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/Arm_canon.fbx']); await p.waitForTimeout(2500);
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, m = pr.maps[0], arm = pr.assets.find(a => /arm/i.test(a.id + a.name)).id, sc = 40 / ((PRIM.FBX.cache[arm] || { bounds: { size: 40 } }).bounds.size || 40);
    const E = (name, over) => Sc.addProp(pr, Object.assign({ name, type: 'enemy', asset: arm, scale: sc, height: 60, radius: 16, speed: 200, sight: 1, attack: 'melee', attackRange: 20, damage: 0 }, over)).id;
    const C = c => (c + 0.5) * 128; window.__C = C;
    m.paths = [{ id: 'path1', pts: [[C(21), C(21)], [C(26), C(21)], [C(26), C(26)], [C(21), C(26)]], loop: true, layer: 0 }, { id: 'path2', pts: [[C(22), C(23)], [C(25), C(23)]], loop: false, layer: 0 }];
    const ids = window.__ids = { pat: E('Patrol', { behavior: 'path' }), pp: E('PingPong', { behavior: 'path' }), ch: E('Chaser with path', {}) };
    m.things = [{ id: 'ta', prop: ids.pat, x: C(21), y: C(21), ang: 0, layer: 0, path: 'path1' }, { id: 'tb', prop: ids.pp, x: C(22), y: C(23), ang: 0, layer: 0, path: 'path2' }, { id: 'tc', prop: ids.ch, x: C(26), y: C(26), ang: 0, layer: 0, path: 'path1' }];
    m.n.t = 9; S.tags.emit([]); });
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(300);
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.things && __studio.S.play.things.length >= 3, null, { timeout: 12000 }); await p.waitForTimeout(400);
  const go = (n, fn, px, py) => p.evaluate(([n, fn, px, py]) => { const q = __studio.S.play; q.msg.style.display = 'none'; if (fn) (new Function('q', fn))(q); if (px !== null) { q.x = px; q.y = py; q.body = null; }
    let t = q.last || performance.now(); const tr = { ta: [], tb: [], tc: [] }; for (let i = 0; i < n; i++) { if (px !== null && !q.body) { q.x = px; q.y = py; } t += 16; q.frame(t); cancelAnimationFrame(q.raf); if (q.pdead && !tr.died) tr.died = [i, q.T.get('player.health'), Math.round(q.z)]; if (i % 25 === 0) (tr.me = tr.me || []).push([+(q.x / 128 - 0.5).toFixed(1), +(q.y / 128 - 0.5).toFixed(1)]); if (i % 25 === 0) for (const o of q.things) tr[o.th.id].push([+(o.x / 128 - 0.5).toFixed(1), +(o.y / 128 - 0.5).toFixed(1), o.st[0] + (o.awake ? '!' : '')]); }
    q.raf = requestAnimationFrame(z => q.frame(z)); tr.hp = q.T.get('player.health'); tr.dead = !!q.pdead; tr.z = Math.round(q.z); return JSON.stringify(tr); }, [n, fn || '', px === undefined ? null : px, py === undefined ? null : py]);
  // the player hides in a far corner behind nothing: everyone can see you in an open room, so first make them blind to you
  console.log('player out of sight (6 s), cells:', await go(375, '', 24 * 128, 24 * 128));
  console.log('they can see you (4 s):', await go(250, 'for (const o of q.things) o.awake = true;', 23.5 * 128, 23.5 * 128));
  console.log('you vanish (8 s):', await go(500, '', 23.5 * 128, 23.5 * 128));
  // seen from behind vs in front (only "ta" can see: sight 3000)
  const rel = (back) => p.evaluate(back => { const q = __studio.S.play, o = q.things.find(x => x.th.id === 'ta'); q.T.set(o.pre + 'sight', 3000); return 0; }, back);
  await rel();
  console.log('behind the patroller:', await p.evaluate(() => { const q = __studio.S.play, o = q.things.find(x => x.th.id === 'ta'); let t = q.last; const r = []; for (let i = 0; i < 60; i++) { const a = o.yaw * Math.PI / 180; q.x = o.x - Math.sin(a) * 260; q.y = o.y - Math.cos(a) * 260; q.body = null; t += 16; q.frame(t); cancelAnimationFrame(q.raf); } q.raf = requestAnimationFrame(z => q.frame(z)); return JSON.stringify({ st: o.st, awake: !!o.awake }); }));
  console.log('in front of it:', await p.evaluate(() => { const q = __studio.S.play, o = q.things.find(x => x.th.id === 'ta'); let t = q.last; for (let i = 0; i < 30; i++) { const a = o.yaw * Math.PI / 180; q.x = o.x + Math.sin(a) * 260; q.y = o.y + Math.cos(a) * 260; q.body = null; t += 16; q.frame(t); cancelAnimationFrame(q.raf); } q.raf = requestAnimationFrame(z => q.frame(z)); return JSON.stringify({ st: o.st, awake: !!o.awake }); }));
  // map editor: a path started on an enemy is linked to it
  await p.evaluate(() => { const q = __studio.S.play; if (q) q.leave(); }); await p.waitForTimeout(300);
  await p.click('.tab:has-text("Map")'); await p.waitForTimeout(400);
  await p.evaluate(() => { const m = __studio.S.project.maps[0]; delete m.things.find(t => t.id === 'tb').path; m.paths = m.paths.filter(q => q.id !== 'path2'); __studio.S.med.redraw(); });
  await p.click('.mptab:has-text("Objects")'); await p.click('.mpiece:has-text("Path")'); await p.waitForTimeout(200);
  const cellPx = (cx, cy) => p.evaluate(([cx, cy]) => { const m = __studio.S.med, r = m.cv.getBoundingClientRect(); return [r.left + m.sx((cx + 0.5) * 128), r.top + m.sy((cy + 0.5) * 128)]; }, [cx, cy]);
  for (const [x, y] of [[22, 23], [22, 25]]) { const c = await cellPx(x, y); await p.mouse.click(c[0], c[1]); await p.waitForTimeout(120); }
  await p.keyboard.press('Enter'); await p.waitForTimeout(200);
  console.log('auto link:', await p.evaluate(() => JSON.stringify(__studio.S.project.maps[0].things.map(t => [t.id, t.path || '-']))));
  await p.screenshot({ path: out + '_map.png' });
};
