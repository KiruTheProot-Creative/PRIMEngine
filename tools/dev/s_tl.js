const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.click('.mcard:has-text("DOOM")'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/Arm_canon.fbx', SP + '/beep.wav']); await p.waitForTimeout(1800);
  console.log('assets', await p.evaluate(() => JSON.stringify(__studio.S.project.assets.map(a => a.name + ':' + a.type))));
  await p.click('.tab:has-text("Timeline")'); await p.waitForTimeout(1200); await p.screenshot({ path: out + '_tl1.png' });
  await p.click('.tla:has-text("shot")'); await p.waitForTimeout(600);
  await p.click('button:has-text("Add sound at playhead")'); await p.waitForTimeout(200); await p.screenshot({ path: out + '_tlmenu.png' });
  await p.click('.pitem >> nth=0'); await p.waitForTimeout(500);
  await p.evaluate(() => { __studio.S.tl.t = 0.2; }); await p.waitForTimeout(300);
  await p.click('button:has-text("Add sound at playhead")'); await p.click('.pitem >> nth=0'); await p.waitForTimeout(500);
  console.log('events', await p.evaluate(() => JSON.stringify({ ev: __studio.S.project.events, t: Object.keys(__studio.S.project.tags).filter(k => k.startsWith('ev')) })));
  await p.screenshot({ path: out + '_tl2.png' });
  await p.evaluate(() => { __studio.S.tl.t = 0.4; }); await p.waitForTimeout(400); await p.screenshot({ path: out + '_tl3.png' });
  // in game: attach the model, set the click animation, fire, count sounds that start
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(500); await p.click('button:has-text("New weapon")'); await p.click('.pitem:has-text("Arm_canon")'); await p.waitForTimeout(800);
  await p.evaluate(() => { window.__aud = 0; const P = PRIM.Audio.play; PRIM.Audio.play = (u, v, l) => { window.__aud++; return P(u, v, l); }; });
  await p.click('button:has-text("Player view")'); await p.waitForTimeout(600); await p.click('button:has-text("Walk map1")'); await p.waitForTimeout(1000);
  console.log('events after attach', await p.evaluate(() => JSON.stringify(__studio.S.project.events.map(e => e.id))));
  
  const ok = await p.evaluate(() => { const q = __studio.S.play; if (!q) return 'no play'; q.fire(); return 'fired'; }); await p.waitForTimeout(900);
  console.log('walk', ok, 'sounds started', await p.evaluate(() => window.__aud));
  const u = await p.evaluate(async () => { const pr = __studio.S.project; return JSON.stringify(JSON.parse(JSON.stringify(pr)).events.length); }); console.log('events survive json', u);
};
