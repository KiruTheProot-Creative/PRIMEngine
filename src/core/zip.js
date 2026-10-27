/* Minimal ZIP reader/writer (no libraries). A .prime file is a ZIP, so it can be opened with any unzip tool. */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), Z = (PRIM.Zip = {});
  const TAB = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
  Z.crc32 = u8 => { let c = 0xFFFFFFFF; for (let i = 0; i < u8.length; i++) c = TAB[(c ^ u8[i]) & 255] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; };

  // files: [{ name, data:Uint8Array }]  ->  Uint8Array  (entries are stored, not compressed: images/sounds are already compressed)
  Z.write = files => {
    const enc = new TextEncoder(); let size = 22;
    const ents = files.map(f => { const nm = enc.encode(f.name); size += 76 + 2 * nm.length + f.data.length; return { nm, data: f.data, crc: Z.crc32(f.data) }; });
    const out = new Uint8Array(size), dv = new DataView(out.buffer), n = new Date();
    const dt = ((n.getFullYear() - 1980) << 9) | ((n.getMonth() + 1) << 5) | n.getDate(), tm = (n.getHours() << 11) | (n.getMinutes() << 5) | (n.getSeconds() >> 1); let p = 0;
    for (const e of ents) { e.off = p;
      dv.setUint32(p, 0x04034b50, true); dv.setUint16(p + 4, 20, true); dv.setUint16(p + 6, 0x0800, true); dv.setUint16(p + 8, 0, true); dv.setUint16(p + 10, tm, true); dv.setUint16(p + 12, dt, true);
      dv.setUint32(p + 14, e.crc, true); dv.setUint32(p + 18, e.data.length, true); dv.setUint32(p + 22, e.data.length, true); dv.setUint16(p + 26, e.nm.length, true); dv.setUint16(p + 28, 0, true);
      out.set(e.nm, p + 30); out.set(e.data, p + 30 + e.nm.length); p += 30 + e.nm.length + e.data.length; }
    const cd = p;
    for (const e of ents) {
      dv.setUint32(p, 0x02014b50, true); dv.setUint16(p + 4, 20, true); dv.setUint16(p + 6, 20, true); dv.setUint16(p + 8, 0x0800, true); dv.setUint16(p + 10, 0, true); dv.setUint16(p + 12, tm, true); dv.setUint16(p + 14, dt, true);
      dv.setUint32(p + 16, e.crc, true); dv.setUint32(p + 20, e.data.length, true); dv.setUint32(p + 24, e.data.length, true); dv.setUint16(p + 28, e.nm.length, true);
      dv.setUint16(p + 30, 0, true); dv.setUint16(p + 32, 0, true); dv.setUint16(p + 34, 0, true); dv.setUint16(p + 36, 0, true); dv.setUint32(p + 38, 0, true); dv.setUint32(p + 42, e.off, true);
      out.set(e.nm, p + 46); p += 46 + e.nm.length; }
    dv.setUint32(p, 0x06054b50, true); dv.setUint16(p + 4, 0, true); dv.setUint16(p + 6, 0, true); dv.setUint16(p + 8, ents.length, true); dv.setUint16(p + 10, ents.length, true); dv.setUint32(p + 12, p - cd, true); dv.setUint32(p + 16, cd, true); dv.setUint16(p + 20, 0, true);
    return out.slice(0, p + 22);
  };

  // Uint8Array -> Promise<[{ name, data }]>   (supports stored and deflate entries)
  Z.read = async buf => {
    const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength), dec = new TextDecoder(); let e = -1;
    for (let i = buf.length - 22; i >= Math.max(0, buf.length - 65557); i--) if (dv.getUint32(i, true) === 0x06054b50) { e = i; break; }
    if (e < 0) throw new Error("not a valid .prime file (zip directory not found)");
    const count = dv.getUint16(e + 10, true); let p = dv.getUint32(e + 16, true); const out = [];
    for (let i = 0; i < count; i++) {
      if (dv.getUint32(p, true) !== 0x02014b50) throw new Error("zip directory is damaged");
      const method = dv.getUint16(p + 10, true), csize = dv.getUint32(p + 20, true), nlen = dv.getUint16(p + 28, true), elen = dv.getUint16(p + 30, true), clen = dv.getUint16(p + 32, true), loff = dv.getUint32(p + 42, true);
      const name = dec.decode(buf.subarray(p + 46, p + 46 + nlen)); p += 46 + nlen + elen + clen; if (name.endsWith("/")) continue;
      const start = loff + 30 + dv.getUint16(loff + 26, true) + dv.getUint16(loff + 28, true), raw = buf.subarray(start, start + csize);
      if (method === 0) out.push({ name, data: raw.slice() });
      else if (method === 8 && typeof DecompressionStream !== "undefined") out.push({ name, data: new Uint8Array(await new Response(new Blob([raw]).stream().pipeThrough(new DecompressionStream("deflate-raw"))).arrayBuffer()) });
      else throw new Error("unsupported compression in " + name);
    }
    return out;
  };
})();
