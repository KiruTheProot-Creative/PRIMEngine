module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.click('.mcard:has-text("DOOM")'); await p.waitForSelector('.medcv');
  const add = pts => p.evaluate(pts => { const S = __studio.S, m = S.project.maps[0], r = PRIM.MapGeo.addRoom(S.project, m, S.tags, pts.map(([x, y]) => ({ x, y }))); S.tags.emit([]);
    return JSON.stringify({ err: r.error, secs: (r.sectors || []).map(s => s.id), cut: r.cut, total: m.sectors.map(s => s.id + ':' + s.v.length).join(' ') }); }, pts);
  console.log('touching  ', await add([[128, -128], [384, -128], [384, 128], [128, 128]]));
  console.log('T-corridor', await add([[-384, -32], [-128, -32], [-128, 32], [-384, 32]]));
  console.log('overlap   ', await add([[0, 64], [64, 64], [64, 320], [0, 320]]));
  console.log('inside    ', await add([[-32, -32], [32, -32], [32, 32], [-32, 32]]));
  console.log('surround  ', await add([[-600, -600], [600, -600], [600, 600], [-600, 600]]));
  console.log('walls', await p.evaluate(() => { const m = __studio.S.project.maps[0], W = PRIM.R3D.walls(m, __studio.S.tags); return W.length; }));
  await p.click('.tab:has-text("Map")'); await p.waitForTimeout(400); await p.click('button:has-text("Zoom to fit")'); await p.waitForTimeout(300); await p.screenshot({ path: out + '_rooms.png' });
};
