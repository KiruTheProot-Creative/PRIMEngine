module.exports = async (p, out) => {
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(400);
  await menu('Screen', 'Create Map'); await p.click('.mcard:has-text("DOOM")'); await p.waitForSelector('.medcv'); await p.waitForTimeout(300);
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(700);
  const t = await p.evaluate(() => document.body.innerText); console.log('inspector has', /Breathing/.test(t), /Walking bob/.test(t), /Hud Movement/.test(t));
  await p.evaluate(() => {
    const S = __studio.S, Sc = PRIM.Schema, pr = S.project; let a = pr.player.attachments[0];
    if (!a) { a = { id: 'att', script: { nodes: [], links: [] } }; pr.player.attachments.push(a); Sc.defineAttach(pr, a, { name: 'tester', type: 'animation' }); }
    const k = 'player.' + a.id, sc = a.script, add = (t, o) => Sc.addSNode(pr, k, sc, t, 0, 0, o), link = (x, o, y) => sc.links.push({ a: x.id, o, b: y.id });
    for (const st of ['standing', 'walking', 'running', 'jumping', 'falling', 'inair']) { const e = add('onState', { state: st }), s = add('say', { text: 'enter ' + st }); link(e, 'next', s); }
    const f = add('onKey', { key: 'f' }), ik = add('ifState', { state: 'moving' }), t1 = add('say', { text: 'f while moving' }), t2 = add('say', { text: 'f not moving' }); link(f, 'next', ik); link(ik, 'then', t1); link(ik, 'else', t2);
    S.tags.set('player.hudMove', true); S.tags.set('player.breatheAmount', 1); S.tags.set('player.bobAmount', 2);
  });
  await p.click('button:has-text("Walk map1")'); await p.waitForTimeout(800);
  await p.evaluate(() => { window.__said = []; const q = __studio.S.play; q.sh.o.say = t => window.__said.push(t); q.look = () => {}; });
  await p.mouse.click(700, 400); await p.waitForTimeout(500);
  const info = () => p.evaluate(() => { const q = __studio.S.play; return JSON.stringify({ state: q.T.get('player.state'), vm: q.vmOff && q.vmOff.map(x => +x.toFixed(2)), hud: q.hudHost.style.transform }); });
  const said = async () => { const r = await p.evaluate(() => { const r = window.__said.join(' | '); window.__said = []; return r; }); return r; };
  console.log('standing', await info(), '|', await said());
  await p.keyboard.press('f'); await p.waitForTimeout(100); console.log('f standing:', await said());
  await p.keyboard.down('w'); await p.waitForTimeout(500); console.log('walking', await info()); await p.keyboard.press('f'); await p.waitForTimeout(100); console.log('f walking:', await said());
  await p.evaluate(() => { const q = __studio.S.play; q.x = -100; q.y = 0; q.yaw = 0; }); await p.keyboard.down('Shift'); await p.waitForTimeout(300); console.log('running', await info(), await said()); await p.keyboard.up('Shift'); await p.keyboard.up('w');
  await p.waitForTimeout(400); await said();
  await p.keyboard.down('Space'); await p.waitForTimeout(150); await p.keyboard.up('Space'); console.log('jump', await info(), await said());
  await p.waitForTimeout(1500); console.log('landed', await info(), await said());
  // hud movement: turn quickly
  await p.evaluate(() => { const q = __studio.S.play; q.yaw += 0.2; }); await p.waitForTimeout(60); console.log('hud after a turn', await info());
  await p.waitForTimeout(900); console.log('hud settled', await info());
};
