// Screen editor media: prologue screen (scroller + typer, typing sound, then -> screen), cutscene screen (video ends -> next screen, skip), sound element on a menu.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/clip.webm', SP + '/beep.wav']); await p.waitForTimeout(1500);
  console.log('asset types', await p.evaluate(() => JSON.stringify(__studio.S.project.assets.map(a => a.type))));
  await menu('Screen', 'Create Prologue Screen'); await p.waitForTimeout(500);
  await p.screenshot({ path: out + '_pro_edit.png' });
  const sid = await p.evaluate(() => __studio.S.screenId); console.log('prologue screen', sid);
  // scroller in a test runtime
  const run = (fn) => p.evaluate(fn);
  await p.evaluate(sid => { const T = __studio.S.tags; T.set(sid + '.speed', 400); T.set(sid + '.endHold', 0.2); T.set(sid + '.next', 'main'); }, sid);
  const res = await p.evaluate(async sid => { const S = __studio.S, host = document.createElement('div'); host.style.cssText = 'position:fixed;left:0;top:0;width:640px;height:360px;z-index:99'; document.body.append(host);
    const rt = new PRIM.Runtime({ host, project: S.project, tags: S.tags.fork(), mode: 'test', urls: S.urls, screenId: sid }); const shown = []; const orig = rt.show.bind(rt); rt.show = (id, b) => { shown.push(id); return orig(id, b); };
    const st = []; let t = performance.now(); for (let i = 0; i < 400 && rt.screenId === sid; i++) { t += 33; cancelAnimationFrame(rt.raf); rt.frame(t); if (i === 30) st.push(rt.pro && rt.pro.crawl.style.top); }
    cancelAnimationFrame(rt.raf); const r = { mid: st, landed: rt.screenId, shown }; rt.destroy(); host.remove(); return r; }, sid);
  console.log('scroller:', JSON.stringify(res));
  // typer with sound
  await p.evaluate(sid => { const T = __studio.S.tags, snd = __studio.S.project.assets.find(a => a.type === 'sound').id; T.set(sid + '.style', 'typer'); T.set(sid + '.cps', 60); T.set(sid + '.pageHold', 0.3); T.set(sid + '.typeSound', snd); T.set(sid + '.text', 'Page one line.\nSecond line.\n\nPage two.'); }, sid);
  await p.evaluate(() => __studio.renderInspector && 0); await p.waitForTimeout(300); await p.screenshot({ path: out + '_typer_edit.png' });
  const r2 = await p.evaluate(async sid => { const S = __studio.S, host = document.createElement('div'); host.style.cssText = 'position:fixed;left:0;top:0;width:640px;height:360px;z-index:99'; document.body.append(host); let sounds = 0; const op = PRIM.Audio.play; PRIM.Audio.play = function () { sounds++; return { stop() {}, setVolume() {}, playing: true }; };
    const rt = new PRIM.Runtime({ host, project: S.project, tags: S.tags.fork(), mode: 'test', urls: S.urls, screenId: sid }); const seen = []; let t = performance.now();
    for (let i = 0; i < 300 && rt.screenId === sid; i++) { t += 33; cancelAnimationFrame(rt.raf); rt.frame(t); if (rt.pro && rt.pro.ty && i % 10 === 5) seen.push(rt.pro.pi + ':' + (rt.pro.ty.firstChild ? rt.pro.ty.firstChild.textContent.length : 0)); }
    cancelAnimationFrame(rt.raf); PRIM.Audio.play = op; const r = { seen: seen.join(' '), landed: rt.screenId, sounds }; rt.destroy(); host.remove(); return r; }, sid);
  console.log('typer:', JSON.stringify(r2));
  // skip
  const r3 = await p.evaluate(async sid => { const S = __studio.S, host = document.createElement('div'); host.style.cssText = 'position:fixed;left:0;top:0;width:640px;height:360px;z-index:99'; document.body.append(host);
    const rt = new PRIM.Runtime({ host, project: S.project, tags: S.tags.fork(), mode: 'test', urls: S.urls, screenId: sid }); rt.key({ key: ' ' }); const r = rt.screenId; rt.destroy(); host.remove(); return r; }, sid);
  console.log('skip ->', r3);
  // cutscene with the webm
  await menu('Screen', 'Create Cutscene Screen'); await p.waitForTimeout(800);
  const cs = await p.evaluate(() => __studio.S.screenId); await p.screenshot({ path: out + '_cut_edit.png' });
  const r4 = await p.evaluate(async cs => { const S = __studio.S, host = document.createElement('div'); host.style.cssText = 'position:fixed;left:0;top:0;width:640px;height:360px;z-index:99'; document.body.append(host);
    const rt = new PRIM.Runtime({ host, project: S.project, tags: S.tags.fork(), mode: 'test', urls: S.urls, screenId: cs }); const v = rt.vids && Object.values(rt.vids)[0]; await new Promise(r => setTimeout(r, 3500)); const r = { hadVideo: !!v, err: v && v.error && v.error.code, t: v && +v.currentTime.toFixed(2), landed: rt.screenId }; rt.destroy(); host.remove(); return r; }, cs);
  console.log('cutscene plays to the end:', JSON.stringify(r4));
  const r5 = await p.evaluate(async cs => { const S = __studio.S, host = document.createElement('div'); host.style.cssText = 'position:fixed;left:0;top:0;width:640px;height:360px;z-index:99'; document.body.append(host);
    const rt = new PRIM.Runtime({ host, project: S.project, tags: S.tags.fork(), mode: 'test', urls: S.urls, screenId: cs }); await new Promise(r => setTimeout(r, 300)); rt.key({ key: 'Enter' }); const r = rt.screenId; rt.destroy(); host.remove(); return r; }, cs);
  console.log('cutscene skipped ->', r5);
  // a sound element on the main menu
  const r6 = await p.evaluate(async () => { const S = __studio.S, Sc = PRIM.Schema, sc = S.project.screens.find(s => s.id === 'main'), snd = S.project.assets.find(a => a.type === 'sound').id, n = { id: 'snd1', type: 'sound' }; sc.nodes.push(n); Sc.defineNode(S.project, 'main', n, { asset: snd, delay: 0.2, loop: true });
    const host = document.createElement('div'); host.style.cssText = 'position:fixed;left:0;top:0;width:640px;height:360px;z-index:99'; document.body.append(host); const calls = []; const op = PRIM.Audio.play; PRIM.Audio.play = function (u, v, loop) { const h = { stopped: false, stop() { h.stopped = true; }, setVolume() {}, playing: true }; calls.push([!!loop, h]); return h; };
    const rt = new PRIM.Runtime({ host, project: S.project, tags: S.tags.fork(), mode: 'test', urls: S.urls, screenId: 'main' }); await new Promise(r => setTimeout(r, 400)); const started = calls.length; rt.show('settings'); const stopped = calls.every(c => c[1].stopped); rt.destroy(); host.remove(); PRIM.Audio.play = op; return { started, loop: calls[0] && calls[0][0], stopped }; });
  console.log('screen sound:', JSON.stringify(r6));
};
