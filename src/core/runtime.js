/* PRIMEngine screen runtime. Draws one screen on a 1280x720 stage that scales to fit its host.
   Modes: "edit" (click/drag elements, silent), "test" (fully interactive inside the editor), "play" (fullscreen game; settings are remembered). HUD screens are NOT drawn here: see hud.js (game) and studio/hudeditor.js (editor).
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
.rt-vid{background:#000;object-fit:contain}
.rt-snd{padding:6px 12px;border:1px dashed rgba(125,243,255,.6);border-radius:14px;color:#7df3ff;font:14px "Share Tech Mono",monospace;background:rgba(0,0,0,.4)}
.rt-pro{position:absolute;inset:0;overflow:hidden}
.rt-pro .persp{position:absolute;inset:0;perspective:520px;perspective-origin:50% 30%;-webkit-mask-image:linear-gradient(to bottom,transparent 0%,#000 38%);mask-image:linear-gradient(to bottom,transparent 0%,#000 38%)}
.rt-pro .plane{position:absolute;left:50%;top:0;transform-origin:50% 100%;height:100%}
.rt-pro .crawl{position:absolute;left:0;right:0;white-space:pre-wrap;line-height:1.35}
.rt-pro .intro{position:absolute;inset:0;display:grid;place-items:center;text-align:center;white-space:pre-wrap;opacity:0;line-height:1.4}
.rt-pro .typer{position:absolute;left:50%;transform:translateX(-50%);white-space:pre-wrap;line-height:1.45;transition:opacity .4s}
.rt-pro .typer i{visibility:hidden;font-style:normal}
.rt-hint{position:absolute;right:14px;top:10px;font:14px "Share Tech Mono",monospace;color:rgba(255,255,255,.45);pointer-events:none;transition:opacity 1s}`;
    document.head.append(s);
  }

  PRIM.Runtime = class Runtime {
    constructor(o) {
      this.o = o; this.host = o.host; this.project = o.project; this.tags = o.tags; this.mode = o.mode || "edit"; this.urls = o.urls || {};
      this.screenId = o.screenId; this.selId = o.selId || null; this.hover = null; this.press = null; this.drag = null; this.game = false; this.splash = null;
      this.paused = false; this.music = null; this.musicId = null; this.alpha = {}; this.els = {}; this.fps = { n: 0, t: 0, v: 0 }; this.dead = false; this.inv = false;
      const host = this.host; host.replaceChildren(); host.classList.add("rt-host"); this.hist = []; if (o.transparent) host.style.background = "transparent";
      this.stage = div("rt-stage"); this.layer = div("rt-layer"); this.fpsEl = div("rt-fps"); this.stage.append(this.layer, this.fpsEl); host.append(this.stage);
      if (this.mode === "play" && !o.noHint) { this.hintEl = div("rt-hint"); this.hintEl.textContent = "Esc to exit"; host.append(this.hintEl); setTimeout(() => { if (this.hintEl) this.hintEl.style.opacity = 0; }, 3000); }
      this.ro = new ResizeObserver(() => this.fit()); this.ro.observe(host);
      this.unsub = this.tags.on(n => this.onTag(n));
      this.h = { down: e => this.down(e), move: e => this.move(e), up: e => this.up(e), key: e => this.key(e), fs: () => this.fsChange() };
      this.stage.addEventListener("pointerdown", this.h.down); this.stage.addEventListener("pointermove", this.h.move); this.stage.addEventListener("pointerup", this.h.up); this.stage.addEventListener("pointercancel", this.h.up);
      window.addEventListener("keydown", this.h.key); document.addEventListener("fullscreenchange", this.h.fs);
      this.sh = this.mode !== "edit" ? new PRIM.Script.Host({ tags: this.tags, sfx: (id, v) => this.sfx(id, v), loop: (id, v) => this.loopSfx(id, v), stopLoop: id => this.stopLoops(id), goto: id => { if (this.project.screens.some(s => s.id === id)) this.show(id); }, say: t => { if (this.o.say) this.o.say(t); }, broadcast: name => { const sc = this.screen(); if (sc) for (const n of sc.nodes) this.script(sc, n, "onMessage", name); } }) : null;
      if (this.sh) this.sh.watch(() => { const sc = this.screen(); return sc ? sc.nodes.filter(n => n.script).map(n => ({ key: sc.id + "." + n.id, script: n.script })) : []; });
      this.fit(); this.show(this.screenId || this.project.screens[0].id);
      this.raf = requestAnimationFrame(t => this.frame(t));
    }
    destroy() { this.stopVideos(); this.stopSounds(true); this.pro = null; this.stopLoops(""); this.dead = true; if (this.sh) this.sh.stopAll(); cancelAnimationFrame(this.raf); this.ro.disconnect(); this.unsub(); window.removeEventListener("keydown", this.h.key); document.removeEventListener("fullscreenchange", this.h.fs); this.stopMusic(); this.host.classList.remove("rt-host"); this.host.style.background = ""; this.host.style.backdropFilter = ""; this.host.replaceChildren(); }
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
    show(id, isBack) {
      if (!isBack && this.screenId && this.screenId !== id && this.mode !== "edit") { this.hist.push(this.screenId); if (this.hist.length > 30) this.hist.shift(); }
      if (this.screenId !== id) { this.stopVideos(); this.stopSounds(false); } this.pro = null;
      this.screenId = id; this.splash = null; this.game = false; this.hover = null; this.press = null; const sc = this.screen(); if (!sc) return; if (this.mode !== "edit" && PRIM.Save) PRIM.Save.refreshTags(this.tags, this.project.id);
      this.setMusic(this.tags.get(sc.id + ".music") || ""); this.rebuild();
      if (this.sh) { this.sh.stopRuns(); for (const n of sc.nodes) this.script(sc, n, "onStart"); }
      if (this.o.onScreen) this.o.onScreen(id);
      if (sc.kind === "splash" && this.mode !== "edit") this.startSplash(false);
      if (sc.kind === "prologue" && this.mode !== "edit") this.proStart(false);
      this.startSounds(sc);
    }
    // ---------------- drawing
    rebuild() {
      const sc = this.screen(), T = this.tags; if (!sc) return;
      this.fpsEl.style.display = this.mode !== "edit" && T.get("gfx.showFps") ? "block" : "none"; this.stage.style.filter = "brightness(" + (T.get("gfx.brightness") || 1) + ")"; this.stage.style.background = T.get(sc.id + ".bg") || "#000";
      if (sc.kind === "pause") {   // a pause menu darkens (and blurs) the frozen game behind it; in the editor a stand-in picture plays the game
        const c = Runtime.rgba(T.get(sc.id + ".bg") || "#000000", +T.get(sc.id + ".dim")), bl = +T.get(sc.id + ".blur") || 0;
        if (this.o.transparent) { this.stage.style.background = "transparent"; this.host.style.background = c; this.host.style.backdropFilter = bl > 0 ? "blur(" + bl + "px)" : ""; }
        else this.stage.style.background = "linear-gradient(" + c + "," + c + "), linear-gradient(165deg,#34506a 0%,#1a2633 45%,#2a1d12 75%,#4a3018 100%)";
      }
      if (this.splash || this.pro) return;
      this.layer.replaceChildren(); this.els = {};
      const bgi = T.get(sc.id + ".bgImage"); if (bgi && this.urls[bgi]) { const im = new Image(); im.src = this.urls[bgi]; im.style.cssText = "position:absolute;inset:0;width:100%;height:100%;object-fit:cover"; this.layer.append(im); }
      if (this.game) { const g = div("rt-game"); g.textContent = "Your game starts here.\nThe 3D game view comes in a later step.\n\nClick or press Esc to go back."; g.style.whiteSpace = "pre-line"; this.layer.append(g); return; }
      if (sc.kind === "prologue") { const pr = this.proDraw(sc, true); this.layer.append(pr.box); if (pr.st === "scroller") { pr.crawl.style.top = (+T.get(sc.id + ".tilt") > 0 ? H * 0.35 : H * 0.2) + "px"; } else if (pr.pages.length) this.proShow(pr.pages[0], pr.pages[0].length, pr.ty); if (pr.phase === "intro" && !pr.pages) pr.intro.style.opacity = 0; return; }
      if (sc.kind === "splash") { const n = sc.nodes.find(x => x.id === this.selId) || sc.nodes[0]; if (n) { const el = this.splashEl(sc, n); el.style.left = "50%"; el.style.top = "50%"; this.layer.append(el); this.els[n.id] = el; if (n.id === this.selId) el.classList.add("rt-sel"); }
        else { const e = div("rt-ph rt-n"); e.textContent = "No splash screens yet. Add one on the left."; e.style.left = "50%"; e.style.top = "50%"; this.layer.append(e); } return; }
      for (const n of sc.nodes) { const el = this.makeNode(sc, n); const st = (n.type === "image" || n.type === "video") && this.P(sc, n, "stretch"); el.style.left = (st ? W / 2 : this.px(sc, n)) + "px"; el.style.top = (st ? H / 2 : this.py(sc, n)) + "px"; if (this.mode === "edit" && n.id === this.selId) el.classList.add("rt-sel"); this.layer.append(el); this.els[n.id] = el; }
      if (this.mode === "edit" && this.selId) { const n = sc.nodes.find(x => x.id === this.selId); if (n && this.sizeProps(n).length) this.addHandle(sc, n); }
    }
    sized(el, w, h) { if (w > 0) el.style.width = w + "px"; if (h > 0) el.style.height = h + "px"; }
    ph(text, w, h) { const e = div("rt-n rt-ph"); e.textContent = text; this.sized(e, w, h); return e; }
    textStyle(el, sc, n) { el.style.fontFamily = fontFamily(this.P(sc, n, "font")); el.style.fontSize = this.P(sc, n, "size") + "px"; el.style.color = this.P(sc, n, "color"); }
    makeNode(sc, n) {
      const T = this.tags, p = k => this.P(sc, n, k); let el;
      if (n.type === "title") { el = div("rt-n"); el.textContent = this.S(sc, n, "text"); this.textStyle(el, sc, n); el.style.opacity = p("opacity"); }
      else if (n.type === "image") { const u = this.urls[p("asset")]; if (u) { el = new Image(); el.src = u; el.className = "rt-n"; el.draggable = false; this.sized(el, p("w"), p("h")); el.style.opacity = p("opacity"); if (p("stretch")) { el.style.width = W + "px"; el.style.height = H + "px"; } } else el = this.ph("No image chosen", p("w"), p("h")); }
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
      } else if (n.type === "pips") { el = div("rt-n"); if (PRIM.HUD && PRIM.HUD.pips) PRIM.HUD.pips(el, T, p);
      } else if (n.type === "bar") {
        const b = p("bind"), sp = T.spec(b), mt = p("max"), ms = mt && T.spec(mt), v = sp ? +T.get(b) : 0, mx = ms ? +T.get(mt) : sp ? sp.max : 1, mn = ms ? 0 : sp ? sp.min : 0, f = clamp((v - mn) / ((mx - mn) || 1), 0, 1);
        el = div("rt-n"); el.style.width = (p("w") || 260) + "px"; el.style.height = (p("h") || 22) + "px"; el.style.background = p("bg"); el.style.borderRadius = "3px"; el.style.overflow = "hidden"; el.style.opacity = p("opacity");
        const fl = div(""); fl.style.cssText = "position:absolute;left:0;top:0;bottom:0;width:" + f * 100 + "%;background:" + p("color"); const lb = div(""); lb.textContent = this.S(sc, n, "label"); lb.style.cssText = "position:absolute;inset:0;display:grid;place-items:center;letter-spacing:.1em;text-shadow:0 1px 2px #000a";
        lb.style.fontFamily = fontFamily(p("font")); lb.style.fontSize = p("size") + "px"; lb.style.color = p("textColor"); el.append(fl, lb);
      } else if (n.type === "video") { el = this.videoEl(sc, n); }
      else if (n.type === "sound") { const a = (this.project.assets || []).find(q => q.id === p("asset")); el = div("rt-n rt-snd"); el.textContent = "♪ " + (a ? a.name : "no sound chosen"); if (this.mode !== "edit") el.style.display = "none"; }
      else el = this.ph("?");
      return el;
    }
    // a video element: kept between redraws (so it keeps playing), sized like an image or stretched over the whole screen
    videoEl(sc, n) {
      const p = k => this.P(sc, n, k), u = this.urls[p("asset")]; if (!u) return this.ph("Video: choose an .mp4 file", p("w") || 640, p("h") || 360);
      this.vids = this.vids || {}; let v = this.vids[n.id]; if (!v || v.dataset.src !== u) { v = document.createElement("video"); v.dataset.src = u; v.src = u; v.className = "rt-n rt-vid"; v.playsInline = true; v.preload = "auto"; this.vids[n.id] = v;
        if (this.mode === "edit") { v.muted = true; v.addEventListener("loadeddata", () => { try { v.currentTime = Math.min(0.5, (v.duration || 1) / 4); } catch (e) {} }); }
        else { v.addEventListener("ended", () => { if (this.vids && this.vids[n.id] === v && !v.loop) this.videoEnd(sc, n); }); v.volume = clamp(this.vol("music") * (+p("volume") || 0), 0, 1); const go = () => v.play().catch(() => { v.muted = true; v.play().catch(() => {}); }); go(); } }
      v.loop = !!p("loop"); if (this.mode !== "edit") v.volume = clamp(this.vol("music") * (+p("volume") || 0), 0, 1);
      if (p("stretch")) { v.style.width = W + "px"; v.style.height = H + "px"; v.style.objectFit = p("fit") || "contain"; } else { v.style.width = (p("w") || 640) + "px"; v.style.height = p("h") ? p("h") + "px" : "auto"; v.style.objectFit = "contain"; }
      v.style.opacity = p("opacity"); return v; }
    videoEnd(sc, n) { if (!this.vids || !this.vids[n.id]) return; const v = this.vids[n.id]; v.pause(); delete this.vids[n.id]; this.thenDo(this.P(sc, n, "then"), this.P(sc, n, "next")); }
    stopVideos() { for (const v of Object.values(this.vids || {})) { try { v.pause(); v.removeAttribute("src"); v.load(); } catch (e) {} } this.vids = {}; }
    // a key or click while a skippable video plays (or a prologue runs) ends it
    skipMedia() { if (this.mode === "edit") return false; if (this.pro) { if (this.pro.preview || this.tags.get(this.screenId + ".skippable")) { this.proEnd(); } return true; }
      const sc = this.screen(); if (!sc) return false; for (const n of sc.nodes) if (n.type === "video" && this.vids && this.vids[n.id] && !this.vids[n.id].paused && this.P(sc, n, "skippable")) { this.videoEnd(sc, n); return true; } return false; }
    // what happens after a video / prologue: nothing, another screen, a new game, back, the main menu
    thenDo(what, next) { if (this.dead) return; if (what === "goto") { if (next && this.project.screens.some(s => s.id === next)) this.show(next); }
      else if (what === "start") { const opt = { slot: +this.tags.get("save.slot") || 1 }; if (this.o.onStartGame) this.o.onStartGame(this, opt); else { this.game = true; this.rebuild(); } }
      else if (what === "back") this.back();
      else if (what === "menu") { if (this.o.onMenu) this.o.onMenu(); else { const m = this.project.screens.find(s => s.id === "main") || this.project.screens.find(s => s.kind === "menu"); if (m) this.show(m.id); } } }
    // screen sounds (element "sound"): start when the screen opens (after their delay), stop when it changes (unless told not to)
    startSounds(sc) { this.stopSounds(false); if (this.mode === "edit") return; this.snd = this.snd || [];
      for (const n of sc.nodes) { if (n.type !== "sound") continue; const p = k => this.P(sc, n, k), u = this.urls[p("asset")]; if (!u) continue; const it = { stop: !!p("stopOnLeave"), h: null, tm: 0, sid: sc.id };
        it.tm = setTimeout(() => { if (this.dead || (it.stop && this.screenId !== it.sid)) return; it.h = PRIM.Audio.play(u, clamp(this.vol(p("channel") === "music" ? "music" : "sfx") * (+p("volume") || 0), 0, 1), !!p("loop")); }, Math.max(0, +p("delay") || 0) * 1000); this.snd.push(it); } }
    stopSounds(all) { this.snd = (this.snd || []).filter(it => { if (!all && !it.stop) return true; clearTimeout(it.tm); if (it.h) it.h.stop(0.15); return false; }); }

    // ---------------- prologue screens: "scroller" (text crawling up and away) or "typer" (letter by letter, page by page)
    proText(sc) { return String(this.tags.interpolate(this.tags.get(sc.id + ".text") || "")).replace(/\r/g, ""); }
    proDraw(sc, preview) {
      const T = this.tags, g = k => T.get(sc.id + "." + k), box = div("rt-pro"), st = g("style") || "scroller", font = fontFamily(g("font")), wd = +g("width") || 820;
      const style = el => { el.style.fontFamily = font; el.style.fontSize = (+g("size") || 40) + "px"; el.style.color = g("color") || "#ffd84a"; el.style.textAlign = g("align") || "center"; };
      const pro = { sc, st, t: 0, last: performance.now(), preview: !!preview, box, phase: g("intro") ? "intro" : "main", pi: 0, pt: 0, chars: 0, done: 0 };
      const intro = div("intro"); intro.textContent = this.tags.interpolate(g("intro") || ""); intro.style.fontFamily = font; intro.style.fontSize = Math.round((+g("size") || 40) * 0.8) + "px"; intro.style.color = g("introColor") || "#4fc3ff"; box.append(intro); pro.intro = intro;
      if (st === "scroller") { const persp = div("persp"), plane = div("plane"), crawl = div("crawl"); plane.style.width = wd + "px"; plane.style.marginLeft = -wd / 2 + "px"; plane.style.transform = "rotateX(" + (+g("tilt") || 0) + "deg)"; crawl.textContent = this.proText(sc); style(crawl); plane.append(crawl); persp.append(plane); box.append(persp); pro.crawl = crawl; pro.persp = persp; if (!(+g("tilt") > 0)) { persp.style.webkitMaskImage = "none"; persp.style.maskImage = "none"; } }
      else { const ty = div("typer"); ty.style.width = wd + "px"; style(ty); const pl = g("place") || "bottom"; if (pl === "top") ty.style.top = "70px"; else if (pl === "middle") { ty.style.top = "50%"; ty.style.transform = "translate(-50%,-50%)"; } else ty.style.bottom = "70px"; box.append(ty); pro.ty = ty; pro.pages = this.proText(sc).split(/\n[ \t]*\n/).map(q => q.replace(/^\n+|\n+$/g, "")).filter(q => q.length); }
      return pro;
    }
    proStart(preview) { const sc = this.screen(); if (!sc || sc.kind !== "prologue") return; this.layer.replaceChildren(); const bgi = this.tags.get(sc.id + ".bgImage"); if (bgi && this.urls[bgi]) { const im = new Image(); im.src = this.urls[bgi]; im.style.cssText = "position:absolute;inset:0;width:100%;height:100%;object-fit:cover"; this.layer.append(im); }
      this.pro = this.proDraw(sc, preview); this.layer.append(this.pro.box); this.proStep(0); }
    proStep(dt) {
      const pr = this.pro, sc = pr.sc, g = k => this.tags.get(sc.id + "." + k); pr.t += dt;
      if (pr.phase === "intro") { pr.pt += dt; const a = pr.pt < 1 ? pr.pt : pr.pt < 3.5 ? 1 : Math.max(0, 1 - (pr.pt - 3.5)); pr.intro.style.opacity = a; if (pr.pt >= 4.6) { pr.phase = "main"; pr.pt = 0; } return; }
      if (pr.phase === "end") { pr.pt += dt; if (pr.pt >= (+g("endHold") || 0)) this.proEnd(); return; }
      if (pr.st === "scroller") { pr.pt += dt; const h = pr.crawl.offsetHeight || 600, sp = +g("speed") || 45, y = H - pr.pt * sp; pr.crawl.style.top = y + "px"; if (y < -h - (+g("tilt") > 0 ? H * 1.5 : 0)) { pr.phase = "end"; pr.pt = 0; } return; }
      const page = pr.pages[pr.pi]; if (page === undefined) { pr.phase = "end"; pr.pt = 0; pr.ty.style.opacity = 0; return; }
      if (pr.hold !== undefined) { pr.hold -= dt; if (pr.hold <= 0.4) pr.ty.style.opacity = pr.pi === pr.pages.length - 1 ? 1 : 0; if (pr.hold <= 0) { pr.hold = undefined; pr.pi++; pr.chars = 0; pr.acc = 0; pr.ty.style.opacity = 1; if (pr.pi >= pr.pages.length) { pr.phase = "end"; pr.pt = 0; } } return; }
      pr.acc = (pr.acc || 0) + dt * (+g("cps") || 16); const was = pr.chars; pr.chars = Math.min(page.length, Math.floor(pr.acc)); this.proShow(page, pr.chars);
      if (pr.chars > was) { const every = Math.max(1, +g("typeEvery") || 2), s = g("typeSound"); for (let i = was; i < pr.chars; i++) if (!/\s/.test(page[i]) && (pr.typed = (pr.typed || 0) + 1) % every === 0 && s && !pr.preview) { this.sfx(s); break; } }
      if (pr.chars >= page.length) pr.hold = Math.max(0.45, +g("pageHold") || 0) + 0.4;
    }
    proShow(page, n, ty) { ty = ty || this.pro.ty; const a = document.createTextNode(page.slice(0, n)), b = document.createElement("i"); b.textContent = page.slice(n); ty.replaceChildren(a, b); }
    proEnd() { const pr = this.pro; if (!pr) return; this.pro = null; if (pr.preview) { this.rebuild(); if (this.o.onSplashEnd) this.o.onSplashEnd(); return; } this.thenDo(this.tags.get(pr.sc.id + ".then"), this.tags.get(pr.sc.id + ".next")); }
    previewPrologue() { if (this.mode === "edit" && this.screen() && this.screen().kind === "prologue") this.proStart(true); }
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
    static rgba(hex, a) { const h = String(hex || "#000").replace("#", ""), f = h.length === 3 ? h.split("").map(c => c + c).join("") : h.padEnd(6, "0"), n = parseInt(f.slice(0, 6), 16) || 0; return "rgba(" + (n >> 16 & 255) + "," + (n >> 8 & 255) + "," + (n & 255) + "," + clamp(isNaN(a) ? 0.55 : a, 0, 1) + ")"; }
    // "Go back": the screen you came from (pause menus: nothing left = resume the game)
    back() { const id = this.hist.pop(); if (id && this.project.screens.some(s => s.id === id)) this.show(id, true); else if (this.o.onResume) this.o.onResume(); }
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
      if (this.pro && !this.paused) { const pr = this.pro, dt = Math.min(0.1, (now - pr.last) / 1000); pr.last = now; this.proStep(dt); }
      this.fps.n++; if (now - this.fps.t > 500) { this.fps.v = Math.round(this.fps.n * 1000 / (now - this.fps.t)); this.fps.n = 0; this.fps.t = now; if (this.fpsEl.style.display !== "none") this.fpsEl.textContent = this.fps.v + " FPS"; }
    }

    // ---------------- audio
    vol(kind) { const T = this.tags; return clamp((T.get("audio.master") || 0) * (T.get("audio." + kind) || 0), 0, 1); }
    // looping sounds started by the "Loop a sound" node (one loop per sound)
    loopSfx(id, mult) { if (this.mode === "edit" || !id || !this.urls[id]) return; this.loops = this.loops || {}; const vol = clamp(this.vol("sfx") * (mult === undefined || isNaN(mult) ? 1 : mult), 0, 1); if (this.loops[id] && this.loops[id].playing) { this.loops[id].setVolume(vol); return; } this.loops[id] = PRIM.Audio.play(this.urls[id], vol, true); }
    stopLoops(id) { const L = this.loops || {}; for (const k of Object.keys(L)) if (!id || k === id) { L[k].stop(0.03); delete L[k]; } }
    sfx(id, mult) { if (this.mode === "edit" || !id || !this.urls[id]) return; PRIM.Audio.play(this.urls[id], clamp(this.vol("sfx") * (mult === undefined || isNaN(mult) ? 1 : mult), 0, 1), false); }
    setMusic(id) {
      if (this.mode === "edit") return; if (id === this.musicId) return; this.stopMusic(); this.musicId = id; if (!id || !this.urls[id]) return;
      this.music = PRIM.Audio.play(this.urls[id], this.vol("music"), true);
    }
    stopMusic() { if (this.music) { this.music.stop(0.25); this.music = null; } this.musicId = null; }
    applyAudio() { if (this.music) this.music.setVolume(this.vol("music")); const sc = this.screen(); if (sc && this.vids && this.mode !== "edit") for (const n of sc.nodes) { const v = this.vids[n.id]; if (v) v.volume = clamp(this.vol("music") * (+this.P(sc, n, "volume") || 0), 0, 1); } }

    // ---------------- input
    pt(e) { const r = this.stage.getBoundingClientRect(); return [(e.clientX - r.left) / this.scale, (e.clientY - r.top) / this.scale]; }
    box(n) { const el = this.els[n.id]; if (!el) return null; const sc = this.screen(), x = this.px(sc, n), y = this.py(sc, n), w = Math.max(10, el.offsetWidth), h = Math.max(10, el.offsetHeight); return { x0: x - w / 2, y0: y - h / 2, w, h }; }
    // which tags a corner handle changes: width / height (images, bars, textures, sliders, toggles) or text size (titles, text buttons)
    sizeProps(n) {
      const sc = this.screen(); if (!sc || sc.kind === "splash") return [];
      if ((n.type === "image" || n.type === "video") && this.P(this.screen(), n, "stretch")) return []; if (n.type === "video") return ["w"]; if (n.type === "image" || n.type === "bar") return ["w", "h"]; if (n.type === "slider" || n.type === "toggle") return ["w"];
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
      if (this.paused) return; this.stage.setPointerCapture(e.pointerId); const [sx, sy] = this.pt(e);
      if (this.game) { this.game = false; this.rebuild(); return; }
      if (this.splash) { this.skipSplash(); return; }
      if (this.pro && this.pro.preview && this.mode === "edit") { this.pro = null; this.rebuild(); return; }
      if (this.mode !== "edit" && (this.pro || this.vids) && this.skipMedia()) return;
      if (this.mode === "edit" && e.target && e.target.classList && e.target.classList.contains("rt-handle")) { const sc = this.screen(), n = sc.nodes.find(x => x.id === this.selId), el = n && this.els[n.id], b = n && this.box(n); if (n && el && b) { const hx = +e.target.dataset.cx, hy = +e.target.dataset.cy; this.drag = { id: n.id, rs: true, sx: hx, sy: hy, ax: hx > 0 ? b.x0 : b.x0 + b.w, ay: hy > 0 ? b.y0 : b.y0 + b.h, cy0: b.y0 + b.h / 2, w0: el.offsetWidth, h0: el.offsetHeight }; } return; }
      if (this.mode === "edit") {
        const sc = this.screen(), n = sc.kind === "splash" ? (sc.nodes.find(x => x.id === this.selId) || null) : this.hit(sx, sy, true);
        if (this.o.onSelect) this.o.onSelect(n ? n.id : null);
        if (n && sc.kind !== "splash") { this.selId = n.id; this.drag = { id: n.id, dx: sx - this.px(sc, n), dy: sy - this.py(sc, n) }; this.rebuild(); }
        return;
      }
      PRIM.Audio.resume();
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
        else if (a === "start" || a === "continue" || a === "load") {   // new game in a slot, the latest save, or one slot
          const S = PRIM.Save, pid = this.project.id, slot = Math.max(1, Math.min(3, Math.round(+P("slot") || 1))), want = a === "continue" ? (S ? S.latest(pid) : 0) : a === "load" ? slot : 0, data = want && S ? S.read(pid, want) : null;
          if (a === "load" && !data) { this.sfx(P("clickSound")); return; }
          const opt = data ? { slot: want, load: data } : { slot: a === "start" ? slot : (S && S.latest(pid)) || 1 };
          if (T.has("save.slot")) T.set("save.slot", opt.slot);
          if (this.o.onStartGame) this.o.onStartGame(this, opt); else { this.game = true; this.rebuild(); } } else if (a === "quit") { if (this.o.onQuit) this.o.onQuit(); else if (this.o.onExit) this.o.onExit(); }
        else if (a === "back") this.back();
        else if (a === "resume") { if (this.o.onResume) this.o.onResume(); }
        else if (a === "restart") { if (this.o.onRestart) this.o.onRestart(); }
        else if (a === "menu") { if (this.o.onMenu) this.o.onMenu(); else { const m = this.project.screens.find(s => s.id === "main") || this.project.screens.find(s => s.kind === "menu"); if (m) this.show(m.id); } } }
      else if (n.type === "toggle") { const b = P("bind"), sp = T.spec(b); if (sp && sp.type === "bool") T.set(b, !T.get(b)); }
    }
    key(e) {
      if (this.mode === "edit" || this.paused) return;
      if (this.splash) { this.skipSplash(); return; }
      if ((this.pro || this.vids) && this.skipMedia()) return;
      if (e.key === "Escape") { if (this.o.onEscape) { this.o.onEscape(); return; } if (this.game) { this.game = false; this.rebuild(); } else if (this.mode === "play" && this.o.onExit && !this.o.noEscExit) this.o.onExit(); return; }
      if (this.sh && !/INPUT|SELECT|TEXTAREA/.test((e.target && e.target.tagName) || "")) { const sc = this.screen(); if (sc) for (const n of sc.nodes) this.script(sc, n, "onKey", e.key === " " ? "space" : e.key === "Control" ? "ctrl" : e.key); }
    }
  };
})();
