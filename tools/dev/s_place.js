const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.click('.mcard:has-text("DOOM")'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles(SP + '/cube_bin.fbx'); await p.waitForTimeout(1500);
  await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(400);
  await p.click('button:has-text("New object")'); await p.click('.pitem:has-text("cube_bin")'); await p.waitForTimeout(800);
  await p.evaluate(() => { const id = __studio.S.project.props[0].id; __studio.S.tags.set(id + '.type', 'door'); });
  await p.click('.tab:has-text("Map")'); await p.waitForTimeout(600);
  await p.click('button:has-text("Place Object")'); await p.waitForTimeout(300);
  const pt = await p.evaluate(() => { const e = __studio.S.med, r = e.cv.getBoundingClientRect(); return { x: r.left + e.sx(0), y: r.top + e.sy(-128) + 2 }; });
  await p.mouse.click(pt.x, pt.y); await p.waitForTimeout(400);
  console.log('things', await p.evaluate(() => JSON.stringify(__studio.S.project.maps[0].things)));
  await p.screenshot({ path: out + '_place.png' });
  await p.keyboard.press('Delete'); await p.waitForTimeout(200); console.log('after delete', await p.evaluate(() => JSON.stringify(__studio.S.project.maps[0].things)));
  console.log('done');
};
