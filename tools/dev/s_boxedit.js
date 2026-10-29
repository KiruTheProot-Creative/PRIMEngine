// Object Editor: solid boxes list + drag handles (move / resize), enemy muzzle point + arrow handles.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/Arm_canon.fbx']); await p.waitForTimeout(2000);
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, arm = pr.assets.find(a => /arm/i.test(a.name)).id; const d = Sc.addProp(pr, { name: 'Desk', type: 'decoration', asset: arm, solid: 'box' }); Sc.ensureSolids(pr); S.propSel = d.id; S.tags.emit([]); });
  await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(1500);
  await p.click('button:has-text("+ Box")'); await p.waitForTimeout(400);
  await p.click('.tbtn:has-text("Front")'); await p.waitForTimeout(400);
  const before = await p.evaluate(() => { const S = __studio.S, pr = S.project.props[0], b = pr.solids.find(q => q.id === S.sbSel), T = S.tags, k = pr.id + '.sb.' + b.id + '.'; return { n: pr.solids.length, x: T.get(k + 'x'), w: T.get(k + 'w') }; });
  console.log('before', JSON.stringify(before));
  await p.screenshot({ path: out + '_front.png' });
  // find the +X face handle on screen: scan for the red handle by hovering a grid of points and checking the cursor
  const cv = await p.$('.v3cv'), bb = await cv.boundingBox(); let hit = null;
  for (let y = bb.y + 244; y < bb.y + 270 && !hit; y += 3) for (let x = bb.x + bb.width - 60; x > bb.x + bb.width / 2 && !hit; x -= 3) { await p.mouse.move(x, y); if (await p.evaluate(() => document.querySelector('.v3cv').style.cursor) === 'move') hit = [x, y]; }
  console.log('handle at', JSON.stringify(hit), JSON.stringify(await p.evaluate(() => __studio.S.pe.dbg())));
  if (hit) { await p.mouse.down(); await p.mouse.move(hit[0] + 40, hit[1], { steps: 5 }); await p.mouse.up(); await p.waitForTimeout(300); }
  const after = await p.evaluate(() => { const S = __studio.S, pr = S.project.props[0], b = pr.solids.find(q => q.id === S.sbSel), T = S.tags, k = pr.id + '.sb.' + b.id + '.'; return { x: T.get(k + 'x'), y: T.get(k + 'y'), w: T.get(k + 'w'), h: T.get(k + 'h') }; });
  console.log('after drag', JSON.stringify(after));
  await p.screenshot({ path: out + '_after.png' });
  // enemy muzzle
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, arm = pr.assets.find(a => /arm/i.test(a.name)).id; const e = Sc.addProp(pr, { name: 'Gunner', type: 'enemy', asset: arm, muzzleOn: true, muzzleY: 70, muzzleZ: 30 }); S.propSel = e.id; S.tags.emit([]); __studio.renderCenter && 0; });
  await p.click('.tab:has-text("Timeline")'); await p.waitForTimeout(200); await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(1500); await p.click('.tbtn:has-text("Side")'); await p.waitForTimeout(400);
  await p.screenshot({ path: out + '_muzzle.png' });
};
