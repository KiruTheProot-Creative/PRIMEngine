/* PRIMEngine game player: runs a finished game outside the Studio.
   "File > Build Game" writes one .html file: the engine scripts from src/core, this player, and the project with every asset
   inside it. Opening that file starts the game at the start screen (menus, then the maps), exactly like Maximize on Play.
   boot(data, host): data = { project, assets: { assetId: dataURL } } */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {});
  const CSS = `
html,body{margin:0;height:100%;background:#000;overflow:hidden;font-family:"Chakra Petch",system-ui,sans-serif}
#game{position:fixed;inset:0}
.p3layer{position:absolute;inset:0;z-index:60}.p3root{position:absolute;inset:0;background:#000;overflow:hidden}.p3cv{cursor:none}
.p3x{position:absolute;left:50%;top:50%;width:0;height:0;pointer-events:none}.p3hud{position:absolute;inset:0;pointer-events:none}
.p3leave{position:absolute;top:14px;right:14px;z-index:5;padding:8px 14px;background:#1b232d;color:#e6f0fa;border:1px solid #5a6b7d;border-radius:4px;font-size:14px;cursor:pointer}.p3leave:hover{background:#2a3644}
.p3msg{position:absolute;inset:0;display:grid;place-items:center;text-align:center;padding:20px;background:rgba(0,0,0,.45);color:#e6f0fa;font-size:15px;cursor:pointer;letter-spacing:.04em}
.v3cv{position:absolute;inset:0;width:100%;height:100%;display:block;outline:none}
.gp-end{position:fixed;inset:0;display:grid;place-items:center;color:#cfe0ee;font:22px "Share Tech Mono",ui-monospace,monospace;text-align:center;line-height:2}
.gp-end button{font:inherit;font-size:16px;color:#d9f7ff;background:#14202b;border:1px solid #3d5a72;border-radius:4px;padding:6px 18px;cursor:pointer}`;
  PRIM.GamePlayer = {
    boot(data, host) {
      const st = document.createElement("style"); st.textContent = CSS; document.head.append(st);
      const project = data.project, urls = data.assets || {}, T = new PRIM.Tags(project.tags), key = "prime-game-" + project.id;
      document.title = project.name || "Game";
      for (const a of project.assets) if (a.type === "font" && urls[a.id] && window.FontFace) { try { new FontFace("prim-font-" + a.id, "url(" + urls[a.id] + ")").load().then(f => document.fonts.add(f)).catch(() => {}); } catch (e) {} }
      if (PRIM.FBX) { for (const a of project.assets) if (a.type === "model" && urls[a.id]) PRIM.FBX.load(a.id, urls[a.id]); }
      if (PRIM.Audio) PRIM.Audio.preload(project.assets.filter(a => a.type === "sound" && urls[a.id]).map(a => urls[a.id]));
      const run = () => {
        host.replaceChildren(); const tg = T.fork(); let saved = {}; try { saved = JSON.parse(localStorage.getItem(key) || "{}"); } catch (e) {} for (const k in saved) if (T.has(k)) tg.ov[k] = saved[k];
        let start = T.get("project.startScreen"); if (!project.screens.some(s => s.id === start)) start = project.screens[0].id;
        const startGame = rt => {
          const sm = rt.tags.get("player.startMap"), m = project.maps.find(x => x.id === sm) || project.maps[0]; if (!m) { rt.game = true; rt.rebuild(); return; }
          rt.stopMusic(); const layer = document.createElement("div"); layer.className = "p3layer"; rt.host.append(layer);
          const pl = new PRIM.Play3D({ host: layer, project, map: m, tags: rt.tags, urls, mode: "play", debug: false, onExit: () => { pl.destroy(); layer.remove(); },
            onScreen: id => { pl.destroy(); layer.remove(); if (project.screens.some(x => x.id === id)) rt.show(id); } });
        };
        const rt = new PRIM.Runtime({ host, project, tags: tg, mode: "play", urls, screenId: start, noEscExit: true, onPersist: ov => { try { localStorage.setItem(key, JSON.stringify(ov)); } catch (e) {} }, onExit: () => end(rt), onStartGame: startGame });
      };
      const end = rt => { rt.destroy(); const box = document.createElement("div"); box.className = "gp-end"; box.innerHTML = "<div>Thanks for playing!<br><button>Play again</button></div>"; box.querySelector("button").onclick = () => { box.remove(); run(); }; host.replaceChildren(box); try { window.close(); } catch (e) {} };
      run();
    },
  };
})();
