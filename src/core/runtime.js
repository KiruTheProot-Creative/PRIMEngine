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
.rt-safe{position:absolute;inset:0;border:2px dashed rgba(125,243,255,.85);box-shadow:0 0 0 1px rgba(0,0,0,.6);pointer-events:none;z-index:99999}
.rt-n{position:absolute;transform:translate(-50%,-50%);white-space:pre;line-height:1.1}
.rt-sel{outline:2px dashed #ffb53a;outline-offset:5px}
.rt-handle{position:absolute;width:16px;height:16px;margin:-8px 0 0 -8px;background:#ffb53a;border:2px solid #000;border-radius:3px;z-index:5}
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
      this.paused = false; this.music = null; this.musicId = null; this.alpha = {}; this.els = {}; this.fps = { n: 0, t: 0, v: 0 }; this.dead = false; this.inv = false;
      const host = this.host; host.replaceChildren(); host.classList.add("rt-host"); if (this.mode === "hud") host.style.background = "transparent";
      this.stage = div("rt-stage"); if (this.mode === "hud") this.stage.style.pointerEvents = "none"; this.layer = div("rt-layer"); this.fpsEl = div("rt-fps"); this.stage.append(this.layer, this.fpsEl); host.append(this.stage);
      if (this.mode === "play") { this.hintEl = div("rt-hint"); this.hintEl.textContent = "Esc to exit"; host.append(this.hintEl); setTimeout(() => { if (this.hintEl) this.hintEl.style.opacity = 0; }, 3000); }
      this.ro = new ResizeObserver(() => this.fit()); this.ro.observe(host);
      this.unsub = this.tags.on(n => this.onTag(n));
      this.h = { down: e => this.down(e), move: e => this.move(e), up: e => this.up(e), key: e => this.key(e), fs: () => this.fsChange() };
      this.stage.addEventListener("pointerdown", this.h.down); this.stage.addEventListener("pointermove", this.h.move); this.stage.addEventListener("pointerup", this.h.up); this.stage.addEventListener("pointercancel", this.h.up);
      window.addEventListener("keydown", this.h.key); document.addEventListener("fullscreenchange", this.h.fs);
      this.sh = this.mode !== "edit" ? new PRIM.Script.Host({ tags: this.tags, sfx: (id, v) => this.sfx(id, v), goto: id => { if (this.project.screens.some(s => s.id === id)) this.show(id); }, say: t => { if (this.o.say) this.o.say(t); } }) : null;
      this.fit(); this.show(this.screenId || this.project.screens[0].id);
      this.raf = requestAnimationFrame(t => this.frame(t));
    }
    destroy() { this.dead = true; if (this.sh) this.sh.stopAll(); cancelAnimationFrame(this.raf); this.ro.disconnect(); this.unsub(); window.removeEventListener("keydown", this.h.key); document.removeEventListener("fullscreenchange", this.h.fs); this.stopMusic(); this.host.classList.remove("rt-host"); this.host.replaceChildren(); }
    // positions are stored from the screen middle (x0,y0 = middle, y goes up); the stage itself is drawn from the top-left corner
    px(sc, n) { return W / 2 + (+this.P(sc, n, "x") || 0); }
    py(sc, n) { return H / 2 - (+this.P(sc, n, "y") || 0); }
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
      if (this.sh) { this.sh.stopRuns(); for (const n of sc.nodes) this.script(sc, n, "onStart"); }
      if (this.o.onScreen) this.o.onScreen(id);
      if (sc.kind === "splash" && this.mode !== "edit") this.startSplash(false);
    }
    // ---------------- drawing
    rebuild() {
      const sc = this.screen(), T = this.tags; if (!sc) return;
      this.fpsEl.style.display = this.mode !== "edit" && T.get("gfx.showFps") ? "block" : "none"; const hud = this.mode === "hud", hudEdit = this.mode === "edit" && sc.kind === "hud"; this.stage.style.overflow = hud || hudEdit ? "visible" : "hidden"; if (hudEdit && !this.safeEl) { this.safeEl = div("rt-safe"); this.stage.append(this.safeEl); } if (this.safeEl) this.safeEl.style.display = hudEdit ? "block" : "none"; this.stage.style.filter = hud ? "none" : "brightness(" + (T.get("gfx.brightness") || 1) + ")"; this.stage.style.background = hud ? "transparent" : T.get(sc.id + ".bg") || "#000";
      if (this.splash) return;
      this.layer.replaceChildren(); this.els = {};
      const bgi = T.get(sc.id + ".bgImage"); if (bgi && this.urls[bgi]) { const im = new Image(); im.src = this.urls[bgi]; im.style.cssText = "position:absolute;inset:0;width:100%;height:100%;object-fit:cover"; this.layer.append(im); }
      if (this.game) { const g = div("rt-game"); g.textContent = "Your game starts here.\nThe 3D game view comes in a later step.\n\nClick or press Esc to go back."; g.style.whiteSpace = "pre-line"; this.layer.append(g); return; }
      if (sc.kind === "splash") { const n = sc.nodes.find(x => x.id === this.selId) || sc.nodes[0]; if (n) { const el = this.splashEl(sc, n); el.style.left = "50%"; el.style.top = "50%"; this.layer.append(el); this.els[n.id] = el; if (n.id === this.selId) el.classList.add("rt-sel"); }
        else { const e = div("rt-ph rt-n"); e.textContent = "No splash screens yet. Add one on the left."; e.style.left = "50%"; e.style.top = "50%"; this.layer.append(e); } return; }
      for (const n of sc.nodes) { const el = this.makeNode(sc, n); const st = n.type === "image" && this.P(sc, n, "stretch"); el.style.left = (st ? W / 2 : this.px(sc, n)) + "px"; el.style.top = (st ? H / 2 : this.py(sc, n)) + "px"; if (this.mode === "edit" && n.id === this.selId) el.classList.add("rt-sel"); this.layer.append(el); this.els[n.id] = el; }
      if (this.mode === "edit" && this.selId) { const n = sc.nodes.find(x => x.id === this.selId); if (n && this.sizeProps(n).length) this.addHandle(sc, n); }
    }
    sized(el, w, h) { if (w > 0) el.style.width = w + "px"; if (h > 0) el.style.height = h + "px"; }
    ph(text, w, h) { const e = div("rt-n rt-ph"); e.textContent = text; this.sized(e, w, h); return e; }
    textStyle(el, sc, n) { el.style.fontFamily = fontFamily(this.P(sc, n, "font")); el.style.fontSize = this.P(sc, n, "size") + "px"; el.style.color = this.P(sc, n, "color"); }
    makeNode(sc, n) {
      const T = this.tags, p = k => this.P(sc, n, k); let el;
      if (n.type === "title") { el = div("rt-n"); el.textContent = this.S(sc, n, "text"); this.textStyle(el, sc, n); el.style.opacity = p("opacity"); }
      else if (n.type === "image") { const u = this.urls[p("asset")]; if (u) { el = new Image(); el.src = u; el.className = "rt-n"; el.draggable = false; this.sized(el, p("w"), p("h")); el.style.opacity = p("opacity"); if (p("stretch")) { const A = this.mode === "hud" && this.tags.get("player.hudMove") ? (+this.tags.get("player.hudMoveAmount") || 1) : 0; el.style.width = (W + 140 * A + 4) + "px"; el.style.height = (H + 100 * A + 4) + "px"; } } else el = this.ph("No image chosen", p("w"), p("h")); }
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
      } else if (n.type === "bar") {
        const b = p("bind"), sp = T.spec(b), mt = p("max"), ms = mt && T.spec(mt), v = sp ? +T.get(b) : 0, mx = ms ? +T.get(mt) : sp ? sp.max : 1, mn = ms ? 0 : sp ? sp.min : 0, f = clamp((v - mn) / ((mx - mn) || 1), 0, 1);
        el = div("rt-n"); el.style.width = (p("w") || 260) + "px"; el.style.height = (p("h") || 22) + "px"; el.style.background = p("bg"); el.style.borderRadius = "3px"; el.style.overflow = "hidden"; el.style.opacity = p("opacity");
        const fl = div(""); fl.style.cssText = "position:absolute;left:0;top:0;bottom:0;width:" + f * 100 + "%;background:" + p("color"); const lb = div(""); lb.textContent = this.S(sc, n, "label"); lb.style.cssText = "position:absolute;inset:0;display:grid;place-items:center;letter-spacing:.1em;text-shadow:0 1px 2px #000a";
        lb.style.fontFamily = fontFamily(p("font")); lb.style.fontSize = p("size") + "px"; lb.style.color = p("textColor"); el.append(fl, lb);
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
      if (sp && !this.paused) { const dt = Math.min(0.1, (now - sp.last) / 1000); sp.last = now; sp.t += dt; const sc = this.screen(), dur = this.P(sc, sp.n, "duration");
        if (sp.t >= dur) { sp.i++; sp.t = 0; if (sp.i >= sc.nodes.length) return this.endSplash(); this.splashItem(); } else this.applyFade(); }
      this.fps.n++; if (now - this.fps.t > 500) { this.fps.v = Math.round(this.fps.n * 1000 / (now - this.fps.t)); this.fps.n = 0; this.fps.t = now; if (this.fpsEl.style.display !== "none") this.fpsEl.textContent = this.fps.v + " FPS"; }
    }

    // ---------------- audio
    vol(kind) { const T = this.tags; return clamp((T.get("audio.master") || 0) * (T.get("audio." + kind) || 0), 0, 1); }
    sfx(id, mult) { if (this.mode === "edit" || !id || !this.urls[id]) return; const a = new Audio(this.urls[id]); a.volume = clamp(this.vol("sfx") * (mult === undefined || isNaN(mult) ? 1 : mult), 0, 1); a.play().catch(() => {}); }
    setMusic(id) {
      if (this.mode === "edit") return; if (id === this.musicId) return; this.stopMusic(); this.musicId = id; if (!id || !this.urls[id]) return;
      const a = new Audio(this.urls[id]); a.loop = true; a.volume = this.vol("music"); this.music = a; a.play().catch(() => {});
    }
    stopMusic() { if (this.music) { this.music.pause(); this.music = null; } this.musicId = null; }
    applyAudio() { if (this.music) this.music.volume = this.vol("music"); }

    // ---------------- input
    pt(e) { const r = this.stage.getBoundingClientRect(); return [(e.clientX - r.left) / this.scale, (e.clientY - r.top) / this.scale]; }
    box(n) { const el = this.els[n.id]; if (!el) return null; const sc = this.screen(), x = this.px(sc, n), y = this.py(sc, n), w = Math.max(10, el.offsetWidth), h = Math.max(10, el.offsetHeight); return { x0: x - w / 2, y0: y - h / 2, w, h }; }
    // which tags a corner handle changes: width / height (images, bars, textures, sliders, toggles) or text size (titles, text buttons)
    sizeProps(n) {
      const sc = this.screen(); if (!sc || sc.kind === "splash") return [];
      if (n.type === "image" && this.P(this.screen(), n, "stretch")) return []; if (n.type === "image" || n.type === "bar") return ["w", "h"]; if (n.type === "slider" || n.type === "toggle") return ["w"];
      if (n.type === "button") return this.P(sc, n, "mode") === "texture" ? ["w", "h"] : ["size"]; if (n.type === "title") return ["size"]; return [];
    }
    // four corner handles: dragging one scales the element while the opposite corner stays where it is
    addHandle(sc, n) { this.handles = [[-1, -1], [1, -1], [-1, 1], [1, 1]].map(([cx, cy]) => { const h = div("rt-handle"); h.dataset.cx = cx; h.dataset.cy = cy; h.style.cursor = cx * cy > 0 ? "nwse-resize" : "nesw-resize"; h.title = "Drag to resize (Shift = free proportions for images)"; this.layer.append(h); return h; }); this.placeHandle(); }
    placeHandle() { const n = this.screen() && this.screen().nodes.find(x => x.id === this.selId), b = n && this.box(n); if (!b || !this.handles) return; for (const h of this.handles) { h.style.left = b.x0 + (+h.dataset.cx > 0 ? b.w : 0) + "px"; h.style.top = b.y0 + (+h.dataset.cy > 0 ? b.h : 0) + "px"; } }
    resizeTo(sx, sy, free) {
      const sc = this.screen(), d = this.drag, n = sc.nodes.find(x => x.id === d.id), el = this.els[d.id]; if (!n || !el) return; const props = this.sizeProps(n), set = {}, pre = sc.id + "." + n.id + ".";
      if (props[0] === "size") { const v = clamp(Math.round(Math.abs(sy - d.cy0) * 2 / 1.1), 8, 300); set[pre + "size"] = v; el.style.fontSize = v + "px"; }
      else { let w = clamp(Math.round(Math.abs(sx - d.ax)), 8, W * 2), h = clamp(Math.round(Math.abs(sy - d.ay)), 4, H * 2);
        if (props.length > 1 && (n.type === "image" || n.type === "button") && !free && d.w0 > 0) h = Math.max(4, Math.round(w * d.h0 / d.w0));
        const cx = d.ax + d.sx * w / 2, cy = d.ay + d.sy * h / 2; set[pre + "w"] = w; el.style.width = w + "px"; if (props.length > 1) { set[pre + "h"] = h; el.style.height = h + "px"; }
        set[pre + "x"] = Math.round(cx - W / 2); set[pre + "y"] = Math.round(H / 2 - cy); el.style.left = cx + "px"; el.style.top = cy + "px"; }
      this.tags.setMany(set); this.placeHandle();
    }
    alphaAt(el, u, v) {
      const src = el.src; let c = this.alpha[src];
      if (!c) { if (!el.naturalWidth) return true; const cv = document.createElement("canvas"); cv.width = el.naturalWidth; cv.height = el.naturalHeight; const g = cv.getContext("2d"); g.drawImage(el, 0, 0); try { c = this.alpha[src] = { w: cv.width, h: cv.height, d: g.getImageData(0, 0, cv.width, cv.height).data }; } catch (e) { return true; } }
      const px = clamp(Math.floor(u * c.w), 0, c.w - 1), py = clamp(Math.floor(v * c.h), 0, c.h - 1); return c.d[(py * c.w + px) * 4 + 3] > 12;
    }
    hit(sx, sy, forEdit) {
      const sc = this.screen(); if (!sc || sc.kind === "splash" || this.game) return null;
      for (let i = sc.nodes.length - 1; i >= 0; i--) { const n = sc.nodes[i], b = this.box(n); if (!b || sx < b.x0 || sy < b.y0 || sx > b.x0 + b.w || sy > b.y0 + b.h) continue;
        if (forEdit) return n; if (!["button", "slider", "toggle"].includes(n.type) && !(this.sh && n.script && this.sh.has({ script: n.script }, "onClick"))) continue;
        if (n.type === "button" && this.P(sc, n, "mode") === "texture" && this.els[n.id].tagName === "IMG" && !this.alphaAt(this.els[n.id], (sx - b.x0) / b.w, (sy - b.y0) / b.h)) continue; return n; }
      return null;
    }
    setSlider(n, sx) {
      const sc = this.screen(), b = this.box(n), bind = this.P(sc, n, "bind"), sp = this.tags.spec(bind); if (!b || !sp) return;
      let v = sp.min + (sp.max - sp.min) * clamp((sx - b.x0) / b.w, 0, 1); if (sp.step) v = Math.round(v / sp.step) * sp.step; this.tags.set(bind, Math.round(v * 1000) / 1000);
    }
    down(e) {
      if (this.paused || this.mode === "hud") return; this.stage.setPointerCapture(e.pointerId); const [sx, sy] = this.pt(e);
      if (this.game) { this.game = false; this.rebuild(); return; }
      if (this.splash) { this.skipSplash(); return; }
      if (this.mode === "edit" && e.target && e.target.classList && e.target.classList.contains("rt-handle")) { const sc = this.screen(), n = sc.nodes.find(x => x.id === this.selId), el = n && this.els[n.id], b = n && this.box(n); if (n && el && b) { const hx = +e.target.dataset.cx, hy = +e.target.dataset.cy; this.drag = { id: n.id, rs: true, sx: hx, sy: hy, ax: hx > 0 ? b.x0 : b.x0 + b.w, ay: hy > 0 ? b.y0 : b.y0 + b.h, cy0: b.y0 + b.h / 2, w0: el.offsetWidth, h0: el.offsetHeight }; } return; }
      if (this.mode === "edit") {
        const sc = this.screen(), n = sc.kind === "splash" ? (sc.nodes.find(x => x.id === this.selId) || null) : this.hit(sx, sy, true);
        if (this.o.onSelect) this.o.onSelect(n ? n.id : null);
        if (n && sc.kind !== "splash") { this.selId = n.id; this.drag = { id: n.id, dx: sx - this.px(sc, n), dy: sy - this.py(sc, n) }; this.rebuild(); }
        return;
      }
      if (this.music && this.music.paused) this.music.play().catch(() => {});
      const n = this.hit(sx, sy, false); if (!n) return; this.press = n.id; if (n.type === "slider") this.setSlider(n, sx); this.rebuild();
    }
    move(e) {
      if (this.paused) return; const [sx, sy] = this.pt(e), sc = this.screen(); if (!sc) return;
      if (this.drag && this.drag.rs) return this.resizeTo(sx, sy, e.shiftKey);
      if (this.drag) { const d = this.drag, snap = e.shiftKey ? 10 : 1, x = clamp(Math.round((sx - d.dx - W / 2) / snap) * snap, -W / 2, W / 2), y = clamp(Math.round((H / 2 - (sy - d.dy)) / snap) * snap, -H / 2, H / 2); this.tags.setMany({ [sc.id + "." + d.id + ".x"]: x, [sc.id + "." + d.id + ".y"]: y }); const el = this.els[d.id]; if (el) { el.style.left = W / 2 + x + "px"; el.style.top = H / 2 - y + "px"; } this.placeHandle(); return; }
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
    // node scripts (Node Coder): run the chains of one element that start with this event
    script(sc, n, type, arg) { if (this.sh && n.script) this.sh.trigger({ key: sc.id + "." + n.id, script: n.script }, type, arg); }
    act(sc, n) {
      const T = this.tags, P = k => this.P(sc, n, k); this.script(sc, n, "onClick");
      if (n.type === "button") { this.sfx(P("clickSound")); const a = P("action");
        if (a === "goto") { const t = P("target"); if (this.project.screens.some(s => s.id === t)) this.show(t); }
        else if (a === "start") { if (this.o.onStartGame) this.o.onStartGame(this); else { this.game = true; this.rebuild(); } } else if (a === "quit") { if (this.o.onExit) this.o.onExit(); } }
      else if (n.type === "toggle") { const b = P("bind"), sp = T.spec(b); if (sp && sp.type === "bool") T.set(b, !T.get(b)); }
    }
    key(e) {
      if (this.mode === "edit" || this.mode === "hud" || this.paused) return;
      if (this.splash) { this.skipSplash(); return; }
      if (e.key === "Escape") { if (this.game) { this.game = false; this.rebuild(); } else if (this.mode === "play" && this.o.onExit) this.o.onExit(); return; }
      if (this.sh && !/INPUT|SELECT|TEXTAREA/.test((e.target && e.target.tagName) || "")) { const sc = this.screen(); if (sc) for (const n of sc.nodes) this.script(sc, n, "onKey", e.key === " " ? "space" : e.key === "Control" ? "ctrl" : e.key); }
    }
  };
})();
