#pragma once
// Enemies (Player editor > Enemies): enemy types made once (enemies.txt, by ID), placed as Enemy objects. A type has its
// health, what hurts it (weaknesses), how it moves (stands, walks, crawls over blocks like a Zoomer, flies, hops, or
// hangs on the ceiling and drops like a Skree), whether it shoots, what it drops, when it comes back, its animations,
// sounds and scan entry. The moving and deciding is here (plain C++, tested); the game draws them, hits them and gets
// hurt by them. Collision is against the world's solid blocks (a function says which cells are solid).
#include "game.h"
#include "sound.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mc {

enum class EnemyMove { Still, Walk, Crawl, Fly, Hop, Drop, COUNT };
const char* enemyMoveName(EnemyMove m);
enum class EnemyRespawn { Never, OnReturn, After, COUNT };   // OnReturn: when you come back to the world (or play again)
const char* enemyRespawnName(EnemyRespawn r);
enum class EnemyDeath { Explode, Fracture, Animation, Burst, COUNT };   // explode: its model as little cubes flung around; fracture: a few big chunks;
const char* enemyDeathName(EnemyDeath d);                                // animation: its "die" clip; burst: only sparks

struct EnemyWeak { std::string by = "Missile"; float times = 2; };   // a beam's or missile's name, or Any beam, Missile, Charged, Bomb, Power Bomb
struct EnemyDrop { std::string what = "Energy"; int amount = 10; float chance = 0.3f; };   // Energy (health), or a counter (the missiles' name...)

struct EnemyType {
    int id = 0;
    std::string name = "Enemy", model = "@enemy-walker";
    float size = 1;                                     // its body (blocks across): the model is fitted to it
    EnemyMove move = EnemyMove::Walk;
    float health = 30, touchDamage = 10, knockback = 4;
    float speed = 2, chaseSpeed = 3.5f, sight = 14;     // blocks per second (crawlers: blocks along the surface); how far it notices you
    bool chases = true;                                 // goes after you once it sees you (walk, fly, hop)
    bool shoots = false; float shootEvery = 2, shotSpeed = 12, shotDamage = 8, shotRange = 30; uint32_t shotColor = 0xFF6030;
    float hopHeight = 1.6f, hopEvery = 1.4f;            // hop
    float hover = 2.5f, roam = 4;                       // fly: how high above where it started, how far it wanders
    float dropReach = 2.5f; int shrapnel = 6;           // drop: how far to the side you can be for it to fall; pieces when it bursts
    bool onlyWeak = false;                              // only its weaknesses hurt it (else everything does, weaknesses more)
    std::vector<EnemyWeak> weak;
    std::vector<EnemyDrop> drops;
    EnemyRespawn respawn = EnemyRespawn::OnReturn; float respawnTime = 30;
    EnemyDeath death = EnemyDeath::Explode;
    std::string animIdle = "idle", animMove = "walk", animAttack = "attack", animHurt = "hurt", animDie = "die";
    std::string sndNotice, sndAttack, sndHurt, sndDie;
    int scanId = 0;                                     // its scan entry (0: none)
    uint32_t color = 0xFF8040;                          // its bursts and hurt flash
    bool lockable = true;                               // lock-on (right mouse) can pick it
    bool onRadar = true;                                // shows as a dot on the HUD's radar
    int grapple = 0;                                    // the Grapple Beam: 0 it is pulled to you (and stunned), 1 it pulls you to it (big, heavy ones), 2 nothing (the beam slips off)
};
// how a kind of drop looks (Energy, or a counter: Missile, Power Bomb ...): a model, a bigger one from some amount on,
// spinning and bobbing, a light, its own pickup sound, how long it stays
struct DropLook {
    std::string what = "Energy";
    std::string model; float scale = 0.55f;             // model: "" the built-in placeholder
    int bigFrom = 0; std::string bigModel; float bigScale = 0.8f;   // from this amount on (0: never), this model instead (like Prime's large energy)
    float spin = 120, bob = 0.1f;                       // degrees a second; blocks up and down
    uint32_t lightColor = 0xFFD080; float light = 0, lightPower = 1;   // a light around it: reach (0: none), brightness
    SoundUse sound;                                     // picking it up (none: the game's pickup sound)
    float life = 12, blinkAt = 3;                       // seconds it stays; blinks for the last few
};
struct EnemySet {
    std::vector<EnemyType> types;
    std::vector<DropLook> drops;                        // the drops' looks (a kind without one: the placeholder)
    const DropLook* drop(const std::string& what) const;
    EnemyType* find(int id);
    const EnemyType* find(int id) const;
    int add(EnemyType t);                               // the next free ID (if it has none or a taken one)
};
EnemySet defaultEnemies();                              // placeholders: Zoomer, Hornoad, Reo, Trooper, Skree, Turret
bool saveEnemies(const std::string& path, const EnemySet& s);
bool loadEnemies(const std::string& path, EnemySet& s);

// how much a hit does (0: it is immune): base damage times its weakness (the best matching one)
float enemyDamage(const EnemyType& t, const std::string& weapon, bool missile, bool charged, bool bomb, float base);

// ---- in play
enum class EnemyMode { Idle, Chase, Fall, Burrow, Dead };
struct EnemyState {
    uint32_t obj = 0; int type = 0;
    V3 home, pos, vel; V3 fwd{ 0, 0, 1 }, up{ 0, 1, 0 };   // pos: the bottom middle of its body (crawlers: on their surface)
    float health = 30, hurtT = 0, shootT = 1, hopT = 0.5f, timer = 0, lostT = 0, deadT = 0, respawnIn = -1, age = 0;
    EnemyMode mode = EnemyMode::Idle; bool onGround = false, seen = false;
    // crawling: the air cell it is in, the surface's normal (out of the block it clings to), its way along the surface
    int cell[3] = { 0, 0, 0 }, n[3] = { 0, 1, 0 }, d[3] = { 0, 0, 1 }; bool attached = false;
    V3 from, mid, to, fromUp{ 0, 1, 0 }, toUp{ 0, 1, 0 }, fromFwd{ 0, 0, 1 }, toFwd{ 0, 0, 1 }; float t = 1, stepLen = 1;
    bool alive() const { return mode != EnemyMode::Dead; }
};
struct EnemyTick { bool fire = false; V3 shotFrom, shotDir; bool burst = false; bool noticed = false; };
using SolidFn = std::function<bool(int, int, int)>;
float enemyHalfWidth(const EnemyType& t);
float enemyHeight(const EnemyType& t);
Box3 enemyBox(const EnemyState& s, const EnemyType& t);  // where it can be hit (and touches you)
void enemyStart(EnemyState& s, const EnemyType& t, V3 pos, double yawDeg, const SolidFn& solid);   // crawlers cling to the nearest block, droppers hang from the ceiling
// one step: target = the middle of your body, canSee = it can see you (nothing in between)
EnemyTick enemyUpdate(EnemyState& s, const EnemyType& t, const SolidFn& solid, V3 target, bool canSee, double dt);
void enemyHit(EnemyState& s, const EnemyType& t, float damage, V3 from);   // hurt: flash, knocked back, maybe dead

} // namespace mc
