// 2D sprite enemy: strips per state, faces the camera, animates, gets hit, dies on its last death frame.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Folder')]); await fc.setFiles(SP + '/assets_test'); await p.waitForTimeout(700);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  await p.evaluate(() => { const S = __studio.S, pr = S.project, A = pr.assets, img = A.find(a => a.name === 'logo.png').id, btn = A.find(a => a.name === 'btn.png').id;
    const e = PRIM.Schema.addProp(pr, { name: 'Imp', type: 'enemy', look: 'sprite', spriteIdle: img, spriteIdleFrames: 4, spriteWalk: img, spriteWalkFrames: 4, spriteDeath: btn, spriteDeathFrames: 2, spriteHeight: 120, speed: 0, sight: 2000 });
    pr.maps[0].things = [{ id: 't1', prop: e.id, x: 3072 + 300, y: 3072, ang: 0, layer: 0 }]; pr.maps[0].n.t = 1; S.tags.emit([]); });
  await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(500); await p.screenshot({ path: out + '_oe.png' });
  console.log('sprite fields', await p.evaluate(() => [...document.querySelectorAll('.insp .flab span')].map(e => e.textContent).filter(t => /strip|picture|Sprite|Frames|Looks/.test(t)).join(' | ')));
  await p.click('.tab:has-text("Map")'); await p.waitForTimeout(300); await p.click('button:has-text("Walk this map")'); await p.waitForTimeout(1500);
  await p.evaluate(() => { const q = __studio.S.play; q.msg.style.display = 'none'; q.look = () => {}; q.yaw = 0; q.pitch = 0; });
  await p.waitForTimeout(800); await p.screenshot({ path: out + '_game.png' });
  console.log('item', await p.evaluate(() => { const q = __studio.S.play, o = q.things[0], it = q.spriteItem(o, q.view); return JSON.stringify({ sprite: o.sprite, st: o.st, key: o.anim, w: Math.round(it.w), h: it.h, frame: it.frame, n: it.n.map(x => +x.toFixed(2)) }); }));
  console.log('hit + kill', await p.evaluate(() => { const q = __studio.S.play, o = q.things[0], O = q.pEye(), D = PRIM.M4.norm(PRIM.M4.sub([o.x, o.floor + 50, o.y], O)), h = q.hitEnemy(O, D, 5000); if (h) q.damageEnemy(h.o, 1000); for (let i = 0; i < 90; i++) q.enemyStep(o, 1 / 60); const it = q.spriteItem(o, q.view); return !!h + ' ' + o.st + ' frame ' + it.frame + '/' + it.frames; }));
  await p.waitForTimeout(500); await p.screenshot({ path: out + '_dead.png' });
  await p.keyboard.press('Escape');
};
