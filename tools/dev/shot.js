// Dev screenshot harness: node tools/dev/shot.js out_prefix [steps.js]   (serve the repo root on :8766 first)
const { chromium } = require('playwright');
const out = process.argv[2] || 'shot', steps = process.argv[3] ? require(require('path').resolve(process.argv[3])) : null;
(async () => {
  const b = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist', '--autoplay-policy=no-user-gesture-required'] });
  const w = +process.env.W || 1280, h = +process.env.H || 720;
  const p = await b.newPage({ viewport: { width: w, height: h } });
  const logs = [];
  p.on('console', m => { if (['error', 'warning'].includes(m.type())) logs.push(m.type() + ': ' + m.text()); });
  p.on('pageerror', e => logs.push('PAGEERROR: ' + e.message));
  await p.goto('http://127.0.0.1:8766/index.html');
  await p.waitForFunction(() => window.__prime && !document.getElementById('playBtn').disabled, null, { timeout: 30000 }).catch(e => logs.push('wait failed ' + e.message));
  await p.waitForTimeout(600);
  if (steps) await steps(p, out); else await p.screenshot({ path: out + '_editor.png' });
  console.log(logs.join('\n') || 'no console errors');
  await b.close();
})();
