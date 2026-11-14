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
bool itemWorks(ItemType t) { return (int)t <= (int)ItemType::ChargeBeam; }
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
    add(ItemType::ScanVisor, "Scan Visor", false, true);
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

static std::string esc(const std::string& s) { std::string o; for (char c : s) { if (c == '%') o += "%25"; else if (c == ' ') o += "%20"; else o += c; } return o.empty() ? "-" : o; }
static std::string unesc(const std::string& s) {
    if (s == "-") return ""; std::string o;
    for (size_t i = 0; i < s.size(); i++) { if (s[i] == '%' && i + 2 < s.size()) { o += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16); i += 2; } else o += s[i]; }
    return o;
}
bool saveItems(const std::string& path, const ItemSet& s) {
    std::ofstream f(path); if (!f) return false;
    f << "items 1\npower_bombs " << esc(s.powerBombCounter) << " " << s.powerBombStart << " " << s.powerBombMax << "\n";
    for (const ItemDef& d : s.items)
        f << "item " << itemTypeName(d.type) << " " << esc(d.name) << " " << d.used << " " << d.startsOwned << " " << d.v[0] << " " << d.v[1] << " " << d.v[2] << " " << d.color << "\n";
    return (bool)f;
}
bool loadItems(const std::string& path, ItemSet& s) {
    std::ifstream f(path); if (!f) return false;
    ItemSet out; out.items.clear(); std::string line;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k; l >> k;
        if (k == "power_bombs") { std::string c; l >> c >> out.powerBombStart >> out.powerBombMax; out.powerBombCounter = unesc(c); }
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
