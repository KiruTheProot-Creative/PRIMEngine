/* PRIMEngine FBX reader. Reads ASCII FBX (e.g. Blockbench) and binary FBX (e.g. Blender).
   Gives back a small model: a bone / node tree where each node may hold a mesh, the animations found in the file
   (position, rotation and scale keys per node) and the embedded texture. Animation moves nodes (rigid bones);
   skin weights are not used yet. */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), F = (PRIM.FBX = { cache: {}, pending: {} }), M = PRIM.M4, KTIME = 46186158000;

  class Node { constructor(name, props) { this.name = name; this.props = props; this.kids = []; } get(n) { return this.kids.find(k => k.name === n) || null; } all(n) { return this.kids.filter(k => k.name === n); } }

  // ------------------------------------------------------------ ASCII
  function splitProps(s) {
    const out = []; let cur = "", q = false;
    for (const ch of s) { if (ch === '"') { q = !q; cur += ch; } else if (ch === "," && !q) { out.push(cur.trim()); cur = ""; } else cur += ch; }
    if (cur.trim()) out.push(cur.trim());
    return out.map(p => { if (p.startsWith('"') && p.endsWith('"') && p.length >= 2) return p.slice(1, -1); const n = Number(p); return p !== "" && isFinite(n) ? n : p; });
  }
  function parseAscii(text) {
    const root = new Node("root", []), stack = [root];
    for (const raw of text.split("\n")) {
      const line = raw.trim(); if (!line || line[0] === ";") continue; if (line === "}") { stack.pop(); continue; }
      const m = /^([^:]+):\s*(.*)$/.exec(line); if (!m) continue; const name = m[1].trim(); let rest = m[2]; const opens = rest.endsWith("{"); if (opens) rest = rest.slice(0, -1).trim();
      if (name === "a") { stack[stack.length - 1].props = rest.split(",").filter(x => x.trim() !== "").map(Number); continue; }
      const node = new Node(name, name === "Content" ? [rest.trim().replace(/^"|"$/g, "")] : splitProps(rest)); stack[stack.length - 1].kids.push(node); if (opens) stack.push(node);
    }
    return root;
  }

  // ------------------------------------------------------------ binary
  async function inflate(bytes) { const ds = new DecompressionStream("deflate"), w = ds.writable.getWriter(); w.write(bytes); w.close(); return new Uint8Array(await new Response(ds.readable).arrayBuffer()); }
  async function parseBinary(buf) {
    const dv = new DataView(buf), u8 = new Uint8Array(buf), big = dv.getUint32(23, true) >= 7500, td = new TextDecoder(); let pos = 27;
    const rdN = () => { const v = big ? Number(dv.getBigUint64(pos, true)) : dv.getUint32(pos, true); pos += big ? 8 : 4; return v; };
    const ARR = { f: [4, Float32Array, "getFloat32"], d: [8, Float64Array, "getFloat64"], i: [4, Int32Array, "getInt32"], l: [8, null, "getBigInt64"], b: [1, Uint8Array, "getUint8"] };
    async function readNode() {
      const end = rdN(), np = rdN(), pl = rdN(); const nl = u8[pos++]; if (end === 0) return null; const name = td.decode(u8.subarray(pos, pos + nl)); pos += nl; const props = [], propEnd = pos + pl;
      for (let i = 0; i < np; i++) {
        const t = String.fromCharCode(u8[pos++]);
        if (t === "Y") { props.push(dv.getInt16(pos, true)); pos += 2; } else if (t === "C") { props.push(u8[pos++]); } else if (t === "I") { props.push(dv.getInt32(pos, true)); pos += 4; }
        else if (t === "F") { props.push(dv.getFloat32(pos, true)); pos += 4; } else if (t === "D") { props.push(dv.getFloat64(pos, true)); pos += 8; } else if (t === "L") { props.push(Number(dv.getBigInt64(pos, true))); pos += 8; }
        else if (t === "S") { const l = dv.getUint32(pos, true); pos += 4; props.push(td.decode(u8.subarray(pos, pos + l)).replace(/^(.*)\x00\x01(.*)$/, "$2::$1")); pos += l; }
        else if (t === "R") { const l = dv.getUint32(pos, true); pos += 4; props.push(u8.slice(pos, pos + l)); pos += l; }
        else if (ARR[t]) {
          const len = dv.getUint32(pos, true), enc = dv.getUint32(pos + 4, true), cl = dv.getUint32(pos + 8, true); pos += 12; let bytes = u8.slice(pos, pos + cl); pos += cl; if (enc === 1) bytes = await inflate(bytes);
          const [sz, Ty, get] = ARR[t], d = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength), out = new Array(len); for (let k = 0; k < len; k++) out[k] = t === "l" ? Number(d.getBigInt64(k * 8, true)) : d[get](k * sz, true); props.push(out);
        } else throw new Error("FBX: unknown property type " + t);
      }
      pos = propEnd; const node = new Node(name, props); if (props.length === 1 && Array.isArray(props[0])) node.props = props[0];
      while (pos < end) { const c = await readNode(); if (!c) { continue; } node.kids.push(c); } pos = end; return node;
    }
    const root = new Node("root", []); while (pos < buf.byteLength - 160) { const n = await readNode(); if (!n) break; root.kids.push(n); } return root;
  }

  // ------------------------------------------------------------ tree -> model
  const num = (a, d) => (a && a.length ? a : d);
  function lcl(o, key, def) { const pr = o.get("Properties70"); if (pr) for (const p of pr.all("P")) if (p.props[0] === key && p.props.length >= 3) return p.props.slice(-3).map(Number); return def; }
  function layerVal(layer, dataName, idxName, comps, poly, corner, vert) {
    const mt = layer.get("MappingInformationType"), rt = layer.get("ReferenceInformationType"), data = layer.get(dataName), idx = layer.get(idxName); if (!data) return null;
    const map = mt ? mt.props[0] : "ByPolygonVertex"; let i = map === "ByPolygon" ? poly : map === "ByPolygonVertex" ? corner : map === "AllSame" ? 0 : vert;
    if (rt && rt.props[0] === "IndexToDirect" && idx) i = idx.props[i]; return [data.props[i * comps], data.props[i * comps + 1], comps > 2 ? data.props[i * comps + 2] : 0];
  }
  function buildMesh(g) {
    const verts = g.get("Vertices").props, pvi = g.get("PolygonVertexIndex").props, nl = g.get("LayerElementNormal"), ul = g.get("LayerElementUV"), P = [], N = [], U = [];
    const polys = []; let cur = []; pvi.forEach((v, i) => { const end = v < 0, vi = end ? -v - 1 : v; cur.push([vi, i]); if (end) { polys.push(cur); cur = []; } });
    polys.forEach((poly, pi) => {
      for (let t = 1; t < poly.length - 1; t++) {
        const tri = [poly[0], poly[t], poly[t + 1]], pos = tri.map(([vi]) => [verts[vi * 3], verts[vi * 3 + 1], verts[vi * 3 + 2]]);
        const e1 = [pos[1][0] - pos[0][0], pos[1][1] - pos[0][1], pos[1][2] - pos[0][2]], e2 = [pos[2][0] - pos[0][0], pos[2][1] - pos[0][1], pos[2][2] - pos[0][2]], fn = M.norm(M.cross(e1, e2));
        tri.forEach(([vi, ci], k) => {
          P.push(+pos[k][0].toFixed(4), +pos[k][1].toFixed(4), +pos[k][2].toFixed(4));
          const n = nl ? layerVal(nl, "Normals", "NormalsIndex", 3, pi, ci, vi) : null; N.push(...(n || fn));
          const u = ul ? layerVal(ul, "UV", "UVIndex", 2, pi, ci, vi) : null; U.push(u ? +u[0].toFixed(5) : 0, u ? +(1 - u[1]).toFixed(5) : 0);
        });
      }
    });
    return { p: P, n: N, u: U };
  }
  function toModel(tree) {
    const objs = tree.get("Objects"), cn = tree.get("Connections"); if (!objs || !cn) throw new Error("This FBX file has no objects.");
    const conns = cn.all("C").map(k => [k.props[1], k.props[2], k.props[3]]);
    const models = {}, geoms = {}, curveNodes = {}, curves = {}, stacks = {}, layers = {};
    for (const o of objs.kids) { const id = o.props[0], nm = typeof o.props[1] === "string" ? o.props[1].split("::").pop() : ""; if (o.name === "Model") models[id] = [nm, o]; else if (o.name === "Geometry") geoms[id] = o; else if (o.name === "AnimationCurveNode") curveNodes[id] = o; else if (o.name === "AnimationCurve") curves[id] = o; else if (o.name === "AnimationStack") stacks[id] = [nm, o]; else if (o.name === "AnimationLayer") layers[id] = [nm, o]; }
    let texBytes = null; for (const o of objs.kids) if (o.name === "Video") { const c = o.get("Content"); if (c && c.props[0] && (typeof c.props[0] === "string" ? c.props[0].length > 8 : c.props[0].length)) { const v = c.props[0]; if (typeof v === "string") { const bin = atob(v); texBytes = new Uint8Array(bin.length); for (let i = 0; i < bin.length; i++) texBytes[i] = bin.charCodeAt(i); } else texBytes = v; break; } }
    const parent = {}; for (const [a, b] of conns) if (a in models && (b in models || b === 0)) parent[a] = b;
    const order = [], visit = id => { order.push(id); for (const c of Object.keys(models).filter(m => parent[m] === id)) visit(isNaN(c) ? c : Number(c)); }; for (const r of Object.keys(models).filter(m => parent[m] === 0 || !(m in parent))) visit(isNaN(r) ? r : Number(r));
    const seen = new Set(), ord2 = order.filter(i => !seen.has(i) && seen.add(i)), index = {}; ord2.forEach((id, i) => { index[id] = i; });
    const geomOf = {}; for (const [a, b] of conns) if (a in geoms && b in models) geomOf[b] = a;
    const nodes = ord2.map(id => { const [nm, o] = models[id], n = { name: nm, parent: parent[id] in index ? index[parent[id]] : -1, t: lcl(o, "Lcl Translation", [0, 0, 0]), r: lcl(o, "Lcl Rotation", [0, 0, 0]), s: lcl(o, "Lcl Scaling", [1, 1, 1]), pre: lcl(o, "PreRotation", [0, 0, 0]) }; if (id in geomOf) n.mesh = buildMesh(geoms[geomOf[id]]); return n; });
    // animations
    const curveBy = {}; for (const [a, b, prop] of conns) if (a in curves && b in curveNodes && prop) curveBy[b + "|" + prop] = a;
    const target = {}; for (const [a, b, prop] of conns) if (a in curveNodes && b in models) target[a] = [b, prop];
    const layerNodes = {}; for (const [a, b] of conns) if (a in curveNodes && b in layers) (layerNodes[b] = layerNodes[b] || []).push(a);
    const stackLayer = {}; for (const [a, b] of conns) if (a in layers && b in stacks) stackLayer[b] = a;
    const anims = {};
    for (const sid of Object.keys(stacks)) {
      const [sname, so] = stacks[sid]; let dur = 0; const pr = so.get("Properties70"); if (pr) for (const p of pr.all("P")) if (p.props[0] === "LocalStop") dur = Number(p.props[p.props.length - 1]) / KTIME; const tracks = [];
      for (const cnid of layerNodes[stackLayer[sid]] || []) {
        const [mid, prop] = target[cnid] || []; if (!(mid in index)) continue; const key = { "Lcl Translation": 0, "Lcl Rotation": 1, "Lcl Scaling": 2 }[prop]; if (key === undefined) continue; const ch = [];
        for (const ax of ["d|X", "d|Y", "d|Z"]) { const cid = curveBy[cnid + "|" + ax]; if (cid === undefined) { ch.push(null); continue; } const c = curves[cid], ts = c.get("KeyTime").props.map(t => +(t / KTIME).toFixed(5)), vs = c.get("KeyValueFloat").props.map(v => +Number(v).toFixed(4)); ch.push({ t: ts, v: vs }); dur = Math.max(dur, ts[ts.length - 1] || 0); }
        tracks.push({ n: index[mid], p: key, c: ch });
      }
      let name = sname || "Animation", k = 2; while (anims[name]) name = sname + " " + k++; anims[name] = { dur: +dur.toFixed(4), tracks };
    }
    const model = { nodes, anims, texBytes }; model.bounds = F.bounds(model); return model;
  }
  F.bounds = function (model) {
    const w = F.pose(model, null, 0), lo = [1e9, 1e9, 1e9], hi = [-1e9, -1e9, -1e9]; model.nodes.forEach((n, i) => { if (!n.mesh) return; for (let k = 0; k < n.mesh.p.length; k += 3) { const q = M.point(w[i], n.mesh.p[k], n.mesh.p[k + 1], n.mesh.p[k + 2]); for (let a = 0; a < 3; a++) { lo[a] = Math.min(lo[a], q[a]); hi[a] = Math.max(hi[a], q[a]); } } });
    return lo[0] > hi[0] ? { lo: [0, 0, 0], hi: [1, 1, 1], size: 1 } : { lo, hi, size: Math.max(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]) };
  };
  // ------------------------------------------------------------ pose at a time in an animation (null = rest pose): world matrix of every node
  function sample(c, t) { const ts = c.t, vs = c.v; if (!ts.length) return 0; if (t <= ts[0]) return vs[0]; const n = ts.length - 1; if (t >= ts[n]) return vs[n]; let i = 0; while (ts[i + 1] < t) i++; const f = (t - ts[i]) / ((ts[i + 1] - ts[i]) || 1); return vs[i] + (vs[i + 1] - vs[i]) * f; }
  F.pose = function (model, anim, time) {
    const local = model.nodes.map(n => ({ t: n.t.slice(), r: n.r.slice(), s: n.s.slice() })), a = anim ? model.anims[anim] : null;
    if (a) for (const tr of a.tracks) { const L = local[tr.n], key = ["t", "r", "s"][tr.p]; for (let ax = 0; ax < 3; ax++) if (tr.c[ax]) L[key][ax] = sample(tr.c[ax], time); }
    const world = []; model.nodes.forEach((n, i) => { const m = M.trs(local[i].t, local[i].r, local[i].s, n.pre); world[i] = n.parent >= 0 ? M.mul(world[n.parent], m) : m; }); return world;
  };
  // ------------------------------------------------------------ entry points
  F.parse = async function (buf) {
    const head = new Uint8Array(buf, 0, Math.min(20, buf.byteLength)), isBin = new TextDecoder().decode(head).startsWith("Kaydara FBX Binary");
    return toModel(isBin ? await parseBinary(buf) : parseAscii(new TextDecoder().decode(buf)));
  };
  // small summary stored in the project (the file itself stays the source of truth)
  F.summary = m => ({ anims: Object.keys(m.anims).map(n => ({ name: n, dur: m.anims[n].dur, tracks: m.anims[n].tracks.length })), nodes: m.nodes.length, meshes: m.nodes.filter(n => n.mesh).length, size: +m.bounds.size.toFixed(2) });
  // a flat picture as a "model": one quad (width = picture aspect, height 1) so images can be attached to the camera like any other object
  F.loadImage = function (id, url) {
    if (F.cache[id]) return Promise.resolve(F.cache[id]); if (!url) return Promise.resolve(null);
    return F.pending[id] || (F.pending[id] = fetch(url).then(r => r.arrayBuffer()).then(buf => new Promise((res, rej) => { const im = new Image(); im.onload = () => res([buf, im.naturalWidth / (im.naturalHeight || 1)]); im.onerror = rej; im.src = URL.createObjectURL(new Blob([buf])); })).then(([buf, asp]) => {
      const w = asp / 2, p = [-w, -0.5, 0, w, -0.5, 0, w, 0.5, 0, -w, -0.5, 0, w, 0.5, 0, -w, 0.5, 0], n = [0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1], u = [0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0];
      const m = { nodes: [{ name: "image", parent: -1, t: [0, 0, 0], r: [0, 0, 0], s: [1, 1, 1], pre: [0, 0, 0], mesh: { p, n, u } }], anims: {}, texBytes: new Uint8Array(buf), image: true }; m.bounds = F.bounds(m); F.cache[id] = m; delete F.pending[id]; return m;
    }).catch(e => { delete F.pending[id]; return null; }));
  };
  F.load = function (id, url) { if (F.cache[id]) return Promise.resolve(F.cache[id]); if (!url) return Promise.resolve(null); return F.pending[id] || (F.pending[id] = fetch(url).then(r => r.arrayBuffer()).then(F.parse).then(m => { F.cache[id] = m; delete F.pending[id]; return m; }).catch(e => { delete F.pending[id]; console.warn("FBX failed", e); return null; })); };
})();
