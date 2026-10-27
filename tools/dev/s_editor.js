module.exports = async (p, out) => {
  // 1. move the cannon with the X slider and see it change
  await p.evaluate(() => { const r = document.querySelector('#inspector input[type=range]'); r.value = 0.12; r.dispatchEvent(new Event('input')); });
  await p.waitForTimeout(300);
  console.log('posX', await p.evaluate(() => __prime.project.objects.armCannon.transform.position[0]));
  await p.screenshot({ path: out + '_moved.png' });
  // 2. audio tab: charge loop, waveform markers
  await p.evaluate(() => document.querySelector('#inspector').parentElement.scrollTo(0, 99999));
  await p.click('.tab:has-text("Charge loop")'); await p.waitForTimeout(400);
  await p.evaluate(() => document.querySelector('#right').scrollTo(0, 99999));
  await p.screenshot({ path: out + '_audio.png' });
  // 3. drag the start marker
  const r = await p.locator('.wave .mk').first().boundingBox(); const w = await p.locator('.wave').boundingBox();
  await p.mouse.move(r.x + 7, r.y + 30); await p.mouse.down(); await p.mouse.move(w.x + w.width * 0.25, r.y + 30, { steps: 4 }); await p.mouse.up();
  console.log('loop start', await p.evaluate(() => __prime.project.objects.armCannon.audio.chargeLoop.start));
  // 4. play mode
  await p.click('#playBtn'); await p.waitForTimeout(300);
  await p.evaluate(() => { __prime.manual = true; __prime.step(30); });
  await p.waitForTimeout(300);
  console.log('mode', await p.evaluate(() => __prime.Game.mode), 'chrome hidden', await p.evaluate(() => getComputedStyle(document.getElementById('topbar')).display));
  await p.screenshot({ path: out + '_play.png' });
  // fire + charge
  await p.evaluate(() => { __prime.Game.S.noLockSupport = true; __prime.Game.S.firing = true; __prime.Game.fireDown(); __prime.step(120); });
  await p.screenshot({ path: out + '_charge.png' });
  console.log('charge', await p.evaluate(() => JSON.stringify({ on: __prime.Game.S.charge.on, full: __prime.Game.S.charge.full })));
  await p.keyboard.press('Escape'); await p.waitForTimeout(200);
  console.log('after esc', await p.evaluate(() => __prime.Game.mode));
  await p.click('#exitBtn'); await p.waitForTimeout(300);
  console.log('back to', await p.evaluate(() => __prime.Game.mode));
  await p.screenshot({ path: out + '_back.png' });
};
