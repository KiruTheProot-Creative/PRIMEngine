#pragma once
// Game projects: each game is one folder with everything it needs.
//   projects/<folder>/game.txt      name, start world, player rules
//   projects/<folder>/blocks.txt    the game's blocks
//   projects/<folder>/textures/     the game's pictures
//   projects/<folder>/worlds/       the game's worlds (levels)
#include <ctime>
#include <string>
#include <vector>

namespace mc {

struct ProjectEntry {
    std::string folder;      // full path
    std::string name;        // from game.txt
    int worlds = 0;
    std::time_t opened = 0;  // when game.txt was last written (opening a project writes it)
};

namespace Projects {
std::vector<ProjectEntry> list(const std::string& root);                        // most recently opened first
std::string create(const std::string& root, const std::string& name);           // makes the folder + game.txt; returns its path
std::string safeFolderName(const std::string& name);                             // "Space: Pirates!" -> "Space_ Pirates_"
std::string uniqueFolder(const std::string& parent, const std::string& name);    // adds " (2)" ... if taken
bool remove(const std::string& folder);
int countWorlds(const std::string& folder);
// before projects existed, the engine kept game\ and worlds\ next to the program: move them into a first project
std::string migrateOld(const std::string& exeDir, const std::string& root);       // returns the new project's path, or ""
std::string ago(std::time_t t, std::time_t now);                                 // "today", "yesterday", "3 days ago"
}

} // namespace mc
