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
      { k: "action", label: "On click", type: "choice", options: [["none", "Nothing"], ["goto", "Go to screen"], ["back", "Go back (previous screen)"], ["start", "Start a new game"], ["continue", "Continue (the latest save; a new game if there is none)"], ["load", "Load a save slot"], ["resume", "Resume game (pause menu)"], ["restart", "Restart level (pause menu)"], ["menu", "Quit to main menu"], ["quit", "Quit (close the game)"]], def: "none" },
      { k: "target", label: "Screen", type: "screenRef", def: "", showIf: ["action", "goto"] },
      { k: "confirm", label: "Ask “Are you sure?” first", type: "bool", def: false }, { k: "confirmText", label: "Question", type: "string", def: "Are you sure?", showIf: ["confirm", true] }, { k: "yesText", label: "Yes button", type: "string", def: "Yes", showIf: ["confirm", true] }, { k: "noText", label: "No button", type: "string", def: "No", showIf: ["confirm", true] },
      { k: "slot", label: "Save slot (1 to 3; menus can show {save.info1}, {save.info2}, {save.info3})", type: "number", def: 1, min: 1, max: 3, step: 1, showIf: ["action", ["start", "load"]] }] },
    slider: { label: "Slider", icon: "◐", props: [{ k: "label", label: "Label", type: "string", def: "Value" }, { k: "bind", label: "Controls tag", type: "tagRef", refType: "number", def: "" }, FONT, SIZE(26), COLOR("color", "Color", "#d9f7ff"), COLOR("accent", "Accent", "#7df3ff"), X, Y, W("Width", 420)] },
    toggle: { label: "Toggle", icon: "◉", props: [{ k: "label", label: "Label", type: "string", def: "Option" }, { k: "bind", label: "Controls tag", type: "tagRef", refType: "bool", def: "" }, FONT, SIZE(26), COLOR("color", "Color", "#d9f7ff"), COLOR("accent", "Accent", "#7df3ff"), X, Y, W("Width", 420)] },
    // a choice picker: "<  Normal  >" (left / right or click the arrows) that sets a tag; options from the tag (a list tag) or written as  Easy=0, Normal=1, Hard=2
    choice: { label: "Choice picker", icon: "⇋", props: [{ k: "label", label: "Label", type: "string", def: "Difficulty" }, { k: "bind", label: "Controls tag", type: "tagRef", refType: "any", def: "" },
      { k: "options", label: "Choices (Name=value, one per line; empty = the tag's own list)", type: "string", def: "Easy=0\nNormal=1\nHard=2", multi: true }, { k: "wrap", label: "Goes round (after the last comes the first)", type: "bool", def: true },
      FONT, SIZE(26), COLOR("color", "Color", "#d9f7ff"), COLOR("accent", "Accent (arrows, pointed at)", "#7df3ff"), X, Y, W("Width", 520), { k: "changeSound", label: "Change sound", type: "asset", assetType: "sound", def: "" }] },
    // a key binding: shows the key; click it (or Enter) and press a new key / mouse button
    keybind: { label: "Key binding", icon: "⌨", props: [{ k: "label", label: "Label", type: "string", def: "Jump" }, { k: "bind", label: "Key tag it changes (e.g. player.keyJump)", type: "tagRef", refType: "any", def: "player.keyJump" },
      { k: "waitText", label: "Text while waiting for a key", type: "string", def: "Press a key…" }, FONT, SIZE(26), COLOR("color", "Color", "#d9f7ff"), COLOR("accent", "Accent (key box, pointed at)", "#7df3ff"), X, Y, W("Width", 520)] },
    pips: { label: "Counter boxes (energy tanks, lives)", icon: "▪", props: [{ k: "bind", label: "Filled boxes = tag", type: "tagRef", refType: "number", def: "player.tanks" }, { k: "max", label: "Number of boxes = tag (empty = the tag's highest value)", type: "tagRef", refType: "number", def: "player.tanksMax" },
      { k: "box", label: "Box size", type: "number", def: 18, min: 2, max: 200, step: 1 }, { k: "gap", label: "Space between boxes", type: "number", def: 5, min: 0, max: 100, step: 1 }, { k: "perRow", label: "Boxes per row (0 = one row)", type: "number", def: 7, min: 0, max: 100, step: 1 },
      COLOR("color", "Filled color", "#ffb030"), COLOR("empty", "Empty color", "#3a2a12"), COLOR("border", "Border color", "#000000"), Object.assign({}, X, { def: -520 }), Object.assign({}, Y, { def: -272 }), OPACITY] },
    minimap: { label: "Minimap (+ radar)", icon: "◍", props: [Object.assign({}, X, { def: 500 }), Object.assign({}, Y, { def: 230 }), W("Width", 220), Object.assign({}, H, { def: 220, label: "Height" }),
      { k: "shape", label: "Shape", type: "choice", options: [["circle", "Round"], ["square", "Square"]], def: "circle" },
      { k: "zoom", label: "Zoom: cells across (fewer = closer)", type: "number", def: 14, min: 3, max: 120, step: 1 },
      { k: "turn", label: "Turns with your view (off = north is always up)", type: "bool", def: true },
      { k: "reveal", label: "Only shows places you have been near", type: "bool", def: false }, { k: "revealRadius", label: "…how near (cells)", type: "number", def: 4, min: 1, max: 30, step: 1, showIf: ["reveal", true] },
      { k: "items", label: "Shows pick-ups, keys, exits and doors", type: "bool", def: true },
      { k: "enemies", label: "Shows enemies", type: "choice", options: [["radar", "Only when the radar finds them"], ["always", "Always"], ["none", "Never"]], def: "radar" },
      { k: "radar", label: "Radar", type: "bool", def: true },
      { k: "radarStyle", label: "Radar look", type: "choice", options: [["sweep", "Turning sweep line"], ["pulse", "Expanding ring"]], def: "sweep", showIf: ["radar", true] },
      { k: "radarRate", label: "Radar scans per second", type: "number", def: 0.5, min: 0.05, max: 10, step: 0.05, showIf: ["radar", true] },
      { k: "radarRange", label: "Radar range (cells)", type: "number", def: 8, min: 1, max: 100, step: 0.5, showIf: ["radar", true] },
      COLOR("radarColor", "Radar colour", "#3aff7a"), COLOR("bg", "Background", "#06121a"), COLOR("floor", "Floor", "#173544"), COLOR("wall", "Walls", "#7df3ff"), COLOR("door", "Doors", "#ffb030"),
      COLOR("player", "You", "#ffffff"), COLOR("enemy", "Enemies", "#ff3a3a"), COLOR("item", "Pick-ups", "#ffe066"), COLOR("border", "Border", "#7df3ff"), OPACITY] },
    bossbar: { label: "Boss health bar", icon: "☠", props: [Object.assign({}, X, { def: 0 }), Object.assign({}, Y, { def: 300 }), W("Width", 640), Object.assign({}, H, { def: 16, label: "Bar height" }),
      { k: "showName", label: "Shows the boss name", type: "bool", def: true }, FONT, SIZE(22), COLOR("textColor", "Name colour", "#ffffff"),
      COLOR("color", "Health colour", "#ff3a3a"), COLOR("lagColor", "Damage trail colour", "#ffe0a0"), COLOR("bg", "Back colour", "#1a0606"), COLOR("border", "Border colour", "#000000"),
      { k: "marks", label: "Marks where the phases start", type: "bool", def: true }, { k: "showPhase", label: "Shows the phase name", type: "bool", def: false }, OPACITY] },
    keys: { label: "Keys you carry", icon: "⚿", props: [Object.assign({}, X, { def: -560 }), Object.assign({}, Y, { def: 180 }), { k: "box", label: "Icon size", type: "number", def: 34, min: 8, max: 200, step: 1 }, { k: "gap", label: "Space between", type: "number", def: 6, min: 0, max: 100, step: 1 },
      { k: "vertical", label: "Stacked (top to bottom)", type: "bool", def: false }, { k: "missing", label: "Shows keys you don't have yet (faded)", type: "bool", def: false }, OPACITY] },
    bar: { label: "Bar", icon: "▬", props: [{ k: "bind", label: "Shows tag", type: "tagRef", refType: "number", def: "" }, { k: "max", label: "Full when tag (optional)", type: "tagRef", refType: "number", def: "" }, { k: "label", label: "Label", type: "string", def: "" }, FONT, SIZE(18),
      COLOR("textColor", "Label color", "#ffffff"), COLOR("color", "Fill color", "#ff4a4a"), COLOR("bg", "Back color", "#2a0d0d"), X, Y, W("Width", 260), { k: "h", label: "Height", type: "number", def: 22, min: 4, max: 200, step: 1 }, OPACITY] },
    // a panel: a coloured box (rounded corners, border, gradient, shadow) or a picture frame that stretches without warping its corners (9-slice), as a background for menus
    panel: { label: "Panel (box / frame)", icon: "▢", props: [X, Y, W("Width", 520), Object.assign({}, H, { def: 320, label: "Height" }), COLOR("fill", "Fill color", "#0b1a26"), { k: "fillAlpha", label: "Fill see-through (0 = clear, 1 = solid)", type: "number", def: 0.85, min: 0, max: 1, step: 0.05 },
      { k: "grad", label: "Gradient", type: "choice", options: [["none", "None (one color)"], ["down", "Top to bottom"], ["across", "Left to right"], ["glow", "From the middle"]], def: "none" }, Object.assign(COLOR("fill2", "Second gradient color", "#000000"), { showIf: ["grad", ["down", "across", "glow"]] }),
      { k: "border", label: "Border width (px)", type: "number", def: 2, min: 0, max: 40, step: 1 }, COLOR("borderColor", "Border color", "#2c5a74"), { k: "radius", label: "Corner roundness (px)", type: "number", def: 8, min: 0, max: 300, step: 1 },
      { k: "shadow", label: "Shadow size (px)", type: "number", def: 0, min: 0, max: 120, step: 1 }, COLOR("shadowColor", "Shadow / glow color", "#000000"),
      { k: "image", label: "Frame picture (optional, 9-slice)", type: "asset", assetType: "image", def: "" }, { k: "slice", label: "Frame corner size in the picture (px; the corners never stretch)", type: "number", def: 16, min: 0, max: 512, step: 1 },
      { k: "sliceScale", label: "Frame corner size on screen (x)", type: "number", def: 1, min: 0.1, max: 10, step: 0.1 }, OPACITY] },
    // the codex (logbook): every scannable object type, grouped; scanned ones can be read (title, picture, the codex entry), the rest show as ??? (or are left out)
    codex: { label: "Codex (scan logbook)", icon: "📖", props: [X, Y, W("Width", 1000), Object.assign({}, H, { def: 520, label: "Height" }), { k: "listW", label: "List width (px)", type: "number", def: 320, min: 120, max: 900, step: 10 },
      FONT, SIZE(22), COLOR("color", "Text color", "#d9f7ff"), COLOR("headColor", "Group / title color", "#7df3ff"), COLOR("selColor", "Selected entry", "#ffb53a"), COLOR("bg", "Background", "#0b1a26"), { k: "bgAlpha", label: "Background see-through (0 = clear)", type: "number", def: 0.85, min: 0, max: 1, step: 0.05 },
      { k: "locked", label: "Not yet scanned entries", type: "choice", options: [["mask", "Show as ???"], ["hide", "Leave them out"]], def: "mask" }, { k: "emptyText", label: "Text when nothing is scanned yet", type: "string", def: "Nothing scanned yet. Use the Scan visor." },
      { k: "showImage", label: "Show the scan picture", type: "bool", def: true }, { k: "selectSound", label: "Sound when an entry is picked", type: "asset", assetType: "sound", def: "" }] },
    // a save slot: shows what is in slot 1..3 (place, play time, date, a small picture); click = load it (empty = new game in it); hold the delete key while pointing at it = delete it
    saveslot: { label: "Save slot", icon: "▣", props: [{ k: "slot", label: "Slot (1 to 3)", type: "number", def: 1, min: 1, max: 3, step: 1 }, X, Y, W("Width", 620), Object.assign({}, H, { def: 110, label: "Height" }),
      FONT, SIZE(28), COLOR("color", "Text color", "#d9f7ff"), COLOR("subColor", "Small text color", "#8fb3c4"), COLOR("bg", "Background", "#0b1a26"), COLOR("hoverBg", "Background when pointed at", "#123247"), COLOR("border", "Border", "#2c5a74"), COLOR("hoverBorder", "Border when pointed at", "#7df3ff"),
      { k: "radius", label: "Corner roundness (px)", type: "number", def: 6, min: 0, max: 60, step: 1 }, { k: "texture", label: "Background picture (optional)", type: "asset", assetType: "image", def: "" },
      { k: "title", label: "Title ({n} = the slot number)", type: "string", def: "FILE {n}" }, { k: "emptyText", label: "Text when empty", type: "string", def: "Empty – start a new game" },
      { k: "showShot", label: "Show a picture of where you saved", type: "bool", def: true }, { k: "showPlace", label: "Show the place", type: "bool", def: true }, { k: "showTime", label: "Show the play time", type: "bool", def: true }, { k: "showDate", label: "Show the date", type: "bool", def: true }, { k: "showEnergy", label: "Show energy (health + tanks)", type: "bool", def: false },
      { k: "emptyClick", label: "Clicking an empty slot", type: "choice", options: [["new", "Starts a new game in it"], ["nothing", "Does nothing (a load-only screen)"]], def: "new" },
      { k: "deleteKey", label: "Hold this key while pointing at it to delete (empty = can not delete)", type: "choice", options: [["", "(none)"], ["r", "R"], ["delete", "Delete"], ["backspace", "Backspace"], ["x", "X"], ["d", "D"]], def: "r" },
      { k: "deleteHold", label: "Hold for (s)", type: "number", def: 1, min: 0, max: 5, step: 0.1 }, COLOR("deleteColor", "Delete bar color", "#ff4a5a"),
      { k: "hoverSound", label: "Hover sound", type: "asset", assetType: "sound", def: "" }, { k: "clickSound", label: "Click sound", type: "asset", assetType: "sound", def: "" }, { k: "deleteSound", label: "Delete sound", type: "asset", assetType: "sound", def: "" }, OPACITY] },
    // a video (.mp4 / .webm): a cutscene when stretched to the screen; when it ends (or is skipped) it can go to a screen or start the game
    video: { label: "Video (cutscene)", icon: "▶", props: [{ k: "asset", label: "Video (.mp4 / .webm)", type: "asset", assetType: "video", def: "" }, X, Y, W("Width (0 = 640)"), H, { k: "stretch", label: "Fill the whole screen (a cutscene)", type: "bool", def: true }, { k: "fit", label: "Fill style", type: "choice", options: [["contain", "Whole picture (black bars if needed)"], ["cover", "Cover the screen (cuts the edges)"]], def: "contain", showIf: ["stretch", true] },
      { k: "volume", label: "Volume (with the music volume setting)", type: "number", def: 1, min: 0, max: 2, step: 0.05 }, { k: "loop", label: "Loop", type: "bool", def: false }, { k: "skippable", label: "A key or click skips it", type: "bool", def: true },
      { k: "then", label: "When it ends", type: "choice", options: [["none", "Nothing"], ["goto", "Go to a screen"], ["start", "Start a new game"], ["back", "Go back (previous screen)"], ["menu", "Main menu"]], def: "goto" }, { k: "next", label: "Screen", type: "screenRef", def: "main", showIf: ["then", "goto"] }, OPACITY] },
    // a sound or music file that plays when the screen opens (invisible in the game)
    sound: { label: "Sound / music", icon: "♪", props: [{ k: "asset", label: "Sound file", type: "asset", assetType: "sound", def: "" }, { k: "channel", label: "Volume setting", type: "choice", options: [["music", "Music"], ["sfx", "Sound effects"]], def: "sfx" },
      { k: "volume", label: "Volume", type: "number", def: 1, min: 0, max: 2, step: 0.05 }, { k: "delay", label: "Starts after (s)", type: "number", def: 0, min: 0, max: 600, step: 0.1 }, { k: "loop", label: "Loop", type: "bool", def: false }, { k: "stopOnLeave", label: "Stops when the screen changes", type: "bool", def: true }, X, Y] },
    splash: { label: "Splash", icon: "✦", props: [
      { k: "kind", label: "Shows", type: "choice", options: [["text", "Text"], ["image", "Image"]], def: "text" },
      { k: "text", label: "Text", type: "string", def: "Made with PRIMEngine", showIf: ["kind", "text"] }, Object.assign({}, FONT, { showIf: ["kind", "text"] }), Object.assign({}, SIZE(56), { showIf: ["kind", "text"] }), Object.assign(COLOR("color", "Color", "#ffffff"), { showIf: ["kind", "text"] }),
      { k: "asset", label: "Image", type: "asset", assetType: "image", def: "", showIf: ["kind", "image"] }, Object.assign({}, W("Width (0 = natural)"), { showIf: ["kind", "image"] }), Object.assign({}, H, { label: "Height (0 = natural)", showIf: ["kind", "image"] }),
      { k: "duration", label: "Length (s)", type: "number", def: 3, min: 0.5, max: 30, step: 0.1 },
      { k: "fadeIn", label: "Fade in (s)", type: "number", def: 0.8, min: 0, max: 5, step: 0.05 }, { k: "fadeInType", label: "Fade in style", type: "choice", options: FADES, def: "fade" },
      { k: "fadeOut", label: "Fade out (s)", type: "number", def: 0.8, min: 0, max: 5, step: 0.05 }, { k: "fadeOutType", label: "Fade out style", type: "choice", options: FADES, def: "fade" }] },
  };
  // things every element can have: an always-running animation, an entrance when the screen opens, text styling (text elements)
  const EANIM = [{ k: "anim", label: "Animation", type: "choice", options: [["none", "None"], ["pulse", "Pulse (grows and shrinks)"], ["float", "Float (up and down)"], ["blink", "Blink"], ["glowpulse", "Fade in and out"], ["spin", "Spin"], ["sway", "Sway (tilts left and right)"], ["shake", "Shake"]], def: "none" },
    { k: "animSpeed", label: "Animation speed (times per second)", type: "number", def: 0.6, min: 0.05, max: 10, step: 0.05 }, { k: "animAmount", label: "Animation strength", type: "number", def: 1, min: 0, max: 10, step: 0.05 },
    { k: "enter", label: "When the screen opens", type: "choice", options: [["none", "Just there"], ["fade", "Fades in"], ["up", "Slides in from below"], ["down", "Slides in from above"], ["left", "Slides in from the right"], ["right", "Slides in from the left"], ["zoom", "Zooms in"], ["pop", "Pops in"]], def: "none" },
    { k: "enterDelay", label: "…after (s)", type: "number", def: 0, min: 0, max: 30, step: 0.05 }, { k: "enterTime", label: "…taking (s)", type: "number", def: 0.4, min: 0.05, max: 10, step: 0.05 }];
  const TXT = [{ k: "bold", label: "Bold", type: "bool", def: false }, { k: "italic", label: "Italic", type: "bool", def: false }, { k: "spacing", label: "Letter spacing (px)", type: "number", def: 0, min: -10, max: 60, step: 0.5 },
    { k: "lineH", label: "Line height", type: "number", def: 1.15, min: 0.6, max: 4, step: 0.05 }, { k: "talign", label: "Lines line up", type: "choice", options: [["center", "Centered"], ["left", "Left"], ["right", "Right"]], def: "center" },
    { k: "outline", label: "Outline width (px)", type: "number", def: 0, min: 0, max: 20, step: 0.5 }, COLOR("outlineColor", "Outline color", "#000000"),
    { k: "shadowX", label: "Shadow right (px)", type: "number", def: 0, min: -50, max: 50, step: 1 }, { k: "shadowY", label: "Shadow down (px)", type: "number", def: 0, min: -50, max: 50, step: 1 }, { k: "shadowBlur", label: "Shadow blur (px)", type: "number", def: 0, min: 0, max: 60, step: 1 }, COLOR("shadowColor", "Shadow color", "#000000"),
    { k: "glow", label: "Glow size (px, 0 = none)", type: "number", def: 0, min: 0, max: 80, step: 1 }, COLOR("glowColor", "Glow color", "#7df3ff")];
  S.EANIM = EANIM; S.TXT = TXT;
  for (const t of ["title", "button"]) S.NODE[t].props.push(...TXT.map(q => t === "button" ? Object.assign({}, q, { showIf: ["mode", "text"] }) : q));
  S.NODE.title.props[0] = Object.assign({}, S.NODE.title.props[0], { multi: true });
  // a sprite strip on an image element: frames side by side
  S.NODE.image.props.push({ k: "frames", label: "Frames side by side (a sprite strip; 1 = still picture)", type: "number", def: 1, min: 1, max: 256, step: 1 }, { k: "fps", label: "Frames per second", type: "number", def: 10, min: 0.5, max: 60, step: 0.5 }, { k: "slice", label: "9-slice corner size (px, 0 = off; stretches without warping the corners)", type: "number", def: 0, min: 0, max: 512, step: 1 });
  for (const [t, d] of Object.entries(S.NODE)) if (!["splash", "sound", "video"].includes(t)) d.props.push(...EANIM);
  // how a screen comes in (in the game: the old screen goes, the new one arrives)
  const TRANS = [{ k: "trans", label: "Transition when it opens", type: "choice", options: [["none", "Cut (instant)"], ["fade", "Cross-fade"], ["black", "Fade through black"], ["white", "Fade through white"], ["left", "Slide in from the right"], ["right", "Slide in from the left"], ["up", "Slide up"], ["down", "Slide down"], ["zoom", "Zoom in"], ["wipe", "Wipe"], ["blur", "Blur in"]], def: "fade" },
    { k: "transTime", label: "Transition time (s)", type: "number", def: 0.35, min: 0.05, max: 5, step: 0.05 }];
  S.TRANS = TRANS;
  S.SCREEN = {
    menu: [COLOR("bg", "Background color", "#030a12"), { k: "bgImage", label: "Background image", type: "asset", assetType: "image", def: "" }, { k: "music", label: "Music", type: "asset", assetType: "sound", def: "" }],
    hud: [],
    // a pause menu: drawn over the frozen game (Esc). Its buttons can resume, restart, go to other pause screens, quit...
    pause: [COLOR("bg", "Darkening color", "#000000"), { k: "dim", label: "Darken the game (0 = see-through, 1 = solid)", type: "number", def: 0.55, min: 0, max: 1, step: 0.05 }, { k: "blur", label: "Blur the game (px)", type: "number", def: 4, min: 0, max: 30, step: 1 }, { k: "bgImage", label: "Background image", type: "asset", assetType: "image", def: "" }, { k: "music", label: "Music while paused", type: "asset", assetType: "sound", def: "" }],
    splash: [COLOR("bg", "Background color", "#000000"), { k: "skippable", label: "Click or key skips", type: "bool", def: true }, { k: "next", label: "Then go to", type: "screenRef", def: "main" }],
    // the prologue: a story text before the game. "scroller" = text crawling away into the distance, "typer" = typed letter by letter, page after page
    prologue: [{ k: "style", label: "Style", type: "choice", options: [["scroller", "Scroller: the text crawls up and away into space"], ["typer", "Typer: typed letter by letter, page by page"], ["credits", "Credits: rolls up, lines starting with # are headings"]], def: "scroller" },
      COLOR("headColor", "Heading color (credits: lines starting with #)", "#7df3ff"), { k: "headSize", label: "Heading size (credits)", type: "number", def: 30, min: 8, max: 200, step: 1 },
      { k: "intro", label: "Opening line (shown alone first, empty = none)", type: "string", def: "", multi: true }, COLOR("introColor", "Opening line color", "#4fc3ff"),
      { k: "text", label: "Story text (typer: an empty line starts a new page)", type: "string", def: "Long ago, on a world at the edge of the map…\n\nThe colony fell silent.\nOne hunter was sent to find out why.", multi: true },
      FONT, SIZE(40), COLOR("color", "Text color", "#ffd84a"), { k: "align", label: "Text alignment", type: "choice", options: [["center", "Centered"], ["left", "Left"], ["justify", "Justified (straight edges)"]], def: "center" },
      { k: "width", label: "Text column width (px)", type: "number", def: 820, min: 200, max: 1280, step: 10 },
      { k: "speed", label: "Scroll speed (px per second)", type: "number", def: 45, min: 5, max: 600, step: 1, showIf: ["style", ["scroller", "credits"]] },
      { k: "tilt", label: "Tilt into the distance (degrees, 0 = flat credits roll)", type: "number", def: 55, min: 0, max: 80, step: 1, showIf: ["style", "scroller"] },
      { k: "cps", label: "Typing speed (letters per second)", type: "number", def: 16, min: 1, max: 200, step: 1, showIf: ["style", "typer"] },
      { k: "typeSound", label: "Typing sound", type: "asset", assetType: "sound", def: "", showIf: ["style", "typer"] }, { k: "typeEvery", label: "Typing sound every (letters)", type: "number", def: 2, min: 1, max: 20, step: 1, showIf: ["style", "typer"] },
      { k: "pageHold", label: "A full page stays for (s)", type: "number", def: 2.5, min: 0, max: 30, step: 0.1, showIf: ["style", "typer"] },
      { k: "place", label: "Where the text sits", type: "choice", options: [["bottom", "Bottom"], ["middle", "Middle"], ["top", "Top"]], def: "bottom", showIf: ["style", "typer"] },
      COLOR("bg", "Background color", "#000000"), { k: "bgImage", label: "Background image (stars…)", type: "asset", assetType: "image", def: "" }, { k: "music", label: "Music", type: "asset", assetType: "sound", def: "" },
      { k: "endHold", label: "Waits at the end (s)", type: "number", def: 1.5, min: 0, max: 30, step: 0.1 }, { k: "skippable", label: "A key or click skips it", type: "bool", def: true },
      { k: "then", label: "Then", type: "choice", options: [["none", "Nothing"], ["goto", "Go to a screen"], ["start", "Start a new game"], ["back", "Go back (previous screen)"], ["menu", "Main menu"]], def: "goto" }, { k: "next", label: "Screen", type: "screenRef", def: "main", showIf: ["then", "goto"] }],
  };
  for (const k of ["menu", "pause", "prologue"]) S.SCREEN[k].push(...TRANS);
  const UI = [{ k: "btnHover", label: "Hover sound for all its buttons (when a button has none)", type: "asset", assetType: "sound", def: "" }, { k: "btnClick", label: "Click sound for all its buttons (when a button has none)", type: "asset", assetType: "sound", def: "" },
    { k: "navSound", label: "Sound when moving between buttons with keys / gamepad", type: "asset", assetType: "sound", def: "" }, { k: "escBack", label: "Esc / gamepad B goes back", type: "bool", def: true }];
  for (const k of ["menu", "pause"]) S.SCREEN[k].push(...UI);
  // missing tags of screens and elements (settings added in a later version) get their default value
  S.ensureScreens = project => { for (const sc of project.screens || []) { for (const p of S.SCREEN[sc.kind] || []) { const k = sc.id + "." + p.k; if (!project.tags[k]) project.tags[k] = rec(p, sc.id, sc.id); }
      for (const n of sc.nodes || []) { const d = S.NODE[n.type]; if (!d) continue; for (const p of d.props) { const k = sc.id + "." + n.id + "." + p.k; if (!project.tags[k]) project.tags[k] = rec(p, sc.id, sc.id + "." + n.id); else if (p.multi && !project.tags[k].multi) project.tags[k].multi = true; } } } };
  S.addPrologue = (project, id) => { const sc = { id: S.uid(id || "prologue", project.screens.map(s => s.id)), kind: "prologue", nodes: [] }; project.screens.push(sc); S.defineScreen(project, sc); return sc; };

  const rec = (p, group, owner) => { const r = { type: p.type, value: p.def, label: p.label, group, owner }; for (const k of ["min", "max", "step", "options", "assetType", "refType", "showIf", "modelTag", "multi"]) if (p[k] !== undefined) r[k] = p[k]; r.prop = p.k; return r; };
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
    { k: "ao", label: "Ambient occlusion (soft shadows in corners and under ledges)", type: "bool", def: true }, MNUM("aoStrength", "Ambient occlusion: strength", 0.6, 0, 1, 0.05, { showIf: ["ao", true] }), MNUM("aoRadius", "Ambient occlusion: reach (units, 128 = a cell)", 96, 16, 256, 4, { showIf: ["ao", true] }),
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
    { k: "bobSync", label: "Sync bobbing to step distance (each footstep lands on the low point; the speed above is then ignored)", type: "bool", def: false, showIf: ["bob", true] },
    PNUM("animBlend", "Smooth change between animations (seconds, 0 = instant; weapons, enemies, doors, objects)", 0.15, 0, 1, 0.01), { k: "hudMove", label: "Hud Movement (the HUD sways when you look around, like Metroid Prime)", type: "bool", def: false }, PNUM("hudMoveAmount", "Hud Movement strength", 1, 0.1, 4, 0.05, { showIf: ["hudMove", true] }),
    { k: "maxHealth", label: "Health", type: "number", def: 100, min: 1, max: 1000, step: 1 },
    { k: "crosshair", label: "Show crosshair", type: "bool", def: true }, { k: "crosshairSize", label: "Crosshair size", type: "number", def: 10, min: 2, max: 60, step: 1, showIf: ["crosshair", true] }, { k: "crosshairGap", label: "Crosshair gap", type: "number", def: 4, min: 0, max: 30, step: 1, showIf: ["crosshair", true] },
    { k: "crosshairThickness", label: "Crosshair thickness", type: "number", def: 2, min: 1, max: 8, step: 1, showIf: ["crosshair", true] }, Object.assign(COLOR("crosshairColor", "Crosshair color", "#ffffff"), { showIf: ["crosshair", true] }),
    { k: "hud", label: "HUD screen", type: "screenRef", def: "hud" },
    { k: "reloadKey", label: "Reload key", type: "choice", options: [], def: "r" },
    { k: "keyForward", label: "Walk forward key", type: "choice", options: [], def: "w" }, { k: "keyBack", label: "Walk back key", type: "choice", options: [], def: "s" }, { k: "keyLeft", label: "Step left key", type: "choice", options: [], def: "a" }, { k: "keyRight", label: "Step right key", type: "choice", options: [], def: "d" },
    { k: "keyJump", label: "Jump key", type: "choice", options: [], def: "space" }, { k: "visorsOn", label: "Visors (Scanner, X-Ray, Night vision…)", type: "bool", def: false }, { k: "visorKey", label: "Visor wheel key (hold, point, let go)", type: "choice", options: [], def: "q" },
    { k: "visorStart", label: "Visor at the start", type: "choice", options: [], def: "combat" }, { k: "visorSlow", label: "Game speed while the visor wheel is open (1 = normal)", type: "number", def: 0.3, min: 0, max: 1, step: 0.05 }, { k: "keySprint", label: "Sprint key", type: "choice", options: [], def: "shift" },
    { k: "startMap", label: "The game starts on map (empty = the first map)", type: "mapRef", def: "" },
    { k: "livesMode", label: "Lives system", type: "choice", options: [["off", "Off (dying = the death setting below)"], ["classic", "Classic: lives, respawn at the last checkpoint"], ["reserve", "Energy reserve: energy tanks refill your health (Metroid style)"]], def: "off" },
    PNUM("livesStart", "Lives at the start", 3, 1, 99, 1, { showIf: ["livesMode", "classic"] }),
    PNUM("tanksStart", "Energy tanks at the start", 0, 0, 99, 1, { showIf: ["livesMode", "reserve"] }), PNUM("tanksLimit", "Most energy tanks you can collect", 14, 1, 99, 1, { showIf: ["livesMode", "reserve"] }),
    { k: "deathAction", label: "When the player dies", type: "choice", options: [["restart", "Restart the map"], ["save", "Back to the last save (restarts the map if there is none)"], ["screen", "Go to a screen (e.g. Game Over)"]], def: "restart" },
    { k: "deathScreen", label: "Game over screen", type: "screenRef", def: "", showIf: ["deathAction", "screen"] }, PNUM("deathDelay", "Wait after dying (s)", 2.5, 0, 30, 0.1),
    { k: "damageFlash", label: "Red flash when hurt", type: "bool", def: true },
    { k: "pauseScreen", label: "Pause menu (Esc). Empty = Esc leaves the game", type: "screenRef", def: "pause" },
    { k: "pauseVolume", label: "Game sounds while paused (0 = silent, 1 = full)", type: "number", def: 0.25, min: 0, max: 1, step: 0.05 },
  ];
  S.PLAYER_GROUPS = [["Movement", ["speed", "sprintEnabled", "sprintPercent", "jumpEnabled", "jumpHeight", "gravity"]], ["Controls", ["keyForward", "keyBack", "keyLeft", "keyRight", "keyJump", "keySprint", "interactKey", "reloadKey"]], ["Visors", ["visorsOn", "visorKey", "visorStart", "visorSlow"]], ["Feel", ["breathe", "breatheAmount", "breatheSpeed", "bob", "bobAmount", "bobSpeed", "bobSync", "animBlend"]], ["Sounds", ["footstep", "footstep2", "footstep3", "stepDistance", "jumpSound", "landSound", "hurtSound", "deathSound", "soundVolume"]], ["Body", ["maxHealth", "eyeHeight", "radius", "stepHeight"]], ["Camera", ["fov", "aimMode", "freeAimRadius", "mouseSens"]], ["Crosshair", ["crosshair", "crosshairSize", "crosshairGap", "crosshairThickness", "crosshairColor"]], ["HUD", ["hud", "hudMove", "hudMoveAmount", "damageFlash"]], ["Lives", ["livesMode", "livesStart", "tanksStart", "tanksLimit"]], ["Game rules", ["startMap", "deathAction", "deathScreen", "deathDelay", "pauseScreen", "pauseVolume"]]];
  // 3D objects attached to the player camera (weapons, hands...). Each one has tags player.<id>.*
  const ANIM = (k, label, extra) => Object.assign({ k, label, type: "anim", def: "" }, extra || {}), NUM = (k, label, def, min, max, step, extra) => Object.assign({ k, label, type: "number", def, min, max, step }, extra || {}), WP = ["type", "weapon"], NI = ["type", ["weapon", "animation"]], W_ = { showIf: WP };
  // a showIf is [tagKey, value] or [tagKey, [value, value...]]
  S.shown = (T, pre, pr) => { const ok = c => { const v = T.get(pre + c[0]), w = c[1]; return Array.isArray(w) ? w.includes(v) : v === w; }; return (!pr.showIf || ok(pr.showIf)) && (!pr.showIf2 || ok(pr.showIf2)) && (!pr.also || pr.also.every(ok)); };
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
    { k: "doorColor", label: "Door colour (doors that need this weapon glow in it)", type: "color", def: "#ff8a2a", showIf: WP },
    { k: "useNeedTag", label: "Can only be taken out while this tag… (empty = always)", type: "tagRef", refType: "any", def: "", showIf: WP },
    { k: "useNeedOp", label: "is", type: "choice", options: [["=", "equal to"], ["!=", "not equal to"], [">", "greater than"], ["<", "less than"], [">=", "at least"], ["<=", "at most"]], def: "=", showIf: WP },
    { k: "useNeedVal", label: "this value", type: "string", def: "yes", showIf: WP },
    { k: "useLockMsg", label: "Message when you try to take it out too early (empty = none)", type: "string", def: "", showIf: WP },
    { k: "projType", label: "Projectile", type: "choice", options: [["none", "None"], ["hitscan", "Hitscan (a straight line to where you aim)"], ["particle", "Particle (sphere or laser bolt)"], ["mesh", "Mesh (a 3D model that flies off)"]], def: "none", showIf: WP },
    NUM("fireRate", "Most shots per second (0 = no limit)", 0, 0, 60, 0.5, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }),
    NUM("emitX", "Emitter point X (where projectiles start)", 0, -500, 500, 0.5, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }), NUM("emitY", "Emitter point Y", 0, -500, 500, 0.5, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }), NUM("emitZ", "Emitter point Z", 0, -500, 500, 0.5, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }),
    NUM("emitYaw", "Emitter arrow: turn left / right", 0, -180, 180, 1, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }), NUM("emitPitch", "Emitter arrow: up / down", 0, -89, 89, 1, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }),
    { k: "projColor", label: "Color", type: "color", def: "#ffcc33", showIf: ["projType", ["hitscan", "particle"]] },
    NUM("projWidth", "Line thickness", 0.6, 0.05, 20, 0.05, { showIf: ["projType", "hitscan"] }), NUM("projLineTime", "Line stays visible (seconds)", 0.12, 0.02, 5, 0.01, { showIf: ["projType", "hitscan"] }),
    { k: "projShade", label: "Particle shading", type: "choice", options: [["smooth", "Smooth (round)"], ["flat", "Faceted (low-poly retro)"]], def: "smooth", showIf: ["projType", ["hitscan", "particle", "mesh"]] },
    { k: "projShape", label: "Particle shape", type: "choice", options: [["sphere", "Sphere"], ["laser", "Laser bolt"]], def: "sphere", showIf: ["projType", "particle"] },
    { k: "projModel", label: "Projectile model (FBX)", type: "asset", assetType: "model", def: "", showIf: ["projType", "mesh"] }, NUM("projScale", "Model scale", 1, 0.001, 100, 0.001, { showIf: ["projType", "mesh"] }),
    NUM("projRx", "Model turn X (fix its facing)", 0, -180, 180, 1, { showIf: ["projType", "mesh"] }), NUM("projRy", "Model turn Y", 0, -180, 180, 1, { showIf: ["projType", "mesh"] }), NUM("projRz", "Model turn Z", 0, -180, 180, 1, { showIf: ["projType", "mesh"] }),
    NUM("projSize", "Size", 3, 0.2, 80, 0.1, { showIf: ["projType", "particle"] }), NUM("projSpeed", "Speed", 1400, 50, 8000, 50, { showIf: ["projType", ["particle", "mesh"]] }), NUM("projLife", "Flies for (seconds)", 2, 0.1, 30, 0.1, { showIf: ["projType", ["particle", "mesh"]] }),
    NUM("projEmit", "Emission (glow brightness)", 1, 0, 6, 0.05, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }), { k: "projLight", label: "Projectile lights up the world", type: "bool", def: false, showIf: ["projType", ["particle", "mesh"]] },
    NUM("projLightPower", "Projectile light brightness", 1.5, 0, 8, 0.05, { showIf: ["projType", ["particle", "mesh"]] }), NUM("projLightRange", "Projectile light reach", 220, 10, 2000, 5, { showIf: ["projType", ["particle", "mesh"]] }), NUM("projSpread", "Spread (degrees: shots fly off in a random cone)", 0, 0, 45, 0.25, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }), NUM("projCount", "Shots at once (pellets, like a shotgun)", 1, 1, 30, 1, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }), NUM("aimSpread", "Spread while aiming (1 = the same, 0.3 = much tighter)", 0.3, 0, 1, 0.05, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }),
    { k: "projLightShadow", label: "Projectile light casts shadows (only the nearest flying light does, to keep it fast)", type: "bool", def: true, showIf: ["projType", ["particle", "mesh"]] },
    { k: "projCharge", label: "Bigger when charged (charge beam)", type: "bool", def: true, showIf: ["projType", ["particle", "mesh"]] },
    NUM("projDamage", "Damage per hit (to enemies)", 25, 0, 100000, 1, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }),
    // impact on walls, floors and ceilings: a sound and a mark (one picture, or an animation = frames side by side in one image)
    { k: "impactSound", label: "Wall hit sound", type: "asset", assetType: "sound", def: "", showIf: ["projType", ["hitscan", "particle", "mesh"]] },
    { k: "impactSoundCharged", label: "Wall hit sound of a charged shot (empty = the normal one)", type: "asset", assetType: "sound", def: "", showIf: ["projType", ["hitscan", "particle", "mesh"]] },
    NUM("chargedFrom", "A shot counts as charged from (charge 0 to 1; 1 = only full charge)", 1, 0.01, 1, 0.01, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }),
    { k: "impactImage", label: "Wall hit mark / animation (frames side by side in one picture)", type: "asset", assetType: "image", def: "", showIf: ["projType", ["hitscan", "particle", "mesh"]] },
    NUM("impactFrames", "Frames in that picture (1 = a still mark)", 1, 1, 64, 1, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }), NUM("impactFps", "Animation speed (frames per second)", 15, 1, 60, 1, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }),
    NUM("impactSize", "Mark size (units, 128 = a cell)", 16, 1, 256, 1, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }), NUM("impactStay", "Mark stays for (seconds after the animation)", 4, 0, 60, 0.5, { showIf: ["projType", ["hitscan", "particle", "mesh"]] }),
    { k: "impactGlow", label: "Mark glows (not darkened by shadows)", type: "bool", def: false, showIf: ["projType", ["hitscan", "particle", "mesh"]] },
    // splash damage and the explosion where a shot lands (wall, enemy, door, or the end of its flight)
    { k: "projSplash", label: "Splash damage (hurts everything around where it lands)", type: "bool", def: false, showIf: ["projType", ["hitscan", "particle", "mesh"]] },
    NUM("splashRadius", "Splash radius (units, 128 = a cell)", 160, 8, 2000, 4, { showIf: ["projType", ["hitscan", "particle", "mesh"]], showIf2: ["projSplash", true] }),
    NUM("splashDamage", "Splash damage (in the middle; a quarter of it at the edge)", 50, 0, 100000, 1, { showIf: ["projType", ["hitscan", "particle", "mesh"]], showIf2: ["projSplash", true] }),
    NUM("splashCharge", "Splash radius at full charge (x the normal radius, 1 = same)", 1, 1, 10, 0.05, { showIf: ["projType", ["hitscan", "particle", "mesh"]], showIf2: ["projSplash", true] }),
    { k: "splashSphere", label: "Shock sphere (grows to the splash size, then fades)", type: "bool", def: false, showIf: ["projType", ["hitscan", "particle", "mesh"]], showIf2: ["projSplash", true] },
    NUM("splashSphereTime", "Shock sphere: how long it lasts (seconds)", 0.45, 0.05, 5, 0.05, { showIf: ["projSplash", true], showIf2: ["splashSphere", true] }),
    { k: "splashColor1", label: "Shock sphere: middle color", type: "color", def: "#ffe08a", showIf: ["projSplash", true], showIf2: ["splashSphere", true] },
    { k: "splashColor2", label: "Shock sphere: edge color", type: "color", def: "#ff4a1a", showIf: ["projSplash", true], showIf2: ["splashSphere", true] },
    { k: "splashAnim", label: "Shock sphere: animated gradient", type: "choice", options: [["none", "None (middle to edge)"], ["bands", "Rings rolling over it"], ["swirl", "Swirl"], ["pulse", "Pulse (colors swap)"], ["noise", "Flicker (energy noise)"]], def: "bands", showIf: ["projSplash", true], showIf2: ["splashSphere", true] },
    NUM("splashAnimSpeed", "Shock sphere: animation speed", 2, 0, 20, 0.1, { showIf: ["projSplash", true], showIf2: ["splashSphere", true] }),
    NUM("splashGlow", "Shock sphere: brightness (above 1 glows with bloom)", 1.4, 0, 8, 0.05, { showIf: ["projSplash", true], showIf2: ["splashSphere", true] }),
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
    { k: "explodeSoundCharged", label: "Explosion sound of a charged shot (empty = the normal one)", type: "asset", assetType: "sound", def: "", showIf: ["projType", ["hitscan", "particle", "mesh"]] },
    // ammo: "ammo" = rounds ready to fire, "reserve" = the rest carried. Both are live tags (a HUD text can show {player.att1.ammo})
    { k: "ammoUse", label: "Uses ammo", type: "bool", def: false, showIf: WP },
    NUM("ammoStart", "Ammo at the start", 50, 0, 100000, 1, { showIf: WP, showIf2: ["ammoUse", true] }), NUM("ammoMax", "Most ammo you can carry", 200, 1, 100000, 1, { showIf: WP, showIf2: ["ammoUse", true] }),
    NUM("clipSize", "Magazine size (0 = no reloading, Doom style)", 0, 0, 10000, 1, { showIf: WP, showIf2: ["ammoUse", true] }), NUM("reloadTime", "Reload time (s, used when there is no reload animation)", 1.2, 0.05, 30, 0.05, { showIf: WP, showIf2: ["ammoUse", true] }),
    ANIM("reloadAnim", "Reload animation", { showIf: WP, showIf2: ["ammoUse", true] }), { k: "reloadSound", label: "Reload sound", type: "asset", assetType: "sound", def: "", showIf: WP, showIf2: ["ammoUse", true] }, { k: "emptySound", label: "Empty click sound", type: "asset", assetType: "sound", def: "", showIf: WP, showIf2: ["ammoUse", true] },
    NUM("ammo", "Ammo ready to fire (live)", 0, 0, 100000, 1, { live: true }), NUM("reserve", "Ammo in reserve (live)", 0, 0, 100000, 1, { live: true }),
    // aiming (its own tab in the weapon inspector): lock on to a target, or look down the sights with a zoom
    { k: "aimType", label: "Aim button", type: "choice", options: [["none", "None"], ["lock", "Lock on (locks onto the enemy you look at)"], ["sights", "Look down the sights (zoom)"]], def: "none", showIf: ["type", "weapon"], tab: "aim" },
    { k: "aimKey", label: "Aim key", type: "choice", options: [], def: "mouse2", showIf: ["type", "weapon"], showIf2: ["aimType", ["lock", "sights"]], tab: "aim" },
    { k: "aimHold", label: "How the key works", type: "choice", options: [["hold", "Hold to aim"], ["toggle", "Press to switch on / off"]], def: "hold", showIf: ["type", "weapon"], showIf2: ["aimType", ["lock", "sights"]], tab: "aim" },
    NUM("aimTime", "Time to aim in / out (s)", 0.15, 0, 2, 0.01, { showIf: ["type", "weapon"], showIf2: ["aimType", ["lock", "sights"]], tab: "aim" }),
    NUM("aimZoom", "Zoom (1 = none, 2 = twice as close)", 1.6, 1, 10, 0.05, { showIf: ["type", "weapon"], showIf2: ["aimType", "sights"], tab: "aim" }),
    NUM("adsX", "Weapon moves while aiming: X (to the middle = minus its X)", 0, -500, 500, 0.5, { showIf: ["type", "weapon"], showIf2: ["aimType", "sights"], tab: "aim" }), NUM("adsY", "…Y", 0, -500, 500, 0.5, { showIf: ["type", "weapon"], showIf2: ["aimType", "sights"], tab: "aim" }), NUM("adsZ", "…Z", 0, -500, 500, 0.5, { showIf: ["type", "weapon"], showIf2: ["aimType", "sights"], tab: "aim" }),
    NUM("aimSens", "Mouse speed while aiming (1 = normal)", 0.6, 0.05, 2, 0.05, { showIf: ["type", "weapon"], showIf2: ["aimType", "sights"], tab: "aim" }), NUM("aimMove", "Walking speed while aiming (1 = normal)", 0.6, 0, 1, 0.05, { showIf: ["type", "weapon"], showIf2: ["aimType", ["lock", "sights"]], tab: "aim" }),
    { k: "aimHideCross", label: "Hide the crosshair while aiming", type: "bool", def: false, showIf: ["type", "weapon"], showIf2: ["aimType", "sights"], tab: "aim" },
    { k: "aimOverlay", label: "Scope picture while aiming (optional, fills the screen)", type: "asset", assetType: "image", def: "", showIf: ["type", "weapon"], showIf2: ["aimType", "sights"], tab: "aim" },
    NUM("lockRange", "Lock on: reach (units)", 1500, 50, 20000, 10, { showIf: ["type", "weapon"], showIf2: ["aimType", "lock"], tab: "aim" }), NUM("lockAngle", "Lock on: how far from the crosshair a target can be (degrees)", 20, 1, 90, 1, { showIf: ["type", "weapon"], showIf2: ["aimType", "lock"], tab: "aim" }),
    NUM("lockTurn", "Lock on: how fast the view follows the target (0 = it does not)", 8, 0, 40, 0.5, { showIf: ["type", "weapon"], showIf2: ["aimType", "lock"], tab: "aim" }),
    { k: "lockHoming", label: "Lock on: shots curve toward the target", type: "bool", def: true, showIf: ["type", "weapon"], showIf2: ["aimType", "lock"], tab: "aim" }, NUM("lockHomingTurn", "…how hard they curve", 4, 0, 40, 0.5, { showIf: ["type", "weapon"], showIf2: ["aimType", "lock"], tab: "aim" }),
    { k: "lockColor", label: "Lock on: marker colour", type: "color", def: "#ffb030", showIf: ["type", "weapon"], showIf2: ["aimType", "lock"], tab: "aim" }, NUM("lockSize", "Lock on: marker size (px)", 46, 10, 300, 1, { showIf: ["type", "weapon"], showIf2: ["aimType", "lock"], tab: "aim" }),
    { k: "lockSound", label: "Lock on: sound when it locks", type: "asset", assetType: "sound", def: "", showIf: ["type", "weapon"], showIf2: ["aimType", "lock"], tab: "aim" },
  ];
  // ---- placeable objects (the Object Editor): doors, pick-ups, decorations. Tags prop<N>.*  They are placed in maps (map.things).
  const EN = ["enemy", "boss"];
  S.PROP = [
    { k: "name", label: "Name", type: "string", def: "Object" },
    { k: "type", label: "Type", type: "choice", options: [["decoration", "Decoration (just a model)"], ["door", "Door (fills a cell between walls)"], ["pickup", "Pick-up (collected by touching)"], ["enemy", "Enemy (hunts and attacks the player)"], ["exit", "Level exit (touch it to leave the level)"], ["npc", "Character (NPC: talk to it, make choices)"], ["speaker", "Speaker (plays a sound you hear around it)"], ["interactive", "Interactive (buttons, consoles, screens)"], ["checkpoint", "Checkpoint (where you respawn with the Lives system)"], ["spawner", "Spawner (keeps making new objects: enemies, pick-ups…)"], ["key", "Key (opens locked doors)"], ["boss", "Boss (an enemy with phases and a health bar)"], ["save", "Save station (saves the game)"]], def: "decoration" },
    { k: "look", label: "Looks like", type: "choice", options: [["model", "3D model (FBX)"], ["sprite", "2D sprite (pictures that always face the player)"]], def: "model", showIf: ["type", ["enemy", "boss", "decoration", "pickup", "key", "exit", "npc"]] },
    { k: "asset", label: "Model (FBX)", type: "asset", assetType: "model", def: "", showIf: ["look", "model"] },
    // 2D sprites: each picture holds the frames of one animation side by side (a strip). They always turn to face the player.
    { k: "spriteIdle", label: "Standing picture / animation strip", type: "asset", assetType: "image", def: "", showIf: ["look", "sprite"] }, NUM("spriteIdleFrames", "Frames in it", 1, 1, 64, 1, { showIf: ["look", "sprite"] }),
    { k: "spriteWalk", label: "Walking strip", type: "asset", assetType: "image", def: "", showIf: ["look", "sprite"], showIf2: ["type", ["enemy", "boss", "npc"]] }, NUM("spriteWalkFrames", "Frames in it", 1, 1, 64, 1, { showIf: ["look", "sprite"], showIf2: ["type", ["enemy", "boss", "npc"]] }),
    { k: "spriteAttack", label: "Attack strip", type: "asset", assetType: "image", def: "", showIf: ["look", "sprite"], showIf2: ["type", EN] }, NUM("spriteAttackFrames", "Frames in it", 1, 1, 64, 1, { showIf: ["look", "sprite"], showIf2: ["type", EN] }),
    { k: "spriteHurt", label: "Hurt strip", type: "asset", assetType: "image", def: "", showIf: ["look", "sprite"], showIf2: ["type", EN] }, NUM("spriteHurtFrames", "Frames in it", 1, 1, 64, 1, { showIf: ["look", "sprite"], showIf2: ["type", EN] }),
    { k: "spriteDeath", label: "Death strip (its last frame stays as the body)", type: "asset", assetType: "image", def: "", showIf: ["look", "sprite"], showIf2: ["type", EN] }, NUM("spriteDeathFrames", "Frames in it", 1, 1, 64, 1, { showIf: ["look", "sprite"], showIf2: ["type", EN] }),
    NUM("spriteFps", "Animation speed (frames per second)", 8, 1, 60, 1, { showIf: ["look", "sprite"] }), NUM("spriteHeight", "Sprite height (units, 128 = a story)", 110, 4, 1024, 1, { showIf: ["look", "sprite"] }),
    { k: "spriteBlend", label: "How it is drawn", type: "choice", options: [["normal", "Normal (soft / blurred edges blend in)"], ["glow", "Glow (adds its light: dark parts are see-through)"]], def: "normal", showIf: ["look", "sprite"] },
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
    NUM("pickRadius", "Pick-up distance (units)", 28, 8, 200, 1, { showIf: ["type", ["pickup", "key"]] }), { k: "pickSound", label: "Pick-up sound", type: "asset", assetType: "sound", def: "", showIf: ["type", ["pickup", "key"]] },
    // material (Material foldout of the Object Editor): base colour map + emission
    { k: "matTexture", label: "Base color map (replaces the model's own picture)", type: "asset", assetType: "image", def: "" },
    { k: "matEmitMap", label: "Emission map (bright parts glow; empty = the whole object glows)", type: "asset", assetType: "image", def: "" }, NUM("matEmit", "Emission strength (0 = no glow)", 0, 0, 8, 0.05),
    // enemy
    NUM("health", "Health", 100, 1, 100000, 1, { showIf: ["type", EN] }), NUM("speed", "Walking speed (units / second)", 150, 0, 2000, 5, { showIf: ["type", EN] }),
    NUM("radius", "Body width (radius, units)", 22, 4, 200, 1, { showIf: ["type", ["enemy", "boss", "npc"]] }), NUM("height", "Body height (units, 64 = 1 m)", 110, 10, 600, 1, { showIf: ["type", EN] }),
    NUM("sight", "Sees the player from (units)", 900, 50, 10000, 10, { showIf: ["type", EN] }),
    // how it moves: chase (waits, then hunts you), path (walks a path drawn on the map), stationary (stands still or hangs from the ceiling)
    { k: "behavior", label: "Behaviour", type: "choice", options: [["chase", "Normal: waits until it sees you, then hunts you"], ["path", "Patrol: follows a path drawn on the map"], ["stationary", "Stationary: stands still, or hangs from the ceiling"], ["drone", "Drone: flies, circles around you, then stops to shoot or dash at you"]], def: "chase", showIf: ["type", EN] },
    { k: "crawl", label: "Can walk on walls and ceilings (climbs when a wall is in its way, drops on you from above)", type: "bool", def: false, showIf: ["type", EN], showIf2: ["behavior", "chase"] },
    { k: "detect", label: "Notices the player", type: "choice", options: [["sees", "Yes: sees you and reacts"], ["blind", "No: never notices you (a trap)"]], def: "sees", showIf: ["type", EN] },
    { k: "blindMode", label: "Without noticing you it", type: "choice", options: [["shoot", "Fires on a timer, straight ahead (or down from a ceiling)"], ["touch", "Is a damage hitbox: touching it hurts"], ["both", "Both"]], def: "shoot", showIf: ["type", EN], showIf2: ["detect", "blind"] },
    { k: "pathChase", label: "Leaves the path to hunt you once it sees you", type: "bool", def: true, showIf: ["type", EN], showIf2: ["behavior", "path"] },
    NUM("pathWait", "Waits at each point (s)", 0, 0, 30, 0.1, { showIf: ["type", EN], showIf2: ["behavior", "path"] }),
    NUM("pathFov", "While patrolling it only sees you in front (view width in degrees, 360 = all around)", 140, 10, 360, 5, { showIf: ["type", EN], showIf2: ["behavior", "path"] }),
    NUM("pathGiveUp", "Goes back to its path after losing you for (s)", 5, 0.5, 120, 0.5, { showIf: ["type", EN], showIf2: ["behavior", "path"] }),
    { k: "mount", label: "Where it sits", type: "choice", options: [["floor", "On the floor (a turret)"], ["ceiling", "On the nearest ceiling above it"]], def: "floor", showIf: ["type", EN], showIf2: ["behavior", "stationary"] },
    { k: "turn", label: "Turns to face you", type: "bool", def: true, showIf: ["type", EN], showIf2: ["behavior", "stationary"] },
    { k: "ceilFlip", label: "Hang upside down (model turned over)", type: "bool", def: true, showIf: ["type", EN], showIf2: ["mount", "ceiling"], also: [["behavior", "stationary"]] },
    NUM("coneAngle", "Detection cone: width (degrees from straight down)", 25, 2, 85, 1, { showIf: ["type", EN], showIf2: ["mount", "ceiling"], also: [["behavior", "stationary"]] }),
    NUM("coneRange", "Detection cone: reach (units down, 128 = a story)", 400, 32, 5000, 8, { showIf: ["type", EN], showIf2: ["mount", "ceiling"], also: [["behavior", "stationary"]] }),
    { k: "ceilAction", label: "When you walk under it", type: "choice", options: [["shoot", "Shoots down at you (its Attack settings, projectile if melee)"], ["drop", "Drops down (and maybe turns into another enemy)"], ["selfdestruct", "Self-destructs (explodes)"]], def: "shoot", showIf: ["type", EN], showIf2: ["mount", "ceiling"], also: [["behavior", "stationary"]] },
    { k: "dropInto", label: "After landing it becomes (empty = stays itself and hunts you)", type: "propRef", refType: "enemy", def: "", showIf: ["type", EN], showIf2: ["ceilAction", "drop"], also: [["behavior", "stationary"], ["mount", "ceiling"]] },
    { k: "sdFall", label: "Falls first, explodes when it lands", type: "bool", def: false, showIf: ["type", EN], showIf2: ["ceilAction", "selfdestruct"], also: [["behavior", "stationary"], ["mount", "ceiling"]] },
    NUM("fuse", "Fuse before exploding (s, it blinks)", 0.6, 0, 10, 0.05, { showIf: ["type", EN], showIf2: ["ceilAction", "selfdestruct"], also: [["behavior", "stationary"], ["mount", "ceiling"]] }),
    { k: "dropSound", label: "Sound when it lets go / arms itself", type: "asset", assetType: "sound", def: "", showIf: ["type", EN], showIf2: ["mount", "ceiling"], also: [["behavior", "stationary"]] },
    // blast (self-destruct, and the "pieces explode" death)
    NUM("blastRadius", "Blast radius (units, 0 = harmless)", 160, 0, 3000, 4, { showIf: ["type", EN] }), NUM("blastDamage", "Blast damage (in the middle)", 30, 0, 100000, 1, { showIf: ["type", EN] }),
    { k: "blastColor", label: "Explosion colour", type: "color", def: "#ff8a2a", showIf: ["type", EN] }, NUM("blastCount", "Explosion particles", 40, 0, 200, 1, { showIf: ["type", EN] }),
    { k: "blastSphere", label: "Expanding explosion sphere (grows to the blast radius)", type: "bool", def: true, showIf: ["type", EN] },
    NUM("blastSphereTime", "Sphere: how long it grows (s)", 0.45, 0.05, 5, 0.05, { showIf: ["type", EN], showIf2: ["blastSphere", true] }),
    { k: "blastSphereColor1", label: "Sphere: middle colour", type: "color", def: "#ffe08a", showIf: ["type", EN], showIf2: ["blastSphere", true] }, { k: "blastSphereColor2", label: "Sphere: edge colour", type: "color", def: "#ff4a1a", showIf: ["type", EN], showIf2: ["blastSphere", true] },
    { k: "blastSphereAnim", label: "Sphere: animated gradient", type: "choice", options: [["none", "None (middle to edge)"], ["bands", "Rings rolling over it"], ["swirl", "Swirl"], ["pulse", "Pulse (colors swap)"], ["noise", "Flicker (energy noise)"]], def: "bands", showIf: ["type", EN], showIf2: ["blastSphere", true] },
    NUM("blastSphereSpeed", "Sphere: animation speed", 2, 0, 20, 0.1, { showIf: ["type", EN], showIf2: ["blastSphere", true] }), NUM("blastSphereGlow", "Sphere: glow", 1.2, 0, 6, 0.05, { showIf: ["type", EN], showIf2: ["blastSphere", true] }), { k: "blastSound", label: "Explosion sound", type: "asset", assetType: "sound", def: "", showIf: ["type", EN] },
    { k: "attack", label: "Attack", type: "choice", options: [["melee", "Melee (hits up close)"], ["hitscan", "Gun (instant hit)"], ["projectile", "Projectile (a flying ball you can dodge)"]], def: "melee", showIf: ["type", EN] },
    NUM("attackRange", "Attacks from (units)", 70, 10, 5000, 5, { showIf: ["type", EN] }), NUM("damage", "Damage per hit", 10, 0, 100000, 1, { showIf: ["type", EN] }),
    NUM("attackRate", "Time between attacks (s)", 1.2, 0.1, 30, 0.05, { showIf: ["type", EN] }), NUM("attackDelay", "Hit lands after (s, into the attack)", 0.3, 0, 10, 0.05, { showIf: ["type", EN] }),
    NUM("projSpeed", "Projectile speed (units / s)", 600, 50, 5000, 10, { showIf: ["type", EN], showIf2: ["attack", "projectile"] }), NUM("projSize", "Projectile size", 6, 1, 100, 0.5, { showIf: ["type", EN], showIf2: ["attack", "projectile"] }),
    { k: "projColor", label: "Projectile colour", type: "color", def: "#ff6a2a", showIf: ["type", EN] },
    ANIM("walkAnim", "Walking animation", { showIf: ["type", EN] }), ANIM("attackAnim", "Attack animation", { showIf: ["type", EN] }), ANIM("hurtAnim", "Hurt animation", { showIf: ["type", EN] }), ANIM("deathAnim", "Death animation", { showIf: ["type", EN] }),
    { k: "deathStyle", label: "How it dies", type: "choice", options: [["anim", "Death animation"], ["slice", "Sliced into pieces (3D models)"]], def: "anim", showIf: ["type", EN] },
    NUM("slicePieces", "Pieces", 4, 2, 8, 1, { showIf: ["type", EN], showIf2: ["deathStyle", "slice"] }), NUM("sliceForce", "How hard the pieces fly apart", 260, 0, 3000, 10, { showIf: ["type", EN], showIf2: ["deathStyle", "slice"] }),
    { k: "sliceInside", label: "Colour of the cut faces", type: "color", def: "#7a1414", showIf: ["type", EN], showIf2: ["deathStyle", "slice"] },
    { k: "sliceBlast", label: "Then the pieces explode", type: "bool", def: true, showIf: ["type", EN], showIf2: ["deathStyle", "slice"] },
    NUM("sliceTime", "Pieces explode after (s) / stay for (s) without exploding", 0.7, 0.05, 60, 0.05, { showIf: ["type", EN], showIf2: ["deathStyle", "slice"] }),
    { k: "sliceSound", label: "Sound when it is sliced", type: "asset", assetType: "sound", def: "", showIf: ["type", EN], showIf2: ["deathStyle", "slice"] },
    { k: "corpse", label: "Body stays after death", type: "bool", def: true, showIf: ["type", EN] },
    { k: "sightSound", label: "Sound when it spots you", type: "asset", assetType: "sound", def: "", showIf: ["type", EN] }, { k: "attackSound", label: "Attack sound", type: "asset", assetType: "sound", def: "", showIf: ["type", EN] },
    { k: "hurtSound", label: "Hurt sound", type: "asset", assetType: "sound", def: "", showIf: ["type", EN] }, { k: "deathSound", label: "Death sound", type: "asset", assetType: "sound", def: "", showIf: ["type", EN] },
    { k: "killTag", label: "When killed, change this number tag (e.g. a score)", type: "tagRef", refType: "number", def: "", showIf: ["type", EN] }, NUM("killAmount", "By", 1, -100000, 100000, 1, { showIf: ["type", EN] }),
    // level exit
    { k: "exitTo", label: "Leads to", type: "choice", options: [["map", "Another map"], ["screen", "A screen (e.g. a 'You win' screen)"]], def: "map", showIf: ["type", "exit"] },
    { k: "exitMap", label: "Map", type: "mapRef", def: "", showIf: ["type", "exit"], showIf2: ["exitTo", "map"] }, { k: "exitScreen", label: "Screen", type: "screenRef", def: "", showIf: ["type", "exit"], showIf2: ["exitTo", "screen"] },
    NUM("exitRadius", "Touch distance (units)", 40, 8, 400, 1, { showIf: ["type", "exit"] }), { k: "exitSound", label: "Sound", type: "asset", assetType: "sound", def: "", showIf: ["type", "exit"] },
  ];
  S.defineProp = (project, p, over) => { for (const pr of S.PROP) { const r = rec(pr, "props", p.id); if (pr.type === "anim") r.modelTag = p.id + ".asset"; if (over && pr.k in over) r.value = over[pr.k]; project.tags[p.id + "." + pr.k] = r; } };
  S.addProp = (project, over) => { const p = { id: S.uid("prop", project.props.map(x => x.id)) }; project.props.push(p); S.defineProp(project, p, Object.assign({ name: p.id }, over || {})); return p; };
  // ---- objects that give off light (lamps, torches, glowing crystals...): a point light above the object, optionally casting real-time shadows
  S.PROP.push({ k: "lightOn", label: "Gives off light", type: "bool", def: false, showIf: ["type", ["decoration", "pickup", "key", "enemy", "boss", "exit", "npc", "interactive", "checkpoint"]] },
    { k: "lightColor", label: "Light colour", type: "color", def: "#ffd9a0", showIf: ["lightOn", true] }, NUM("lightIntensity", "Light brightness", 1.6, 0, 10, 0.05, { showIf: ["lightOn", true] }),
    NUM("lightRange", "Light reach (units, 128 = a cell)", 420, 32, 4000, 8, { showIf: ["lightOn", true] }), NUM("lightHeight", "Light height above the floor (units)", 80, -64, 512, 1, { showIf: ["lightOn", true] }),
    NUM("lightFlicker", "Flicker (0 = steady, 1 = like a torch)", 0, 0, 1, 0.01, { showIf: ["lightOn", true] }), { k: "lightShadows", label: "Casts shadows", type: "bool", def: true, showIf: ["lightOn", true] });
  // ---- NPC talk (characters): talk settings are prop tags; the dialogue is a list of lines (structure prop.dialog.lines = [{id}]) whose texts are tags <prop>.d.<line>.*
  S.PROP.push({ k: "spkSound", label: "Sound", type: "asset", assetType: "sound", def: "", showIf: ["type", "speaker"] }, NUM("spkVolume", "Volume", 1, 0, 1, 0.01, { showIf: ["type", "speaker"] }),
    NUM("spkRadius", "Heard within (units, 128 = a cell; silent outside)", 512, 16, 8000, 8, { showIf: ["type", "speaker"] }), NUM("spkHeight", "Sound height above the floor", 64, 0, 1024, 1, { showIf: ["type", "speaker"] }),
    { k: "spkLoop", label: "Loops forever (off = plays once each time you walk into its radius)", type: "bool", def: true, showIf: ["type", "speaker"] },
    { k: "spkShow", label: "Model visible in the game", type: "bool", def: false, showIf: ["type", "speaker"] });
  // spawner: makes a new copy of another object every few seconds, around itself
  S.PROP.push({ k: "spawnWhat", label: "Spawns this object", type: "propRef", def: "", showIf: ["type", "spawner"] },
    NUM("spawnEvery", "Every (seconds)", 4, 0.1, 600, 0.1, { showIf: ["type", "spawner"] }), NUM("spawnFirst", "First one after (seconds)", 1, 0, 600, 0.1, { showIf: ["type", "spawner"] }),
    NUM("spawnMax", "At most this many alive at once (0 = no limit)", 3, 0, 100, 1, { showIf: ["type", "spawner"] }), NUM("spawnTotal", "In total (0 = never stops)", 0, 0, 10000, 1, { showIf: ["type", "spawner"] }),
    NUM("spawnRadius", "Spread around it (units, 0 = exactly on it)", 48, 0, 1024, 4, { showIf: ["type", "spawner"] }), NUM("spawnNear", "Only while you are within (units, 0 = always)", 0, 0, 20000, 16, { showIf: ["type", "spawner"] }),
    { k: "spawnAwake", label: "Spawned enemies hunt you right away", type: "bool", def: true, showIf: ["type", "spawner"] },
    { k: "spawnFx", label: "Flash + particles when something appears", type: "bool", def: true, showIf: ["type", "spawner"] }, { k: "spawnColor", label: "Flash colour", type: "color", def: "#7df3ff", showIf: ["type", "spawner"], showIf2: ["spawnFx", true] },
    { k: "spawnSound", label: "Sound when something appears", type: "asset", assetType: "sound", def: "", showIf: ["type", "spawner"] },
    { k: "spawnNeedTag", label: "Only while this tag…", type: "tagRef", refType: "any", def: "", showIf: ["type", "spawner"] }, { k: "spawnNeedOp", label: "is", type: "choice", options: [["=", "equal to"], ["!=", "not equal to"], [">", "greater than"], ["<", "less than"], [">=", "at least"], ["<=", "at most"]], def: "=", showIf: ["type", "spawner"] }, { k: "spawnNeedVal", label: "this value", type: "string", def: "yes", showIf: ["type", "spawner"] },
    { k: "spawnShow", label: "Model visible in the game", type: "bool", def: false, showIf: ["type", "spawner"] });
  S.PROP.push({ k: "popupOn", label: "Show a pop-up when picked up (pauses the game)", type: "bool", def: false, showIf: ["type", ["pickup", "key"]] },
    { k: "popupTitle", label: "Pop-up title (empty = the object's name)", type: "string", def: "", showIf: ["type", ["pickup", "key"]], showIf2: ["popupOn", true] },
    { k: "popupText", label: "Pop-up text", type: "string", def: "", showIf: ["type", ["pickup", "key"]], showIf2: ["popupOn", true] },
    { k: "popupImage", label: "Pop-up picture", type: "asset", assetType: "image", def: "", showIf: ["type", ["pickup", "key"]], showIf2: ["popupOn", true] },
    { k: "popupFirst", label: "Only the first time you get this kind of item", type: "bool", def: true, showIf: ["type", ["pickup", "key"]], showIf2: ["popupOn", true] },
    { k: "popupSound", label: "Pop-up sound (fanfare)", type: "asset", assetType: "sound", def: "", showIf: ["type", ["pickup", "key"]], showIf2: ["popupOn", true] },
    NUM("cpRadius", "Touch radius (units, 128 = a cell)", 64, 8, 1000, 4, { showIf: ["type", "checkpoint"] }), { k: "cpHeal", label: "Refills health", type: "bool", def: true, showIf: ["type", "checkpoint"] },
    { k: "cpMessage", label: "Message when reached", type: "string", def: "Checkpoint", showIf: ["type", "checkpoint"] }, { k: "cpSound", label: "Sound when reached", type: "asset", assetType: "sound", def: "", showIf: ["type", "checkpoint"] });
  S.PROP.push({ k: "useMode", label: "How it is used", type: "choice", options: [["key", "Look at a hitbox and press the Interact key (E)"], ["aim", "Aim the crosshair at a hitbox and click"], ["cursor", "Screen: step close, the mouse moves a cursor, click hitboxes"]], def: "key", showIf: ["type", "interactive"] },
    NUM("useRange", "Can be used from (units, 128 = a cell)", 120, 16, 1000, 4, { showIf: ["type", "interactive"] }),
    { k: "useHint", label: "Hint text (empty = the hitbox name)", type: "string", def: "", showIf: ["type", "interactive"] },
    { k: "showBoxes", label: "Show the hitboxes while playing (for testing)", type: "bool", def: false, showIf: ["type", "interactive"] });
  // ---- keys: a key object is picked up like a pick-up; while you hold it its live tag <key>.have is yes. Doors can need a key (or a tag) to open.
  const OPS = [["=", "equal to"], ["!=", "not equal to"], [">", "greater than"], ["<", "less than"], [">=", "at least"], ["<=", "at most"]];
  S.PROP.push({ k: "keyName", label: "Key name (shown in messages)", type: "string", def: "Red key", showIf: ["type", "key"] },
    { k: "keyColor", label: "Key colour (map, minimap, HUD)", type: "color", def: "#ff3a3a", showIf: ["type", "key"] },
    { k: "keyIcon", label: "HUD picture (empty = a key symbol in the key colour)", type: "asset", assetType: "image", def: "", showIf: ["type", "key"] },
    { k: "keyKeep", label: "Kept when you go to the next level", type: "bool", def: false, showIf: ["type", "key"] },
    { k: "lockKey", label: "Locked: needs this key", type: "propRef", refType: "key", def: "", showIf: ["type", "door"] },
    { k: "lockUse", label: "The key is used up when the door opens", type: "bool", def: false, showIf: ["type", "door"] },
    { k: "lockNeedTag", label: "Locked: only opens while this tag…", type: "tagRef", refType: "any", def: "", showIf: ["type", "door"] }, { k: "lockNeedOp", label: "is", type: "choice", options: OPS, def: "=", showIf: ["type", "door"] }, { k: "lockNeedVal", label: "this value", type: "string", def: "yes", showIf: ["type", "door"] },
    { k: "lockMsg", label: "Message when locked ({key} = the key's name)", type: "string", def: "Locked. You need the {key}.", showIf: ["type", "door"] },
    { k: "lockSound", label: "Sound when locked", type: "asset", assetType: "sound", def: "", showIf: ["type", "door"] },
    { k: "shotFirst", label: "First shot that opens it must be", type: "choice", options: [["any", "any shot"], ["weapon", "from a certain weapon (e.g. a missile)"], ["charged", "a charged shot"]], def: "any", showIf: ["type", "door"], showIf2: ["openMode", "shot"] },
    { k: "shotWeapon", label: "That weapon", type: "weaponRef", def: "", showIf: ["type", "door"], showIf2: ["shotFirst", "weapon"] },
    { k: "shotAfter", label: "After that first shot it opens with any weapon", type: "bool", def: true, showIf: ["type", "door"], showIf2: ["openMode", "shot"] },
    { k: "shotMsg", label: "Message when the wrong shot hits it (empty = none)", type: "string", def: "", showIf: ["type", "door"], showIf2: ["openMode", "shot"] },
    // energy shield: shown while the door needs a certain weapon / a charged shot, or is sealed by its tag (boss rooms); the right shot breaks it
    { k: "shieldOn", label: "Energy shield", type: "choice", options: [["auto", "Yes: while it needs a certain weapon or a charged shot, or is sealed by its tag"], ["off", "Never"]], def: "auto", showIf: ["type", "door"] },
    { k: "shieldColor", label: "Shield colour (sealed and charged-shot doors; weapon doors use the weapon's Door colour)", type: "color", def: "#ff3a5a", showIf: ["type", "door"], showIf2: ["shieldOn", "auto"] },
    NUM("shieldAlpha", "Shield strength (see-through 0 .. 1 solid glow)", 0.5, 0.05, 1, 0.01, { showIf: ["type", "door"], showIf2: ["shieldOn", "auto"] }),
    NUM("shieldSpeed", "Shield movement speed", 1, 0, 10, 0.05, { showIf: ["type", "door"], showIf2: ["shieldOn", "auto"] }),
    NUM("shieldSize", "Shield pattern size (bigger = finer)", 1, 0.1, 10, 0.05, { showIf: ["type", "door"], showIf2: ["shieldOn", "auto"] }),
    NUM("shieldGlow", "Shield glow", 1.4, 0, 6, 0.05, { showIf: ["type", "door"], showIf2: ["shieldOn", "auto"] }),
    { k: "ricochet", label: "Wrong shots bounce off the shield (once)", type: "bool", def: true, showIf: ["type", "door"], showIf2: ["shieldOn", "auto"] },
    { k: "shieldClank", label: "Sound when a wrong shot hits the shield", type: "asset", assetType: "sound", def: "", showIf: ["type", "door"], showIf2: ["shieldOn", "auto"] },
    { k: "shieldBreak", label: "Sound when the shield breaks", type: "asset", assetType: "sound", def: "", showIf: ["type", "door"], showIf2: ["shieldOn", "auto"] });
  // ---- solid objects: a collision box that blocks walking (and, if set, shots). In the object's own space = the Object Editor grid (X right, Y up, Z front, origin = middle of the cell on the floor)
  const SOL = ["decoration", "interactive", "save", "checkpoint", "speaker", "spawner", "npc", "exit"], SB = { showIf: ["type", SOL], showIf2: ["solid", "box"] };
  S.PROP.push({ k: "solid", label: "Solid (blocks the player and enemies)", type: "choice", options: [["none", "No: you walk through it"], ["auto", "Yes: a box around the model"], ["box", "Yes: my own boxes (drag them in the view)"]], def: "none", showIf: ["type", SOL] },
    { k: "solidShots", label: "Also stops shots", type: "bool", def: true, showIf: ["type", SOL], showIf2: ["solid", ["auto", "box"]] },
    NUM("solidX", "Box middle X (right)", 0, -1024, 1024, 1, SB), NUM("solidY", "Box middle Y (up)", 32, -1024, 2048, 1, SB), NUM("solidZ", "Box middle Z (front)", 0, -1024, 1024, 1, SB),
    NUM("solidW", "Box width (X)", 64, 1, 2048, 1, SB), NUM("solidH", "Box height (Y)", 64, 1, 2048, 1, SB), NUM("solidD", "Box depth (Z)", 64, 1, 2048, 1, SB));
  // ---- visors: scanning, codex, which visor shows / highlights the object
  S.PROP.push({ k: "scannable", label: "Can be scanned (Scan visor)", type: "bool", def: false }, { k: "scanTitle", label: "Scan title", type: "string", def: "", showIf: ["scannable", true] },
    { k: "scanText", label: "Scan pop-up text (short)", type: "string", def: "", multi: true, showIf: ["scannable", true] }, { k: "scanImage", label: "Scan picture (optional)", type: "asset", assetType: "image", def: "", showIf: ["scannable", true] },
    { k: "scanTime", label: "Scan time (s, 0 = the visor's)", type: "number", def: 0, min: 0, max: 30, step: 0.05, showIf: ["scannable", true] },
    { k: "codexCat", label: "Codex group", type: "choice", options: [], def: "auto", showIf: ["scannable", true] }, { k: "codexText", label: "Codex entry (the long text in the codex)", type: "string", def: "", multi: true, showIf: ["scannable", true] },
    { k: "visorShow", label: "Highlighted in", type: "choice", options: [["auto", "Automatic (Scanner: if it can be scanned; X-Ray: characters and enemies)"], ["scan", "The Scanner"], ["xray", "X-Ray"], ["both", "Scanner and X-Ray"], ["all", "Every visor"], ["none", "No visor"]], def: "auto" },
    { k: "visorColorOn", label: "Own highlight colour", type: "bool", def: false }, Object.assign(COLOR("visorColor", "Highlight colour", "#ffffff"), { showIf: ["visorColorOn", true] }),
    { k: "seenIn", label: "Can be seen", type: "choice", options: [["always", "Always"]], def: "always" }, { k: "solidUnseen", label: "Still solid while it can not be seen", type: "bool", def: false });
  // ---- drones (behavior "drone"): hover, circle the player, stop at random moments, then shoot or dash at the player
  const DR = { showIf: ["type", EN], showIf2: ["behavior", "drone"] };
  S.PROP.push(NUM("hover", "Flies this high above the floor (units, 64 = 1 m)", 80, 0, 1000, 1, DR), NUM("hoverBob", "Bobs up and down by (units)", 6, 0, 64, 1, DR),
    NUM("orbitDist", "Circles at this distance from you (units)", 260, 32, 3000, 4, DR),
    { k: "orbitTurn", label: "Changes its circling direction now and then", type: "bool", def: true, ...DR },
    NUM("droneWaitMin", "Circles at least (s) before stopping", 1.5, 0, 30, 0.1, DR), NUM("droneWaitMax", "…and at most (s)", 4, 0, 30, 0.1, DR),
    NUM("droneStop", "Stops and aims for (s) before attacking", 0.6, 0, 10, 0.05, DR),
    { k: "droneAttack", label: "Then it", type: "choice", options: [["shoot", "Shoots at you (its Attack settings; melee = projectile)"], ["dash", "Dashes into you"], ["random", "Randomly shoots or dashes"]], def: "random", ...DR },
    NUM("droneShots", "Shots per stop", 1, 1, 50, 1, DR), NUM("droneShotGap", "Time between those shots (s)", 0.2, 0.02, 5, 0.01, DR),
    NUM("droneDashSpeed", "Dash speed (units/s)", 900, 50, 5000, 10, DR), NUM("droneDashTime", "Dash lasts at most (s)", 0.8, 0.05, 5, 0.05, DR),
    NUM("droneDashDamage", "Dash damage (0 = its Damage)", 0, 0, 100000, 1, DR), { k: "droneDashSound", label: "Dash sound", type: "asset", assetType: "sound", def: "", ...DR },
    NUM("droneRecover", "After attacking it waits (s)", 0.5, 0, 10, 0.05, DR));
  // ---- enemy muzzle: where its shots come from (the Object Editor grid: X right, Y up, Z front) + an arrow (yaw / pitch; 0 = straight ahead)
  const MZ = { showIf: ["type", EN], showIf2: ["muzzleOn", true] };
  S.PROP.push({ k: "muzzleOn", label: "Shots come from a muzzle point (drag it in the view)", type: "bool", def: false, showIf: ["type", EN] },
    NUM("muzzleX", "Muzzle X (right)", 0, -2048, 2048, 0.5, MZ), NUM("muzzleY", "Muzzle Y (up)", 60, -2048, 4096, 0.5, MZ), NUM("muzzleZ", "Muzzle Z (front)", 20, -2048, 2048, 0.5, MZ),
    NUM("muzzleYaw", "Arrow: turn left / right (degrees)", 0, -180, 180, 1, MZ), NUM("muzzlePitch", "Arrow: up / down (degrees)", 0, -89, 89, 1, MZ),
    { k: "muzzleAim", label: "Shots fly", type: "choice", options: [["player", "From the muzzle toward you"], ["arrow", "Along the arrow (straight out of the gun)"]], def: "player", ...MZ });
  // ---- pick-ups and keys: a sound they make while lying there (a hum you hear when you get close)
  S.PROP.push({ k: "humSound", label: "Sound it makes while lying there (loops, heard nearby)", type: "asset", assetType: "sound", def: "", showIf: ["type", ["pickup", "key"]] },
    NUM("humVolume", "…its volume", 0.7, 0, 2, 0.05, { showIf: ["type", ["pickup", "key"]] }), NUM("humRadius", "…heard up to (units)", 384, 32, 4000, 8, { showIf: ["type", ["pickup", "key"]] }));
  // ---- save stations: step in, press Interact, (confirm,) the game is saved to the current slot
  S.PROP.push(NUM("saveRadius", "Can be used within (units, 128 = a cell)", 96, 16, 600, 4, { showIf: ["type", "save"] }),
    { k: "saveName", label: "Name in the save list (empty = the map's name)", type: "string", def: "", showIf: ["type", "save"] },
    { k: "saveAsk", label: "Asks “Save the game?” first", type: "bool", def: true, showIf: ["type", "save"] },
    { k: "saveHeal", label: "Refills health and energy tanks", type: "bool", def: true, showIf: ["type", "save"] },
    { k: "saveAmmo", label: "Refills ammo", type: "bool", def: true, showIf: ["type", "save"] },
    { k: "saveMsg", label: "Message after saving", type: "string", def: "Game saved", showIf: ["type", "save"] },
    { k: "saveSound", label: "Sound when saving", type: "asset", assetType: "sound", def: "", showIf: ["type", "save"] });
  // ---- bosses: an enemy (all the enemy settings) + a fight (health bar, intro, music, win) + phases (structure prop.phases = [{id}], values = tags <prop>.ph.<phase>.*)
  const BS = { showIf: ["type", "boss"] }, BX = (o) => Object.assign({ showIf: ["type", "boss"] }, o || {});
  S.PROP.push({ k: "bossName", label: "Boss name (health bar)", type: "string", def: "", showIf: ["type", "boss"] },
    { k: "bossBar", label: "Show the boss health bar", type: "bool", def: true, showIf: ["type", "boss"] },
    { k: "bossStart", label: "The fight starts", type: "choice", options: [["sees", "When it sees you"], ["tag", "When a tag…"]], def: "sees", showIf: ["type", "boss"] },
    { k: "bossStartTag", label: "…this tag", type: "tagRef", refType: "any", def: "", showIf: ["type", "boss"], showIf2: ["bossStart", "tag"] }, { k: "bossStartOp", label: "is", type: "choice", options: OPS, def: "=", showIf: ["type", "boss"], showIf2: ["bossStart", "tag"] }, { k: "bossStartVal", label: "this value", type: "string", def: "yes", showIf: ["type", "boss"], showIf2: ["bossStart", "tag"] },
    NUM("bossIntroTime", "Intro: waits before attacking (s, can't be hurt)", 1.5, 0, 30, 0.1, BS), { k: "bossIntroMsg", label: "Intro message (empty = none)", type: "string", def: "", showIf: ["type", "boss"] },
    { k: "bossIntroSound", label: "Intro sound (roar)", type: "asset", assetType: "sound", def: "", showIf: ["type", "boss"] },
    { k: "bossMusic", label: "Fight music (loops until it is defeated)", type: "asset", assetType: "sound", def: "", showIf: ["type", "boss"] }, NUM("bossMusicVol", "Music volume", 0.8, 0, 1, 0.01, BS),
    { k: "bossFlinch", label: "Flinches when hit (hurt animation)", type: "bool", def: false, showIf: ["type", "boss"] },
    { k: "bossWinTag", label: "When defeated, set this tag (e.g. to open a door)", type: "tagRef", refType: "any", def: "", showIf: ["type", "boss"] }, { k: "bossWinVal", label: "to (value, +n / -n adds, toggle)", type: "string", def: "yes", showIf: ["type", "boss"] },
    { k: "bossWinMsg", label: "Message when defeated", type: "string", def: "", showIf: ["type", "boss"] },
    // how it moves and shoots: the boss's own values = its first phase; each phase can change them
    { k: "bossMove", label: "Moves", type: "choice", options: [["chase", "Walks toward you"], ["stationary", "Stays where it is (turns to you)"], ["keep", "Keeps its distance"], ["circle", "Circles around you"], ["dash", "Charges at you now and then"]], def: "chase", showIf: ["type", "boss"] },
    NUM("keepDist", "Distance it keeps (units)", 500, 50, 5000, 10, BX({ showIf2: ["bossMove", ["keep", "circle"]] })),
    NUM("dashSpeed", "Charge speed (units / s)", 900, 50, 5000, 10, BX({ showIf2: ["bossMove", "dash"] })), NUM("dashEvery", "Charges every (s)", 3, 0.3, 60, 0.1, BX({ showIf2: ["bossMove", "dash"] })), NUM("dashTime", "A charge lasts (s)", 0.6, 0.05, 10, 0.05, BX({ showIf2: ["bossMove", "dash"] })),
    { k: "pattern", label: "Shot pattern", type: "choice", options: [["aimed", "One shot at you"], ["spread", "A fan of shots at you"], ["ring", "A ring all around"], ["spiral", "A spinning ring (turns each volley)"], ["rain", "Rain from above, around you"]], def: "aimed", showIf: ["type", "boss"], showIf2: ["attack", ["hitscan", "projectile"]] },
    NUM("patCount", "Shots in the pattern", 5, 1, 64, 1, BX({ showIf2: ["attack", ["hitscan", "projectile"]] })), NUM("patSpread", "Fan width (degrees) / rain radius (units)", 40, 1, 2000, 1, BX({ showIf2: ["attack", ["hitscan", "projectile"]] })),
    NUM("volleys", "Volleys per attack", 1, 1, 50, 1, BX({ showIf2: ["attack", ["hitscan", "projectile"]] })), NUM("volleyGap", "Time between volleys (s)", 0.25, 0.02, 5, 0.01, BX({ showIf2: ["attack", ["hitscan", "projectile"]] })),
    NUM("patTurn", "Spiral: turn per volley (degrees)", 15, -180, 180, 1, BX({ showIf2: ["attack", ["hitscan", "projectile"]] })),
    NUM("armor", "Damage it takes (1 = normal, 0.5 = half, 0 = can't be hurt)", 1, 0, 10, 0.05, BS));
  // drops: what a killed enemy leaves behind (structure prop.drops = [{id}], values = tags <prop>.dr.<id>.*); each row rolls its own chance
  S.DROP = [{ k: "what", label: "Drops", type: "propRef", def: "" }, NUM("chance", "Chance (%)", 50, 0, 100, 1), NUM("count", "How many", 1, 1, 50, 1)];
  S.addDrop = (project, p, over) => { p.drops = p.drops || []; const d = { id: S.uid("dr", p.drops.map(x => x.id)) }; p.drops.push(d); for (const pr of S.DROP) { const r = rec(pr, "drops", p.id); if (over && pr.k in over) r.value = over[pr.k]; project.tags[p.id + ".dr." + d.id + "." + pr.k] = r; } return d; };
  S.removeDrop = (project, p, d) => { p.drops.splice(p.drops.indexOf(d), 1); for (const k of Object.keys(project.tags)) if (k.startsWith(p.id + ".dr." + d.id + ".")) delete project.tags[k]; };
  S.PROP.push(NUM("dropSpread", "Drops scatter within (units)", 40, 0, 1000, 4, { showIf: ["type", EN] }));
  // the values a phase can change (everything else stays the boss's own)
  S.PHASE_KEYS = ["bossMove", "speed", "keepDist", "dashSpeed", "dashEvery", "dashTime", "attack", "attackRange", "damage", "attackRate", "attackDelay", "projSpeed", "projSize", "projColor", "pattern", "patCount", "patSpread", "volleys", "volleyGap", "patTurn", "armor", "idleAnim", "walkAnim", "attackAnim", "hurtAnim"];
  S.PHASE = [{ k: "name", label: "Phase name", type: "string", def: "Phase" }, NUM("startAt", "Starts when its health is at or below (%)", 100, 0, 100, 1),
    NUM("enterTime", "Change-over: pause before it fights again (s, can't be hurt)", 1, 0, 30, 0.1), ANIM("enterAnim", "Change-over animation"), { k: "enterSound", label: "Change-over sound", type: "asset", assetType: "sound", def: "" },
    { k: "enterMsg", label: "Message on screen (empty = none)", type: "string", def: "" }, { k: "enterFlash", label: "Flash the screen", type: "bool", def: true }, { k: "enterColor", label: "Flash colour", type: "color", def: "#ffffff", showIf: ["enterFlash", true] },
    NUM("enterShake", "Shake the camera (0 = no)", 0.6, 0, 10, 0.1), NUM("enterHeal", "Heals itself by (% of full health)", 0, 0, 100, 1),
    { k: "enterSpawn", label: "Calls in helpers (enemy)", type: "propRef", refType: "enemy", def: "" }, NUM("enterSpawnCount", "How many helpers", 0, 0, 50, 1), NUM("enterSpawnRadius", "Helpers appear within (units)", 300, 0, 3000, 10),
    { k: "enterTag", label: "Set this tag", type: "tagRef", refType: "any", def: "" }, { k: "enterVal", label: "to (value, +n / -n adds, toggle)", type: "string", def: "yes" },
    { k: "enterMessage", label: "Send this message to every script (empty = none)", type: "string", def: "" },
    { k: "music", label: "Change the fight music to (empty = keep it)", type: "asset", assetType: "sound", def: "" }]
    .concat(S.PHASE_KEYS.map(k => { const d = S.PROP.find(q => q.k === k), c = Object.assign({}, d); for (const f of ["showIf", "showIf2", "also"]) delete c[f];
      const cond = [d.showIf, d.showIf2].concat(d.also || []).filter(x => x && x[0] !== "type"); if (cond[0]) c.showIf = cond[0]; if (cond[1]) c.showIf2 = cond[1]; c.ph = true; return c; }));
  const phRec = (p, pr) => { const r = rec(pr, "phase", p.id); if (pr.type === "anim") r.modelTag = p.id + ".asset"; return r; };
  S.addPhase = (project, p, over) => { p.phases = p.phases || []; const b = { id: S.uid("ph", p.phases.map(x => x.id)) }; p.phases.push(b);
    for (const pr of S.PHASE) { const r = phRec(p, pr); if (pr.ph && project.tags[p.id + "." + pr.k]) r.value = project.tags[p.id + "." + pr.k].value; if (over && pr.k in over) r.value = over[pr.k]; project.tags[p.id + ".ph." + b.id + "." + pr.k] = r; } return b; };
  S.removePhase = (project, p, b) => { p.phases.splice(p.phases.indexOf(b), 1); for (const k of Object.keys(project.tags)) if (k.startsWith(p.id + ".ph." + b.id + ".")) delete project.tags[k]; };
  // a key's live tag <key>.have exists only for key objects
  // ---- VISORS: preset types combat / scan / xray / night (+ custom). Structure project.visors = [{id}], values = tags <visorId>.* (Schema.VISOR)
  const VT = t => ({ showIf: ["type", t] }), VC = (k, label, def, t) => Object.assign(COLOR(k, label, def), t ? VT(t) : {});
  S.VISOR = [{ k: "name", label: "Name", type: "string", def: "Visor" },
    { k: "type", label: "Kind", type: "choice", options: [["combat", "Combat (normal view)"], ["scan", "Scanner (scan objects, codex entries)"], ["xray", "X-Ray (see characters and enemies through walls)"], ["night", "Night vision (full-bright + its effects)"], ["custom", "Custom (only its effects and highlights)"]], def: "combat" },
    { k: "on", label: "The player has it", type: "bool", def: true }, { k: "needTag", label: "…only while this tag (empty = always)", type: "tagRef", refType: "any", def: "" }, { k: "needVal", label: "…is", type: "string", def: "yes" },
    { k: "key", label: "Direct key (optional; the wheel always works)", type: "choice", options: [], def: "" }, { k: "icon", label: "Icon in the wheel (picture)", type: "asset", assetType: "image", def: "" }, COLOR("color", "Wheel / highlight colour", "#7df3ff"),
    { k: "hud", label: "HUD while it is on (empty = the normal HUD)", type: "screenRef", def: "" }, { k: "switchSound", label: "Sound when switching to it", type: "asset", assetType: "sound", def: "" }, { k: "loop", label: "Sound while it is on (loops)", type: "asset", assetType: "sound", def: "" },
    { k: "noFire", label: "Weapons can not fire while it is on", type: "bool", def: false },
    { k: "fullBright", label: "Full-bright (everything lit, no darkness)", type: "bool", def: false }, { k: "fogOff", label: "No fog", type: "bool", def: false },
    // highlights: outline + see-through fill on objects (traced geometry)
    { k: "fillA", label: "Highlight fill (0 = only outlines, 1 = solid)", type: "number", def: 0.28, min: 0, max: 1, step: 0.01 }, { k: "lineW", label: "Outline width (px)", type: "number", def: 2, min: 0, max: 8, step: 0.5 }, { k: "glow", label: "Outline brightness (above 1 glows with bloom)", type: "number", def: 1.6, min: 0, max: 8, step: 0.05 },
    { k: "pattern", label: "Fill pattern", type: "choice", options: [["none", "Smooth"], ["lines", "Scan lines"], ["grid", "Grid"], ["dots", "Dots"]], def: "lines" }, { k: "trace", label: "Trace the geometry (wireframe lines)", type: "bool", def: false }, { k: "traceA", label: "Wireframe strength", type: "number", def: 0.35, min: 0, max: 1, step: 0.01 },
    { k: "range", label: "Range (units; how far highlights and scanning reach)", type: "number", def: 1600, min: 64, max: 20000, step: 16 },
    Object.assign({ k: "scanKey", label: "Lock-on / scan button (hold)", type: "choice", options: [], def: "mouse2" }, VT("scan")), Object.assign({ k: "scanTime", label: "Scan time (s, objects can override)", type: "number", def: 1.2, min: 0.05, max: 30, step: 0.05 }, VT("scan")),
    Object.assign({ k: "scanAngle", label: "Lock-on reach around the crosshair (degrees)", type: "number", def: 12, min: 1, max: 60, step: 1 }, VT("scan")), Object.assign({ k: "scanTurn", label: "The view follows the locked object", type: "bool", def: true }, VT("scan")),
    Object.assign({ k: "scanSound", label: "Sound while scanning (loops)", type: "asset", assetType: "sound", def: "" }, VT("scan")), Object.assign({ k: "scanDone", label: "Sound when a scan is done", type: "asset", assetType: "sound", def: "" }, VT("scan")),
    VC("cEnemy", "Colour: enemies", "#ff4a4a", "scan"), VC("cBoss", "Colour: bosses", "#ff2ad4", "scan"), VC("cPickup", "Colour: pick-ups and keys", "#ffd84a", "scan"), VC("cDoor", "Colour: doors", "#4aa8ff", "scan"), VC("cNpc", "Colour: characters", "#7dff9a", "scan"), VC("cObject", "Colour: other objects", "#ff9a3a", "scan"), VC("cScanned", "Colour: already scanned", "#3a7dff", "scan"),
    VC("cChar", "Colour: characters and enemies", "#ff6a3a", "xray"), VC("cOther", "Colour: other objects shown in X-Ray", "#9ad4ff", "xray")];
  const vRec = (p, vid) => rec(p, "visor", vid);
  S.addVisor = (project, type, over) => { project.visors = project.visors || []; const base = { combat: "combat", scan: "scan", xray: "xray", night: "night" }[type] || "visor", id = S.uid(base, project.visors.map(v => v.id).concat(project.screens.map(s => s.id), Object.keys(project.tags).map(k => k.split(".")[0])).filter((x, i, a) => a.indexOf(x) === i)), v = { id }; project.visors.push(v);
    const pre = { combat: { name: "Combat" }, scan: { name: "Scan", noFire: true, color: "#ffb53a", key: "" }, xray: { name: "X-Ray", color: "#ff6a3a", fullBright: false, range: 1400, fillA: 0.45, pattern: "none" }, night: { name: "Night vision", color: "#5cff7a", fullBright: true, fogOff: true, fillA: 0 } }[type] || { name: "Visor" };
    for (const p of S.VISOR) { const r = vRec(p, id); if (p.k === "type") r.value = type; if (p.k in pre) r.value = pre[p.k]; if (over && p.k in over) r.value = over[p.k]; project.tags[id + "." + p.k] = r; } return v; };
  S.removeVisor = (project, v) => { project.visors = (project.visors || []).filter(x => x !== v); for (const k of Object.keys(project.tags)) if (k.startsWith(v.id + ".")) delete project.tags[k]; S.ensureVisors(project); };
  S.ensureVisors = project => { const T = project.tags; if (!project.visors) { project.visors = []; for (const t of ["combat", "scan", "xray", "night"]) S.addVisor(project, t); }
    for (const v of project.visors) for (const p of S.VISOR) { const k = v.id + "." + p.k; if (!T[k]) T[k] = vRec(p, v.id); if (p.options && (p.k === "key" || p.k === "scanKey")) T[k].options = [["", "(none)"]].concat(S.KEYS); else if (p.options) T[k].options = p.options; T[k].label = p.label; }
    const vo = [["", "Always"]].concat(project.visors.map(v => [v.id, "Only with: " + (T[v.id + ".name"] ? T[v.id + ".name"].value : v.id)])); for (const e of (project.compositor && project.compositor.effects) || []) { const k = "comp." + e.id + ".visor"; if (T[k]) T[k].options = vo; }
    const so = [["always", "Always"]].concat(project.visors.filter(v => T[v.id + ".type"] && T[v.id + ".type"].value !== "combat").map(v => [v.id, "Only with: " + T[v.id + ".name"].value])); for (const p of project.props || []) { const k = p.id + ".seenIn"; if (T[k]) T[k].options = so; }
    const st = T["player.visorStart"]; if (st) st.options = project.visors.map(v => [v.id, T[v.id + ".name"] ? T[v.id + ".name"].value : v.id]);
    for (const [k, l, ty, d] of [["visor", "Current visor (live)", "string", ""], ["visorName", "Current visor name (live)", "string", ""], ["visorType", "Current visor kind (live)", "string", "combat"], ["scanning", "Scan progress 0..1 (live)", "number", 0]]) if (!T["player." + k]) T["player." + k] = { type: ty, value: d, label: l, group: "player", builtin: true, live: true }; };
  // codex: one live yes/no tag per scannable object type, codex.<prop>, set when it is scanned (saved with the game)
  S.ensureCodex = project => { for (const p of project.props || []) { const k = "codex." + p.id, on = project.tags[p.id + ".scannable"] && project.tags[p.id + ".scannable"].value; if (on && !project.tags[k]) project.tags[k] = { type: "bool", value: false, label: "Scanned: " + ((project.tags[p.id + ".name"] || {}).value || p.id) + " (live)", group: "codex", builtin: true, live: true }; } };
  S.CODEXCAT = [["auto", "Automatic (by object kind)"], ["creatures", "Creatures"], ["characters", "Characters"], ["items", "Items"], ["tech", "Tech and places"], ["lore", "Lore"]];
  S.codexCat = (project, pid) => { const T = project.tags, c = T[pid + ".codexCat"] && T[pid + ".codexCat"].value, ty = T[pid + ".type"] && T[pid + ".type"].value; if (c && c !== "auto") return c; return ty === "enemy" || ty === "boss" ? "creatures" : ty === "npc" ? "characters" : ty === "pickup" || ty === "key" ? "items" : ty === "door" || ty === "save" || ty === "checkpoint" || ty === "exit" || ty === "interactive" ? "tech" : "lore"; };
  S.ensureKeys = project => { for (const p of project.props || []) { const k = p.id + ".have", isKey = project.tags[p.id + ".type"] && project.tags[p.id + ".type"].value === "key";
    if (isKey && !project.tags[k]) project.tags[k] = { type: "bool", value: false, label: "The player has this key (live)", group: "props", owner: p.id, builtin: true }; else if (!isKey && project.tags[k]) delete project.tags[k]; } };
  S.PROP.push({ k: "npcName", label: "Name shown when talking (empty = the object's name)", type: "string", def: "", showIf: ["type", "npc"] }, NUM("talkRange", "Can talk from (units, 128 = a cell)", 140, 30, 600, 5, { showIf: ["type", "npc"] }),
    ANIM("talkAnim", "Talking animation (3D model)", { showIf: ["type", "npc"], showIf2: ["look", "model"] }), { k: "talkSprite", label: "Talking strip (2D sprite)", type: "asset", assetType: "image", def: "", showIf: ["type", "npc"], showIf2: ["look", "sprite"] }, NUM("talkSpriteFrames", "Frames in it", 1, 1, 64, 1, { showIf: ["type", "npc"], showIf2: ["look", "sprite"] }),
    { k: "faceTalker", label: "Turns to face the player while talking", type: "bool", def: true, showIf: ["type", "npc"] },
    { k: "npcDialogue", label: "Dialogue (made in the Dialogue Editor; empty = the lines below)", type: "dialogRef", def: "", showIf: ["type", "npc"] });
  const goOpts = [["next", "the next line"], ["end", "the end (stop talking)"]];
  S.DLINE = [{ k: "speaker", label: "Who speaks (empty = the character)", type: "string", def: "" }, { k: "text", label: "What they say", type: "string", def: "..." }, { k: "sound", label: "Voice / sound", type: "asset", assetType: "sound", def: "" },
    { k: "next", label: "Then go to", type: "choice", options: goOpts, def: "next" }]
    .concat(...[1, 2, 3, 4].map(i => [{ k: "c" + i + "text", label: "Choice " + i + " (empty = no choice)", type: "string", def: "" }, { k: "c" + i + "go", label: "Choice " + i + " goes to", type: "choice", options: goOpts, def: "next" },
      { k: "c" + i + "tag", label: "Choice " + i + " changes the tag (optional)", type: "tagRef", refType: "any", def: "" }, { k: "c" + i + "val", label: "to (a number: 5, or +1 / -1 to add)", type: "string", def: "" }]));
  S.dlineOptions = p => goOpts.concat(((p.dialog && p.dialog.lines) || []).map((l, i) => [l.id, "line " + (i + 1) + " (" + l.id + ")"]));
  S.addDLine = (project, p, over) => { p.dialog = p.dialog || { lines: [] }; const l = { id: S.uid("line", p.dialog.lines.map(x => x.id)) }; p.dialog.lines.push(l); for (const pr of S.DLINE) { const r = rec(pr, "dialog", p.id); if (over && pr.k in over) r.value = over[pr.k]; project.tags[p.id + ".d." + l.id + "." + pr.k] = r; } S.syncDOpts(project, p); return l; };
  // ---- interactive objects: invisible hitboxes (structure prop.hitboxes = [{id}], values = tags <prop>.hb.<box>.*), in the object's own space
  // (the Object Editor grid: X right, Y up, Z front, origin = middle of the cell on the floor). Later in-game screens / puzzles reuse them (`kind`).
  S.HITBOX = [{ k: "name", label: "Name (shown in the hint, sent to nodes)", type: "string", def: "Button" },
    NUM("x", "Middle X (right)", 0, -512, 512, 0.5), NUM("y", "Middle Y (up)", 64, -512, 1024, 0.5), NUM("z", "Middle Z (front)", 8, -512, 512, 0.5),
    NUM("w", "Width", 24, 1, 1024, 0.5), NUM("h", "Height", 24, 1, 1024, 0.5), NUM("d", "Depth", 12, 1, 1024, 0.5),
    { k: "setTag", label: "When pressed: change this tag", type: "tagRef", refType: "any", def: "" }, { k: "setVal", label: "To (a value, +1 / -1 to add, “toggle” flips yes / no)", type: "string", def: "toggle" },
    { k: "message", label: "When pressed: send this message to node scripts", type: "string", def: "" }, { k: "sound", label: "Press sound", type: "asset", assetType: "sound", def: "" },
    { k: "anim", label: "Press animation (of the object's model)", type: "anim", def: "" }, NUM("cooldown", "Can be pressed again after (seconds)", 0.3, 0, 60, 0.05), { k: "once", label: "Works only once", type: "bool", def: false },
    { k: "needTag", label: "Locked unless this tag…", type: "tagRef", refType: "any", def: "" }, { k: "needOp", label: "is", type: "choice", options: [["=", "equal to"], ["!=", "not equal to"], [">", "greater than"], ["<", "less than"], [">=", "at least"], ["<=", "at most"]], def: "=" },
    { k: "needVal", label: "this value", type: "string", def: "yes" }, { k: "lockedSound", label: "Sound when locked", type: "asset", assetType: "sound", def: "" }];
  const hbRec = (p, pr) => { const r = rec(pr, "hitbox", p.id); if (pr.type === "anim") r.modelTag = p.id + ".asset"; return r; };
  S.addHitbox = (project, p, over) => { p.hitboxes = p.hitboxes || []; const b = { id: S.uid("hb", p.hitboxes.map(x => x.id)), kind: "button" }; p.hitboxes.push(b); for (const pr of S.HITBOX) { const r = hbRec(p, pr); if (over && pr.k in over) r.value = over[pr.k]; project.tags[p.id + ".hb." + b.id + "." + pr.k] = r; } return b; };
  // solid boxes (prop tag solid = "box"): structure prop.solids = [{id}], values = tags <prop>.sb.<id>.x/y/z (middle) + w/h/d, object space like hitboxes
  S.SBOX = [NUM("x", "Middle X (right)", 0, -2048, 2048, 1), NUM("y", "Middle Y (up)", 32, -2048, 4096, 1), NUM("z", "Middle Z (front)", 0, -2048, 2048, 1),
    NUM("w", "Width (X)", 64, 1, 4096, 1), NUM("h", "Height (Y)", 64, 1, 4096, 1), NUM("d", "Depth (Z)", 64, 1, 4096, 1)];
  S.addSolidBox = (project, p, over) => { p.solids = p.solids || []; const b = { id: S.uid("sb", p.solids.map(x => x.id)) }; p.solids.push(b); for (const pr of S.SBOX) { const r = rec(pr, "solid", p.id); if (over && pr.k in over) r.value = over[pr.k]; project.tags[p.id + ".sb." + b.id + "." + pr.k] = r; } return b; };
  S.removeSolidBox = (project, p, b) => { p.solids.splice(p.solids.indexOf(b), 1); for (const k of Object.keys(project.tags)) if (k.startsWith(p.id + ".sb." + b.id + ".")) delete project.tags[k]; };
  S.ensureSolids = project => { for (const p of project.props || []) { const v = k => project.tags[p.id + "." + k] ? project.tags[p.id + "." + k].value : undefined;
      if (v("solid") === "box" && !(p.solids && p.solids.length)) S.addSolidBox(project, p, { x: v("solidX") || 0, y: v("solidY") === undefined ? 32 : v("solidY"), z: v("solidZ") || 0, w: v("solidW") || 64, h: v("solidH") || 64, d: v("solidD") || 64 });
      for (const b of p.solids || []) for (const pr of S.SBOX) { const k = p.id + ".sb." + b.id + "." + pr.k; if (!project.tags[k]) project.tags[k] = rec(pr, "solid", p.id); } } };
  S.removeHitbox = (project, p, b) => { p.hitboxes.splice(p.hitboxes.indexOf(b), 1); for (const k of Object.keys(project.tags)) if (k.startsWith(p.id + ".hb." + b.id + ".")) delete project.tags[k]; };
  S.removeDLine = (project, p, l) => { p.dialog.lines.splice(p.dialog.lines.indexOf(l), 1); for (const k of Object.keys(project.tags)) if (k.startsWith(p.id + ".d." + l.id + ".")) delete project.tags[k]; S.syncDOpts(project, p); };
  S.syncDOpts = (project, p) => { const o = S.dlineOptions(p); for (const l of (p.dialog && p.dialog.lines) || []) for (const k of ["next", "c1go", "c2go", "c3go", "c4go"]) { const r = project.tags[p.id + ".d." + l.id + "." + k]; if (r) { r.options = o; if (!o.some(x => x[0] === r.value)) r.value = "next"; } } };
  S.removeProp = (project, p) => { project.props.splice(project.props.indexOf(p), 1); for (const k of Object.keys(project.tags)) if (k.startsWith(p.id + ".")) delete project.tags[k]; for (const m of project.maps || []) m.things = (m.things || []).filter(t => t.prop !== p.id); };
  // ---- node scripts (see script.js). ports: outs = output names; ins:false = event node (starts a chain)
  const ANYTAG = { k: "tag", label: "Tag", type: "tagRef", refType: "any", def: "" }, NUMTAG = { k: "tag", label: "Number tag", type: "tagRef", refType: "number", def: "" };
  // keys and mouse buttons for the Node Coder dropdowns
  S.KEYS = [["mouse1", "Left mouse button"], ["mouse2", "Right mouse button"], ["mouse3", "Middle mouse button"], ["space", "Space"], ["shift", "Shift"], ["ctrl", "Ctrl"], ["alt", "Alt"], ["enter", "Enter"], ["tab", "Tab"], ["arrowup", "Arrow up"], ["arrowdown", "Arrow down"], ["arrowleft", "Arrow left"], ["arrowright", "Arrow right"]]
    .concat("abcdefghijklmnopqrstuvwxyz".split("").map(c => [c, c.toUpperCase()]), "0123456789".split("").map(c => [c, c]), [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12].map(i => ["f" + i, "F" + i]));
  S.ATTACH.find(p => p.k === "aimKey").options = S.KEYS; S.PLAYER.find(p => p.k === "interactKey").options = S.KEYS; S.PLAYER.find(p => p.k === "reloadKey").options = S.KEYS; for (const k of ["keyForward", "keyBack", "keyLeft", "keyRight", "keyJump", "keySprint", "visorKey"]) S.PLAYER.find(p => p.k === k).options = S.KEYS; S.PROP.find(p => p.k === "codexCat").options = S.CODEXCAT;
  const KEYP = (def, label) => ({ k: "key", label: label || "Key or mouse button", type: "choice", options: S.KEYS, def: def || "r" });
  S.STATES = [["standing", "Standing still"], ["walking", "Walking"], ["running", "Running (sprinting)"], ["jumping", "Jumping (going up)"], ["falling", "Falling"], ["moving", "Moving (walking or running)"], ["inair", "In the air (jumping or falling)"]];
  S.SNODES = {
    onStart: { label: "When the game starts", cat: "event", ins: false, outs: ["next"], ctx: ["screen", "object", "player"], props: [] },
    onClick: { label: "When clicked", cat: "event", ins: false, outs: ["next"], ctx: ["screen", "object"], props: [] },
    onKey: { label: "When a key is pressed", cat: "event", ins: false, outs: ["next"], ctx: ["screen", "object", "player"], props: [KEYP("r")] },
    onKeyUp: { label: "When a key is released", cat: "event", ins: false, outs: ["next"], ctx: ["object", "player"], props: [KEYP("r")] },
    onState: { label: "When the player is… (state starts)", cat: "event", ins: false, outs: ["next"], ctx: ["object", "player"], props: [{ k: "state", label: "State", type: "choice", options: S.STATES, def: "walking" }] },
    onHold: { label: "While a key is held", cat: "event", ins: false, outs: ["next"], ctx: ["object", "player"], props: [KEYP("mouse1", "Key or mouse button held down"), { k: "interval", label: "Run again every (seconds, 0 = every frame)", type: "number", def: 0.1, min: 0, max: 60, step: 0.01 }] },
    onEquip: { label: "When this weapon is taken out", cat: "event", ins: false, outs: ["next"], ctx: ["weapon"], props: [] },
    setTag: { label: "Set a tag", cat: "tags", outs: ["next"], ctx: ["screen", "object"], props: [ANYTAG, { k: "value", label: "To value", type: "string", def: "0" }] },
    changeTag: { label: "Change a number by", cat: "tags", outs: ["next"], ctx: ["screen", "object"], props: [NUMTAG, { k: "amount", label: "Add (negative = subtract)", type: "number", def: 1, min: -100000, max: 100000, step: 1 }] },
    playSound: { label: "Play a sound", cat: "sound", outs: ["next"], ctx: ["screen", "object"], props: [{ k: "sound", label: "Sound", type: "asset", assetType: "sound", def: "" }, { k: "volume", label: "Volume", type: "number", def: 1, min: 0, max: 1, step: 0.05 }] },
    loopSound: { label: "Loop a sound", cat: "sound", outs: ["next"], ctx: ["screen", "object"], props: [{ k: "sound", label: "Sound", type: "asset", assetType: "sound", def: "" }, { k: "volume", label: "Volume", type: "number", def: 1, min: 0, max: 1, step: 0.05 }] },
    stopLoop: { label: "Stop looping a sound", cat: "sound", outs: ["next"], ctx: ["screen", "object"], props: [{ k: "sound", label: "Sound (empty = every looping sound)", type: "asset", assetType: "sound", def: "" }] },
    playAnim: { label: "Play an animation", cat: "look", outs: ["next"], ctx: ["object"], objOnly: true, props: [{ k: "anim", label: "Animation", type: "anim", def: "" }, { k: "loop", label: "Keep repeating it", type: "bool", def: false }, { k: "skipReload", label: "Not while reloading (the reload animation keeps playing)", type: "bool", def: true }] },
    gotoScreen: { label: "Go to a screen", cat: "action", outs: ["next"], ctx: ["screen"], props: [{ k: "screen", label: "Screen", type: "screenRef", def: "" }] },
    say: { label: "Write a message", cat: "action", outs: ["next"], ctx: ["screen", "object"], props: [{ k: "text", label: "Message (shows in the Console)", type: "string", def: "Hello" }] },
    wait: { label: "Wait", cat: "control", outs: ["next"], ctx: ["screen", "object"], props: [{ k: "seconds", label: "Seconds", type: "number", def: 0.5, min: 0, max: 600, step: 0.05 }] },
    if: { label: "If", cat: "control", outs: ["then", "else"], ctx: ["screen", "object"], props: [ANYTAG, { k: "op", label: "Is", type: "choice", options: [["=", "equal to"], ["!=", "not equal to"], [">", "greater than"], ["<", "less than"], [">=", "at least"], ["<=", "at most"]], def: "=" }, { k: "value", label: "This value", type: "string", def: "0" }] },
    repeat: { label: "Repeat", cat: "control", outs: ["body", "next"], ctx: ["screen", "object"], props: [{ k: "times", label: "Times", type: "number", def: 3, min: 1, max: 1000, step: 1 }] },
    ifState: { label: "If the player is…", cat: "sense", outs: ["then", "else"], ctx: ["object", "player"], props: [{ k: "state", label: "State", type: "choice", options: S.STATES, def: "standing" }] },
    ifKey: { label: "If a key is held", cat: "sense", outs: ["then", "else"], ctx: ["object", "player"], props: [KEYP("shift", "Key or mouse button")] },
    onCharge: { label: "Charge weapon (hold to charge)", cat: "weapon", ins: false, outs: ["start", "charged", "fire", "cancel"], ctx: ["weapon"], props: [KEYP("mouse1", "Hold this key or button to charge"),
      { k: "startDelay", label: "Delay before charging starts (seconds)", type: "number", def: 0.5, min: 0, max: 10, step: 0.05 },
      { k: "chargeAnim", label: "Charging animation (plays once)", type: "anim", def: "" }, { k: "loopAnim", label: "Charged animation (loops while held)", type: "anim", def: "" }, { k: "fireAnim", label: "Fire animation (on release)", type: "anim", def: "" },
      { k: "chargeTime", label: "Time to full charge (seconds, 0 = length of the charging animation)", type: "number", def: 0, min: 0, max: 60, step: 0.05 }] },
    chargeShow: { label: "Show charge particle", cat: "weapon", outs: ["next"], ctx: ["weapon"], props: [COLOR("color", "Color", "#33ccff"), { k: "size", label: "Start size", type: "number", def: 1, min: 0.2, max: 80, step: 0.1 }] },
    chargeGrow: { label: "Grow the charge particle", cat: "weapon", outs: ["next"], ctx: ["weapon"], props: [{ k: "size", label: "Grow to size", type: "number", def: 8, min: 0.2, max: 200, step: 0.1 }, { k: "seconds", label: "Over (seconds, 0 = until fully charged)", type: "number", def: 0, min: 0, max: 60, step: 0.05 }] },
    chargeSuck: { label: "Particles fly into the charge", cat: "weapon", outs: ["next"], ctx: ["weapon"], props: [{ k: "rate", label: "Particles per second", type: "number", def: 40, min: 1, max: 300, step: 1 }, { k: "radius", label: "Start distance (units around the charge; it sits close to the camera, so small values look big)", type: "number", def: 10, min: 1, max: 500, step: 0.5 },
      { k: "size", label: "Particle size", type: "number", def: 0.35, min: 0.05, max: 20, step: 0.05 }, { k: "time", label: "Time to reach the charge (seconds)", type: "number", def: 0.4, min: 0.05, max: 5, step: 0.05 }, { k: "same", label: "Same color as the charge", type: "bool", def: true }, COLOR("color", "Own color (when not the same)", "#ffffff")] },
    fireProjectile: { label: "Fire the projectile", cat: "weapon", outs: ["next"], ctx: ["weapon"], props: [] },
    reload: { label: "Reload the weapon", cat: "weapon", outs: ["next"], ctx: ["weapon"], props: [] },
    ifAmmo: { label: "If the weapon has ammo", cat: "weapon", outs: ["then", "else"], ctx: ["weapon"], props: [] },
    forever: { label: "Repeat forever", cat: "control", outs: ["body"], ctx: ["screen", "object"], props: [] },
    // ---- node update 2 (Scratch-style categories). "player" = the Player's own script (Hierarchy > Player)
    onJump: { label: "When the player jumps", cat: "event", ins: false, outs: ["next"], ctx: ["object", "player"], props: [] },
    onLand: { label: "When the player lands", cat: "event", ins: false, outs: ["next"], ctx: ["object", "player"], props: [] },
    onHurt: { label: "When the player gets hurt", cat: "event", ins: false, outs: ["next"], ctx: ["object", "player"], props: [] },
    onDeath: { label: "When the player dies", cat: "event", ins: false, outs: ["next"], ctx: ["object", "player"], props: [] },
    onKill: { label: "When an enemy is killed", cat: "event", ins: false, outs: ["next"], ctx: ["object", "player"], props: [] },
    onFire: { label: "When this weapon fires", cat: "event", ins: false, outs: ["next"], ctx: ["weapon"], props: [] },
    onImpact: { label: "When a shot hits something", cat: "event", ins: false, outs: ["next"], ctx: ["weapon", "player"], props: [{ k: "what", label: "Hits", type: "choice", options: [["any", "anything"], ["level", "the level (walls, floors, ceilings, doors)"], ["enemy", "an enemy"], ["end", "nothing (flew out of range)"]], def: "any" },
      { k: "charged", label: "Which shots", type: "choice", options: [["any", "charged or not"], ["charged", "only charged shots"], ["normal", "only normal shots"]], def: "any" }] },
    onReload: { label: "When reloading starts", cat: "event", ins: false, outs: ["next"], ctx: ["weapon"], props: [] },
    onReloadDone: { label: "When reloading is done", cat: "event", ins: false, outs: ["next"], ctx: ["weapon"], props: [] },
    onButton: { label: "When a button is pressed", cat: "event", ins: false, outs: ["next"], ctx: ["object", "player"], props: [{ k: "name", label: "Button name (empty = any button)", type: "string", def: "" }] },
    onRespawn: { label: "When the player respawns (Lives system)", cat: "event", ins: false, outs: ["next"], ctx: ["object", "player"], props: [] },
    onMessage: { label: "When I receive a message", cat: "event", ins: false, outs: ["next"], ctx: ["screen", "object", "player"], props: [{ k: "name", label: "Message name", type: "string", def: "go" }] },
    onTag: { label: "When a tag becomes…", cat: "event", ins: false, outs: ["next"], ctx: ["screen", "object", "player"], props: [ANYTAG, { k: "op", label: "Is", type: "choice", options: [["=", "equal to"], ["!=", "not equal to"], [">", "greater than"], ["<", "less than"], [">=", "at least"], ["<=", "at most"]], def: ">=" }, { k: "value", label: "This value", type: "string", def: "10" }] },
    sendMessage: { label: "Send a message (to every script)", cat: "action", outs: ["next"], ctx: ["screen", "object", "player"], props: [{ k: "name", label: "Message name", type: "string", def: "go" }] },
    waitUntil: { label: "Wait until…", cat: "control", outs: ["next"], ctx: ["screen", "object", "player"], props: [ANYTAG, { k: "op", label: "Is", type: "choice", options: [["=", "equal to"], ["!=", "not equal to"], [">", "greater than"], ["<", "less than"], [">=", "at least"], ["<=", "at most"]], def: "=" }, { k: "value", label: "This value", type: "string", def: "yes" }] },
    repeatUntil: { label: "Repeat until…", cat: "control", outs: ["body", "next"], ctx: ["screen", "object", "player"], props: [ANYTAG, { k: "op", label: "Is", type: "choice", options: [["=", "equal to"], ["!=", "not equal to"], [">", "greater than"], ["<", "less than"], [">=", "at least"], ["<=", "at most"]], def: "=" }, { k: "value", label: "This value", type: "string", def: "yes" }] },
    chance: { label: "With a chance of…", cat: "control", outs: ["then", "else"], ctx: ["screen", "object", "player"], props: [{ k: "percent", label: "Chance (%)", type: "number", def: 50, min: 0, max: 100, step: 1 }] },
    stop: { label: "Stop this chain", cat: "control", outs: [], ctx: ["screen", "object", "player"], props: [] },
    stopAll: { label: "Stop all of this object's scripts", cat: "control", outs: [], ctx: ["screen", "object", "player"], props: [] },
    ifReloading: { label: "If reloading", cat: "sense", outs: ["then", "else"], ctx: ["object", "player"], props: [] },
    ifNear: { label: "If an enemy is near", cat: "sense", outs: ["then", "else"], ctx: ["object", "player"], props: [{ k: "dist", label: "Within (units, 128 = a cell)", type: "number", def: 256, min: 8, max: 10000, step: 8 }] },
    randomTag: { label: "Set a number to a random value", cat: "tags", outs: ["next"], ctx: ["screen", "object", "player"], props: [NUMTAG, { k: "min", label: "From", type: "number", def: 1, min: -100000, max: 100000, step: 1 }, { k: "max", label: "To", type: "number", def: 10, min: -100000, max: 100000, step: 1 }, { k: "whole", label: "Whole numbers only", type: "bool", def: true }] },
    mathTag: { label: "Do math on a number", cat: "tags", outs: ["next"], ctx: ["screen", "object", "player"], props: [NUMTAG, { k: "op", label: "Operation", type: "choice", options: [["*", "multiply by"], ["/", "divide by"], ["min", "at most (cap at)"], ["max", "at least (raise to)"], ["mod", "remainder after dividing by"], ["round", "round (value = steps, 1 = whole numbers)"]], def: "*" }, { k: "value", label: "Value", type: "number", def: 2, min: -100000, max: 100000, step: 0.01 }] },
    toggleTag: { label: "Flip a yes / no tag", cat: "tags", outs: ["next"], ctx: ["screen", "object", "player"], props: [{ k: "tag", label: "Yes / no tag", type: "tagRef", refType: "bool", def: "" }] },
    stopAnim: { label: "Stop the animation (back to idle)", cat: "look", outs: ["next"], ctx: ["object"], objOnly: true, props: [] },
    showHide: { label: "Show or hide this object", cat: "look", outs: ["next"], ctx: ["object"], objOnly: true, props: [{ k: "show", label: "Make it", type: "choice", options: [["show", "visible"], ["hide", "hidden"], ["toggle", "the opposite"]], def: "hide" }] },
    shake: { label: "Shake the camera", cat: "look", outs: ["next"], ctx: ["object", "player"], props: [{ k: "strength", label: "Strength", type: "number", def: 1, min: 0, max: 10, step: 0.1 }, { k: "seconds", label: "For (seconds)", type: "number", def: 0.3, min: 0.02, max: 10, step: 0.02 }] },
    flashScreen: { label: "Flash the screen", cat: "look", outs: ["next"], ctx: ["object", "player"], props: [COLOR("color", "Color", "#ffffff"), { k: "strength", label: "Strength (0 to 1)", type: "number", def: 0.5, min: 0, max: 1, step: 0.05 }] },
    jump: { label: "Jump", cat: "player", outs: ["next"], ctx: ["object", "player"], props: [{ k: "height", label: "Height (units, 0 = the player's jump height)", type: "number", def: 0, min: 0, max: 5000, step: 1 }, { k: "air", label: "Also in the air", type: "bool", def: false }] },
    airJump: { label: "Double jump (jump again in the air)", cat: "player", outs: ["next"], ctx: ["object", "player"], props: [{ k: "max", label: "Jumps allowed in the air (1 = double jump, 2 = triple…)", type: "number", def: 1, min: 1, max: 20, step: 1 }, { k: "height", label: "Height (units, 0 = the player's jump height)", type: "number", def: 0, min: 0, max: 5000, step: 1 }] },
    launch: { label: "Push the player (dash, knock-back, launch)", cat: "player", outs: ["next"], ctx: ["object", "player"], props: [{ k: "forward", label: "Forward (where you look; negative = back)", type: "number", def: 600, min: -20000, max: 20000, step: 10 }, { k: "side", label: "Sideways (right; negative = left)", type: "number", def: 0, min: -20000, max: 20000, step: 10 }, { k: "up", label: "Up", type: "number", def: 0, min: -20000, max: 20000, step: 10 }] },
    hurtPlayer: { label: "Hurt or heal the player", cat: "player", outs: ["next"], ctx: ["object", "player"], props: [{ k: "amount", label: "Damage (negative = heal)", type: "number", def: 10, min: -100000, max: 100000, step: 1 }] },
    teleport: { label: "Move the player to the start", cat: "player", outs: ["next"], ctx: ["object", "player"], props: [] },
    switchWeapon: { label: "Take out a weapon", cat: "weapon", outs: ["next"], ctx: ["object", "player"], props: [{ k: "which", label: "Which", type: "choice", options: [["next", "the next one"], ["prev", "the previous one"], ["num", "number…"]], def: "next" }, { k: "num", label: "Weapon number (when “number…”)", type: "number", def: 1, min: 1, max: 99, step: 1 }] },
  };
  // ---- Compositor: post-processing effects applied to the game picture, in chain order (project.compositor.effects = [{ id, type }], values = tags comp.<id>.<k>; comp.<id>.on can be switched by nodes)
  const CN = (k, label, def, min, max, step) => ({ k, label, type: "number", def, min, max, step });
  S.COMPFX = {
    grade: { label: "Color grading", icon: "◐", desc: "Brightness, contrast, saturation, gamma, a colour tint and warm / cold.", props: [CN("brightness", "Brightness", 1, 0, 3, 0.01), CN("contrast", "Contrast", 1, 0, 3, 0.01), CN("saturation", "Saturation (0 = black and white)", 1, 0, 3, 0.01), CN("gamma", "Gamma (midtones)", 1, 0.2, 3, 0.01), CN("warmth", "Warm (+) / cold (-)", 0, -1, 1, 0.01), COLOR("tint", "Tint", "#ffffff")] },
    vignette: { label: "Vignette", icon: "◎", desc: "Darker (or coloured) edges.", props: [CN("strength", "Strength", 0.5, 0, 1, 0.01), CN("size", "Size (where it starts)", 0.75, 0, 1.5, 0.01), CN("soft", "Softness", 0.45, 0.01, 1.5, 0.01), COLOR("color", "Color", "#000000")] },
    grain: { label: "Film grain", icon: "░", desc: "Moving noise, like film or a cheap camera.", props: [CN("amount", "Amount", 0.08, 0, 1, 0.01), CN("size", "Grain size (pixels)", 1.5, 1, 8, 0.1), { k: "color", label: "Coloured grain", type: "bool", def: false }] },
    chroma: { label: "Chromatic aberration", icon: "◈", desc: "Red and blue split apart toward the edges, like a lens.", props: [CN("amount", "Amount (pixels)", 2, 0, 30, 0.1), { k: "edges", label: "Only toward the edges", type: "bool", def: true }] },
    pixelate: { label: "Pixelate", icon: "▦", desc: "Big chunky pixels (a lower resolution look).", props: [CN("size", "Pixel size", 3, 1, 32, 1)] },
    posterize: { label: "Fewer colors (retro palette)", icon: "▤", desc: "Limits the colours per channel, optionally with ordered dithering like old PCs and consoles.", props: [CN("levels", "Colours per channel", 16, 2, 64, 1), { k: "dither", label: "Dithering (pattern instead of banding)", type: "bool", def: true }, CN("ditherSize", "Dither pattern size (pixels)", 1, 1, 8, 1)] },
    crt: { label: "CRT screen", icon: "▭", desc: "Scanlines, a curved glass screen and a red / green / blue pixel mask. In front of the HUD, the lines and mask cover the HUD too (the curve bends only the 3D picture).", props: [CN("lines", "Scanline strength", 0.35, 0, 1, 0.01), CN("count", "Number of scanlines", 360, 60, 1440, 10), CN("curve", "Screen curve", 0.08, 0, 0.5, 0.005), CN("mask", "Pixel mask strength", 0.2, 0, 1, 0.01)] },
    sharpen: { label: "Sharpen", icon: "◇", desc: "Crisper edges and texture pixels.", props: [CN("amount", "Amount", 0.4, 0, 3, 0.01)] },
    night: { label: "Night vision", icon: "◉", desc: "Bright green picture with noise. Switch it on and off with a node (tag comp.<effect>.on).", props: [CN("strength", "Strength", 1, 0, 1, 0.01), CN("gain", "Brightness boost", 2.5, 0.5, 10, 0.1), CN("noise", "Noise", 0.12, 0, 1, 0.01), COLOR("color", "Color", "#5cff7a")] },
    lens: { label: "Lens distortion", icon: "◯", desc: "Fish-eye (positive) or pincushion (negative) bending. The HUD itself is never bent.", props: [CN("amount", "Amount", 0.15, -0.5, 1, 0.01)] },
    letterbox: { label: "Letterbox (cinema bars)", icon: "▬", desc: "Black bars at the top and bottom.", props: [CN("size", "Bar height (part of the screen)", 0.12, 0, 0.45, 0.005), COLOR("color", "Color", "#000000")] },
  };
  const compRec = (p, id) => { const r = rec(p, "compositor", "comp." + id); return r; };
  S.COMPLAYER = [["behind", "Behind the HUD (only the 3D picture)"], ["front", "In front of the HUD (covers the HUD too)"]];
  S.compKeys = (type) => [{ k: "on", label: "Effect on", type: "bool", def: true }, { k: "layer", label: "Layer", type: "choice", options: S.COMPLAYER, def: "behind" }, { k: "visor", label: "Used", type: "choice", options: [["", "Always"]], def: "" }].concat(S.COMPFX[type].props);
  S.addComp = (project, type) => { const C = project.compositor = project.compositor || { effects: [] }; const id = S.uid("fx", C.effects.map(e => e.id)); C.effects.push({ id, type }); for (const p of S.compKeys(type)) project.tags["comp." + id + "." + p.k] = compRec(p, id); return C.effects[C.effects.length - 1]; };
  S.removeComp = (project, id) => { const C = project.compositor; if (!C) return; C.effects = C.effects.filter(e => e.id !== id); for (const k of Object.keys(project.tags)) if (k.startsWith("comp." + id + ".")) delete project.tags[k]; };
  S.ensureComp = project => { const C = project.compositor = project.compositor || { effects: [] }; C.effects = C.effects.filter(e => S.COMPFX[e.type]); for (const e of C.effects) for (const p of S.compKeys(e.type)) { const k = "comp." + e.id + "." + p.k; if (!project.tags[k]) project.tags[k] = compRec(p, e.id); else project.tags[k].label = p.label; } };
  // the active effects with their values, in order (used by the renderer)
  S.compActive = (project, T, visor) => { const vz = visor !== undefined ? visor : (T.get("player.visor") || ""); return ((project && project.compositor && project.compositor.effects) || []).filter(e => S.COMPFX[e.type] && T.get("comp." + e.id + ".on") !== false && (!T.get("comp." + e.id + ".visor") || T.get("comp." + e.id + ".visor") === vz)).map(e => { const v = {}; for (const p of S.COMPFX[e.type].props) v[p.k] = { type: p.type, val: T.get("comp." + e.id + "." + p.k) }; return { id: e.id, type: e.type, layer: T.get("comp." + e.id + ".layer") === "front" ? "front" : "behind", v }; }); };
  // ---- pause, zones, dialogues
  Object.assign(S.SNODES, {
    onAim: { label: "When aiming starts (aim button)", cat: "event", ins: false, outs: ["next"], ctx: ["weapon", "player"], props: [] },
    onAimEnd: { label: "When aiming stops", cat: "event", ins: false, outs: ["next"], ctx: ["weapon", "player"], props: [] },
    onPause: { label: "When the game is paused", cat: "event", ins: false, outs: ["next"], ctx: ["object", "player"], props: [] },
    onResume: { label: "When the game is resumed", cat: "event", ins: false, outs: ["next"], ctx: ["object", "player"], props: [] },
    pauseGame: { label: "Open the pause menu", cat: "action", outs: ["next"], ctx: ["object", "player"], props: [{ k: "screen", label: "Pause screen (empty = the normal one)", type: "screenRef", def: "" }] },
    startDialogue: { label: "Start a dialogue", cat: "action", outs: ["next"], ctx: ["object", "player"], props: [{ k: "dialog", label: "Dialogue", type: "dialogRef", def: "" }, { k: "wait", label: "Wait until the talk is over", type: "bool", def: true }] },
    // custom zones (map editor > Zones): their script hears these
    onEnter: { label: "When the player enters the zone", cat: "event", ins: false, outs: ["next"], ctx: ["zone"], props: [] },
    onLeave: { label: "When the player leaves the zone", cat: "event", ins: false, outs: ["next"], ctx: ["zone"], props: [] },
    onInside: { label: "While the player is inside (every few moments)", cat: "event", ins: false, outs: ["next"], ctx: ["zone"], props: [{ k: "interval", label: "Every (seconds)", type: "number", def: 1, min: 0.05, max: 60, step: 0.05 }] },
    // dialogue graphs (Dialogue Editor): start -> lines and choices -> end
    dStart: { label: "Dialogue starts", cat: "event", ins: false, outs: ["next"], ctx: ["dialogue"], props: [] },
    dLine: { label: "Say a line", cat: "dialogue", outs: ["next"], ctx: ["dialogue"], props: [{ k: "speaker", label: "Speaker (empty = the NPC's name)", type: "string", def: "" }, { k: "text", label: "Text ({tag} shows a tag's value)", type: "string", multi: true, def: "Hello there." }, { k: "sound", label: "Voice / sound", type: "asset", assetType: "sound", def: "" }, { k: "auto", label: "Go on by itself after (s, 0 = wait for the player)", type: "number", def: 0, min: 0, max: 60, step: 0.1 }] },
    dChoice: { label: "Ask a question (choices)", cat: "dialogue", outs: ["c1", "c2", "c3", "c4"], outText: true, ctx: ["dialogue"], props: [{ k: "speaker", label: "Speaker (empty = the NPC's name)", type: "string", def: "" }, { k: "text", label: "Question (empty = keep the last line)", type: "string", multi: true, def: "" }, { k: "c1", label: "Choice 1", type: "string", def: "Yes" }, { k: "c2", label: "Choice 2", type: "string", def: "No" }, { k: "c3", label: "Choice 3 (empty = not shown)", type: "string", def: "" }, { k: "c4", label: "Choice 4 (empty = not shown)", type: "string", def: "" }] },
    dEnd: { label: "End the dialogue", cat: "dialogue", outs: [], ctx: ["dialogue"], props: [] },
  });
  for (const k of ["setTag", "changeTag", "toggleTag", "randomTag", "mathTag", "if", "chance", "playSound", "wait", "sendMessage", "say", "waitUntil"]) if (S.SNODES[k] && !S.SNODES[k].ctx.includes("dialogue")) S.SNODES[k].ctx.push("dialogue");
  // ---- keys + bosses (script.js hands these to the game through o.custom)
  const KEYREF = { k: "key", label: "Key", type: "propRef", refType: "key", def: "" }, KC = ["object", "player", "zone", "boss", "dialogue"], BC = ["boss"];
  const PHNAME = { k: "phase", label: "Phase name (empty = any phase)", type: "string", def: "" };
  Object.assign(S.SNODES, {
    saveGame: { label: "Save the game (here, current slot)", cat: "action", outs: ["next"], ctx: ["object", "player", "zone", "boss", "dialogue"], props: [] },
    onSave: { label: "When the game is saved", cat: "event", ins: false, outs: ["next"], ctx: ["object", "player", "zone"], props: [] },
    onLoad: { label: "When a save is loaded", cat: "event", ins: false, outs: ["next"], ctx: ["object", "player", "zone"], props: [] },
    giveKey: { label: "Give the player a key", cat: "tags", outs: ["next"], ctx: KC, props: [KEYREF] },
    takeKey: { label: "Take a key away", cat: "tags", outs: ["next"], ctx: KC, props: [KEYREF] },
    ifHasKey: { label: "If the player has a key", cat: "sense", outs: ["then", "else"], ctx: KC, props: [KEYREF] },
    setVisor: { label: "Switch visor", cat: "player", outs: ["next"], ctx: KC, props: [{ k: "visor", label: "Visor (its name or id; empty = combat)", type: "string", def: "Scan" }] },
    ifVisor: { label: "If this visor is on", cat: "sense", outs: ["then", "else"], ctx: KC, props: [{ k: "visor", label: "Visor (name, id or kind: scan, xray, night…)", type: "string", def: "scan" }] },
    onVisor: { label: "When the visor changes", cat: "event", ins: false, outs: ["next"], ctx: ["object", "player", "zone", "boss"], props: [{ k: "visor", label: "To this visor (empty = any)", type: "string", def: "" }] },
    onScan: { label: "When something is scanned", cat: "event", ins: false, outs: ["next"], ctx: ["object", "player", "zone", "boss"], props: [{ k: "what", label: "Object (empty = anything)", type: "propRef", refType: "", def: "" }] },
    onKeyGot: { label: "When a key is picked up", cat: "event", ins: false, outs: ["next"], ctx: ["object", "player", "zone", "boss"], props: [Object.assign({}, KEYREF, { label: "Key (empty = any key)" })] },
    // boss script (Object Editor > a Boss > Open its script)
    onBossStart: { label: "When the fight starts", cat: "event", ins: false, outs: ["next"], ctx: BC, props: [] },
    onPhase: { label: "When a phase starts", cat: "event", ins: false, outs: ["next"], ctx: BC, props: [PHNAME] },
    onBossHurt: { label: "When the boss is hit", cat: "event", ins: false, outs: ["next"], ctx: BC, props: [] },
    onBossHealth: { label: "When its health drops to…", cat: "event", ins: false, outs: ["next"], ctx: BC, props: [{ k: "percent", label: "Health (%)", type: "number", def: 50, min: 0, max: 100, step: 1 }] },
    onBossAttack: { label: "When the boss attacks", cat: "event", ins: false, outs: ["next"], ctx: BC, props: [] },
    onBossDeath: { label: "When the boss is defeated", cat: "event", ins: false, outs: ["next"], ctx: BC, props: [] },
    setPhase: { label: "Go to a phase", cat: "boss", outs: ["next"], ctx: BC, props: [{ k: "phase", label: "Phase name (empty = the next one)", type: "string", def: "" }] },
    bossFire: { label: "Fire a pattern", cat: "boss", outs: ["next"], ctx: BC, props: [{ k: "pattern", label: "Pattern", type: "choice", options: [["aimed", "One shot at you"], ["spread", "A fan of shots at you"], ["ring", "A ring all around"], ["spiral", "A spinning ring"], ["rain", "Rain from above, around you"]], def: "ring" },
      { k: "count", label: "Shots", type: "number", def: 12, min: 1, max: 128, step: 1 }, { k: "spread", label: "Fan width (degrees) / rain radius (units)", type: "number", def: 60, min: 1, max: 3000, step: 1 },
      { k: "volleys", label: "Volleys", type: "number", def: 1, min: 1, max: 100, step: 1 }, { k: "gap", label: "Time between volleys (s)", type: "number", def: 0.2, min: 0.02, max: 10, step: 0.01 }, { k: "turn", label: "Spiral turn per volley (degrees)", type: "number", def: 12, min: -180, max: 180, step: 1 },
      { k: "speed", label: "Shot speed (0 = the boss's own)", type: "number", def: 0, min: 0, max: 5000, step: 10 }, { k: "size", label: "Shot size (0 = the boss's own)", type: "number", def: 0, min: 0, max: 200, step: 0.5 },
      { k: "damage", label: "Damage (0 = the boss's own)", type: "number", def: 0, min: 0, max: 100000, step: 1 }, COLOR("color", "Colour", "#ff4a8a"), { k: "own", label: "Use the boss's own colour", type: "bool", def: true }, { k: "wait", label: "Wait until all volleys are fired", type: "bool", def: true }] },
    bossMove: { label: "Change how it moves (for a while)", cat: "boss", outs: ["next"], ctx: BC, props: [{ k: "move", label: "Moves", type: "choice", options: [["chase", "Walks toward you"], ["stationary", "Stays where it is"], ["keep", "Keeps its distance"], ["circle", "Circles around you"], ["dash", "Charges at you now and then"], ["flee", "Runs away from you"]], def: "circle" },
      { k: "seconds", label: "For (seconds, 0 = until changed again)", type: "number", def: 4, min: 0, max: 600, step: 0.1 }, { k: "speed", label: "Speed (0 = its own)", type: "number", def: 0, min: 0, max: 5000, step: 10 }] },
    bossDash: { label: "Charge at the player", cat: "boss", outs: ["next"], ctx: BC, props: [{ k: "speed", label: "Speed (units / s)", type: "number", def: 1000, min: 50, max: 6000, step: 10 }, { k: "seconds", label: "For (seconds)", type: "number", def: 0.6, min: 0.05, max: 10, step: 0.05 }, { k: "damage", label: "Damage if it hits you", type: "number", def: 20, min: 0, max: 100000, step: 1 }, { k: "wait", label: "Wait until the charge is over", type: "bool", def: true }] },
    bossTeleport: { label: "Teleport the boss", cat: "boss", outs: ["next"], ctx: BC, props: [{ k: "where", label: "To", type: "choice", options: [["start", "Where it was placed"], ["random", "A random spot near where it was placed"], ["behind", "Behind the player"], ["far", "The far side of the player"]], def: "random" }, { k: "radius", label: "Distance (units)", type: "number", def: 400, min: 0, max: 5000, step: 10 }, { k: "fx", label: "Flash + particles", type: "bool", def: true }, COLOR("color", "Flash colour", "#c07dff")] },
    bossSpawn: { label: "Call in helpers", cat: "boss", outs: ["next"], ctx: BC, props: [{ k: "what", label: "Enemy", type: "propRef", refType: "enemy", def: "" }, { k: "count", label: "How many", type: "number", def: 2, min: 1, max: 50, step: 1 }, { k: "radius", label: "Within (units of the boss)", type: "number", def: 300, min: 0, max: 3000, step: 10 }] },
    bossHeal: { label: "Heal the boss", cat: "boss", outs: ["next"], ctx: BC, props: [{ k: "percent", label: "By (% of full health, negative = hurt it)", type: "number", def: 10, min: -100, max: 100, step: 1 }] },
    bossShield: { label: "Shield (can't be hurt)", cat: "boss", outs: ["next"], ctx: BC, props: [{ k: "on", label: "Shield", type: "choice", options: [["on", "on"], ["off", "off"]], def: "on" }, { k: "seconds", label: "For (seconds, 0 = until switched off)", type: "number", def: 3, min: 0, max: 600, step: 0.1 }, COLOR("color", "Shield glow", "#7df3ff")] },
    bossHold: { label: "Stop attacking for a while", cat: "boss", outs: ["next"], ctx: BC, props: [{ k: "seconds", label: "Seconds", type: "number", def: 2, min: 0, max: 600, step: 0.1 }] },
    bossAnim: { label: "Play a boss animation", cat: "boss", outs: ["next"], ctx: BC, props: [{ k: "anim", label: "Animation", type: "anim", def: "" }, { k: "wait", label: "Wait until it is over", type: "bool", def: true }] },
    ifPhase: { label: "If the boss is in a phase", cat: "sense", outs: ["then", "else"], ctx: BC, props: [{ k: "phase", label: "Phase name", type: "string", def: "Phase 2" }] },
    ifBossHealth: { label: "If the boss's health is…", cat: "sense", outs: ["then", "else"], ctx: BC, props: [{ k: "op", label: "Is", type: "choice", options: OPS, def: "<=" }, { k: "percent", label: "Percent", type: "number", def: 50, min: 0, max: 100, step: 1 }] },
    ifPlayerDist: { label: "If the player is…", cat: "sense", outs: ["then", "else"], ctx: BC, props: [{ k: "op", label: "Closer / farther", type: "choice", options: [["<", "closer than"], [">", "farther than"]], def: "<" }, { k: "dist", label: "Units (128 = a cell)", type: "number", def: 300, min: 0, max: 20000, step: 8 }] },
  });
  S.SCAT = { event: "Events", dialogue: "Dialogue", control: "Control", sense: "Sensing", tags: "Tags & math", look: "Animation & looks", sound: "Sound", player: "Player", weapon: "Weapon", boss: "Boss", action: "Screens & messages" };
  // Dialogues: project.dialogues = [{ id, name, nodes, links }] (a script; owner key = the id, node values are tags <id>.s.<node>.*)
  S.addDialogue = (project, name) => {
    const L = project.dialogues = project.dialogues || [], taken = L.map(d => d.id).concat(project.screens.map(s => s.id), (project.maps || []).map(m => m.id), (project.props || []).map(p => p.id));
    const d = { id: S.uid("dialog1", taken).replace(/^dialog1(\d+)$/, "dialog$1"), name: name || "Dialogue " + (L.length + 1), nodes: [], links: [] }; L.push(d);
    const A = (type, x, y, over) => S.addSNode(project, d.id, d, type, x, y, over), lk = (a, o, b) => d.links.push({ a: a.id, o, b: b.id });
    const st = A("dStart", 20, 40), l1 = A("dLine", 280, 40, { text: "Hello there, traveller." }), q = A("dChoice", 540, 40, { text: "", c1: "Who are you?", c2: "Goodbye." }), l2 = A("dLine", 820, 10, { text: "Just someone keeping watch." }), e = A("dEnd", 1080, 120);
    lk(st, "next", l1); lk(l1, "next", q); lk(q, "c1", l2); lk(l2, "next", e); lk(q, "c2", e); return d;
  };
  S.removeDialogue = (project, d) => { project.dialogues = (project.dialogues || []).filter(x => x !== d); for (const k of Object.keys(project.tags)) { if (k.startsWith(d.id + ".s.")) delete project.tags[k]; else if (project.tags[k].type === "dialogRef" && project.tags[k].value === d.id) project.tags[k].value = ""; } };
  // Zones: map.zones = [{ id, type: "trigger" | "custom", x0, y0, x1, y1, layer, script? }] (cells, inclusive). Values are tags <map>.z.<zone>.*
  S.ZONE = [
    { k: "name", label: "Name", type: "string", def: "Zone" },
    { k: "height", label: "Height (stories, 0 = only its own story)", type: "number", def: 0, min: 0, max: 16, step: 1 },
    { k: "once", label: "Only the first time", type: "bool", def: false },
    { k: "needTag", label: "Only if this tag…", type: "tagRef", refType: "any", def: "" }, { k: "needOp", label: "is", type: "choice", options: [["=", "equal to"], ["!=", "not equal to"], [">", "greater than"], ["<", "less than"], [">=", "at least"], ["<=", "at most"]], def: "=" }, { k: "needVal", label: "this value", type: "string", def: "yes" },
    // trigger zones: ready-made actions when you walk in (and out)
    { k: "setTag", label: "On enter: set this tag", type: "tagRef", refType: "any", def: "", trig: true }, { k: "setVal", label: "to (value, +n / -n adds, toggle)", type: "string", def: "true", trig: true },
    { k: "message", label: "On enter: send a message (to every script)", type: "string", def: "", trig: true },
    { k: "sound", label: "On enter: play a sound", type: "asset", assetType: "sound", def: "", trig: true },
    { k: "say", label: "On enter: show a text (top of the screen)", type: "string", def: "", trig: true },
    { k: "hurt", label: "Damage per second inside (negative heals)", type: "number", def: 0, min: -1000, max: 1000, step: 1, trig: true },
    { k: "dialog", label: "On enter: start a dialogue", type: "dialogRef", def: "", trig: true },
    { k: "screen", label: "On enter: go to a screen", type: "screenRef", def: "", trig: true },
    { k: "teleport", label: "On enter: teleport to (cell x,y,story e.g. 10,4,0; empty = no)", type: "string", def: "", trig: true },
    { k: "leaveTag", label: "On leave: set this tag", type: "tagRef", refType: "any", def: "", trig: true }, { k: "leaveVal", label: "to", type: "string", def: "false", trig: true },
    { k: "leaveMessage", label: "On leave: send a message", type: "string", def: "", trig: true },
  ];
  S.zoneKey = (mapId, zid, k) => mapId + ".z." + zid + "." + k;
  S.addZone = (project, map, z, over) => { map.zones = map.zones || []; const id = S.uid("zone1", map.zones.map(q => q.id)).replace(/^zone1(\d+)$/, "zone$1"); const zone = Object.assign({ id, type: "trigger", layer: 0 }, z); if (zone.type === "custom") zone.script = zone.script || { nodes: [], links: [] }; map.zones.push(zone);
    for (const p of S.ZONE) { const r = rec(p, map.id, map.id + ".z." + id); if (p.k === "name") r.value = (zone.type === "custom" ? "Custom zone " : "Trigger zone ") + id.replace("zone", ""); if (over && p.k in over) r.value = over[p.k]; project.tags[S.zoneKey(map.id, id, p.k)] = r; } return zone; };
  S.removeZone = (project, map, zone) => { map.zones = (map.zones || []).filter(q => q !== zone); const pre = map.id + ".z." + zone.id + "."; for (const k of Object.keys(project.tags)) if (k.startsWith(pre)) delete project.tags[k]; };
  // per placed door: its own requirements (Map editor), tags <map>.th.<thing>.<key>; they only exist while the door uses its own settings, Play3D ov() reads them first
  S.DOOR_OWN = ["openMode", "shotFirst", "shotWeapon", "shotAfter", "shotMsg", "lockKey", "lockUse", "lockNeedTag", "lockNeedOp", "lockNeedVal", "lockMsg", "lockSound", "shieldOn", "shieldColor"];
  S.thingPre = (mapId, thId) => mapId + ".th." + thId + ".";
  S.doorOwnDef = k => { const p = S.PROP.find(q => q.k === k); if (!p) return null; const d = Object.assign({}, p); delete d.showIf; delete d.showIf2; if (p.showIf2) d.showIf = p.showIf2; if (k === "shotWeapon") d.also = [["openMode", "shot"]]; return d; };
  S.doorOwnHas = (project, mapId, th) => !!project.tags[S.thingPre(mapId, th.id) + "openMode"];
  S.doorOwnOn = (project, mapId, th) => { const pre = S.thingPre(mapId, th.id); for (const k of S.DOOR_OWN) { const d = S.doorOwnDef(k); if (!d) continue; const r = rec(d, mapId, pre.slice(0, -1)), src = project.tags[th.prop + "." + k]; if (src) r.value = src.value; project.tags[pre + k] = r; } };
  S.doorOwnOff = (project, mapId, th) => { const pre = S.thingPre(mapId, th.id); for (const k of Object.keys(project.tags)) if (k.startsWith(pre)) delete project.tags[k]; };
  S.ensureThingTags = project => { for (const m of project.maps || []) { const ids = new Set((m.things || []).map(t => t.id)), pre = m.id + ".th."; for (const k of Object.keys(project.tags)) { if (!k.startsWith(pre)) continue; const rest = k.slice(pre.length), id = rest.slice(0, rest.indexOf(".")), key = rest.slice(rest.indexOf(".") + 1); if (!ids.has(id)) { delete project.tags[k]; continue; } const d = S.doorOwnDef(key); if (d) { const r = project.tags[k]; r.label = d.label; if (d.options) r.options = d.options; } } } };
  S.ensureZones = project => { for (const m of project.maps || []) for (const z of m.zones || []) { for (const p of S.ZONE) { const k = S.zoneKey(m.id, z.id, p.k); if (!project.tags[k]) project.tags[k] = rec(p, m.id, m.id + ".z." + z.id); } if (z.type === "custom") { z.script = z.script || { nodes: [], links: [] }; for (const n of z.script.nodes) S.ensureSNode(project, m.id + ".z." + z.id, n); } } };
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
    for (const p of project.props) for (const b of p.hitboxes || []) for (const pr of S.HITBOX) { const k = p.id + ".hb." + b.id + "." + pr.k; if (!project.tags[k]) project.tags[k] = hbRec(p, pr); }
    for (const p of project.props) if (p.dialog) { for (const l of p.dialog.lines) for (const pr of S.DLINE) if (!project.tags[p.id + ".d." + l.id + "." + pr.k]) project.tags[p.id + ".d." + l.id + "." + pr.k] = rec(pr, "dialog", p.id); S.syncDOpts(project, p); }
    for (const p of project.props) for (const d of p.drops || []) for (const pr of S.DROP) { const k = p.id + ".dr." + d.id + "." + pr.k; if (!project.tags[k]) project.tags[k] = rec(pr, "drops", p.id); }
    for (const p of project.props) for (const b of p.phases || []) for (const pr of S.PHASE) { const k = p.id + ".ph." + b.id + "." + pr.k; if (!project.tags[k]) project.tags[k] = phRec(p, pr); }
    for (const p of project.props) { for (const k of OLDMAT) delete project.tags[p.id + "." + k]; for (const pr of S.PROP) { const r = project.tags[p.id + "." + pr.k]; if (!r) { const n = rec(pr, "props", p.id); if (pr.type === "anim") n.modelTag = p.id + ".asset"; project.tags[p.id + "." + pr.k] = n; } else { r.label = pr.label; if (pr.options) r.options = pr.options; if (pr.max !== undefined) { r.max = pr.max; if (+r.value > pr.max) r.value = pr.max; } } } }
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
    for (const [n, label] of [["gfx.ao", "Ambient occlusion (corner shading)"], ["gfx.culling", "Culling (skip what you can not see)"], ["gfx.lod", "Auto-LOD (simpler models far away)"], ["gfx.shadows", "Real-time shadows"], ["gfx.bloom", "Bloom (glow)"]]) if (!project.tags[n]) project.tags[n] = { type: "bool", value: true, label, group: "gfx", builtin: true };
    for (const att of project.player.attachments) {   // bring older objects up to date: a "weapon" tick became the object type
      const pre = "player." + att.id + ".", old = project.tags[pre + "weapon"];
      for (const p of S.ATTACH) { const r = project.tags[pre + p.k]; if (!r) { const n = rec(p, "player", "player." + att.id); if (p.type === "anim") n.modelTag = pre + "asset"; if (p.k === "type") n.value = old && old.value ? "weapon" : "animation"; if (p.k === "name") n.value = old && old.value ? "Weapon" : "Object"; project.tags[pre + p.k] = n; } else { r.label = p.label; if (p.showIf) r.showIf = p.showIf; else delete r.showIf; if (p.options) r.options = p.options; } }
      delete project.tags[pre + "weapon"];
      if (att.script) for (const n of att.script.nodes) S.ensureSNode(project, "player." + att.id, n, pre + "asset");
      if (!att.script) att.script = { nodes: [], links: [] };   // the old "click animation" menu became a script: when clicked, play that animation
      const fa = project.tags[pre + "fireAnim"]; if (fa) { if (fa.value && !att.script.nodes.length) { const a = S.addSNode(project, "player." + att.id, att.script, "onClick", 20, 20), b = S.addSNode(project, "player." + att.id, att.script, "playAnim", 260, 20, { anim: fa.value }, pre + "asset"); att.script.links.push({ a: a.id, o: "next", b: b.id }); } delete project.tags[pre + "fireAnim"]; }
    }
    const newPause = !project.tags["player.pauseScreen"];
    for (const p of S.PLAYER) if (!project.tags["player." + p.k]) { const r = rec(p, "player", "player"); r.builtin = true; project.tags["player." + p.k] = r; }
    for (const sc of project.screens || []) for (const el of sc.nodes || []) if (el.script) for (const n of el.script.nodes) S.ensureSNode(project, sc.id + "." + el.id, n);
    if (!project.tags["player.state"]) project.tags["player.state"] = { type: "string", value: "standing", label: "What the player is doing: standing, walking, running, jumping, falling (live)", group: "player", builtin: true };
    if (project.player && project.player.script) for (const n of project.player.script.nodes) S.ensureSNode(project, "player", n);
    S.ensureComp(project); S.ensureZones(project); S.ensureScreens(project); S.ensureThingTags(project);
    for (const d of project.dialogues || []) for (const n of d.nodes) S.ensureSNode(project, d.id, n);
    for (const [k, l] of [["lives", "Lives left (live)"], ["tanks", "Full energy tanks (live)"], ["tanksMax", "Energy tanks owned (live)"]]) if (!project.tags["player." + k]) project.tags["player." + k] = { type: "number", value: 0, min: 0, max: 99, step: 1, label: l, group: "player", builtin: true };
    if (!project.tags["player.lastButton"]) project.tags["player.lastButton"] = { type: "string", value: "", label: "Name of the last button pressed (live)", group: "player", builtin: true };
    for (const [k, l] of [["aiming", "The aim button is held (live)"], ["locked", "Locked onto a target (live)"]]) if (!project.tags["player." + k]) project.tags["player." + k] = { type: "bool", value: false, label: l, group: "player", builtin: true };
    for (const [k, t, v, l] of [["bossActive", "bool", false, "A boss fight is on (live)"], ["bossHealth", "number", 0, "Boss health (live)"], ["bossMax", "number", 0, "Boss full health (live)"], ["bossName", "string", "", "Boss name (live)"], ["bossPhase", "string", "", "Boss phase name (live)"]]) if (!project.tags["player." + k]) project.tags["player." + k] = Object.assign({ type: t, value: v, label: l, group: "player", builtin: true }, t === "number" ? { min: 0, max: 1000000, step: 1 } : {});
    for (let i = 1; i <= 3; i++) if (!project.tags["save.info" + i]) project.tags["save.info" + i] = { type: "string", value: "Empty", label: "Save slot " + i + ": where and when (live)", group: "save", builtin: true };
    if (!project.tags["save.any"]) project.tags["save.any"] = { type: "bool", value: false, label: "There is at least one save (live)", group: "save", builtin: true };
    if (!project.tags["save.slot"]) project.tags["save.slot"] = { type: "number", value: 1, min: 1, max: 3, step: 1, label: "Current save slot (live)", group: "save", builtin: true };
    for (const sc of project.screens || []) for (const n of sc.nodes || []) if (n.type === "button") for (const p of S.NODE.button.props) { const k = sc.id + "." + n.id + "." + p.k, r = project.tags[k]; if (!r) project.tags[k] = rec(p, sc.id, sc.id + "." + n.id); else { if (p.options) r.options = p.options; if (p.showIf) r.showIf = p.showIf; r.label = p.label; } }
    for (const p of S.PLAYER) { const r = project.tags["player." + p.k]; if (r && p.options) r.options = p.options; }
    S.ensureKeys(project); S.ensureSolids(project); S.ensureVisors(project); S.ensureCodex(project); for (const p of project.props || []) if (p.script) for (const n of p.script.nodes) S.ensureSNode(project, p.id, n, p.id + ".asset");
    if (!project.tags["player.reloading"]) project.tags["player.reloading"] = { type: "bool", value: false, label: "The current weapon is reloading (live)", group: "player", builtin: true };
    if (!project.tags["player.charge"]) project.tags["player.charge"] = { type: "number", value: 0, label: "Charge beam progress, 0 to 1 (live)", group: "player", min: 0, max: 1, step: 0.01, builtin: true };
    if (!project.tags["player.weapon"]) project.tags["player.weapon"] = { type: "number", value: 1, label: "Current weapon number (live)", group: "player", min: 0, max: 99, step: 1, builtin: true };
    if (!project.tags["player.health"]) project.tags["player.health"] = { type: "number", value: 100, label: "Current health (live)", group: "player", min: 0, max: 1000, step: 1, builtin: true };
    if (newPause && !project.screens.some(s => s.id === "pause")) S.addPauseScreen(project, "pause", true);
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
    B("gfx.shadows", "bool", true, "Real-time shadows", "gfx"); B("gfx.ao", "bool", true, "Ambient occlusion (corner shading)", "gfx"); B("gfx.bloom", "bool", true, "Bloom (glow)", "gfx");
    B("project.startScreen", "screenRef", "splash", "Start screen", "project");
  };

  // a pause menu screen (kind "pause"): PAUSED, Resume, Restart, Options, Quit to menu; withOptions adds a second pause screen with the settings and a Back button
  S.addPauseScreen = (project, id, withOptions) => {
    const taken = project.screens.map(s => s.id); id = S.uid(id || "pause", taken); const sc = { id, kind: "pause", nodes: [] }; project.screens.push(sc); S.defineScreen(project, sc);
    const add = (s, nid, type, over) => { const n = { id: nid, type }; s.nodes.push(n); S.defineNode(project, s.id, n, over); };
    add(sc, "title", "title", { text: "PAUSED", size: 88, y: 170 });
    add(sc, "resume", "button", { text: "Resume", y: 50, action: "resume" });
    add(sc, "restart", "button", { text: "Restart level", y: -15, action: "restart" });
    let opt = null; if (withOptions) { opt = { id: S.uid(id + "_options", project.screens.map(s => s.id)), kind: "pause", nodes: [] }; project.screens.push(opt); S.defineScreen(project, opt); }
    if (opt) add(sc, "options", "button", { text: "Options", y: -80, action: "goto", target: opt.id });
    add(sc, "menu", "button", { text: "Quit to main menu", y: opt ? -145 : -80, action: "menu" });
    if (opt) { add(opt, "title", "title", { text: "Options", size: 64, y: 230 });
      [["master", "Master volume", "audio.master", 140], ["sfx", "SFX volume", "audio.sfx", 75], ["music", "Music volume", "audio.music", 10], ["brightness", "Brightness", "gfx.brightness", -55]].forEach(([i, l, b, y]) => add(opt, i, "slider", { label: l, bind: b, y }));
      add(opt, "shadows", "toggle", { label: "Shadows", bind: "gfx.shadows", y: -115 }); add(opt, "bloom", "toggle", { label: "Bloom (glow)", bind: "gfx.bloom", y: -160 });
      add(opt, "back", "button", { text: "Back", y: -250, action: "back" }); }
    return sc;
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
