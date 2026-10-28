const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  p.on('console', m => { if (m.type() === 'error') console.log('CONSOLE', m.text()); });
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles(SP + '/cube_bin.fbx'); await p.waitForTimeout(1500);
  await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(500);
  await p.click('button:has-text("New object")'); await p.click('.pitem:has-text("cube_bin")'); await p.waitForTimeout(1200);
  console.log('props', await p.evaluate(() => JSON.stringify(__studio.S.project.props)));
  await p.evaluate(() => { const T = __studio.S.tags, id = __studio.S.project.props[0].id; T.set(id + '.type', 'door'); T.set(id + '.openMode', 'interact');  });
  await p.waitForTimeout(500); await p.screenshot({ path: out + '_obj.png' });
  // a wall across the starter room (row 24) with a gap at cell 23, and a door in that gap
  await p.evaluate(() => { const S = __studio.S, m = S.project.maps[0], G = PRIM.Grid, id = S.project.props[0].id; for (let x = 20; x < 28; x++) if (x !== 23) G.edit(m, 0, x, 24).w = '';
    m.things = [{ id: 't1', prop: id, x: 23 * 128 + 64, y: 24 * 128 + 64, ang: 0, layer: 0 }]; m.n.t = 1; S.tags.emit([]); });
  await p.click('.tab:has-text("Map")').catch(() => {}); await p.waitForTimeout(400); await p.screenshot({ path: out + '_map.png' });
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(400); await p.click('button:has-text("Walk map1")'); await p.waitForFunction(() => __studio.S.play && __studio.S.play.hudHost, null, { timeout: 8000 }); await p.waitForTimeout(1200);
  // a splash shock sphere between you and a closed door must show in front of the door
  const X = 23 * 128 + 64, Y0 = 24 * 128 + 64;
  const shot = (withShock) => p.evaluate(([X, Y0, w]) => { const q = __studio.S.play; q.msg.style.display = 'none'; q.body = null; q.x = X; q.y = Y0 + 400; q.z = 0; q.yaw = -Math.PI / 2; q.pitch = 0;
    q.fx = w ? [{ kind: 'shock', deco: true, p: [X, 90, Y0 + 220], R: 70, life: 100, t: 30, c1: [1, 0, 0], c2: [1, 0, 0], anim: 'none', aspd: 2, glow: 1.5 }] : [];
    let t = q.last || performance.now(); for (let i = 0; i < 2; i++) { t += 16; q.frame(t); cancelAnimationFrame(q.raf); }
    const gl = q.view.gl, d = new Uint8Array(4); gl.readPixels(gl.drawingBufferWidth >> 1, gl.drawingBufferHeight >> 1, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, d); q.raf = requestAnimationFrame(z => q.frame(z));
    return JSON.stringify({ door: q.things.length, pixel: Array.from(d.slice(0, 3)) }); }, [X, Y0, withShock]);
  console.log('door only', await shot(false));
  console.log('door + shock in front', await shot(true));
  await p.screenshot({ path: out + '_shock.png', timeout: 90000 }).catch(() => console.log('(skip)'));
};
