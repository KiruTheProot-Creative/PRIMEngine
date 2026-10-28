module.exports = async (p, out) => {
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(400);
  await menu('Screen', 'Create Map'); await p.click('.mcard:has-text("DOOM")'); await p.waitForSelector('.medcv'); await p.waitForTimeout(300);
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(700);
  const t = await p.evaluate(() => document.querySelector('#inspector, .insp') ? document.body.innerText : '');
  console.log('has Sounds', /Footstep sound/.test(t), /Jump sound/.test(t));
  // Node coder key dropdown
  await p.click('.hrow:has-text("main") >> nth=0'); await p.click('.hrow:has-text("play")'); await p.click('.tab:has-text("Node Coder")'); await p.waitForTimeout(400);
  await p.click('button:has-text("Add node")'); const items = await p.locator('.pitem').allTextContents(); console.log('node menu', items.join(' | '));
  await p.hover('.pitem:has-text("Events")'); await p.waitForTimeout(200); await p.click('.pitem:has-text("When a key is pressed")'); await p.waitForTimeout(300);
  console.log('key select options', await p.evaluate(() => { const s = [...document.querySelectorAll('.insp select')].find(x => [...x.options].some(o => /mouse/i.test(o.text))); return s ? s.options.length + ' first: ' + [...s.options].slice(0, 4).map(o => o.text).join(',') : 'none'; }));
  await p.screenshot({ path: out + '_k.png' });
};
