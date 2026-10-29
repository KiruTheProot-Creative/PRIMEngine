/* PRIMEngine save games. A save is one JSON object in the player's browser / desktop app storage (localStorage),
   key "prime-save-<project id>-<slot>", slots 1..3:
   { v, time, map, station, name (station name), pos: {x, y, z, yaw}, tags: {name: value}, world: {mapId: {taken: [], unlocked: [], shot: [], shields: [], bosses: []}} }
   Live tags save.info1..3 ("Empty" or where / when) and save.any let menu screens show the slots. */
(function () {
  "use strict";
  const PRIM = (window.PRIM = window.PRIM || {}), SLOTS = 3;
  const key = (pid, slot) => "prime-save-" + (pid || "game") + "-" + slot;
  const Save = PRIM.Save = {
    SLOTS,
    read(pid, slot) { try { const s = localStorage.getItem(key(pid, slot)); return s ? JSON.parse(s) : null; } catch (e) { return null; } },
    write(pid, slot, data) { try { localStorage.setItem(key(pid, slot), JSON.stringify(data)); return true; } catch (e) { return false; } },
    erase(pid, slot) { try { localStorage.removeItem(key(pid, slot)); } catch (e) {} },
    // the slot saved most recently (or 0 when there is no save at all)
    latest(pid) { let best = 0, bt = -1; for (let s = 1; s <= SLOTS; s++) { const d = Save.read(pid, s); if (d && d.time > bt) { bt = d.time; best = s; } } return best; },
    info(d) { if (!d) return "Empty"; const t = new Date(d.time), two = n => String(n).padStart(2, "0"); return (d.name || d.map || "Saved") + "  ·  " + t.getFullYear() + "-" + two(t.getMonth() + 1) + "-" + two(t.getDate()) + " " + two(t.getHours()) + ":" + two(t.getMinutes()); },
    // fill the live tags the menus can show ({save.info1} …)
    refreshTags(T, pid) { if (!T || !T.has || !T.has("save.info1")) return; const o = {}; let any = false; for (let s = 1; s <= SLOTS; s++) { const d = Save.read(pid, s); if (d) any = true; o["save.info" + s] = Save.info(d); } o["save.any"] = any; T.setMany(o); },
  };
})();
