// Weapon aim button: lock on (target, marker, view follows, homing shots) and sights (zoom, slower mouse, weapon moves); spread + pellets.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/Arm_canon.fbx']); await p.waitForTimeout(2500);
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(500);
  await p.click('button:has-text("New weapon")'); await p.click('.pitem:has-text("Arm_canon")'); await p.waitForTimeout(1500);
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, T = S.tags, at = pr.player.attachments[0], pre = 'player.' + at.id + '.', arm = pr.assets.find(a => /arm/i.test(a.id + a.name)).id;
    T.setMany({ [pre + 'projType']: 'particle', [pre + 'projSpeed']: 600, [pre + 'aimType']: 'lock', [pre + 'lockTurn']: 10 }); window.__pre = pre;
    const en = Sc.addProp(pr, { name: 'Target', type: 'enemy', asset: arm, scale: 0.3, height: 60, radius: 20, speed: 0, sight: 10, attack: 'melee', damage: 0, health: 1000 }).id;
    const m = pr.maps[0]; m.things.push({ id: 'te', prop: en, x: 24.5 * 128 + 300, y: 24.5 * 128 + 60, ang: 0, layer: 0 }); S.tags.emit([]); });
  await p.click('.objtabs button:has-text("Aim")'); await p.waitForTimeout(300);
  console.log('aim tab:', (await p.locator('.insp').innerText()).replace(/\s+/g, ' ').match(/Aim button.{0,260}/)[0]);
  await p.screenshot({ path: out + '_insp.png' });
  await p.evaluate(() => { __studio.S.objView = false; }); await p.click('.tab:has-text("Map")'); await p.waitForTimeout(200); await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(400);
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.things && __studio.S.play.things.length >= 1, null, { timeout: 12000 }); await p.waitForTimeout(400);
  const go = (n, fn) => p.evaluate(([n, fn]) => { const q = __studio.S.play; q.msg.style.display = 'none'; if (fn) (new Function('q', fn))(q); let t = q.last || performance.now(); for (let i = 0; i < n; i++) { t += 16; q.frame(t); cancelAnimationFrame(q.raf); } q.raf = requestAnimationFrame(z => q.frame(z));
    const T = q.T, o = q.things.find(x => x.th.id === 'te'), c = q.view.cam, d = [o.x - c.x, o.y - c.z], off = Math.atan2(d[1], d[0]) - q.yaw;
    return JSON.stringify({ aiming: T.get('player.aiming'), locked: T.get('player.locked'), yawOff: +Math.atan2(Math.sin(off), Math.cos(off)).toFixed(3), marker: q.lockEl ? q.lockEl.style.display : '-', fovDeg: +(q.view.fov * 180 / Math.PI).toFixed(1), k: +(q.aimK || 0).toFixed(2) }); }, [n, fn || '']);
  console.log('start (facing east, target 17° to the right):', await go(2, 'q.x = 24.5 * 128; q.y = 24.5 * 128; q.yaw = 0; q.pitch = 0; q.body = null;'));
  console.log('hold aim:', await go(20, 'q.aimInput("mouse2", true)'));
  console.log('view follows:', await go(30));
  console.log('debug', await p.evaluate(() => { const q = __studio.S.play, w = q.aimW(), c = q.view.cam, f = PRIM.R3D.camBasis(c.yaw, c.pitch).f; return JSON.stringify({ w: w && w.type, things: q.things.map(o => [o.type, o.th.id, q.eAlive(o), o.init, o.st, Math.round(o.x), Math.round(o.y)]), cam: [c.x, c.y, c.z].map(Math.round), f: f.map(x => +x.toFixed(2)), lock: q.lock && !!q.lock.o, found: !!(w && q.lockFind(w)), dbg: (() => { const o = q.things[0], e = [c.x, c.y, c.z], d = PRIM.M4.sub(q.eCenter(o), e), L = PRIM.M4.len(d), a = Math.acos(PRIM.M4.dot(d, f) / L) * 180 / Math.PI, h = q.view.raycast(e, d.map(x => x / L), L); return { L: Math.round(L), a: +a.toFixed(1), hit: h && Math.round(h.t), rng: q.T.get(w.pre + 'lockRange'), ang: q.T.get(w.pre + 'lockAngle'), info: h && h.info }; })() }); }));
  await p.evaluate(() => { const q = __studio.S.play; cancelAnimationFrame(q.raf); q.frame(q.last + 16); cancelAnimationFrame(q.raf); window.__img = q.view.gl.canvas.toDataURL('image/png'); q.raf = requestAnimationFrame(z => q.frame(z)); });
  console.log('fire while locked:', await p.evaluate(() => { const q = __studio.S.play; q.fx = []; q.fireProj(q.curWeapon()); const f = q.fx.find(x => x.pre); return JSON.stringify({ homing: !!(f && f.home), toward: f && f.d.map(x => +x.toFixed(2)) }); }));
  console.log('let go:', await go(10, 'q.aimInput("mouse2", false)'));
  // sights: zoom, weapon moves, slower mouse, spread tighter
  await p.evaluate(() => { const T = __studio.S.play.T, pre = window.__pre; T.setMany({ [pre + 'aimType']: 'sights', [pre + 'aimZoom']: 2, [pre + 'adsX']: -10, [pre + 'aimSens']: 0.5, [pre + 'projSpread']: 10, [pre + 'projCount']: 8, [pre + 'aimSpread']: 0.2 }); });
  const spread = () => p.evaluate(() => { const q = __studio.S.play; q.fx = []; q.fireProj(q.curWeapon()); const ds = q.fx.filter(f => f.pre).map(f => f.d), m = ds.reduce((a, d) => a.map((x, i) => x + d[i] / ds.length), [0, 0, 0]), L = Math.hypot(...m); return JSON.stringify({ pellets: ds.length, widestDeg: +Math.max(...ds.map(d => Math.acos(Math.min(1, (d[0] * m[0] + d[1] * m[1] + d[2] * m[2]) / L)) * 180 / Math.PI)).toFixed(1) }); });
  console.log('hip fire spread:', await spread());
  const sens = () => p.evaluate(() => { const q = __studio.S.play, y0 = q.yaw; q.look(100, 0); const d = q.yaw - y0; q.yaw = y0; return +d.toFixed(3); });
  console.log('mouse turn (normal):', await sens());
  console.log('aim down sights:', await go(30, 'q.aimInput("mouse2", true)'));
  console.log('mouse turn (aiming):', await sens(), '| aimed spread:', await spread());
  require('fs').writeFileSync(out + '_lock.png', Buffer.from((await p.evaluate(() => window.__img)).split(',')[1], 'base64'));
  { const url = await p.evaluate(() => { const q = __studio.S.play; cancelAnimationFrame(q.raf); q.frame(q.last + 16); cancelAnimationFrame(q.raf); const u = q.view.gl.canvas.toDataURL('image/png'); q.raf = requestAnimationFrame(z => q.frame(z)); return u; }); require('fs').writeFileSync(out + '_sights.png', Buffer.from(url.split(',')[1], 'base64')); }
};
