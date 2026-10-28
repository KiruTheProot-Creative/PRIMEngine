/* PRIMEngine (Prime edition): what a project is made of.
   Structure (rooms, brushes, things, beams, HUD screens) lives in plain lists; EVERY value is a tag (PRIM.Tags), named
   <id>.<property>  (b12.top.tex, t4.lock, beam1.damage, player.speed, visor.scan.hud ...).
   Units: 64 = 1 metre, the grid is 32 (0.5 m). Y is up. */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), D = (PRIM.PE = {});
  const N = (k, label, def, min, max, step, x) => Object.assign({ k, label, type: "number", def, min, max, step }, x || {});
  const B = (k, label, def, x) => Object.assign({ k, label, type: "bool", def: !!def }, x || {});
  const C = (k, label, def, x) => Object.assign({ k, label, type: "color", def }, x || {});
  const S = (k, label, def, x) => Object.assign({ k, label, type: "string", def: def || "" }, x || {});
  const A = (k, label, at, x) => Object.assign({ k, label, type: "asset", assetType: at, def: "" }, x || {});
  const CH = (k, label, options, def, x) => Object.assign({ k, label, type: "choice", options, def }, x || {});
  const AN = (k, label, x) => Object.assign({ k, label, type: "anim", def: "" }, x || {});
  D.N = N; D.B = B; D.C = C; D.S = S; D.A = A; D.CH = CH;

  D.KEYS = [["mouse1", "Left mouse button"], ["mouse2", "Right mouse button"], ["mouse3", "Middle mouse button"], ["space", "Space"], ["shift", "Shift"], ["ctrl", "Ctrl"], ["alt", "Alt"], ["tab", "Tab"], ["enter", "Enter"],
    ...[..."abcdefghijklmnopqrstuvwxyz0123456789"].map(c => [c, c.toUpperCase()]), ["arrowup", "Arrow up"], ["arrowdown", "Arrow down"], ["arrowleft", "Arrow left"], ["arrowright", "Arrow right"]];
  const KEY = (k, label, def) => CH(k, label, D.KEYS, def);

  // ---------------------------------------------------------------- the world
  D.WORLD = [C("sky", "Sky colour", "#0b1622"), C("fog", "Fog colour", "#0a121b"), N("fogDistance", "Fog distance (units, 64 = 1 m)", 4000, 200, 40000, 50), N("grid", "Grid (units, 32 = 0.5 m)", 32, 4, 256, 4)];
  D.ROOM = [S("name", "Name", "Room"), N("ambient", "Ambient light", 0.85, 0, 2, 0.05), C("ambientColor", "Ambient colour", "#ffffff"), A("music", "Music", "sound")];
  // a brush: box or ramp. Faces get their own texture / colour
  D.FACES = { box: ["top", "bottom", "north", "south", "east", "west"], ramp: ["slope", "bottom", "back", "left", "right"] };
  D.FACE_LABEL = { top: "Top", bottom: "Bottom", north: "North (+Z)", south: "South (-Z)", east: "East (+X)", west: "West (-X)", slope: "Slope", back: "Tall end", left: "Left side", right: "Right side" };
  D.BRUSH = [A("tex", "Texture (all faces)", "image"), C("color", "Colour (all faces)", "#9aa6b2"), N("uvScale", "Texture size (metres per repeat)", 1, 0.1, 32, 0.05),
    B("solid", "Solid (blocks the player)", true), B("xrayOnly", "Only visible with the X-ray visor", false), N("heat", "Heat (shows in the Thermal visor)", 0, 0, 1, 0.05)];
  D.FACE = [A("tex", "Texture", "image"), C("color", "Colour", "")];
  // things placed in rooms
  const SCAN = [S("scanTitle", "Logbook title", ""), S("scanText", "Logbook text", "", { multiline: true }), A("scanImage", "Logbook picture", "image"), S("scanCategory", "Logbook category", "Research"), N("scanTime", "Scan time (s)", 1.2, 0.1, 10, 0.1)];
  D.THING_KINDS = {
    start: { label: "Player start", icon: "▲", props: [] },
    light: { label: "Light", icon: "✹", props: [C("color", "Colour", "#ffe8c4"), N("intensity", "Brightness", 1.4, 0, 10, 0.05), N("range", "Reach (units)", 520, 20, 6000, 10), N("flicker", "Flicker", 0, 0, 1, 0.05)] },
    door: { label: "Door", icon: "▯", props: [N("width", "Width (units)", 128, 32, 512, 8), N("height", "Height (units)", 160, 32, 512, 8),
      CH("lock", "Opens with", [["any", "Any beam (blue)"], ["beam", "One beam only"], ["missile", "Missiles only (red)"], ["locked", "Never (sealed)"], ["open", "No shield (always open)"]], "any"),
      S("lockBeam", "Beam that opens it", "", { showIf: ["lock", "beam"], ref: "beam" }), N("closeAfter", "Closes again after (s, 0 = stays open)", 4, 0, 60, 0.5), A("openSound", "Opening sound", "sound")].concat(SCAN) },
    pickup: { label: "Power-up / pick-up", icon: "◆", props: [
      CH("item", "Gives", [["energyTank", "Energy tank (+100 max energy)"], ["missileExpansion", "Missile expansion (+missiles)"], ["missileLauncher", "Missile launcher"], ["beam", "A beam"], ["visor", "A visor"], ["charge", "Charge beam"], ["combo", "Charge combos"], ["energy", "Energy refill"], ["missiles", "Missile refill"], ["flag", "A custom item (tag)"]], "energyTank"),
      N("amount", "Amount", 5, 0, 10000, 1, { showIf: ["item", ["missileExpansion", "missileLauncher", "energy", "missiles"]] }), S("beam", "Beam", "", { showIf: ["item", "beam"], ref: "beam" }),
      CH("visor", "Visor", [["scan", "Scan"], ["thermal", "Thermal"], ["xray", "X-ray"]], "scan", { showIf: ["item", "visor"] }), S("flag", "Item tag name", "item.key", { showIf: ["item", "flag"] }),
      S("message", "Message when collected (empty = automatic)", ""), A("model", "Model (empty = built-in shape)", "model"), N("scale", "Model scale", 1, 0.01, 100, 0.01), C("color", "Shape colour", "#4fe3ff"), A("sound", "Sound", "sound")].concat(SCAN) },
    enemy: { label: "Enemy", icon: "☠", props: [A("model", "Model (empty = built-in shape)", "model"), N("scale", "Model scale", 1, 0.01, 100, 0.01), C("color", "Shape colour", "#d0553a"),
      N("health", "Health", 60, 1, 100000, 1), N("speed", "Speed (units / s)", 160, 0, 3000, 5), B("flying", "Flies", false), N("hover", "Flying height (units)", 120, 0, 2000, 4, { showIf: ["flying", true] }),
      N("radius", "Body radius", 28, 4, 400, 1), N("height", "Body height", 96, 8, 800, 1), N("sight", "Sees you from (units)", 1100, 50, 20000, 10),
      CH("attack", "Attack", [["melee", "Melee"], ["shot", "Shoots projectiles"]], "melee"), N("damage", "Damage", 10, 0, 1000, 1), N("attackRange", "Attack range (units)", 90, 10, 6000, 5), N("attackRate", "Time between attacks (s)", 1.4, 0.1, 30, 0.05),
      N("shotSpeed", "Projectile speed", 700, 50, 6000, 10, { showIf: ["attack", "shot"] }), C("shotColor", "Projectile colour", "#ff7a3a", { showIf: ["attack", "shot"] }),
      CH("weakTo", "Can be hurt by", [["all", "Everything"], ["beam", "One beam only"], ["missile", "Missiles only"]], "all"), S("weakBeam", "Beam", "", { showIf: ["weakTo", "beam"], ref: "beam" }),
      N("heat", "Heat (Thermal visor)", 1, 0, 1, 0.05), B("xrayOnly", "Only visible with the X-ray visor", false), B("lockable", "Can be locked on to", true),
      N("dropEnergy", "Chance to drop energy (0-1)", 0.5, 0, 1, 0.05), N("dropMissiles", "Chance to drop missiles (0-1)", 0.25, 0, 1, 0.05),
      AN("idleAnim", "Idle animation"), AN("walkAnim", "Moving animation"), AN("attackAnim", "Attack animation"), AN("deathAnim", "Death animation"),
      A("hurtSound", "Hurt sound", "sound"), A("deathSound", "Death sound", "sound"), A("attackSound", "Attack sound", "sound")].concat(SCAN) },
    scan: { label: "Scan point", icon: "◎", props: [N("radius", "Size of the scan target (units)", 24, 4, 400, 1)].concat(SCAN) },
    model: { label: "Model", icon: "▣", props: [A("model", "Model (FBX)", "model"), N("scale", "Scale", 1, 0.001, 1000, 0.001), N("rx", "Turn X", 0, -180, 180, 1), N("rz", "Turn Z", 0, -180, 180, 1), AN("anim", "Animation (loops)"),
      B("solid", "Solid (box around it)", false), B("xrayOnly", "Only visible with the X-ray visor", false), N("heat", "Heat (Thermal visor)", 0, 0, 1, 0.05), B("lockable", "Can be locked on to (a lock point)", false)].concat(SCAN) },
  };

  // ---------------------------------------------------------------- player, visors, beams
  D.PLAYER = [
    N("speed", "Run speed (units / s)", 330, 20, 3000, 5), N("jumpHeight", "Jump height (units)", 110, 0, 1200, 2), N("gravity", "Gravity", 1700, 50, 20000, 10), N("airControl", "Steering in the air (0-1)", 0.6, 0, 1, 0.05),
    N("height", "Body height (units)", 150, 32, 600, 2), N("eye", "Eye height (units)", 128, 16, 580, 2), N("radius", "Body radius (units)", 22, 4, 200, 1), N("step", "Step height (units)", 22, 0, 128, 1), N("maxSlope", "Steepest walkable slope (degrees)", 46, 5, 85, 1),
    N("fov", "Field of view (degrees)", 78, 40, 120, 1), N("mouseSens", "Mouse sensitivity", 1, 0.1, 5, 0.05),
    N("lockRange", "Lock-on range (units)", 1400, 100, 20000, 10), N("lockAngle", "Lock-on cone (degrees)", 32, 2, 90, 1), N("dashSpeed", "Dash speed (units / s)", 620, 0, 5000, 10), N("dashTime", "Dash time (s)", 0.28, 0.05, 2, 0.01),
    N("energyPerTank", "Energy per tank", 100, 1, 10000, 1), N("startEnergy", "Energy at the start", 99, 1, 100000, 1), N("startTanks", "Energy tanks at the start", 0, 0, 99, 1), N("startMissiles", "Missiles at the start (0 = no launcher)", 0, 0, 999, 1),
    B("startCharge", "Starts with the charge beam", true), B("startCombo", "Starts with charge combos", false), N("missileDamage", "Missile damage", 40, 0, 10000, 1), N("missileSpeed", "Missile speed", 1300, 50, 8000, 10), C("missileColor", "Missile colour", "#ff9d3a"),
    N("chargeTime", "Charge time (s)", 1, 0.1, 10, 0.05), N("comboMissiles", "Missiles used by a charge combo", 5, 0, 100, 1),
    A("cannon", "Arm cannon model (empty = built-in shape)", "model"), N("cannonScale", "Cannon scale", 1, 0.001, 100, 0.001), N("cannonX", "Cannon right", 9, -100, 100, 0.5), N("cannonY", "Cannon down", 7, -100, 100, 0.5), N("cannonZ", "Cannon forward", 16, -100, 200, 0.5),
    N("cannonRx", "Cannon turn X", 0, -180, 180, 1), N("cannonRy", "Cannon turn Y", 0, -180, 180, 1), N("cannonRz", "Cannon turn Z", 0, -180, 180, 1), C("cannonColor", "Built-in cannon colour", "#c9a24a"), AN("cannonFire", "Cannon fire animation", { modelTag: "player.cannon" }),
    KEY("fireKey", "Fire / scan", "mouse1"), KEY("lockKey", "Lock on (hold)", "mouse2"), KEY("missileKey", "Missile", "f"), KEY("jumpKey", "Jump / dash", "space"),
    S("beamKeys", "Beam keys (comma list)", "1,2,3,4"), KEY("visorCombat", "Combat visor", "z"), KEY("visorScan", "Scan visor", "x"), KEY("visorThermal", "Thermal visor", "c"), KEY("visorXray", "X-ray visor", "v"),
    A("shotSound", "Default beam sound", "sound"), A("missileSound", "Missile sound", "sound"), A("hurtSound", "Hurt sound", "sound"), A("pickupSound", "Item fanfare", "sound"), A("scanSound", "Scan complete sound", "sound"), A("lockSound", "Lock-on sound", "sound")];
  D.PLAYER_GROUPS = [["Movement", ["speed", "jumpHeight", "gravity", "airControl", "dashSpeed", "dashTime"]], ["Body", ["height", "eye", "radius", "step", "maxSlope"]], ["Camera", ["fov", "mouseSens", "lockRange", "lockAngle"]],
    ["Start of the game", ["startEnergy", "energyPerTank", "startTanks", "startMissiles", "startCharge", "startCombo"]], ["Weapons", ["missileDamage", "missileSpeed", "missileColor", "chargeTime", "comboMissiles"]], ["Arm cannon", ["cannon", "cannonScale", "cannonX", "cannonY", "cannonZ", "cannonRx", "cannonRy", "cannonRz", "cannonColor", "cannonFire"]],
    ["Controls", ["fireKey", "lockKey", "missileKey", "jumpKey", "beamKeys", "visorCombat", "visorScan", "visorThermal", "visorXray"]], ["Sounds", ["shotSound", "missileSound", "hurtSound", "pickupSound", "scanSound", "lockSound"]]];
  D.VISORS = [["combat", "Combat"], ["scan", "Scan"], ["thermal", "Thermal"], ["xray", "X-ray"]];
  D.VISOR = [B("start", "Available from the start", false), S("hud", "HUD screen", "", { ref: "screen" }), A("sound", "Switch sound", "sound")];
  D.BEAM = [S("name", "Name", "Beam"), C("color", "Colour", "#ffd23a"), B("start", "Available from the start", false), N("damage", "Damage", 10, 0, 10000, 1), N("rate", "Shots per second", 4, 0.2, 30, 0.1),
    N("speed", "Shot speed (units / s)", 1700, 50, 20000, 10), N("size", "Shot size", 5, 0.5, 100, 0.5), CH("pattern", "Shot pattern", [["single", "Single bolt"], ["spread", "Spread (3 bolts)"], ["wave", "Wave (wobbles)"], ["rapid", "Rapid stream"]], "single"),
    N("chargeDamage", "Charged shot damage", 50, 0, 100000, 1), N("chargeSize", "Charged shot size", 14, 1, 200, 0.5), N("freeze", "Freezes enemies for (s)", 0, 0, 30, 0.1), N("burn", "Burn damage per second", 0, 0, 1000, 1),
    N("comboDamage", "Charge combo damage", 160, 0, 100000, 1), CH("combo", "Charge combo", [["blast", "Big blast"], ["volley", "Missile volley"], ["stream", "Beam stream"]], "blast"), A("sound", "Shot sound", "sound"), A("chargeSound", "Charging sound (loops)", "sound")];
  D.DEFAULT_BEAMS = [{ name: "Pulse", color: "#ffd23a", start: true, damage: 10, rate: 4, pattern: "single" }, { name: "Arc", color: "#c27bff", damage: 14, rate: 3, pattern: "wave", comboDamage: 200 },
    { name: "Frost", color: "#bff4ff", damage: 18, rate: 1.6, speed: 1300, pattern: "single", freeze: 3 }, { name: "Flame", color: "#ff6a2a", damage: 12, rate: 5, pattern: "spread", burn: 6 }];

  // HUD screens (drawn by PRIM.HUD, edited with PRIM.HudEditor): one per visor
  D.HUD_NODE = {
    title: { label: "Text", icon: "T", props: [S("text", "Text", "Text"), CH("font", "Font", [["builtin:display", "Display"], ["builtin:mono", "Mono"], ["builtin:sans", "Sans"]], "builtin:mono"), N("size", "Size", 28, 6, 300, 1), C("color", "Colour", "#bff4ff"), N("x", "X (0 = middle)", 0, -4000, 4000, 1), N("y", "Y (0 = middle, up +)", 0, -4000, 4000, 1), N("opacity", "Opacity", 1, 0, 1, 0.05)] },
    image: { label: "Image", icon: "▤", props: [A("asset", "Image", "image"), N("x", "X (0 = middle)", 0, -4000, 4000, 1), N("y", "Y (0 = middle, up +)", 0, -4000, 4000, 1), N("w", "Width (0 = natural)", 0, 0, 8000, 1), N("h", "Height (0 = auto)", 0, 0, 8000, 1), B("stretch", "Stretch to screen (a visor frame)", false), N("opacity", "Opacity", 1, 0, 1, 0.05)] },
    bar: { label: "Bar", icon: "▬", props: [S("bind", "Shows tag", "player.energyNow", { ref: "tag" }), S("max", "Full when tag", "player.energyMax", { ref: "tag" }), S("label", "Label", ""), CH("font", "Font", [["builtin:display", "Display"], ["builtin:mono", "Mono"]], "builtin:mono"), N("size", "Text size", 14, 6, 100, 1),
      C("textColor", "Text colour", "#ffffff"), C("color", "Fill colour", "#7df3ff"), C("bg", "Back colour", "#0b2a33"), N("x", "X", 0, -4000, 4000, 1), N("y", "Y", 0, -4000, 4000, 1), N("w", "Width", 260, 4, 4000, 1), N("h", "Height", 12, 2, 400, 1), N("opacity", "Opacity", 1, 0, 1, 0.05)] },
    tanks: { label: "Energy tanks", icon: "▦", props: [N("x", "X", 0, -4000, 4000, 1), N("y", "Y", 0, -4000, 4000, 1), N("box", "Box size", 14, 2, 100, 1), C("color", "Full colour", "#7df3ff"), C("empty", "Empty colour", "#17343d"), N("opacity", "Opacity", 1, 0, 1, 0.05)] },
  };

  // ---------------------------------------------------------------- tag helpers
  D.uid = (base, taken) => { let i = 1; while (taken.includes(base + i)) i++; return base + i; };
  D.rec = (p, owner) => { const r = { type: p.type, value: p.def, label: p.label, owner }; for (const k of ["min", "max", "step", "options", "assetType", "showIf", "ref", "multiline", "hidden", "modelTag"]) if (p[k] !== undefined) r[k] = p[k]; r.prop = p.k; return r; };
  D.define = (project, pre, props, over) => { for (const p of props) { const n = pre + "." + p.k; if (project.tags[n] && !over) continue; const r = D.rec(p, pre); if (over && p.k in over) r.value = over[p.k]; else if (project.tags[n]) continue; project.tags[n] = r; } };
  D.dropTags = (project, pre) => { for (const k of Object.keys(project.tags)) if (k === pre || k.startsWith(pre + ".")) delete project.tags[k]; };
  D.shown = (T, pre, p) => { if (!p.showIf || p.hidden) return !p.hidden; const v = T.get(pre + "." + p.showIf[0]), want = p.showIf[1]; return Array.isArray(want) ? want.includes(v) : v === want; };
  D.nextId = (project, base) => { project.n = project.n || {}; project.n[base] = (project.n[base] || 0) + 1; return base + project.n[base]; };

  // ---------------------------------------------------------------- building blocks
  D.addRoom = (project, over) => { const id = D.nextId(project, "room"), r = { id, brushes: [], things: [] }; project.rooms.push(r); D.define(project, id, D.ROOM, Object.assign({ name: "Room " + project.n.room }, over || {})); return r; };
  D.defineBrush = (project, b, over) => { D.define(project, b.id, D.BRUSH, over || {}); for (const f of D.FACES[b.shape]) D.define(project, b.id + "." + f, D.FACE, {}); };
  D.addBrush = (project, room, shape, p, s, dir, over) => { const b = { id: D.nextId(project, "b"), shape, p: p.slice(), s: s.slice(), dir: dir || 0 }; room.brushes.push(b); D.defineBrush(project, b, over); return b; };
  D.addThing = (project, room, kind, p, yaw, over) => { const t = { id: D.nextId(project, "t"), kind, p: p.slice(), yaw: yaw || 0 }; room.things.push(t); D.define(project, t.id, [S("name", "Name", D.THING_KINDS[kind].label)].concat(D.THING_KINDS[kind].props), over || {}); return t; };
  D.removeBrush = (project, room, b) => { room.brushes.splice(room.brushes.indexOf(b), 1); D.dropTags(project, b.id); };
  D.removeThing = (project, room, t) => { room.things.splice(room.things.indexOf(t), 1); D.dropTags(project, t.id); };
  D.addBeam = (project, over) => { const id = D.nextId(project, "beam"); project.beams.push({ id }); D.define(project, id, D.BEAM, over || {}); return id; };
  D.addHud = (project, id, nodes) => { const sc = { id, kind: "hud", nodes: [] }; project.screens.push(sc); for (const [nid, type, over] of nodes) { sc.nodes.push({ id: nid, type }); D.define(project, id + "." + nid, D.HUD_NODE[type].props, over); } return sc; };
  D.findRoomOf = (project, id) => project.rooms.find(r => r.brushes.some(b => b.id === id) || r.things.some(t => t.id === id) || r.id === id) || null;

  // a new project: one room with a floor and four walls, a start point, a light, the default beams, visors and their HUDs
  D.template = (id, name) => {
    const p = { id, name, kind: "pe", format: 1, created: Date.now(), modified: Date.now(), assets: [], folders: [], tags: {}, rooms: [], beams: [], screens: [], anims: [], n: {} };
    D.define(p, "world", D.WORLD, {}); D.define(p, "player", D.PLAYER, {});
    p.tags["player.energyNow"] = { type: "number", value: 99, label: "Energy now (live)", owner: "player", min: 0, max: 100000, step: 1, live: true };
    p.tags["player.energyMax"] = { type: "number", value: 99, label: "Energy of one tank (live)", owner: "player", min: 0, max: 100000, step: 1, live: true };
    p.tags["player.tanks"] = { type: "number", value: 0, label: "Energy tanks owned (live)", owner: "player", min: 0, max: 99, step: 1, live: true };
    p.tags["player.tanksFull"] = { type: "number", value: 0, label: "Full energy tanks (live)", owner: "player", min: 0, max: 99, step: 1, live: true };
    p.tags["player.missiles"] = { type: "number", value: 0, label: "Missiles (live)", owner: "player", min: 0, max: 999, step: 1, live: true };
    p.tags["player.missilesMax"] = { type: "number", value: 0, label: "Missile capacity (live)", owner: "player", min: 0, max: 999, step: 1, live: true };
    p.tags["player.beamName"] = { type: "string", value: "", label: "Current beam name (live)", owner: "player", live: true };
    p.tags["player.visorName"] = { type: "string", value: "", label: "Current visor name (live)", owner: "player", live: true };
    p.tags["player.charge"] = { type: "number", value: 0, label: "Charge 0-1 (live)", owner: "player", min: 0, max: 1, step: 0.01, live: true };
    for (const [v] of D.VISORS) D.define(p, "visor." + v, D.VISOR, { start: v === "combat" || v === "scan", hud: "hud_" + v });
    D.DEFAULT_BEAMS.forEach(b => D.addBeam(p, b));
    const base = [["energy", "title", { text: "ENERGY {player.energyNow}", x: -560, y: 318, size: 22, color: "#bff4ff" }], ["tanks", "tanks", { x: -560, y: 290 }], ["bar", "bar", { x: -460, y: 268, w: 220, h: 8 }],
      ["missiles", "title", { text: "{player.missiles}", x: 560, y: 318, size: 24, color: "#ffc27a" }], ["beam", "title", { text: "{player.beamName}", x: 540, y: -318, size: 18, color: "#ffe9a6" }], ["visor", "title", { text: "{player.visorName} VISOR", x: -540, y: -318, size: 16, color: "#9fd8e6" }]];
    for (const [v] of D.VISORS) D.addHud(p, "hud_" + v, base);
    const r = D.addRoom(p, { name: "Landing Site" }), g = 32;
    D.addBrush(p, r, "box", [-640, -32, -640], [1280, 32, 1280], 0, { color: "#7a858f" });
    for (const [x, z, w, d] of [[-640, -640, 1280, 32], [-640, 608, 1280, 32], [-640, -608, 32, 1216], [608, -608, 32, 1216]]) D.addBrush(p, r, "box", [x, 0, z], [w, 256, d], 0, { color: "#8d98a3" });
    D.addBrush(p, r, "ramp", [128, 0, -320], [192, 96, 256], 0, { color: "#a3a07c" }); D.addBrush(p, r, "box", [320, 0, -320], [256, 96, 256], 0, { color: "#a3a07c" });
    D.addThing(p, r, "start", [0, 0, 0], 0); D.addThing(p, r, "light", [0, 220, 0], 0); void g;
    return p;
  };
  // older / damaged files: add what is missing
  D.heal = p => {
    p.assets = p.assets || []; p.folders = p.folders || []; p.rooms = p.rooms || []; p.beams = p.beams || []; p.screens = p.screens || []; p.anims = p.anims || []; p.n = p.n || {}; p.tags = p.tags || {};
    D.define(p, "world", D.WORLD); D.define(p, "player", D.PLAYER); for (const [v] of D.VISORS) D.define(p, "visor." + v, D.VISOR);
    for (const r of p.rooms) { D.define(p, r.id, D.ROOM); for (const b of r.brushes) D.defineBrush(p, b); for (const t of r.things) D.define(p, t.id, [S("name", "Name", D.THING_KINDS[t.kind].label)].concat(D.THING_KINDS[t.kind].props)); }
    for (const b of p.beams) D.define(p, b.id, D.BEAM);
    for (const sc of p.screens) for (const n of sc.nodes) if (D.HUD_NODE[n.type]) D.define(p, sc.id + "." + n.id, D.HUD_NODE[n.type].props);
    return p;
  };
})();
