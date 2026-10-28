const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/padloop.wav']); await p.waitForTimeout(800);
  await p.mouse.click(5, 5);
  const r = await p.evaluate(async () => { const S = __studio.S, a = S.project.assets.find(x => x.type === 'sound'), u = S.urls[a.id], d = await PRIM.Audio.load(u);
    const h = PRIM.Audio.play(u, 0.5, true); await new Promise(r => setTimeout(r, 300)); const res = { decoded: !!d, len: d && +d.buf.duration.toFixed(3), loopStart: d && +d.s.toFixed(3), loopEnd: d && +d.e.toFixed(3), srcLoop: h.src && h.src.loop, srcStart: h.src && +h.src.loopStart.toFixed(3), srcEnd: h.src && +h.src.loopEnd.toFixed(3), ctx: PRIM.Audio.ctx().state };
    h.stop(0.02); await new Promise(r => setTimeout(r, 100)); res.afterStop = h.playing; return JSON.stringify(res); });
  console.log('loop', r);
};
