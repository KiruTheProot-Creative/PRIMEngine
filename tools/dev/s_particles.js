// Particle "graphics cache": balls drawn with one stored quad (instancing) vs the old rebuilt-every-frame spheres; budget, culling,
// fire rate, moving-light shadow cap, charged shot only affects the next shot. Frames are stepped by hand.
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles(['/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad/cube_bin.fbx']); await p.waitForTimeout(1500);
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(300);
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.view, null, { timeout: 8000 }); await p.waitForTimeout(500);
  // the same 1500 balls, timed through both paths (CPU time of the draw call, gl.finish included)
  console.log('bench', await p.evaluate(() => { const q = __studio.S.play, v = q.view; cancelAnimationFrame(q.raf);
    const c = v.cam, B = PRIM.R3D.camBasis(c.yaw, c.pitch), list = []; for (let i = 0; i < 1500; i++) { const d = 150 + Math.random() * 500, sx = (Math.random() - 0.5) * d * 0.8, sy = (Math.random() - 0.5) * d * 0.4; list.push({ kind: 'sphere', p: [0, 1, 2].map(k => [c.x, c.y, c.z][k] + B.f[k] * d + B.s[k] * sx + B.u[k] * sy), c: [1, 0.6, 0.2], size: 2 + Math.random() * 6, flat: i % 7 === 0 }); }
    const gl = v.gl, time = () => { v.draw(); gl.finish(); let t0 = performance.now(); for (let k = 0; k < 3; k++) v.drawFx(list); const js = (performance.now() - t0) / 3; t0 = performance.now(); gl.finish(); return [+js.toFixed(1), +((performance.now() - t0) / 3 + js).toFixed(1)]; };
    const b0 = (v.partPr = null, time()); v.partPr = undefined; const a = time(), st = JSON.stringify(v.fxStats); window.__list = list;
    return JSON.stringify({ ext: (gl.getSupportedExtensions() || []).filter(e => /instanc/i.test(e)).join(','), newCpuMs: a[0], newTotalMs: a[1], oldCpuMs: b0[0], oldTotalMs: b0[1], instancing: !!v.inst, stats: st }); }));
  await p.evaluate(() => { const v = __studio.S.play.view; v.draw(); v.drawFx(__list); v.present(); }); await p.screenshot({ path: out + '_bench.png', timeout: 90000 }).catch(() => console.log('(skip)'));
  await p.evaluate(() => { const q = __studio.S.play; q.raf = requestAnimationFrame(x => q.frame(x)); });
  // a weapon: particle shots that explode into 200 bits, light with shadows, 8 shots per second
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, T = S.tags; const att = { id: 'att9' }; pr.player.attachments.push(att); Sc.defineAttach(pr, att, { type: 'weapon', name: 'Blaster', asset: pr.assets.find(a => /cube/.test(a.id)).id }); att.script = { nodes: [], links: [] }; Sc.ensurePlayer(pr);
    T.setMany({ 'player.att9.projType': 'particle', 'player.att9.projSplash': false, 'player.att9.explodeType': 'particles', 'player.att9.explodeCount': 200, 'player.att9.projLight': true, 'player.att9.fireRate': 8, 'player.att9.projSpeed': 300 }); T.emit([]); });
  await p.keyboard.press('Escape').catch(() => {}); await p.waitForTimeout(300);
  await p.evaluate(() => { const q = __studio.S.play; if (q) q.leave(); }); await p.waitForTimeout(300);
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.view && !__studio.S.play.dead, null, { timeout: 8000 }); await p.waitForTimeout(500);
  const run = (code) => p.evaluate(code => { const q = __studio.S.play; cancelAnimationFrame(q.raf); const r = (new Function('q', code))(q); q.raf = requestAnimationFrame(x => q.frame(x)); return JSON.stringify(r); }, code);
  console.log('weapon', await run('const a = q.atts.find(x => x.id === "att9"); return { found: !!a, emit: !!(a && a.emit) };'));
  // spam: try 60 shots in 1 second of game time -> only 8 per second get out
  console.log('fire rate', await run(`const a = q.atts.find(x => x.id === "att9"); let n0 = (q.fx || []).length, shots = 0; const real = performance.now; let fake = real.call(performance); performance.now = () => fake;
    for (let i = 0; i < 60; i++) { const before = (q.fx || []).length; q.fireProj(a); if ((q.fx || []).length > before) shots++; fake += 1000 / 60; q.frame(fake); cancelAnimationFrame(q.raf); } performance.now = real; return { triedIn1s: 60, shotsOut: shots };`));
  // many explosions at once: the budget keeps the bits under control
  console.log('budget', await run(`const pre = "player.att9."; for (let i = 0; i < 20; i++) { q.explode(pre, [q.x, q.z + 50, q.y - 300 - i * 10], [0, 1, 0], "wall", 0); q.stepFx(0.001); } const bits = q.fx.filter(f => f.deco && f.kind === "sphere").length; return { explosions: 20, wanted: 4000, bitsAlive: bits };`));
  // moving lights: 6 glowing shots in view -> only 1 gets a shadow map; enemy shots never
  console.log('shadows', await run(`const v = q.view; v.setLights([0, 1, 2, 3, 4, 5].map(i => ({ p: [q.x + i * 30, q.z + 60, q.y - 200], r: 220, c: [1, 1, 1], type: 0, shadow: true }))); v.renderShadows(); const a = v.shSlots.length; return { movingLights: 6, withShadows: a };`));
  // charged shot then normal shots: only the first one is big
  console.log('charge then normal', await run(`const a = q.atts.find(x => x.id === "att9"); q.T.set("player.att9.fireRate", 0); q.fx = []; q.pendingShot = { size: 20, ch: 1, until: performance.now() + 1500 }; q.fireProj(a); q.fireProj(a); q.fireProj(a); return q.fx.filter(f => f.kind === "sphere" && !f.deco).map(f => +f.size.toFixed(1));`));
};
