const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  p.on('console', m => { if (m.type() === 'error') console.log('CONSOLE', m.text()); });
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.click('.mcard:has-text("DOOM")'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles(SP + '/cube_bin.fbx'); await p.waitForTimeout(1500);
  await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(500);
  await p.click('button:has-text("New object")'); await p.click('.pitem:has-text("cube_bin")'); await p.waitForTimeout(1200);
  console.log('props', await p.evaluate(() => JSON.stringify(__studio.S.project.props)));
  await p.evaluate(() => { const T = __studio.S.tags, id = __studio.S.project.props[0].id; T.set(id + '.type', 'door'); T.set(id + '.openMode', 'proximity');  });
  await p.waitForTimeout(500); await p.screenshot({ path: out + '_obj.png' });
  // place a door in the map editor with the Place Object tool
  await p.click('.tab:has-text("Map Editor")').catch(() => {}); await p.waitForTimeout(500);
  const tabs = await p.evaluate(() => [...document.querySelectorAll('.tab')].map(t => t.textContent)); console.log('tabs', tabs.join('|'));
  await p.keyboard.press('Escape');
  await p.evaluate(() => { const S = __studio.S, m = S.project.maps[0], id = S.project.props[0].id; m.things = [{ id: 't1', prop: id, sec: m.sectors[0].id, a: m.sectors[0].v[0], t: 0.5 }]; m.n.t = 1; S.tags.emit([]); });
  await p.screenshot({ path: out + '_map.png' });
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(400); await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.hudHost, null, { timeout: 8000 }); await p.waitForTimeout(1200);
  await p.evaluate(() => { const q = __studio.S.play; q.msg.style.display = 'none'; q.look = () => {}; q.yaw = -Math.PI / 2; q.pitch = 0; q.x = 0; q.y = 40; });
  const st = () => p.evaluate(() => { const q = __studio.S.play; return JSON.stringify((q.things || []).map(o => [o.type, o.st, +o.p.toFixed(2)])) + ' pass=' + JSON.stringify(q.doorOpen) + ' walls=' + q.walls.filter(w => w.door).length; });
  console.log('start', await st()); await p.screenshot({ path: out + '_closed.png' });
  await p.evaluate(() => { const q = __studio.S.play; q.x = 0; q.y = 100; q.yaw = -Math.PI / 2; });
  console.log('far', await st());
  await p.evaluate(() => { const q = __studio.S.play; q.x = 0; q.y = -60; }); await p.waitForTimeout(600); console.log('near (proximity)', await st()); await p.waitForTimeout(1500); console.log('later', await st()); await p.screenshot({ path: out + '_open.png' });
  await p.evaluate(() => { const q = __studio.S.play; q.x = 0; q.y = 20; });
  await p.waitForTimeout(200);
  // walk through the open doorway
  await p.evaluate(() => { const q = __studio.S.play; q.x = 0; q.y = -60; }); 
  await p.screenshot({ path: out + '_open2.png' }); await p.keyboard.down('w'); await p.waitForTimeout(2500); await p.keyboard.up('w'); console.log('after walking (y)', await p.evaluate(() => __studio.S.play.y));
  // interact + shot modes
  const setMode = m => p.evaluate(m => { const q = __studio.S.play, o = q.things[0]; __studio.S.tags.set(o.pre + 'openMode', m); __studio.S.tags.set(o.pre + 'autoClose', false); o.st = 'closed'; o.p = 0; o.t = 0; q.doorOpen = {}; q.x = 0; q.y = -60; q.yaw = -Math.PI / 2; q.pitch = 0; }, m);
  await setMode('interact'); await p.waitForTimeout(300); console.log('interact: idle', await st()); await p.keyboard.press('e'); await p.waitForTimeout(1500); console.log('interact: after E', await st());
  await setMode('shot'); await p.waitForTimeout(300); await p.keyboard.press('e'); await p.waitForTimeout(300); console.log('shot: E does nothing', await st());
  await p.evaluate(() => { const q = __studio.S.play, c = q.view.cam, B = PRIM.R3D.camBasis(c.yaw, c.pitch), O = [c.x, c.y, c.z], h = q.hitDoor(O, B.f, 500); console.log('hit', !!h); if (h) q.shootDoor(h.th); });
  await p.waitForTimeout(1500); console.log('shot: after bullet', await st());
};
