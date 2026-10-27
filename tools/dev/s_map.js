const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(400);
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Folder')]); await fc.setFiles(SP + '/assets_test'); await p.waitForTimeout(700);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.mcard');
  console.log('modern disabled:', await p.locator('.mcard:has-text("Modern")').isDisabled());
  await p.screenshot({ path: out + '_newmap.png' });
  await p.click('.mcard:has-text("DOOM")'); await p.waitForSelector('.medcv'); await p.waitForTimeout(500);
  const pt = (x, y) => p.evaluate(([x, y]) => { const e = __studio.S.med, r = e.cv.getBoundingClientRect(); return [r.left + e.sx(x), r.top + e.sy(y)]; }, [x, y]);
  const clickW = async (x, y, o) => { const [a, b] = await pt(x, y); await p.mouse.click(a, b, o); };
  await p.screenshot({ path: out + '_map1.png' });
  // select a node, give its wall a texture
  await clickW(128, -128); await p.waitForTimeout(150);
  console.log('sel', await p.evaluate(() => JSON.stringify(__studio.S.msel)));
  await p.selectOption('select[aria-label="Wall texture"]', { index: 1 }); await p.waitForTimeout(150);
  console.log('tex tag', await p.evaluate(() => __studio.S.tags.get('map1.v2.tex')));
  // draw a second room to the right: key d, click 4 points, click first
  await p.focus('.med'); await p.keyboard.press('d');
  for (const [x, y] of [[160, -64], [288, -64], [288, 96], [160, 96]]) await clickW(x, y);
  await clickW(160, -64); await p.waitForTimeout(200);
  console.log('rooms', await p.evaluate(() => __studio.S.project.maps[0].sectors.length + ' verts ' + __studio.S.project.maps[0].verts.length));
  // double-click a wall of room 2 to insert a node
  const [mx, my] = await pt(224, -64); await p.mouse.dblclick(mx, my); await p.waitForTimeout(150);
  console.log('verts after insert', await p.evaluate(() => __studio.S.project.maps[0].verts.length));
  // open the doorway: set first room wall at (128,128)->(-128,128)? just open the wall v2 (right wall of room 1)
  await p.evaluate(() => { const T = __studio.S.tags; T.set('map1.s2.floorColor', '#7a5a3a'); T.set('map1.s2.ceil', 192); T.set('map1.s1.floorTex', __studio.S.project.assets.find(a => a.type === 'image').id); });
  await p.screenshot({ path: out + '_map2.png' });
  // drop an image onto a wall
  await p.click('.trow:has-text("Images")'); await p.waitForTimeout(150);
  const [dx, dy] = await pt(-128, 0); const box = await p.locator('.medcv').boundingBox();
  await p.locator('.pit:has-text("btn.png")').dragTo(p.locator('.medcv'), { targetPosition: { x: dx - box.x, y: dy - box.y } }); await p.waitForTimeout(250);
  console.log('dropped on wall -> v1 tex', await p.evaluate(() => __studio.S.tags.get('map1.v1.tex') + ' | v4 ' + __studio.S.tags.get('map1.v4.tex')));
  // 3D tab
  await p.click('.tab:has-text("3D Fly")'); await p.waitForTimeout(1200);
  console.log('3d', await p.evaluate(() => __studio.S.v3 ? 'tris ' + __studio.S.v3.triCount + ' frames ' + __studio.S.v3.frames : 'NO VIEW'));
  await p.screenshot({ path: out + '_3d.png' });
  // look/move with keys
  await p.mouse.click(600, 300); await p.keyboard.down('d'); await p.waitForTimeout(500); await p.keyboard.up('d'); await p.keyboard.down('w'); await p.waitForTimeout(500); await p.keyboard.up('w');
  console.log('cam', await p.evaluate(() => JSON.stringify(__studio.S.v3.cam)));
  await p.screenshot({ path: out + '_3d_moved.png' });
  // fullscreen fly and leave with Esc
  await p.evaluate(() => document.exitPointerLock()); await p.click('button:has-text("Maximize on Play")'); await p.waitForTimeout(700); await p.screenshot({ path: out + '_fly.png' });
  await p.keyboard.press('Escape'); await p.waitForTimeout(200);
  console.log('layer after esc', await p.locator('.play-layer').count());
  // the game's Play button starts the map
  await p.click('.hrow:has-text("main")'); await p.click('.tab:has-text("Game")'); await p.waitForTimeout(500);
  const [bx, by] = await p.evaluate(() => { const b = document.querySelector('.rt-stage [data-id="play"], .rt-stage .rt-btn'); const r = b.getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; }).catch(() => [0, 0]);
  console.log('play btn at', bx, by);
  // persistence: pack and unpack
  const ok = await p.evaluate(async () => { const S = __studio.S; const { bytes } = await PRIM.Prime.pack(S.project, async () => null); const r = await PRIM.Prime.unpack(bytes); return r.project.maps.length + ' map, ' + r.project.maps[0].sectors.length + ' rooms, tag ' + r.project.tags['map1.s2.ceil'].value; });
  console.log('prime roundtrip:', ok);
  await p.screenshot({ path: out + '_end.png' });
};
