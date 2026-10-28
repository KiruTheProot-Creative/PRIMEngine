// Splash damage shock sphere: grows to the splash radius and fades; gradient + animation modes. Frames stepped by hand.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/Arm_canon.fbx']); await p.waitForTimeout(1500);
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(400);
  await p.click('button:has-text("New weapon")'); await p.click('.pitem:has-text("Arm_canon")'); await p.waitForTimeout(1500);
  await p.evaluate(() => __studio.S.tags.setMany({ 'player.att.projType': 'particle', 'player.att.projSplash': true, 'player.att.splashRadius': 220, 'player.att.splashSphere': true, 'player.att.splashSphereTime': 1 }));
  await p.click('.insp :text-is("Material")').catch(() => {}); await p.click('.insp :text-is("Object")').catch(() => {}); await p.waitForTimeout(300);
  console.log('fields:', ['Shock sphere (grows', 'middle color', 'edge color', 'animated gradient', 'animation speed', 'brightness'].map(k => k + '=' + 0).length, await p.evaluate(() => ['Shock sphere (grows', 'middle color', 'edge color', 'animated gradient', 'animation speed', 'brightness (above'].map(k => document.querySelector('.insp').innerText.includes(k)).join(',')));
  await p.click('button:has-text("Player view")').catch(() => {}); await p.waitForTimeout(300);
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.hud, null, { timeout: 8000 }); await p.waitForTimeout(800);
  const step = (n, boom) => p.evaluate(([n, boom]) => { const q = __studio.S.play; q.msg.style.display = 'none'; q.look = () => {}; q.body = null; q.x = 3072 - 300; q.y = 3072; q.yaw = 0; q.pitch = 0;
    if (boom) q.explode('player.att.', [3072 + 150, 60, 3072], [-1, 0, 0]); let t = q.last || performance.now(); for (let i = 0; i < n; i++) { t += 1000 / 60; q.frame(t); cancelAnimationFrame(q.raf); } q.raf = requestAnimationFrame(x => q.frame(x));
    const f = (q.fx || []).find(f => f.kind === 'shock'); return f ? JSON.stringify({ t: +f.t.toFixed(2), radius: Math.round(f.R * (1 - Math.pow(1 - Math.min(1, f.t / f.life), 3))), anim: f.anim }) : 'gone'; }, [n, boom]);
  console.log('just after', await step(6, true));
  console.log('at 0.25 s', await step(9)); await p.screenshot({ path: out + '_a.png', timeout: 90000 }).catch(() => console.log('(skip)'));
  console.log('at 0.6 s', await step(21)); await p.screenshot({ path: out + '_b.png', timeout: 90000 }).catch(() => console.log('(skip)'));
  console.log('at 1.1 s', await step(30));
  await p.evaluate(() => __studio.S.tags.setMany({ 'player.att.splashAnim': 'swirl', 'player.att.splashColor1': '#7affff', 'player.att.splashColor2': '#3a3aff' }));
  console.log('swirl', await step(15, true)); await p.screenshot({ path: out + '_swirl.png', timeout: 90000 }).catch(() => console.log('(skip)'));
};
