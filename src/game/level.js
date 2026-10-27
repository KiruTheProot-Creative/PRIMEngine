/* PRIMEngine: the test chamber (a tile-grid level) and its collision.
   Legend: # wall, M glowing pillar, . floor, P player start, E drone, D door, H energy pickup. 1 tile = 2 m. */
(function () {
  "use strict";
  const PRIM = window.PRIM, shade = (c, n) => c.map(v => Math.max(0, Math.min(255, (v + n) | 0)));
  const L = (PRIM.Level = {});

  L.TILE = 2; L.WALL_H = 5;
  L.MAP = [
    "##########################",
    "#........#...............#",
    "#........#...M.......M...#",
    "#..P.....#...............#",
    "#........#.......E.......#",
    "#....E...D...............#",
    "#........#...M.......M...#",
    "#........#...............#",
    "#...H....#.....E....E....#",
    "#........#...............#",
    "#........#...M.......M...#",
    "#........#.......H.......#",
    "#........#...............#",
    "##########################",
  ];
  L.MW = L.MAP[0].length; L.MH = L.MAP.length;
  L.LIGHTS = [
    { x: 5, z: 3.5, col: [0.55, 0.8, 1.0], r: 13 }, { x: 5, z: 8.5, col: [0.5, 0.75, 1.0], r: 13 },
    { x: 13, z: 3, col: [1.0, 0.75, 0.45], r: 12 }, { x: 21, z: 3, col: [1.0, 0.75, 0.45], r: 12 },
    { x: 13, z: 9, col: [0.6, 0.85, 1.0], r: 12 }, { x: 21, z: 9, col: [1.0, 0.7, 0.4], r: 12 },
    { x: 17.5, z: 6.5, col: [0.8, 0.9, 1.0], r: 14 },
  ];
  L.doors = []; L.batches = {}; L.TEX = {};

  L.cellCh = (x, z) => (x < 0 || z < 0 || x >= L.MW || z >= L.MH) ? "#" : L.MAP[z][x];
  L.doorAt = (x, z) => L.doors.find(d => d.cx === x && d.cz === z);
  L.solidCell = (x, z) => { const c = L.cellCh(x, z); if (c === "#" || c === "M") return true; if (c === "D") { const d = L.doorAt(x, z); return !d || d.open < 0.85; } return false; };
  L.solidAt = (x, z) => L.solidCell(Math.floor(x / L.TILE), Math.floor(z / L.TILE));
  const staticSolid = (x, z) => { const c = L.cellCh(x, z); return c === "#" || c === "M"; };
  L.resetDoors = () => { for (const d of L.doors) { d.open = 0; d.target = 0; d.hit = 0; } };

  function makeTextures() {
    const G = PRIM.gfx, T = L.TEX;
    T.wall = G.mkTex(64, 64, (x, y, r) => { const px = x % 32, py = y % 32;
      if (px === 0 || py === 0) return [10, 16, 22, 0]; if (px === 1 || py === 1) return [58, 76, 90, 0]; if (px === 31 || py === 31) return [18, 26, 34, 0];
      if ((px === 3 || px === 28) && (py === 3 || py === 28)) return [110, 130, 142, 0]; if (py >= 22 && py <= 23) return [70, 225, 255, 90];
      return [...shade([38, 52, 62], r() * 14 - 7 - py * 0.5), 0]; });
    T.pillar = G.mkTex(64, 64, (x, y, r) => { const px = x % 32;
      if (px === 0) return [8, 14, 20, 0]; if (px >= 14 && px <= 17) return [90, 235, 255, 210]; if (px >= 12 && px <= 19) return [20, 60, 74, 40]; if (y % 16 === 0) return [12, 20, 28, 0];
      return [...shade([24, 34, 42], r() * 10 - 5), 0]; });
    T.floor = G.mkTex(64, 64, (x, y, r) => { const px = x % 32, py = y % 32;
      if (px === 0 || py === 0) return [8, 12, 16, 0]; if ((px + py) % 8 === 0 && px > 6 && px < 26 && py > 6 && py < 26) return [16, 22, 28, 0];
      const base = ((x >> 5) + (y >> 5)) % 2 ? [30, 38, 46] : [26, 33, 40]; return [...shade(base, r() * 10 - 5), 0]; });
    T.ceil = G.mkTex(64, 64, (x, y, r) => { if (x % 32 === 0 || y % 32 === 0) return [8, 12, 16, 0]; if (y % 32 > 12 && y % 32 < 20) return [14, 20, 26, 0]; return [...shade([24, 30, 36], r() * 8 - 4), 0]; });
    T.lamp = G.mkTex(16, 16, () => [210, 240, 255, 255]);
    T.door = G.mkTex(64, 64, (x, y, r) => { const px = x % 32, py = y % 32, edge = Math.min(px, 31 - px, py, 31 - py);
      if (edge < 2) return [20, 60, 120, 255]; const hex = ((px >> 2) + (py >> 2)) % 2; return [40 + hex * 25 + r() * 10, 130 + hex * 40, 255, 215]; });
    T.metal = G.mkTex(8, 8, (x, y, r) => [...shade([120, 130, 140], r() * 30 - 15), 255]);
  }

  function batch(name, tex) { return L.batches[name] || (L.batches[name] = { tex, d: [], n: 0 }); }
  function quad(b, p, uvs, sh) { for (const i of [0, 1, 2, 0, 2, 3]) { b.d.push(p[i][0], p[i][1], p[i][2], uvs[i][0], uvs[i][1], sh); b.n++; } }

  L.build = function () {
    makeTextures();
    const T = L.TILE, H = L.WALL_H, TX = L.TEX;
    for (let z = 0; z < L.MH; z++) for (let x = 0; x < L.MW; x++) {
      const ch = L.cellCh(x, z), x0 = x * T, x1 = x0 + T, z0 = z * T, z1 = z0 + T;
      if (ch === "D") L.doors.push({ cx: x, cz: z, open: 0, target: 0, axis: (staticSolid(x - 1, z) && staticSolid(x + 1, z)) ? "x" : "z", hit: 0 });
      if (ch === "#" || ch === "M") {
        const b = batch(ch === "M" ? "pillar" : "wall", ch === "M" ? TX.pillar : TX.wall), vh = H / T * 0.5;
        if (!staticSolid(x + 1, z)) quad(b, [[x1,0,z0],[x1,0,z1],[x1,H,z1],[x1,H,z0]], [[0,vh],[1,vh],[1,0],[0,0]], 0.78);
        if (!staticSolid(x - 1, z)) quad(b, [[x0,0,z1],[x0,0,z0],[x0,H,z0],[x0,H,z1]], [[0,vh],[1,vh],[1,0],[0,0]], 0.78);
        if (!staticSolid(x, z + 1)) quad(b, [[x1,0,z1],[x0,0,z1],[x0,H,z1],[x1,H,z1]], [[0,vh],[1,vh],[1,0],[0,0]], 0.92);
        if (!staticSolid(x, z - 1)) quad(b, [[x0,0,z0],[x1,0,z0],[x1,H,z0],[x0,H,z0]], [[0,vh],[1,vh],[1,0],[0,0]], 0.92);
      } else {
        quad(batch("floor", TX.floor), [[x0,0,z0],[x1,0,z0],[x1,0,z1],[x0,0,z1]], [[0,0],[1,0],[1,1],[0,1]], 1.0);
        quad(batch("ceil", TX.ceil), [[x0,H,z1],[x1,H,z1],[x1,H,z0],[x0,H,z0]], [[0,1],[1,1],[1,0],[0,0]], 0.9);
      }
    }
    for (const l of L.LIGHTS) { const cx = l.x * T, cz = l.z * T, s = 0.9, y = H - 0.02;
      quad(batch("lamp", TX.lamp), [[cx-s,y,cz+s],[cx+s,y,cz+s],[cx+s,y,cz-s],[cx-s,y,cz-s]], [[0,0],[1,0],[1,1],[0,1]], 1); }
    for (const k in L.batches) { const b = L.batches[k]; b.buf = PRIM.gfx.vbo(b.d); b.d = null; }
  };
})();
