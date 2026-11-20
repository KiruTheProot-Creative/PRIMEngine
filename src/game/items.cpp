#include "items.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace mc {

const char* itemTypeName(ItemType t) {
    static const char* n[] = { "energy_tank", "missile_expansion", "power_bomb_expansion", "power_bomb", "suit", "hi_jump", "space_jump", "speed_booster", "spring_ball", "charge_beam",
                               "screw_attack", "grapple_beam", "spider_ball", "boost_ball", "shinespark", "wall_jump", "scan_visor", "thermal_visor", "xray_visor" };
    int i = (int)t; return i >= 0 && i < (int)ItemType::COUNT ? n[i] : "?";
}
bool itemWorks(ItemType t) { return (int)t <= (int)ItemType::ChargeBeam || t == ItemType::ScanVisor || t == ItemType::ThermalVisor || t == ItemType::XRayVisor; }   // (thermal / x-ray: their looks so far)
bool itemCounted(ItemType t) { return t == ItemType::EnergyTank || t == ItemType::MissileExpansion || t == ItemType::PowerBombExpansion; }
const char* itemGroup(ItemType t) {
    switch (t) {
    case ItemType::EnergyTank: case ItemType::MissileExpansion: case ItemType::PowerBombExpansion: return "Tanks and expansions";
    case ItemType::PowerBomb: case ItemType::SpringBall: case ItemType::SpiderBall: case ItemType::BoostBall: return "Morph Ball";
    case ItemType::Suit: return "Suits";
    case ItemType::HiJump: case ItemType::SpaceJump: case ItemType::SpeedBooster: case ItemType::Shinespark: case ItemType::ScrewAttack: case ItemType::WallJump: return "Boots and moves";
    case ItemType::ChargeBeam: case ItemType::GrappleBeam: return "Beam upgrades";
    default: return "Visors";
    }
}
const char* itemValueName(ItemType t, int k) {
    switch (t) {
    case ItemType::EnergyTank: return k == 0 ? "Energy each" : nullptr;
    case ItemType::MissileExpansion: return k == 0 ? "Missiles each" : nullptr;
    case ItemType::PowerBombExpansion: return k == 0 ? "Power Bombs each" : nullptr;
    case ItemType::PowerBomb: return k == 0 ? "Reach (blocks)" : k == 1 ? "Fuse (s)" : "Impact sphere (blocks)";
    case ItemType::Suit: return k == 0 ? "Damage taken (x)" : k == 1 ? "Lava damage (x)" : "Moves freely in water and lava (0/1)";
    case ItemType::HiJump: return k == 0 ? "Jump height (blocks)" : nullptr;
    case ItemType::SpaceJump: return k == 0 ? "Jumps in the air" : nullptr;
    case ItemType::SpeedBooster: return k == 0 ? "Run this long first (s)" : k == 1 ? "Then this much faster (x)" : nullptr;
    case ItemType::SpringBall: return k == 0 ? "Jump height (blocks)" : nullptr;
    default: return nullptr;
    }
}

ItemSet defaultItems() {
    ItemSet s;
    auto add = [&](ItemType t, const char* n, bool used, bool start, float a = 0, float b = 0, float c = 0, uint32_t col = 0xFFFFFF) {
        ItemDef d; d.type = t; d.name = n; d.used = used; d.startsOwned = start; d.v[0] = a; d.v[1] = b; d.v[2] = c; d.color = col; s.items.push_back(d);
    };
    add(ItemType::EnergyTank, "Energy Tank", true, false, 100);
    add(ItemType::MissileExpansion, "Missile Expansion", true, false, 5);
    add(ItemType::PowerBombExpansion, "Power Bomb Expansion", true, false, 1);
    add(ItemType::PowerBomb, "Power Bomb", true, false, 6, 2, 12);
    add(ItemType::SpringBall, "Spring Ball", true, false, 1.2f);
    add(ItemType::SpiderBall, "Spider Ball", false, false);
    add(ItemType::BoostBall, "Boost Ball", false, false);
    add(ItemType::Suit, "Varia Suit", true, false, 0.5f, 0.5f, 0, 0xFF9A30);
    add(ItemType::Suit, "Gravity Suit", true, false, 0.25f, 0.2f, 1, 0x9A60FF);
    add(ItemType::Suit, "Phazon Suit", true, false, 0.15f, 0.1f, 1, 0x404048);
    add(ItemType::HiJump, "Hi-Jump Boots", true, false, 2.4f);
    add(ItemType::SpaceJump, "Space Jump Boots", true, false, 1);
    add(ItemType::SpeedBooster, "Speed Booster", true, false, 1.5f, 2.0f);
    add(ItemType::Shinespark, "Shinespark", false, false);
    add(ItemType::ScrewAttack, "Screw Attack", false, false);
    add(ItemType::WallJump, "Wall Jump", false, false);
    add(ItemType::ChargeBeam, "Charge Beam", true, true);
    add(ItemType::GrappleBeam, "Grapple Beam", false, false);
    add(ItemType::ScanVisor, "Scan Visor", true, true);
    add(ItemType::ThermalVisor, "Thermal Visor", false, false);
    add(ItemType::XRayVisor, "X-Ray Visor", false, false);
    return s;
}

int itemCount(const ItemDef& d, const std::function<int(const std::string&)>& have) {
    if (!d.used) return 0;
    int n = std::max(0, have(d.name));
    if (itemCounted(d.type)) return n;
    return (d.startsOwned || n > 0) ? 1 : 0;
}

ItemEffects itemEffects(const ItemSet& s, const PlayerRules& base, const std::function<int(const std::string&)>& have) {
    ItemEffects e; e.maxHealth = base.maxHealth; e.jumpHeight = base.jumpHeight;
    bool chargeInGame = false, chargeOwned = false;
    for (size_t i = 0; i < s.items.size(); i++) {
        const ItemDef& d = s.items[i]; int n = itemCount(d, have);
        if (d.type == ItemType::ChargeBeam && d.used) { chargeInGame = true; chargeOwned = n > 0; }
        if (n <= 0) continue;
        switch (d.type) {
        case ItemType::EnergyTank: e.maxHealth += d.v[0] * n; break;
        case ItemType::MissileExpansion: e.missileBonus += (int)std::lround(d.v[0]) * n; break;
        case ItemType::PowerBombExpansion: e.powerBombBonus += (int)std::lround(d.v[0]) * n; break;
        case ItemType::PowerBomb: e.powerBombs = true; e.pbReach = std::max(0.5f, d.v[0]); e.pbFuse = std::max(0.1f, d.v[1]); e.pbSphere = std::max(0.f, d.v[2]); break;
        case ItemType::Suit: e.suit = (int)i; break;   // the last one in the list you have counts (put them in upgrade order)
        case ItemType::HiJump: e.jumpHeight = std::max(e.jumpHeight, d.v[0]); break;
        case ItemType::SpaceJump: e.airJumps = std::max(e.airJumps, (int)std::lround(d.v[0])); break;
        case ItemType::SpeedBooster: e.speedBooster = true; e.boostAfter = std::max(0.f, d.v[0]); e.boostSpeed = std::max(1.f, d.v[1]); break;
        case ItemType::SpringBall: e.springBall = true; e.springJump = std::max(0.1f, d.v[0]); break;
        default: break;
        }
    }
    if (e.suit >= 0) { const ItemDef& d = s.items[e.suit]; e.damageTaken = std::max(0.f, d.v[0]); e.lavaTaken = std::max(0.f, d.v[1]); e.freeInLiquid = d.v[2] > 0.5f; }
    e.canCharge = !chargeInGame || chargeOwned;
    return e;
}

static std::string esc(const std::string& s) { std::string o; for (char c : s) { if (c == '%') o += "%25"; else if (c == ' ') o += "%20"; else if (c == '\n') o += "%0A"; else o += c; } return o.empty() ? "-" : o; }
static std::string unesc(const std::string& s) {
    if (s == "-") return ""; std::string o;
    for (size_t i = 0; i < s.size(); i++) { if (s[i] == '%' && i + 2 < s.size()) { o += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16); i += 2; } else o += s[i]; }
    return o;
}
GetPopup defaultPopup(const ItemSet& s, const std::string& name, int hint) {
    GetPopup p; p.name = name; p.title = "{name}";
    const ItemDef* d = s.byName(name);
    if (d) {
        p.bigFirstOnly = itemCounted(d->type);
        switch (d->type) {
        case ItemType::EnergyTank: p.text = "Your suit can store more energy now. Energy fills up all the way."; break;
        case ItemType::MissileExpansion: p.text = "You can carry more missiles now: {missiles} in all."; break;
        case ItemType::PowerBombExpansion: p.text = "You can carry more Power Bombs now."; break;
        case ItemType::PowerBomb: p.text = "A huge blast that clears everything around you. Drop one while rolled up."; break;
        case ItemType::Suit: p.text = "Your suit is stronger now: less damage, and new places to go."; break;
        case ItemType::HiJump: p.text = "Your jumps reach much higher now."; break;
        case ItemType::SpaceJump: p.text = "Press {key:jump} again in the air to jump once more."; break;
        case ItemType::SpeedBooster: p.text = "Keep running and you speed up until nothing stops you."; break;
        case ItemType::SpringBall: p.text = "Press {key:jump} while rolled up to hop."; break;
        case ItemType::ChargeBeam: p.text = "Hold {key:fire} to gather energy, let go for a stronger shot."; break;
        case ItemType::ScrewAttack: p.text = "Spin through the air and tear through what's in your way."; break;
        case ItemType::GrappleBeam: p.text = "Latch onto grapple points and swing across gaps."; break;
        case ItemType::SpiderBall: p.text = "Roll along magnetic rails, even up walls and ceilings."; break;
        case ItemType::BoostBall: p.text = "Build up speed while rolled up and shoot forward."; break;
        case ItemType::Shinespark: p.text = "Store the Speed Booster's power and launch yourself with it."; break;
        case ItemType::WallJump: p.text = "Jump off walls to climb up narrow shafts."; break;
        case ItemType::ScanVisor: p.text = "Press {key:visor} to switch visors. Scan things to learn about them."; break;
        case ItemType::ThermalVisor: p.text = "See heat: warm things glow through the dark."; break;
        case ItemType::XRayVisor: p.text = "See through walls and find what is hidden."; break;
        default: p.text = ""; break;
        }
        return p;
    }
    switch (hint) {
    case HINT_BEAM: p.text = "A new beam for your arm cannon."; break;
    case HINT_MISSILE: p.text = "Press {key:missile} to fire a missile. Some doors only open for them."; break;
    case HINT_MORPH: p.text = "Press {key:ball} to roll into a ball and fit through small gaps."; break;
    case HINT_BOMBS: p.text = "Press {key:fire} while rolled up to drop a bomb."; break;
    default: p.style = POPUP_BANNER; p.text = ""; break;   // keys, collectibles: a banner
    }
    return p;
}
GetPopup popupFor(const ItemSet& s, const std::string& name, int hint) {
    for (const GetPopup& p : s.popups) if (p.name == name) return p;
    return defaultPopup(s, name, hint);
}
void setPopup(ItemSet& s, const GetPopup& p) {
    for (GetPopup& q : s.popups) if (q.name == p.name) { q = p; return; }
    s.popups.push_back(p);
}
bool popupCounted(const ItemSet& s, const std::string& name) { const ItemDef* d = s.byName(name); return d && itemCounted(d->type); }
int popupStyleNow(const GetPopup& p, bool counted, int howMany) {
    if (p.style == POPUP_BIG && counted && p.bigFirstOnly && howMany > 1) return POPUP_BANNER;
    return p.style;
}

bool saveItems(const std::string& path, const ItemSet& s) {
    std::ofstream f(path); if (!f) return false;
    f << "items 1\npower_bombs " << esc(s.powerBombCounter) << " " << s.powerBombStart << " " << s.powerBombMax << "\n";
    for (const ItemDef& d : s.items)
        f << "item " << itemTypeName(d.type) << " " << esc(d.name) << " " << d.used << " " << d.startsOwned << " " << d.v[0] << " " << d.v[1] << " " << d.v[2] << " " << d.color << "\n";
    f << "popups " << esc(soundUseText(s.fanfare)) << " " << s.musicDuck << "\n";
    for (const GetPopup& p : s.popups)
        f << "popup " << esc(p.name) << " " << p.style << " " << p.bigFirstOnly << " " << esc(p.title) << " " << esc(p.text) << " " << esc(soundUseText(p.fanfare)) << " " << esc(p.model) << "\n";
    return (bool)f;
}
bool loadItems(const std::string& path, ItemSet& s) {
    std::ifstream f(path); if (!f) return false;
    ItemSet out; out.items.clear(); std::string line;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k; l >> k;
        if (k == "power_bombs") { std::string c; l >> c >> out.powerBombStart >> out.powerBombMax; out.powerBombCounter = unesc(c); }
        else if (k == "popups") { std::string fa; l >> fa >> out.musicDuck; soundUseFrom(unesc(fa), out.fanfare); }
        else if (k == "popup") {
            std::string n, t, x, fa, m; GetPopup p; l >> n >> p.style >> p.bigFirstOnly >> t >> x >> fa >> m; if (!l && !l.eof()) continue;
            p.name = unesc(n); p.title = unesc(t); p.text = unesc(x); soundUseFrom(unesc(fa), p.fanfare); p.model = unesc(m);
            if (!p.name.empty()) out.popups.push_back(p);
        }
        else if (k == "item") {
            std::string t, n; ItemDef d; l >> t >> n >> d.used >> d.startsOwned >> d.v[0] >> d.v[1] >> d.v[2] >> d.color; if (!l) continue;
            int ti = -1; for (int i = 0; i < (int)ItemType::COUNT; i++) if (t == itemTypeName((ItemType)i)) ti = i;
            if (ti < 0) continue;
            d.type = (ItemType)ti; d.name = unesc(n); out.items.push_back(d);
        }
    }
    bool anySuit = std::any_of(out.items.begin(), out.items.end(), [](const ItemDef& x) { return x.type == ItemType::Suit; });
    for (const ItemDef& d : defaultItems().items)   // kinds a newer engine knows: added as they come (the suits only if there are none)
        if (d.type == ItemType::Suit ? !anySuit : std::none_of(out.items.begin(), out.items.end(), [&](const ItemDef& x) { return x.type == d.type; })) out.items.push_back(d);
    s = out; return true;
}

} // namespace mc
