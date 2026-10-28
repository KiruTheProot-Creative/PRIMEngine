// Interaction system: interactive object with two hitboxes, E key / aim + click / screen cursor modes, tag changes, lock condition, messages + "When a button is pressed".
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/cube_bin.fbx']); await p.waitForTimeout(1500);
  // object: interactive, two hitboxes stacked (Plus low, Power high); custom tags
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, T = S.tags, cube = pr.assets.find(a => /cube/.test(a.id)).id;
    const o = Sc.addProp(pr, { name: 'Panel', type: 'interactive', asset: cube }); T.setMany({ [o.id + '.scale']: 0.5, [o.id + '.showBoxes']: true });
    T.define('presses', { type: 'number', value: 0, min: 0, max: 99, step: 1, label: 'Presses', group: 'presses', custom: true }); T.define('door_power', { type: 'bool', value: false, label: 'Door power', group: 'door_power', custom: true });
    Sc.addHitbox(pr, o, { name: 'Plus', x: 0, y: 40, z: 0, w: 40, h: 30, d: 40, setTag: 'presses', setVal: '+1' });
    Sc.addHitbox(pr, o, { name: 'Power', x: 0, y: 100, z: 0, w: 40, h: 30, d: 40, setTag: 'door_power', setVal: 'toggle', message: 'power', needTag: 'presses', needOp: '>=', needVal: '2' });
    pr.maps[0].things = [{ id: 't1', prop: o.id, x: 3072 + 100, y: 3072, ang: 0 }]; pr.maps[0].n.t = 1;
    const pl = pr.player; pl.script = { nodes: [], links: [] }; const A = (type, x, y, over) => Sc.addSNode(pr, 'player', pl.script, type, x, y, over);
    let a = A('onMessage', 0, 0, { name: 'power' }), b = A('say', 250, 0, { text: 'power message, door_power={door_power}' }); pl.script.links.push({ a: a.id, o: 'next', b: b.id });
    a = A('onButton', 0, 150, { name: 'Plus' }); b = A('say', 250, 150, { text: 'plus pressed ({presses})' }); pl.script.links.push({ a: a.id, o: 'next', b: b.id });
    S.propSel = o.id; T.emit([]); window.__o = o.id; });
  await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(800);
  console.log('inspector:', await p.evaluate(() => { const t = document.querySelector('.insp').innerText; return ['Interaction', 'How it is used', 'Hitboxes (2)', 'When pressed', 'Lock (puzzles)', 'Fit to the model'].map(k => k + '=' + t.includes(k)).join(' '); }));
  await p.screenshot({ path: out + '_editor.png' });
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(300);
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.hud, null, { timeout: 8000 }); await p.waitForTimeout(800);
  // drive frames by hand (slow software GL)
  const go = (pitch, n, fn, yaw) => p.evaluate(([pitch, n, fn, yaw]) => { const q = __studio.S.play; q.msg.style.display = 'none'; q.body = null; q.x = 3072; q.y = 3072; q.yaw = yaw || 0; if (pitch !== null) q.pitch = pitch; window.__said = window.__said || []; q.o.say = t => __said.push(t);
    let t = q.last || performance.now(); for (let i = 0; i < n; i++) { t += 1000 / 60; q.frame(t); cancelAnimationFrame(q.raf); } q.raf = requestAnimationFrame(x => q.frame(x)); if (fn) (new Function('q', fn))(q);
    const T = q.T; return JSON.stringify({ presses: T.get('presses'), power: T.get('door_power'), hint: q.hintEl && q.hintEl.style.display !== 'none' ? q.hintEl.textContent : '', ui: !!q.ui, last: T.get('player.lastButton') }); }, [pitch, n, fn || '', yaw || 0]);
  const low = -Math.atan2(102 + 0 - 40, 100), high = -Math.atan2(102 - 100, 100);
  console.log('look at Plus', await go(low, 3));
  console.log('E on Plus', await go(low, 1, 'q.interact()'));
  console.log('look at Power, E (locked)', await go(high, 3, 'q.interact()'));
  await p.waitForTimeout(400); console.log('E on Plus again', await go(low, 1, 'q.interact()'));
  console.log('E on Power (unlocked)', await go(high, 1, 'q.interact()'));
  await p.waitForTimeout(200); console.log('said', await p.evaluate(() => JSON.stringify(__said)));
  await p.screenshot({ path: out + '_key.png', timeout: 90000 }).catch(() => console.log('(skip)'));
  // aim mode: left click presses, the weapon does not fire
  await p.evaluate(() => __studio.S.tags.set(window.__o + '.useMode', 'aim')); await p.waitForTimeout(400);
  console.log('aim: hint + click on Plus', await go(low, 2, 'window.__took = q.useClick()'), 'click taken:', await p.evaluate(() => window.__took));
  console.log('aim: click on nothing', await go(0.6, 1, 'window.__took = q.useClick()'), 'click taken:', await p.evaluate(() => window.__took));
  // cursor (screen) mode
  await p.evaluate(() => __studio.S.tags.set(window.__o + '.useMode', 'cursor')); await p.waitForTimeout(400);
  console.log('screen: looking at Plus', await go(low, 3));
  console.log('screen: cursor up onto Power and click', await go(null, 1, 'const y0 = q.ui && q.ui.y; q.look(0, -230); q.frame(q.last + 16); cancelAnimationFrame(q.raf); window.__hov = [y0, q.ui && Math.round(q.ui.y), q.ui && q.ui.hover && q.T.get(q.ui.hover.o.pre + "hb." + q.ui.hover.b.id + ".name"), +q.pitch.toFixed(2)]; q.useClick()'), 'cursor y before/after, hover, pitch (unchanged):', await p.evaluate(() => JSON.stringify(window.__hov)));
  await p.screenshot({ path: out + '_cursor.png', timeout: 90000 }).catch(() => console.log('(skip)'));
  console.log('screen: cursor off the object -> look again', await go(null, 1, 'q.look(900, 0)'), 'pitch now', await p.evaluate(() => +__studio.S.play.pitch.toFixed(2)));
  console.log('screen: still facing it, no re-grab', await go(null, 3));
  console.log('turn away and back -> grabbed again', await go(null, 3, '', 1.5), await go(low, 3));
};
