module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.click('.mcard:has-text("DOOM")'); await p.waitForSelector('.medcv');
  await p.evaluate(() => { const S = __studio.S, m = S.project.maps[0]; PRIM.MapGeo.addRoom(S.project, m, S.tags, [[128, -128], [384, -128], [384, 128], [128, 128]].map(([x, y]) => ({ x, y }))); S.tags.emit([]); });
  const walk = async () => { await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(300); await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.hudHost, null, { timeout: 8000 }); await p.waitForTimeout(800);
    await p.evaluate(() => { const q = __studio.S.play; q.msg.style.display = 'none'; q.look = () => {}; q.yaw = 0; q.pitch = 0; q.x = 60; q.y = 0; });
    await p.keyboard.down('w'); await p.waitForTimeout(1800); await p.keyboard.up('w'); const x = await p.evaluate(() => __studio.S.play.x); await p.screenshot({ path: out + '_walk.png' }); await p.keyboard.press('Escape'); await p.waitForTimeout(300); return x; };
  console.log('closed wall: x =', await walk());
  await p.click('button:has-text("Leave")').catch(() => {}); await p.waitForTimeout(300);
  // open only on s2's side (its left wall)
  await p.evaluate(() => { const S = __studio.S, m = S.project.maps[0], s2 = m.sectors[1]; for (let i = 0; i < s2.v.length; i++) { const a = m.verts.find(v => v.id === s2.v[i]), b = m.verts.find(v => v.id === s2.v[(i + 1) % 4]); if (a.x === 128 && b.x === 128) S.tags.set('map1.' + a.id + '.open', true); } });
  await p.click('.tab:has-text("Map")'); await p.waitForTimeout(300);
  console.log('open on one side: x =', await walk());
};
