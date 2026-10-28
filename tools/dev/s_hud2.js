const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n').slice(0, 2).join(' | ')));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.click('.mcard:has-text("DOOM")'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/assets_test/Images/logo.png']); await p.waitForTimeout(600);
  await p.click('.hrow:has-text("hud") >> nth=0'); await p.waitForTimeout(500);
  console.log('editor', await p.evaluate(() => !!document.querySelector('#center .hed') + ' z=' + __studio.S.rt.z.toFixed(3)));
  const T = (k) => p.evaluate(k => __studio.S.tags.get(k), k);
  const r = await p.evaluate(() => { const R = __studio.S.rt, q = R.rect('healthbar'), w = R.wrap.getBoundingClientRect(); return { x: w.left + q.x0 + q.w / 2, y: w.top + q.y0 + q.h / 2 }; });
  console.log('before', await T('hud.healthbar.x'), await T('hud.healthbar.y'));
  await p.mouse.move(r.x, r.y); await p.mouse.down(); await p.mouse.move(r.x + 60, r.y - 30, { steps: 5 }); await p.mouse.up(); await p.waitForTimeout(200);
  console.log('after drag (+60px,-30px)', await T('hud.healthbar.x'), await T('hud.healthbar.y'), 'selected', await p.evaluate(() => __studio.S.selId));
  const hd = await p.evaluate(() => { const e = [...document.querySelectorAll('.hed-h')].find(x => x.dataset.cx === '1' && x.dataset.cy === '1'), b = e.getBoundingClientRect(); return { x: b.left + 7, y: b.top + 7, vis: getComputedStyle(e).display }; });
  console.log('handle', hd.vis); await p.mouse.move(hd.x, hd.y); await p.mouse.down(); await p.mouse.move(hd.x + 50, hd.y + 10, { steps: 4 }); await p.mouse.up(); await p.waitForTimeout(200);
  console.log('after resize w/h', await T('hud.healthbar.w'), await T('hud.healthbar.h'));
  await p.evaluate(() => __studio.S.tags.set('hud.healthtext.x', -900)); await p.waitForTimeout(200);
  console.log('outside-frame element drawn', await p.evaluate(() => { const R = __studio.S.rt, q = R.rect('healthtext'), fr = R.frame.getBoundingClientRect(), w = R.wrap.getBoundingClientRect(); return JSON.stringify({ left: Math.round(q.x0 + w.left), frameLeft: Math.round(fr.left), visible: getComputedStyle(R.hud.items.healthtext.el).display }); }));
  await p.screenshot({ path: out + '_ed.png' });
  await p.mouse.move(r.x, r.y); await p.mouse.wheel(0, -300); await p.waitForTimeout(200); console.log('zoom', await p.evaluate(() => __studio.S.rt.z.toFixed(3)));
  await p.keyboard.press('f'); await p.waitForTimeout(100);
  // stretched visor + sway in the game
  await p.evaluate(() => { const S = __studio.S, T = S.tags; T.set('player.hudMove', true); T.set('player.hudMoveAmount', 1.5); T.set('hud.healthtext.x', -260); });
  await p.click('.tbtn:has-text("Create") >> nth=0'); await p.click('.pitem:has-text("Image")'); await p.waitForTimeout(300);
  await p.locator('.insp select').first().selectOption({ label: 'logo.png' }); await p.waitForTimeout(200);
  await p.locator('.insp .fld', { hasText: 'Stretch to screen' }).locator('input[type=checkbox]').first().check(); await p.waitForTimeout(300);
  await p.screenshot({ path: out + '_ed2.png' });
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(400); await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.hud, null, { timeout: 8000 }); await p.waitForTimeout(800);
  await p.evaluate(() => { __studio.S.play.msg.style.display = 'none'; __studio.S.play.feel = () => {}; });
  const cover = () => p.evaluate(() => { const q = __studio.S.play, el = q.hudHost.querySelector('img.hud-fill'), r = el.getBoundingClientRect(); return JSON.stringify({ L: Math.round(r.left), T: Math.round(r.top), R: Math.round(innerWidth - r.right), B: Math.round(innerHeight - r.bottom) }); });
  for (const [w, h] of [[1440, 860], [1000, 900], [1600, 600]]) { await p.setViewportSize({ width: w, height: h }); await p.waitForTimeout(400);
    const res = []; for (const [sx, sy] of [[0, 0], [105, 75], [-105, -75], [105, -75], [-105, 75]]) { await p.evaluate(([a, b]) => __studio.S.play.hud.sway(a, b), [sx, sy]); await p.waitForTimeout(60); res.push(await cover()); }
    console.log(w + 'x' + h + ' (all values must be <= 0: image past every window edge)', res.join(' ')); }
  await p.evaluate(() => __studio.S.play.hud.sway(105, 75)); await p.waitForTimeout(100); await p.screenshot({ path: out + '_game.png' });
};
