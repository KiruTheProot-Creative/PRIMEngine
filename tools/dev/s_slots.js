// Save slot screen: Create Save Slot Screen (main Start button points at it), slots show place / play time / date / picture, click loads, hold R deletes.
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await p.evaluate(() => { const pid = __studio.S.project.id, c = document.createElement('canvas'); c.width = 192; c.height = 108; const g = c.getContext('2d'); const gr = g.createLinearGradient(0, 0, 192, 108); gr.addColorStop(0, '#234'); gr.addColorStop(1, '#a63'); g.fillStyle = gr; g.fillRect(0, 0, 192, 108);
    PRIM.Save.write(pid, 1, { v: 1, time: Date.now() - 3600e3, map: 'map1', name: 'Landing site', play: 8040, hp: 86, tanks: 2, shot: c.toDataURL('image/jpeg', 0.7), tags: {}, world: {} });
    PRIM.Save.write(pid, 2, { v: 1, time: Date.now(), map: 'map2', name: 'Chozo ruins', play: 754, hp: 40, tags: {}, world: {} }); PRIM.Save.erase(pid, 3); });
  await menu('Screen', 'Create Save Slot Screen'); await p.waitForTimeout(600);
  console.log('main start ->', await p.evaluate(() => { const T = __studio.S.tags, m = __studio.S.project.screens.find(s => s.id === 'main'), b = m.nodes.find(n => T.get('main.' + n.id + '.action') === 'goto'); return b && T.get('main.' + b.id + '.target'); }));
  await p.screenshot({ path: out + '_edit.png' });
  const r = await p.evaluate(async () => { const S = __studio.S, host = document.createElement('div'); host.style.cssText = 'position:fixed;left:0;top:0;width:1280px;height:720px;z-index:99'; document.body.append(host); let started = null;
    const rt = new PRIM.Runtime({ host, project: S.project, tags: S.tags.fork(), mode: 'test', urls: S.urls, screenId: S.screenId, onStartGame: (r, o) => { started = { slot: o.slot, load: !!o.load, name: o.load && o.load.name }; } });
    const sc = rt.screen(), sl = i => sc.nodes.filter(n => n.type === 'saveslot')[i], texts = sc.nodes.filter(n => n.type === 'saveslot').map(n => rt.els[n.id].innerText.replace(/\s+/g, ' '));
    // hold R on slot 2
    rt.hover = sl(1).id; rt.key({ key: 'r', repeat: false }); let t = performance.now(); for (let i = 0; i < 10; i++) { t += 50; rt.frame(t); } const mid = rt.delHold && +rt.delHold.k.toFixed(2); for (let i = 0; i < 20; i++) { t += 50; rt.frame(t); }
    const after = !!PRIM.Save.read(S.project.id, 2);
    // hold R on slot 1 but let go early
    rt.hover = sl(0).id; rt.key({ key: 'r', repeat: false }); t = performance.now(); for (let i = 0; i < 5; i++) { t += 50; rt.frame(t); } rt.keyUp({ key: 'r' }); for (let i = 0; i < 30; i++) { t += 50; rt.frame(t); } const kept = !!PRIM.Save.read(S.project.id, 1);
    rt.act(sc, sl(0)); const s1 = started; started = null; rt.act(sc, sl(2)); const s3 = started; cancelAnimationFrame(rt.raf);
    await new Promise(r => setTimeout(r, 300)); window.__shotHost = host; return { texts, mid, slot2Left: after, slot1Kept: kept, click1: s1, clickEmpty: s3 }; });
  console.log(JSON.stringify(r, null, 1));
  await p.screenshot({ path: out + '_play.png' });
};
