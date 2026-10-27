const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.click('.mcard:has-text("DOOM")'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles(SP + '/Arm_canon.fbx'); await p.waitForTimeout(1500);
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(600);
  await p.click('button:has-text("New weapon")'); await p.click('.pitem:has-text("Arm_canon")'); await p.waitForTimeout(1500);
  await p.evaluate(() => { const T = __studio.S.tags; T.set('player.att.emitZ', -100); T.set('player.att.emitY', 62); T.set('player.att.muzzleZ', -100); T.set('player.att.muzzleY', 62); T.set('player.att.x', 6); T.set('player.att.y', -14); T.set('player.att.z', -22); });
  // node coder: Shift+A menu with categories
  await p.click('.tab:has-text("Node Coder")'); await p.waitForTimeout(500);
  await p.mouse.move(500, 300); await p.keyboard.press('Shift+A'); await p.waitForTimeout(200);
  console.log('shift+A menu:', await p.locator('.pop .pitem').allTextContents());
  await p.locator('.pop .pitem:has-text("Weapon")').hover(); await p.waitForTimeout(200);
  console.log('weapon submenu:', await p.locator('.pop .pitem').allTextContents());
  await p.screenshot({ path: out + '_menu.png' });
  await p.locator('.pop .pitem:has-text("Charge beam")').click(); await p.waitForTimeout(300);
  console.log('nodes', await p.evaluate(() => JSON.stringify(__studio.S.project.player.attachments[0].script.nodes.map(n => n.type))));
  const info = await p.evaluate(() => { const S = __studio.S, a = S.project.player.attachments[0], an = Object.keys(PRIM.FBX.cache[S.tags.get('player.att.asset')].anims); return { an, id: a.id }; });
  console.log('anims', JSON.stringify(info.an));
  await p.evaluate(a => {
    const S = __studio.S, Sc = PRIM.Schema, pr = S.project, at = pr.player.attachments[0], k = 'player.' + at.id, ch = at.script.nodes.find(n => n.type === 'onCharge');
    const nm = a[0], set = (key, v) => S.tags.set(Sc.snodeKey(k, ch.id, key), v); set('chargeAnim', 'charge'); set('loopAnim', 'Charge-loop'); set('fireAnim', 'CHarged Shot'); set('chargeTime', 0.4);
    const f = Sc.addSNode(pr, k, at.script, 'fireProjectile', 300, 0, null, k + '.asset'); at.script.links.push({ a: ch.id, o: 'fire', b: f.id });
    const sy = Sc.addSNode(pr, k, at.script, 'say', 300, 80, { text: 'charged!' }, k + '.asset'); at.script.links.push({ a: ch.id, o: 'charged', b: sy.id });
    const sc2 = Sc.addSNode(pr, k, at.script, 'say', 300, 160, { text: 'cancelled' }, k + '.asset'); at.script.links.push({ a: ch.id, o: 'cancel', b: sc2.id });
    S.tags.set('player.att.projType', 'particle'); S.tags.set('player.att.projShape', 'sphere'); S.tags.set('player.att.projSize', 6); S.tags.set('player.att.projColor', '#33ccff'); S.tags.set('player.att.projSpeed', 300);
  }, info.an);
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(400);
  await p.click('button:has-text("Edit muzzle, arrow")').catch(async () => { await p.click('button:has-text("Player Editor")').catch(() => {}); });
  await p.waitForTimeout(500);
  console.log('emitter tools', await p.locator('.tbtn').allTextContents().then(a => a.filter(x => /emitter|Emitter/.test(x)).join(' | ')));
  const before = await p.evaluate(() => [__studio.S.tags.get('player.att.emitX'), __studio.S.tags.get('player.att.emitYaw')].join(','));
  await p.click('.tbtn:has-text("Set projectile emitter")'); const cb = await p.locator('canvas.v3cv').boundingBox(); await p.mouse.click(cb.x + cb.width * 0.4, cb.y + cb.height * 0.5); await p.waitForTimeout(300);
  await p.click('.tbtn:has-text("Aim emitter arrow")'); await p.mouse.move(cb.x + cb.width * 0.5, cb.y + cb.height * 0.2); await p.mouse.down(); await p.mouse.move(cb.x + cb.width * 0.5, cb.y + cb.height * 0.25, { steps: 3 }); await p.mouse.up(); await p.waitForTimeout(300);
  console.log('emitter before', before, 'after', await p.evaluate(() => ['emitX', 'emitY', 'emitZ', 'emitYaw', 'emitPitch'].map(k => __studio.S.tags.get('player.att.' + k)).join(',')));
  await p.screenshot({ path: out + '_emit.png' });
  await p.click('.tbtn:has-text("Player view")'); await p.waitForTimeout(400);
  await p.evaluate(() => { const T = __studio.S.tags; T.set('player.att.emitX', 0); T.set('player.att.emitY', 62); T.set('player.att.emitZ', -100); T.set('player.att.emitYaw', 0); T.set('player.att.emitPitch', 0); });
  console.log('projectile fields', await p.evaluate(() => /Particle shape/.test(document.body.innerText) && /Bigger when charged/.test(document.body.innerText)));
  await p.click('button:has-text("Walk map1")'); await p.waitForTimeout(1200);
  await p.evaluate(() => { const q = __studio.S.play; q.msg.style.display = 'none'; window.__said = []; q.sh.o.say = t => window.__said.push(t); q.yaw = 0; q.x = -60; q.look = () => {}; });
  await p.mouse.click(700, 400); await p.waitForTimeout(300);
  const snap = () => p.evaluate(() => { const q = __studio.S.play, a = q.atts[0]; return JSON.stringify([a.fire, a.loopAnim, +a.t.toFixed(2), (q.fx || []).length, +q.T.get('player.charge').toFixed(2)]); });
  await p.evaluate(() => { window.__log = []; const q = __studio.S.play; const o = q.sh.o.fire; q.sh.o.fire = k => { window.__log.push('FIRE ' + performance.now().toFixed(0)); return o(k); }; });
  await p.mouse.move(700, 400); await p.mouse.down();
  for (let i = 0; i < 8; i++) { await p.waitForTimeout(250); console.log('held', i, await snap()); }
  await p.mouse.up();
  for (let i = 0; i < 10; i++) { await p.waitForTimeout(250); console.log('after release', i, await snap()); }
  console.log('fire calls', await p.evaluate(() => JSON.stringify(window.__log)), 'said', await p.evaluate(() => window.__said.join(',')));
};
