// PRIM.MapGeo: room geometry helpers for the map editor.
// - addRoom: a newly drawn room is cut so it never overlaps an existing room (the overlapping part stays with the old room),
//   then every node that lands on another room's wall splits that wall, so touching walls share the same two end points.
// - Rooms keep their own nodes; a wall that two rooms share is recognised by position (see render3d ekey).
(function () {
  const PRIM = (window.PRIM = window.PRIM || {}), G = (PRIM.MapGeo = {}), EPS = 1e-6, R3 = v => Math.round(v * 1000) / 1000;

  const area = p => { let a = 0; for (let i = 0; i < p.length; i++) { const q = p[(i + 1) % p.length]; a += p[i].x * q.y - q.x * p[i].y; } return a / 2; };
  const ccw = p => area(p) < 0 ? p.slice().reverse() : p.slice();
  const inside = (pt, poly) => { let c = false; for (let a = 0, b = poly.length - 1; a < poly.length; b = a++) if ((poly[a].y > pt.y) !== (poly[b].y > pt.y) && pt.x < (poly[b].x - poly[a].x) * (pt.y - poly[a].y) / (poly[b].y - poly[a].y) + poly[a].x) c = !c; return c; };
  const same = (a, b) => Math.abs(a.x - b.x) < 0.01 && Math.abs(a.y - b.y) < 0.01;
  const onSeg = (p, a, b) => { const dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy; if (l2 < EPS) return null; const t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2; if (t <= 0 || t >= 1) return null; const x = a.x + dx * t, y = a.y + dy * t; return Math.hypot(p.x - x, p.y - y) < 0.01 ? t : null; };

  // insert into poly the points where it meets the edges / nodes of other
  function split(poly, other) {
    const out = [];
    for (let i = 0; i < poly.length; i++) {
      const a = poly[i], b = poly[(i + 1) % poly.length], hits = [], dx = b.x - a.x, dy = b.y - a.y; out.push({ x: a.x, y: a.y });
      for (let j = 0; j < other.length; j++) {
        const c = other[j], d = other[(j + 1) % other.length], ex = d.x - c.x, ey = d.y - c.y, den = dx * ey - dy * ex;
        if (Math.abs(den) > EPS) { const t = ((c.x - a.x) * ey - (c.y - a.y) * ex) / den, u = ((c.x - a.x) * dy - (c.y - a.y) * dx) / den; if (t > 1e-9 && t < 1 - 1e-9 && u >= -1e-9 && u <= 1 + 1e-9) hits.push({ t, x: R3(a.x + dx * t), y: R3(a.y + dy * t) }); }
        else for (const q of [c, d]) { const t = onSeg(q, a, b); if (t !== null) hits.push({ t, x: q.x, y: q.y }); }
      }
      hits.sort((p, q) => p.t - q.t); for (const h of hits) if (!same(h, out[out.length - 1]) && !same(h, b)) out.push({ x: h.x, y: h.y });
    }
    return out;
  }
  const simplify = p => { let q = p.filter((v, i) => !same(v, p[(i + 1) % p.length])), ch = true; while (ch && q.length > 3) { ch = false; for (let i = 0; i < q.length; i++) { const a = q[(i + q.length - 1) % q.length], b = q[i], c = q[(i + 1) % q.length]; if (Math.abs((b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x)) < 1e-4) { q.splice(i, 1); ch = true; break; } } } return q; };

  // A minus B (both counter-clockwise). Returns { loops } or { error }
  function diff(A, B) {
    const A2 = split(A, B), B2 = split(B, A), eps = 0.05, edges = [], seen = {}, key = (p, q) => p.x + "," + p.y + ">" + q.x + "," + q.y;
    const add = (p, q) => { const k = key(p, q); if (!seen[k]) { seen[k] = 1; edges.push({ p, q, used: false }); } };
    const mid = (p, q) => ({ x: (p.x + q.x) / 2, y: (p.y + q.y) / 2 }), left = (p, q) => { const l = Math.hypot(q.x - p.x, q.y - p.y) || 1; return { x: -(q.y - p.y) / l, y: (q.x - p.x) / l }; };
    for (let i = 0; i < A2.length; i++) { const p = A2[i], q = A2[(i + 1) % A2.length], m = mid(p, q), n = left(p, q); if (!inside({ x: m.x + n.x * eps, y: m.y + n.y * eps }, B)) add(p, q); }
    for (let i = 0; i < B2.length; i++) { const p = B2[i], q = B2[(i + 1) % B2.length], m = mid(p, q), n = left(p, q); if (inside({ x: m.x - n.x * eps, y: m.y - n.y * eps }, A)) add(q, p); }
    const loops = [];
    for (const e0 of edges) {
      if (e0.used) continue; const loop = [e0.p]; let e = e0, guard = 0; e.used = true;
      for (;;) { if (++guard > 5000) return { error: "geometry" }; if (same(e.q, e0.p)) break; loop.push(e.q); const nx = edges.find(x => !x.used && same(x.p, e.q)); if (!nx) return { error: "geometry" }; nx.used = true; e = nx; }
      const s = simplify(loop); if (s.length < 3) continue; const a = area(s); if (a < -1) return { error: "hole" }; if (a > 1) loops.push(s);
    }
    return { loops };
  }

  const vpoly = (m, sec) => sec.v.map(id => m.verts.find(v => v.id === id)).filter(Boolean).map(v => ({ x: v.x, y: v.y }));

  // put a node into wall a->next of sector sec at point p; the new node copies the wall settings of a
  function splitEdge(project, m, T, sec, i, p) {
    const a = m.verts.find(v => v.id === sec.v[i]), over = {}; for (const pr of PRIM.Schema.VERT) over[pr.k] = T.get(m.id + "." + a.id + "." + pr.k);
    const nv = PRIM.Schema.addVert(project, m, p.x, p.y, over); sec.v.splice(i + 1, 0, nv.id); return nv;
  }
  // every node that lies on the inside of another room's wall splits that wall (so shared walls have identical end points)
  G.stitch = function (project, m, T) {
    let changed = true, guard = 0;
    while (changed && guard++ < 50) {
      changed = false;
      outer: for (const X of m.sectors) for (const vid of X.v) { const v = m.verts.find(o => o.id === vid); if (!v) continue;
        for (const Y of m.sectors) { if (Y === X) continue; for (let i = 0; i < Y.v.length; i++) { const a = m.verts.find(o => o.id === Y.v[i]), b = m.verts.find(o => o.id === Y.v[(i + 1) % Y.v.length]); if (!a || !b) continue; if (onSeg(v, a, b) !== null) { splitEdge(project, m, T, Y, i, { x: v.x, y: v.y }); changed = true; break outer; } } } }
    }
  };

  // pts: the drawn outline. Returns { sectors:[...], cut:bool } or { error:"text" }
  G.addRoom = function (project, m, T, pts) {
    const S = PRIM.Schema; if (pts.length < 3) return { error: "A room needs at least 3 nodes." };
    let pieces = [ccw(pts.map(p => ({ x: p.x, y: p.y })))]; const orig = area(pieces[0]);
    if (Math.abs(area(pieces[0])) < 1) return { error: "That room has no area." };
    for (const sec of m.sectors) {
      const B = ccw(vpoly(m, sec)); if (B.length < 3) continue; const next = [];
      for (const A of pieces) { const r = diff(A, B); if (r.error === "hole") return { error: "This room would completely surround another room. Draw it in two parts instead." }; if (r.error) return { error: "Could not combine this room with room “" + sec.id + "”. Try a simpler outline." }; next.push(...r.loops); }
      pieces = next;
    }
    if (!pieces.length) return { error: "That room would lie completely inside another room." };
    const made = []; for (const poly of pieces) { const ids = poly.map(p => S.addVert(project, m, p.x, p.y).id); made.push(S.addSector(project, m, ids)); }
    G.stitch(project, m, T); return { sectors: made, cut: Math.abs(pieces.reduce((s, p) => s + area(p), 0) - orig) > 1 };
  };
})();
