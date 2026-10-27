/* PRIMEngine 3D renderer (WebGL 1). Draws a map made of sectors: textured walls, floors and ceilings with
   sector light and distance fog, pixel-crisp (nearest) textures. Map x/y = ground plane, height goes up.
   Also holds a free-fly camera with mouse look. */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), R = (PRIM.R3D = {}), M4 = PRIM.M4;
  const TILE = 64;  // one texture repeat = 64 map units

  // ------------------------------------------------------------ small math
  function persp(f, a, n, fa) { const t = 1 / Math.tan(f / 2), nf = 1 / (n - fa); return [t / a, 0, 0, 0, 0, t, 0, 0, 0, 0, (fa + n) * nf, -1, 0, 0, 2 * fa * n * nf, 0]; }
  function mul(a, b) { const o = new Array(16); for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) { let s = 0; for (let k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k]; o[c * 4 + r] = s; } return o; }
  function look(px, py, pz, yaw, pitch) {   // view matrix for a camera at p looking along yaw/pitch (radians); yaw 0 = +x, 90deg = +z
    const cy = Math.cos(yaw), sy = Math.sin(yaw), cp = Math.cos(pitch), sp = Math.sin(pitch);
    const f = [cy * cp, sp, sy * cp], up0 = [0, 1, 0];
    const s = [f[1] * up0[2] - f[2] * up0[1], f[2] * up0[0] - f[0] * up0[2], f[0] * up0[1] - f[1] * up0[0]]; const sl = Math.hypot(s[0], s[1], s[2]) || 1; s[0] /= sl; s[1] /= sl; s[2] /= sl;
    const u = [s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]];
    return [s[0], u[0], -f[0], 0, s[1], u[1], -f[1], 0, s[2], u[2], -f[2], 0, -(s[0] * px + s[1] * py + s[2] * pz), -(u[0] * px + u[1] * py + u[2] * pz), f[0] * px + f[1] * py + f[2] * pz, 1];
  }
  const hex = h => { const n = parseInt(String(h || "#888888").replace("#", ""), 16) || 0; return [(n >> 16 & 255) / 255, (n >> 8 & 255) / 255, (n & 255) / 255]; };

  // ------------------------------------------------------------ polygon -> triangles (ear clipping, works for any simple polygon, either winding)
  R.triangulate = function (pts) {
    const n = pts.length; if (n < 3) return []; let idx = pts.map((_, i) => i), area = 0;
    for (let i = 0; i < n; i++) { const a = pts[i], b = pts[(i + 1) % n]; area += a[0] * b[1] - b[0] * a[1]; }
    if (area < 0) idx.reverse();
    const cross = (a, b, c) => (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    const inside = (p, a, b, c) => cross(a, b, p) >= 0 && cross(b, c, p) >= 0 && cross(c, a, p) >= 0, out = []; let guard = 0;
    while (idx.length > 3 && guard++ < 5000) {
      let cut = false;
      for (let i = 0; i < idx.length; i++) {
        const ia = idx[(i + idx.length - 1) % idx.length], ib = idx[i], ic = idx[(i + 1) % idx.length], a = pts[ia], b = pts[ib], c = pts[ic];
        if (cross(a, b, c) <= 0) continue;
        let ok = true; for (const j of idx) { if (j === ia || j === ib || j === ic) continue; if (inside(pts[j], a, b, c)) { ok = false; break; } }
        if (ok) { out.push([ia, ib, ic]); idx.splice(i, 1); cut = true; break; }
      }
      if (!cut) break;
    }
    if (idx.length >= 3) for (let i = 1; i < idx.length - 1; i++) out.push([idx[0], idx[i], idx[i + 1]]);
    return out;
  };

  // which sector (room) contains the map point x,y, or null
  R.sectorAt = function (m, x, y) {
    const vs = {}; for (const v of m.verts) vs[v.id] = v;
    for (let i = m.sectors.length - 1; i >= 0; i--) { const q = m.sectors[i].v.map(id => vs[id]).filter(Boolean); let c = false; for (let a = 0, b = q.length - 1; a < q.length; b = a++) if ((q[a].y > y) !== (q[b].y > y) && x < (q[b].x - q[a].x) * (y - q[a].y) / (q[b].y - q[a].y) + q[a].x) c = !c; if (c) return m.sectors[i]; }
    return null;
  };

  // camera orientation vectors for a yaw / pitch (radians): right, up, forward
  R.camBasis = function (yaw, pitch) { const cp = Math.cos(pitch), f = [Math.cos(yaw) * cp, Math.sin(pitch), Math.sin(yaw) * cp], sr = M4.norm(M4.cross(f, [0, 1, 0])); return { s: sr, u: M4.cross(sr, f), f }; };

  // solid wall segments of a map (those not marked open), for collision
  R.walls = function (m, T) {
    const vs = {}; for (const v of m.verts) vs[v.id] = v; const out = [];
    for (const sec of m.sectors) { const ids = sec.v.filter(i => vs[i]); for (let i = 0; i < ids.length; i++) { const A = vs[ids[i]], B = vs[ids[(i + 1) % ids.length]]; if (T.get(m.id + "." + A.id + ".open")) continue; out.push({ ax: A.x, ay: A.y, bx: B.x, by: B.y }); } }
    return out;
  };
  // the room at a point with its floor and ceiling height, or null
  R.sectorInfo = function (m, T, x, y) { const s = R.sectorAt(m, x, y); return s ? { sec: s, floor: +T.get(m.id + "." + s.id + ".floor"), ceil: +T.get(m.id + "." + s.id + ".ceil") } : null; };

  // ------------------------------------------------------------ the view
  const VS = "attribute vec3 aPos;attribute vec2 aUV;attribute vec3 aCol;uniform mat4 uVP;uniform mat4 uM;varying vec2 vUV;varying vec3 vCol;varying float vD;varying vec3 vW;void main(){vec4 w=uM*vec4(aPos,1.0);vec4 p=uVP*w;gl_Position=p;vUV=aUV;vCol=aCol;vD=p.w;vW=w.xyz;}";
  const FS = [
    "#ifdef GL_FRAGMENT_PRECISION_HIGH", "precision highp float;", "#else", "precision mediump float;", "#endif",
    "varying vec2 vUV;varying vec3 vCol;varying float vD;varying vec3 vW;uniform sampler2D uTex;uniform vec3 uFog;uniform float uFogD;uniform float uBright;",
    "uniform vec4 uLP[8];uniform vec4 uLC[8];uniform vec4 uLN[8];uniform vec4 uLU[8];uniform int uNL;uniform vec3 uCam;",
    "uniform vec3 uTint;uniform vec3 uEmit;uniform float uLit;uniform float uAlpha;uniform float uSpec;",
    "void main(){",
    " vec3 base=texture2D(uTex,vUV).rgb*uTint; vec3 c=base*vCol*uBright;",
    " if(uNL>0){",
    "  vec3 V=normalize(uCam-vW);",
    "#ifdef DERIV", "  vec3 n=normalize(cross(dFdx(vW),dFdy(vW)));", "#else", "  vec3 n=vec3(0.0,1.0,0.0);", "#endif",
    "  if(dot(n,V)<0.0) n=-n;",
    "  vec3 ls=vec3(0.0); vec3 ss=vec3(0.0);",
    "  for(int i=0;i<8;i++){ if(i>=uNL) break;",
    "   vec3 Lp=uLP[i].xyz; float rng=uLP[i].w; vec3 col=uLC[i].rgb; float ty=uLC[i].w; vec3 tg=Lp; float fc=1.0;",
    "   if(ty>0.5){ vec3 an=uLN[i].xyz; vec3 ux=uLU[i].xyz; vec3 vx=cross(an,ux); vec3 rel=vW-Lp; tg=Lp+ux*clamp(dot(rel,ux),-uLN[i].w,uLN[i].w)+vx*clamp(dot(rel,vx),-uLU[i].w,uLU[i].w); }",
    "   vec3 L=tg-vW; float d=length(L); L=L/max(d,0.001);",
    "   if(ty>0.5) fc=clamp(dot(-L,uLN[i].xyz)*2.0+0.15,0.0,1.0);",
    "   float att=clamp(1.0-d/rng,0.0,1.0); att=att*att;",
    "   ls+=col*max(dot(n,L),0.0)*att*fc;",
    "   vec3 H=normalize(L+V); ss+=col*pow(max(dot(n,H),0.0),24.0)*att*fc;",
    "  }",
    "  c+=base*ls*uLit+ss*uSpec;",
    " }",
    " c+=base*uEmit;",
    " float f=clamp(vD/uFogD,0.0,1.0); gl_FragColor=vec4(mix(c,uFog,f*f),uAlpha);",
    "}"].join("\n");

  R.View = function (canvas, o) {
    o = o || {}; const gl = canvas.getContext("webgl", { antialias: false, alpha: false }) || canvas.getContext("experimental-webgl");
    this.canvas = canvas; this.ok = !!gl; if (!gl) return; this.gl = gl;
    const sh = (t, src) => { const s = gl.createShader(t); gl.shaderSource(s, src); gl.compileShader(s); if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s)); return s; };
    const pr = this.pr = gl.createProgram(); gl.attachShader(pr, sh(gl.VERTEX_SHADER, VS)); const dx = gl.getExtension("OES_standard_derivatives"); gl.attachShader(pr, sh(gl.FRAGMENT_SHADER, (dx ? "#extension GL_OES_standard_derivatives : enable\n#define DERIV\n" : "") + FS)); gl.linkProgram(pr); gl.useProgram(pr);
    this.loc = { pos: gl.getAttribLocation(pr, "aPos"), uv: gl.getAttribLocation(pr, "aUV"), col: gl.getAttribLocation(pr, "aCol"), vp: gl.getUniformLocation(pr, "uVP"), m: gl.getUniformLocation(pr, "uM"), tex: gl.getUniformLocation(pr, "uTex"), lp: gl.getUniformLocation(pr, "uLP"), lc: gl.getUniformLocation(pr, "uLC"), ln: gl.getUniformLocation(pr, "uLN"), lu: gl.getUniformLocation(pr, "uLU"), nl: gl.getUniformLocation(pr, "uNL"), cam: gl.getUniformLocation(pr, "uCam"), tint: gl.getUniformLocation(pr, "uTint"), emit: gl.getUniformLocation(pr, "uEmit"), lit: gl.getUniformLocation(pr, "uLit"), alpha: gl.getUniformLocation(pr, "uAlpha"), spec: gl.getUniformLocation(pr, "uSpec"), fog: gl.getUniformLocation(pr, "uFog"), fogD: gl.getUniformLocation(pr, "uFogD"), bright: gl.getUniformLocation(pr, "uBright") };
    gl.enable(gl.DEPTH_TEST); gl.disable(gl.CULL_FACE);
    this.cam = { x: 0, y: 56, z: 0, yaw: Math.PI / 2, pitch: 0 }; this.batches = []; this.texCache = {}; this.sky = [0.08, 0.14, 0.23]; this.fog = [0.02, 0.03, 0.05]; this.fogD = 1400; this.bright = 1; this.fov = 1.15; this.keys = {}; this.speed = 300; this.running = false; this.frames = 0;
    this.defTex = this.makeTex(checker()); { const w = document.createElement("canvas"); w.width = w.height = 2; const x = w.getContext("2d"); x.fillStyle = "#fff"; x.fillRect(0, 0, 2, 2); this.whiteTex = this.makeTex(w); }
  };
  const P = R.View.prototype;

  function checker() { const c = document.createElement("canvas"); c.width = c.height = 64; const x = c.getContext("2d"); for (let j = 0; j < 8; j++) for (let i = 0; i < 8; i++) { x.fillStyle = (i + j) % 2 ? "#b8b8b8" : "#d8d8d8"; x.fillRect(i * 8, j * 8, 8, 8); } x.strokeStyle = "#7a7a7a"; x.lineWidth = 2; x.strokeRect(1, 1, 62, 62); return c; }
  P.makeTex = function (src) {
    const gl = this.gl; let w = src.naturalWidth || src.width, h = src.naturalHeight || src.height; const pot = n => Math.min(1024, Math.max(2, 1 << Math.round(Math.log2(Math.max(1, n)))));
    const c = document.createElement("canvas"); c.width = pot(w); c.height = pot(h); const x = c.getContext("2d"); x.imageSmoothingEnabled = false; x.drawImage(src, 0, 0, c.width, c.height);
    const t = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, t); gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, c);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT); return t;
  };
  // returns a GL texture for an image asset id (loads it in the background; the default checker shows until then)
  P.texFor = function (id, urls) {
    if (!id || !urls || !urls[id]) return this.defTex; if (this.texCache[id]) return this.texCache[id].t;
    const e = this.texCache[id] = { t: this.defTex }, im = new Image(); im.onload = () => { e.t = this.makeTex(im); this.dirtyTex = true; }; im.src = urls[id]; return e.t;
  };

  // (re)build all geometry from the project's map + tags
  P.setMap = function (project, map, tags, urls) { this.project = project; this.map = map; this.tags = tags; this.urls = urls; this.rebuild(); };
  // draws just these triangles (used by the Wall Editor): by = { textureId: [x,y,z,u,v,r,g,b ...] }
  P.setBatches = function (by, urls) {
    const gl = this.gl; for (const b of this.batches) gl.deleteBuffer(b.buf); this.urls = urls; this.map = null;
    this.batches = Object.keys(by).map(k => { const buf = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, buf); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(by[k]), gl.STATIC_DRAW); return { buf, n: by[k].length / 8, tex: k, sec: "", mn: [-1e9, -1e9, -1e9], mx: [1e9, 1e9, 1e9] }; });
    this.triCount = this.batches.reduce((s, b) => s + b.n / 3, 0);
  };
  P.rebuild = function () {
    if (!this.ok || !this.map) return; const gl = this.gl, m = this.map, T = this.tags, vs = {}; for (const v of m.verts) vs[v.id] = v;
    this.sky = hex(T.get(m.id + ".skyColor")); this.fog = hex(T.get(m.id + ".fogColor")); this.fogD = Math.max(100, +T.get(m.id + ".fogDistance") || 1400);
    for (const b of this.batches) gl.deleteBuffer(b.buf); const by = {};
    this.tri = []; let curSec = ""; const add = (texId, tri) => { (by[(texId || "") + "|" + curSec] = by[(texId || "") + "|" + curSec] || []).push(...tri); for (let i = 0; i + 23 < tri.length; i += 24) this.tri.push(tri[i], tri[i + 1], tri[i + 2], tri[i + 8], tri[i + 9], tri[i + 10], tri[i + 16], tri[i + 17], tri[i + 18]); };
    for (const sec of m.sectors) {
      const ids = sec.v.filter(i => vs[i]); if (ids.length < 3) continue; curSec = sec.id; const pre = m.id + "." + sec.id + ".", fl = +T.get(pre + "floor"), ce = +T.get(pre + "ceil"), li = +T.get(pre + "light");
      const pts = ids.map(i => [vs[i].x, vs[i].y]), tris = R.triangulate(pts);
      for (const [h, key, col] of [[fl, "floor", 1], [ce, "ceil", 0.8]]) {
        const c = hex(T.get(pre + key + "Color")).map(x => x * li * col), tex = T.get(pre + key + "Tex"), out = [];
        for (const t of tris) for (const k of t) out.push(pts[k][0], h, pts[k][1], pts[k][0] / TILE, pts[k][1] / TILE, c[0], c[1], c[2]);
        add(tex, out);
      }
      for (let i = 0; i < ids.length; i++) {
        const A = vs[ids[i]], B = vs[ids[(i + 1) % ids.length]], vp = m.id + "." + A.id + "."; if (T.get(vp + "open") || ce <= fl) continue;
        const len = Math.hypot(B.x - A.x, B.y - A.y), dir = Math.abs(B.x - A.x) > Math.abs(B.y - A.y) ? 1 : 0.82, c = hex(T.get(vp + "color")).map(x => x * li * dir), u1 = len / TILE, v0 = 0, v1 = (ce - fl) / TILE;
        const rows = String(T.get(vp + "rows") || "").split(",").map(x => x.trim()), WM = PRIM.WallMesh, custom = !!WM && !!this.project && rows.some(x => x && WM.byId(this.project, x));
        let nx = 0, nz = 0; if (custom) { let a2 = 0; for (let k = 0; k < pts.length; k++) { const p1 = pts[k], p2 = pts[(k + 1) % pts.length]; a2 += p1[0] * p2[1] - p2[0] * p1[1]; } const dx = (B.x - A.x) / (len || 1), dz = (B.y - A.y) / (len || 1); nx = a2 > 0 ? -dz : dz; nz = a2 > 0 ? dx : -dx; }   // nx,nz: the wall's normal, pointing into the room
        const o = custom ? -1 : 0, q = (x, y, z, u, v) => [x + nx * o, y, z + nz * o, u, v, c[0], c[1], c[2]];
        add(T.get(vp + "tex"), [...q(A.x, ce, A.y, 0, v0), ...q(B.x, ce, B.y, u1, v0), ...q(B.x, fl, B.y, u1, v1), ...q(A.x, ce, A.y, 0, v0), ...q(B.x, fl, B.y, u1, v1), ...q(A.x, fl, A.y, 0, v1)]);
        if (custom) {   // custom wall types: the wall is cut into panels of 96 x 96 units (1.5 m) and each layer (row, bottom to top) is filled with its wall type
          const SZ = WM.SIZE, nR = Math.max(1, Math.round((ce - fl) / SZ)), nC = Math.max(1, Math.round(len / SZ)), sy = (ce - fl) / (SZ * nR), sx = len / (SZ * nC), rep = T.get(vp + "rowsRepeat") !== false, sX = nz, sZ = -nx, fwd = (B.x - A.x) * sX + (B.y - A.y) * sZ > 0, S0 = fwd ? A : B, ux = fwd ? (B.x - A.x) / len : (A.x - B.x) / len, uz = fwd ? (B.y - A.y) / len : (A.y - B.y) / len;
          const lastRow = rows.length ? rows[rows.length - 1] : "";
          for (let rr = 0; rr < nR; rr++) {
            const id = rr < rows.length ? rows[rr] : rep ? lastRow : "", wall = id && WM.byId(this.project, id); if (!wall) continue; const tr = WM.tris(wall, T);
            for (let cc = 0; cc < nC; cc++) for (const t of tr) { const out = []; for (let k = 0; k < 3; k++) { const P = t.p[k], X = (cc * SZ + P[0]) * sx; out.push(S0.x + ux * X + nx * P[2], fl + (rr * SZ + P[1]) * sy, S0.y + uz * X + nz * P[2], t.uv[k][0], t.uv[k][1], c[0] * t.tint[0], c[1] * t.tint[1], c[2] * t.tint[2]); } add(t.tex, out); }
          }
        }
      }
    }
    this.batches = Object.keys(by).map(k => { const buf = gl.createBuffer(), a = by[k], cut = k.lastIndexOf("|"), mn = [1e9, 1e9, 1e9], mx = [-1e9, -1e9, -1e9]; gl.bindBuffer(gl.ARRAY_BUFFER, buf); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(a), gl.STATIC_DRAW);
      for (let i = 0; i < a.length; i += 8) for (let q = 0; q < 3; q++) { if (a[i + q] < mn[q]) mn[q] = a[i + q]; if (a[i + q] > mx[q]) mx[q] = a[i + q]; }
      return { buf, n: a.length / 8, tex: k.slice(0, cut), sec: k.slice(cut + 1), mn, mx }; });
    this.triCount = this.batches.reduce((s, b) => s + b.n / 3, 0);
    // rooms that touch through an open wall (a "portal"): used to skip rooms that can not be seen
    const ekey = (a, b) => a < b ? a + "|" + b : b + "|" + a, owners = {}; this.adj = {};
    for (const sec of m.sectors) { const ids = sec.v.filter(i => vs[i]); for (let i = 0; i < ids.length; i++) (owners[ekey(ids[i], ids[(i + 1) % ids.length])] = owners[ekey(ids[i], ids[(i + 1) % ids.length])] || []).push(sec.id); }
    for (const sec of m.sectors) { const ids = sec.v.filter(i => vs[i]), pre = m.id + "." + sec.id + "."; for (let i = 0; i < ids.length; i++) {
      const A = vs[ids[i]], B = vs[ids[(i + 1) % ids.length]]; if (!T.get(m.id + "." + A.id + ".open")) continue;
      for (const o of owners[ekey(A.id, B.id)] || []) if (o !== sec.id) { const so = m.sectors.find(x => x.id === o), lo = Math.min(+T.get(pre + "floor"), +T.get(m.id + "." + o + ".floor")), hi = Math.max(+T.get(pre + "ceil"), +T.get(m.id + "." + o + ".ceil")); (this.adj[sec.id] = this.adj[sec.id] || []).push({ to: o, mn: [Math.min(A.x, B.x), lo, Math.min(A.y, B.y)], mx: [Math.max(A.x, B.x), hi, Math.max(A.y, B.y)] }); }
    } }
  };

  // ---- lights and materials (see the fragment shader)
  P.setLights = function (list) { this.lights = list || []; };
  P.useLights = function (camSpace) {
    const gl = this.gl, L = this.loc, c = this.cam, B = R.camBasis(c.yaw, c.pitch), all = (this.lights || []).map(l => ({ l, k: Math.hypot(l.p[0] - c.x, l.p[1] - c.y, l.p[2] - c.z) / Math.max(1, l.r) })).sort((a, b) => a.k - b.k).slice(0, 8).filter(x => x.k < 1.6).map(x => x.l);
    const toC = p => { const d = [p[0] - c.x, p[1] - c.y, p[2] - c.z]; return [M4.dot(d, B.s), M4.dot(d, B.u), -M4.dot(d, B.f)]; }, dC = v => [M4.dot(v, B.s), M4.dot(v, B.u), -M4.dot(v, B.f)];
    const lp = new Float32Array(32), lc = new Float32Array(32), ln = new Float32Array(32), lu = new Float32Array(32);
    all.forEach((l, i) => { const p = camSpace ? toC(l.p) : l.p, n = l.n ? (camSpace ? dC(l.n) : l.n) : [0, 1, 0], u = l.u ? (camSpace ? dC(l.u) : l.u) : [1, 0, 0];
      lp.set([p[0], p[1], p[2], l.r], i * 4); lc.set([l.c[0], l.c[1], l.c[2], l.type ? 1 : 0], i * 4); ln.set([n[0], n[1], n[2], l.hw || 0], i * 4); lu.set([u[0], u[1], u[2], l.hh || 0], i * 4); });
    this.nLights = all.length; gl.uniform4fv(L.lp, lp); gl.uniform4fv(L.lc, lc); gl.uniform4fv(L.ln, ln); gl.uniform4fv(L.lu, lu); gl.uniform1i(L.nl, all.length); gl.uniform3fv(L.cam, camSpace ? [0, 0, 0] : [c.x, c.y, c.z]);
  };
  // material of what is drawn next: { tint, emit, lit, alpha, spec } (all optional)
  P.setMat = function (m) {
    const gl = this.gl, L = this.loc; m = m || {}; gl.uniform3fv(L.tint, m.tint || [1, 1, 1]); gl.uniform3fv(L.emit, m.emit || [0, 0, 0]); gl.uniform1f(L.lit, m.lit === undefined ? 1 : m.lit); gl.uniform1f(L.spec, m.spec || 0);
    const a = m.alpha === undefined ? 1 : m.alpha; gl.uniform1f(L.alpha, a); if (a < 0.999) { gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA); } else gl.disable(gl.BLEND);
  };
  // ---- culling: view frustum + rooms reachable through open walls
  P.frustum = function () {
    const m = this.vp, row = i => [m[i], m[4 + i], m[8 + i], m[12 + i]], r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3), add = (a, b, s) => a.map((v, i) => v + s * b[i]);
    return [add(r3, r0, 1), add(r3, r0, -1), add(r3, r1, 1), add(r3, r1, -1), add(r3, r2, 1), add(r3, r2, -1)].map(p => { const l = Math.hypot(p[0], p[1], p[2]) || 1; return p.map(v => v / l); });
  };
  const inBox = (pl, mn, mx) => { for (const p of pl) { const x = p[0] > 0 ? mx[0] : mn[0], y = p[1] > 0 ? mx[1] : mn[1], z = p[2] > 0 ? mx[2] : mn[2]; if (p[0] * x + p[1] * y + p[2] * z + p[3] < 0) return false; } return true; };
  P.visible = function () {
    const on = !this.tags || !this.tags.has || !this.tags.has("gfx.culling") || this.tags.get("gfx.culling"); if (!on || !this.map || !this.vp) return null;
    const pl = this.frustum(), c = this.cam, here = R.sectorAt(this.map, c.x, c.z); let secs = null;
    if (here && this.adj) { secs = new Set([here.id]); const q = [here.id]; while (q.length) { const s = q.shift(); for (const e of this.adj[s] || []) if (!secs.has(e.to) && inBox(pl, e.mn, e.mx)) { secs.add(e.to); q.push(e.to); } } }
    return b => (!secs || secs.has(b.sec)) && inBox(pl, b.mn, b.mx);
  };

  P.resize = function () { const c = this.canvas, d = Math.min(2, window.devicePixelRatio || 1), w = Math.max(2, Math.floor(c.clientWidth * d)), h = Math.max(2, Math.floor(c.clientHeight * d)); if (c.width !== w || c.height !== h) { c.width = w; c.height = h; } };
  P.draw = function () {
    if (!this.ok) return; const gl = this.gl, c = this.cam; this.resize(); gl.viewport(0, 0, this.canvas.width, this.canvas.height);
    gl.clearColor(this.sky[0] * this.bright, this.sky[1] * this.bright, this.sky[2] * this.bright, 1); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    const vp = mul(persp(this.fov, this.canvas.width / this.canvas.height, 2, 12000), look(c.x, c.y, c.z, c.yaw, c.pitch));
    this.vp = vp; gl.uniformMatrix4fv(this.loc.vp, false, new Float32Array(vp)); gl.uniform3fv(this.loc.fog, this.fog); gl.uniform1f(this.loc.fogD, this.fogD); gl.uniform1f(this.loc.bright, this.bright); gl.uniform1i(this.loc.tex, 0); gl.uniformMatrix4fv(this.loc.m, false, new Float32Array(M4.ident()));
    const L = this.loc; gl.enableVertexAttribArray(L.pos); gl.enableVertexAttribArray(L.uv); gl.enableVertexAttribArray(L.col);
    this.useLights(false); this.setMat();
    const vis = this.visible(), st = this.stats = { tris: 0, chunks: 0, totalTris: this.triCount || 0, totalChunks: this.batches.length, lights: this.nLights || 0 };
    for (const b of this.batches) { if (vis && !vis(b)) continue; st.chunks++; st.tris += b.n / 3; gl.bindBuffer(gl.ARRAY_BUFFER, b.buf); gl.vertexAttribPointer(L.pos, 3, gl.FLOAT, false, 32, 0); gl.vertexAttribPointer(L.uv, 2, gl.FLOAT, false, 32, 12); gl.vertexAttribPointer(L.col, 3, gl.FLOAT, false, 32, 20);
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, this.texFor(b.tex, this.urls)); gl.drawArrays(gl.TRIANGLES, 0, b.n); }
    this.frames++;
  };

  // projectiles and beams in the world. list items: { kind: "line"|"laser"|"sphere"|"mesh", ... } (see play3d.js)
  P.drawFx = function (list) {
    if (!list.length || !this.vp) return; const gl = this.gl, L = this.loc, a = [], Ld = M4.norm([0.3, 0.8, 0.5]); this.useLights(false);
    gl.uniformMatrix4fv(L.vp, false, new Float32Array(this.vp)); gl.uniformMatrix4fv(L.m, false, new Float32Array(M4.ident())); gl.enableVertexAttribArray(L.pos); gl.enableVertexAttribArray(L.uv); gl.enableVertexAttribArray(L.col);
    const bind = (buf, tex) => { gl.bindBuffer(gl.ARRAY_BUFFER, buf); gl.vertexAttribPointer(L.pos, 3, gl.FLOAT, false, 32, 0); gl.vertexAttribPointer(L.uv, 2, gl.FLOAT, false, 32, 12); gl.vertexAttribPointer(L.col, 3, gl.FLOAT, false, 32, 20); gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, tex); };
    const push = (v, c) => a.push(v[0], v[1], v[2], 0, 0, c[0], c[1], c[2]);
    const beam = (p, q, th, c) => { const dn = M4.norm(M4.sub(q, p)), ref = Math.abs(dn[1]) > 0.9 ? [1, 0, 0] : [0, 1, 0], p1 = M4.norm(M4.cross(dn, ref)), p2 = M4.cross(dn, p1);
      for (const o of [p1, p2]) { const s = (v, k) => [v[0] + o[0] * th * k, v[1] + o[1] * th * k, v[2] + o[2] * th * k], A1 = s(p, 1), A2 = s(p, -1), B1 = s(q, 1), B2 = s(q, -1); for (const v of [A1, B1, B2, A1, B2, A2]) push(v, c); } };
    const LAT = 6, LON = 8;
    for (const f of list) {
      if (f.kind === "line") beam(f.a, f.b, f.w * Math.max(0.15, 1 - f.t / f.life), f.c);
      else if (f.kind === "laser") beam(M4.sub(f.p, f.d.map(q => q * f.size * 7)), f.p, f.size * 0.4, f.c);
      else if (f.kind === "sphere") {
        const pt = (i, j) => { const th = i / LAT * Math.PI, ph = j / LON * Math.PI * 2, n = [Math.sin(th) * Math.cos(ph), Math.cos(th), Math.sin(th) * Math.sin(ph)], lit = 0.65 + 0.35 * Math.max(0, M4.dot(n, Ld));
          return [[f.p[0] + n[0] * f.size, f.p[1] + n[1] * f.size, f.p[2] + n[2] * f.size], [f.c[0] * lit, f.c[1] * lit, f.c[2] * lit]]; };
        for (let i = 0; i < LAT; i++) for (let j = 0; j < LON; j++) { const q = [pt(i, j), pt(i + 1, j), pt(i + 1, j + 1), pt(i, j), pt(i + 1, j + 1), pt(i, j + 1)]; for (const [v, c] of q) push(v, c); }
      }
    }
    this.setMat({ lit: 0 });
    if (a.length) { const buf = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, buf); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(a), gl.STREAM_DRAW); bind(buf, this.whiteTex); gl.drawArrays(gl.TRIANGLES, 0, a.length / 8); gl.deleteBuffer(buf); }
    for (const f of list) if (f.kind === "mesh" && f.gm) { this.setMat(f.mat); const lv = this.lodLevelAt(Math.hypot(f.p[0] - this.cam.x, f.p[1] - this.cam.y, f.p[2] - this.cam.z), 20 * (f.scale || 1)); this.lodLevel = lv; for (let ms of f.gm.meshes) { ms = this.lodOf(f.gm, ms, lv); gl.uniformMatrix4fv(L.m, false, new Float32Array(M4.mul(f.M, f.pose[ms.node]))); bind(ms.buf, f.gm.tex); gl.drawArrays(gl.TRIANGLES, 0, ms.n); } }
    this.setMat(); gl.uniformMatrix4fv(L.m, false, new Float32Array(M4.ident()));
  };

  // nearest solid surface along a ray (origin o, direction d): { t, p } or null. Used for aiming.
  P.raycast = function (o, d, maxT) {
    const T = this.tri || [], eps = 1e-7; let best = maxT || 1e5, hit = false;
    for (let i = 0; i < T.length; i += 9) {
      const e1x = T[i + 3] - T[i], e1y = T[i + 4] - T[i + 1], e1z = T[i + 5] - T[i + 2], e2x = T[i + 6] - T[i], e2y = T[i + 7] - T[i + 1], e2z = T[i + 8] - T[i + 2];
      const px = d[1] * e2z - d[2] * e2y, py = d[2] * e2x - d[0] * e2z, pz = d[0] * e2y - d[1] * e2x, det = e1x * px + e1y * py + e1z * pz; if (Math.abs(det) < eps) continue;
      const inv = 1 / det, tx = o[0] - T[i], ty = o[1] - T[i + 1], tz = o[2] - T[i + 2], u = (tx * px + ty * py + tz * pz) * inv; if (u < 0 || u > 1) continue;
      const qx = ty * e1z - tz * e1y, qy = tz * e1x - tx * e1z, qz = tx * e1y - ty * e1x, v = (d[0] * qx + d[1] * qy + d[2] * qz) * inv; if (v < 0 || u + v > 1) continue;
      const t = (e2x * qx + e2y * qy + e2z * qz) * inv; if (t > 0.01 && t < best) { best = t; hit = true; }
    }
    return hit ? { t: best, p: [o[0] + d[0] * best, o[1] + d[1] * best, o[2] + d[2] * best] } : null;
  };

  // ------------------------------------------------------------ models (FBX) drawn in front of the camera
  P.loadModel = function (model) {
    const gl = this.gl, L = M4.norm([0.3, 0.8, 0.5]), gm = { model, meshes: [], tex: this.whiteTex };
    model.nodes.forEach((n, i) => { if (!n.mesh || !n.mesh.p.length) return; const m = n.mesh, cnt = m.p.length / 3, a = new Float32Array(cnt * 8);
      for (let k = 0; k < cnt; k++) { const lit = 0.55 + 0.45 * Math.max(0, M4.dot([m.n[k * 3], m.n[k * 3 + 1], m.n[k * 3 + 2]], L)); a.set([m.p[k * 3], m.p[k * 3 + 1], m.p[k * 3 + 2], m.u[k * 2], m.u[k * 2 + 1], lit, lit, lit], k * 8); }
      const buf = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, buf); gl.bufferData(gl.ARRAY_BUFFER, a, gl.STATIC_DRAW); gm.meshes.push({ node: i, buf, n: cnt }); });
    if (model.texBytes) { const im = new Image(); im.onload = () => { gm.tex = this.makeTex(im); }; im.src = URL.createObjectURL(new Blob([model.texBytes], { type: "image/png" })); }
    return gm;
  };
  // auto-LOD: merge nearby vertices of a mesh (grid clustering). level 1 = about half the detail, 2 = a quarter
  P.lodOf = function (gm, ms, level) {
    if (!level) return ms; ms.lod = ms.lod || {}; if (ms.lod[level]) return ms.lod[level];
    const mesh = gm.model.nodes[ms.node].mesh, cnt = mesh.p.length / 3; let mn = [1e9, 1e9, 1e9], mx = [-1e9, -1e9, -1e9];
    for (let i = 0; i < cnt; i++) for (let q = 0; q < 3; q++) { const v = mesh.p[i * 3 + q]; if (v < mn[q]) mn[q] = v; if (v > mx[q]) mx[q] = v; }
    const cell = Math.max(1e-6, Math.hypot(mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]) / (level === 1 ? 28 : 12)), key = i => [0, 1, 2].map(q => Math.floor((mesh.p[i * 3 + q] - mn[q]) / cell)).join(","), acc = {};
    for (let i = 0; i < cnt; i++) { const k = key(i), a = acc[k] = acc[k] || { n: 0, p: [0, 0, 0], i }; a.n++; for (let q = 0; q < 3; q++) a.p[q] += mesh.p[i * 3 + q]; }
    const out = [], L = M4.norm([0.3, 0.8, 0.5]);
    for (let t = 0; t + 2 < cnt; t += 3) { const ks = [key(t), key(t + 1), key(t + 2)]; if (ks[0] === ks[1] || ks[1] === ks[2] || ks[0] === ks[2]) continue;
      for (let j = 0; j < 3; j++) { const a = acc[ks[j]], i = a.i, lit = 0.55 + 0.45 * Math.max(0, M4.dot([mesh.n[i * 3], mesh.n[i * 3 + 1], mesh.n[i * 3 + 2]], L)); out.push(a.p[0] / a.n, a.p[1] / a.n, a.p[2] / a.n, mesh.u[i * 2], mesh.u[i * 2 + 1], lit, lit, lit); } }
    const gl = this.gl, buf = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, buf); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(out), gl.STATIC_DRAW);
    return (ms.lod[level] = { node: ms.node, buf, n: out.length / 8 });
  };
  P.lodLevelAt = function (dist, size) {
    if (!this.tags || !this.tags.has || !this.tags.has("gfx.lod") || !this.tags.get("gfx.lod")) return 0; const k = dist / Math.max(1, size); return k > 90 ? 2 : k > 35 ? 1 : 0;
  };
  // items: [{ gm, pose, M }] (M places the model in camera space: looking down -Z, +X right, +Y up). segs: [{a, b, c}] debug arrows/points
  P.drawViewmodel = function (items, segs) {
    const gl = this.gl, L = this.loc; gl.clear(gl.DEPTH_BUFFER_BIT); gl.uniformMatrix4fv(L.vp, false, new Float32Array(persp(this.fov, this.canvas.width / this.canvas.height, 0.5, 3000))); gl.uniform1f(L.fogD, 1e9);
    gl.enableVertexAttribArray(L.pos); gl.enableVertexAttribArray(L.uv); gl.enableVertexAttribArray(L.col); this.useLights(true);
    const bind = (buf, tex) => { gl.bindBuffer(gl.ARRAY_BUFFER, buf); gl.vertexAttribPointer(L.pos, 3, gl.FLOAT, false, 32, 0); gl.vertexAttribPointer(L.uv, 2, gl.FLOAT, false, 32, 12); gl.vertexAttribPointer(L.col, 3, gl.FLOAT, false, 32, 20); gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, tex); };
    for (const it of items) { this.setMat(it.mat); for (const ms of it.gm.meshes) { gl.uniformMatrix4fv(L.m, false, new Float32Array(M4.mul(it.M, it.pose[ms.node]))); bind(ms.buf, it.gm.tex); gl.drawArrays(gl.TRIANGLES, 0, ms.n); } }
    this.setMat({ lit: 0 });
    if (segs && segs.length) {
      const a = [], th = 0.25; for (const sg of segs) { const d = M4.sub(sg.b, sg.a), dn = M4.norm(d), ref = Math.abs(dn[1]) > 0.9 ? [1, 0, 0] : [0, 1, 0], p1 = M4.norm(M4.cross(dn, ref)), p2 = M4.cross(dn, p1);
        for (const p of [p1, p2]) { const o = [p[0] * th, p[1] * th, p[2] * th], q = (v, s) => [v[0] + o[0] * s, v[1] + o[1] * s, v[2] + o[2] * s], A1 = q(sg.a, 1), A2 = q(sg.a, -1), B1 = q(sg.b, 1), B2 = q(sg.b, -1);
          for (const v of [A1, B1, B2, A1, B2, A2]) a.push(v[0], v[1], v[2], 0, 0, sg.c[0], sg.c[1], sg.c[2]); } }
      const buf = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, buf); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(a), gl.STREAM_DRAW); gl.uniformMatrix4fv(L.m, false, new Float32Array(M4.ident())); bind(buf, this.whiteTex); gl.disable(gl.DEPTH_TEST); gl.drawArrays(gl.TRIANGLES, 0, a.length / 8); gl.enable(gl.DEPTH_TEST); gl.deleteBuffer(buf);
    }
    gl.uniformMatrix4fv(L.m, false, new Float32Array(M4.ident()));
  };

  // ------------------------------------------------------------ free-fly camera
  P.placeCamera = function (x, z, yawDeg, h) { Object.assign(this.cam, { x, z, y: h === undefined ? 56 : h, yaw: (yawDeg || 0) * Math.PI / 180, pitch: 0 }); };
  P.attach = function () {
    const el = this.canvas, self = this; el.tabIndex = 0;
    const kd = e => { if (!self.focus || /INPUT|SELECT|TEXTAREA/.test((e.target && e.target.tagName) || "")) return; const k = e.key.toLowerCase(); if ("wasdqe".includes(k) && k.length === 1 || k === "shift" || k.startsWith("arrow")) { self.keys[k] = true; e.preventDefault(); } };
    const ku = e => { self.keys[e.key.toLowerCase()] = false; };
    const mm = e => { if (document.pointerLockElement === el || self.dragging) { self.cam.yaw += e.movementX * 0.003; self.cam.pitch = Math.max(-1.45, Math.min(1.45, self.cam.pitch - e.movementY * 0.003)); } };
    el.addEventListener("pointerdown", e => { self.focus = true; el.focus(); if (e.button === 2 || e.button === 0) { try { const r = el.requestPointerLock && el.requestPointerLock(); if (r && r.catch) r.catch(() => { self.dragging = true; }); } catch (x) { self.dragging = true; } if (!el.requestPointerLock) self.dragging = true; } });
    addEventListener("pointerup", () => { self.dragging = false; });
    el.addEventListener("blur", () => { self.focus = false; self.keys = {}; }); el.addEventListener("contextmenu", e => e.preventDefault());
    addEventListener("keydown", kd); addEventListener("keyup", ku); addEventListener("mousemove", mm);
    this.detach = () => { removeEventListener("keydown", kd); removeEventListener("keyup", ku); removeEventListener("mousemove", mm); };
  };
  P.step = function (dt) {
    const k = this.keys, c = this.cam, sp = this.speed * (k.shift ? 2.5 : 1) * dt, fx = Math.cos(c.yaw), fz = Math.sin(c.yaw);
    if (k.w || k.arrowup) { c.x += fx * sp; c.z += fz * sp; } if (k.s || k.arrowdown) { c.x -= fx * sp; c.z -= fz * sp; }
    if (k.d) { c.x -= fz * sp; c.z += fx * sp; } if (k.a) { c.x += fz * sp; c.z -= fx * sp; }
    if (k.e) c.y += sp; if (k.q) c.y -= sp; if (k.arrowleft) c.yaw -= 1.8 * dt; if (k.arrowright) c.yaw += 1.8 * dt;
  };
  P.start = function () {
    if (this.running || !this.ok) return; this.running = true; let last = performance.now();
    const loop = t => { if (!this.running) return; const dt = Math.min(0.1, (t - last) / 1000); last = t; this.step(dt); if (this.onFrame) this.onFrame(dt); this.draw(); this.raf = requestAnimationFrame(loop); };
    this.raf = requestAnimationFrame(loop);
  };
  P.stop = function () { this.running = false; cancelAnimationFrame(this.raf); };
  P.destroy = function () { this.stop(); if (this.detach) this.detach(); if (document.pointerLockElement === this.canvas) document.exitPointerLock(); if (this.gl) { const e = this.gl.getExtension("WEBGL_lose_context"); if (e) e.loseContext(); } };
})();
