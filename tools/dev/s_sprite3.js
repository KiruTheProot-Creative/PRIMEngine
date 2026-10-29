// Sprite visibility from many distances and angles (counts blue pixels of the orb sprite).
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await p.evaluate(() => { window.__L = 1; }); await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/orb_blue.png']); await p.waitForTimeout(1500);
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, m = pr.maps[0], img = pr.assets.find(a => a.name.includes('blue')).id, C = c => (c + 0.5) * 128;
    const orb = Sc.addProp(pr, { name: 'Orb', type: 'pickup', look: 'sprite', spriteIdle: img, spriteHeight: 60, lightOn: true, lightColor: '#3a6cff' }).id; const lamp = Sc.addProp(pr, { name: 'Lamp', type: 'decoration', lightOn: true, lightColor: '#7df3ff', lightHeight: 110 }).id; S.tags.setMany({ [m.id + '.fogOn']: true, [m.id + '.fogStart']: 300, [m.id + '.fogEnd']: 2000, [m.id + '.ao']: true });
    const G = PRIM.Grid, L0 = m.layers[0].cells; for (const k in L0) { const c = L0[k], [x, y] = k.split(',').map(Number), u = G.edit(m, 1, x, y); if (G.isWall(c)) u.w = c.w; else { delete c.c; u.c = ''; } } m.things = [{ id: 'a', prop: orb, x: C(24), y: C(24), ang: 0, layer: +(window.__L || 0) }, { id: 'l', prop: lamp, x: C(22), y: C(22), ang: 0, layer: 0 }]; m.n.t = 9; S.tags.emit([]); });
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(300);
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.things && __studio.S.play.things.length >= 1, null, { timeout: 15000 }); await p.waitForTimeout(800);
  const res = await p.evaluate(() => { const q = __studio.S.play, v = q.view, gl = v.gl, o = q.things.find(x => x.th.id === 'a'), out = []; v.post = true; cancelAnimationFrame(q.raf); q.msg.style.display = 'none'; o.th.prop; const cx = o.fr.cx, cy = o.fr.cy; out.push('floor ' + o.floor);
    const W = gl.drawingBufferWidth, H = gl.drawingBufferHeight, px = new Uint8Array(W * H * 4);
    for (const d of [100, 200, 400]) { const row = []; for (let a = 0; a < 360; a += 45) { const r = a * Math.PI / 180; q.x = cx + Math.cos(r) * d; q.y = cy + Math.sin(r) * d; q.body = null; q.yaw = Math.atan2(cy - q.y, cx - q.x); q.pitch = -0.2; let t = q.last; t += 16; q.frame(t); cancelAnimationFrame(q.raf); t += 16; q.frame(t); cancelAnimationFrame(q.raf); if (!out.lights) out.lights = 'lights ' + (v.lightsN || '') + ' sprite? ' + !!o.sprite + ' things ' + q.things.length;
      gl.readPixels(0, 0, W, H, gl.RGBA, gl.UNSIGNED_BYTE, px); let n = 0; for (let i = 0; i < px.length; i += 4) if (px[i + 2] > 150 && px[i] < 90 && px[i + 1] < 120) n++; row.push(a + ':' + n); } out.push(d + ' -> ' + row.join(' ')); }
    q.raf = requestAnimationFrame(z => q.frame(z)); return out; });
  console.log(res.join('\n'), res.lights);
};
