// Screen editor: multi-select (shift-click, box), line up, group, drag together with snapping guides, copy / paste, undo / redo.
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(500);
  const ctr = id => p.evaluate(id => { const el = __studio.S.rt.els[id], r = el.getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; }, id);
  const st = () => p.evaluate(() => { const S = __studio.S, T = S.tags; return { sel: S.selIds, primary: S.selId, n: __studio.S.project.screens.find(s => s.id === 'main').nodes.length, pos: Object.fromEntries(S.project.screens.find(s => s.id === 'main').nodes.map(n => [n.id, [T.get('main.' + n.id + '.x'), T.get('main.' + n.id + '.y')]])) }; });
  let c = await ctr('title'); await p.mouse.click(c[0], c[1]); c = await ctr('play'); await p.keyboard.down('Shift'); await p.mouse.click(c[0], c[1]); await p.keyboard.up('Shift'); await p.waitForTimeout(200);
  console.log('shift-click:', JSON.stringify((await st()).sel), await p.evaluate(() => !!document.querySelector('.insp .fh') && [...document.querySelectorAll('.insp .fh')].map(x => x.innerText).join('|')));
  await p.click('button:has-text("⇤ Left")'); await p.waitForTimeout(200);
  console.log('left edges:', await p.evaluate(() => ['title', 'play'].map(id => Math.round(__studio.S.rt.els[id].getBoundingClientRect().left)).join(' ')));
  // box select settings + quit
  const a = await ctr('settings'), b = await ctr('quit'), stage = await p.evaluate(() => { const r = __studio.S.rt.stage.getBoundingClientRect(); return [r.left, r.top, r.width]; });
  await p.mouse.move(stage[0] + 20, a[1] - 25); await p.mouse.down(); await p.mouse.move(stage[0] + stage[2] - 20, b[1] + 25, { steps: 6 }); await p.mouse.up(); await p.waitForTimeout(200);
  console.log('box select:', JSON.stringify((await st()).sel));
  await p.keyboard.press('Control+g'); await p.waitForTimeout(200);
  c = await ctr('quit'); await p.mouse.click(c[0], c[1]); await p.waitForTimeout(150); console.log('click one of the group:', JSON.stringify((await st()).sel));
  const before = await st(); c = await ctr('quit'); await p.mouse.move(c[0], c[1]); await p.mouse.down(); await p.mouse.move(c[0] + 60, c[1] + 30, { steps: 6 }); await p.mouse.up(); await p.waitForTimeout(500);
  const after = await st(); console.log('group drag moved both:', JSON.stringify(['settings', 'quit'].map(id => [after.pos[id][0] - before.pos[id][0], after.pos[id][1] - before.pos[id][1]])));
  await p.keyboard.press('Control+c'); await p.keyboard.press('Control+v'); await p.waitForTimeout(500); console.log('pasted:', (await st()).n, JSON.stringify((await st()).sel));
  await p.keyboard.press('Control+z'); await p.waitForTimeout(400); console.log('undo paste:', (await st()).n);
  await p.keyboard.press('Control+z'); await p.waitForTimeout(400); const u2 = await st(); console.log('undo drag:', JSON.stringify(u2.pos.quit), 'was', JSON.stringify(before.pos.quit));
  await p.keyboard.press('Control+y'); await p.waitForTimeout(400); console.log('redo drag:', JSON.stringify((await st()).pos.quit));
  // snapping: drag the title so its middle passes the screen middle
  await p.evaluate(() => __studio.S.tags.set('main.title.x', 40)); await p.waitForTimeout(200); c = await ctr('title'); await p.mouse.click(c[0], c[1]); await p.mouse.move(c[0], c[1]); await p.mouse.down(); await p.mouse.move(c[0] - 18, c[1] + 2, { steps: 4 });
  const guides = await p.evaluate(() => document.querySelectorAll('.rt-guide').length); await p.screenshot({ path: out + '_guides.png' }); await p.mouse.up(); await p.waitForTimeout(200);
  console.log('snap to middle:', JSON.stringify((await st()).pos.title), 'guides', guides);
  await p.keyboard.press('ArrowRight'); await p.keyboard.press('Shift+ArrowUp'); await p.waitForTimeout(200); console.log('nudge:', JSON.stringify((await st()).pos.title));
};
