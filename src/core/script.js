/* PRIMEngine node scripts. A script belongs to one object (a screen element or a player object / weapon):
   { nodes: [{ id, type, x, y }], links: [{ a: fromNode, o: outputName, b: toNode }] }
   A node's values are tags named <owner tag prefix>.s.<nodeId>.<value>. Events (green nodes) start a chain; the chain then follows the "next" links. */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {});
  const sleep = (ms, tok) => new Promise(res => { const t = setTimeout(res, Math.max(0, ms)); tok.cancel = () => { clearTimeout(t); res(); }; });
  PRIM.Script = {};
  PRIM.Script.Host = class Host {
    // o: { tags, sfx(assetId, volume), goto(screenId), playAnim(ownerKey, animName, loop), say(text) }
    constructor(o) { this.o = o; this.T = o.tags; this.runs = []; this.dead = false; }
    stopOwner(key) { this.runs = this.runs.filter(r => { if (r.key === key) { r.dead = true; if (r.cancel) r.cancel(); return false; } return true; }); }
    stopRuns() { for (const r of this.runs) { r.dead = true; if (r.cancel) r.cancel(); } this.runs = []; }
    stopAll() { this.dead = true; if (this.watchT) clearInterval(this.watchT); for (const r of this.runs) { r.dead = true; if (r.cancel) r.cancel(); } this.runs = []; }
    // a node's "Only if" condition (tags <owner>.s.<node>.needTag / needOp / needVal): no tag = always allowed
    ok(owner, n) { const k = owner.key + ".s." + n.id + ".", t = this.T.get(k + "needTag"); if (!t) return true; return this.test(t, this.T.get(k + "needOp") || "=", this.T.get(k + "needVal")); }
    has(owner, type) { return !!(owner.script && owner.script.nodes.some(n => n.type === type)); }
    // owner: { key: tag prefix, script }. arg is the key name for onKey.
    trigger(owner, type, arg, only) {
      if (this.dead || !owner.script) return;
      for (const n of owner.script.nodes) {
        if (n.type !== type) continue; if (only && n.id !== only) continue; if ((type === "onKey" || type === "onKeyUp" || type === "onHold") && String(this.T.get(owner.key + ".s." + n.id + ".key") || "").toLowerCase() !== String(arg || "").toLowerCase()) continue; if (type === "onImpact") { const [w, c] = String(arg || "").split("|"), fw = this.T.get(owner.key + ".s." + n.id + ".what") || "any", fc = this.T.get(owner.key + ".s." + n.id + ".charged") || "any"; if (fw !== "any" && fw !== w) continue; if (fc === "charged" && c !== "1" || fc === "normal" && c === "1") continue; } if (type === "onButton" && String(this.T.get(owner.key + ".s." + n.id + ".name") || "").trim() && String(this.T.get(owner.key + ".s." + n.id + ".name")).trim().toLowerCase() !== String(arg || "").trim().toLowerCase()) continue; if (type === "onMessage" && String(this.T.get(owner.key + ".s." + n.id + ".name") || "").trim().toLowerCase() !== String(arg || "").trim().toLowerCase()) continue; if (type === "onState" && String(this.T.get(owner.key + ".s." + n.id + ".state") || "") !== String(arg || "")) continue;
        if (!this.ok(owner, n)) continue; this.runOut(owner, n.id, "next");
      }
    }
    // "When a tag becomes…" events: checked 20 times a second; a chain starts each time the comparison turns true
    watch(owners) {
      if (this.watchT) clearInterval(this.watchT); this.prevTag = {};
      this.watchT = setInterval(() => { if (this.dead) return clearInterval(this.watchT); for (const ow of owners() || []) { if (!ow.script) continue; for (const n of ow.script.nodes) { if (n.type !== "onTag") continue; const k = ow.key + ".s." + n.id + ".", t = this.T.get(k + "tag"); if (!t) continue; const now = this.test(t, this.T.get(k + "op") || "=", this.T.get(k + "value")), id = ow.key + "/" + n.id, was = this.prevTag[id]; this.prevTag[id] = now; if (now && was === false && this.ok(ow, n)) this.runOut(ow, n.id, "next"); } } }, 50);
    }
    // start the chain that hangs off one output of a node (used by events with several outputs, like the charge beam)
    runOut(owner, id, out) {
      if (this.dead || !owner.script) return; const tok = { key: owner.key, dead: false }; this.runs.push(tok);
      return this.chain(owner, this.link(owner, id, out), tok).catch(e => this.o.say && this.o.say("Script problem: " + e.message)).then(() => { this.runs = this.runs.filter(r => r !== tok); });
    }
    // a dialogue graph: runs from its "Dialogue starts" node; the promise ends when the talk is over
    play(owner) { const st = owner.script && owner.script.nodes.find(n => n.type === "dStart"); return st && !this.dead ? this.runOut(owner, st.id, "next") || Promise.resolve() : Promise.resolve(); }
    link(owner, id, out) { const l = owner.script.links.find(x => x.a === id && x.o === out); return l ? l.b : null; }
    setTag(name, raw) {
      const T = this.T, sp = T.spec(name); if (!sp) return; let v = raw;
      if (sp.type === "number") { v = parseFloat(T.interpolate(String(raw))); if (!isFinite(v)) return; if (isFinite(sp.min)) v = Math.max(sp.min, v); if (isFinite(sp.max)) v = Math.min(sp.max, v); }
      else if (sp.type === "bool") v = /^(1|true|on|yes)$/i.test(String(raw).trim()); else v = T.interpolate(String(raw));
      T.set(name, v);
    }
    test(name, op, raw) {
      const T = this.T, sp = T.spec(name); if (!sp) return false; let a = T.get(name), b = raw;
      if (sp.type === "number") { a = +a; b = parseFloat(T.interpolate(String(raw))); } else if (sp.type === "bool") { a = !!a; b = /^(1|true|on|yes)$/i.test(String(raw).trim()); } else { a = String(a); b = T.interpolate(String(raw)); }
      switch (op) { case "=": return a === b; case "!=": return a !== b; case ">": return a > b; case "<": return a < b; case ">=": return a >= b; case "<=": return a <= b; } return false;
    }
    async chain(owner, id, tok) {
      let guard = 0;
      while (id && !tok.dead && !this.dead) {
        const n = owner.script.nodes.find(x => x.id === id); if (!n || !this.ok(owner, n)) return; const g = k => this.T.get(owner.key + ".s." + n.id + "." + k); let out = "next";
        switch (n.type) {
          case "setTag": this.setTag(g("tag"), g("value")); break;
          case "changeTag": { const t = g("tag"), sp = this.T.spec(t); if (sp && sp.type === "number") this.setTag(t, (+this.T.get(t) || 0) + (+g("amount") || 0)); break; }
          case "loopSound": if (g("sound") && this.o.loop) this.o.loop(g("sound"), +g("volume")); break;
          case "stopLoop": if (this.o.stopLoop) this.o.stopLoop(g("sound") || ""); break;
          case "playSound": if (g("sound") && this.o.sfx) this.o.sfx(g("sound"), +g("volume")); break;
          case "playAnim": if (this.o.playAnim && !(g("skipReload") !== false && this.o.isReloading && this.o.isReloading())) this.o.playAnim(owner.key, g("anim"), !!g("loop")); break;
          case "stopAnim": if (this.o.stopAnim) this.o.stopAnim(owner.key); break;
          case "showHide": if (this.o.showHide) this.o.showHide(owner.key, String(g("show") || "hide")); break;
          case "shake": if (this.o.shake) this.o.shake(+g("strength") || 0, +g("seconds") || 0.3); break;
          case "flashScreen": if (this.o.flash) this.o.flash(String(g("color") || "#ffffff"), +g("strength") || 0); break;
          case "jump": if (this.o.jump) this.o.jump(+g("height") || 0, !!g("air")); break;
          case "airJump": if (this.o.airJump) this.o.airJump(Math.max(1, +g("max") || 1), +g("height") || 0); break;
          case "launch": if (this.o.launch) this.o.launch(+g("forward") || 0, +g("side") || 0, +g("up") || 0); break;
          case "hurtPlayer": if (this.o.hurt) this.o.hurt(+g("amount") || 0); break;
          case "teleport": if (this.o.teleport) this.o.teleport(); break;
          case "switchWeapon": if (this.o.switchWeapon) this.o.switchWeapon(String(g("which") || "next"), +g("num") || 1); break;
          case "sendMessage": if (this.o.broadcast) { const nm = String(g("name") || "").trim(); setTimeout(() => { if (!this.dead) this.o.broadcast(nm); }, 0); } break;
          case "waitUntil": { let i = 0; while (!tok.dead && !this.dead && !this.test(g("tag"), g("op") || "=", g("value"))) { await sleep(16, tok); i++; } break; }
          case "repeatUntil": { const body = this.link(owner, n.id, "body"); while (!tok.dead && !this.dead && !this.test(g("tag"), g("op") || "=", g("value"))) { await this.chain(owner, body, tok); await sleep(16, tok); } out = "next"; break; }
          case "chance": out = Math.random() * 100 < (+g("percent") || 0) ? "then" : "else"; break;
          case "stop": return;
          case "stopAll": this.stopOwner(owner.key); return;
          case "ifReloading": out = this.o.isReloading && this.o.isReloading() ? "then" : "else"; break;
          case "ifNear": out = this.o.nearEnemy && this.o.nearEnemy(+g("dist") || 0) ? "then" : "else"; break;
          case "randomTag": { const t = g("tag"), a = +g("min") || 0, b = +g("max") || 0, lo = Math.min(a, b), hi = Math.max(a, b); let v = lo + Math.random() * (hi - lo); if (g("whole") !== false) v = Math.floor(lo + Math.random() * (Math.floor(hi) - Math.ceil(lo) + 1)) + (Math.ceil(lo) - lo); this.setTag(t, v); break; }
          case "mathTag": { const t = g("tag"), sp = this.T.spec(t); if (!sp || sp.type !== "number") break; const a = +this.T.get(t) || 0, b = +g("value") || 0; let v = a;
            switch (String(g("op"))) { case "*": v = a * b; break; case "/": v = b ? a / b : a; break; case "min": v = Math.min(a, b); break; case "max": v = Math.max(a, b); break; case "mod": v = b ? ((a % b) + b) % b : a; break; case "round": v = b ? Math.round(a / b) * b : Math.round(a); break; } this.setTag(t, v); break; }
          case "toggleTag": { const t = g("tag"), sp = this.T.spec(t); if (sp) this.T.set(t, sp.type === "bool" ? !this.T.get(t) : !/^(1|true|on|yes)$/i.test(String(this.T.get(t)))); break; }
          case "dLine": { if (!this.o.dlgLine) break; await this.o.dlgLine({ speaker: this.T.interpolate(String(g("speaker") || "")), text: this.T.interpolate(String(g("text") || "")), sound: g("sound"), auto: +g("auto") || 0 }); break; }
          case "dChoice": { const opts = [1, 2, 3, 4].map(i => ({ out: "c" + i, label: this.T.interpolate(String(g("c" + i) || "").trim()) })).filter(c => c.label);
            if (!this.o.dlgChoice || !opts.length) return; out = await this.o.dlgChoice({ speaker: this.T.interpolate(String(g("speaker") || "")), text: this.T.interpolate(String(g("text") || "")), opts }); if (!out) return; break; }
          case "dEnd": return;
          case "pauseGame": if (this.o.pause) setTimeout(() => { if (!this.dead) this.o.pause(g("screen") || ""); }, 0); break;
          case "startDialogue": { const pr = this.o.dialogue ? this.o.dialogue(g("dialog")) : null; if (pr && g("wait") !== false) await pr; break; }
          case "gotoScreen": if (this.o.goto) this.o.goto(g("screen")); break;
          case "say": if (this.o.say) this.o.say(this.T.interpolate(String(g("text")))); break;
          case "wait": await sleep((+g("seconds") || 0) * 1000, tok); break;
          case "if": out = this.test(g("tag"), g("op"), g("value")) ? "then" : "else"; break;
          case "chargeShow": if (this.o.orb) this.o.orb(owner.key, "show", { color: String(g("color")), size: +g("size") || 1 }); break;
          case "chargeGrow": if (this.o.orb) this.o.orb(owner.key, "grow", { size: +g("size") || 1, seconds: +g("seconds") || 0 }); break;
          case "chargeSuck": if (this.o.orb) this.o.orb(owner.key, "suck", { rate: +g("rate") || 40, radius: +g("radius") || 10, size: +g("size") || 0.35, time: +g("time") || 0.4, color: g("same") === false ? String(g("color") || "#ffffff") : null }); break;
          case "fireProjectile": if (this.o.fire) this.o.fire(owner.key); break;
          case "reload": if (this.o.reload) this.o.reload(owner.key); break;
          case "ifAmmo": out = !this.o.hasAmmo || this.o.hasAmmo(owner.key) ? "then" : "else"; break;
          case "ifState": out = this.o.isState && this.o.isState(String(g("state") || "")) ? "then" : "else"; break;
          case "ifKey": out = this.o.held && this.o.held(String(g("key") || "").toLowerCase()) ? "then" : "else"; break;
          case "repeat": { const body = this.link(owner, n.id, "body"), cnt = Math.min(1000, +g("times") || 0); for (let i = 0; i < cnt && !tok.dead; i++) { await this.chain(owner, body, tok); await sleep(0, tok); } break; }
          case "forever": { const body = this.link(owner, n.id, "body"); while (!tok.dead && !this.dead) { await this.chain(owner, body, tok); await sleep(16, tok); } return; }
        }
        id = this.link(owner, n.id, out); if (++guard % 200 === 0) await sleep(0, tok);
      }
    }
  };
})();
