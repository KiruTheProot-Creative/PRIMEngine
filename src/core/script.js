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
    stopAll() { this.dead = true; for (const r of this.runs) { r.dead = true; if (r.cancel) r.cancel(); } this.runs = []; }
    // a node's "Only if" condition (tags <owner>.s.<node>.needTag / needOp / needVal): no tag = always allowed
    ok(owner, n) { const k = owner.key + ".s." + n.id + ".", t = this.T.get(k + "needTag"); if (!t) return true; return this.test(t, this.T.get(k + "needOp") || "=", this.T.get(k + "needVal")); }
    has(owner, type) { return !!(owner.script && owner.script.nodes.some(n => n.type === type)); }
    // owner: { key: tag prefix, script }. arg is the key name for onKey.
    trigger(owner, type, arg, only) {
      if (this.dead || !owner.script) return;
      for (const n of owner.script.nodes) {
        if (n.type !== type) continue; if (only && n.id !== only) continue; if ((type === "onKey" || type === "onKeyUp" || type === "onHold") && String(this.T.get(owner.key + ".s." + n.id + ".key") || "").toLowerCase() !== String(arg || "").toLowerCase()) continue; if (type === "onState" && String(this.T.get(owner.key + ".s." + n.id + ".state") || "") !== String(arg || "")) continue;
        if (!this.ok(owner, n)) continue; this.runOut(owner, n.id, "next");
      }
    }
    // start the chain that hangs off one output of a node (used by events with several outputs, like the charge beam)
    runOut(owner, id, out) {
      if (this.dead || !owner.script) return; const tok = { key: owner.key, dead: false }; this.runs.push(tok);
      this.chain(owner, this.link(owner, id, out), tok).catch(e => this.o.say && this.o.say("Script problem: " + e.message)).then(() => { this.runs = this.runs.filter(r => r !== tok); });
    }
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
          case "playAnim": if (this.o.playAnim) this.o.playAnim(owner.key, g("anim"), !!g("loop")); break;
          case "gotoScreen": if (this.o.goto) this.o.goto(g("screen")); break;
          case "say": if (this.o.say) this.o.say(this.T.interpolate(String(g("text")))); break;
          case "wait": await sleep((+g("seconds") || 0) * 1000, tok); break;
          case "if": out = this.test(g("tag"), g("op"), g("value")) ? "then" : "else"; break;
          case "chargeShow": if (this.o.orb) this.o.orb(owner.key, "show", { color: String(g("color")), size: +g("size") || 1 }); break;
          case "chargeGrow": if (this.o.orb) this.o.orb(owner.key, "grow", { size: +g("size") || 1, seconds: +g("seconds") || 0 }); break;
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
