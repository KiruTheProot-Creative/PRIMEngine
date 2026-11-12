// library.h : the game's blocks and textures, shared by every world.
//   <game>/textures/*.png   one image per texture (the file name is the texture name)
//   <game>/blocks.txt       every block: id, name, shape, look and the texture on each face
// The first time, the built-in blocks are written out, so even stone can be repainted in any paint program.
#pragma once
#include "blocks.h"
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace mc {

enum class ImportAs { TexturesOnly, Blocks, CrossPlanes, FlatPlanes };

class Library {
public:
    std::string dir;                                      // UTF-8 path of the game folder
    std::string texturesDir() const { return dir + "/textures"; }
    std::string blocksFile() const { return dir + "/blocks.txt"; }

    bool load(BlockRegistry& r, std::string* report = nullptr);   // makes the default files if needed
    bool saveBlocks(const BlockRegistry& r) const;
    // files changed in the textures folder since the last load/reload (cheap: only looks at dates)
    bool texturesChanged() const;
    int reloadTextures(BlockRegistry& r);                 // re-reads new/changed files, drops deleted ones; returns how many changed
    // copies images into the textures folder (replacing same names) and optionally makes a block for each new one
    struct ImportResult { std::vector<std::string> textures; std::vector<BlockId> blocks; std::vector<std::string> failed; };
    ImportResult import(const std::vector<std::string>& files, ImportAs as, BlockRegistry& r);
    bool deleteTexture(const std::string& name, BlockRegistry& r);

    static std::string textureNameFor(const std::string& file);    // "My Rock.png" -> "My_Rock"
    static BlockDef blockFromTexture(const BlockRegistry& r, const std::string& tex, ImportAs as);

private:
    std::map<std::string, std::filesystem::file_time_type> stamps;   // texture file -> date when loaded
    std::map<std::string, std::filesystem::file_time_type> scan() const;
    bool loadTexture(BlockRegistry& r, const std::string& name);
};

} // namespace mc
