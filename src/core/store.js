/* PRIMEngine storage. Project data -> localStorage (a working copy / recent list). Imported files -> IndexedDB.
   The real project file is the .prime file (see prime.js); the browser copy is an autosave safety net. */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), St = (PRIM.Store = {});
  const IDX = "prime.studio.index", PKEY = id => "prime.studio.p." + id, SKEY = id => "prime.studio.settings." + id;
  const ls = { get(k) { try { return localStorage.getItem(k); } catch (e) { return null; } }, set(k, v) { try { localStorage.setItem(k, v); return true; } catch (e) { return false; } }, del(k) { try { localStorage.removeItem(k); } catch (e) {} } };
  const mem = new Map(); let dbp = null;
  const db = () => dbp || (dbp = new Promise((res, rej) => { try { const r = indexedDB.open("prime-studio", 1); r.onupgradeneeded = () => r.result.createObjectStore("assets"); r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error); } catch (e) { rej(e); } }));
  async function tx(mode, fn) { const d = await db(); return new Promise((res, rej) => { const t = d.transaction("assets", mode), r = fn(t.objectStore("assets")); t.oncomplete = () => res(r && r.result); t.onerror = () => rej(t.error); }); }

  // ---------- file types
  const MIME = { png: "image/png", jpg: "image/jpeg", jpeg: "image/jpeg", gif: "image/gif", webp: "image/webp", bmp: "image/bmp", svg: "image/svg+xml", wav: "audio/wav", mp3: "audio/mpeg", ogg: "audio/ogg", m4a: "audio/mp4", flac: "audio/flac", ttf: "font/ttf", otf: "font/otf", woff: "font/woff", woff2: "font/woff2", json: "application/json", txt: "text/plain" };
  const ext = n => (n.split(".").pop() || "").toLowerCase();
  St.mime = n => MIME[ext(n)] || "application/octet-stream";
  St.typeOf = n => { const e = ext(n), m = MIME[e] || ""; return m.startsWith("image/") ? "image" : m.startsWith("audio/") ? "sound" : m.startsWith("font/") ? "font" : "file"; };

  // ---------- project index / working copies
  St.list = () => { try { return (JSON.parse(ls.get(IDX) || "[]")).sort((a, b) => b.modified - a.modified); } catch (e) { return []; } };
  const writeIdx = p => { const l = St.list().filter(x => x.id !== p.id); l.push({ id: p.id, name: p.name, modified: p.modified, file: p.fileName || "" }); ls.set(IDX, JSON.stringify(l)); };
  St.migrate = p => { if (p.format === 2) { p.format = 3; p.folders = []; for (const a of p.assets) a.path = a.path || ""; } if (!p.folders) p.folders = []; if (!p.maps) p.maps = []; for (const a of p.assets) if (a.path === undefined) a.path = ""; return p; };
  St.save = p => { p.modified = Date.now(); const ok = ls.set(PKEY(p.id), JSON.stringify(p)); writeIdx(p); return ok; };
  St.load = id => { try { const p = JSON.parse(ls.get(PKEY(id))); return p && (p.format === 2 || p.format === 3) ? St.migrate(p) : null; } catch (e) { return null; } };
  St.create = () => {
    const names = St.list().map(x => x.name); let n = 1, name = "My Game"; while (names.includes(name)) name = "My Game " + ++n;
    const p = PRIM.Schema.template("p" + Date.now().toString(36) + Math.random().toString(36).slice(2, 5), name); St.save(p); return p;
  };
  St.remove = async id => {
    ls.del(PKEY(id)); ls.del(SKEY(id)); ls.set(IDX, JSON.stringify(St.list().filter(x => x.id !== id)));
    for (const k of [...mem.keys()]) if (k.startsWith(id + "/")) mem.delete(k);
    try { await tx("readwrite", s => s.delete(IDBKeyRange.bound(id + "/", id + "/￿"))); } catch (e) {}
  };
  St.loadSettings = id => { try { return JSON.parse(ls.get(SKEY(id)) || "{}"); } catch (e) { return {}; } };
  St.saveSettings = (id, o) => ls.set(SKEY(id), JSON.stringify(o));

  // ---------- asset blobs
  St.putAsset = async (pid, id, blob) => { mem.set(pid + "/" + id, blob); try { await tx("readwrite", s => s.put(blob, pid + "/" + id)); } catch (e) {} };
  St.delAsset = async (pid, id) => { mem.delete(pid + "/" + id); try { await tx("readwrite", s => s.delete(pid + "/" + id)); } catch (e) {} };
  St.getAsset = async (pid, id) => { if (mem.has(pid + "/" + id)) return mem.get(pid + "/" + id); try { return await tx("readonly", s => s.get(pid + "/" + id)); } catch (e) { return null; } };
  St.registerFont = async (id, url) => { try { const f = new FontFace("prim-font-" + id, "url(" + url + ")"); await f.load(); document.fonts.add(f); return true; } catch (e) { return false; } };
  St.loadAssets = async project => {
    const urls = {};
    await Promise.all(project.assets.map(async a => { const b = await St.getAsset(project.id, a.id); if (!b) return; urls[a.id] = URL.createObjectURL(b); if (a.type === "font") await St.registerFont(a.id, urls[a.id]); }));
    return urls;
  };

  // ---------- folders inside a project ("Sounds", "Sounds/UI"). "" is the root (Assets).
  const parentOf = p => p.includes("/") ? p.slice(0, p.lastIndexOf("/")) : "";
  St.parentOf = parentOf;
  St.folders = project => { const set = new Set(project.folders); for (const a of project.assets) { let p = a.path; while (p) { set.add(p); p = parentOf(p); } } for (const f of [...set]) { let p = parentOf(f); while (p) { set.add(p); p = parentOf(p); } } return [...set].sort((a, b) => a.localeCompare(b)); };
  St.ensureFolder = (project, path) => { if (path && !project.folders.includes(path)) project.folders.push(path); };
  St.uniqueName = (taken, name) => { if (!taken.includes(name)) return name; const dot = name.lastIndexOf("."), b = dot > 0 ? name.slice(0, dot) : name, e = dot > 0 ? name.slice(dot) : ""; let i = 2; while (taken.includes(b + "_" + i + e)) i++; return b + "_" + i + e; };
  // items: [{ path (relative to baseFolder), name, file }] -> list of new asset records
  St.ingest = async (project, urls, items, baseFolder) => {
    const added = [], clean = PRIM.Schema.clean;
    for (const it of items) {
      if (!it.name || it.name.startsWith(".") || /^(thumbs\.db|desktop\.ini)$/i.test(it.name)) continue;
      const path = [baseFolder, it.path].filter(Boolean).join("/"); St.ensureFolder(project, path);
      const name = St.uniqueName(project.assets.filter(a => a.path === path).map(a => a.name), it.name), id = PRIM.Schema.uid(clean(name.replace(/\.[^.]+$/, "")) || "asset", project.assets.map(a => a.id));
      const blob = new Blob([it.file], { type: St.mime(name) }); await St.putAsset(project.id, id, blob); urls[id] = URL.createObjectURL(blob);
      const rec = { id, type: St.typeOf(name), name, path, size: blob.size }; if (rec.type === "font") await St.registerFont(id, urls[id]); project.assets.push(rec); added.push(rec);
    }
    return added;
  };

  // ---------- .prime files
  St.exportPrime = project => PRIM.Prime.pack(project, id => St.getAsset(project.id, id));
  St.importPrime = async bytes => {
    const { project, blobs, missing } = await PRIM.Prime.unpack(bytes); St.migrate(project);
    project.id = "p" + Date.now().toString(36) + Math.random().toString(36).slice(2, 5);
    for (const id in blobs) await St.putAsset(project.id, id, blobs[id]);
    St.save(project); return { project, missing };
  };
})();
