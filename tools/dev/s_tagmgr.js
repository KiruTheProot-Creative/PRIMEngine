// Tag Manager tab: groups, search, type filter, create your own tag, edit value, rename (references follow), "used by", delete.
module.exports = async (p, out) => {
  p.on('pageerror', e => console.log('PAGEERROR', e.message, (e.stack || '').split('\n')[1]));
  await p.click('.tile.plus'); await p.waitForSelector('#app'); await p.waitForTimeout(300);
  await p.click('.tab:has-text("Tag Manager")'); await p.waitForTimeout(400);
  console.log('groups:', (await p.locator('.tmside .tmg').allTextContents()).join(' | '));
  await p.fill('.tmq', 'volume'); await p.waitForTimeout(200);
  console.log('search volume:', (await p.locator('.tmtable .tmr:not(.tmhd) .tpk').allTextContents()).join(' | '));
  await p.fill('.tmq', ''); await p.selectOption('.tmbar select', 'color'); await p.waitForTimeout(200);
  console.log('colors only:', await p.locator('.tmbar .hint').innerText());
  await p.selectOption('.tmbar select', 'all');
  // new tag
  await p.click('.cbar button:has-text("+ New tag")'); await p.waitForTimeout(200);
  await p.fill('.tmnew input[aria-label=name]', 'Bad Name!'); await p.click('.tmnew button:has-text("Create tag")'); console.log('bad name:', await p.locator('.tmnew .warn').innerText());
  await p.fill('.tmnew input[aria-label=name]', 'coins'); await p.fill('.tmnew input[aria-label=label]', 'Coins collected'); await p.fill('.tmnew input[aria-label=value]', '5'); await p.fill('.tmnew input[aria-label=min]', '0'); await p.fill('.tmnew input[aria-label=max]', '999');
  await p.click('.tmnew button:has-text("Create tag")'); await p.waitForTimeout(300);
  console.log('created:', await p.evaluate(() => JSON.stringify(__studio.S.tags.spec('coins'))), '| my tags list:', (await p.locator('.tmtable .tmr:not(.tmhd) .tpk').allTextContents()).join(' | '));
  await p.screenshot({ path: out + '_mine.png' });
  // used by: a HUD text shows it
  await p.evaluate(() => { const T = __studio.S.tags, k = T.names().find(n => /^hud\.healthtext\.text$/.test(n)); T.set(k, 'HP {player.health}  Coins {coins}'); window.__k = k; });
  await p.click('.tmtable .tmr:has-text("coins")'); await p.waitForTimeout(300);
  console.log('inspector:', (await p.locator('.insp').innerText()).split('\n').filter(Boolean).slice(0, 4).join(' / '), '| used by:', await p.locator('.insp .fold:has-text("Used by")').innerText().then(t => t.replace(/\s+/g, ' ')));
  // edit the value in the table
  await p.fill('.tmtable .tmr.sel input[type=number]', '42').catch(async () => { await p.locator('.tmtable .tmr.sel input').first().fill('42'); }); await p.keyboard.press('Enter'); await p.waitForTimeout(200);
  console.log('value now:', await p.evaluate(() => __studio.S.tags.get('coins')));
  await p.screenshot({ path: out + '_insp.png' });
  // rename: references follow
  await p.fill('.insp input[aria-label="Tag name"]', 'gold'); await p.keyboard.press('Tab'); await p.waitForTimeout(300);
  console.log('renamed:', await p.evaluate(() => JSON.stringify({ old: __studio.S.tags.has('coins'), gold: __studio.S.tags.get('gold'), text: __studio.S.tags.get(window.__k) })));
  // delete (it is used, so it asks)
  p.once('dialog', d => d.accept()); await p.click('.insp button:has-text("Delete")'); await p.waitForTimeout(300);
  console.log('deleted:', await p.evaluate(() => __studio.S.tags.has('gold')));
};
