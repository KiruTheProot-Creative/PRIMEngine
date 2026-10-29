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
      const text = e => { e.style.fontFamily = font(p("font")); e.style.fontSize = p("size") + "px"; e.style.color = p("color"); if (PRIM.Runtime && PRIM.Runtime.textFxG) PRIM.Runtime.textFxG(e, p); };
      if (PRIM.Runtime && PRIM.Runtime.animG && !this.edit) { const sig = [p("anim"), p("animSpeed"), p("animAmount"), p("enter"), p("enterDelay"), p("enterTime")].join("|"); if (it.aSig !== sig) { it.aSig = sig; PRIM.Runtime.animG(el, p, (performance.now() - (this.t0 || (this.t0 = performance.now()))) / 1000); } }
      if (n.type === "title") { el.textContent = this.S(n, "text"); text(el); el.style.opacity = p("opacity"); }
      else if (n.type === "image") { this.pic(el, p("asset"), +p("w"), +p("h")); el.style.opacity = p("opacity"); }
      else if (n.type === "pips") PRIM.HUD.pips(el, this.tags, p);
      else if (n.type === "panel") { if (PRIM.Runtime && PRIM.Runtime.panelG) PRIM.Runtime.panelG(el, p, this.urls); el.style.opacity = p("opacity"); }
      else if (n.type === "minimap") { const w = +p("w") || 220, h = +p("h") || 220, k = Math.max(1, s * (window.devicePixelRatio || 1)); el.style.width = w + "px"; el.style.height = h + "px"; el.style.opacity = p("opacity");
        if (el.width !== Math.round(w * k) || el.height !== Math.round(h * k)) { el.width = Math.round(w * k); el.height = Math.round(h * k); } it.k = k; this.mapDraw(it, this.edit || !this.world ? this.demoWorld() : this.lastWorld); }
      else if (n.type === "bossbar") this.bossDraw(it, 0);
      else if (n.type === "keys") this.keysDraw(el, p);
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
      if (n.type === "pips") { const e = mk("div", "hud-el"); e.style.whiteSpace = "normal"; return e; }
      if (n.type === "minimap") return mk("canvas", "hud-el");
      if (n.type === "bossbar") { const e = mk("div", "hud-el"); e.style.whiteSpace = "normal"; e.style.textAlign = "center"; e.style.transition = "opacity .35s"; const nm = mk("div"), bar = mk("div"); bar.style.cssText = "position:relative;overflow:hidden;box-sizing:border-box"; const lag = mk("i"), fill = mk("i"); lag.style.cssText = fill.style.cssText = "position:absolute;left:0;top:0;bottom:0"; bar.append(lag, fill); e.append(nm, bar); return e; }
      if (n.type === "keys") { const e = mk("div", "hud-el"); e.style.whiteSpace = "normal"; return e; }
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
    wants() { return this.order.some(id => { const t = this.items[id].n.type; return t === "minimap" || t === "bossbar"; }); }
    // ---- game feed (Play3D calls tick(world) every frame): minimap, radar, boss bar trail
    tick(world, dt) { this.lastWorld = world; this.world = true; for (const id of this.order) { const it = this.items[id]; if (!it.el) continue; if (it.n.type === "minimap") this.mapDraw(it, world); else if (it.n.type === "bossbar") this.bossDraw(it, dt || 0); } }
    // in the editor: the first map around its player start
    demoWorld() { const pr = this.project, m = (pr.maps || [])[0], T = this.tags; if (!m) return null; const items = [], doors = [];
      for (const th of m.things || []) { const ty = T.get(th.prop + ".type"); if (ty === "door") { const f = PRIM.Grid && PRIM.Grid.thingFrame ? PRIM.Grid.thingFrame(m, T, th) : null; if (f) doors.push(this.doorSeg(f, T.get(th.prop + ".lockKey") ? T.get(T.get(th.prop + ".lockKey") + ".keyColor") : null, false)); continue; }
        const k = ty === "enemy" || ty === "boss" ? ty : ty === "key" ? "key" : ty === "pickup" ? "item" : ty === "exit" ? "exit" : ty === "npc" ? "npc" : null; if (k) items.push({ id: th.id, x: th.x, y: th.y, k, c: ty === "key" ? T.get(th.prop + ".keyColor") : null, L: th.layer || 0 }); }
      return { map: m, L: +T.get(m.id + ".startLayer") || 0, x: +T.get(m.id + ".startX") || 0, y: +T.get(m.id + ".startY") || 0, yaw: (+T.get(m.id + ".startAngle") || 0) * Math.PI / 180, t: 0.3, items, doors, demo: true }; }
    doorSeg(f, c, open) { const w = (f.w || 128) / 2; return { x0: f.cx - f.ux * w, y0: f.cy - f.uy * w, x1: f.cx + f.ux * w, y1: f.cy + f.uy * w, c, open }; }
    // the story you are on, drawn once per map / story into a picture (8 px per cell), then moved and turned every frame
    mapBase(it, w, p) { const m = w.map, L = w.L, G = PRIM.Grid, layer = m.layers[L], rv = !!p("reveal") && !w.demo, seen = rv ? this.seenOf(m.id, L) : null, key = [m.id, L, layer ? Object.keys(layer.cells).length : 0, p("floor"), p("wall"), rv ? seen.size : -1, w.demo ? JSON.stringify(layer && layer.cells).length : 0].join("|");
      if (it.base && it.baseKey === key) return it.base; it.baseKey = key; if (!layer) return (it.base = null);
      let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9; for (const k in layer.cells) { const [x, y] = k.split(",").map(Number); x0 = Math.min(x0, x); y0 = Math.min(y0, y); x1 = Math.max(x1, x); y1 = Math.max(y1, y); } if (x0 > x1) return (it.base = null);
      const PX = 8, cv = it.base && it.base.cv || document.createElement("canvas"); cv.width = (x1 - x0 + 3) * PX; cv.height = (y1 - y0 + 3) * PX; const c = cv.getContext("2d"), ox = x0 - 1, oy = y0 - 1; c.clearRect(0, 0, cv.width, cv.height);
      const solid = (x, y) => { const q = G.cell(m, L, x, y); return !q || G.isWall(q) || (q.f === undefined && !q.sh); }, vis = (x, y) => !seen || seen.has(x + "," + y);
      for (const k in layer.cells) { const q = layer.cells[k], [x, y] = k.split(",").map(Number); if (!vis(x, y)) continue; const X = (x - ox) * PX, Y = (y - oy) * PX;
        if (G.isWall(q)) { c.fillStyle = p("wall"); c.globalAlpha = 0.22; c.fillRect(X, Y, PX, PX); c.globalAlpha = 1; continue; }
        if (q.f === undefined && !q.sh) continue; c.fillStyle = p("floor"); c.globalAlpha = q.sh ? 0.75 : 1; c.fillRect(X, Y, PX, PX); c.globalAlpha = 1;
        if (q.dg && q.dg.length > 2) { c.fillStyle = p("wall"); c.globalAlpha = 0.5; c.beginPath(); q.dg.forEach((pt, i) => c[i ? "lineTo" : "moveTo"](X + pt[0] * PX, Y + pt[1] * PX)); c.closePath(); c.fill(); c.globalAlpha = 1; } }
      c.strokeStyle = p("wall"); c.lineWidth = 1.5; c.beginPath();
      for (const k in layer.cells) { const q = layer.cells[k], [x, y] = k.split(",").map(Number); if (G.isWall(q) || (q.f === undefined && !q.sh) || !vis(x, y)) continue; const X = (x - ox) * PX, Y = (y - oy) * PX;
        if (solid(x, y - 1)) { c.moveTo(X, Y); c.lineTo(X + PX, Y); } if (solid(x, y + 1)) { c.moveTo(X, Y + PX); c.lineTo(X + PX, Y + PX); } if (solid(x - 1, y)) { c.moveTo(X, Y); c.lineTo(X, Y + PX); } if (solid(x + 1, y)) { c.moveTo(X + PX, Y); c.lineTo(X + PX, Y + PX); } }
      c.stroke(); return (it.base = { cv, ox, oy, PX }); }
    seenOf(id, L) { this.seen = this.seen || {}; return this.seen[id + "/" + L] || (this.seen[id + "/" + L] = new Set()); }
    mapDraw(it, w) {
      const el = it.el, c = el.getContext("2d"), n = it.n, p = k => this.P(n, k), W2 = el.width, H2 = el.height, k = it.k || 1; c.setTransform(1, 0, 0, 1, 0, 0); c.clearRect(0, 0, W2, H2); if (!w) return;
      if (p("reveal") && !w.demo) { const S = this.seenOf(w.map.id, w.L), r = Math.max(1, +p("revealRadius") || 4), cx = Math.floor(w.x / 128), cy = Math.floor(w.y / 128); for (let y = cy - r; y <= cy + r; y++) for (let x = cx - r; x <= cx + r; x++) if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) S.add(x + "," + y); }
      const round = p("shape") !== "square", cxp = W2 / 2, cyp = H2 / 2, R = Math.min(W2, H2) / 2;
      c.save(); c.beginPath(); if (round) c.arc(cxp, cyp, R - 1, 0, Math.PI * 2); else c.rect(1, 1, W2 - 2, H2 - 2); c.clip();
      c.fillStyle = p("bg"); c.fillRect(0, 0, W2, H2);
      const sc = W2 / (Math.max(3, +p("zoom") || 14) * 128), rot = p("turn") !== false ? -(w.yaw + Math.PI / 2) : 0, b = this.mapBase(it, w, p);
      c.translate(cxp, cyp); c.rotate(rot); c.scale(sc, sc); c.translate(-w.x, -w.y);
      if (b) { c.imageSmoothingEnabled = false; c.drawImage(b.cv, b.ox * 128, b.oy * 128, b.cv.width / b.PX * 128, b.cv.height / b.PX * 128); }
      const px = 1 / sc;   // one screen pixel in world units
      if (p("items") !== false) { for (const d of w.doors || []) { if (d.open) continue; c.strokeStyle = d.c || p("door"); c.lineWidth = 4 * k * px; c.beginPath(); c.moveTo(d.x0, d.y0); c.lineTo(d.x1, d.y1); c.stroke(); }
        for (const q of w.items || []) { if ((q.L || 0) !== w.L || q.k === "enemy" || q.k === "boss") continue; c.fillStyle = q.c || (q.k === "exit" ? "#4aff8a" : q.k === "npc" ? "#7aa8ff" : q.k === "save" ? "#7dffb0" : p("item")); const s2 = (q.k === "key" ? 5 : 3.5) * k * px; if (q.k === "key" || q.k === "exit" || q.k === "save") c.fillRect(q.x - s2, q.y - s2, s2 * 2, s2 * 2); else { c.beginPath(); c.arc(q.x, q.y, s2, 0, Math.PI * 2); c.fill(); } } }
      // enemies: always, or as radar blips that fade until the next scan
      const mode = p("enemies") || "radar", radar = !!p("radar"), rate = Math.max(0.05, +p("radarRate") || 0.5), rng = Math.max(1, +p("radarRange") || 8) * 128, t = w.t || 0, st = p("radarStyle") || "sweep";
      it.blips = it.blips || {}; const ph = (t * rate) % 1, prev = it.lastPh === undefined ? ph : it.lastPh; it.lastPh = ph; const wrapped = ph < prev;
      for (const q of w.items || []) { if (q.k !== "enemy" && q.k !== "boss") continue; const dx = q.x - w.x, dy = q.y - w.y, d = Math.hypot(dx, dy);
        if ((q.L || 0) !== w.L && !w.demo) continue; if (mode === "always") { this.dot(c, q.x, q.y, (q.k === "boss" ? 7 : 4) * k * px, p("enemy"), 1); continue; } if (mode !== "radar" || !radar || d > rng) continue;
        let hit = false; if (st === "pulse") { const r0 = prev * rng, r1 = ph * rng; hit = wrapped ? d >= r0 || d <= r1 : d >= r0 && d <= r1; } else { const a = ((Math.atan2(dy, dx) / (Math.PI * 2)) % 1 + 1) % 1; hit = wrapped ? a >= prev || a <= ph : a >= prev && a <= ph; }
        if (hit || w.demo) it.blips[q.id] = { x: q.x, y: q.y, t, big: q.k === "boss" }; }
      for (const id in it.blips) { const bl = it.blips[id], a = 1 - (t - bl.t) * rate; if (a <= 0 && !w.demo) { delete it.blips[id]; continue; } this.dot(c, bl.x, bl.y, (bl.big ? 7 : 4) * k * px, p("enemy"), w.demo ? 1 : Math.max(0, a)); }
      if (radar) { c.save(); c.translate(w.x, w.y); const col = p("radarColor"); c.strokeStyle = col; c.globalAlpha = 0.35; c.lineWidth = 1 * k * px; c.beginPath(); c.arc(0, 0, rng, 0, Math.PI * 2); c.stroke();
        if (st === "pulse") { c.globalAlpha = 0.8 * (1 - ph); c.lineWidth = 2.5 * k * px; c.beginPath(); c.arc(0, 0, Math.max(1, ph * rng), 0, Math.PI * 2); c.stroke(); }
        else { const a = ph * Math.PI * 2; for (let i = 0; i < 12; i++) { c.globalAlpha = 0.28 * (1 - i / 12); c.fillStyle = col; c.beginPath(); c.moveTo(0, 0); c.arc(0, 0, rng, a - (i + 1) * 0.05, a - i * 0.05); c.closePath(); c.fill(); }
          c.globalAlpha = 0.9; c.lineWidth = 1.6 * k * px; c.beginPath(); c.moveTo(0, 0); c.lineTo(Math.cos(a) * rng, Math.sin(a) * rng); c.stroke(); }
        c.restore(); }
      c.restore();
      // you: an arrow in the middle
      c.save(); c.translate(cxp, cyp); c.rotate(p("turn") !== false ? 0 : w.yaw + Math.PI / 2); const s3 = 7 * k; c.fillStyle = p("player"); c.strokeStyle = "#000"; c.lineWidth = 1.2 * k; c.beginPath(); c.moveTo(0, -s3 * 1.3); c.lineTo(s3, s3); c.lineTo(0, s3 * 0.45); c.lineTo(-s3, s3); c.closePath(); c.fill(); c.stroke(); c.restore();
      c.strokeStyle = p("border"); c.lineWidth = 2 * k; c.beginPath(); if (round) c.arc(cxp, cyp, R - k, 0, Math.PI * 2); else c.rect(k, k, W2 - 2 * k, H2 - 2 * k); c.stroke();
    }
    dot(c, x, y, r, col, a) { c.globalAlpha = a; c.fillStyle = col; c.beginPath(); c.arc(x, y, r, 0, Math.PI * 2); c.fill(); c.globalAlpha = 1; }
    // boss health bar: hidden unless a fight is on (always shown in the editor)
    bossDraw(it, dt) { const el = it.el, n = it.n, p = k => this.P(n, k), T = this.tags, demo = this.edit || !this.world, on = demo || !!T.get("player.bossActive");
      const mx = demo ? 100 : +T.get("player.bossMax") || 1, hp = demo ? 70 : Math.max(0, +T.get("player.bossHealth") || 0), f = clamp(hp / mx, 0, 1);
      it.lag = it.lag === undefined || f > it.lag ? f : Math.max(f, it.lag - dt * 0.35); el.style.opacity = on ? p("opacity") : 0;
      const nm = el.firstChild, bar = el.lastChild, w = +p("w") || 640, h = +p("h") || 16; el.style.width = w + "px";
      nm.style.display = p("showName") !== false ? "" : "none"; nm.style.fontFamily = font(p("font")); nm.style.fontSize = p("size") + "px"; nm.style.color = p("textColor"); nm.style.marginBottom = "4px"; nm.style.letterSpacing = ".12em"; nm.style.textShadow = "0 1px 3px #000";
      const ph = demo ? "Phase 1" : T.get("player.bossPhase"); nm.textContent = (demo ? "BOSS NAME" : String(T.get("player.bossName") || "")) + (p("showPhase") && ph ? "  ·  " + ph : "");
      bar.style.width = w + "px"; bar.style.height = h + "px"; bar.style.background = p("bg"); bar.style.border = "2px solid " + p("border"); bar.firstChild.style.width = it.lag * 100 + "%"; bar.firstChild.style.background = p("lagColor"); bar.children[1].style.width = f * 100 + "%"; bar.children[1].style.background = p("color");
      const marks = p("marks") !== false ? (demo ? [0.66, 0.33] : this.bossMarks || []) : [], sig = marks.join(","); if (it.mk !== sig) { it.mk = sig; for (const q of [...bar.querySelectorAll("b")]) q.remove(); for (const m of marks) { const b = mk("b"); b.style.cssText = "position:absolute;top:0;bottom:0;width:2px;background:rgba(0,0,0,.75);left:" + m * 100 + "%"; bar.append(b); } } }
    // keys you carry (key objects whose tag <key>.have is yes)
    keysDraw(el, p) { const T = this.tags, keys = (this.project.props || []).filter(q => T.get(q.id + ".type") === "key"), demo = this.edit || !this.world, miss = !!p("missing"), sz = +p("box") || 34, gap = +p("gap") || 0;
      const list = keys.map((q, i) => ({ id: q.id, have: demo ? i % 2 === 0 : !!T.get(q.id + ".have"), col: T.get(q.id + ".keyColor") || "#ffffff", icon: T.get(q.id + ".keyIcon") })).filter(q => q.have || miss);
      const sig = [list.map(q => q.id + q.have + q.col + q.icon).join(","), sz, gap, p("vertical"), p("opacity")].join("|"); if (el.dataset.k === sig) return; el.dataset.k = sig;
      el.style.display = "flex"; el.style.flexDirection = p("vertical") ? "column" : "row"; el.style.gap = gap + "px"; el.style.opacity = p("opacity");
      el.replaceChildren(...list.map(q => { const d = mk("div"); d.style.cssText = "width:" + sz + "px;height:" + sz + "px;flex:none;opacity:" + (q.have ? 1 : 0.25) + ";filter:drop-shadow(0 1px 2px #000)";
        if (q.icon && this.urls[q.icon]) { d.style.background = 'center/contain no-repeat url("' + this.urls[q.icon] + '")'; } else d.innerHTML = '<svg viewBox="0 0 32 32" width="100%" height="100%"><circle cx="10" cy="16" r="7" fill="none" stroke="' + q.col + '" stroke-width="4"/><path d="M16 16 H30 M25 16 V22 M29 16 V21" stroke="' + q.col + '" stroke-width="4" fill="none"/></svg>'; return d; }));
      if (!list.length) { el.style.width = sz + "px"; el.style.height = sz + "px"; } else { el.style.width = ""; el.style.height = ""; } }
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
  // "Counter boxes": a grid of boxes, the first `bind` of them filled (energy tanks, lives). Shared with runtime.js (menu screens).
  PRIM.HUD.pips = (el, T, p) => {
    const b = p("bind"), sp = T.spec(b), mt = p("max"), ms = mt && T.spec(mt), v = Math.max(0, Math.round(sp ? +T.get(b) || 0 : 0)), total = Math.max(0, Math.min(200, Math.round(ms ? +T.get(mt) || 0 : sp ? +sp.max || 0 : 0))), sz = +p("box") || 18, gap = +p("gap") || 0, per = +p("perRow") || 0;
    const key = [v, total, sz, gap, per, p("color"), p("empty"), p("border"), p("opacity")].join("|"); if (el.dataset.k === key) return; el.dataset.k = key;
    const cols = per > 0 ? Math.min(per, Math.max(1, total)) : Math.max(1, total); el.style.display = "grid"; el.style.gridTemplateColumns = "repeat(" + cols + ", " + sz + "px)"; el.style.gap = gap + "px"; el.style.opacity = p("opacity"); el.style.direction = "ltr";
    const kids = []; for (let i = 0; i < total; i++) { const d = document.createElement("i"); d.style.cssText = "display:block;width:" + sz + "px;height:" + sz + "px;box-sizing:border-box;border:2px solid " + p("border") + ";background:" + (i < v ? p("color") : p("empty")) + (i < v ? ";box-shadow:0 0 6px " + p("color") : ""); kids.push(d); }
    el.replaceChildren(...kids); if (!total) { el.style.width = sz + "px"; el.style.height = sz + "px"; } else { el.style.width = ""; el.style.height = ""; }
  };
})();