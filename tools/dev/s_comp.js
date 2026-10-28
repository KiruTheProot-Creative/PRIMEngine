// Compositor tab: add effects from the menu, every shader compiles, order / on-off / remove, the preview uses them, and so does the game.
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  await p.click('.tab:has-text("Compositor")'); await p.waitForTimeout(1200);
  const add = async name => { await p.click('.cbar button:has-text("+ Add effect")').catch(() => p.click('button:has-text("+ Add effect") >> nth=0')); await p.click('.pop .pitem:has-text("' + name + '")'); await p.waitForTimeout(500); };
  console.log('menu', await (async () => { await p.click('button:has-text("+ Add effect") >> nth=0'); const t = await p.locator('.pop .pitem').allTextContents(); await p.keyboard.press('Escape'); await p.mouse.click(5, 5); return t.join(' | '); })());
  // every effect compiles
  console.log('shaders', await p.evaluate(() => { const v = __studio.S.v3; return v ? JSON.stringify(Object.keys(PRIM.Schema.COMPFX).map(k => [k, !!v.fxProg(k)])) : 'no view'; }));
  await add('Pixelate'); await add('Fewer colors'); await add('CRT screen'); await add('Vignette');
  console.log('chain', await p.locator('.compstrip .cfx').allTextContents());
  await p.evaluate(() => { const T = __studio.S.tags, L = __studio.S.project.compositor.effects; T.set('comp.' + L[0].id + '.size', 4); T.set('comp.' + L[1].id + '.levels', 6); });
  await p.waitForTimeout(1500); await p.screenshot({ path: out + '_chain.png' });
  console.log('inspector', (await p.locator('.insp').innerText()).split('\n').slice(0, 8).join(' / '));
  // move CRT before pixelate, switch vignette off, remove "fewer colors"
  await p.click('.compstrip .cfx.fx:has-text("CRT") button:has-text("◀")'); await p.click('.compstrip .cfx.fx:has-text("CRT") button:has-text("◀")');
  await p.locator('.compstrip .cfx.fx:has-text("Vignette") input[type=checkbox]').uncheck();
  await p.click('.compstrip .cfx.fx:has-text("Fewer colors") button:has-text("✕")'); await p.waitForTimeout(300);
  console.log('after edits', await p.evaluate(() => JSON.stringify(__studio.S.project.compositor.effects.map(e => [e.type, __studio.S.tags.get('comp.' + e.id + '.on')]))), 'active', await p.evaluate(() => JSON.stringify(PRIM.Schema.compActive(__studio.S.project, __studio.S.tags).map(e => e.type))));
  await add('Night vision'); await p.waitForTimeout(1500); await p.screenshot({ path: out + '_night.png' });
  // pixel check: the preview really changes when compare is held
  const px = () => p.evaluate(() => { const cv = document.querySelector('.comppv canvas'), g = cv.getContext('webgl', { preserveDrawingBuffer: true }) || cv.getContext('webgl'); const d = new Uint8Array(4); g.readPixels(cv.width >> 1, cv.height >> 2, 1, 1, g.RGBA, g.UNSIGNED_BYTE, d); return Array.from(d.slice(0, 3)); });
  await p.waitForTimeout(800); const a = await px(); await p.evaluate(() => { __studio.S.compOff = true; }); await p.waitForTimeout(1500); const b = await px(); await p.evaluate(() => { __studio.S.compOff = false; });
  console.log('pixel with effects', JSON.stringify(a), 'without', JSON.stringify(b));
  // survives save / load (json) and the game uses it
  console.log('saved', await p.evaluate(() => { const j = JSON.parse(JSON.stringify(__studio.S.project)); return JSON.stringify(j.compositor.effects.map(e => e.type)) + ' tags ' + Object.keys(j.tags).filter(k => k.startsWith('comp.')).length; }));
  // layers: vignette (switched back on) + grading in front of the HUD
  await p.evaluate(() => { const T = __studio.S.tags, L = __studio.S.project.compositor.effects, id = t => L.find(e => e.type === t).id; T.set('comp.' + id('vignette') + '.on', true); T.set('comp.' + id('vignette') + '.layer', 'front'); T.set('comp.' + id('vignette') + '.strength', 0.9); T.set('comp.' + id('night') + '.on', false); const g = PRIM.Schema.addComp(__studio.S.project, 'grade'); T.set('comp.' + g.id + '.layer', 'front'); T.set('comp.' + g.id + '.saturation', 0); const c = PRIM.Schema.addComp(__studio.S.project, 'crt'); T.set('comp.' + c.id + '.layer', 'front'); T.set('comp.' + c.id + '.lines', 0.6); T.set('comp.' + c.id + '.count', 120); T.emit([]); });
  await p.evaluate(() => { window.__noPost = false; let n = 0; const o = PRIM.R3D.View.prototype.fxProg; window.__fxRuns = 0; const pr = PRIM.R3D.View.prototype.present; PRIM.R3D.View.prototype.present = function () { if (this.post && PRIM.Schema.compActive(this.project, this.tags).length) window.__fxRuns++; return pr.call(this); }; });
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(300); await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.hud, null, { timeout: 8000 }); await p.waitForTimeout(2500);
  console.log('game frames with effects', await p.evaluate(() => window.__fxRuns > 0));
  console.log('front of HUD', await p.evaluate(() => { const q = __studio.S.play; return JSON.stringify({ overlays: q.fxo ? q.fxo.children.length : 0, hudFilter: q.hudHost.style.filter, xhFilter: q.xh.style.filter, svgPrims: q.fxo && q.fxo.querySelector('filter') ? q.fxo.querySelector('filter').children.length : 0, domFx: q.view.domFx }); }));
  await p.screenshot({ path: out + '_game.png', timeout: 60000 }).catch(() => console.log('(game screenshot skipped)'));
  await p.keyboard.press('Escape');
};
