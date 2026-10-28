/* PRIMEngine project schema: which properties each screen / element has. Every property becomes a tag. */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), S = (PRIM.Schema = {});
  const FONT = { k: "font", label: "Font", type: "font", def: "builtin:display" };
  // positions are measured from the MIDDLE of the screen: x0,y0 = middle, x grows to the right, y grows upward
  const X = { k: "x", label: "X (0 = middle)", type: "number", def: 0, min: -640, max: 640, step: 1 }, Y = { k: "y", label: "Y (0 = middle, up is +)", type: "number", def: 0, min: -360, max: 360, step: 1 };
  const SIZE = d => ({ k: "size", label: "Text size", type: "number", def: d, min: 8, max: 300, step: 1 });
  const COLOR = (k, label, d) => ({ k, label, type: "color", def: d });
  const W = (label, d) => ({ k: "w", label: label || "Width (0 = auto)", type: "number", def: d || 0, min: 0, max: 1280, step: 1 });
  const H = { k: "h", label: "Height (0 = auto)", type: "number", def: 0, min: 0, max: 720, step: 1 };
  const LOCK = extra => Object.assign({ k: "lock", label: "Keep proportions (width and height move together)", type: "bool", def: true }, extra || {});
  const OPACITY = { k: "opacity", label: "Opacity", type: "number", def: 1, min: 0, max: 1, step: 0.05 };
  const FADES = [["fade", "Fade"], ["zoom", "Zoom"], ["slide", "Slide up"], ["blur", "Blur"], ["cut", "Cut (instant)"]];

  S.NODE = {
    title: { label: "Title", icon: "T", props: [{ k: "text", label: "Text", type: "string", def: "Title" }, FONT, SIZE(64), COLOR("color", "Color", "#ffffff"), X, Y, OPACITY] },
    image: { label: "Image", icon: "▤", props: [{ k: "asset", label: "Image", type: "asset", assetType: "image", def: "" }, X, Y, W("Width (0 = natural)"), H, LOCK(), { k: "stretch", label: "Stretch to screen (fills the whole screen, ignores position and size)", type: "bool", def: false }, OPACITY] },
    button: { label: "Button", icon: "▭", props: [
      { k: "mode", label: "Look", type: "choice", options: [["text", "Text"], ["texture", "Texture (image is the hit-box)"]], def: "text" },
      { k: "text", label: "Text", type: "string", def: "Button", showIf: ["mode", "text"] }, Object.assign({}, FONT, { showIf: ["mode", "text"] }), Object.assign({}, SIZE(36), { showIf: ["mode", "text"] }),
      Object.assign(COLOR("color", "Color", "#d9f7ff"), { showIf: ["mode", "text"] }), Object.assign(COLOR("hoverColor", "Hover color", "#7df3ff"), { showIf: ["mode", "text"] }),
      { k: "texture", label: "Texture", type: "asset", assetType: "image", def: "", showIf: ["mode", "texture"] }, { k: "hoverTexture", label: "Hover texture (optional)", type: "asset", assetType: "image", def: "", showIf: ["mode", "texture"] },
      X, Y, Object.assign({}, W("Width (0 = natural)"), { showIf: ["mode", "texture"] }), Object.assign({}, H, { label: "Height (0 = natural)", showIf: ["mode", "texture"] }), LOCK({ showIf: ["mode", "texture"] }),
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

  const rec = (p, group, owner) => { const r = { type: p.type, value: p.def, label: p.label, group, owner }; for (const k of ["min", "max", "step", "options", "assetType", "refType", "showIf", "modelTag"]) if (p[k] !== undefined) r[k] = p[k]; r.prop = p.k; return r; };
  S.defineNode = (project, sid, node, over) => { for (const p of S.NODE[node.type].props) { const r = rec(p, sid, sid + "." + node.id); if (over && p.k in over) r.value = over[p.k]; project.tags[sid + "." + node.id + "." + p.k] = r; } };
  S.defineScreen = (project, sc, over) => { for (const p of S.SCREEN[sc.kind]) { const r = rec(p, sc.id, sc.id); if (over && p.k in over) r.value = over[p.k]; project.tags[sc.id + "." + p.k] = r; } };
  S.uid = (base, taken) => { let i = 1, id = base; while (taken.includes(id)) id = base + ++i; return id; };
  S.clean = s => String(s || "").toLowerCase().replace(/[^a-z0-9_]+/g, "_").replace(/^_+|_+$/g, "").replace(/^(\d)/, "_$1");

  // ---- 3D maps. Geometry (where the nodes are, which nodes make a sector) is structure; every other value is a tag.
  // Units: 32 units = one tile = 0.5 m (so 64 units = 1 m). A wall panel is 3 x 3 tiles = 96 x 96 units = 1.5 m. Player eye height is 102 (1.6 m).
  const TEX = (k, label) => ({ k, label, type: "asset", assetType: "image", def: "" });
  S.MAP = [
    COLOR("skyColor", "Sky color", "#16243a"), COLOR("fogColor", "Fog color", "#06090e"), { k: "fogDistance", label: "Fog distance", type: "number", def: 1400, min: 200, max: 6000, step: 50 },
    { k: "startX", label: "Player start X", type: "number", def: 0, min: -4096, max: 4096, step: 1 }, { k: "startY", label: "Player start Y", type: "number", def: 0, min: -4096, max: 4096, step: 1 },
    { k: "startAngle", label: "Player start angle", type: "number", def: 90, min: 0, max: 360, step: 1 }, { k: "gridSize", label: "Tile size (32 units = 0.5 m)", type: "number", def: 32, min: 8, max: 128, step: 8 }, { k: "wallThickness", label: "Wall thickness (16 units = 0.25 m)", type: "number", def: 16, min: 4, max: 64, step: 2 },
  ];
  S.SECTOR = [
    { k: "floor", label: "Floor height", type: "number", def: 0, min: -512, max: 1024, step: 8 }, { k: "ceil", label: "Ceiling height (96 = 1.5 m)", type: "number", def: 192, min: -512, max: 1024, step: 8 },
    { k: "light", label: "Light", type: "number", def: 1, min: 0, max: 1, step: 0.05 }, TEX("floorTex", "Floor texture"), COLOR("floorColor", "Floor color", "#6a6a74"), TEX("ceilTex", "Ceiling texture"), COLOR("ceilColor", "Ceiling color", "#42424c"),
  ];
  S.VERT = [TEX("tex", "Wall texture"), COLOR("color", "Wall color", "#a89c8a"), { k: "open", label: "Open (no wall)", type: "bool", def: false }, { k: "rows", label: "Wall layers (wall types, bottom to top)", type: "string", def: "" }, { k: "rowsRepeat", label: "Repeat the top layer upward", type: "bool", def: true }];
  const defP = (project, list, group, owner, id, over) => { for (const p of list) { const r = rec(p, group, owner); if (over && p.k in over) r.value = over[p.k]; project.tags[id + "." + p.k] = r; } };
  S.defineMap = (project, m, over) => defP(project, S.MAP, m.id, m.id, m.id, over);
  S.defineSector = (project, m, sec, over) => defP(project, S.SECTOR, m.id, m.id + "." + sec.id, m.id + "." + sec.id, over);
  S.defineVert = (project, m, v, over) => defP(project, S.VERT, m.id, m.id + "." + v.id, m.id + "." + v.id, over);
  // adds a vertex / sector made of points [[x,y],...]; returns the sector
  S.addVert = (project, m, x, y, over) => { const v = { id: "v" + (++m.n.v), x, y }; m.verts.push(v); S.defineVert(project, m, v, over); return v; };
  S.addSector = (project, m, ids, over) => { const sec = { id: "s" + (++m.n.s), v: ids.slice() }; m.sectors.push(sec); S.defineSector(project, m, sec, over); return sec; };
  S.newMap = (project, id, style) => {
    const m = { id, style: style || "doom", verts: [], sectors: [], things: [], n: { v: 0, s: 0, t: 0 } }; project.maps.push(m); S.defineMap(project, m);
    const ids = [[-128, -128], [128, -128], [128, 128], [-128, 128]].map(([x, y]) => S.addVert(project, m, x, y).id); S.addSector(project, m, ids); return m;
  };
  S.removeSector = (project, m, sec) => { const used = new Set(); for (const o of m.sectors) if (o !== sec) o.v.forEach(i => used.add(i)); m.sectors.splice(m.sectors.indexOf(sec), 1);
    for (const i of sec.v) if (!used.has(i)) { m.verts.splice(m.verts.findIndex(v => v.id === i), 1); for (const k of Object.keys(project.tags)) if (k.startsWith(m.id + "." + i + ".")) delete project.tags[k]; }
    for (const k of Object.keys(project.tags)) if (k.startsWith(m.id + "." + sec.id + ".")) delete project.tags[k]; };

  // ---- the player (one entity per project). Every value is a tag named player.*
  const PNUM = (k, label, def, min, max, step, extra) => Object.assign({ k, label, type: "number", def, min, max, step }, extra || {});
  S.PLAYER = [
    { k: "speed", label: "Move speed", type: "number", def: 280, min: 40, max: 1200, step: 10 }, { k: "gravity", label: "Gravity", type: "number", def: 1100, min: 0, max: 4000, step: 50 },
    { k: "jumpEnabled", label: "Jumping", type: "bool", def: true }, { k: "jumpHeight", label: "Jump height", type: "number", def: 48, min: 8, max: 240, step: 2, showIf: ["jumpEnabled", true] },
    { k: "sprintEnabled", label: "Sprinting", type: "bool", def: true }, { k: "sprintPercent", label: "Sprint speed bonus %", type: "number", def: 60, min: 0, max: 300, step: 5, showIf: ["sprintEnabled", true] },
    { k: "eyeHeight", label: "Eye height (64 = 1 m)", type: "number", def: 102, min: 16, max: 200, step: 1 }, { k: "radius", label: "Body width (collision)", type: "number", def: 16, min: 4, max: 48, step: 1 }, { k: "interactKey", label: "Interact key (open doors...)", type: "choice", options: null, def: "e" }, { k: "stepHeight", label: "Step height", type: "number", def: 24, min: 0, max: 64, step: 1 },
    { k: "fov", label: "Field of view", type: "number", def: 80, min: 40, max: 120, step: 1 }, { k: "aimMode", label: "Aim mode", type: "choice", options: [["classic", "Classic (normal FPS aim)"], ["free", "Free aim (bodycam style)"]], def: "classic" },
    { k: "freeAimRadius", label: "Free aim: aim area (degrees)", type: "number", def: 14, min: 3, max: 45, step: 1, showIf: ["aimMode", "free"] }, { k: "mouseSens", label: "Mouse sensitivity", type: "number", def: 1, min: 0.1, max: 4, step: 0.05 },
    { k: "footstep", label: "Footstep sound", type: "asset", assetType: "sound", def: "" }, { k: "footstep2", label: "Footstep sound 2 (optional, picked at random)", type: "asset", assetType: "sound", def: "" }, { k: "footstep3", label: "Footstep sound 3 (optional)", type: "asset", assetType: "sound", def: "" },
    { k: "stepDistance", label: "Distance between footsteps", type: "number", def: 90, min: 20, max: 400, step: 5 }, { k: "jumpSound", label: "Jump sound", type: "asset", assetType: "sound", def: "" }, { k: "landSound", label: "Landing sound", type: "asset", assetType: "sound", def: "" },
    { k: "hurtSound", label: "Hurt sound (when health goes down)", type: "asset", assetType: "sound", def: "" }, { k: "deathSound", label: "Death sound (health reaches 0)", type: "asset", assetType: "sound", def: "" }, { k: "soundVolume", label: "Player sounds volume", type: "number", def: 1, min: 0, max: 1, step: 0.05 },
    { k: "breathe", label: "Breathing (when standing still)", type: "bool", def: true }, PNUM("breatheAmount", "Breathing amount", 0.25, 0, 3, 0.01, { showIf: ["breathe", true] }), PNUM("breatheSpeed", "Breathing speed (breaths per second)", 0.25, 0.05, 2, 0.01, { showIf: ["breathe", true] }),
    { k: "bob", label: "Walking bob", type: "bool", def: true }, PNUM("bobAmount", "Walking bob amount", 0.5, 0, 4, 0.01, { showIf: ["bob", true] }), PNUM("bobSpeed", "Walking bob speed (steps per second)", 1.8, 0.2, 6, 0.05, { showIf: ["bob", true] }),
    { k: "hudMove", label: "Hud Movement (the HUD sways when you look around, like Metroid Prime)", type: "bool", def: false }, PNUM("hudMoveAmount", "Hud Movement strength", 1, 0.1, 4, 0.05, { showIf: ["hudMove", true] }),
    { k: "maxHealth", label: "Health", type: "number", def: 100, min: 1, max: 1000, step: 1 },
    { k: "crosshair", label: "Show crosshair", type: "bool", def: true }, { k: "crosshairSize", label: "Crosshair size", type: "number", def: 10, min: 2, max: 60, step: 1, showIf: ["crosshair", true] }, { k: "crosshairGap", label: "Crosshair gap", type: "number", def: 4, min: 0, max: 30, step: 1, showIf: ["crosshair", true] },
    { k: "crosshairThickness", label: "Crosshair thickness", type: "number", def: 2, min: 1, max: 8, step: 1, showIf: ["crosshair", true] }, Object.assign(COLOR("crosshairColor", "Crosshair color", "#ffffff"), { showIf: ["crosshair", true] }),
    { k: "hud", label: "HUD screen", type: "screenRef", def: "hud" },
    { k: "reloadKey", label: "Reload key", type: "choice", options: [], def: "r" },
    { k: "startMap", label: "The game starts on map (empty = the first map)", type: "mapRef", def: "" },
    { k: "deathAction", label: "When the player dies", type: "choice", options: [["restart", "Restart the map"], ["screen", "Go to a screen (e.g. Game Over)"]], def: "restart" },
    { k: "deathScreen", label: "Game over screen", type: "screenRef", def: "", showIf: ["deathAction", "screen"] }, PNUM("deathDelay", "Wait after dying (s)", 2.5, 0, 30, 0.1),
    { k: "damageFlash", label: "Red flash when hurt", type: "bool", def: true },
  ];
  S.PLAYER_GROUPS = [["Movement", ["speed", "sprintEnabled", "sprintPercent", "jumpEnabled", "jumpHeight", "gravity", "interactKey"]], ["Feel", ["breathe", "breatheAmount", "breatheSpeed", "bob", "bobAmount", "bobSpeed"]], ["Sounds", ["footstep", "footstep2", "footstep3", "stepDistance", "jumpSound", "landSound", "hurtSound", "deathSound", "soundVolume"]], ["Body", ["maxHealth", "eyeHeight", "radius", "stepHeight"]], ["Camera", ["fov", "aimMode", "freeAimRadius", "mouseSens"]], ["Crosshair", ["crosshair", "crosshairSize", "crosshairGap", "crosshairThickness", "crosshairColor"]], ["HUD", ["hud", "hudMove", "hudMoveAmount", "damageFlash"]], ["Game rules", ["startMap", "deathAction", "deathScreen", "deathDelay", "reloadKey"]]];
  // 3D objects attached to the player camera (weapons, hands...). Each one has tags player.<id>.*
  const ANIM = (k, label, extra) => Object.assign({ k, label, type: "anim", def: "" }, extra || {}), NUM = (k, label, def, min, max, step, extra) => Object.assign({ k, label, type: "number", def, min, max, step }, extra || {}), WP = ["type", "weapon"], NI = ["type", ["weapon", "animation"]], W_ = { showIf: WP };
  // a showIf is [tagKey, value] or [tagKey, [value, value...]]
  S.shown = (T, pre, pr) => { const ok = c => { const v = T.get(pre + c[0]), w = c[1]; return Array.isArray(w) ? w.includes(v) : v === w; }; return (!pr.showIf || ok(pr.showIf)) && (!pr.showIf2 || ok(pr.showIf2)); };
  S.ATTACH = [
    { k: "name", label: "Name", type: "string", def: "Object" },
    { k: "type", label: "Type", type: "choice", options: [["weapon", "Weapon (in the weapon list, aims and fires)"], ["animation", "Animation object (3D model)"], ["image", "Image object (flat picture)"]], def: "animation" },
    { k: "asset", label: "Model", type: "asset", assetType: "model", def: "", showIf: NI }, { k: "image", label: "Image", type: "asset", assetType: "image", def: "", showIf: ["type", "image"] }, { k: "visible", label: "Visible", type: "bool", def: true },
    NUM("x", "Right / left", 7, -200, 200, 0.5), NUM("y", "Up / down", -8, -200, 200, 0.5), NUM("z", "Forward (negative = in front)", -14, -400, 200, 0.5),
    NUM("rx", "Rotate X", 0, -180, 180, 1), NUM("ry", "Rotate Y", 0, -180, 180, 1), NUM("rz", "Rotate Z", 0, -180, 180, 1), NUM("scale", "Scale", 1, 0.001, 100, 0.001),
    NUM("pivotX", "Pivot X (turn point)", 0, -2000, 2000, 0.1), NUM("pivotY", "Pivot Y (turn point)", 0, -2000, 2000, 0.1), NUM("pivotZ", "Pivot Z (turn point)", 0, -2000, 2000, 0.1),
    ANIM("idleAnim", "Idle animation (loops)", { showIf: NI }),
    NUM("muzzleX", "Muzzle point X", 0, -500, 500, 0.5, W_), NUM("muzzleY", "Muzzle point Y", 0, -500, 500, 0.5, W_), NUM("muzzleZ", "Muzzle point Z", 0, -500, 500, 0.5, W_),
    NUM("arrowYaw", "Arrow: turn left / right", 0, -180, 180, 1, W_), NUM("arrowPitch", "Arrow: up / down", 0, -89, 89, 1, W_), NUM("aimMax", "Max aim turn (degrees)", 30, 0, 90, 1, W_),
    // ---- Light tab: this object can glow and light up the world
    { k: "lightType", label: "Light source", type: "choice", options: [["none", "None"], ["point", "Point light (shines in all directions)"], ["area", "Area light (a flat panel that shines one way)"]], def: "none", tab: "light" },
    { k: "lightColor", label: "Light color", type: "color", def: "#ffe2b0", showIf: ["lightType", ["point", "area"]], tab: "light" },
    NUM("lightIntensity", "Brightness", 1.5, 0, 8, 0.05, { showIf: ["lightType", ["point", "area"]], tab: "light" }), NUM("lightRange", "Reach (how far it shines)", 300, 10, 3000, 5, { showIf: ["lightType", ["point", "area"]], tab: "light" }),
    NUM("lightFlicker", "Flicker (0 = steady)", 0, 0, 1, 0.01, { showIf: ["lightType", ["point", "area"]], tab: "light" }),
    NUM("lightX", "Light position X", 0, -500, 500, 0.5, { showIf: ["lightType", ["point", "area"]], tab: "light" }), NUM("lightY", "Light position Y", 0, -500, 500, 0.5, { showIf: ["lightType", ["point", "area"]], tab: "light" }), NUM("lightZ", "Light position Z", 0, -500, 500, 0.5, { showIf: ["lightType", ["point", "area"]], tab: "light" }),
    NUM("lightWidth", "Panel width", 40, 1, 1000, 1, { showIf: ["lightType", "area"], tab: "light" }), NUM("lightHeight", "Panel height", 20, 1, 1000, 1, { showIf: ["lightType", "area"], tab: "light" }),
    NUM("lightYaw", "Panel faces: turn left / right", 0, -180, 180, 1, { showIf: ["lightType", "area"], tab: "light" }), NUM("lightPitch", "Panel faces: up / down", 0, -89, 89, 1, { showIf: ["lightType", "area"], tab: "light" }),
    // ---- Material tab: how the object (and what a weapon fires) looks
    { k: "matTint", label: "Base color (tints the picture)", type: "color", def: "#ffffff", tab: "material" },
    NUM("matEmit", "Glow amount (shines by itself)", 0, 0, 4, 0.05, { tab: "material" }), { k: "matEmitColor", label: "Glow color", type: "color", def: "#ffffff", tab: "material" },
    NUM("matLit", "Reacts to lights (0 = ignores them, 1 = fully)", 1, 0, 1, 0.05, { tab: "material" }), NUM("matOpacity", "Opacity (1 = solid)", 1, 0.05, 1, 0.05, { tab: "material" }), NUM("matShine", "Shininess (light glints)", 0, 0, 1, 0.05, { tab: "material" }),
    NUM("matRough", "Roughness (0 = mirror smooth, 1 = chalk)", 0.7, 0, 1, 0.01, { tab: "material" }), NUM("matMetal", "Metallic (0 = plastic / paint, 1 = bare metal)", 0, 0, 1, 0.01, { tab: "material" }),
    NUM("matReflect", "Reflections of sky and ground", 1, 0, 3, 0.05, { tab: "material" }), { k: "matTexture", label: "Texture (replaces the model's own picture)", type: "asset", assetType: "image", def: "", tab: "material" },
    // projectile: what a "Fire the projectile" node launches from the muzzle point
    { k: "projType", label: "Projectile", type: "choice", options: [["none", "None"], ["hitscan", "Hitscan (a straight line to where you aim)"], ["particle", "Particle (sphere or laser bolt)"], ["mesh", "Mesh (a 3D model that flies off)"]], def: "none", showIf: WP },
    NUM("emitX", "Emitter point X (where projectiles start)", 0, -500, 500, 0.5, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }), NUM("emitY", "Emitter point Y", 0, -500, 500, 0.5, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }), NUM("emitZ", "Emitter point Z", 0, -500, 500, 0.5, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }),
    NUM("emitYaw", "Emitter arrow: turn left / right", 0, -180, 180, 1, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }), NUM("emitPitch", "Emitter arrow: up / down", 0, -89, 89, 1, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }),
    { k: "projColor", label: "Color", type: "color", def: "#ffcc33", showIf: ["projType", ["hitscan", "particle"]] },
    NUM("projWidth", "Line thickness", 0.6, 0.05, 20, 0.05, { showIf: ["projType", "hitscan"] }), NUM("projLineTime", "Line stays visible (seconds)", 0.12, 0.02, 5, 0.01, { showIf: ["projType", "hitscan"] }),
    { k: "projShape", label: "Particle shape", type: "choice", options: [["sphere", "Sphere"], ["laser", "Laser bolt"]], def: "sphere", showIf: ["projType", "particle"] },
    { k: "projModel", label: "Projectile model (FBX)", type: "asset", assetType: "model", def: "", showIf: ["projType", "mesh"] }, NUM("projScale", "Model scale", 1, 0.001, 100, 0.001, { showIf: ["projType", "mesh"] }),
    NUM("projRx", "Model turn X (fix its facing)", 0, -180, 180, 1, { showIf: ["projType", "mesh"] }), NUM("projRy", "Model turn Y", 0, -180, 180, 1, { showIf: ["projType", "mesh"] }), NUM("projRz", "Model turn Z", 0, -180, 180, 1, { showIf: ["projType", "mesh"] }),
    NUM("projSize", "Size", 3, 0.2, 80, 0.1, { showIf: ["projType", "particle"] }), NUM("projSpeed", "Speed", 1400, 50, 8000, 50, { showIf: ["projType", ["particle", "mesh"]] }), NUM("projLife", "Flies for (seconds)", 2, 0.1, 30, 0.1, { showIf: ["projType", ["particle", "mesh"]] }),
    NUM("projEmit", "Emission (glow brightness)", 1, 0, 6, 0.05, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }), { k: "projLight", label: "Projectile lights up the world", type: "bool", def: false, showIf: ["projType", ["particle", "mesh"]] },
    NUM("projLightPower", "Projectile light brightness", 1.5, 0, 8, 0.05, { showIf: ["projType", ["particle", "mesh"]] }), NUM("projLightRange", "Projectile light reach", 220, 10, 2000, 5, { showIf: ["projType", ["particle", "mesh"]] }),
    { k: "projCharge", label: "Bigger when charged (charge beam)", type: "bool", def: true, showIf: ["projType", ["particle", "mesh"]] },
    NUM("projDamage", "Damage per hit (to enemies)", 25, 0, 100000, 1, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }),
    // ammo: "ammo" = rounds ready to fire, "reserve" = the rest carried. Both are live tags (a HUD text can show {player.att1.ammo})
    { k: "ammoUse", label: "Uses ammo", type: "bool", def: false, showIf: WP },
    NUM("ammoStart", "Ammo at the start", 50, 0, 100000, 1, { showIf: WP, showIf2: ["ammoUse", true] }), NUM("ammoMax", "Most ammo you can carry", 200, 1, 100000, 1, { showIf: WP, showIf2: ["ammoUse", true] }),
    NUM("clipSize", "Magazine size (0 = no reloading, Doom style)", 0, 0, 10000, 1, { showIf: WP, showIf2: ["ammoUse", true] }), NUM("reloadTime", "Reload time (s, used when there is no reload animation)", 1.2, 0.05, 30, 0.05, { showIf: WP, showIf2: ["ammoUse", true] }),
    ANIM("reloadAnim", "Reload animation", { showIf: WP, showIf2: ["ammoUse", true] }), { k: "reloadSound", label: "Reload sound", type: "asset", assetType: "sound", def: "", showIf: WP, showIf2: ["ammoUse", true] }, { k: "emptySound", label: "Empty click sound", type: "asset", assetType: "sound", def: "", showIf: WP, showIf2: ["ammoUse", true] },
    NUM("ammo", "Ammo ready to fire (live)", 0, 0, 100000, 1, { live: true }), NUM("reserve", "Ammo in reserve (live)", 0, 0, 100000, 1, { live: true }),
  ];
  // ---- placeable objects (the Object Editor): doors, pick-ups, decorations. Tags prop<N>.*  They are placed in maps (map.things).
  S.PROP = [
    { k: "name", label: "Name", type: "string", def: "Object" },
    { k: "type", label: "Type", type: "choice", options: [["decoration", "Decoration (just a model)"], ["door", "Door (opens in a wall)"], ["pickup", "Pick-up (collected by touching)"], ["enemy", "Enemy (hunts and attacks the player)"], ["exit", "Level exit (touch it to leave the level)"]], def: "decoration" },
    { k: "asset", label: "Model (FBX)", type: "asset", assetType: "model", def: "" },
    NUM("scale", "Scale", 1, 0.001, 100, 0.001), NUM("rx", "Rotate X", 0, -180, 180, 1), NUM("ry", "Rotate Y", 0, -180, 180, 1), NUM("rz", "Rotate Z", 0, -180, 180, 1),
    NUM("ox", "Move X (sideways)", 0, -500, 500, 0.5), NUM("oy", "Move Y (up)", 0, -500, 500, 0.5), NUM("oz", "Move Z (toward the front)", 0, -500, 500, 0.5),
    ANIM("idleAnim", "Idle animation (loops)"),
    NUM("doorWidth", "Doorway width (units, 96 = 1.5 m)", 96, 16, 400, 1, { showIf: ["type", "door"] }), NUM("doorHeight", "Doorway height (units)", 144, 16, 400, 1, { showIf: ["type", "door"] }),
    { k: "openMode", label: "Opens", type: "choice", options: [["interact", "When interacted with (Interact key)"], ["proximity", "When the player is close"], ["shot", "When shot"]], def: "interact", showIf: ["type", "door"] },
    NUM("openRange", "Opens when closer than (units)", 110, 20, 600, 5, { showIf: ["type", "door"], showIf2: ["openMode", "proximity"] }), NUM("interactRange", "Can be used from (units)", 90, 20, 400, 5, { showIf: ["type", "door"], showIf2: ["openMode", "interact"] }),
    ANIM("openAnim", "Opening animation", { showIf: ["type", "door"] }), ANIM("closeAnim", "Closing animation (empty = opening played backwards)", { showIf: ["type", "door"] }),
    { k: "autoClose", label: "Closes again by itself", type: "bool", def: true, showIf: ["type", "door"] }, NUM("closeAfter", "Closes after (seconds)", 3, 0.5, 60, 0.5, { showIf: ["type", "door"], showIf2: ["autoClose", true] }),
    { k: "openSound", label: "Opening sound", type: "asset", assetType: "sound", def: "", showIf: ["type", "door"] }, { k: "closeSound", label: "Closing sound", type: "asset", assetType: "sound", def: "", showIf: ["type", "door"] },
    { k: "giveTag", label: "Changes this number tag", type: "tagRef", refType: "number", def: "", showIf: ["type", "pickup"] }, NUM("giveAmount", "By (negative = subtract)", 10, -100000, 100000, 1, { showIf: ["type", "pickup"] }),
    NUM("pickRadius", "Pick-up distance (units)", 28, 8, 200, 1, { showIf: ["type", "pickup"] }), { k: "pickSound", label: "Pick-up sound", type: "asset", assetType: "sound", def: "", showIf: ["type", "pickup"] },
    // material (Material foldout of the Object Editor)
    { k: "matTint", label: "Base color (tints the picture)", type: "color", def: "#ffffff" }, { k: "matTexture", label: "Texture (replaces the model's own picture)", type: "asset", assetType: "image", def: "" },
    NUM("matRough", "Roughness (0 = mirror smooth, 1 = chalk)", 0.7, 0, 1, 0.01), NUM("matMetal", "Metallic (0 = plastic / paint, 1 = bare metal)", 0, 0, 1, 0.01), NUM("matReflect", "Reflections of sky and ground", 1, 0, 3, 0.05),
    NUM("matEmit", "Glow amount (shines by itself)", 0, 0, 4, 0.05), { k: "matEmitColor", label: "Glow color", type: "color", def: "#ffffff" }, NUM("matLit", "Reacts to lights (0 = ignores them, 1 = fully)", 1, 0, 1, 0.05), NUM("matOpacity", "Opacity (1 = solid)", 1, 0.05, 1, 0.05),
    // enemy
    NUM("health", "Health", 100, 1, 100000, 1, { showIf: ["type", "enemy"] }), NUM("speed", "Walking speed (units / second)", 150, 0, 2000, 5, { showIf: ["type", "enemy"] }),
    NUM("radius", "Body width (radius, units)", 22, 4, 200, 1, { showIf: ["type", "enemy"] }), NUM("height", "Body height (units, 64 = 1 m)", 110, 10, 600, 1, { showIf: ["type", "enemy"] }),
    NUM("sight", "Sees the player from (units)", 900, 50, 10000, 10, { showIf: ["type", "enemy"] }),
    { k: "attack", label: "Attack", type: "choice", options: [["melee", "Melee (hits up close)"], ["hitscan", "Gun (instant hit)"], ["projectile", "Projectile (a flying ball you can dodge)"]], def: "melee", showIf: ["type", "enemy"] },
    NUM("attackRange", "Attacks from (units)", 70, 10, 5000, 5, { showIf: ["type", "enemy"] }), NUM("damage", "Damage per hit", 10, 0, 100000, 1, { showIf: ["type", "enemy"] }),
    NUM("attackRate", "Time between attacks (s)", 1.2, 0.1, 30, 0.05, { showIf: ["type", "enemy"] }), NUM("attackDelay", "Hit lands after (s, into the attack)", 0.3, 0, 10, 0.05, { showIf: ["type", "enemy"] }),
    NUM("projSpeed", "Projectile speed (units / s)", 600, 50, 5000, 10, { showIf: ["type", "enemy"], showIf2: ["attack", "projectile"] }), NUM("projSize", "Projectile size", 6, 1, 100, 0.5, { showIf: ["type", "enemy"], showIf2: ["attack", "projectile"] }),
    { k: "projColor", label: "Projectile colour", type: "color", def: "#ff6a2a", showIf: ["type", "enemy"] },
    ANIM("walkAnim", "Walking animation", { showIf: ["type", "enemy"] }), ANIM("attackAnim", "Attack animation", { showIf: ["type", "enemy"] }), ANIM("hurtAnim", "Hurt animation", { showIf: ["type", "enemy"] }), ANIM("deathAnim", "Death animation", { showIf: ["type", "enemy"] }),
    { k: "corpse", label: "Body stays after death", type: "bool", def: true, showIf: ["type", "enemy"] },
    { k: "sightSound", label: "Sound when it spots you", type: "asset", assetType: "sound", def: "", showIf: ["type", "enemy"] }, { k: "attackSound", label: "Attack sound", type: "asset", assetType: "sound", def: "", showIf: ["type", "enemy"] },
    { k: "hurtSound", label: "Hurt sound", type: "asset", assetType: "sound", def: "", showIf: ["type", "enemy"] }, { k: "deathSound", label: "Death sound", type: "asset", assetType: "sound", def: "", showIf: ["type", "enemy"] },
    { k: "killTag", label: "When killed, change this number tag (e.g. a score)", type: "tagRef", refType: "number", def: "", showIf: ["type", "enemy"] }, NUM("killAmount", "By", 1, -100000, 100000, 1, { showIf: ["type", "enemy"] }),
    // level exit
    { k: "exitTo", label: "Leads to", type: "choice", options: [["map", "Another map"], ["screen", "A screen (e.g. a 'You win' screen)"]], def: "map", showIf: ["type", "exit"] },
    { k: "exitMap", label: "Map", type: "mapRef", def: "", showIf: ["type", "exit"], showIf2: ["exitTo", "map"] }, { k: "exitScreen", label: "Screen", type: "screenRef", def: "", showIf: ["type", "exit"], showIf2: ["exitTo", "screen"] },
    NUM("exitRadius", "Touch distance (units)", 40, 8, 400, 1, { showIf: ["type", "exit"] }), { k: "exitSound", label: "Sound", type: "asset", assetType: "sound", def: "", showIf: ["type", "exit"] },
  ];
  S.defineProp = (project, p, over) => { for (const pr of S.PROP) { const r = rec(pr, "props", p.id); if (pr.type === "anim") r.modelTag = p.id + ".asset"; if (over && pr.k in over) r.value = over[pr.k]; project.tags[p.id + "." + pr.k] = r; } };
  S.addProp = (project, over) => { const p = { id: S.uid("prop", project.props.map(x => x.id)) }; project.props.push(p); S.defineProp(project, p, Object.assign({ name: p.id }, over || {})); return p; };
  S.removeProp = (project, p) => { project.props.splice(project.props.indexOf(p), 1); for (const k of Object.keys(project.tags)) if (k.startsWith(p.id + ".")) delete project.tags[k]; for (const m of project.maps || []) m.things = (m.things || []).filter(t => t.prop !== p.id); };
  // ---- node scripts (see script.js). ports: outs = output names; ins:false = event node (starts a chain)
  const ANYTAG = { k: "tag", label: "Tag", type: "tagRef", refType: "any", def: "" }, NUMTAG = { k: "tag", label: "Number tag", type: "tagRef", refType: "number", def: "" };
  // keys and mouse buttons for the Node Coder dropdowns
  S.KEYS = [["mouse1", "Left mouse button"], ["mouse2", "Right mouse button"], ["mouse3", "Middle mouse button"], ["space", "Space"], ["shift", "Shift"], ["ctrl", "Ctrl"], ["alt", "Alt"], ["enter", "Enter"], ["tab", "Tab"], ["arrowup", "Arrow up"], ["arrowdown", "Arrow down"], ["arrowleft", "Arrow left"], ["arrowright", "Arrow right"]]
    .concat("abcdefghijklmnopqrstuvwxyz".split("").map(c => [c, c.toUpperCase()]), "0123456789".split("").map(c => [c, c]), [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12].map(i => ["f" + i, "F" + i]));
  S.PLAYER.find(p => p.k === "interactKey").options = S.KEYS; S.PLAYER.find(p => p.k === "reloadKey").options = S.KEYS;
  const KEYP = (def, label) => ({ k: "key", label: label || "Key or mouse button", type: "choice", options: S.KEYS, def: def || "r" });
  S.STATES = [["standing", "Standing still"], ["walking", "Walking"], ["running", "Running (sprinting)"], ["jumping", "Jumping (going up)"], ["falling", "Falling"], ["moving", "Moving (walking or running)"], ["inair", "In the air (jumping or falling)"]];
  S.SNODES = {
    onStart: { label: "When the game starts", cat: "event", ins: false, outs: ["next"], ctx: ["screen", "object"], props: [] },
    onClick: { label: "When clicked", cat: "event", ins: false, outs: ["next"], ctx: ["screen", "object"], props: [] },
    onKey: { label: "When a key is pressed", cat: "event", ins: false, outs: ["next"], ctx: ["screen", "object"], props: [KEYP("r")] },
    onKeyUp: { label: "When a key is released", cat: "event", ins: false, outs: ["next"], ctx: ["object"], props: [KEYP("r")] },
    onState: { label: "When the player is… (state starts)", cat: "event", ins: false, outs: ["next"], ctx: ["object"], props: [{ k: "state", label: "State", type: "choice", options: S.STATES, def: "walking" }] },
    onHold: { label: "While a key is held", cat: "event", ins: false, outs: ["next"], ctx: ["object"], props: [KEYP("mouse1", "Key or mouse button held down"), { k: "interval", label: "Run again every (seconds, 0 = every frame)", type: "number", def: 0.1, min: 0, max: 60, step: 0.01 }] },
    onEquip: { label: "When this weapon is taken out", cat: "event", ins: false, outs: ["next"], ctx: ["weapon"], props: [] },
    setTag: { label: "Set a tag", cat: "action", outs: ["next"], ctx: ["screen", "object"], props: [ANYTAG, { k: "value", label: "To value", type: "string", def: "0" }] },
    changeTag: { label: "Change a number by", cat: "action", outs: ["next"], ctx: ["screen", "object"], props: [NUMTAG, { k: "amount", label: "Add (negative = subtract)", type: "number", def: 1, min: -100000, max: 100000, step: 1 }] },
    playSound: { label: "Play a sound", cat: "action", outs: ["next"], ctx: ["screen", "object"], props: [{ k: "sound", label: "Sound", type: "asset", assetType: "sound", def: "" }, { k: "volume", label: "Volume", type: "number", def: 1, min: 0, max: 1, step: 0.05 }] },
    loopSound: { label: "Loop a sound", cat: "action", outs: ["next"], ctx: ["screen", "object"], props: [{ k: "sound", label: "Sound", type: "asset", assetType: "sound", def: "" }, { k: "volume", label: "Volume", type: "number", def: 1, min: 0, max: 1, step: 0.05 }] },
    stopLoop: { label: "Stop looping a sound", cat: "action", outs: ["next"], ctx: ["screen", "object"], props: [{ k: "sound", label: "Sound (empty = every looping sound)", type: "asset", assetType: "sound", def: "" }] },
    playAnim: { label: "Play an animation", cat: "action", outs: ["next"], ctx: ["object"], props: [{ k: "anim", label: "Animation", type: "anim", def: "" }, { k: "loop", label: "Keep repeating it", type: "bool", def: false }] },
    gotoScreen: { label: "Go to a screen", cat: "action", outs: ["next"], ctx: ["screen"], props: [{ k: "screen", label: "Screen", type: "screenRef", def: "" }] },
    say: { label: "Write a message", cat: "action", outs: ["next"], ctx: ["screen", "object"], props: [{ k: "text", label: "Message (shows in the Console)", type: "string", def: "Hello" }] },
    wait: { label: "Wait", cat: "logic", outs: ["next"], ctx: ["screen", "object"], props: [{ k: "seconds", label: "Seconds", type: "number", def: 0.5, min: 0, max: 600, step: 0.05 }] },
    if: { label: "If", cat: "logic", outs: ["then", "else"], ctx: ["screen", "object"], props: [ANYTAG, { k: "op", label: "Is", type: "choice", options: [["=", "equal to"], ["!=", "not equal to"], [">", "greater than"], ["<", "less than"], [">=", "at least"], ["<=", "at most"]], def: "=" }, { k: "value", label: "This value", type: "string", def: "0" }] },
    repeat: { label: "Repeat", cat: "logic", outs: ["body", "next"], ctx: ["screen", "object"], props: [{ k: "times", label: "Times", type: "number", def: 3, min: 1, max: 1000, step: 1 }] },
    ifState: { label: "If the player is…", cat: "logic", outs: ["then", "else"], ctx: ["object"], props: [{ k: "state", label: "State", type: "choice", options: S.STATES, def: "standing" }] },
    ifKey: { label: "If a key is held", cat: "logic", outs: ["then", "else"], ctx: ["object"], props: [KEYP("shift", "Key or mouse button")] },
    onCharge: { label: "Charge weapon (hold to charge)", cat: "weapon", ins: false, outs: ["start", "charged", "fire", "cancel"], ctx: ["weapon"], props: [KEYP("mouse1", "Hold this key or button to charge"),
      { k: "startDelay", label: "Delay before charging starts (seconds)", type: "number", def: 0.5, min: 0, max: 10, step: 0.05 },
      { k: "chargeAnim", label: "Charging animation (plays once)", type: "anim", def: "" }, { k: "loopAnim", label: "Charged animation (loops while held)", type: "anim", def: "" }, { k: "fireAnim", label: "Fire animation (on release)", type: "anim", def: "" },
      { k: "chargeTime", label: "Time to full charge (seconds, 0 = length of the charging animation)", type: "number", def: 0, min: 0, max: 60, step: 0.05 }] },
    chargeShow: { label: "Show charge particle", cat: "weapon", outs: ["next"], ctx: ["weapon"], props: [COLOR("color", "Color", "#33ccff"), { k: "size", label: "Start size", type: "number", def: 1, min: 0.2, max: 80, step: 0.1 }] },
    chargeGrow: { label: "Grow the charge particle", cat: "weapon", outs: ["next"], ctx: ["weapon"], props: [{ k: "size", label: "Grow to size", type: "number", def: 8, min: 0.2, max: 200, step: 0.1 }, { k: "seconds", label: "Over (seconds, 0 = until fully charged)", type: "number", def: 0, min: 0, max: 60, step: 0.05 }] },
    fireProjectile: { label: "Fire the projectile", cat: "weapon", outs: ["next"], ctx: ["weapon"], props: [] },
    reload: { label: "Reload the weapon", cat: "weapon", outs: ["next"], ctx: ["weapon"], props: [] },
    ifAmmo: { label: "If the weapon has ammo", cat: "weapon", outs: ["then", "else"], ctx: ["weapon"], props: [] },
    forever: { label: "Repeat forever", cat: "logic", outs: ["body"], ctx: ["screen", "object"], props: [] },
  };
  S.SCAT = { event: "Events", action: "Actions", logic: "Logic", weapon: "Weapon" };
  const SKEY = (ownerKey, id, k) => ownerKey + ".s." + id + "." + k;
  S.defineSNode = (project, ownerKey, node, over, modelTag) => { for (const p of S.SNODES[node.type].props) { const r = rec(p, "script", ownerKey); if (p.type === "anim") r.modelTag = modelTag; if (over && p.k in over) r.value = over[p.k]; project.tags[SKEY(ownerKey, node.id, p.k)] = r; } };
  S.addSNode = (project, ownerKey, script, type, x, y, over, modelTag) => { const node = { id: S.uid("n", script.nodes.map(n => n.id)), type, x: Math.round(x), y: Math.round(y) }; script.nodes.push(node); S.defineSNode(project, ownerKey, node, over, modelTag); return node; };
  S.removeSNode = (project, ownerKey, script, node) => { for (const p of S.SNODES[node.type].props) delete project.tags[SKEY(ownerKey, node.id, p.k)]; script.nodes.splice(script.nodes.indexOf(node), 1); script.links = script.links.filter(l => l.a !== node.id && l.b !== node.id); };
  S.snodeKey = SKEY;
  S.defineAttach = (project, att, over) => {
    if (over && over.name === undefined) over = Object.assign({ name: "Object" }, over); for (const p of S.ATTACH) { const r = rec(p, "player", "player." + att.id); if (p.type === "anim") r.modelTag = "player." + att.id + ".asset"; if (over && p.k in over) r.value = over[p.k]; project.tags["player." + att.id + "." + p.k] = r; } };
  S.removeAttach = (project, att) => { for (const k of Object.keys(project.tags)) if (k.startsWith("player." + att.id + ".")) delete project.tags[k]; project.player.attachments.splice(project.player.attachments.indexOf(att), 1); };
  // Sound events on model animations (Timeline tab). Structure: project.events = [{id, asset, anim}]. Values are tags ev1.time / ev1.sound / ev1.volume.
  S.EVENT = [{ k: "time", label: "Time (seconds)", type: "number", def: 0, min: 0, max: 600, step: 0.01 }, { k: "sound", label: "Sound", type: "asset", assetType: "sound", def: "" }, { k: "volume", label: "Volume", type: "number", def: 1, min: 0, max: 1, step: 0.01 }];
  // Animation length overrides (Timeline tab): project.animLen = [{id, asset, anim}] with tag <id>.length (seconds, 0 = the model's own length). Playing is stretched or squeezed to fit; sound times are in the new seconds.
  S.ANIMLEN = [{ k: "length", label: "Animation length (seconds, 0 = original)", type: "number", def: 0, min: 0, max: 600, step: 0.01 }];
  S.animLen = (project, tags, aid, name, dur) => { const e = (project.animLen || []).find(x => x.asset === aid && x.anim === name), l = e ? +tags.get(e.id + ".length") || 0 : 0, len = l > 0 ? l : dur; return { len, speed: len > 0 ? dur / len : 1, entry: e || null }; };
  S.defineAnimLen = (project, e, over) => { for (const p of S.ANIMLEN) { const r = rec(p, "anims", e.id); if (over && p.k in over) r.value = over[p.k]; project.tags[e.id + "." + p.k] = r; } };
  S.defineEvent = (project, ev, over) => { for (const p of S.EVENT) { const r = rec(p, "events", ev.id); if (over && p.k in over) r.value = over[p.k]; project.tags[ev.id + "." + p.k] = r; } };
  S.removeEvent = (project, ev) => { for (const p of S.EVENT) delete project.tags[ev.id + "." + p.k]; const i = project.events.indexOf(ev); if (i >= 0) project.events.splice(i, 1); };
  // older projects measured x,y from the top-left corner of the 1280x720 stage: move them to the new middle-origin system so nothing shifts
  S.toCenter = project => {
    if (project.origin === "center") return; project.origin = "center";
    for (const sc of project.screens) { if (sc.kind === "splash") continue; for (const n of sc.nodes) for (const [k, f, mn, mx] of [["x", v => v - 640, -640, 640], ["y", v => 360 - v, -360, 360]]) { const r = project.tags[sc.id + "." + n.id + "." + k]; if (r) { r.value = f(+r.value || 0); r.min = mn; r.max = mx; r.label = k === "x" ? "X (0 = middle)" : "Y (0 = middle, up is +)"; } } }
  };
  // ---- wall types (the Wall Editor): a small mesh that fills a 96 x 96 panel (1.5 m x 1.5 m). Geometry is structure; each face has tags wallN.fM.tex / .color
  S.WFACE = [TEX("tex", "Texture"), COLOR("color", "Color tint", "#ffffff")];
  S.defineWallFace = (project, w, f, over) => defP(project, S.WFACE, w.id, w.id + "." + f.id, w.id + "." + f.id, over);
  S.ensureWalls = project => {
    if (!project.walls) project.walls = []; if (!project.props) project.props = []; if (!project.anims) project.anims = [];
    for (const m of project.maps || []) { if (!m.things) m.things = []; if (m.n && m.n.t === undefined) m.n.t = m.things.length; if (!project.tags[m.id + ".wallThickness"]) project.tags[m.id + ".wallThickness"] = rec(S.MAP.find(p => p.k === "wallThickness"), m.id, m.id); }
    for (const p of project.props) for (const pr of S.PROP) if (!project.tags[p.id + "." + pr.k]) { const r = rec(pr, "props", p.id); if (pr.type === "anim") r.modelTag = p.id + ".asset"; project.tags[p.id + "." + pr.k] = r; }
    { const ik = project.tags["player.interactKey"]; if (project.tags["player.speed"] && !ik) project.tags["player.interactKey"] = rec(S.PLAYER.find(p => p.k === "interactKey"), "player", "player"); } if (project.tags["player.speed"]) for (const pp of S.PLAYER) if (!project.tags["player." + pp.k]) project.tags["player." + pp.k] = rec(pp, "player", "player");
    for (const m of project.maps || []) for (const v of m.verts) for (const p of S.VERT) if (!project.tags[m.id + "." + v.id + "." + p.k]) project.tags[m.id + "." + v.id + "." + p.k] = rec(p, m.id, m.id + "." + v.id);
    for (const w of project.walls) for (const f of w.faces) for (const p of S.WFACE) if (!project.tags[w.id + "." + f.id + "." + p.k]) project.tags[w.id + "." + f.id + "." + p.k] = rec(p, w.id, w.id + "." + f.id);
    const eh = project.tags["player.eyeHeight"]; if (eh && !project.eyeMoved) { if (eh.value === 56) eh.value = 102; project.eyeMoved = true; }
  };
  S.ensurePlayer = project => {
    S.ensureWalls(project);
    if (!project.events) project.events = [];
    if (!project.animLen) project.animLen = [];
    project.player = project.player || { attachments: [] }; if (!project.player.attachments) project.player.attachments = [];
    delete project.tags["player.freeAim"];
    for (const [n, label] of [["gfx.culling", "Culling (skip what you can not see)"], ["gfx.lod", "Auto-LOD (simpler models far away)"]]) if (!project.tags[n]) project.tags[n] = { type: "bool", value: true, label, group: "gfx", builtin: true };
    for (const att of project.player.attachments) {   // bring older objects up to date: a "weapon" tick became the object type
      const pre = "player." + att.id + ".", old = project.tags[pre + "weapon"];
      for (const p of S.ATTACH) { const r = project.tags[pre + p.k]; if (!r) { const n = rec(p, "player", "player." + att.id); if (p.type === "anim") n.modelTag = pre + "asset"; if (p.k === "type") n.value = old && old.value ? "weapon" : "animation"; if (p.k === "name") n.value = old && old.value ? "Weapon" : "Object"; project.tags[pre + p.k] = n; } else { r.label = p.label; if (p.showIf) r.showIf = p.showIf; else delete r.showIf; if (p.options) r.options = p.options; } }
      delete project.tags[pre + "weapon"];
      if (att.script) for (const n of att.script.nodes) { const def = S.SNODES[n.type]; if (!def) continue; for (const p of def.props) { const k = S.snodeKey("player." + att.id, n.id, p.k); if (!project.tags[k]) { const r = rec(p, "script", "player." + att.id); if (p.type === "anim") r.modelTag = pre + "asset"; project.tags[k] = r; } } }
      if (!att.script) att.script = { nodes: [], links: [] };   // the old "click animation" menu became a script: when clicked, play that animation
      const fa = project.tags[pre + "fireAnim"]; if (fa) { if (fa.value && !att.script.nodes.length) { const a = S.addSNode(project, "player." + att.id, att.script, "onClick", 20, 20), b = S.addSNode(project, "player." + att.id, att.script, "playAnim", 260, 20, { anim: fa.value }, pre + "asset"); att.script.links.push({ a: a.id, o: "next", b: b.id }); } delete project.tags[pre + "fireAnim"]; }
    }
    for (const p of S.PLAYER) if (!project.tags["player." + p.k]) { const r = rec(p, "player", "player"); r.builtin = true; project.tags["player." + p.k] = r; }
    if (!project.tags["player.state"]) project.tags["player.state"] = { type: "string", value: "standing", label: "What the player is doing: standing, walking, running, jumping, falling (live)", group: "player", builtin: true };
    if (!project.tags["player.charge"]) project.tags["player.charge"] = { type: "number", value: 0, label: "Charge beam progress, 0 to 1 (live)", group: "player", min: 0, max: 1, step: 0.01, builtin: true };
    if (!project.tags["player.weapon"]) project.tags["player.weapon"] = { type: "number", value: 1, label: "Current weapon number (live)", group: "player", min: 0, max: 99, step: 1, builtin: true };
    if (!project.tags["player.health"]) project.tags["player.health"] = { type: "number", value: 100, label: "Current health (live)", group: "player", min: 0, max: 1000, step: 1, builtin: true };
    if (!project.screens.some(s => s.id === "hud") && project.tags["player.hud"].value === "hud") {
      const sc = { id: "hud", kind: "hud", nodes: [] }; project.screens.push(sc); S.defineScreen(project, sc);
      const add = (id, type, over) => { const n = { id, type }; sc.nodes.push(n); S.defineNode(project, "hud", n, over); };
      add("healthbar", "bar", { bind: "player.health", max: "player.maxHealth", label: "HEALTH", x: -450, y: -314 }); add("healthtext", "title", { text: "{player.health}", size: 34, x: -260, y: -312, color: "#ffd9d9" });
    }
  };

  // the look of an object, from its Material tab tags
  S.matOf = (T, pre) => {
    const hx = (h, d) => { h = String(h || d); return [parseInt(h.slice(1, 3), 16) / 255, parseInt(h.slice(3, 5), 16) / 255, parseInt(h.slice(5, 7), 16) / 255]; }, e = +T.get(pre + "matEmit") || 0, op = T.get(pre + "matOpacity"), lit = T.get(pre + "matLit");
    const ro = T.get(pre + "matRough"), me = T.get(pre + "matMetal"), rf = T.get(pre + "matReflect");
    return { tint: hx(T.get(pre + "matTint"), "#ffffff"), emit: hx(T.get(pre + "matEmitColor"), "#ffffff").map(q => q * e), lit: lit === undefined ? 1 : +lit, alpha: op === undefined ? 1 : +op, spec: +T.get(pre + "matShine") || 0,
      pbr: ro !== undefined, rough: ro === undefined ? 0.7 : +ro, metal: +me || 0, env: rf === undefined ? 1 : +rf, texId: T.get(pre + "matTexture") || "" };
  };
  S.builtins = project => {
    const B = (n, type, value, label, group, extra) => { project.tags[n] = Object.assign({ type, value, label, group, builtin: true }, extra); };
    B("audio.master", "number", 0.8, "Master volume", "audio", { min: 0, max: 1, step: 0.01 });
    B("audio.sfx", "number", 1, "SFX volume", "audio", { min: 0, max: 1, step: 0.01 });
    B("audio.music", "number", 0.7, "Music volume", "audio", { min: 0, max: 1, step: 0.01 });
    B("gfx.fullscreen", "bool", false, "Fullscreen", "gfx");
    B("gfx.brightness", "number", 1, "Brightness", "gfx", { min: 0.5, max: 1.5, step: 0.05 });
    B("gfx.showFps", "bool", false, "Show FPS counter", "gfx");
    B("gfx.culling", "bool", true, "Culling (skip what you can not see)", "gfx");
    B("gfx.lod", "bool", true, "Auto-LOD (simpler models far away)", "gfx");
    B("project.startScreen", "screenRef", "splash", "Start screen", "project");
  };

  // a brand-new project: splash -> main menu ("My Game") -> settings
  S.template = (id, name) => {
    const p = { format: 3, id, name, modified: Date.now(), tags: {}, assets: [], folders: [], maps: [], screens: [] };
    S.builtins(p);
    const add = (sc, id, type, over) => { if (over && over.y !== undefined) over.y = 360 - over.y; const n = { id, type }; sc.nodes.push(n); S.defineNode(p, sc.id, n, over); };
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
    p.origin = "center"; S.ensurePlayer(p);
    return p;
  };
})();
