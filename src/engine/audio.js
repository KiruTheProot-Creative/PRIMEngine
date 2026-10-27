/* PRIMEngine: audio.
   Sounds are plain assets. Objects own named "slots" (e.g. armCannon.shot); each slot holds a binding:
   { sound: assetId|null, volume, pitch, loop, start, end }.  If a slot has no sound, a built-in synth
   placeholder plays instead, so a new project is never silent. */
(function () {
  "use strict";
  const PRIM = window.PRIM, A = (PRIM.Audio = { ctx: null, master: null, buffers: {}, peaksCache: {} });

  A.init = function (volume = 0.8) {
    if (A.ctx) return;
    try { A.ctx = new (window.AudioContext || window.webkitAudioContext)(); } catch (e) { return; }
    A.master = A.ctx.createGain(); A.master.gain.value = volume; A.master.connect(A.ctx.destination);
  };
  A.resume = () => { if (A.ctx && A.ctx.state === "suspended") A.ctx.resume().catch(() => {}); };
  A.setVolume = v => { if (A.master) A.master.gain.value = v; };
  A.load = async function (id, url) {
    if (!A.ctx) return false;
    try { const r = await fetch(url); if (!r.ok) throw new Error(r.status); A.buffers[id] = await A.ctx.decodeAudioData(await r.arrayBuffer()); return true; }
    catch (e) { console.warn("sound failed", id, e); return false; }
  };
  A.has = id => !!A.buffers[id];
  A.duration = id => A.buffers[id] ? A.buffers[id].duration : 0;

  A.setListener = function (p, yaw) {
    if (!A.ctx) return; const L = A.ctx.listener, fx = -Math.sin(yaw), fz = -Math.cos(yaw);
    if (L.positionX) { L.positionX.value = p.x; L.positionY.value = p.y; L.positionZ.value = p.z; L.forwardX.value = fx; L.forwardY.value = 0; L.forwardZ.value = fz; L.upX.value = 0; L.upY.value = 1; L.upZ.value = 0; }
    else { L.setPosition(p.x, p.y, p.z); L.setOrientation(fx, 0, fz, 0, 1, 0); }
  };

  // low-level: play an asset. o = { vol, rate, loop, start, end, pos:[x,y,z] }
  A.play = function (id, o = {}) {
    const buf = A.buffers[id]; if (!A.ctx || !buf) return null;
    const src = A.ctx.createBufferSource(); src.buffer = buf; src.playbackRate.value = o.rate || 1;
    const start = Math.max(0, o.start || 0), end = o.end && o.end > start ? Math.min(o.end, buf.duration) : buf.duration;
    const g = A.ctx.createGain(); g.gain.value = o.vol === undefined ? 1 : o.vol; let out = g;
    if (o.pos) { const pn = A.ctx.createPanner(); pn.panningModel = "equalpower"; pn.distanceModel = "inverse"; pn.refDistance = 4; pn.rolloffFactor = 1.1;
      if (pn.positionX) { pn.positionX.value = o.pos[0]; pn.positionY.value = o.pos[1]; pn.positionZ.value = o.pos[2]; } else pn.setPosition(o.pos[0], o.pos[1], o.pos[2]);
      g.connect(pn); out = pn; }
    src.connect(g); out.connect(A.master);
    if (o.loop) { src.loop = true; src.loopStart = start; src.loopEnd = end; src.start(0, start); }
    else src.start(0, start, Math.max(0.01, end - start));
    const h = { src, gain: g, ended: false, stop(fade = 0.08) { try { const t = A.ctx.currentTime; g.gain.cancelScheduledValues(t); g.gain.setValueAtTime(g.gain.value, t); g.gain.linearRampToValueAtTime(0.0001, t + fade); src.stop(t + fade + 0.02); } catch (e) {} } };
    src.onended = () => { h.ended = true; if (o.onend) o.onend(); };
    return h;
  };

  // play a binding. extra = { pos, volMul, rateMul }
  A.playBinding = function (b, extra = {}) {
    if (!b || !b.sound || !A.buffers[b.sound]) return null;
    return A.play(b.sound, { vol: (b.volume === undefined ? 1 : b.volume) * (extra.volMul || 1), rate: (b.pitch || 1) * (extra.rateMul || 1), loop: !!b.loop,
      start: b.start || 0, end: b.end || 0, pos: extra.pos, onend: extra.onend });
  };

  // ---- placeholder synth sounds, keyed by "object.slot"
  function noise(dur, freq, vol) { if (!A.ctx) return; const n = Math.floor(A.ctx.sampleRate * dur), b = A.ctx.createBuffer(1, n, A.ctx.sampleRate), d = b.getChannelData(0);
    for (let i = 0; i < n; i++) d[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / n, 2.2);
    const s = A.ctx.createBufferSource(); s.buffer = b; const f = A.ctx.createBiquadFilter(); f.type = "lowpass"; f.frequency.value = freq; const g = A.ctx.createGain(); g.gain.value = vol;
    s.connect(f).connect(g).connect(A.master); s.start(); }
  function tone(type, f0, f1, dur, vol) { if (!A.ctx) return; const t = A.ctx.currentTime, o = A.ctx.createOscillator(), g = A.ctx.createGain(); o.type = type;
    o.frequency.setValueAtTime(f0, t); o.frequency.exponentialRampToValueAtTime(Math.max(20, f1), t + dur); g.gain.setValueAtTime(vol, t); g.gain.exponentialRampToValueAtTime(0.001, t + dur);
    o.connect(g).connect(A.master); o.start(t); o.stop(t + dur + 0.02); }
  A.synth = {
    "armCannon.shot": () => { tone("sawtooth", 1100, 160, 0.13, 0.16); tone("square", 520, 90, 0.09, 0.08); noise(0.06, 5000, 0.12); },
    "armCannon.chargeStart": () => tone("sine", 200, 900, 0.9, 0.06),
    "armCannon.chargeLoop": () => tone("triangle", 600, 620, 0.3, 0.05),
    "armCannon.chargedShot": () => { tone("sawtooth", 700, 60, 0.4, 0.2); noise(0.25, 3000, 0.2); },
    "drone.shot": () => { tone("triangle", 300, 110, 0.28, 0.16); noise(0.12, 1400, 0.1); },
    "drone.hit": () => tone("square", 480, 220, 0.07, 0.1),
    "drone.death": () => { tone("sawtooth", 260, 40, 0.5, 0.2); noise(0.3, 800, 0.2); },
    "player.hurt": () => { tone("sawtooth", 200, 55, 0.3, 0.22); noise(0.16, 700, 0.14); },
    "door.open": () => { tone("sine", 140, 620, 0.5, 0.13); tone("triangle", 70, 280, 0.5, 0.08); },
    "world.impact": () => { noise(0.2, 2400, 0.2); tone("sine", 160, 50, 0.15, 0.2); },
    "world.pickup": () => { tone("square", 700, 700, 0.06, 0.08); setTimeout(() => tone("square", 1050, 1050, 0.09, 0.08), 70); },
  };

  // play "object.slot" from the project (or its synth placeholder). Returns a handle or null.
  A.playSlot = function (project, key, extra = {}) {
    const [obj, slot] = key.split(".");
    const b = project && project.objects[obj] && project.objects[obj].audio && project.objects[obj].audio[slot];
    const h = A.playBinding(b, extra);
    if (h) return h;
    if (!(b && b.sound) && A.synth[key]) A.synth[key]();
    return null;
  };

  // waveform peaks (0..1) for the editor
  A.peaks = function (id, bins) {
    const key = id + ":" + bins; if (A.peaksCache[key]) return A.peaksCache[key];
    const buf = A.buffers[id]; if (!buf) return null;
    const d = buf.getChannelData(0), step = Math.max(1, Math.floor(d.length / bins)), out = new Float32Array(bins);
    for (let i = 0; i < bins; i++) { let m = 0; for (let j = i * step; j < Math.min(d.length, (i + 1) * step); j += Math.max(1, step >> 5)) m = Math.max(m, Math.abs(d[j])); out[i] = m; }
    return (A.peaksCache[key] = out);
  };
})();
