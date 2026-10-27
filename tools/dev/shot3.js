// Studio harness with the file-picker APIs removed so the download / <input> fallbacks are exercised.
const { chromium } = require('playwright');
const out = process.argv[2], steps = require(require('path').resolve(process.argv[3]));
(async () => {
  const b = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--ignore-gpu-blocklist', '--enable-unsafe-swiftshader'] }); const ctx = await b.newContext({ viewport: { width: +process.env.W || 1440, height: +process.env.H || 860 }, acceptDownloads: true }); const p = await ctx.newPage();
  await p.addInitScript(() => { delete window.showSaveFilePicker; delete window.showOpenFilePicker; delete window.showDirectoryPicker; });
  const logs = []; p.on('console', m => { if (m.type() === 'error' && !/ERR_TUNNEL|Failed to load resource/.test(m.text())) logs.push(m.text()); }); p.on('pageerror', e => logs.push('PAGEERROR: ' + e.message));
  await p.goto('http://127.0.0.1:8766/index.html'); await p.waitForSelector('.wiz');
  await steps(p, out); console.log(logs.join('\n') || 'no console errors'); await b.close();
})();
