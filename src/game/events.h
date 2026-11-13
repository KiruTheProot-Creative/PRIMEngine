#pragma once
// Game logic (G3): event cards. "When <something happens> - only if <conditions> - do <actions, in order>".
// Objects are named in the cards (all objects with that name are meant). Plain C++, tested on Linux.
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mc {

enum class When { GameStarts, EnterZone, LeaveZone, Touch, Shot, Pickup, DoorOpens, DoorCloses, Dies, Every, After, CountReaches, COUNT };
enum class If { HasKey, NoKey, CountAtLeast, FlagOn, FlagOff, HealthBelow, COUNT };
enum class Do { Message, OpenDoor, CloseDoor, LockDoor, UnlockDoor, MovePlatform, Show, Hide, LightOn, LightOff, Particles, Teleport,
                Heal, Hurt, Kill, Checkpoint, GiveKey, TakeKey, AddCount, FlagOn, FlagOff, Wait, Win,
                HudShow, HudHide, HudFlash, COUNT };   // HUD: an element on the player's HUD, by name

// what each kind needs: an object (and which kind of object), a name, a text, a number
struct Need { const char* label; bool object = false, name = false, text = false, number = false; const char* objectKind = ""; const char* numberLabel = ""; };
const Need& needOf(When w);
const Need& needOf(If c);
const Need& needOf(Do d);

struct EventWhen { When kind = When::EnterZone; std::string target; float number = 1; };
struct EventIf { If kind = If::HasKey; std::string name; float number = 1; };
struct EventDo { Do kind = Do::Message; std::string target, text; float number = 1; };
struct EventCard {
    std::string name = "New event";
    bool enabled = true, once = false;
    EventWhen when; std::vector<EventIf> ifs; std::vector<EventDo> dos;
};

// the card as one sentence: "When the player walks into Hall, only if ...: open Gate, wait 2 s, show \"Hi\"."
std::string describe(const EventCard& c);

bool saveEvents(const std::string& path, const std::vector<EventCard>& cards);
bool loadEvents(const std::string& path, std::vector<EventCard>& cards);

// something that happened in the game (subject: the object's name, or a pickup / key / counter name)
struct EventSignal { When kind; std::string subject, also; };   // also: a second name that may match (a pickup's "Gem" besides its object name)

// what the game answers and does for the cards
struct EventHost {
    virtual ~EventHost() = default;
    virtual bool hasKey(const std::string& name) = 0;
    virtual int count(const std::string& name) = 0;
    virtual float health() = 0;
    virtual void run(const EventDo& action) = 0;
};

class EventRunner {
public:
    std::set<std::string> flags;
    void start(const std::vector<EventCard>& cards, EventHost& host);   // Play begins: "game starts" cards run
    void signal(const std::vector<EventCard>& cards, const EventSignal& s, EventHost& host);
    void tick(const std::vector<EventCard>& cards, double dt, EventHost& host);   // timers, waiting actions, counters
    int firedCount(int card) const { auto it = fires.find(card); return it == fires.end() ? 0 : it->second; }
private:
    double time = 0;
    std::map<int, int> fires; std::map<int, double> lastEvery; std::set<int> countDone;
    struct Waiting { double at; int card; size_t next; };
    std::vector<Waiting> waiting;
    bool conditions(const EventCard& c, EventHost& host);
    void fire(const std::vector<EventCard>& cards, int i, EventHost& host);
    void runFrom(const std::vector<EventCard>& cards, int card, size_t from, EventHost& host);
};

} // namespace mc
