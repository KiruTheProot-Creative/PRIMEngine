// Zones (trigger + custom with a script) and node dialogues (Dialogue Editor, NPC with a dialogue, a zone starting one).
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles(['/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad/cube_bin.fbx']); await p.waitForTimeout(1500);
  // zones drawn with the Zone tool in the map editor
  await p.click('.mptab:has-text("Zones")'); await p.waitForTimeout(200);
  console.log('zones panel:', (await p.locator('.medpal').innerText()).replace(/\s+/g, ' ').slice(0, 120));
  await p.click('.mpiece:has-text("Trigger zone")'); await p.waitForTimeout(200);
  const cellPx = (cx, cy) => p.evaluate(([cx, cy]) => { const m = __studio.S.med, r = m.cv.getBoundingClientRect(); return [r.left + m.sx((cx + 0.5) * 128), r.top + m.sy((cy + 0.5) * 128)]; }, [cx, cy]);
  const drag = async (a, b) => { const A = await cellPx(...a), B = await cellPx(...b); await p.mouse.move(A[0], A[1]); await p.mouse.down(); await p.mouse.move((A[0] + B[0]) / 2, (A[1] + B[1]) / 2); await p.mouse.move(B[0], B[1]); await p.mouse.up(); await p.waitForTimeout(250); };
  await drag([21, 21], [22, 22]);
  await p.click('.mpiece:has-text("Custom zone")'); await p.waitForTimeout(200);
  await drag([26, 23], [27, 25]);
  console.log('zones:', await p.evaluate(() => JSON.stringify(__studio.S.project.maps[0].zones.map(z => [z.id, z.type, z.x0, z.y0, z.x1, z.y1]))), '| inspector:', (await p.locator('.insp').innerText()).split('\n').slice(0, 3).join(' / '));
  await p.screenshot({ path: out + '_map.png' });
  // set them up (trigger: tag + text + message; custom: script)
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, T = S.tags, m = pr.maps[0], [a, b] = m.zones;
    T.define('entered', { type: 'bool', value: false, label: 'Entered', group: 'entered', custom: true }); T.define('zcount', { type: 'number', value: 0, min: 0, max: 99, step: 1, label: 'Zone visits', group: 'zcount', custom: true });
    T.setMany({ [m.id + '.z.' + a.id + '.setTag']: 'entered', [m.id + '.z.' + a.id + '.setVal']: 'true', [m.id + '.z.' + a.id + '.say']: 'Welcome ({zcount} visits)', [m.id + '.z.' + a.id + '.message']: 'hello' });
    const K = m.id + '.z.' + b.id, A = (t, x, y, o) => Sc.addSNode(pr, K, b.script, t, x, y, o), lk = (x, y) => b.script.links.push({ a: x.id, o: 'next', b: y.id });
    let e = A('onEnter', 0, 0), c = A('changeTag', 250, 0, { tag: 'zcount', amount: 1 }); lk(e, c); e = A('onLeave', 0, 150); c = A('say', 250, 150, { text: 'left the custom zone' }); lk(e, c);
    e = A('onInside', 0, 300, { interval: 0.25 }); c = A('changeTag', 250, 300, { tag: 'zcount', amount: 10 }); lk(e, c);
    const pl = pr.player; pl.script = { nodes: [], links: [] }; e = Sc.addSNode(pr, 'player', pl.script, 'onMessage', 0, 0, { name: 'hello' }); c = Sc.addSNode(pr, 'player', pl.script, 'say', 250, 0, { text: 'got message hello' }); pl.script.links.push({ a: e.id, o: 'next', b: c.id });
    T.emit([]); });
  // Dialogue Editor
  await p.click('.tab:has-text("Dialogue Editor")'); await p.waitForTimeout(400);
  await p.click('.empty-c button:has-text("+ New dialogue")'); await p.waitForTimeout(500);
  console.log('dialogue nodes:', (await p.locator('.ncn .nch').allTextContents()).join(' | '), '| choice outs:', (await p.locator('.ncn.dialogue .nco span').allTextContents()).filter(Boolean).join(' | '));
  console.log('add menu has Dialogue:', await p.evaluate(() => JSON.stringify(__studio.S.coderMenu.map(m => m.t + '(' + m.sub.length + ')'))));
  await p.click('.ncn:has-text("Say a line") >> nth=0'); await p.waitForTimeout(200);
  console.log('line inspector:', (await p.locator('.insp').innerText()).replace(/\s+/g, ' ').slice(0, 220));
  await p.screenshot({ path: out + '_dlg.png' });
  // an NPC that uses it, right in front of the start
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, d = pr.dialogues[0]; const o = Sc.addProp(pr, { name: 'Guard', type: 'npc', asset: pr.assets.find(a => /cube/.test(a.id)).id }); S.tags.set(o.id + '.scale', 0.4); S.tags.set(o.id + '.npcDialogue', d.id); pr.maps[0].things.push({ id: 't9', prop: o.id, x: 3072, y: 3072 + 120, ang: 180, layer: 0 }); window.__npc = o.id; S.tags.emit([]); });
  await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(500);
  console.log('npc inspector:', (await p.locator('.insp .fold:has-text("Dialogue (node graph)")').innerText()).replace(/\s+/g, ' ').slice(0, 200));
  // play (frames by hand)
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(300);
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.hud, null, { timeout: 8000 }); await p.waitForTimeout(500);
  const go = (x, y, n, fn) => p.evaluate(([x, y, n, fn]) => { const q = __studio.S.play; q.msg.style.display = 'none'; window.__said = window.__said || []; q.o.say = t => __said.push(t); if (x !== null) { q.x = x; q.y = y; q.body = null; }
    let t = q.last || performance.now(); for (let i = 0; i < n; i++) { t += 16; q.frame(t); cancelAnimationFrame(q.raf); } q.raf = requestAnimationFrame(z => q.frame(z)); if (fn) (new Function('q', fn))(q);
    const T = q.T; return JSON.stringify({ entered: T.get('entered'), zcount: T.get('zcount'), toast: q.toastEl ? q.toastEl.textContent : '', talk: q.talk ? (q.talkEl.innerText || '').replace(/\s+/g, ' ').slice(0, 90) : null }); }, [x, y, n, fn || '']);
  console.log('into the trigger zone:', await go(21.5 * 128, 21.5 * 128, 3));
  await p.waitForTimeout(100); console.log('said:', await p.evaluate(() => JSON.stringify(__said)));
  console.log('into the custom zone:', await go(26.5 * 128, 24.5 * 128, 3));
  console.log('stay inside 0.6 s:', await go(null, null, 38));
  console.log('leave it:', await go(29.5 * 128, 20.5 * 128, 3)); await p.waitForTimeout(100); console.log('said:', await p.evaluate(() => JSON.stringify(__said)));
  // talk to the NPC: line, Continue, choice 1, line, end
  console.log('talk:', await go(null, null, 1, 'const o = q.things.find(t => t.th.prop === window.__npc); q.talkStart(o)')); await p.waitForTimeout(100);
  console.log('line 1:', await go(null, null, 1));
  await p.screenshot({ path: out + '_talk.png', timeout: 90000 }).catch(() => console.log('(skip)'));
  await go(null, null, 1, 'q.talkKey("e")'); await p.waitForTimeout(100); console.log('question:', await go(null, null, 1));
  await go(null, null, 1, 'q.talkKey("1")'); await p.waitForTimeout(100); console.log('answer 1:', await go(null, null, 1));
  await go(null, null, 1, 'q.talkKey("space")'); await p.waitForTimeout(100); console.log('end:', await go(null, null, 1));
  // a trigger zone that starts the dialogue; answer 2 ends it at once
  await p.evaluate(() => { const S = __studio.S, m = S.project.maps[0], z = m.zones[0]; S.tags.set(m.id + '.z.' + z.id + '.dialog', S.project.dialogues[0].id); });
  console.log('re-enter trigger zone:', await go(29.5 * 128, 20.5 * 128, 2), await go(22.5 * 128, 22.5 * 128, 2)); await p.waitForTimeout(100);
  await go(null, null, 1, 'q.talkKey("enter")'); await p.waitForTimeout(100); await go(null, null, 1, 'q.talkKey("2")'); await p.waitForTimeout(100); console.log('answer 2 -> ', await go(null, null, 1));
  // Esc during a talk opens the pause menu and keeps the talk
  console.log('pause during talk:', await go(null, null, 1, 'const o = q.things.find(t => t.th.prop === window.__npc); q.talkStart(o); q.pauseOpen()'), await p.evaluate(() => !!__studio.S.play.paused), await go(null, null, 1, 'q.pauseClose()'));
};
