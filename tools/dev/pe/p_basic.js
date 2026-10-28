module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  await p.click('button:has-text("New project")'); await p.waitForSelector('.pe-app'); await p.waitForTimeout(1200);
  await p.screenshot({ path: out + '_editor.png' });
  // draw a box with the Box tool
  await p.click('button:has-text("Box (B)")'); const c = await p.evaluate(() => { const r = document.querySelector('.pb canvas').getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; });
  await p.mouse.move(c.x + 60, c.y + 60); await p.mouse.down(); await p.mouse.move(c.x + 200, c.y + 120, { steps: 6 }); await p.mouse.up(); await p.waitForTimeout(300);
  console.log('after box', await p.evaluate(() => { const r = __pe.project.rooms[0]; const b = r.brushes[r.brushes.length - 1]; return r.brushes.length + ' brushes, last ' + b.id + ' p=' + b.p + ' s=' + b.s + ' sel=' + JSON.stringify(__pe.sel); }));
  await p.screenshot({ path: out + '_box.png' });
  // drag it with the select tool
  await p.click('button:has-text("Select (V)")'); await p.evaluate(() => __pe.builder.select(null)); const before = await p.evaluate(() => __pe.project.rooms[0].brushes.slice(-1)[0].p.join(','));
  await p.mouse.move(c.x + 130, c.y + 90); await p.mouse.down(); await p.mouse.move(c.x + 230, c.y + 90, { steps: 6 }); await p.mouse.up(); await p.waitForTimeout(300);
  console.log('moved', before, '->', await p.evaluate(() => __pe.project.rooms[0].brushes.slice(-1)[0].p.join(',')));
  await p.keyboard.press('Control+z'); await p.waitForTimeout(200); console.log('undo ->', await p.evaluate(() => __pe.project.rooms[0].brushes.slice(-1)[0].p.join(',')));
  // place an enemy and a pick-up
  await p.selectOption('.pe-cbar select', 'enemy'); await p.mouse.click(c.x - 100, c.y + 80); await p.waitForTimeout(200);
  await p.selectOption('.pe-cbar select', 'pickup'); await p.mouse.click(c.x - 160, c.y + 120); await p.waitForTimeout(300);
  console.log('things', await p.evaluate(() => __pe.project.rooms[0].things.map(t => t.kind + '@' + t.p.join(',')).join(' | ')));
  console.log('inspector', await p.evaluate(() => [...document.querySelectorAll('.pe-right .pe-fh')].map(e => e.textContent).join(' | ')));
  await p.screenshot({ path: out + '_things.png' });
};
