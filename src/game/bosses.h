#pragma once
// Bosses (Player editor > Bosses): an enemy type made a boss - a health bar, weak points / armour / breakable armour
// (parts of its model, or boxes on its body), phases (each starting when its health drops, a part breaks, time passes or
// a flag is set) and in each phase a list of moves played in order, at random or by distance: walking, chasing, hovering,
// jumping, dashing, shooting (aimed, spread, straight, ring, rain, homing, lobbed), slams, spawning enemies, a shield,
// opening the weak point, roaring. An arena zone starts the fight (its doors seal), it has its own music, and a death
// sequence. All without code. Plain C++, tested.
#include "enemies.h"
#include "sound.h"
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mc {

enum class BossMoveKind { Wait, Walk, Chase, MoveTo, Hover, JumpAt, Hop, Dash, Shoot, Slam, Spawn, Shield, OpenWeak, CloseWeak, Roar, COUNT };
const char* bossMoveName(BossMoveKind k);
enum class ShotPattern { Aimed, Spread, Straight, Ring, Rain, Homing, Lob, COUNT };
const char* shotPatternName(ShotPattern p);

struct BossMove {
    BossMoveKind kind = BossMoveKind::Wait;
    float time = 1;                                      // seconds the move takes
    float speed = 3;                                     // walking / chasing / moving / dashing (blocks a second)
    float height = 3;                                    // hover: above its home; jumps: how high
    V3 offset{ 0, 0, 0 };                                // MoveTo: where (blocks from its home)
    // shooting
    ShotPattern pattern = ShotPattern::Aimed; int count = 1; float spread = 30;   // how many, fanned over this many degrees
    float shotSpeed = 12, damage = 15, size = 0.25f, gravity = 14, homing = 2.5f; uint32_t color = 0xFF6030;
    float every = 0;                                     // fires again every so many seconds during the move (0: once)
    V3 from{ 0, 0.3, 0.5 };                              // where shots leave: blocks from its middle (z: ahead), turned with it
    // the rest
    int spawnType = 0, spawnCount = 1;                   // Spawn: which enemy type (its ID), how many
    std::string part;                                    // OpenWeak / CloseWeak: which part (empty: every weak point)
    float slamRadius = 6, slamDamage = 20;               // Slam: a shockwave along the floor
    float shake = 0;                                     // the screen shakes (roar, slam: how hard)
    std::string anim; SoundUse sound;                    // the model's animation during it, a sound when it starts
    float weight = 1, range = 0;                         // random picking: how often; by distance: only when you are within range (0: any)
};

enum class PhaseStart { HealthBelow, PartBroken, AfterTime, Flag, COUNT };
const char* phaseStartName(PhaseStart s);
enum class PickOrder { InOrder, Random, ByDistance, COUNT };
const char* pickOrderName(PickOrder p);
struct BossPhase {
    std::string name = "Phase 1";
    PhaseStart start = PhaseStart::HealthBelow; float value = 100; std::string what;   // health % / seconds into the fight; the part / flag
    PickOrder order = PickOrder::InOrder;
    float speed = 1;                                     // all its moves faster (2: twice as fast)
    uint32_t tint = 0xFF4020; float tintAmount = 0;     // it glows in this colour
    int musicMode = 0; SoundUse music;                   // 0 keeps the music, 1 plays this
    std::vector<BossMove> moves;
};

enum class PartRole { Weak, Armour, Breakable, COUNT };
const char* partRoleName(PartRole r);
struct BossPart {
    std::string name = "Weak point";
    std::string modelPart;                               // a moving part of its imported model (empty: the box below)
    Box3 box{ { 0.3, 0.55, 0.7 }, { 0.7, 0.85, 1.0 } };  // without a model part: where on its body (0..1 of its box: x left..right, y up, z back..front)
    PartRole role = PartRole::Weak;
    float damage = 2;                                    // Weak: damage times this
    float health = 100;                                  // Breakable: its own health
    bool startsOpen = true;                              // Weak: can be hit from the start (else: only after an "open" move)
};

struct BossDef {
    int enemyType = 0;                                   // which enemy type is this boss
    std::string title;                                   // on the boss bar (empty: the enemy type's name)
    float health = 600; uint32_t barColor = 0xFF5030;
    bool bodyHurts = true;                               // a shot on the body (not a part) hurts it
    std::vector<BossPart> parts;
    std::vector<BossPhase> phases;
    std::string arena;                                   // a zone: walking in starts the fight (empty: when it sees you)
    bool sealDoors = true;                               // the doors at the arena close and lock until it is defeated
    float introTime = 1.5f; SoundUse roar; std::string introAnim;   // before the fight: it roars (no attacks yet)
    SoundUse music;                                      // the fight's music (stops when it is defeated)
    float deathTime = 2.5f;                              // explosions before it goes
    std::string flag;                                    // turned on when it is defeated (empty: "<title> defeated")
};
struct BossSet {
    std::vector<BossDef> bosses;
    BossDef* forType(int enemyType); const BossDef* forType(int enemyType) const;
};
BossDef defaultBoss(int enemyType);                      // a starting point: one phase that walks, shoots and roars
BossDef kraidExample(int enemyType);                     // like Metroid's Kraid: walks, belly spikes straight ahead, claws lobbed; faster below half health
bool saveBosses(const std::string& path, const BossSet& s);
bool loadBosses(const std::string& path, BossSet& s);

// ---- the fight
struct BossShot { V3 from, vel; float damage, size, gravity, homing, life; uint32_t color; };
struct BossTick {
    std::vector<BossShot> shots;
    int spawnType = 0, spawnCount = 0; V3 spawnAt;
    bool slam = false; float slamRadius = 0, slamDamage = 0;
    float shake = 0; const SoundUse* sound = nullptr;
    int phaseStarted = -1;                               // a new phase began (its index)
    bool introOver = false;
};
struct BossState {
    bool fighting = false, dying = false; float introT = 0, dieT = 0, fightT = 0, maxHealth = 1;
    int phase = 0, move = -1; float moveT = 0, fireT = 0; bool fired = false; uint32_t rng = 1;
    std::map<std::string, float> partHealth; std::set<std::string> broken, open;
    float shieldT = 0;                                   // can't be hurt while > 0
    V3 home, target; int walkDir = 1; float vy = 0; bool onGround = false;
    std::string anim;                                    // the animation it wants now
};
void bossStart(BossState& b, const BossDef& d, EnemyState& e);                 // the fight begins (the intro first)
using FlagFn = std::function<bool(const std::string&)>;
BossTick bossUpdate(BossState& b, const BossDef& d, EnemyState& e, const EnemyType& t, const SolidFn& solid, V3 you, const FlagFn& flagOn, double dt);
Box3 bossPartBox(const BossPart& p, const EnemyState& e, const EnemyType& t); // a part without a model part: its box in the world
struct BossHitResult { float damage = 0; bool deflected = false, broke = false; std::string part; };
// a hit on `part` ("" : the body): what it does (armour, a closed weak point, the shield: deflected)
BossHitResult bossHit(BossState& b, const BossDef& d, EnemyState& e, const std::string& part, float damage);
float bossHealthFrac(const BossState& b, const EnemyState& e);

} // namespace mc
