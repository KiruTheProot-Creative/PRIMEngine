/* PRIMEngine grid maps (EasyFPS style). A map is a grid of cells, 2 m x 2 m (128 x 128 units), stacked in layers
   (stories) that are 2 m (128 units) tall. In each cell of a layer you can put:
     w  : a wall block (texture id; "" = default look). ws = { 0..3: texture } overrides single sides (0 N, 1 E, 2 S, 3 W)
     f  : a floor (texture id), h = floor height 0..15 in 16ths of a cell (8 units each)
          sh = "slope" | "stairs" (absent = flat), sd = direction it rises toward (0 N, 1 E, 2 S, 3 W), sr = rise 1..16 (16ths)
     c  : a ceiling (texture id) at the top of the layer
     li : light 0..100 (%), default 100
   Structure: map = { id, style: "grid", w, h, layers: [{ cells: { "x,y": cell } }], things: [], n: { t } }. Map settings are tags.
   World: X = cell x * 128, Z = cell y * 128 (y grows "south" = down on the editor), Y = up (layer L starts at L * 128).
   This file builds the triangles for drawing, the collision triangles, ray casts and the walking body (capsule). */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), G = (PRIM.Grid = {});
  const C = G.CELL = 128, LH = G.LAYER = 128, STEP = G.STEP = 8, CHUNK = 8;
  G.DIRS = [[0, -1], [1, 0], [0, 1], [-1, 0]]; G.DIRN = ["north", "east", "south", "west"];
  const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]], add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]], mul = (a, k) => [a[0] * k, a[1] * k, a[2] * k];
  const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2], cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
  const len = a => Math.hypot(a[0], a[1], a[2]), norm = a => { const l = len(a) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };
  G.v = { sub, add, mul, dot, cross, len, norm };

  // ------------------------------------------------------------ map data
  G.newMap = (id, w, h) => ({ id, style: "grid", w: w || 48, h: h || 48, layers: [{ cells: {} }], things: [], n: { t: 0 } });
  G.key = (x, y) => x + "," + y;
  G.cell = (m, L, x, y) => { const l = m.layers[L]; return l ? l.cells[x + "," + y] || null : null; };
  G.isWall = c => !!c && c.w !== undefined;
  G.hasFloor = c => !!c && c.w === undefined && c.f !== undefined;
  // a cell's own copy, created when missing; empty cells are removed again by G.tidy
  G.edit = (m, L, x, y) => { while (m.layers.length <= L) m.layers.push({ cells: {} }); const l = m.layers[L], k = x + "," + y; return l.cells[k] || (l.cells[k] = {}); };
  G.tidy = (m, L, x, y) => { const l = m.layers[L], k = x + "," + y, c = l && l.cells[k]; if (c && !Object.keys(c).length) delete l.cells[k]; };
  // how far along the rising direction a point of the cell is (u, v = 0..1 across the cell, u east, v south)
  const along = (d, u, v) => d === 1 ? u : d === 3 ? 1 - u : d === 2 ? v : 1 - v;
  // floor height (units above the layer bottom) at u, v inside the cell, or null when there is no floor
  G.hAt = (c, u, v) => {
    if (!G.hasFloor(c)) return null; const base = (c.h || 0) * STEP; if (!c.sh) return base; const r = Math.max(1, Math.min(16, c.sr || 8)), t = along(c.sd || 0, u, v);
    if (c.sh === "slope") return base + t * r * STEP; return base + (Math.min(7, Math.floor(t * 8)) + 1) * r;   // stairs: 8 steps, each r units high
  };
  // the floor height in world units under a world point on layer L (null = no floor there)
  G.floorY = (m, L, X, Z) => { const cx = Math.floor(X / C), cy = Math.floor(Z / C), c = G.cell(m, L, cx, cy), h = G.hAt(c, X / C - cx, Z / C - cy); return h === null ? null : L * LH + h; };
  G.layerOf = Y => Math.max(0, Math.floor((Y + 4) / LH));
  // the highest floor at or below Y at a world point (searching down the layers), for placing things
  G.groundY = (m, X, Z, Y) => { for (let L = Math.min(m.layers.length - 1, G.layerOf(Y === undefined ? 1e9 : Y)); L >= 0; L--) { const f = G.floorY(m, L, X, Z); if (f !== null && (Y === undefined || f <= Y + 4)) return f; } return 0; };
  G.bounds = m => { let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9; for (const l of m.layers) for (const k in l.cells) { const [x, y] = k.split(",").map(Number); x0 = Math.min(x0, x); y0 = Math.min(y0, y); x1 = Math.max(x1, x); y1 = Math.max(y1, y); } return x0 > x1 ? null : { x0, y0, x1, y1 }; };

  // a starter map: one 8 x 8 room with floor, ceiling and walls around it
  G.starter = (m, wallTex, floorTex, ceilTex) => {
    const cx = Math.floor(m.w / 2) - 4, cy = Math.floor(m.h / 2) - 4;
    for (let y = -1; y <= 8; y++) for (let x = -1; x <= 8; x++) { const c = G.edit(m, 0, cx + x, cy + y); if (x < 0 || y < 0 || x > 7 || y > 7) c.w = wallTex || ""; else { c.f = floorTex || ""; c.c = ceilTex || ""; } }
    return { x: (cx + 4) * C, z: (cy + 4) * C };
  };

  // ------------------------------------------------------------ geometry
  function tri(a, b, c, info) { const n = norm(cross(sub(b, a), sub(c, a))); return { a, b, c, n, info, mn: [Math.min(a[0], b[0], c[0]), Math.min(a[1], b[1], c[1]), Math.min(a[2], b[2], c[2])], mx: [Math.max(a[0], b[0], c[0]), Math.max(a[1], b[1], c[1]), Math.max(a[2], b[2], c[2])] }; }
  G.tri = tri;
  // build(m) -> { by: { "texId|chunk": [x,y,z,u,v,r,g,b ...] }, tris: [collision triangles with .info] }
  G.build = (m, opt) => {
    opt = opt || {}; const by = {}, tris = [], noCol = !!opt.noCollision;
    // one quad (p0..p3 around), uvs, colour k, info for picking / painting
    // f = the open side of the face (collision is one-sided: you can't get caught on the back of a ceiling or floor)
    const quad = (tex, L, cx, cy, p, uv, k, info, f) => {
      const key = (tex || "") + "|" + L + ":" + Math.floor(cx / CHUNK) + ":" + Math.floor(cy / CHUNK), a = by[key] || (by[key] = []);
      for (const i of [0, 1, 2, 0, 2, 3]) a.push(p[i][0], p[i][1], p[i][2], uv[i][0], uv[i][1], k, k, k);
      if (!noCol) { if (len(cross(sub(p[1], p[0]), sub(p[2], p[0]))) > 1e-6) { const t = tri(p[0], p[1], p[2], info); t.f = f; tris.push(t); } if (len(cross(sub(p[2], p[0]), sub(p[3], p[0]))) > 1e-6) { const t = tri(p[0], p[2], p[3], info); t.f = f; tris.push(t); } }
    };
    // the side of a cell facing direction d: its two ends A -> B as the viewer outside sees them left -> right
    const edge = (cx, cy, d) => { const x0 = cx * C, z0 = cy * C, x1 = x0 + C, z1 = z0 + C; return d === 0 ? [[x1, z0], [x0, z0]] : d === 1 ? [[x1, z1], [x1, z0]] : d === 2 ? [[x0, z1], [x1, z1]] : [[x0, z0], [x0, z1]]; };
    const shade = d => d === 1 || d === 3 ? 0.8 : 0.92;
    // the local u, v of a world point (nudged into the cell so stairs/edges sample the right side)
    const local = (cx, cy, X, Z) => [Math.min(0.999, Math.max(0.001, X / C - cx)), Math.min(0.999, Math.max(0.001, Z / C - cy))];
    m.layers.forEach((layer, L) => {
      const B = L * LH;
      for (const k in layer.cells) {
        const c = layer.cells[k], [cx, cy] = k.split(",").map(Number), li = (c.li === undefined ? 100 : +c.li) / 100, X0 = cx * C, Z0 = cy * C, X1 = X0 + C, Z1 = Z0 + C;
        if (G.isWall(c)) {
          for (let d = 0; d < 4; d++) {
            const n = G.cell(m, L, cx + G.DIRS[d][0], cy + G.DIRS[d][1]); if (G.isWall(n)) continue;
            const [A, Bp] = edge(cx, cy, d), tex = c.ws && c.ws[d] !== undefined ? c.ws[d] : c.w;
            quad(tex, L, cx, cy, [[A[0], B + LH, A[1]], [Bp[0], B + LH, Bp[1]], [Bp[0], B, Bp[1]], [A[0], B, A[1]]], [[0, 0], [1, 0], [1, 1], [0, 1]], li * shade(d), { kind: "wall", layer: L, x: cx, y: cy, side: d }, [G.DIRS[d][0], 0, G.DIRS[d][1]]);
          }
          const up = G.cell(m, L + 1, cx, cy); if (!G.isWall(up) && !(G.hasFloor(up) && !up.h && !up.sh)) quad(c.w, L, cx, cy, [[X0, B + LH, Z0], [X1, B + LH, Z0], [X1, B + LH, Z1], [X0, B + LH, Z1]], [[0, 0], [1, 0], [1, 1], [0, 1]], li, { kind: "walltop", layer: L, x: cx, y: cy }, [0, 1, 0]);
          const dn = L > 0 ? G.cell(m, L - 1, cx, cy) : null; if (L > 0 && !G.isWall(dn) && !(dn && dn.c !== undefined)) quad(c.w, L, cx, cy, [[X0, B, Z0], [X0, B, Z1], [X1, B, Z1], [X1, B, Z0]], [[0, 0], [0, 1], [1, 1], [1, 0]], li * 0.7, { kind: "wall", layer: L, x: cx, y: cy, side: 4 }, [0, -1, 0]);
          continue;
        }
        if (c.f !== undefined) {
          const info = { kind: "floor", layer: L, x: cx, y: cy }, hw = (X, Z) => { const q = local(cx, cy, X, Z); return B + G.hAt(c, q[0], q[1]); };
          if (c.sh === "stairs") {
            const r = Math.max(1, Math.min(16, c.sr || 8)), d = c.sd || 0, base = B + (c.h || 0) * STEP;
            for (let i = 0; i < 8; i++) {   // each step: its tread and the riser in front of the next one
              const t0 = i / 8, t1 = (i + 1) / 8, y = base + (i + 1) * r, rc = d === 1 ? [t0, t1, 0, 1] : d === 3 ? [1 - t1, 1 - t0, 0, 1] : d === 2 ? [0, 1, t0, t1] : [0, 1, 1 - t1, 1 - t0];
              const xa = X0 + rc[0] * C, xb = X0 + rc[1] * C, za = Z0 + rc[2] * C, zb = Z0 + rc[3] * C;
              quad(c.f, L, cx, cy, [[xa, y, za], [xb, y, za], [xb, y, zb], [xa, y, zb]], [[xa / C, za / C], [xb / C, za / C], [xb / C, zb / C], [xa / C, zb / C]], li, info, [0, 1, 0]);
              if (i < 7) { const yt = y + r, tt = t1, P = d === 1 ? [[X0 + tt * C, Z1], [X0 + tt * C, Z0]] : d === 3 ? [[X1 - tt * C, Z0], [X1 - tt * C, Z1]] : d === 2 ? [[X0, Z0 + tt * C], [X1, Z0 + tt * C]] : [[X1, Z1 - tt * C], [X0, Z1 - tt * C]];
                quad(c.f, L, cx, cy, [[P[0][0], yt, P[0][1]], [P[1][0], yt, P[1][1]], [P[1][0], y, P[1][1]], [P[0][0], y, P[0][1]]], [[0, (B + LH - yt) / C], [1, (B + LH - yt) / C], [1, (B + LH - y) / C], [0, (B + LH - y) / C]], li * 0.78, info, [-G.DIRS[d][0], 0, -G.DIRS[d][1]]); }
            }
          } else quad(c.f, L, cx, cy, [[X0, hw(X0, Z0), Z0], [X1, hw(X1, Z0), Z0], [X1, hw(X1, Z1), Z1], [X0, hw(X0, Z1), Z1]], [[X0 / C, Z0 / C], [X1 / C, Z0 / C], [X1 / C, Z1 / C], [X0 / C, Z1 / C]], li, info, [0, 1, 0]);
          // the sides of a raised floor (or slope / stairs) toward lower neighbours
          for (let d = 0; d < 4; d++) {
            const nx = cx + G.DIRS[d][0], ny = cy + G.DIRS[d][1], n = G.cell(m, L, nx, ny); if (G.isWall(n)) continue;
            const [A, Bp] = edge(cx, cy, d), segs = c.sh === "stairs" || (n && n.sh === "stairs") ? 8 : 1;
            for (let j = 0; j < segs; j++) {
              const s0 = j / segs, s1 = (j + 1) / segs, P0 = [A[0] + (Bp[0] - A[0]) * s0, A[1] + (Bp[1] - A[1]) * s0], P1 = [A[0] + (Bp[0] - A[0]) * s1, A[1] + (Bp[1] - A[1]) * s1], M = [(P0[0] + P1[0]) / 2, (P0[1] + P1[1]) / 2];
              const mine = P => { const q = local(cx, cy, segs > 1 ? M[0] : P[0], segs > 1 ? M[1] : P[1]); return G.hAt(c, q[0], q[1]); };
              const theirs = P => { if (!G.hasFloor(n)) return 0; const q = local(nx, ny, segs > 1 ? M[0] : P[0], segs > 1 ? M[1] : P[1]); return G.hAt(n, q[0], q[1]); };
              const t0 = mine(P0), t1 = mine(P1), b0 = Math.min(t0, theirs(P0)), b1 = Math.min(t1, theirs(P1)); if (t0 - b0 + t1 - b1 < 0.01) continue;
              quad(c.f, L, cx, cy, [[P0[0], B + t0, P0[1]], [P1[0], B + t1, P1[1]], [P1[0], B + b1, P1[1]], [P0[0], B + b0, P0[1]]], [[s0, (LH - t0) / C], [s1, (LH - t1) / C], [s1, (LH - b1) / C], [s0, (LH - b0) / C]], li * shade(d) * 0.9, { kind: "floor", layer: L, x: cx, y: cy, side: d }, [G.DIRS[d][0], 0, G.DIRS[d][1]]);
            }
          }
        }
        if (c.c !== undefined) { const y = B + LH - 1; quad(c.c, L, cx, cy, [[X0, y, Z0], [X0, y, Z1], [X1, y, Z1], [X1, y, Z0]], [[X0 / C, Z0 / C], [X0 / C, Z1 / C], [X1 / C, Z1 / C], [X1 / C, Z0 / C]], li * 0.8, { kind: "ceil", layer: L, x: cx, y: cy }, [0, -1, 0]); }
      }
    });
    // above a door that is lower than the story: a lintel (the wall closes over the doorway), textured like the walls beside it
    for (const f of opt.doors || []) {
      const L = f.layer || 0, top = L * LH + LH, cx = Math.floor(f.cx / C), cy = Math.floor(f.cy / C); if (f.fy + f.h >= top - 0.5) continue;
      const side = [[cx - f.ux, cy - f.uy], [cx + f.ux, cy + f.uy]].map(([x, y]) => G.cell(m, L, x, y)).find(G.isWall), tex = side ? side.w : "";
      const [mn, mx] = G.doorBox(f); mn[1] = f.fy + f.h; mx[1] = top; const P = (x, y, z) => [x, y, z], k = 0.85, info = { kind: "wall", layer: L, x: cx, y: cy, side: 5 };
      const faces = [[P(mn[0], mx[1], mn[2]), P(mx[0], mx[1], mn[2]), P(mx[0], mn[1], mn[2]), P(mn[0], mn[1], mn[2]), [0, 0, -1]], [P(mx[0], mx[1], mx[2]), P(mn[0], mx[1], mx[2]), P(mn[0], mn[1], mx[2]), P(mx[0], mn[1], mx[2]), [0, 0, 1]],
        [P(mn[0], mx[1], mx[2]), P(mn[0], mx[1], mn[2]), P(mn[0], mn[1], mn[2]), P(mn[0], mn[1], mx[2]), [-1, 0, 0]], [P(mx[0], mx[1], mn[2]), P(mx[0], mx[1], mx[2]), P(mx[0], mn[1], mx[2]), P(mx[0], mn[1], mn[2]), [1, 0, 0]],
        [P(mn[0], mn[1], mn[2]), P(mx[0], mn[1], mn[2]), P(mx[0], mn[1], mx[2]), P(mn[0], mn[1], mx[2]), [0, -1, 0]]];
      for (const q of faces) { const w = Math.max(Math.abs(q[1][0] - q[0][0]), Math.abs(q[1][2] - q[0][2])) / C, hh = (q[0][1] - q[3][1]) / C; quad(tex, L, cx, cy, q.slice(0, 4), [[0, 0], [w, 0], [w, hh], [0, hh]], k, info, q[4]); }
    }
    return { by, tris };
  };

  // ------------------------------------------------------------ spatial lookup (cells of 256 units on X / Z)
  const CS = 256;
  G.index = list => { const g = new Map(); list.forEach((t, i) => { for (let x = Math.floor(t.mn[0] / CS); x <= Math.floor(t.mx[0] / CS); x++) for (let z = Math.floor(t.mn[2] / CS); z <= Math.floor(t.mx[2] / CS); z++) { const k = x + "," + z; let a = g.get(k); if (!a) g.set(k, a = []); a.push(i); } }); return { g, list, stamp: new Uint32Array(list.length), st: 0 }; };
  G.near = (ix, mn, mx) => { const out = [], s = ++ix.st; for (let x = Math.floor(mn[0] / CS); x <= Math.floor(mx[0] / CS); x++) for (let z = Math.floor(mn[2] / CS); z <= Math.floor(mx[2] / CS); z++) { const a = ix.g.get(x + "," + z); if (a) for (const i of a) if (ix.stamp[i] !== s) { ix.stamp[i] = s; const t = ix.list[i]; if (t.mx[1] >= mn[1] && t.mn[1] <= mx[1]) out.push(t); } } return out; };

  // ray against one triangle (Moller-Trumbore): distance or -1
  const hitTri = (t, o, d) => { const e1 = sub(t.b, t.a), e2 = sub(t.c, t.a), p = cross(d, e2), det = dot(e1, p); if (Math.abs(det) < 1e-9) return -1; const inv = 1 / det, s = sub(o, t.a), u = dot(s, p) * inv; if (u < 0 || u > 1) return -1; const q = cross(s, e1), v = dot(d, q) * inv; if (v < 0 || u + v > 1) return -1; return dot(e2, q) * inv; };
  // nearest triangle along a ray: { t, p, tri, info } or null. Walks the 256-unit cells along the ray, so long rays stay cheap.
  G.raycast = (ix, o, d, maxT, extra) => {
    let best = null; const test = t => { const tt = hitTri(t, o, d); if (tt > 0.01 && tt < (best ? best.t : maxT)) best = { t: tt, tri: t }; };
    if (ix) {
      const s = ++ix.st; let x = Math.floor(o[0] / CS), z = Math.floor(o[2] / CS); const sx = d[0] > 0 ? 1 : -1, sz = d[2] > 0 ? 1 : -1;
      const tdx = Math.abs(d[0]) > 1e-9 ? CS / Math.abs(d[0]) : 1e30, tdz = Math.abs(d[2]) > 1e-9 ? CS / Math.abs(d[2]) : 1e30;
      let tx = Math.abs(d[0]) > 1e-9 ? ((sx > 0 ? (x + 1) * CS - o[0] : o[0] - x * CS) / Math.abs(d[0])) : 1e30, tz = Math.abs(d[2]) > 1e-9 ? ((sz > 0 ? (z + 1) * CS - o[2] : o[2] - z * CS) / Math.abs(d[2])) : 1e30, tin = 0;
      for (let guard = 0; guard < 4096; guard++) {
        const a = ix.g.get(x + "," + z); if (a) for (const i of a) if (ix.stamp[i] !== s) { ix.stamp[i] = s; test(ix.list[i]); }
        const tnext = Math.min(tx, tz); if (best && best.t <= tnext) break; if (tnext > maxT) break; tin = tnext;
        if (tx < tz) { x += sx; tx += tdx; } else { z += sz; tz += tdz; }
      }
    }
    if (extra) for (const t of extra) test(t);
    if (best) { best.p = add(o, mul(d, best.t)); best.info = best.tri.info || null; } return best;
  };

  // closest point on a triangle to p (Ericson, Real-Time Collision Detection 5.1.5)
  G.closest = (p, a, b, c) => {
    const ab = sub(b, a), ac = sub(c, a), ap = sub(p, a), d1 = dot(ab, ap), d2 = dot(ac, ap); if (d1 <= 0 && d2 <= 0) return a;
    const bp = sub(p, b), d3 = dot(ab, bp), d4 = dot(ac, bp); if (d3 >= 0 && d4 <= d3) return b;
    const vc = d1 * d4 - d3 * d2; if (vc <= 0 && d1 >= 0 && d3 <= 0) return add(a, mul(ab, d1 / (d1 - d3)));
    const cp = sub(p, c), d5 = dot(ab, cp), d6 = dot(ac, cp); if (d6 >= 0 && d5 <= d6) return c;
    const vb = d5 * d2 - d1 * d6; if (vb <= 0 && d2 >= 0 && d6 <= 0) return add(a, mul(ac, d2 / (d2 - d6)));
    const va = d3 * d6 - d5 * d4; if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) return add(b, mul(sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
    const den = 1 / (va + vb + vc), v = vb * den, w = vc * den; return add(a, add(mul(ab, v), mul(ac, w)));
  };
  // a box (mn, mx corners) as 12 triangles: closed doors and other solid blocks
  G.boxTris = (mn, mx, info) => {
    const [x0, y0, z0] = mn, [x1, y1, z1] = mx, P = (x, y, z) => [x, y, z], out = [], q = (a, b, c, d) => { out.push(tri(a, b, c, info), tri(a, c, d, info)); };
    q(P(x0, y0, z0), P(x1, y0, z0), P(x1, y1, z0), P(x0, y1, z0)); q(P(x0, y0, z1), P(x0, y1, z1), P(x1, y1, z1), P(x1, y0, z1));
    q(P(x0, y0, z0), P(x0, y1, z0), P(x0, y1, z1), P(x0, y0, z1)); q(P(x1, y0, z0), P(x1, y0, z1), P(x1, y1, z1), P(x1, y1, z0));
    q(P(x0, y1, z0), P(x1, y1, z0), P(x1, y1, z1), P(x0, y1, z1)); q(P(x0, y0, z0), P(x0, y0, z1), P(x1, y0, z1), P(x1, y0, z0)); return out;
  };

  // ------------------------------------------------------------ the walking body: feet at pos, a capsule of radius r and height h.
  // body = { pos, vel, r, h, step, maxSlope (cos of the steepest walkable slope), ground (out), groundN (out) }; extra = moving triangles (doors)
  G.push = (body, ix, extra) => {
    const r = body.r, h = body.h, n = Math.max(2, Math.ceil((h - 2 * r) / r) + 1), gap = (h - 2 * r) / (n - 1); let ground = null, down = false;
    for (let it = 0; it < 4; it++) {
      let moved = false; const mn = [body.pos[0] - r - 2, body.pos[1] - 2, body.pos[2] - r - 2], mx = [body.pos[0] + r + 2, body.pos[1] + h + 2, body.pos[2] + r + 2];
      const list = ix ? G.near(ix, mn, mx) : []; if (extra) for (const t of extra) if (t.mx[0] >= mn[0] && t.mn[0] <= mx[0] && t.mx[1] >= mn[1] && t.mn[1] <= mx[1] && t.mx[2] >= mn[2] && t.mn[2] <= mx[2]) list.push(t);
      for (const t of list) for (let i = 0; i < n; i++) {
        const top = i === n - 1, c = [body.pos[0], body.pos[1] + r + i * gap + (top ? r * 0.65 : 0), body.pos[2]], q = G.closest(c, t.a, t.b, t.c), dv = sub(c, q), d = len(dv); if (d >= r + (i === 0 ? 1.5 : 0) || d < 1e-6 || (t.f && dot(dv, t.f) < 0)) continue;
        let nn = mul(dv, 1 / d); const rr = i > 0 && nn[1] < -0.3 ? r * 0.35 : r; if (d >= rr && i > 0) continue;   // the head is narrow: walking under the edge of a ceiling (stairwells) works
        if (i === 0 && nn[1] > body.maxSlope) { ground = ground && ground[1] > nn[1] ? ground : nn; if (d < r) { body.pos[1] += (r - d) / nn[1]; moved = true; } if (body.vel[1] < 0) body.vel[1] = 0; continue; }   // standing: pushed straight up, no sliding down
        if (d >= rr) continue;
        if (nn[1] > 0 && nn[1] <= body.maxSlope && i === 0) nn = norm([nn[0], Math.min(nn[1], 0.1), nn[2]]);   // too steep: acts like a wall
        if (nn[1] < -0.5) { down = true; if (body.vel[1] > 0) body.vel[1] = 0; }   // head bump
        const k = rr - d; body.pos[0] += nn[0] * k; body.pos[1] += nn[1] * k; body.pos[2] += nn[2] * k; moved = true;
        const vn = dot(body.vel, nn); if (vn < 0) body.vel = sub(body.vel, mul(nn, vn));
      }
      if (!moved) break;
    }
    body.ground = !!ground; body.groundN = ground; body.squeeze = !!ground && down; return body;
  };
  G.move = (body, dt, ix, extra) => {
    const sp = len(body.vel) * dt, steps = Math.max(1, Math.min(12, Math.ceil(sp / (body.r * 0.6)))), sdt = dt / steps, was = body.ground;
    for (let s = 0; s < steps; s++) {
      const from = body.pos.slice(), want = mul(body.vel, sdt); body.pos = add(body.pos, want); G.push(body, ix, extra);
      if (body.squeeze) { body.pos = from; G.push(body, ix, extra); if (body.squeeze) body.pos = from; body.ground = true; continue; }   // no room for the head here: do not go there
      // step up: blocked sideways while on the ground -> try again from higher up, then settle back down
      const hw = Math.hypot(want[0], want[2]), got = Math.hypot(body.pos[0] - from[0], body.pos[2] - from[2]);
      if ((was || body.ground) && hw > 0.01 && got < hw * 0.6 && body.step > 0) {
        const tryB = { pos: [from[0], from[1] + body.step, from[2]], vel: [body.vel[0], 0, body.vel[2]], r: body.r, h: body.h, maxSlope: body.maxSlope, step: 0 }; G.push(tryB, ix, extra);
        if (tryB.pos[1] > from[1] + 1) { tryB.pos = add(tryB.pos, [want[0], 0, want[2]]); G.push(tryB, ix, extra); for (let d = 0; d < body.step + 2 && !tryB.ground; d += 3) { tryB.pos[1] -= 3; G.push(tryB, ix, extra); }
          const got2 = Math.hypot(tryB.pos[0] - from[0], tryB.pos[2] - from[2]); if (got2 > got + 0.5 && tryB.ground) { body.pos = tryB.pos; body.ground = true; body.groundN = tryB.groundN; } }
      }
    }
    // stay on the ground when walking down slopes and steps
    if (was && !body.ground && body.vel[1] <= 0) { const t = { pos: body.pos.slice(), vel: [0, -1, 0], r: body.r, h: body.h, maxSlope: body.maxSlope }; t.pos[1] -= body.step + 2; G.push(t, ix, extra); if (t.ground && body.pos[1] - t.pos[1] <= body.step + 2) { body.pos = t.pos; body.ground = true; body.groundN = t.groundN; body.vel[1] = 0; } }
    return body;
  };

  // ------------------------------------------------------------ placed things
  // where a placed thing stands: { cx, cy (world X / Z), fy (floor), ux, uy (facing sideways), nx, ny (facing forward), door }
  G.thingFrame = (m, T, th) => {
    const a = (th.ang || 0) * Math.PI / 180, L = th.layer || 0;
    if (T && T.get(th.prop + ".type") === "door") {   // doors are blocks: W cells wide (along ang: 0 east, 90 south, 180 west, 270 north from the clicked cell) and H stories tall, filling them to the top
      const ax = (Math.floor(th.x / C) + 0.5) * C, ay = (Math.floor(th.y / C) + 0.5) * C, q = Math.round((th.ang || 0) / 90) * Math.PI / 2, r = v => Math.round(v), ux = r(Math.cos(q)), uy = -r(Math.sin(q));
      const W = Math.max(1, Math.min(8, Math.round(+T.get(th.prop + ".doorCellsW") || 1))), H = Math.max(1, Math.min(4, Math.round(+T.get(th.prop + ".doorCellsH") || 1))), f0 = G.floorY(m, L, ax, ay), fy = f0 === null ? L * LH : f0;
      const cells = []; for (let i = 0; i < W; i++) cells.push([Math.floor(ax / C) + ux * i, Math.floor(ay / C) + uy * i]);
      return { cx: ax + ux * (W - 1) * C / 2, cy: ay + uy * (W - 1) * C / 2, fy, layer: L, ux, uy, nx: r(Math.sin(q)), ny: r(Math.cos(q)), w: W * C, h: (L + H) * LH - fy, thick: 16, door: true, cells, W, H };
    }
    const f = G.floorY(m, L, th.x, th.y);
    return { cx: th.x, cy: th.y, fy: f === null ? L * LH : f, ux: Math.cos(a), uy: -Math.sin(a), nx: Math.sin(a), ny: Math.cos(a), door: false };
  };
  G.doorBox = f => { const hx = Math.abs(f.ux) * f.w / 2 + Math.abs(f.nx) * f.thick / 2, hz = Math.abs(f.uy) * f.w / 2 + Math.abs(f.ny) * f.thick / 2; return [[f.cx - hx, f.fy, f.cy - hz], [f.cx + hx, f.fy + f.h, f.cy + hz]]; };
})();
