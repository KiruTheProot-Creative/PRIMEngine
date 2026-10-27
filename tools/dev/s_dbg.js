module.exports = async (p, out) => {
  await p.click('.pcard.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await p.click('button:has-text("▶ Play")'); await p.waitForSelector('.play-layer'); await p.waitForTimeout(800);
  console.log(await p.evaluate(() => { const e = document.elementFromPoint(600, 300); return e.className + ' | ' + e.tagName + ' | stage? ' + !!e.closest('.rt-stage') + ' | layer rect ' + JSON.stringify(document.querySelector('.play-layer').getBoundingClientRect()); }));
  await p.evaluate(() => { const r = document.querySelector('.play-layer').__rt; const o = r.down.bind(r); r.stage.addEventListener('pointerdown', () => console.log('pd fired')); });
  p.on('console', m => console.log('PAGE:', m.text()));
  await p.mouse.click(600, 300); await p.waitForTimeout(300);
  console.log(await p.evaluate(() => { const r = document.querySelector('.play-layer').__rt; return JSON.stringify({ s: !!r.splash, t: r.splash && r.splash.t }); }));
};
