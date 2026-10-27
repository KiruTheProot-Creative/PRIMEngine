/* PRIMEngine 3D renderer (WebGL 1). Draws a map made of sectors: textured walls, floors and ceilings with
   sector light and distance fog, pixel-crisp (nearest) textures. Map x/y = ground plane, height goes up.
   Also holds a free-fly camera with mouse look. */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), R = (PRIM.R3D = {});
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

  // solid wall segments of a map (those not marked open), for collision
  R.walls = function (m, T) {
    const vs = {}; for (const v of m.verts) vs[v.id] = v; const out = [];
    for (const sec of m.sectors) { const ids = sec.v.filter(i => vs[i]); for (let i = 0; i < ids.length; i++) { const A = vs[ids[i]], B = vs[ids[(i + 1) % ids.length]]; if (T.get(m.id + "." + A.id + ".open")) continue; out.push({ ax: A.x, ay: A.y, bx: B.x, by: B.y }); } }
    return out;
  };
  // the room at a point with its floor and ceiling height, or null
  R.sectorInfo = function (m, T, x, y) { const s = R.sectorAt(m, x, y); return s ? { sec: s, floor: +T.get(m.id + "." + s.id + ".floor"), ceil: +T.get(m.id + "." + s.id + ".ceil") } : null; };

  // ------------------------------------------------------------ the view
  const VS = "attribute vec3 aPos;attribute vec2 aUV;attribute vec3 aCol;uniform mat4 uVP;varying vec2 vUV;varying vec3 vCol;varying float vD;void main(){vec4 p=uVP*vec4(aPos,1.0);gl_Position=p;vUV=aUV;vCol=aCol;vD=p.w;}";
  const FS = "precision mediump float;varying vec2 vUV;varying vec3 vCol;varying float vD;uniform sampler2D uTex;uniform vec3 uFog;uniform float uFogD;uniform float uBright;void main(){vec3 c=texture2D(uTex,vUV).rgb*vCol*uBright;float f=clamp(vD/uFogD,0.0,1.0);gl_FragColor=vec4(mix(c,uFog,f*f),1.0);}";

  R.View = function (canvas, o) {
    o = o || {}; const gl = canvas.getContext("webgl", { antialias: false, alpha: false }) || canvas.getContext("experimental-webgl");
    this.canvas = canvas; this.ok = !!gl; if (!gl) return; this.gl = gl;
    const sh = (t, src) => { const s = gl.createShader(t); gl.shaderSource(s, src); gl.compileShader(s); if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s)); return s; };
    const pr = this.pr = gl.createProgram(); gl.attachShader(pr, sh(gl.VERTEX_SHADER, VS)); gl.attachShader(pr, sh(gl.FRAGMENT_SHADER, FS)); gl.linkProgram(pr); gl.useProgram(pr);
    this.loc = { pos: gl.getAttribLocation(pr, "aPos"), uv: gl.getAttribLocation(pr, "aUV"), col: gl.getAttribLocation(pr, "aCol"), vp: gl.getUniformLocation(pr, "uVP"), tex: gl.getUniformLocation(pr, "uTex"), fog: gl.getUniformLocation(pr, "uFog"), fogD: gl.getUniformLocation(pr, "uFogD"), bright: gl.getUniformLocation(pr, "uBright") };
    gl.enable(gl.DEPTH_TEST); gl.disable(gl.CULL_FACE);
    this.cam = { x: 0, y: 56, z: 0, yaw: Math.PI / 2, pitch: 0 }; this.batches = []; this.texCache = {}; this.sky = [0.08, 0.14, 0.23]; this.fog = [0.02, 0.03, 0.05]; this.fogD = 1400; this.bright = 1; this.fov = 1.15; this.keys = {}; this.speed = 300; this.running = false; this.frames = 0;
    this.defTex = this.makeTex(checker());
  };
  const P = R.View.prototype;

  function checker() { const c = document.createElement("canvas"); c.width = c.height = 64; const x = c.getContext("2d"); for (let j = 0; j < 8; j++) for (let i = 0; i < 8; i++) { x.fillStyle = (i + j) % 2 ? "#b8b8b8" : "#d8d8d8"; x.fillRect(i * 8, j * 8, 8, 8); } x.strokeStyle = "#7a7a7a"; x.lineWidth = 2; x.strokeRect(1, 1, 62, 62); return c; }
  P.makeTex = function (src) {
    const gl = this.gl; let w = src.naturalWidth || src.width, h = src.naturalHeight || src.height; const pot = n => Math.min(512, Math.max(2, 1 << Math.round(Math.log2(Math.max(1, n)))));
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
  P.rebuild = function () {
    if (!this.ok || !this.map) return; const gl = this.gl, m = this.map, T = this.tags, vs = {}; for (const v of m.verts) vs[v.id] = v;
    this.sky = hex(T.get(m.id + ".skyColor")); this.fog = hex(T.get(m.id + ".fogColor")); this.fogD = Math.max(100, +T.get(m.id + ".fogDistance") || 1400);
    for (const b of this.batches) gl.deleteBuffer(b.buf); const by = {};
    const add = (texId, tri) => { (by[texId || ""] = by[texId || ""] || []).push(...tri); };
    for (const sec of m.sectors) {
      const ids = sec.v.filter(i => vs[i]); if (ids.length < 3) continue; const pre = m.id + "." + sec.id + ".", fl = +T.get(pre + "floor"), ce = +T.get(pre + "ceil"), li = +T.get(pre + "light");
      const pts = ids.map(i => [vs[i].x, vs[i].y]), tris = R.triangulate(pts);
      for (const [h, key, col] of [[fl, "floor", 1], [ce, "ceil", 0.8]]) {
        const c = hex(T.get(pre + key + "Color")).map(x => x * li * col), tex = T.get(pre + key + "Tex"), out = [];
        for (const t of tris) for (const k of t) out.push(pts[k][0], h, pts[k][1], pts[k][0] / TILE, pts[k][1] / TILE, c[0], c[1], c[2]);
        add(tex, out);
      }
      for (let i = 0; i < ids.length; i++) {
        const A = vs[ids[i]], B = vs[ids[(i + 1) % ids.length]], vp = m.id + "." + A.id + "."; if (T.get(vp + "open") || ce <= fl) continue;
        const len = Math.hypot(B.x - A.x, B.y - A.y), dir = Math.abs(B.x - A.x) > Math.abs(B.y - A.y) ? 1 : 0.82, c = hex(T.get(vp + "color")).map(x => x * li * dir), u1 = len / TILE, v0 = 0, v1 = (ce - fl) / TILE;
        const q = (x, y, z, u, v) => [x, y, z, u, v, c[0], c[1], c[2]];
        add(T.get(vp + "tex"), [...q(A.x, ce, A.y, 0, v0), ...q(B.x, ce, B.y, u1, v0), ...q(B.x, fl, B.y, u1, v1), ...q(A.x, ce, A.y, 0, v0), ...q(B.x, fl, B.y, u1, v1), ...q(A.x, fl, A.y, 0, v1)]);
      }
    }
    this.batches = Object.keys(by).map(k => { const buf = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, buf); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(by[k]), gl.STATIC_DRAW); return { buf, n: by[k].length / 8, tex: k }; });
    this.triCount = this.batches.reduce((s, b) => s + b.n / 3, 0);
  };

  P.resize = function () { const c = this.canvas, d = Math.min(2, window.devicePixelRatio || 1), w = Math.max(2, Math.floor(c.clientWidth * d)), h = Math.max(2, Math.floor(c.clientHeight * d)); if (c.width !== w || c.height !== h) { c.width = w; c.height = h; } };
  P.draw = function () {
    if (!this.ok) return; const gl = this.gl, c = this.cam; this.resize(); gl.viewport(0, 0, this.canvas.width, this.canvas.height);
    gl.clearColor(this.sky[0] * this.bright, this.sky[1] * this.bright, this.sky[2] * this.bright, 1); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    const vp = mul(persp(this.fov, this.canvas.width / this.canvas.height, 2, 12000), look(c.x, c.y, c.z, c.yaw, c.pitch));
    gl.uniformMatrix4fv(this.loc.vp, false, new Float32Array(vp)); gl.uniform3fv(this.loc.fog, this.fog); gl.uniform1f(this.loc.fogD, this.fogD); gl.uniform1f(this.loc.bright, this.bright); gl.uniform1i(this.loc.tex, 0);
    const L = this.loc; gl.enableVertexAttribArray(L.pos); gl.enableVertexAttribArray(L.uv); gl.enableVertexAttribArray(L.col);
    for (const b of this.batches) { gl.bindBuffer(gl.ARRAY_BUFFER, b.buf); gl.vertexAttribPointer(L.pos, 3, gl.FLOAT, false, 32, 0); gl.vertexAttribPointer(L.uv, 2, gl.FLOAT, false, 32, 12); gl.vertexAttribPointer(L.col, 3, gl.FLOAT, false, 32, 20);
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, this.texFor(b.tex, this.urls)); gl.drawArrays(gl.TRIANGLES, 0, b.n); }
    this.frames++;
  };

  // ------------------------------------------------------------ free-fly camera
  P.placeCamera = function (x, z, yawDeg, h) { Object.assign(this.cam, { x, z, y: h === undefined ? 56 : h, yaw: (yawDeg || 0) * Math.PI / 180, pitch: 0 }); };
  P.attach = function () {
    const el = this.canvas, self = this; el.tabIndex = 0;
    const kd = e => { if (!self.focus) return; const k = e.key.toLowerCase(); if ("wasdqe".includes(k) && k.length === 1 || k === "shift" || k.startsWith("arrow")) { self.keys[k] = true; e.preventDefault(); } };
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
