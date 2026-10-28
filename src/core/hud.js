/* PRIMEngine HUD renderer (rebuilt from scratch).
   Draws one HUD screen over the game, and is also what the HUD editor shows, so editing and playing look the same.
   - Nothing in here clips. Elements hang off a point in the middle of the host; only the window edge cuts them.
   - Positions: x / y from the middle of a 1280 x 720 view, y up. The whole layout scales with the host
     (scale = the largest that fits 1280 x 720 into the host), wider or taller windows simply show more room around it.
   - "Stretch to screen" images are sized to the host itself (not to 1280 x 720), plus the furthest the HUD can sway,
     so a full-screen visor never shows an edge, whatever the window shape and however far the HUD swings.
   - Sway: sway(x, y) moves everything by x, y screen pixels.
   Options: { host, project, tags, urls, screenId, edit (no scripts / sounds), onUpdate(hud) } */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), W = 1280, H = 720, clamp = (v, a, b) => Math.max(a, Math.min(b, v));
  const FONTS = { "builtin:display": '"Chakra Petch", system-ui, sans-serif', "builtin:sans": "system-ui, sans-serif", "builtin:serif": "Georgia, 'Times New Roman', serif", "builtin:mono": '"Share Tech Mono", ui-monospace, monospace' };
  const font = v => !v ? FONTS["builtin:display"] : FONTS[v] || '"prim-font-' + v + '", sans-serif';
  const mk = (tag, cls) => { const e = document.createElement(tag); if (cls) e.className = cls; return e; };
  const SWAY_X = 70, SWAY_Y = 50;   // the furthest the HUD sways at strength 1 (see Play3D.feel)

  if (!document.getElementById("hud-style")) {
    const s = mk("style"); s.id = "hud-style";
    s.textContent = `
.hud-root{position:absolute;inset:0;overflow:visible;pointer-events:none;user-select:none;-webkit-user-select:none}
.hud-sway{position:absolute;inset:0;overflow:visible}
.hud-mid{position:absolute;left:50%;top:50%;width:0;height:0;overflow:visible}
.hud-el{position:absolute;left:0;top:0;transform:translate(-50%,-50%);white-space:pre;line-height:1.1}
.hud-fill{position:absolute;left:0;top:0;transform:translate(-50%,-50%);max-width:none;max-height:none}
.hud-ph{display:grid;place-items:center;border:2px dashed rgba(125,243,255,.55);color:rgba(125,243,255,.85);font:14px "Share Tech Mono",monospace;background:rgba(125,243,255,.07);min-width:150px;min-height:70px;padding:8px;white-space:normal;text-align:center}
.hud-bar{overflow:hidden;border-radius:3px}
.hud-bar i{position:absolute;left:0;top:0;bottom:0}
.hud-bar b{position:absolute;inset:0;display:grid;place-items:center;font-weight:normal;letter-spacing:.1em;text-shadow:0 1px 2px #000a}
.hud-row{display:flex;justify-content:space-between;margin-bottom:6px;letter-spacing:.06em}
.hud-track{position:relative;height:8px;background:rgba(255,255,255,.14);border-radius:4px}
.hud-track i{position:absolute;left:0;top:0;bottom:0;border-radius:4px}
.hud-sw{position:relative;width:52px;height:26px;border-radius:13px;background:rgba(255,255,255,.16);flex:none}
.hud-sw i{position:absolute;top:3px;left:3px;width:20px;height:20px;border-radius:50%;background:#fff}
.hud-sw.on i{left:29px}`;
    document.head.append(s);
  }

  PRIM.HUD = class HUD {
    constructor(o) {
      this.o = o; this.host = o.host; this.project = o.project; this.tags = o.tags; this.urls = o.urls || {}; this.screenId = o.screenId; this.edit = !!o.edit;
      this.items = {}; this.order = []; this.sx = 0; this.sy = 0; this.dead = false; this.pend = 0;
      this.root = mk("div", "hud-root"); this.swayEl = mk("div", "hud-sway"); this.mid = mk("div", "hud-mid");
      this.swayEl.append(this.mid); this.root.append(this.swayEl); this.host.append(this.root);
      this.ro = new ResizeObserver(() => this.update()); this.ro.observe(this.host);
      this.unsub = this.tags.on(() => this.later());
      if (!this.edit && PRIM.Script) {
        this.sh = new PRIM.Script.Host({ tags: this.tags, sfx: (id, v) => this.sound(id, v, false), loop: (id, v) => this.sound(id, v, true), stopLoop: id => this.stopLoops(id), say: t => { if (o.say) o.say(t); } });
        this.kd = e => { if (/INPUT|SELECT|TEXTAREA/.test((e.target && e.target.tagName) || "")) return; const k = e.key === " " ? "space" : e.key === "Control" ? "ctrl" : e.key.length === 1 ? e.key.toLowerCase() : e.key.toLowerCase(); this.each(n => this.run(n, "onKey", k)); };
        addEventListener("keydown", this.kd);
      }
      this.build(); if (this.sh) this.each(n => this.run(n, "onStart"));
    }
    destroy() { this.dead = true; cancelAnimationFrame(this.pend); this.ro.disconnect(); this.unsub(); if (this.kd) removeEventListener("keydown", this.kd); if (this.sh) this.sh.stopAll(); this.stopLoops(""); this.root.remove(); }
    screen() { return this.project.screens.find(s => s.id === this.screenId); }
    each(f) { const sc = this.screen(); if (sc) for (const n of sc.nodes) f(n, sc); }
    run(n, type, arg) { const sc = this.screen(); if (this.sh && n.script) this.sh.trigger({ key: sc.id + "." + n.id, script: n.script }, type, arg); }
    later() { if (this.pend || this.dead) return; this.pend = requestAnimationFrame(() => { this.pend = 0; if (!this.dead) this.update(); }); }
    setScreen(id) { this.screenId = id; this.build(); }
    // ---- size of the view
    scale() { return Math.min(this.host.clientWidth / W, this.host.clientHeight / H) || 1; }
    swayMax() { const T = this.tags; if (!T.get("player.hudMove")) return [0, 0]; const a = +T.get("player.hudMoveAmount") || 1; return [SWAY_X * a, SWAY_Y * a]; }
    sway(x, y) { this.sx = x || 0; this.sy = y || 0; this.swayEl.style.transform = this.sx || this.sy ? "translate(" + this.sx.toFixed(1) + "px," + this.sy.toFixed(1) + "px)" : ""; }
    // ---- elements: built once, then only their values change
    build() {
      this.mid.replaceChildren(); this.items = {}; this.order = [];
      this.each(n => { this.items[n.id] = { n, el: null, kind: "" }; this.order.push(n.id); }); this.update();
    }
    get els() { const o = {}; for (const id in this.items) if (this.items[id].el) o[id] = this.items[id].el; return o; }
    P(n, k) { return this.tags.get(this.screenId + "." + n.id + "." + k); }
    S(n, k) { return this.tags.interpolate(this.P(n, k)); }
    update() {
      const sc = this.screen(); if (!sc) { this.mid.replaceChildren(); return; }
      if (sc.nodes.length !== this.order.length || sc.nodes.some((n, i) => n.id !== this.order[i] || this.items[n.id].n !== n)) return this.build();
      const s = this.scale(), [mx, my] = this.swayMax();
      const fw = this.host.clientWidth + 2 * mx + 4, fh = this.host.clientHeight + 2 * my + 4;
      for (const id of this.order) { const it = this.items[id]; this.draw(it, s, fw, fh); }
      // drawing order = the element list (Back / Front in the inspector)
      let prev = null; for (const id of this.order) { const el = this.items[id].el; if (!el) continue; if (el.previousSibling !== prev || el.parentNode !== this.mid) this.mid.insertBefore(el, prev ? prev.nextSibling : this.mid.firstChild); prev = el; }
      if (this.o.onUpdate) this.o.onUpdate(this);
    }
    draw(it, s, fw, fh) {
      const n = it.n, p = k => this.P(n, k), stretch = n.type === "image" && !!p("stretch"), kind = n.type + (stretch ? ":fill" : "") + (n.type === "button" ? ":" + p("mode") : "");
      if (it.kind !== kind || !it.el) { if (it.el) it.el.remove(); it.el = this.make(n, stretch); it.kind = kind; this.mid.append(it.el); }
      const el = it.el;
      if (stretch) { const u = this.urls[p("asset")]; if (u && el.dataset.u !== u) { el.src = u; el.dataset.u = u; } el.style.visibility = u ? "visible" : "hidden"; el.style.width = fw + "px"; el.style.height = fh + "px"; el.style.opacity = p("opacity"); return; }
      el.style.left = (+p("x") || 0) * s + "px"; el.style.top = -(+p("y") || 0) * s + "px"; el.style.transform = "translate(-50%,-50%) scale(" + s + ")";   // each element scales around its own middle
      const size = (e, w, h) => { e.style.width = w > 0 ? w + "px" : ""; e.style.height = h > 0 ? h + "px" : ""; };
      const text = e => { e.style.fontFamily = font(p("font")); e.style.fontSize = p("size") + "px"; e.style.color = p("color"); };
      if (n.type === "title") { el.textContent = this.S(n, "text"); text(el); el.style.opacity = p("opacity"); }
      else if (n.type === "image") { this.pic(el, p("asset"), +p("w"), +p("h")); el.style.opacity = p("opacity"); }
      else if (n.type === "bar") {
        const T = this.tags, b = p("bind"), sp = T.spec(b), mt = p("max"), ms = mt && T.spec(mt), v = sp ? +T.get(b) : 0, mxv = ms ? +T.get(mt) : sp ? sp.max : 1, mn = ms ? 0 : sp ? sp.min : 0, f = clamp((v - mn) / ((mxv - mn) || 1), 0, 1);
        size(el, +p("w") || 260, +p("h") || 22); el.style.background = p("bg"); el.style.opacity = p("opacity");
        const fl = el.firstChild, lb = el.lastChild; fl.style.width = f * 100 + "%"; fl.style.background = p("color"); lb.textContent = this.S(n, "label"); lb.style.fontFamily = font(p("font")); lb.style.fontSize = p("size") + "px"; lb.style.color = p("textColor");
      }
      else if (n.type === "button") {
        if (p("mode") === "texture") this.pic(el, p("texture"), +p("w"), +p("h"));
        else { el.textContent = this.S(n, "text"); text(el); el.style.padding = "10px 30px"; el.style.letterSpacing = ".08em"; }
      }
      else if (n.type === "slider" || n.type === "toggle") {
        const T = this.tags, b = p("bind"), sp = T.spec(b); el.style.width = (+p("w") || 420) + "px"; text(el); el.firstChild.textContent = this.S(n, "label");
        if (n.type === "slider") { const v = sp ? +T.get(b) : 0, f = sp ? clamp((v - sp.min) / ((sp.max - sp.min) || 1), 0, 1) : 0, fl = el.lastChild.firstChild; fl.style.width = f * 100 + "%"; fl.style.background = p("accent"); }
        else { const on = !!(sp && T.get(b)), sw = el.lastChild; sw.classList.toggle("on", on); sw.style.background = on ? p("accent") : ""; }
      }
    }
    make(n, stretch) {
      if (stretch) { const e = mk("img", "hud-fill"); e.draggable = false; e.alt = ""; return e; }
      if (n.type === "image") { const e = mk("div", "hud-el"); e.style.backgroundSize = "100% 100%"; return e; }
      if (n.type === "button") { const e = mk("div", "hud-el"); e.style.backgroundSize = "100% 100%"; return e; }
      if (n.type === "bar") { const e = mk("div", "hud-el hud-bar"); e.append(mk("i"), mk("b")); return e; }
      if (n.type === "slider") { const e = mk("div", "hud-el"); e.style.whiteSpace = "normal"; const row = mk("div", "hud-row"), tr = mk("div", "hud-track"); tr.append(mk("i")); e.append(row, tr); return e; }
      if (n.type === "toggle") { const e = mk("div", "hud-el"); e.style.cssText = "display:flex;justify-content:space-between;align-items:center"; const sw = mk("div", "hud-sw"); sw.append(mk("i")); e.append(mk("span"), sw); return e; }
      return mk("div", "hud-el");
    }
    // a picture drawn as the element's background: w / h of 0 = the image's own size (or keep its proportions when one is set)
    pic(el, id, w, h) {
      const u = id && this.urls[id];
      if (!u) { el.dataset.u = ""; el.style.backgroundImage = ""; el.classList.add("hud-ph"); el.textContent = "No image chosen"; el.style.width = w > 0 ? w + "px" : ""; el.style.height = h > 0 ? h + "px" : ""; el.style.display = this.edit ? "" : "none"; return; }
      el.style.display = ""; if (el.classList.contains("hud-ph")) { el.classList.remove("hud-ph"); el.textContent = ""; }
      if (el.dataset.u !== u) { el.dataset.u = u; el.dataset.nw = el.dataset.nh = ""; el.style.backgroundImage = 'url("' + u + '")'; const im = new Image(); im.onload = () => { if (el.dataset.u !== u) return; el.dataset.nw = im.naturalWidth; el.dataset.nh = im.naturalHeight; this.later(); }; im.src = u; }
      const nw = +el.dataset.nw || 0, nh = +el.dataset.nh || 0, r = nw && nh ? nh / nw : 1;
      el.style.width = (w > 0 ? w : h > 0 ? h / r : nw || 64) + "px"; el.style.height = (h > 0 ? h : w > 0 ? w * r : nh || 64) + "px";
    }
    // ---- sounds started by HUD scripts
    vol() { const T = this.tags; return clamp((+T.get("audio.master") || 0) * (+T.get("audio.sfx") || 0), 0, 1); }
    sound(id, mult, loop) {
      if (this.edit || !id || !this.urls[id]) return; const v = clamp(this.vol() * (mult === undefined || isNaN(mult) ? 1 : mult), 0, 1);
      if (!loop) { PRIM.Audio.play(this.urls[id], v, false); return; }
      this.loops = this.loops || {}; if (this.loops[id] && this.loops[id].playing) { this.loops[id].setVolume(v); return; } this.loops[id] = PRIM.Audio.play(this.urls[id], v, true);
    }
    stopLoops(id) { const L = this.loops || {}; for (const k of Object.keys(L)) if (!id || k === id) { L[k].stop(0.03); delete L[k]; } }
    // ---- where an element is on screen (for the editor): { x0, y0, w, h } in host pixels, or null
    rectOf(id) { const it = this.items[id]; if (!it || !it.el) return null; const r = it.el.getBoundingClientRect(), hr = this.host.getBoundingClientRect(); return { x0: r.left - hr.left, y0: r.top - hr.top, w: r.width, h: r.height }; }
  };
})();
