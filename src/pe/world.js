/* PRIMEngine (Prime edition) world geometry: turns the rooms' brushes (boxes and ramps) into
   - render batches (grouped by texture and material) for PRIM.R3D.View.setWorld,
   - a collision soup of triangles with a coarse grid for fast nearby / ray queries,
   - per-triangle info (which brush, which face) so the editor can pick faces.
   Also the 3D character mover (capsule as a stack of spheres, collide-and-slide, steps and slopes). */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), D = PRIM.PE, W = (PRIM.PEWorld = {});
  const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]], add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]], mul = (a, k) => [a[0] * k, a[1] * k, a[2] * k];
  const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2], cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]], len = a => Math.hypot(a[0], a[1], a[2]), norm = a => { const l = len(a) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };
  W.v = { sub, add, mul, dot, cross, len, norm };
  const hex = (h, d) => { h = String(h || d || "#ffffff"); if (h[0] !== "#" || h.length < 7) h = d || "#ffffff"; return [1, 3, 5].map(i => parseInt(h.slice(i, i + 2), 16) / 255); };
  W.hex = hex;

  // the faces of a brush as polygons (outward winding) with their names
  W.faces = b => {
    const [x, y, z] = b.p, [sx, sy, sz] = b.s, out = [];
    if (b.shape === "ramp") {
      const d = b.dir || 0, L = d % 2 === 0 ? sx : sz, Wd = d % 2 === 0 ? sz : sx, H = sy;
      const P = (u, w, h) => d === 0 ? [x + u, y + h, z + w] : d === 1 ? [x + w, y + h, z + u] : d === 2 ? [x + sx - u, y + h, z + w] : [x + w, y + h, z + sz - u];
      out.push(["slope", [P(0, 0, 0), P(L, 0, H), P(L, Wd, H), P(0, Wd, 0)]], ["bottom", [P(0, 0, 0), P(0, Wd, 0), P(L, Wd, 0), P(L, 0, 0)]], ["back", [P(L, 0, 0), P(L, Wd, 0), P(L, Wd, H), P(L, 0, H)]],
        ["left", [P(0, 0, 0), P(L, 0, 0), P(L, 0, H)]], ["right", [P(0, Wd, 0), P(L, Wd, H), P(L, Wd, 0)]]);
    } else {
      const X = x + sx, Y = y + sy, Z = z + sz;
      out.push(["top", [[x, Y, z], [x, Y, Z], [X, Y, Z], [X, Y, z]]], ["bottom", [[x, y, z], [X, y, z], [X, y, Z], [x, y, Z]]], ["north", [[x, y, Z], [X, y, Z], [X, Y, Z], [x, Y, Z]]],
        ["south", [[x, y, z], [x, Y, z], [X, Y, z], [X, y, z]]], ["east", [[X, y, z], [X, Y, z], [X, Y, Z], [X, y, Z]]], ["west", [[x, y, z], [x, y, Z], [x, Y, Z], [x, Y, z]]]);
    }
    const c = [x + sx / 2, y + sy / 2, z + sz / 2];
    for (const f of out) { const v = f[1], n = cross(sub(v[1], v[0]), sub(v[2], v[0])), m = v.reduce((a, q) => add(a, mul(q, 1 / v.length)), [0, 0, 0]); if (dot(n, sub(m, c)) < 0) v.reverse(); f.push(norm(cross(sub(v[1], v[0]), sub(v[2], v[0])))); }
    return out;   // [[name, verts, normal]]
  };

  // ---- everything the world needs, built from the project. opts: { editor (show hidden brushes faintly), sel (ids drawn highlighted) }
  W.build = (project, T, opts) => {
    opts = opts || {}; const groups = {}, tris = [], info = [], solid = [];
    const group = (tex, mat) => { const k = (tex || "") + "|" + JSON.stringify(mat || {}); return groups[k] = groups[k] || { tex: tex || "", mat: mat || null, data: [] }; };
    for (const room of project.rooms) {
      const amb = +T.get(room.id + ".ambient"), ac = hex(T.get(room.id + ".ambientColor"), "#ffffff").map(q => q * (isNaN(amb) ? 0.85 : amb));
      for (const b of room.brushes) {
        const g = k => T.get(b.id + "." + k), xr = !!g("xrayOnly"), heat = +g("heat") || 0, sc = 64 * (+g("uvScale") || 1), isSolid = g("solid") !== false;
        if (xr && !opts.editor && !opts.xray) { if (isSolid) for (const [, v] of W.faces(b)) for (let i = 1; i + 1 < v.length; i++) solid.push(tri(v[0], v[i], v[i + 1], b.id)); continue; }
        for (const [fname, v, n] of W.faces(b)) {
          const ft = T.get(b.id + "." + fname + ".tex") || g("tex"), fc = T.get(b.id + "." + fname + ".color") || g("color"), col = hex(fc, "#9aa6b2");
          const shade = 0.72 + 0.28 * Math.max(0, n[1]) + 0.12 * Math.abs(n[0]) - (n[1] < -0.5 ? 0.18 : 0), c = [col[0] * ac[0] * shade, col[1] * ac[1] * shade, col[2] * ac[2] * shade];
          const mat = (heat || xr || opts.matOf) ? Object.assign({}, heat ? { heat } : {}, xr ? { xray: 1, alpha: opts.editor ? 0.45 : 1 } : {}) : null, G = group(ft, mat && Object.keys(mat).length ? mat : null);
          const ax = Math.abs(n[0]), ay = Math.abs(n[1]), az = Math.abs(n[2]), uv = q => ay >= ax && ay >= az ? [q[0] / sc, q[2] / sc] : ax >= az ? [q[2] / sc, -q[1] / sc] : [q[0] / sc, -q[1] / sc];
          for (let i = 1; i + 1 < v.length; i++) for (const q of [v[0], v[i], v[i + 1]]) { const t = uv(q); G.data.push(q[0], q[1], q[2], t[0], t[1], c[0], c[1], c[2]); }
          for (let i = 1; i + 1 < v.length; i++) { const t = tri(v[0], v[i], v[i + 1], b.id); t.face = fname; t.room = room.id; tris.push(t); if (isSolid) solid.push(t); }
        }
      }
    }
    const w = { batches: Object.values(groups), tris, solid };
    w.grid = W.grid(solid); w.pickGrid = W.grid(tris); return w;
  };
  function tri(a, b, c, id) { const n = norm(cross(sub(b, a), sub(c, a))); return { a, b, c, n, id, mn: [Math.min(a[0], b[0], c[0]), Math.min(a[1], b[1], c[1]), Math.min(a[2], b[2], c[2])], mx: [Math.max(a[0], b[0], c[0]), Math.max(a[1], b[1], c[1]), Math.max(a[2], b[2], c[2])] }; }
  W.tri = tri;

  // ---- a coarse grid of triangles (cells of 256 units on X / Z)
  const CS = 256;
  W.grid = list => { const g = new Map(); list.forEach((t, i) => { for (let x = Math.floor(t.mn[0] / CS); x <= Math.floor(t.mx[0] / CS); x++) for (let z = Math.floor(t.mn[2] / CS); z <= Math.floor(t.mx[2] / CS); z++) { const k = x + "," + z; let a = g.get(k); if (!a) g.set(k, a = []); a.push(i); } }); return { g, list }; };
  W.near = (grid, mn, mx) => { const seen = new Set(), out = []; for (let x = Math.floor(mn[0] / CS); x <= Math.floor(mx[0] / CS); x++) for (let z = Math.floor(mn[2] / CS); z <= Math.floor(mx[2] / CS); z++) { const a = grid.g.get(x + "," + z); if (a) for (const i of a) if (!seen.has(i)) { seen.add(i); const t = grid.list[i]; if (t.mx[1] >= mn[1] && t.mn[1] <= mx[1]) out.push(t); } } return out; };

  // ---- ray against triangles: { t, p, tri } or null. extra = more triangles (doors, solid models...)
  W.raycast = (grid, o, d, maxT, extra, skip) => {
    let best = null; const test = t => { if (skip && skip(t)) return; const e1 = sub(t.b, t.a), e2 = sub(t.c, t.a), p = cross(d, e2), det = dot(e1, p); if (Math.abs(det) < 1e-9) return; const inv = 1 / det, s = sub(o, t.a), u = dot(s, p) * inv; if (u < 0 || u > 1) return; const q = cross(s, e1), v = dot(d, q) * inv; if (v < 0 || u + v > 1) return; const tt = dot(e2, q) * inv; if (tt > 0.01 && tt < (best ? best.t : maxT)) best = { t: tt, tri: t }; };
    const end = add(o, mul(d, maxT)), mn = [Math.min(o[0], end[0]), Math.min(o[1], end[1]), Math.min(o[2], end[2])], mx = [Math.max(o[0], end[0]), Math.max(o[1], end[1]), Math.max(o[2], end[2])];
    // walk the cells along the ray (long rays touch many cells; small worlds are cheap either way)
    if ((mx[0] - mn[0]) * (mx[2] - mn[2]) > 4096 * 4096) { for (const t of grid.list) test(t); } else for (const t of W.near(grid, mn, mx)) test(t);
    if (extra) for (const t of extra) test(t);
    if (best) best.p = add(o, mul(d, best.t)); return best;
  };

  // closest point on a triangle to p (Ericson, Real-Time Collision Detection 5.1.5)
  W.closest = (p, a, b, c) => {
    const ab = sub(b, a), ac = sub(c, a), ap = sub(p, a), d1 = dot(ab, ap), d2 = dot(ac, ap); if (d1 <= 0 && d2 <= 0) return a;
    const bp = sub(p, b), d3 = dot(ab, bp), d4 = dot(ac, bp); if (d3 >= 0 && d4 <= d3) return b;
    const vc = d1 * d4 - d3 * d2; if (vc <= 0 && d1 >= 0 && d3 <= 0) return add(a, mul(ab, d1 / (d1 - d3)));
    const cp = sub(p, c), d5 = dot(ab, cp), d6 = dot(ac, cp); if (d6 >= 0 && d5 <= d6) return c;
    const vb = d5 * d2 - d1 * d6; if (vb <= 0 && d2 >= 0 && d6 <= 0) return add(a, mul(ac, d2 / (d2 - d6)));
    const va = d3 * d6 - d5 * d4; if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) return add(b, mul(sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
    const den = 1 / (va + vb + vc), v = vb * den, w = vc * den; return add(a, add(mul(ab, v), mul(ac, w)));
  };
  // a box as triangles (for doors and solid models)
  W.boxTris = (mn, mx, id) => { const b = { shape: "box", p: mn, s: sub(mx, mn) }, out = []; for (const [, v] of W.faces(b)) for (let i = 1; i + 1 < v.length; i++) { const t = tri(v[0], v[i], v[i + 1], id); out.push(t); } return out; };

  // ---- the character: feet at pos, capsule of radius r and height h. Moves pos by vel*dt with collide-and-slide.
  // body = { pos, vel, r, h, step, maxSlope (cos), ground (out), groundN (out) }, extra = dynamic triangles
  W.push = (body, grid, extra) => {
    const r = body.r, h = body.h, n = Math.max(2, Math.ceil((h - 2 * r) / r) + 1), gap = (h - 2 * r) / (n - 1); let ground = null;
    for (let it = 0; it < 4; it++) {
      let moved = false; const mn = [body.pos[0] - r - 2, body.pos[1] - 2, body.pos[2] - r - 2], mx = [body.pos[0] + r + 2, body.pos[1] + h + 2, body.pos[2] + r + 2];
      const list = W.near(grid, mn, mx); if (extra) for (const t of extra) if (t.mx[0] >= mn[0] && t.mn[0] <= mx[0] && t.mx[1] >= mn[1] && t.mn[1] <= mx[1] && t.mx[2] >= mn[2] && t.mn[2] <= mx[2]) list.push(t);
      for (const t of list) for (let i = 0; i < n; i++) {
        const c = [body.pos[0], body.pos[1] + r + i * gap, body.pos[2]], q = W.closest(c, t.a, t.b, t.c), dv = sub(c, q), d = len(dv); if (d >= r + (i === 0 ? 1.5 : 0) || d < 1e-6) continue;
        let nn = mul(dv, 1 / d);
        if (i === 0 && nn[1] > body.maxSlope) { ground = ground && ground[1] > nn[1] ? ground : nn; if (d < r) { body.pos[1] += (r - d) / nn[1]; moved = true; } if (body.vel[1] < 0) body.vel[1] = 0; continue; }   // standing: push straight up, no sliding down slopes
        if (d >= r) continue;
        if (nn[1] > 0 && nn[1] <= body.maxSlope && i === 0) { nn = norm([nn[0], Math.min(nn[1], 0.1), nn[2]]); }   // too steep: treat as a wall
        const k = r - d; body.pos[0] += nn[0] * k; body.pos[1] += nn[1] * k; body.pos[2] += nn[2] * k; moved = true;
        const vn = dot(body.vel, nn); if (vn < 0) body.vel = sub(body.vel, mul(nn, vn));
      }
      if (!moved) break;
    }
    body.ground = !!ground; body.groundN = ground; return body;
  };
  W.move = (body, dt, grid, extra) => {
    const sp = len(body.vel) * dt, steps = Math.max(1, Math.min(12, Math.ceil(sp / (body.r * 0.6)))), sdt = dt / steps, was = body.ground;
    for (let s = 0; s < steps; s++) {
      const from = body.pos.slice(), want = mul(body.vel, sdt); body.pos = add(body.pos, want); W.push(body, grid, extra);
      // step up: blocked sideways while on the ground -> try again from higher up, then settle
      const hw = Math.hypot(want[0], want[2]), got = Math.hypot(body.pos[0] - from[0], body.pos[2] - from[2]);
      if ((was || body.ground) && hw > 0.01 && got < hw * 0.6 && body.step > 0) {
        const tryB = { pos: [from[0], from[1] + body.step, from[2]], vel: [body.vel[0], 0, body.vel[2]], r: body.r, h: body.h, maxSlope: body.maxSlope, step: 0 }; W.push(tryB, grid, extra);
        tryB.pos = add(tryB.pos, [want[0], 0, want[2]]); W.push(tryB, grid, extra); tryB.pos[1] -= body.step + 1; W.push(tryB, grid, extra);
        const got2 = Math.hypot(tryB.pos[0] - from[0], tryB.pos[2] - from[2]); if (got2 > got + 0.5 && tryB.ground) { body.pos = tryB.pos; body.ground = true; body.groundN = tryB.groundN; }
      }
    }
    // stick to the ground when walking down slopes and steps
    if (was && !body.ground && body.vel[1] <= 0) { const t = { pos: body.pos.slice(), vel: [0, -1, 0], r: body.r, h: body.h, maxSlope: body.maxSlope }; t.pos[1] -= body.step + 2; W.push(t, grid, extra); if (t.ground && body.pos[1] - t.pos[1] <= body.step + 2) { body.pos = t.pos; body.ground = true; body.groundN = t.groundN; body.vel[1] = 0; } }
    return body;
  };
})();
