/* PRIMEngine storage. Project data -> localStorage. Imported files (images, sounds, fonts) -> IndexedDB.
   (Projects live in this browser for now; saving to real files / Git comes later.) */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), St = (PRIM.Store = {});
  const IDX = "prime.studio.index", PKEY = id => "prime.studio.p." + id, SKEY = id => "prime.studio.settings." + id;
  const ls = { get(k) { try { return localStorage.getItem(k); } catch (e) { return null; } }, set(k, v) { try { localStorage.setItem(k, v); return true; } catch (e) { return false; } }, del(k) { try { localStorage.removeItem(k); } catch (e) {} } };
  const mem = new Map(); let dbp = null;
  const db = () => dbp || (dbp = new Promise((res, rej) => { try { const r = indexedDB.open("prime-studio", 1); r.onupgradeneeded = () => r.result.createObjectStore("assets"); r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error); } catch (e) { rej(e); } }));
  async function tx(mode, fn) { const d = await db(); return new Promise((res, rej) => { const t = d.transaction("assets", mode), r = fn(t.objectStore("assets")); t.oncomplete = () => res(r && r.result); t.onerror = () => rej(t.error); }); }

  St.list = () => { try { return (JSON.parse(ls.get(IDX) || "[]")).sort((a, b) => b.modified - a.modified); } catch (e) { return []; } };
  const writeIdx = p => { const l = St.list().filter(x => x.id !== p.id); l.push({ id: p.id, name: p.name, modified: p.modified }); ls.set(IDX, JSON.stringify(l)); };
  St.save = p => { p.modified = Date.now(); const ok = ls.set(PKEY(p.id), JSON.stringify(p)); writeIdx(p); return ok; };
  St.load = id => { try { const p = JSON.parse(ls.get(PKEY(id))); return p && p.format === 2 ? p : null; } catch (e) { return null; } };
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

  St.putAsset = async (pid, id, blob) => { mem.set(pid + "/" + id, blob); try { await tx("readwrite", s => s.put(blob, pid + "/" + id)); } catch (e) {} };
  St.delAsset = async (pid, id) => { mem.delete(pid + "/" + id); try { await tx("readwrite", s => s.delete(pid + "/" + id)); } catch (e) {} };
  St.getAsset = async (pid, id) => { if (mem.has(pid + "/" + id)) return mem.get(pid + "/" + id); try { return await tx("readonly", s => s.get(pid + "/" + id)); } catch (e) { return null; } };
  St.registerFont = async (id, url) => { try { const f = new FontFace("prim-font-" + id, "url(" + url + ")"); await f.load(); document.fonts.add(f); return true; } catch (e) { return false; } };
  // load every asset of a project -> { assetId: objectURL }, and register its fonts
  St.loadAssets = async project => {
    const urls = {};
    await Promise.all(project.assets.map(async a => { const b = await St.getAsset(project.id, a.id); if (!b) return; urls[a.id] = URL.createObjectURL(b); if (a.type === "font") await St.registerFont(a.id, urls[a.id]); }));
    return urls;
  };
})();
