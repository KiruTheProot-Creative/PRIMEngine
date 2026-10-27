// Studio harness (no WebGL needed): node tools/dev/shot2.js out_prefix steps.js
const { chromium } = require('playwright');
const out = process.argv[2], steps = require(require('path').resolve(process.argv[3]));
(async () => {
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: +process.env.W || 1280, height: +process.env.H || 720 } });
  const logs = []; p.on('console', m => { if (m.type() === 'error' && !/ERR_TUNNEL|Failed to load resource/.test(m.text())) logs.push(m.text()); }); p.on('pageerror', e => logs.push('PAGEERROR: ' + e.message));
  await p.goto('http://127.0.0.1:8766/index.html'); await p.waitForSelector('.mgr');
  await steps(p, out); console.log(logs.join('\n') || 'no console errors'); await b.close();
})();
