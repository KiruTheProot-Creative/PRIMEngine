module.exports = async (p, out) => {
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  await p.evaluate(() => __studio.S.tags.set('player.aimMode', 'free'));
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(400);
  console.log('radius field shown:', await p.locator('text=Free aim: aim area').count());
  await p.click('button:has-text("Walk map1")'); await p.waitForTimeout(400); await p.evaluate(() => { __studio.S.play.msg.style.display = 'none'; });
  const r = () => p.evaluate(() => { const q = __studio.S.play; return { yaw: +q.yaw.toFixed(3), aimYaw: +q.aimYaw.toFixed(3), aimPitch: +q.aimPitch.toFixed(3), xh: q.xh.style.transform }; });
  await p.evaluate(() => __studio.S.play.look(40, -20)); await p.waitForTimeout(250); console.log('small move (inside area)', JSON.stringify(await r())); await p.screenshot({ path: out + '_free1.png' });
  await p.evaluate(() => __studio.S.play.look(300, 0)); await p.waitForTimeout(250); console.log('big move (camera turns)', JSON.stringify(await r()));
  await p.evaluate(() => __studio.S.tags.set('player.aimMode', 'classic')); await p.evaluate(() => __studio.S.play.look(10, 0)); await p.waitForTimeout(200); console.log('classic', JSON.stringify(await r()));
};
