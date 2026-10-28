const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(400);
  console.log('screens', await p.evaluate(() => __studio.S.project.screens.map(s => s.id + ':' + s.kind).join(',')));
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv'); await p.waitForTimeout(300);
  // Player Editor tab
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(900); await p.screenshot({ path: out + '_player.png' });
  console.log('tabs', await p.locator('#center .tab').allTextContents());
  // change speed, sprint, crosshair colour through tags
  await p.evaluate(() => { const T = __studio.S.tags; T.set('player.crosshairColor', '#7df3ff'); T.set('player.crosshairSize', 14); });
  await p.waitForTimeout(200); await p.screenshot({ path: out + '_player2.png' });
  // walk: open and drive with keys
  await p.click('button:has-text("Walk map1")'); await p.waitForTimeout(600);
  const st = () => p.evaluate(() => { const q = __studio.S.play; return { x: Math.round(q.x), y: Math.round(q.y), z: Math.round(q.z), yaw: +q.yaw.toFixed(2) }; });
  console.log('start', JSON.stringify(await st()));
  await p.mouse.click(700, 400); await p.waitForTimeout(100);
  await p.keyboard.down('w'); await p.waitForTimeout(400); console.log('after 0.4s W', JSON.stringify(await st()));
  await p.keyboard.up('w');
  await p.keyboard.down('Shift'); await p.keyboard.down('w'); await p.waitForTimeout(400); console.log('sprinting 0.4s', JSON.stringify(await st())); await p.keyboard.up('w'); await p.keyboard.up('Shift');
  // walk into the wall for 3s: must stay inside the 256 room (radius 16 => |y|<=112 or so)
  await p.keyboard.down('w'); await p.waitForTimeout(2500); await p.keyboard.up('w'); console.log('wall bump', JSON.stringify(await st()));
  // jump
  await p.keyboard.down('Space'); await p.waitForTimeout(150); const hi = await st(); await p.keyboard.up('Space'); await p.waitForTimeout(900); console.log('jump peak z', hi.z, 'landed', JSON.stringify(await st()));
  // health keys
  await p.keyboard.press('h'); await p.keyboard.press('h'); await p.waitForTimeout(200);
  console.log('health', await p.evaluate(() => __studio.S.play.T.get('player.health')), 'project health untouched', await p.evaluate(() => __studio.S.tags.get('player.health')));
  await p.screenshot({ path: out + '_walk.png' });
  await p.evaluate(() => document.exitPointerLock()); await p.waitForTimeout(200); console.log("leave btn visible", await p.locator(".p3leave").isVisible()); await p.locator(".p3leave").click(); await p.waitForTimeout(200);
  console.log('layers left', await p.locator('.p3layer').count());
  // sprint disabled / jump disabled toggles
  await p.evaluate(() => { const T = __studio.S.tags; T.set('player.jumpEnabled', false); T.set('player.sprintEnabled', false); });
  await p.click('.hrow:has-text("hud")'); await p.waitForTimeout(400); await p.screenshot({ path: out + '_hud.png' });
  console.log('hud nodes', await p.evaluate(() => __studio.S.project.screens.find(s => s.id === 'hud').nodes.map(n => n.id + ':' + n.type).join(',')));
};
