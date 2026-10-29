// Save stations and save games: what is saved (place, tags, keys, opened doors, taken pick-ups), dying -> back to the save, menu Continue / Load slot. Frames stepped by hand.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/Arm_canon.fbx']); await p.waitForTimeout(2000);
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, m = pr.maps[0], arm = pr.assets.find(a => /arm/i.test(a.id + a.name)).id, C = c => (c + 0.5) * 128;
    for (let i = 1; i <= 3; i++) PRIM.Save.erase(pr.id, i);
    const key = Sc.addProp(pr, { name: 'Blue key', type: 'key', asset: arm, scale: 0.3, keyName: 'Blue key', keyColor: '#3a8aff' }); Sc.ensureKeys(pr);
    const door = Sc.addProp(pr, { name: 'Blue door', type: 'door', asset: arm, lockKey: key.id, openMode: 'interact', autoClose: false });
    const st = Sc.addProp(pr, { name: 'Save pod', type: 'save', saveAsk: false, saveName: 'Landing site' });
    pr.tags.upgrades = { type: 'number', value: 0, label: 'Upgrades found', group: 'custom', custom: true };
    S.tags.set('player.deathAction', 'save'); S.tags.set('player.deathDelay', 0.1);
    m.things = [{ id: 'k', prop: key.id, x: C(21), y: C(26), ang: 0, layer: 0 }, { id: 'd', prop: door.id, x: C(22), y: C(21), ang: 0, layer: 0 }, { id: 's', prop: st.id, x: C(26), y: C(26), ang: 0, layer: 0 }]; m.n.t = 9;
    window.__ids = { key: key.id }; S.tags.emit([]); });
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(300); await p.evaluate(() => { __studio.S.objView = false; }); await p.click('.tab:has-text("Timeline")'); await p.waitForTimeout(200); await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(400);
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.things && __studio.S.play.things.length >= 3, null, { timeout: 15000 }); await p.waitForTimeout(500);
  const C = c => (c + 0.5) * 128;
  const go = (n, fn, px, py, yaw) => p.evaluate(([n, fn, px, py, yaw]) => { const q = __studio.S.play; q.msg.style.display = 'none'; if (px !== null) { q.x = px; q.y = py; q.body = null; q.z = 0; } if (yaw !== null) { q.yaw = yaw; q.pitch = 0; }
    let t = q.last || performance.now(); for (let i = 0; i < n; i++) { t += 16; q.frame(t); cancelAnimationFrame(q.raf); } q.raf = requestAnimationFrame(z => q.frame(z)); const r = fn ? (new Function('q', fn))(q) : null; const T = q.T, th = id => q.things.find(o => o.th.id === id);
    return JSON.stringify({ r, at: [Math.round(q.x / 128 - 0.5), Math.round(q.y / 128 - 0.5)], hp: T.get('player.health'), key: T.get(window.__ids.key + '.have'), keyTaken: !!(th('k') && th('k').taken), door: th('d') && { st: th('d').st, unlocked: !!th('d').unlocked }, upgrades: T.get('upgrades'), hint: q.hintEl && q.hintEl.style.display !== 'none' ? q.hintEl.textContent : '', toast: q.toastEl && q.toastEl.style.opacity !== '0' ? q.toastEl.textContent : '' }); }, [n, fn || '', px === undefined ? null : px, py === undefined ? null : py, yaw === undefined ? null : yaw]);
  console.log('take the key:', await go(10, null, C(21), C(26)));
  console.log('open the door:', await go(30, 'q.interact(); return null;', C(22), C(21) + 70, -Math.PI / 2));
  console.log('at the station (hint):', await go(5, 'q.T.set("upgrades", 2); q.T.set("player.health", 40); return null;', C(26), C(26)));
  console.log('press E:', await go(3, 'q.interact(); return JSON.stringify(PRIM.Save.read(q.project.id, 1)).slice(0, 260);'));
  console.log('walk away + change things:', await go(5, 'q.T.set("upgrades", 7); return null;', C(23), C(23)));
  console.log('die -> back to the save:', await go(20, 'q.T.set("player.health", 0); return null;'), await go(30));
  // menus: Continue loads the latest save, Load slot 2 does nothing (empty), Start new game uses its slot
  console.log('menus:', await p.evaluate(() => { const S = __studio.S, pr = S.project, sc = pr.screens.find(s => s.id === 'main'), btn = sc.nodes.find(n => n.type === 'button'), T = S.tags.fork(), host = document.createElement('div'); document.body.append(host); const got = [];
    const rt = new PRIM.Runtime({ host, project: pr, tags: T, mode: 'play', urls: S.urls, screenId: 'main', onStartGame: (r, sv) => got.push(sv ? (sv.load ? 'load slot ' + sv.slot + ' at ' + sv.load.name : 'new game in slot ' + sv.slot) : 'none') });
    const k = 'main.' + btn.id + '.'; for (const [a, slot] of [['continue', 1], ['load', 2], ['start', 3]]) { T.set(k + 'action', a); T.set(k + 'slot', slot); rt.act(sc, btn); }
    const info = T.get('save.info1'); rt.destroy(); host.remove(); return JSON.stringify({ got, info1: info, any: T.get('save.any'), info2: T.get('save.info2') }); }));
  // Player inspector shows the slots
  await p.evaluate(() => { const q = __studio.S.play; if (q) q.leave(); }); await p.waitForTimeout(500);
  console.log('inspector:', await p.evaluate(() => [...document.querySelectorAll('.insp .fold')].map(f => f.innerText.replace(/\s+/g, ' ')).filter(t => /Save games/.test(t)).join('').slice(0, 260)));
};
