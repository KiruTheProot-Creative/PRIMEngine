// Charge "particles fly into the charge" + smooth / faceted particle shading + objects off screen are culled. Frames are stepped by hand (works on slow software GL).
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/Arm_canon.fbx', SP + '/cube_bin.fbx']); await p.waitForTimeout(1800);
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(400);
  await p.click('button:has-text("New weapon")'); await p.click('.pitem:has-text("Arm_canon")'); await p.waitForTimeout(1500);
  await p.evaluate(() => { const S = __studio.S, T = S.tags, pr = S.project; T.setMany({ 'player.att.emitZ': -100, 'player.att.emitY': 62, 'player.att.projType': 'particle', 'player.att.projColor': '#33ccff' });
    const cube = pr.assets.find(a => /cube/.test(a.id)).id, d = PRIM.Schema.addProp(pr, { name: 'Box', type: 'decoration', asset: cube }); pr.maps[0].things = [{ id: 't1', prop: d.id, x: 3072 + 400, y: 3072, ang: 0 }, { id: 't2', prop: d.id, x: 3072 - 400, y: 3072, ang: 0 }]; pr.maps[0].n.t = 2; T.emit([]); });
  await p.click('button:has-text("Player view")').catch(() => {}); await p.waitForTimeout(300);
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.hud, null, { timeout: 8000 }); await p.waitForTimeout(800);
  const steps = (n, extra) => p.evaluate(([n, extra]) => { const q = __studio.S.play; q.msg.style.display = 'none'; q.look = () => {}; q.body = null; q.x = 3072; q.y = 3072; q.yaw = 0; q.pitch = 0; if (extra === 'orb') { q.orbCmd('player.att', 'show', { color: '#33ccff', size: 3 }); q.orbCmd('player.att', 'suck', { rate: 60, radius: 10, size: 0.35, time: 0.4, color: null }); }
    let t = q.last || performance.now(); for (let i = 0; i < n; i++) { t += 1000 / 60; q.frame(t); cancelAnimationFrame(q.raf); } q.raf = requestAnimationFrame(x => q.frame(x));
    const o = q.orb; return JSON.stringify({ bits: o && o.sp ? o.sp.length : 0, culled: q.view.culledFx }); }, [n, extra]);
  console.log('after 0.5 s of charging', await steps(30, 'orb'));
  console.log('bits move inward', await p.evaluate(() => { const o = __studio.S.play.orb; return JSON.stringify(o.sp.slice(0, 3).map(q => [Math.round(Math.hypot(...q.off)), +(q.t / q.T).toFixed(2)])); }));
  await p.screenshot({ path: out + '_smooth.png', timeout: 90000 }).catch(() => console.log('(screenshot skipped)'));
  await p.evaluate(() => __studio.S.tags.set('player.att.projShade', 'flat')); await steps(3);
  await p.screenshot({ path: out + '_flat.png', timeout: 90000 }).catch(() => console.log('(screenshot skipped)'));
  console.log('one box ahead, one behind -> culled', await steps(2));
  await p.evaluate(() => __studio.S.tags.set('gfx.culling', false)); console.log('culling off', await steps(2));
};
