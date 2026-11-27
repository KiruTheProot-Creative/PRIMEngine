#pragma once
// Cutscenes: a timeline of tracks - the camera (keyframes it glides through), actors (models or world objects that move
// along their own keys and play animation clips), texts (subtitles, comms, visor readouts, titles), sounds, fades and
// event actions (open a door, play music, set a flag ... the same actions as the event cards).
// Two views: Camera (a film camera, the HUD hidden, black bars) and Visor (first person: through the visor, the HUD stays).
// Plain C++, tested on Linux; the app plays them, draws them and edits them.
#include "events.h"
#include "game.h"
#include "sound.h"
#include <string>
#include <vector>

namespace mc {

enum class CutView { Camera, Visor, COUNT };
const char* cutViewName(CutView v);

// the camera at a moment: where it is, where it looks (degrees; yaw 0 looks toward +Z, like the player), its tilt and view angle
struct CamKey {
    float t = 0;
    V3 pos{ 0, 0, 0 };
    float yaw = 0, pitch = 0, roll = 0, fov = 70;
    bool ease = false;    // it slows down to arrive here and starts slowly from here (the first and the last key always do)
    bool cut = false;     // a cut: the camera jumps here at this moment (no glide from the key before)
};

// an actor: a model shown only during the cutscene (Samus), or a world object it moves (the gunship on its pad)
struct ActorKey { float t = 0; V3 pos{ 0, 0, 0 }; float yaw = 0; bool ease = false; bool cut = false; };
struct ActorClip { float t = 0, length = 2; std::string clip; float speed = 1; bool once = false; };   // plays this animation from t for length seconds (it loops; once: it plays one time and then holds its last pose, also after the bar, until another bar starts)
struct CutActor {
    std::string name = "Actor";
    std::string object;            // a world object's name: that object is moved (and stays where its own place is, after)
    std::string model;             // else: this model, shown only while the cutscene plays
    float scale = 1;
    float showFrom = 0, showTo = -1;   // when it is there (-1: to the end)
    std::vector<ActorKey> keys;
    std::vector<ActorClip> clips;
};

enum class CutTextStyle { Subtitle, Comms, Readout, Title, COUNT };
const char* cutTextStyleName(CutTextStyle s);
struct CutText {
    float t = 0, length = 4;
    std::string text, speaker;     // speaker: comms only ("Outpost Control")
    CutTextStyle style = CutTextStyle::Subtitle;
    float typeSpeed = 30;          // letters a second (0: all at once)
};
struct CutSound { float t = 0; SoundUse sound; };

enum class CutFadeKind { FromBlack, ToBlack, Flash, FromWhite, ToWhite, COUNT };
const char* cutFadeName(CutFadeKind k);
struct CutFade { float t = 0, length = 1; CutFadeKind kind = CutFadeKind::FromBlack; };

struct CutEvent { float t = 0; EventDo action; };

struct Cutscene {
    std::string name = "New cutscene";
    CutView view = CutView::Camera;
    float length = 10;                 // seconds
    bool letterbox = true;             // Camera view: black bars at the top and bottom
    bool skippable = true;             // holding the skip button jumps to the end (what lasts still happens)
    bool onNewGame = false;            // plays by itself when a new game starts (after the story screens)
    bool fromPlayer = false;           // the camera starts where the player looks, and glides to the first key
    bool lookOnly = false;             // Visor: the camera stays at the player's eyes; the keys only turn the view
    bool handOff = false;              // at the end the player stands where the camera ended, looking the same way
    float blendOut = 0.8f;             // (no hand-off) after the end the view glides back to the player's own, this long
    float lookAround = 0;              // Visor: the player may look around this many degrees (0: the view is locked)
    std::vector<CamKey> cam;
    std::vector<CutActor> actors;
    std::vector<CutText> texts;
    std::vector<CutSound> sounds;
    std::vector<CutFade> fades;
    std::vector<CutEvent> events;
};

// ---- where things are at a moment
struct CamPose { V3 pos{ 0, 0, 0 }; double yaw = 0, pitch = 0, roll = 0, fov = 70; bool valid = false; };   // degrees
CamPose cameraAt(const Cutscene& c, float t);
CamPose blendPose(const CamPose& a, const CamPose& b, double f);   // f 0..1, turning the short way round
V3 poseForward(const CamPose& p);                                   // where it looks (unit length)

struct ActorPose { V3 pos{ 0, 0, 0 }; double yaw = 0; bool shown = false, keyed = false; int clip = -1; std::string clipName; float clipTime = 0; float clipSpeed = 1; bool hold = false; };   // hold: stop at the clip's last pose (no looping)
ActorPose actorAt(const CutActor& a, float t, float length);

struct FadeNow { float amount = 0; bool white = false; };   // over the picture: how much (0..1), white or black
FadeNow fadeAt(const Cutscene& c, float t);
float letterboxAt(const Cutscene& c, float t, float blendOut);   // 0..1: how far the black bars are in (they slide in and out)

// how much of a typed text shows at t (bytes, whole UTF-8 letters) and how visible it is (fading in and out)
size_t cutTypedBytes(const CutText& x, float t);
float cutTextAlpha(const CutText& x, float t);

// actions that change the world for good (doors, flags, keys, music...): skipping still does them; shakes, messages and bursts it leaves out
bool isLasting(Do d);
// skipped at `from`: the lasting actions still to come, in order
std::vector<EventDo> skippedActions(const Cutscene& c, float from);

// things that start in (from, to]: their indexes (in time order)
std::vector<int> eventsBetween(const Cutscene& c, float from, float to);
std::vector<int> soundsBetween(const Cutscene& c, float from, float to);

void sortTracks(Cutscene& c);           // keys, texts, sounds, fades and events in time order
float lastMoment(const Cutscene& c);    // the latest thing on any track (for "fit the length")
int findCutscene(const std::vector<Cutscene>& list, const std::string& name);   // -1: none

bool saveCutscenes(const std::string& path, const std::vector<Cutscene>& list);
bool loadCutscenes(const std::string& path, std::vector<Cutscene>& list);

} // namespace mc
