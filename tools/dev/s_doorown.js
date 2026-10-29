// Per placed door requirements (Map editor "What this door needs"): one door object, two placed doors, the second gets its own lock (a certain weapon) through the inspector; Play3D reads it via ov(), the first door keeps the object's settings.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/Arm_canon.fbx']); await p.waitForTimeout(2000);
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, T = S.tags, m = pr.maps[0], arm = pr.assets.find(a => /arm/i.test(a.id + a.name)).id, C = c => (c + 0.5) * 128;
    const att = { id: 'att9' }; pr.player.attachments.push(att); Sc.defineAttach(pr, att, { type: 'weapon', name: 'Missile', asset: arm }); att.script = { nodes: [], links: [] }; Sc.ensurePlayer(pr); T.setMany({ 'player.att9.projType': 'particle', 'player.att9.doorColor': '#20ff60' });
    const d = Sc.addProp(pr, { name: 'Hatch', type: 'door', asset: arm, openMode: 'interact', autoClose: false });
    m.things = [{ id: 'd1', prop: d.id, x: C(24), y: C(21), ang: 0, layer: 0 }, { id: 'd2', prop: d.id, x: C(26), y: C(21), ang: 0, layer: 0 }, { id: 'gone', prop: d.id, x: C(28), y: C(21), ang: 0, layer: 0 }]; m.n.t = 9;
    Sc.doorOwnOn(pr, m.id, m.things[2]); m.things.pop(); T.emit([]); });
  // select d2 in the map editor and pick "A certain weapon"
  await p.click('.tab:has-text("Map")').catch(() => {}); await p.waitForTimeout(300);
  await p.evaluate(() => { const S = __studio.S; S.med.select({ kind: 'thing', id: 'd2' }); });
  await p.waitForTimeout(300);
  const r1 = await p.evaluate(() => { const S = __studio.S; return { fold: !!document.querySelector('select[aria-label="Opens with"]'), msel: S.msel && S.msel.id }; }); console.log('inspector', JSON.stringify(r1));
  await p.selectOption('select[aria-label="Opens with"]', 'weapon'); await p.waitForTimeout(300);
  const r2 = await p.evaluate(() => { const T = __studio.S.tags, m = __studio.S.project.maps[0]; return { open: T.get(m.id + '.th.d2.openMode'), first: T.get(m.id + '.th.d2.shotFirst'), wpn: T.get(m.id + '.th.d2.shotWeapon'), d1: T.has(m.id + '.th.d1.openMode'), cur: document.querySelector('select[aria-label="Opens with"]').value, fields: document.querySelectorAll('.insp .tn').length }; }); console.log('after pick', JSON.stringify(r2));
  await p.screenshot({ path: out + '_insp.png' });
  await p.selectOption('select[aria-label="Opens with"]', 'object'); await p.waitForTimeout(300);
  console.log('back to object, tags left', await p.evaluate(() => { const pr = __studio.S.project, m = pr.maps[0]; return Object.keys(pr.tags).filter(k => k.startsWith(m.id + '.th.d2.')).length; }));
  await p.selectOption('select[aria-label="Opens with"]', 'weapon'); await p.waitForTimeout(300);
  // reload check: orphan tags of a deleted thing are cleaned
  const r3 = await p.evaluate(() => { const pr = __studio.S.project, m = pr.maps[0]; PRIM.Schema.ensureThingTags(pr); return { orphan: Object.keys(pr.tags).filter(k => k.startsWith(m.id + '.th.gone.')).length, own: Object.keys(pr.tags).filter(k => k.startsWith(m.id + '.th.d2.')).length }; }); console.log('cleanup', JSON.stringify(r3));
  await p.evaluate(() => { __studio.S.objView = false; }); await p.click('.tab:has-text("Timeline")'); await p.waitForTimeout(200); await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(400);
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.things && __studio.S.play.things.length >= 2, null, { timeout: 15000 }); await p.waitForTimeout(500);
  const r4 = await p.evaluate(() => { const q = __studio.S.play, D = id => { const o = q.things.find(x => x.th.id === id), s = q.shieldState(o); return { tp: o.tp, mode: q.ov(o, 'openMode'), sh: s && s.col.map(v => +v.toFixed(2)) }; }; return { d1: D('d1'), d2: D('d2') }; }); console.log('play', JSON.stringify(r4));
};
