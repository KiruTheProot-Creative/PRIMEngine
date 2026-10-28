// NPC: a character with a dialogue (choices jump to lines and change a tag), the talk prompt, the dialogue box, keys and clicks.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Folder')]); await fc.setFiles(SP + '/assets_test'); await p.waitForTimeout(700);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(400);
  await p.click('button:has-text("New object")'); await p.click('.pitem:has-text("2D sprite object")'); await p.waitForTimeout(500);
  await p.evaluate(() => { const S = __studio.S, id = S.project.props[0].id; S.tags.setMany({ [id + '.type']: 'npc', [id + '.name']: 'Old Miner', [id + '.spriteHeight']: 110 }); });
  await p.waitForTimeout(300); await p.click('.tab:has-text("Map")'); await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(400);
  // build the dialogue with the inspector buttons
  for (let i = 0; i < 3; i++) { await p.click('button:has-text("+ Add a line")'); await p.waitForTimeout(150); }
  await p.locator('.dlgline >> nth=0').locator('button:has-text("+ Add a choice")').click(); await p.waitForTimeout(150);
  await p.locator('.dlgline >> nth=0').locator('button:has-text("+ Add a choice")').click(); await p.waitForTimeout(150);
  const ids = await p.evaluate(() => { const S = __studio.S, pr = S.project.props[0], L = pr.dialog.lines.map(l => pr.id + '.d.' + l.id + '.'), T = S.tags;
    T.setMany({ [L[0] + 'text']: 'Hey, stranger. Need something?', [L[0] + 'c1text']: 'Heal me, please.', [L[0] + 'c1go']: pr.dialog.lines[2].id, [L[0] + 'c1tag']: 'player.health', [L[0] + 'c1val']: '+25',
      [L[0] + 'c2text']: 'No, bye.', [L[0] + 'c2go']: 'end', [L[1] + 'text']: '(never shown)', [L[2] + 'text']: 'There you go. Stay safe down there.', [L[2] + 'next']: 'end' });
    S.project.maps[0].things = [{ id: 't1', prop: pr.id, x: 3072 + 112, y: 3072, ang: 0, layer: 0 }]; S.project.maps[0].n.t = 1; T.emit([]); return L; });
  await p.click('.tab:has-text("Map")'); await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(400);
  await p.screenshot({ path: out + '_editor.png', fullPage: false });
  console.log('options of choice 1', await p.evaluate(() => { const S = __studio.S, pr = S.project.props[0]; return JSON.stringify(S.tags.spec(pr.id + '.d.' + pr.dialog.lines[0].id + '.c1go').options.map(o => o[0])); }));
  await p.click('.tab:has-text("Map")'); await p.waitForTimeout(300); await p.click('button:has-text("Walk this map")'); await p.waitForTimeout(1200);
  await p.evaluate(() => { const q = __studio.S.play; q.msg.style.display = 'none'; q.look = () => {}; q.body = null; q.x = 3072; q.y = 3072; q.yaw = 0; q.pitch = 0; q.T.set('player.health', 50); });
  await p.waitForTimeout(600); console.log('prompt', await p.evaluate(() => { const h = __studio.S.play.hintEl; return h && h.style.display !== 'none' ? h.textContent : 'none'; }));
  await p.keyboard.press('e'); await p.waitForTimeout(400); await p.screenshot({ path: out + '_talk.png' });
  const box = () => p.evaluate(() => { const q = __studio.S.play; return q.talk ? q.talkEl.innerText.replace(/\n+/g, ' | ') : 'closed'; });
  console.log('line 1:', await box());
  await p.keyboard.press('1'); await p.waitForTimeout(200); console.log('after choice 1:', await box(), ' health', await p.evaluate(() => __studio.S.play.T.get('player.health')));
  await p.click('.p3talk button'); await p.waitForTimeout(200); console.log('after continue:', await box());
  await p.keyboard.press('e'); await p.waitForTimeout(200); await p.keyboard.press('2'); await p.waitForTimeout(200); console.log('talk again, choice 2:', await box());
  console.log('player blocked by its body:', await p.evaluate(() => { const q = __studio.S.play; q.keys.w = true; for (let i = 0; i < 90; i++) q.step(1 / 60); q.keys.w = false; return Math.round(q.x) + ' (npc at ' + (3072 + 112) + ', radius 22 + 16)'; }));
  await p.keyboard.press('Escape');
};
