/* PRIMEngine: low-level WebGL helpers (shaders, textures, meshes, sprites).
   Three shader programs: "world" (static level, per-pixel lights + fog), "model" (lit models),
   "sprite" (additive glows). No third-party libraries. */
(function () {
  "use strict";
  const PRIM = window.PRIM, G = (PRIM.gfx = {});

  G.FOG_COL = [0.012, 0.03, 0.05];

  G.init = function (canvas) {
    const gl = canvas.getContext("webgl", { antialias: false, alpha: false });
    if (!gl) return false;
    G.gl = gl; G.canvas = canvas;

    function mkProg(vs, fs, attribs) {
      const sh = (t, s) => { const o = gl.createShader(t); gl.shaderSource(o, s); gl.compileShader(o);
        if (!gl.getShaderParameter(o, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(o)); return o; };
      const p = gl.createProgram();
      gl.attachShader(p, sh(gl.VERTEX_SHADER, vs)); gl.attachShader(p, sh(gl.FRAGMENT_SHADER, fs));
      attribs.forEach((n, i) => gl.bindAttribLocation(p, i, n));
      gl.linkProgram(p);
      if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p));
      p._u = {}; p.u = n => (n in p._u) ? p._u[n] : (p._u[n] = gl.getUniformLocation(p, n)); return p;
    }
    G.attribs = n => { for (let i = 0; i < 4; i++) { if (i < n) gl.enableVertexAttribArray(i); else gl.disableVertexAttribArray(i); } };
    G.vbo = arr => { const b = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, b); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(arr), gl.STATIC_DRAW); return b; };

    const P = (G.PROG = {});
    P.world = mkProg(`
attribute vec3 aPos; attribute vec2 aUV; attribute float aShade;
uniform mat4 uVP; varying vec2 vUV; varying vec3 vW; varying float vS;
void main(){ vUV = aUV; vW = aPos; vS = aShade; gl_Position = uVP * vec4(aPos, 1.0); }`, `
precision mediump float;
varying vec2 vUV; varying vec3 vW; varying float vS;
uniform sampler2D uTex; uniform vec3 uCam; uniform vec3 uFogCol; uniform vec2 uFog; uniform vec3 uAmb;
uniform vec4 uLP[8]; uniform vec3 uLC[8];
void main(){
  vec4 t = texture2D(uTex, vUV);
  vec3 l = uAmb;
  for (int i = 0; i < 8; i++) { float d = distance(vW, uLP[i].xyz); float a = clamp(1.0 - d / uLP[i].w, 0.0, 1.0); l += uLC[i] * a * a; }
  vec3 c = t.rgb * l * vS + t.rgb * t.a * 1.5;
  float f = clamp((distance(vW, uCam) - uFog.x) / (uFog.y - uFog.x), 0.0, 1.0);
  gl_FragColor = vec4(mix(c, uFogCol, f), 1.0);
}`, ["aPos", "aUV", "aShade"]);

    P.model = mkProg(`
attribute vec3 aPos; attribute vec3 aN; attribute vec2 aUV;
uniform mat4 uMVP; uniform mat4 uModel; varying vec2 vUV; varying vec3 vN; varying vec3 vW;
void main(){ vUV = aUV; vN = (uModel * vec4(aN, 0.0)).xyz; vW = (uModel * vec4(aPos, 1.0)).xyz; gl_Position = uMVP * vec4(aPos, 1.0); }`, `
precision mediump float;
varying vec2 vUV; varying vec3 vN; varying vec3 vW;
uniform sampler2D uTex; uniform vec3 uTint; uniform vec3 uGlow; uniform vec3 uAmb; uniform vec3 uLDir; uniform vec3 uLCol;
uniform vec3 uCam; uniform vec3 uFogCol; uniform vec2 uFog; uniform float uFogOn;
void main(){
  vec4 t = texture2D(uTex, vUV); if (t.a < 0.1) discard;
  vec3 n = normalize(vN);
  float d = max(dot(n, normalize(uLDir)), 0.0);
  float h = 0.55 + 0.45 * n.y;
  vec3 c = t.rgb * uTint * (uAmb * h + uLCol * d) + uGlow;
  if (uFogOn > 0.5) { float f = clamp((distance(vW, uCam) - uFog.x) / (uFog.y - uFog.x), 0.0, 1.0); c = mix(c, uFogCol, f); }
  gl_FragColor = vec4(c, 1.0);
}`, ["aPos", "aN", "aUV"]);

    P.sprite = mkProg(`
attribute vec2 aC; uniform mat4 uVP; uniform vec3 uCen; uniform vec3 uR; uniform vec3 uU; uniform float uS; varying vec2 vC;
void main(){ vC = aC; vec3 p = uCen + (uR * aC.x + uU * aC.y) * uS; gl_Position = uVP * vec4(p, 1.0); }`, `
precision mediump float;
varying vec2 vC; uniform vec4 uCol; uniform float uPow;
void main(){ float d = length(vC); if (d > 1.0) discard; float a = pow(1.0 - d, uPow); gl_FragColor = vec4(uCol.rgb * a * uCol.a, 1.0); }`, ["aC"]);
    G.QUAD = G.vbo([-1,-1, 1,-1, 1,1, -1,-1, 1,1, -1,1]);

    // ---- textures
    G.texParams = (t, repeat, nearestMip) => {
      gl.bindTexture(gl.TEXTURE_2D, t); gl.generateMipmap(gl.TEXTURE_2D);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, nearestMip ? gl.NEAREST_MIPMAP_NEAREST : gl.NEAREST_MIPMAP_LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      const w = repeat ? gl.REPEAT : gl.CLAMP_TO_EDGE;
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, w); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, w);
    };
    // fn(x, y, rand) returns [r, g, b, emissive]; alpha doubles as an "emissive" mask for world textures
    G.mkTex = (w, h, fn, seed = 0) => {
      const data = new Uint8Array(w * h * 4), r = PRIM.rnd(w * 131 + h * 7 + seed);
      for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) { const c = fn(x, y, r), i = (y * w + x) * 4; data[i] = c[0]; data[i+1] = c[1]; data[i+2] = c[2]; data[i+3] = c[3] === undefined ? 0 : c[3]; }
      const t = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, t);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, w, h, 0, gl.RGBA, gl.UNSIGNED_BYTE, data);
      G.texParams(t, true); return t;
    };
    G.whiteTex = G.mkTex(2, 2, () => [255, 255, 255, 255]);
    G.texFromImage = img => {
      const t = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, t);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, img);
      G.texParams(t, false, true); return t;
    };

    // ---- box meshes (drones, doors): interleaved pos(3) normal(3) uv(2)
    G.boxMesh = (sx, sy, sz) => {
      const x = sx / 2, y = sy / 2, z = sz / 2, d = [];
      const f = (n, c) => { const uv = [[0,1],[1,1],[1,0],[0,0]]; for (const i of [0,1,2,0,2,3]) d.push(c[i][0], c[i][1], c[i][2], n[0], n[1], n[2], uv[i][0], uv[i][1]); };
      f([1,0,0], [[x,-y,z],[x,-y,-z],[x,y,-z],[x,y,z]]); f([-1,0,0], [[-x,-y,-z],[-x,-y,z],[-x,y,z],[-x,y,-z]]);
      f([0,1,0], [[-x,y,z],[x,y,z],[x,y,-z],[-x,y,-z]]); f([0,-1,0], [[-x,-y,-z],[x,-y,-z],[x,-y,z],[-x,-y,z]]);
      f([0,0,1], [[-x,-y,z],[x,-y,z],[x,y,z],[-x,y,z]]); f([0,0,-1], [[x,-y,-z],[-x,-y,-z],[-x,y,-z],[x,y,-z]]);
      return { buf: G.vbo(d), n: 36 };
    };
    G.drawModelMesh = (m, mvp, model, tex, tint, glow) => {
      const p = P.model;
      gl.bindBuffer(gl.ARRAY_BUFFER, m.buf);
      gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 32, 0); gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 32, 12); gl.vertexAttribPointer(2, 2, gl.FLOAT, false, 32, 24);
      gl.bindTexture(gl.TEXTURE_2D, tex);
      gl.uniformMatrix4fv(p.u("uMVP"), false, mvp); gl.uniformMatrix4fv(p.u("uModel"), false, model);
      gl.uniform3fv(p.u("uTint"), tint); gl.uniform3fv(p.u("uGlow"), glow);
      gl.drawArrays(gl.TRIANGLES, 0, m.n);
    };
    G.drawSprite = (x, y, z, size, col, pw) => {
      const p = P.sprite;
      gl.uniform3f(p.u("uCen"), x, y, z); gl.uniform1f(p.u("uS"), size);
      gl.uniform4f(p.u("uCol"), col[0], col[1], col[2], col[3] === undefined ? 1 : col[3]); gl.uniform1f(p.u("uPow"), pw || 2);
      gl.drawArrays(gl.TRIANGLES, 0, 6);
    };
    return true;
  };
})();
