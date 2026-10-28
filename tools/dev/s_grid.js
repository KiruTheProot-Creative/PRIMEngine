// Grid map editor: paint walls / floors / heights / slopes / stairs, stories, room tool, objects, start; fly view; walking up stairs to story 2; fog + mood.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(400);
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Folder')]); await fc.setFiles(SP + '/assets_test'); await p.waitForTimeout(700);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv'); await p.waitForTimeout(500);
  const S = () => '__studio.S';
  const info = () => p.evaluate(() => { const m = __studio.S.project.maps[0]; return { layers: m.layers.length, cells: m.layers.map(l => Object.keys(l.cells).length), start: [__studio.S.tags.get('map1.startX'), __studio.S.tags.get('map1.startY')] }; });
  console.log('new map', JSON.stringify(await info()));
  await p.screenshot({ path: out + '_g1.png' });
  const cellPt = (cx, cy) => p.evaluate(([cx, cy]) => { const e = __studio.S.med, r = e.cv.getBoundingClientRect(); return [r.left + e.sx((cx + 0.5) * 128), r.top + e.sy((cy + 0.5) * 128)]; }, [cx, cy]);
  const clickC = async (cx, cy, o) => { const [a, b] = await cellPt(cx, cy); await p.mouse.click(a, b, o); };
  const dragC = async (a0, b0, a1, b1, button) => { const [x0, y0] = await cellPt(a0, b0), [x1, y1] = await cellPt(a1, b1); await p.mouse.move(x0, y0); await p.mouse.down({ button: button || 'left' }); await p.mouse.move((x0 + x1) / 2, (y0 + y1) / 2, { steps: 4 }); await p.mouse.move(x1, y1, { steps: 4 }); await p.mouse.up({ button: button || 'left' }); };
  const cell = (L, x, y) => p.evaluate(([L, x, y]) => JSON.stringify(PRIM.Grid.cell(__studio.S.project.maps[0], L, x, y)), [L, x, y]);
  // pick a texture, paint a wall line inside the room
  await p.click('.mswatch:has-text("btn")'); await p.focus('.med'); await p.keyboard.press('w');
  await dragC(21, 22, 23, 22); await p.waitForTimeout(100);
  console.log('wall 22,22', await cell(0, 22, 22));
  await clickC(22, 22, { button: 'right' }); console.log('after right-click', await cell(0, 22, 22));
  // heights: H tool at 8/16
  await p.keyboard.press('h'); await p.evaluate(() => { __studio.S.med.s.hgt = 8; }); await clickC(25, 25); console.log('height', await cell(0, 25, 25));
  // stairs rising east, full story, at 26,22 ; then story 2 floor at 27..28,22
  await p.keyboard.press('t'); await p.evaluate(() => { const s = __studio.S.med.s; s.dir = 1; s.rise = 16; }); await clickC(26, 22); console.log('stairs', await cell(0, 26, 22));
  await p.keyboard.press('s'); await p.evaluate(() => { const s = __studio.S.med.s; s.dir = 2; s.rise = 8; }); await clickC(21, 26); console.log('slope', await cell(0, 21, 26));
  // two ramps in a row: the second one starts where the first ends (automatic start height), then one set to start at 12/16
  await p.evaluate(() => { const s = __studio.S.med.s; s.dir = 1; s.rise = 8; s.base = -1; }); await clickC(21, 24); await clickC(22, 24);
  await p.evaluate(() => { __studio.S.med.s.base = 12; }); await clickC(23, 24); await p.evaluate(() => { __studio.S.med.s.base = -1; });
  console.log('ramps in a row', await cell(0, 21, 24), await cell(0, 22, 24), await cell(0, 23, 24));
  // remove the ceiling above the stairs and the cell after, go up a story and lay a floor
  await p.keyboard.press('c'); await dragC(26, 22, 27, 22, 'right');
  await p.keyboard.press('PageUp'); await p.keyboard.press('f'); await dragC(27, 22, 27, 24); await p.waitForTimeout(100);
  console.log('story2', JSON.stringify(await info()), await cell(1, 27, 23));
  await p.screenshot({ path: out + '_g2.png' });
  await p.keyboard.press('PageDown');
  // room tool: a new room east
  await p.keyboard.press('r'); await dragC(29, 19, 34, 24); console.log('room', JSON.stringify(await info()), await cell(0, 29, 20), await cell(0, 31, 21));
  // undo / redo
  await p.keyboard.press('Control+z'); const u = JSON.stringify(await info()); await p.keyboard.press('Control+y'); console.log('undo', u, 'redo', JSON.stringify(await info()));
  // diagonal walls from the Rooms panel: a 45° one and a 2-cell 26° slant
  await p.click('.mpiece:has-text("Diagonal")'); await p.click('.mpshape >> nth=0'); await clickC(21, 21);
  await p.click('.mpshape >> nth=1'); await p.keyboard.press('d'); await clickC(26, 26); await p.waitForTimeout(100);
  console.log('diag', await cell(0, 21, 21), await cell(0, 26, 26), await cell(0, 26, 27));
  await p.screenshot({ path: out + '_diag.png' });
  await p.click('.mptab:has-text("Objects")'); console.log('objects tab', await p.locator('.medpal').innerText());
  await p.click('.mptab:has-text("Rooms")');
  // select tool + inspector
  await p.keyboard.press('v'); await dragC(23, 23, 24, 24); await p.waitForTimeout(200);
  console.log('inspector', await p.locator('.insp .idin').first().textContent());
  // player start
  await p.keyboard.press('p'); await clickC(22, 24); console.log('start', JSON.stringify((await info()).start));
  await p.screenshot({ path: out + '_g3.png' });
  // fog + mood on the map
  await p.evaluate(() => { const T = __studio.S.tags; T.setMany({ 'map1.mood': 'dusk', 'map1.fogStart': 200, 'map1.fogEnd': 1500 }); });
  // fly editor: hit a face and texture it
  await p.click('.tab:has-text("Fly Editor")'); await p.waitForTimeout(1200);
  console.log('3d', await p.evaluate(() => { const v = __studio.S.v3; return v ? 'tris ' + v.triCount + ' fog ' + v.fogS + '-' + v.fogE + ' mood ' + JSON.stringify(v.mood) : 'NO VIEW'; }));
  const box = await p.locator('.v3cv').boundingBox(); await p.mouse.click(box.x + box.width / 2, box.y + box.height / 2); await p.waitForTimeout(300);
  console.log('fly sel', await p.evaluate(() => JSON.stringify(__studio.S.fsl)));
  await p.screenshot({ path: out + '_g4.png' });
  // walk: put the player in front of the stairs, walk east up them and onto story 2
  await p.click('.tab:has-text("Map")'); await p.waitForTimeout(300);
  await p.evaluate(() => { const T = __studio.S.tags; T.setMany({ 'map1.startX': 25 * 128 + 64, 'map1.startY': 22 * 128 + 64, 'map1.startAngle': 0, 'map1.startLayer': 0 }); });
  await p.click('button:has-text("Walk this map")'); await p.waitForTimeout(800);
  await p.mouse.click(700, 400); await p.waitForTimeout(200);
  const pos = () => p.evaluate(() => { const q = __studio.S.play; return [Math.round(q.x), Math.round(q.y), Math.round(q.z), q.ground]; });
  // drive the physics directly (the headless browser draws few frames): hold W for some steps at 60 fps
  const walk = (x, y, z, yaw, secs) => p.evaluate(([x, y, z, yaw, secs]) => { const q = __studio.S.play; q.body = null; q.x = x; q.y = y; q.z = z; q.yaw = yaw; q.ground = true; q.keys.w = true; const tr = []; for (let i = 0; i < secs * 60; i++) { q.step(1 / 60); if (i % 30 === 29) tr.push([Math.round(q.x), Math.round(q.y), Math.round(q.z)]); } q.keys.w = false; return JSON.stringify(tr); }, [x, y, z, yaw, secs]);
  console.log('cells', await cell(0, 26, 22), await cell(0, 27, 22), await cell(1, 27, 22), await p.evaluate(() => JSON.stringify(__studio.S.play.keys) + ' tris ' + __studio.S.play.view.world.list.length));
  console.log('up the stairs (east):', await walk(25 * 128 + 64, 22 * 128 + 64, 0, 0, 2.5));
  console.log('into the south wall:', await walk(22 * 128 + 64, 25 * 128 + 64, 0, Math.PI / 2, 2), '(wall face at y', 28 * 128, ')');
  console.log('up the 8/16 step (25,25) from the west:', await walk(23 * 128 + 64, 25 * 128 + 64, 0, 0, 1));
  console.log('into the 45° diagonal at (21,21), going north-west:', await walk(22 * 128 + 64, 22 * 128 + 64, 0, -3 * Math.PI / 4, 1.5));
  console.log('down the slope (21,26) going south:', await walk(21 * 128 + 64, 25 * 128 + 64, 0, Math.PI / 2, 1.2));
  await p.keyboard.press('Escape'); await p.waitForTimeout(300);
};
