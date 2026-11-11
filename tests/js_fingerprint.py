import asyncio, json
from playwright.async_api import async_playwright
JS = """
() => {
  const out = [];
  for (const [k,m] of Object.entries(MATS)) {
    let s = 0; const v = m.mb.v;
    for (let i = 0; i < v.length; i += 12) s += v[i] + v[i+1]*2 + v[i+2]*3 + v[i+8] + v[i+6];
    out.push(`mesh ${k} verts=${v.length/12} idx=${m.mb.i.length} sum=${s.toFixed(2)}`);
  }
  for (const k of ['floor','wall','trim','cannon','core','strip','door','env','sprite']) {
    const t = makeTex(k); let s = 0; for (const b of t) s += b; out.push(`tex ${k} sum=${s}`);
  }
  return out.join('\\n');
}
"""
async def main():
    async with async_playwright() as p:
        b = await p.chromium.launch(executable_path="/usr/bin/google-chrome", args=["--no-sandbox","--use-angle=swiftshader","--use-gl=angle","--enable-unsafe-swiftshader"])
        pg = await b.new_page()
        await pg.goto("file:///home/claude/prime-engine/web/index.html?ui=0")
        print(await pg.evaluate(JS))
        await b.close()
asyncio.run(main())
