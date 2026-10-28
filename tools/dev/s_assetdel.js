// Asset library: Delete button (asks, says where used), right-click menu, map textures fall back, Remove unused assets, Ctrl+X / C / V.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/boom.png', SP + '/beep.wav', SP + '/bg_menu.png', SP + '/cube_bin.fbx']); await p.waitForTimeout(1500);
  const names = () => p.evaluate(() => JSON.stringify(__studio.S.project.assets.map(a => (a.path ? a.path + '/' : '') + a.name)));
  // boom.png as a wall texture in the map, beep as a player sound
  await p.evaluate(() => { const S = __studio.S, pr = S.project, m = pr.maps[0], id = pr.assets.find(a => a.name === 'boom.png').id; let n = 0; for (const c of Object.values(m.layers[0].cells)) if (c.w !== undefined && n < 5) { c.w = id; n++; } S.tags.set('player.jumpSound', pr.assets.find(a => a.name === 'beep.wav').id); S.tags.emit([]); });
  const tile = n => p.locator('.pit:has-text("' + n + '")');
  await tile('boom.png').click(); await p.waitForTimeout(300);
  console.log('inspector:', (await p.locator('.insp').innerText()).split('\n').filter(l => /Used by|map1|Delete asset/.test(l)).join(' | '));
  // right-click menu
  await tile('beep.wav').click({ button: 'right' }); await p.waitForTimeout(200);
  console.log('right-click menu:', (await p.locator('.pop .pitem').allTextContents()).join(' | ')); await p.keyboard.press('Escape');
  // delete boom.png with the button (confirm dialog accepted)
  await tile('boom.png').click(); await p.waitForTimeout(200); let dlg = ''; p.once('dialog', d => { dlg = d.message(); d.accept(); });
  await p.click('.insp button:has-text("Delete asset")'); await p.waitForTimeout(400);
  console.log('dialog said:', dlg.split('\n').filter(Boolean).slice(0, 2).join(' / '));
  console.log('after delete:', await names(), 'walls still pointing at it:', await p.evaluate(() => Object.values(__studio.S.project.maps[0].layers[0].cells).filter(c => c.w && !__studio.S.project.assets.some(a => a.id === c.w)).length));
  // cut / copy / paste: make a folder, copy beep into it, cut bg_menu into it
  await menu('Assets', 'Create Folder'); await p.waitForTimeout(300); await p.keyboard.press('Escape'); await p.waitForTimeout(200);
  await tile('beep.wav').click(); await p.keyboard.press('Control+c'); await p.waitForTimeout(200);
  await tile('bg_menu.png').click(); await p.keyboard.press('Control+x'); await p.waitForTimeout(200);
  console.log('cut looks faded:', await p.locator('.pit.cut').count());
  await tile('New Folder').dblclick(); await p.waitForTimeout(300); await p.click('#proj .pbody', { position: { x: 400, y: 120 } }).catch(() => {}); await p.keyboard.press('Control+v'); await p.waitForTimeout(800);
  console.log('after paste (move):', await names());
  // copy is still on the clipboard? (cut replaced it) -> copy beep again and paste into the folder
  await p.evaluate(() => { __studio.S.folder = ''; }); await p.click('#proj .pbody', { position: { x: 400, y: 120 } }).catch(() => {}); await p.evaluate(() => { __studio.S.folder = ''; }); 
  await p.evaluate(() => __studio.S.tags.emit([])); await p.waitForTimeout(200);
  await p.evaluate(() => { const S = __studio.S; S.sel = { kind: 'asset', key: S.project.assets.find(a => a.name === 'beep.wav' && !a.path).id }; S.multi = null; S.pfocus = true; });
  await p.keyboard.press('Control+c'); await p.evaluate(() => { __studio.S.folder = 'New Folder'; __studio.S.pfocus = true; }); await p.keyboard.press('Control+v'); await p.waitForTimeout(800);
  console.log('after paste (copy):', await names(), 'copy plays:', await p.evaluate(() => { const S = __studio.S, a = S.project.assets.find(x => x.path === 'New Folder' && x.name.startsWith('beep')); return !!(a && S.urls[a.id]); }));
  // remove unused: the cube model and the copied beep are unused; the original beep is the jump sound
  p.once('dialog', d => { console.log('unused dialog:', d.message().split('\n').filter(l => l.startsWith('•')).join(' ')); d.accept(); });
  await menu('Assets', 'Remove Unused Assets'); await p.waitForTimeout(400);
  console.log('after remove unused:', await names(), 'jump sound kept:', await p.evaluate(() => !!__studio.S.project.assets.find(a => a.id === __studio.S.tags.get('player.jumpSound'))));
};
