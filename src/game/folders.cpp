#include "folders.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace mc {

std::string FolderTree::clean(const std::string& p) {
    std::string o; std::string part;
    auto flush = [&] {
        size_t a = part.find_first_not_of(" \t"), b = part.find_last_not_of(" \t");
        if (a != std::string::npos) { if (!o.empty()) o += "/"; o += part.substr(a, b - a + 1); }
        part.clear();
    };
    for (char c : p) { if (c == '/' || c == '\\') flush(); else if (c != '\n' && c != '\r') part += c; }
    flush(); return o;
}
std::string FolderTree::parent(const std::string& p) { size_t s = p.rfind('/'); return s == std::string::npos ? "" : p.substr(0, s); }
std::string FolderTree::leaf(const std::string& p) { size_t s = p.rfind('/'); return s == std::string::npos ? p : p.substr(s + 1); }
bool FolderTree::within(const std::string& path, const std::string& folder) {
    return folder.empty() || path == folder || (path.size() > folder.size() && path.compare(0, folder.size(), folder) == 0 && path[folder.size()] == '/');
}
std::string FolderTree::moved(const std::string& path, const std::string& from, const std::string& to) {
    if (from.empty() || !within(path, from)) return path;
    return clean(to + path.substr(from.size()));
}
void FolderTree::add(const std::string& p0) { std::string p = clean(p0); while (!p.empty()) { paths.insert(p); p = parent(p); } }
std::vector<std::string> FolderTree::children(const std::string& par) const {
    std::vector<std::string> out; for (const std::string& p : paths) if (parent(p) == par) out.push_back(p);
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        std::string x = a, y = b; for (char& c : x) c = (char)std::tolower((unsigned char)c); for (char& c : y) c = (char)std::tolower((unsigned char)c); return x < y; });
    return out;
}
bool FolderTree::rename(const std::string& f0, const std::string& t0) {
    std::string from = clean(f0), to = clean(t0);
    if (from.empty() || to.empty() || from == to || within(to, from)) return false;
    std::set<std::string> next;
    for (const std::string& p : paths) next.insert(moved(p, from, to));
    paths.clear(); for (const std::string& p : next) add(p);
    add(to); return true;
}
void FolderTree::remove(const std::string& p0) {
    std::string p = clean(p0); if (p.empty()) return;
    for (auto it = paths.begin(); it != paths.end();) { if (within(*it, p)) it = paths.erase(it); else ++it; }
}
void AssetFolders::renameIn(std::map<std::string, std::string>& in, const std::string& from, const std::string& to) {
    for (auto& kv : in) kv.second = FolderTree::moved(kv.second, FolderTree::clean(from), FolderTree::clean(to));
}
void AssetFolders::dropIn(std::map<std::string, std::string>& in, const std::string& folder) {
    std::string f = FolderTree::clean(folder), up = FolderTree::parent(f);
    for (auto& kv : in) if (!f.empty() && FolderTree::within(kv.second, f)) kv.second = up;
}

static std::string esc(const std::string& s) { std::string o; for (char c : s) { if (c == '%') o += "%25"; else if (c == ' ') o += "%20"; else if (c == '\t') o += "%09"; else o += c; } return o.empty() ? "-" : o; }
static std::string unesc(const std::string& s) {
    if (s == "-") return ""; std::string o;
    for (size_t i = 0; i < s.size(); i++) { if (s[i] == '%' && i + 2 < s.size()) { o += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16); i += 2; } else o += s[i]; }
    return o;
}
bool saveAssetFolders(const std::string& path, const AssetFolders& a) {
    std::ofstream f(path); if (!f) return false;
    f << "asset_folders 1\n";
    auto tree = [&](const char* k, const FolderTree& t) { for (const std::string& p : t.paths) f << "folder " << k << " " << esc(p) << "\n"; };
    tree("textures", a.textures); tree("models", a.models); tree("blocks", a.blocks); tree("objects", a.objects);
    for (auto& kv : a.textureIn) if (!kv.second.empty()) f << "texture " << esc(kv.first) << " " << esc(kv.second) << "\n";
    for (auto& kv : a.modelIn) if (!kv.second.empty()) f << "model " << esc(kv.first) << " " << esc(kv.second) << "\n";
    return (bool)f;
}
bool loadAssetFolders(const std::string& path, AssetFolders& a) {
    std::ifstream f(path); if (!f) return false;
    a = AssetFolders{}; std::string line;
    while (std::getline(f, line)) {
        std::istringstream l(line); std::string k, x, y; l >> k >> x >> y;
        if (k == "folder") { FolderTree* t = x == "textures" ? &a.textures : x == "models" ? &a.models : x == "blocks" ? &a.blocks : x == "objects" ? &a.objects : nullptr; if (t) t->add(unesc(y)); }
        else if (k == "texture") { a.textureIn[unesc(x)] = FolderTree::clean(unesc(y)); a.textures.add(unesc(y)); }
        else if (k == "model") { a.modelIn[unesc(x)] = FolderTree::clean(unesc(y)); a.models.add(unesc(y)); }
    }
    return true;
}

} // namespace mc
