// Node conditions ("Only if…"): an event or action only happens while a tag matches; the charge weapon only charges when allowed.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles(SP + '/Arm_canon.fbx'); await p.waitForTimeout(1500);
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(500);
  await p.click('button:has-text("New weapon")'); await p.click('.pitem:has-text("Arm_canon")'); await p.waitForTimeout(1500);
  await p.evaluate(() => { const S = __studio.S, P = S.project, Sc = PRIM.Schema, a = P.player.attachments[0], key = 'player.' + a.id;
    P.tags['canChargeWeapon'] = { type: 'bool', value: false, label: 'Can charge the weapon', group: 'custom' };
    const k = Sc.addSNode(P, key, a.script, 'onKey', 20, 20, { key: 'f' }), sy = Sc.addSNode(P, key, a.script, 'say', 260, 20, { text: 'F pressed' }); a.script.links.push({ a: k.id, o: 'next', b: sy.id });
    const c = Sc.addSNode(P, key, a.script, 'onCharge', 20, 200, { key: 'mouse1', startDelay: 0, chargeTime: 0.5 }), s2 = Sc.addSNode(P, key, a.script, 'say', 260, 200, { text: 'charging' }); a.script.links.push({ a: c.id, o: 'start', b: s2.id });
    S.tags.setMany({ [Sc.snodeKey(key, k.id, 'needTag')]: 'canChargeWeapon', [Sc.snodeKey(key, k.id, 'needVal')]: 'YES', [Sc.snodeKey(key, c.id, 'needTag')]: 'canChargeWeapon', [Sc.snodeKey(key, c.id, 'needVal')]: 'yes' }); S.tags.emit([]); });
  await p.click('.tab:has-text("Node Coder")'); await p.waitForTimeout(500);
  console.log('lock badges', await p.evaluate(() => [...document.querySelectorAll('.nch')].map(e => e.textContent).join(' | ')));
  await p.click('.nch:has-text("When a key is pressed")'); await p.waitForTimeout(300); await p.screenshot({ path: out + '_insp.png' });
  console.log('condition foldout', await p.evaluate(() => /Only if/.test(document.querySelector('.insp').innerText)));
  await p.click('.tab:has-text("Map")'); await p.waitForTimeout(400); await p.click('button:has-text("Walk this map")'); await p.waitForTimeout(1200);
  const run = () => p.evaluate(async () => { const q = __studio.S.play; window.__said = []; q.sh.o.say = t => window.__said.push(t); q.press('f'); q.release('f'); q.press('mouse1'); for (let i = 0; i < 10; i++) q.hold(0.1); const ch = q.T.get('player.charge'); q.release('mouse1'); q.hold(0.1); await new Promise(r => setTimeout(r, 200)); return window.__said.join(',') + ' | charge ' + (+ch).toFixed(2); });
  console.log('tag off (no):', await run());
  await p.evaluate(() => __studio.S.play.T.set('canChargeWeapon', true));
  console.log('tag on (yes):', await run());
  await p.keyboard.press('Escape');
};
