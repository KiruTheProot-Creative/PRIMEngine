// Tag picker: grouped lists (Player > weapon, screens, maps...), search bar, picking a tag; weapon ammo tags note.
const SP = '/tmp/claude-0/-home-claude/4d2f2209-a771-580d-8052-566f87920e94/scratchpad';
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  const menu = async (top, item) => { await p.click('.mb:has-text("' + top + '")'); await p.click('.pitem:has-text("' + item + '")'); };
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await menu('Screen', 'Create Map'); await p.waitForSelector('.medcv');
  let [fc] = await Promise.all([p.waitForEvent('filechooser'), menu('Assets', 'Import Files')]); await fc.setFiles(SP + '/Arm_canon.fbx'); await p.waitForTimeout(1500);
  await p.click('.tab:has-text("Player Editor")'); await p.waitForTimeout(500);
  await p.click('button:has-text("New weapon")'); await p.click('.pitem:has-text("Arm_canon")'); await p.waitForTimeout(1500);
  console.log('ammo note:', await p.locator('.insp .note:has-text("own ammo tags")').innerText().catch(() => 'MISSING'));
  // a "Set a tag" node in the Player's script: its Tag field is the new picker
  await p.evaluate(() => { const S = __studio.S, pl = S.project.player; pl.script = { nodes: [], links: [] }; const n = PRIM.Schema.addSNode(S.project, 'player', pl.script, 'setTag', 40, 40); S.coderT = { kind: 'player' }; S.cnode = n.id; });
  await p.click('.tab:has-text("Node Coder")'); await p.waitForTimeout(600);
  await p.click('.insp .tagbtn >> nth=0'); await p.waitForTimeout(300);
  console.log('groups:', (await p.locator('.tagpick .tpg').allTextContents()).join(' | '));
  await p.click('.tagpick .tpg:has-text("Player")'); await p.waitForTimeout(200);
  console.log('player opened:', (await p.locator('.tagpick .tpg.sub').allTextContents()).join(' | '));
  await p.screenshot({ path: out + '_groups.png' });
  await p.fill('.tagpick .tpq', 'reserve'); await p.waitForTimeout(200);
  console.log('search "reserve":', (await p.locator('.tagpick .tpi .tpk').allTextContents()).join(' | '));
  await p.screenshot({ path: out + '_search.png' });
  await p.click('.tagpick .tpi:has-text("player.att.reserve")'); await p.waitForTimeout(300);
  console.log('picked:', await p.evaluate(() => { const S = __studio.S, n = S.project.player.script.nodes[0]; return S.tags.get('player.s.' + n.id + '.tag'); }), '| button:', await p.locator('.insp .tagbtn >> nth=0').innerText());
await p.click('.insp .tagbtn >> nth=0'); await p.fill('.tagpick .tpq', 'zzzz'); console.log('no match:', await p.locator('.tagpick .tpe').innerText()); await p.keyboard.press('Escape');
  console.log('closed:', await p.locator('.tagpick').count());
};
