// "When a shot hits something" node, charged impacts: other hit / explosion sounds, splash radius grows with the charge.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/Arm_canon.fbx', SP + '/beep.wav', SP + '/boomcharged.wav']); await p.waitForTimeout(1500);
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(400);
  await p.click('button:has-text("New weapon")'); await p.click('.pitem:has-text("Arm_canon")'); await p.waitForTimeout(1500);
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, T = S.tags, snd = n => pr.assets.find(a => a.type === 'sound' && a.name.startsWith(n)).id;
    T.define('hits', { type: 'number', value: 0, min: 0, max: 999, step: 1, label: 'Hits', group: 'hits', custom: true });
    T.setMany({ 'player.att.emitZ': -100, 'player.att.emitY': 62, 'player.att.projType': 'hitscan', 'player.att.projSplash': true, 'player.att.splashRadius': 100, 'player.att.splashCharge': 2.5, 'player.att.splashSphere': true,
      'player.att.impactSound': snd('beep'), 'player.att.impactSoundCharged': snd('boomcharged'), 'player.att.explodeSound': snd('beep'), 'player.att.explodeSoundCharged': snd('boomcharged') });
    const w = pr.player.attachments[0]; w.script = { nodes: [], links: [] }; const A = (sc, key, type, x, y, over) => Sc.addSNode(pr, key, sc, type, x, y, over, 'player.att.asset'), L = (sc, a, b) => sc.links.push({ a: a.id, o: 'next', b: b.id });
    let a = A(w.script, 'player.att', 'onImpact', 0, 0), b = A(w.script, 'player.att', 'changeTag', 250, 0, { tag: 'hits', amount: 1 }); L(w.script, a, b);
    a = A(w.script, 'player.att', 'onImpact', 0, 150, { what: 'level', charged: 'charged' }); b = A(w.script, 'player.att', 'say', 250, 150, { text: 'charged shot hit the level' }); L(w.script, a, b);
    a = A(w.script, 'player.att', 'onImpact', 0, 300, { what: 'enemy' }); b = A(w.script, 'player.att', 'say', 250, 300, { text: 'enemy hit (wrong!)' }); L(w.script, a, b);
    const pl = pr.player; pl.script = { nodes: [], links: [] }; a = A(pl.script, 'player', 'onImpact', 0, 0, { what: 'end' }); b = A(pl.script, 'player', 'say', 250, 0, { text: 'a shot flew into nothing' }); L(pl.script, a, b);
    T.emit([]); });
  await p.click('.insp :text-is("Material")').catch(() => {}); await p.click('.insp :text-is("Object")').catch(() => {}); await p.waitForTimeout(300);
  console.log('fields:', await p.evaluate(() => ['Wall hit sound of a charged shot', 'counts as charged from', 'Splash radius at full charge', 'Explosion sound of a charged shot'].map(k => document.querySelector('.insp').innerText.includes(k)).join(',')));
  await p.click('button:has-text("Player view")').catch(() => {}); await p.waitForTimeout(300);
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.hud, null, { timeout: 8000 }); await p.waitForTimeout(800);
  await p.evaluate(() => { window.__snd = []; const o = PRIM.Audio.play; PRIM.Audio.play = (u, v, l, at) => { const a = __studio.S.project.assets.find(x => __studio.S.play.urls[x.id] === u); window.__snd.push(a ? a.name : '?'); return o(u, v, l, at); }; window.__said = []; const q = __studio.S.play; q.o.say = t => __said.push(t); q.msg.style.display = 'none'; });
  const shot = (charge, setup) => p.evaluate(([charge, setup]) => { const q = __studio.S.play; q.body = null; q.x = 3072; q.y = 3072; q.yaw = 0; q.pitch = 0; if (setup) (new Function('q', setup))(q); window.__snd = []; q.fx = []; q.fireCharge = charge;
    let t = q.last || performance.now(); t += 16; q.frame(t); cancelAnimationFrame(q.raf); q.fireProj(q.atts[0]); for (let i = 0; i < 20; i++) { t += 16; q.frame(t); cancelAnimationFrame(q.raf); } q.raf = requestAnimationFrame(x => q.frame(x));
    const sh = (q.fx || []).find(f => f.kind === 'shock'); return JSON.stringify({ sounds: __snd, hits: q.T.get('hits'), shockRadius: sh ? Math.round(sh.R) : null }); }, [charge, setup || '']);
  console.log('normal shot   ', await shot(0));
  console.log('half charge   ', await shot(0.5));
  console.log('full charge   ', await shot(1));
  console.log('into nothing  ', await shot(0, "q.T.set('player.att.projType', 'particle'); q.T.set('player.att.projLife', 0.05); q.T.set('player.att.projSpeed', 300);"));
  await p.waitForTimeout(300); console.log('said', await p.evaluate(() => JSON.stringify(__said)));
};
