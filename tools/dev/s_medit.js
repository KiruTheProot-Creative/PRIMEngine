module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.click('.mcard:has-text("DOOM")'); await p.waitForSelector('.medcv'); await p.waitForTimeout(300);
  const E = () => p.evaluate(() => { const m = __studio.S.project.maps[0]; return m.sectors.map(s => s.id + ':' + s.v.length).join(' ') + ' | verts ' + m.verts.length; });
  const W = (x, y) => p.evaluate(([x, y]) => { const e = __studio.S.med, r = e.cv.getBoundingClientRect(); return [r.left + e.sx(x), r.top + e.sy(y)]; }, [x, y]);
  // box room east of the start room
  await p.click('button:has-text("Box Room")'); let a = await W(128, -64), b = await W(320, 64); await p.mouse.move(...a); await p.mouse.down(); await p.mouse.move(...b, { steps: 5 }); await p.mouse.up(); await p.waitForTimeout(300);
  console.log('after box', await E());
  await p.keyboard.press('Control+z'); await p.waitForTimeout(400); console.log('after undo', await E());
  await p.keyboard.press('Control+y'); await p.waitForTimeout(400); console.log('after redo', await E());
  // drag the new room down by 64
  await p.click('button:has-text("Select / Move")'); a = await W(250, 0); b = await W(250, 96); await p.mouse.move(...a); await p.mouse.down(); await p.mouse.move(...b, { steps: 5 }); await p.mouse.up(); await p.waitForTimeout(300);
  console.log('room moved, its nodes', await p.evaluate(() => { const m = __studio.S.project.maps[0], s = m.sectors[1]; return s.v.map(id => { const v = m.verts.find(x => x.id === id); return v.x + ',' + v.y; }).join(' '); }));
  // shift-drag rubber band over everything, copy, paste with Ctrl+D
  a = await W(-200, -200); b = await W(380, 230); const rc = await p.evaluate(() => { const r = __studio.S.med.cv.getBoundingClientRect(); return [r.left, r.top, r.right, r.bottom]; }); a = [Math.max(a[0], rc[0] + 3), Math.max(a[1], rc[1] + 3)]; b = [Math.min(b[0], rc[2] - 3), Math.min(b[1], rc[3] - 3)]; console.log('band', a.map(Math.round), b.map(Math.round), rc.map(Math.round)); await p.keyboard.down('Shift'); await p.mouse.move(...a); await p.mouse.down(); await p.mouse.move(...b, { steps: 5 }); await p.mouse.up(); await p.keyboard.up('Shift'); await p.waitForTimeout(200);
  console.log('multi', await p.evaluate(() => JSON.stringify(__studio.S.msel)));
  await p.keyboard.press('Control+d'); await p.waitForTimeout(400); console.log('after duplicate', await E());
  await p.screenshot({ path: out + '_me.png' });
  await p.keyboard.press('Delete'); await p.waitForTimeout(300); console.log('after delete copies', await E());
  console.log('stack', await p.evaluate(() => __studio.S.med.undoS.length + ' focus=' + document.activeElement.className)); await p.keyboard.press('Control+z'); await p.waitForTimeout(400); console.log('undo delete', await E());
};
