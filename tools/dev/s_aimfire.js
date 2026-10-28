// Firing (left click) while the aim button (right mouse) is held: real mouse events through Playwright.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/Arm_canon.fbx']); await p.waitForTimeout(2500);
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(500);
  await p.click('button:has-text("New weapon")'); await p.click('.pitem:has-text("Arm_canon")'); await p.waitForTimeout(1500);
  await p.evaluate(() => { const S = __studio.S, at = S.project.player.attachments[0], pre = 'player.' + at.id + '.'; S.tags.setMany({ [pre + 'projType']: 'hitscan', [pre + 'aimType']: 'sights', [pre + 'fireRate']: 0 }); });
  await p.evaluate(() => { __studio.S.objView = false; }); await p.click('.tab:has-text("Map")'); await p.waitForTimeout(200); await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(400);
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.view, null, { timeout: 12000 }); await p.waitForTimeout(400);
  await p.evaluate(() => { const q = __studio.S.play; q.__shots = 0; const f = q.fire.bind(q); q.fire = (...a) => { q.__shots++; return f(...a); }; });
  const box = await p.locator('canvas').first().boundingBox(); const cx = box.x + box.width / 2, cy = box.y + box.height / 2;
  await p.mouse.move(cx, cy); await p.mouse.down(); await p.mouse.up(); await p.waitForTimeout(200);   // first click only grabs the mouse
  const st = () => p.evaluate(() => { const q = __studio.S.play; return JSON.stringify({ shots: q.__shots, aiming: q.T.get('player.aiming'), engaged: q.engaged }); });
  console.log('engaged:', await st());
  await p.mouse.down({ button: 'left' }); await p.mouse.up({ button: 'left' }); await p.waitForTimeout(150);
  console.log('normal click:', await st());
  await p.mouse.down({ button: 'right' }); await p.waitForTimeout(300);
  await p.mouse.down({ button: 'left' }); await p.waitForTimeout(100); await p.mouse.up({ button: 'left' }); await p.waitForTimeout(100);
  await p.mouse.down({ button: 'left' }); await p.waitForTimeout(100); await p.mouse.up({ button: 'left' }); await p.waitForTimeout(100);
  console.log('two clicks while aiming:', await st());
  await p.mouse.up({ button: 'right' }); await p.waitForTimeout(300);
  console.log('let go of aim:', await st());
};
