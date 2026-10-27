/* The .prime project file: a ZIP containing project.json (all screens, tags and the asset list)
   and an Assets/ folder that mirrors the project's asset folders. Rename it to .zip to look inside. */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), P = (PRIM.Prime = { EXT: ".prime", FORMAT: 3 });
  const enc = new TextEncoder(), dec = new TextDecoder(), assetPath = a => "Assets/" + (a.path ? a.path + "/" : "") + a.name;
  P.pack = async (project, getBlob) => {
    const files = [{ name: "project.json", data: enc.encode(JSON.stringify(project, null, 1)) },
      { name: "README.txt", data: enc.encode("PRIMEngine project file.\nOpen it with PRIMEngine Studio. project.json holds the screens and tags; Assets/ holds your files.\n") }];
    let missing = 0;
    for (const a of project.assets) { const b = await getBlob(a.id); if (!b) { missing++; continue; } files.push({ name: assetPath(a), data: new Uint8Array(await b.arrayBuffer()) }); }
    return { bytes: PRIM.Zip.write(files), missing };
  };
  // -> { project, blobs: { assetId: Blob }, missing: [names] }
  P.unpack = async bytes => {
    const files = await PRIM.Zip.read(bytes), pj = files.find(f => f.name === "project.json");
    if (!pj) throw new Error("this is not a PRIMEngine project (project.json is missing)");
    const project = JSON.parse(dec.decode(pj.data)); if (!project || !Array.isArray(project.screens) || !project.tags) throw new Error("project.json looks damaged");
    const by = {}; for (const f of files) by[f.name] = f.data; const blobs = {}, missing = [];
    for (const a of project.assets || []) { const d = by[assetPath(a)] || by["Assets/" + a.name]; if (d) blobs[a.id] = new Blob([d], { type: PRIM.Store.mime(a.name) }); else missing.push(a.name); }
    return { project, blobs, missing };
  };
})();
