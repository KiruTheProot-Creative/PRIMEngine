const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.click('.mcard:has-text("DOOM")'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/assets_test/Images/logo.png']); await p.waitForTimeout(800);
  await p.click('.tab:has-text("Wall Editor")'); await p.waitForTimeout(400);
  await p.click('button:has-text("+ New wall")'); await p.waitForTimeout(500);
  const st = () => p.evaluate(() => { const w = __studio.S.project.walls[0]; return JSON.stringify({ faces: w.faces.length, verts: w.verts.length, sel: [...__studio.S.wed.sel.faces] }); });
  console.log('new wall', await st());
  const cb = await p.locator('canvas.v3cv').boundingBox(); const cx = cb.x + cb.width / 2, cy = cb.y + cb.height / 2;
  await p.evaluate(() => __studio.S.wed.view('front')); await p.waitForTimeout(300);
  await p.mouse.click(cx, cy); await p.waitForTimeout(300); console.log('after click', await st());
  // extrude by dragging (tool button), seen from the side so the drag has depth
  await p.click('.tbtn:has-text("Extrude")'); await p.evaluate(() => __studio.S.wed.view('iso')); await p.waitForTimeout(300);
  await p.mouse.move(cx, cy); await p.mouse.down(); await p.mouse.move(cx + 30, cy - 40, { steps: 6 }); await p.mouse.up(); await p.waitForTimeout(300); console.log('after extrude', await st());
  await p.click('.tbtn:has-text("Inset")'); await p.mouse.move(cx, cy); await p.mouse.down(); await p.mouse.move(cx + 60, cy, { steps: 5 }); await p.mouse.up(); await p.waitForTimeout(300); console.log('after inset', await st());
  await p.click('.tbtn:has-text("Extrude")'); await p.mouse.move(cx, cy); await p.mouse.down(); await p.mouse.move(cx + 30, cy - 40, { steps: 6 }); await p.mouse.up(); await p.waitForTimeout(300);
  await p.click('.tbtn:has-text("Cut ⬍")'); await p.waitForTimeout(200); console.log('after cut', await st());
  // texture
  await p.locator('.insp select').last().selectOption({ label: 'logo.png' }); await p.waitForTimeout(300);
  console.log('tex tag', await p.evaluate(() => { const w = __studio.S.project.walls[0], f = [...__studio.S.wed.sel.faces][0]; return __studio.S.tags.get(w.id + '.' + f + '.tex'); }));
  await p.screenshot({ path: out + '_ed.png' });
  await p.click('.tbtn:has-text("Undo")'); await p.waitForTimeout(200); console.log('after undo', await st());
  await p.click('.tbtn:has-text("Vertex")'); await p.mouse.click(cx, cy); await p.click('.tbtn:has-text("Select")'); await p.keyboard.press('a'); await p.waitForTimeout(200);
  console.log('vertex mode all', await p.evaluate(() => __studio.S.wed.sel.verts.size));
  await p.click('.tbtn:has-text("Face")'); await p.mouse.click(cx, cy); await p.keyboard.press('x'); await p.waitForTimeout(200); console.log('after delete', await st());
  await p.keyboard.press('Control+z'); await p.waitForTimeout(200); console.log('after ctrl+z', await st());
  // use it on a map wall
  await p.click('.tab:has-text("Map")'); await p.waitForTimeout(500);
  await p.evaluate(() => { const S = __studio.S, m = S.project.maps[0]; S.msel = { kind: 'vert', id: m.verts[0].id }; if (S.med) S.med.select({ kind: 'vert', id: m.verts[0].id }); });
  await p.waitForTimeout(400);
  console.log('layers box', await p.evaluate(() => /Wall layers/.test(document.body.innerText)));
  await p.click('button:has-text("+ Add layer")'); await p.waitForTimeout(200); await p.click('button:has-text("Copy to the whole room")'); await p.waitForTimeout(200);
  console.log('rows tags', await p.evaluate(() => __studio.S.project.maps[0].verts.map(v => __studio.S.tags.get('map1.' + v.id + '.rows')).join('|')));
  await p.screenshot({ path: out + '_map.png' });
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(400); await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.hudHost, null, { timeout: 8000 }); await p.waitForTimeout(1500);
  await p.evaluate(() => { const q = __studio.S.play; q.msg.style.display = 'none'; q.look = () => {}; q.yaw = 0; q.x = -40; });
  await p.waitForTimeout(500); await p.screenshot({ path: out + '_game.png' });
  console.log('prime roundtrip walls', await p.evaluate(async () => { const pr = __studio.S.project, b = await PRIM.Prime.pack(pr, {}).catch(e => null); return b ? 'packed ' + (b.length || b.byteLength) : 'pack n/a'; }));
  console.log('no console errors');
};
