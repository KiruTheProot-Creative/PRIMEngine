const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.screenshot({ path: out + '_wizard.png' });
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(500);
  await p.screenshot({ path: out + '_editor.png' });
  // import a whole folder from "the computer"
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Folder')]); await fc.setFiles(SP + '/assets_test'); await p.waitForTimeout(800);
  const st = () => p.evaluate(() => JSON.stringify({ assets: __studio.S.project.assets.map(a => a.path + '/' + a.name), folders: __studio.Store.folders(__studio.S.project) }));
  console.log('after folder import', await st());
  await p.click('.trow:has-text("assets_test")'); await p.waitForTimeout(200);
  await p.screenshot({ path: out + '_imported.png' });
  // new folder + rename inline
  await menu('Assets', 'Create Folder'); await p.waitForSelector('.rename'); await p.fill('.rename', 'Fonts & Co'); await p.press('.rename', 'Enter'); await p.waitForTimeout(200);
  console.log('folders', await p.evaluate(() => JSON.stringify(__studio.S.project.folders)));
  // drag the logo image onto the Scene view -> new Image element
  await p.click('.hrow:has-text("main")'); await p.click('.trow:has-text("Images")'); await p.waitForTimeout(150);
  const logo = p.locator('.pit:has-text("logo.png")'); const stage = await p.locator('.rt-stage').boundingBox();
  await logo.dragTo(p.locator('.host'), { targetPosition: { x: 200, y: 120 } }); await p.waitForTimeout(300);
  console.log('nodes on main', await p.evaluate(() => __studio.S.project.screens.find(s => s.id === 'main').nodes.map(n => n.id).join(',')));
  // assign hover/click sounds to the Play button via drag onto the field (and select)
  await p.click('.hrow:has-text("play")'); await p.waitForTimeout(200);
  await p.selectOption('select[aria-label="Hover sound"]', { index: 1 }); await p.selectOption('select[aria-label="Click sound"]', { index: 2 });
  console.log('play sounds', await p.evaluate(() => __studio.S.tags.get('main.play.hoverSound') + ' / ' + __studio.S.tags.get('main.play.clickSound')));
  await p.screenshot({ path: out + '_inspector.png' });
  // move btn.png into the new folder by drag & drop onto the folder in the tree
  await p.click('.trow:has-text("Images")'); await p.locator('.pit:has-text("btn.png")').dragTo(p.locator('.trow:has-text("Fonts & Co")')); await p.waitForTimeout(250);
  console.log('btn path', await p.evaluate(() => __studio.S.project.assets.find(a => a.name === 'btn.png').path));
  // save as .prime (download fallback)
  const [dl] = await Promise.all([p.waitForEvent('download'), menu('File', 'Save As')]); await dl.saveAs(SP + '/test.prime'); await p.waitForTimeout(300);
  console.log('downloaded', dl.suggestedFilename());
  await p.screenshot({ path: out + '_saved.png' });
  const before = await p.evaluate(() => JSON.stringify({ tags: Object.keys(__studio.S.project.tags).length, assets: __studio.S.project.assets.length, screens: __studio.S.project.screens.map(s => s.id + ':' + s.nodes.length) }));
  // back to wizard, delete project, reopen from file
  await p.click('button:has-text("← Projects")'); await p.waitForSelector('.wiz');
  await p.click('.tdel'); await p.click('.tdel'); await p.waitForTimeout(300);
  console.log('tiles after delete', await p.locator('.tile-open').count());
  [fc] = await Promise.all([p.waitForEvent('filechooser'), p.click('button:has-text("Open .prime file")')]); await fc.setFiles(SP + '/test.prime'); await p.waitForSelector('#app'); await p.waitForTimeout(700);
  const after = await p.evaluate(() => JSON.stringify({ tags: Object.keys(__studio.S.project.tags).length, assets: __studio.S.project.assets.length, screens: __studio.S.project.screens.map(s => s.id + ':' + s.nodes.length) }));
  console.log('round trip equal:', before === after, after);
  console.log('asset blobs readable', await p.evaluate(async () => { const P = __studio.S.project, b = await Promise.all(P.assets.map(a => __studio.Store.getAsset(P.id, a.id))); return b.every(x => x && x.size > 0); }));
  await p.click('.tab:has-text("Game")'); await p.waitForTimeout(400); await p.screenshot({ path: out + '_game.png' });
  await p.click('.tab:has-text("Scene")');
  await p.click('.mb:has-text("Window")'); await p.click('.pitem:has-text("Skin: Classic light")'); await p.waitForTimeout(300); await p.screenshot({ path: out + '_light.png' });
  await p.click('.tab:has-text("Console")'); await p.waitForTimeout(150); await p.screenshot({ path: out + '_console.png' });
};
