const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad', fs = require('fs');
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/cube_bin.fbx', SP + '/beep.wav', SP + '/assets_test/Images/logo.png']); await p.waitForTimeout(1200);
  await p.evaluate(() => { const S = __studio.S, pr = S.project; pr.name = 'Test Game'; const e = PRIM.Schema.addProp(pr, { name: 'Box', type: 'decoration', asset: pr.assets.find(a => a.type === 'model').id }); pr.maps[0].things = [{ id: 't1', prop: e.id, x: 0, y: 80, ang: 0 }]; pr.maps[0].n.t = 1; S.tags.emit([]); });
  await p.click('.mb:has-text("File")'); console.log('menu has Build Game', await p.evaluate(() => [...document.querySelectorAll('.pitem')].some(e => /Build Game/.test(e.textContent)))); await p.keyboard.press('Escape');
  const r = await p.evaluate(async () => { const r = await window.__gameHtml(); return { html: r.html, missing: r.missing }; });
  fs.writeFileSync(SP + '/built_game.html', r.html); console.log('built', Math.round(r.html.length / 1024) + ' KB, missing', r.missing);
  // play the built file in a fresh page
  const g = await p.context().newPage(); g.on('pageerror', e => console.log('GAME PAGEERROR', e.message)); g.on('console', m => { if (m.type() === 'error') console.log('GAME CONSOLE', m.text()); });
  await g.setViewportSize({ width: 1280, height: 720 }); await g.goto('file://' + SP + '/built_game.html'); await g.waitForTimeout(1500);
  console.log('game screen', await g.evaluate(() => [...document.querySelectorAll('.rt-n')].map(e => e.textContent).filter(Boolean).slice(0, 6).join(' | ')));
  for (let i = 0; i < 6; i++) { const t = await g.evaluate(() => document.body.innerText); if (/play/i.test(t)) break; await g.mouse.click(640, 360); await g.waitForTimeout(700); }
  console.log('menu', await g.evaluate(() => [...document.querySelectorAll('.rt-n')].map(e => e.textContent).filter(Boolean).slice(0, 6).join(' | ')));
  await g.screenshot({ path: out + '_menu.png' });
  // press the button that starts the game
  const btn = await g.evaluate(() => { const b = [...document.querySelectorAll('.rt-n')].find(e => /play|start/i.test(e.textContent)); if (!b) return null; const r = b.getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; });
  if (btn) { await g.mouse.click(...btn); await g.waitForTimeout(2500); }
  console.log('in game', await g.evaluate(() => !!document.querySelector('.p3root canvas')), 'hud', await g.evaluate(() => !!document.querySelector('.hud-root')));
  await g.screenshot({ path: out + '_game.png' });
};
