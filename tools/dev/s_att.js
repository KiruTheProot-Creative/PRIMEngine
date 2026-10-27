const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.click('.mcard:has-text("DOOM")'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles(SP + '/Arm_canon.fbx'); await p.waitForTimeout(1500);
  console.log('asset', await p.evaluate(() => JSON.stringify(__studio.S.project.assets.map(a => ({ n: a.name, t: a.type, anims: a.meta && a.meta.anims && a.meta.anims.map(x => x.name) })))));
  await p.click('.pit:has-text("Arm_canon")'); await p.waitForTimeout(300); await p.screenshot({ path: out + '_asset.png' });
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(600);
  await p.click('button:has-text("Attach model")'); await p.click('.pitem:has-text("Arm_canon")'); await p.waitForTimeout(1500);
  console.log('att tags', await p.evaluate(() => { const T = __studio.S.tags; return JSON.stringify({ asset: T.get('player.att1.asset'), scale: T.get('player.att1.scale'), idle: T.get('player.att1.idleAnim'), fire: T.get('player.att1.fireAnim') }); }));
  await p.screenshot({ path: out + '_att1.png' });
  await p.evaluate(() => { const T = __studio.S.tags; T.set('player.att1.weapon', true); T.set('player.att1.muzzleZ', -100); T.set('player.att1.muzzleY', 62); T.set('player.att1.x', 6); T.set('player.att1.y', -14); T.set('player.att1.z', -22); });
  await p.waitForTimeout(900); await p.screenshot({ path: out + '_att2.png' });
  await p.click('button:has-text("Walk map1")'); await p.waitForTimeout(1200); await p.evaluate(() => { __studio.S.play.msg.style.display = 'none'; });
  await p.waitForTimeout(500); await p.screenshot({ path: out + '_walk1.png' });
  // face the wall close up: aim yaw 0 (towards +x) and walk to it
  await p.evaluate(() => { const q = __studio.S.play; q.yaw = 0; q.x = 90; });
  await p.waitForTimeout(800); await p.screenshot({ path: out + '_walk_wall.png' });
  await p.evaluate(() => { const q = __studio.S.play; q.fire(); }); await p.waitForTimeout(150); await p.screenshot({ path: out + '_fire.png' });
  console.log('fire state', await p.evaluate(() => JSON.stringify(__studio.S.play.atts.map(a => ({ id: a.id, fire: a.fire, t: +a.t.toFixed(2) })))));
};
