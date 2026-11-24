#include "game.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace mc {

// ------------------------------------------------------------------ keys
const char* actName(Act a) {
    static const char* n[] = { "Move forward", "Move back", "Move left", "Move right", "Jump", "Sprint", "Walk slowly", "Fire / bomb", "Missile / Power Bomb",
                               "Lock-on / scan", "Use", "Next visor", "Morph Ball", "Next missile kind", "Map", "Grapple Beam" };
    int i = (int)a; return i >= 0 && i < (int)Act::COUNT ? n[i] : "?";
}
const char* actKey(Act a) {
    static const char* n[] = { "forward", "back", "left", "right", "jump", "sprint", "walk", "fire", "missile", "lockon", "use", "visor", "ball", "next_missile", "map", "grapple" };
    int i = (int)a; return i >= 0 && i < (int)Act::COUNT ? n[i] : "?";
}
KeyBinds::KeyBinds() {
    static const int d[(int)Act::COUNT][2] = { { 'W', 0x26 }, { 'S', 0x28 }, { 'A', 0x25 }, { 'D', 0x27 }, { 0x20, 0 }, { 0x11, 0 }, { 0x10, 0 }, { 1, 0 }, { 'F', 0 },
                                               { 2, 0 }, { 'E', 0 }, { 'V', 0 }, { 'C', 0 }, { 'Q', 0 }, { 'M', 0 }, { 'G', 0 } };
    for (int i = 0; i < (int)Act::COUNT; i++) { key[i][0] = d[i][0]; key[i][1] = d[i][1]; }
}
void KeyBinds::set(Act a, int slot, int vk) {
    int i = (int)a; if (i < 0 || i >= (int)Act::COUNT || slot < 0 || slot > 1) return;
    if (vk) for (int j = 0; j < (int)Act::COUNT; j++) for (int s = 0; s < 2; s++) if (key[j][s] == vk && !(j == i && s == slot)) key[j][s] = 0;
    key[i][slot] = vk;
    for (auto& k : key) if (!k[0] && k[1]) { k[0] = k[1]; k[1] = 0; }   // the first slot is never empty while the second is not
}
const char* keyNameFallback(int vk) {
    switch (vk) {
    case 0: return "-"; case 1: return "Left mouse"; case 2: return "Right mouse"; case 4: return "Middle mouse"; case 5: return "Mouse 4"; case 6: return "Mouse 5";
    case 0x08: return "Backspace"; case 0x09: return "Tab"; case 0x0D: return "Enter"; case 0x10: return "Shift"; case 0x11: return "Ctrl"; case 0x12: return "Alt";
    case 0x14: return "Caps Lock"; case 0x1B: return "Esc"; case 0x20: return "Space"; case 0x21: return "Page Up"; case 0x22: return "Page Down"; case 0x23: return "End"; case 0x24: return "Home";
    case 0x25: return "Left"; case 0x26: return "Up"; case 0x27: return "Right"; case 0x28: return "Down"; case 0x2D: return "Insert"; case 0x2E: return "Delete";
    case 0xA0: return "Left Shift"; case 0xA1: return "Right Shift"; case 0xA2: return "Left Ctrl"; case 0xA3: return "Right Ctrl"; case 0xA4: return "Left Alt"; case 0xA5: return "Alt Gr";
    default: break;
    }
    static char buf[8][8]; static int at = 0; char* b = buf[at = (at + 1) % 8];
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) { b[0] = (char)vk; b[1] = 0; return b; }
    if (vk >= 0x70 && vk <= 0x87) { std::snprintf(b, 8, "F%d", vk - 0x6F); return b; }
    if (vk >= 0x60 && vk <= 0x69) { std::snprintf(b, 8, "Num %d", vk - 0x60); return b; }
    return "";
}

// ------------------------------------------------------------------ settings
bool Settings::load(const std::string& path) {
    std::ifstream f(path); if (!f) return false;
    std::string k; bool hadRes = false;
    while (f >> k) {
        if (k == "render_distance") f >> renderDistance; else if (k == "vertical_distance") f >> verticalDistance; else if (k == "distant_detail") f >> distantDetail; else if (k == "target_fps") f >> targetFps; else if (k == "hide_hidden") { int v; f >> v; hideHidden = v != 0; }
        else if (k == "fov") f >> fov; else if (k == "mouse_sensitivity") f >> mouseSensitivity; else if (k == "sound_volume") f >> soundVolume; else if (k == "ui_scale") f >> uiScale;
        else if (k == "vsync") { int v; f >> v; vsync = v != 0; }
        else if (k == "block_preview") { int v; f >> v; blockPreview = v != 0; }
        else if (k == "time_of_day") f >> timeOfDay;
        else if (k == "day_cycle") { int v; f >> v; dayCycle = v != 0; }
        else if (k == "shadows") f >> shadowQuality;
        else if (k == "shadow_distance") f >> shadowDistance;
        else if (k == "shadow_soft") f >> shadowSoft; else if (k == "shadow_softness") f >> shadowSoftness;
        else if (k == "cloud_cover") f >> cloudCover;
        else if (k == "ground_fog") f >> groundFog;
        else if (k == "cloud_shadows") { int v; f >> v; cloudShadows = v != 0; }
        else if (k == "bloom") f >> bloom;
        else if (k == "swaying") { int v; f >> v; swaying = v != 0; }
        else if (k == "reflections") f >> reflections;
        else if (k == "reflect_res") { f >> reflectRes; reflectRes = std::clamp(reflectRes, 0, 3); }
        else if (k == "anti_alias") { f >> antiAlias; antiAlias = std::clamp(antiAlias, 0, 2); }
        else if (k == "render_scale") { f >> renderScale; renderScale = std::clamp(renderScale, 0.25f, 2.f); }
        else if (k == "upscaler") { f >> upscaler; upscaler = std::clamp(upscaler, 0, 1); }
        else if (k == "up_sharpness") { f >> upSharp; upSharp = std::clamp(upSharp, 0.f, 1.f); }
        else if (k == "pixel_shadows") { f >> pixelShadows; if (pixelShadows != 16 && pixelShadows != 32) pixelShadows = 0; }
        else if (k == "probes") { f >> probes; probes = std::clamp(probes, 0, 2); }
        else if (k == "show_fps") { int v = 0; f >> v; showFps = v != 0; }
        else if (k == "ambient_occlusion") f >> ambientOcclusion;
        else if (k == "ao_strength") f >> aoStrength;
        else if (k == "bumps") f >> bumps; else if (k == "coat") f >> coat;
        else if (k == "materials") { int v; f >> v; materials = v != 0; }
        else if (k == "hud_glow") { int v; f >> v; hudGlow = v != 0; }
        else if (k == "eye_adapt") { int v; f >> v; eyeAdapt = v != 0; }
        else if (k == "film_fx") { int v; f >> v; filmFx = v != 0; }
        else if (k == "ray_tracing") { int v; f >> v; rayTracing = v != 0; }
        else if (k == "rt_reflections") { int v; f >> v; rtReflect = v != 0; }
        else if (k == "rt_objects") { int v; f >> v; rtObjects = v != 0; }
        else if (k == "rt_lamp_shadows") { int v; f >> v; rtShadows = v != 0; }
        else if (k == "lamp_blocks") { f >> lampBlocks; lampBlocks = std::clamp(lampBlocks, 0, 2); }
        else if (k == "lamp_fill") { f >> lampFill; lampFill = std::clamp(lampFill, 0.f, 1.f); }
        else if (k == "caustic_strength") { f >> causticStrength; causticStrength = std::clamp(causticStrength, 0.f, 3.f); }
        else if (k == "godrays") { f >> godrays; godrays = std::clamp(godrays, 0.f, 3.f); }
        else if (k == "water") { int a = 2, b = 1, c = 1, d = 1, e = 1; f >> a >> b >> c >> d >> e; waterCaustics = std::clamp(a, 0, 2); waterCausticsAbove = b != 0; waterShafts = c != 0; waterFoam = d != 0; visorDrops = e != 0; }
        else if (k == "motion_blur") { f >> motionBlur; motionBlur = std::clamp(motionBlur, 0.f, 1.f); }
        else if (k == "light_shadows") { f >> lightShadows; lightShadows = std::clamp(lightShadows, 0, 128); }
        else if (k == "light_shadow_distance") { f >> lightShadowDist; lightShadowDist = std::clamp(lightShadowDist, 8.f, 256.f); }
        else if (k == "light_shadow_res") { f >> lightShadowRes; lightShadowRes = lightShadowRes <= 128 ? 128 : lightShadowRes <= 256 ? 256 : 512; }
        else if (k == "lamp_distance") { f >> lampDistance; lampDistance = std::clamp(lampDistance, 16.f, 256.f); }
        else if (k == "light_fog") { int v; f >> v; lightFog = v != 0; }
        else if (k == "fog_grid") { int v; f >> v; fogGrid = v != 0; }
        else if (k == "lava_light") f >> lavaLight;
        else if (k == "bounce") f >> bounce;
        else if (k == "planar_floors") { int v; f >> v; planarFloors = v != 0; }
        else if (k == "lens_flares") { int v; f >> v; lensFlares = v != 0; }
        else if (k == "guess_materials") { int v; f >> v; guessMaterials = v != 0; }
        else if (k == "shadow_res") { f >> shadowRes; hadRes = true; }
        else if (k == "brightness") f >> brightness;
        else if (k == "fullscreen") { int v; f >> v; fullscreen = v != 0; }
        else if (k == "volumes") f >> volWeapons >> volMovement >> volEnemies >> volWorld;
        else if (k == "volumes2") f >> volMenus >> volMusic;
        else if (k == "invert_y") { int v; f >> v; invertY = v != 0; }
        else if (k == "lock_mode") f >> lockMode;
        else if (k == "cave_culling") { int v; f >> v; caveCulling = v != 0; }
        else if (k == "screen_shake") f >> screenShake;
        else if (k == "key") {
            std::string n; int a = 0, b = 0; f >> n >> a >> b;
            for (int i = 0; i < (int)Act::COUNT; i++) if (n == actKey((Act)i)) { keys.key[i][0] = a; keys.key[i][1] = b; }
        }
        else { std::string rest; std::getline(f, rest); }
    }
    if (!hadRes) shadowRes = shadowQuality == 1 ? 1536 : 2048;   // (older files: "normal" shadows were 1536, "high" 2048)
    shadowQuality = std::max(0, std::min(2, shadowQuality)); shadowRes = std::max(512, std::min(4096, shadowRes));
    brightness = std::max(0.3f, std::min(2.5f, brightness)); screenShake = std::max(0.f, std::min(2.f, screenShake)); lockMode = std::max(0, std::min(2, lockMode));
    renderDistance = std::max(2, std::min(48, renderDistance)); verticalDistance = std::max(1, std::min(16, verticalDistance)); distantDetail = std::max(0, std::min(3, distantDetail));
    return true;
}
bool Settings::save(const std::string& path) const {
    std::ofstream f(path); if (!f) return false;
    f << "render_distance " << renderDistance << "\nvertical_distance " << verticalDistance << "\ndistant_detail " << distantDetail << "\ntarget_fps " << targetFps << "\nhide_hidden " << (hideHidden ? 1 : 0) << "\nfov " << fov << "\nmouse_sensitivity " << mouseSensitivity << "\nsound_volume " << soundVolume
      << "\nui_scale " << uiScale << "\nvsync " << (vsync ? 1 : 0) << "\nblock_preview " << (blockPreview ? 1 : 0)
      << "\ntime_of_day " << timeOfDay << "\nday_cycle " << (dayCycle ? 1 : 0) << "\nshadows " << shadowQuality << "\nshadow_distance " << shadowDistance << "\nshadow_soft " << shadowSoft << "\nshadow_softness " << shadowSoftness
      << "\ncloud_cover " << cloudCover << "\nground_fog " << groundFog << "\ncloud_shadows " << (cloudShadows ? 1 : 0)
      << "\nbloom " << bloom << "\nswaying " << (swaying ? 1 : 0) << "\nreflections " << reflections << "\nreflect_res " << reflectRes << "\nanti_alias " << antiAlias << "\nrender_scale " << renderScale << "\nupscaler " << upscaler << "\nup_sharpness " << upSharp << "\npixel_shadows " << pixelShadows << "\nprobes " << probes << "\nshow_fps " << (showFps ? 1 : 0)
      << "\nambient_occlusion " << ambientOcclusion << "\nao_strength " << aoStrength
      << "\nbumps " << bumps << "\ncoat " << coat << "\nmaterials " << (materials ? 1 : 0) << "\nhud_glow " << (hudGlow ? 1 : 0) << "\nguess_materials " << (guessMaterials ? 1 : 0)
      << "\nshadow_res " << shadowRes << "\nbrightness " << brightness << "\nfullscreen " << (fullscreen ? 1 : 0)
      << "\nvolumes " << volWeapons << " " << volMovement << " " << volEnemies << " " << volWorld << "\nvolumes2 " << volMenus << " " << volMusic << "\ninvert_y " << (invertY ? 1 : 0) << "\nlock_mode " << lockMode << "\ncave_culling " << (caveCulling ? 1 : 0) << "\nscreen_shake " << screenShake << "\neye_adapt " << (eyeAdapt ? 1 : 0) << "\nfilm_fx " << (filmFx ? 1 : 0) << "\nlight_shadows " << lightShadows << "\nlight_shadow_distance " << lightShadowDist << "\nlight_shadow_res " << lightShadowRes << "\nlamp_distance " << lampDistance << "\nlight_fog " << (lightFog ? 1 : 0) << "\nfog_grid " << (fogGrid ? 1 : 0) << "\nlava_light " << lavaLight << "\nbounce " << bounce << "\nplanar_floors " << (planarFloors ? 1 : 0) << "\nlens_flares " << (lensFlares ? 1 : 0) << "\nmotion_blur " << motionBlur << "\nray_tracing " << (rayTracing ? 1 : 0) << "\nrt_reflections " << (rtReflect ? 1 : 0) << "\nrt_objects " << (rtObjects ? 1 : 0) << "\nrt_lamp_shadows " << (rtShadows ? 1 : 0) << "\nlamp_blocks " << lampBlocks << "\nlamp_fill " << lampFill << "\nwater " << waterCaustics << " " << (waterCausticsAbove ? 1 : 0) << " " << (waterShafts ? 1 : 0) << " " << (waterFoam ? 1 : 0) << " " << (visorDrops ? 1 : 0) << "\ncaustic_strength " << causticStrength << "\ngodrays " << godrays << "\n";
    for (int i = 0; i < (int)Act::COUNT; i++) f << "key " << actKey((Act)i) << " " << keys.key[i][0] << " " << keys.key[i][1] << "\n";
    return (bool)f;
}

// ------------------------------------------------------------------ player (creative mode)
void Player::applySize(const PlayerRules& r) {
    height = std::clamp((double)r.height, 0.2, 8.0); halfW = std::clamp((double)r.width, 0.1, 8.0) * 0.5;
    eyeH = std::clamp((double)r.eyeHeight, 0.05, height - 0.02); stepH = std::clamp((double)r.stepHeight, 0.0, height * 0.75);
    if (ball) { double b = std::clamp((double)r.ballSize, 0.1, 4.0); height = b; halfW = b * 0.49; eyeH = b * 0.5; stepH = std::min(stepH, b * 0.3); }
}
void Player::look(double dx, double dy) {
    yaw += dx; pitch = std::max(-1.55, std::min(1.55, pitch - dy));
}

void Player::update(const World& w, const Controls& c, double dt, const PlayerRules& r) {
    V3 fw{ std::sin(yaw), 0, std::cos(yaw) }, rt{ std::cos(yaw), 0, -std::sin(yaw) }, mv{ 0, 0, 0 };
    if (c.forward) mv += fw;
    if (c.back) mv = mv - fw;
    if (c.right) mv += rt;
    if (c.left) mv = mv - rt;
    double speed = flying ? (c.sprint ? 21.6 : 10.9) : (c.sneak ? r.walkSpeed * 0.3 : (c.sprint && r.canSprint) ? r.sprintSpeed : r.walkSpeed);   // blocks per second
    bool jumpEdge = c.jump && !jumpPrev; jumpPrev = c.jump;
    jumpedOut = false;
    if (!flying && !ball && r.speedBooster) {   // Speed Booster: keep running (sprinting, if there is sprint) and it kicks in
        bool running = length(mv) > 0 && !c.sneak && (c.sprint || !r.canSprint) && c.forward;
        if (!running) runTime = 0; else if (onGround || boosting) runTime += (float)dt;
        boosting = running && runTime >= r.boostAfter;
        if (boosting) speed *= r.boostSpeed;
    } else { runTime = 0; boosting = false; }
    if (length(mv) > 0) mv = normalize(mv) * speed;
    if (c.orbit && !flying && (c.left != c.right) && !c.forward && !c.back) {   // lock-on: around the target, keeping the distance
        V3 p{ feet.x - c.orbitAround.x, 0, feet.z - c.orbitAround.z }; double rr = std::sqrt(p.x * p.x + p.z * p.z);
        if (rr > 0.3 && dt > 1e-6) {
            double th = (c.right ? 1 : -1) * speed * dt / rr, cs = std::cos(th), sn = std::sin(th);
            V3 q{ p.x * cs - p.z * sn, 0, p.x * sn + p.z * cs };
            mv = (q - p) * (1 / dt);
        }
    }
    if (flying) {
        // flying: quick to start and stop, Space up, Shift down
        double k = std::min(1.0, dt * 12);
        vel.x += (mv.x - vel.x) * k; vel.z += (mv.z - vel.z) * k;
        double vy = (c.jump ? 7.5 : 0) - (c.sneak ? 7.5 : 0);
        vel.y += (vy - vel.y) * k;
    } else if (int liq = r.freeInLiquid ? 0 : w.liquidAt({ feet.x, feet.y + 0.6, feet.z })) {   // (the Gravity Suit moves as on land)
        // swimming: slow, you sink a little, Space swims up; lava is much thicker
        double k = liq == 2 ? 0.3 : 0.55;
        vel.x = mv.x * k; vel.z = mv.z * k;
        vel.y = std::max(vel.y - (liq == 2 ? 3.0 : 6.0) * dt, liq == 2 ? -0.8 : -2.2);
        double swimUp = liq == 2 ? 1.2 : 3.2;
        if (c.jump && vel.y < swimUp) vel.y = std::min(vel.y + (liq == 2 ? 10.0 : 22.0) * dt, swimUp);   // (never slows down a jump out)
        if (c.sneak) vel.y = std::max(vel.y - 10.0 * dt, -3.0);
        bool headOut = !w.liquidAt(eye()), wall = false;
        if (c.jump && headOut && length(mv) > 0) {   // swimming into a ledge with Space held: climb out on its own
            V3 d = normalize(mv) * (halfW + 0.2);
            wall = w.solidAt((int)std::floor(feet.x + d.x), (int)std::floor(feet.y + 0.7), (int)std::floor(feet.z + d.z));
        }
        if (c.jump && headOut && (jumpEdge || onGround || wall) && vel.y < 3.5) {   // head above the surface: jump out (onto a ledge, too)
            double h = std::max(0.1f, r.jumpHeight) + 0.75;   // a normal jump, from the feet's depth under the surface (a ledge one block up is in reach)
            vel.y = std::sqrt(2 * r.gravity * h) * (liq == 2 ? 0.7 : 1.0);
            jumpedOut = true;
        }
    } else if (ball) {
        // the morph ball: rolls up to speed and keeps rolling a while when you let go; no jumping (bombs throw it up)
        if (length(mv) > 0) mv = normalize(mv) * (double)r.ballSpeed;
        double k = std::min(1.0, dt * (length(mv) > 0 ? 5.0 : 1.8));
        vel.x += (mv.x - vel.x) * k; vel.z += (mv.z - vel.z) * k;
        vel.y = std::max(vel.y - (double)r.gravity * dt, -78.0);
        if (r.springBall && jumpEdge && onGround) vel.y = std::sqrt(2 * r.gravity * std::max(0.1f, r.springJump));   // the Spring Ball
    } else {
        {   // on foot: gets up to speed and slows down over a moment (not in one frame); in the air you steer less (momentum, like Prime)
            bool moving = length(mv) > 0;
            double accRate = std::max(speed, 0.1) / std::max(1e-3, (double)r.accelTime), stopRate = std::max((double)r.walkSpeed, 0.1) / std::max(1e-3, (double)r.stopTime);
            double rate = moving ? accRate : stopRate;
            if (moving && vel.x * mv.x + vel.z * mv.z < 0) rate = std::max(accRate, stopRate);   // (turning back: as quick as stopping)
            if ((moving && r.accelTime <= 1e-3) || (!moving && r.stopTime <= 1e-3)) rate = 1e9;   // (0: at once)
            if (!onGround) rate *= std::clamp((double)r.airControl, 0.0, 1.0);
            double dx = mv.x - vel.x, dz = mv.z - vel.z, dl = std::sqrt(dx * dx + dz * dz), most = rate * dt;
            if (dl > most && dl > 1e-9) { dx *= most / dl; dz *= most / dl; }
            vel.x += dx; vel.z += dz;
        }
        if (dashT > 0) { vel.x = dashVel.x + mv.x * 0.15; vel.z = dashVel.z + mv.z * 0.15; dashT -= dt; }   // a dash: sideways until you land
        vel.y = std::max(vel.y - (double)r.gravity * dt, -78.0);
        if (onGround) airJumpsUsed = 0;
        int wade = r.freeInLiquid || onGround ? 0 : w.liquidAt({ feet.x, feet.y + 0.15, feet.z });   // bobbing at the surface (between swim strokes)
        if (c.jump && onGround) vel.y = r.jumpSpeed();   // default about 1.25 blocks high
        else if (wade && jumpEdge && vel.y < 3.5 && !w.liquidAt(eye())) {   // a jump out of the water, as when swimming (not a Space Jump)
            vel.y = std::sqrt(2 * r.gravity * (std::max(0.1f, r.jumpHeight) + 0.75)) * (wade == 2 ? 0.7 : 1.0); jumpedOut = true;
        } else if (!onGround && jumpEdge && airJumpsUsed < r.airJumps) { vel.y = r.jumpSpeed(); airJumpsUsed++; }   // Space Jump: again in the air
    }
    feet = w.moveBox(feet, vel, halfW, height, dt, onGround, flying ? 0.0 : stepH);   // walking climbs half blocks and stairs by itself
    if (dashT > 0) { if (!onGround) dashAir = true; else if (dashAir) dashT = 0; }   // the dash ends on landing
    if (flying || ball) dashT = 0;
    if (flying && onGround && c.sneak) flying = false;   // land by flying down onto the ground
}

// ------------------------------------------------------------------ undo / redo
void EditHistory::push(std::vector<Edit>&& g) {
    if (g.empty()) return;
    undos.push_back(std::move(g)); if (undos.size() > 1000) undos.erase(undos.begin());
    redos.clear();
}
bool EditHistory::undo(World& w) {
    if (undos.empty()) return false;
    auto g = std::move(undos.back()); undos.pop_back();
    for (auto it = g.rbegin(); it != g.rend(); ++it) w.set(it->x, it->y, it->z, it->before);
    redos.push_back(std::move(g)); return true;
}
bool EditHistory::redo(World& w) {
    if (redos.empty()) return false;
    auto g = std::move(redos.back()); redos.pop_back();
    for (auto& e : g) w.set(e.x, e.y, e.z, e.after);
    undos.push_back(std::move(g)); return true;
}

// ------------------------------------------------------------------ game settings (game/game.txt)
bool GameSettings::save(const std::string& path) const {
    std::ofstream f(path); if (!f) return false;
    const PlayerRules& p = player;
    f << "name " << name << "\nstart_world " << (startWorld.empty() ? "-" : startWorld) << "\nlast_world " << (lastWorld.empty() ? "-" : lastWorld)
      << "\nplayer_width " << p.width << "\nplayer_height " << p.height << "\neye_height " << p.eyeHeight << "\nstep_height " << p.stepHeight << "\nball_size " << p.ballSize
      << "\nwalk_speed " << p.walkSpeed << "\nsprint_speed " << p.sprintSpeed << "\nmove_feel " << p.accelTime << " " << p.stopTime << " " << p.airControl << "\njump_height " << p.jumpHeight << "\ngravity " << p.gravity
      << "\nmax_health " << p.maxHealth << "\nlava_damage " << p.lavaDamage << "\nvoid_depth " << p.voidDepth << "\nrespawn_delay " << p.respawnDelay
      << "\nfall_damage " << p.fallDamage << "\ncan_sprint " << (p.canSprint ? 1 : 0)
      << "\nmorph_fx " << p.morphColor << " " << p.morphFx << " " << (p.morphHudFx ? 1 : 0) << "\nbounce_damage " << p.bounceDamage << "\nquick_save " << (p.quickSave ? 1 : 0)
      << "\nball_axle " << p.ballAxle.x << " " << p.ballAxle.y << " " << p.ballAxle.z << " " << p.ballBalance << " " << p.ballPivot.x << " " << p.ballPivot.y << " " << p.ballPivot.z
      << "\nball_trail " << (p.ballTrail ? 1 : 0) << " " << p.trailPoint.x << " " << p.trailPoint.y << " " << p.trailPoint.z << " " << p.trailColor << " " << p.trailSize << " " << p.trailLife
      << "\nball_ribbon " << p.trailStyle << " " << p.ribbonWidth << " " << p.ribbonTaper << " " << p.ribbonGlow << " " << (p.ribbonStreaks ? 1 : 0) << " " << p.ribbonFacing
      << "\ndeath_fx " << p.deathFx << " " << p.deathHit << " " << p.deathStatic << " " << p.deathShutdown << " " << p.deathOff << " " << p.deathSlowMo << " " << p.deathDrop << " " << p.deathTvOff << " " << p.deathSkip << " " << p.deathStaticAmount << " " << p.deathStaticColor
      << "\nmorph_ball " << p.morphBall << "\nbombs " << p.bombs << "\nball_speed " << p.ballSpeed << "\nbomb_jump " << p.bombJump << "\nbombs_at_once " << p.bombsAtOnce
      << "\nbomb_fuse " << p.bombFuse << "\nbomb_reach " << p.bombReach << "\nbomb_sphere " << p.bombSphere << "\nball_model " << (p.ballModel.empty() ? "-" : p.ballModel) << "\n";
    return (bool)f;
}
bool GameSettings::load(const std::string& path) {
    std::ifstream f(path); if (!f) return false;
    std::string k; PlayerRules& p = player;
    while (f >> k) {
        if (k == "name") { f >> std::ws; std::getline(f, name); }
        else if (k == "start_world") { f >> std::ws; std::getline(f, startWorld); if (startWorld == "-") startWorld.clear(); }
        else if (k == "last_world") { f >> std::ws; std::getline(f, lastWorld); if (lastWorld == "-") lastWorld.clear(); }
        else if (k == "player_width") f >> p.width; else if (k == "player_height") f >> p.height; else if (k == "eye_height") f >> p.eyeHeight;
        else if (k == "step_height") f >> p.stepHeight; else if (k == "ball_size") f >> p.ballSize;
        else if (k == "walk_speed") f >> p.walkSpeed; else if (k == "sprint_speed") f >> p.sprintSpeed; else if (k == "jump_height") f >> p.jumpHeight;
        else if (k == "gravity") f >> p.gravity; else if (k == "max_health") f >> p.maxHealth; else if (k == "lava_damage") f >> p.lavaDamage;
        else if (k == "void_depth") f >> p.voidDepth; else if (k == "respawn_delay") f >> p.respawnDelay; else if (k == "fall_damage") f >> p.fallDamage;
        else if (k == "can_sprint") { int v; f >> v; p.canSprint = v != 0; }
        else if (k == "move_feel") { f >> p.accelTime >> p.stopTime >> p.airControl; p.accelTime = std::clamp(p.accelTime, 0.f, 2.f); p.stopTime = std::clamp(p.stopTime, 0.f, 2.f); p.airControl = std::clamp(p.airControl, 0.f, 1.f); }
        else if (k == "bounce_damage") f >> p.bounceDamage;
        else if (k == "quick_save") { int v = 0; f >> v; p.quickSave = v != 0; }
        else if (k == "ball_axle") f >> p.ballAxle.x >> p.ballAxle.y >> p.ballAxle.z >> p.ballBalance >> p.ballPivot.x >> p.ballPivot.y >> p.ballPivot.z;
        else if (k == "ball_ribbon") { int st = 1; f >> p.trailStyle >> p.ribbonWidth >> p.ribbonTaper >> p.ribbonGlow >> st; p.ribbonStreaks = st != 0; int fc; if (f >> fc) p.ribbonFacing = fc; else f.clear(); }
        else if (k == "ball_trail") { int on = 0; f >> on >> p.trailPoint.x >> p.trailPoint.y >> p.trailPoint.z >> p.trailColor >> p.trailSize >> p.trailLife; p.ballTrail = on != 0; }
        else if (k == "morph_fx") { int hf = 1; f >> p.morphColor >> p.morphFx >> hf; p.morphHudFx = hf != 0; }
        else if (k == "death_fx") { int on, sm, dr, tv, sk; f >> on >> p.deathHit >> p.deathStatic >> p.deathShutdown >> p.deathOff >> sm >> dr >> tv >> sk >> p.deathStaticAmount >> p.deathStaticColor;
                                    p.deathFx = on != 0; p.deathSlowMo = sm != 0; p.deathDrop = dr != 0; p.deathTvOff = tv != 0; p.deathSkip = sk != 0; }
        else if (k == "morph_ball") f >> p.morphBall; else if (k == "bombs") f >> p.bombs; else if (k == "ball_speed") f >> p.ballSpeed; else if (k == "bomb_jump") f >> p.bombJump;
        else if (k == "bombs_at_once") f >> p.bombsAtOnce; else if (k == "bomb_fuse") f >> p.bombFuse; else if (k == "bomb_reach") f >> p.bombReach; else if (k == "bomb_sphere") f >> p.bombSphere;
        else if (k == "ball_model") { f >> std::ws; std::getline(f, p.ballModel); if (p.ballModel == "-") p.ballModel.clear(); }
        else { std::string rest; std::getline(f, rest); }
    }
    return true;
}

// ------------------------------------------------------------------ play mode
void PlayState::start(Player& p, const PlayerRules& r, V3 spawn, double spawnYaw) {
    health = r.maxHealth; deadFor = -1; hurtFlash = 0; falling = false; cause = "";
    p.feet = spawn; p.vel = { 0, 0, 0 }; p.yaw = spawnYaw; p.pitch = 0; p.flying = false; p.onGround = false;
}
PlayState::Event PlayState::tick(const World& w, Player& p, const PlayerRules& r, V3 spawn, double spawnYaw, double dt) {
    hurtFlash = std::max(0.0, hurtFlash - dt);
    if (dead()) {
        deadFor += dt;
        if (deadFor >= std::max((double)r.respawnDelay, r.deathFx ? r.deathLength() + 0.25 : 0.0)) { start(p, r, spawn, spawnYaw); return Respawned; }
        return Nothing;
    }
    float before = health;
    if (w.liquidAt({ p.feet.x, p.feet.y + 0.3, p.feet.z }) == 2) { health -= r.lavaDamage * (float)dt; cause = "burned in lava"; }
    // falling: remember where it started, hurt on landing
    if (!p.onGround && p.vel.y < 0 && !w.liquidAt({ p.feet.x, p.feet.y + 0.3, p.feet.z })) { if (!falling) { falling = true; fallStart = p.feet.y; } }
    else if (falling) {
        falling = false; double h = fallStart - p.feet.y;
        if (p.onGround && r.fallDamage > 0 && h > 4) { health -= r.fallDamage * r.damageTaken * (float)(h - 4); cause = "fell too far"; }
    }
    if (p.feet.y < r.voidDepth) { health = 0; cause = "fell out of the world"; }
    if (health < before) hurtFlash = 0.4;
    if (health <= 0) { health = 0; deadFor = 0; deaths++; p.vel = { 0, 0, 0 }; return Died; }
    return health < before ? Hurt : Nothing;
}

// ------------------------------------------------------------------ world info (world.txt)
static std::string escLook(std::string s) { for (size_t p; (p = s.find(' ')) != std::string::npos;) s.replace(p, 1, "%20"); return s.empty() ? "-" : s; }
static std::string unescLook(std::string s) { if (s == "-") return ""; for (size_t p; (p = s.find("%20")) != std::string::npos;) s.replace(p, 3, " "); return s; }
bool WorldInfo::save(const std::string& dir) const {
    std::error_code ec; std::filesystem::create_directories(dir, ec);
    std::ofstream f(dir + "/world.txt"); if (!f) return false;
    f.precision(17);
    f << "name " << name << "\ngenerator " << genKind << "\nlayers " << layers.size();
    for (auto& l : layers) f << " " << l.first << " " << l.second;
    f << "\nplayer " << player.feet.x << " " << player.feet.y << " " << player.feet.z << " " << player.yaw << " " << player.pitch << " " << (player.flying ? 1 : 0) << "\nhotbar " << hotSel;
    for (auto& h : hotbar) { std::string e = h; for (size_t p; (p = e.find(' ')) != std::string::npos;) e.replace(p, 1, "%20"); f << " " << (e.empty() ? "-" : e); }   // names may have spaces
    f << "\nweather " << weather << " " << (autoWeather ? 1 : 0) << "\nenvironment " << environment << "\ntrue_dark " << (trueDark ? 1 : 0) << "\nnight_dark " << nightDark << "\n";
    f << "min_light " << minLight << " " << minLightColor << "\nstart_hour " << startHour << " " << (keepClock ? 1 : 0) << "\n";
    if (hasSpawn) f << "spawn " << spawn.x << " " << spawn.y << " " << spawn.z << " " << spawnYaw << "\n";
    f << "look " << escLook(look) << "\n";
    for (auto& z : zones) { f << "zone " << z.env << " " << z.lo.x << " " << z.lo.y << " " << z.lo.z << " " << z.hi.x << " " << z.hi.y << " " << z.hi.z << " " << z.name << "\n"; if (!z.look.empty()) f << "zonelook " << escLook(z.look) << "\n"; }
    return (bool)f;
}
bool WorldInfo::load(const std::string& dir) {
    std::ifstream f(dir + "/world.txt"); if (!f) return false;
    zones.clear();
    std::string k;
    while (f >> k) {
        if (k == "name") { f >> std::ws; std::getline(f, name); }
        else if (k == "generator") f >> genKind;
        else if (k == "layers") { size_t n = 0; f >> n; layers.clear(); for (size_t i = 0; i < n && i < 64; i++) { std::string b; int t; f >> b >> t; layers.push_back({ b, t }); } }
        else if (k == "player") { int fl = 1; f >> player.feet.x >> player.feet.y >> player.feet.z >> player.yaw >> player.pitch >> fl; player.flying = fl != 0; }
        else if (k == "hotbar") { f >> hotSel; for (auto& h : hotbar) { f >> h; if (h == "-") h.clear(); for (size_t p; (p = h.find("%20")) != std::string::npos;) h.replace(p, 3, " "); } hotSel = std::max(0, std::min(8, hotSel)); }
        else if (k == "weather") { int a = 0; f >> weather >> a; autoWeather = a != 0; }
        else if (k == "environment") f >> environment;
        else if (k == "true_dark") { int v; f >> v; trueDark = v != 0; }
        else if (k == "night_dark") { f >> nightDark; nightDark = std::clamp(nightDark, 0.f, 1.f); }
        else if (k == "min_light") { f >> minLight >> minLightColor; minLight = std::clamp(minLight, 0.f, 1.f); }
        else if (k == "start_hour") { int kc = 0; f >> startHour >> kc; keepClock = kc != 0; startHour = std::clamp(startHour, 0.f, 23.99f); }
        else if (k == "look") { std::string l; f >> l; look = unescLook(l); }
        else if (k == "zonelook") { std::string l; f >> l; if (!zones.empty()) zones.back().look = unescLook(l); }
        else if (k == "spawn") { f >> spawn.x >> spawn.y >> spawn.z >> spawnYaw; hasSpawn = true; }
        else if (k == "zone") { EnvZone z; f >> z.env >> z.lo.x >> z.lo.y >> z.lo.z >> z.hi.x >> z.hi.y >> z.hi.z >> std::ws; std::getline(f, z.name); zones.push_back(z); }
        else { std::string rest; std::getline(f, rest); }
    }
    return true;
}
void WorldInfo::applyGenerator(Generator& g, const BlockRegistry& r) const {
    g.kind = genKind; g.layers.clear();
    for (auto& l : layers) { BlockId id = r.find(l.first); if (l.second > 0) g.layers.push_back({ id, l.second }); }
}

} // namespace mc
