/* PRIMEngine 3D renderer (WebGL 1). Draws a grid map (see grid.js: walls, floors, slopes, stairs, ceilings on layers)
   with per-cell light, distance fog (start / end) and a mood (colour grading), pixel-crisp (nearest) textures.
   World: X / Z = ground plane, Y = up. Also holds a free-fly camera with mouse look. */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), R = (PRIM.R3D = {}), M4 = PRIM.M4;

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

  // camera orientation vectors for a yaw / pitch (radians): right, up, forward
  R.camBasis = function (yaw, pitch) { const cp = Math.cos(pitch), f = [Math.cos(yaw) * cp, Math.sin(pitch), Math.sin(yaw) * cp], sr = M4.norm(M4.cross(f, [0, 1, 0])); return { s: sr, u: M4.cross(sr, f), f }; };
  // mood of a map (tags mapN.mood*): colour grading applied to everything drawn, before the fog
  R.MOODS = {
    neutral: { tint: "#ffffff", bright: 1, contrast: 1, sat: 1 }, dusk: { tint: "#ffd2a8", bright: 0.95, contrast: 1.1, sat: 0.9 }, night: { tint: "#9fb4ff", bright: 0.7, contrast: 1.15, sat: 0.7 },
    hell: { tint: "#ff9c80", bright: 1, contrast: 1.25, sat: 1.25 }, toxic: { tint: "#c4ff9a", bright: 0.95, contrast: 1.1, sat: 0.85 }, cold: { tint: "#c8e6ff", bright: 1, contrast: 1.05, sat: 0.6 },
    retro: { tint: "#ffffff", bright: 1.05, contrast: 1.3, sat: 1.35 }, noir: { tint: "#ffffff", bright: 1, contrast: 1.35, sat: 0 },
  };
  // ------------------------------------------------------------ the view
  const VS = "attribute vec3 aPos;attribute vec2 aUV;attribute vec3 aCol;uniform mat4 uVP;uniform mat4 uM;varying vec2 vUV;varying vec3 vCol;varying float vD;varying vec3 vW;void main(){vec4 w=uM*vec4(aPos,1.0);vec4 p=uVP*w;gl_Position=p;vUV=aUV;vCol=aCol;vD=p.w;vW=w.xyz;}";
  const FS = [
    "#ifdef GL_FRAGMENT_PRECISION_HIGH", "precision highp float;", "#else", "precision mediump float;", "#endif",
    "varying vec2 vUV;varying vec3 vCol;varying float vD;varying vec3 vW;uniform sampler2D uTex;uniform sampler2D uETex;uniform vec3 uFog;uniform vec2 uFogR;uniform float uBright;",
    "uniform vec4 uLP[8];uniform vec4 uLC[8];uniform vec4 uLN[8];uniform vec4 uLU[8];uniform int uNL;uniform vec3 uCam;",
    // material: base colour (texture x tint), emission (an emission map, or the base colour itself) x strength, extra flat glow (effects)
    "uniform vec3 uTint;uniform vec3 uEmit;uniform float uLit;uniform float uAlpha;uniform float uEmitK;uniform float uEmitMap;",
    // mood: colour tint x brightness, contrast, saturation
    "uniform vec3 uMoodT;uniform float uMoodC;uniform float uMoodS;uniform float uClipY;",
    "void main(){",
    " if(vW.y>uClipY) discard;", " vec4 tx=texture2D(uTex,vUV); vec3 alb=tx.rgb*uTint;",
    " vec3 c=alb*vCol*uBright;",
    "#ifdef DERIV", " vec3 n=normalize(cross(dFdx(vW),dFdy(vW)));", "#else", " vec3 n=vec3(0.0,1.0,0.0);", "#endif",
    " vec3 V=normalize(uCam-vW); if(dot(n,V)<0.0) n=-n;",
    " if(uNL>0){",
    "  vec3 ls=vec3(0.0);",
    "  for(int i=0;i<8;i++){ if(i>=uNL) break;",
    "   vec3 Lp=uLP[i].xyz; float rng=uLP[i].w; vec3 col=uLC[i].rgb; float ty=uLC[i].w; vec3 tg=Lp; float fc=1.0;",
    "   if(ty>0.5){ vec3 an=uLN[i].xyz; vec3 ux=uLU[i].xyz; vec3 vx=cross(an,ux); vec3 rel=vW-Lp; tg=Lp+ux*clamp(dot(rel,ux),-uLN[i].w,uLN[i].w)+vx*clamp(dot(rel,vx),-uLU[i].w,uLU[i].w); }",
    "   vec3 L=tg-vW; float d=length(L); L=L/max(d,0.001);",
    "   if(ty>0.5) fc=clamp(dot(-L,uLN[i].xyz)*2.0+0.15,0.0,1.0);",
    "   float att=clamp(1.0-d/rng,0.0,1.0); att=att*att;",
    "   ls+=col*max(dot(n,L),0.0)*att*fc;",
    "  }",
    "  c+=alb*ls*uLit;",
    " }",
    " vec3 em=uEmitMap>0.5?texture2D(uETex,vUV).rgb:alb; c+=em*uEmitK+uEmit;",
    " float g=dot(c,vec3(0.299,0.587,0.114)); c=mix(vec3(g),c,uMoodS); c=(c-0.5)*uMoodC+0.5; c=max(c*uMoodT,0.0);",
    " float f=clamp((vD-uFogR.x)/max(1.0,uFogR.y-uFogR.x),0.0,1.0); gl_FragColor=vec4(mix(c,uFog,f),uAlpha);",
    "}"].join("\n");

  R.View = function (canvas, o) {
    o = o || {}; const gl = canvas.getContext("webgl", { antialias: false, alpha: false }) || canvas.getContext("experimental-webgl");
    this.canvas = canvas; this.ok = !!gl; if (!gl) return; this.gl = gl;
    const sh = (t, src) => { const s = gl.createShader(t); gl.shaderSource(s, src); gl.compileShader(s); if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s)); return s; };
    const pr = this.pr = gl.createProgram(); gl.attachShader(pr, sh(gl.VERTEX_SHADER, VS)); const dx = gl.getExtension("OES_standard_derivatives"); gl.attachShader(pr, sh(gl.FRAGMENT_SHADER, (dx ? "#extension GL_OES_standard_derivatives : enable\n#define DERIV\n" : "") + FS)); gl.linkProgram(pr); gl.useProgram(pr);
    this.loc = { pos: gl.getAttribLocation(pr, "aPos"), uv: gl.getAttribLocation(pr, "aUV"), col: gl.getAttribLocation(pr, "aCol"), vp: gl.getUniformLocation(pr, "uVP"), m: gl.getUniformLocation(pr, "uM"), tex: gl.getUniformLocation(pr, "uTex"), lp: gl.getUniformLocation(pr, "uLP"), lc: gl.getUniformLocation(pr, "uLC"), ln: gl.getUniformLocation(pr, "uLN"), lu: gl.getUniformLocation(pr, "uLU"), nl: gl.getUniformLocation(pr, "uNL"), cam: gl.getUniformLocation(pr, "uCam"), tint: gl.getUniformLocation(pr, "uTint"), emit: gl.getUniformLocation(pr, "uEmit"), lit: gl.getUniformLocation(pr, "uLit"), alpha: gl.getUniformLocation(pr, "uAlpha"), etex: gl.getUniformLocation(pr, "uETex"), emitK: gl.getUniformLocation(pr, "uEmitK"), emitMap: gl.getUniformLocation(pr, "uEmitMap"), moodT: gl.getUniformLocation(pr, "uMoodT"), moodC: gl.getUniformLocation(pr, "uMoodC"), moodS: gl.getUniformLocation(pr, "uMoodS"), clipY: gl.getUniformLocation(pr, "uClipY"), fog: gl.getUniformLocation(pr, "uFog"), fogR: gl.getUniformLocation(pr, "uFogR"), bright: gl.getUniformLocation(pr, "uBright") };
    gl.enable(gl.DEPTH_TEST); gl.disable(gl.CULL_FACE);
    this.cam = { x: 0, y: 56, z: 0, yaw: Math.PI / 2, pitch: 0 }; this.batches = []; this.texCache = {}; this.sky = [0.08, 0.14, 0.23]; this.fog = [0.02, 0.03, 0.05]; this.fogS = 1e8; this.fogE = 1e9; this.mood = { tint: [1, 1, 1], contrast: 1, sat: 1 }; this.bright = 1; this.fov = 1.15; this.keys = {}; this.speed = 300; this.running = false; this.frames = 0;
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
  // draws just these triangles: by = { textureId: [x,y,z,u,v,r,g,b ...] }
  P.setBatches = function (by, urls) {
    const gl = this.gl; for (const b of this.batches) gl.deleteBuffer(b.buf); this.urls = urls; this.map = null; this.world = null;
    this.batches = Object.keys(by).map(k => { const buf = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, buf); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(by[k]), gl.STATIC_DRAW); return { buf, n: by[k].length / 8, tex: k, mn: [-1e9, -1e9, -1e9], mx: [1e9, 1e9, 1e9] }; });
    this.triCount = this.batches.reduce((s, b) => s + b.n / 3, 0);
  };
  // the map's look: sky, fog and mood tags (mapN.*)
  P.applyLook = function () {
    const m = this.map, T = this.tags; if (!m || !T) return; const g = k => T.get(m.id + "." + k);
    this.sky = hex(g("skyColor")); this.fog = hex(g("fogColor")); const on = g("fogOn") !== false, fs = +g("fogStart") || 0, fe = Math.max(fs + 1, +g("fogEnd") || 1600);
    this.fogS = on ? fs : 1e8; this.fogE = on ? fe : 1e9; if (!on) this.fog = this.sky;
    const pre = R.MOODS[g("mood")], num = (k, d) => g(k) === undefined || g(k) === "" ? d : +g(k), src = pre || { tint: g("moodTint") || "#ffffff", bright: num("moodBrightness", 1), contrast: num("moodContrast", 1), sat: num("moodSaturation", 1) };
    const md = { tint: hex(src.tint), contrast: src.contrast, sat: src.sat }, br = src.bright;
    md.tint = md.tint.map(q => q * br); this.mood = md;
  };
  P.rebuild = function () {
    if (!this.ok || !this.map) return; const gl = this.gl, m = this.map; this.applyLook();
    for (const b of this.batches) gl.deleteBuffer(b.buf); this.batches = []; this.world = null; this.triCount = 0;
    if (m.style !== "grid" || !PRIM.Grid) return;
    const T = this.tags, doors = T ? (m.things || []).filter(th => T.get(th.prop + ".type") === "door").map(th => { const f = PRIM.Grid.thingFrame(m, T, th); f.layer = th.layer || 0; return f; }) : [];
    const r = PRIM.Grid.build(m, { doors }); this.world = PRIM.Grid.index(r.tris);
    this.batches = Object.keys(r.by).map(k => { const buf = gl.createBuffer(), a = r.by[k], cut = k.lastIndexOf("|"), mn = [1e9, 1e9, 1e9], mx = [-1e9, -1e9, -1e9]; gl.bindBuffer(gl.ARRAY_BUFFER, buf); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(a), gl.STATIC_DRAW);
      for (let i = 0; i < a.length; i += 8) for (let q = 0; q < 3; q++) { if (a[i + q] < mn[q]) mn[q] = a[i + q]; if (a[i + q] > mx[q]) mx[q] = a[i + q]; }
      return { buf, n: a.length / 8, tex: k.slice(0, cut), mn, mx }; });
    this.triCount = this.batches.reduce((s, b) => s + b.n / 3, 0);
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
  // material of what is drawn next: { tint, emit (flat glow colour), emitK (emission strength), emitTex (GL texture of the emission map), lit, alpha } (all optional)
  P.setMat = function (m) {
    const gl = this.gl, L = this.loc; m = m || {}; gl.uniform3fv(L.tint, m.tint || [1, 1, 1]); gl.uniform3fv(L.emit, m.emit || [0, 0, 0]); gl.uniform1f(L.lit, m.lit === undefined ? 1 : m.lit); gl.uniform1f(L.emitK, m.emitK || 0); gl.uniform1f(L.clipY, m.clipY === undefined ? 1e9 : m.clipY);
    gl.uniform1f(L.emitMap, m.emitTex ? 1 : 0); gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, m.emitTex || this.whiteTex); gl.activeTexture(gl.TEXTURE0);
    const a = m.alpha === undefined ? 1 : m.alpha; gl.uniform1f(L.alpha, a); if (a < 0.999) { gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA); } else gl.disable(gl.BLEND);
  };
  // fog + mood uniforms (fog = false: the viewmodel pass, never fogged)
  P.lookUniforms = function (fog) {
    const gl = this.gl, L = this.loc, md = this.mood || { tint: [1, 1, 1], contrast: 1, sat: 1 }; gl.uniform3fv(L.fog, this.fog); gl.uniform2fv(L.fogR, fog ? [this.fogS, this.fogE] : [1e8, 1e9]);
    gl.uniform3fv(L.moodT, md.tint); gl.uniform1f(L.moodC, md.contrast); gl.uniform1f(L.moodS, md.sat); gl.uniform1i(L.tex, 0); gl.uniform1i(L.etex, 1);
  };
  // the sky colour with the mood applied (the clear colour)
  P.skyNow = function () { const md = this.mood || { tint: [1, 1, 1], contrast: 1, sat: 1 }, s = this.sky.map(q => q * this.bright), g = s[0] * 0.299 + s[1] * 0.587 + s[2] * 0.114; return s.map((q, i) => Math.max(0, Math.min(1, ((g + (q - g) * md.sat) - 0.5) * md.contrast + 0.5) * md.tint[i])); };
  // ---- culling: view frustum + rooms reachable through open walls
  P.frustum = function () {
    const m = this.vp, row = i => [m[i], m[4 + i], m[8 + i], m[12 + i]], r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3), add = (a, b, s) => a.map((v, i) => v + s * b[i]);
    return [add(r3, r0, 1), add(r3, r0, -1), add(r3, r1, 1), add(r3, r1, -1), add(r3, r2, 1), add(r3, r2, -1)].map(p => { const l = Math.hypot(p[0], p[1], p[2]) || 1; return p.map(v => v / l); });
  };
  const inBox = (pl, mn, mx) => { for (const p of pl) { const x = p[0] > 0 ? mx[0] : mn[0], y = p[1] > 0 ? mx[1] : mn[1], z = p[2] > 0 ? mx[2] : mn[2]; if (p[0] * x + p[1] * y + p[2] * z + p[3] < 0) return false; } return true; };
  P.visible = function () {
    const on = !this.tags || !this.tags.has || !this.tags.has("gfx.culling") || this.tags.get("gfx.culling"); if (!on || !this.vp) return null;
    const pl = this.frustum(); return b => inBox(pl, b.mn, b.mx);
  };

  P.resize = function () { const c = this.canvas, d = Math.min(2, window.devicePixelRatio || 1), w = Math.max(2, Math.floor(c.clientWidth * d)), h = Math.max(2, Math.floor(c.clientHeight * d)); if (c.width !== w || c.height !== h) { c.width = w; c.height = h; } };
  P.draw = function () {
    if (!this.ok) return; const gl = this.gl, c = this.cam; this.resize(); gl.viewport(0, 0, this.canvas.width, this.canvas.height);
    const sk = this.skyNow(); gl.clearColor(sk[0], sk[1], sk[2], 1); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    const vp = mul(persp(this.fov, this.canvas.width / this.canvas.height, 2, 12000), look(c.x, c.y, c.z, c.yaw, c.pitch));
    this.vp = vp; gl.uniformMatrix4fv(this.loc.vp, false, new Float32Array(vp)); this.lookUniforms(true); gl.uniform1f(this.loc.bright, this.bright); gl.uniformMatrix4fv(this.loc.m, false, new Float32Array(M4.ident()));
    const L = this.loc; gl.enableVertexAttribArray(L.pos); gl.enableVertexAttribArray(L.uv); gl.enableVertexAttribArray(L.col);
    this.useLights(false); this.setMat();
    const vis = this.visible(), st = this.stats = { tris: 0, chunks: 0, totalTris: this.triCount || 0, totalChunks: this.batches.length, lights: this.nLights || 0 };
    for (const b of this.batches) { if (vis && !vis(b)) continue; st.chunks++; st.tris += b.n / 3; gl.bindBuffer(gl.ARRAY_BUFFER, b.buf); gl.vertexAttribPointer(L.pos, 3, gl.FLOAT, false, 32, 0); gl.vertexAttribPointer(L.uv, 2, gl.FLOAT, false, 32, 12); gl.vertexAttribPointer(L.col, 3, gl.FLOAT, false, 32, 20);
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, this.texFor(b.tex, this.urls)); gl.drawArrays(gl.TRIANGLES, 0, b.n); }
    if (this.overlay && this.overlay.length) this.drawFx(this.overlay);
    this.frames++;
  };
  // the world ray through a point of the canvas (px, py in CSS pixels from its top-left corner)
  P.rayAt = function (px, py) {
    const c = this.cam, B = R.camBasis(c.yaw, c.pitch), r = this.canvas.getBoundingClientRect(), nx = (px / r.width) * 2 - 1, ny = 1 - (py / r.height) * 2, th = Math.tan(this.fov / 2), ar = r.width / r.height;
    return { o: [c.x, c.y, c.z], d: M4.norm([0, 1, 2].map(k => B.f[k] + B.s[k] * nx * th * ar + B.u[k] * ny * th)) };
  };

  // projectiles and beams in the world. list items: { kind: "line"|"laser"|"sphere"|"mesh", ... } (see play3d.js)
  P.drawFx = function (list) {
    if (!list.length || !this.vp) return; const gl = this.gl, L = this.loc, a = [], Ld = M4.norm([0.3, 0.8, 0.5]); this.useLights(false);
    gl.uniformMatrix4fv(L.vp, false, new Float32Array(this.vp)); this.lookUniforms(true); gl.uniformMatrix4fv(L.m, false, new Float32Array(M4.ident())); gl.enableVertexAttribArray(L.pos); gl.enableVertexAttribArray(L.uv); gl.enableVertexAttribArray(L.col);
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
    for (const f of list) if (f.kind === "mesh" && f.gm) { this.setMat(f.mat); const lv = this.lodLevelAt(Math.hypot(f.p[0] - this.cam.x, f.p[1] - this.cam.y, f.p[2] - this.cam.z), 20 * (f.scale || 1)); this.lodLevel = lv; for (let ms of f.gm.meshes) { ms = this.lodOf(f.gm, ms, lv); gl.uniformMatrix4fv(L.m, false, new Float32Array(M4.mul(f.M, f.pose[ms.node]))); bind(ms.buf, f.tex || f.gm.tex); gl.drawArrays(gl.TRIANGLES, 0, ms.n); } }
    this.setMat(); gl.uniformMatrix4fv(L.m, false, new Float32Array(M4.ident()));
  };

  // nearest solid surface along a ray (origin o, direction d): { t, p, info } or null. extraTris = moving blocks (closed doors)
  P.raycast = function (o, d, maxT) { const h = PRIM.Grid && (this.world || this.extraTris) ? PRIM.Grid.raycast(this.world, o, d, maxT || 1e5, this.extraTris) : null; return h ? { t: h.t, p: h.p, info: h.info } : null; };

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
    const gl = this.gl, L = this.loc; gl.clear(gl.DEPTH_BUFFER_BIT); gl.uniformMatrix4fv(L.vp, false, new Float32Array(persp(this.fov, this.canvas.width / this.canvas.height, 0.5, 3000))); this.lookUniforms(false);
    gl.enableVertexAttribArray(L.pos); gl.enableVertexAttribArray(L.uv); gl.enableVertexAttribArray(L.col); this.useLights(true);
    const bind = (buf, tex) => { gl.bindBuffer(gl.ARRAY_BUFFER, buf); gl.vertexAttribPointer(L.pos, 3, gl.FLOAT, false, 32, 0); gl.vertexAttribPointer(L.uv, 2, gl.FLOAT, false, 32, 12); gl.vertexAttribPointer(L.col, 3, gl.FLOAT, false, 32, 20); gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, tex); };
    for (const it of items) { this.setMat(it.mat); for (const ms of it.gm.meshes) { gl.uniformMatrix4fv(L.m, false, new Float32Array(M4.mul(it.M, it.pose[ms.node]))); bind(ms.buf, it.tex || it.gm.tex); gl.drawArrays(gl.TRIANGLES, 0, ms.n); } }
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
    const kd = e => { if (!self.focus || (self.editMode && !self.looking) || /INPUT|SELECT|TEXTAREA/.test((e.target && e.target.tagName) || "")) return; const k = e.key.toLowerCase(); if ("wasdqe".includes(k) && k.length === 1 || k === "shift" || k.startsWith("arrow")) { self.keys[k] = true; e.preventDefault(); } };
    const ku = e => { self.keys[e.key.toLowerCase()] = false; };
    const mm = e => { if (self.editMode && !self.looking) { if (self.onHover && e.target === el) { const r = el.getBoundingClientRect(); self.onHover(e.clientX - r.left, e.clientY - r.top); } return; } if (document.pointerLockElement === el || self.dragging) { self.cam.yaw += e.movementX * 0.003; self.cam.pitch = Math.max(-1.45, Math.min(1.45, self.cam.pitch - e.movementY * 0.003)); } };
    el.addEventListener("pointerdown", e => { self.focus = true; el.focus(); if (self.editMode) { if (e.button === 0 && self.onPick) { const r = el.getBoundingClientRect(); self.onPick({ x: e.clientX - r.left, y: e.clientY - r.top, shift: e.shiftKey || e.ctrlKey }); return; } if (e.button !== 2) return; self.looking = true; }
      if (e.button === 2 || e.button === 0) { try { const r = el.requestPointerLock && el.requestPointerLock(); if (r && r.catch) r.catch(() => { self.dragging = true; }); } catch (x) { self.dragging = true; } if (!el.requestPointerLock) self.dragging = true; } });
    addEventListener("pointerup", e => { self.dragging = false; if (self.editMode && self.looking && e.button === 2) { self.looking = false; self.keys = {}; try { if (document.pointerLockElement === el) document.exitPointerLock(); } catch (x) {} } });
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
