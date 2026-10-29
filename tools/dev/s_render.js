// Renderer: HDR + bloom pipeline, real-time shadows from a lamp object (cube shadow maps), settings on/off.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.evaluate(() => { window.__noPost = false; });
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles(SP + '/cube_bin.fbx'); await p.waitForTimeout(1500);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  await p.evaluate(() => { const S = __studio.S, pr = S.project, G = PRIM.Grid, m = pr.maps[0], cube = pr.assets.find(a => a.type === 'model').id;
    // a dark room with two pillars; a lamp between them, a glowing crystal
    G.edit(m, 0, 23, 22).w = ''; G.edit(m, 0, 25, 23).w = ''; for (const k in m.layers[0].cells) m.layers[0].cells[k].li = 25;
    const lamp = PRIM.Schema.addProp(pr, { name: 'Lamp', type: 'decoration', asset: cube, scale: 0.15, lightOn: true, lightColor: '#ffb070', lightIntensity: 2.6, lightRange: 700, lightHeight: 60 });
    const gem = PRIM.Schema.addProp(pr, { name: 'Gem', type: 'decoration', asset: cube, scale: 0.25, matEmit: 3 });
    m.things = [{ id: 't1', prop: lamp.id, x: 24 * 128 + 64, y: 23 * 128, ang: 0, layer: 0 }, { id: 't2', prop: gem.id, x: 22 * 128 + 64, y: 25 * 128 + 64, ang: 0, layer: 0 }]; m.n.t = 2;
    S.tags.setMany({ 'map1.startX': 24 * 128 + 64, 'map1.startY': 26 * 128 + 64, 'map1.startAngle': 270, 'map1.fogOn': false }); });
  await p.click('button:has-text("Walk this map")'); await p.waitForTimeout(2500);
  await p.evaluate(() => { const q = __studio.S.play; q.msg.style.display = 'none'; q.look = () => {}; q.pitch = -0.1; });
  await p.waitForTimeout(1500); await p.screenshot({ path: out + '_on.png' });
  console.log('state', await p.evaluate(() => { const v = __studio.S.play.view; return JSON.stringify({ post: v.post, hdr: v.hdr, shadowsDrawn: Object.keys(v.shMaps || {}).length, lights: (v.lights || []).length, casters: (v.casters || []).length, err: v.gl.getError() }); }));
  await p.evaluate(() => { const T = __studio.S.play.T; T.set('gfx.shadows', false); }); await p.waitForTimeout(1200); await p.screenshot({ path: out + '_noshadow.png' });
  console.log('shadows off ->', await p.evaluate(() => __studio.S.play.view.shSlots.length));
  await p.evaluate(() => { const T = __studio.S.play.T; T.set('gfx.shadows', true); T.set('gfx.bloom', false); }); await p.waitForTimeout(1200); await p.screenshot({ path: out + '_nobloom.png' });
  console.log('gl error', await p.evaluate(() => __studio.S.play.view.gl.getError()));
  await p.keyboard.press('Escape');
};
