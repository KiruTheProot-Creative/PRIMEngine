/* PRIMEngine audio engine (rebuilt on the Web Audio API).
   - Every sound file is decoded once and kept in memory, so repeated sounds start instantly.
   - Loops are sample-exact (the buffer loops inside the audio hardware clock, no gap like <audio loop> has).
   - Encoders (MP3 especially) add a little silence at the start and end of a file. That silence is found when the
     file is decoded and skipped, so a loop wraps from the last real sample straight to the first one.
   - play(url, volume, loop) returns a handle: { stop(fadeSeconds), setVolume(v), playing }.
     Volume is the final 0..1 value (callers multiply master x sfx x their own volume, as before).
   - Directional (3D) sound: play(url, vol, loop, at) with at = { p: [x, y, z], ref, max } places the sound in the world
     (stereo pan left / right, a little muffled behind you, fading out from `ref` to silence at `max` units; all values glide, no clicks).
     listen(pos, forward, up, right) moves the ear (Play3D calls it every frame with the camera); handle.setPos(p) moves a sound; handle.mix = last {g, pan, lp}.
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
  // ---- directional sound: our own stereo mixer (no HRTF: that crackled when moved every frame and flipped sides when you turned fast).
  // Each placed sound: source -> gain (its volume) -> distance gain -> low-pass (muffled a little when behind you) -> stereo pan -> speakers.
  // Pan, distance and muffle are worked out here from the ear's right / forward vectors and glide smoothly (no clicks).
  const live = new Set(); A.ear = null; const GLIDE = 0.035;
  const mixOf = (at, ear) => {
    if (!ear) return { g: A.world, pan: 0, lp: 20000 }; const d = [at.p[0] - ear.p[0], at.p[1] - ear.p[1], at.p[2] - ear.p[2]], dist = Math.hypot(d[0], d[1], d[2]), ref = Math.max(1, at.ref || 96), max = Math.max(ref + 1, at.max || 2500);
    const g = dist <= ref ? 1 : Math.max(0, 1 - (dist - ref) / (max - ref)), x = d[0] * ear.r[0] + d[1] * ear.r[1] + d[2] * ear.r[2], z = d[0] * ear.f[0] + d[1] * ear.f[1] + d[2] * ear.f[2], flat = Math.hypot(x, z);
    const near = clamp(flat / 64, 0, 1), pan = flat > 0.001 ? clamp(x / flat, -1, 1) * 0.8 * near : 0, back = flat > 0.001 ? clamp(-z / flat, 0, 1) * near : 0;
    return { g: g * (1 - 0.15 * back) * A.world, pan, lp: 20000 - 14500 * back };
  };
  const applyMix = (h, now) => { if (!h.dg) return; const m = mixOf(h.at, A.ear); h.mix = m; const t = ctx.currentTime;
    if (now) { h.dg.gain.value = m.g; if (h.sp) h.sp.pan.value = m.pan; h.lp.frequency.value = m.lp; return; }
    h.dg.gain.setTargetAtTime(m.g, t, GLIDE); if (h.sp) h.sp.pan.setTargetAtTime(m.pan, t, GLIDE); h.lp.frequency.setTargetAtTime(m.lp, t, GLIDE); };
  // world sounds (placed in the level) can be turned down together, e.g. while the pause menu is open
  A.world = 1; A.setWorld = v => { A.world = clamp(isNaN(v) ? 1 : +v, 0, 1); if (ctx) for (const h of live) { if (h.playing) applyMix(h); } };
  A.listen = (pos, fwd, up, right) => {
    const r = right || [fwd[2], 0, -fwd[0]]; A.ear = { p: pos.slice(), f: fwd.slice(), u: up.slice(), r: r.slice() };
    for (const h of live) { if (!h.playing) { live.delete(h); continue; } applyMix(h); }
  };
  A.play = (url, volume, loop, at) => {
    const h = { playing: true, at: at ? Object.assign({}, at, { p: at.p.slice() }) : null, dg: null, sp: null, lp: null, mix: null,
      setPos(p) { if (this.at) { this.at.p = p.slice(); applyMix(this); } }, vol: clamp(isNaN(volume) ? 1 : +volume, 0, 1), src: null, gain: null, el: null,
      setVolume(v) { this.vol = clamp(isNaN(v) ? 1 : +v, 0, 1); if (this.gain) this.gain.gain.setTargetAtTime(this.vol, ctx.currentTime, 0.015); if (this.el) this.el.volume = this.vol; },
      stop(fade) { if (!this.playing) return; this.playing = false; live.delete(this); if (this.el) { this.el.pause(); return; } if (!this.src) return; const t = ctx.currentTime, f = Math.max(0.005, fade || 0.01);
        try { this.gain.gain.cancelScheduledValues(t); this.gain.gain.setValueAtTime(this.gain.gain.value, t); this.gain.gain.linearRampToValueAtTime(0, t + f); this.src.stop(t + f + 0.01); } catch (e) {} } };
    if (!url) { h.playing = false; return h; }
    const c = A.ctx(); if (!c) return fallback(h, url, loop); A.resume();
    A.load(url).then(d => {
      if (!h.playing) return; if (!d) return fallback(h, url, loop);
      const src = c.createBufferSource(), g = c.createGain(); src.buffer = d.buf; g.gain.value = h.vol; src.connect(g);
      if (h.at) { h.dg = c.createGain(); h.lp = c.createBiquadFilter(); h.lp.type = "lowpass"; h.lp.Q.value = 0.5; h.sp = c.createStereoPanner ? c.createStereoPanner() : null;
        g.connect(h.dg); h.dg.connect(h.lp); if (h.sp) { h.lp.connect(h.sp); h.sp.connect(c.destination); } else h.lp.connect(c.destination); applyMix(h, true); live.add(h); }
      else g.connect(c.destination);
      if (loop) { src.loop = true; src.loopStart = d.s; src.loopEnd = d.e; src.start(0, d.s); } else { src.start(0, d.s); src.onended = () => { h.playing = false; live.delete(h); }; }
      h.src = src; h.gain = g;
    });
    return h;
  };
  function fallback(h, url, loop) { const a = new Audio(url); a.loop = !!loop; a.volume = h.vol; h.el = a; a.onended = () => { if (!loop) h.playing = false; }; a.play().catch(() => {}); return h; }
})();
