module.exports = async (p, out) => {
  await p.screenshot({ path: out + '_manager0.png' });
  await p.click('.pcard.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(400);
  console.log('screens', await p.evaluate(() => __studio.S.project.screens.map(s => s.id + ':' + s.nodes.length).join(' ')), 'tags', await p.evaluate(() => Object.keys(__studio.S.project.tags).length));
  await p.screenshot({ path: out + '_editor_main.png' });
  // drag the Play button down in the preview
  const box = await p.locator('.rt-stage').boundingBox(); const sc = box.width / 1280;
  await p.mouse.move(box.x + 640 * sc, box.y + 380 * sc); await p.mouse.down(); await p.mouse.move(box.x + 640 * sc, box.y + 300 * sc, { steps: 5 }); await p.mouse.up();
  console.log('play y after drag', await p.evaluate(() => __studio.S.project.tags['main.play.y'].value), 'selected', await p.evaluate(() => __studio.S.selId));
  // rename element and see tag move
  await p.fill('.idin', 'start_btn'); await p.press('.idin', 'Tab'); await p.waitForTimeout(200);
  console.log('renamed has tag', await p.evaluate(() => 'main.start_btn.text' in __studio.S.project.tags && !('main.play.text' in __studio.S.project.tags)));
  // tags tab + custom tag
  await p.click('.tab:has-text("Tags")'); await p.fill('input[aria-label="New tag name"]', 'player.speed'); await p.click('button:has-text("+ Add tag")'); await p.waitForTimeout(100);
  console.log('custom tag', await p.evaluate(() => JSON.stringify(__studio.S.project.tags['player.speed'])));
  await p.screenshot({ path: out + '_tags.png' });
  await p.click('.tab:has-text("Properties")');
  // settings screen
  await p.click('.row-item:has-text("settings") >> nth=0'); await p.waitForTimeout(300); await p.screenshot({ path: out + '_settings.png' });
  // test mode: drag master slider on settings
  await p.click('.segb:has-text("Test")'); await p.waitForTimeout(300);
  const b2 = await p.locator('.rt-stage').boundingBox(), s2 = b2.width / 1280;
  await p.mouse.move(b2.x + 450 * s2, b2.y + 185 * s2); await p.mouse.down(); await p.mouse.move(b2.x + 500 * s2, b2.y + 185 * s2, { steps: 4 }); await p.mouse.up();
  console.log('test slider moved base master (should stay .8):', await p.evaluate(() => __studio.S.project.tags['audio.master'].value));
  await p.screenshot({ path: out + '_test.png' });
  // splash screen edit
  await p.click('.segb:has-text("Edit")'); await p.click('.row-item:has-text("splash") >> nth=0'); await p.waitForTimeout(300);
  await p.click('.row-item:has-text("intro")'); await p.waitForTimeout(200); await p.screenshot({ path: out + '_splash.png' });
  // play fullscreen: splash runs then main menu
  await p.click('button:has-text("▶ Play")'); await p.waitForSelector('.play-layer'); await p.waitForTimeout(1200); await p.screenshot({ path: out + '_play_splash.png' });
  await p.mouse.click(600, 300); await p.waitForTimeout(500);
  console.log('screen after skip', await p.evaluate(() => document.querySelector('.play-layer').__rt.screenId));
  await p.screenshot({ path: out + '_play_menu.png' });
  await p.keyboard.press('Escape'); await p.waitForTimeout(200);
  console.log('play layer gone', await p.evaluate(() => !document.querySelector('.play-layer')));
  // back to manager
  await p.click('button:has-text("← Projects")'); await p.waitForTimeout(200); await p.screenshot({ path: out + '_manager1.png' });
};
