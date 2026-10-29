// Custom visors: remove every preset, add a named Custom visor with its own abilities (scan + see through walls), duplicate / order / remove clean-up.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/Arm_canon.fbx']); await p.waitForTimeout(2000);
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, T = S.tags, m = pr.maps[0], arm = pr.assets.find(a => /arm/i.test(a.name)).id, C = c => (c + 0.5) * 128;
    T.set('player.visorsOn', true); Sc.ensureVisors(pr);
    const en = Sc.addProp(pr, { name: 'Bug', type: 'enemy', asset: arm, scannable: true, scanTitle: 'Bug', scanText: 'A bug.', sight: 10, speed: 0 }); Sc.ensureVisors(pr); Sc.ensureCodex(pr);
    const sx = Math.floor(+T.get(m.id + '.startX') / 128), sy = Math.floor(+T.get(m.id + '.startY') / 128); m.things = [{ id: 'e1', prop: en.id, x: C(sx + 3), y: C(sy), ang: 180, layer: 0 }]; m.n.t = 9; window.__en = en.id; T.emit([]); });
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(400); await p.evaluate(() => { __studio.S.objView = false; }); await p.click('.tab:has-text("Timeline")'); await p.waitForTimeout(200); await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(400);
  const fold = () => p.evaluate(() => [...document.querySelectorAll('.insp .fold')].filter(f => /Visor list/.test(f.innerText)).map(f => f.innerText.replace(/\s+/g, ' ').slice(0, 260)).join(''));
  // remove all four presets (confirm dialogs accepted)
  p.on('dialog', d => d.accept());
  for (let i = 0; i < 4; i++) { await p.click('.insp button:has-text("Remove")'); await p.waitForTimeout(150); }
  console.log('empty:', await fold());
  await p.click('.insp button:has-text("+ Add visor")'); await p.click('.pitem:has-text("Custom")'); await p.waitForTimeout(300);
  console.log('name focused:', await p.evaluate(() => document.activeElement && document.activeElement.value));
  await p.keyboard.type('Thermal'); await p.keyboard.press('Enter'); await p.waitForTimeout(200);
  console.log('custom fields:', await fold());
  await p.evaluate(() => { const S = __studio.S, T = S.tags, v = S.project.visors[0]; T.set(v.id + '.seeThrough', true); T.set(v.id + '.canScan', true); });
  await p.waitForTimeout(200); console.log('with abilities:', (await fold()).slice(0, 260));
  await p.click('.insp button:has-text("Duplicate")'); await p.waitForTimeout(200); await p.click('.insp button:has-text("◀")'); await p.waitForTimeout(200);
  console.log('order:', await p.evaluate(() => __studio.S.project.visors.map(v => __studio.S.tags.get(v.id + '.name')).join(', ')));
  console.log('seenIn cleanup:', await p.evaluate(() => { const S = __studio.S, T = S.tags, pr = S.project, c = pr.visors[0]; T.set(window.__en + '.seenIn', c.id); PRIM.Schema.removeVisor(pr, c); return T.get(window.__en + '.seenIn') + ' / start ' + T.get('player.visorStart') + ' / left ' + pr.visors.length; }));
  await p.screenshot({ path: out + '_insp.png' });
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.things && __studio.S.play.things.length >= 1, null, { timeout: 15000 }); await p.waitForTimeout(500);
  const go = (n, fn) => p.evaluate(([n, fn]) => { const q = __studio.S.play; q.msg.style.display = 'none'; const r0 = fn ? (new Function('q', fn))(q) : null; let t = q.last || performance.now(); for (let i = 0; i < n; i++) { t += 16; q.frame(t); cancelAnimationFrame(q.raf); } q.raf = requestAnimationFrame(z => q.frame(z));
    const T = q.T, vi = q.visorItems(); return JSON.stringify({ r0, visor: T.get('player.visorName'), type: T.get('player.visorType'), canScan: q.vCan('scan'), xray: q.vCan('xray'), items: vi && vi.items.map(i => i.xray), codex: T.get('codex.' + window.__en), popup: !!q.popup }); }, [n, fn || '']);
  console.log('play:', await go(3));
  console.log('scan with the custom visor:', await go(110, `const e = q.things.find(o => o.th.id === 'e1'); q.yaw = Math.atan2(e.fr.cy - q.y, e.fr.cx - q.x) + 0.05; q.pitch = 0; q.press('mouse2'); return !!(q.scan && q.scan.o);`));
};
