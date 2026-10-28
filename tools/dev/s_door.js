const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  p.on('console', m => { if (m.type() === 'error') console.log('CONSOLE', m.text()); });
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles(SP + '/cube_bin.fbx'); await p.waitForTimeout(1500);
  await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(500);
  await p.click('button:has-text("New object")'); await p.click('.pitem:has-text("cube_bin")'); await p.waitForTimeout(1200);
  console.log('props', await p.evaluate(() => JSON.stringify(__studio.S.project.props)));
  await p.evaluate(() => { const T = __studio.S.tags, id = __studio.S.project.props[0].id; T.set(id + '.type', 'door'); T.set(id + '.openMode', 'proximity');  });
  await p.waitForTimeout(500); await p.screenshot({ path: out + '_obj.png' });
  // a wall across the starter room (row 24) with a gap at cell 23, and a door in that gap
  await p.evaluate(() => { const S = __studio.S, m = S.project.maps[0], G = PRIM.Grid, id = S.project.props[0].id; for (let x = 20; x < 28; x++) if (x !== 23) G.edit(m, 0, x, 24).w = '';
    m.things = [{ id: 't1', prop: id, x: 23 * 128 + 64, y: 24 * 128 + 64, ang: 0, layer: 0 }]; m.n.t = 1; S.tags.emit([]); });
  await p.click('.tab:has-text("Map")').catch(() => {}); await p.waitForTimeout(400); await p.screenshot({ path: out + '_map.png' });
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(400); await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.hudHost, null, { timeout: 8000 }); await p.waitForTimeout(1200);
  const X = 23 * 128 + 64, Y0 = 24 * 128 + 64;
  const put = (dy) => p.evaluate(([X, Y]) => { const q = __studio.S.play; q.msg.style.display = 'none'; q.look = () => {}; q.body = null; q.x = X; q.y = Y; q.z = 0; q.yaw = -Math.PI / 2; q.pitch = 0; }, [X, Y0 + dy]);
  const st = () => p.evaluate(() => { const q = __studio.S.play; return JSON.stringify((q.things || []).map(o => [o.type, o.st, +o.p.toFixed(2)])) + ' pass=' + JSON.stringify(q.doorOpen) + ' doorTris=' + (q.dtris || []).length; });
  const walk = secs => p.evaluate(secs => { const q = __studio.S.play; q.keys.w = true; for (let i = 0; i < secs * 60; i++) { q.step(1 / 60); q.thingStep(1 / 60); } q.keys.w = false; return Math.round(q.y); }, secs);
  await put(260); await p.evaluate(() => { const q = __studio.S.play, o = q.things[0]; o.st = 'closed'; o.p = 0; __studio.S.tags.set(o.pre + 'openMode', 'interact'); q.doorOpen = {}; }); await p.waitForTimeout(800); await p.screenshot({ path: out + '_closed.png' });
  await put(260); await p.evaluate(() => { const q = __studio.S.play, o = q.things[0]; o.st = 'closed'; o.p = 0; __studio.S.tags.set(o.pre + 'openMode', 'interact'); q.doorOpen = {}; }); await p.waitForTimeout(800); await p.screenshot({ path: out + '_closed.png' });
  console.log('fit 1x1', await p.evaluate(() => { const q = __studio.S.play, o = q.things[0], M = q.doorFit(o, o.fr), P = PRIM.M4.point, b = o.model.bounds; return JSON.stringify([P(M, b.lo[0], b.lo[1], b.lo[2]), P(M, b.hi[0], b.hi[1], b.hi[2])].map(v => v.map(x => Math.round(x)))); }));
  await put(400); console.log('far', await st());
  console.log('walk into the closed door (proximity off) ->', await p.evaluate(() => { __studio.S.tags.set(__studio.S.project.props[0].id + '.openMode', 'interact'); return 1; }), await walk(1.5), '(door face at', Y0 + 8, ')');
  await p.evaluate(() => __studio.S.tags.set(__studio.S.project.props[0].id + '.openMode', 'proximity')); await p.waitForTimeout(200);
  await put(120); await p.evaluate(() => { const q = __studio.S.play; for (let i = 0; i < 90; i++) q.thingStep(1 / 60); }); console.log('near (proximity)', await st());
  await p.evaluate(() => { const q = __studio.S.play; for (let i = 0; i < 120; i++) q.thingStep(1 / 60); }); console.log('later', await st()); await p.screenshot({ path: out + '_open.png' });
  console.log('walk through the open door ->', await walk(1.5));
  // interact + shot modes
  const setMode = m => p.evaluate(([m, X, Y]) => { const q = __studio.S.play, o = q.things[0]; __studio.S.tags.set(o.pre + 'openMode', m); __studio.S.tags.set(o.pre + 'autoClose', false); o.st = 'closed'; o.p = 0; o.t = 0; q.doorOpen = {}; q.body = null; q.x = X; q.y = Y; q.z = 0; q.yaw = -Math.PI / 2; q.pitch = 0; }, [m, X, Y0 + 70]);
  await setMode('interact'); console.log('closed door blocks: walked to y', await walk(1), '(should stop near', Y0 + 8 + 16, ')'); await setMode('interact'); await p.waitForTimeout(300); console.log('interact: idle', await st()); await p.keyboard.press('e'); await p.waitForTimeout(1500); console.log('interact: after E', await st());
  await setMode('shot'); await p.waitForTimeout(300); await p.keyboard.press('e'); await p.waitForTimeout(300); console.log('shot: E does nothing', await st());
  await p.evaluate(() => { const q = __studio.S.play, c = q.view.cam, B = PRIM.R3D.camBasis(c.yaw, c.pitch), O = [c.x, c.y, c.z], h = q.hitDoor(O, B.f, 500); console.log('hit', !!h); if (h) q.shootDoor(h.th); });
  await p.waitForTimeout(1500); console.log('shot: after bullet', await st());
  // a 2 x 2 door: frame size, and how it looks in the map editor
  await p.evaluate(() => { const S = __studio.S, id = S.project.props[0].id; S.tags.setMany({ [id + '.doorCellsW']: 2, [id + '.doorCellsH']: 2 }); const f = PRIM.Grid.thingFrame(S.project.maps[0], S.tags, S.project.maps[0].things[0]); console.log('frame 2x2', JSON.stringify([f.cx, f.cy, f.w, f.h, f.cells])); });
  await p.keyboard.press('Escape'); await p.waitForTimeout(400); await p.click('.tab:has-text("Map")').catch(() => {}); await p.waitForTimeout(500); await p.screenshot({ path: out + '_map2x2.png' });
};
