// Elevators (stops list, E menu, carrying the player) and the Metroid-style room reset when a door closes behind you. Frames stepped by hand.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/Arm_canon.fbx']); await p.waitForTimeout(2500);
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, T = S.tags, G = PRIM.Grid, arm = pr.assets.find(a => /arm/i.test(a.id + a.name)).id, m = pr.maps[0];
    m.layers = [{ cells: {} }, { cells: {} }]; m.things = []; m.zones = []; m.paths = [];
    for (let y = 0; y <= 4; y++) for (let x = 0; x <= 12; x++) { const c = G.edit(m, 0, x, y); if (x === 0 || y === 0 || x === 12 || y === 4 || (x === 5 && y !== 2)) c.w = ''; else c.f = ''; }
    for (let y = 1; y <= 3; y++) for (let x = 7; x <= 11; x++) if (!(x === 9 && y === 2)) G.edit(m, 1, x, y).f = '';
    const en = Sc.addProp(pr, { name: 'Bug', type: 'enemy', asset: arm, height: 40, radius: 14, speed: 0, sight: 10, attack: 'melee', attackRange: 10, damage: 0 }).id;
    const dr = Sc.addProp(pr, { name: 'Gate', type: 'door', asset: arm, openMode: 'interact', autoClose: true, closeAfter: 0.5 }).id;
    const el = Sc.addProp(pr, { name: 'Lift', type: 'elevator', elevW: 1, elevD: 1, elevSpeed: 256 }).id;
    m.things.push({ id: 'bug', prop: en, x: 2.5 * 128, y: 2.5 * 128, ang: 0, layer: 0 }, { id: 'gate', prop: dr, x: 5.5 * 128, y: 2.5 * 128, ang: 90, layer: 0 },
      { id: 'lift', prop: el, x: 9.5 * 128, y: 2.5 * 128, ang: 0, layer: 0, stops: [{ s: 0, h: 0, name: 'Bottom' }, { s: 1, h: 0, name: 'Top' }] });
    T.set(m.id + '.startX', 8.5 * 128); T.set(m.id + '.startY', 2.5 * 128); S.tags.emit([]); });
  // map editor: the elevator shows on every story, its stops list in the inspector, a basement story under story 1
  await p.click('.tab:has-text("Map")'); await p.waitForTimeout(400);
  console.log('editor:', await p.evaluate(() => { const S = __studio.S, me = S.med; me.setLayer(1); const onTop = me.things().map(t => t.id); me.sel = { kind: 'thing', id: 'lift' }; me.emitSel(); return JSON.stringify({ onTop }); }));
  await p.waitForTimeout(300);
  console.log('inspector:', await p.evaluate(() => [...document.querySelectorAll('.insp .fold')].filter(f => /Elevator stops/.test(f.innerText)).map(f => f.innerText.replace(/\s+/g, ' ').slice(0, 200)).join('')));
  await p.screenshot({ path: out + '_map.png' });
  console.log('basement:', await p.evaluate(() => { const S = __studio.S, me = S.med, m = S.project.maps[0]; me.setLayer(0); me.setLayer(-1); const a = { base: m.base, L: me.s.L, layers: m.layers.length, lift: m.things.find(t => t.id === 'lift'), start: S.tags.get(m.id + '.startLayer'), chip: document.querySelector('.medbar .chip, .chip') && document.querySelector('.chip').innerText };
    me.history(true); a.undoBase = m.base; a.undoLayers = m.layers.length; return JSON.stringify(a); }));
  await p.evaluate(() => __studio.S.med.setLayer(0));
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(300);
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.things && __studio.S.play.things.length >= 3, null, { timeout: 15000 }); await p.waitForTimeout(400);
  const go = (n, fn) => p.evaluate(([n, fn]) => { const q = __studio.S.play; q.msg.style.display = 'none'; let t = q.last || performance.now(); for (let i = 0; i < n; i++) { t += 16; q.frame(t); cancelAnimationFrame(q.raf); } q.raf = requestAnimationFrame(z => q.frame(z));
    const r = fn ? (new Function('q', fn))(q) : null, L = q.things.find(o => o.type === 'elevator'), B = q.things.find(o => o.th.id === 'bug');
    return JSON.stringify({ r, z: Math.round(q.z), lift: L && L.el ? Math.round(L.el.y) : null, bug: B && { st: B.st, hp: B.hp, x: Math.round(B.x) }, resets: q.roomResets || 0 }); }, [n, fn || '']);
  // elevator: stand on it, E opens the stops menu, pick "Top"
  console.log('on lift:', await go(10, 'q.x = 9.5 * 128; q.y = 2.5 * 128; q.body = null; return !!q.elevOn();'));
  console.log('menu:', await go(2, 'q.interact(); return [...document.querySelectorAll(".p3talk button, .p3talk .pick, .p3talk li")].map(b => b.innerText.trim()).join(" | ") || document.querySelector(".p3talk") && document.querySelector(".p3talk").innerText.replace(/\\s+/g, " ");'));
  console.log('pick Top:', await go(1, 'q.talkKey("2"); return q.talk ? "menu still open" : "closed";'));
  console.log('riding 0.3 s:', await go(18));
  console.log('arrived:', await go(80, 'return q.elevOn() ? "still on" : "off";'));
  await p.screenshot({ path: out + '_top.png' });
  console.log('down again:', await go(90, 'const L = q.things.find(o => o.type === "elevator"); q.elevGo(L, 0); return "go";'));
  // room reset: kill the bug in the west room, stand east of the gate, open + let it close
  console.log('kill bug:', await go(2, 'const B = q.things.find(o => o.th.id === "bug"); q.enemyStep(B, 0.016); q.damageEnemy(B, 9999); return B.st;'));
  console.log('gate cycle:', await go(120, 'q.x = 7.5 * 128; q.y = 2.5 * 128; q.z = 0; q.body = null; const D = q.things.find(o => o.type === "door"); q.openDoor(D, true); return D.st;'));
  console.log('after close:', await go(200, 'const D = q.things.find(o => o.type === "door"); return D.st + " " + JSON.stringify(q.lastRoomReset);'));
  console.log('bug sync:', await go(5));
};
