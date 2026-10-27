# PRIMEngine: notes for Claude

- Owner (Kiru) does not code; they direct, make art/audio and test. Explain in plain English; do the coding.
- Plain classic `<script>` files on a global `PRIM` namespace. No bundler, no ES modules, no third-party libs.
- `index.html` is PRIMEngine Studio (project manager + screen editor). `legacy-editor.html` is the old Metroid-style test editor (still works, uses src/engine, src/game, src/editor).
- Studio: EVERY value is a tag (`src/core/tags.js`), named `screen.element.property` or `audio.master`. Structure (screens, element ids) is separate from values. Renaming an element moves its tags and fixes references.
- Studio projects live in localStorage; imported files in IndexedDB (`src/core/store.js`). Screens are drawn by `src/core/runtime.js` (DOM, 1280x720 stage).
- Never commit `projects/*/assets/` (private / fan-game material). Never draw or invent Nintendo characters, creatures or logos; placeholders must be original.
- Never write `*/` inside a block comment (it ended one early once).
- Test studio: serve repo root on :8766, `NODE_PATH=$(npm root -g) node tools/dev/shot2.js <out> tools/dev/s_studio.js`. Legacy editor: `tools/dev/shot.js` + `s_editor.js` (needs software GL).
- Publishing: `python3 tools/build_single.py` (add `legacy-editor.html` for the old one), then publish `dist/index.html` as an artifact (Studio needs no supporting files).
