// Enemy behaviours: patrol path (drawn with the Path tool), turret, ceiling enemies (shoot / drop and turn into another enemy / self-destruct),
// and the "sliced into pieces" death with exploding pieces. Frames are stepped by hand.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/cube_bin.fbx', SP + '/Arm_canon.fbx']); await p.waitForTimeout(2500);
  // draw a patrol path with the Path tool: 3 points, Enter
  await p.click('.mptab:has-text("Objects")'); await p.click('.mpiece:has-text("Path")'); await p.waitForTimeout(200);
  const cellPx = (cx, cy) => p.evaluate(([cx, cy]) => { const m = __studio.S.med, r = m.cv.getBoundingClientRect(); return [r.left + m.sx((cx + 0.5) * 128), r.top + m.sy((cy + 0.5) * 128)]; }, [cx, cy]);
  for (const [x, y] of [[21, 21], [26, 21], [26, 23]]) { const q = await cellPx(x, y); await p.mouse.click(q[0], q[1]); await p.waitForTimeout(120); }
  await p.keyboard.press('Enter'); await p.waitForTimeout(200);
  console.log('path:', await p.evaluate(() => JSON.stringify(__studio.S.project.maps[0].paths)));
  console.log('path inspector:', (await p.locator('.insp').innerText()).split('\n').slice(0, 4).join(' / '));
  // enemies (all cubes)
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, T = S.tags, cube = pr.assets.find(a => /cube/.test(a.id)).id, m = pr.maps[0];
    const E = (name, over) => Sc.addProp(pr, Object.assign({ name, type: 'enemy', asset: cube, scale: 0.3, height: 40, radius: 16, speed: 200, attack: 'projectile', attackRange: 2000, sight: 3000, attackRate: 0.5, attackDelay: 0, damage: 5 }, over)).id;
    const ids = window.__ids = { patrol: E('Patrol', { behavior: 'path', pathChase: false, attack: 'melee', attackRange: 20 }), turret: E('Turret', { behavior: 'stationary', sight: 300 }), shooter: E('Ceiling gun', { behavior: 'stationary', mount: 'ceiling', ceilAction: 'shoot', coneAngle: 30 }),
      chaser: E('Chaser', { attack: 'melee', attackRange: 30 }), dropper: null, bomber: null, slicer: E('Slicer', { asset: pr.assets.find(a => /arm/i.test(a.id + a.name)).id, scale: 60 / ((PRIM.FBX.cache[pr.assets.find(a => /arm/i.test(a.id + a.name)).id] || { bounds: { size: 60 } }).bounds.size || 60), height: 100, sight: 60, attack: 'melee', deathStyle: 'slice', slicePieces: 5, sliceTime: 0.5, blastDamage: 0, blastCount: 30 }) };
    ids.dropper = E('Dropper', { behavior: 'stationary', mount: 'ceiling', ceilAction: 'drop', dropInto: ids.chaser, coneAngle: 30 }); ids.bomber = E('Bomber', { behavior: 'stationary', mount: 'ceiling', ceilAction: 'selfdestruct', fuse: 0.3, blastRadius: 200, blastDamage: 40, coneAngle: 30 });
    const C = c => (c + 0.5) * 128; m.things = [{ id: 'tp', prop: ids.patrol, x: C(21), y: C(21), ang: 0, layer: 0, path: m.paths[0].id }, { id: 'tt', prop: ids.turret, x: C(20), y: C(27), ang: 0, layer: 0 }, { id: 'ts', prop: ids.shooter, x: C(23), y: C(26), ang: 0, layer: 0 },
      { id: 'td', prop: ids.dropper, x: C(25), y: C(26), ang: 0, layer: 0 }, { id: 'tb', prop: ids.bomber, x: C(27), y: C(27), ang: 0, layer: 0 }, { id: 'tx', prop: ids.slicer, x: C(22), y: C(24), ang: 0, layer: 0 }]; m.n.t = 9; S.tags.emit([]); });
  await p.click('.mptab:has-text("Objects")').catch(() => {}); await p.waitForTimeout(400); await p.screenshot({ path: out + '_map.png' });
  await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(400);
  await p.evaluate(() => { __studio.S.propSel = window.__ids.dropper; }); await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(400);
  console.log('behaviour fields (dropper):', await p.evaluate(() => [...document.querySelectorAll('.insp .fold')].filter(f => /Behaviour/.test(f.innerText)).map(f => f.innerText.replace(/\s+/g, ' ').slice(0, 400)).join('')));
  // play
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(300);
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.things && __studio.S.play.things.length >= 6, null, { timeout: 12000 }); await p.waitForTimeout(400);
  const go = (n, fn, px, py) => p.evaluate(([n, fn, px, py]) => { const q = __studio.S.play; q.msg.style.display = 'none'; if (px !== null) { q.x = px; q.y = py; q.body = null; q.z = 0; } let t = q.last || performance.now(); for (let i = 0; i < n; i++) { t += 16; q.frame(t); cancelAnimationFrame(q.raf); } q.raf = requestAnimationFrame(z => q.frame(z)); const r = fn ? (new Function('q', fn))(q) : null;
    const E = id => q.things.find(o => o.th.id === id), s = o => o ? { st: o.st, x: Math.round(o.x), y: Math.round(o.y), floor: Math.round(o.floor), mount: o.mount || '', prop: o.pre.replace('.', ''), taken: !!o.taken } : null;
    return JSON.stringify({ r, hp: Math.round(q.T.get('player.health')), patrol: s(E('tp')), shooter: s(E('ts')), dropper: s(E('td')), bomber: s(E('tb')), hostile: (q.fx || []).filter(f => f.hostile).length }); }, [n, fn || '', px === undefined ? null : px, py === undefined ? null : py]);
  const C = c => (c + 0.5) * 128;
  console.log('start (far away):', await go(3, null, C(27.4), C(24.2)));
  console.log('patrol after 2 s:', await go(120));
  console.log('patrol after 4 s:', await go(120));
  console.log('under the ceiling gun:', await go(40, null, C(23), C(26)));
  console.log('under the dropper:', await go(10, null, C(25), C(26)));
  console.log('dropper lands:', await go(60, null, C(25), C(25.4)));
  console.log('under the bomber:', await go(60, null, C(27), C(27)));
  await p.screenshot({ path: out + '_boom.png', timeout: 90000 }).catch(() => console.log('(skip)'));
  // slice death
  console.log('model', await p.evaluate(() => { const q = __studio.S.play, o = q.things.find(x => x.th.id === "tx"); return JSON.stringify({ b: o.model.bounds, nodes: o.model.nodes.map(n => n.mesh ? n.mesh.p.length / 3 : 0), M: o.lastM && o.lastM.map(x => +x.toFixed(2)), P: o.lastPose && o.lastPose.map(m => m && m.map(x => +x.toFixed(2))) }); }));
  console.log('slice:', await go(1, 'const o = q.things.find(x => x.th.id === "tx"); q.x = o.x + 230; q.y = o.y; q.body = null; q.yaw = Math.PI; q.pitch = -0.15; q.damageEnemy(o, 9999); return { pieces: q.fx.filter(f => f.piece).length, caps: q.fx.filter(f => f.piece && f.gm.meshes.length > 1).length, verts: q.fx.filter(f => f.piece).map(f => f.gm.meshes[0].n) };'));
  console.log('pieces fly:', await go(8, 'return q.fx.filter(f => f.piece).map(f => f.p.map(Math.round));'));
  console.log('debug', await p.evaluate(() => { const q = __studio.S.play, v = q.view, c = v.cam, B = PRIM.R3D.camBasis(c.yaw, c.pitch), P = q.fx.filter(f => f.piece); return JSON.stringify({ cam: [c.x, c.y, c.z].map(Math.round), f: B.f.map(x => +x.toFixed(2)), pieces: P.map(f => [f.p.map(Math.round), f.gm.model.bounds.lo.map(Math.round), f.gm.model.bounds.hi.map(Math.round)]), culled: v.culledFx }); }));
  { const url = await p.evaluate(() => { const q = __studio.S.play; cancelAnimationFrame(q.raf); q.frame(q.last + 16); cancelAnimationFrame(q.raf); const u = q.view.gl.canvas.toDataURL('image/png'); q.raf = requestAnimationFrame(z => q.frame(z)); return u; }); require('fs').writeFileSync(out + '_slice.png', Buffer.from(url.split(',')[1], 'base64')); }
  for (let i = 0; i < 6; i++) console.log('pieces explode:', await go(5, 'return { pieces: q.fx.filter(f => f.piece).length, bits: q.fx.filter(f => f.deco && f.kind === "sphere").length, shocks: q.fx.filter(f => f.kind === "shock").length, decoN: q.decoN };'));
};
