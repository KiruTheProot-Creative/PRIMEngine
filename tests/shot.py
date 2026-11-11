import sys, asyncio
from playwright.async_api import async_playwright
async def main(url, out, wait=2500, w=1280, h=720):
    async with async_playwright() as p:
        b = await p.chromium.launch(executable_path="/usr/bin/google-chrome", args=["--no-sandbox","--use-angle=swiftshader","--use-gl=angle","--enable-unsafe-swiftshader","--ignore-gpu-blocklist"])
        pg = await b.new_page(viewport={"width":w,"height":h})
        logs=[]
        pg.on("console", lambda m: logs.append(m.text))
        pg.on("pageerror", lambda e: logs.append("ERR "+str(e)))
        await pg.goto(url)
        await pg.wait_for_timeout(wait)
        await pg.screenshot(path=out)
        print("\n".join(logs[:15]))
        print(await pg.evaluate("document.getElementById('err').textContent"))
        await b.close()
asyncio.run(main(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv)>3 else 2500))
