// Wall impacts: a hitscan weapon and a particle weapon hit the wall -> hit sound + a mark that animates, stays, fades.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Folder')]); await fc.setFiles(SP + '/assets_test'); await p.waitForTimeout(700);
  [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles(SP + '/Arm_canon.fbx'); await p.waitForTimeout(1500);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(600);
  await p.click('button:has-text("New weapon")'); await p.click('.pitem:has-text("Arm_canon")'); await p.waitForTimeout(1500);
  const ids = await p.evaluate(() => { const S = __studio.S, A = S.project.assets, img = A.find(a => a.name === 'logo.png').id, snd = A.find(a => a.type === 'sound').id;
    S.tags.setMany({ 'player.att.projType': 'hitscan', 'player.att.impactImage': img, 'player.att.impactSound': snd, 'player.att.impactFrames': 4, 'player.att.impactFps': 8, 'player.att.impactSize': 40, 'player.att.impactStay': 20 }); return { img, snd }; });
  await p.waitForTimeout(300); await p.screenshot({ path: out + '_insp.png' });
  console.log('fields shown', await p.evaluate(() => [...document.querySelectorAll('.insp .tn')].map(e => e.textContent).filter(t => /impact/.test(t)).join(' ')));
  await p.click('.tab:has-text("Map")'); await p.waitForTimeout(300); await p.click('button:has-text("Walk this map")'); await p.waitForTimeout(1200);
  const r = await p.evaluate(() => { const q = __studio.S.play, played = []; const orig = PRIM.Audio.play; PRIM.Audio.play = (u, v, l) => { played.push(+v.toFixed(2)); return orig(u, v, l); };
    q.msg.style.display = 'none'; q.look = () => {}; q.yaw = 0; q.pitch = -0.2; q.drawHand(q.view, 0.016); const at = q.atts[0]; q.fireProj(at);
    const m = q.marks || []; return { marks: m.length, at: m[0] && m[0].p.map(Math.round), n: m[0] && m[0].n.map(x => +x.toFixed(2)), played }; });
  console.log('hitscan', JSON.stringify(r));
  await p.evaluate(() => { const q = __studio.S.play, m = q.marks[0]; q.body = null; q.x = m.p[0]; q.y = m.p[2] - 200; q.z = 0; q.yaw = Math.PI / 2; q.pitch = -0.1; q.T.set('player.att.visible', false); });
  await p.waitForTimeout(700); await p.screenshot({ path: out + '_mark.png' });
  console.log('frames over time', await p.evaluate(async () => { const q = __studio.S.play, f = []; for (let i = 0; i < 6; i++) { q.stepMarks(0.15); f.push(q.marks.length ? q.marks[0].frame + '/' + q.marks[0].alpha.toFixed(2) : 'gone'); } for (let i = 0; i < 10; i++) q.stepMarks(0.15); f.push(q.marks.length ? 'still' : 'gone'); return f.join(' '); }));
  // particle projectile
  console.log('particle', await p.evaluate(() => { const q = __studio.S.play, T = q.T; T.set('player.att.projType', 'particle'); T.set('player.att.projSpeed', 3000); q.marks = []; q.fireProj(q.atts[0]); for (let i = 0; i < 60; i++) q.stepFx(1 / 60); return (q.marks || []).length + ' mark(s), fx left ' + q.fx.length; }));
  await p.keyboard.press('Escape');
};
