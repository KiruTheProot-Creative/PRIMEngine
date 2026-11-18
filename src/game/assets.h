#pragma once
// The asset manager (the Assets panel, like Unity's Project window): every asset of the game - blocks, textures, models,
// sounds, pictures, videos, enemy types, bosses, screens, scan entries, worlds and prefabs - lives in one folder
// structure you make yourself. The folders are the editor's own (assets.txt): the files stay where the engine keeps them,
// so nothing breaks when you sort. Searching ("kraid t:sound"), favourites, and prefabs (placed objects saved to be
// placed again) are here too. Plain C++, tested.
#include "folders.h"
#include "objects.h"
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mc {

enum class AssetType { Block, Texture, Model, Sound, Picture, Video, Enemy, Boss, Screen, Scan, World, Prefab, COUNT };
const char* assetTypeName(AssetType t);                  // "Sound"
const char* assetTypePlural(AssetType t);                // "Sounds" (the folder new ones of a kind start in)
const char* assetTypeKey(AssetType t);                   // "sound" (for searching: t:sound)

struct AssetId {
    AssetType type = AssetType::Block; std::string key;  // key: what the engine calls it (a file name, a block's name, an enemy type's ID...)
    std::string str() const;                             // "sound:roar.wav"
    static bool parse(const std::string& s, AssetId& out);
    bool operator<(const AssetId& o) const { return type != o.type ? type < o.type : key < o.key; }
    bool operator==(const AssetId& o) const { return type == o.type && key == o.key; }
};

struct AssetTree {
    FolderTree folders;                                  // "Bosses/Kraid" ...
    std::map<std::string, std::string> where;            // asset (its str()) -> its folder ("" : the top)
    std::set<std::string> favourites;                    // assets (str()) and folders ("folder:Bosses/Kraid")
    bool has(const AssetId& a) const { return where.count(a.str()) > 0; }
    std::string folderOf(const AssetId& a) const;
    void put(const AssetId& a, const std::string& folder);   // (its folder is made too)
    void forget(const AssetId& a);
    void renameAsset(const AssetId& a, const std::string& newKey);   // stays in its folder (and a favourite)
    bool renameFolder(const std::string& from, const std::string& to);   // what is inside goes along; false: not possible
    void removeFolder(const std::string& f);             // what is inside moves to its parent
    // assets the tree does not know yet (new ones, made anywhere) go into `folder(asset)`; returns how many
    int adopt(const std::vector<AssetId>& all, const std::function<std::string(const AssetId&)>& folder);
    void prune(const std::vector<AssetId>& all);         // ones that are gone are forgotten
    std::vector<AssetId> in(const std::string& folder) const;   // directly in this folder
    int countWithin(const std::string& folder) const;    // in it and its sub-folders
};
bool saveAssetTree(const std::string& path, const AssetTree& t);
bool loadAssetTree(const std::string& path, AssetTree& t);

// searching: words to find in the name, and "t:sound" / "t:model" ... for kinds (several: any of them)
struct AssetQuery { std::vector<std::string> words; std::set<AssetType> types; bool empty() const { return words.empty() && types.empty(); } };
AssetQuery parseAssetQuery(const std::string& q);
bool assetMatches(const AssetQuery& q, AssetType t, const std::string& name);
// a name that is not taken yet: "Kraid", "Kraid 2", "Kraid 3" ...; files keep their extension ("roar 2.wav")
std::string uniqueAssetName(const std::string& want, const std::function<bool(const std::string&)>& taken);

// ---- prefabs: objects (with all their settings) saved to be placed again; positions from a base point (their bottom middle)
struct Prefab { std::string name; std::vector<WorldObject> objects; };
Prefab makePrefab(const std::string& name, const std::vector<WorldObject>& objs);
// copies for the world, at `at`, turned by yawDeg (their places, ways and paths turn along); new IDs and free names
std::vector<WorldObject> placePrefab(const Prefab& p, V3 at, double yawDeg, uint32_t& nextId, const std::function<std::string(const std::string&)>& freeName);
bool savePrefab(const std::string& path, const Prefab& p);
bool loadPrefab(const std::string& path, Prefab& p);     // (its name: the file's)

} // namespace mc
