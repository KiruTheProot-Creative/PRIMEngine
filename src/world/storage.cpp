#include "storage.h"
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace mc {

RegionStore::RegionStore(std::string d) : dir(std::move(d)) {
    std::error_code ec; std::filesystem::create_directories(dir, ec);
}

std::string RegionStore::path(I3 r) const {
    char b[96]; snprintf(b, sizeof b, "/r.%d.%d.%d.mcr", r.x, r.y, r.z); return dir + b;
}

static void put16(std::vector<uint8_t>& v, uint32_t x) { v.push_back((uint8_t)(x & 255)); v.push_back((uint8_t)(x >> 8)); }
static void put32(std::vector<uint8_t>& v, uint32_t x) { put16(v, x & 0xFFFF); put16(v, x >> 16); }
static uint32_t get32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

std::vector<uint8_t> RegionStore::encode(const Chunk& c) {
    std::vector<uint8_t> out; out.reserve(64);
    int i = 0;
    while (i < CS3) {
        int j = i; while (j < CS3 && c.b[j] == c.b[i] && j - i < 65535) j++;
        put16(out, c.b[i]); put16(out, (uint32_t)(j - i)); i = j;
    }
    return out;
}
bool RegionStore::decode(const std::vector<uint8_t>& d, Chunk& out) {
    size_t p = 0; int i = 0;
    while (p + 4 <= d.size()) {
        BlockId id = (BlockId)(d[p] | (d[p + 1] << 8)); int n = d[p + 2] | (d[p + 3] << 8); p += 4;
        if (i + n > CS3) return false;
        for (int k = 0; k < n; k++) out.b[i++] = id;
    }
    if (i != CS3) return false;
    out.recount(); return true;
}

RegionStore::Region& RegionStore::region(I3 rp) {
    auto it = regions.find(rp);
    if (it == regions.end()) {
        // keep at most 64 regions in memory: write out the least recently used one
        if (regions.size() >= 64) {
            auto victim = regions.begin();
            for (auto i2 = regions.begin(); i2 != regions.end(); ++i2) if (i2->second.lastUse < victim->second.lastUse) victim = i2;
            if (victim->second.dirty) writeRegion(victim->first, victim->second);
            regions.erase(victim);
        }
        Region r; r.blobs.resize(RS * RS * RS);
        std::ifstream f(path(rp), std::ios::binary);
        if (f) {
            std::vector<uint8_t> all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (all.size() >= 8 + RS * RS * RS * 4 && all[0] == 'M' && all[1] == 'C' && all[2] == 'R' && all[3] == 'G') {
                size_t off = 8 + RS * RS * RS * 4;
                for (int k = 0; k < RS * RS * RS; k++) {
                    uint32_t len = get32(&all[8 + k * 4]);
                    if (off + len > all.size()) break;
                    r.blobs[k].assign(all.begin() + off, all.begin() + off + len); off += len;
                }
            }
        }
        it = regions.emplace(rp, std::move(r)).first;
    }
    it->second.lastUse = ++useCounter;
    return it->second;
}

void RegionStore::writeRegion(I3 rp, const Region& r) {
    std::vector<uint8_t> out = { 'M', 'C', 'R', 'G' }; put32(out, 1);
    for (auto& b : r.blobs) put32(out, (uint32_t)b.size());
    for (auto& b : r.blobs) out.insert(out.end(), b.begin(), b.end());
    std::string p = path(rp), tmp = p + ".tmp";
    { std::ofstream f(tmp, std::ios::binary | std::ios::trunc); f.write((const char*)out.data(), (std::streamsize)out.size()); }
    std::error_code ec; std::filesystem::rename(tmp, p, ec);   // replace in one step so a crash never leaves half a file
    if (ec) { std::filesystem::remove(p, ec); std::filesystem::rename(tmp, p, ec); }
}

static int slot(I3 c) { int x = floorMod(c.x, RegionStore::RS), y = floorMod(c.y, RegionStore::RS), z = floorMod(c.z, RegionStore::RS); return (y * RegionStore::RS + z) * RegionStore::RS + x; }
static I3 regionOf(I3 c) { return { floorDiv(c.x, RegionStore::RS), floorDiv(c.y, RegionStore::RS), floorDiv(c.z, RegionStore::RS) }; }

bool RegionStore::load(I3 cpos, Chunk& out) {
    Region& r = region(regionOf(cpos));
    const auto& blob = r.blobs[slot(cpos)];
    if (blob.empty()) return false;
    return decode(blob, out);
}
void RegionStore::save(const Chunk& c) {
    Region& r = region(regionOf(c.pos));
    r.blobs[slot(c.pos)] = encode(c); r.dirty = true;
}
void RegionStore::flush() {
    for (auto& kv : regions) if (kv.second.dirty) { writeRegion(kv.first, kv.second); kv.second.dirty = false; }
}

} // namespace mc
