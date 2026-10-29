// Solid decorations: auto box around the model and own box; they block walking (you can stand on low ones) and shots. Frames stepped by hand.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/Arm_canon.fbx']); await p.waitForTimeout(2000);
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, m = pr.maps[0], arm = pr.assets.find(a => /arm/i.test(a.id + a.name)).id, sc = 100 / ((PRIM.FBX.cache[arm] || { bounds: { size: 100 } }).bounds.size || 100), C = c => (c + 0.5) * 128;
    const crate = Sc.addProp(pr, { name: 'Crate', type: 'decoration', asset: arm, scale: sc, solid: 'auto' }), low = Sc.addProp(pr, { name: 'Step', type: 'decoration', solid: 'box', solidY: 8, solidH: 16, solidW: 96, solidD: 96, solidShots: false });
    window.__ids = { crate: crate.id, low: low.id };
    m.things = [{ id: 'c', prop: crate.id, x: C(24), y: C(24), ang: 0, layer: 0 }, { id: 'l', prop: low.id, x: C(22), y: C(24), ang: 0, layer: 0 }]; m.n.t = 9; S.tags.emit([]); });
  await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(300); await p.evaluate(() => { __studio.S.propSel = window.__ids.crate; }); await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(600);
  console.log('solid foldout:', await p.evaluate(() => [...document.querySelectorAll('.insp .fold')].map(f => f.innerText.replace(/\s+/g, ' ')).filter(t => /Solid \(collision boxes\)/.test(t)).join('').slice(0, 220)));
  await p.evaluate(() => { __studio.S.tags.set(window.__ids.crate + '.solid', 'box'); }); await p.click('.tab:has-text("Timeline")'); await p.waitForTimeout(200); await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(600);
  await p.click('button:has-text("Fit this box to the model")'); await p.waitForTimeout(400); await p.screenshot({ path: out + '_editor.png' });
  console.log('fitted box:', await p.evaluate(() => { const T = __studio.S.tags, pre = window.__ids.crate + '.'; const pr = __studio.S.project.props.find(x => x.id + '.' === pre), b = pr.solids[0]; return T.get(pre + 'solid') + ' ' + ['x', 'y', 'z', 'w', 'h', 'd'].map(k => T.get(pre + 'sb.' + b.id + '.' + k)).join(' '); }));
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(300); await p.evaluate(() => { __studio.S.objView = false; }); await p.click('.tab:has-text("Timeline")'); await p.waitForTimeout(200); await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(400);
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.things && __studio.S.play.things.length >= 1, null, { timeout: 15000 }); await p.waitForTimeout(500);
  const C = c => (c + 0.5) * 128;
  const walk = (x, y, yaw, n) => p.evaluate(([x, y, yaw, n]) => { const q = __studio.S.play; q.msg.style.display = 'none'; q.x = x; q.y = y; q.z = 0; q.body = null; q.yaw = yaw; q.pitch = 0; q.keys.w = true; let t = q.last; for (let i = 0; i < n; i++) { t += 16; q.frame(t); cancelAnimationFrame(q.raf); } q.keys.w = false; q.raf = requestAnimationFrame(z => q.frame(z)); return [+(q.x / 128 - 0.5).toFixed(2), +(q.y / 128 - 0.5).toFixed(2), Math.round(q.z)]; }, [x, y, yaw, n]);
  console.log('walk east into the crate (from 22.6 -> stops before 24):', JSON.stringify(await walk(C(22.6), C(24), 0, 60)));
  console.log('walk onto the low step (z rises):', JSON.stringify(await walk(C(20.5), C(24), 0, 40)));
  console.log('shots:', await p.evaluate(() => { const q = __studio.S.play, v = q.view, C = c => (c + 0.5) * 128, h1 = v.raycast([C(22.6), 30, C(24)], [1, 0, 0], 800), h2 = v.raycast([C(20.5), 8, C(24)], [1, 0, 0], 200); return JSON.stringify({ crateHit: h1 && Math.round(h1.t), crateKind: h1 && h1.info && h1.info.kind, lowStepHit: h2 && h2.info && h2.info.kind }); }));
  console.log('walk across the low box (y follows its top):', await p.evaluate(() => { const q = __studio.S.play, G = PRIM.Grid, C = c => (c + 0.5) * 128, w = q.view.world, ex = q.doorTris(); const b = { pos: [C(22) - 48 - 17, 0, C(24)], vel: [0, 0, 0], r: 16, h: 110, step: 24, maxSlope: 0.69, ground: true }; const tr = [];
    for (let i = 0; i < 30; i++) { b.vel[0] = 200; b.vel[2] = 0; b.vel[1] -= 1100 / 60; G.move(b, 1 / 60, w, ex); if (i % 5 === 0) tr.push([Math.round(b.pos[0] - (C(22) - 48)), Math.round(b.pos[1]), b.ground ? 'g' : '', b.squeeze ? 'S' : '']); }
    return JSON.stringify({ tr, extra: ex.length, step: q.T.get('player.stepHeight') }); }));
};
