/* PRIMEngine editor v0.
   Left: scene tree + asset list.  Centre: live viewport.  Right: inspector.
   Edits change the project object in place; the game reads that object every frame, so changes show instantly.
   Saved to the browser automatically, and to a project.json file with the Save button. */
(function () {
  "use strict";
  const PRIM = window.PRIM, A = PRIM.Audio, G = PRIM.gfx, Game = PRIM.Game;
  const $ = id => document.getElementById(id);
  const PROJECT_URL = (new URLSearchParams(location.search).get("project")) || "projects/prime-test/project.json";
  const BASE = PROJECT_URL.replace(/[^/]*$/, "");

  const E = (PRIM.Editor = { project: null, sel: { kind: "object", id: "armCannon" }, assetsOk: {}, clip: null });
  let project, arm = null, dirty = false, saveTimer = 0, fileHandle = null, LS_KEY = "";
  let previewHandle = null, previewTimer = 0, phRaf = 0;

  // ---------- what each object exposes in the inspector ----------
  const SLOT_LABELS = {
    "armCannon.shot": "Shot", "armCannon.chargeStart": "Charge start", "armCannon.chargeLoop": "Charge loop", "armCannon.chargedShot": "Charged shot",
    "drone.shot": "Shot", "drone.hit": "Hit", "drone.death": "Death", "player.hurt": "Hurt", "door.open": "Open", "world.impact": "Impact", "world.pickup": "Pickup",
  };
  const SETTINGS = {
    player: [["player", "speed", "Move speed", 1, 12, 0.1], ["player", "sensitivity", "Look sensitivity", 0.2, 3, 0.05], ["player", "eye", "Eye height", 1, 2.2, 0.01], ["player", "maxEnergy", "Max energy", 10, 200, 1]],
    armCannon: [["beam", "fireDelay", "Fire delay (s)", 0.08, 1, 0.01], ["beam", "chargeTime", "Charge time (s)", 0.3, 4, 0.05], ["beam", "damage", "Shot damage", 0.5, 10, 0.5], ["beam", "chargedDamage", "Charged damage", 1, 20, 0.5], ["beam", "speed", "Shot speed", 10, 90, 1]],
    drone: [["enemy", "hp", "Health", 1, 20, 1], ["enemy", "speed", "Move speed", 0.5, 8, 0.1], ["enemy", "damage", "Damage", 1, 40, 1], ["enemy", "sight", "Sight range", 5, 50, 1]],
    world: [["display", "fov", "Field of view", 50, 110, 1], ["display", "pixel", "Pixel size", 1, 4, 1], ["display", "volume", "Master volume", 0, 1, 0.05]],
  };
  const ICON = { model: "▣", image: "▤", sound: "♪" };

  // ---------- tiny DOM helper ----------
  function h(tag, props, ...kids) {
    const n = document.createElement(tag);
    for (const k in (props || {})) { const v = props[k]; if (k === "class") n.className = v; else if (k === "text") n.textContent = v; else if (k.startsWith("on")) n.addEventListener(k.slice(2), v); else if (v !== false && v != null) n.setAttribute(k, v === true ? "" : v); }
    for (const c of kids.flat()) if (c != null) n.append(c.nodeType ? c : document.createTextNode(c));
    return n;
  }
  const fmt = (v, step) => { const d = step >= 1 ? 0 : Math.min(3, Math.ceil(-Math.log10(step))); return Number(v).toFixed(d); };

  // ---------- saving ----------
  function markDirty() {
    dirty = true; const s = $("saveState"); s.textContent = "unsaved changes"; s.classList.add("dirty");
    clearTimeout(saveTimer); saveTimer = setTimeout(autosave, 500);
  }
  function autosave() { try { localStorage.setItem(LS_KEY, JSON.stringify(project)); const s = $("saveState"); s.textContent = "autosaved in this browser"; } catch (e) {} }
  function markSaved(msg) { dirty = false; const s = $("saveState"); s.textContent = msg || "saved"; s.classList.remove("dirty"); }
  const json = () => JSON.stringify(project, null, 2);
  async function saveFile() {
    const text = json();
    try {
      if (window.showSaveFilePicker) {
        if (!fileHandle) fileHandle = await window.showSaveFilePicker({ suggestedName: "project.json", types: [{ description: "PRIMEngine project", accept: { "application/json": [".json"] } }] });
        const w = await fileHandle.createWritable(); await w.write(text); await w.close(); markSaved("saved to file"); return;
      }
    } catch (e) { if (e && e.name === "AbortError") return; }
    try {
      const a = h("a", { href: URL.createObjectURL(new Blob([text], { type: "application/json" })), download: "project.json" }); document.body.append(a); a.click(); a.remove(); markSaved("downloaded project.json");
    } catch (e) { copyJson(); }
  }
  async function copyJson() {
    const text = json();
    try { await navigator.clipboard.writeText(text); markSaved("copied to clipboard"); }
    catch (e) { const ta = h("textarea", { style: "position:fixed;opacity:0" }); ta.value = text; document.body.append(ta); ta.select(); let ok = false; try { ok = document.execCommand("copy"); } catch (x) {} ta.remove(); $("saveState").textContent = ok ? "copied to clipboard" : "copy blocked by the browser"; }
  }

  // ---------- loading ----------
  const loadImage = url => new Promise(res => { const im = new Image(); im.onload = () => res(im); im.onerror = () => res(null); im.src = url; });
  async function loadJson(url) { try { const r = await fetch(url); if (!r.ok) return null; return await r.json(); } catch (e) { return null; } }
  const assetById = id => project.assets.find(a => a.id === id);
  const assetUrl = a => BASE + a.file;

  async function boot() {
    const canvas = $("view");
    if (!G.init(canvas)) { $("projName").textContent = "WebGL is not available in this browser"; return; }
    A.init(0.8);
    let base = await loadJson(PROJECT_URL);
    if (!base) { $("projName").textContent = "could not load " + PROJECT_URL; return; }
    LS_KEY = "prime.project:" + PROJECT_URL;
    try { const saved = localStorage.getItem(LS_KEY); if (saved) { const p = JSON.parse(saved); if (p && p.format === base.format) { base = p; } } } catch (e) {}
    project = E.project = base;
    $("projName").textContent = project.name + "  ·  " + PROJECT_URL;

    // assets: anything missing is flagged instead of breaking the editor
    const imgs = {}; let armData = null;
    await Promise.all(project.assets.map(async a => {
      if (a.type === "image") { imgs[a.id] = await loadImage(assetUrl(a)); E.assetsOk[a.id] = !!imgs[a.id]; }
      else if (a.type === "sound") E.assetsOk[a.id] = await A.load(a.id, assetUrl(a));
      else if (a.type === "model") { const d = await loadJson(assetUrl(a)); E.assetsOk[a.id] = !!d; if (a.id === project.objects.armCannon.model) armData = d; }
    }));
    PRIM.Level.build();
    const am = project.objects.armCannon;
    if (armData) { try { arm = new PRIM.SkeletalModel(armData, imgs[am.texture] || null); } catch (e) { console.warn("arm model failed", e); } }
    const vis = project.ui && project.ui.visor && assetById(project.ui.visor.asset);
    Game.init({ project, arm, canvas, stage: $("stage"), visorUrl: vis && E.assetsOk[vis.id] ? assetUrl(vis) : null });
    Game.bindCanvas(); Game.setVisorVisible(!project.ui || !project.ui.visor || project.ui.visor.visible !== false);
    Game.S.P.yaw = -Math.PI / 2 + 0.28;
    E.arm = arm; E.imgs = imgs;
    Game.onExit = () => { renderAll(); };
    bindChrome(); renderAll();
    $("playBtn").disabled = false;
    $("saveState").textContent = "loaded";
  }

  // ---------- chrome ----------
  function bindChrome() {
    $("playBtn").onclick = () => { stopPreview(); A.resume(); Game.startPlay(); };
    $("saveBtn").onclick = saveFile; $("copyBtn").onclick = copyJson;
    addEventListener("resize", () => Game.resize());
    new ResizeObserver(() => Game.resize()).observe($("stage"));
    // drag to look around in the viewport
    const cv = $("view"); let drag = null;
    cv.addEventListener("pointerdown", e => { if (Game.mode !== "edit") return; drag = e.pointerId; cv.setPointerCapture(e.pointerId); A.resume(); });
    cv.addEventListener("pointermove", e => { if (drag === e.pointerId && Game.mode === "edit") Game.look(e.movementX, e.movementY); });
    const end = e => { if (drag === e.pointerId) drag = null; }; cv.addEventListener("pointerup", end); cv.addEventListener("pointercancel", end);
    addEventListener("beforeunload", e => { if (dirty) { try { autosave(); } catch (x) {} } });
    // test / debug hooks
    window.__prime = { Game, project, E, step: n => { for (let i = 0; i < (n || 1); i++) Game.update(1 / 60); }, get manual() { return Game.manual; }, set manual(v) { Game.manual = v; } };
  }

  // ---------- left panel ----------
  function renderTree() {
    const items = [["player", "Player", 0, "☺"], ["armCannon", "Arm cannon", 1, "▣"], ["visor", "Visor / HUD", 1, "▤"], ["drone", "Drone", 0, "◈"], ["door", "Door", 0, "▥"], ["world", "World", 0, "◫"]];
    const t = $("tree"); t.replaceChildren(...items.map(([id, label, ind, ic]) => {
      const sel = E.sel.kind === "object" && E.sel.id === id;
      return h("button", { class: "row-item" + (ind ? " indent" : "") + (sel ? " sel" : ""), "aria-pressed": sel, onclick: () => select("object", id) }, h("span", { class: "ic", text: ic }), h("span", { class: "nm", text: label }));
    }));
  }
  function renderAssets() {
    const box = $("assets"); box.replaceChildren();
    for (const type of ["model", "image", "sound"]) {
      const list = project.assets.filter(a => a.type === type); if (!list.length) continue;
      box.append(h("div", { class: "sec-h", text: type + "s" }));
      for (const a of list) {
        const sel = E.sel.kind === "asset" && E.sel.id === a.id, ok = E.assetsOk[a.id];
        box.append(h("button", { class: "row-item" + (sel ? " sel" : "") + (ok ? "" : " missing"), title: ok ? a.file : "missing file: " + a.file, onclick: () => select("asset", a.id) },
          h("span", { class: "ic", text: ICON[type] }), h("span", { class: "nm", text: a.label }), h("span", { class: "meta", text: ok ? (type === "sound" ? A.duration(a.id).toFixed(2) + "s" : "") : "missing" })));
      }
    }
  }
  function select(kind, id) { stopPreview(); E.sel = { kind, id }; renderAll(); }
  function renderAll() { renderTree(); renderAssets(); renderInspector(); }

  // ---------- inspector building blocks ----------
  function numField(label, get, set, min, max, step) {
    const out = h("input", { class: "num-in", type: "number", min, max, step, value: fmt(get(), step), "aria-label": label });
    const rng = h("input", { type: "range", min, max, step, value: get(), "aria-label": label + " slider" });
    const apply = v => { v = Number(v); if (!isFinite(v)) return; set(v); markDirty(); };
    rng.oninput = () => { out.value = fmt(rng.value, step); apply(rng.value); };
    out.onchange = () => { apply(out.value); rng.value = out.value; };
    return h("div", { class: "field" }, h("div", { class: "top" }, h("label", { text: label }), out), rng);
  }
  const group = (title, ...kids) => h("div", { class: "group" }, title ? h("div", { class: "sec-h", text: title }) : null, ...kids);
  const resetBtn = fn => h("button", { class: "mini", text: "Reset", onclick: fn });

  function settingsGroup(objId) {
    const rows = SETTINGS[objId]; if (!rows) return null;
    return group("Settings", rows.map(([sec, key, label, min, max, step]) => numField(label, () => project.settings[sec][key], v => {
      project.settings[sec][key] = v;
      if (sec === "display" && key === "pixel") Game.resize();
      if (sec === "display" && key === "volume") A.setVolume(v);
      if (sec === "player" && key === "eye" && Game.S.mode === "edit") Game.S.P.y = v;
    }, min, max, step)));
  }

  // ---------- arm cannon transform ----------
  const DEF_TRANSFORM = { position: [0.3, -0.265, -0.85], rotation: [2.3, 3.4, 0], scale: 1 };
  function transformGroup() {
    const t = project.objects.armCannon.transform;
    const rows = [
      ["Position X (right)", () => t.position[0], v => t.position[0] = v, -1, 1, 0.005],
      ["Position Y (up)", () => t.position[1], v => t.position[1] = v, -1, 1, 0.005],
      ["Position Z (forward)", () => -t.position[2], v => t.position[2] = -v, 0.2, 2, 0.005],
      ["Rotate X (pitch)", () => t.rotation[0], v => t.rotation[0] = v, -45, 45, 0.1],
      ["Rotate Y (turn)", () => t.rotation[1], v => t.rotation[1] = v, -45, 45, 0.1],
      ["Rotate Z (roll)", () => t.rotation[2], v => t.rotation[2] = v, -45, 45, 0.1],
      ["Scale", () => t.scale, v => t.scale = v, 0.3, 2.5, 0.01],
    ];
    const g = group("Position on the player", rows.map(r => numField(...r)),
      h("div", { class: "btnrow" }, resetBtn(() => { t.position = DEF_TRANSFORM.position.slice(); t.rotation = DEF_TRANSFORM.rotation.slice(); t.scale = DEF_TRANSFORM.scale; markDirty(); renderInspector(); })),
      h("div", { class: "note", text: "Meters, relative to the camera. The cannon is shown in the viewport as you edit." }));
    return g;
  }
  function animGroup() {
    if (!arm) return group("Animations", h("div", { class: "note warn", text: "The arm cannon model is missing, so there is nothing to preview." }));
    const cur = E.clip;
    return group("Animations", h("div", { class: "note", text: "Click to preview in the viewport." }),
      arm.clipNames().map(n => h("button", { class: "clip" + (cur === n ? " sel" : ""), onclick: () => { E.clip = n; Game.previewClip(n); renderInspector(); } }, h("span", { text: n }), h("span", { class: "meta", text: arm.anims[n].dur.toFixed(2) + "s" }))));
  }

  // ---------- audio slot editor ----------
  function slotsOf(objId) { const a = project.objects[objId] && project.objects[objId].audio; return a ? Object.keys(a) : []; }
  function audioGroup(objId) {
    const slots = slotsOf(objId); if (!slots.length) return null;
    const key = "slot:" + objId; if (!slots.includes(E[key])) E[key] = slots[0];
    const slot = E[key], b = project.objects[objId].audio[slot], full = objId + "." + slot;
    const tabs = h("div", { class: "tabs", role: "tablist" }, slots.map(s => h("button", { class: "tab" + (s === slot ? " sel" : ""), role: "tab", "aria-selected": s === slot, text: SLOT_LABELS[objId + "." + s] || s, onclick: () => { stopPreview(); E[key] = s; renderInspector(); } })));
    const sel = h("select", { class: "sel", "aria-label": "Sound" }, h("option", { value: "", text: "Built-in placeholder" }),
      project.assets.filter(a => a.type === "sound").map(a => h("option", { value: a.id, text: a.label + (E.assetsOk[a.id] ? "" : " (missing)") })));
    sel.value = b.sound || ""; sel.onchange = () => { stopPreview(); b.sound = sel.value || null; b.start = 0; b.end = 0; markDirty(); renderInspector(); };
    const hasSnd = b.sound && A.has(b.sound);
    const kids = [tabs, h("div", { class: "field" }, h("div", { class: "top" }, h("label", { text: "Sound" })), sel)];
    kids.push(numField("Volume", () => b.volume, v => b.volume = v, 0, 1.5, 0.01), numField("Pitch", () => b.pitch, v => b.pitch = v, 0.3, 2, 0.01));
    kids.push(h("label", { class: "chk" }, h("input", { type: "checkbox", checked: !!b.loop, onchange: e => { b.loop = e.target.checked; markDirty(); renderInspector(); } }), h("span", { text: "Loop" })));
    if (hasSnd) kids.push(waveEditor(b));
    else kids.push(h("div", { class: "note", text: b.sound ? "That sound file is missing, so the placeholder plays." : "No sound assigned. A built-in placeholder plays so nothing is silent." }));
    kids.push(h("div", { class: "btnrow" },
      h("button", { class: "mini", text: "▶ Preview", onclick: () => previewSlot(full, b) }), h("button", { class: "mini", text: "■ Stop", onclick: stopPreview }),
      resetBtn(() => { stopPreview(); b.volume = 1; b.pitch = 1; b.start = 0; b.end = 0; markDirty(); renderInspector(); })));
    return group("Audio", kids);
  }
  function previewSlot(full, b) {
    stopPreview(); A.resume();
    const hd = A.playSlot(project, full); previewHandle = hd; if (!hd) return;
    const dur = A.duration(b.sound), start = b.start || 0, end = b.end > start ? b.end : dur, span = (end - start) / (b.pitch || 1), t0 = performance.now();
    const ph = document.querySelector(".wave .ph");
    const tick = () => { const el = (performance.now() - t0) / 1000; if (!ph || !previewHandle) return;
      let k = b.loop ? (el % span) / span : Math.min(1, el / span); ph.style.display = "block"; ph.style.left = ((start + k * (end - start)) / dur * 100) + "%";
      if (b.loop || el < span + 0.1) phRaf = requestAnimationFrame(tick); else ph.style.display = "none"; };
    cancelAnimationFrame(phRaf); tick();
    if (!b.loop) previewTimer = setTimeout(() => { previewHandle = null; }, span * 1000 + 200);
  }
  function stopPreview() {
    cancelAnimationFrame(phRaf); clearTimeout(previewTimer);
    if (previewHandle) { try { previewHandle.stop(0.05); } catch (e) {} previewHandle = null; }
    const ph = document.querySelector(".wave .ph"); if (ph) ph.style.display = "none";
  }

  // waveform with draggable start / end markers
  function waveEditor(b) {
    const dur = A.duration(b.sound), wrap = h("div", { class: "wave" });
    const cv = h("canvas"), shL = h("div", { class: "shade" }), shR = h("div", { class: "shade" }), ph = h("div", { class: "ph" });
    const mkS = h("div", { class: "mk", tabindex: 0, role: "slider", "aria-label": "Start", "aria-valuemin": 0, "aria-valuemax": dur }), mkE = h("div", { class: "mk", tabindex: 0, role: "slider", "aria-label": "End", "aria-valuemin": 0, "aria-valuemax": dur });
    wrap.append(cv, shL, shR, ph, mkS, mkE);
    const info = h("div", { class: "wave-info" }), iS = h("span"), iL = h("span"), iE = h("span"); info.append(iS, iL, iE);
    const endV = () => (b.end > b.start ? Math.min(b.end, dur) : dur);
    const layout = () => {
      const s = (b.start || 0) / dur * 100, e = endV() / dur * 100;
      shL.style.left = 0; shL.style.width = s + "%"; shR.style.left = e + "%"; shR.style.width = (100 - e) + "%"; mkS.style.left = s + "%"; mkE.style.left = e + "%";
      mkS.setAttribute("aria-valuenow", (b.start || 0).toFixed(2)); mkE.setAttribute("aria-valuenow", endV().toFixed(2));
      iS.textContent = "start " + (b.start || 0).toFixed(2) + "s"; iL.textContent = "length " + (endV() - (b.start || 0)).toFixed(2) + "s"; iE.textContent = "end " + endV().toFixed(2) + "s";
    };
    const draw = () => {
      const w = Math.max(10, Math.floor(wrap.clientWidth)), hh = 84, dpr = window.devicePixelRatio || 1; cv.width = w * dpr; cv.height = hh * dpr;
      const c = cv.getContext("2d"); c.scale(dpr, dpr); c.clearRect(0, 0, w, hh);
      const pk = A.peaks(b.sound, w); if (!pk) return; c.fillStyle = "#2fb8d6";
      for (let i = 0; i < w; i++) { const v = Math.max(1, pk[i] * (hh - 8)); c.fillRect(i, (hh - v) / 2, 1, v); }
    };
    const setMarker = (which, clientX) => {
      const r = wrap.getBoundingClientRect(), t = Math.min(1, Math.max(0, (clientX - r.left) / r.width)) * dur, gap = 0.02;
      if (which === "s") b.start = Math.min(t, endV() - gap); else b.end = Math.max(t, (b.start || 0) + gap);
      if (b.end >= dur - 0.005) b.end = 0; layout(); markDirty();
    };
    for (const [mk, which] of [[mkS, "s"], [mkE, "e"]]) {
      mk.addEventListener("pointerdown", e => { mk.setPointerCapture(e.pointerId); mk._d = true; e.preventDefault(); });
      mk.addEventListener("pointermove", e => { if (mk._d) setMarker(which, e.clientX); });
      mk.addEventListener("pointerup", () => { mk._d = false; });
      mk.addEventListener("keydown", e => { const st = e.shiftKey ? 0.1 : 0.01, d = e.key === "ArrowLeft" ? -st : e.key === "ArrowRight" ? st : 0; if (!d) return; e.preventDefault();
        if (which === "s") b.start = Math.min(endV() - 0.02, Math.max(0, (b.start || 0) + d)); else { b.end = Math.max((b.start || 0) + 0.02, Math.min(dur, endV() + d)); if (b.end >= dur - 0.005) b.end = 0; } layout(); markDirty(); });
    }
    layout(); requestAnimationFrame(() => { draw(); layout(); });
    return h("div", null, wrap, info, h("div", { class: "note", text: b.loop ? "Drag the markers to choose the loop region." : "Drag the markers to trim the sound." }));
  }

  // ---------- inspector pages ----------
  function renderInspector() {
    const box = $("inspector"), s = E.sel; box.replaceChildren();
    if (s.kind === "asset") { renderAssetInspector(box, assetById(s.id)); return; }
    if (s.id === "visor") {
      const v = project.ui.visor, a = assetById(v.asset), ok = E.assetsOk[v.asset];
      box.append(h("div", { class: "insp-title", text: "Visor / HUD" }), h("div", { class: "insp-sub", text: "attached to the player's screen" }),
        group("Display", h("label", { class: "chk" }, h("input", { type: "checkbox", checked: v.visible !== false, onchange: e => { v.visible = e.target.checked; Game.setVisorVisible(v.visible); markDirty(); } }), h("span", { text: "Show visor frame" })),
          h("div", { class: "note", text: ok ? "Visor image: " + a.label : "The visor image is missing, so the frame is hidden." }),
          ok ? h("div", { class: "preview-box" }, h("img", { src: assetUrl(a), alt: "Visor frame" })) : null,
          h("div", { class: "note", text: "Energy, hostiles and the charge reticle are built in for now. A HUD designer comes later." })));
      return;
    }
    const o = project.objects[s.id];
    box.append(h("div", { class: "insp-title", text: o.label }), h("div", { class: "insp-sub", text: o.attachedTo ? "attached to the " + o.attachedTo : "object" }));
    if (s.id === "armCannon") { box.append(transformGroup()); box.append(animGroup()); }
    box.append(audioGroup(s.id) || ""); box.append(settingsGroup(s.id) || "");
  }
  function renderAssetInspector(box, a) {
    const ok = E.assetsOk[a.id], used = [];
    for (const oid in project.objects) { const au = project.objects[oid].audio || {}; for (const sl in au) if (au[sl].sound === a.id) used.push((project.objects[oid].label) + " · " + (SLOT_LABELS[oid + "." + sl] || sl)); }
    box.append(h("div", { class: "insp-title", text: a.label }), h("div", { class: "insp-sub", text: a.type + " · " + a.file }));
    if (!ok) { box.append(group("", h("div", { class: "note warn", text: "This file was not found. Put it at " + BASE + a.file + " and reload." }))); return; }
    if (a.type === "image") box.append(group("Preview", h("div", { class: "preview-box" }, h("img", { src: assetUrl(a), alt: a.label }))));
    if (a.type === "sound") {
      const b = { sound: a.id, volume: 1, pitch: 1, loop: false, start: 0, end: 0 };
      box.append(group("Preview", waveEditorReadonly(a.id), h("div", { class: "btnrow" }, h("button", { class: "mini", text: "▶ Play", onclick: () => { stopPreview(); A.resume(); previewHandle = A.playBinding(b); } }), h("button", { class: "mini", text: "■ Stop", onclick: stopPreview })),
        h("div", { class: "note", text: "Length " + A.duration(a.id).toFixed(2) + " s" })));
    }
    if (a.type === "model" && arm) box.append(animGroup());
    if (a.type !== "model") box.append(group("Used by", h("div", { class: "note", text: used.length ? used.join("\n") : "Nothing yet." })));
  }
  function waveEditorReadonly(id) {
    const cv = h("canvas"), wrap = h("div", { class: "wave" }, cv);
    requestAnimationFrame(() => { const w = Math.max(10, Math.floor(wrap.clientWidth)), hh = 84, dpr = window.devicePixelRatio || 1; cv.width = w * dpr; cv.height = hh * dpr;
      const c = cv.getContext("2d"); c.scale(dpr, dpr); const pk = A.peaks(id, w); if (!pk) return; c.fillStyle = "#2fb8d6"; for (let i = 0; i < w; i++) { const v = Math.max(1, pk[i] * (hh - 8)); c.fillRect(i, (hh - v) / 2, 1, v); } });
    return wrap;
  }

  boot().catch(e => { console.error(e); $("projName").textContent = "editor failed to start: " + e.message; });
})();
