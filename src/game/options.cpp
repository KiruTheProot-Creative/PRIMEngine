#include "options.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace mc {

namespace {
struct Field { float* f = nullptr; int* i = nullptr; bool* b = nullptr; };
Field field(Settings& s, const std::string& k) {
    Field r;
    if (k == "volume") r.f = &s.soundVolume; else if (k == "vol_weapons") r.f = &s.volWeapons; else if (k == "vol_movement") r.f = &s.volMovement;
    else if (k == "vol_enemies") r.f = &s.volEnemies; else if (k == "vol_world") r.f = &s.volWorld; else if (k == "vol_menus") r.f = &s.volMenus; else if (k == "vol_music") r.f = &s.volMusic;
    else if (k == "fullscreen") r.b = &s.fullscreen; else if (k == "vsync") r.b = &s.vsync; else if (k == "brightness") r.f = &s.brightness; else if (k == "screen_shake") r.f = &s.screenShake;
    else if (k == "fov") r.f = &s.fov; else if (k == "ui_scale") r.f = &s.uiScale;
    else if (k == "render_distance") r.i = &s.renderDistance; else if (k == "vertical_distance") r.i = &s.verticalDistance; else if (k == "distant_detail") r.i = &s.distantDetail; else if (k == "target_fps") r.i = &s.targetFps; else if (k == "max_fps") r.i = &s.maxFps; else if (k == "hide_hidden") r.b = &s.hideHidden;
    else if (k == "shadows") r.i = &s.shadowQuality; else if (k == "shadow_res") r.i = &s.shadowRes; else if (k == "shadow_distance") r.f = &s.shadowDistance;
    else if (k == "shadow_soft") r.i = &s.shadowSoft; else if (k == "shadow_softness") r.f = &s.shadowSoftness;
    else if (k == "clouds") r.f = &s.cloudCover; else if (k == "cloud_shadows") r.b = &s.cloudShadows; else if (k == "fog") r.f = &s.groundFog;
    else if (k == "bloom") r.f = &s.bloom; else if (k == "reflections") r.i = &s.reflections; else if (k == "reflect_res") r.i = &s.reflectRes; else if (k == "anti_alias") r.i = &s.antiAlias; else if (k == "render_scale") r.f = &s.renderScale; else if (k == "upscaler") r.i = &s.upscaler; else if (k == "up_sharpness") r.f = &s.upSharp; else if (k == "pixel_shadows") r.i = &s.pixelShadows; else if (k == "probes") r.i = &s.probes; else if (k == "show_fps") r.b = &s.showFps; else if (k == "ao") r.i = &s.ambientOcclusion; else if (k == "ao_strength") r.f = &s.aoStrength;
    else if (k == "swaying") r.b = &s.swaying; else if (k == "hud_glow") r.b = &s.hudGlow;
    else if (k == "eye_adapt") r.b = &s.eyeAdapt; else if (k == "film_fx") r.b = &s.filmFx; else if (k == "motion_blur") r.f = &s.motionBlur; else if (k == "lamp_blocks") r.i = &s.lampBlocks; else if (k == "ray_tracing") r.b = &s.rayTracing; else if (k == "rt_reflections") r.b = &s.rtReflect; else if (k == "rt_objects") r.b = &s.rtObjects; else if (k == "rt_lamp_shadows") r.b = &s.rtShadows; else if (k == "lamp_fill") r.f = &s.lampFill; else if (k == "light_shadows") r.i = &s.lightShadows; else if (k == "light_shadow_distance") r.f = &s.lightShadowDist; else if (k == "light_shadow_res") r.i = &s.lightShadowRes; else if (k == "lamp_distance") r.f = &s.lampDistance; else if (k == "water_caustics") r.i = &s.waterCaustics; else if (k == "water_caustics_above") r.b = &s.waterCausticsAbove; else if (k == "water_shafts") r.b = &s.waterShafts; else if (k == "water_foam") r.b = &s.waterFoam; else if (k == "visor_drops") r.b = &s.visorDrops; else if (k == "caustic_strength") r.f = &s.causticStrength; else if (k == "godrays") r.f = &s.godrays; else if (k == "light_fog") r.b = &s.lightFog; else if (k == "fog_grid") r.b = &s.fogGrid; else if (k == "lava_light") r.f = &s.lavaLight; else if (k == "bounce") r.i = &s.bounce; else if (k == "planar_floors") r.b = &s.planarFloors; else if (k == "reflect_chunks") r.i = &s.reflectChunks; else if (k == "reflect_far") r.b = &s.reflectFar; else if (k == "lens_flares") r.b = &s.lensFlares;
    else if (k == "connect_textures") r.b = &s.connectTextures; else if (k == "grass_distance") r.f = &s.grassDistance;
    else if (k == "bumps") r.f = &s.bumps; else if (k == "coat") r.f = &s.coat; else if (k == "materials") r.b = &s.materials; else if (k == "guess_materials") r.b = &s.guessMaterials;
    else if (k == "time_of_day") r.f = &s.timeOfDay; else if (k == "day_cycle") r.b = &s.dayCycle; else if (k == "block_preview") r.b = &s.blockPreview;
    else if (k == "sensitivity") r.f = &s.mouseSensitivity; else if (k == "invert_y") r.b = &s.invertY; else if (k == "lock_mode") r.i = &s.lockMode; else if (k == "cave_culling") r.b = &s.caveCulling;
    return r;
}
float readField(const Field& f) { return f.f ? *f.f : f.i ? (float)*f.i : f.b ? (*f.b ? 1.f : 0.f) : 0.f; }
void writeField(const Field& f, float v) { if (f.f) *f.f = v; else if (f.i) *f.i = (int)std::lround(v); else if (f.b) *f.b = v > 0.5f; }

OptionDef slider(const char* key, const char* label, const char* group, const char* section, float lo, float hi, float step, const char* fmt, const char* zero = "", const char* tip = "") {
    OptionDef d; d.key = key; d.label = label; d.group = group; d.section = section; d.type = OptType::Slider; d.lo = lo; d.hi = hi; d.step = step; d.fmt = fmt; d.zero = zero; d.tip = tip; return d;
}
OptionDef toggle(const char* key, const char* label, const char* group, const char* section, const char* tip = "") {
    OptionDef d; d.key = key; d.label = label; d.group = group; d.section = section; d.type = OptType::Toggle; d.tip = tip; return d;
}
OptionDef choice(const char* key, const char* label, const char* group, const char* section, std::vector<std::string> ch, std::vector<float> vals = {}, const char* tip = "") {
    OptionDef d; d.key = key; d.label = label; d.group = group; d.section = section; d.type = OptType::Choice; d.choices = std::move(ch); d.values = std::move(vals); d.tip = tip; return d;
}
OptionDef needs(OptionDef d, const char* n) { d.needs = n; return d; }
OptionDef editorOnly(OptionDef d) { d.inGame = false; return d; }
OptionDef reset(const char* group) { OptionDef d; d.key = std::string("reset_") + group; d.label = "Back to the start values"; d.group = group; d.section = ""; d.type = OptType::Reset; return d; }
} // namespace

const std::vector<std::string>& optionGroups() { static const std::vector<std::string> g = { "Sound", "Display", "World", "Lighting", "Shadows", "Water", "Voxel tracing", "Post-processing", "Controls" }; return g; }

const std::vector<OptionDef>& optionDefs() {
    static const std::vector<OptionDef> defs = [] {
        std::vector<OptionDef> v;
        const char* S = "Sound"; const char* D = "Display"; const char* W = "World"; const char* L = "Lighting"; const char* SH = "Shadows"; const char* RT = "Voxel tracing"; const char* WA = "Water"; const char* P = "Post-processing"; const char* C = "Controls";
        v.push_back(slider("volume", "Master volume", S, "Volume", 0, 2, 0.05f, "%", "silent"));
        v.push_back(slider("vol_weapons", "Weapons", S, "Volume", 0, 2, 0.05f, "%", "silent", "Beams, charging, missiles, their hits."));
        v.push_back(slider("vol_movement", "Movement", S, "Volume", 0, 2, 0.05f, "%", "silent", "Steps, jumps, landing, the Morph Ball and its bombs."));
        v.push_back(slider("vol_enemies", "Enemies", S, "Volume", 0, 2, 0.05f, "%", "silent", "Enemies noticing you, attacking, getting hurt, dying."));
        v.push_back(slider("vol_world", "World", S, "Volume", 0, 2, 0.05f, "%", "silent", "Doors, events and everything else."));
        v.push_back(slider("vol_menus", "Menus", S, "Volume", 0, 2, 0.05f, "%", "silent", "Picking and using things in the menus, typed text."));
        v.push_back(slider("vol_music", "Music", S, "Volume", 0, 2, 0.05f, "%", "silent"));
        v.push_back(reset(S));

        // ---- Display: the screen, the picture, the HUD
        { OptionDef p; p.key = "quality_preset"; p.label = "Quality preset"; p.group = D; p.section = "Quality"; p.type = OptType::Preset;
          p.tip = "Sets the heavy graphics options at once (shadows, light, reflections, distance, render scale...). Change one by hand and it says Custom."; v.push_back(p); }
        v.push_back(toggle("fullscreen", "Fullscreen", D, "Screen", "Borderless, over the whole screen."));
        v.push_back(toggle("vsync", "VSync", D, "Screen", "Waits for the screen: no tearing, frame rate up to the screen's."));
        v.push_back(choice("render_scale", "Render scale", D, "Screen", { "33% (ultra performance)", "50% (performance)", "58% (balanced)", "67% (quality)", "75%", "85%", "100% (the screen's own)", "125%", "150%", "200% (supersampled)" },
                           { 0.33f, 0.5f, 0.58f, 0.67f, 0.75f, 0.85f, 1.f, 1.25f, 1.5f, 2.f },
                           "The 3D world is drawn this much of the screen's size, then fitted to it. Below 100%: faster (the upscaler rebuilds the detail). Above 100%: the smoothest edges there are, but heavy. The HUD and menus always stay sharp."));
        v.push_back(choice("max_fps", "Frame cap", D, "Screen", { "Off", "30 FPS", "60 FPS", "90 FPS", "120 FPS", "144 FPS", "165 FPS" }, { 0, 30, 60, 90, 120, 144, 165 },
            "At most this many frames a second, evenly spaced. A steady frame rate feels smoother than one jumping between 90 and 150. Pick one your computer reaches almost everywhere."));
        v.push_back(choice("target_fps", "Auto resolution", D, "Screen", { "Off", "Keep 30 FPS", "Keep 45 FPS", "Keep 60 FPS", "Keep 75 FPS", "Keep 90 FPS", "Keep 120 FPS", "Keep 144 FPS" }, { 0, 30, 45, 60, 75, 90, 120, 144 },
            "When the graphics card can't keep up, the world is drawn a little smaller (down to half of the render scale), and sharper again when it can. The upscaler hides most of it."));
        v.push_back(choice("upscaler", "Upscaler", D, "Screen", { "Sharp (this frame only)", "PrimeScaler" }, {},
                           "PrimeScaler: the camera moves a fraction of a pixel every frame, and the picture is rebuilt from several frames, so a smaller render scale still looks sharp. At 100% it is the smoothest anti-aliasing. Sharp: enlarges this frame only (fast, softer)."));
        v.push_back(slider("up_sharpness", "Sharpness", D, "Screen", 0, 1, 0.05f, "%", "off", "How much PrimeScaler sharpens its picture."));
        v.push_back(slider("brightness", "Brightness", D, "Picture", 0.3f, 2.5f, 0.05f, "%"));
        v.push_back(slider("fov", "Field of view", D, "Picture", 50, 110, 1, "%.0f"));
        v.push_back(slider("screen_shake", "Screen shake", D, "Picture", 0, 2, 0.05f, "%", "off", "Bosses stomping, slams and roars shake the view."));
        v.push_back(slider("ui_scale", "Interface size", D, "HUD and menus", 0.75f, 2, 0.05f, "%"));
        v.push_back(toggle("hud_glow", "HUD glow", D, "HUD and menus", "The HUD's glow, and its light on the helmet."));
        v.push_back(toggle("show_fps", "Show frames a second", D, "Performance", "A small counter in the corner: frames a second and milliseconds a frame (not capped unless VSync is on)."));
        v.push_back(reset(D));

        // ---- World: how far, the sky, nature, the surfaces of blocks
        v.push_back(slider("render_distance", "Render distance", W, "Distance", 2, 32, 1, "%.0f chunks"));
        v.push_back(slider("vertical_distance", "Up / down distance", W, "Distance", 1, 12, 1, "%.0f chunks"));
        v.push_back(toggle("hide_hidden", "Skip what is hidden", W, "Distance", "Chunks and far copies hidden behind walls and hills aren't drawn (worked out from what was drawn a frame or two before)."));
        v.push_back(choice("distant_detail", "Distant detail", W, "Distance", { "Off", "Low (twice as far)", "Medium (4 x as far)", "High (8 x as far)" }, {}, "Past the render distance the world goes on as simpler copies (blocks merged 2, 4 and 8 at a time): you see much further for little cost."));
        v.push_back(toggle("cave_culling", "Skip what is behind walls", W, "Distance", "Chunks you cannot see from where you are (rock, closed caves, the other side of walls) are not drawn. Off: everything in range is drawn."));
        v.push_back(slider("clouds", "Clouds", W, "Sky", 0, 1, 0.05f, "%", "none"));
        v.push_back(toggle("cloud_shadows", "Cloud shadows", W, "Sky"));
        v.push_back(slider("fog", "Ground fog", W, "Sky", 0, 2, 0.05f, "%", "none"));
        v.push_back(toggle("swaying", "Leaves sway in the wind", W, "Nature"));
        v.push_back(slider("grass_distance", "Grass blades", W, "Nature", 0, 96, 4, "%.0f blocks", "off", "Blocks with grass blades (block editor) grow them, out to this far from you: a pixel-wide blade for many of the top's pixels, swaying, leaning out over edges. Farther: shorter rows, then the plain top."));
        v.push_back(toggle("connect_textures", "Connected textures", W, "Surfaces", "Blocks set to connect (block editor) creep over the blocks beside them with a jagged border in the texture's own pixels, instead of a straight seam."));
        v.push_back(slider("bumps", "Bumpy textures", W, "Surfaces", 0, 2, 0.05f, "%", "off", "Each texture's light and dark pixels become little bumps that catch the light."));
        v.push_back(slider("coat", "Texture coat", W, "Surfaces", 0, 2, 0.05f, "%", "off", "A fine noise inside each pixel, so big flat pixels look like stone, dirt, wood..."));
        v.push_back(toggle("materials", "Materials (shiny, metal, glowing)", W, "Surfaces"));
        v.push_back(editorOnly(toggle("guess_materials", "Guess materials from texture names", W, "Surfaces",
                                      "Textures you did not set up get a material from their name's words (iron: metal, glass: shiny, lava: glowing...).")));
        v.push_back(editorOnly(slider("time_of_day", "Time of day", W, "Editor", 0, 24, 0.1f, "%.1f h")));
        v.push_back(editorOnly(toggle("day_cycle", "The sun moves (a day is 20 minutes)", W, "Editor")));
        v.push_back(editorOnly(toggle("block_preview", "Show where the next block goes", W, "Editor")));
        v.push_back(reset(W));

        // ---- Lighting: light that bounces, shade in corners, reflections
        v.push_back(choice("bounce", "Bounce light (on screen)", L, "Light", { "Off", "Normal", "Far-reaching" }, {}, "Lit and glowing surfaces light the ones near them (a green glow tints the wall beside it)."));
        v.push_back(choice("ao", "Ambient occlusion", L, "Light", { "Off (only block corners)", "Normal", "High" }, {}, "Soft shade in corners, under overhangs and where things touch."));
        v.push_back(needs(slider("ao_strength", "Occlusion strength", L, "Light", 0.2f, 3, 0.1f, "%.1f"), "ao"));
        v.push_back(toggle("light_fog", "Lights glow in the air", L, "Light", "Beams and halos around lights in fog and haze."));
        v.push_back(toggle("fog_grid", "Smooth fog (built over frames)", L, "Light", "Fog, light shafts and glowing lights are worked out in a small 3D grid that builds up over a few frames: faster and without grain. Off: every pixel looks through the air each frame (sharper thin beams, slower)."));
        v.push_back(slider("lava_light", "Lava light", L, "Light", 0, 3, 0.05f, "%.2f", "off", "How strongly lava lights the blocks around it (ceilings, pillars, shores) and the air over it."));
        v.push_back(slider("godrays", "Light shafts (god rays)", L, "Light", 0, 3, 0.05f, "%.2f", "off", "How strong the sun's beams are in fog and haze, and under water."));
        v.push_back(choice("reflections", "Reflections", L, "Reflections", { "Off (only the sky)", "Normal", "High" }, {}, "Shiny surfaces mirror what is on the screen (with Ray tracing: the blocks off screen and behind you too)."));
        v.push_back(needs(choice("reflect_res", "Reflection resolution", L, "Reflections", { "Full", "Three quarters", "Half", "A quarter (fastest)" }), "reflections"));
        v.push_back(needs(toggle("planar_floors", "Exact floor and water reflections", L, "Reflections", "The shiny floor or water near you mirrors everything exactly, animated things too (the world is drawn once more, at half size)."), "reflections"));
        v.push_back(needs(slider("reflect_chunks", "How far floors and water reflect", L, "Reflections", 1, 32, 1, "%.0f chunks", "", "A lake mirrors the mountains on its far side: the further, the more is drawn a second time. At the render distance or more: as far as the world is drawn."), "planar_floors"));
        v.push_back(needs(toggle("reflect_far", "Reflect the far land too", L, "Reflections", "Past the render distance: the distant land (its far copies) in the mirror as well. Only when the reflections reach as far as the world is drawn."), "planar_floors"));
        v.push_back(reset(L));

        // ---- Shadows: the sun's and the lamps'
        v.push_back(toggle("shadows", "Sun shadows", SH, "The sun"));
        v.push_back(needs(choice("shadow_res", "Shadow resolution", SH, "The sun", { "1024", "1536", "2048", "3072", "4096 (heavy)" }, { 1024, 1536, 2048, 3072, 4096 },
                                 "The size of each of the four shadow maps. Higher: crisper shadows, more graphics memory."), "shadows"));
        v.push_back(needs(slider("shadow_distance", "Shadow distance", SH, "The sun", 24, 512, 8, "%.0f blocks", "", "How far shadows reach. The sharpness near you stays the same."), "shadows"));
        v.push_back(needs(choice("shadow_soft", "Shadow edges", SH, "The sun", { "Sharp", "Smooth", "Soft like sunlight" }), "shadows"));
        v.push_back(needs(slider("shadow_softness", "Shadow softness", SH, "The sun", 0.25f, 3, 0.05f, "%.2f"), "shadow_soft"));
        v.push_back(choice("light_shadows", "Shadows from lamps", SH, "Lamps", { "Off", "2 lights", "4 lights", "8 lights", "16 lights", "32 lights", "64 lights", "128 lights" }, { 0, 2, 4, 8, 16, 32, 64, 128 },
                           "How many lamps cast real shadows at once (light objects first, then lamp blocks). Their shadows are kept and only drawn again when something near them moves or changes, so many lamps stay cheap."));
        v.push_back(needs(slider("light_shadow_distance", "Lamp shadow distance", SH, "Lamps", 16, 160, 4, "%.0f blocks", "", "Lamps this near you get shadow maps (enemies and objects cast shadows from them). They fade out gently toward it, and a lamp keeps its shadow while it stays in reach - no shadows popping as you walk."), "light_shadows"));
        v.push_back(needs(choice("light_shadow_res", "Lamp shadow detail", SH, "Lamps", { "Low (128)", "Normal (256)", "High (512)", "Very high (1024)" }, { 128, 256, 512, 1024 }, "How sharp each lamp's shadow map is. The blocks' shadows are baked once (at loading), so sharp maps cost memory, not speed: up to an eighth of the graphics card's memory is used (it steps down by itself past that)."), "light_shadows"));
        v.push_back(choice("pixel_shadows", "Pixelated shadows", SH, "Style", { "Off (smooth)", "16 pixels a block", "32 pixels a block" }, { 0, 16, 32 },
                           "Shadow edges snap to the textures' pixel grid, so shadows look like part of the pixel art."));
        v.push_back(reset(SH));

        // ---- Water: light through the waves, the shore, the visor
        v.push_back(choice("water_caustics", "Caustics", WA, "Light through the waves", { "Off", "Low", "High" }, {},
                           "The waves gather sunlight into bright, moving lines on everything under the water. High: two layers and a touch of rainbow at their edges."));
        v.push_back(needs(slider("caustic_strength", "Caustics strength", WA, "Light through the waves", 0, 3, 0.05f, "%.2f", "off", "How bright the caustics' lines are (and how dark between them)."), "water_caustics"));
        v.push_back(needs(toggle("water_caustics_above", "Reflected caustics", WA, "Light through the waves", "The sunlight the waves bounce up dances on walls and on the undersides of things just above the water."), "water_caustics"));
        v.push_back(toggle("water_shafts", "Light shafts under water", WA, "Light through the waves", "Under water, beams of sunlight come down through the waves (walls and overhangs cut them off)."));
        v.push_back(toggle("water_foam", "Shore foam and soft edges", WA, "The surface", "Foam where the water meets the shore and laps against it; the water's edge fades softly instead of a hard line."));
        v.push_back(toggle("visor_drops", "Drops on the visor", WA, "The visor", "Coming up out of the water leaves drops on Samus's visor that bend the view and dry off one by one. Rain leaves a few too."));
        v.push_back(reset(WA));

        v.push_back(choice("lamp_blocks", "Lamp blocks", RT, "Real-time lighting", { "Classic (spreads block by block)", "Real light", "Real light and shadows" }, {},
                           "Real light: glowing blocks light things by direction, like lamps, and fall off smoothly. And shadows: walls and pillars block it (through the blocks for every lamp; the nearest lamps also let enemies and objects cast shadows, as many as \"Shadows from lamps\" allows)."));
        v.push_back(slider("lamp_fill", "Lamp fill light", RT, "Real-time lighting", 0, 1, 0.05f, "%", "none", "With real lamp blocks: how much of the classic spreading light stays, as a soft fill that creeps round corners (so shadows are dark, but never pitch black)."));
        v.push_back(slider("lamp_distance", "Lamp light distance", RT, "Real-time lighting", 24, 160, 4, "%.0f blocks", "", "How far away real lamp blocks still shine. They fade out gently toward it (no lights popping off as you walk away)."));
        // ---- Ray tracing ("fake RTX"): rays step block by block through a copy of the world on the graphics card (any graphics card)
        v.push_back(toggle("ray_tracing", "Voxel tracing", RT, "Through the blocks", "Rays step block by block through a copy of the world on the graphics card: reflections of what is off screen and behind you, lamp shadows from every lamp, light probes. Works on any graphics card (no RTX needed). Off: only what is on the screen (screen-space) and shadow maps: faster."));
        v.push_back(needs(toggle("rt_reflections", "Reflections of the blocks", RT, "Through the blocks", "Reflections find the blocks that are not on the screen (behind you, around corners). Off: only what is on screen, else the sky."), "ray_tracing"));
        v.push_back(needs(toggle("rt_objects", "Objects and enemies in reflections", RT, "Through the blocks", "Objects, enemies, the Morph Ball and missiles show up in those reflections too."), "ray_tracing"));
        v.push_back(needs(toggle("rt_lamp_shadows", "Lamp shadows through the blocks", RT, "Through the blocks", "Every real lamp block gets shadows from the walls and pillars between (Lighting > Lamp blocks). Off: only the nearest lamps' shadow maps."), "ray_tracing"));
        v.push_back(needs(choice("probes", "Light probes", RT, "Through the blocks", { "Off", "Wide (every 4 blocks)", "Fine (every 2 blocks)" }, {},
                           "Bounce light everywhere: invisible probes trace rays through the blocks and learn what light reaches them (the sun, the sky, lamps, glowing blocks, coloured walls). No light through walls. Any graphics card."), "ray_tracing"));
        v.push_back(reset(RT));

        // ---- Post-processing: what happens to the finished picture
        v.push_back(choice("anti_alias", "Anti-aliasing", P, "Edges", { "Off", "Smooth edges", "Smoother edges" }, {}, "Smooths the stair-stepped edges of things (the pixels inside textures stay crisp). With PrimeScaler on, it does this itself."));
        v.push_back(slider("bloom", "Glow (bloom)", P, "Light", 0, 1, 0.05f, "%", "off"));
        v.push_back(toggle("eye_adapt", "Eye adaptation", P, "Light", "Like your eyes or a camera: dark places slowly get brighter, bright ones darker (as much as the area's look says)."));
        v.push_back(toggle("lens_flares", "Lens flares", P, "Light", "Looking toward the sun: a glow, a streak and rings of light (if it is not behind something)."));
        v.push_back(toggle("film_fx", "Film grain and lens fringes", P, "Film", "When the area's look has them."));
        v.push_back(slider("motion_blur", "Motion blur", P, "Motion", 0, 1, 0.05f, "%", "off", "The picture smears a little as you turn and move, like a camera. The helmet and the arm cannon stay sharp. Looks the same at any frame rate."));
        v.push_back(reset(P));

        v.push_back(slider("sensitivity", "Mouse sensitivity", C, "Mouse", 0.1f, 3, 0.05f, "%.2f"));
        v.push_back(toggle("invert_y", "Invert up / down", C, "Mouse", "Moving the mouse up looks down."));
        v.push_back(choice("lock_mode", "Lock-on button", C, "Mouse", { "As the game says", "Hold it", "Press to lock, press to let go" }));
        for (int a = 0; a < (int)Act::COUNT; a++) { OptionDef d; d.key = std::string("key_") + actKey((Act)a); d.label = actName((Act)a); d.group = C; d.section = "Keys"; d.type = OptType::Key; d.act = a; v.push_back(d); }
        v.push_back(reset(C));
        return v;
    }();
    return defs;
}

int findOption(const std::string& key) {
    const auto& d = optionDefs(); for (int i = 0; i < (int)d.size(); i++) if (d[i].key == key) return i; return -1;
}

float optionGet(const Settings& s, const OptionDef& d) {
    if (d.type == OptType::Preset) return gamePresets ? (float)matchingPreset(s, *gamePresets) : -1.f;
    Field f = field(const_cast<Settings&>(s), d.key); float v = readField(f);
    if (d.type == OptType::Choice && !d.values.empty()) {
        int best = 0; for (int i = 1; i < (int)d.values.size(); i++) if (std::fabs(d.values[i] - v) < std::fabs(d.values[best] - v)) best = i;
        return (float)best;
    }
    return v;
}

void optionSet(Settings& s, const OptionDef& d, float v) {
    Field f = field(s, d.key);
    switch (d.type) {
    case OptType::Slider: writeField(f, std::clamp(v, d.lo, d.hi)); break;
    case OptType::Toggle: writeField(f, v > 0.5f ? 1.f : 0.f); break;
    case OptType::Choice: {
        int n = (int)d.choices.size(); int i = std::clamp((int)std::lround(v), 0, std::max(0, n - 1));
        writeField(f, d.values.empty() ? (float)i : d.values[i]); break; }
    default: break;
    }
}

void optionStep(Settings& s, const OptionDef& d, int dir) {
    if (d.type == OptType::Preset) {   // the next / previous preset (from Custom: the nearest end)
        if (!gamePresets || gamePresets->empty()) return;
        int n = (int)gamePresets->size(), cur = matchingPreset(s, *gamePresets);
        int to = cur < 0 ? (dir > 0 ? 0 : n - 1) : std::clamp(cur + dir, 0, n - 1);
        applyPreset(s, (*gamePresets)[to]); return;
    }
    float v = optionGet(s, d);
    switch (d.type) {
    case OptType::Slider: { float n = std::round((v + dir * d.step - d.lo) / d.step) * d.step + d.lo; optionSet(s, d, n); break; }
    case OptType::Toggle: optionSet(s, d, v > 0.5f ? 0.f : 1.f); break;
    case OptType::Choice: optionSet(s, d, v + dir); break;
    default: break;
    }
}

std::string optionText(const Settings& s, const OptionDef& d) {
    if (d.type == OptType::Preset) { int i = gamePresets ? matchingPreset(s, *gamePresets) : -1; return i >= 0 ? (*gamePresets)[i].name : "Custom"; }
    float v = optionGet(s, d); char b[64];
    switch (d.type) {
    case OptType::Toggle: return v > 0.5f ? "On" : "Off";
    case OptType::Choice: { int i = (int)std::lround(v); return i >= 0 && i < (int)d.choices.size() ? d.choices[i] : "?"; }
    case OptType::Slider:
        if (!d.zero.empty() && v <= d.lo + 1e-4f) return d.zero;
        if (d.fmt == "%") { std::snprintf(b, sizeof b, "%d%%", (int)std::lround(v * 100)); return b; }
        std::snprintf(b, sizeof b, d.fmt.c_str(), v); return b;
    default: return "";
    }
}

bool optionShown(const Settings& s, const OptionDef& d, bool inGame) {
    if (inGame && !d.inGame) return false;
    if (!d.needs.empty()) { int n = findOption(d.needs); if (n >= 0 && optionGet(s, optionDefs()[n]) <= 0) return false; }
    return true;
}

void optionReset(Settings& s, const std::string& group) {
    Settings fresh;
    for (const OptionDef& d : optionDefs()) {
        if (d.group != group || !d.inGame) continue;
        if (d.type == OptType::Key) s.keys.key[d.act][0] = fresh.keys.key[d.act][0], s.keys.key[d.act][1] = fresh.keys.key[d.act][1];
        else if (d.type != OptType::Reset && d.type != OptType::Preset) writeField(field(s, d.key), readField(field(fresh, d.key)));
    }
}

// ------------------------------------------------------------------ quality presets
std::vector<QualityPreset>* gamePresets = nullptr;
const std::vector<std::string>& presetKeys() {
    static const std::vector<std::string> k = { "render_scale", "render_distance", "vertical_distance", "shadows", "shadow_res", "shadow_distance", "shadow_soft", "light_shadows",
        "probes", "bounce", "ao", "reflections", "reflect_res", "planar_floors", "anti_alias", "bloom", "light_fog", "clouds", "cloud_shadows", "bumps", "coat", "swaying", "lens_flares", "upscaler", "lamp_blocks", "ray_tracing", "water_caustics", "water_shafts" };
    return k;
}
std::vector<QualityPreset> defaultPresets() {
    // render_scale, render_distance, vertical_distance, shadows, shadow_res, shadow_distance, shadow_soft, light_shadows, probes, bounce, ao, reflections, reflect_res,
    // planar_floors, anti_alias, bloom, light_fog, clouds, cloud_shadows, bumps, coat, swaying, lens_flares, upscaler, lamp_blocks, ray_tracing, water_caustics, water_shafts
    struct Row { const char* name; float v[28]; };
    static const Row rows[] = {
        { "Very low", { 0.5f, 6, 3, 0, 1024, 48, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0.3f, 0, 0.3f, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 } },
        { "Low",      { 0.58f, 8, 4, 1, 1024, 64, 0, 0, 0, 0, 1, 0, 2, 0, 0, 0.5f, 0, 0.5f, 0, 0.5f, 0.5f, 1, 0, 1, 0, 0, 1, 0 } },
        { "Medium",   { 0.67f, 10, 5, 1, 2048, 96, 1, 2, 0, 1, 1, 1, 2, 0, 0, 0.7f, 1, 0.6f, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 } },
        { "High",     { 0.85f, 14, 6, 1, 2048, 160, 2, 4, 1, 1, 1, 1, 1, 1, 0, 0.8f, 1, 0.7f, 1, 1, 1, 1, 1, 1, 2, 1, 2, 1 } },
        { "Ultra",    { 1, 20, 8, 1, 3072, 256, 2, 16, 2, 2, 2, 2, 0, 1, 0, 0.8f, 1, 0.75f, 1, 1, 1, 1, 1, 1, 2, 1, 2, 1 } },
        { "Prime",    { 1.5f, 28, 10, 1, 4096, 384, 2, 32, 2, 2, 2, 2, 0, 1, 0, 0.85f, 1, 0.8f, 1, 1.2f, 1.2f, 1, 1, 1, 2, 1, 2, 1 } },
    };
    std::vector<QualityPreset> out;
    for (const Row& r : rows) { QualityPreset p; p.name = r.name; for (size_t k = 0; k < presetKeys().size(); k++) p.values.push_back({ presetKeys()[k], r.v[k] }); out.push_back(p); }
    return out;
}
QualityPreset presetFrom(const Settings& s, const std::string& name) {
    QualityPreset p; p.name = name;
    for (const std::string& k : presetKeys()) p.values.push_back({ k, readField(field(const_cast<Settings&>(s), k)) });
    return p;
}
void applyPreset(Settings& s, const QualityPreset& p) {
    for (const auto& kv : p.values) {
        int i = findOption(kv.first);
        if (i < 0) { writeField(field(s, kv.first), kv.second); continue; }
        const OptionDef& d = optionDefs()[i];
        if (d.type == OptType::Choice && !d.values.empty()) {   // choices with their own values: the nearest one
            int best = 0; for (int j = 1; j < (int)d.values.size(); j++) if (std::fabs(d.values[j] - kv.second) < std::fabs(d.values[best] - kv.second)) best = j;
            optionSet(s, d, (float)best);
        } else optionSet(s, d, kv.second);
    }
}
int matchingPreset(const Settings& s, const std::vector<QualityPreset>& ps) {
    for (int i = 0; i < (int)ps.size(); i++) {
        bool same = true;
        for (const auto& kv : ps[i].values) { float v = readField(field(const_cast<Settings&>(s), kv.first)); if (std::fabs(v - kv.second) > 1e-3f * std::max(1.f, std::fabs(kv.second))) { same = false; break; } }
        if (same) return i;
    }
    return -1;
}
static std::string pesc(const std::string& s) { std::string o; for (char c : s) { if (c == '%') o += "%25"; else if (c == ' ') o += "%20"; else o += c; } return o.empty() ? "-" : o; }
static std::string punesc(const std::string& s) { if (s == "-") return ""; std::string o; for (size_t i = 0; i < s.size(); i++) { if (s[i] == '%' && i + 2 < s.size()) { o += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16); i += 2; } else o += s[i]; } return o; }
bool savePresets(const std::string& path, const std::vector<QualityPreset>& ps) {
    std::ofstream f(path); if (!f) return false;
    f << "presets 1\n";
    for (const QualityPreset& p : ps) { f << "preset " << pesc(p.name); for (const auto& kv : p.values) f << " " << kv.first << "=" << kv.second; f << "\n"; }
    return (bool)f;
}
bool loadPresets(const std::string& path, std::vector<QualityPreset>& ps) {
    std::ifstream f(path); if (!f) return false;
    std::vector<QualityPreset> out; std::string line;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k, n; l >> k; if (k != "preset" || !(l >> n)) continue;
        QualityPreset p; p.name = punesc(n); std::string kv;
        while (l >> kv) { size_t e = kv.find('='); if (e != std::string::npos) p.values.push_back({ kv.substr(0, e), (float)std::atof(kv.c_str() + e + 1) }); }
        out.push_back(p);
    }
    if (out.empty()) return false;
    ps = out; return true;
}

} // namespace mc
