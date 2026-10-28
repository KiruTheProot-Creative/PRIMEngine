/* PRIMEngine audio engine (rebuilt on the Web Audio API).
   - Every sound file is decoded once and kept in memory, so repeated sounds start instantly.
   - Loops are sample-exact (the buffer loops inside the audio hardware clock, no gap like <audio loop> has).
   - Encoders (MP3 especially) add a little silence at the start and end of a file. That silence is found when the
     file is decoded and skipped, so a loop wraps from the last real sample straight to the first one.
   - play(url, volume, loop) returns a handle: { stop(fadeSeconds), setVolume(v), playing }.
     Volume is the final 0..1 value (callers multiply master x sfx x their own volume, as before).
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
  A.play = (url, volume, loop) => {
    const h = { playing: true, vol: clamp(isNaN(volume) ? 1 : +volume, 0, 1), src: null, gain: null, el: null,
      setVolume(v) { this.vol = clamp(isNaN(v) ? 1 : +v, 0, 1); if (this.gain) this.gain.gain.setTargetAtTime(this.vol, ctx.currentTime, 0.015); if (this.el) this.el.volume = this.vol; },
      stop(fade) { if (!this.playing) return; this.playing = false; if (this.el) { this.el.pause(); return; } if (!this.src) return; const t = ctx.currentTime, f = Math.max(0.005, fade || 0.01);
        try { this.gain.gain.cancelScheduledValues(t); this.gain.gain.setValueAtTime(this.gain.gain.value, t); this.gain.gain.linearRampToValueAtTime(0, t + f); this.src.stop(t + f + 0.01); } catch (e) {} } };
    if (!url) { h.playing = false; return h; }
    const c = A.ctx(); if (!c) return fallback(h, url, loop); A.resume();
    A.load(url).then(d => {
      if (!h.playing) return; if (!d) return fallback(h, url, loop);
      const src = c.createBufferSource(), g = c.createGain(); src.buffer = d.buf; g.gain.value = h.vol; src.connect(g); g.connect(c.destination);
      if (loop) { src.loop = true; src.loopStart = d.s; src.loopEnd = d.e; src.start(0, d.s); } else { src.start(0, d.s); src.onended = () => { h.playing = false; }; }
      h.src = src; h.gain = g;
    });
    return h;
  };
  function fallback(h, url, loop) { const a = new Audio(url); a.loop = !!loop; a.volume = h.vol; h.el = a; a.onended = () => { if (!loop) h.playing = false; }; a.play().catch(() => {}); return h; }
})();
