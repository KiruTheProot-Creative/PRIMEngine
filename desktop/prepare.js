// Copies the web version of the Studio into desktop/app so Electron can package it.
const fs = require("fs"), path = require("path");
const root = path.join(__dirname, ".."), out = path.join(__dirname, "app");
fs.rmSync(out, { recursive: true, force: true });
fs.mkdirSync(path.join(out, "src"), { recursive: true });
for (const f of ["index.html", "classic.html"]) fs.copyFileSync(path.join(root, f), path.join(out, f));
for (const d of ["core", "studio", "pe"]) fs.cpSync(path.join(root, "src", d), path.join(out, "src", d), { recursive: true });
console.log("prepared desktop/app");
