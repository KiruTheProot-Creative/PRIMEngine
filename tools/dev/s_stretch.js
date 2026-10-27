const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/assets_test/Images/logo.png']); await p.waitForTimeout(600);
  await p.click('.hrow:has-text("main") >> nth=0'); await p.click('.tbtn:has-text("Create") >> nth=0'); await p.click('.pitem:has-text("Image")'); await p.waitForTimeout(300);
  await p.locator('.insp select').first().selectOption({ label: 'logo.png' }); await p.waitForTimeout(400);
  const m = () => p.evaluate(() => { const r = __studio.S.rt, id = __studio.S.selId, el = r.els[id]; return [el.offsetWidth, el.offsetHeight, el.style.left, el.style.top, r.host.querySelectorAll('.rt-handle').length]; });
  console.log('normal', await m());
  await p.locator('.insp .fld', { hasText: 'Stretch to screen' }).locator('input[type=checkbox]').first().check(); await p.waitForTimeout(400);
  console.log('stretched', await m());
  await p.screenshot({ path: out + '_st.png' });
};
