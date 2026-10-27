/* Talks to the outside world: saving/opening .prime files and picking files or folders from the computer.
   Desktop app (Electron) -> native dialogs through window.primeDesktop.  Browser -> File System Access API, with download / <input> fallbacks. */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), H = (PRIM.Host = { desktop: !!window.primeDesktop });
  const base = p => p.split(/[\\/]/).pop();
  const mkFile = (bytes, name) => new File([bytes], name);

  // ---- save a .prime file. `ref` = what the last save returned (file handle or path) so Ctrl+S overwrites. -> { ref, name } | null
  H.saveFile = async (suggested, bytes, ref, forceDialog) => {
    if (H.desktop) { const r = await window.primeDesktop.saveFile(suggested, bytes, forceDialog ? null : ref); return r ? { ref: r, name: base(r) } : null; }
    if (window.showSaveFilePicker) {
      try {
        let h = !forceDialog && ref && ref.createWritable ? ref : null;
        if (!h) h = await window.showSaveFilePicker({ suggestedName: suggested, types: [{ description: "PRIMEngine project", accept: { "application/octet-stream": [".prime"] } }] });
        const w = await h.createWritable(); await w.write(bytes); await w.close(); return { ref: h, name: h.name };
      } catch (e) { if (e && e.name === "AbortError") return null; /* blocked (e.g. inside an iframe): fall through to download */ }
    }
    const a = document.createElement("a"); a.href = URL.createObjectURL(new Blob([bytes], { type: "application/octet-stream" })); a.download = suggested; document.body.append(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 4000); return { ref: null, name: suggested, downloaded: true };
  };

  // ---- open a .prime file -> { bytes, name, ref } | null
  H.openFile = async () => {
    if (H.desktop) { const r = await window.primeDesktop.openFile(); return r ? { bytes: r.bytes, name: base(r.path), ref: r.path } : null; }
    if (window.showOpenFilePicker) {
      try { const [h] = await window.showOpenFilePicker({ types: [{ description: "PRIMEngine project", accept: { "application/octet-stream": [".prime"] } }] }); const f = await h.getFile(); return { bytes: new Uint8Array(await f.arrayBuffer()), name: f.name, ref: h }; }
      catch (e) { if (e && e.name === "AbortError") return null; }
    }
    const f = (await pickInput({ accept: ".prime" }))[0]; return f ? { bytes: new Uint8Array(await f.arrayBuffer()), name: f.name, ref: null } : null;
  };
  H.readFile = async f => ({ bytes: new Uint8Array(await f.arrayBuffer()), name: f.name, ref: null });

  // ---- hidden <input type=file> fallback
  function pickInput(o) {
    return new Promise(res => { const i = document.createElement("input"); i.type = "file"; if (o.accept) i.accept = o.accept; if (o.multiple) i.multiple = true; if (o.dir) i.webkitdirectory = true; i.style.display = "none"; document.body.append(i);
      i.onchange = () => { res([...i.files]); i.remove(); }; i.oncancel = () => { res([]); i.remove(); }; i.click(); });
  }
  const asItems = (files, keepTop) => files.map(f => ({ path: (f.webkitRelativePath ? f.webkitRelativePath.split("/").slice(0, -1).join("/") : ""), name: f.name, file: f }));

  // ---- pick files / a whole folder from the computer -> [{ path:"Folder/Sub", name, file }]
  H.pickFiles = async () => {
    if (H.desktop) return (await window.primeDesktop.pickFiles()).map(x => ({ path: "", name: base(x.path), file: mkFile(x.bytes, base(x.path)) }));
    return asItems(await pickInput({ multiple: true }));
  };
  H.pickFolder = async () => {
    if (H.desktop) return (await window.primeDesktop.pickFolder()).map(x => { const parts = x.path.split("/"), name = parts.pop(); return { path: parts.join("/"), name, file: mkFile(x.bytes, name) }; });
    if (window.showDirectoryPicker) {
      try { const dir = await window.showDirectoryPicker(), out = []; await walkHandle(dir, dir.name, out); return out; } catch (e) { if (e && e.name === "AbortError") return []; }
    }
    return asItems(await pickInput({ dir: true }));
  };
  async function walkHandle(dir, path, out) { for await (const [name, h] of dir.entries()) { if (h.kind === "file") out.push({ path, name, file: await h.getFile() }); else await walkHandle(h, path + "/" + name, out); } }

  // ---- files/folders dropped from the OS. Call synchronously inside the drop event; returns a promise.
  H.fromDrop = dt => {
    const entries = [...(dt.items || [])].map(i => (i.webkitGetAsEntry ? i.webkitGetAsEntry() : null)).filter(Boolean), plain = [...(dt.files || [])];
    if (!entries.length) return Promise.resolve(asItems(plain));
    const out = [];
    const walk = (en, path) => new Promise(res => {
      if (en.isFile) en.file(f => { out.push({ path, name: en.name, file: f }); res(); }, () => res());
      else { const r = en.createReader(), all = []; const next = () => r.readEntries(async es => { if (!es.length) { for (const c of all) await walk(c, path ? path + "/" + en.name : en.name); res(); } else { all.push(...es); next(); } }, () => res()); next(); }
    });
    return (async () => { for (const en of entries) await walk(en, ""); return out; })();
  };
})();
