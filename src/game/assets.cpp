#include "assets.h"
#include "visors.h"   // escText / unescText
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace mc {

static const char* const typeNames[] = { "Block", "Texture", "Model", "Sound", "Picture", "Video", "Enemy type", "Boss", "Screen", "Scan entry", "World", "Prefab" };
static const char* const typePlurals[] = { "Blocks", "Textures", "Models", "Sounds", "Pictures", "Videos", "Enemies", "Bosses", "Screens", "Scans", "Worlds", "Prefabs" };
static const char* const typeKeys[] = { "block", "texture", "model", "sound", "picture", "video", "enemy", "boss", "screen", "scan", "world", "prefab" };
const char* assetTypeName(AssetType t) { int i = (int)t; return i >= 0 && i < (int)AssetType::COUNT ? typeNames[i] : "?"; }
const char* assetTypePlural(AssetType t) { int i = (int)t; return i >= 0 && i < (int)AssetType::COUNT ? typePlurals[i] : "?"; }
const char* assetTypeKey(AssetType t) { int i = (int)t; return i >= 0 && i < (int)AssetType::COUNT ? typeKeys[i] : "?"; }

std::string AssetId::str() const { return std::string(assetTypeKey(type)) + ":" + key; }
bool AssetId::parse(const std::string& s, AssetId& out) {
    size_t c = s.find(':'); if (c == std::string::npos) return false;
    std::string k = s.substr(0, c);
    for (int i = 0; i < (int)AssetType::COUNT; i++) if (k == typeKeys[i]) { out.type = (AssetType)i; out.key = s.substr(c + 1); return !out.key.empty(); }
    return false;
}

std::string AssetTree::folderOf(const AssetId& a) const { auto it = where.find(a.str()); return it == where.end() ? "" : it->second; }
void AssetTree::put(const AssetId& a, const std::string& folder) { std::string f = FolderTree::clean(folder); if (!f.empty()) folders.add(f); where[a.str()] = f; }
void AssetTree::forget(const AssetId& a) { where.erase(a.str()); favourites.erase(a.str()); }
void AssetTree::renameAsset(const AssetId& a, const std::string& newKey) {
    auto it = where.find(a.str()); if (it == where.end()) return;
    std::string f = it->second; bool fav = favourites.erase(a.str()) > 0; where.erase(it);
    AssetId n{ a.type, newKey }; where[n.str()] = f; if (fav) favourites.insert(n.str());
}
bool AssetTree::renameFolder(const std::string& from0, const std::string& to0) {
    std::string from = FolderTree::clean(from0), to = FolderTree::clean(to0);
    if (from.empty() || to.empty() || from == to || FolderTree::within(to, from)) return false;
    if (!folders.rename(from, to)) return false;
    for (auto& kv : where) if (FolderTree::within(kv.second, from)) kv.second = FolderTree::moved(kv.second, from, to);
    std::set<std::string> fav;
    for (const std::string& f : favourites) fav.insert(f.rfind("folder:", 0) == 0 && FolderTree::within(f.substr(7), from) ? "folder:" + FolderTree::moved(f.substr(7), from, to) : f);
    favourites = fav;
    return true;
}
void AssetTree::removeFolder(const std::string& f0) {
    std::string f = FolderTree::clean(f0); if (f.empty()) return;
    std::string up = FolderTree::parent(f);
    for (auto& kv : where) if (FolderTree::within(kv.second, f)) kv.second = up;
    for (auto it = favourites.begin(); it != favourites.end();) { if (it->rfind("folder:", 0) == 0 && FolderTree::within(it->substr(7), f)) it = favourites.erase(it); else ++it; }
    folders.remove(f);
}
int AssetTree::adopt(const std::vector<AssetId>& all, const std::function<std::string(const AssetId&)>& folder) {
    int n = 0; for (const AssetId& a : all) if (!has(a)) { put(a, folder ? folder(a) : ""); n++; }
    return n;
}
void AssetTree::prune(const std::vector<AssetId>& all) {
    std::set<std::string> live; for (const AssetId& a : all) live.insert(a.str());
    for (auto it = where.begin(); it != where.end();) { if (!live.count(it->first)) { favourites.erase(it->first); it = where.erase(it); } else ++it; }
}
std::vector<AssetId> AssetTree::in(const std::string& folder) const {
    std::string f = FolderTree::clean(folder); std::vector<AssetId> v;
    for (auto& kv : where) if (kv.second == f) { AssetId a; if (AssetId::parse(kv.first, a)) v.push_back(a); }
    return v;
}
int AssetTree::countWithin(const std::string& folder) const { int n = 0; for (auto& kv : where) n += FolderTree::within(kv.second, folder); return n; }

bool saveAssetTree(const std::string& path, const AssetTree& t) {
    std::ofstream f(path); if (!f) return false;
    f << "assets 1\n";
    for (const std::string& p : t.folders.all()) f << "folder " << escText(p) << "\n";
    for (auto& kv : t.where) f << "in " << escText(kv.first) << " " << escText(kv.second) << "\n";
    for (const std::string& s : t.favourites) f << "fav " << escText(s) << "\n";
    return (bool)f;
}
bool loadAssetTree(const std::string& path, AssetTree& t) {
    std::ifstream f(path); if (!f) return false;
    t = AssetTree{}; std::string line;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k, a, b; l >> k;
        if (k == "folder" && l >> a) t.folders.add(unescText(a));
        else if (k == "in" && l >> a >> b) { std::string fo = FolderTree::clean(unescText(b)); t.where[unescText(a)] = fo; if (!fo.empty()) t.folders.add(fo); }
        else if (k == "fav" && l >> a) t.favourites.insert(unescText(a));
    }
    return true;
}

static std::string lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
AssetQuery parseAssetQuery(const std::string& q) {
    AssetQuery r; std::istringstream in(q); std::string w;
    while (in >> w) {
        std::string lw = lower(w);
        if (lw.rfind("t:", 0) == 0 && lw.size() > 2) {   // a kind: its key, its name or its plural, the start is enough ("t:snd" no, "t:sou" yes)
            std::string k = lw.substr(2); bool any = false;
            for (int i = 0; i < (int)AssetType::COUNT; i++) {
                std::string a = typeKeys[i], b = lower(typeNames[i]), c = lower(typePlurals[i]);
                if (a.rfind(k, 0) == 0 || b.rfind(k, 0) == 0 || c.rfind(k, 0) == 0) { r.types.insert((AssetType)i); any = true; }
            }
            if (!any) r.words.push_back(lw);
        } else r.words.push_back(lw);
    }
    return r;
}
bool assetMatches(const AssetQuery& q, AssetType t, const std::string& name) {
    if (!q.types.empty() && !q.types.count(t)) return false;
    std::string n = lower(name);
    for (const std::string& w : q.words) if (n.find(w) == std::string::npos) return false;
    return true;
}
std::string uniqueAssetName(const std::string& want, const std::function<bool(const std::string&)>& taken) {
    if (!taken(want)) return want;
    std::string stem = want, ext; size_t dot = want.rfind('.');
    if (dot != std::string::npos && dot > 0 && want.size() - dot <= 6 && want.find(' ', dot) == std::string::npos) { stem = want.substr(0, dot); ext = want.substr(dot); }
    {   // "Kraid 2" -> "Kraid": count on from there
        size_t sp = stem.rfind(' ');
        if (sp != std::string::npos && sp + 1 < stem.size() && std::all_of(stem.begin() + sp + 1, stem.end(), [](char c) { return std::isdigit((unsigned char)c); })) stem = stem.substr(0, sp);
    }
    for (int i = 2;; i++) { std::string n = stem + " " + std::to_string(i) + ext; if (!taken(n)) return n; }
}

// ---- prefabs
Prefab makePrefab(const std::string& name, const std::vector<WorldObject>& objs) {
    Prefab p; p.name = name; if (objs.empty()) return p;
    V3 lo = objs[0].pos, hi = objs[0].pos;
    for (const WorldObject& o : objs) { lo = { std::min(lo.x, o.pos.x), std::min(lo.y, o.pos.y), std::min(lo.z, o.pos.z) }; hi = { std::max(hi.x, o.pos.x), std::max(hi.y, o.pos.y), std::max(hi.z, o.pos.z) }; }
    V3 base{ (lo.x + hi.x) * 0.5, lo.y, (lo.z + hi.z) * 0.5 };
    if (objs.size() == 1) base = objs[0].pos;
    for (WorldObject o : objs) { o.pos = o.pos - base; p.objects.push_back(o); }
    return p;
}
std::vector<WorldObject> placePrefab(const Prefab& p, V3 at, double yawDeg, uint32_t& nextId, const std::function<std::string(const std::string&)>& freeName) {
    std::vector<WorldObject> out; double a = yawDeg * 3.14159265358979 / 180, c = std::cos(a), s = std::sin(a);
    auto turn = [&](V3 v) { return V3{ v.x * c + v.z * s, v.y, -v.x * s + v.z * c }; };   // the same way a yaw turns a model
    long quarter = std::lround(yawDeg / 90.0); bool square = std::fabs(yawDeg - quarter * 90.0) < 1e-6 && (quarter % 2 != 0);
    for (WorldObject o : p.objects) {
        o.id = nextId++; o.pos = at + turn(o.pos); o.yaw += yawDeg;
        for (V3& q : o.platform.points) q = turn(q);
        if (square) std::swap(o.size.x, o.size.z);   // zones turned a quarter: their box too
        o.name = freeName ? freeName(o.name) : o.name;
        out.push_back(o);
    }
    return out;
}
bool savePrefab(const std::string& path, const Prefab& p) { return saveObjects(path, p.objects); }
bool loadPrefab(const std::string& path, Prefab& p) {
    p = Prefab{}; if (!loadObjects(path, p.objects)) return false;
    p.name = std::filesystem::u8path(path).stem().u8string(); return true;
}

} // namespace mc
