const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/assets_test/Images/logo.png', SP + '/assets_test/Images/btn.png']); await p.waitForTimeout(800);
  await p.click('.hrow:has-text("main") >> nth=0'); await p.click('.tbtn:has-text("Create") >> nth=0'); await p.click('.pitem:has-text("Image")'); await p.waitForTimeout(300);
  await p.locator('.insp select').first().selectOption({ label: 'logo.png' }); await p.waitForTimeout(400);
  const m = () => p.evaluate(() => { const r = __studio.S.rt, id = __studio.S.selId, el = r.els[id]; return [id, el.tagName, el.offsetWidth, el.offsetHeight, __studio.S.tags.get('main.' + id + '.w'), __studio.S.tags.get('main.' + id + '.h')]; });
  console.log('start', await m());
  const inputs = await p.evaluate(() => [...document.querySelectorAll('.insp .fld')].map(f => f.querySelector('.flab').textContent));
  console.log(JSON.stringify(inputs));
  const f = n => p.locator('.insp .fld', { hasText: n }).locator('input.num').first();
  await f('Width').fill('300'); await p.keyboard.press('Enter'); await p.waitForTimeout(400); console.log('after width', await m());
  await f('Height').fill('100'); await p.keyboard.press('Enter'); await p.waitForTimeout(400); console.log('after height', await m());
  const w = f('Width'); await w.click(); await p.keyboard.press('ArrowUp'); await p.keyboard.press('ArrowUp'); await p.waitForTimeout(300); console.log('after arrows', await m());
  const lb = await p.locator('.insp .fld', { hasText: 'Width' }).locator('.flab.scrub').first().boundingBox(); await p.mouse.move(lb.x + 20, lb.y + 8); await p.mouse.down(); await p.mouse.move(lb.x + 60, lb.y + 8, { steps: 5 }); await p.mouse.up(); await p.waitForTimeout(300); console.log('after scrub', await m());
  await w.click({ clickCount: 3 }); await p.keyboard.type('150'); await p.locator('.insp .fld', { hasText: 'Opacity' }).click(); await p.waitForTimeout(300); console.log('after typing+blur', await m());
  // after mouse interactions on the canvas
  const hb = await p.locator('.rt-handle').last().boundingBox(); await p.mouse.move(hb.x + 7, hb.y + 7); await p.mouse.down(); await p.mouse.move(hb.x + 60, hb.y + 40, { steps: 5 }); await p.mouse.up(); await p.waitForTimeout(300); console.log('after handle', await m(), await p.evaluate(() => !!__studio.S.rt.drag));
  await f('Width').fill('200'); await p.keyboard.press('Enter'); await p.waitForTimeout(300); console.log('field after handle', await m());
  const el = await p.locator('.rt-stage img').boundingBox(); await p.mouse.move(el.x + el.width / 2, el.y + el.height / 2); await p.mouse.down(); await p.mouse.move(el.x + 80, el.y + 80, { steps: 5 }); await p.mouse.up(); await p.waitForTimeout(300);
  await f('Width').fill('250'); await p.keyboard.press('Enter'); await p.waitForTimeout(300); console.log('field after move', await m(), await p.evaluate(() => !!__studio.S.rt.drag));
  // proportions lock: width change drags height along; top-left handle keeps the opposite corner
  await f('Width').fill('120'); await p.keyboard.press('Enter'); await p.waitForTimeout(300); console.log('locked width', await m());
  const tl = await p.locator('.rt-handle').first().boundingBox(), br0 = await p.locator('.rt-handle').last().boundingBox(); await p.mouse.move(tl.x + 8, tl.y + 8); await p.mouse.down(); await p.mouse.move(tl.x - 40, tl.y - 40, { steps: 5 }); await p.mouse.up(); await p.waitForTimeout(300);
  const br1 = await p.locator('.rt-handle').last().boundingBox(); console.log('after corner', await m(), 'opposite corner moved:', Math.abs(br1.x - br0.x) + Math.abs(br1.y - br0.y));
  await p.screenshot({ path: out + '_s1.png' });
};
