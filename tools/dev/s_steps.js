// Footsteps: walk across flat floor, up stairs and down a slope and log when footstep sounds play (they must not double up).
module.exports = async (p, out) => {
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv'); await p.waitForTimeout(300);
  await p.evaluate(() => { const S = __studio.S, m = S.project.maps[0], G = PRIM.Grid; Object.assign(G.edit(m, 0, 22, 23), { sh: 'stairs', sd: 1, sr: 6 }); Object.assign(G.edit(m, 0, 23, 23), { h: 6 }); Object.assign(G.edit(m, 0, 24, 23), { h: 6, sh: 'slope', sd: 3, sr: 6 });
    Object.assign(G.edit(m, 0, 24, 23), { h: 0 }); for (let x = 20; x < 28; x++) delete G.edit(m, 0, x, 23).c; S.tags.setMany({ 'player.footstep': 'fs1', 'player.footstep2': 'fs2', 'player.landSound': 'land', 'player.jumpSound': 'jump' }); });
  await p.click('button:has-text("Walk this map")'); await p.waitForTimeout(800);
  const r = await p.evaluate(() => { const q = __studio.S.play; cancelAnimationFrame(q.raf); q.dead = false; const log = []; let t = 0; q.psound = tag => log.push([+t.toFixed(2), tag, Math.round(q.x), Math.round(q.z)]); q.footstep = (orig => function () { log.push([+t.toFixed(2), 'STEP', Math.round(q.x), Math.round(q.z)]); })();
    q.body = null; q.x = 20 * 128 + 64; q.y = 23 * 128 + 64; q.z = 0; q.yaw = 0; q.ground = true; q.keys.w = true;
    for (let i = 0; i < 60 * 5; i++) { t += 1 / 60; q.step(1 / 60); } q.keys.w = false; return log; });
  for (const l of r) console.log(JSON.stringify(l));
  await p.keyboard.press('Escape');
};
