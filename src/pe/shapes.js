/* Built-in placeholder shapes (original, plain geometry) used when no FBX model is chosen:
   pick-ups (a gem), enemies (a spiky orb), doors (frame + shield), the arm cannon, shots. They are models in the same
   format as loaded FBX files, so the renderer draws them the same way; their colour comes from the material tint. */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), SH = (PRIM.PEShapes = {});
  const model = (parts) => { const nodes = parts.map((m, i) => ({ name: "part" + i, parent: -1, t: [0, 0, 0], r: [0, 0, 0], s: [1, 1, 1], pre: [0, 0, 0], mesh: m })); const md = { nodes, anims: {}, texBytes: null }; md.bounds = PRIM.FBX ? PRIM.FBX.bounds(md) : null; return md; };
  const mesh = () => ({ p: [], n: [], u: [] });
  const triAdd = (m, a, b, c) => { const e1 = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], e2 = [c[0] - a[0], c[1] - a[1], c[2] - a[2]], n = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]], l = Math.hypot(...n) || 1;
    for (const q of [a, b, c]) { m.p.push(...q); m.n.push(n[0] / l, n[1] / l, n[2] / l); m.u.push(0, 0); } };
  SH.box = (m, mn, mx) => { const [x, y, z] = mn, [X, Y, Z] = mx, q = (a, b, c, d) => { triAdd(m, a, b, c); triAdd(m, a, c, d); };
    q([x, Y, z], [x, Y, Z], [X, Y, Z], [X, Y, z]); q([x, y, z], [X, y, z], [X, y, Z], [x, y, Z]); q([x, y, Z], [X, y, Z], [X, Y, Z], [x, Y, Z]); q([x, y, z], [x, Y, z], [X, Y, z], [X, y, z]); q([X, y, z], [X, Y, z], [X, Y, Z], [X, y, Z]); q([x, y, z], [x, y, Z], [x, Y, Z], [x, Y, z]); return m; };
  // a gem: two pyramids
  SH.gem = (r) => { const m = mesh(), t = [0, r * 1.3, 0], b = [0, -r * 1.3, 0], ring = []; for (let i = 0; i < 6; i++) { const a = i / 6 * Math.PI * 2; ring.push([Math.cos(a) * r, 0, Math.sin(a) * r]); }
    for (let i = 0; i < 6; i++) { const p = ring[i], q = ring[(i + 1) % 6]; triAdd(m, t, q, p); triAdd(m, b, p, q); } return model([m]); };
  // an orb with spikes (enemy placeholder), radius r, standing on the ground
  SH.orb = (r, h) => { const m = mesh(), cy = h / 2, sy = h / 2, P = (th, ph, k) => [Math.sin(th) * Math.cos(ph) * r * k, cy + Math.cos(th) * sy * k, Math.sin(th) * Math.sin(ph) * r * k], LA = 6, LO = 10;
    for (let i = 0; i < LA; i++) for (let j = 0; j < LO; j++) { const t0 = i / LA * Math.PI, t1 = (i + 1) / LA * Math.PI, p0 = j / LO * Math.PI * 2, p1 = (j + 1) / LO * Math.PI * 2, a = P(t0, p0, 1), b = P(t1, p0, 1), c = P(t1, p1, 1), d = P(t0, p1, 1);
      if ((i + j) % 3 === 0 && i > 0 && i < LA - 1) { const s = P((t0 + t1) / 2, (p0 + p1) / 2, 1.45); triAdd(m, a, b, s); triAdd(m, b, c, s); triAdd(m, c, d, s); triAdd(m, d, a, s); } else { triAdd(m, a, b, c); triAdd(m, a, c, d); } }
    const eye = mesh(); SH.box(eye, [-r * 0.35, cy + sy * 0.15, r * 0.82], [r * 0.35, cy + sy * 0.4, r * 1.02]); return model([m, eye]); };
  // the arm cannon: a barrel with rings (points down -Z, the way the camera looks)
  SH.cannon = () => { const m = mesh(); SH.box(m, [-3.2, -3.2, -26], [3.2, 3.2, 0]); SH.box(m, [-4.4, -4.4, -9], [4.4, 4.4, 3]); SH.box(m, [-3.8, -3.8, -22], [3.8, 3.8, -18]); SH.box(m, [-1.4, 2.6, -24], [1.4, 4.6, -6]); return model([m]); };
  // a door: frame (two posts and a top) and a shield, w x h, lying across X, centred on 0, thickness d
  SH.doorFrame = (w, h, d) => { const m = mesh(), t = 14; SH.box(m, [-w / 2 - t, 0, -d / 2 - 4], [-w / 2, h + t, d / 2 + 4]); SH.box(m, [w / 2, 0, -d / 2 - 4], [w / 2 + t, h + t, d / 2 + 4]); SH.box(m, [-w / 2 - t, h, -d / 2 - 4], [w / 2 + t, h + t, d / 2 + 4]); return model([m]); };
  SH.doorShield = (w, h) => { const m = mesh(); SH.box(m, [-w / 2, 0, -4], [w / 2, h, 4]); return model([m]); };
})();
