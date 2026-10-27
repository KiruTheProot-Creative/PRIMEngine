module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message));
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await p.click('.hrow:has-text("hud") >> nth=0'); await p.waitForTimeout(400);
  await p.evaluate(() => { const T = __studio.S.tags, n = __studio.S.project.screens.find(s => s.kind === 'hud').nodes[0]; T.set('hud.' + n.id + '.x', -700); });
  await p.waitForTimeout(400);
  console.log('edit', await p.evaluate(() => { const st = document.querySelector('#center .rt-stage'), sf = st.querySelector('.rt-safe'); return JSON.stringify({ overflow: st.style.overflow, safe: sf && getComputedStyle(sf).display }); }));
  await p.screenshot({ path: out + '_hudedge.png' });
  await p.click('.hrow:has-text("main") >> nth=0'); await p.waitForTimeout(400);
  console.log('main screen', await p.evaluate(() => { const st = document.querySelector('#center .rt-stage'), sf = st.querySelector('.rt-safe'); return JSON.stringify({ overflow: st.style.overflow, safe: sf && getComputedStyle(sf).display }); }));
  console.log('no console errors');
};
