#pragma once
// PRIMEngine on Direct3D 12: a layer that takes the same calls the engine made to Direct3D 11 (the same descriptions, the same
// order of things) and does them on D3D12 - resource states and barriers, descriptor heaps, pipeline states, uploads, frames in
// flight. The engine's drawing code stays as it was; the D3D12 device and command list are open for what only D3D12 can do
// (ray tracing). Uses the D3D11 headers only for their descriptions and enums.
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace d12 {
using Microsoft::WRL::ComPtr;
class Device; class DeviceContext; class SwapChain; class Resource;
struct Page;

// ---- every object: counted like a COM object; at zero it is let go once the GPU is done with the frame it was last used in
class DeviceChild {
public:
    DeviceChild();
    virtual ~DeviceChild() = default;
    ULONG AddRef() { return (ULONG)++refs; }
    ULONG Release();
    HRESULT QueryInterface(REFIID, void** p) { if (p) *p = nullptr; return E_NOINTERFACE; }
    uint32_t uid;   // never used twice (pipeline states are found by these)
private:
    std::atomic<long> refs{ 1 };
};

enum class Kind : uint8_t { Buffer, Tex1D, Tex2D, Tex3D };

class Resource : public DeviceChild {
public:
    Kind kind = Kind::Buffer;
    D3D11_USAGE usage = D3D11_USAGE_DEFAULT; UINT bind = 0, cpu = 0, misc = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN; UINT width = 1, height = 1, depth = 1, mips = 1, arraySize = 1;   // textures
    UINT byteWidth = 0, stride = 0;                                                                              // buffers
    UINT subresources() const { return mips * (kind == Kind::Tex3D ? 1 : arraySize); }
    // where it lives on the GPU: its own resource, or a stretch of a shared page (small vertex / index buffers)
    ComPtr<ID3D12Resource> own; ID3D12Resource* gres = nullptr; uint64_t goff = 0; Page* page = nullptr;
    std::vector<D3D12_RESOURCE_STATES> ownStates;
    std::vector<D3D12_RESOURCE_STATES>& states();
    // constant and dynamic buffers: kept on the CPU, copied into the frame's upload memory when changed (each change: a new place)
    bool virt = false; std::vector<uint8_t> shadow; ID3D12Resource* vres = nullptr; uint64_t voff = 0; uint64_t vepoch = ~0ull;
    uint8_t* mapPtr = nullptr; ID3D12Resource* mapRes = nullptr; uint64_t mapOff = 0; UINT mapSub = 0; UINT mapPitch = 0;
    // read back (staging): a readback buffer laid out like the texture
    bool staging = false; std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> fp; std::vector<UINT> fpRows; uint8_t* rbPtr = nullptr; uint64_t fenceAt = 0;
    // the swap chain's "buffer 0": always the one being drawn this frame
    SwapChain* swapOf = nullptr;
    Resource* real();
    D3D12_GPU_VIRTUAL_ADDRESS gpuVA();
    ID3D12Resource* d3d() { return real()->gres; }   // (for ray tracing and other D3D12-only work)
    ~Resource() override;
};
class Buffer : public Resource { public: void GetDesc(D3D11_BUFFER_DESC* d); };
class Texture2D : public Resource { public: void GetDesc(D3D11_TEXTURE2D_DESC* d); };
class Texture3D : public Resource { public: void GetDesc(D3D11_TEXTURE3D_DESC* d); };

struct SubRange { UINT mip0 = 0, mips = 1, slice0 = 0, slices = 1; };
class View : public DeviceChild {
public:
    ComPtr<Resource> res; SubRange range; DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{}; std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> perBuffer;   // perBuffer: one for each swap chain buffer
    int heapType = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE handle();
    void GetResource(Resource** r) { *r = res.Get(); if (*r) (*r)->AddRef(); }
    bool overlaps(const View& o) const;
    ~View() override;
};
class ShaderResourceView : public View { public: D3D12_SHADER_RESOURCE_VIEW_DESC desc{}; UINT firstEl = 0, numEl = 0; };   // (a dynamic buffer's view is made where it is now, at each draw)
class RenderTargetView : public View { public: D3D12_RENDER_TARGET_VIEW_DESC desc{}; };
class DepthStencilView : public View { public: bool readOnly = false; };
class UnorderedAccessView : public View { public: D3D12_UNORDERED_ACCESS_VIEW_DESC desc{}; };

class ShaderBase : public DeviceChild {
public:
    std::vector<uint8_t> code;
    uint8_t srvDim[64] = {}; bool srvStructured[64] = {}; int srvMax = -1, cbMax = -1, smpMax = -1, uavMax = -1; uint8_t uavDim[8] = {};   // from the shader itself
};
class VertexShader : public ShaderBase {};
class PixelShader : public ShaderBase {};
class InputLayout : public DeviceChild { public: std::vector<D3D12_INPUT_ELEMENT_DESC> el; std::vector<std::string> names; };
class BlendState : public DeviceChild { public: D3D12_BLEND_DESC d{}; };
class RasterizerState : public DeviceChild { public: D3D12_RASTERIZER_DESC d{}; bool scissor = false; };
class DepthStencilState : public DeviceChild { public: D3D12_DEPTH_STENCIL_DESC d{}; };
class SamplerState : public DeviceChild { public: D3D12_SAMPLER_DESC d{}; D3D12_CPU_DESCRIPTOR_HANDLE cpu{}; ~SamplerState() override; };
class ComputeShader : public ShaderBase {};
// ---- ray tracing (DXR 1.1: ray queries): a mesh's acceleration structure (one or more triangle lists), the frame's scene of them
class AccelStruct : public DeviceChild {
public:
    Page* page = nullptr; uint64_t off = 0, size = 0; D3D12_GPU_VIRTUAL_ADDRESS va = 0;
    struct Geo { ComPtr<Buffer> vb, ib; }; std::vector<Geo> geos;   // (kept: the scene's shader reads their vertices)
    ~AccelStruct() override;
};
struct RtGeometry { Buffer* vb = nullptr; UINT stride = 0, vertexCount = 0; Buffer* ib = nullptr; UINT indexCount = 0; bool opaque = true; };   // positions: float3 at the start of each vertex; indices: 32-bit
struct RtInstance { AccelStruct* blas = nullptr; float transform[3][4] = {}; UINT id = 0; UINT mask = 0xFF; };
class Query : public DeviceChild { public: D3D11_QUERY type = D3D11_QUERY_EVENT; UINT slot = 0; uint64_t fenceAt = 0; bool ended = false; };

// ---- the device: makes things (any thread)
class Device {
public:
    ULONG AddRef() { return 1; } ULONG Release() { return 1; }
    HRESULT CreateBuffer(const D3D11_BUFFER_DESC* d, const D3D11_SUBRESOURCE_DATA* init, Buffer** out);
    HRESULT CreateTexture2D(const D3D11_TEXTURE2D_DESC* d, const D3D11_SUBRESOURCE_DATA* init, Texture2D** out);
    HRESULT CreateTexture3D(const D3D11_TEXTURE3D_DESC* d, const D3D11_SUBRESOURCE_DATA* init, Texture3D** out);
    HRESULT CreateShaderResourceView(Resource* r, const D3D11_SHADER_RESOURCE_VIEW_DESC* d, ShaderResourceView** out);
    HRESULT CreateRenderTargetView(Resource* r, const D3D11_RENDER_TARGET_VIEW_DESC* d, RenderTargetView** out);
    HRESULT CreateDepthStencilView(Resource* r, const D3D11_DEPTH_STENCIL_VIEW_DESC* d, DepthStencilView** out);
    HRESULT CreateUnorderedAccessView(Resource* r, const D3D11_UNORDERED_ACCESS_VIEW_DESC* d, UnorderedAccessView** out);
    HRESULT CreateVertexShader(const void* code, SIZE_T n, void* linkage, VertexShader** out);
    HRESULT CreatePixelShader(const void* code, SIZE_T n, void* linkage, PixelShader** out);
    HRESULT CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC* e, UINT n, const void* vsCode, SIZE_T vsSize, InputLayout** out);
    HRESULT CreateBlendState(const D3D11_BLEND_DESC* d, BlendState** out);
    HRESULT CreateRasterizerState(const D3D11_RASTERIZER_DESC* d, RasterizerState** out);
    HRESULT CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC* d, DepthStencilState** out);
    HRESULT CreateSamplerState(const D3D11_SAMPLER_DESC* d, SamplerState** out);
    HRESULT CreateQuery(const D3D11_QUERY_DESC* d, Query** out);
    HRESULT CreateComputeShader(const void* code, SIZE_T n, void* linkage, ComputeShader** out);   // (DXIL: ray tracing shaders)
    bool inlineRaytracing();   // DXR 1.1 (ray queries in any shader) and shader model 6.5: what the RTX passes need
    // D3D12 itself (ray tracing ...)
    ID3D12Device* d3d();
    ID3D12Device5* d3d5();   // null: no DXR on this device
    const DXGI_ADAPTER_DESC1& adapter();
    bool raytracing();       // DXR tier 1.0 or better
};

// ---- the context: draws (one thread: the main one)
class DeviceContext {
public:
    ULONG AddRef() { return 1; } ULONG Release() { return 1; }
    void VSSetShader(VertexShader* s, void* const* = nullptr, UINT = 0);
    void PSSetShader(PixelShader* s, void* const* = nullptr, UINT = 0);
    void VSSetShaderResources(UINT start, UINT n, ShaderResourceView* const* v);
    void PSSetShaderResources(UINT start, UINT n, ShaderResourceView* const* v);
    void VSSetConstantBuffers(UINT start, UINT n, Buffer* const* b);
    void PSSetConstantBuffers(UINT start, UINT n, Buffer* const* b);
    void VSSetSamplers(UINT start, UINT n, SamplerState* const* s);
    void PSSetSamplers(UINT start, UINT n, SamplerState* const* s);
    void IASetInputLayout(InputLayout* l);
    void IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY t);
    void IASetVertexBuffers(UINT start, UINT n, Buffer* const* b, const UINT* strides, const UINT* offsets);
    void IASetIndexBuffer(Buffer* b, DXGI_FORMAT f, UINT offset);
    void OMSetRenderTargets(UINT n, RenderTargetView* const* rt, DepthStencilView* ds);
    void OMSetRenderTargetsAndUnorderedAccessViews(UINT n, RenderTargetView* const* rt, DepthStencilView* ds, UINT uavStart, UINT nu, UnorderedAccessView* const* u, const UINT* counts);
    void OMGetRenderTargets(UINT n, RenderTargetView** rt, DepthStencilView** ds);   // (counted: Release them, as with D3D11)
    void OMSetBlendState(BlendState* b, const FLOAT factor[4], UINT mask);
    void OMSetDepthStencilState(DepthStencilState* d, UINT ref);
    void RSSetState(RasterizerState* r);
    void RSGetState(RasterizerState** r);
    void RSSetViewports(UINT n, const D3D11_VIEWPORT* v);
    void RSGetViewports(UINT* n, D3D11_VIEWPORT* v);
    void RSSetScissorRects(UINT n, const D3D11_RECT* r);
    void Draw(UINT count, UINT start);
    void DrawIndexed(UINT count, UINT start, INT base);
    void DrawIndexedInstanced(UINT count, UINT instances, UINT start, INT base, UINT startInstance);
    void DrawInstanced(UINT count, UINT instances, UINT start, UINT startInstance);
    void ClearRenderTargetView(RenderTargetView* v, const FLOAT c[4]);
    void ClearDepthStencilView(DepthStencilView* v, UINT flags, FLOAT depth, UINT8 stencil);
    void ClearUnorderedAccessViewFloat(UnorderedAccessView* v, const FLOAT c[4]);
    void UpdateSubresource(Resource* r, UINT sub, const D3D11_BOX* box, const void* data, UINT rowPitch, UINT depthPitch);
    HRESULT Map(Resource* r, UINT sub, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE* m);
    void Unmap(Resource* r, UINT sub);
    void CopyResource(Resource* dst, Resource* src);
    void CopySubresourceRegion(Resource* dst, UINT dstSub, UINT x, UINT y, UINT z, Resource* src, UINT srcSub, const D3D11_BOX* box);
    void GenerateMips(ShaderResourceView* v);
    void Begin(Query* q);
    void End(Query* q);
    HRESULT GetData(Query* q, void* data, UINT size, UINT flags);
    // ray tracing: build a mesh's acceleration structure (recorded now, usable this frame), the frame's scene, then a compute pass
    // that traces into it - it reads everything bound to the pixel shader (its textures, constants, samplers) plus, in space1:
    // t0 the scene, t1 per instance where its triangles are (8 uints: per triangle list vertex buffer table index, byte offset,
    // index buffer table index, byte offset), u0 the output; space2: t0.. the buffers those point into (ByteAddressBuffer[256])
    HRESULT BuildBlas(const RtGeometry* g, UINT n, AccelStruct** out);
    void BuildTlas(const RtInstance* inst, UINT n);
    void DispatchRays(ComputeShader* cs, UINT x, UINT y, UINT z, UnorderedAccessView* out, ShaderBase* dimsLike);   // dimsLike: whose texture kinds the empty slots take
    // the engine's own: everything bound, kept and put back (the ImGui renderer)
    struct Saved; Saved* SaveState(); void RestoreState(Saved* s);
    // D3D12 itself: the command list being recorded, and the state a resource must be in (barriers done before the next call)
    ID3D12GraphicsCommandList* d3d();
    ID3D12GraphicsCommandList4* d3d4();
    void Need(Resource* r, D3D12_RESOURCE_STATES s);
    void FlushBarriers();
    void Invalidate();   // after D3D12 work of your own: everything is set again before the next draw
};

class SwapChain {
public:
    ULONG AddRef() { return 1; } ULONG Release() { return 1; }
    HRESULT GetBuffer(UINT i, Texture2D** out);
    HRESULT Present(UINT sync, UINT flags);
    HRESULT ResizeBuffers(UINT n, UINT w, UINT h, DXGI_FORMAT f, UINT flags);
};

// one device for the window: the fastest graphics card (or, without one, WARP on the processor)
struct Setup { HWND hwnd = nullptr; UINT buffers = 3; DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM; bool tearing = false; bool debug = false; bool warp = false; std::string logPath; };
HRESULT Create(const Setup& s, Device** dev, DeviceContext** ctx, SwapChain** swap, std::string* driverName);
void Shutdown();
void Log(const std::string& line);   // into the GPU log (next to the program)

} // namespace d12
