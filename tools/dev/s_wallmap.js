module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.click('.mcard:has-text("DOOM")'); await p.waitForSelector('.medcv');
  console.log('ceil', await p.evaluate(() => __studio.S.tags.get('map1.s1.ceil')), 'eye', await p.evaluate(() => __studio.S.tags.get('player.eyeHeight')));
  await p.evaluate(() => { const S = __studio.S, pr = S.project, W = PRIM.WallMesh, w = W.create(pr, 'Test wall'); const f = w.faces[0];
    const r = W.extrude(pr, w, [f.id]); for (const id of r.moved) { const v = W.vert(w, id); v.z += 14; }
    W.inset(pr, w, [f.id], 14); const r2 = W.extrude(pr, w, [f.id]); for (const id of r2.moved) W.vert(w, id).z += 8;
    W.cut(pr, w, [w.faces[w.faces.length - 1].id], 1, 0.5);
    S.tags.set(w.id + '.' + f.id + '.color', '#ff8844');
    for (const v of pr.maps[0].verts) { pr.tags['map1.' + v.id + '.rows'].value = w.id; }
    S.tags.emit([]); window.__w = w.id; });
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(400); await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.hudHost, null, { timeout: 8000 }); await p.waitForTimeout(800);
  await p.evaluate(() => { const q = __studio.S.play; q.msg.style.display = 'none'; q.look = () => {}; q.yaw = 0; q.x = -40; q.y = 0; });
  await p.waitForTimeout(500); await p.screenshot({ path: out + '_wm.png' });
  console.log('stats', await p.evaluate(() => JSON.stringify(__studio.S.play.view.stats)));
  console.log('no console errors');
};
