/* PRIMEngine: math helpers. Everything hangs off the global PRIM object so the engine
   runs from plain <script> tags (no build step, no packages). */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {});

  // 4x4 matrices, column-major (same layout WebGL expects)
  const M4 = {
    ident() { return new Float32Array([1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]); },
    mul(a, b) { const o = new Float32Array(16);
      for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) { let s = 0; for (let k = 0; k < 4; k++) s += a[k*4+r] * b[c*4+k]; o[c*4+r] = s; }
      return o; },
    persp(fov, asp, n, f) { const t = 1 / Math.tan(fov * Math.PI / 360), o = new Float32Array(16);
      o[0] = t / asp; o[5] = t; o[10] = (f + n) / (n - f); o[11] = -1; o[14] = 2 * f * n / (n - f); return o; },
    trans(x, y, z) { const o = M4.ident(); o[12] = x; o[13] = y; o[14] = z; return o; },
    scale(x, y, z) { const o = M4.ident(); o[0] = x; o[5] = y; o[10] = z; return o; },
    rotX(a) { const c = Math.cos(a), s = Math.sin(a), o = M4.ident(); o[5] = c; o[6] = s; o[9] = -s; o[10] = c; return o; },
    rotY(a) { const c = Math.cos(a), s = Math.sin(a), o = M4.ident(); o[0] = c; o[2] = -s; o[8] = s; o[10] = c; return o; },
    rotZ(a) { const c = Math.cos(a), s = Math.sin(a), o = M4.ident(); o[0] = c; o[1] = s; o[4] = -s; o[5] = c; return o; },
  };

  PRIM.M4 = M4;
  PRIM.D2R = Math.PI / 180;
  PRIM.clamp = (v, a, b) => Math.max(a, Math.min(b, v));
  PRIM.lerp = (a, b, t) => a + (b - a) * t;
  PRIM.angDiff = (a, b) => { let d = (b - a) % (Math.PI * 2); if (d > Math.PI) d -= Math.PI * 2; if (d < -Math.PI) d += Math.PI * 2; return d; };
  // small seeded random generator, used for procedural textures
  PRIM.rnd = seed => () => { seed |= 0; seed = seed + 0x6D2B79F5 | 0; let t = Math.imul(seed ^ seed >>> 15, 1 | seed); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; };
})();
