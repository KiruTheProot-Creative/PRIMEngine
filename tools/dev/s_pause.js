// Pause menu: a new project gets a pause screen + options screen; Pause Menu tab; Esc in the game opens it, the game freezes,
// Options / Back / Resume / Restart / Quit to menu work, sounds placed in the world are turned down while paused.
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  console.log('screens:', await p.evaluate(() => __studio.S.project.screens.map(s => s.id + ':' + s.kind).join(' ')), '| pauseScreen tag:', await p.evaluate(() => __studio.S.tags.get('player.pauseScreen')));
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  await p.click('.tab:has-text("Pause Menu")'); await p.waitForTimeout(600);
  console.log('pause tab bar:', (await p.locator('.cbar').innerText()).replace(/\s+/g, ' ').slice(0, 160));
  console.log('elements:', await p.evaluate(() => Object.keys(__studio.S.rt.els).join(',')));
  await p.screenshot({ path: out + '_editor.png' });
  // a second pause screen from the bar menu
  await p.click('.cbar button:has-text("+ New pause screen")'); await p.click('.pop .pitem:has-text("Pause menu (Resume")'); await p.waitForTimeout(400);
  console.log('after new:', await p.evaluate(() => __studio.S.project.screens.filter(s => s.kind === 'pause').map(s => s.id).join(',')), '| editing', await p.evaluate(() => __studio.S.screenId), '| Esc opens', await p.evaluate(() => __studio.S.tags.get('player.pauseScreen')));
  // play
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(300);
  await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.hud, null, { timeout: 8000 }); await p.waitForTimeout(500);
  const st = () => p.evaluate(() => { const q = __studio.S.play; return JSON.stringify({ paused: !!q.paused, screen: q.pauseRt && q.pauseRt.screenId, world: PRIM.Audio.world, x: Math.round(q.x), y: Math.round(q.y), alive: !q.dead }); });
  const step = n => p.evaluate(n => { const q = __studio.S.play; let t = q.last || performance.now(); for (let i = 0; i < n; i++) { t += 16; q.frame(t); cancelAnimationFrame(q.raf); } q.raf = requestAnimationFrame(x => q.frame(x)); }, n);
  await p.evaluate(() => { const q = __studio.S.play; q.engaged = true; q.msg.style.display = 'none'; });
  await p.keyboard.press('Escape'); await p.waitForTimeout(300);
  console.log('after Esc:', await st());
  // frozen: holding W does not move
  const x0 = await p.evaluate(() => __studio.S.play.x); await p.evaluate(() => { __studio.S.play.keys.w = true; }); await step(20); await p.evaluate(() => { __studio.S.play.keys.w = false; });
  console.log('moved while paused:', await p.evaluate(x0 => Math.round(__studio.S.play.x - x0), x0));
  await p.screenshot({ path: out + '_game.png', timeout: 90000 }).catch(() => console.log('(skip)'));
  const clickEl = async id => { const r = await p.evaluate(id => { const e = __studio.S.play.pauseRt.els[id]; const b = e.getBoundingClientRect(); return [b.left + b.width / 2, b.top + b.height / 2]; }, id); await p.mouse.click(r[0], r[1]); await p.waitForTimeout(250); };
  await clickEl('options'); console.log('Options clicked:', await st());
  await p.keyboard.press('Escape'); await p.waitForTimeout(300); console.log('Esc (back):', await st());
  await clickEl('options'); await clickEl('back'); console.log('Back button:', await st());
  await clickEl('resume'); console.log('Resume:', await st());
  // walk a bit, then Restart level puts you back
  await p.evaluate(() => { __studio.S.play.keys.w = true; }); await step(30); await p.evaluate(() => { __studio.S.play.keys.w = false; });
  console.log('walked:', await st());
  await p.evaluate(() => __studio.S.play.pauseOpen()); await p.waitForTimeout(200); await clickEl('restart'); await step(2); console.log('Restart:', await st());
  // a script node opens the pause menu; Quit to main menu leaves the walk test
  await p.evaluate(() => __studio.S.play.pauseOpen()); await p.waitForTimeout(200); await clickEl('menu'); await p.waitForTimeout(300);
  console.log('Quit to menu -> game closed:', await p.evaluate(() => !!(__studio.S.play && __studio.S.play.dead)), '| world volume back:', await p.evaluate(() => PRIM.Audio.world));
};
