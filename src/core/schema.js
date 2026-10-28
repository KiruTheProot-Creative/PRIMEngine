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

  // ---- 3D maps: a grid of 2 m cells in layers (see grid.js). The cells (walls, floors, heights, slopes, stairs, textures) are structure; map settings are tags.
  // Units: 64 units = 1 m, one cell = 128 units = 2 m, one layer (story) = 128 units tall. Player eye height is 102 (1.6 m).
  const MNUM = (k, label, def, min, max, step, extra) => Object.assign({ k, label, type: "number", def, min, max, step }, extra || {});
  S.MOODS = [["neutral", "Neutral"], ["dusk", "Dusk (warm)"], ["night", "Night (dark blue)"], ["hell", "Hell (red, strong)"], ["toxic", "Toxic (green)"], ["cold", "Cold (pale blue)"], ["retro", "Retro (punchy colours)"], ["noir", "Noir (black and white)"], ["custom", "Custom…"]];
  S.MAP = [
    COLOR("skyColor", "Sky color", "#16243a"),
    { k: "fogOn", label: "Distance fog", type: "bool", def: true }, Object.assign(COLOR("fogColor", "Fog color", "#06090e"), { showIf: ["fogOn", true] }),
    MNUM("fogStart", "Fog starts at (units, 128 = one cell)", 256, 0, 20000, 16, { showIf: ["fogOn", true] }), MNUM("fogEnd", "Fully fogged at (units)", 2200, 64, 40000, 16, { showIf: ["fogOn", true] }),
    { k: "mood", label: "Mood", type: "choice", options: S.MOODS, def: "neutral" },
    Object.assign(COLOR("moodTint", "Mood colour (tints everything)", "#ffffff"), { showIf: ["mood", "custom"] }), MNUM("moodBrightness", "Mood brightness", 1, 0.2, 2, 0.05, { showIf: ["mood", "custom"] }),
    MNUM("moodContrast", "Mood contrast", 1, 0.5, 2, 0.05, { showIf: ["mood", "custom"] }), MNUM("moodSaturation", "Mood colourfulness (0 = black and white)", 1, 0, 2, 0.05, { showIf: ["mood", "custom"] }),
    { k: "bloom", label: "Bloom (bright and glowing things shine softly)", type: "bool", def: true }, MNUM("bloomStrength", "Bloom strength", 0.6, 0, 3, 0.05, { showIf: ["bloom", true] }), MNUM("bloomThreshold", "Bloom starts at brightness (1 = only over-bright light and glow)", 1, 0, 4, 0.05, { showIf: ["bloom", true] }),
    MNUM("exposure", "Exposure (overall brightness of the picture)", 1, 0.25, 4, 0.05),
    MNUM("startX", "Player start X", 0, -100000, 100000, 1), MNUM("startY", "Player start Y", 0, -100000, 100000, 1), MNUM("startLayer", "Player start story (0 = ground floor)", 0, 0, 63, 1), MNUM("startAngle", "Player start angle", 90, 0, 360, 1),
  ];
  const defP = (project, list, group, owner, id, over) => { for (const p of list) { const r = rec(p, group, owner); if (over && p.k in over) r.value = over[p.k]; project.tags[id + "." + p.k] = r; } };
  S.defineMap = (project, m, over) => defP(project, S.MAP, m.id, m.id, m.id, over);
  S.newMap = (project, id) => {
    const m = PRIM.Grid.newMap(id); project.maps.push(m); const st = PRIM.Grid.starter(m); S.defineMap(project, m, { startX: st.x, startY: st.z }); return m;
  };

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
    PNUM("animBlend", "Smooth change between animations (seconds, 0 = instant; weapons, enemies, doors, objects)", 0.15, 0, 1, 0.01), { k: "hudMove", label: "Hud Movement (the HUD sways when you look around, like Metroid Prime)", type: "bool", def: false }, PNUM("hudMoveAmount", "Hud Movement strength", 1, 0.1, 4, 0.05, { showIf: ["hudMove", true] }),
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
  S.PLAYER_GROUPS = [["Movement", ["speed", "sprintEnabled", "sprintPercent", "jumpEnabled", "jumpHeight", "gravity", "interactKey"]], ["Feel", ["breathe", "breatheAmount", "breatheSpeed", "bob", "bobAmount", "bobSpeed", "animBlend"]], ["Sounds", ["footstep", "footstep2", "footstep3", "stepDistance", "jumpSound", "landSound", "hurtSound", "deathSound", "soundVolume"]], ["Body", ["maxHealth", "eyeHeight", "radius", "stepHeight"]], ["Camera", ["fov", "aimMode", "freeAimRadius", "mouseSens"]], ["Crosshair", ["crosshair", "crosshairSize", "crosshairGap", "crosshairThickness", "crosshairColor"]], ["HUD", ["hud", "hudMove", "hudMoveAmount", "damageFlash"]], ["Game rules", ["startMap", "deathAction", "deathScreen", "deathDelay", "reloadKey"]]];
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
    // ---- Material tab: a base colour map and an emission (glow) map
    { k: "matTexture", label: "Base color map (replaces the model's own picture)", type: "asset", assetType: "image", def: "", tab: "material" },
    { k: "matEmitMap", label: "Emission map (bright parts glow; empty = the whole object glows)", type: "asset", assetType: "image", def: "", tab: "material" }, NUM("matEmit", "Emission strength (0 = no glow)", 0, 0, 8, 0.05, { tab: "material" }),
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
    // impact on walls, floors and ceilings: a sound and a mark (one picture, or an animation = frames side by side in one image)
    { k: "impactSound", label: "Wall hit sound", type: "asset", assetType: "sound", def: "", showIf: ["projType", ["hitscan", "particle", "mesh"]] },
    { k: "impactImage", label: "Wall hit mark / animation (frames side by side in one picture)", type: "asset", assetType: "image", def: "", showIf: ["projType", ["hitscan", "particle", "mesh"]] },
    NUM("impactFrames", "Frames in that picture (1 = a still mark)", 1, 1, 64, 1, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }), NUM("impactFps", "Animation speed (frames per second)", 15, 1, 60, 1, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }),
    NUM("impactSize", "Mark size (units, 128 = a cell)", 16, 1, 256, 1, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }), NUM("impactStay", "Mark stays for (seconds after the animation)", 4, 0, 60, 0.5, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }),
    { k: "impactGlow", label: "Mark glows (not darkened by shadows)", type: "bool", def: false, showIf: ["projType", ["hitscan", "particle", "mesh"]] },
    // splash damage and the explosion where a shot lands (wall, enemy, door, or the end of its flight)
    { k: "projSplash", label: "Splash damage (hurts everything around where it lands)", type: "bool", def: false, showIf: ["projType", ["hitscan", "particle", "mesh"]] },
    NUM("splashRadius", "Splash radius (units, 128 = a cell)", 160, 8, 2000, 4, { showIf: ["projType", ["hitscan", "particle", "mesh"]], showIf2: ["projSplash", true] }),
    NUM("splashDamage", "Splash damage (in the middle; a quarter of it at the edge)", 50, 0, 100000, 1, { showIf: ["projType", ["hitscan", "particle", "mesh"]], showIf2: ["projSplash", true] }),
    { k: "splashSelf", label: "Splash hurts you too (rocket jump pain)", type: "bool", def: false, showIf: ["projType", ["hitscan", "particle", "mesh"]], showIf2: ["projSplash", true] },
    { k: "explodeType", label: "Explosion", type: "choice", options: [["none", "None"], ["particles", "Particles (a burst of glowing bits)"], ["animation", "Animation (frames side by side in one picture)"]], def: "none", showIf: ["projType", ["hitscan", "particle", "mesh"]] },
    { k: "explodeImage", label: "Explosion picture (frames side by side)", type: "asset", assetType: "image", def: "", showIf: ["projType", ["hitscan", "particle", "mesh"]], showIf2: ["explodeType", "animation"] },
    NUM("explodeFrames", "Frames in that picture", 8, 1, 64, 1, { showIf: ["projType", ["hitscan", "particle", "mesh"]], showIf2: ["explodeType", "animation"] }), NUM("explodeFps", "Animation speed (frames per second)", 20, 1, 60, 1, { showIf: ["projType", ["hitscan", "particle", "mesh"]], showIf2: ["explodeType", "animation"] }),
    { k: "explodeColor", label: "Particle color", type: "color", def: "#ff9933", showIf: ["projType", ["hitscan", "particle", "mesh"]], showIf2: ["explodeType", "particles"] },
    NUM("explodeCount", "How many particles", 24, 1, 200, 1, { showIf: ["projType", ["hitscan", "particle", "mesh"]], showIf2: ["explodeType", "particles"] }), NUM("explodeSpeed", "Particle speed", 320, 10, 4000, 10, { showIf: ["projType", ["hitscan", "particle", "mesh"]], showIf2: ["explodeType", "particles"] }),
    NUM("explodeTime", "Particles last (seconds)", 0.6, 0.05, 10, 0.05, { showIf: ["projType", ["hitscan", "particle", "mesh"]], showIf2: ["explodeType", "particles"] }),
    NUM("explodeBit", "Size of one particle", 4, 0.2, 200, 0.1, { showIf: ["projType", ["hitscan", "particle", "mesh"]], showIf2: ["explodeType", "particles"] }),
    NUM("explodeSize", "Explosion picture size (units, 128 = a cell)", 96, 1, 2000, 1, { showIf: ["projType", ["hitscan", "particle", "mesh"]], showIf2: ["explodeType", "animation"] }),
    { k: "explodeLight", label: "Explosion flash lights up the room", type: "bool", def: true, showIf: ["projType", ["hitscan", "particle", "mesh"]], showIf2: ["explodeType", ["particles", "animation"]] },
    { k: "explodeSound", label: "Explosion sound", type: "asset", assetType: "sound", def: "", showIf: ["projType", ["hitscan", "particle", "mesh"]] },
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
    { k: "type", label: "Type", type: "choice", options: [["decoration", "Decoration (just a model)"], ["door", "Door (fills a cell between walls)"], ["pickup", "Pick-up (collected by touching)"], ["enemy", "Enemy (hunts and attacks the player)"], ["exit", "Level exit (touch it to leave the level)"], ["npc", "Character (NPC: talk to it, make choices)"], ["speaker", "Speaker (plays a sound you hear around it)"]], def: "decoration" },
    { k: "look", label: "Looks like", type: "choice", options: [["model", "3D model (FBX)"], ["sprite", "2D sprite (pictures that always face the player)"]], def: "model", showIf: ["type", ["enemy", "decoration", "pickup", "exit", "npc"]] },
    { k: "asset", label: "Model (FBX)", type: "asset", assetType: "model", def: "", showIf: ["look", "model"] },
    // 2D sprites: each picture holds the frames of one animation side by side (a strip). They always turn to face the player.
    { k: "spriteIdle", label: "Standing picture / animation strip", type: "asset", assetType: "image", def: "", showIf: ["look", "sprite"] }, NUM("spriteIdleFrames", "Frames in it", 1, 1, 64, 1, { showIf: ["look", "sprite"] }),
    { k: "spriteWalk", label: "Walking strip", type: "asset", assetType: "image", def: "", showIf: ["look", "sprite"], showIf2: ["type", ["enemy", "npc"]] }, NUM("spriteWalkFrames", "Frames in it", 1, 1, 64, 1, { showIf: ["look", "sprite"], showIf2: ["type", ["enemy", "npc"]] }),
    { k: "spriteAttack", label: "Attack strip", type: "asset", assetType: "image", def: "", showIf: ["look", "sprite"], showIf2: ["type", "enemy"] }, NUM("spriteAttackFrames", "Frames in it", 1, 1, 64, 1, { showIf: ["look", "sprite"], showIf2: ["type", "enemy"] }),
    { k: "spriteHurt", label: "Hurt strip", type: "asset", assetType: "image", def: "", showIf: ["look", "sprite"], showIf2: ["type", "enemy"] }, NUM("spriteHurtFrames", "Frames in it", 1, 1, 64, 1, { showIf: ["look", "sprite"], showIf2: ["type", "enemy"] }),
    { k: "spriteDeath", label: "Death strip (its last frame stays as the body)", type: "asset", assetType: "image", def: "", showIf: ["look", "sprite"], showIf2: ["type", "enemy"] }, NUM("spriteDeathFrames", "Frames in it", 1, 1, 64, 1, { showIf: ["look", "sprite"], showIf2: ["type", "enemy"] }),
    NUM("spriteFps", "Animation speed (frames per second)", 8, 1, 60, 1, { showIf: ["look", "sprite"] }), NUM("spriteHeight", "Sprite height (units, 128 = a story)", 110, 4, 1024, 1, { showIf: ["look", "sprite"] }),
    NUM("scale", "Scale", 1, 0.001, 100, 0.001), NUM("rx", "Rotate X", 0, -180, 180, 1), NUM("ry", "Rotate Y", 0, -180, 180, 1), NUM("rz", "Rotate Z", 0, -180, 180, 1),
    NUM("ox", "Move X (sideways)", 0, -500, 500, 0.5), NUM("oy", "Move Y (up)", 0, -500, 500, 0.5), NUM("oz", "Move Z (toward the front)", 0, -500, 500, 0.5),
    ANIM("idleAnim", "Idle animation (loops)"),
    NUM("doorCellsW", "Door width (blocks of 2 m)", 1, 1, 8, 1, { showIf: ["type", "door"] }), NUM("doorCellsH", "Door height (stories of 2 m)", 1, 1, 4, 1, { showIf: ["type", "door"] }), { k: "doorFit", label: "Fit the model to the block automatically (fills the doorway, never sticks into walls)", type: "bool", def: true, showIf: ["type", "door"] },
    { k: "openMode", label: "Opens", type: "choice", options: [["interact", "When interacted with (Interact key)"], ["proximity", "When the player is close"], ["shot", "When shot"]], def: "interact", showIf: ["type", "door"] },
    NUM("openRange", "Opens when closer than (units)", 110, 20, 600, 5, { showIf: ["type", "door"], showIf2: ["openMode", "proximity"] }), NUM("interactRange", "Can be used from (units)", 90, 20, 400, 5, { showIf: ["type", "door"], showIf2: ["openMode", "interact"] }),
    ANIM("openAnim", "Opening animation", { showIf: ["type", "door"] }), ANIM("closeAnim", "Closing animation (empty = opening played backwards)", { showIf: ["type", "door"] }),
    { k: "autoClose", label: "Closes again by itself", type: "bool", def: true, showIf: ["type", "door"] }, NUM("closeAfter", "Closes after (seconds)", 3, 0.5, 60, 0.5, { showIf: ["type", "door"], showIf2: ["autoClose", true] }),
    { k: "openSound", label: "Opening sound", type: "asset", assetType: "sound", def: "", showIf: ["type", "door"] }, { k: "closeSound", label: "Closing sound", type: "asset", assetType: "sound", def: "", showIf: ["type", "door"] },
    { k: "giveTag", label: "Changes this number tag", type: "tagRef", refType: "number", def: "", showIf: ["type", "pickup"] }, NUM("giveAmount", "By (negative = subtract)", 10, -100000, 100000, 1, { showIf: ["type", "pickup"] }),
    NUM("pickRadius", "Pick-up distance (units)", 28, 8, 200, 1, { showIf: ["type", "pickup"] }), { k: "pickSound", label: "Pick-up sound", type: "asset", assetType: "sound", def: "", showIf: ["type", "pickup"] },
    // material (Material foldout of the Object Editor): base colour map + emission
    { k: "matTexture", label: "Base color map (replaces the model's own picture)", type: "asset", assetType: "image", def: "" },
    { k: "matEmitMap", label: "Emission map (bright parts glow; empty = the whole object glows)", type: "asset", assetType: "image", def: "" }, NUM("matEmit", "Emission strength (0 = no glow)", 0, 0, 8, 0.05),
    // enemy
    NUM("health", "Health", 100, 1, 100000, 1, { showIf: ["type", "enemy"] }), NUM("speed", "Walking speed (units / second)", 150, 0, 2000, 5, { showIf: ["type", "enemy"] }),
    NUM("radius", "Body width (radius, units)", 22, 4, 200, 1, { showIf: ["type", ["enemy", "npc"]] }), NUM("height", "Body height (units, 64 = 1 m)", 110, 10, 600, 1, { showIf: ["type", "enemy"] }),
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
  // ---- objects that give off light (lamps, torches, glowing crystals...): a point light above the object, optionally casting real-time shadows
  S.PROP.push({ k: "lightOn", label: "Gives off light", type: "bool", def: false, showIf: ["type", ["decoration", "pickup", "enemy", "exit", "npc"]] },
    { k: "lightColor", label: "Light colour", type: "color", def: "#ffd9a0", showIf: ["lightOn", true] }, NUM("lightIntensity", "Light brightness", 1.6, 0, 10, 0.05, { showIf: ["lightOn", true] }),
    NUM("lightRange", "Light reach (units, 128 = a cell)", 420, 32, 4000, 8, { showIf: ["lightOn", true] }), NUM("lightHeight", "Light height above the floor (units)", 80, -64, 512, 1, { showIf: ["lightOn", true] }),
    NUM("lightFlicker", "Flicker (0 = steady, 1 = like a torch)", 0, 0, 1, 0.01, { showIf: ["lightOn", true] }), { k: "lightShadows", label: "Casts shadows", type: "bool", def: true, showIf: ["lightOn", true] });
  // ---- NPC talk (characters): talk settings are prop tags; the dialogue is a list of lines (structure prop.dialog.lines = [{id}]) whose texts are tags <prop>.d.<line>.*
  S.PROP.push({ k: "spkSound", label: "Sound", type: "asset", assetType: "sound", def: "", showIf: ["type", "speaker"] }, NUM("spkVolume", "Volume", 1, 0, 1, 0.01, { showIf: ["type", "speaker"] }),
    NUM("spkRadius", "Heard within (units, 128 = a cell; silent outside)", 512, 16, 8000, 8, { showIf: ["type", "speaker"] }), NUM("spkHeight", "Sound height above the floor", 64, 0, 1024, 1, { showIf: ["type", "speaker"] }),
    { k: "spkLoop", label: "Loops forever (off = plays once each time you walk into its radius)", type: "bool", def: true, showIf: ["type", "speaker"] },
    { k: "spkShow", label: "Model visible in the game", type: "bool", def: false, showIf: ["type", "speaker"] });
  S.PROP.push({ k: "npcName", label: "Name shown when talking (empty = the object's name)", type: "string", def: "", showIf: ["type", "npc"] }, NUM("talkRange", "Can talk from (units, 128 = a cell)", 140, 30, 600, 5, { showIf: ["type", "npc"] }),
    ANIM("talkAnim", "Talking animation (3D model)", { showIf: ["type", "npc"], showIf2: ["look", "model"] }), { k: "talkSprite", label: "Talking strip (2D sprite)", type: "asset", assetType: "image", def: "", showIf: ["type", "npc"], showIf2: ["look", "sprite"] }, NUM("talkSpriteFrames", "Frames in it", 1, 1, 64, 1, { showIf: ["type", "npc"], showIf2: ["look", "sprite"] }),
    { k: "faceTalker", label: "Turns to face the player while talking", type: "bool", def: true, showIf: ["type", "npc"] });
  const goOpts = [["next", "the next line"], ["end", "the end (stop talking)"]];
  S.DLINE = [{ k: "speaker", label: "Who speaks (empty = the character)", type: "string", def: "" }, { k: "text", label: "What they say", type: "string", def: "..." }, { k: "sound", label: "Voice / sound", type: "asset", assetType: "sound", def: "" },
    { k: "next", label: "Then go to", type: "choice", options: goOpts, def: "next" }]
    .concat(...[1, 2, 3, 4].map(i => [{ k: "c" + i + "text", label: "Choice " + i + " (empty = no choice)", type: "string", def: "" }, { k: "c" + i + "go", label: "Choice " + i + " goes to", type: "choice", options: goOpts, def: "next" },
      { k: "c" + i + "tag", label: "Choice " + i + " changes the tag (optional)", type: "tagRef", refType: "any", def: "" }, { k: "c" + i + "val", label: "to (a number: 5, or +1 / -1 to add)", type: "string", def: "" }]));
  S.dlineOptions = p => goOpts.concat(((p.dialog && p.dialog.lines) || []).map((l, i) => [l.id, "line " + (i + 1) + " (" + l.id + ")"]));
  S.addDLine = (project, p, over) => { p.dialog = p.dialog || { lines: [] }; const l = { id: S.uid("line", p.dialog.lines.map(x => x.id)) }; p.dialog.lines.push(l); for (const pr of S.DLINE) { const r = rec(pr, "dialog", p.id); if (over && pr.k in over) r.value = over[pr.k]; project.tags[p.id + ".d." + l.id + "." + pr.k] = r; } S.syncDOpts(project, p); return l; };
  S.removeDLine = (project, p, l) => { p.dialog.lines.splice(p.dialog.lines.indexOf(l), 1); for (const k of Object.keys(project.tags)) if (k.startsWith(p.id + ".d." + l.id + ".")) delete project.tags[k]; S.syncDOpts(project, p); };
  S.syncDOpts = (project, p) => { const o = S.dlineOptions(p); for (const l of (p.dialog && p.dialog.lines) || []) for (const k of ["next", "c1go", "c2go", "c3go", "c4go"]) { const r = project.tags[p.id + ".d." + l.id + "." + k]; if (r) { r.options = o; if (!o.some(x => x[0] === r.value)) r.value = "next"; } } };
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
  // every node can have a condition: it only happens while a tag compares true (e.g. canChargeWeapon = yes). Empty tag = always.
  S.SCOND = [{ k: "needTag", label: "Only if this tag…", type: "tagRef", refType: "any", def: "" }, { k: "needOp", label: "is", type: "choice", options: [["=", "equal to"], ["!=", "not equal to"], [">", "greater than"], ["<", "less than"], [">=", "at least"], ["<=", "at most"]], def: "=" }, { k: "needVal", label: "this value (yes / no, a number, a text)", type: "string", def: "yes" }];
  S.ensureSNode = (project, ownerKey, n, modelTag) => { const def = S.SNODES[n.type]; if (!def) return; for (const p of def.props.concat(S.SCOND)) { const k = SKEY(ownerKey, n.id, p.k); if (!project.tags[k]) { const r = rec(p, "script", ownerKey); if (p.type === "anim") r.modelTag = modelTag; project.tags[k] = r; } } };
  S.defineSNode = (project, ownerKey, node, over, modelTag) => { for (const p of S.SNODES[node.type].props.concat(S.SCOND)) { const r = rec(p, "script", ownerKey); if (p.type === "anim") r.modelTag = modelTag; if (over && p.k in over) r.value = over[p.k]; project.tags[SKEY(ownerKey, node.id, p.k)] = r; } };
  S.addSNode = (project, ownerKey, script, type, x, y, over, modelTag) => { const node = { id: S.uid("n", script.nodes.map(n => n.id)), type, x: Math.round(x), y: Math.round(y) }; script.nodes.push(node); S.defineSNode(project, ownerKey, node, over, modelTag); return node; };
  S.removeSNode = (project, ownerKey, script, node) => { for (const p of S.SNODES[node.type].props.concat(S.SCOND)) delete project.tags[SKEY(ownerKey, node.id, p.k)]; script.nodes.splice(script.nodes.indexOf(node), 1); script.links = script.links.filter(l => l.a !== node.id && l.b !== node.id); };
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
  // material tags that existed before (removed: only a base colour map and an emission are left)
  const OLDMAT = ["matTint", "matEmitColor", "matLit", "matOpacity", "matShine", "matRough", "matMetal", "matReflect", "doorWidth", "doorHeight"];
  S.ensureWalls = project => {
    if (!project.props) project.props = []; delete project.walls; delete project.anims;
    // maps: only grid maps exist now; older room/node maps are removed (owner decision: start fresh)
    const old = (project.maps || []).filter(m => m.style !== "grid"); if (old.length) { project.maps = project.maps.filter(m => m.style === "grid"); for (const m of old) for (const k of Object.keys(project.tags)) if (k.startsWith(m.id + ".")) delete project.tags[k]; project.droppedMaps = (project.droppedMaps || 0) + old.length; }
    for (const k of Object.keys(project.tags)) if (/^wall\d+\./.test(k)) delete project.tags[k];
    for (const m of project.maps || []) { if (!m.things) m.things = []; if (!m.layers) m.layers = [{ cells: {} }]; if (!m.n) m.n = { t: m.things.length }; for (const p of S.MAP) if (!project.tags[m.id + "." + p.k]) project.tags[m.id + "." + p.k] = rec(p, m.id, m.id); for (const k of ["fogDistance", "gridSize", "wallThickness"]) delete project.tags[m.id + "." + k]; }
    for (const p of project.props) if (p.dialog) { for (const l of p.dialog.lines) for (const pr of S.DLINE) if (!project.tags[p.id + ".d." + l.id + "." + pr.k]) project.tags[p.id + ".d." + l.id + "." + pr.k] = rec(pr, "dialog", p.id); S.syncDOpts(project, p); }
    for (const p of project.props) { for (const k of OLDMAT) delete project.tags[p.id + "." + k]; for (const pr of S.PROP) { const r = project.tags[p.id + "." + pr.k]; if (!r) { const n = rec(pr, "props", p.id); if (pr.type === "anim") n.modelTag = p.id + ".asset"; project.tags[p.id + "." + pr.k] = n; } else { r.label = pr.label; if (pr.max !== undefined) { r.max = pr.max; if (+r.value > pr.max) r.value = pr.max; } } } }
    { const ik = project.tags["player.interactKey"]; if (project.tags["player.speed"] && !ik) project.tags["player.interactKey"] = rec(S.PLAYER.find(p => p.k === "interactKey"), "player", "player"); } if (project.tags["player.speed"]) for (const pp of S.PLAYER) if (!project.tags["player." + pp.k]) project.tags["player." + pp.k] = rec(pp, "player", "player");
    for (const att of (project.player && project.player.attachments) || []) for (const k of OLDMAT) delete project.tags["player." + att.id + "." + k];
    const eh = project.tags["player.eyeHeight"]; if (eh && !project.eyeMoved) { if (eh.value === 56) eh.value = 102; project.eyeMoved = true; }
  };
  S.ensurePlayer = project => {
    S.ensureWalls(project);
    if (!project.events) project.events = [];
    if (!project.animLen) project.animLen = [];
    project.player = project.player || { attachments: [] }; if (!project.player.attachments) project.player.attachments = [];
    delete project.tags["player.freeAim"];
    for (const [n, label] of [["gfx.culling", "Culling (skip what you can not see)"], ["gfx.lod", "Auto-LOD (simpler models far away)"], ["gfx.shadows", "Real-time shadows"], ["gfx.bloom", "Bloom (glow)"]]) if (!project.tags[n]) project.tags[n] = { type: "bool", value: true, label, group: "gfx", builtin: true };
    for (const att of project.player.attachments) {   // bring older objects up to date: a "weapon" tick became the object type
      const pre = "player." + att.id + ".", old = project.tags[pre + "weapon"];
      for (const p of S.ATTACH) { const r = project.tags[pre + p.k]; if (!r) { const n = rec(p, "player", "player." + att.id); if (p.type === "anim") n.modelTag = pre + "asset"; if (p.k === "type") n.value = old && old.value ? "weapon" : "animation"; if (p.k === "name") n.value = old && old.value ? "Weapon" : "Object"; project.tags[pre + p.k] = n; } else { r.label = p.label; if (p.showIf) r.showIf = p.showIf; else delete r.showIf; if (p.options) r.options = p.options; } }
      delete project.tags[pre + "weapon"];
      if (att.script) for (const n of att.script.nodes) S.ensureSNode(project, "player." + att.id, n, pre + "asset");
      if (!att.script) att.script = { nodes: [], links: [] };   // the old "click animation" menu became a script: when clicked, play that animation
      const fa = project.tags[pre + "fireAnim"]; if (fa) { if (fa.value && !att.script.nodes.length) { const a = S.addSNode(project, "player." + att.id, att.script, "onClick", 20, 20), b = S.addSNode(project, "player." + att.id, att.script, "playAnim", 260, 20, { anim: fa.value }, pre + "asset"); att.script.links.push({ a: a.id, o: "next", b: b.id }); } delete project.tags[pre + "fireAnim"]; }
    }
    for (const p of S.PLAYER) if (!project.tags["player." + p.k]) { const r = rec(p, "player", "player"); r.builtin = true; project.tags["player." + p.k] = r; }
    for (const sc of project.screens || []) for (const el of sc.nodes || []) if (el.script) for (const n of el.script.nodes) S.ensureSNode(project, sc.id + "." + el.id, n);
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

  // the look of an object, from its Material tab tags: base colour map + emission
  S.matOf = (T, pre) => ({ lit: 1, emitK: +T.get(pre + "matEmit") || 0, texId: T.get(pre + "matTexture") || "", emitTexId: T.get(pre + "matEmitMap") || "" });
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
    B("gfx.shadows", "bool", true, "Real-time shadows", "gfx"); B("gfx.bloom", "bool", true, "Bloom (glow)", "gfx");
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
    add(set, "fullscreen", "toggle", { label: "Fullscreen", bind: "gfx.fullscreen", y: 430 });
    add(set, "showfps", "toggle", { label: "Show FPS", bind: "gfx.showFps", y: 475 });
    add(set, "shadows", "toggle", { label: "Shadows", bind: "gfx.shadows", y: 520 });
    add(set, "bloom", "toggle", { label: "Bloom (glow)", bind: "gfx.bloom", y: 565 });
    add(set, "back", "button", { text: "Back", y: 645, action: "goto", target: "main" });
    p.origin = "center"; S.ensurePlayer(p);
    return p;
  };
})();
