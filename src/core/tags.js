/* PRIMEngine tag system. EVERY editable value in a project is a tag: a named record
   { type, value, label, group, min, max, step, options, assetType, refType, owner, custom, builtin }.
   Names look like "main.play.text" (screen.element.property) or "audio.master".
   Text values can read other tags with {tag.name}. */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {});
  class Tags {
    constructor(t) { this.t = t || {}; this.ov = null; this.ls = new Set(); }
    fork() { const f = new Tags(this.t); f.ov = {}; return f; }          // runtime copy: changes stay in an overlay, the project is untouched
    spec(n) { return this.t[n]; }
    has(n) { return n in this.t; }
    get(n) { if (this.ov && n in this.ov) return this.ov[n]; const r = this.t[n]; return r ? r.value : undefined; }
    set(n, v) { this.setMany({ [n]: v }); }
    setMany(o) { const names = []; for (const n in o) { if (!(n in this.t)) continue; if (this.ov) this.ov[n] = o[n]; else this.t[n].value = o[n]; names.push(n); } this.emit(names); }
    on(fn) { this.ls.add(fn); return () => this.ls.delete(fn); }
    emit(names) { for (const f of [...this.ls]) f(names || []); }
    names() { return Object.keys(this.t); }
    interpolate(s) {
      return String(s == null ? "" : s).replace(/\{([a-zA-Z0-9_.]+)\}/g, (m, n) => { const v = this.get(n); return v === undefined ? m : typeof v === "number" ? String(Math.round(v * 100) / 100) : String(v); });
    }
    define(n, rec) { this.t[n] = rec; }
    removePrefix(p) { for (const n of Object.keys(this.t)) if (n === p || n.startsWith(p + ".")) delete this.t[n]; this.emit([]); }
    // fix every reference to a renamed tag (slider/toggle bindings and {braces} in text)
    refUpdate(o, n) {
      for (const k in this.t) { const r = this.t[k];
        if (r.type === "tagRef" && r.value === o) r.value = n;
        else if (r.type === "string" && typeof r.value === "string" && r.value.includes("{" + o + "}")) r.value = r.value.split("{" + o + "}").join("{" + n + "}"); }
    }
    rename(o, n) { if (!(o in this.t) || n in this.t) return false; this.t[n] = this.t[o]; delete this.t[o]; this.refUpdate(o, n); this.emit([n]); return true; }
    renamePrefix(op, np) {
      const pairs = [], recs = {};
      for (const n of Object.keys(this.t)) if (n === op || n.startsWith(op + ".")) pairs.push([n, np + n.slice(op.length)]);
      for (const [o, n] of pairs) { const r = this.t[o]; delete this.t[o];
        if (r.owner && (r.owner === op || r.owner.startsWith(op + "."))) r.owner = np + r.owner.slice(op.length); recs[n] = r; }
      Object.assign(this.t, recs); for (const [o, n] of pairs) this.refUpdate(o, n); this.emit([]);
    }
  }
  PRIM.Tags = Tags;
})();
