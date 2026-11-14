#pragma once
// Folders you make yourself, for textures, models, blocks and objects. They are only a way of sorting in the editor:
// the files stay where they are, so nothing breaks when you move things around. A folder is a path like
// "Weapons/Beams"; making one makes its parents too. Plain C++, tested on Linux.
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mc {

struct FolderTree {
    std::set<std::string> paths;                        // every folder, parents included

    static std::string clean(const std::string& p);     // "  a//b/ " -> "a/b"
    static std::string parent(const std::string& p);    // "a/b" -> "a"; "a" -> ""
    static std::string leaf(const std::string& p);      // "a/b" -> "b"
    static std::string join(const std::string& a, const std::string& b) { return a.empty() ? clean(b) : clean(a + "/" + b); }
    static bool within(const std::string& path, const std::string& folder);   // the folder itself or anything inside it ("" holds everything)
    static std::string moved(const std::string& path, const std::string& from, const std::string& to);   // path, if inside `from`, as if `from` were `to`

    void add(const std::string& p);
    std::vector<std::string> children(const std::string& parent) const;   // sorted
    // renaming / moving a folder (its inside goes with it); false if `to` is inside `from` or empty
    bool rename(const std::string& from, const std::string& to);
    void remove(const std::string& p);                  // the folder and the ones inside it (what was in them: see the callers)
    std::vector<std::string> all() const { return { paths.begin(), paths.end() }; }
};

struct AssetFolders {
    FolderTree textures, models, blocks, objects;       // blocks and objects keep their folder on themselves (category / folder)
    std::map<std::string, std::string> textureIn, modelIn;   // a texture's / model's folder by its name ("" or none: not in one)
    std::string folderOf(const std::map<std::string, std::string>& in, const std::string& name) const { auto it = in.find(name); return it == in.end() ? "" : it->second; }
    // a folder renamed or deleted: everything inside moves along (deleted: into its parent)
    static void renameIn(std::map<std::string, std::string>& in, const std::string& from, const std::string& to);
    static void dropIn(std::map<std::string, std::string>& in, const std::string& folder);
};
bool saveAssetFolders(const std::string& path, const AssetFolders& a);
bool loadAssetFolders(const std::string& path, AssetFolders& a);

} // namespace mc
