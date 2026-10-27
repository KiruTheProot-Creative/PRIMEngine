const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await p.click('.hrow:has-text("main") >> nth=0'); await p.click('.hrow:has-text("play")'); await p.waitForTimeout(300);
  await p.click('.tab:has-text("Node Coder")'); await p.waitForTimeout(500);
  const add = async name => { await p.click('button:has-text("Add node")'); const cat = /clicked|starts|key/.test(name) ? 'Events' : /Wait|If|Repeat/.test(name) ? 'Logic' : 'Actions'; await p.click('.pop .pitem:has-text("' + cat + '")'); await p.click('.pop .pitem:has-text("' + name + '") >> nth=-1'); await p.waitForTimeout(250); };
  await add('When clicked'); await add('Change a number by'); await add('Write a message'); await p.waitForTimeout(300);
  await p.screenshot({ path: out + '_c1.png' });
  const dots = await p.evaluate(() => [...document.querySelectorAll('.ncn')].map(c => { const r = c.getBoundingClientRect(); const d = c.querySelector('.ncdot'), i = c.querySelector('.ncin'); const dr = d && d.getBoundingClientRect(), ir = i && i.getBoundingClientRect(); return { t: c.querySelector('.nch').textContent, d: dr && [dr.x + 7, dr.y + 7], i: ir && [ir.x + 7, ir.y + 7] }; }));
  console.log(JSON.stringify(dots));
  const drag = async (a, b) => { await p.mouse.move(a[0], a[1]); await p.mouse.down(); await p.mouse.move(b[0], b[1], { steps: 6 }); await p.mouse.up(); await p.waitForTimeout(200); };
  console.log('links', await p.evaluate(() => JSON.stringify(__studio.S.project.screens[1].nodes.find(n => n.id === 'play').script.links)));
  // set the message text via inspector, and the number tag
  await p.click('.ncn:has-text("Write a message")'); await p.waitForTimeout(200);
  await p.locator('.insp input.txt').first().fill('hello from play'); await p.waitForTimeout(200);
  await p.screenshot({ path: out + '_c2.png' });
  // run: game tab, click play button
  await p.evaluate(() => { window.__said = []; });
  await p.click('.tab:has-text("Game")'); await p.waitForTimeout(600);
  await p.evaluate(() => { const o = __studio.S.rt; o.o.say = t => window.__said.push(t); __studio.S.rt.sh.o.say = o.o.say; });
  const b = await p.locator('.rt-btn:has-text("Play")').boundingBox(); await p.mouse.click(b.x + b.width / 2, b.y + b.height / 2); await p.waitForTimeout(500);
  console.log('said', await p.evaluate(() => JSON.stringify(window.__said)));
};
