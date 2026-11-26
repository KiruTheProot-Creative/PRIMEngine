// PRIMEngine on Direct3D 12 - see d3d12shim.h
#include "d3d12shim.h"
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace d12 {

static const UINT FRAMES = 2;                 // frames in flight
static const UINT RING_DESC = 400000;         // shader-visible descriptors a frame may use (more: it waits for the GPU and starts over)
static const UINT SMP_HEAP = 2048;
static const UINT SRV_SLOTS = 64, CB_SLOTS = 14, SMP_SLOTS = 16, UAV_SLOTS = 8, VB_SLOTS = 8, RT_SLOTS = 8;
static const UINT PAGE_SIZE = 32u << 20, PAGE_MAX_ALLOC = 4u << 20, UP_PAGE = 16u << 20;
static const D3D12_RESOURCE_STATES ST_SR = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
static const D3D12_RESOURCE_STATES ST_BUFREAD = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER | ST_SR;
static bool IsRead(D3D12_RESOURCE_STATES s) {
    const D3D12_RESOURCE_STATES W = D3D12_RESOURCE_STATE_RENDER_TARGET | D3D12_RESOURCE_STATE_UNORDERED_ACCESS | D3D12_RESOURCE_STATE_DEPTH_WRITE |
        D3D12_RESOURCE_STATE_STREAM_OUT | D3D12_RESOURCE_STATE_COPY_DEST | D3D12_RESOURCE_STATE_RESOLVE_DEST;
    return s != 0 && (s & W) == 0;
}
static uint64_t AlignUp(uint64_t v, uint64_t a) { return a <= 1 ? v : (v + a - 1) / a * a; }
static UINT Gcd(UINT a, UINT b) { while (b) { UINT t = a % b; a = b; b = t; } return a; }

// ---------------------------------------------------------------- descriptor heaps the CPU keeps views in
struct CpuHeap {
    D3D12_DESCRIPTOR_HEAP_TYPE type{}; UINT inc = 0; std::vector<ComPtr<ID3D12DescriptorHeap>> heaps; std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> free;
    D3D12_CPU_DESCRIPTOR_HANDLE alloc(ID3D12Device* d) {
        if (free.empty()) {
            D3D12_DESCRIPTOR_HEAP_DESC hd{ type, 1024, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 }; ComPtr<ID3D12DescriptorHeap> h;
            if (FAILED(d->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&h)))) return {};
            D3D12_CPU_DESCRIPTOR_HANDLE s = h->GetCPUDescriptorHandleForHeapStart();
            for (UINT i = 1024; i-- > 0;) free.push_back({ s.ptr + (SIZE_T)i * inc });
            heaps.push_back(h);
        }
        D3D12_CPU_DESCRIPTOR_HANDLE r = free.back(); free.pop_back(); return r;
    }
    void release(D3D12_CPU_DESCRIPTOR_HANDLE h) { if (h.ptr) free.push_back(h); }
};

// ---------------------------------------------------------------- upload memory, a frame at a time
struct UpPage { ComPtr<ID3D12Resource> res; uint8_t* cpu = nullptr; uint64_t size = 0, used = 0; };
struct Ring {
    std::vector<UpPage> pages; size_t cur = 0;
    bool alloc(ID3D12Device* d, uint64_t n, uint64_t align, ID3D12Resource** r, uint64_t* off, uint8_t** p) {
        for (;;) {
            if (cur < pages.size()) {
                UpPage& pg = pages[cur]; uint64_t o = AlignUp(pg.used, align);
                if (o + n <= pg.size) { pg.used = o + n; *r = pg.res.Get(); *off = o; *p = pg.cpu + o; return true; }
                if (pg.used == 0 && n > pg.size) {}   // (too small for this: a bigger one after it)
                cur++; continue;
            }
            UpPage pg; pg.size = std::max<uint64_t>(UP_PAGE, AlignUp(n + align, 65536));
            D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_UPLOAD };
            D3D12_RESOURCE_DESC rd{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, pg.size, 1, 1, 1, DXGI_FORMAT_UNKNOWN, { 1, 0 }, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
            if (FAILED(d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&pg.res)))) return false;
            D3D12_RANGE none{ 0, 0 }; pg.res->Map(0, &none, (void**)&pg.cpu);
            pages.insert(pages.begin() + (ptrdiff_t)cur, pg);
        }
    }
    void reset() {   // the big one-off pages go; the usual ones stay for the next frame
        pages.erase(std::remove_if(pages.begin(), pages.end(), [](const UpPage& p) { return p.size > UP_PAGE; }), pages.end());
        if (pages.size() > 6) pages.resize(6);
        for (UpPage& p : pages) p.used = 0; cur = 0;
    }
};

// ---------------------------------------------------------------- shared pages for small vertex and index buffers
struct Page {
    ComPtr<ID3D12Resource> res; uint64_t size = 0; std::map<uint64_t, uint64_t> freeBlocks;   // offset -> size
    std::vector<D3D12_RESOURCE_STATES> st{ 1, D3D12_RESOURCE_STATE_COMMON };
    bool take(uint64_t n, uint64_t* off) {
        for (auto it = freeBlocks.begin(); it != freeBlocks.end(); ++it) if (it->second >= n) {
            *off = it->first; uint64_t rest = it->second - n, at = it->first + n; freeBlocks.erase(it); if (rest) freeBlocks[at] = rest; return true; }
        return false;
    }
    void give(uint64_t off, uint64_t n) {
        auto it = freeBlocks.emplace(off, n).first;
        auto nx = std::next(it); if (nx != freeBlocks.end() && it->first + it->second == nx->first) { it->second += nx->second; freeBlocks.erase(nx); }
        if (it != freeBlocks.begin()) { auto pv = std::prev(it); if (pv->first + pv->second == it->first) { pv->second += it->second; freeBlocks.erase(it); } }
    }
};

// ---------------------------------------------------------------- what is bound (as the D3D11 context had it)
struct StageB { ComPtr<ShaderBase> sh; ComPtr<ShaderResourceView> srv[SRV_SLOTS]; ComPtr<Buffer> cb[CB_SLOTS]; ComPtr<SamplerState> smp[SMP_SLOTS]; };
struct DeviceContext::Saved {
    StageB vs, ps; ComPtr<UnorderedAccessView> uav[UAV_SLOTS]; ComPtr<RenderTargetView> rtv[RT_SLOTS]; UINT nrt = 0; ComPtr<DepthStencilView> dsv;
    ComPtr<InputLayout> il; D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ComPtr<Buffer> vb[VB_SLOTS]; UINT vbStride[VB_SLOTS] = {}, vbOff[VB_SLOTS] = {}; ComPtr<Buffer> ib; DXGI_FORMAT ibFmt = DXGI_FORMAT_R16_UINT; UINT ibOff = 0;
    ComPtr<BlendState> blend; float bf[4] = { 1, 1, 1, 1 }; UINT mask = 0xFFFFFFFF; ComPtr<RasterizerState> rs; ComPtr<DepthStencilState> ds; UINT sref = 0;
    D3D11_VIEWPORT vp[8] = {}; UINT nvp = 0; D3D11_RECT sc[8] = {}; UINT nsc = 0;
};
enum : uint32_t { DT_VS_SRV = 1, DT_VS_CB = 2, DT_VS_SMP = 4, DT_PS_SRV = 8, DT_PS_CB = 16, DT_PS_SMP = 32, DT_UAV = 64, DT_OM = 128, DT_VP = 256, DT_VB = 512, DT_IB = 1024,
                  DT_PSO = 2048, DT_TOPO = 4096, DT_BF = 8192, DT_SREF = 16384, DT_ALL = 0xFFFFFFFF };
enum { RP_VS_CB, RP_VS_SRV, RP_VS_SMP, RP_PS_CB, RP_PS_SRV, RP_PS_SMP, RP_PS_UAV };

struct PsoKey {
    uint32_t vs, ps, il, bl, rs, ds, mask; uint32_t topo, nrt; DXGI_FORMAT rt[8], dsv;
    bool operator==(const PsoKey& o) const { return std::memcmp(this, &o, sizeof *this) == 0; }
};
struct PsoHash { size_t operator()(const PsoKey& k) const { const uint8_t* p = (const uint8_t*)&k; uint64_t h = 1469598103934665603ull; for (size_t i = 0; i < sizeof k; i++) { h ^= p[i]; h *= 1099511628211ull; } return (size_t)h; } };

struct Frame { ComPtr<ID3D12CommandAllocator> alloc; uint64_t fence = 0; Ring ring; };

struct Core {
    std::recursive_mutex mx;
    ComPtr<IDXGIFactory6> factory; ComPtr<IDXGIAdapter1> adapter; DXGI_ADAPTER_DESC1 adesc{};
    ComPtr<ID3D12Device> dev; ComPtr<ID3D12Device5> dev5; bool rt = false;
    ComPtr<ID3D12CommandQueue> queue; ComPtr<ID3D12Fence> fence; HANDLE ev = nullptr; uint64_t signaled = 0;
    Frame fr[FRAMES]; UINT slot = 0; uint64_t epoch = 1;
    ComPtr<ID3D12GraphicsCommandList> list; ComPtr<ID3D12GraphicsCommandList4> list4;
    ComPtr<ID3D12RootSignature> root;
    CpuHeap cpuSrv, cpuRtv, cpuDsv, cpuSmp;
    ComPtr<ID3D12DescriptorHeap> gpuHeap, smpHeap; UINT gpuInc = 0, smpInc = 0; UINT gpuPos = 0, gpuEnd = 0;
    std::map<std::vector<uint32_t>, UINT> smpTables; UINT smpPos = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE nullSrv[11]{}, nullStruct{}, nullRaw{}, nullUav[9]{}, nullRtv{}, scratchRtv{};
    ComPtr<ID3D12Resource> zeroCb;
    ComPtr<SamplerState> defSampler, mipSampler; UINT mipSmpTable = 0;
    std::unordered_map<PsoKey, ComPtr<ID3D12PipelineState>, PsoHash> psos;
    D3D12_BLEND_DESC defBlend{}; D3D12_RASTERIZER_DESC defRaster{}; D3D12_DEPTH_STENCIL_DESC defDepth{};
    ComPtr<ID3D12QueryHeap> qheap; ComPtr<ID3D12Resource> qbuf; uint64_t* qcpu = nullptr; std::vector<UINT> qfree; UINT64 tsFreq = 1;
    std::vector<std::unique_ptr<Page>> pages;
    std::vector<std::pair<uint64_t, DeviceChild*>> dead;
    std::atomic<uint32_t> uidNext{ 1 };
    ComPtr<IDXGISwapChain3> swap; std::vector<ComPtr<Texture2D>> bufs; ComPtr<Texture2D> proxy; UINT bbIndex = 0; UINT swapFlags = 0; DXGI_FORMAT swapFormat{};
    std::vector<uint8_t> mipVS, mipPS; std::map<DXGI_FORMAT, ComPtr<ID3D12PipelineState>> mipPso;
    std::string logPath; bool debug = false; ComPtr<ID3D12InfoQueue> iq; int logged = 0; bool removedLogged = false;
    Device device; DeviceContext context; SwapChain swapObj;
    DeviceContext::Saved b; uint32_t dirty = DT_ALL; ID3D12PipelineState* curPso = nullptr; bool uavDrawn = false; UINT nvbSet = 0;
    // ray tracing
    bool rtInline = false; ComPtr<ID3D12RootSignature> rootCS; std::unordered_map<uint32_t, ComPtr<ID3D12PipelineState>> csPsos;
    std::vector<std::unique_ptr<Page>> asPages;
    struct Scratch { ComPtr<ID3D12Resource> res; uint64_t size = 0, used = 0; } scratch[FRAMES];
    std::vector<std::pair<uint64_t, ComPtr<ID3D12Resource>>> deadRes;
    ComPtr<ID3D12Resource> tlas[FRAMES]; uint64_t tlasSize[FRAMES] = {}; D3D12_GPU_VIRTUAL_ADDRESS tlasVA = 0; bool tlasReady = false, blasPending = false;
    std::vector<Resource*> geoRes; ID3D12Resource* instRes = nullptr; uint64_t instOff = 0; UINT instCount = 0;
    std::vector<D3D12_RESOURCE_BARRIER> barriers; uint64_t stateSerial = 1, checkedSerial = 0;   // (a barrier anywhere: what is bound is checked again at the next draw)

    // -------- fences and frames
    void signal() { queue->Signal(fence.Get(), ++signaled); }
    void waitFor(uint64_t v) { if (v && fence->GetCompletedValue() < v) { fence->SetEventOnCompletion(v, ev); WaitForSingleObject(ev, INFINITE); } }
    void setupList() {
        ID3D12DescriptorHeap* hs[2] = { gpuHeap.Get(), smpHeap.Get() }; list->SetDescriptorHeaps(2, hs);
        list->SetGraphicsRootSignature(root.Get()); dirty = DT_ALL; curPso = nullptr; uavDrawn = false;
    }
    void resetRings() { scratch[slot].used = 0; tlasReady = false; fr[slot].ring.reset(); gpuPos = slot * RING_DESC; gpuEnd = gpuPos + RING_DESC - SRV_SLOTS; epoch++; }   // (a table is declared 64 long: never closer than that to the end)
    void submit() { flushBarriers(); list->Close(); ID3D12CommandList* l[1] = { list.Get() }; queue->ExecuteCommandLists(1, l); }
    void flush(bool wait) {   // in the middle of a frame: what is recorded goes to the GPU (and, wait: is done)
        submit(); signal(); fr[slot].fence = signaled;
        if (wait) { waitFor(signaled); fr[slot].alloc->Reset(); resetRings(); collect(); }
        list->Reset(fr[slot].alloc.Get(), nullptr); setupList();
    }
    void collect() {
        uint64_t done = fence->GetCompletedValue(); std::vector<DeviceChild*> go;
        for (size_t i = 0; i < dead.size();) { if (dead[i].first <= done) { go.push_back(dead[i].second); dead[i] = dead.back(); dead.pop_back(); } else i++; }
        for (DeviceChild* c : go) delete c;
        for (size_t i = 0; i < deadRes.size();) { if (deadRes[i].first <= done) { deadRes[i] = deadRes.back(); deadRes.pop_back(); } else i++; }
    }
    void uavBarrier(ID3D12Resource* r = nullptr) { D3D12_RESOURCE_BARRIER ub{}; ub.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; ub.UAV.pResource = r; barriers.push_back(ub); }
    ComPtr<ID3D12Resource> makeBuffer(uint64_t n, D3D12_RESOURCE_STATES st, bool uav) {
        D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_DEFAULT };
        D3D12_RESOURCE_DESC bd{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, n, 1, 1, 1, DXGI_FORMAT_UNKNOWN, { 1, 0 }, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE };
        ComPtr<ID3D12Resource> r; if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, st, nullptr, IID_PPV_ARGS(&r)))) return nullptr; return r;
    }
    // scratch memory for building acceleration structures (this frame's; full: wait for the builds before and start over)
    D3D12_GPU_VIRTUAL_ADDRESS scratchAlloc(uint64_t n) {
        Scratch& sc = scratch[slot]; n = AlignUp(std::max<uint64_t>(n, 256), 256);
        if (!sc.res || n > sc.size) {
            if (sc.res) { uavBarrier(); deadRes.push_back({ signaled + 1, sc.res }); }
            sc.size = std::max<uint64_t>(n, 64ull << 20); sc.res = makeBuffer(sc.size, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true); sc.used = 0;
            if (!sc.res) return 0;
        } else if (sc.used + n > sc.size) { uavBarrier(); flushBarriers(); sc.used = 0; }
        D3D12_GPU_VIRTUAL_ADDRESS va = sc.res->GetGPUVirtualAddress() + sc.used; sc.used += n; return va;
    }
    // where acceleration structures live: shared pages (they never change state)
    Page* asAlloc(uint64_t n, uint64_t* off) {
        for (auto& p : asPages) if (p->take(n, off)) return p.get();
        auto np = std::make_unique<Page>(); np->size = std::max<uint64_t>(64ull << 20, AlignUp(n, 65536));
        np->res = makeBuffer(np->size, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, true); if (!np->res) return nullptr;
        np->freeBlocks[0] = np->size; np->take(n, off); asPages.push_back(std::move(np)); return asPages.back().get();
    }
    void retire(DeviceChild* c) { std::lock_guard<std::recursive_mutex> lk(mx); dead.push_back({ signaled + 1, c }); }
    // -------- barriers: a resource's subresources into the state they are needed in
    void needSub(Resource* r, UINT sub, D3D12_RESOURCE_STATES want) {
        std::vector<D3D12_RESOURCE_STATES>& st = r->states(); if (sub >= st.size()) return;
        D3D12_RESOURCE_STATES cur = st[sub], next = want;
        if (IsRead(cur) && IsRead(want)) { if ((cur & want) == want) return; next = cur | want; }
        else if (cur == want) return;
        D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = r->gres;
        b.Transition.Subresource = r->page ? D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES : (st.size() == 1 ? D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES : sub);
        b.Transition.StateBefore = cur; b.Transition.StateAfter = next; barriers.push_back(b); st[sub] = next; stateSerial++;
    }
    void needAll(Resource* r, D3D12_RESOURCE_STATES want) {
        r = r->real(); if (!r || r->virt || r->staging || !r->gres) return;
        std::vector<D3D12_RESOURCE_STATES>& st = r->states();
        bool same = true; for (size_t i = 1; i < st.size() && same; i++) same = st[i] == st[0];
        if (same && st.size() > 1) {
            D3D12_RESOURCE_STATES cur = st[0], next = want;
            if (IsRead(cur) && IsRead(want)) { if ((cur & want) == want) return; next = cur | want; } else if (cur == want) return;
            D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = r->gres; b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            b.Transition.StateBefore = cur; b.Transition.StateAfter = next; barriers.push_back(b); for (auto& s : st) s = next; stateSerial++; return;
        }
        for (UINT i = 0; i < (UINT)st.size(); i++) needSub(r, i, want);
    }
    void needView(View* v, D3D12_RESOURCE_STATES want) {
        Resource* r = v->res->real(); if (!r || r->virt || r->staging || !r->gres) return;
        if (r->kind == Kind::Buffer || r->subresources() == 1) { needSub(r, 0, want); return; }
        const SubRange& R = v->range; UINT slices = r->kind == Kind::Tex3D ? 1 : r->arraySize;
        if (R.mip0 == 0 && R.mips >= r->mips && R.slice0 == 0 && R.slices >= slices) { needAll(r, want); return; }
        for (UINT s = R.slice0; s < std::min(slices, R.slice0 + R.slices); s++) for (UINT m = R.mip0; m < std::min(r->mips, R.mip0 + R.mips); m++) needSub(r, m + s * r->mips, want);
    }
    void flushBarriers() { if (!barriers.empty()) { list->ResourceBarrier((UINT)barriers.size(), barriers.data()); barriers.clear(); } }
    // -------- shader-visible descriptors: a run of n for this draw
    bool room(UINT n) { if (gpuPos + n <= gpuEnd) return true; flush(true); return false; }   // false: it started over (everything is set again)
    UINT take(UINT n) { UINT i = gpuPos; gpuPos += n; return i; }
    D3D12_CPU_DESCRIPTOR_HANDLE gcpu(UINT i) { D3D12_CPU_DESCRIPTOR_HANDLE h = gpuHeap->GetCPUDescriptorHandleForHeapStart(); h.ptr += (SIZE_T)i * gpuInc; return h; }
    D3D12_GPU_DESCRIPTOR_HANDLE ggpu(UINT i) { D3D12_GPU_DESCRIPTOR_HANDLE h = gpuHeap->GetGPUDescriptorHandleForHeapStart(); h.ptr += (UINT64)i * gpuInc; return h; }
    // -------- the frame's upload memory
    uint8_t* upload(uint64_t n, uint64_t align, ID3D12Resource** r, uint64_t* off) { uint8_t* p = nullptr; if (!fr[slot].ring.alloc(dev.Get(), n, align, r, off, &p)) return nullptr; return p; }
    // a constant / dynamic buffer into this frame's memory (again if it was put there in an earlier one)
    UINT virtAlign(Resource* r) { if (r->bind & D3D11_BIND_CONSTANT_BUFFER) return 256; if (r->stride) { UINT s = r->stride; return s / Gcd(s, 16) * 16; } return 16; }
    bool refresh(Resource* r) {
        if (!r->virt || r->vepoch == epoch) return false;
        uint64_t n = r->bind & D3D11_BIND_CONSTANT_BUFFER ? AlignUp(r->byteWidth, 256) : r->byteWidth;
        uint8_t* p = upload(std::max<uint64_t>(n, 16), virtAlign(r), &r->vres, &r->voff); if (!p) return false;
        std::memcpy(p, r->shadow.data(), r->shadow.size()); r->vepoch = epoch; return true;
    }
    void renamed(Resource* r) {   // its data moved: whatever binds it is set again
        for (UINT i = 0; i < CB_SLOTS; i++) { if (b.vs.cb[i].Get() == r) dirty |= DT_VS_CB; if (b.ps.cb[i].Get() == r) dirty |= DT_PS_CB; }
        for (UINT i = 0; i < VB_SLOTS; i++) if (b.vb[i].Get() == r) dirty |= DT_VB;
        if (b.ib.Get() == r) dirty |= DT_IB;
        for (UINT i = 0; i < SRV_SLOTS; i++) { if (b.vs.srv[i] && b.vs.srv[i]->res.Get() == r) dirty |= DT_VS_SRV; if (b.ps.srv[i] && b.ps.srv[i]->res.Get() == r) dirty |= DT_PS_SRV; }
    }
    // -------- copies into GPU memory
    void copyIntoBuffer(Resource* r, uint64_t at, const void* data, uint64_t n) {
        ID3D12Resource* ur; uint64_t uo; uint8_t* p = upload(n, 16, &ur, &uo); if (!p) return;
        std::memcpy(p, data, (size_t)n); needSub(r, 0, D3D12_RESOURCE_STATE_COPY_DEST); flushBarriers();
        list->CopyBufferRegion(r->gres, r->goff + at, ur, uo, n);
    }
    void copyIntoTexture(Resource* r, UINT sub, const D3D11_BOX* box, const void* data, UINT rowPitch, UINT depthPitch);
    void log(const std::string& s) {
        if (logPath.empty() || logged > 20000) return; logged++;
        std::ofstream f(logPath, std::ios::app); f << s << "\n";
    }
    void pollMessages() {
        if (!iq) return; UINT64 n = iq->GetNumStoredMessages();
        for (UINT64 i = 0; i < n && logged < 20000; i++) {
            SIZE_T len = 0; iq->GetMessage(i, nullptr, &len); std::vector<uint8_t> m(len); D3D12_MESSAGE* msg = (D3D12_MESSAGE*)m.data();
            if (SUCCEEDED(iq->GetMessage(i, msg, &len))) { const char* sev[] = { "CORRUPTION", "ERROR", "WARNING", "INFO", "MESSAGE" }; log(std::string("[D3D12 ") + sev[std::min<int>(msg->Severity, 4)] + "] " + std::string(msg->pDescription, msg->DescriptionByteLength ? msg->DescriptionByteLength - 1 : 0)); }
        }
        iq->ClearStoredMessages();
    }
    void deviceLost(HRESULT hr) {
        if (removedLogged) return; removedLogged = true;
        char b2[128]; snprintf(b2, sizeof b2, "GPU device removed: 0x%08X, reason 0x%08X", (unsigned)hr, (unsigned)dev->GetDeviceRemovedReason()); log(b2);
        ComPtr<ID3D12DeviceRemovedExtendedData> dred;
        if (SUCCEEDED(dev.As(&dred))) {
            D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT bc{};
            if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&bc))) for (const D3D12_AUTO_BREADCRUMB_NODE* n = bc.pHeadAutoBreadcrumbNode; n; n = n->pNext) {
                UINT done = n->pLastBreadcrumbValue ? *n->pLastBreadcrumbValue : 0;
                snprintf(b2, sizeof b2, "  command list: %u of %u steps done", done, n->BreadcrumbCount); log(b2);
                if (done < n->BreadcrumbCount) { snprintf(b2, sizeof b2, "  it stopped at step %u (op %d)", done, (int)n->pCommandHistory[done]); log(b2); }
            }
        }
        pollMessages();
    }
};
static Core* g = nullptr;
#define LK std::lock_guard<std::recursive_mutex> lk_(g->mx)

// ---------------------------------------------------------------- objects
DeviceChild::DeviceChild() : uid(g ? g->uidNext++ : 0) {}
ULONG DeviceChild::Release() { long n = --refs; if (n == 0) { if (g) g->retire(this); else delete this; } return (ULONG)std::max(0l, n); }
std::vector<D3D12_RESOURCE_STATES>& Resource::states() { return page ? page->st : ownStates; }
Resource* Resource::real() { return swapOf && g && !g->bufs.empty() ? (Resource*)g->bufs[g->bbIndex].Get() : this; }
D3D12_GPU_VIRTUAL_ADDRESS Resource::gpuVA() { Resource* r = real(); if (r->virt) return r->vres ? r->vres->GetGPUVirtualAddress() + r->voff : 0; return r->gres ? r->gres->GetGPUVirtualAddress() + r->goff : 0; }
Resource::~Resource() { if (page && g) page->give(goff, AlignUp(byteWidth, 256)); }
View::~View() { if (!g) return; CpuHeap* h = heapType == 1 ? &g->cpuRtv : heapType == 2 ? &g->cpuDsv : &g->cpuSrv; h->release(cpu); for (auto& p : perBuffer) h->release(p); }
SamplerState::~SamplerState() { if (g) g->cpuSmp.release(cpu); }
AccelStruct::~AccelStruct() { if (page && g) page->give(off, size); }
D3D12_CPU_DESCRIPTOR_HANDLE View::handle() { if (!perBuffer.empty() && g) return perBuffer[std::min<size_t>(g->bbIndex, perBuffer.size() - 1)]; return cpu; }
bool View::overlaps(const View& o) const {
    if (!res || !o.res || res->real() != o.res->real()) return false;
    bool m = range.mip0 < o.range.mip0 + o.range.mips && o.range.mip0 < range.mip0 + range.mips;
    bool s = range.slice0 < o.range.slice0 + o.range.slices && o.range.slice0 < range.slice0 + range.slices;
    return m && s;
}
void Buffer::GetDesc(D3D11_BUFFER_DESC* d) { *d = {}; d->ByteWidth = byteWidth; d->Usage = usage; d->BindFlags = bind; d->CPUAccessFlags = cpu; d->MiscFlags = misc; d->StructureByteStride = stride; }
void Texture2D::GetDesc(D3D11_TEXTURE2D_DESC* d) {
    Resource* r = real(); *d = {}; d->Width = r->width; d->Height = r->height; d->MipLevels = r->mips; d->ArraySize = r->arraySize; d->Format = r->format; d->SampleDesc = { 1, 0 };
    d->Usage = r->usage; d->BindFlags = r->bind; d->CPUAccessFlags = r->cpu; d->MiscFlags = r->misc;
}
void Texture3D::GetDesc(D3D11_TEXTURE3D_DESC* d) { *d = {}; d->Width = width; d->Height = height; d->Depth = depth; d->MipLevels = mips; d->Format = format; d->Usage = usage; d->BindFlags = bind; d->CPUAccessFlags = cpu; d->MiscFlags = misc; }

// ---------------------------------------------------------------- formats
static UINT BitsPerPixel(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R32G32B32A32_UINT: case DXGI_FORMAT_R32G32B32A32_SINT: return 128;
    case DXGI_FORMAT_R32G32B32_TYPELESS: case DXGI_FORMAT_R32G32B32_FLOAT: case DXGI_FORMAT_R32G32B32_UINT: case DXGI_FORMAT_R32G32B32_SINT: return 96;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_UNORM: case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM: case DXGI_FORMAT_R16G16B16A16_SINT: case DXGI_FORMAT_R32G32_TYPELESS: case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_UINT: case DXGI_FORMAT_R32G32_SINT: case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return 64;
    case DXGI_FORMAT_R8_TYPELESS: case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_R8_UINT: case DXGI_FORMAT_R8_SNORM: case DXGI_FORMAT_R8_SINT: case DXGI_FORMAT_A8_UNORM: return 8;
    case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_R16_FLOAT: case DXGI_FORMAT_D16_UNORM: case DXGI_FORMAT_R16_UNORM: case DXGI_FORMAT_R16_UINT: case DXGI_FORMAT_R16_SNORM:
    case DXGI_FORMAT_R16_SINT: case DXGI_FORMAT_R8G8_TYPELESS: case DXGI_FORMAT_R8G8_UNORM: case DXGI_FORMAT_R8G8_UINT: case DXGI_FORMAT_R8G8_SNORM: case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_B5G6R5_UNORM: case DXGI_FORMAT_B5G5R5A1_UNORM: case DXGI_FORMAT_B4G4R4A4_UNORM: return 16;
    case DXGI_FORMAT_BC1_TYPELESS: case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB: case DXGI_FORMAT_BC4_TYPELESS: case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC4_SNORM: return 4;
    case DXGI_FORMAT_BC2_TYPELESS: case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB: case DXGI_FORMAT_BC3_TYPELESS: case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC5_TYPELESS: case DXGI_FORMAT_BC5_UNORM: case DXGI_FORMAT_BC5_SNORM: case DXGI_FORMAT_BC6H_TYPELESS: case DXGI_FORMAT_BC6H_UF16: case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_TYPELESS: case DXGI_FORMAT_BC7_UNORM: case DXGI_FORMAT_BC7_UNORM_SRGB: return 8;
    default: return 32;
    }
}
static bool IsBC(DXGI_FORMAT f) { return (f >= DXGI_FORMAT_BC1_TYPELESS && f <= DXGI_FORMAT_BC5_SNORM) || (f >= DXGI_FORMAT_BC6H_TYPELESS && f <= DXGI_FORMAT_BC7_UNORM_SRGB); }
static void RowsOf(DXGI_FORMAT f, UINT w, UINT h, UINT* rowBytes, UINT* rows) {
    if (IsBC(f)) { UINT bw = std::max(1u, (w + 3) / 4), bh = std::max(1u, (h + 3) / 4); *rowBytes = bw * BitsPerPixel(f) * 2; *rows = bh; }   // (4 bits a pixel: 8 bytes a 4x4 block)
    else { *rowBytes = (w * BitsPerPixel(f) + 7) / 8; *rows = h; }
}

void Core::copyIntoTexture(Resource* r, UINT sub, const D3D11_BOX* box, const void* data, UINT rowPitch, UINT depthPitch) {
    UINT mip = sub % r->mips, w = std::max(1u, r->width >> mip), h = std::max(1u, r->height >> mip), d = r->kind == Kind::Tex3D ? std::max(1u, r->depth >> mip) : 1;
    UINT x = 0, y = 0, z = 0; if (box) { x = box->left; y = box->top; z = box->front; w = box->right - box->left; h = box->bottom - box->top; d = box->back - box->front; }
    if (!w || !h || !d) return;
    UINT rowBytes, rows; RowsOf(r->format, w, h, &rowBytes, &rows);
    UINT pitch = (UINT)AlignUp(rowBytes, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
    ID3D12Resource* ur; uint64_t uo; uint8_t* p = upload((uint64_t)pitch * rows * d, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, &ur, &uo); if (!p) return;
    if (!rowPitch) rowPitch = rowBytes; if (!depthPitch) depthPitch = rowPitch * rows;
    for (UINT k = 0; k < d; k++) for (UINT j = 0; j < rows; j++) std::memcpy(p + (size_t)k * pitch * rows + (size_t)j * pitch, (const uint8_t*)data + (size_t)k * depthPitch + (size_t)j * rowPitch, rowBytes);
    needSub(r, sub, D3D12_RESOURCE_STATE_COPY_DEST); flushBarriers();
    D3D12_TEXTURE_COPY_LOCATION dst{ r->gres, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX }; dst.SubresourceIndex = sub;
    D3D12_TEXTURE_COPY_LOCATION src{ ur, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT };
    src.PlacedFootprint.Offset = uo; src.PlacedFootprint.Footprint = { r->format, IsBC(r->format) ? (w + 3) / 4 * 4 : w, IsBC(r->format) ? (h + 3) / 4 * 4 : h, d, pitch };
    list->CopyTextureRegion(&dst, x, y, z, &src, nullptr);
}

// ---------------------------------------------------------------- the device
ID3D12Device* Device::d3d() { return g->dev.Get(); }
ID3D12Device5* Device::d3d5() { return g->rt ? g->dev5.Get() : nullptr; }
const DXGI_ADAPTER_DESC1& Device::adapter() { return g->adesc; }
bool Device::raytracing() { return g->rt; }

static D3D12_RESOURCE_DESC TexDesc(Kind k, UINT w, UINT h, UINT dOrA, UINT mips, DXGI_FORMAT f, UINT bind) {
    D3D12_RESOURCE_DESC d{}; d.Dimension = k == Kind::Tex3D ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : k == Kind::Tex1D ? D3D12_RESOURCE_DIMENSION_TEXTURE1D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w; d.Height = h; d.DepthOrArraySize = (UINT16)dOrA; d.MipLevels = (UINT16)mips; d.Format = f; d.SampleDesc = { 1, 0 }; d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (bind & D3D11_BIND_RENDER_TARGET) d.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (bind & D3D11_BIND_DEPTH_STENCIL) { d.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL; if (!(bind & D3D11_BIND_SHADER_RESOURCE)) d.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE; }
    if (bind & D3D11_BIND_UNORDERED_ACCESS) d.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    return d;
}
static UINT FullMips(UINT w, UINT h, UINT d) { UINT m = 1, s = std::max(w, std::max(h, d)); while (s > 1) { s >>= 1; m++; } return m; }

static HRESULT MakeTexture(Resource* t, const D3D11_SUBRESOURCE_DATA* init) {
    t->mips = t->mips ? t->mips : FullMips(t->width, t->height, t->kind == Kind::Tex3D ? t->depth : 1);
    if (t->misc & D3D11_RESOURCE_MISC_GENERATE_MIPS) t->bind |= D3D11_BIND_RENDER_TARGET;
    D3D12_RESOURCE_DESC rd = TexDesc(t->kind, t->width, t->height, t->kind == Kind::Tex3D ? t->depth : t->arraySize, t->mips, t->format, t->bind);
    if (t->usage == D3D11_USAGE_STAGING) {   // read back: a buffer laid out like the texture
        UINT n = t->subresources(); t->fp.resize(n); t->fpRows.resize(n); std::vector<UINT64> rs(n); UINT64 total = 0;
        g->dev->GetCopyableFootprints(&rd, 0, n, 0, t->fp.data(), t->fpRows.data(), rs.data(), &total);
        D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_READBACK };
        D3D12_RESOURCE_DESC bd{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, std::max<UINT64>(total, 256), 1, 1, 1, DXGI_FORMAT_UNKNOWN, { 1, 0 }, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
        HRESULT hr = g->dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&t->own)); if (FAILED(hr)) return hr;
        t->gres = t->own.Get(); t->staging = true; t->own->Map(0, nullptr, (void**)&t->rbPtr); return S_OK;
    }
    D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_DEFAULT };
    D3D12_CLEAR_VALUE cv{}; const D3D12_CLEAR_VALUE* pcv = nullptr;
    if (t->bind & D3D11_BIND_DEPTH_STENCIL) {   // (a clear value saves a warning each clear; the typeless depth formats clear as their depth format)
        cv.Format = t->format == DXGI_FORMAT_R32_TYPELESS ? DXGI_FORMAT_D32_FLOAT : t->format == DXGI_FORMAT_R16_TYPELESS ? DXGI_FORMAT_D16_UNORM : t->format == DXGI_FORMAT_R24G8_TYPELESS ? DXGI_FORMAT_D24_UNORM_S8_UINT : t->format;
        cv.DepthStencil = { 1.f, 0 }; pcv = &cv;
    }
    HRESULT hr = g->dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, pcv, IID_PPV_ARGS(&t->own));
    if (FAILED(hr)) { g->log("could not make a texture " + std::to_string(t->width) + "x" + std::to_string(t->height)); return hr; }
    t->gres = t->own.Get(); t->ownStates.assign(t->subresources(), D3D12_RESOURCE_STATE_COMMON);
    if (init) for (UINT s = 0; s < t->subresources(); s++) if (init[s].pSysMem) g->copyIntoTexture(t, s, nullptr, init[s].pSysMem, init[s].SysMemPitch, init[s].SysMemSlicePitch);
    return S_OK;
}
HRESULT Device::CreateTexture2D(const D3D11_TEXTURE2D_DESC* d, const D3D11_SUBRESOURCE_DATA* init, Texture2D** out) {
    LK; if (!d || !out) return E_INVALIDARG; auto* t = new Texture2D; t->kind = Kind::Tex2D;
    t->width = d->Width; t->height = d->Height; t->mips = d->MipLevels; t->arraySize = std::max(1u, d->ArraySize); t->format = d->Format; t->usage = d->Usage; t->bind = d->BindFlags; t->cpu = d->CPUAccessFlags; t->misc = d->MiscFlags;
    HRESULT hr = MakeTexture(t, init); if (FAILED(hr)) { delete t; *out = nullptr; return hr; } *out = t; return S_OK;
}
HRESULT Device::CreateTexture3D(const D3D11_TEXTURE3D_DESC* d, const D3D11_SUBRESOURCE_DATA* init, Texture3D** out) {
    LK; if (!d || !out) return E_INVALIDARG; auto* t = new Texture3D; t->kind = Kind::Tex3D;
    t->width = d->Width; t->height = d->Height; t->depth = d->Depth; t->mips = d->MipLevels; t->arraySize = 1; t->format = d->Format; t->usage = d->Usage; t->bind = d->BindFlags; t->cpu = d->CPUAccessFlags; t->misc = d->MiscFlags;
    HRESULT hr = MakeTexture(t, init); if (FAILED(hr)) { delete t; *out = nullptr; return hr; } *out = t; return S_OK;
}
HRESULT Device::CreateBuffer(const D3D11_BUFFER_DESC* d, const D3D11_SUBRESOURCE_DATA* init, Buffer** out) {
    LK; if (!d || !out || !d->ByteWidth) return E_INVALIDARG; auto* b = new Buffer; b->kind = Kind::Buffer;
    b->byteWidth = d->ByteWidth; b->usage = d->Usage; b->bind = d->BindFlags; b->cpu = d->CPUAccessFlags; b->misc = d->MiscFlags; b->stride = d->StructureByteStride;
    *out = nullptr;
    if (d->Usage == D3D11_USAGE_STAGING) {
        D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_READBACK };
        D3D12_RESOURCE_DESC bd{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, AlignUp(d->ByteWidth, 256), 1, 1, 1, DXGI_FORMAT_UNKNOWN, { 1, 0 }, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
        HRESULT hr = g->dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&b->own)); if (FAILED(hr)) { delete b; return hr; }
        b->gres = b->own.Get(); b->staging = true; b->own->Map(0, nullptr, (void**)&b->rbPtr); *out = b; return S_OK;
    }
    if ((d->BindFlags & D3D11_BIND_CONSTANT_BUFFER) || d->Usage == D3D11_USAGE_DYNAMIC) {   // kept on the CPU, copied up when changed
        b->virt = true; b->shadow.assign(d->ByteWidth, 0); if (init && init->pSysMem) std::memcpy(b->shadow.data(), init->pSysMem, d->ByteWidth);
        *out = b; return S_OK;
    }
    bool tiny = (d->BindFlags & ~(D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER)) == 0 && d->ByteWidth <= PAGE_MAX_ALLOC;
    if (tiny) {   // a stretch of a shared page
        uint64_t n = AlignUp(d->ByteWidth, 256), off = 0; Page* pg = nullptr;
        for (auto& p : g->pages) if (p->take(n, &off)) { pg = p.get(); break; }
        if (!pg) {
            auto np = std::make_unique<Page>(); np->size = PAGE_SIZE;
            D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_DEFAULT };
            D3D12_RESOURCE_DESC bd{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, PAGE_SIZE, 1, 1, 1, DXGI_FORMAT_UNKNOWN, { 1, 0 }, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
            HRESULT hr = g->dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&np->res)); if (FAILED(hr)) { delete b; return hr; }
            np->freeBlocks[0] = PAGE_SIZE; np->take(n, &off); pg = np.get(); g->pages.push_back(std::move(np));
        }
        b->page = pg; b->gres = pg->res.Get(); b->goff = off;
    } else {
        D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_DEFAULT };
        D3D12_RESOURCE_DESC bd{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, AlignUp(d->ByteWidth, 256), 1, 1, 1, DXGI_FORMAT_UNKNOWN, { 1, 0 }, D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                (d->BindFlags & D3D11_BIND_UNORDERED_ACCESS) ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE };
        HRESULT hr = g->dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&b->own)); if (FAILED(hr)) { delete b; return hr; }
        b->gres = b->own.Get(); b->ownStates.assign(1, D3D12_RESOURCE_STATE_COMMON);
    }
    if (init && init->pSysMem) g->copyIntoBuffer(b, 0, init->pSysMem, d->ByteWidth);
    *out = b; return S_OK;
}

// ---- views
static D3D12_SRV_DIMENSION Srv12(D3D11_SRV_DIMENSION d) { return d == D3D11_SRV_DIMENSION_BUFFEREX ? D3D12_SRV_DIMENSION_BUFFER : (D3D12_SRV_DIMENSION)d; }
HRESULT Device::CreateShaderResourceView(Resource* r, const D3D11_SHADER_RESOURCE_VIEW_DESC* d, ShaderResourceView** out) {
    LK; if (!r || !out) return E_INVALIDARG; Resource* R = r->real();
    auto* v = new ShaderResourceView; v->res = r; v->heapType = 0;
    D3D12_SHADER_RESOURCE_VIEW_DESC& o = v->desc; o.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    D3D11_SHADER_RESOURCE_VIEW_DESC dd{};
    if (d) dd = *d;
    else {   // the whole resource, as D3D11 would view it
        dd.Format = R->format;
        if (R->kind == Kind::Buffer) { delete v; return E_INVALIDARG; }
        if (R->kind == Kind::Tex3D) { dd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D; dd.Texture3D.MipLevels = (UINT)-1; }
        else if ((R->misc & D3D11_RESOURCE_MISC_TEXTURECUBE) && R->arraySize == 6) { dd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE; dd.TextureCube.MipLevels = (UINT)-1; }
        else if ((R->misc & D3D11_RESOURCE_MISC_TEXTURECUBE) && R->arraySize > 6) { dd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY; dd.TextureCubeArray.MipLevels = (UINT)-1; dd.TextureCubeArray.NumCubes = R->arraySize / 6; }
        else if (R->arraySize > 1) { dd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY; dd.Texture2DArray.MipLevels = (UINT)-1; dd.Texture2DArray.ArraySize = R->arraySize; }
        else { dd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; dd.Texture2D.MipLevels = (UINT)-1; }
    }
    o.Format = dd.Format == DXGI_FORMAT_UNKNOWN && R->kind != Kind::Buffer ? R->format : dd.Format; o.ViewDimension = Srv12(dd.ViewDimension);
    auto mipsOf = [&](UINT most, UINT n) { return n == (UINT)-1 ? R->mips - most : n; };
    SubRange& sr = v->range;
    switch (dd.ViewDimension) {
    case D3D11_SRV_DIMENSION_BUFFER: case D3D11_SRV_DIMENSION_BUFFEREX: {
        UINT first = dd.ViewDimension == D3D11_SRV_DIMENSION_BUFFER ? dd.Buffer.FirstElement : dd.BufferEx.FirstElement;
        UINT num = dd.ViewDimension == D3D11_SRV_DIMENSION_BUFFER ? dd.Buffer.NumElements : dd.BufferEx.NumElements;
        bool raw = dd.ViewDimension == D3D11_SRV_DIMENSION_BUFFEREX && (dd.BufferEx.Flags & D3D11_BUFFEREX_SRV_FLAG_RAW);
        o.Buffer.FirstElement = first; o.Buffer.NumElements = num; o.Buffer.StructureByteStride = dd.Format == DXGI_FORMAT_UNKNOWN && !raw ? R->stride : 0;
        o.Buffer.Flags = raw ? D3D12_BUFFER_SRV_FLAG_RAW : D3D12_BUFFER_SRV_FLAG_NONE; if (raw) o.Format = DXGI_FORMAT_R32_TYPELESS;
        v->firstEl = first; v->numEl = num;
        if (!R->virt) {   // where it is on its page: counted in elements
            UINT es = o.Buffer.StructureByteStride ? o.Buffer.StructureByteStride : std::max(1u, BitsPerPixel(o.Format) / 8);
            o.Buffer.FirstElement += R->goff / es;
        }
        break; }
    case D3D11_SRV_DIMENSION_TEXTURE1D: o.Texture1D.MostDetailedMip = dd.Texture1D.MostDetailedMip; o.Texture1D.MipLevels = mipsOf(dd.Texture1D.MostDetailedMip, dd.Texture1D.MipLevels); sr = { o.Texture1D.MostDetailedMip, o.Texture1D.MipLevels, 0, 1 }; break;
    case D3D11_SRV_DIMENSION_TEXTURE2D: o.Texture2D.MostDetailedMip = dd.Texture2D.MostDetailedMip; o.Texture2D.MipLevels = mipsOf(dd.Texture2D.MostDetailedMip, dd.Texture2D.MipLevels); sr = { o.Texture2D.MostDetailedMip, o.Texture2D.MipLevels, 0, 1 }; break;
    case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
        o.Texture2DArray.MostDetailedMip = dd.Texture2DArray.MostDetailedMip; o.Texture2DArray.MipLevels = mipsOf(dd.Texture2DArray.MostDetailedMip, dd.Texture2DArray.MipLevels);
        o.Texture2DArray.FirstArraySlice = dd.Texture2DArray.FirstArraySlice; o.Texture2DArray.ArraySize = dd.Texture2DArray.ArraySize;
        sr = { o.Texture2DArray.MostDetailedMip, o.Texture2DArray.MipLevels, o.Texture2DArray.FirstArraySlice, o.Texture2DArray.ArraySize }; break;
    case D3D11_SRV_DIMENSION_TEXTURE3D: o.Texture3D.MostDetailedMip = dd.Texture3D.MostDetailedMip; o.Texture3D.MipLevels = mipsOf(dd.Texture3D.MostDetailedMip, dd.Texture3D.MipLevels); sr = { o.Texture3D.MostDetailedMip, o.Texture3D.MipLevels, 0, 1 }; break;
    case D3D11_SRV_DIMENSION_TEXTURECUBE: o.TextureCube.MostDetailedMip = dd.TextureCube.MostDetailedMip; o.TextureCube.MipLevels = mipsOf(dd.TextureCube.MostDetailedMip, dd.TextureCube.MipLevels); sr = { o.TextureCube.MostDetailedMip, o.TextureCube.MipLevels, 0, 6 }; break;
    case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
        o.TextureCubeArray.MostDetailedMip = dd.TextureCubeArray.MostDetailedMip; o.TextureCubeArray.MipLevels = mipsOf(dd.TextureCubeArray.MostDetailedMip, dd.TextureCubeArray.MipLevels);
        o.TextureCubeArray.First2DArrayFace = dd.TextureCubeArray.First2DArrayFace; o.TextureCubeArray.NumCubes = dd.TextureCubeArray.NumCubes;
        sr = { o.TextureCubeArray.MostDetailedMip, o.TextureCubeArray.MipLevels, o.TextureCubeArray.First2DArrayFace, o.TextureCubeArray.NumCubes * 6 }; break;
    default: delete v; return E_INVALIDARG;
    }
    v->format = o.Format;
    if (!R->virt) { v->cpu = g->cpuSrv.alloc(g->dev.Get()); g->dev->CreateShaderResourceView(R->gres, &o, v->cpu); }
    *out = v; return S_OK;
}
HRESULT Device::CreateRenderTargetView(Resource* r, const D3D11_RENDER_TARGET_VIEW_DESC* d, RenderTargetView** out) {
    LK; if (!r || !out) return E_INVALIDARG; Resource* R = r->real();
    auto* v = new RenderTargetView; v->res = r; v->heapType = 1; D3D12_RENDER_TARGET_VIEW_DESC& o = v->desc;
    D3D11_RENDER_TARGET_VIEW_DESC dd{};
    if (d) dd = *d;
    else { dd.Format = R->format; if (R->kind == Kind::Tex3D) { dd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D; dd.Texture3D.WSize = (UINT)-1; } else if (R->arraySize > 1) { dd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY; dd.Texture2DArray.ArraySize = R->arraySize; } else dd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D; }
    o.Format = dd.Format == DXGI_FORMAT_UNKNOWN ? R->format : dd.Format;
    switch (dd.ViewDimension) {
    case D3D11_RTV_DIMENSION_TEXTURE2D: o.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D; o.Texture2D.MipSlice = dd.Texture2D.MipSlice; v->range = { dd.Texture2D.MipSlice, 1, 0, 1 }; break;
    case D3D11_RTV_DIMENSION_TEXTURE2DARRAY: o.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY; o.Texture2DArray.MipSlice = dd.Texture2DArray.MipSlice; o.Texture2DArray.FirstArraySlice = dd.Texture2DArray.FirstArraySlice; o.Texture2DArray.ArraySize = dd.Texture2DArray.ArraySize;
        v->range = { dd.Texture2DArray.MipSlice, 1, dd.Texture2DArray.FirstArraySlice, dd.Texture2DArray.ArraySize }; break;
    case D3D11_RTV_DIMENSION_TEXTURE3D: o.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D; o.Texture3D.MipSlice = dd.Texture3D.MipSlice; o.Texture3D.FirstWSlice = dd.Texture3D.FirstWSlice; o.Texture3D.WSize = dd.Texture3D.WSize; v->range = { dd.Texture3D.MipSlice, 1, 0, 1 }; break;
    default: delete v; return E_INVALIDARG;
    }
    v->format = o.Format;
    if (r->swapOf) for (auto& bb : g->bufs) { D3D12_CPU_DESCRIPTOR_HANDLE h = g->cpuRtv.alloc(g->dev.Get()); g->dev->CreateRenderTargetView(bb->gres, &o, h); v->perBuffer.push_back(h); }
    else { v->cpu = g->cpuRtv.alloc(g->dev.Get()); g->dev->CreateRenderTargetView(R->gres, &o, v->cpu); }
    *out = v; return S_OK;
}
HRESULT Device::CreateDepthStencilView(Resource* r, const D3D11_DEPTH_STENCIL_VIEW_DESC* d, DepthStencilView** out) {
    LK; if (!r || !out) return E_INVALIDARG; Resource* R = r->real();
    auto* v = new DepthStencilView; v->res = r; v->heapType = 2; D3D12_DEPTH_STENCIL_VIEW_DESC o{};
    D3D11_DEPTH_STENCIL_VIEW_DESC dd{};
    if (d) dd = *d; else { dd.Format = R->format; dd.ViewDimension = R->arraySize > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DARRAY : D3D11_DSV_DIMENSION_TEXTURE2D; dd.Texture2DArray.ArraySize = R->arraySize; }
    o.Format = dd.Format; o.Flags = (D3D12_DSV_FLAGS)dd.Flags; v->readOnly = (dd.Flags & D3D11_DSV_READ_ONLY_DEPTH) != 0;
    if (dd.ViewDimension == D3D11_DSV_DIMENSION_TEXTURE2DARRAY) { o.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY; o.Texture2DArray.MipSlice = dd.Texture2DArray.MipSlice; o.Texture2DArray.FirstArraySlice = dd.Texture2DArray.FirstArraySlice; o.Texture2DArray.ArraySize = dd.Texture2DArray.ArraySize;
        v->range = { dd.Texture2DArray.MipSlice, 1, dd.Texture2DArray.FirstArraySlice, dd.Texture2DArray.ArraySize }; }
    else { o.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; o.Texture2D.MipSlice = dd.Texture2D.MipSlice; v->range = { dd.Texture2D.MipSlice, 1, 0, 1 }; }
    v->format = o.Format; v->cpu = g->cpuDsv.alloc(g->dev.Get()); g->dev->CreateDepthStencilView(R->gres, &o, v->cpu);
    *out = v; return S_OK;
}
HRESULT Device::CreateUnorderedAccessView(Resource* r, const D3D11_UNORDERED_ACCESS_VIEW_DESC* d, UnorderedAccessView** out) {
    LK; if (!r || !out) return E_INVALIDARG; Resource* R = r->real();
    auto* v = new UnorderedAccessView; v->res = r; v->heapType = 0; D3D12_UNORDERED_ACCESS_VIEW_DESC& o = v->desc;
    D3D11_UNORDERED_ACCESS_VIEW_DESC dd{};
    if (d) dd = *d; else { dd.Format = R->format; if (R->kind == Kind::Tex3D) { dd.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D; dd.Texture3D.WSize = (UINT)-1; } else if (R->arraySize > 1) { dd.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY; dd.Texture2DArray.ArraySize = R->arraySize; } else dd.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D; }
    o.Format = dd.Format;
    switch (dd.ViewDimension) {
    case D3D11_UAV_DIMENSION_TEXTURE2D: o.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D; o.Texture2D.MipSlice = dd.Texture2D.MipSlice; v->range = { dd.Texture2D.MipSlice, 1, 0, 1 }; break;
    case D3D11_UAV_DIMENSION_TEXTURE2DARRAY: o.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY; o.Texture2DArray.MipSlice = dd.Texture2DArray.MipSlice; o.Texture2DArray.FirstArraySlice = dd.Texture2DArray.FirstArraySlice; o.Texture2DArray.ArraySize = dd.Texture2DArray.ArraySize;
        v->range = { dd.Texture2DArray.MipSlice, 1, dd.Texture2DArray.FirstArraySlice, dd.Texture2DArray.ArraySize }; break;
    case D3D11_UAV_DIMENSION_TEXTURE3D: o.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D; o.Texture3D.MipSlice = dd.Texture3D.MipSlice; o.Texture3D.FirstWSlice = dd.Texture3D.FirstWSlice; o.Texture3D.WSize = dd.Texture3D.WSize; v->range = { dd.Texture3D.MipSlice, 1, 0, 1 }; break;
    case D3D11_UAV_DIMENSION_BUFFER: o.ViewDimension = D3D12_UAV_DIMENSION_BUFFER; o.Buffer.FirstElement = dd.Buffer.FirstElement; o.Buffer.NumElements = dd.Buffer.NumElements;
        o.Buffer.StructureByteStride = dd.Format == DXGI_FORMAT_UNKNOWN ? R->stride : 0; o.Buffer.Flags = (dd.Buffer.Flags & D3D11_BUFFER_UAV_FLAG_RAW) ? D3D12_BUFFER_UAV_FLAG_RAW : D3D12_BUFFER_UAV_FLAG_NONE; break;
    default: delete v; return E_INVALIDARG;
    }
    v->format = o.Format; v->cpu = g->cpuSrv.alloc(g->dev.Get()); g->dev->CreateUnorderedAccessView(R->gres, nullptr, &o, v->cpu);
    *out = v; return S_OK;
}

// ---- shaders: their bytecode, and what they read (from the shader itself)
static void Reflect(ShaderBase* s) {
    ComPtr<ID3D11ShaderReflection> rf;
    if (FAILED(D3DReflect(s->code.data(), s->code.size(), IID_ID3D11ShaderReflection, (void**)rf.GetAddressOf()))) { s->srvMax = SRV_SLOTS - 1; s->cbMax = CB_SLOTS - 1; s->smpMax = SMP_SLOTS - 1; return; }
    D3D11_SHADER_DESC sd{}; rf->GetDesc(&sd);
    for (UINT i = 0; i < sd.BoundResources; i++) {
        D3D11_SHADER_INPUT_BIND_DESC bd{}; if (FAILED(rf->GetResourceBindingDesc(i, &bd))) continue;
        int last = (int)(bd.BindPoint + std::max(1u, bd.BindCount) - 1);
        switch (bd.Type) {
        case D3D_SIT_CBUFFER: s->cbMax = std::max(s->cbMax, std::min(last, (int)CB_SLOTS - 1)); break;
        case D3D_SIT_SAMPLER: s->smpMax = std::max(s->smpMax, std::min(last, (int)SMP_SLOTS - 1)); break;
        case D3D_SIT_TEXTURE: case D3D_SIT_TBUFFER: case D3D_SIT_STRUCTURED: case D3D_SIT_BYTEADDRESS:
            for (int k = (int)bd.BindPoint; k <= last && k < (int)SRV_SLOTS; k++) { s->srvDim[k] = (uint8_t)(bd.Dimension == D3D_SRV_DIMENSION_BUFFEREX ? 1 : bd.Dimension); s->srvStructured[k] = bd.Type == D3D_SIT_STRUCTURED || bd.Type == D3D_SIT_BYTEADDRESS; }
            s->srvMax = std::max(s->srvMax, std::min(last, (int)SRV_SLOTS - 1)); break;
        case D3D_SIT_UAV_RWTYPED: case D3D_SIT_UAV_RWSTRUCTURED: case D3D_SIT_UAV_RWBYTEADDRESS: case D3D_SIT_UAV_APPEND_STRUCTURED: case D3D_SIT_UAV_CONSUME_STRUCTURED: case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
            for (int k = (int)bd.BindPoint; k <= last && k < (int)UAV_SLOTS; k++) s->uavDim[k] = (uint8_t)bd.Dimension;
            s->uavMax = std::max(s->uavMax, std::min(last, (int)UAV_SLOTS - 1)); break;
        default: break;
        }
    }
}
HRESULT Device::CreateVertexShader(const void* code, SIZE_T n, void*, VertexShader** out) { LK; auto* s = new VertexShader; s->code.assign((const uint8_t*)code, (const uint8_t*)code + n); Reflect(s); *out = s; return S_OK; }
HRESULT Device::CreatePixelShader(const void* code, SIZE_T n, void*, PixelShader** out) { LK; auto* s = new PixelShader; s->code.assign((const uint8_t*)code, (const uint8_t*)code + n); Reflect(s); *out = s; return S_OK; }
HRESULT Device::CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC* e, UINT n, const void*, SIZE_T, InputLayout** out) {
    LK; auto* l = new InputLayout; l->names.reserve(n); l->el.resize(n);
    for (UINT i = 0; i < n; i++) l->names.push_back(e[i].SemanticName ? e[i].SemanticName : "");
    for (UINT i = 0; i < n; i++) l->el[i] = { l->names[i].c_str(), e[i].SemanticIndex, e[i].Format, e[i].InputSlot, e[i].AlignedByteOffset, (D3D12_INPUT_CLASSIFICATION)e[i].InputSlotClass, e[i].InstanceDataStepRate };
    *out = l; return S_OK;
}
HRESULT Device::CreateBlendState(const D3D11_BLEND_DESC* d, BlendState** out) {
    LK; auto* s = new BlendState; s->d.AlphaToCoverageEnable = d->AlphaToCoverageEnable; s->d.IndependentBlendEnable = d->IndependentBlendEnable;
    for (int i = 0; i < 8; i++) { const auto& a = d->RenderTarget[i]; auto& o = s->d.RenderTarget[i];
        o.BlendEnable = a.BlendEnable; o.LogicOpEnable = FALSE; o.SrcBlend = (D3D12_BLEND)a.SrcBlend; o.DestBlend = (D3D12_BLEND)a.DestBlend; o.BlendOp = (D3D12_BLEND_OP)a.BlendOp;
        o.SrcBlendAlpha = (D3D12_BLEND)a.SrcBlendAlpha; o.DestBlendAlpha = (D3D12_BLEND)a.DestBlendAlpha; o.BlendOpAlpha = (D3D12_BLEND_OP)a.BlendOpAlpha; o.LogicOp = D3D12_LOGIC_OP_NOOP; o.RenderTargetWriteMask = a.RenderTargetWriteMask; }
    *out = s; return S_OK;
}
HRESULT Device::CreateRasterizerState(const D3D11_RASTERIZER_DESC* d, RasterizerState** out) {
    LK; auto* s = new RasterizerState; auto& o = s->d;
    o.FillMode = (D3D12_FILL_MODE)d->FillMode; o.CullMode = (D3D12_CULL_MODE)d->CullMode; o.FrontCounterClockwise = d->FrontCounterClockwise; o.DepthBias = d->DepthBias;
    o.DepthBiasClamp = d->DepthBiasClamp; o.SlopeScaledDepthBias = d->SlopeScaledDepthBias; o.DepthClipEnable = d->DepthClipEnable; o.MultisampleEnable = d->MultisampleEnable;
    o.AntialiasedLineEnable = d->AntialiasedLineEnable; o.ForcedSampleCount = 0; o.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF; s->scissor = d->ScissorEnable != FALSE;
    *out = s; return S_OK;
}
HRESULT Device::CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC* d, DepthStencilState** out) {
    LK; auto* s = new DepthStencilState; auto& o = s->d;
    o.DepthEnable = d->DepthEnable; o.DepthWriteMask = (D3D12_DEPTH_WRITE_MASK)d->DepthWriteMask; o.DepthFunc = (D3D12_COMPARISON_FUNC)d->DepthFunc; o.StencilEnable = d->StencilEnable;
    o.StencilReadMask = d->StencilReadMask; o.StencilWriteMask = d->StencilWriteMask;
    auto face = [](const D3D11_DEPTH_STENCILOP_DESC& a) { D3D12_DEPTH_STENCILOP_DESC f{ (D3D12_STENCIL_OP)a.StencilFailOp, (D3D12_STENCIL_OP)a.StencilDepthFailOp, (D3D12_STENCIL_OP)a.StencilPassOp, (D3D12_COMPARISON_FUNC)a.StencilFunc }; return f; };
    o.FrontFace = face(d->FrontFace); o.BackFace = face(d->BackFace);
    *out = s; return S_OK;
}
HRESULT Device::CreateSamplerState(const D3D11_SAMPLER_DESC* d, SamplerState** out) {
    LK; auto* s = new SamplerState; auto& o = s->d;
    o.Filter = (D3D12_FILTER)d->Filter; o.AddressU = (D3D12_TEXTURE_ADDRESS_MODE)d->AddressU; o.AddressV = (D3D12_TEXTURE_ADDRESS_MODE)d->AddressV; o.AddressW = (D3D12_TEXTURE_ADDRESS_MODE)d->AddressW;
    o.MipLODBias = d->MipLODBias; o.MaxAnisotropy = d->MaxAnisotropy; o.ComparisonFunc = (D3D12_COMPARISON_FUNC)d->ComparisonFunc; for (int i = 0; i < 4; i++) o.BorderColor[i] = d->BorderColor[i];
    o.MinLOD = d->MinLOD; o.MaxLOD = d->MaxLOD;
    s->cpu = g->cpuSmp.alloc(g->dev.Get()); g->dev->CreateSampler(&o, s->cpu); *out = s; return S_OK;
}
HRESULT Device::CreateComputeShader(const void* code, SIZE_T n, void*, ComputeShader** out) { LK; auto* s = new ComputeShader; s->code.assign((const uint8_t*)code, (const uint8_t*)code + n); Reflect(s); *out = s; return S_OK; }
bool Device::inlineRaytracing() { return g->rtInline; }
HRESULT Device::CreateQuery(const D3D11_QUERY_DESC* d, Query** out) {
    LK; auto* q = new Query; q->type = d->Query;
    if (q->type == D3D11_QUERY_TIMESTAMP) { if (g->qfree.empty()) { delete q; *out = nullptr; return E_OUTOFMEMORY; } q->slot = g->qfree.back(); g->qfree.pop_back(); }
    *out = q; return S_OK;
}

// ---------------------------------------------------------------- the context: binding
DeviceContext::Saved* DeviceContext::SaveState() { LK; return new Saved(g->b); }
void DeviceContext::RestoreState(Saved* s) { LK; if (!s) return; g->b = *s; delete s; g->dirty = DT_ALL; }
ID3D12GraphicsCommandList* DeviceContext::d3d() { return g->list.Get(); }
ID3D12GraphicsCommandList4* DeviceContext::d3d4() { return g->list4.Get(); }
void DeviceContext::Need(Resource* r, D3D12_RESOURCE_STATES s) { LK; g->needAll(r, s); }
void DeviceContext::FlushBarriers() { LK; g->flushBarriers(); }
void DeviceContext::Invalidate() { LK; g->dirty = DT_ALL; g->curPso = nullptr; ID3D12DescriptorHeap* hs[2] = { g->gpuHeap.Get(), g->smpHeap.Get() }; g->list->SetDescriptorHeaps(2, hs); g->list->SetGraphicsRootSignature(g->root.Get()); }

static bool BoundForOutput(const View* v) {   // (D3D11: a resource being drawn into can't be read at the same time)
    for (UINT i = 0; i < g->b.nrt; i++) if (g->b.rtv[i] && g->b.rtv[i]->overlaps(*v)) return true;
    if (g->b.dsv && !g->b.dsv->readOnly && g->b.dsv->overlaps(*v)) return true;
    for (auto& u : g->b.uav) if (u && u->overlaps(*v)) return true;
    return false;
}
static void UnbindReads(const View* out) {   // (D3D11: drawing into it takes it away from where it was being read)
    for (UINT i = 0; i < SRV_SLOTS; i++) {
        if (g->b.vs.srv[i] && g->b.vs.srv[i]->overlaps(*out)) { g->b.vs.srv[i].Reset(); g->dirty |= DT_VS_SRV; }
        if (g->b.ps.srv[i] && g->b.ps.srv[i]->overlaps(*out)) { g->b.ps.srv[i].Reset(); g->dirty |= DT_PS_SRV; }
    }
}
void DeviceContext::VSSetShader(VertexShader* s, void* const*, UINT) { LK; if (g->b.vs.sh.Get() != s) { g->b.vs.sh = s; g->dirty |= DT_PSO | DT_VS_SRV | DT_VS_CB | DT_VS_SMP; } }
void DeviceContext::PSSetShader(PixelShader* s, void* const*, UINT) { LK; if (g->b.ps.sh.Get() != s) { g->b.ps.sh = s; g->dirty |= DT_PSO | DT_PS_SRV | DT_PS_CB | DT_PS_SMP | DT_UAV; } }
static void SetSrv(StageB& st, uint32_t bit, UINT start, UINT n, ShaderResourceView* const* v) {
    for (UINT i = 0; i < n && start + i < SRV_SLOTS; i++) {
        ShaderResourceView* x = v ? v[i] : nullptr; if (x && BoundForOutput(x)) x = nullptr;
        if (st.srv[start + i].Get() != x) { st.srv[start + i] = x; g->dirty |= bit; }
    }
}
void DeviceContext::VSSetShaderResources(UINT start, UINT n, ShaderResourceView* const* v) { LK; SetSrv(g->b.vs, DT_VS_SRV, start, n, v); }
void DeviceContext::PSSetShaderResources(UINT start, UINT n, ShaderResourceView* const* v) { LK; SetSrv(g->b.ps, DT_PS_SRV, start, n, v); }
void DeviceContext::VSSetConstantBuffers(UINT start, UINT n, Buffer* const* b) { LK; for (UINT i = 0; i < n && start + i < CB_SLOTS; i++) { Buffer* x = b ? b[i] : nullptr; if (g->b.vs.cb[start + i].Get() != x) { g->b.vs.cb[start + i] = x; g->dirty |= DT_VS_CB; } } }
void DeviceContext::PSSetConstantBuffers(UINT start, UINT n, Buffer* const* b) { LK; for (UINT i = 0; i < n && start + i < CB_SLOTS; i++) { Buffer* x = b ? b[i] : nullptr; if (g->b.ps.cb[start + i].Get() != x) { g->b.ps.cb[start + i] = x; g->dirty |= DT_PS_CB; } } }
void DeviceContext::VSSetSamplers(UINT start, UINT n, SamplerState* const* s) { LK; for (UINT i = 0; i < n && start + i < SMP_SLOTS; i++) { SamplerState* x = s ? s[i] : nullptr; if (g->b.vs.smp[start + i].Get() != x) { g->b.vs.smp[start + i] = x; g->dirty |= DT_VS_SMP; } } }
void DeviceContext::PSSetSamplers(UINT start, UINT n, SamplerState* const* s) { LK; for (UINT i = 0; i < n && start + i < SMP_SLOTS; i++) { SamplerState* x = s ? s[i] : nullptr; if (g->b.ps.smp[start + i].Get() != x) { g->b.ps.smp[start + i] = x; g->dirty |= DT_PS_SMP; } } }
void DeviceContext::IASetInputLayout(InputLayout* l) { LK; if (g->b.il.Get() != l) { g->b.il = l; g->dirty |= DT_PSO; } }
void DeviceContext::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY t) { LK; if (g->b.topo != t) { g->b.topo = t; g->dirty |= DT_TOPO | DT_PSO; } }
void DeviceContext::IASetVertexBuffers(UINT start, UINT n, Buffer* const* b, const UINT* strides, const UINT* offsets) {
    LK; for (UINT i = 0; i < n && start + i < VB_SLOTS; i++) { g->b.vb[start + i] = b ? b[i] : nullptr; g->b.vbStride[start + i] = strides ? strides[i] : 0; g->b.vbOff[start + i] = offsets ? offsets[i] : 0; }
    g->dirty |= DT_VB;
}
void DeviceContext::IASetIndexBuffer(Buffer* b, DXGI_FORMAT f, UINT offset) { LK; g->b.ib = b; g->b.ibFmt = f; g->b.ibOff = offset; g->dirty |= DT_IB; }
void DeviceContext::OMSetRenderTargets(UINT n, RenderTargetView* const* rt, DepthStencilView* ds) {
    LK; n = std::min(n, RT_SLOTS);
    for (UINT i = 0; i < RT_SLOTS; i++) g->b.rtv[i] = i < n && rt ? rt[i] : nullptr;
    g->b.nrt = n; g->b.dsv = ds; for (auto& u : g->b.uav) u.Reset();   // (D3D11: render targets set again: the pixel shader's UAVs go)
    for (UINT i = 0; i < n; i++) if (g->b.rtv[i]) UnbindReads(g->b.rtv[i].Get());
    if (ds && !ds->readOnly) UnbindReads(ds);
    g->dirty |= DT_OM | DT_PSO | DT_UAV;
}
void DeviceContext::OMSetRenderTargetsAndUnorderedAccessViews(UINT n, RenderTargetView* const* rt, DepthStencilView* ds, UINT uavStart, UINT nu, UnorderedAccessView* const* u, const UINT*) {
    LK; OMSetRenderTargets(n, rt, ds);
    for (UINT i = 0; i < nu && uavStart + i < UAV_SLOTS; i++) { g->b.uav[uavStart + i] = u ? u[i] : nullptr; if (u && u[i]) UnbindReads(u[i]); }
    g->dirty |= DT_UAV;
}
void DeviceContext::OMGetRenderTargets(UINT n, RenderTargetView** rt, DepthStencilView** ds) {
    LK; for (UINT i = 0; i < n && rt; i++) { rt[i] = i < RT_SLOTS ? g->b.rtv[i].Get() : nullptr; if (rt[i]) rt[i]->AddRef(); }   // (counted, as D3D11 did: the caller lets them go)
    if (ds) { *ds = g->b.dsv.Get(); if (*ds) (*ds)->AddRef(); }
}
void DeviceContext::OMSetBlendState(BlendState* b, const FLOAT f[4], UINT mask) {
    LK; if (g->b.blend.Get() != b || g->b.mask != mask) { g->b.blend = b; g->b.mask = mask; g->dirty |= DT_PSO; }
    float nf[4] = { 1, 1, 1, 1 }; if (f) std::memcpy(nf, f, sizeof nf);
    if (std::memcmp(nf, g->b.bf, sizeof nf)) { std::memcpy(g->b.bf, nf, sizeof nf); g->dirty |= DT_BF; }
}
void DeviceContext::OMSetDepthStencilState(DepthStencilState* d, UINT ref) { LK; if (g->b.ds.Get() != d) { g->b.ds = d; g->dirty |= DT_PSO; } if (g->b.sref != ref) { g->b.sref = ref; g->dirty |= DT_SREF; } }
void DeviceContext::RSSetState(RasterizerState* r) { LK; if (g->b.rs.Get() != r) { g->b.rs = r; g->dirty |= DT_PSO | DT_VP; } }
void DeviceContext::RSGetState(RasterizerState** r) { LK; *r = g->b.rs.Get(); if (*r) (*r)->AddRef(); }
void DeviceContext::RSSetViewports(UINT n, const D3D11_VIEWPORT* v) { LK; g->b.nvp = std::min(n, 8u); for (UINT i = 0; i < g->b.nvp; i++) g->b.vp[i] = v[i]; g->dirty |= DT_VP; }
void DeviceContext::RSGetViewports(UINT* n, D3D11_VIEWPORT* v) { LK; UINT k = std::min(*n, g->b.nvp); if (v) for (UINT i = 0; i < k; i++) v[i] = g->b.vp[i]; *n = v ? k : g->b.nvp; }
void DeviceContext::RSSetScissorRects(UINT n, const D3D11_RECT* r) { LK; g->b.nsc = std::min(n, 8u); for (UINT i = 0; i < g->b.nsc; i++) g->b.sc[i] = r[i]; g->dirty |= DT_VP; }

// ---------------------------------------------------------------- the context: drawing
static D3D12_PRIMITIVE_TOPOLOGY_TYPE TopoType(D3D11_PRIMITIVE_TOPOLOGY t) {
    switch (t) {
    case D3D11_PRIMITIVE_TOPOLOGY_POINTLIST: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    case D3D11_PRIMITIVE_TOPOLOGY_LINELIST: case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP: case D3D11_PRIMITIVE_TOPOLOGY_LINELIST_ADJ: case D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    default: return t >= D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }
}
static ID3D12PipelineState* GetPso() {
    DeviceContext::Saved& b = g->b;
    PsoKey k; std::memset(&k, 0, sizeof k);
    k.vs = b.vs.sh ? b.vs.sh->uid : 0; k.ps = b.ps.sh ? b.ps.sh->uid : 0; k.il = b.il ? b.il->uid : 0; k.bl = b.blend ? b.blend->uid : 0; k.rs = b.rs ? b.rs->uid : 0; k.ds = b.ds ? b.ds->uid : 0;
    k.mask = b.mask; k.topo = (uint32_t)TopoType(b.topo); k.nrt = b.nrt;
    for (UINT i = 0; i < b.nrt; i++) k.rt[i] = b.rtv[i] ? b.rtv[i]->format : DXGI_FORMAT_R8G8B8A8_UNORM;   // (an empty slot: the empty view's format)
    k.dsv = b.dsv ? b.dsv->format : DXGI_FORMAT_UNKNOWN;
    auto it = g->psos.find(k); if (it != g->psos.end()) return it->second.Get();
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{}; d.pRootSignature = g->root.Get();
    if (b.vs.sh) d.VS = { b.vs.sh->code.data(), b.vs.sh->code.size() };
    if (b.ps.sh) d.PS = { b.ps.sh->code.data(), b.ps.sh->code.size() };
    d.BlendState = b.blend ? b.blend->d : g->defBlend; d.SampleMask = b.mask;
    d.RasterizerState = b.rs ? b.rs->d : g->defRaster;
    d.DepthStencilState = b.ds ? b.ds->d : g->defDepth;
    if (!b.dsv) { d.DepthStencilState.DepthEnable = FALSE; d.DepthStencilState.StencilEnable = FALSE; }
    if (b.il) d.InputLayout = { b.il->el.data(), (UINT)b.il->el.size() };
    d.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED; d.PrimitiveTopologyType = (D3D12_PRIMITIVE_TOPOLOGY_TYPE)k.topo;
    d.NumRenderTargets = b.nrt; for (UINT i = 0; i < b.nrt; i++) d.RTVFormats[i] = k.rt[i]; d.DSVFormat = k.dsv; d.SampleDesc = { 1, 0 };
    if (d.BlendState.IndependentBlendEnable) for (UINT i = 0; i < 8; i++) if (i >= b.nrt || d.RTVFormats[i] == DXGI_FORMAT_UNKNOWN) d.BlendState.RenderTarget[i].BlendEnable = FALSE;
    ComPtr<ID3D12PipelineState> p; HRESULT hr = g->dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&p));
    if (FAILED(hr)) { char m[160]; snprintf(m, sizeof m, "a pipeline state could not be made (0x%08X): vs %u ps %u, %u targets", (unsigned)hr, k.vs, k.ps, k.nrt); g->log(m); g->pollMessages(); }
    g->psos[k] = p; return p.Get();
}
static D3D12_CPU_DESCRIPTOR_HANDLE NullSrvFor(const ShaderBase* s, UINT i) {
    if (s->srvStructured[i]) return g->nullStruct;
    UINT d = s->srvDim[i]; return d >= 1 && d <= 10 ? g->nullSrv[d] : g->nullSrv[D3D12_SRV_DIMENSION_TEXTURE2D];
}
static bool SrvTable(StageB& st, UINT param) {
    int n = st.sh ? st.sh->srvMax + 1 : 0; if (n <= 0) return true;
    UINT base = g->take((UINT)n);
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> src; std::vector<UINT> ones;
    auto flushRun = [&](UINT at) { if (src.empty()) return; D3D12_CPU_DESCRIPTOR_HANDLE dst = g->gcpu(at); UINT cnt = (UINT)src.size();
        g->dev->CopyDescriptors(1, &dst, &cnt, cnt, src.data(), ones.data(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); src.clear(); ones.clear(); };
    UINT runAt = base;
    for (int i = 0; i < n; i++) {
        ShaderResourceView* v = st.srv[i].Get(); Resource* r = v ? v->res->real() : nullptr;
        if (r && r->virt) {   // a dynamic buffer: its view made where it is now
            flushRun(runAt); g->refresh(r);
            D3D12_SHADER_RESOURCE_VIEW_DESC o = v->desc; UINT es = o.Buffer.StructureByteStride ? o.Buffer.StructureByteStride : std::max(1u, BitsPerPixel(o.Format) / 8);
            o.Buffer.FirstElement = r->voff / es + v->firstEl; g->dev->CreateShaderResourceView(r->vres, &o, g->gcpu(base + i)); runAt = base + i + 1; continue;
        }
        src.push_back(v && v->cpu.ptr ? v->cpu : NullSrvFor(st.sh.Get(), (UINT)i)); ones.push_back(1);
    }
    flushRun(runAt);
    g->list->SetGraphicsRootDescriptorTable(param, g->ggpu(base)); return true;
}
static void CbTable(StageB& st, UINT param) {
    int n = st.sh ? st.sh->cbMax + 1 : 0; if (n <= 0) return;
    UINT base = g->take((UINT)n);
    for (int i = 0; i < n; i++) {
        Buffer* bf = st.cb[i].Get(); D3D12_CONSTANT_BUFFER_VIEW_DESC cv{};
        if (bf && bf->virt) { g->refresh(bf); cv.BufferLocation = bf->gpuVA(); cv.SizeInBytes = (UINT)AlignUp(bf->byteWidth, 256); }
        else if (bf && bf->gres) { cv.BufferLocation = bf->gpuVA(); cv.SizeInBytes = (UINT)AlignUp(bf->byteWidth, 256); }
        if (!cv.BufferLocation) { cv.BufferLocation = g->zeroCb->GetGPUVirtualAddress(); cv.SizeInBytes = 65536; }
        g->dev->CreateConstantBufferView(&cv, g->gcpu(base + i));
    }
    g->list->SetGraphicsRootDescriptorTable(param, g->ggpu(base));
}
static void SmpTable(StageB& st, UINT param) {
    int n = st.sh ? st.sh->smpMax + 1 : 0; if (n <= 0) return;
    std::vector<uint32_t> key(n); for (int i = 0; i < n; i++) key[i] = st.smp[i] ? st.smp[i]->uid : 0;
    auto it = g->smpTables.find(key); UINT at;
    if (it != g->smpTables.end()) at = it->second;
    else {
        if (g->smpPos + SMP_SLOTS > SMP_HEAP) { g->flush(true); g->smpTables.clear(); g->smpPos = SMP_SLOTS; }   // full: the GPU is done with all of them, start over (the first 16: the layer's own)
        at = g->smpPos; g->smpPos += (UINT)n;
        for (int i = 0; i < n; i++) { D3D12_CPU_DESCRIPTOR_HANDLE dst = g->smpHeap->GetCPUDescriptorHandleForHeapStart(); dst.ptr += (SIZE_T)(at + i) * g->smpInc;
            g->dev->CopyDescriptorsSimple(1, dst, st.smp[i] ? st.smp[i]->cpu : g->defSampler->cpu, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER); }
        g->smpTables[key] = at;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE h = g->smpHeap->GetGPUDescriptorHandleForHeapStart(); h.ptr += (UINT64)at * g->smpInc;
    g->list->SetGraphicsRootDescriptorTable(param, h);
}
static void UavTable() {
    int n = g->b.ps.sh ? g->b.ps.sh->uavMax + 1 : 0; if (n <= 0) return;
    UINT base = g->take((UINT)n);
    for (int i = 0; i < n; i++) {
        UnorderedAccessView* u = g->b.uav[i].Get(); UINT d = g->b.ps.sh->uavDim[i];
        g->dev->CopyDescriptorsSimple(1, g->gcpu(base + i), u ? u->cpu : g->nullUav[d <= 8 ? d : 4], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    g->list->SetGraphicsRootDescriptorTable(RP_PS_UAV, g->ggpu(base));
}
static bool Prepare() {
    DeviceContext::Saved& b = g->b;
    // constant and dynamic buffers from an earlier frame: into this one's memory
    // (only when something bound changed, or the frame did - a run of chunk draws only swaps vertex buffers)
    const uint32_t BINDS = DT_VS_SRV | DT_PS_SRV | DT_VS_CB | DT_PS_CB | DT_OM | DT_UAV | DT_PSO;
    bool full = (g->dirty & BINDS) || g->stateSerial != g->checkedSerial;
    if (full) for (StageB* st : { &b.vs, &b.ps }) {
        for (auto& c : st->cb) if (c && g->refresh(c.Get())) g->dirty |= st == &b.vs ? DT_VS_CB : DT_PS_CB;
        int n = st->sh ? st->sh->srvMax + 1 : 0;
        for (int i = 0; i < n; i++) if (st->srv[i] && st->srv[i]->res->virt && g->refresh(st->srv[i]->res.Get())) g->dirty |= st == &b.vs ? DT_VS_SRV : DT_PS_SRV;
    }
    for (auto& v : b.vb) if (v && g->refresh(v.Get())) g->dirty |= DT_VB;
    if (b.ib && g->refresh(b.ib.Get())) g->dirty |= DT_IB;
    // states
    int nu = b.ps.sh ? b.ps.sh->uavMax + 1 : 0;
    if (full) {
        for (UINT i = 0; i < b.nrt; i++) if (b.rtv[i]) g->needView(b.rtv[i].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        if (b.dsv) g->needView(b.dsv.Get(), b.dsv->readOnly ? D3D12_RESOURCE_STATE_DEPTH_READ : D3D12_RESOURCE_STATE_DEPTH_WRITE);
        for (StageB* st : { &b.vs, &b.ps }) { int n = st->sh ? st->sh->srvMax + 1 : 0; for (int i = 0; i < n; i++) if (st->srv[i]) g->needView(st->srv[i].Get(), ST_SR); }
        for (int i = 0; i < nu; i++) if (b.uav[i]) g->needView(b.uav[i].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    bool anyUav = false; for (int i = 0; i < nu; i++) anyUav |= b.uav[i] != nullptr;
    if (anyUav && g->uavDrawn) { D3D12_RESOURCE_BARRIER ub{}; ub.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; g->barriers.push_back(ub); }   // (D3D11 kept draws writing the same UAV in order)
    g->uavDrawn = anyUav;
    for (auto& v : b.vb) if (v && !v->virt) g->needSub(v.Get(), 0, ST_BUFREAD);
    if (b.ib && !b.ib->virt) g->needSub(b.ib.Get(), 0, ST_BUFREAD);
    g->flushBarriers(); g->checkedSerial = g->stateSerial;
    // descriptors: room for every table this draw sets (if the frame's share is used up it starts over, all set again)
    UINT need = 0;
    if (g->dirty & DT_VS_SRV) need += (UINT)std::max(0, b.vs.sh ? b.vs.sh->srvMax + 1 : 0);
    if (g->dirty & DT_PS_SRV) need += (UINT)std::max(0, b.ps.sh ? b.ps.sh->srvMax + 1 : 0);
    if (g->dirty & DT_VS_CB) need += (UINT)std::max(0, b.vs.sh ? b.vs.sh->cbMax + 1 : 0);
    if (g->dirty & DT_PS_CB) need += (UINT)std::max(0, b.ps.sh ? b.ps.sh->cbMax + 1 : 0);
    if (g->dirty & DT_UAV) need += (UINT)std::max(0, nu);
    if (!g->room(need)) return Prepare();
    if (g->dirty & DT_PSO) { ID3D12PipelineState* p = GetPso(); if (!p) return false; if (p != g->curPso) { g->list->SetPipelineState(p); g->curPso = p; } }
    if (g->dirty & DT_VS_SRV) SrvTable(b.vs, RP_VS_SRV);
    if (g->dirty & DT_PS_SRV) SrvTable(b.ps, RP_PS_SRV);
    if (g->dirty & DT_VS_CB) CbTable(b.vs, RP_VS_CB);
    if (g->dirty & DT_PS_CB) CbTable(b.ps, RP_PS_CB);
    if (g->dirty & DT_VS_SMP) SmpTable(b.vs, RP_VS_SMP);
    if (g->dirty & DT_PS_SMP) SmpTable(b.ps, RP_PS_SMP);
    if (g->dirty & DT_UAV) UavTable();
    if (g->dirty & DT_OM) {
        D3D12_CPU_DESCRIPTOR_HANDLE h[RT_SLOTS]; for (UINT i = 0; i < b.nrt; i++) h[i] = b.rtv[i] ? b.rtv[i]->handle() : g->nullRtv;
        D3D12_CPU_DESCRIPTOR_HANDLE dh = b.dsv ? b.dsv->handle() : D3D12_CPU_DESCRIPTOR_HANDLE{};
        g->list->OMSetRenderTargets(b.nrt, b.nrt ? h : nullptr, FALSE, b.dsv ? &dh : nullptr);
    }
    if (g->dirty & DT_VP) {
        g->list->RSSetViewports(b.nvp, (const D3D12_VIEWPORT*)b.vp);
        if (b.rs && b.rs->scissor && b.nsc) g->list->RSSetScissorRects(b.nsc, (const D3D12_RECT*)b.sc);
        else { D3D12_RECT r[8]; UINT n = std::max(1u, b.nvp); for (UINT i = 0; i < n; i++) r[i] = { 0, 0, 16384, 16384 }; g->list->RSSetScissorRects(n, r); }
    }
    if (g->dirty & DT_VB) {
        D3D12_VERTEX_BUFFER_VIEW vv[VB_SLOTS] = {}; UINT n = 0;
        for (UINT i = 0; i < VB_SLOTS; i++) if (b.vb[i]) { Buffer* x = b.vb[i].Get(); vv[i] = { x->gpuVA() + b.vbOff[i], x->byteWidth > b.vbOff[i] ? x->byteWidth - b.vbOff[i] : 0, b.vbStride[i] }; n = i + 1; }
        UINT set = std::max(n, g->nvbSet); if (set) g->list->IASetVertexBuffers(0, set, vv); g->nvbSet = n;
    }
    if ((g->dirty & DT_IB) && b.ib) { D3D12_INDEX_BUFFER_VIEW iv{ b.ib->gpuVA() + b.ibOff, b.ib->byteWidth - std::min(b.ibOff, b.ib->byteWidth), b.ibFmt }; g->list->IASetIndexBuffer(&iv); }
    if (g->dirty & DT_TOPO) g->list->IASetPrimitiveTopology((D3D12_PRIMITIVE_TOPOLOGY)b.topo);
    if (g->dirty & DT_BF) g->list->OMSetBlendFactor(b.bf);
    if (g->dirty & DT_SREF) g->list->OMSetStencilRef(b.sref);
    g->dirty = 0; return true;
}
void DeviceContext::Draw(UINT count, UINT start) { LK; if (!count || !Prepare()) return; g->list->DrawInstanced(count, 1, start, 0); }
void DeviceContext::DrawInstanced(UINT count, UINT inst, UINT start, UINT si) { LK; if (!count || !inst || !Prepare()) return; g->list->DrawInstanced(count, inst, start, si); }
void DeviceContext::DrawIndexed(UINT count, UINT start, INT base) { LK; if (!count || !g->b.ib || !Prepare()) return; g->list->DrawIndexedInstanced(count, 1, start, base, 0); }
void DeviceContext::DrawIndexedInstanced(UINT count, UINT inst, UINT start, INT base, UINT si) { LK; if (!count || !inst || !g->b.ib || !Prepare()) return; g->list->DrawIndexedInstanced(count, inst, start, base, si); }

void DeviceContext::ClearRenderTargetView(RenderTargetView* v, const FLOAT c[4]) { LK; if (!v) return; g->needView(v, D3D12_RESOURCE_STATE_RENDER_TARGET); g->flushBarriers(); g->list->ClearRenderTargetView(v->handle(), c, 0, nullptr); }
void DeviceContext::ClearDepthStencilView(DepthStencilView* v, UINT flags, FLOAT depth, UINT8 stencil) {
    LK; if (!v) return; g->needView(v, D3D12_RESOURCE_STATE_DEPTH_WRITE); g->flushBarriers();
    g->list->ClearDepthStencilView(v->handle(), (D3D12_CLEAR_FLAGS)flags, depth, stencil, 0, nullptr);
    if (g->b.dsv.Get() == v && v->readOnly) {}   // (a read-only view of it goes back to read at the next draw)
}
void DeviceContext::ClearUnorderedAccessViewFloat(UnorderedAccessView* v, const FLOAT c[4]) {
    LK; if (!v) return; g->room(1); g->needView(v, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); g->flushBarriers();
    UINT i = g->take(1); g->dev->CopyDescriptorsSimple(1, g->gcpu(i), v->cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    g->list->ClearUnorderedAccessViewFloat(g->ggpu(i), v->cpu, v->res->real()->gres, c, 0, nullptr);
}

// ---------------------------------------------------------------- the context: data
void DeviceContext::UpdateSubresource(Resource* r, UINT sub, const D3D11_BOX* box, const void* data, UINT rowPitch, UINT depthPitch) {
    LK; if (!r || !data) return; r = r->real();
    if (r->kind == Kind::Buffer) {
        UINT a = box ? box->left : 0, e = box ? std::min(box->right, r->byteWidth) : r->byteWidth; if (e <= a) return;
        if (r->virt) {   // into its CPU copy, then the whole thing to a new place
            std::memcpy(r->shadow.data() + a, data, e - a); r->vepoch = 0; g->refresh(r); g->renamed(r); return;
        }
        if (r->staging) return;
        g->copyIntoBuffer(r, a, data, e - a); return;
    }
    if (r->staging) return;
    g->copyIntoTexture(r, sub, box, data, rowPitch, depthPitch);
}
HRESULT DeviceContext::Map(Resource* r, UINT sub, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE* m) {
    LK; if (!r || !m) return E_INVALIDARG; r = r->real(); *m = {};
    if (r->staging) {   // read back: when the GPU has copied it
        if (g->fence->GetCompletedValue() < r->fenceAt) {
            if (flags & D3D11_MAP_FLAG_DO_NOT_WAIT) return DXGI_ERROR_WAS_STILL_DRAWING;
            if (r->fenceAt > g->signaled) g->flush(true); else g->waitFor(r->fenceAt);
        }
        if (r->kind == Kind::Buffer) { m->pData = r->rbPtr; m->RowPitch = r->byteWidth; m->DepthPitch = r->byteWidth; return S_OK; }
        if (sub >= r->fp.size()) return E_INVALIDARG;
        m->pData = r->rbPtr + r->fp[sub].Offset; m->RowPitch = r->fp[sub].Footprint.RowPitch; m->DepthPitch = m->RowPitch * r->fpRows[sub]; return S_OK;
    }
    if (r->kind == Kind::Buffer && r->virt) {   // write: a new place in this frame's memory (what was there before stays for the GPU)
        uint64_t n = r->bind & D3D11_BIND_CONSTANT_BUFFER ? AlignUp(r->byteWidth, 256) : r->byteWidth;
        uint8_t* p = g->upload(std::max<uint64_t>(n, 16), g->virtAlign(r), &r->mapRes, &r->mapOff); if (!p) return E_OUTOFMEMORY;
        if (type != D3D11_MAP_WRITE_DISCARD) std::memcpy(p, r->shadow.data(), r->shadow.size());   // (no-overwrite / write: what was in it stays)
        r->mapPtr = p; m->pData = p; m->RowPitch = r->byteWidth; m->DepthPitch = r->byteWidth; return S_OK;
    }
    if (r->kind != Kind::Buffer && (r->usage == D3D11_USAGE_DYNAMIC || type == D3D11_MAP_WRITE_DISCARD)) {   // a dynamic texture: written in upload memory, copied over at Unmap
        D3D12_RESOURCE_DESC rd = r->gres->GetDesc(); D3D12_PLACED_SUBRESOURCE_FOOTPRINT f; UINT rows; UINT64 rb, total;
        g->dev->GetCopyableFootprints(&rd, sub, 1, 0, &f, &rows, &rb, &total);
        uint8_t* p = g->upload(total, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, &r->mapRes, &r->mapOff); if (!p) return E_OUTOFMEMORY;
        r->mapPtr = p; r->mapSub = sub; r->mapPitch = f.Footprint.RowPitch; m->pData = p; m->RowPitch = f.Footprint.RowPitch; m->DepthPitch = f.Footprint.RowPitch * rows; return S_OK;
    }
    return E_INVALIDARG;
}
void DeviceContext::Unmap(Resource* r, UINT sub) {
    LK; if (!r) return; r = r->real(); if (r->staging || !r->mapPtr) return;
    if (r->kind == Kind::Buffer && r->virt) {
        std::memcpy(r->shadow.data(), r->mapPtr, r->shadow.size()); r->vres = r->mapRes; r->voff = r->mapOff; r->vepoch = g->epoch; r->mapPtr = nullptr; g->renamed(r); return;
    }
    D3D12_RESOURCE_DESC rd = r->gres->GetDesc(); D3D12_PLACED_SUBRESOURCE_FOOTPRINT f; UINT rows; UINT64 rb, total;
    g->dev->GetCopyableFootprints(&rd, sub, 1, r->mapOff, &f, &rows, &rb, &total);
    g->needSub(r, sub, D3D12_RESOURCE_STATE_COPY_DEST); g->flushBarriers();
    D3D12_TEXTURE_COPY_LOCATION dst{ r->gres, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX }; dst.SubresourceIndex = sub;
    D3D12_TEXTURE_COPY_LOCATION src{ r->mapRes, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT }; src.PlacedFootprint = f;
    g->list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr); r->mapPtr = nullptr;
}
static void CopyToStaging(Resource* dst, UINT dstSub, UINT x, UINT y, UINT z, Resource* src, UINT srcSub, const D3D11_BOX* box) {
    g->needSub(src, srcSub, D3D12_RESOURCE_STATE_COPY_SOURCE); g->flushBarriers();
    if (dstSub >= dst->fp.size()) return;
    D3D12_TEXTURE_COPY_LOCATION d{ dst->gres, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT }; d.PlacedFootprint = dst->fp[dstSub];
    D3D12_TEXTURE_COPY_LOCATION s{ src->gres, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX }; s.SubresourceIndex = srcSub;
    g->list->CopyTextureRegion(&d, x, y, z, &s, (const D3D12_BOX*)box); dst->fenceAt = g->signaled + 1;
}
void DeviceContext::CopyResource(Resource* dst, Resource* src) {
    LK; if (!dst || !src) return; dst = dst->real(); src = src->real();
    if (dst->kind == Kind::Buffer) {
        UINT n = std::min(dst->byteWidth, src->byteWidth);
        ID3D12Resource* sres = src->gres; uint64_t soff = src->goff;
        if (src->virt) { g->refresh(src); sres = src->vres; soff = src->voff; } else g->needSub(src, 0, D3D12_RESOURCE_STATE_COPY_SOURCE);
        if (!dst->staging) g->needSub(dst, 0, D3D12_RESOURCE_STATE_COPY_DEST);
        g->flushBarriers(); g->list->CopyBufferRegion(dst->gres, dst->goff, sres, soff, n); if (dst->staging) dst->fenceAt = g->signaled + 1; return;
    }
    if (dst->staging) { for (UINT s = 0; s < std::min(dst->subresources(), src->subresources()); s++) CopyToStaging(dst, s, 0, 0, 0, src, s, nullptr); return; }
    g->needAll(src, D3D12_RESOURCE_STATE_COPY_SOURCE); g->needAll(dst, D3D12_RESOURCE_STATE_COPY_DEST); g->flushBarriers();
    g->list->CopyResource(dst->gres, src->gres);
}
void DeviceContext::CopySubresourceRegion(Resource* dst, UINT dstSub, UINT x, UINT y, UINT z, Resource* src, UINT srcSub, const D3D11_BOX* box) {
    LK; if (!dst || !src) return; dst = dst->real(); src = src->real();
    if (dst->kind == Kind::Buffer) {
        UINT a = box ? box->left : 0, e = box ? box->right : src->byteWidth; if (e <= a) return;
        g->needSub(src, 0, D3D12_RESOURCE_STATE_COPY_SOURCE); if (!dst->staging) g->needSub(dst, 0, D3D12_RESOURCE_STATE_COPY_DEST); g->flushBarriers();
        g->list->CopyBufferRegion(dst->gres, dst->goff + x, src->gres, src->goff + a, e - a); if (dst->staging) dst->fenceAt = g->signaled + 1; return;
    }
    if (dst->staging) { CopyToStaging(dst, dstSub, x, y, z, src, srcSub, box); return; }
    g->needSub(src, srcSub, D3D12_RESOURCE_STATE_COPY_SOURCE); g->needSub(dst, dstSub, D3D12_RESOURCE_STATE_COPY_DEST); g->flushBarriers();
    D3D12_TEXTURE_COPY_LOCATION d{ dst->gres, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX }; d.SubresourceIndex = dstSub;
    D3D12_TEXTURE_COPY_LOCATION s{ src->gres, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX }; s.SubresourceIndex = srcSub;
    g->list->CopyTextureRegion(&d, x, y, z, &s, (const D3D12_BOX*)box);
}

// ---- the smaller pictures of a texture (D3D12 has no GenerateMips: each one drawn from the one before)
void DeviceContext::GenerateMips(ShaderResourceView* v) {
    LK; if (!v) return; Resource* r = v->res->real(); if (!r || r->kind != Kind::Tex2D || r->mips < 2) return;
    DXGI_FORMAT f = v->format; auto& pso = g->mipPso[f];
    if (!pso) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC d{}; d.pRootSignature = g->root.Get(); d.VS = { g->mipVS.data(), g->mipVS.size() }; d.PS = { g->mipPS.data(), g->mipPS.size() };
        d.BlendState = g->defBlend; d.SampleMask = 0xFFFFFFFF; d.RasterizerState = g->defRaster; d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; d.DepthStencilState = g->defDepth;
        d.DepthStencilState.DepthEnable = FALSE; d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; d.NumRenderTargets = 1; d.RTVFormats[0] = f; d.SampleDesc = { 1, 0 };
        if (FAILED(g->dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pso)))) { g->log("GenerateMips: no pipeline for this format"); return; }
    }
    const SubRange& R = v->range; UINT last = std::min(r->mips, R.mip0 + R.mips), slices = std::min(r->arraySize, R.slice0 + R.slices);
    g->list->SetPipelineState(pso.Get()); g->curPso = pso.Get(); g->list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D12_GPU_DESCRIPTOR_HANDLE sh = g->smpHeap->GetGPUDescriptorHandleForHeapStart(); sh.ptr += (UINT64)g->mipSmpTable * g->smpInc;
    for (UINT s = R.slice0; s < slices; s++) for (UINT m = R.mip0 + 1; m < last; m++) {
        if (!g->room(1)) { g->list->SetPipelineState(pso.Get()); g->curPso = pso.Get(); g->list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); }
        g->list->SetGraphicsRootDescriptorTable(RP_PS_SMP, sh);
        g->needSub(r, (m - 1) + s * r->mips, ST_SR); g->needSub(r, m + s * r->mips, D3D12_RESOURCE_STATE_RENDER_TARGET); g->flushBarriers();
        UINT i = g->take(1); D3D12_SHADER_RESOURCE_VIEW_DESC sd{}; sd.Format = f; sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY; sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Texture2DArray.MostDetailedMip = m - 1; sd.Texture2DArray.MipLevels = 1; sd.Texture2DArray.FirstArraySlice = s; sd.Texture2DArray.ArraySize = 1;
        g->dev->CreateShaderResourceView(r->gres, &sd, g->gcpu(i)); g->list->SetGraphicsRootDescriptorTable(RP_PS_SRV, g->ggpu(i));
        D3D12_RENDER_TARGET_VIEW_DESC rd{}; rd.Format = f; rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY; rd.Texture2DArray.MipSlice = m; rd.Texture2DArray.FirstArraySlice = s; rd.Texture2DArray.ArraySize = 1;
        g->dev->CreateRenderTargetView(r->gres, &rd, g->scratchRtv); g->list->OMSetRenderTargets(1, &g->scratchRtv, FALSE, nullptr);
        UINT w = std::max(1u, r->width >> m), h = std::max(1u, r->height >> m);
        D3D12_VIEWPORT vp{ 0, 0, (float)w, (float)h, 0, 1 }; D3D12_RECT sc{ 0, 0, (LONG)w, (LONG)h }; g->list->RSSetViewports(1, &vp); g->list->RSSetScissorRects(1, &sc);
        g->list->DrawInstanced(3, 1, 0, 0);
    }
    g->dirty = DT_ALL;
}

// ---- ray tracing: acceleration structures, the scene, a compute pass tracing into it
HRESULT DeviceContext::BuildBlas(const RtGeometry* gs, UINT n, AccelStruct** out) {
    LK; *out = nullptr; if (!g->rtInline || !gs || !n) return E_FAIL;
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> d(n);
    for (UINT i = 0; i < n; i++) {
        Resource* vb = gs[i].vb ? gs[i].vb->real() : nullptr; Resource* ib = gs[i].ib ? gs[i].ib->real() : nullptr;
        if (!vb || !ib || vb->virt || ib->virt || !vb->gres || !ib->gres || !gs[i].indexCount) return E_INVALIDARG;
        g->needSub(vb, 0, ST_BUFREAD); g->needSub(ib, 0, ST_BUFREAD);
        D3D12_RAYTRACING_GEOMETRY_DESC& e = d[i]; e.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES; e.Flags = gs[i].opaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
        e.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT; e.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT; e.Triangles.IndexCount = gs[i].indexCount;
        e.Triangles.VertexCount = gs[i].vertexCount ? gs[i].vertexCount : vb->byteWidth / std::max(1u, gs[i].stride);
        e.Triangles.IndexBuffer = ib->gpuVA(); e.Triangles.VertexBuffer = { vb->gpuVA(), gs[i].stride };
    }
    g->flushBarriers();
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in{}; in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE; in.NumDescs = n; in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY; in.pGeometryDescs = d.data();
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO pi{}; g->dev5->GetRaytracingAccelerationStructurePrebuildInfo(&in, &pi);
    if (!pi.ResultDataMaxSizeInBytes) return E_FAIL;
    auto* a = new AccelStruct; a->size = AlignUp(pi.ResultDataMaxSizeInBytes, 256); a->page = g->asAlloc(a->size, &a->off);
    if (!a->page) { delete a; return E_OUTOFMEMORY; }
    a->va = a->page->res->GetGPUVirtualAddress() + a->off;
    for (UINT i = 0; i < n; i++) a->geos.push_back({ gs[i].vb, gs[i].ib });
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bd{}; bd.DestAccelerationStructureData = a->va; bd.Inputs = in; bd.ScratchAccelerationStructureData = g->scratchAlloc(pi.ScratchDataSizeInBytes);
    if (!bd.ScratchAccelerationStructureData) { delete a; return E_OUTOFMEMORY; }
    g->flushBarriers(); g->list4->BuildRaytracingAccelerationStructure(&bd, 0, nullptr); g->blasPending = true;
    *out = a; return S_OK;
}
struct RtInstGpu { uint32_t v[8]; };
void DeviceContext::BuildTlas(const RtInstance* inst, UINT n) {
    LK; g->tlasReady = false; if (!g->rtInline) return;
    if (g->blasPending) { g->uavBarrier(); g->blasPending = false; }   // (the meshes built this frame are done before the scene is)
    g->geoRes.clear(); std::unordered_map<ID3D12Resource*, uint32_t> gi;
    auto idxOf = [&](Buffer* b) -> uint32_t { Resource* r = b->real(); auto it = gi.find(r->gres); if (it != gi.end()) return it->second;
        if (g->geoRes.size() >= 256) return 0xFFFFFFFF; uint32_t k = (uint32_t)g->geoRes.size(); g->geoRes.push_back(r); gi[r->gres] = k; return k; };
    UINT m = std::max(1u, n);
    ID3D12Resource* dres; uint64_t doff; auto* descs = (D3D12_RAYTRACING_INSTANCE_DESC*)g->upload((uint64_t)m * sizeof(D3D12_RAYTRACING_INSTANCE_DESC), D3D12_RAYTRACING_INSTANCE_DESCS_BYTE_ALIGNMENT, &dres, &doff);
    auto* info = (RtInstGpu*)g->upload((uint64_t)m * sizeof(RtInstGpu), sizeof(RtInstGpu), &g->instRes, &g->instOff);
    if (!descs || !info) return;
    std::memset(descs, 0, (size_t)m * sizeof(D3D12_RAYTRACING_INSTANCE_DESC)); std::memset(info, 0xFF, (size_t)m * sizeof(RtInstGpu));
    UINT k = 0;
    for (UINT i = 0; i < n; i++) {
        AccelStruct* a = inst[i].blas; if (!a) continue;
        D3D12_RAYTRACING_INSTANCE_DESC& d = descs[k]; std::memcpy(d.Transform, inst[i].transform, sizeof d.Transform);
        d.InstanceID = inst[i].id & 0xFFFFFF; d.InstanceMask = inst[i].mask & 0xFF; d.InstanceContributionToHitGroupIndex = 0; d.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE; d.AccelerationStructure = a->va;
        RtInstGpu& f = info[k];
        for (size_t j = 0; j < std::min<size_t>(a->geos.size(), 2); j++) {
            Buffer* vb = a->geos[j].vb.Get(); Buffer* ib = a->geos[j].ib.Get(); if (!vb || !ib) continue;
            f.v[j * 4 + 0] = idxOf(vb); f.v[j * 4 + 1] = (uint32_t)vb->real()->goff; f.v[j * 4 + 2] = idxOf(ib); f.v[j * 4 + 3] = (uint32_t)ib->real()->goff;
        }
        k++;
    }
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in{}; in.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    in.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE; in.NumDescs = k; in.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    in.InstanceDescs = dres->GetGPUVirtualAddress() + doff;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO pi{}; g->dev5->GetRaytracingAccelerationStructurePrebuildInfo(&in, &pi);
    uint64_t need = AlignUp(std::max<uint64_t>(pi.ResultDataMaxSizeInBytes, 256), 65536);
    ComPtr<ID3D12Resource>& t = g->tlas[g->slot];
    if (!t || g->tlasSize[g->slot] < need) { if (t) g->deadRes.push_back({ g->signaled + 1, t }); t = g->makeBuffer(need * 2, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, true); g->tlasSize[g->slot] = need * 2; if (!t) return; }
    g->uavBarrier(t.Get());   // (a pass earlier this frame may still be reading it)
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bd{}; bd.DestAccelerationStructureData = t->GetGPUVirtualAddress(); bd.Inputs = in;
    bd.ScratchAccelerationStructureData = g->scratchAlloc(pi.ScratchDataSizeInBytes); if (!bd.ScratchAccelerationStructureData) return;
    g->flushBarriers(); g->list4->BuildRaytracingAccelerationStructure(&bd, 0, nullptr);
    g->uavBarrier(t.Get()); g->flushBarriers();
    g->tlasVA = t->GetGPUVirtualAddress(); g->instCount = k; g->tlasReady = true;
}
void DeviceContext::DispatchRays(ComputeShader* cs, UINT x, UINT y, UINT z, UnorderedAccessView* out, ShaderBase* dimsLike) {
    LK; if (!g->tlasReady || !cs || !out || !x || !y || !z) return;
    StageB& st = g->b.ps; const UINT NSRV = SRV_SLOTS, NCB = CB_SLOTS, NSMP = SMP_SLOTS, NGEO = 256;
    for (auto& c : st.cb) if (c) g->refresh(c.Get());
    for (auto& v : st.srv) if (v && v->res->virt) g->refresh(v->res.Get());
    for (auto& v : st.srv) if (v) g->needView(v.Get(), ST_SR);
    g->needView(out, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    for (Resource* r : g->geoRes) g->needSub(r, 0, ST_BUFREAD);
    g->room(NCB + NSRV + UAV_SLOTS + 3 + NGEO);
    g->flushBarriers();
    auto& pso = g->csPsos[cs->uid];
    if (!pso) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC d{}; d.pRootSignature = g->rootCS.Get(); d.CS = { cs->code.data(), cs->code.size() };
        HRESULT hr = g->dev->CreateComputePipelineState(&d, IID_PPV_ARGS(&pso));
        if (FAILED(hr)) { char m[96]; snprintf(m, sizeof m, "ray tracing: its compute pipeline could not be made (0x%08X)", (unsigned)hr); g->log(m); g->pollMessages(); return; }
    }
    g->list->SetComputeRootSignature(g->rootCS.Get()); g->list->SetPipelineState(pso.Get()); g->curPso = nullptr; g->dirty |= DT_PSO;
    // the pixel shader's constant buffers, textures, samplers
    UINT cbAt = g->take(NCB);
    for (UINT i = 0; i < NCB; i++) {
        Buffer* bf = st.cb[i].Get(); D3D12_CONSTANT_BUFFER_VIEW_DESC cv{};
        if (bf && (bf->virt || bf->gres)) { cv.BufferLocation = bf->gpuVA(); cv.SizeInBytes = (UINT)AlignUp(bf->byteWidth, 256); }
        if (!cv.BufferLocation) { cv.BufferLocation = g->zeroCb->GetGPUVirtualAddress(); cv.SizeInBytes = 65536; }
        g->dev->CreateConstantBufferView(&cv, g->gcpu(cbAt + i));
    }
    UINT srvAt = g->take(NSRV);
    for (UINT i = 0; i < NSRV; i++) {
        ShaderResourceView* v = st.srv[i].Get(); Resource* r = v ? v->res->real() : nullptr;
        if (r && r->virt) { D3D12_SHADER_RESOURCE_VIEW_DESC o = v->desc; UINT es = o.Buffer.StructureByteStride ? o.Buffer.StructureByteStride : std::max(1u, BitsPerPixel(o.Format) / 8);
            o.Buffer.FirstElement = r->voff / es + v->firstEl; g->dev->CreateShaderResourceView(r->vres, &o, g->gcpu(srvAt + i)); continue; }
        D3D12_CPU_DESCRIPTOR_HANDLE src = v && v->cpu.ptr ? v->cpu : (dimsLike && (int)i <= dimsLike->srvMax ? NullSrvFor(dimsLike, i) : g->nullSrv[D3D12_SRV_DIMENSION_TEXTURE2D]);
        g->dev->CopyDescriptorsSimple(1, g->gcpu(srvAt + i), src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    D3D12_GPU_DESCRIPTOR_HANDLE smpH;
    {   // samplers: a table like the pixel shader's (found again or added)
        std::vector<uint32_t> key(NSMP + 1); key[NSMP] = 0xC0FFEE; for (UINT i = 0; i < NSMP; i++) key[i] = st.smp[i] ? st.smp[i]->uid : 0;
        auto it = g->smpTables.find(key); UINT at;
        if (it != g->smpTables.end()) at = it->second;
        else {
            if (g->smpPos + NSMP > SMP_HEAP) { g->smpTables.clear(); g->smpPos = SMP_SLOTS; }   // (rare: the pixel shaders' tables are made again as they are needed)
            at = g->smpPos; g->smpPos += NSMP;
            for (UINT i = 0; i < NSMP; i++) { D3D12_CPU_DESCRIPTOR_HANDLE dst = g->smpHeap->GetCPUDescriptorHandleForHeapStart(); dst.ptr += (SIZE_T)(at + i) * g->smpInc;
                g->dev->CopyDescriptorsSimple(1, dst, st.smp[i] ? st.smp[i]->cpu : g->defSampler->cpu, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER); }
            g->smpTables[key] = at;
        }
        smpH = g->smpHeap->GetGPUDescriptorHandleForHeapStart(); smpH.ptr += (UINT64)at * g->smpInc;
    }
    UINT uavAt = g->take(UAV_SLOTS); for (UINT i = 0; i < UAV_SLOTS; i++) g->dev->CopyDescriptorsSimple(1, g->gcpu(uavAt + i), g->nullUav[4], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    // the scene, where each instance's triangles are, the output
    UINT rtAt = g->take(3);
    { D3D12_SHADER_RESOURCE_VIEW_DESC sd{}; sd.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE; sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      sd.RaytracingAccelerationStructure.Location = g->tlasVA; g->dev->CreateShaderResourceView(nullptr, &sd, g->gcpu(rtAt)); }
    { D3D12_SHADER_RESOURCE_VIEW_DESC sd{}; sd.ViewDimension = D3D12_SRV_DIMENSION_BUFFER; sd.Format = DXGI_FORMAT_UNKNOWN; sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      sd.Buffer.FirstElement = g->instOff / sizeof(RtInstGpu); sd.Buffer.NumElements = std::max(1u, g->instCount); sd.Buffer.StructureByteStride = sizeof(RtInstGpu);
      g->dev->CreateShaderResourceView(g->instRes, &sd, g->gcpu(rtAt + 1)); }
    g->dev->CopyDescriptorsSimple(1, g->gcpu(rtAt + 2), out->cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    UINT geoAt = g->take(NGEO);
    for (UINT i = 0; i < NGEO; i++) {
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{}; sd.ViewDimension = D3D12_SRV_DIMENSION_BUFFER; sd.Format = DXGI_FORMAT_R32_TYPELESS; sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        if (i < g->geoRes.size()) { ID3D12Resource* r = g->geoRes[i]->gres; sd.Buffer.NumElements = (UINT)(r->GetDesc().Width / 4); g->dev->CreateShaderResourceView(r, &sd, g->gcpu(geoAt + i)); }
        else { sd.Buffer.NumElements = 1; g->dev->CreateShaderResourceView(nullptr, &sd, g->gcpu(geoAt + i)); }
    }
    g->list->SetComputeRootDescriptorTable(0, g->ggpu(cbAt)); g->list->SetComputeRootDescriptorTable(1, g->ggpu(srvAt)); g->list->SetComputeRootDescriptorTable(2, smpH);
    g->list->SetComputeRootDescriptorTable(3, g->ggpu(uavAt)); g->list->SetComputeRootDescriptorTable(4, g->ggpu(rtAt)); g->list->SetComputeRootDescriptorTable(5, g->ggpu(geoAt));
    g->list->Dispatch(x, y, z);
    g->uavBarrier(out->res->real()->gres); g->flushBarriers();
}

// ---- GPU timing
void DeviceContext::Begin(Query*) {}
void DeviceContext::End(Query* q) {
    LK; if (!q) return;
    if (q->type == D3D11_QUERY_TIMESTAMP) { g->list->EndQuery(g->qheap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, q->slot); g->list->ResolveQueryData(g->qheap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, q->slot, 1, g->qbuf.Get(), (UINT64)q->slot * 8); }
    q->fenceAt = g->signaled + 1; q->ended = true;
}
HRESULT DeviceContext::GetData(Query* q, void* data, UINT size, UINT flags) {
    LK; if (!q || !q->ended) return S_FALSE;
    if (g->fence->GetCompletedValue() < q->fenceAt) { if (!(flags & D3D11_ASYNC_GETDATA_DONOTFLUSH) && q->fenceAt > g->signaled) g->flush(false); return S_FALSE; }
    if (!data) return S_OK;
    if (q->type == D3D11_QUERY_TIMESTAMP && size >= 8) { *(UINT64*)data = g->qcpu[q->slot]; return S_OK; }
    if (q->type == D3D11_QUERY_TIMESTAMP_DISJOINT && size >= sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT)) { auto* d = (D3D11_QUERY_DATA_TIMESTAMP_DISJOINT*)data; d->Frequency = g->tsFreq; d->Disjoint = FALSE; return S_OK; }
    if (q->type == D3D11_QUERY_EVENT && size >= sizeof(BOOL)) { *(BOOL*)data = TRUE; return S_OK; }
    return S_OK;
}

// ---------------------------------------------------------------- the swap chain: a frame ends
static void TakeBuffers() {
    DXGI_SWAP_CHAIN_DESC1 sd{}; g->swap->GetDesc1(&sd);
    if (g->bufs.size() != sd.BufferCount) { g->bufs.clear(); for (UINT i = 0; i < sd.BufferCount; i++) { auto* t = new Texture2D; t->kind = Kind::Tex2D; ComPtr<Texture2D> c; c.Attach(t); g->bufs.push_back(c); } }
    for (UINT i = 0; i < sd.BufferCount; i++) {
        Texture2D* t = g->bufs[i].Get(); t->own.Reset(); g->swap->GetBuffer(i, IID_PPV_ARGS(&t->own)); t->gres = t->own.Get();
        t->width = sd.Width; t->height = sd.Height; t->format = sd.Format; t->mips = 1; t->arraySize = 1; t->bind = D3D11_BIND_RENDER_TARGET; t->ownStates.assign(1, D3D12_RESOURCE_STATE_PRESENT);
    }
    g->bbIndex = g->swap->GetCurrentBackBufferIndex();
    if (!g->proxy) { auto* p = new Texture2D; p->kind = Kind::Tex2D; p->swapOf = &g->swapObj; g->proxy.Attach(p); }
}
HRESULT SwapChain::GetBuffer(UINT, Texture2D** out) { LK; *out = g->proxy.Get(); (*out)->AddRef(); return S_OK; }
HRESULT SwapChain::Present(UINT sync, UINT flags) {
    LK;
    g->needAll(g->bufs[g->bbIndex].Get(), D3D12_RESOURCE_STATE_PRESENT);
    g->submit();
    HRESULT hr = g->swap->Present(sync, flags);
    g->signal(); g->fr[g->slot].fence = g->signaled;
    g->pollMessages();
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG) g->deviceLost(hr);
    g->slot = (g->slot + 1) % FRAMES; g->waitFor(g->fr[g->slot].fence);
    g->collect();
    g->fr[g->slot].alloc->Reset(); g->list->Reset(g->fr[g->slot].alloc.Get(), nullptr); g->resetRings(); g->setupList();
    g->bbIndex = g->swap->GetCurrentBackBufferIndex();
    return hr;
}
HRESULT SwapChain::ResizeBuffers(UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT flags) {
    LK; g->flush(true); g->collect();
    for (auto& b : g->bufs) { b->own.Reset(); b->gres = nullptr; }
    HRESULT hr = g->swap->ResizeBuffers(n, w, h, f, flags);
    TakeBuffers(); return hr;
}

// ---------------------------------------------------------------- setting it all up
static const char* kMipShader = R"(
struct V { float4 p : SV_Position; float2 uv : TEXCOORD0; };
V VS(uint id : SV_VertexID) { V o; float2 t = float2((id << 1) & 2, id & 2); o.uv = t; o.p = float4(t * float2(2, -2) + float2(-1, 1), 0, 1); return o; }
Texture2DArray<float4> src : register(t0); SamplerState s : register(s0);
float4 PS(V i) : SV_Target { return src.SampleLevel(s, float3(i.uv, 0), 0); }
)";
void Log(const std::string& line) { if (g) { LK; g->log(line); } }
HRESULT Create(const Setup& s, Device** outDev, DeviceContext** outCtx, SwapChain** outSwap, std::string* driverName) {
    g = new Core; g->logPath = s.logPath; g->debug = s.debug;
    if (s.debug) { ComPtr<ID3D12Debug> dbg; if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) dbg->EnableDebugLayer(); else g->debug = false; }
    { ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred; if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred)))) { dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON); dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON); } }
    HRESULT hr = CreateDXGIFactory2(g->debug ? DXGI_CREATE_FACTORY_DEBUG : 0, IID_PPV_ARGS(&g->factory));
    if (FAILED(hr)) hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&g->factory)); if (FAILED(hr)) return hr;
    if (!s.warp) for (UINT i = 0;; i++) {   // the fastest card that does D3D12
        ComPtr<IDXGIAdapter1> a; if (FAILED(g->factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&a)))) break;
        DXGI_ADAPTER_DESC1 d; a->GetDesc1(&d); if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (SUCCEEDED(D3D12CreateDevice(a.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g->dev)))) { g->adapter = a; g->adesc = d; break; }
    }
    if (!g->dev) {   // no card: WARP (the processor draws it)
        ComPtr<IDXGIAdapter1> a; if (FAILED(g->factory->EnumWarpAdapter(IID_PPV_ARGS(&a)))) return E_FAIL;
        hr = D3D12CreateDevice(a.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g->dev)); if (FAILED(hr)) return hr;
        g->adapter = a; a->GetDesc1(&g->adesc); if (driverName) *driverName = "software (WARP, Direct3D 12)";
    } else if (driverName) { char n[256]; WideCharToMultiByte(CP_UTF8, 0, g->adesc.Description, -1, n, sizeof n, nullptr, nullptr); *driverName = std::string(n) + " (Direct3D 12)"; }
    if (SUCCEEDED(g->dev.As(&g->dev5))) {
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5{}; if (SUCCEEDED(g->dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof o5))) g->rt = o5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0;
        D3D12_FEATURE_DATA_SHADER_MODEL sm{ D3D_SHADER_MODEL_6_5 }; bool sm65 = SUCCEEDED(g->dev->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof sm)) && sm.HighestShaderModel >= D3D_SHADER_MODEL_6_5;
        g->rtInline = g->rt && o5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1 && sm65;
    }
    if (g->debug && SUCCEEDED(g->dev.As(&g->iq))) { g->iq->SetMessageCountLimit(4096); D3D12_MESSAGE_SEVERITY deny[] = { D3D12_MESSAGE_SEVERITY_INFO, D3D12_MESSAGE_SEVERITY_MESSAGE }; D3D12_INFO_QUEUE_FILTER fl{}; fl.DenyList.NumSeverities = 2; fl.DenyList.pSeverityList = deny; g->iq->PushStorageFilter(&fl); }
    ID3D12Device* d = g->dev.Get();
    D3D12_COMMAND_QUEUE_DESC qd{ D3D12_COMMAND_LIST_TYPE_DIRECT }; if (FAILED(hr = d->CreateCommandQueue(&qd, IID_PPV_ARGS(&g->queue)))) return hr;
    if (FAILED(hr = d->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g->fence)))) return hr; g->ev = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    for (auto& f : g->fr) if (FAILED(hr = d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&f.alloc)))) return hr;
    if (FAILED(hr = d->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g->fr[0].alloc.Get(), nullptr, IID_PPV_ARGS(&g->list)))) return hr;
    g->list.As(&g->list4);
    g->queue->GetTimestampFrequency(&g->tsFreq);
    // descriptor heaps
    g->cpuSrv.type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; g->cpuRtv.type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; g->cpuDsv.type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV; g->cpuSmp.type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    for (CpuHeap* h : { &g->cpuSrv, &g->cpuRtv, &g->cpuDsv, &g->cpuSmp }) h->inc = d->GetDescriptorHandleIncrementSize(h->type);
    { D3D12_DESCRIPTOR_HEAP_DESC hd{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, RING_DESC * FRAMES, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0 }; if (FAILED(hr = d->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g->gpuHeap)))) return hr; }
    { D3D12_DESCRIPTOR_HEAP_DESC hd{ D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, SMP_HEAP, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0 }; if (FAILED(hr = d->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g->smpHeap)))) return hr; }
    g->gpuInc = d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); g->smpInc = d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    // empty views for slots nothing is bound to (reading one gives 0, as in D3D11)
    for (UINT k = 1; k <= 10; k++) {
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{}; sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.ViewDimension = (D3D12_SRV_DIMENSION)k; sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        switch (k) { case 1: sd.Format = DXGI_FORMAT_R32_FLOAT; sd.Buffer.NumElements = 1; break; case 2: sd.Texture1D.MipLevels = 1; break; case 3: sd.Texture1DArray.MipLevels = 1; sd.Texture1DArray.ArraySize = 1; break;
            case 4: sd.Texture2D.MipLevels = 1; break; case 5: sd.Texture2DArray.MipLevels = 1; sd.Texture2DArray.ArraySize = 1; break; case 7: sd.Texture2DMSArray.ArraySize = 1; break;
            case 8: sd.Texture3D.MipLevels = 1; break; case 9: sd.TextureCube.MipLevels = 1; break; case 10: sd.TextureCubeArray.MipLevels = 1; sd.TextureCubeArray.NumCubes = 1; break; default: break; }
        g->nullSrv[k] = g->cpuSrv.alloc(d); d->CreateShaderResourceView(nullptr, &sd, g->nullSrv[k]);
    }
    { D3D12_SHADER_RESOURCE_VIEW_DESC sd{}; sd.Format = DXGI_FORMAT_UNKNOWN; sd.ViewDimension = D3D12_SRV_DIMENSION_BUFFER; sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sd.Buffer.NumElements = 1; sd.Buffer.StructureByteStride = 16;
      g->nullStruct = g->cpuSrv.alloc(d); d->CreateShaderResourceView(nullptr, &sd, g->nullStruct); }
    for (UINT k = 1; k <= 8; k++) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.Format = DXGI_FORMAT_R32_FLOAT;
        switch (k) { case 1: ud.ViewDimension = D3D12_UAV_DIMENSION_BUFFER; ud.Buffer.NumElements = 1; break; case 2: ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D; break; case 3: ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY; ud.Texture1DArray.ArraySize = 1; break;
            case 5: ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY; ud.Texture2DArray.ArraySize = 1; break; case 8: ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D; ud.Texture3D.WSize = 1; break; default: ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D; break; }
        g->nullUav[k] = g->cpuSrv.alloc(d); d->CreateUnorderedAccessView(nullptr, nullptr, &ud, g->nullUav[k]);
    }
    g->nullUav[0] = g->nullUav[4];
    { D3D12_RENDER_TARGET_VIEW_DESC rd{}; rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D; g->nullRtv = g->cpuRtv.alloc(d); d->CreateRenderTargetView(nullptr, &rd, g->nullRtv); g->scratchRtv = g->cpuRtv.alloc(d); }
    {   // a constant buffer of zeros for slots nothing is bound to
        D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_UPLOAD }; D3D12_RESOURCE_DESC bd{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, 65536, 1, 1, 1, DXGI_FORMAT_UNKNOWN, { 1, 0 }, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
        if (FAILED(hr = d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g->zeroCb)))) return hr;
        void* p = nullptr; g->zeroCb->Map(0, nullptr, &p); std::memset(p, 0, 65536); g->zeroCb->Unmap(0, nullptr);
    }
    // the default states (what D3D11 used with nothing set)
    g->defBlend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    for (auto& rt : g->defBlend.RenderTarget) { rt.SrcBlend = D3D12_BLEND_ONE; rt.DestBlend = D3D12_BLEND_ZERO; rt.BlendOp = D3D12_BLEND_OP_ADD; rt.SrcBlendAlpha = D3D12_BLEND_ONE; rt.DestBlendAlpha = D3D12_BLEND_ZERO; rt.BlendOpAlpha = D3D12_BLEND_OP_ADD; rt.LogicOp = D3D12_LOGIC_OP_NOOP; rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL; }
    g->defRaster = { D3D12_FILL_MODE_SOLID, D3D12_CULL_MODE_BACK, FALSE, 0, 0.f, 0.f, TRUE, FALSE, FALSE, 0, D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF };
    g->defDepth.DepthEnable = TRUE; g->defDepth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL; g->defDepth.DepthFunc = D3D12_COMPARISON_FUNC_LESS; g->defDepth.StencilReadMask = 0xFF; g->defDepth.StencilWriteMask = 0xFF;
    g->defDepth.FrontFace = g->defDepth.BackFace = { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS };
    {   // samplers: D3D11's default (linear, clamped) for empty slots; the first 16 places of the sampler heap are the layer's own
        D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxAnisotropy = 1; sd.ComparisonFunc = D3D11_COMPARISON_NEVER; sd.MaxLOD = D3D11_FLOAT32_MAX;
        SamplerState* ss = nullptr; g->device.CreateSamplerState(&sd, &ss); g->defSampler.Attach(ss);
        D3D12_CPU_DESCRIPTOR_HANDLE dst = g->smpHeap->GetCPUDescriptorHandleForHeapStart(); d->CopyDescriptorsSimple(1, dst, ss->cpu, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        g->mipSmpTable = 0; g->smpPos = SMP_SLOTS;
    }
    {   // one root signature for everything: per stage a table of constant buffers, of textures, of samplers; the pixel shader's UAVs
        D3D12_DESCRIPTOR_RANGE rg[7] = {
            { D3D12_DESCRIPTOR_RANGE_TYPE_CBV, CB_SLOTS, 0, 0, 0 }, { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, SRV_SLOTS, 0, 0, 0 }, { D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, SMP_SLOTS, 0, 0, 0 },
            { D3D12_DESCRIPTOR_RANGE_TYPE_CBV, CB_SLOTS, 0, 0, 0 }, { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, SRV_SLOTS, 0, 0, 0 }, { D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, SMP_SLOTS, 0, 0, 0 },
            { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, UAV_SLOTS, 0, 0, 0 } };
        D3D12_ROOT_PARAMETER rp[7] = {};
        for (int i = 0; i < 7; i++) { rp[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[i].DescriptorTable = { 1, &rg[i] }; rp[i].ShaderVisibility = i < 3 ? D3D12_SHADER_VISIBILITY_VERTEX : D3D12_SHADER_VISIBILITY_PIXEL; }
        D3D12_ROOT_SIGNATURE_DESC rd{ 7, rp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT };
        ComPtr<ID3DBlob> blob, err; if (FAILED(hr = D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) { if (err) g->log((const char*)err->GetBufferPointer()); return hr; }
        if (FAILED(hr = d->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&g->root)))) return hr;
    }
    if (g->rtInline) {   // the ray tracing passes' root signature: the pixel shader's tables (seen by a compute shader), the scene, the meshes' buffers
        D3D12_DESCRIPTOR_RANGE rg[7] = {
            { D3D12_DESCRIPTOR_RANGE_TYPE_CBV, CB_SLOTS, 0, 0, 0 }, { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, SRV_SLOTS, 0, 0, 0 }, { D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, SMP_SLOTS, 0, 0, 0 },
            { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, UAV_SLOTS, 0, 0, 0 },
            { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 1, 0 }, { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 1, 2 },
            { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 256, 0, 2, 0 } };
        D3D12_ROOT_PARAMETER rp[6] = {};
        for (int i = 0; i < 6; i++) { rp[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; }
        rp[0].DescriptorTable = { 1, &rg[0] }; rp[1].DescriptorTable = { 1, &rg[1] }; rp[2].DescriptorTable = { 1, &rg[2] }; rp[3].DescriptorTable = { 1, &rg[3] };
        rp[4].DescriptorTable = { 2, &rg[4] }; rp[5].DescriptorTable = { 1, &rg[6] };
        D3D12_ROOT_SIGNATURE_DESC rd{ 6, rp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE };
        ComPtr<ID3DBlob> blob, err;
        if (FAILED(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)) || FAILED(d->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&g->rootCS)))) {
            if (err) g->log((const char*)err->GetBufferPointer()); g->rtInline = false; }
    }
    {   // the mip shader
        ComPtr<ID3DBlob> vs, ps, err;
        if (FAILED(hr = D3DCompile(kMipShader, std::strlen(kMipShader), "mips", nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vs, &err))) return hr;
        if (FAILED(hr = D3DCompile(kMipShader, std::strlen(kMipShader), "mips", nullptr, nullptr, "PS", "ps_5_0", 0, 0, &ps, &err))) return hr;
        g->mipVS.assign((uint8_t*)vs->GetBufferPointer(), (uint8_t*)vs->GetBufferPointer() + vs->GetBufferSize()); g->mipPS.assign((uint8_t*)ps->GetBufferPointer(), (uint8_t*)ps->GetBufferPointer() + ps->GetBufferSize());
    }
    {   // GPU timing
        D3D12_QUERY_HEAP_DESC qh{ D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 4096, 0 }; if (SUCCEEDED(d->CreateQueryHeap(&qh, IID_PPV_ARGS(&g->qheap)))) {
            D3D12_HEAP_PROPERTIES hp{ D3D12_HEAP_TYPE_READBACK }; D3D12_RESOURCE_DESC bd{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, 4096 * 8, 1, 1, 1, DXGI_FORMAT_UNKNOWN, { 1, 0 }, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE };
            if (SUCCEEDED(d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&g->qbuf)))) { g->qbuf->Map(0, nullptr, (void**)&g->qcpu); for (UINT i = 4096; i-- > 0;) g->qfree.push_back(i); }
        }
    }
    {   // the swap chain
        DXGI_SWAP_CHAIN_DESC1 sd{}; sd.Format = s.format; sd.SampleDesc = { 1, 0 }; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = std::max(2u, s.buffers);
        sd.Scaling = DXGI_SCALING_STRETCH; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED; sd.Flags = s.tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
        ComPtr<IDXGISwapChain1> sc1; if (FAILED(hr = g->factory->CreateSwapChainForHwnd(g->queue.Get(), s.hwnd, &sd, nullptr, nullptr, &sc1))) return hr;
        if (FAILED(hr = sc1.As(&g->swap))) return hr; g->swapFlags = sd.Flags; g->swapFormat = s.format;
        TakeBuffers();
    }
    g->resetRings(); g->setupList();
    *outDev = &g->device; *outCtx = &g->context; *outSwap = &g->swapObj;
    g->log(std::string("Direct3D 12 on ") + (driverName ? *driverName : std::string("?")) + (g->rtInline ? ", ray tracing: yes (DXR 1.1)" : g->rt ? ", ray tracing: DXR 1.0 only (RTX passes need 1.1)" : ", ray tracing: no") + (g->debug ? ", debug layer on" : ""));
    return S_OK;
}
void Shutdown() { if (!g) return; LK; g->flush(true); g->collect(); }   // (the layer itself stays: the program is ending)

} // namespace d12
