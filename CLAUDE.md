# PRIMEngine: notes for Claude

- Owner (Kiru) does not code; they direct, make art/audio and test. Explain in plain English; do the coding.
- Plain classic `<script>` files on a global `PRIM` namespace. No bundler, no ES modules, no third-party libs.
- Everything tweakable lives in `projects/*/project.json`. The game reads it every frame, so the editor edits it live.
- Never commit `projects/*/assets/` (private / fan-game material). Never draw or invent Nintendo characters, creatures or logos; placeholders must be original.
- Never write `*/` inside a block comment (it ended one early once).
- Test: serve the repo root on :8766, then `NODE_PATH=$(npm root -g) node tools/dev/shot.js <out> tools/dev/s_editor.js` (Playwright, software GL).
- Publishing: `python3 tools/build_single.py`, then publish `dist/index.html` as an artifact with `projects/` as supporting files.
