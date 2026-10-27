const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n').slice(0, 3).join(' | ')));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.click('.mcard:has-text("DOOM")'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/assets_test/Images/logo.png']); await p.waitForTimeout(600);
  await p.evaluate(() => { const S = __studio.S, T = S.tags; T.set('player.hudMove', true); T.set('player.hudMoveAmount', 1.5); });
  await p.click('.hrow:has-text("hud") >> nth=0'); await p.click('.tbtn:has-text("Create") >> nth=0'); await p.click('.pitem:has-text("Image")'); await p.waitForTimeout(300);
  await p.locator('.insp select').first().selectOption({ label: 'logo.png' }); await p.waitForTimeout(300);
  await p.locator('.insp .fld', { hasText: 'Stretch to screen' }).locator('input[type=checkbox]').first().check(); await p.waitForTimeout(300);
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(400); await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.hudHost, null, { timeout: 8000 }).catch(async () => { console.log('play missing; tab=', await p.evaluate(() => __studio.S.tab + ' ' + document.body.innerText.slice(0,0))); }); await p.waitForTimeout(800);
  await p.evaluate(() => { const q = __studio.S.play; q.msg.style.display = 'none'; });
  const m = () => p.evaluate(() => { const q = __studio.S.play, el = q.hudHost.querySelector('img.rt-n'), r = el.getBoundingClientRect(), h = q.hudHost.getBoundingClientRect(); return JSON.stringify({ imgL: Math.round(r.left - h.left), imgR: Math.round(h.right - r.right), imgT: Math.round(r.top - h.top), imgB: Math.round(h.bottom - r.bottom), tf: q.hudHost.style.transform }); });
  console.log('still', await m());
  await p.mouse.click(700, 400); for (let i = 0; i < 30; i++) { await p.mouse.move(700 + (i % 2 ? 300 : -300), 400 + (i % 2 ? 150 : -150)); await p.evaluate(() => { const f = __studio.S.play; f.hx = 70 * 1.5; f.hy = 50 * 1.5; }); }
  console.log('swaying (negative = gap)', await m()); await p.screenshot({ path: out + '_v.png' });
};
