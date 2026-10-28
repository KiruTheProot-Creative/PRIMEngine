/* PRIMEngine HUD editor (rebuilt from scratch).
   Shows a HUD screen exactly as the game draws it (it uses PRIM.HUD, the same renderer), inside a view you can pan and zoom.
   The dashed frame is what the player sees while standing still; nothing is cut at that frame, so elements can sit
   outside it. When "Hud Movement" is on, a dotted frame shows how far the HUD can sway.
   Mouse: click to select, drag to move (Shift = snap to 10), drag an orange corner to resize (Shift = free proportions),
   wheel = zoom, middle or right button drag (or Space + drag) = pan. Keys: arrows nudge the selection (Shift = 10), F = fit.
   Options: { host, project, tags, urls, screenId, selId, onSelect(id|null), onChange() } */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), W = 1280, H = 720, clamp = (v, a, b) => Math.max(a, Math.min(b, v));
  const mk = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text) e.textContent = text; return e; };

  if (!document.getElementById("hed-style")) {
    const s = mk("style"); s.id = "hed-style";
    s.textContent = `
.hed{position:relative;width:100%;height:100%;overflow:hidden;outline:none;cursor:default;touch-action:none;
  background:radial-gradient(ellipse at 50% 115%,#26384c 0%,#0f1822 55%,#070b10 100%)}
.hed-grid{position:absolute;inset:0;pointer-events:none;opacity:.35;background-image:linear-gradient(rgba(125,243,255,.08) 1px,transparent 1px),linear-gradient(90deg,rgba(125,243,255,.08) 1px,transparent 1px)}
.hed-view{position:absolute;overflow:visible;pointer-events:none}
.hed-frame{position:absolute;border:2px dashed rgba(125,243,255,.85);box-shadow:0 0 0 1px rgba(0,0,0,.55);pointer-events:none;box-sizing:border-box}
.hed-sway{position:absolute;border:1px dotted rgba(255,214,120,.7);pointer-events:none;box-sizing:border-box}
.hed-lab{position:absolute;font:11px "Share Tech Mono",ui-monospace,monospace;color:rgba(125,243,255,.9);text-shadow:0 1px 2px #000;pointer-events:none;white-space:nowrap}
.hed-ov{position:absolute;inset:0;pointer-events:none}
.hed-box{position:absolute;outline:2px dashed #ffb53a;outline-offset:3px;pointer-events:none}
.hed-hov{position:absolute;outline:1px solid rgba(255,255,255,.55);outline-offset:3px;pointer-events:none}
.hed-h{position:absolute;width:14px;height:14px;margin:-7px 0 0 -7px;background:#ffb53a;border:2px solid #000;border-radius:3px;pointer-events:auto;box-sizing:border-box}
.hed-tools{position:absolute;right:8px;top:8px;display:flex;gap:6px;align-items:center;font:12px "Share Tech Mono",ui-monospace,monospace;color:#cfe;pointer-events:auto}
.hed-tools button{font:inherit;color:inherit;background:#1b2733;border:1px solid #3a4b5c;border-radius:4px;padding:3px 9px;cursor:pointer}
.hed-tools button:hover{background:#263646}
.hed-tools span{background:#0008;padding:3px 7px;border-radius:4px}`;
    document.head.append(s);
  }

  PRIM.HudEditor = class HudEditor {
    constructor(o) {
      this.o = o; this.host = o.host; this.project = o.project; this.tags = o.tags; this._urls = o.urls || {}; this.screenId = o.screenId; this.selId = o.selId || null;
      this.z = 0.5; this.px = 0; this.py = 0; this.fitted = false; this.drag = null; this.hovId = null; this.dead = false; this.paused = false;
      this.wrap = mk("div", "hed"); this.wrap.tabIndex = 0; this.grid = mk("div", "hed-grid"); this.swayEl = mk("div", "hed-sway"); this.view = mk("div", "hed-view"); this.frame = mk("div", "hed-frame");
      this.lab = mk("div", "hed-lab", "1280 × 720 · standing still"); this.ov = mk("div", "hed-ov"); this.box = mk("div", "hed-box"); this.hov = mk("div", "hed-hov");
      this.tools = mk("div", "hed-tools"); this.zoomLab = mk("span"); const fit = mk("button", "", "Fit"); fit.title = "Fit the view (F)"; fit.onclick = () => this.fit(); const one = mk("button", "", "100%"); one.title = "Actual size"; one.onclick = () => this.zoomAt(this.wrap.clientWidth / 2, this.wrap.clientHeight / 2, 1 / this.z);
      this.tools.append(this.zoomLab, one, fit);
      this.handles = [[-1, -1], [1, -1], [-1, 1], [1, 1]].map(([cx, cy]) => { const e = mk("div", "hed-h"); e.dataset.cx = cx; e.dataset.cy = cy; e.style.cursor = cx * cy > 0 ? "nwse-resize" : "nesw-resize"; e.title = "Drag to resize (Shift = free proportions)"; return e; });
      this.ov.append(this.hov, this.box, ...this.handles); this.wrap.append(this.grid, this.swayEl, this.view, this.frame, this.lab, this.ov, this.tools);
      this.host.replaceChildren(this.wrap);
      this.hud = new PRIM.HUD({ host: this.view, project: this.project, tags: this.tags, urls: this._urls, screenId: this.screenId, edit: true, onUpdate: () => this.overlay() });
      this.h = { down: e => this.down(e), move: e => this.move(e), up: e => this.up(e), wheel: e => this.wheel(e), key: e => this.key(e), keyup: e => { if (e.key === " ") this.space = false; } };
      this.wrap.addEventListener("pointerdown", this.h.down); this.wrap.addEventListener("pointermove", this.h.move); this.wrap.addEventListener("pointerup", this.h.up); this.wrap.addEventListener("pointercancel", this.h.up);
      this.wrap.addEventListener("wheel", this.h.wheel, { passive: false }); this.wrap.addEventListener("keydown", this.h.key); this.wrap.addEventListener("keyup", this.h.keyup); this.wrap.addEventListener("contextmenu", e => e.preventDefault());
      this.ro = new ResizeObserver(() => { if (!this.fitted) this.fit(); else this.layout(); }); this.ro.observe(this.wrap);
      this.unsub = this.tags.on(() => this.layoutSoon());
      this.fit();
    }
    destroy() { this.dead = true; this.ro.disconnect(); this.unsub(); this.hud.destroy(); this.host.replaceChildren(); }
    // the Studio talks to the editor through these
    get urls() { return this._urls; } set urls(u) { this._urls = u || {}; if (this.hud) this.hud.urls = this._urls; }
    get els() { return this.hud.els; }
    rebuild() { this.hud.urls = this._urls; this.hud.build(); this.layout(); }
    previewSplash() {} stopMusic() {}
    screen() { return this.project.screens.find(s => s.id === this.screenId); }
    P(n, k) { return this.tags.get(this.screenId + "." + n.id + "." + k); }
    // ---- view: z = screen pixels per HUD unit, px / py = pan in screen pixels
    fit() { const w = this.wrap.clientWidth, h = this.wrap.clientHeight; if (!w || !h) return; const [mx, my] = this.hud.swayMax(); this.z = Math.max(0.05, Math.min((w - 60) / (W + 2 * mx + 160), (h - 60) / (H + 2 * my + 120))); this.px = 0; this.py = 0; this.fitted = true; this.layout(); }
    zoomAt(sx, sy, f) { const z = clamp(this.z * f, 0.05, 6), w = this.wrap.clientWidth, h = this.wrap.clientHeight, ox = w / 2 + this.px, oy = h / 2 + this.py; this.px = sx - (sx - ox) * z / this.z - w / 2; this.py = sy - (sy - oy) * z / this.z - h / 2; this.z = z; this.layout(); }
    layoutSoon() { if (this.ls || this.dead) return; this.ls = requestAnimationFrame(() => { this.ls = 0; if (!this.dead) this.layout(); }); }
    layout() {
      const w = this.wrap.clientWidth, h = this.wrap.clientHeight, z = this.z, cx = w / 2 + this.px, cy = h / 2 + this.py, vw = W * z, vh = H * z, [mx, my] = this.hud.swayMax();
      Object.assign(this.view.style, { left: cx - vw / 2 + "px", top: cy - vh / 2 + "px", width: vw + "px", height: vh + "px" });
      Object.assign(this.frame.style, { left: cx - vw / 2 + "px", top: cy - vh / 2 + "px", width: vw + "px", height: vh + "px" });
      this.lab.style.left = cx - vw / 2 + 2 + "px"; this.lab.style.top = cy - vh / 2 - 16 + "px";
      this.swayEl.style.display = mx || my ? "block" : "none"; Object.assign(this.swayEl.style, { left: cx - vw / 2 - mx + "px", top: cy - vh / 2 - my + "px", width: vw + 2 * mx + "px", height: vh + 2 * my + "px" });
      const g = 32 * z; this.grid.style.backgroundSize = g + "px " + g + "px"; this.grid.style.backgroundPosition = (cx % g) + "px " + (cy % g) + "px";
      this.zoomLab.textContent = Math.round(z * 100) + "%"; this.hud.update();
    }
    // ---- where things are (wrap pixels)
    vo() { return [parseFloat(this.view.style.left) || 0, parseFloat(this.view.style.top) || 0]; }
    rect(id) { const r = this.hud.rectOf(id); if (!r) return null; const [ox, oy] = this.vo(); return { x0: r.x0 + ox, y0: r.y0 + oy, w: Math.max(6, r.w), h: Math.max(6, r.h) }; }
    rel(e) { const r = this.wrap.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; }
    stretch(n) { return n.type === "image" && !!this.P(n, "stretch"); }
    hit(sx, sy) {
      const sc = this.screen(); if (!sc) return null; const inside = n => { const r = this.rect(n.id); return r && sx >= r.x0 && sy >= r.y0 && sx <= r.x0 + r.w && sy <= r.y0 + r.h; };
      for (let i = sc.nodes.length - 1; i >= 0; i--) { const n = sc.nodes[i]; if (!this.stretch(n) && inside(n)) return n; }   // full-screen images last, so they never hide the rest
      for (let i = sc.nodes.length - 1; i >= 0; i--) { const n = sc.nodes[i]; if (this.stretch(n) && inside(n)) return n; }
      return null;
    }
    // where a dropped file lands, as HUD x / y
    dropPoint(e) { const [sx, sy] = this.rel(e), w = this.wrap.clientWidth, h = this.wrap.clientHeight; return { x: Math.round((sx - (w / 2 + this.px)) / this.z), y: Math.round(((h / 2 + this.py) - sy) / this.z) }; }
    // which values a corner handle changes
    sizeProps(n) { if (this.stretch(n)) return []; if (n.type === "image" || n.type === "bar") return ["w", "h"]; if (n.type === "slider" || n.type === "toggle") return ["w"]; if (n.type === "button") return this.P(n, "mode") === "texture" ? ["w", "h"] : ["size"]; if (n.type === "title") return ["size"]; return []; }
    overlay() {
      const sc = this.screen(), n = sc && this.selId && sc.nodes.find(x => x.id === this.selId), r = n && this.rect(n.id), place = (e, b) => { if (!b) { e.style.display = "none"; return; } Object.assign(e.style, { display: "block", left: b.x0 + "px", top: b.y0 + "px", width: b.w + "px", height: b.h + "px" }); };
      place(this.box, r); const hn = this.hovId && this.hovId !== this.selId && sc && sc.nodes.find(x => x.id === this.hovId); place(this.hov, hn ? this.rect(hn.id) : null);
      const show = r && this.sizeProps(n).length; for (const e of this.handles) { e.style.display = show ? "block" : "none"; if (show) { e.style.left = r.x0 + (+e.dataset.cx > 0 ? r.w : 0) + "px"; e.style.top = r.y0 + (+e.dataset.cy > 0 ? r.h : 0) + "px"; } }
    }
    select(id) { this.selId = id; this.overlay(); if (this.o.onSelect) this.o.onSelect(id); }
    // ---- mouse
    down(e) {
      this.wrap.focus(); const [sx, sy] = this.rel(e); this.wrap.setPointerCapture(e.pointerId);
      if (e.button === 1 || e.button === 2 || (e.button === 0 && this.space)) { this.drag = { pan: true, x: e.clientX, y: e.clientY, px: this.px, py: this.py }; this.wrap.style.cursor = "grabbing"; return; }
      if (e.button !== 0) return; const sc = this.screen(), T = this.tags;
      if (e.target.classList.contains("hed-h")) { const n = sc.nodes.find(x => x.id === this.selId), r = n && this.rect(n.id); if (!n || !r) return; const hx = +e.target.dataset.cx, hy = +e.target.dataset.cy;
        this.drag = { rs: true, n, hx, hy, ax: hx > 0 ? r.x0 : r.x0 + r.w, ay: hy > 0 ? r.y0 : r.y0 + r.h, w0: r.w, h0: r.h, size0: +this.P(n, "size") || 32 }; return; }
      const n = this.hit(sx, sy); if (!n) { this.select(null); return; }
      if (n.id !== this.selId) this.select(n.id);
      if (!this.stretch(n)) this.drag = { mv: true, n, sx, sy, x0: +this.P(n, "x") || 0, y0: +this.P(n, "y") || 0, moved: false };
    }
    move(e) {
      const [sx, sy] = this.rel(e), d = this.drag, T = this.tags, pre = n => this.screenId + "." + n.id + ".";
      if (!d) { const n = this.hit(sx, sy), id = n ? n.id : null; if (id !== this.hovId) { this.hovId = id; this.overlay(); } this.wrap.style.cursor = this.space ? "grab" : n ? "move" : "default"; return; }
      if (d.pan) { this.px = d.px + e.clientX - d.x; this.py = d.py + e.clientY - d.y; this.layout(); return; }
      if (d.mv) { const snap = e.shiftKey ? 10 : 1, x = clamp(Math.round((d.x0 + (sx - d.sx) / this.z) / snap) * snap, -4000, 4000), y = clamp(Math.round((d.y0 - (sy - d.sy) / this.z) / snap) * snap, -4000, 4000); d.moved = true; T.setMany({ [pre(d.n) + "x"]: x, [pre(d.n) + "y"]: y }); return; }
      if (d.rs) {
        const n = d.n, props = this.sizeProps(n), set = {}, z = this.z, wrapW = this.wrap.clientWidth, wrapH = this.wrap.clientHeight, cx0 = wrapW / 2 + this.px, cy0 = wrapH / 2 + this.py;
        let w = Math.max(8, Math.abs(sx - d.ax)), h = Math.max(4, Math.abs(sy - d.ay));
        if (props[0] === "size") { const k = h / d.h0; set[pre(n) + "size"] = clamp(Math.round(d.size0 * k), 6, 400); h = d.h0 * k; w = d.w0 * k; }
        else { if (props.length > 1 && (n.type === "image" || n.type === "button") && !e.shiftKey) h = w * d.h0 / d.w0; set[pre(n) + "w"] = Math.round(w / z); if (props.length > 1) set[pre(n) + "h"] = Math.round(h / z); else h = d.h0; }
        const ccx = d.ax + d.hx * w / 2, ccy = d.ay + d.hy * h / 2; set[pre(n) + "x"] = Math.round((ccx - cx0) / z); set[pre(n) + "y"] = Math.round((cy0 - ccy) / z);
        d.moved = true; T.setMany(set);
      }
    }
    up() { const d = this.drag; this.drag = null; this.wrap.style.cursor = ""; if (d && (d.mv || d.rs) && d.moved && this.o.onChange) this.o.onChange(); }
    wheel(e) { e.preventDefault(); const [sx, sy] = this.rel(e); this.zoomAt(sx, sy, e.deltaY < 0 ? 1.15 : 1 / 1.15); }
    key(e) {
      if (/INPUT|SELECT|TEXTAREA/.test((e.target && e.target.tagName) || "") && e.target !== this.wrap) return;
      if (e.key === " ") { this.space = true; e.preventDefault(); return; }
      if (e.key === "f" || e.key === "F") { this.fit(); return; }
      const dir = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, 1], ArrowDown: [0, -1] }[e.key]; if (!dir || !this.selId) return;
      const sc = this.screen(), n = sc && sc.nodes.find(x => x.id === this.selId); if (!n || this.stretch(n)) return; e.preventDefault(); const st = e.shiftKey ? 10 : 1, pre = this.screenId + "." + n.id + ".";
      this.tags.setMany({ [pre + "x"]: (+this.P(n, "x") || 0) + dir[0] * st, [pre + "y"]: (+this.P(n, "y") || 0) + dir[1] * st }); if (this.o.onChange) this.o.onChange();
    }
  };
})();
