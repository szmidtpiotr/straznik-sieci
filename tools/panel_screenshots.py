#!/usr/bin/env python3
"""Regenerates docs/img/panel-*.png from tools/panel_demo.py (sample data, no device needed).

    pip install playwright && playwright install chromium
    python3 tools/panel_screenshots.py
"""
import asyncio
import subprocess
import sys
import time
from pathlib import Path

from playwright.async_api import async_playwright

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "docs/img"
PORT = 8766
URL = f"http://127.0.0.1:{PORT}/"
# Headless Chromium on Linux often draws no colour emoji. If the monochrome "Noto Emoji"
# font is installed (fonts.google.com/noto/specimen/Noto+Emoji), use it for emoji only.
EMOJI_CSS = """@font-face{font-family:EmojiFB;src:local('Noto Emoji');
unicode-range:U+1F000-1FAFF,U+2600-27BF,U+2190-21FF,U+23E9-23FA,U+2B00-2BFF,U+FE0F}
:root{--ui:EmojiFB,system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;--mono:EmojiFB,ui-monospace,"SF Mono","JetBrains Mono",Menlo,Consolas,monospace}"""


async def shot(pg, path, mobile):
    # Full-page shots on a phone: grow the viewport instead of stitching, so the
    # fixed bottom tab bar lands at the bottom and not in the middle of the page.
    if mobile:
        h = await pg.evaluate("document.documentElement.scrollHeight")
        await pg.set_viewport_size({"width": 390, "height": h})
        await pg.wait_for_timeout(300)
    await pg.screenshot(path=path)
    if mobile:
        await pg.set_viewport_size({"width": 390, "height": 844})


async def main():
    OUT.mkdir(parents=True, exist_ok=True)
    errs = []
    async with async_playwright() as p:
        b = await p.chromium.launch()
        for name, vp, mobile in [("telefon", {"width": 390, "height": 844}, True),
                                 ("komputer", {"width": 1280, "height": 860}, False)]:
            ctx = await b.new_context(viewport=vp, device_scale_factor=2 if mobile else 1, is_mobile=mobile,
                                      has_touch=mobile, locale="pl-PL", timezone_id="Europe/Warsaw")
            pg = await ctx.new_page()
            pg.on("pageerror", lambda e: errs.append(f"{name}: {e}"))
            pg.on("console", lambda m: errs.append(f"{name} console: {m.text}") if m.type == "error" else None)
            await pg.goto(URL)
            await pg.add_style_tag(content=EMOJI_CSS)
            await pg.wait_for_timeout(1500)
            await shot(pg, OUT / f"panel-{name}-posterunki.png", mobile)
            if mobile:
                await pg.click("#tree .row >> text=NAS (zdjęcia)")
                await pg.wait_for_timeout(800)
                await pg.screenshot(path=OUT / "panel-telefon-karta-celu.png")
                await pg.keyboard.press("Escape")
                await pg.wait_for_timeout(300)
                await pg.click("text=+ Dodaj")
                await pg.wait_for_timeout(600)
                await pg.click(".types button >> text=http")
                await pg.wait_for_timeout(300)
                await pg.screenshot(path=OUT / "panel-telefon-dodaj-cel.png")
                await pg.keyboard.press("Escape")
                await pg.wait_for_timeout(300)
            for tab in ["serwer", "dziennik", "ustawienia"]:
                await pg.click(f"#tabs button[data-v={tab}]")
                await pg.wait_for_timeout(900)
                await shot(pg, OUT / f"panel-{name}-{tab}.png", mobile)
            await ctx.close()
        await b.close()
    print("\n".join(errs) or "bez błędów w konsoli")


srv = subprocess.Popen([sys.executable, str(ROOT / "tools/panel_demo.py"), str(PORT)], stdout=subprocess.DEVNULL)
try:
    time.sleep(1)
    asyncio.run(main())
finally:
    srv.terminate()
