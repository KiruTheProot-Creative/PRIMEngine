module.exports = async (p, out) => {
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(400);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv'); await p.waitForTimeout(300);
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(700);
  console.log('sound tags', await p.evaluate(() => ['footstep', 'footstep2', 'stepDistance', 'jumpSound', 'landSound', 'hurtSound', 'deathSound', 'soundVolume'].map(k => k + '=' + __studio.S.tags.get('player.' + k)).join(' ')));
  console.log('inspector groups', await p.evaluate(() => [...document.querySelectorAll('.insp .grp, .insp .gh')].map(e => e.textContent.trim()).join('|')));
  await p.evaluate(() => {
    const S = __studio.S, Sc = PRIM.Schema, pr = S.project; let a = pr.player.attachments[0];
    if (!a) { a = { id: 'att', script: { nodes: [], links: [] } }; pr.player.attachments.push(a); Sc.defineAttach(pr, a, { name: 'tester', type: 'animation' }); }
    const k = 'player.' + a.id, sc = a.script, add = (t, o) => Sc.addSNode(pr, k, sc, t, 0, 0, o), link = (x, o, y) => sc.links.push({ a: x.id, o, b: y.id });
    const h = add('onHold', { key: 'mouse1', interval: 0.05 }), hs = add('say', { text: 'hold' }); link(h, 'next', hs);
    const r = add('onKey', { key: 'mouse2' }), rs = add('say', { text: 'rmb' }); link(r, 'next', rs);
    const u = add('onKeyUp', { key: 'mouse2' }), us = add('say', { text: 'rmbup' }); link(u, 'next', us);
    const f = add('onKey', { key: 'f' }), ik = add('ifKey', { key: 'shift' }), t1 = add('say', { text: 'shift-f' }), t2 = add('say', { text: 'plain-f' }); link(f, 'next', ik); link(ik, 'then', t1); link(ik, 'else', t2);
    S.tags.set('player.footstep', 'fakeA'); S.tags.set('player.footstep2', 'fakeB'); S.tags.set('player.jumpSound', 'fakeJ'); S.tags.set('player.hurtSound', 'fakeH'); S.tags.set('player.stepDistance', 40);
  });
  await p.click('button:has-text("Walk map1")'); await p.waitForTimeout(600);
  await p.evaluate(() => { window.__said = []; window.__sfx = []; const q = __studio.S.play; q.sh.o.say = t => window.__said.push(t); q.sfx = id => window.__sfx.push(id); });
  await p.mouse.click(700, 400); await p.waitForTimeout(150);
  const said = () => p.evaluate(() => { const r = window.__said.join(','); window.__said = []; return r; });
  await said();
  await p.mouse.move(700, 400); await p.mouse.down({ button: 'left' }); await p.waitForTimeout(500); await p.mouse.up({ button: 'left' });
  const holds = (await said()).split(',').filter(x => x === 'hold').length; console.log('hold fired', holds, 'times in 0.5s (expect ~8-10)');
  await p.waitForTimeout(300); const after = (await said()); console.log('after release (expect empty):', JSON.stringify(after));
  await p.mouse.down({ button: 'right' }); await p.waitForTimeout(100); await p.mouse.up({ button: 'right' }); await p.waitForTimeout(100); console.log('right button:', await said());
  await p.keyboard.press('f'); await p.waitForTimeout(100); console.log('plain f:', await said());
  await p.keyboard.down('Shift'); await p.keyboard.press('f'); await p.keyboard.up('Shift'); await p.waitForTimeout(100); console.log('shift f:', await said());
  await p.evaluate(() => { window.__sfx = []; }); await p.keyboard.down('w'); await p.waitForTimeout(800); await p.keyboard.up('w');
  console.log('footsteps', await p.evaluate(() => window.__sfx.join(',')));
  await p.evaluate(() => { window.__sfx = []; }); await p.keyboard.down('Space'); await p.waitForTimeout(100); await p.keyboard.up('Space'); await p.waitForTimeout(1200); console.log('jump sounds', await p.evaluate(() => window.__sfx.join(',')));
  await p.evaluate(() => { window.__sfx = []; }); await p.keyboard.press('h'); await p.waitForTimeout(200); console.log('hurt', await p.evaluate(() => window.__sfx.join(',')));
};
