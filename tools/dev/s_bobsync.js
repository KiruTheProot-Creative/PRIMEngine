// "Sync bobbing to step distance": the footstep happens at the bob's low point.
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  await p.evaluate(() => __studio.S.tags.setMany({ 'player.bobSync': true, 'player.bobAmount': 1 }));
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(300);
  console.log('setting shown:', await p.evaluate(() => document.querySelector('.insp').innerText.includes('Sync bobbing to step distance')));
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.hud, null, { timeout: 8000 }); await p.waitForTimeout(600);
  console.log(await p.evaluate(() => { const q = __studio.S.play; q.msg.style.display = 'none'; q.body = null; q.x = 2900; q.y = 3072; q.yaw = 0; q.keys.w = true; let steps = 0; const fs = q.footstep.bind(q), at = []; q.footstep = () => { steps++; at.push(+q.vmOff[1].toFixed(3)); fs(); };
    let lowest = 0; for (let i = 0; i < 150; i++) { q.step(1 / 60); q.feel(1 / 60); lowest = Math.min(lowest, q.vmOff[1]); } q.keys.w = false;
    return 'footsteps ' + steps + ', bob height at each footstep ' + JSON.stringify(at) + ', lowest bob ' + lowest.toFixed(3) + ' (the footstep values should be close to the lowest)'; }));
};
