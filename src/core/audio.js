/* PRIMEngine audio engine (rebuilt on the Web Audio API).
   - Every sound file is decoded once and kept in memory, so repeated sounds start instantly.
   - Loops are sample-exact (the buffer loops inside the audio hardware clock, no gap like <audio loop> has).
   - Encoders (MP3 especially) add a little silence at the start and end of a file. That silence is found when the
     file is decoded and skipped, so a loop wraps from the last real sample straight to the first one.
   - play(url, volume, loop) returns a handle: { stop(fadeSeconds), setVolume(v), playing }.
     Volume is the final 0..1 value (callers multiply master x sfx x their own volume, as before).
   - Directional (3D) sound: play(url, vol, loop, at) with at = { p: [x, y, z], ref, max } places the sound in the world
     (HRTF panner: left / right / behind, and it fades out linearly from `ref` to silence at `max` units).
     listen(pos, forward, up, right) moves the ear (Play3D calls it every frame with the camera); handle.setPos(p) moves a sound.
   - If decoding fails (an unusual format), it falls back to an <audio> element with the same handle. */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), A = (PRIM.Audio = {}), clamp = (v, a, b) => Math.max(a, Math.min(b, v));
  let ctx = null; const cache = {};
  A.ctx = () => {
    if (ctx) return ctx; const C = window.AudioContext || window.webkitAudioContext; if (!C) return null;
    try { ctx = new C(); } catch (e) { return null; }
    const wake = () => { if (ctx.state === "suspended") ctx.resume().catch(() => {}); }; addEventListener("pointerdown", wake, true); addEventListener("keydown", wake, true); return ctx;
  };
  A.resume = () => { const c = A.ctx(); if (c && c.state === "suspended") c.resume().catch(() => {}); };
  // the part of the file that holds sound: { s, e } in seconds (leading / trailing silence under about -60 dB is skipped, at most 0.3 s each side)
  function trim(buf) {
    const thr = 0.001, n = buf.length, max = Math.min(n, Math.floor(buf.sampleRate * 0.3)), chans = []; for (let c = 0; c < buf.numberOfChannels; c++) chans.push(buf.getChannelData(c));
    const loud = i => chans.some(d => Math.abs(d[i]) > thr); let a = 0, b = n - 1;
    while (a < max && !loud(a)) a++; while (b > n - 1 - max && b > a && !loud(b)) b--;
    if (a >= max) a = 0; if (b <= n - 1 - max) b = n - 1; return { s: a / buf.sampleRate, e: (b + 1) / buf.sampleRate };
  }
  A.load = url => {
    if (!url) return Promise.resolve(null); if (cache[url]) return cache[url]; const c = A.ctx(); if (!c) return Promise.resolve(null);
    cache[url] = fetch(url).then(r => r.arrayBuffer()).then(ab => new Promise((ok, no) => { const p = c.decodeAudioData(ab, ok, no); if (p && p.catch) p.catch(no); }))
      .then(buf => Object.assign({ buf }, trim(buf))).catch(() => null);
    return cache[url];
  };
  A.preload = urls => { for (const u of urls) A.load(u); };
  // the world may be left-handed for Web Audio (its right = forward x up): then mirror one axis of everything
  let flip = false; const fx = v => flip ? [v[0], v[1], -v[2]] : v;
  const setP = (pn, p) => { p = fx(p); if (pn.positionX) { const t = ctx.currentTime; pn.positionX.setTargetAtTime(p[0], t, 0.02); pn.positionY.setTargetAtTime(p[1], t, 0.02); pn.positionZ.setTargetAtTime(p[2], t, 0.02); } else pn.setPosition(p[0], p[1], p[2]); };
  A.listen = (pos, fwd, up, right) => {
    const c = A.ctx(); if (!c) return; const L = c.listener;
    if (right) { const cr = [fwd[1] * up[2] - fwd[2] * up[1], fwd[2] * up[0] - fwd[0] * up[2], fwd[0] * up[1] - fwd[1] * up[0]]; flip = cr[0] * right[0] + cr[1] * right[1] + cr[2] * right[2] < 0; }
    const p = fx(pos), f = fx(fwd), u = fx(up); A.ear = { p: pos.slice(), f: fwd.slice(), flip };
    if (L.positionX) { const t = c.currentTime; L.positionX.setValueAtTime(p[0], t); L.positionY.setValueAtTime(p[1], t); L.positionZ.setValueAtTime(p[2], t); L.forwardX.setValueAtTime(f[0], t); L.forwardY.setValueAtTime(f[1], t); L.forwardZ.setValueAtTime(f[2], t); L.upX.setValueAtTime(u[0], t); L.upY.setValueAtTime(u[1], t); L.upZ.setValueAtTime(u[2], t); }
    else { L.setPosition(p[0], p[1], p[2]); L.setOrientation(f[0], f[1], f[2], u[0], u[1], u[2]); }
  };
  A.play = (url, volume, loop, at) => {
    const h = { playing: true, at: at || null, pan: null,
      setPos(p) { if (this.at) this.at.p = p.slice(); if (this.pan) setP(this.pan, p); }, vol: clamp(isNaN(volume) ? 1 : +volume, 0, 1), src: null, gain: null, el: null,
      setVolume(v) { this.vol = clamp(isNaN(v) ? 1 : +v, 0, 1); if (this.gain) this.gain.gain.setTargetAtTime(this.vol, ctx.currentTime, 0.015); if (this.el) this.el.volume = this.vol; },
      stop(fade) { if (!this.playing) return; this.playing = false; if (this.el) { this.el.pause(); return; } if (!this.src) return; const t = ctx.currentTime, f = Math.max(0.005, fade || 0.01);
        try { this.gain.gain.cancelScheduledValues(t); this.gain.gain.setValueAtTime(this.gain.gain.value, t); this.gain.gain.linearRampToValueAtTime(0, t + f); this.src.stop(t + f + 0.01); } catch (e) {} } };
    if (!url) { h.playing = false; return h; }
    const c = A.ctx(); if (!c) return fallback(h, url, loop); A.resume();
    A.load(url).then(d => {
      if (!h.playing) return; if (!d) return fallback(h, url, loop);
      const src = c.createBufferSource(), g = c.createGain(); src.buffer = d.buf; g.gain.value = h.vol; src.connect(g);
      if (h.at && c.createPanner) { const pn = c.createPanner(), a = h.at; pn.panningModel = "HRTF"; pn.distanceModel = "linear"; pn.refDistance = Math.max(1, a.ref || 96); pn.maxDistance = Math.max(pn.refDistance + 1, a.max || 2500); pn.rolloffFactor = 1;
        if (pn.positionX) { const p = fx(a.p); pn.positionX.value = p[0]; pn.positionY.value = p[1]; pn.positionZ.value = p[2]; } else setP(pn, a.p); g.connect(pn); pn.connect(c.destination); h.pan = pn; }
      else g.connect(c.destination);
      if (loop) { src.loop = true; src.loopStart = d.s; src.loopEnd = d.e; src.start(0, d.s); } else { src.start(0, d.s); src.onended = () => { h.playing = false; }; }
      h.src = src; h.gain = g;
    });
    return h;
  };
  function fallback(h, url, loop) { const a = new Audio(url); a.loop = !!loop; a.volume = h.vol; h.el = a; a.onended = () => { if (!loop) h.playing = false; }; a.play().catch(() => {}); return h; }
})();
