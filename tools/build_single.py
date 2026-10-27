#!/usr/bin/env python3
"""Builds dist/index.html: the editor with every script inlined (one file), in the fragment style the
artifact host expects (no <html>/<head>/<body> wrappers). Asset files are NOT inlined; they stay beside it.
usage: python3 tools/build_single.py"""
import re, pathlib
root = pathlib.Path(__file__).resolve().parent.parent
src = (root / "index.html").read_text(encoding="utf-8")

def inline(m):
    js = (root / m.group(1)).read_text(encoding="utf-8").replace("</script", "<\\/script")
    return "<script>\n" + js + "\n</script>"
src = re.sub(r'<script src="([^"]+)"></script>', inline, src)

head = re.search(r"<head>(.*?)</head>", src, re.S).group(1)
body = re.search(r"<body>(.*?)</body>", src, re.S).group(1)
head = re.sub(r'<meta charset[^>]*>\s*|<meta name="viewport"[^>]*>\s*', "", head)   # the host supplies these
out = head.strip() + "\n" + body.strip() + "\n"
(root / "dist").mkdir(exist_ok=True)
(root / "dist" / "index.html").write_text(out, encoding="utf-8")
print("dist/index.html", len(out) // 1024, "KB")
