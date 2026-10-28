// Animation blending: a weapon switching from IDLE to "shot" eases between them (no jump), and blend 0 is instant.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles(SP + '/Arm_canon.fbx'); await p.waitForTimeout(1500);
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(600);
  await p.click('button:has-text("New weapon")'); await p.click('.pitem:has-text("Arm_canon")'); await p.waitForTimeout(1500);
  console.log('blend setting', await p.evaluate(() => __studio.S.tags.get('player.animBlend')));
  await p.click('.tab:has-text("Map")'); await p.waitForTimeout(300); await p.click('button:has-text("Walk this map")'); await p.waitForTimeout(1000);
  const run = blend => p.evaluate(blend => { const q = __studio.S.play; cancelAnimationFrame(q.raf); q.T.set('player.animBlend', blend); const at = q.atts[0], v = q.view, dt = 1 / 60, out = [];
    const tip = () => { const pose = at.mLast ? PRIM.FBX.world(at.model, at.mLast) : null; return pose ? pose.reduce((s, m) => s + Math.abs(m[12]) + Math.abs(m[13]) + Math.abs(m[14]), 0) : 0; };
    for (let i = 0; i < 20; i++) q.drawHand(v, dt); let last = tip(); q.playAnim('player.' + at.id, 'shot', false);
    for (let i = 0; i < 20; i++) { q.drawHand(v, dt); const now = tip(); out.push(+(now - last).toFixed(2)); last = now; } return out; }, blend);
  const a = await run(0.15), b = await run(0);
  console.log('per-frame pose change, blend 0.15 s:', a.slice(0, 12).join(' '), ' biggest', Math.max(...a.map(Math.abs)).toFixed(2));
  console.log('per-frame pose change, instant   :', b.slice(0, 12).join(' '), ' biggest', Math.max(...b.map(Math.abs)).toFixed(2));
  await p.keyboard.press('Escape');
};
