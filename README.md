# PRIMEngine

*Design your game.* A no-code / low-code retro first-person shooter engine and editor, built from scratch
(own WebGL renderer, no third-party libraries).

**Status: Studio v0.** A project manager, and a screens editor: splash sequences (length + fade styles), titles, images,
buttons (text or texture with hover/click sounds), sliders and toggles, a settings screen, and a tag system where every value has a name.
The earlier Metroid-style test editor is still in `legacy-editor.html`.

## Running it on your computer

Browsers block loading files from disk, so the project needs a tiny local server. In this folder:

```
python3 -m http.server 8000
```

then open <http://localhost:8000/>. (A one-click desktop app comes later.)

## Layout

| Path | What it is |
| --- | --- |
| `index.html` | the editor shell |
| `src/engine/` | math, WebGL renderer, skeletal models, audio |
| `src/game/` | the game runtime and the test level |
| `src/editor/` | the editor |
| `projects/<name>/project.json` | everything the editor edits: asset list, settings, object transforms, sound bindings |
| `projects/<name>/assets/` | the project's files. **Git-ignored**, they stay on your machine |
| `tools/bake_fbx.py` | turns a Blockbench ASCII FBX into `arm.json` + texture |
| `tools/build_single.py` | builds `dist/index.html`, a one-file editor |

## Saving

The editor autosaves into the browser. **Save** writes `project.json` to a file (or downloads it); put it back in
`projects/<name>/` to make the change permanent in Git.

## Assets

`projects/*/assets/` is ignored by Git on purpose, so nothing private ends up in the public repo.
