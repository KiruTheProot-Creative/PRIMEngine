/* PRIMEngine (Prime edition) storage. Working copies of projects -> localStorage (autosave), imported files -> IndexedDB.
   The real project file is a .prime file: a ZIP with project.json and an Assets/ folder (same idea as the Classic Studio). */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), St = (PRIM.PEStore = {}), D = PRIM.PE;
  const IDX = "prime.pe.index", PKEY = id => "prime.pe.p." + id;
  const ls = { get(k) { try { return localStorage.getItem(k); } catch (e) { return null; } }, set(k, v) { try { localStorage.setItem(k, v); return true; } catch (e) { return false; } }, del(k) { try { localStorage.removeItem(k); } catch (e) {} } };
  const mem = new Map(); let dbp = null;
  const db = () => dbp || (dbp = new Promise((res, rej) => { try { const r = indexedDB.open("prime-pe", 1); r.onupgradeneeded = () => r.result.createObjectStore("assets"); r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error); } catch (e) { rej(e); } }));
  async function tx(mode, fn) { const d = await db(); return new Promise((res, rej) => { const t = d.transaction("assets", mode), r = fn(t.objectStore("assets")); t.oncomplete = () => res(r && r.result); t.onerror = () => rej(t.error); }); }
  const MIME = { png: "image/png", jpg: "image/jpeg", jpeg: "image/jpeg", gif: "image/gif", webp: "image/webp", bmp: "image/bmp", svg: "image/svg+xml", wav: "audio/wav", mp3: "audio/mpeg", ogg: "audio/ogg", m4a: "audio/mp4", flac: "audio/flac", ttf: "font/ttf", otf: "font/otf", woff: "font/woff", woff2: "font/woff2" };
  const ext = n => (n.split(".").pop() || "").toLowerCase();
  St.mime = n => MIME[ext(n)] || "application/octet-stream";
  St.typeOf = n => { const e = ext(n), m = MIME[e] || ""; if (e === "fbx") return "model"; return m.startsWith("image/") ? "image" : m.startsWith("audio/") ? "sound" : m.startsWith("font/") ? "font" : "file"; };
  const newId = () => "p" + Date.now().toString(36) + Math.random().toString(36).slice(2, 5);
  St.list = () => { try { return JSON.parse(ls.get(IDX) || "[]").sort((a, b) => b.modified - a.modified); } catch (e) { return []; } };
  const writeIdx = p => { const l = St.list().filter(x => x.id !== p.id); l.push({ id: p.id, name: p.name, modified: p.modified }); ls.set(IDX, JSON.stringify(l)); };
  St.save = p => { p.modified = Date.now(); const ok = ls.set(PKEY(p.id), JSON.stringify(p)); writeIdx(p); return ok; };
  St.load = id => { try { const p = JSON.parse(ls.get(PKEY(id))); return p && p.kind === "pe" ? D.heal(p) : null; } catch (e) { return null; } };
  St.create = () => { const names = St.list().map(x => x.name); let n = 1, name = "My Prime Game"; while (names.includes(name)) name = "My Prime Game " + ++n; const p = D.template(newId(), name); St.save(p); return p; };
  St.remove = async id => { ls.del(PKEY(id)); ls.set(IDX, JSON.stringify(St.list().filter(x => x.id !== id))); for (const k of [...mem.keys()]) if (k.startsWith(id + "/")) mem.delete(k); };
  St.putAsset = async (pid, id, blob) => { mem.set(pid + "/" + id, blob); try { await tx("readwrite", s => s.put(blob, pid + "/" + id)); } catch (e) {} };
  St.delAsset = async (pid, id) => { mem.delete(pid + "/" + id); try { await tx("readwrite", s => s.delete(pid + "/" + id)); } catch (e) {} };
  St.getAsset = async (pid, id) => { if (mem.has(pid + "/" + id)) return mem.get(pid + "/" + id); try { return await tx("readonly", s => s.get(pid + "/" + id)); } catch (e) { return null; } };
  St.loadAssets = async project => { const urls = {}; await Promise.all(project.assets.map(async a => { const b = await St.getAsset(project.id, a.id); if (!b) return; urls[a.id] = URL.createObjectURL(b); if (a.type === "font") await St.font(a.id, urls[a.id]); })); return urls; };
  St.font = async (id, url) => { try { const f = new FontFace("prim-font-" + id, "url(" + url + ")"); await f.load(); document.fonts.add(f); } catch (e) {} };
  // files[] = [{ name, file }]
  St.ingest = async (project, urls, files) => {
    const added = [];
    for (const it of files) {
      if (!it.name || it.name.startsWith(".")) continue; const taken = project.assets.map(a => a.name); let name = it.name, i = 2; while (taken.includes(name)) { const d = it.name.lastIndexOf("."); name = (d > 0 ? it.name.slice(0, d) : it.name) + "_" + i++ + (d > 0 ? it.name.slice(d) : ""); }
      const base = name.replace(/\.[^.]+$/, "").toLowerCase().replace(/[^a-z0-9_]+/g, "_").replace(/^_+|_+$/g, "") || "asset", id = project.assets.some(a => a.id === base) ? D.uid(base, project.assets.map(a => a.id)) : base;
      const blob = new Blob([it.file], { type: St.mime(name) }); await St.putAsset(project.id, id, blob); urls[id] = URL.createObjectURL(blob);
      const rec = { id, type: St.typeOf(name), name, path: "", size: blob.size }; if (rec.type === "font") await St.font(id, urls[id]);
      if (rec.type === "model" && PRIM.FBX) { try { const m = await PRIM.FBX.parse(await blob.arrayBuffer()); PRIM.FBX.cache[id] = m; if (PRIM.FBX.decorate) PRIM.FBX.decorate(id, m); rec.meta = PRIM.FBX.summary(m); } catch (e) { rec.meta = { error: String(e.message || e) }; } }
      project.assets.push(rec); added.push(rec);
    }
    return added;
  };
  // ---------- .prime files
  const enc = new TextEncoder(), dec = new TextDecoder();
  St.pack = async project => { const files = [{ name: "project.json", data: enc.encode(JSON.stringify(project)) }, { name: "README.txt", data: enc.encode("PRIMEngine (Prime edition) project. Open it with PRIMEngine.\n") }]; let missing = 0;
    for (const a of project.assets) { const b = await St.getAsset(project.id, a.id); if (!b) { missing++; continue; } files.push({ name: "Assets/" + a.name, data: new Uint8Array(await b.arrayBuffer()) }); }
    return { bytes: PRIM.Zip.write(files), missing }; };
  St.unpack = async bytes => {
    const files = await PRIM.Zip.read(bytes), pj = files.find(f => f.name === "project.json"); if (!pj) throw new Error("this is not a PRIMEngine project");
    const project = JSON.parse(dec.decode(pj.data)); if (!project || project.kind !== "pe") throw new Error(project && project.screens ? "this project was made with the Classic Studio (open it there)" : "project.json looks damaged");
    D.heal(project); project.id = newId(); const by = {}; for (const f of files) by[f.name] = f.data; let missing = 0;
    for (const a of project.assets) { const d = by["Assets/" + a.name] || by["Assets/" + (a.path ? a.path + "/" : "") + a.name]; if (d) await St.putAsset(project.id, a.id, new Blob([d], { type: St.mime(a.name) })); else missing++; }
    St.save(project); return { project, missing };
  };
})();
