/* PRIMEngine screen runtime. Draws one screen on a 1280x720 stage that scales to fit its host.
   Modes: "edit" (click/drag elements, silent), "test" (fully interactive inside the editor), "play" (fullscreen game; settings are remembered).
   Everything it shows is read from tags, so any change to a tag shows up straight away. */
(function () {
  "use strict";
  const PRIM = window.PRIM, W = 1280, H = 720, clamp = (v, a, b) => Math.max(a, Math.min(b, v));
  const FONTS = { "builtin:display": '"Chakra Petch", system-ui, sans-serif', "builtin:sans": "system-ui, sans-serif", "builtin:serif": "Georgia, 'Times New Roman', serif", "builtin:mono": '"Share Tech Mono", ui-monospace, monospace' };
  PRIM.BUILTIN_FONTS = [["builtin:display", "Display (Chakra Petch)"], ["builtin:sans", "Sans"], ["builtin:serif", "Serif"], ["builtin:mono", "Mono"]];
  const fontFamily = v => !v ? FONTS["builtin:display"] : FONTS[v] || '"prim-font-' + v + '", sans-serif';
  const div = cls => { const d = document.createElement("div"); d.className = cls; return d; };

  if (!document.getElementById("rt-style")) {
    const s = document.createElement("style"); s.id = "rt-style";
    s.textContent = `
.rt-host{position:relative;width:100%;height:100%;overflow:hidden;background:#000}
.rt-stage{position:absolute;left:50%;top:50%;width:${W}px;height:${H}px;transform-origin:center;overflow:hidden;user-select:none;-webkit-user-select:none;touch-action:none}
.rt-layer{position:absolute;inset:0}
.rt-n{position:absolute;transform:translate(-50%,-50%);white-space:pre;line-height:1.1}
.rt-sel{outline:2px dashed #ffb53a;outline-offset:5px}
.rt-btn{padding:10px 30px;letter-spacing:.08em;transition:transform .08s}
.rt-btn.press{transform:translate(-50%,-50%) scale(.96)}
.rt-ph{display:grid;place-items:center;border:2px dashed rgba(125,243,255,.5);color:rgba(125,243,255,.8);font:14px "Share Tech Mono",monospace;background:rgba(125,243,255,.06);min-width:160px;min-height:80px;padding:8px}
.rt-row{display:flex;justify-content:space-between;margin-bottom:6px;letter-spacing:.06em}
.rt-track{position:relative;height:8px;background:rgba(255,255,255,.14);border-radius:4px}
.rt-fill{position:absolute;left:0;top:0;bottom:0;border-radius:4px}
.rt-thumb{position:absolute;top:50%;width:18px;height:18px;margin:-9px 0 0 -9px;border-radius:50%;background:#fff;box-shadow:0 0 8px currentColor}
.rt-sw{position:relative;width:52px;height:26px;border-radius:13px;background:rgba(255,255,255,.16);flex:none}
.rt-sw i{position:absolute;top:3px;left:3px;width:20px;height:20px;border-radius:50%;background:#fff;transition:left .12s}
.rt-sw.on i{left:29px}
.rt-fps{position:absolute;left:14px;top:10px;font:16px "Share Tech Mono",monospace;color:#7df3ff;text-shadow:0 1px 2px #000;pointer-events:none}
.rt-game{position:absolute;inset:0;display:grid;place-items:center;text-align:center;background:#000;color:#7ea4b5;font:22px "Share Tech Mono",monospace;line-height:1.7;padding:40px}
.rt-hint{position:absolute;right:14px;top:10px;font:14px "Share Tech Mono",monospace;color:rgba(255,255,255,.45);pointer-events:none;transition:opacity 1s}`;
    document.head.append(s);
  }

  PRIM.Runtime = class Runtime {
    constructor(o) {
      this.o = o; this.host = o.host; this.project = o.project; this.tags = o.tags; this.mode = o.mode || "edit"; this.urls = o.urls || {};
      this.screenId = o.screenId; this.selId = o.selId || null; this.hover = null; this.press = null; this.drag = null; this.game = false; this.splash = null;
      this.music = null; this.musicId = null; this.alpha = {}; this.els = {}; this.fps = { n: 0, t: 0, v: 0 }; this.dead = false; this.inv = false;
      const host = this.host; host.replaceChildren(); host.classList.add("rt-host");
      this.stage = div("rt-stage"); this.layer = div("rt-layer"); this.fpsEl = div("rt-fps"); this.stage.append(this.layer, this.fpsEl); host.append(this.stage);
      if (this.mode === "play") { this.hintEl = div("rt-hint"); this.hintEl.textContent = "Esc to exit"; host.append(this.hintEl); setTimeout(() => { if (this.hintEl) this.hintEl.style.opacity = 0; }, 3000); }
      this.ro = new ResizeObserver(() => this.fit()); this.ro.observe(host);
      this.unsub = this.tags.on(n => this.onTag(n));
      this.h = { down: e => this.down(e), move: e => this.move(e), up: e => this.up(e), key: e => this.key(e), fs: () => this.fsChange() };
      this.stage.addEventListener("pointerdown", this.h.down); this.stage.addEventListener("pointermove", this.h.move); this.stage.addEventListener("pointerup", this.h.up); this.stage.addEventListener("pointercancel", this.h.up);
      window.addEventListener("keydown", this.h.key); document.addEventListener("fullscreenchange", this.h.fs);
      this.fit(); this.show(this.screenId || this.project.screens[0].id);
      this.raf = requestAnimationFrame(t => this.frame(t));
    }
    destroy() { this.dead = true; cancelAnimationFrame(this.raf); this.ro.disconnect(); this.unsub(); window.removeEventListener("keydown", this.h.key); document.removeEventListener("fullscreenchange", this.h.fs); this.stopMusic(); this.host.classList.remove("rt-host"); this.host.replaceChildren(); }
    fit() { const s = Math.min(this.host.clientWidth / W, this.host.clientHeight / H) || 1; this.scale = s; this.stage.style.transform = `translate(-50%,-50%) scale(${s})`; }
    screen() { return this.project.screens.find(s => s.id === this.screenId); }
    P(sc, n, k) { return this.tags.get(sc.id + "." + n.id + "." + k); }
    S(sc, n, k) { return this.tags.interpolate(this.P(sc, n, k)); }
    onTag(names) {
      if (this.mode === "play" && this.o.onPersist) { const ov = {}; for (const k in this.tags.ov || {}) if ((k.startsWith("audio.") || (k.startsWith("gfx.") && k !== "gfx.fullscreen"))) ov[k] = this.tags.ov[k]; this.o.onPersist(ov); }
      if (names.includes("gfx.fullscreen") && this.mode === "play") { const v = this.tags.get("gfx.fullscreen"); if (v && !document.fullscreenElement) { try { this.host.requestFullscreen().catch(() => {}); } catch (e) {} } else if (!v && document.fullscreenElement) document.exitFullscreen().catch(() => {}); }
      this.applyAudio(); this.invalidate();
    }
    fsChange() { if (this.mode === "play" && !document.fullscreenElement && this.tags.get("gfx.fullscreen")) this.tags.set("gfx.fullscreen", false); }
    invalidate() { if (this.inv || this.dead) return; this.inv = true; queueMicrotask(() => { this.inv = false; if (!this.dead && !this.drag) this.rebuild(); }); }

    // ---------------- navigation
    show(id) {
      this.screenId = id; this.splash = null; this.game = false; this.hover = null; this.press = null; const sc = this.screen(); if (!sc) return;
      this.setMusic(this.tags.get(sc.id + ".music") || ""); this.rebuild();
      if (this.o.onScreen) this.o.onScreen(id);
      if (sc.kind === "splash" && this.mode !== "edit") this.startSplash(false);
    }
    // ---------------- drawing
    rebuild() {
      const sc = this.screen(), T = this.tags; if (!sc) return;
      this.fpsEl.style.display = this.mode !== "edit" && T.get("gfx.showFps") ? "block" : "none"; this.stage.style.filter = "brightness(" + (T.get("gfx.brightness") || 1) + ")"; this.stage.style.background = T.get(sc.id + ".bg") || "#000";
      if (this.splash) return;
      this.layer.replaceChildren(); this.els = {};
      const bgi = T.get(sc.id + ".bgImage"); if (bgi && this.urls[bgi]) { const im = new Image(); im.src = this.urls[bgi]; im.style.cssText = "position:absolute;inset:0;width:100%;height:100%;object-fit:cover"; this.layer.append(im); }
      if (this.game) { const g = div("rt-game"); g.textContent = "Your game starts here.\nThe 3D game view comes in a later step.\n\nClick or press Esc to go back."; g.style.whiteSpace = "pre-line"; this.layer.append(g); return; }
      if (sc.kind === "splash") { const n = sc.nodes.find(x => x.id === this.selId) || sc.nodes[0]; if (n) { const el = this.splashEl(sc, n); el.style.left = "50%"; el.style.top = "50%"; this.layer.append(el); this.els[n.id] = el; if (n.id === this.selId) el.classList.add("rt-sel"); }
        else { const e = div("rt-ph rt-n"); e.textContent = "No splash screens yet. Add one on the left."; e.style.left = "50%"; e.style.top = "50%"; this.layer.append(e); } return; }
      for (const n of sc.nodes) { const el = this.makeNode(sc, n); el.style.left = this.P(sc, n, "x") + "px"; el.style.top = this.P(sc, n, "y") + "px"; if (this.mode === "edit" && n.id === this.selId) el.classList.add("rt-sel"); this.layer.append(el); this.els[n.id] = el; }
    }
    sized(el, w, h) { if (w > 0) el.style.width = w + "px"; if (h > 0) el.style.height = h + "px"; }
    ph(text, w, h) { const e = div("rt-n rt-ph"); e.textContent = text; this.sized(e, w, h); return e; }
    textStyle(el, sc, n) { el.style.fontFamily = fontFamily(this.P(sc, n, "font")); el.style.fontSize = this.P(sc, n, "size") + "px"; el.style.color = this.P(sc, n, "color"); }
    makeNode(sc, n) {
      const T = this.tags, p = k => this.P(sc, n, k); let el;
      if (n.type === "title") { el = div("rt-n"); el.textContent = this.S(sc, n, "text"); this.textStyle(el, sc, n); el.style.opacity = p("opacity"); }
      else if (n.type === "image") { const u = this.urls[p("asset")]; if (u) { el = new Image(); el.src = u; el.className = "rt-n"; el.draggable = false; this.sized(el, p("w"), p("h")); el.style.opacity = p("opacity"); } else el = this.ph("No image chosen", p("w"), p("h")); }
      else if (n.type === "button") {
        const hov = this.hover === n.id, prs = this.press === n.id;
        if (p("mode") === "texture") {
          const u = this.urls[(hov && p("hoverTexture")) || p("texture")];
          if (u) { el = new Image(); el.src = u; el.className = "rt-n rt-btn"; el.style.padding = "0"; el.draggable = false; this.sized(el, p("w"), p("h")); if (hov && !p("hoverTexture")) el.style.filter = "brightness(1.3)"; } else el = this.ph("Texture button: choose a texture", p("w"), p("h"));
        } else { el = div("rt-n rt-btn"); el.textContent = this.S(sc, n, "text"); this.textStyle(el, sc, n); el.style.color = hov ? p("hoverColor") : p("color"); }
        if (prs) el.classList.add("press");
      } else if (n.type === "slider") {
        const b = p("bind"), sp = T.spec(b), v = sp ? T.get(b) : 0, mn = sp ? sp.min : 0, mx = sp ? sp.max : 1, f = clamp((v - mn) / ((mx - mn) || 1), 0, 1);
        el = div("rt-n"); el.style.width = (p("w") || 420) + "px"; this.textStyle(el, sc, n); el.style.whiteSpace = "normal"; el.style.setProperty("--a", p("accent"));
        const row = div("rt-row"), a = document.createElement("span"), c = document.createElement("span"); a.textContent = this.S(sc, n, "label"); c.textContent = !sp ? "(no tag)" : mn === 0 && mx === 1 ? Math.round(v * 100) + "%" : (Math.round(v * 100) / 100).toString(); row.append(a, c);
        const tr = div("rt-track"), fl = div("rt-fill"), th = div("rt-thumb"); fl.style.width = f * 100 + "%"; fl.style.background = p("accent"); th.style.left = f * 100 + "%"; th.style.color = p("accent"); tr.append(fl, th); el.append(row, tr);
      } else if (n.type === "toggle") {
        const b = p("bind"), sp = T.spec(b), on = !!(sp && T.get(b));
        el = div("rt-n"); el.style.width = (p("w") || 420) + "px"; el.style.display = "flex"; el.style.justifyContent = "space-between"; el.style.alignItems = "center"; this.textStyle(el, sc, n);
        const a = document.createElement("span"); a.textContent = this.S(sc, n, "label"); const sw = div("rt-sw" + (on ? " on" : "")); sw.style.background = on ? p("accent") : ""; sw.append(document.createElement("i")); el.append(a, sw);
      } else el = this.ph("?");
      return el;
    }
    splashEl(sc, n) {
      const p = k => this.P(sc, n, k); let el;
      if (p("kind") === "image") { const u = this.urls[p("asset")]; if (u) { el = new Image(); el.src = u; el.draggable = false; this.sized(el, p("w"), p("h")); } else el = this.ph("No image chosen", p("w"), p("h")); }
      else { el = div(""); el.textContent = this.S(sc, n, "text"); this.textStyle(el, sc, n); el.style.whiteSpace = "pre"; }
      el.classList.add("rt-n"); return el;
    }

    // ---------------- splash sequence
    startSplash(preview) {
      const sc = this.screen(); if (!sc.nodes.length) return this.endSplash();
      this.splash = { i: 0, t: 0, preview, last: performance.now() }; this.splashItem();
    }
    splashItem() {
      const sc = this.screen(), sp = this.splash, n = sc.nodes[sp.i]; this.stage.style.background = this.tags.get(sc.id + ".bg");
      this.layer.replaceChildren(); const el = this.splashEl(sc, n); el.style.left = "50%"; el.style.top = "50%"; this.layer.append(el); sp.el = el; sp.n = n; this.applyFade();
    }
    static fx(type, p) {
      if (type === "cut") return { o: 1, s: 1, y: 0, b: 0 };
      if (type === "zoom") return { o: p, s: 0.8 + 0.2 * p, y: 0, b: 0 };
      if (type === "slide") return { o: p, s: 1, y: (1 - p) * 40, b: 0 };
      if (type === "blur") return { o: p, s: 1, y: 0, b: (1 - p) * 16 };
      return { o: p, s: 1, y: 0, b: 0 };
    }
    applyFade() {
      const sp = this.splash, sc = this.screen(), n = sp.n, P = k => this.P(sc, n, k), dur = P("duration"), fi = P("fadeIn"), fo = P("fadeOut");
      const a = Runtime.fx(P("fadeInType"), fi > 0 ? clamp(sp.t / fi, 0, 1) : 1), b = Runtime.fx(P("fadeOutType"), fo > 0 ? clamp((dur - sp.t) / fo, 0, 1) : 1), s = sp.el.style;
      s.opacity = a.o * b.o; s.transform = `translate(-50%,-50%) translateY(${a.y + b.y}px) scale(${a.s * b.s})`; s.filter = a.b + b.b > 0.01 ? `blur(${a.b + b.b}px)` : "none";
    }
    skipSplash() { const sp = this.splash; if (!sp) return; if (sp.preview || this.tags.get(this.screenId + ".skippable")) { sp.t = 1e9; } }
    endSplash() {
      const sc = this.screen(), preview = this.splash && this.splash.preview; this.splash = null;
      if (preview) { this.rebuild(); if (this.o.onSplashEnd) this.o.onSplashEnd(); return; }
      const next = this.tags.get(sc.id + ".next"); if (next && this.project.screens.some(s => s.id === next)) this.show(next); else this.rebuild();
    }
    previewSplash() { if (this.mode === "edit" && this.screen().kind === "splash") this.startSplash(true); }
    stopPreview() { if (this.splash && this.splash.preview) { this.splash = null; this.rebuild(); } }
    frame(now) {
      if (this.dead) return; this.raf = requestAnimationFrame(t => this.frame(t));
      const sp = this.splash;
      if (sp) { const dt = Math.min(0.1, (now - sp.last) / 1000); sp.last = now; sp.t += dt; const sc = this.screen(), dur = this.P(sc, sp.n, "duration");
        if (sp.t >= dur) { sp.i++; sp.t = 0; if (sp.i >= sc.nodes.length) return this.endSplash(); this.splashItem(); } else this.applyFade(); }
      this.fps.n++; if (now - this.fps.t > 500) { this.fps.v = Math.round(this.fps.n * 1000 / (now - this.fps.t)); this.fps.n = 0; this.fps.t = now; if (this.fpsEl.style.display !== "none") this.fpsEl.textContent = this.fps.v + " FPS"; }
    }

    // ---------------- audio
    vol(kind) { const T = this.tags; return clamp((T.get("audio.master") || 0) * (T.get("audio." + kind) || 0), 0, 1); }
    sfx(id) { if (this.mode === "edit" || !id || !this.urls[id]) return; const a = new Audio(this.urls[id]); a.volume = this.vol("sfx"); a.play().catch(() => {}); }
    setMusic(id) {
      if (this.mode === "edit") return; if (id === this.musicId) return; this.stopMusic(); this.musicId = id; if (!id || !this.urls[id]) return;
      const a = new Audio(this.urls[id]); a.loop = true; a.volume = this.vol("music"); this.music = a; a.play().catch(() => {});
    }
    stopMusic() { if (this.music) { this.music.pause(); this.music = null; } this.musicId = null; }
    applyAudio() { if (this.music) this.music.volume = this.vol("music"); }

    // ---------------- input
    pt(e) { const r = this.stage.getBoundingClientRect(); return [(e.clientX - r.left) / this.scale, (e.clientY - r.top) / this.scale]; }
    box(n) { const el = this.els[n.id]; if (!el) return null; const sc = this.screen(), x = this.P(sc, n, "x"), y = this.P(sc, n, "y"), w = Math.max(10, el.offsetWidth), h = Math.max(10, el.offsetHeight); return { x0: x - w / 2, y0: y - h / 2, w, h }; }
    alphaAt(el, u, v) {
      const src = el.src; let c = this.alpha[src];
      if (!c) { if (!el.naturalWidth) return true; const cv = document.createElement("canvas"); cv.width = el.naturalWidth; cv.height = el.naturalHeight; const g = cv.getContext("2d"); g.drawImage(el, 0, 0); try { c = this.alpha[src] = { w: cv.width, h: cv.height, d: g.getImageData(0, 0, cv.width, cv.height).data }; } catch (e) { return true; } }
      const px = clamp(Math.floor(u * c.w), 0, c.w - 1), py = clamp(Math.floor(v * c.h), 0, c.h - 1); return c.d[(py * c.w + px) * 4 + 3] > 12;
    }
    hit(sx, sy, forEdit) {
      const sc = this.screen(); if (!sc || sc.kind === "splash" || this.game) return null;
      for (let i = sc.nodes.length - 1; i >= 0; i--) { const n = sc.nodes[i], b = this.box(n); if (!b || sx < b.x0 || sy < b.y0 || sx > b.x0 + b.w || sy > b.y0 + b.h) continue;
        if (forEdit) return n; if (!["button", "slider", "toggle"].includes(n.type)) continue;
        if (n.type === "button" && this.P(sc, n, "mode") === "texture" && this.els[n.id].tagName === "IMG" && !this.alphaAt(this.els[n.id], (sx - b.x0) / b.w, (sy - b.y0) / b.h)) continue; return n; }
      return null;
    }
    setSlider(n, sx) {
      const sc = this.screen(), b = this.box(n), bind = this.P(sc, n, "bind"), sp = this.tags.spec(bind); if (!b || !sp) return;
      let v = sp.min + (sp.max - sp.min) * clamp((sx - b.x0) / b.w, 0, 1); if (sp.step) v = Math.round(v / sp.step) * sp.step; this.tags.set(bind, Math.round(v * 1000) / 1000);
    }
    down(e) {
      this.stage.setPointerCapture(e.pointerId); const [sx, sy] = this.pt(e);
      if (this.game) { this.game = false; this.rebuild(); return; }
      if (this.splash) { this.skipSplash(); return; }
      if (this.mode === "edit") {
        const sc = this.screen(), n = sc.kind === "splash" ? (sc.nodes.find(x => x.id === this.selId) || null) : this.hit(sx, sy, true);
        if (this.o.onSelect) this.o.onSelect(n ? n.id : null);
        if (n && sc.kind !== "splash") { this.selId = n.id; this.drag = { id: n.id, dx: sx - this.P(sc, n, "x"), dy: sy - this.P(sc, n, "y") }; this.rebuild(); }
        return;
      }
      if (this.music && this.music.paused) this.music.play().catch(() => {});
      const n = this.hit(sx, sy, false); if (!n) return; this.press = n.id; if (n.type === "slider") this.setSlider(n, sx); this.rebuild();
    }
    move(e) {
      const [sx, sy] = this.pt(e), sc = this.screen(); if (!sc) return;
      if (this.drag) { const d = this.drag, snap = e.shiftKey ? 10 : 1, x = clamp(Math.round((sx - d.dx) / snap) * snap, 0, W), y = clamp(Math.round((sy - d.dy) / snap) * snap, 0, H); this.tags.setMany({ [sc.id + "." + d.id + ".x"]: x, [sc.id + "." + d.id + ".y"]: y }); const el = this.els[d.id]; if (el) { el.style.left = x + "px"; el.style.top = y + "px"; } return; }
      if (this.mode === "edit" || this.splash || this.game) return;
      if (this.press) { const n = sc.nodes.find(x => x.id === this.press); if (n && n.type === "slider") this.setSlider(n, sx); return; }
      const n = this.hit(sx, sy, false), id = n && n.type === "button" ? n.id : null;
      this.stage.style.cursor = n ? "pointer" : "default";
      if (id !== this.hover) { this.hover = id; if (id) this.sfx(this.P(sc, n, "hoverSound")); this.rebuild(); }
    }
    up(e) {
      if (this.drag) { this.drag = null; this.rebuild(); if (this.o.onChange) this.o.onChange(); return; }
      if (!this.press) return; const [sx, sy] = this.pt(e), sc = this.screen(), id = this.press; this.press = null;
      const n = this.hit(sx, sy, false); this.rebuild(); if (n && n.id === id) this.act(sc, n);
    }
    act(sc, n) {
      const T = this.tags, P = k => this.P(sc, n, k);
      if (n.type === "button") { this.sfx(P("clickSound")); const a = P("action");
        if (a === "goto") { const t = P("target"); if (this.project.screens.some(s => s.id === t)) this.show(t); }
        else if (a === "start") { this.game = true; this.rebuild(); } else if (a === "quit") { if (this.o.onExit) this.o.onExit(); } }
      else if (n.type === "toggle") { const b = P("bind"), sp = T.spec(b); if (sp && sp.type === "bool") T.set(b, !T.get(b)); }
    }
    key(e) {
      if (this.mode === "edit") return;
      if (this.splash) { this.skipSplash(); return; }
      if (e.key === "Escape") { if (this.game) { this.game = false; this.rebuild(); } else if (this.mode === "play" && this.o.onExit) this.o.onExit(); }
    }
  };
})();
