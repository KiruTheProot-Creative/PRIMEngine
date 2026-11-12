#include "events.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace mc {

const Need& needOf(When w) {
    static const Need n[] = {
        { "the game starts" },
        { "the player walks into", true, false, false, false, "zone" },
        { "the player walks out of", true, false, false, false, "zone" },
        { "the player touches", true, false, false, false, "any" },
        { "something is shot:", true, false, false, false, "any" },
        { "the player picks up", false, true },
        { "a door opens:", true, false, false, false, "door" },
        { "a door closes:", true, false, false, false, "door" },
        { "the player dies" },
        { "every few seconds", false, false, false, true, "", "seconds" },
        { "after some seconds", false, false, false, true, "", "seconds" },
        { "a counter reaches", false, true, false, true, "", "how many" },
    };
    return n[std::clamp((int)w, 0, (int)When::COUNT - 1)];
}
const Need& needOf(If c) {
    static const Need n[] = {
        { "the player has the key", false, true }, { "the player has not got the key", false, true },
        { "a counter is at least", false, true, false, true, "", "how many" },
        { "a flag is on", false, true }, { "a flag is off", false, true },
        { "health is below", false, false, false, true, "", "health" },
    };
    return n[std::clamp((int)c, 0, (int)If::COUNT - 1)];
}
const Need& needOf(Do d) {
    static const Need n[] = {
        { "show a message", false, false, true, true, "", "seconds" },
        { "open the door", true, false, false, false, "door" }, { "close the door", true, false, false, false, "door" },
        { "lock the door", true, false, false, false, "door" }, { "unlock the door", true, false, false, false, "door" },
        { "move the platform", true, false, false, false, "platform" },
        { "show", true, false, false, false, "any" }, { "hide", true, false, false, false, "any" },
        { "switch on", true, false, false, false, "light" }, { "switch off", true, false, false, false, "light" },
        { "burst of particles at", true, false, true, false, "any" },
        { "teleport the player to", true },
        { "heal the player", false, false, false, true, "", "health" }, { "hurt the player", false, false, false, true, "", "health" },
        { "kill the player" }, { "set a checkpoint here" },
        { "give the key", false, true }, { "take the key", false, true },
        { "add to a counter", false, true, false, true, "", "how many" },
        { "turn a flag on", false, true }, { "turn a flag off", false, true },
        { "wait", false, false, false, true, "", "seconds" },
        { "win: show", false, false, true },
    };
    return n[std::clamp((int)d, 0, (int)Do::COUNT - 1)];
}

// ------------------------------------------------------------------ saving (one line per part, names escaped)
static std::string esc(const std::string& s) { std::string o; for (char c : s) { if (c == '%') o += "%25"; else if (c == ' ') o += "%20"; else if (c == '\n') o += "%0A"; else o += c; } return o.empty() ? "-" : o; }
static std::string unesc(const std::string& s) {
    if (s == "-") return ""; std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) { std::string h = s.substr(i + 1, 2); o += (char)std::stoi(h, nullptr, 16); i += 2; } else o += s[i];
    }
    return o;
}
bool saveEvents(const std::string& path, const std::vector<EventCard>& cards) {
    std::ofstream f(path); if (!f) return false;
    f << "events 1\n";
    for (const EventCard& c : cards) {
        f << "card " << esc(c.name) << " " << (c.enabled ? 1 : 0) << " " << (c.once ? 1 : 0) << "\n";
        f << "when " << (int)c.when.kind << " " << esc(c.when.target) << " " << c.when.number << "\n";
        for (const EventIf& i : c.ifs) f << "if " << (int)i.kind << " " << esc(i.name) << " " << i.number << "\n";
        for (const EventDo& d : c.dos) f << "do " << (int)d.kind << " " << esc(d.target) << " " << esc(d.text) << " " << d.number << "\n";
    }
    return (bool)f;
}
bool loadEvents(const std::string& path, std::vector<EventCard>& cards) {
    cards.clear(); std::ifstream f(path); if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k; l >> k;
        if (k == "card") { EventCard c; std::string n; int e = 1, o = 0; l >> n >> e >> o; c.name = unesc(n); c.enabled = e != 0; c.once = o != 0; cards.push_back(c); }
        else if (cards.empty()) continue;
        else if (k == "when") { int t; std::string a; l >> t >> a >> cards.back().when.number; cards.back().when.kind = (When)std::clamp(t, 0, (int)When::COUNT - 1); cards.back().when.target = unesc(a); }
        else if (k == "if") { EventIf i; int t; std::string a; l >> t >> a >> i.number; i.kind = (If)std::clamp(t, 0, (int)If::COUNT - 1); i.name = unesc(a); cards.back().ifs.push_back(i); }
        else if (k == "do") { EventDo d; int t; std::string a, b; l >> t >> a >> b >> d.number; d.kind = (Do)std::clamp(t, 0, (int)Do::COUNT - 1); d.target = unesc(a); d.text = unesc(b); cards.back().dos.push_back(d); }
    }
    return true;
}

// ------------------------------------------------------------------ running them
bool EventRunner::conditions(const EventCard& c, EventHost& h) {
    for (const EventIf& i : c.ifs) {
        bool ok = true;
        switch (i.kind) {
        case If::HasKey: ok = h.hasKey(i.name); break;
        case If::NoKey: ok = !h.hasKey(i.name); break;
        case If::CountAtLeast: ok = h.count(i.name) >= (int)i.number; break;
        case If::FlagOn: ok = flags.count(i.name) > 0; break;
        case If::FlagOff: ok = flags.count(i.name) == 0; break;
        case If::HealthBelow: ok = h.health() < i.number; break;
        default: break;
        }
        if (!ok) return false;
    }
    return true;
}
void EventRunner::runFrom(const std::vector<EventCard>& cards, int ci, size_t from, EventHost& h) {
    const EventCard& c = cards[ci];
    for (size_t k = from; k < c.dos.size(); k++) {
        const EventDo& d = c.dos[k];
        switch (d.kind) {
        case Do::Wait: waiting.push_back({ time + std::max(0.f, d.number), ci, k + 1 }); return;   // the rest comes later
        case Do::FlagOn: flags.insert(d.text.empty() ? d.target : d.text); break;
        case Do::FlagOff: flags.erase(d.text.empty() ? d.target : d.text); break;
        default: h.run(d); break;
        }
    }
}
void EventRunner::fire(const std::vector<EventCard>& cards, int i, EventHost& h) {
    const EventCard& c = cards[i];
    if (!c.enabled || (c.once && fires[i] > 0) || !conditions(c, h)) return;
    fires[i]++;
    runFrom(cards, i, 0, h);
}
void EventRunner::start(const std::vector<EventCard>& cards, EventHost& h) {
    time = 0; fires.clear(); lastEvery.clear(); waiting.clear(); flags.clear(); countDone.clear();
    for (int i = 0; i < (int)cards.size(); i++) if (cards[i].when.kind == When::GameStarts) fire(cards, i, h);
}
void EventRunner::signal(const std::vector<EventCard>& cards, const EventSignal& s, EventHost& h) {
    for (int i = 0; i < (int)cards.size(); i++) {
        const EventWhen& w = cards[i].when;
        if (w.kind != s.kind) continue;
        bool any = w.target.empty();   // no object named: any one
        if (!any && w.target != s.subject && w.target != s.also) continue;
        fire(cards, i, h);
    }
}
void EventRunner::tick(const std::vector<EventCard>& cards, double dt, EventHost& h) {
    time += dt;
    for (int i = 0; i < (int)cards.size(); i++) {
        const EventWhen& w = cards[i].when;
        if (w.kind == When::Every) {
            double every = std::max(0.1f, w.number), last = lastEvery.count(i) ? lastEvery[i] : 0;
            if (time - last >= every) { lastEvery[i] = time; fire(cards, i, h); }
        } else if (w.kind == When::After) {
            if (!lastEvery.count(i) && time >= w.number) { lastEvery[i] = time; fire(cards, i, h); }
        } else if (w.kind == When::CountReaches) {
            bool reached = h.count(w.target) >= (int)w.number;
            if (reached && !countDone.count(i)) { countDone.insert(i); fire(cards, i, h); }
            else if (!reached) countDone.erase(i);
        }
    }
    // actions that were waiting
    std::vector<Waiting> due, later;
    for (const Waiting& wt : waiting) (wt.at <= time ? due : later).push_back(wt);
    waiting = later;
    for (const Waiting& wt : due) if (wt.card < (int)cards.size()) runFrom(cards, wt.card, wt.next, h);
}

} // namespace mc
