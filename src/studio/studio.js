/* PRIMEngine Studio: project manager + screen editor. */
(function () {
  "use strict";
  const PRIM = window.PRIM, Schema = PRIM.Schema, Store = PRIM.Store, Tags = PRIM.Tags, root = document.getElementById("root");
  const S = { project: null, tags: null, urls: {}, screenId: null, selId: null, tab: "props", testing: false, rt: null, filter: "", dirtyT: 0, unsub: null };
  let E = {};

  function h(tag, props, ...kids) {
    const n = document.createElement(tag);
    for (const k in (props || {})) { const v = props[k]; if (k === "class") n.className = v; else if (k === "text") n.textContent = v; else if (k.startsWith("on")) n.addEventListener(k.slice(2), v); else if (v !== false && v != null) n.setAttribute(k, v === true ? "" : v); }
    for (const c of kids.flat()) if (c != null && c !== false) n.append(c.nodeType ? c : document.createTextNode(c));
    return n;
  }
  const fmtDate = t => { const d = new Date(t); return d.toLocaleDateString(undefined, { month: "short", day: "numeric" }) + " " + d.toLocaleTimeString(undefined, { hour: "2-digit", minute: "2-digit" }); };
  const screens = () => S.project.screens, curScreen = () => screens().find(s => s.id === S.screenId) || screens()[0];

  // =============================================================== project manager
  function renderManager() {
    closeEditorState(); document.title = "PRIMEngine Studio";
    const list = Store.list(), pendingDel = renderManager.pending;
    const cards = list.map(p => h("div", { class: "pcard" },
      h("button", { class: "pcard-open", onclick: () => openProject(p.id), "aria-label": "Open " + p.name }, h("div", { class: "pthumb", text: (p.name[0] || "?").toUpperCase() }), h("div", { class: "pname", text: p.name }), h("div", { class: "pdate", text: fmtDate(p.modified) })),
      h("button", { class: "pdel" + (pendingDel === p.id ? " arm" : ""), "aria-label": "Delete " + p.name, text: pendingDel === p.id ? "Delete?" : "✕", onclick: async () => {
        if (renderManager.pending === p.id) { renderManager.pending = null; await Store.remove(p.id); renderManager(); } else { renderManager.pending = p.id; renderManager(); setTimeout(() => { if (renderManager.pending === p.id && !S.project) { renderManager.pending = null; renderManager(); } }, 3000); } } })));
    const plus = h("button", { class: "pcard plus", "aria-label": "New project", onclick: async () => { const p = Store.create(); await openProject(p.id); } }, h("div", { class: "plus-sign", text: "+" }), h("div", { class: "pname", text: "New project" }));
    root.replaceChildren(h("div", { class: "mgr" }, h("header", {}, h("div", { class: "brand" }, "PRIM", h("span", { text: "Engine" })), h("div", { class: "motto", text: "Design your game." })),
      h("h1", { text: "Projects" }), h("div", { class: "pgrid" }, plus, ...cards), h("p", { class: "mgr-note", text: "Projects are stored in this browser for now. File saving and Git come later." })));
  }

  async function openProject(id) {
    const p = Store.load(id); if (!p) return renderManager();
    S.project = p; S.tags = new Tags(p.tags); S.urls = await Store.loadAssets(p); S.screenId = p.screens.find(s => s.kind === "menu") ? p.screens.find(s => s.id === "main") ? "main" : p.screens[0].id : p.screens[0].id; S.selId = null; S.tab = "props"; S.testing = false;
    S.unsub = S.tags.on(() => markDirty()); renderEditor();
  }
  function closeEditorState() { if (S.rt) { S.rt.destroy(); S.rt = null; } if (S.unsub) { S.unsub(); S.unsub = null; } S.project = null; }
  async function backToManager() { clearTimeout(S.dirtyT); if (S.project) Store.save(S.project); closeEditorState(); renderManager(); }

  // =============================================================== saving
  function markDirty() { E.status && (E.status.textContent = "unsaved…", E.status.classList.add("dirty")); clearTimeout(S.dirtyT); S.dirtyT = setTimeout(saveNow, 400); }
  function saveNow() { clearTimeout(S.dirtyT); if (!S.project) return; const ok = Store.save(S.project); if (E.status) { E.status.textContent = ok ? "saved in this browser" : "could not save (browser storage full)"; E.status.classList.toggle("dirty", !ok); } }

  // =============================================================== editor shell
  function renderEditor() {
    document.title = S.project.name + " · PRIMEngine Studio";
    E.status = h("span", { id: "status", text: "saved in this browser" });
    const name = h("input", { class: "projname", value: S.project.name, "aria-label": "Project name", onchange: e => { S.project.name = e.target.value.trim() || "Untitled"; markDirty(); document.title = S.project.name + " · PRIMEngine Studio"; } });
    E.left = h("aside", { id: "left" }); E.right = h("aside", { id: "right" }); E.host = h("div", { class: "host" });
    E.seg = h("div", { class: "seg", role: "group", "aria-label": "Mode" }); E.tools = h("div", { class: "tools" });
    E.center = h("main", { id: "center" }, h("div", { class: "ctools" }, E.seg, E.tools, h("span", { class: "csize", text: "1280 × 720" })), h("div", { class: "hostwrap" }, E.host));
    root.replaceChildren(h("div", { id: "app" }, h("header", { id: "top" }, h("button", { class: "tb", onclick: backToManager, text: "← Projects" }), h("div", { class: "brand sm" }, "PRIM", h("span", { text: "Engine" })), name, h("div", { class: "sp" }), E.status, h("button", { class: "tb go", onclick: play, text: "▶ Play" })), E.left, E.center, E.right));
    renderAll();
  }
  function renderAll() { renderLeft(); renderCenter(); renderRight(); }
  function select(id) { S.selId = id; if (S.rt) { S.rt.selId = id; S.rt.rebuild(); } renderLeft(); renderRight(); }

  // =============================================================== center: edit / test
  function renderCenter() {
    E.seg.replaceChildren(...[["edit", "Edit"], ["test", "Test"]].map(([k, l]) => h("button", { class: "segb" + ((k === "test") === S.testing ? " sel" : ""), "aria-pressed": (k === "test") === S.testing, text: l, onclick: () => { S.testing = k === "test"; renderCenter(); } })));
    const sc = curScreen();
    E.tools.replaceChildren(...[!S.testing && sc.kind === "splash" ? h("button", { class: "tb sm", text: "▶ Preview sequence", onclick: () => { if (S.rt) S.rt.previewSplash(); } }) : null,
      S.testing ? h("span", { class: "hint", text: "Try it: hover and click buttons, drag sliders." }) : h("span", { class: "hint", text: sc.kind === "splash" ? "Pick a splash on the left to see it." : "Click to select, drag to move (Shift = snap to 10)." })].filter(Boolean));
    mountRuntime();
  }
  function mountRuntime() {
    if (S.rt) S.rt.destroy();
    const tags = S.testing ? S.tags.fork() : S.tags;
    S.rt = new PRIM.Runtime({ host: E.host, project: S.project, tags, mode: S.testing ? "test" : "edit", urls: S.urls, screenId: S.screenId, selId: S.selId,
      onSelect: id => { S.selId = id; renderLeft(); renderRight(); }, onChange: () => renderRight(), onExit: () => { S.testing = false; renderCenter(); } });
  }

  // =============================================================== left: screens, elements, assets
  function renderLeft() {
    const sc = curScreen(), isSplash = sc.kind === "splash";
    const screenRows = screens().map(s => h("button", { class: "row-item" + (s.id === sc.id ? " sel" : ""), onclick: () => { S.screenId = s.id; S.selId = null; renderAll(); } }, h("span", { class: "ic", text: s.kind === "splash" ? "✦" : "▭" }), h("span", { class: "nm", text: s.id }), S.tags.get("project.startScreen") === s.id ? h("span", { class: "meta", text: "start" }) : null));
    const types = isSplash ? ["splash"] : ["title", "button", "image", "slider", "toggle"];
    const nodeRows = sc.nodes.map(n => h("button", { class: "row-item indent" + (n.id === S.selId ? " sel" : ""), onclick: () => select(n.id) }, h("span", { class: "ic", text: Schema.NODE[n.type].icon }), h("span", { class: "nm", text: n.id }), h("span", { class: "meta", text: n.type })));
    const assetRows = S.project.assets.map(a => h("div", { class: "row-item asset" }, h("span", { class: "ic", text: { image: "▤", sound: "♪", font: "Aa" }[a.type] }), h("span", { class: "nm", title: a.name, text: a.name }),
      a.type === "sound" ? h("button", { class: "mini", "aria-label": "Play " + a.name, text: "▶", onclick: () => { const u = new Audio(S.urls[a.id]); u.volume = 0.7; u.play().catch(() => {}); } }) : null,
      h("button", { class: "mini", "aria-label": "Remove " + a.name, text: "✕", onclick: () => removeAsset(a) })));
    const file = h("input", { type: "file", multiple: true, accept: "image/*,audio/*,.ttf,.otf,.woff,.woff2", hidden: true, onchange: async e => { await importFiles([...e.target.files]); e.target.value = ""; } });
    E.left.replaceChildren(
      h("div", { class: "sec-h" }, "Screens", h("button", { class: "mini", text: "+ Screen", onclick: addScreen })), ...screenRows,
      h("div", { class: "sec-h" }, isSplash ? "Splash sequence" : "Elements"),
      h("div", { class: "addrow" }, types.map(t => h("button", { class: "mini", text: "+ " + Schema.NODE[t].label, onclick: () => addNode(t) }))),
      ...(nodeRows.length ? nodeRows : [h("div", { class: "hint-sm", text: isSplash ? "No splash screens yet." : "Nothing on this screen yet." })]),
      h("div", { class: "sec-h" }, "Assets", h("button", { class: "mini", text: "+ Import", onclick: () => file.click() })), file,
      ...(assetRows.length ? assetRows : [h("div", { class: "hint-sm", text: "Import images, sounds (.wav .mp3 .ogg) and fonts (.ttf .otf .woff)." })]));
  }

  // ---- structure edits
  function addNode(type) {
    const sc = curScreen(), id = Schema.uid(type, sc.nodes.map(n => n.id)), node = { id, type }; sc.nodes.push(node);
    const over = type === "splash" ? {} : { y: Math.min(680, 200 + (sc.nodes.length - 1) % 8 * 60) }; if (type === "title") over.text = "Title"; if (type === "button") over.text = "Button";
    Schema.defineNode(S.project, sc.id, node, over); S.selId = id; S.tab = "props"; afterStructure();
  }
  function afterStructure() { markDirty(); S.tags.emit([]); renderLeft(); renderRight(); if (S.rt && !S.testing) { S.rt.selId = S.selId; S.rt.rebuild(); } }
  function deleteNode(n) { const sc = curScreen(); S.tags.removePrefix(sc.id + "." + n.id); sc.nodes.splice(sc.nodes.indexOf(n), 1); S.selId = null; afterStructure(); }
  function dupNode(n) {
    const sc = curScreen(), id = Schema.uid(n.type, sc.nodes.map(x => x.id)), pre = sc.id + "." + n.id + ".", np = sc.id + "." + id + ".";
    for (const k of Object.keys(S.project.tags)) if (k.startsWith(pre)) { const r = JSON.parse(JSON.stringify(S.project.tags[k])); r.owner = sc.id + "." + id; S.project.tags[np + k.slice(pre.length)] = r; }
    for (const ax of ["x", "y"]) { const r = S.project.tags[np + ax]; if (r) r.value = Math.min(ax === "x" ? 1280 : 720, r.value + 30); }
    sc.nodes.splice(sc.nodes.indexOf(n) + 1, 0, { id, type: n.type }); S.selId = id; afterStructure();
  }
  function moveNode(n, d) { const a = curScreen().nodes, i = a.indexOf(n), j = i + d; if (j < 0 || j >= a.length) return; [a[i], a[j]] = [a[j], a[i]]; afterStructure(); }
  function renameNode(n, raw) {
    const sc = curScreen(), id = Schema.clean(raw); if (!id || id === n.id || sc.nodes.some(x => x.id === id)) return renderRight();
    S.tags.renamePrefix(sc.id + "." + n.id, sc.id + "." + id); n.id = id; S.selId = id; afterStructure();
  }
  function addScreen() {
    const id = Schema.uid("screen", screens().map(s => s.id)), sc = { id, kind: "menu", nodes: [] }; S.project.screens.push(sc); Schema.defineScreen(S.project, sc);
    const n = { id: "title", type: "title" }; sc.nodes.push(n); Schema.defineNode(S.project, id, n, { text: id, y: 120 }); S.screenId = id; S.selId = null; afterStructure(); renderCenter();
  }
  function deleteScreen(sc) {
    const was = sc.id; S.tags.removePrefix(was); S.project.screens.splice(S.project.screens.indexOf(sc), 1);
    for (const k in S.project.tags) { const r = S.project.tags[k]; if (r.type === "screenRef" && r.value === was) r.value = ""; }
    S.screenId = (screens().find(s => s.id === "main") || screens()[0]).id; S.selId = null; afterStructure(); renderCenter();
  }
  function renameScreen(sc, raw) {
    const id = Schema.clean(raw); if (!id || id === sc.id || screens().some(s => s.id === id)) return renderRight();
    const old = sc.id; S.tags.renamePrefix(old, id); for (const k in S.project.tags) { const r = S.project.tags[k]; if (r.group === old) r.group = id; if (r.type === "screenRef" && r.value === old) r.value = id; }
    sc.id = id; S.screenId = id; afterStructure(); renderCenter();
  }

  // ---- assets
  async function importFiles(files) {
    for (const f of files) {
      const ext = (f.name.split(".").pop() || "").toLowerCase(), type = f.type.startsWith("image/") ? "image" : f.type.startsWith("audio/") || ["wav", "mp3", "ogg", "m4a", "flac"].includes(ext) ? "sound" : ["ttf", "otf", "woff", "woff2"].includes(ext) ? "font" : null; if (!type) continue;
      const id = Schema.uid(Schema.clean(f.name.replace(/\.[^.]+$/, "")) || type, S.project.assets.map(a => a.id)); await Store.putAsset(S.project.id, id, f);
      S.urls[id] = URL.createObjectURL(f); if (type === "font") await Store.registerFont(id, S.urls[id]); S.project.assets.push({ id, type, name: f.name });
    }
    markDirty(); renderLeft(); renderRight(); if (S.rt) S.rt.urls = S.urls, S.rt.rebuild();
  }
  async function removeAsset(a) {
    for (const k in S.project.tags) { const r = S.project.tags[k]; if ((r.type === "asset" || r.type === "font") && r.value === a.id) r.value = r.type === "font" ? "builtin:display" : ""; }
    S.project.assets.splice(S.project.assets.indexOf(a), 1); delete S.urls[a.id]; await Store.delAsset(S.project.id, a.id); afterStructure();
  }

  // =============================================================== right: inspector
  function renderRight() {
    const tabs = h("div", { class: "tabs", role: "tablist" }, [["props", "Properties"], ["tags", "Tags"]].map(([k, l]) => h("button", { class: "tab" + (S.tab === k ? " sel" : ""), role: "tab", "aria-selected": S.tab === k, text: l, onclick: () => { S.tab = k; renderRight(); } })));
    E.right.replaceChildren(tabs, S.tab === "tags" ? tagsBody() : propsBody());
  }
  function propsBody() {
    const sc = curScreen(), node = S.selId && sc.nodes.find(n => n.id === S.selId), box = h("div", { class: "insp" });
    if (node) {
      const idIn = h("input", { class: "idin", value: node.id, "aria-label": "Element name", onchange: e => renameNode(node, e.target.value) });
      box.append(h("div", { class: "insp-title" }, idIn), h("div", { class: "insp-sub", text: Schema.NODE[node.type].label + " on screen “" + sc.id + "”" }),
        h("div", { class: "btnrow" }, h("button", { class: "mini", text: sc.kind === "splash" ? "◀ Earlier" : "▼ Back", onclick: () => moveNode(node, -1) }), h("button", { class: "mini", text: sc.kind === "splash" ? "Later ▶" : "▲ Front", onclick: () => moveNode(node, 1) }), h("button", { class: "mini", text: "Duplicate", onclick: () => dupNode(node) }), h("button", { class: "mini danger", text: "Delete", onclick: () => deleteNode(node) })));
      for (const p of Schema.NODE[node.type].props) { if (p.showIf && S.tags.get(sc.id + "." + node.id + "." + p.k) === undefined) continue; if (p.showIf && S.tags.get(sc.id + "." + node.id + "." + p.showIf[0]) !== p.showIf[1]) continue; box.append(field(sc.id + "." + node.id + "." + p.k)); }
      if (node.type === "button") box.append(h("div", { class: "note", text: "Hover and click sounds play in Test mode and in the game. With a texture, only the visible (non-transparent) parts of the image react." }));
      if (node.type === "title" || node.type === "button" || node.type === "slider" || node.type === "toggle") box.append(h("div", { class: "note", text: "Tip: write {audio.master} in any text to show a tag's live value." }));
    } else {
      const idIn = h("input", { class: "idin", value: sc.id, "aria-label": "Screen name", onchange: e => renameScreen(sc, e.target.value) });
      box.append(h("div", { class: "insp-title" }, idIn), h("div", { class: "insp-sub", text: sc.kind === "splash" ? "Splash screens, played in order" : "Menu screen" }));
      for (const p of Schema.SCREEN[sc.kind]) box.append(field(sc.id + "." + p.k));
      const isStart = S.tags.get("project.startScreen") === sc.id;
      box.append(h("div", { class: "btnrow" }, h("button", { class: "mini", disabled: isStart, text: isStart ? "This is the start screen" : "Make start screen", onclick: () => { S.tags.set("project.startScreen", sc.id); renderLeft(); renderRight(); } }), sc.kind === "menu" ? h("button", { class: "mini danger", text: "Delete screen", onclick: () => deleteScreen(sc) }) : null));
      box.append(h("div", { class: "note", text: "Select an element in the preview or on the left to edit it. Every value you edit is a named tag; see the Tags tab." }));
    }
    return box;
  }

  // ---- one field for any tag, chosen by the tag's type
  function field(name, o) {
    o = o || {}; const t = S.tags.spec(name); if (!t) return h("div");
    const compact = !!o.compact, label = o.label || t.label || name, redraw = () => { if (!compact) renderRight(); };
    const set = v => S.tags.set(name, v), tagLine = compact ? null : h("div", { class: "tagname", text: name });
    const wrap = (...k) => h("div", { class: "field" + (compact ? " compact" : "") }, ...k);
    switch (t.type) {
      case "number": {
        const step = t.step || 1, dec = step >= 1 ? 0 : Math.min(3, Math.ceil(-Math.log10(step))), has = isFinite(t.min) && isFinite(t.max);
        const inp = h("input", { class: "num-in", type: "number", step, value: (+S.tags.get(name)).toFixed(dec), "aria-label": label }), rng = has && !compact ? h("input", { type: "range", min: t.min, max: t.max, step, value: S.tags.get(name), "aria-label": label + " slider" }) : null;
        inp.onchange = () => { let v = parseFloat(inp.value); if (!isFinite(v)) return; if (has) v = Math.max(t.min, Math.min(t.max, v)); set(v); inp.value = v.toFixed(dec); if (rng) rng.value = v; };
        if (rng) rng.oninput = () => { inp.value = (+rng.value).toFixed(dec); set(+rng.value); };
        return wrap(h("div", { class: "top" }, h("label", { text: label }), inp), rng, tagLine);
      }
      case "bool": return wrap(h("label", { class: "chk" }, h("input", { type: "checkbox", checked: !!S.tags.get(name), onchange: e => { set(e.target.checked); redraw(); } }), h("span", { text: label })), tagLine);
      case "color": return wrap(h("div", { class: "top" }, h("label", { text: label }), h("input", { type: "color", class: "col", value: S.tags.get(name), "aria-label": label, oninput: e => set(e.target.value) })), tagLine);
      case "string": return wrap(h("div", { class: "top" }, h("label", { text: label })), h("input", { class: "txt", type: "text", value: S.tags.get(name), "aria-label": label, oninput: e => set(e.target.value) }), tagLine);
      case "asset": case "font": case "choice": case "screenRef": case "tagRef": {
        let opts = [];
        if (t.type === "asset") opts = [["", "(none)"], ...S.project.assets.filter(a => a.type === t.assetType).map(a => [a.id, a.name])];
        else if (t.type === "font") opts = [...PRIM.BUILTIN_FONTS, ...S.project.assets.filter(a => a.type === "font").map(a => [a.id, a.name])];
        else if (t.type === "choice") opts = t.options;
        else if (t.type === "screenRef") opts = [["", "(none)"], ...screens().map(s => [s.id, s.id])];
        else opts = [["", "(pick a tag)"], ...S.tags.names().filter(n => S.tags.spec(n).type === t.refType).map(n => [n, S.tags.spec(n).label ? S.tags.spec(n).label + "  ·  " + n : n])];
        const sel = h("select", { class: "dd", "aria-label": label }, opts.map(([v, l]) => h("option", { value: v, text: l }))); sel.value = S.tags.get(name); if (sel.value !== String(S.tags.get(name))) sel.value = "";
        sel.onchange = () => { set(sel.value); redraw(); };
        const play = t.type === "asset" && t.assetType === "sound" && S.tags.get(name) && S.urls[S.tags.get(name)] ? h("button", { class: "mini", "aria-label": "Play sound", text: "▶", onclick: () => { const a = new Audio(S.urls[S.tags.get(name)]); a.volume = 0.7; a.play().catch(() => {}); } }) : null;
        return wrap(h("div", { class: "top" }, h("label", { text: label })), h("div", { class: "selrow" }, sel, play), tagLine);
      }
    }
    return wrap(h("label", { text: label }));
  }

  // ---- tags tab
  function tagsBody() {
    const box = h("div", { class: "insp" }), names = S.tags.names();
    const nn = h("input", { class: "txt", placeholder: "name, e.g. player.speed", "aria-label": "New tag name" }), ty = h("select", { class: "dd", "aria-label": "Type" }, [["number", "Number"], ["bool", "On / Off"], ["string", "Text"], ["color", "Color"]].map(([v, l]) => h("option", { value: v, text: l })));
    const mn = h("input", { class: "num-in", type: "number", value: 0, "aria-label": "Minimum" }), mx = h("input", { class: "num-in", type: "number", value: 1, "aria-label": "Maximum" }), err = h("div", { class: "note warn" });
    const add = () => {
      const name = nn.value.trim().toLowerCase(); err.textContent = "";
      if (!/^[a-z][a-z0-9_]*(\.[a-z0-9_]+)*$/.test(name)) { err.textContent = "Use lowercase letters, numbers, _ and dots (like player.speed)."; return; } if (S.tags.has(name)) { err.textContent = "That tag already exists."; return; }
      const type = ty.value, rec = { type, value: type === "number" ? 0 : type === "bool" ? false : type === "color" ? "#ffffff" : "", label: name, group: name.split(".")[0], custom: true };
      if (type === "number") { rec.min = +mn.value; rec.max = +mx.value; rec.step = (rec.max - rec.min) > 10 ? 1 : 0.01; rec.value = rec.min; } S.tags.define(name, rec); S.tags.emit([name]); renderRight();
    };
    box.append(h("div", { class: "group" }, h("div", { class: "sec-h", text: "New tag" }), h("div", { class: "field" }, nn), h("div", { class: "field" }, ty), h("div", { class: "field mm" }, h("label", { text: "Min" }), mn, h("label", { text: "Max" }), mx), h("div", { class: "btnrow" }, h("button", { class: "mini", text: "+ Add tag", onclick: add })), err));
    const filter = h("input", { class: "txt", type: "search", placeholder: "Filter tags…", value: S.filter, "aria-label": "Filter tags", oninput: e => { S.filter = e.target.value; list(); } }), listBox = h("div");
    box.append(h("div", { class: "field" }, filter), listBox);
    const list = () => {
      const f = S.filter.trim().toLowerCase(), groups = {}; for (const n of names.filter(n => !f || n.includes(f) || (S.tags.spec(n).label || "").toLowerCase().includes(f))) (groups[S.tags.spec(n).group || n.split(".")[0]] = groups[S.tags.spec(n).group || n.split(".")[0]] || []).push(n);
      listBox.replaceChildren(h("div", { class: "note", text: names.length + " tags" }), ...Object.keys(groups).sort().map(g => h("div", { class: "group" }, h("div", { class: "sec-h", text: g }), groups[g].map(n => tagRow(n)))));
    };
    list(); return box;
  }
  function tagRow(n) {
    const t = S.tags.spec(n);
    const nameEl = t.custom ? h("input", { class: "tname", value: n, "aria-label": "Tag name", onchange: e => { const nv = e.target.value.trim().toLowerCase(); if (/^[a-z][a-z0-9_]*(\.[a-z0-9_]+)*$/.test(nv) && S.tags.rename(n, nv)) { S.tags.t[nv].group = nv.split(".")[0]; } renderRight(); } }) : h("div", { class: "tname ro", title: t.builtin ? "built-in tag" : "named after its screen and element", text: n });
    return h("div", { class: "trow" }, h("div", { class: "thead" }, nameEl, t.custom ? h("button", { class: "mini", "aria-label": "Delete tag", text: "✕", onclick: () => { delete S.project.tags[n]; S.tags.emit([]); renderRight(); } }) : null), field(n, { compact: true }));
  }

  // =============================================================== play (fullscreen, remembers audio/graphics settings)
  function play() {
    saveNow(); const layer = h("div", { class: "play-layer" }); document.body.append(layer);
    const tg = S.tags.fork(), saved = Store.loadSettings(S.project.id); for (const k in saved) if (S.tags.has(k)) tg.ov[k] = saved[k];
    let start = S.tags.get("project.startScreen"); if (!screens().some(s => s.id === start)) start = screens()[0].id;
    const prev = S.rt; if (prev) prev.stopMusic();
    const rt = new PRIM.Runtime({ host: layer, project: S.project, tags: tg, mode: "play", urls: S.urls, screenId: start, onPersist: ov => Store.saveSettings(S.project.id, ov), onExit: close });
    function close() { rt.destroy(); layer.remove(); if (document.fullscreenElement) document.exitFullscreen().catch(() => {}); }
    layer.__rt = rt;
  }

  // =============================================================== boot
  window.__studio = { S, Store, openProject, renderManager };
  renderManager();
})();
