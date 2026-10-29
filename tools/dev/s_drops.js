// Soft (blurred) sprites blend instead of being cut hard; "glow" sprites add light. Enemy drops (chance per row); self-destruct drops nothing.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/Arm_canon.fbx', SP + '/orb_blue.png', SP + '/orb_red.png', SP + '/orb_yellow.png']); await p.waitForTimeout(2500);
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, m = pr.maps[0], A = n => pr.assets.find(a => a.name.includes(n)).id, arm = A('Arm'), C = c => (c + 0.5) * 128;
    const orb = (n, col, blend) => Sc.addProp(pr, { name: n + ' orb', type: 'pickup', look: 'sprite', spriteIdle: A(col), spriteHeight: 40, spriteBlend: blend, giveTag: 'player.health', giveAmount: 5 }).id;
    const blue = orb('Blue', 'blue', 'normal'), red = orb('Red', 'red', 'glow'), yel = orb('Yellow', 'yellow', 'normal');
    const en = Sc.addProp(pr, { name: 'Grunt', type: 'enemy', asset: arm, scale: 0.4, health: 10, sight: 10, speed: 0, dropSpread: 60 }); Sc.addDrop(pr, en, { what: blue, chance: 100, count: 2 }); Sc.addDrop(pr, en, { what: red, chance: 0 }); Sc.addDrop(pr, en, { what: yel, chance: 100 });
    const bomb = Sc.addProp(pr, { name: 'Bomber', type: 'enemy', asset: arm, scale: 0.4, behavior: 'stationary', mount: 'ceiling', ceilAction: 'selfdestruct', fuse: 0.1, coneAngle: 60, blastDamage: 0 }); Sc.addDrop(pr, bomb, { what: blue, chance: 100, count: 3 });
    window.__ids = { blue, red, yel, en: en.id, bomb: bomb.id };
    m.things = [{ id: 'o1', prop: blue, x: C(23) - 50, y: C(22), ang: 0, layer: 0 }, { id: 'o2', prop: red, x: C(23), y: C(22), ang: 0, layer: 0 }, { id: 'o3', prop: yel, x: C(23) + 50, y: C(22), ang: 0, layer: 0 },
      { id: 'e1', prop: en.id, x: C(26), y: C(26), ang: 0, layer: 0 }, { id: 'e2', prop: bomb.id, x: C(21), y: C(26), ang: 0, layer: 0 }]; m.n.t = 9; S.tags.emit([]); });
  await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(300); await p.evaluate(() => { __studio.S.propSel = window.__ids.en; }); await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(400);
  console.log('drops UI:', await p.evaluate(() => [...document.querySelectorAll('.insp .fold')].map(f => f.innerText.replace(/\s+/g, ' ')).filter(t => /drops/.test(t)).map(t => t.slice(0, 300)).join('')));
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(300);
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.things && __studio.S.play.things.length >= 5, null, { timeout: 15000 }); await p.waitForTimeout(800);
  const go = (n, fn) => p.evaluate(([n, fn]) => { const q = __studio.S.play; q.msg.style.display = 'none'; let t = q.last || performance.now(); for (let i = 0; i < n; i++) { t += 16; q.frame(t); cancelAnimationFrame(q.raf); } q.raf = requestAnimationFrame(z => q.frame(z)); const r = fn ? (new Function('q', fn))(q) : null; return JSON.stringify(r); }, [n, fn || '']);
  // look at the three orbs from close by
  await go(30, 'q.x = (23.5) * 128; q.y = (22.5) * 128 + 110; q.body = null; q.yaw = -Math.PI / 2; q.pitch = -0.12;');
  const shot = await p.evaluate(() => { const q = __studio.S.play; cancelAnimationFrame(q.raf); q.x = 23.5 * 128; q.y = 22.5 * 128 + 110; q.yaw = -Math.PI / 2; q.pitch = -0.12; q.body = null; q.frame(q.last + 16); cancelAnimationFrame(q.raf); const cv = q.view.gl.canvas, u = cv.toDataURL('image/png'); q.raf = requestAnimationFrame(z => q.frame(z)); return u; });
  require('fs').writeFileSync(out + '_orbs.png', Buffer.from(shot.split(',')[1], 'base64'));
  console.log('kill the grunt:', await go(5, 'const o = q.things.find(x => x.th.id === "e1"); q.damageEnemy(o, 99); return q.things.filter(x => x.th.spawned).map(x => x.th.prop + "@" + Math.round(Math.hypot(x.th.x - o.x, x.th.y - o.y)));'));
  console.log('bomber blows itself up:', await go(2, 'const o = q.things.find(x => x.th.id === "e2"); o.st = "fuse"; o.fuseT = 0.05; return null;'), await go(20, 'const o = q.things.find(x => x.th.id === "e2"); return { st: o.st, drops: q.things.filter(x => x.th.spawned && x.th.id.startsWith("e2")).length };'));
};
