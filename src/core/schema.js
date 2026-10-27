/* PRIMEngine project schema: which properties each screen / element has. Every property becomes a tag. */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), S = (PRIM.Schema = {});
  const FONT = { k: "font", label: "Font", type: "font", def: "builtin:display" };
  const X = { k: "x", label: "X", type: "number", def: 640, min: 0, max: 1280, step: 1 }, Y = { k: "y", label: "Y", type: "number", def: 360, min: 0, max: 720, step: 1 };
  const SIZE = d => ({ k: "size", label: "Text size", type: "number", def: d, min: 8, max: 300, step: 1 });
  const COLOR = (k, label, d) => ({ k, label, type: "color", def: d });
  const W = (label, d) => ({ k: "w", label: label || "Width (0 = auto)", type: "number", def: d || 0, min: 0, max: 1280, step: 1 });
  const H = { k: "h", label: "Height (0 = auto)", type: "number", def: 0, min: 0, max: 720, step: 1 };
  const OPACITY = { k: "opacity", label: "Opacity", type: "number", def: 1, min: 0, max: 1, step: 0.05 };
  const FADES = [["fade", "Fade"], ["zoom", "Zoom"], ["slide", "Slide up"], ["blur", "Blur"], ["cut", "Cut (instant)"]];

  S.NODE = {
    title: { label: "Title", icon: "T", props: [{ k: "text", label: "Text", type: "string", def: "Title" }, FONT, SIZE(64), COLOR("color", "Color", "#ffffff"), X, Y, OPACITY] },
    image: { label: "Image", icon: "▤", props: [{ k: "asset", label: "Image", type: "asset", assetType: "image", def: "" }, X, Y, W("Width (0 = natural)"), H, OPACITY] },
    button: { label: "Button", icon: "▭", props: [
      { k: "mode", label: "Look", type: "choice", options: [["text", "Text"], ["texture", "Texture (image is the hit-box)"]], def: "text" },
      { k: "text", label: "Text", type: "string", def: "Button", showIf: ["mode", "text"] }, Object.assign({}, FONT, { showIf: ["mode", "text"] }), Object.assign({}, SIZE(36), { showIf: ["mode", "text"] }),
      Object.assign(COLOR("color", "Color", "#d9f7ff"), { showIf: ["mode", "text"] }), Object.assign(COLOR("hoverColor", "Hover color", "#7df3ff"), { showIf: ["mode", "text"] }),
      { k: "texture", label: "Texture", type: "asset", assetType: "image", def: "", showIf: ["mode", "texture"] }, { k: "hoverTexture", label: "Hover texture (optional)", type: "asset", assetType: "image", def: "", showIf: ["mode", "texture"] },
      X, Y, Object.assign({}, W("Width (0 = natural)"), { showIf: ["mode", "texture"] }), Object.assign({}, H, { label: "Height (0 = natural)", showIf: ["mode", "texture"] }),
      { k: "hoverSound", label: "Hover sound", type: "asset", assetType: "sound", def: "" }, { k: "clickSound", label: "Click sound", type: "asset", assetType: "sound", def: "" },
      { k: "action", label: "On click", type: "choice", options: [["none", "Nothing"], ["goto", "Go to screen"], ["start", "Start game"], ["quit", "Quit"]], def: "none" },
      { k: "target", label: "Screen", type: "screenRef", def: "", showIf: ["action", "goto"] }] },
    slider: { label: "Slider", icon: "◐", props: [{ k: "label", label: "Label", type: "string", def: "Value" }, { k: "bind", label: "Controls tag", type: "tagRef", refType: "number", def: "" }, FONT, SIZE(26), COLOR("color", "Color", "#d9f7ff"), COLOR("accent", "Accent", "#7df3ff"), X, Y, W("Width", 420)] },
    toggle: { label: "Toggle", icon: "◉", props: [{ k: "label", label: "Label", type: "string", def: "Option" }, { k: "bind", label: "Controls tag", type: "tagRef", refType: "bool", def: "" }, FONT, SIZE(26), COLOR("color", "Color", "#d9f7ff"), COLOR("accent", "Accent", "#7df3ff"), X, Y, W("Width", 420)] },
    bar: { label: "Bar", icon: "▬", props: [{ k: "bind", label: "Shows tag", type: "tagRef", refType: "number", def: "" }, { k: "max", label: "Full when tag (optional)", type: "tagRef", refType: "number", def: "" }, { k: "label", label: "Label", type: "string", def: "" }, FONT, SIZE(18),
      COLOR("textColor", "Label color", "#ffffff"), COLOR("color", "Fill color", "#ff4a4a"), COLOR("bg", "Back color", "#2a0d0d"), X, Y, W("Width", 260), { k: "h", label: "Height", type: "number", def: 22, min: 4, max: 200, step: 1 }, OPACITY] },
    splash: { label: "Splash", icon: "✦", props: [
      { k: "kind", label: "Shows", type: "choice", options: [["text", "Text"], ["image", "Image"]], def: "text" },
      { k: "text", label: "Text", type: "string", def: "Made with PRIMEngine", showIf: ["kind", "text"] }, Object.assign({}, FONT, { showIf: ["kind", "text"] }), Object.assign({}, SIZE(56), { showIf: ["kind", "text"] }), Object.assign(COLOR("color", "Color", "#ffffff"), { showIf: ["kind", "text"] }),
      { k: "asset", label: "Image", type: "asset", assetType: "image", def: "", showIf: ["kind", "image"] }, Object.assign({}, W("Width (0 = natural)"), { showIf: ["kind", "image"] }), Object.assign({}, H, { label: "Height (0 = natural)", showIf: ["kind", "image"] }),
      { k: "duration", label: "Length (s)", type: "number", def: 3, min: 0.5, max: 30, step: 0.1 },
      { k: "fadeIn", label: "Fade in (s)", type: "number", def: 0.8, min: 0, max: 5, step: 0.05 }, { k: "fadeInType", label: "Fade in style", type: "choice", options: FADES, def: "fade" },
      { k: "fadeOut", label: "Fade out (s)", type: "number", def: 0.8, min: 0, max: 5, step: 0.05 }, { k: "fadeOutType", label: "Fade out style", type: "choice", options: FADES, def: "fade" }] },
  };
  S.SCREEN = {
    menu: [COLOR("bg", "Background color", "#030a12"), { k: "bgImage", label: "Background image", type: "asset", assetType: "image", def: "" }, { k: "music", label: "Music", type: "asset", assetType: "sound", def: "" }],
    hud: [],
    splash: [COLOR("bg", "Background color", "#000000"), { k: "skippable", label: "Click or key skips", type: "bool", def: true }, { k: "next", label: "Then go to", type: "screenRef", def: "main" }],
  };

  const rec = (p, group, owner) => { const r = { type: p.type, value: p.def, label: p.label, group, owner }; for (const k of ["min", "max", "step", "options", "assetType", "refType", "showIf"]) if (p[k] !== undefined) r[k] = p[k]; r.prop = p.k; return r; };
  S.defineNode = (project, sid, node, over) => { for (const p of S.NODE[node.type].props) { const r = rec(p, sid, sid + "." + node.id); if (over && p.k in over) r.value = over[p.k]; project.tags[sid + "." + node.id + "." + p.k] = r; } };
  S.defineScreen = (project, sc, over) => { for (const p of S.SCREEN[sc.kind]) { const r = rec(p, sc.id, sc.id); if (over && p.k in over) r.value = over[p.k]; project.tags[sc.id + "." + p.k] = r; } };
  S.uid = (base, taken) => { let i = 1, id = base; while (taken.includes(id)) id = base + ++i; return id; };
  S.clean = s => String(s || "").toLowerCase().replace(/[^a-z0-9_]+/g, "_").replace(/^_+|_+$/g, "").replace(/^(\d)/, "_$1");

  // ---- 3D maps. Geometry (where the nodes are, which nodes make a sector) is structure; every other value is a tag.
  // Units: 64 units is one standard wall tile. Eye height of the player is 56.
  const TEX = (k, label) => ({ k, label, type: "asset", assetType: "image", def: "" });
  S.MAP = [
    COLOR("skyColor", "Sky color", "#16243a"), COLOR("fogColor", "Fog color", "#06090e"), { k: "fogDistance", label: "Fog distance", type: "number", def: 1400, min: 200, max: 6000, step: 50 },
    { k: "startX", label: "Player start X", type: "number", def: 0, min: -4096, max: 4096, step: 1 }, { k: "startY", label: "Player start Y", type: "number", def: 0, min: -4096, max: 4096, step: 1 },
    { k: "startAngle", label: "Player start angle", type: "number", def: 90, min: 0, max: 360, step: 1 }, { k: "gridSize", label: "Grid size", type: "number", def: 32, min: 8, max: 128, step: 8 },
  ];
  S.SECTOR = [
    { k: "floor", label: "Floor height", type: "number", def: 0, min: -512, max: 1024, step: 8 }, { k: "ceil", label: "Ceiling height", type: "number", def: 128, min: -512, max: 1024, step: 8 },
    { k: "light", label: "Light", type: "number", def: 1, min: 0, max: 1, step: 0.05 }, TEX("floorTex", "Floor texture"), COLOR("floorColor", "Floor color", "#6a6a74"), TEX("ceilTex", "Ceiling texture"), COLOR("ceilColor", "Ceiling color", "#42424c"),
  ];
  S.VERT = [TEX("tex", "Wall texture"), COLOR("color", "Wall color", "#a89c8a"), { k: "open", label: "Open (no wall)", type: "bool", def: false }];
  const defP = (project, list, group, owner, id, over) => { for (const p of list) { const r = rec(p, group, owner); if (over && p.k in over) r.value = over[p.k]; project.tags[id + "." + p.k] = r; } };
  S.defineMap = (project, m, over) => defP(project, S.MAP, m.id, m.id, m.id, over);
  S.defineSector = (project, m, sec, over) => defP(project, S.SECTOR, m.id, m.id + "." + sec.id, m.id + "." + sec.id, over);
  S.defineVert = (project, m, v, over) => defP(project, S.VERT, m.id, m.id + "." + v.id, m.id + "." + v.id, over);
  // adds a vertex / sector made of points [[x,y],...]; returns the sector
  S.addVert = (project, m, x, y, over) => { const v = { id: "v" + (++m.n.v), x, y }; m.verts.push(v); S.defineVert(project, m, v, over); return v; };
  S.addSector = (project, m, ids, over) => { const sec = { id: "s" + (++m.n.s), v: ids.slice() }; m.sectors.push(sec); S.defineSector(project, m, sec, over); return sec; };
  S.newMap = (project, id, style) => {
    const m = { id, style: style || "doom", verts: [], sectors: [], n: { v: 0, s: 0 } }; project.maps.push(m); S.defineMap(project, m);
    const ids = [[-128, -128], [128, -128], [128, 128], [-128, 128]].map(([x, y]) => S.addVert(project, m, x, y).id); S.addSector(project, m, ids); return m;
  };
  S.removeSector = (project, m, sec) => { const used = new Set(); for (const o of m.sectors) if (o !== sec) o.v.forEach(i => used.add(i)); m.sectors.splice(m.sectors.indexOf(sec), 1);
    for (const i of sec.v) if (!used.has(i)) { m.verts.splice(m.verts.findIndex(v => v.id === i), 1); for (const k of Object.keys(project.tags)) if (k.startsWith(m.id + "." + i + ".")) delete project.tags[k]; }
    for (const k of Object.keys(project.tags)) if (k.startsWith(m.id + "." + sec.id + ".")) delete project.tags[k]; };

  // ---- the player (one entity per project). Every value is a tag named player.*
  S.PLAYER = [
    { k: "speed", label: "Move speed", type: "number", def: 280, min: 40, max: 1200, step: 10 }, { k: "gravity", label: "Gravity", type: "number", def: 1100, min: 0, max: 4000, step: 50 },
    { k: "jumpEnabled", label: "Jumping", type: "bool", def: true }, { k: "jumpHeight", label: "Jump height", type: "number", def: 48, min: 8, max: 240, step: 2, showIf: ["jumpEnabled", true] },
    { k: "sprintEnabled", label: "Sprinting", type: "bool", def: true }, { k: "sprintPercent", label: "Sprint speed bonus %", type: "number", def: 60, min: 0, max: 300, step: 5, showIf: ["sprintEnabled", true] },
    { k: "eyeHeight", label: "Eye height", type: "number", def: 56, min: 16, max: 160, step: 1 }, { k: "radius", label: "Body width (collision)", type: "number", def: 16, min: 4, max: 48, step: 1 }, { k: "stepHeight", label: "Step height", type: "number", def: 24, min: 0, max: 64, step: 1 },
    { k: "fov", label: "Field of view", type: "number", def: 80, min: 40, max: 120, step: 1 }, { k: "aimMode", label: "Aim mode", type: "choice", options: [["classic", "Classic (normal FPS aim)"], ["free", "Free aim (bodycam style)"]], def: "classic" },
    { k: "freeAimRadius", label: "Free aim: aim area (degrees)", type: "number", def: 14, min: 3, max: 45, step: 1, showIf: ["aimMode", "free"] }, { k: "mouseSens", label: "Mouse sensitivity", type: "number", def: 1, min: 0.1, max: 4, step: 0.05 },
    { k: "maxHealth", label: "Health", type: "number", def: 100, min: 1, max: 1000, step: 1 },
    { k: "crosshair", label: "Show crosshair", type: "bool", def: true }, { k: "crosshairSize", label: "Crosshair size", type: "number", def: 10, min: 2, max: 60, step: 1, showIf: ["crosshair", true] }, { k: "crosshairGap", label: "Crosshair gap", type: "number", def: 4, min: 0, max: 30, step: 1, showIf: ["crosshair", true] },
    { k: "crosshairThickness", label: "Crosshair thickness", type: "number", def: 2, min: 1, max: 8, step: 1, showIf: ["crosshair", true] }, Object.assign(COLOR("crosshairColor", "Crosshair color", "#ffffff"), { showIf: ["crosshair", true] }),
    { k: "hud", label: "HUD screen", type: "screenRef", def: "hud" },
  ];
  S.PLAYER_GROUPS = [["Movement", ["speed", "sprintEnabled", "sprintPercent", "jumpEnabled", "jumpHeight", "gravity"]], ["Body", ["maxHealth", "eyeHeight", "radius", "stepHeight"]], ["Camera", ["fov", "aimMode", "freeAimRadius", "mouseSens"]], ["Crosshair", ["crosshair", "crosshairSize", "crosshairGap", "crosshairThickness", "crosshairColor"]], ["HUD", ["hud"]]];
  S.ensurePlayer = project => {
    project.player = project.player || { attachments: [] }; if (!project.player.attachments) project.player.attachments = [];
    delete project.tags["player.freeAim"];
    for (const p of S.PLAYER) if (!project.tags["player." + p.k]) { const r = rec(p, "player", "player"); r.builtin = true; project.tags["player." + p.k] = r; }
    if (!project.tags["player.health"]) project.tags["player.health"] = { type: "number", value: 100, label: "Current health (live)", group: "player", min: 0, max: 1000, step: 1, builtin: true };
    if (!project.screens.some(s => s.id === "hud") && project.tags["player.hud"].value === "hud") {
      const sc = { id: "hud", kind: "hud", nodes: [] }; project.screens.push(sc); S.defineScreen(project, sc);
      const add = (id, type, over) => { const n = { id, type }; sc.nodes.push(n); S.defineNode(project, "hud", n, over); };
      add("healthbar", "bar", { bind: "player.health", max: "player.maxHealth", label: "HEALTH", x: 190, y: 674 }); add("healthtext", "title", { text: "{player.health}", size: 34, x: 380, y: 672, color: "#ffd9d9" });
    }
  };

  S.builtins = project => {
    const B = (n, type, value, label, group, extra) => { project.tags[n] = Object.assign({ type, value, label, group, builtin: true }, extra); };
    B("audio.master", "number", 0.8, "Master volume", "audio", { min: 0, max: 1, step: 0.01 });
    B("audio.sfx", "number", 1, "SFX volume", "audio", { min: 0, max: 1, step: 0.01 });
    B("audio.music", "number", 0.7, "Music volume", "audio", { min: 0, max: 1, step: 0.01 });
    B("gfx.fullscreen", "bool", false, "Fullscreen", "gfx");
    B("gfx.brightness", "number", 1, "Brightness", "gfx", { min: 0.5, max: 1.5, step: 0.05 });
    B("gfx.showFps", "bool", false, "Show FPS counter", "gfx");
    B("project.startScreen", "screenRef", "splash", "Start screen", "project");
  };

  // a brand-new project: splash -> main menu ("My Game") -> settings
  S.template = (id, name) => {
    const p = { format: 3, id, name, modified: Date.now(), tags: {}, assets: [], folders: [], maps: [], screens: [] };
    S.builtins(p);
    const add = (sc, id, type, over) => { const n = { id, type }; sc.nodes.push(n); S.defineNode(p, sc.id, n, over); };
    const splash = { id: "splash", kind: "splash", nodes: [] }, main = { id: "main", kind: "menu", nodes: [] }, set = { id: "settings", kind: "menu", nodes: [] };
    p.screens.push(splash, main, set);
    S.defineScreen(p, splash); S.defineScreen(p, main); S.defineScreen(p, set);
    add(splash, "intro", "splash", { text: "Made with PRIMEngine" });
    add(main, "title", "title", { text: "My Game", size: 104, y: 190 });
    add(main, "play", "button", { text: "Play", y: 380, action: "start" });
    add(main, "settings", "button", { text: "Settings", y: 450, action: "goto", target: "settings" });
    add(main, "quit", "button", { text: "Quit", y: 520, action: "quit" });
    add(set, "title", "title", { text: "Settings", size: 64, y: 80 });
    [["master", "Master volume", "audio.master", 170], ["sfx", "SFX volume", "audio.sfx", 235], ["music", "Music volume", "audio.music", 300], ["brightness", "Brightness", "gfx.brightness", 365]].forEach(([i, l, b, y]) => add(set, i, "slider", { label: l, bind: b, y }));
    add(set, "fullscreen", "toggle", { label: "Fullscreen", bind: "gfx.fullscreen", y: 450 });
    add(set, "showfps", "toggle", { label: "Show FPS", bind: "gfx.showFps", y: 505 });
    add(set, "back", "button", { text: "Back", y: 620, action: "goto", target: "main" });
    S.ensurePlayer(p);
    return p;
  };
})();
