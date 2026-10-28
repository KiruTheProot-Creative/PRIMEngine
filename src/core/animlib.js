// PRIM.AnimLib: animations made in the Animation Editor.
// Stored in the project as structure: project.anims = [{ id, asset, name, dur, keys: [{ node, t, p:[x,y,z], r:[x,y,z], s:[x,y,z] }] }]
//   asset = the FBX asset it belongs to, node = the name of a part of that model, t = time in seconds,
//   p / r / s = the part's own position, rotation (degrees) and scale at that moment.
// apply() turns them into ordinary model animations (same format as FBX animations), so every animation list,
// the Timeline tab and the game play them like any other animation of that model.
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), L = (PRIM.AnimLib = {});
  const nodeIndex = (model, name) => model.nodes.findIndex(n => n.name === name);
  L.list = (project, asset) => (project.anims || []).filter(a => a.asset === asset);
  // keys of one part, sorted by time
  L.keysOf = (a, node) => a.keys.filter(k => k.node === node).sort((x, y) => x.t - y.t);
  // build the model animation of one editor animation
  L.build = (a, model) => {
    const by = {}; for (const k of a.keys) (by[k.node] = by[k.node] || []).push(k); const tracks = [];
    for (const name in by) { const n = nodeIndex(model, name); if (n < 0) continue; const ks = by[name].sort((x, y) => x.t - y.t), ts = ks.map(k => k.t);
      [["p", 0], ["r", 1], ["s", 2]].forEach(([f, pi]) => tracks.push({ n, p: pi, c: [0, 1, 2].map(ax => ({ t: ts, v: ks.map(k => k[f][ax]) })) })); }
    return { dur: Math.max(0.05, +a.dur || 1), tracks, custom: a.id };
  };
  // put all editor animations of this asset into the loaded model (and take out ones that were deleted)
  L.apply = (project, model, asset) => {
    if (!model || !project) return; for (const k of Object.keys(model.anims)) if (model.anims[k].custom) delete model.anims[k];
    for (const a of L.list(project, asset)) model.anims[a.name] = L.build(a, model);
  };
  L.applyAll = project => { const F = PRIM.FBX; if (!F) return; for (const id in F.cache) L.apply(project, F.cache[id], id); };
  // a part's own transform at a time: from the keys (straight lines between them), or its rest pose when it has none
  L.valueAt = (a, model, name, time) => {
    const n = model.nodes[nodeIndex(model, name)], rest = n ? { p: n.t.slice(), r: n.r.slice(), s: n.s.slice() } : { p: [0, 0, 0], r: [0, 0, 0], s: [1, 1, 1] };
    const ks = a ? L.keysOf(a, name) : []; if (!ks.length) return rest; if (time <= ks[0].t) return clone(ks[0]); const last = ks[ks.length - 1]; if (time >= last.t) return clone(last);
    let i = 0; while (ks[i + 1].t < time) i++; const A = ks[i], B = ks[i + 1], f = (time - A.t) / ((B.t - A.t) || 1), mix = (x, y) => x.map((v, j) => v + (y[j] - v) * f);
    return { p: mix(A.p, B.p), r: mix(A.r, B.r), s: mix(A.s, B.s) };
  };
  const clone = k => ({ p: k.p.slice(), r: k.r.slice(), s: k.s.slice() });
  // set (or replace) the key of a part at a time
  L.setKey = (a, node, t, v) => { t = Math.round(t * 1000) / 1000; const k = a.keys.find(x => x.node === node && Math.abs(x.t - t) < 0.0005); const r3 = x => x.map(q => Math.round(q * 1000) / 1000);
    if (k) { k.p = r3(v.p); k.r = r3(v.r); k.s = r3(v.s); return k; } const nk = { node, t, p: r3(v.p), r: r3(v.r), s: r3(v.s) }; a.keys.push(nk); return nk; };
  L.removeKey = (a, node, t) => { a.keys = a.keys.filter(k => !(k.node === node && Math.abs(k.t - t) < 0.0005)); };
  // start an editor animation from an animation the FBX file already has (keys at every time the file has a key)
  L.fromModel = (a, model, animName) => {
    const src = model.anims[animName]; if (!src) return; a.keys = []; a.dur = src.dur || a.dur; const times = {};
    for (const tr of src.tracks) for (const c of tr.c) if (c) for (const t of c.t) (times[tr.n] = times[tr.n] || new Set()).add(Math.round(t * 1000) / 1000);
    const F = PRIM.FBX;
    for (const n in times) { const node = model.nodes[n]; for (const t of [...times[n]].sort((x, y) => x - y)) { const loc = { t: node.t.slice(), r: node.r.slice(), s: node.s.slice() };
      for (const tr of src.tracks) if (tr.n === +n) { const key = ["t", "r", "s"][tr.p]; for (let ax = 0; ax < 3; ax++) if (tr.c[ax]) loc[key][ax] = F.sampleCurve(tr.c[ax], t); }
      L.setKey(a, node.name, t, { p: loc.t, r: loc.r, s: loc.s }); } }
  };
})();
