// Splash damage + explosions (particles / animation), directional (3D) sound and the speaker object.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/cube_bin.fbx', SP + '/Arm_canon.fbx', SP + '/beep.wav', SP + '/boom.png']); await p.waitForTimeout(1800);
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(500);
  await p.click('button:has-text("New weapon")'); await p.click('.pitem:has-text("Arm_canon")'); await p.waitForTimeout(1500);
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, T = S.tags, m = pr.maps[0], A = id => pr.assets.find(a => a.type === id), cube = pr.assets.find(a => a.type === 'model' && /cube/.test(a.id)).id;
    const en = Sc.addProp(pr, { name: 'Dummy', type: 'enemy', asset: cube }), sp = Sc.addProp(pr, { name: 'Radio', type: 'speaker' });
    T.setMany({ [en.id + '.speed']: 1, [en.id + '.sight']: 1, [en.id + '.radius']: 20, [en.id + '.health']: 100, [sp.id + '.spkSound']: A('sound').id, [sp.id + '.spkRadius']: 512,
      'player.att.emitZ': -100, 'player.att.emitY': 62, 'player.att.projType': 'particle', 'player.att.projSplash': true, 'player.att.splashRadius': 200, 'player.att.splashDamage': 40, 'player.att.explodeType': 'particles', 'player.att.explodeCount': 10, 'player.att.explodeSound': A('sound').id });
    m.things = [{ id: 't1', prop: en.id, x: 3400, y: 3072, ang: 0 }, { id: 't2', prop: sp.id, x: 3072, y: 3072 - 300, ang: 0 }]; m.n.t = 2; S.tags.emit([]); window.__ids = { en: en.id, sp: sp.id }; });
  await p.click('.insp :text-is("Material")'); await p.waitForTimeout(200); await p.click('.insp :text-is("Object")'); await p.waitForTimeout(300);
  console.log('weapon fields', await p.locator('.insp').innerText().then(t => ['Splash damage', 'Splash radius', 'Explosion', 'How many particles', 'Explosion sound'].map(k => k + ':' + t.includes(k)).join(' ')));
  await p.screenshot({ path: out + '_insp.png' });
  // map editor shows the speaker with its radius
  await p.click('.tab:has-text("Map")'); await p.waitForTimeout(400); await p.screenshot({ path: out + '_map.png' });
  // play
  await p.evaluate(nl => { window.__snd = []; const o = PRIM.Audio.play; PRIM.Audio.play = (u, v, l, at) => { const h = o(u, v, l, at); window.__snd.push({ loop: !!l, at: at ? { p: at.p.map(Math.round), max: at.max } : null, h }); return h; }; });
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(300); await p.click('button:has-text("Player view")'); await p.waitForTimeout(300); await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.hud, null, { timeout: 8000 }); await p.waitForTimeout(800);
  await p.evaluate(() => { const q = __studio.S.play; q.msg.style.display = 'none'; q.look = () => {}; q.body = null; q.x = 3072; q.y = 3072; q.yaw = 0; q.pitch = 0; });
  await p.waitForTimeout(600);
  console.log('speaker sound', await p.evaluate(() => JSON.stringify(__snd.filter(s => s.loop).map(s => ({ at: s.at, mixer: !!s.h.dg })))), 'ear', await p.evaluate(() => JSON.stringify(PRIM.Audio.ear && { p: PRIM.Audio.ear.p.map(Math.round), r: PRIM.Audio.ear.r.map(v => +v.toFixed(2)) })));
  // which ear: the speaker is north of us (y - 300); facing east (yaw 0) north is on the LEFT. In Web Audio coordinates the speaker must be on the listener's left (dot with its right vector < 0).
  console.log('speaker on the left ear', await p.evaluate(() => JSON.stringify(__snd.find(s => s.loop).h.mix)));
  // turn around (face west): the same speaker must now be on the right, smoothly
  await p.evaluate(() => { __studio.S.play.yaw = Math.PI; }); await p.waitForTimeout(800);
  console.log('after turning around (right ear)', await p.evaluate(() => JSON.stringify(__snd.find(s => s.loop).h.mix)));
  await p.evaluate(() => { __studio.S.play.yaw = 0; });
  // splash damage next to the enemy (60 units away from its middle = 40 from its body)
  const st = () => p.evaluate(() => { const q = __studio.S.play, e = q.things.find(o => o.type === 'enemy'); return JSON.stringify({ enemyHp: e && Math.round(e.hp), me: q.T.get('player.health'), deco: (q.fx || []).filter(f => f.deco && f.kind === 'sphere').length, flash: (q.fx || []).filter(f => f.kind === 'flash').length, marks: (q.marks || []).filter(m => m.bill).length, booms: __snd.filter(s => !s.loop && s.at).length }); });
  await p.evaluate(() => { const q = __studio.S.play, e = q.things.find(o => o.type === 'enemy'); q.explode('player.att.', [e.x - 60, e.floor + 50, e.y], [-1, 0, 0]); });
  console.log('splash near enemy', await st());
  await p.waitForTimeout(150); await p.screenshot({ path: out + '_particles.png', timeout: 60000 }).catch(() => console.log('(screenshot skipped: slow software GL)'));
  // far away: no damage; behind a wall: no damage
  await p.evaluate(() => { const q = __studio.S.play, e = q.things.find(o => o.type === 'enemy'); q.explode('player.att.', [e.x - 400, e.floor + 50, e.y], [-1, 0, 0]); });
  console.log('splash far away', await st());
  // splash on yourself
  await p.evaluate(() => { const q = __studio.S.play; q.T.set('player.att.splashSelf', true); q.explode('player.att.', [q.x + 50, q.z + 40, q.y], [-1, 0, 0]); });
  console.log('self splash', await st());
  // animation explosion
  await p.evaluate(() => { const q = __studio.S.play, T = q.T; T.set('player.att.splashSelf', false); T.set('player.att.explodeType', 'animation'); T.set('player.att.explodeImage', __studio.S.project.assets.find(a => a.type === 'image').id); T.set('player.att.explodeFrames', 6); T.set('player.att.explodeFps', 6); T.set('player.att.explodeSize', 80); q.explode('player.att.', [q.x + 200, q.z + 60, q.y], [-1, 0, 0]); });
  await p.waitForFunction(() => (__studio.S.play.marks || []).some(m => m.bill && m.frame > 0), null, { timeout: 6000 }).catch(() => console.log('ANIM NOT ADVANCING')); console.log('animation explosion', await st(), await p.evaluate(() => JSON.stringify((__studio.S.play.marks || []).filter(m => m.bill).map(m => ({ frame: m.frame, n: m.n.map(v => +v.toFixed(2)) })))));
  await p.screenshot({ path: out + '_anim.png', timeout: 60000 }).catch(() => console.log('(screenshot skipped: slow software GL)'));
  // a real shot: particle flies into the wall east, lands, explodes
  await p.evaluate(() => { const q = __studio.S.play; q.fx = []; q.marks = []; q.T.set('player.att.explodeType', 'particles'); q.fireProj(q.atts.find(a => a.id === 'att')); window.__n0 = __snd.filter(s => !s.loop && s.at).length; });
  console.log('fired', await p.evaluate(() => { const q = __studio.S.play; return JSON.stringify({ fx: (q.fx || []).map(f => [f.kind, f.p && f.p.map(Math.round)]), frames: q.view.frames, emit: !!q.atts.find(a => a.id === 'att').emit }); }));
  await p.waitForFunction(() => (__studio.S.play.fx || []).some(f => f.deco), null, { timeout: 8000 }).catch(() => console.log('NO EXPLOSION'));
  console.log('shot landed', await st(), await p.evaluate(() => { const q = __studio.S.play, e = q.things.find(o => o.type === 'enemy'); return 'enemy now ' + Math.round(e.hp) + ' (' + e.st + ')'; }));
  await p.waitForTimeout(1500); console.log('after it settles', await st());
  await p.keyboard.press('Escape'); await p.waitForTimeout(300);
  console.log('speaker stopped after leaving', await p.evaluate(() => __snd.filter(s => s.loop).every(s => !s.h.playing)));
};
