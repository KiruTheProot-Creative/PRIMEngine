const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/beep.wav']); await p.waitForTimeout(800);
  await p.addInitScript(() => {});
  await p.evaluate(() => { window.__au = []; const P = PRIM.Audio.play; PRIM.Audio.play = (u, v, loop) => { const h = P(u, v, loop); window.__au.push('play loop=' + !!loop); const st = h.stop.bind(h); h.stop = f => { window.__au.push('stop loop=' + !!loop); return st(f); }; return h; }; });
  await p.evaluate(() => { const S = __studio.S, Sc = PRIM.Schema, pr = S.project, sc = pr.screens.find(s => s.id === 'main'), n = sc.nodes.find(x => x.type === 'title'), key = sc.id + '.' + n.id, snd = pr.assets.find(a => a.type === 'sound').id;
    n.script = { nodes: [], links: [] }; const a = Sc.addSNode(pr, key, n.script, 'onStart', 0, 0), b = Sc.addSNode(pr, key, n.script, 'loopSound', 200, 0, { sound: snd, volume: 0.5 }), c = Sc.addSNode(pr, key, n.script, 'loopSound', 400, 0, { sound: snd, volume: 0.5 }), d = Sc.addSNode(pr, key, n.script, 'wait', 600, 0, { seconds: 0.4 }), e = Sc.addSNode(pr, key, n.script, 'stopLoop', 800, 0, { sound: '' });
    n.script.links.push({ a: a.id, o: 'next', b: b.id }, { a: b.id, o: 'next', b: c.id }, { a: c.id, o: 'next', b: d.id }, { a: d.id, o: 'next', b: e.id }); S.tags.emit([]); });
  console.log('node types', await p.evaluate(() => [PRIM.Schema.SNODES.loopSound.label, PRIM.Schema.SNODES.stopLoop.label].join(' | ')));
  await p.click('.tab:has-text("Game")'); await p.waitForTimeout(1500);
  console.log('audio events', await p.evaluate(() => JSON.stringify(window.__au)));
  console.log('no console errors');
};
