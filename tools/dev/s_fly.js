const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.click('.mcard:has-text("DOOM")'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles(SP + '/assets_test/Images/logo.png'); await p.waitForTimeout(1200);
  await p.click('.tab:has-text("Fly Editor")'); await p.waitForTimeout(900);
  const box = await p.evaluate(() => { const r = document.querySelector('.v3cv').getBoundingClientRect(); return { x: r.left, y: r.top, w: r.width, h: r.height }; });
  const cam = () => p.evaluate(() => { const c = __studio.S.v3.cam; return [Math.round(c.x), Math.round(c.z), +c.yaw.toFixed(2)].join(','); });
  console.log('cam start', await cam());
  // left click selects a face
  await p.mouse.click(box.x + box.w / 2, box.y + box.h / 2); await p.waitForTimeout(300);
  console.log('selected', await p.evaluate(() => JSON.stringify(__studio.S.fsl)));
  await p.keyboard.down('Shift'); await p.mouse.click(box.x + box.w / 2, box.y + box.h * 0.95); await p.keyboard.up('Shift'); await p.waitForTimeout(300);
  console.log('selected +shift', await p.evaluate(() => JSON.stringify(__studio.S.fsl)));
  await p.screenshot({ path: out + '_sel.png' });
  await p.selectOption('.insp select', { label: 'logo.png' }).catch(e => console.log('select fail', e.message)); await p.waitForTimeout(500);
  console.log('tags', await p.evaluate(() => { const T = __studio.S.tags; return JSON.stringify(__studio.S.fsl.map(f => f.kind === 'wall' ? T.get('map1.' + f.vert + '.tex') : T.get('map1.' + f.sec + '.' + f.kind + 'Tex'))); }));
  await p.screenshot({ path: out + '_tex.png' });
  // right mouse + W flies
  await p.mouse.move(box.x + box.w / 2, box.y + box.h / 2); await p.mouse.down({ button: 'right' }); await p.keyboard.down('w'); await p.waitForTimeout(500); await p.keyboard.up('w'); await p.mouse.up({ button: 'right' });
  console.log('cam after right+W', await cam());
  await p.keyboard.down('w'); await p.waitForTimeout(400); await p.keyboard.up('w'); console.log('cam after W without right button (should be same)', await cam());
};
