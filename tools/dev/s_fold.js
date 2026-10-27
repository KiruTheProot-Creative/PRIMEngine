module.exports = async (p, out) => {
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await p.click('.hrow:has-text("main") >> nth=0'); await p.click('.tbtn:has-text("Create")'); await p.click('.pitem:has-text("Folder inside")'); await p.waitForTimeout(300);
  await p.click('.tbtn:has-text("Create") >> nth=0'); await p.screenshot({ path: out + '_menu.png' }); await p.click('.pitem:has-text("Folder"):not(:has-text("inside"))'); await p.waitForTimeout(300);
  console.log('folders', await p.evaluate(() => JSON.stringify(__studio.S.project.hfolders)));
  const dnd = (src, dst) => p.evaluate(([a, b]) => { const q = t => [...document.querySelectorAll('.hrow')].find(r => r.querySelector('.hnm') && r.querySelector('.hnm').textContent === t), dt = new DataTransfer(), A = q(a), B = q(b); A.dispatchEvent(new DragEvent('dragstart', { dataTransfer: dt, bubbles: true })); B.dispatchEvent(new DragEvent('dragover', { dataTransfer: dt, bubbles: true, cancelable: true })); B.dispatchEvent(new DragEvent('drop', { dataTransfer: dt, bubbles: true, cancelable: true })); }, [src, dst]);
  await dnd('quit', 'folder'); await p.waitForTimeout(200); await dnd('hud', 'folder2'); await p.waitForTimeout(300);
  console.log('after drag', await p.evaluate(() => JSON.stringify({ q: __studio.S.project.screens[1].nodes.find(n => n.id === 'quit').folder, s: __studio.S.project.screens.map(s => [s.id, s.folder]) })));
  await p.screenshot({ path: out + '_f1.png' });
  await p.reload(); 
};
