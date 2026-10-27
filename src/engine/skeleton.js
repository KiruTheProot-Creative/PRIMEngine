/* PRIMEngine: skeletal models.
   A model is a tree of nodes (bones/groups), some carrying box meshes, plus named animation clips.
   tools/bake_fbx.py converts a Blockbench FBX into the JSON this class reads. */
(function () {
  "use strict";
  const PRIM = window.PRIM, M4 = PRIM.M4, D2R = PRIM.D2R;

  function sampleCh(ch, t) {
    const ts = ch.t, vs = ch.v, n = ts.length;
    if (t <= ts[0]) return vs[0]; if (t >= ts[n - 1]) return vs[n - 1];
    let lo = 0, hi = n - 1; while (hi - lo > 1) { const m = (lo + hi) >> 1; if (ts[m] <= t) lo = m; else hi = m; }
    return vs[lo] + (vs[hi] - vs[lo]) * ((t - ts[lo]) / (ts[hi] - ts[lo]));
  }

  PRIM.SkeletalModel = class SkeletalModel {
    constructor(data, image) {
      const G = PRIM.gfx;
      this.nodes = data.nodes; this.anims = data.anims; this.world = []; this.meshes = [];
      this.pose = new Float32Array(this.nodes.length * 9); this.prev = new Float32Array(this.nodes.length * 9);
      this.clip = Object.keys(this.anims)[0]; this.t = 0; this.loop = true; this.blend = 0; this.blendD = 0.08; this.done = false;
      this.nodes.forEach((n, i) => { if (!n.mesh) return;
        const m = n.mesh, d = [], c = m.p.length / 3;
        for (let v = 0; v < c; v++) d.push(m.p[v*3], m.p[v*3+1], m.p[v*3+2], m.n[v*3], m.n[v*3+1], m.n[v*3+2], m.u[v*2], m.u[v*2+1]);
        this.meshes.push({ node: i, buf: G.vbo(d), n: c }); });
      this.tex = image ? G.texFromImage(image) : PRIM.gfx.whiteTex;
      this.evaluate(); this.prev.set(this.pose);
    }
    clipNames() { return Object.keys(this.anims); }
    play(name, loop, blend = 0.08) {
      if (!this.anims[name]) return;
      this.prev.set(this.pose); this.blend = blend; this.blendD = blend; this.clip = name; this.t = 0; this.loop = loop; this.done = false;
    }
    update(dt) {
      const clip = this.anims[this.clip]; if (!clip) return;
      this.t += dt;
      if (this.t >= clip.dur) { if (this.loop) this.t %= clip.dur || 1; else { this.t = clip.dur; this.done = true; } }
      this.blend = Math.max(0, this.blend - dt);
      this.evaluate();
    }
    evaluate() {
      const clip = this.anims[this.clip], out = new Float32Array(this.nodes.length * 9);
      this.nodes.forEach((n, i) => { out.set(n.t, i * 9); out.set(n.r, i * 9 + 3); out.set(n.s, i * 9 + 6); });
      if (clip) for (const tr of clip.tracks) for (let a = 0; a < 3; a++) { const ch = tr.c[a]; if (ch) out[tr.n * 9 + tr.p * 3 + a] = sampleCh(ch, this.t); }
      if (this.blend > 0) { const k = this.blend / this.blendD; for (let i = 0; i < out.length; i++) out[i] = out[i] + (this.prev[i] - out[i]) * k; }
      this.pose = out;
      const w = this.world, p = out;
      for (let i = 0; i < this.nodes.length; i++) {
        const o = i * 9;
        let m = M4.mul(M4.trans(p[o], p[o+1], p[o+2]), M4.mul(M4.rotZ(p[o+5] * D2R), M4.mul(M4.rotY(p[o+4] * D2R), M4.rotX(p[o+3] * D2R))));
        if (p[o+6] !== 1 || p[o+7] !== 1 || p[o+8] !== 1) m = M4.mul(m, M4.scale(p[o+6], p[o+7], p[o+8]));
        const par = this.nodes[i].parent; w[i] = par >= 0 ? M4.mul(w[par], m) : m;
      }
    }
    // draw every mesh; `place` puts the model into view space, `proj` is the projection
    draw(place, proj, glow) {
      for (const m of this.meshes) { const model = M4.mul(place, this.world[m.node]);
        PRIM.gfx.drawModelMesh(m, M4.mul(proj, model), model, this.tex, [1, 1, 1], glow); }
    }
  };
})();
