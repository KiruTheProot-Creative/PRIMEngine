// Bug: after placing an object, D (turn) kept turning that object instead of the diagonal / slope / stairs piece.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/cube_bin.fbx']); await p.waitForTimeout(1200);
  await p.evaluate(() => { const S = __studio.S; PRIM.Schema.addProp(S.project, { name: 'Box', type: 'decoration', asset: S.project.assets.find(a => /cube/.test(a.id)).id }); S.tags.emit([]); });
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv'); await p.waitForTimeout(400);
  const cellPt = (cx, cy) => p.evaluate(([cx, cy]) => { const e = __studio.S.med, r = e.cv.getBoundingClientRect(); return [r.left + e.sx((cx + 0.5) * 128), r.top + e.sy((cy + 0.5) * 128)]; }, [cx, cy]);
  const st = () => p.evaluate(() => { const e = __studio.S.med; return JSON.stringify({ tool: e.s.tool, sel: e.sel && e.sel.kind, thingAng: (e.m.things || []).map(t => t.ang || 0), diagRot: e.s.rot, slopeDir: e.s.dir }); });
  // place an object with the Objects panel
  await p.click('.mptab:has-text("Objects")'); await p.click('.medpal >> text=Box'); const [x, y] = await cellPt(23, 23); await p.mouse.click(x, y); await p.waitForTimeout(200);
  console.log('placed object:', await st());
  await p.focus('.med'); await p.keyboard.press('d'); console.log('D with Object tool (turns the object):', await st());
  // now a diagonal piece from the Rooms panel, press D
  await p.click('.mptab:has-text("Rooms")'); await p.click('.mpiece:has-text("Diagonal")'); await p.focus('.med'); await p.keyboard.press('d'); await p.keyboard.press('d');
  console.log('Diagonal tool, D twice (turns the piece, object stays):', await st());
  await p.click('.mpiece:has-text("Slope")'); await p.focus('.med'); await p.keyboard.press('d'); console.log('Slope tool, D:', await st());
  await p.click('.mpiece:has-text("Diagonal")'); await p.click('.medbar button:has-text("Turn")').catch(() => p.click('button:has-text("⟳ Turn")')); console.log('Turn button on the diagonal toolbar:', await st());
};
