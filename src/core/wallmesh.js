/* PRIMEngine: wall types. A wall type is a small mesh that fills a panel of 96 x 96 units (3 x 3 tiles = 1.5 m x 1.5 m).
   Panel space: x runs along the wall (0..96), y goes up (0..96), z comes out of the wall toward the room (negative = into the wall).
   A wall = { id, name, n:{v,f}, verts:[{id,x,y,z}], faces:[{id, v:[vertex ids in a loop]}] }; each face has tags <wall>.<face>.tex / .color. */
(function () {
  const PRIM = (window.PRIM = window.PRIM || {}), W = (PRIM.WallMesh = {}), S = PRIM.Schema;
  W.SIZE = 96; W.UV = 64;   // one texture repeat = 64 units
  const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]], cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]], dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  const norm = a => { const l = Math.hypot(a[0], a[1], a[2]) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };
  const hex = h => { h = String(h || "#ffffff"); return [parseInt(h.slice(1, 3), 16) / 255, parseInt(h.slice(3, 5), 16) / 255, parseInt(h.slice(5, 7), 16) / 255]; };
  W.vert = (w, id) => w.verts.find(v => v.id === id); W.face = (w, id) => w.faces.find(f => f.id === id);
  W.pos = (w, id) => { const v = W.vert(w, id); return v ? [v.x, v.y, v.z] : [0, 0, 0]; };
  W.create = (project, name, over) => {
    const ids = project.walls.map(x => x.id), id = S.uid("wall", ids), w = { id, name: name || id, n: { v: 0, f: 0 }, verts: [], faces: [] }; project.walls.push(w);
    const vs = [[0, 0], [W.SIZE, 0], [W.SIZE, W.SIZE], [0, W.SIZE]].map(([x, y]) => W.addVert(w, x, y, 0)); W.addFace(project, w, vs.map(v => v.id), over); return w;
  };
  W.addVert = (w, x, y, z) => { const v = { id: "v" + (++w.n.v), x, y, z }; w.verts.push(v); return v; };
  W.addFace = (project, w, ids, over) => { const f = { id: "f" + (++w.n.f), v: ids.slice() }; w.faces.push(f); S.defineWallFace(project, w, f, over); return f; };
  W.remove = (project, w) => { project.walls.splice(project.walls.indexOf(w), 1); for (const k of Object.keys(project.tags)) if (k.startsWith(w.id + ".")) delete project.tags[k];
    for (const m of project.maps || []) for (const v of m.verts) { const k = m.id + "." + v.id + ".rows", r = project.tags[k]; if (r && r.value) r.value = String(r.value).split(",").map(x => x === w.id ? "" : x).join(","); } };
  // normal of a face (Newell), centre of a face
  W.normal = (w, f) => { let n = [0, 0, 0]; const L = f.v.map(id => W.pos(w, id)); for (let i = 0; i < L.length; i++) { const a = L[i], b = L[(i + 1) % L.length]; n[0] += (a[1] - b[1]) * (a[2] + b[2]); n[1] += (a[2] - b[2]) * (a[0] + b[0]); n[2] += (a[0] - b[0]) * (a[1] + b[1]); } return norm(n); };
  W.centre = (w, f) => { const c = [0, 0, 0]; for (const id of f.v) { const p = W.pos(w, id); c[0] += p[0]; c[1] += p[1]; c[2] += p[2]; } return c.map(x => x / f.v.length); };
  // triangles of every face, in panel space: [{ face, tex, tint:[r,g,b], p:[[x,y,z] x3], uv:[[u,v] x3] }]  (faces are fans: they stay convex)
  W.tris = (w, T) => {
    const out = [];
    for (const f of w.faces) {
      if (f.v.length < 3) continue; const L = f.v.map(id => W.pos(w, id)), n = W.normal(w, f), t = norm(Math.abs(n[1]) > 0.9 ? [1, 0, 0] : cross([0, 1, 0], n)), b = cross(n, t), tex = T.get(w.id + "." + f.id + ".tex") || "", tint = hex(T.get(w.id + "." + f.id + ".color"));
      const uv = p => [dot(p, t) / W.UV, -dot(p, b) / W.UV];
      for (let i = 1; i + 1 < L.length; i++) { const p = [L[0], L[i], L[i + 1]]; out.push({ face: f.id, tex, tint, p, uv: p.map(uv) }); }
    }
    return out;
  };
  // ---- undo snapshots (structure + this wall's tags)
  W.snap = (project, w) => { const tags = {}; for (const k of Object.keys(project.tags)) if (k.startsWith(w.id + ".")) tags[k] = JSON.parse(JSON.stringify(project.tags[k])); return JSON.stringify({ n: w.n, verts: w.verts, faces: w.faces, tags }); };
  W.restore = (project, w, str) => { const s = JSON.parse(str); w.n = s.n; w.verts = s.verts; w.faces = s.faces; for (const k of Object.keys(project.tags)) if (k.startsWith(w.id + ".")) delete project.tags[k]; Object.assign(project.tags, s.tags); };
  const copyTags = (project, w, from, to) => { for (const p of S.WFACE) { const a = project.tags[w.id + "." + from.id + "." + p.k], b = project.tags[w.id + "." + to.id + "." + p.k]; if (a && b) b.value = a.value; } };
  W.cleanup = (project, w) => { const used = new Set(); for (const f of w.faces) f.v.forEach(i => used.add(i)); w.verts = w.verts.filter(v => used.has(v.id)); };
  W.deleteFaces = (project, w, ids) => { const s = new Set(ids); for (const f of w.faces.filter(f => s.has(f.id))) for (const k of Object.keys(project.tags)) if (k.startsWith(w.id + "." + f.id + ".")) delete project.tags[k]; w.faces = w.faces.filter(f => !s.has(f.id)); W.cleanup(project, w); };
  const ek = (a, b) => a < b ? a + "|" + b : b + "|" + a;
  // pull the chosen faces out of the wall: copies of their corner points move with them, side faces join them to the wall. Returns { moved: [vertex ids], dir: average normal }
  W.extrude = (project, w, ids) => {
    const sel = w.faces.filter(f => ids.includes(f.id)), cnt = {}, map = {}, n = [0, 0, 0];
    for (const f of sel) { const fn = W.normal(w, f); n[0] += fn[0]; n[1] += fn[1]; n[2] += fn[2]; for (let i = 0; i < f.v.length; i++) { const k = ek(f.v[i], f.v[(i + 1) % f.v.length]); cnt[k] = (cnt[k] || 0) + 1; } }
    for (const f of sel) for (const id of f.v) if (!map[id]) { const o = W.vert(w, id), c = W.addVert(w, o.x, o.y, o.z); map[id] = c.id; }
    for (const f of sel) for (let i = 0; i < f.v.length; i++) { const a = f.v[i], b = f.v[(i + 1) % f.v.length]; if (cnt[ek(a, b)] !== 1) continue; const sf = W.addFace(project, w, [a, b, map[b], map[a]]); copyTags(project, w, f, sf); }
    for (const f of sel) f.v = f.v.map(id => map[id]);
    return { moved: Object.values(map), dir: norm(n) };
  };
  // shrink each chosen face toward its middle and add a ring of faces around it
  W.inset = (project, w, ids, amount) => {
    for (const f of w.faces.filter(f => ids.includes(f.id))) {
      const c = W.centre(w, f), nv = f.v.map(id => { const p = W.pos(w, id), d = sub(c, p), l = Math.hypot(d[0], d[1], d[2]) || 1, k = Math.min(0.9, amount / l), v = W.addVert(w, p[0] + d[0] * k, p[1] + d[1] * k, p[2] + d[2] * k); return v.id; }), old = f.v.slice();
      for (let i = 0; i < old.length; i++) { const j = (i + 1) % old.length, rf = W.addFace(project, w, [old[i], old[j], nv[j], nv[i]]); copyTags(project, w, f, rf); }
      f.v = nv;
    }
  };
  // cut quads in two with a line across the edges that run mostly along `axis` (0 = x, 1 = y, 2 = z), at fraction t of the way from the low end. Neighbours get the new point so nothing cracks.
  W.cut = (project, w, ids, axis, t) => {
    let did = 0;
    for (const f of w.faces.filter(f => ids.includes(f.id))) {
      if (f.v.length !== 4) continue; const P = f.v.map(id => W.pos(w, id)), len = i => Math.abs(P[(i + 1) % 4][axis] - P[i][axis]), pair = len(0) + len(2) >= len(1) + len(3) ? 0 : 1, e1 = pair, e2 = pair + 2;
      const mk = i => { const a = f.v[i], b = f.v[(i + 1) % 4], pa = W.pos(w, a), pb = W.pos(w, b), lo = pa[axis] <= pb[axis], A = lo ? a : b, B = lo ? b : a, PA = lo ? pa : pb, PB = lo ? pb : pa, p = W.addVert(w, PA[0] + (PB[0] - PA[0]) * t, PA[1] + (PB[1] - PA[1]) * t, PA[2] + (PB[2] - PA[2]) * t);
        for (const g of w.faces) { if (g === f) continue; for (let k = 0; k < g.v.length; k++) { const x = g.v[k], y = g.v[(k + 1) % g.v.length]; if ((x === a && y === b) || (x === b && y === a)) { g.v.splice(k + 1, 0, p.id); break; } } } return p.id; };
      const p = mk(e1), q = mk(e2), v = f.v, o = [v[e1], v[(e1 + 1) % 4], v[(e1 + 2) % 4], v[(e1 + 3) % 4]];   // o[0]-o[1] is the first cut edge, o[2]-o[3] the opposite one
      const nf = W.addFace(project, w, [p, o[1], o[2], q]); copyTags(project, w, f, nf); f.v = [o[0], p, q, o[3]]; did++;
    }
    return did;
  };
  // does this wall have anything to draw
  W.byId = (project, id) => (project.walls || []).find(x => x.id === id);
})();
