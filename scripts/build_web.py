#!/usr/bin/env python3
"""Packs the website into one self-contained file: dist/interviewprep.html.

The page, styles, script, C++ core and resources all sit inside that file,
so it works when opened from disk and on any static host. Run with: make web
"""

import base64
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read(path, mode="r"):
    with open(os.path.join(ROOT, path), mode, **({} if "b" in mode else {"encoding": "utf8"})) as f:
        return f.read()


def main():
    page = read("web/index.html")
    # Inside a <script> tag the only sequence that can end the block early is "</".
    resources = read("data/resources.json").replace("</", "<\\/")
    script = read("web/app.js").replace("</script", "<\\/script")
    wasm = base64.b64encode(read("web/core.wasm", "rb")).decode("ascii")

    for marker, value in (("/*__STYLE__*/", read("web/style.css")), ("/*__RESOURCES__*/", resources),
                          ("/*__WASM__*/", wasm), ("/*__SCRIPT__*/", script)):
        assert marker in page, marker
        page = page.replace(marker, value)

    out = os.path.join(ROOT, "dist", "interviewprep.html")
    with open(out, "w", encoding="utf8") as f:
        f.write(page)
    print(f"dist/interviewprep.html, {os.path.getsize(out) / 1e6:.2f} MB")


if __name__ == "__main__":
    main()
