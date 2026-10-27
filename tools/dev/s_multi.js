const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/assets_test/Images/logo.png', SP + '/assets_test/Images/btn.png', SP + '/beep.wav']); await p.waitForTimeout(800);
  const st = () => p.evaluate(() => [...document.querySelectorAll('.pgrid .pit.sel')].map(e => e.dataset.k.split(':')[0] + ':' + e.querySelector('.pname').textContent).join(',') + ' | insp=' + (document.querySelector('.insp .idin') || {}).textContent);
  const items = p.locator('.pgrid .pit');
  console.log('count', await items.count());
  await items.nth(0).click(); console.log('single', await st());
  await items.nth(2).click({ modifiers: ['Shift'] }); console.log('shift range', await st());
  await items.nth(1).click({ modifiers: ['Control'] }); console.log('ctrl toggle off', await st());
  await items.nth(1).click({ modifiers: ['Control'] }); console.log('ctrl toggle on', await st());
  await items.nth(0).click(); console.log('plain', await st());
  const g = await p.locator('.pgrid').boundingBox(), last = await items.nth(2).boundingBox();
  await p.mouse.move(g.x + g.width - 10, g.y + g.height - 10); await p.mouse.down(); await p.mouse.move(g.x + 2, g.y + 2, { steps: 8 }); await p.mouse.up(); await p.waitForTimeout(200);
  console.log('marquee', await st());
  await p.screenshot({ path: out + '_m.png' });
  await p.mouse.click(g.x + g.width - 10, g.y + g.height - 10); console.log('empty click', await st());
  await p.click('.mb:has-text("Assets")'); await p.click('.pitem:has-text("Create Folder")'); await p.keyboard.type('box'); await p.keyboard.press('Enter'); await p.waitForTimeout(300);
  console.log('items', await p.locator('.pgrid .pit').evaluateAll(e => e.map(x => x.dataset.k)));
  const sel = async (n, mod) => p.locator('.pgrid .pit').nth(n).click(mod ? { modifiers: [mod] } : {});
  await sel(1); await sel(2, 'Control');
  console.log('picked', await st());
  await p.evaluate(() => {
    const items = [...document.querySelectorAll('.pgrid .pit')], dt = new DataTransfer();
    const src = items.find(e => e.classList.contains('sel')), dst = items.find(e => e.dataset.k.startsWith('folder'));
    src.dispatchEvent(new DragEvent('dragstart', { dataTransfer: dt, bubbles: true }));
    dst.dispatchEvent(new DragEvent('dragover', { dataTransfer: dt, bubbles: true, cancelable: true }));
    dst.dispatchEvent(new DragEvent('drop', { dataTransfer: dt, bubbles: true, cancelable: true }));
  });
  await p.waitForTimeout(300);
  console.log('after drop, root:', await p.locator('.pgrid .pit').evaluateAll(e => e.map(x => x.dataset.k)), 'assets paths:', await p.evaluate(() => __studio.S.project.assets.map(a => a.path + '/' + a.name)));
  // delete two (press twice)
  await sel(0); await sel(1, 'Shift'); console.log('picked2', await st());
  await p.locator('.pgrid').click({ position: { x: 5, y: 5 }, modifiers: ['Control'] }).catch(() => {});
  await sel(0); await sel(1, 'Control');
  await p.keyboard.press('Delete'); await p.keyboard.press('Delete'); await p.waitForTimeout(300);
  console.log('after delete', await p.locator('.pgrid .pit').evaluateAll(e => e.map(x => x.dataset.k)), await p.evaluate(() => __studio.S.project.assets.length));
};
