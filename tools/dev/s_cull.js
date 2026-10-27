module.exports = async (p, out) => {
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.click('.mcard:has-text("DOOM")'); await p.waitForSelector('.medcv');
  // a long row of separate rooms (a corridor of boxes) along x
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, m = pr.maps[0];
    for (let i = 1; i <= 12; i++) { const x0 = i * 400, ids = [[x0 - 128, -128], [x0 + 128, -128], [x0 + 128, 128], [x0 - 128, 128]].map(([x, y]) => Sc.addVert(pr, m, x, y).id); Sc.addSector(pr, m, ids); }
    S.tags.emit([]); });
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(400); await p.click('button:has-text("Walk map1")'); await p.waitForTimeout(1500);
  await p.evaluate(() => { const q = __studio.S.play; q.msg.style.display = 'none'; q.look = () => {}; q.yaw = 0; q.x = -60; __studio.S.tags.set('gfx.showFps', true); });
  const st = () => p.evaluate(() => JSON.stringify(__studio.S.play.view.stats));
  await p.waitForTimeout(500); console.log('culling on, facing the rooms', await st());
  await p.evaluate(() => { __studio.S.play.yaw = Math.PI; }); await p.waitForTimeout(500); console.log('culling on, facing away', await st());
  await p.evaluate(() => { __studio.S.tags.set('gfx.culling', false); }); await p.waitForTimeout(500); console.log('culling OFF, facing away', await st());
  await p.screenshot({ path: out + '_cull.png' });
  console.log('no console errors');
};
