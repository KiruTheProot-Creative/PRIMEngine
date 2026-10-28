// Wall / ceiling crawlers, blind enemies (timer shots, damage hitbox), the self-destruct sphere settings. Frames stepped by hand.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/Arm_canon.fbx']); await p.waitForTimeout(2500);
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, G = PRIM.Grid, m = pr.maps[0], arm = pr.assets.find(a => /arm/i.test(a.id + a.name)).id, sc = 40 / ((PRIM.FBX.cache[arm] || { bounds: { size: 40 } }).bounds.size || 40);
    for (let y = 20; y <= 27; y++) { const c = G.edit(m, 0, 24, y); c.f = c.f || ''; c.h = 8; }   // a block half a story tall across the room
    const E = (name, over) => Sc.addProp(pr, Object.assign({ name, type: 'enemy', asset: arm, scale: sc, height: 40, radius: 16, speed: 220, sight: 3000, attack: 'melee', attackRange: 20, damage: 5, attackRate: 0.5, attackDelay: 0 }, over)).id;
    const ids = window.__ids = { crawler: E('Crawler', { crawl: true }), blindShot: E('Blind gun', { detect: 'blind', blindMode: 'shoot', attack: 'projectile', projSpeed: 900 }), blindTouch: E('Blind spikes', { detect: 'blind', blindMode: 'touch', damage: 7, attackRate: 0.4 }),
      bomber: E('Bomber', { behavior: 'stationary', mount: 'ceiling', ceilAction: 'selfdestruct', fuse: 0.1, blastRadius: 180, blastDamage: 10, blastSphereColor1: '#a0ffff', blastSphereColor2: '#2040ff', blastSphereAnim: 'swirl', blastSphereTime: 0.8 }) };
    const C = c => (c + 0.5) * 128; m.things = [{ id: 'tc', prop: ids.crawler, x: C(22), y: C(24), ang: 0, layer: 0 }, { id: 'tg', prop: ids.blindShot, x: C(21), y: C(21), ang: 0, layer: 0 }, { id: 'tk', prop: ids.blindTouch, x: C(26), y: C(21), ang: 0, layer: 0 }, { id: 'tb', prop: ids.bomber, x: C(21), y: C(26), ang: 0, layer: 0 }]; m.n.t = 9; S.tags.emit([]); });
  await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(300); await p.evaluate(() => { __studio.S.propSel = window.__ids.crawler; }); await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(400);
  console.log('crawler fields:', await p.evaluate(() => [...document.querySelectorAll('.insp .fold')].filter(f => /Behaviour/.test(f.innerText)).map(f => f.innerText.replace(/\s+/g, ' ').slice(0, 300)).join('')));
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(300);
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.things && __studio.S.play.things.length >= 4, null, { timeout: 12000 }); await p.waitForTimeout(400);
  const C = c => (c + 0.5) * 128;
  const go = (n, fn, px, py) => p.evaluate(([n, fn, px, py]) => { const q = __studio.S.play; q.msg.style.display = 'none'; if (px !== null) { q.x = px; q.y = py; q.body = null; q.z = 0; } q.T.set('player.health', Math.max(50, q.T.get('player.health'))); let t = q.last || performance.now(); const tr = []; for (let i = 0; i < n; i++) { t += 16; q.frame(t); cancelAnimationFrame(q.raf); const o = q.things.find(x => x.th.id === 'tc'); if (i % 10 === 0) tr.push([Math.round(o.x), Math.round(o.floor), o.surf ? o.surf.n.map(Math.round).join('') : o.st[0]]); } q.raf = requestAnimationFrame(z => q.frame(z));
    const r = fn ? (new Function('q', fn))(q) : null; return JSON.stringify({ r, hp: Math.round(q.T.get('player.health')), trail: tr }); }, [n, fn || '', px === undefined ? null : px, py === undefined ? null : py]);
  // the crawler is behind the block: it climbs it, walks over, comes down and reaches you
  await p.evaluate(() => { const o = __studio.S.play.things.find(x => x.th.id === 'tc'); o.awake = true; });
  console.log('crawl 1:', await go(60, null, C(26.4), C(24)));
  console.log('crawl 2:', await go(60));
  console.log('crawl 3:', await go(90));
  // on the ceiling: put it up there, it crawls over you and drops
  console.log('ceiling:', await go(60, 'const o = q.things.find(x => x.th.id === "tc"); return { st: o.st, surf: !!o.surf };', null, null), await p.evaluate(() => { const q = __studio.S.play, o = q.things.find(x => x.th.id === 'tc'); o.surf = { n: [0, -1, 0], p: [(22 + 0.5) * 128, 127, (22.5) * 128] }; o.st = 'chase'; o.awake = true; return 'placed on ceiling'; }));
  console.log('ceiling crawl:', await go(150, 'const o = q.things.find(x => x.th.id === "tc"); return { st: o.st, surf: o.surf && o.surf.n };', C(25.5), C(22.5)));
  // blind gun: faces south (+Z); you stand north of it -> it still fires south, not at you
  console.log('blind gun:', await go(60, 'return q.fx.filter(f => f.hostile).map(f => f.d.map(x => +x.toFixed(2)))', C(21), C(20.2)));
  console.log('blind spikes (touching):', await go(60, null, C(26), C(21) + 20), '| away:', await go(30, null, C(25), C(23)));
  // bomber: its sphere uses the new settings
  console.log('bomber sphere:', await go(30, 'const s = q.fx.find(f => f.kind === "shock"); return s && { R: s.R, life: s.life, c1: s.c1, c2: s.c2, anim: s.anim };', C(21), C(26)));
  { const url = await p.evaluate(() => { const q = __studio.S.play; cancelAnimationFrame(q.raf); q.yaw = Math.PI / 2; q.x = (21.5) * 128; q.y = (24.5) * 128; q.frame(q.last + 16); cancelAnimationFrame(q.raf); const u = q.view.gl.canvas.toDataURL('image/png'); q.raf = requestAnimationFrame(z => q.frame(z)); return u; }); require('fs').writeFileSync(out + '_sphere.png', Buffer.from(url.split(',')[1], 'base64')); }
};
