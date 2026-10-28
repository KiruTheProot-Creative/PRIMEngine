module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await p.click('.hrow:has-text("hud") >> nth=0'); await p.waitForTimeout(400);
  const size = () => p.evaluate(() => { const w = document.querySelector('#center .hed'); return w ? w.clientWidth + 'x' + w.clientHeight : 'none'; });
  console.log('editor before', await size());
  for (let i = 0; i < 2; i++) { await p.click('.tbtn.play'); await p.waitForTimeout(600); console.log('testing', await p.evaluate(() => __studio.S.testing), 'hud drawn', await p.evaluate(() => !!document.querySelector('#center .hud-root'))); await p.click('.tbtn.play'); await p.waitForTimeout(500); console.log('editor after stop', await size()); }
  const r = await p.evaluate(() => { const R = __studio.S.rt, q = R.rect('healthbar'), w = R.wrap.getBoundingClientRect(); return { x: w.left + q.x0 + q.w / 2, y: w.top + q.y0 + q.h / 2 }; });
  const x0 = await p.evaluate(() => __studio.S.tags.get('hud.healthbar.x'));
  await p.mouse.move(r.x, r.y); await p.mouse.down(); await p.mouse.move(r.x + 40, r.y, { steps: 4 }); await p.mouse.up(); await p.waitForTimeout(200);
  console.log('drag works after play/stop:', x0, '->', await p.evaluate(() => __studio.S.tags.get('hud.healthbar.x')));
  await p.screenshot({ path: out + '_hp.png' });
};
