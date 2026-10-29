// Sprite pick-ups with post-processing (HDR + bloom) and a light on them, on a floor, on a raised floor and on top of a wall block.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/orb_blue.png']); await p.waitForTimeout(1500);
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, m = pr.maps[0], G = PRIM.Grid, img = pr.assets.find(a => a.name.includes('blue')).id, C = c => (c + 0.5) * 128;
    const orb = Sc.addProp(pr, { name: 'Orb', type: 'pickup', look: 'sprite', spriteIdle: img, spriteHeight: 40, lightOn: true, lightColor: '#3a6cff' }).id;
    G.edit(m, 0, 24, 22).h = 6; const w = G.edit(m, 0, 25, 22); w.w = '';
    m.things = [{ id: 'a', prop: orb, x: C(23), y: C(22), ang: 0, layer: 0 }, { id: 'b', prop: orb, x: C(24), y: C(22), ang: 0, layer: 0 }, { id: 'c', prop: orb, x: C(25), y: C(22), ang: 0, layer: 0 }]; m.n.t = 9; S.tags.emit([]); });
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(300);
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.things && __studio.S.play.things.length >= 3, null, { timeout: 15000 }); await p.waitForTimeout(800);
  console.log('floors', await p.evaluate(() => JSON.stringify(__studio.S.play.things.map(o => [o.th.id, Math.round(o.floor), o.sprite]))));
  for (const post of [false, true]) {
    const url = await p.evaluate(post => { const q = __studio.S.play; cancelAnimationFrame(q.raf); q.msg.style.display = 'none'; q.view.post = post; q.x = 24.5 * 128; q.y = 22.5 * 128 + 330; q.yaw = -Math.PI / 2; q.pitch = 0; q.body = null; let t = q.last; for (let i = 0; i < 3; i++) { t += 16; q.frame(t); cancelAnimationFrame(q.raf); } const u = q.view.gl.canvas.toDataURL('image/png'); q.raf = requestAnimationFrame(z => q.frame(z)); return u; }, post);
    require('fs').writeFileSync(out + (post ? '_post.png' : '_plain.png'), Buffer.from(url.split(',')[1], 'base64')); }
};
