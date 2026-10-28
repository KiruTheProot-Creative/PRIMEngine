// Node update 2: Player script (double jump, push, hurt/heal), messages, "when a tag becomes", math nodes, "not while reloading", new menu categories.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles(SP + '/Arm_canon.fbx'); await p.waitForTimeout(1500);
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(500);
  await p.click('button:has-text("New weapon")'); await p.click('.pitem:has-text("Arm_canon")'); await p.waitForTimeout(1500);
  await p.click('button:has-text("Player view")').catch(() => {}); await p.waitForTimeout(200);
  // the Player's own Node Coder
  await p.click('.hrow:has-text("Player") >> nth=0'); await p.waitForTimeout(300);
  await p.click('button:has-text("Open the Player\'s Node Coder")'); await p.waitForTimeout(500);
  await p.mouse.move(600, 300); await p.keyboard.press('Shift+A'); await p.waitForTimeout(200);
  console.log('player menu:', (await p.locator('.pop .pitem').allTextContents()).join(' | '));
  await p.locator('.pop .pitem:has-text("Player")').last().hover(); await p.waitForTimeout(200);
  console.log('player submenu:', (await p.locator('.pop .pitem').allTextContents()).join(' | '));
  await p.locator('.pop .pitem:has-text("Double jump")').click(); await p.waitForTimeout(300);
  await p.screenshot({ path: out + '_coder.png' });
  // build the scripts directly
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, pl = pr.player, T = S.tags;
    pl.script = { nodes: [], links: [] }; const A = (sc, key, type, x, y, over, mt) => Sc.addSNode(pr, key, sc, type, x, y, over, mt), L = (sc, a, b, o) => sc.links.push({ a: a.id, o: o || 'next', b: b.id });
    let a = A(pl.script, 'player', 'onKey', 0, 0, { key: 'space' }), b = A(pl.script, 'player', 'airJump', 250, 0, { max: 1 }); L(pl.script, a, b);
    a = A(pl.script, 'player', 'onKey', 0, 150, { key: 'q' }); b = A(pl.script, 'player', 'launch', 250, 150, { forward: 900 }); L(pl.script, a, b);
    a = A(pl.script, 'player', 'onKey', 0, 300, { key: 'm' }); b = A(pl.script, 'player', 'sendMessage', 250, 300, { name: 'Ouch' }); L(pl.script, a, b);
    a = A(pl.script, 'player', 'onMessage', 0, 450, { name: 'ouch' }); b = A(pl.script, 'player', 'hurtPlayer', 250, 450, { amount: 30 }); L(pl.script, a, b);
    a = A(pl.script, 'player', 'onTag', 0, 600, { tag: 'player.health', op: '<=', value: '50' }); b = A(pl.script, 'player', 'say', 250, 600, { text: 'low health {player.health}' }); L(pl.script, a, b);
    a = A(pl.script, 'player', 'onKey', 0, 750, { key: 'h' }); b = A(pl.script, 'player', 'hurtPlayer', 250, 750, { amount: -15 }); L(pl.script, a, b);
    a = A(pl.script, 'player', 'onKey', 0, 900, { key: 'k' }); b = A(pl.script, 'player', 'mathTag', 250, 900, { tag: 'player.speed', op: '*', value: 2 }); const c = A(pl.script, 'player', 'toggleTag', 500, 900, { tag: 'player.sprintEnabled' }), d = A(pl.script, 'player', 'randomTag', 750, 900, { tag: 'player.jumpHeight', min: 40, max: 60 }); L(pl.script, a, b); L(pl.script, b, c); L(pl.script, c, d);
    a = A(pl.script, 'player', 'onLand', 0, 1050); b = A(pl.script, 'player', 'changeTag', 250, 1050, { tag: 'player.maxHealth', amount: 1 }); L(pl.script, a, b);
    // weapon: fire animation, but not while reloading
    const w = pr.player.attachments[0], an = Object.keys(PRIM.FBX.cache[T.get('player.att.asset')].anims)[0]; w.script = { nodes: [], links: [] };
    a = A(w.script, 'player.att', 'onClick', 0, 0); b = A(w.script, 'player.att', 'playAnim', 250, 0, { anim: an }, 'player.att.asset'); L(w.script, a, b);
    for (const c of Object.values(pr.maps[0].layers[0].cells)) delete c.c;   // open sky: room to jump
    S.tags.emit([]); window.__an = an; });
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(300); await p.click('button:has-text("Player view")').catch(() => {}); await p.waitForTimeout(200);
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.hud, null, { timeout: 8000 }); await p.waitForTimeout(500);
  const q = (fn, arg) => p.evaluate(fn, arg);
  await q(() => { const q = __studio.S.play; q.msg.style.display = 'none'; q.look = () => {}; q.body = null; q.x = 3072; q.y = 3072; q.yaw = 0; window.__said = []; q.o.say = t => __said.push(t); });
  // double jump: jump from the ground, then press space again in the air
  console.log('double jump', await q(() => { const q = __studio.S.play, out = []; for (let i = 0; i < 300 && !q.ground; i++) q.step(1 / 60); q.airJumps = 0; out.push(['on ground', q.ground]); q.press('space'); for (let i = 0; i < 20; i++) q.step(1 / 60); q.release('space'); out.push(['air after 1st', Math.round(q.z), Math.round(q.vz)]);
    q.press('space'); out.push(['req', q.jumpReq, q.airJumps, q.ground]); q.step(1 / 60); out.push(['2nd jump', Math.round(q.vz)]); q.release('space'); for (let i = 0; i < 10; i++) q.step(1 / 60); q.press('space'); q.step(1 / 60); out.push(['3rd press (not allowed)', Math.round(q.vz)]); q.release('space');
    for (let i = 0; i < 240 && !q.ground; i++) q.step(1 / 60); out.push(['landed', q.ground, 'air jumps reset', q.airJumps]); return JSON.stringify(out); }));
  console.log('push forward', await q(() => { const q = __studio.S.play, x0 = q.x; q.press('q'); q.release('q'); for (let i = 0; i < 30; i++) q.step(1 / 60); return Math.round(q.x - x0) + ' units in 0.5 s (standing still)'; }));
  await q(() => { const q = __studio.S.play; q.press('m'); q.release('m'); }); await p.waitForTimeout(300);
  await q(() => { const q = __studio.S.play; q.press('m'); q.release('m'); }); await p.waitForTimeout(400);
  console.log('message -> hurt x2', await q(() => __studio.S.play.T.get('player.health')), 'said', await q(() => JSON.stringify(__said)));
  await q(() => { const q = __studio.S.play; q.press('h'); q.release('h'); }); await p.waitForTimeout(200); console.log('heal', await q(() => __studio.S.play.T.get('player.health')));
  await q(() => { const q = __studio.S.play; q.press('k'); q.release('k'); }); await p.waitForTimeout(200); console.log('math', await q(() => { const T = __studio.S.play.T; return JSON.stringify([T.get('player.speed'), T.get('player.sprintEnabled'), T.get('player.jumpHeight')]); }));
  console.log('onLand ran', await q(() => __studio.S.play.T.get('player.maxHealth')));
  // not while reloading
  console.log('fire anim', await q(() => { const q = __studio.S.play, at = q.atts[0]; at.fire = null; q.reloading = { at, t: 5 }; q.T.set('player.reloading', true); q.fire(); const during = at.fire; return new Promise(r => setTimeout(() => { const d2 = at.fire; q.reloading = null; q.T.set('player.reloading', false); q.fire(); setTimeout(() => r(JSON.stringify({ duringReload: d2, after: at.fire })), 100); }, 100)); }));
  await p.keyboard.press('Escape');
};
