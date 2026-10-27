const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/assets_test/Images/logo.png']); await p.waitForTimeout(800);
  await p.click('.hrow:has-text("splash") >> nth=0'); await p.click('.tbtn:has-text("Create") >> nth=0'); await p.click('.pitem:has-text("Splash")'); await p.waitForTimeout(300);
  await p.locator('.insp .fld', { hasText: 'Shows' }).locator('select').selectOption('image'); await p.waitForTimeout(300);
  await p.locator('select[aria-label="Image"]').selectOption({ label: 'logo.png' }); await p.waitForTimeout(400);
  const m = () => p.evaluate(() => { const r = __studio.S.rt, id = __studio.S.selId, el = r.els[id]; return [id, el && el.tagName, el && el.offsetWidth, el && el.offsetHeight, !!r.splash]; });
  console.log('start', await m());
  await p.locator('.insp .fld', { hasText: 'Width' }).locator('input.num').fill('300'); await p.keyboard.press('Enter'); await p.waitForTimeout(400); console.log('after width', await m());
  const pv = p.locator('button:has-text("Preview")'); if (await pv.count()) { await pv.first().click(); await p.waitForTimeout(500); console.log('preview', await m()); }
  await p.screenshot({ path: out + '_s2.png' });
};
