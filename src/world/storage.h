// storage.h : saves chunks into region files (8 x 8 x 8 chunks per file), like Minecraft's region format.
// Only chunks you changed are saved; untouched chunks are simply generated again.
#pragma once
#include "chunk.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace mc {

class RegionStore {
public:
    static constexpr int RS = 8;                 // chunks per region side
    explicit RegionStore(std::string dir);
    ~RegionStore() { flush(); }
    bool load(I3 cpos, Chunk& out);              // true if this chunk was saved before
    void save(const Chunk& c);                   // into the region cache (written by flush)
    void flush();                                // write changed regions to disk
    size_t cachedRegions() const { return regions.size(); }

    static std::vector<uint8_t> encode(const Chunk& c);   // run-length encoding of the block ids
    static bool decode(const std::vector<uint8_t>& data, Chunk& out);

private:
    struct Region { std::vector<std::vector<uint8_t>> blobs; bool dirty = false; uint64_t lastUse = 0; };
    Region& region(I3 rpos);
    std::string path(I3 rpos) const;
    void writeRegion(I3 rpos, const Region& r);
    std::string dir;
    std::unordered_map<I3, Region, I3Hash> regions;
    uint64_t useCounter = 0;
};

} // namespace mc
