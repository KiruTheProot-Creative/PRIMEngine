# PRIMEngine: notes for Claude

- Owner (Kiru) does not code; they direct, make art/audio and test. Explain in plain English; do the coding.
- Plain classic `<script>` files on a global `PRIM` namespace. No bundler, no ES modules, no third-party libs.
- `index.html` is PRIMEngine Studio (project manager + screen editor). `legacy-editor.html` is the old Metroid-style test editor (still works, uses src/engine, src/game, src/editor).
- Studio: EVERY value is a tag (`src/core/tags.js`), named `screen.element.property` or `audio.master`. Structure (screens, element ids) is separate from values. Renaming an element moves its tags and fixes references.
- Project file = `.prime` (a ZIP: project.json + Assets/ folders), see `src/core/prime.js`, `zip.js`. Browser autosave copy lives in localStorage; imported files in IndexedDB (`src/core/store.js`). `src/core/host.js` handles save/open/pick-folder (Electron via `window.primeDesktop`, else File System Access API with download/<input> fallbacks).
- Maps (`project.maps`, format still 3): geometry is structure (`verts`, `sectors` = closed loops of vertex ids); every other value is a tag (`map1.s1.floor`, `map1.v3.tex` = texture of the wall running from node v3 to the next node of its room). Editor styles: `doom` (built, `src/studio/mapeditor.js`), `modern` (locked in the New Map dialog, comes later). 3D renderer = `src/core/render3d.js` (WebGL1, free-fly camera). Game Play action `start` launches the first map in the fly view for now.
- Screens are drawn by `src/core/runtime.js` (DOM, 1280x720 stage). Studio UI (`src/studio/studio.js`, CSS in index.html) copies classic Unity: Hierarchy, Scene/Game, Inspector (+Tags), Project explorer, Console; skins `dark` / `light`.
- TARGET IS WINDOWS ONLY for now (owner decision). Mac/Linux builds come after the Windows app is finished; do not spend time on them.
- Desktop app: `desktop/` (Electron). Built on GitHub by `.github/workflows/build-desktop.yml` (push to main = Actions artifacts; tag `v*` = Release). npm is NOT reachable from the Claude sandbox, so desktop builds can't be tested locally.
- Never commit `projects/*/assets/` (private / fan-game material). Never draw or invent Nintendo characters, creatures or logos; placeholders must be original.
- Never write `*/` inside a block comment (it ended one early once).
- Test studio: serve repo root on :8766, `NODE_PATH=$(npm root -g) node tools/dev/shot3.js <out> tools/dev/s_unity.js` (removes File System Access APIs to test fallbacks). Legacy editor: `tools/dev/shot.js` + `s_editor.js` (needs software GL). Map test: `tools/dev/s_map.js` through shot3.js (shot3 launches Chromium with SwiftShader so WebGL works).
- Publishing: `python3 tools/build_single.py` (add `legacy-editor.html` for the old one), then publish `dist/index.html` as an artifact (Studio needs no supporting files).
