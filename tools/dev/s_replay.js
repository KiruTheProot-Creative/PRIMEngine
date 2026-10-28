// Playtest started and stopped several times (and a project switch): only ONE game may keep running, so footsteps never stack.
module.exports = async (p, out) => {
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv'); await p.waitForTimeout(300);
  await p.evaluate(() => { window.__games = []; const C = PRIM.Play3D; PRIM.Play3D = class extends C { constructor(o) { super(o); window.__games.push(this); } }; });
  const alive = () => p.evaluate(() => window.__games.filter(g => !g.dead && g.mode === 'play').length);
  for (let i = 0; i < 4; i++) {   // the ▶ button: playtest on, start the game, footsteps, playtest off
    await p.click('.tbtn.play'); await p.waitForTimeout(600);
    await p.evaluate(() => { const rt = __studio.S.rt; rt.o.onStartGame(rt); }); await p.waitForTimeout(500);
    await p.click('.tbtn.play'); await p.waitForTimeout(500);
    console.log('after ▶ playtest', i + 1, 'games still running:', await alive());
  }
  for (let i = 0; i < 3; i++) {   // Maximize on Play: start, start the game, Escape out
    await p.click('button:has-text("Maximize on Play")'); await p.waitForTimeout(600);
    await p.evaluate(() => { const rt = document.querySelector('.play-layer').__rt; rt.o.onStartGame(rt); }); await p.waitForTimeout(500);
    await p.evaluate(() => { document.querySelector('.play-layer').__rt.o.onExit(); }); await p.waitForTimeout(500);
    console.log('after maximized playtest', i + 1, 'games still running:', await alive());
  }
  for (let i = 0; i < 3; i++) {   // the Game tab: start the game inside it, then switch tabs
    await p.click('.tab:has-text("Game")'); await p.waitForTimeout(500);
    await p.evaluate(() => { const rt = __studio.S.rt; if (rt && rt.o.onStartGame) rt.o.onStartGame(rt); }); await p.waitForTimeout(500);
    await p.click('.tab:has-text("Map")'); await p.waitForTimeout(500);
    console.log('after game tab', i + 1, 'games still running:', await alive());
  }
  await p.click('.tab:has-text("Game")'); await p.waitForTimeout(400); await p.evaluate(() => { const rt = __studio.S.rt; if (rt && rt.o.onStartGame) rt.o.onStartGame(rt); }); await p.waitForTimeout(400);
  await p.click('button:has-text("Projects")'); await p.waitForTimeout(800); console.log('after leaving the project, games still running:', await alive());
};
