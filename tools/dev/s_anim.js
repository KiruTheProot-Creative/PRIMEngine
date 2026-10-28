const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles(SP + '/Arm_canon.fbx'); await p.waitForTimeout(1500);
  await p.click('.tab:has-text("Animation Editor")'); await p.waitForTimeout(800);
  await p.click('button:has-text("New animation")'); await p.waitForTimeout(200);
  console.log('menu', await p.evaluate(() => [...document.querySelectorAll('.pitem')].map(e => e.textContent).join(' | ')));
  await p.click('.pitem:has-text("Empty animation")'); await p.waitForTimeout(800);
  const info = () => p.evaluate(() => { const S = __studio.S, a = S.project.anims[0], mo = PRIM.FBX.cache[a.asset]; return JSON.stringify({ name: a.name, keys: a.keys.length, node: S.aed && S.aed.st.node, inModel: !!mo.anims[a.name], dur: a.dur }); });
  console.log('created', await info());
  // key the part at 0, then turn it at 0.5 s with the inspector
  await p.click('button:has-text("Key part")'); await p.waitForTimeout(200);
  await p.evaluate(() => __studio.S.aed.setTime(0.5)); await p.waitForTimeout(200);
  const turn = p.locator('.insp .fld', { hasText: 'Turn Y' }).locator('input'); await turn.fill('90'); await turn.press('Enter'); await p.waitForTimeout(300);
  console.log('after keys', await info(), await p.evaluate(() => JSON.stringify(__studio.S.project.anims[0].keys.map(k => [k.node, k.t, k.r]))));
  // drag in the viewport with the rotate tool
  const box = await p.evaluate(() => { const r = __studio.S.aed.cv.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; });
  await p.evaluate(() => { __studio.S.aed.setTime(1); __studio.S.aed.root.focus(); }); await p.keyboard.press('r'); await p.mouse.move(box.x, box.y); await p.mouse.down(); await p.mouse.move(box.x + 60, box.y, { steps: 6 }); await p.mouse.up(); await p.waitForTimeout(200);
  console.log('after drag rotate', await p.evaluate(() => JSON.stringify(__studio.S.project.anims[0].keys.map(k => [k.t, k.r.map(Math.round)]))));
  // pose in the middle is interpolated
  console.log('pose at 0.25 differs from 0', await p.evaluate(() => { const a = __studio.S.project.anims[0], mo = PRIM.FBX.cache[a.asset], P0 = PRIM.FBX.pose(mo, a.name, 0), P1 = PRIM.FBX.pose(mo, a.name, 0.25); return P0.some((m, i) => m.some((v, j) => Math.abs(v - P1[i][j]) > 1e-4)); }));
  await p.keyboard.press('Control+z'); await p.waitForTimeout(100); console.log('after undo', await p.evaluate(() => __studio.S.project.anims[0].keys.length));
  await p.screenshot({ path: out + '_ae.png' });
  // the animation is offered in other animation lists (a weapon's idle animation)
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(400); await p.click('button:has-text("New weapon")'); await p.click('.pitem:has-text("Arm_canon")'); await p.waitForTimeout(1200);
  console.log('offered in weapon anim list', await p.evaluate(() => [...document.querySelectorAll('.insp select option')].some(o => o.textContent === 'New animation')));
};
