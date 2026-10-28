const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles([SP + '/Arm_canon.fbx', SP + '/cube_bin.fbx', SP + '/assets_test/Images/logo.png']); await p.waitForTimeout(1500);
  await p.click('.tab:has-text("Object Editor")'); await p.waitForTimeout(400); await p.click('button:has-text("New object")'); await p.click('.pitem:has-text("cube_bin")'); await p.waitForTimeout(1200); await p.evaluate(() => { const T = __studio.S.tags, id = __studio.S.project.props[0].id; T.set(id + '.ry', 35); T.set(id + '.rx', 20); }); await p.click('button:has-text("Scale to grid")');
  console.log('material fields', await p.evaluate(() => [...document.querySelectorAll('.insp .fld')].map(e => e.querySelector('.flab span') && e.querySelector('.flab span').textContent).filter(t => /Rough|Metal|Reflect|Texture/.test(t || '')).join(' | ')));
  const shot = async (n, set) => { await p.evaluate(set => { const T = __studio.S.tags, id = __studio.S.project.props[0].id; for (const k in set) T.set(id + '.' + k, set[k]); }, set); await p.waitForTimeout(500); await p.screenshot({ path: out + '_' + n + '.png', clip: { x: 245, y: 135, width: 860, height: 430 } }); };
  await shot('plastic', { matRough: 0.8, matMetal: 0 }); await shot('metal', { matRough: 0.15, matMetal: 1, matTint: '#ffd27a' });
  await shot('tex', { matTexture: await p.evaluate(() => __studio.S.project.assets.find(a => a.type === 'image').id), matMetal: 0, matRough: 0.5, matTint: '#ffffff' });
  // weapon material tab
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(400); await p.click('button:has-text("New weapon")'); await p.click('.pitem:has-text("Arm_canon")'); await p.waitForTimeout(1200);
  await p.click('.objtabs button:has-text("Material")').catch(() => console.log('no material tab button')); await p.waitForTimeout(300);
  console.log('weapon material fields', await p.evaluate(() => [...document.querySelectorAll('.insp .fld')].map(e => e.querySelector('.flab span') && e.querySelector('.flab span').textContent).filter(t => /Rough|Metal|Reflect|Texture/.test(t || '')).join(' | ')));
  await p.evaluate(() => { const T = __studio.S.tags, id = __studio.S.project.player.attachments[0].id; T.set('player.' + id + '.matMetal', 1); T.set('player.' + id + '.matRough', 0.2); }); await p.waitForTimeout(500); await p.screenshot({ path: out + '_weapon.png' });
};
