// main.cpp : PRIMEngine, Windows + Direct3D 11.
// Title screen (worlds) -> creative mode in an unlimited block world. The world, mesher and player live in
// src/world and src/game (no Windows code there); this file is the window, the renderer and the menus.
#include "../world/mesher.h"
#include "../game/game.h"
#include "../world/library.h"
#include "../world/shapes.h"
#include "../game/demo.h"
#include "../game/project.h"
#include "../game/brush.h"
#include "../game/objects.h"
#include "../game/weather.h"
#include "../core/png.h"
#include "../core/noise.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#include "../../third_party/imgui/imgui.h"
#include "../../third_party/imgui/imgui_internal.h"   // dock layout builder, side bars
#include "../../third_party/imgui/imgui_stdlib.h"
#include "../../third_party/imgui/backends/imgui_impl_win32.h"
#include "../../third_party/imgui/backends/imgui_impl_dx11.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <functional>
#include <cctype>
#include <cstdarg>
#include <cfloat>

using namespace DirectX;
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
using namespace mc;

static std::string g_shotLog;   // screenshot mode: errors go to this file instead of a message box
static void Log(const std::string& line) { if (g_shotLog.empty()) return; std::ofstream f(g_shotLog, std::ios::app); f << line << "\n"; }
[[noreturn]] static void Fatal(const std::string& what, HRESULT hr = S_OK) {
    char b[160]; snprintf(b, sizeof b, "\n(HRESULT 0x%08X)", (unsigned)hr);
    if (!g_shotLog.empty()) { Log("FATAL: " + what + (hr ? b : "")); ExitProcess(3); }
    MessageBoxA(nullptr, (what + (hr ? b : "")).c_str(), "PRIMEngine", MB_ICONERROR | MB_OK); ExitProcess(1);
}
#define HR(x) do { HRESULT _hr = (x); if (FAILED(_hr)) Fatal("Direct3D call failed: " #x, _hr); } while (0)

static std::string ExeDir() { char p[MAX_PATH]; GetModuleFileNameA(nullptr, p, MAX_PATH); std::string s(p); return s.substr(0, s.find_last_of("\\/") + 1); }
static std::string ReadShader(const char* file) {
    for (const char* rel : { "shaders\\", "..\\shaders\\", "..\\..\\shaders\\" }) {
        std::ifstream f(ExeDir() + rel + file, std::ios::binary); if (f) { std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
    }
    Fatal(std::string("Could not find shaders\\") + file + " next to the program.");
}
static ComPtr<ID3DBlob> Compile(const std::string& src, const char* entry, const char* target) {
    ComPtr<ID3DBlob> code, err;
    HRESULT hr = D3DCompile(src.data(), src.size(), "block.hlsl", nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
    if (FAILED(hr)) Fatal(err ? (const char*)err->GetBufferPointer() : "Shader compile failed", hr);
    return code;
}

// must match cbuffer Frame in block.hlsl
struct alignas(16) FrameCB {
    XMFLOAT4X4 viewProj, shadowVP;
    float sunDir[4], sunColor[4], ambient[4], fogRange[4], skyTop[4], skyHorizon[4], sunGlow[4], camF[4], camR[4], camU[4], proj[4], shadowInfo[4];
    float camPos[4], volume[4], depth[4], extra[4];
    XMFLOAT4X4 skyVP; float weather[4], weather2[4];
    float env[4], envPart[4], envPartColor[4], refl[4], clipPlane[4], mirror[4], ao[4];
};
struct alignas(16) ChunkCB { float offset[4]; float ax[4] = { 1, 0, 0, 0 }, ay[4] = { 0, 1, 0, 0 }, az[4] = { 0, 0, 1, 0 }; };   // axes: objects are turned and sized

struct GpuChunk { ComPtr<ID3D11Buffer> vbO, ibO, vbC, ibC, vbW, ibW; UINT nO = 0, nC = 0, nW = 0; };
struct ModelGpu { std::vector<std::pair<I3, GpuChunk>> parts; std::vector<Box3> boxes; };   // a block model's meshes (16^3 pieces) and collision

struct App;
static App* g_app = nullptr;

struct App {
    // ---- window + device
    HWND hwnd = nullptr; int cw = 1280, ch = 720; bool resized = false;
    int rw = 0, rh = 0; float vx = 0, vy = 0;            // size of the 3D picture, and where it sits in the window (the editor's viewport)
    ComPtr<ID3D11Texture2D> sceneTex; ComPtr<ID3D11RenderTargetView> sceneRTV; ComPtr<ID3D11ShaderResourceView> sceneSRV;   // the viewport picture
    ImVec2 viewSize{ 0, 0 };
    ComPtr<ID3D11Device> dev; ComPtr<ID3D11DeviceContext> ctx; ComPtr<IDXGISwapChain> swap; ComPtr<ID3D11RenderTargetView> backRTV;
    ComPtr<ID3D11Texture2D> depthTex; ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11VertexShader> vsBlock, vsSky, vsShadow; ComPtr<ID3D11PixelShader> psOpaque, psCutout, psSky, psGhost, psShadowCut, psPost, psVolume, psBloomDown, psBloomUp, psRain, psBolt;
    ComPtr<ID3D11VertexShader> vsSkyMap, vsRain, vsDust;
    ComPtr<ID3D11PixelShader> psReflect, psAO, psAOApply;
    ComPtr<ID3D11Texture2D> indTex, aoTex; ComPtr<ID3D11RenderTargetView> indRTV, aoRTV; ComPtr<ID3D11ShaderResourceView> indSRV, aoSRV;   // ambient occlusion
    ComPtr<ID3D11Texture2D> mirTex, mirDepth; ComPtr<ID3D11RenderTargetView> mirRTV; ComPtr<ID3D11ShaderResourceView> mirSRV; ComPtr<ID3D11DepthStencilView> mirDSV;   // a mirror's view (half size)
    ComPtr<ID3D11RasterizerState> rsCullFront; MirrorPlane lastMirror;
    ComPtr<ID3D11Texture2D> surfTex, reflTex; ComPtr<ID3D11RenderTargetView> surfRTV, reflRTV; ComPtr<ID3D11ShaderResourceView> surfSRV, reflSRV;   // for reflections
    ComPtr<ID3D11Texture2D> skyMapTex; ComPtr<ID3D11DepthStencilView> skyMapDSV; ComPtr<ID3D11ShaderResourceView> skyMapSRV;   // what is above each spot
    Weather weather; bool weatherWin = false;
    EnvState envState; bool envWin = false;
    // the weather as this environment allows it (no weather in space, snow instead of rain where it is cold)
    WeatherLook EffWeather() const {
        WeatherLook w = weather.now; const EnvLook& E = envState.now; float a = E.atmosphere;
        w.cover *= E.clouds * a; w.dim *= a; w.dark *= a; w.strikesPerMin *= a;
        if (E.cold > 0.5f) { w.snow = std::max(w.snow, w.rain); w.rain = 0; }
        w.rain *= a; w.snow *= a; w.wind *= a;
        return w;
    }
    ComPtr<ID3D11Buffer> vbBolt, ibBolt; UINT nBolt = 0; ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Texture2D> hdrTex; ComPtr<ID3D11RenderTargetView> hdrRTV; ComPtr<ID3D11ShaderResourceView> hdrSRV;      // the picture before tonemapping
    ComPtr<ID3D11Texture2D> volTex; ComPtr<ID3D11RenderTargetView> volRTV; ComPtr<ID3D11ShaderResourceView> volSRV;      // ... with clouds and fog
    ComPtr<ID3D11ShaderResourceView> depthSRV, noiseSRV, depthCopySRV; ComPtr<ID3D11Texture2D> depthCopy; ComPtr<ID3D11PixelShader> psWater; ComPtr<ID3D11SamplerState> sampLinear, sampClamp; double cloudTime = 0;
    struct BloomLevel { ComPtr<ID3D11Texture2D> tex; ComPtr<ID3D11RenderTargetView> rtv; ComPtr<ID3D11ShaderResourceView> srv; int w = 0, h = 0; };
    std::vector<BloomLevel> bloomLv; ComPtr<ID3D11BlendState> bsAdd; float nightAmount = 0;
    ComPtr<ID3D11Texture2D> shadowTex; ComPtr<ID3D11DepthStencilView> shadowDSV; ComPtr<ID3D11ShaderResourceView> shadowSRV; int shadowSize = 0;
    ComPtr<ID3D11RasterizerState> rsShadow, rsNoCull; ComPtr<ID3D11SamplerState> sampShadow; std::string driverName = "graphics card";
    ComPtr<ID3D11Buffer> cbFrame, cbChunk;
    ComPtr<ID3D11RasterizerState> rsCull; ComPtr<ID3D11DepthStencilState> dsOn, dsOff, dsTestOnly; ComPtr<ID3D11BlendState> bsOff, bsAlpha;
    ComPtr<ID3D11Buffer> vbGhost, ibGhost; UINT nGhost = 0; BlockId ghostKey = 0xFFFFFFFFu;   // the see-through preview block
    ComPtr<ID3D11SamplerState> sampBlocks, sampPoint;
    ComPtr<ID3D11ShaderResourceView> texArray; std::vector<ComPtr<ID3D11ShaderResourceView>> icons;
    std::vector<ComPtr<ID3D11ShaderResourceView>> retired;   // replaced textures the UI of this frame may still draw   // icons: one small texture per block texture (menus)

    // ---- engine
    BlockRegistry reg; Settings settings; std::string settingsPath;
    enum class Screen { Title, Game } screen = Screen::Title;
    std::unique_ptr<World> world; std::unique_ptr<MeshWorkers> workers;
    WorldInfo info; std::string worldDir; Player player; EditHistory history;
    std::unordered_map<I3, GpuChunk, I3Hash> gpu; std::unordered_map<I3, uint32_t, I3Hash> latestSeq; uint32_t seqCounter = 0;
    BlockId hotbar[9] = {}; int hotSel = 0;
    Library library; double libPoll = 0;

    // ---- importing and editing blocks
    std::vector<std::string> importFiles; int importAs = 1;
    bool libraryOpen = false;                     // the blocks & textures window, opened from the title screen
    bool editorOpen = false; BlockDef draft; BlockId draftId = AIR; int faceMode = 0, pickSlot = -1;
    std::string deleteTexAsk; BlockId deleteBlockAsk = AIR; const char* wantPopup = nullptr;

    // ---- shapes
    bool shapeMenu = false; std::string shapeSearch; int shapeCat = 0;
    bool manualRot = false; int manRot = 0;          // R / T turn the next block by hand; G goes back to automatic
    bool targetValid = false; int tx = 0, ty = 0, tz = 0; BlockId targetValue = AIR;   // where the next block goes

    // ---- input + UI state
    bool captured = false, paused = false, inventory = false, showF3 = false, hideHud = false, showSettings = false;
    double mouseDX = 0, mouseDY = 0, lastSpace = -10, breakCool = 0, placeCool = 0, autosave = 0;
    bool lPrev = true, rPrev = true, mPrev = true;
    bool hoverValid = false; Hit hover{};
    std::string toast; double toastT = 0;
    double fps = 0; int fpsN = 0; double fpsAcc = 0;

    // ---- the editor (G1): docked panels around the viewport, and Play mode
    enum class Sel { None, World, Weather, Environment, Zone, Spawn, Player, Game, Block, Texture, Shape, Tool, Object, Selection, Model };
    // ---- objects (G2)
    ModelLibrary models; std::map<std::string, ModelGpu> modelGpu;
    std::vector<WorldObject> objects; uint32_t nextObjId = 1;
    std::string placing, selModel, newModelName; double placeYaw = 0, placeScale = 1; int movingObj = -1, dupFrom = -1; bool replaceWithObject = true;
    I3 selA{ 0, 0, 0 }, selB{ 0, 0, 0 }; int selStage = 0; BlockModel clipboard; bool hasClip = false;   // selection box (tool 2)
    // ---- tools (T): a second hotbar with editing tools; slot 1 is the terrain brush
    bool toolMode = false, brushMenu = false; int toolSel = 0;
    BrushSettings brush; BlockId brushMat = 0;    // 0: the block in the block hotbar
    BrushStroke stroke; bool stroking = false; BrushOp strokeOp = BrushOp::Raise; int strokeY = 0; double dabCool = 0; uint32_t strokeSeed = 1;
    bool toolValid = false; Hit toolHit{};
    Sel sel = Sel::World; int selIndex = 0;
    int assetKind = 0; std::string assetCat, assetSearch; float assetSize = 64;   // asset browser: 0 blocks, 1 textures, 2 shapes
    bool resetLayout = false, showZones = true, captureNext = false, focusView = false, focusAssets = false;
    bool plainView = false;                       // screenshots: just the 3D picture, no editor around it
    GameSettings game; std::string gamePath;
    bool playMode = false; PlayState play; Player editCam; V3 playSpawn{}; double playYaw = 0;
    bool EditorView() const { return screen == Screen::Game && !playMode && !plainView; }
    std::string newName = "New World"; int newType = 0; std::string deleteAsk, renameFrom;
    std::string projectDir; std::vector<ProjectEntry> projects; int projSel = 0; std::string newProjName; int newWorldType = 0;
    std::function<void()> pending;                // a big change (open a project or world) done at the start of the next frame, not in the middle of drawing
    std::vector<std::string> worldList;

    // ================================================================== setup
    void Init() {
        DXGI_SWAP_CHAIN_DESC sd{}; sd.BufferCount = 2; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0, got;
        if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &sd, &swap, &dev, &got, &ctx))) {
            driverName = "software (WARP)";   // no usable graphics card (e.g. the build machine): Windows draws it on the processor
            HR(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &sd, &swap, &dev, &got, &ctx));
        }
        MakeBackbuffer(); rw = cw; rh = ch; MakeTargets();

        std::string src = ReadShader("block.hlsl");
        auto vb = Compile(src, "VS_Block", "vs_5_0"); HR(dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &vsBlock));
        auto b = Compile(src, "PS_Opaque", "ps_5_0"); HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psOpaque));
        b = Compile(src, "PS_Cutout", "ps_5_0"); HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psCutout));
        b = Compile(src, "PS_Ghost", "ps_5_0"); HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psGhost));
        b = Compile(src, "VS_Shadow", "vs_5_0"); HR(dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vsShadow));
        b = Compile(src, "PS_ShadowCutout", "ps_5_0"); HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psShadowCut));
        b = Compile(src, "PS_Volume", "ps_5_0"); HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psVolume));
        b = Compile(src, "PS_BloomDown", "ps_5_0"); HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psBloomDown));
        b = Compile(src, "PS_BloomUp", "ps_5_0"); HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psBloomUp));
        b = Compile(src, "VS_SkyMap", "vs_5_0"); HR(dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vsSkyMap));
        b = Compile(src, "VS_Rain", "vs_5_0"); HR(dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vsRain));
        b = Compile(src, "VS_Dust", "vs_5_0"); HR(dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vsDust));
        b = Compile(src, "PS_Rain", "ps_5_0"); HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psRain));
        b = Compile(src, "PS_Bolt", "ps_5_0"); HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psBolt));
        {   // "what is above": a depth picture of the world seen from straight above (512 x 512 over 128 x 128 blocks)
            D3D11_TEXTURE2D_DESC d{}; d.Width = d.Height = 512; d.MipLevels = 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_R32_TYPELESS; d.SampleDesc.Count = 1; d.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
            HR(dev->CreateTexture2D(&d, nullptr, &skyMapTex));
            D3D11_DEPTH_STENCIL_VIEW_DESC dv{}; dv.Format = DXGI_FORMAT_D32_FLOAT; dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D; HR(dev->CreateDepthStencilView(skyMapTex.Get(), &dv, &skyMapDSV));
            D3D11_SHADER_RESOURCE_VIEW_DESC sv{}; sv.Format = DXGI_FORMAT_R32_FLOAT; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1; HR(dev->CreateShaderResourceView(skyMapTex.Get(), &sv, &skyMapSRV));
        }
        b = Compile(src, "PS_Reflect", "ps_5_0"); HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psReflect));
        b = Compile(src, "PS_AO", "ps_5_0"); HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psAO));
        b = Compile(src, "PS_AOApply", "ps_5_0"); HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psAOApply));
        b = Compile(src, "PS_Water", "ps_5_0"); HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psWater));
        b = Compile(src, "PS_Post", "ps_5_0"); HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psPost));
        b = Compile(src, "VS_Sky", "vs_5_0"); HR(dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vsSky));
        b = Compile(src, "PS_Sky", "ps_5_0"); HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psSky));
        D3D11_INPUT_ELEMENT_DESC el[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 }, { "TEXCOORD", 2, DXGI_FORMAT_R32_UINT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 3, DXGI_FORMAT_R32_UINT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
        HR(dev->CreateInputLayout(el, 5, vb->GetBufferPointer(), vb->GetBufferSize(), &layout));
        auto cb = [&](UINT size, ComPtr<ID3D11Buffer>& out) { D3D11_BUFFER_DESC d{}; d.ByteWidth = size; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; HR(dev->CreateBuffer(&d, nullptr, &out)); };
        cb(sizeof(FrameCB), cbFrame); cb(sizeof(ChunkCB), cbChunk);

        D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_BACK; rd.FrontCounterClockwise = FALSE; rd.DepthClipEnable = TRUE; HR(dev->CreateRasterizerState(&rd, &rsCull));
        rd.CullMode = D3D11_CULL_FRONT; HR(dev->CreateRasterizerState(&rd, &rsCullFront));   // mirrored views turn every triangle around
        rd.CullMode = D3D11_CULL_NONE; HR(dev->CreateRasterizerState(&rd, &rsNoCull));   // full-screen passes (sky, final picture)
        rd.SlopeScaledDepthBias = 1.5f; rd.DepthClipEnable = FALSE; HR(dev->CreateRasterizerState(&rd, &rsShadow));
        D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL; HR(dev->CreateDepthStencilState(&dd, &dsOn));
        dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; HR(dev->CreateDepthStencilState(&dd, &dsTestOnly));
        dd.DepthEnable = FALSE; HR(dev->CreateDepthStencilState(&dd, &dsOff));
        D3D11_BLEND_DESC bd{}; bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; HR(dev->CreateBlendState(&bd, &bsOff));
        auto& rt = bd.RenderTarget[0]; rt.BlendEnable = TRUE; rt.SrcBlend = D3D11_BLEND_SRC_ALPHA; rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOp = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D11_BLEND_ONE; rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOpAlpha = D3D11_BLEND_OP_ADD; HR(dev->CreateBlendState(&bd, &bsAlpha));
        rt.SrcBlend = rt.DestBlend = rt.SrcBlendAlpha = rt.DestBlendAlpha = D3D11_BLEND_ONE; HR(dev->CreateBlendState(&bd, &bsAdd));
        D3D11_SAMPLER_DESC sm{}; sm.Filter = D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR; sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; sm.MaxLOD = D3D11_FLOAT32_MAX; HR(dev->CreateSamplerState(&sm, &sampBlocks));
        sm.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; HR(dev->CreateSamplerState(&sm, &sampPoint));
        sm.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; HR(dev->CreateSamplerState(&sm, &sampLinear));
        sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; HR(dev->CreateSamplerState(&sm, &sampClamp));
        {   // the cloud noise volume
            const int N = 64; std::vector<uint8_t> nz = makeCloudNoise(N);
            D3D11_TEXTURE3D_DESC td{}; td.Width = td.Height = td.Depth = N; td.MipLevels = 1; td.Format = DXGI_FORMAT_R8G8_UNORM; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA sd3{ nz.data(), N * 2, N * N * 2 }; ComPtr<ID3D11Texture3D> t3; HR(dev->CreateTexture3D(&td, &sd3, &t3)); HR(dev->CreateShaderResourceView(t3.Get(), nullptr, &noiseSRV));
        }
        D3D11_SAMPLER_DESC ss{}; ss.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT; ss.AddressU = ss.AddressV = ss.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
        ss.BorderColor[0] = ss.BorderColor[1] = ss.BorderColor[2] = ss.BorderColor[3] = 1; ss.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL; ss.MaxLOD = D3D11_FLOAT32_MAX; HR(dev->CreateSamplerState(&ss, &sampShadow));

        Shapes::get();   // prepares every shape in every rotation (a few milliseconds)
        UploadTextures();   // only the "missing" texture until a project is opened
        DragAcceptFiles(hwnd, TRUE);

        IMGUI_CHECKVERSION(); ImGui::CreateContext();
        static std::string ini = ExeDir() + "imgui.ini"; ImGui::GetIO().IniFilename = ini.c_str();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        Style();
        ImGui_ImplWin32_Init(hwnd); ImGui_ImplDX11_Init(dev.Get(), ctx.Get());

        settingsPath = ExeDir() + "settings.txt"; settings.load(settingsPath);
        Projects::migrateOld(ExeDir(), ProjectsRoot());   // the game\ and worlds\ folders from before projects become "My First Game"
        RAWINPUTDEVICE rid{ 0x01, 0x02, 0, hwnd }; RegisterRawInputDevices(&rid, 1, sizeof rid);   // raw mouse movement for looking around
        RefreshProjects();
    }
    void MakeBackbuffer() { ComPtr<ID3D11Texture2D> bb; HR(swap->GetBuffer(0, IID_PPV_ARGS(&bb))); backRTV.Reset(); HR(dev->CreateRenderTargetView(bb.Get(), nullptr, &backRTV)); }
    // everything the 3D picture needs, at its size (rw x rh): the whole window when playing, the viewport panel in the editor
    void MakeTargets() {
        rw = std::max(16, rw); rh = std::max(16, rh);
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
        if (sceneSRV) retired.push_back(sceneSRV);
        D3D11_TEXTURE2D_DESC d{}; d.Width = (UINT)rw; d.Height = (UINT)rh; d.MipLevels = 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_R32_TYPELESS; d.SampleDesc.Count = 1; d.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        depthTex.Reset(); dsv.Reset(); depthSRV.Reset(); HR(dev->CreateTexture2D(&d, nullptr, &depthTex));
        { D3D11_DEPTH_STENCIL_VIEW_DESC dv{}; dv.Format = DXGI_FORMAT_D32_FLOAT; dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D; HR(dev->CreateDepthStencilView(depthTex.Get(), &dv, &dsv));
          D3D11_SHADER_RESOURCE_VIEW_DESC sv{}; sv.Format = DXGI_FORMAT_R32_FLOAT; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1; HR(dev->CreateShaderResourceView(depthTex.Get(), &sv, &depthSRV));
          depthCopy.Reset(); depthCopySRV.Reset(); D3D11_TEXTURE2D_DESC cd = d; cd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
          HR(dev->CreateTexture2D(&cd, nullptr, &depthCopy)); HR(dev->CreateShaderResourceView(depthCopy.Get(), &sv, &depthCopySRV)); }
        d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        hdrTex.Reset(); hdrRTV.Reset(); hdrSRV.Reset();
        HR(dev->CreateTexture2D(&d, nullptr, &hdrTex)); HR(dev->CreateRenderTargetView(hdrTex.Get(), nullptr, &hdrRTV)); HR(dev->CreateShaderResourceView(hdrTex.Get(), nullptr, &hdrSRV));
        { D3D11_TEXTURE2D_DESC id = d; id.Format = DXGI_FORMAT_R11G11B10_FLOAT; indTex.Reset(); indRTV.Reset(); indSRV.Reset();
          HR(dev->CreateTexture2D(&id, nullptr, &indTex)); HR(dev->CreateRenderTargetView(indTex.Get(), nullptr, &indRTV)); HR(dev->CreateShaderResourceView(indTex.Get(), nullptr, &indSRV));
          id.Width = std::max(1u, (UINT)rw / 2); id.Height = std::max(1u, (UINT)rh / 2); id.Format = DXGI_FORMAT_R8_UNORM; aoTex.Reset(); aoRTV.Reset(); aoSRV.Reset();
          HR(dev->CreateTexture2D(&id, nullptr, &aoTex)); HR(dev->CreateRenderTargetView(aoTex.Get(), nullptr, &aoRTV)); HR(dev->CreateShaderResourceView(aoTex.Get(), nullptr, &aoSRV)); }
        reflTex.Reset(); reflRTV.Reset(); reflSRV.Reset();
        HR(dev->CreateTexture2D(&d, nullptr, &reflTex)); HR(dev->CreateRenderTargetView(reflTex.Get(), nullptr, &reflRTV)); HR(dev->CreateShaderResourceView(reflTex.Get(), nullptr, &reflSRV));
        { D3D11_TEXTURE2D_DESC sd2 = d; sd2.Format = DXGI_FORMAT_R8G8B8A8_UNORM; surfTex.Reset(); surfRTV.Reset(); surfSRV.Reset();
          HR(dev->CreateTexture2D(&sd2, nullptr, &surfTex)); HR(dev->CreateRenderTargetView(surfTex.Get(), nullptr, &surfRTV)); HR(dev->CreateShaderResourceView(surfTex.Get(), nullptr, &surfSRV)); }
        {   // the mirror view: half size, own depth
            D3D11_TEXTURE2D_DESC md = d; md.Width = std::max<UINT>(1, d.Width / 2); md.Height = std::max<UINT>(1, d.Height / 2);
            mirTex.Reset(); mirRTV.Reset(); mirSRV.Reset(); mirDepth.Reset(); mirDSV.Reset();
            HR(dev->CreateTexture2D(&md, nullptr, &mirTex)); HR(dev->CreateRenderTargetView(mirTex.Get(), nullptr, &mirRTV)); HR(dev->CreateShaderResourceView(mirTex.Get(), nullptr, &mirSRV));
            md.Format = DXGI_FORMAT_D32_FLOAT; md.BindFlags = D3D11_BIND_DEPTH_STENCIL; HR(dev->CreateTexture2D(&md, nullptr, &mirDepth)); HR(dev->CreateDepthStencilView(mirDepth.Get(), nullptr, &mirDSV));
        }
        volTex.Reset(); volRTV.Reset(); volSRV.Reset();
        HR(dev->CreateTexture2D(&d, nullptr, &volTex)); HR(dev->CreateRenderTargetView(volTex.Get(), nullptr, &volRTV)); HR(dev->CreateShaderResourceView(volTex.Get(), nullptr, &volSRV));
        { D3D11_TEXTURE2D_DESC sd3 = d; sd3.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sceneTex.Reset(); sceneRTV.Reset(); sceneSRV.Reset();
          HR(dev->CreateTexture2D(&sd3, nullptr, &sceneTex)); HR(dev->CreateRenderTargetView(sceneTex.Get(), nullptr, &sceneRTV)); HR(dev->CreateShaderResourceView(sceneTex.Get(), nullptr, &sceneSRV)); }
        bloomLv.clear();   // half size, quarter size, ... six levels
        for (int i = 0, w = rw / 2, h = rh / 2; i < 6; i++, w = std::max(1, w / 2), h = std::max(1, h / 2)) {
            BloomLevel L; L.w = std::max(1, w); L.h = std::max(1, h);
            D3D11_TEXTURE2D_DESC bd2{}; bd2.Width = (UINT)L.w; bd2.Height = (UINT)L.h; bd2.MipLevels = 1; bd2.ArraySize = 1; bd2.Format = DXGI_FORMAT_R11G11B10_FLOAT; bd2.SampleDesc.Count = 1; bd2.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            HR(dev->CreateTexture2D(&bd2, nullptr, &L.tex)); HR(dev->CreateRenderTargetView(L.tex.Get(), nullptr, &L.rtv)); HR(dev->CreateShaderResourceView(L.tex.Get(), nullptr, &L.srv));
            bloomLv.push_back(L);
        }
    }
    void MakeShadowMap(int size) {
        shadowTex.Reset(); shadowDSV.Reset(); shadowSRV.Reset(); shadowSize = size; if (!size) return;
        D3D11_TEXTURE2D_DESC d{}; d.Width = d.Height = (UINT)size; d.MipLevels = 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_R32_TYPELESS; d.SampleDesc.Count = 1; d.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        HR(dev->CreateTexture2D(&d, nullptr, &shadowTex));
        D3D11_DEPTH_STENCIL_VIEW_DESC dv{}; dv.Format = DXGI_FORMAT_D32_FLOAT; dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D; HR(dev->CreateDepthStencilView(shadowTex.Get(), &dv, &shadowDSV));
        D3D11_SHADER_RESOURCE_VIEW_DESC sv{}; sv.Format = DXGI_FORMAT_R32_FLOAT; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1; HR(dev->CreateShaderResourceView(shadowTex.Get(), &sv, &shadowSRV));
    }
    void OnResize() {
        resized = false; ctx->OMSetRenderTargets(0, nullptr, nullptr); backRTV.Reset();
        HR(swap->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0));
        RECT r; GetClientRect(hwnd, &r); cw = std::max<int>(2, r.right); ch = std::max<int>(2, r.bottom);
        MakeBackbuffer();
    }
    void UploadTextures() {
        const int S = reg.layerSize(), n = (int)reg.textureCount(); int mips = 1; while ((S >> mips) > 0) mips++;
        std::vector<std::vector<uint8_t>> px(n); for (int i = 0; i < n; i++) px[i] = reg.layer(i, S);
        // sRGB textures: the shader gets real light values. Smaller copies (mips) are averaged in linear light.
        D3D11_TEXTURE2D_DESC d{}; d.Width = d.Height = (UINT)S; d.MipLevels = (UINT)mips; d.ArraySize = (UINT)n; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; d.SampleDesc.Count = 1;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> t; HR(dev->CreateTexture2D(&d, nullptr, &t));
        static float toLin[256]; if (toLin[255] == 0) for (int k = 0; k < 256; k++) toLin[k] = std::pow(k / 255.f, 2.2f);
        for (int i = 0; i < n; i++) {
            std::vector<uint8_t> cur = px[i]; int sz = S;
            for (int m = 0; m < mips; m++) {
                ctx->UpdateSubresource(t.Get(), D3D11CalcSubresource(m, i, mips), nullptr, cur.data(), sz * 4, 0);
                if (sz == 1) break;
                int h = sz / 2; std::vector<uint8_t> nx((size_t)h * h * 4);
                for (int y = 0; y < h; y++) for (int x = 0; x < h; x++) for (int c = 0; c < 4; c++) {
                    float acc = 0; for (int k = 0; k < 4; k++) { uint8_t v = cur[((size_t)(y * 2 + (k >> 1)) * sz + x * 2 + (k & 1)) * 4 + c]; acc += c < 3 ? toLin[v] : v / 255.f; }
                    acc /= 4; nx[((size_t)y * h + x) * 4 + c] = (uint8_t)std::lround((c < 3 ? std::pow(acc, 1 / 2.2f) : acc) * 255);
                }
                cur.swap(nx); sz = h;
            }
        }
        if (texArray) retired.push_back(texArray);
        for (auto& i : icons) retired.push_back(i);
        texArray.Reset(); HR(dev->CreateShaderResourceView(t.Get(), nullptr, &texArray));
        icons.clear();
        for (int i = 0; i < n; i++) {
            D3D11_TEXTURE2D_DESC id{}; id.Width = id.Height = (UINT)S; id.MipLevels = 1; id.ArraySize = 1; id.Format = DXGI_FORMAT_R8G8B8A8_UNORM; id.SampleDesc.Count = 1; id.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA sdt{ px[i].data(), (UINT)S * 4, 0 }; ComPtr<ID3D11Texture2D> it; HR(dev->CreateTexture2D(&id, &sdt, &it));
            ComPtr<ID3D11ShaderResourceView> srv; HR(dev->CreateShaderResourceView(it.Get(), nullptr, &srv)); icons.push_back(srv);
        }
    }
    static int Threads() { return (int)std::max(2u, std::thread::hardware_concurrency()) - 1; }
    // Every change to blocks or textures goes through here: the mesh threads read the registry, so they pause,
    // then the textures are re-uploaded and every loaded chunk is rebuilt with the new look.
    template <class F> void ChangeBlocks(F fn) {
        if (workers) workers->stop();
        fn(); reg.resolve(); UploadTextures(); ghostKey = 0xFFFFFFFFu;
        if (world) world->recountEmitters();   // a block may glow (or stop glowing) now
        if (workers) workers->start(Threads());
        if (world) { latestSeq.clear(); for (auto& kv : world->chunks) world->dirty.insert(kv.first); }
        for (auto& h : hotbar) if (h != AIR && !reg.has(h)) h = AIR;
    }
    void PollTextures(double dt) {   // repainted a PNG in your paint program? it shows up within a second
        if ((libPoll += dt) < 1.0) return;
        libPoll = 0;
        if (!library.texturesChanged()) return;
        int n = 0; ChangeBlocks([&] { n = library.reloadTextures(reg); });
        if (n) Toast(n == 1 ? "A texture was updated" : std::to_string(n) + " textures were updated");
    }
    std::vector<std::string> PickImages() {   // Windows "open file" window, several files allowed
        std::vector<char> buf(1 << 16, 0);
        OPENFILENAMEA o{}; o.lStructSize = sizeof o; o.hwndOwner = hwnd; o.lpstrFilter = "PNG images (*.png)\0*.png\0"; o.lpstrFile = buf.data(); o.nMaxFile = (DWORD)buf.size();
        o.lpstrTitle = "Import images as textures"; o.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        std::vector<std::string> out; if (!GetOpenFileNameA(&o)) return out;
        std::string first = buf.data(); const char* p = buf.data() + first.size() + 1;
        if (!*p) { out.push_back(first); return out; }   // one file: the full path
        for (; *p; p += strlen(p) + 1) out.push_back(first + "\\" + p);   // several: folder, then names
        return out;
    }
    void OnDrop(HDROP d) {
        UINT n = DragQueryFileA(d, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n; i++) { char path[MAX_PATH]; if (DragQueryFileA(d, i, path, MAX_PATH)) importFiles.push_back(path); }
        DragFinish(d);
        if (importFiles.empty()) return;
        wantPopup = "Import images"; editorOpen = false;
        if (screen == Screen::Game) { paused = false; inventory = true; Capture(false); } else libraryOpen = true;
        SetForegroundWindow(hwnd);
    }
    void Style() {
        ImGui::StyleColorsDark(); ImGuiStyle& s = ImGui::GetStyle();
        s.WindowRounding = 4; s.FrameRounding = 3; s.GrabRounding = 3; s.WindowPadding = ImVec2(14, 12); s.FramePadding = ImVec2(10, 6); s.ItemSpacing = ImVec2(8, 7);
        ImVec4* c = s.Colors;
        c[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.09f, 0.94f); c[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.09f, 0.10f, 0.97f);
        c[ImGuiCol_Button] = ImVec4(0.30f, 0.30f, 0.32f, 1); c[ImGuiCol_ButtonHovered] = ImVec4(0.38f, 0.45f, 0.62f, 1); c[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.40f, 0.60f, 1);
        c[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.18f, 1); c[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.24f, 0.30f, 1);
        c[ImGuiCol_Header] = ImVec4(0.26f, 0.32f, 0.45f, 1); c[ImGuiCol_HeaderHovered] = ImVec4(0.32f, 0.40f, 0.58f, 1);
        c[ImGuiCol_SliderGrab] = ImVec4(0.55f, 0.65f, 0.85f, 1); c[ImGuiCol_CheckMark] = ImVec4(0.6f, 0.8f, 0.4f, 1);
    }

    // ================================================================== worlds
    std::string WorldsDir() { std::string d = projectDir + "/worlds"; std::error_code ec; fs::create_directories(d, ec); return d; }
    void RefreshWorlds() {
        worldList.clear(); std::error_code ec;
        for (auto& e : fs::directory_iterator(WorldsDir(), ec)) if (e.is_directory() && fs::exists(e.path() / "world.txt")) worldList.push_back(e.path().filename().string());
        std::sort(worldList.begin(), worldList.end());
    }
    void Toast(const std::string& s) { toast = s; toastT = 3.5; }
    void CreateWorld(std::string name, int type) {
        std::string folder; for (char c : name) folder += (isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_') ? c : '_';
        if (folder.empty()) folder = "World";
        std::string base = folder; int n = 2; while (fs::exists(WorldsDir() + "/" + folder)) folder = base + " (" + std::to_string(n++) + ")";
        WorldInfo wi; wi.name = name;
        switch (type) {
        case 1: wi.layers = { { "stone", 6 } }; break;
        case 2: wi.layers = { { "stone", 3 }, { "sand", 3 } }; break;
        case 3: wi.genKind = Generator::Void; wi.layers.clear(); break;
        default: break;
        }
        wi.player.feet = { 0.5, 0, 0.5 }; wi.player.flying = true;
        const char* defaults[9] = { "grass", "dirt", "stone", "cobblestone", "planks", "log", "glass", "bricks", "lamp" };
        for (int i = 0; i < 9; i++) wi.hotbar[i] = defaults[i];
        std::string dir = WorldsDir() + "/" + folder; wi.save(dir);
        OpenWorld(folder);
        if (type == 3) { world->set(0, -1, 0, reg.find("stone")); }   // something to stand on in an empty world
    }
    void OpenWorld(const std::string& folder) {
        if (world) CloseWorld();
        worldDir = WorldsDir() + "/" + folder;
        game.lastWorld = folder; game.save(gamePath); RefreshWorlds();
        info = WorldInfo{}; info.load(worldDir);
        world = std::make_unique<World>(reg); info.applyGenerator(world->gen, reg);
        world->store = std::make_unique<RegionStore>(worldDir + "/region");
        workers = std::make_unique<MeshWorkers>(reg); workers->start(Threads());
        player = info.player; history.clear(); gpu.clear(); latestSeq.clear();
        LoadWorldObjects(); selStage = 0;
        for (int i = 0; i < 9; i++) {   // "stone" or "stone@stairs"
            std::string e = info.hotbar[i], shape; size_t at = e.rfind('@'); if (at != std::string::npos) { shape = e.substr(at + 1); e = e.substr(0, at); }
            BlockId t = reg.find(e); hotbar[i] = t ? makeBlock(t, Shapes::get().find(shape)) : AIR;
        }
        hotSel = info.hotSel;
        envState.update(0, player.eye(), info.environment, info.zones, true);
        { int wi = findWeather(info.weather); weather = Weather{}; weather.set(wi < 0 ? 1 : wi, true); weather.autoChange = info.autoWeather;
          weather.wetness = weather.now.rain > 0.1f ? 1.f : 0.f; weather.snowCover = weather.now.snow > 0.1f ? 1.f : 0.f; weather.seed((uint32_t)std::time(nullptr)); }
        screen = Screen::Game; paused = false; inventory = false; autosave = 0; playMode = false; sel = Sel::World;
        Toast("Opened " + info.name + ". Click the viewport to fly and build, Esc frees the mouse, F5 plays");
    }
    void SaveWorld() {
        if (!world) return;
        info.player = playMode ? editCam : player; info.hotSel = hotSel; info.weather = weather.type().key; info.autoWeather = weather.autoChange; for (int i = 0; i < 9; i++) info.hotbar[i] = !reg.has(hotbar[i]) ? "" : reg.get(hotbar[i]).name + (shapeOf(hotbar[i]) ? "@" + Shapes::get().info(shapeOf(hotbar[i])).key : "");
        info.save(worldDir); world->saveAll();
        saveObjects(worldDir + "/objects.txt", objects);
    }
    void CloseWorld() {   // the project stays open
        if (playMode) StopPlay();
        SaveWorld(); workers.reset(); world.reset(); gpu.clear(); latestSeq.clear(); Capture(false);
    }
    std::string CurrentWorld() const { return world ? fs::path(worldDir).filename().string() : ""; }
    void RenameWorld(const std::string& from, const std::string& to) {
        bool wasOpen = from == CurrentWorld(); if (wasOpen) CloseWorld();
        std::string folder = Projects::uniqueFolder(WorldsDir(), to); std::error_code ec;
        fs::rename(WorldsDir() + "/" + from, WorldsDir() + "/" + folder, ec);
        if (ec) folder = from;
        else { WorldInfo wi; wi.load(WorldsDir() + "/" + folder); wi.name = to; wi.save(WorldsDir() + "/" + folder);
               if (game.startWorld == from) game.startWorld = folder; if (game.lastWorld == from) game.lastWorld = folder; game.save(gamePath); }
        RefreshWorlds();
        if (wasOpen) OpenWorld(folder);
        Toast(ec ? "Could not rename the world (is a file in it open?)" : "Renamed to " + to);
    }
    void DeleteWorld(const std::string& folder) {
        if (folder == CurrentWorld()) return;   // never the open one
        std::error_code ec; fs::remove_all(WorldsDir() + "/" + folder, ec);
        if (game.startWorld == folder) { game.startWorld.clear(); game.save(gamePath); }
        RefreshWorlds(); Toast("Deleted the world " + folder);
    }

    // ================================================================== input
    void Capture(bool on) {
        if (captured == on) return;
        captured = on; ShowCursor(on ? FALSE : TRUE);
        ImGuiIO& io = ImGui::GetIO();
        if (on) {
            io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
            RECT r{ (LONG)vx, (LONG)vy, (LONG)(vx + rw), (LONG)(vy + rh) };   // the mouse stays inside the 3D picture
            POINT a{ 0, 0 }; ClientToScreen(hwnd, &a); RECT cr{ a.x + r.left, a.y + r.top, a.x + r.right, a.y + r.bottom }; ClipCursor(&cr);
            POINT c{ (r.left + r.right) / 2, (r.top + r.bottom) / 2 }; ClientToScreen(hwnd, &c); SetCursorPos(c.x, c.y);
            focusView = true;
            lPrev = rPrev = mPrev = true;   // the click that captured the mouse must not also break a block
        } else { io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse; ClipCursor(nullptr); }
        mouseDX = mouseDY = 0;
    }
    bool Playing() const { return screen == Screen::Game && captured && !paused && !inventory && !shapeMenu && !brushMenu; }
    static bool Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
    void OnKey(WPARAM k, bool repeat) {
        if (screen != Screen::Game) {
            if (k == VK_ESCAPE) { if (editorOpen) editorOpen = false; else if (showSettings) showSettings = false; else libraryOpen = false; }
            return;
        }
        bool typing = ImGui::GetIO().WantTextInput;
        if (k == VK_F5 && !repeat) { if (playMode) StopPlay(); else StartPlay(); return; }
        if (k == VK_ESCAPE) {
            if (shapeMenu) { shapeMenu = false; Capture(true); }
            else if (brushMenu) { brushMenu = false; Capture(true); }
            else if (inventory) { inventory = false; Capture(true); }
            else if (showSettings) showSettings = false;
            else if (playMode) { paused = !paused; Capture(!paused); }
            else if (captured) Capture(false);            // editor: the mouse goes back to the panels
            else if (editorOpen && !EditorView()) editorOpen = false;
            return;
        }
        if (k == VK_F3) { showF3 = !showF3; return; }
        if (k == VK_F1) { hideHud = !hideHud; return; }
        if (typing || playMode) return;                   // playing: no building tools
        if (Down(VK_CONTROL) && k == 'Z') { if (history.undo(*world)) Toast("Undo"); return; }
        if (Down(VK_CONTROL) && k == 'Y') { if (history.redo(*world)) Toast("Redo"); return; }
        if (Down(VK_CONTROL) && k == 'S') { SaveWorld(); Toast("Saved"); return; }
        if (Down(VK_CONTROL) && k == 'C' && toolMode && toolSel == 1) { SelCopy(); return; }
        if (Down(VK_CONTROL) && k == 'V' && toolMode && toolSel == 1) { SelPaste(); return; }
        if (Down(VK_CONTROL) && k == 'D' && sel == Sel::Object) { DuplicateObject(selIndex); return; }
        if (k == 'E' && !paused) { if (EditorView()) { Capture(false); focusAssets = true; } else { inventory = !inventory; editorOpen = false; shapeMenu = false; Capture(!inventory); } return; }
        if (k == 'T' && !repeat && !paused && !inventory) {   // block hotbar <-> tool hotbar
            toolMode = !toolMode; shapeMenu = brushMenu = false; EndStroke();
            if (toolMode && EditorView()) sel = Sel::Tool; else if (sel == Sel::Tool) sel = Sel::World;
            Toast(toolMode ? "Tools (T: back to blocks, Q: brush options)" : "Blocks"); return;
        }
        if (k == 'Q' && !paused && !inventory) {
            if (toolMode) { brushMenu = !brushMenu; shapeMenu = false; Capture(!brushMenu); }
            else { shapeMenu = !shapeMenu; shapeSearch.clear(); Capture(!shapeMenu); }
            return;
        }
        if (toolMode && (k == VK_OEM_PLUS || k == VK_ADD)) { brush.radius = std::min(32, brush.radius + 1); return; }
        if (toolMode && (k == VK_OEM_MINUS || k == VK_SUBTRACT)) { brush.radius = std::max(0, brush.radius - 1); return; }
        if (!Playing() || repeat) return;
        if (k >= '1' && k <= '9') { if (toolMode) toolSel = (int)(k - '1'); else hotSel = (int)(k - '1'); }
        if (toolMode && toolSel == 1) {   // selection box
            if (k == VK_DELETE) SelFill(AIR);
            if (k == 'F') SelFill(reg.has(typeOf(hotbar[hotSel])) ? hotbar[hotSel] : reg.find("stone"));
        }
        if (toolMode && toolSel == 2) {   // objects
            if (k == 'R' && !placing.empty()) placeYaw = std::fmod(placeYaw + (Down(VK_SHIFT) ? 15 : 90) + 540, 360) - 180;
            if (sel == Sel::Object && placing.empty()) {
                if (k == 'G') PickUpObject(selIndex);
                if (k == VK_DELETE) DeleteObject(selIndex);
                if (k == 'R') { objects[selIndex].yaw = std::fmod(objects[selIndex].yaw + (Down(VK_SHIFT) ? 15 : 90) + 540, 360) - 180; RebuildSolids(); }
            }
        }
        if (k == VK_SPACE) {
            double now = ImGui::GetTime();
            if (now - lastSpace < 0.3) { player.toggleFly(); Toast(player.flying ? "Flying" : "Walking"); lastSpace = -10; } else lastSpace = now;
        }
        if (k == 'R' && !toolMode) {   // turn the next block by hand (starts from the automatic rotation); Shift+R tips it over
            if (!manualRot) { manualRot = true; manRot = targetValid ? rotOf(targetValue) : 0; }
            int look = LookFace(); bool tip = Down(VK_SHIFT);
            manRot = Shapes::turn(manRot, !tip ? 1 : (look == F_PZ || look == F_NZ) ? 0 : 2);
        }
        if (k == 'G' && manualRot) { manualRot = false; Toast("Rotation: automatic"); }
    }
    void OnWheel(int delta) {
        if (Playing() && !playMode && toolMode) {
            if (Down(VK_SHIFT)) brush.radius = std::clamp(brush.radius + (delta > 0 ? 1 : -1), 0, 32);
            else toolSel = (toolSel + (delta > 0 ? 8 : 1)) % 9;
            return;
        }
        if (Playing() && !playMode) hotSel = (hotSel + (delta > 0 ? 8 : 1)) % 9; }

    // ================================================================== game update
    void UpdateGame(double dt) {
        if (Playing()) {
            double s = 0.0022 * settings.mouseSensitivity; player.look(mouseDX * s, mouseDY * s);
            Controls c; bool fg = GetForegroundWindow() == hwnd;
            if (fg) { c.forward = Down('W'); c.back = Down('S'); c.left = Down('A'); c.right = Down('D'); c.jump = Down(VK_SPACE); c.sneak = Down(VK_SHIFT); c.sprint = Down(VK_CONTROL) && !Down('Z') && !Down('Y'); }
            if (playMode) { c.sprint = c.sprint && game.player.canSprint; if (play.dead()) c = Controls{}; }
            player.update(*world, c, std::min(dt, 0.05), playMode ? game.player : PlayerRules{});
            if (playMode) hoverValid = false; else if (toolMode) ToolInteract(dt); else Interact(dt);
        } else { lPrev = rPrev = mPrev = true; if (stroking) EndStroke(); }
        toolValid = toolMode && !playMode && world->raycast(player.eye(), player.forward(), 160.0, toolHit);   // where the brush is (shown even with the mouse free)
        if (playMode && !paused) {   // health, lava, falling out of the world, respawning
            PlayState::Event e = play.tick(*world, player, game.player, playSpawn, playYaw, std::min(dt, 0.05));
            if (e == PlayState::Died) Toast(std::string("You ") + play.cause);
        }
        mouseDX = mouseDY = 0;
        weather.update(dt, player.feet);
        world->liquidTimer += dt;   // water and lava flow 10 steps a second
        for (int k = 0; k < 3 && world->liquidTimer >= 0.1; k++) { world->liquidTimer -= 0.1; world->tickLiquids(); }
        if (world->liquidTimer > 0.3) world->liquidTimer = 0;
        envState.update(dt, player.eye(), info.environment, info.zones);
        cloudTime += dt * (0.5 + weather.now.wind);
        if (settings.dayCycle) { settings.timeOfDay += (float)(dt * 24.0 / 1200.0); if (settings.timeOfDay >= 24) settings.timeOfDay -= 24; }
        // load chunks around the player and keep their meshes fresh
        I3 pc = chunkOf(ifloor(player.feet.x), ifloor(player.feet.y), ifloor(player.feet.z));
        world->stream(pc, settings.renderDistance, settings.verticalDistance, 24);
        ScheduleMeshing(pc);
        CollectMeshes();
        if ((autosave += dt) > 60) { autosave = 0; SaveWorld(); }
    }
    // ---- panel layout helpers: everything fits the width of whatever panel it is in
    // a property row: the label on the left (cut short with "..." when narrow, full text on hover), the field fills the rest
    void Prop(const char* label) {
        float ui = settings.uiScale, avail = ImGui::GetContentRegionAvail().x, sp = ImGui::GetStyle().ItemSpacing.x;
        float lw = std::clamp(avail * 0.38f, 56 * ui, 150 * ui);
        ImVec2 p = ImGui::GetCursorScreenPos(); float fh = ImGui::GetFrameHeight();
        ImGui::Dummy(ImVec2(lw - sp, fh));
        float tw = ImGui::CalcTextSize(label).x; bool cut = tw > lw - sp;
        ImVec2 tp(p.x, p.y + ImGui::GetStyle().FramePadding.y);
        ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), tp, ImVec2(p.x + lw - sp, p.y + fh), p.x + lw - sp, label, nullptr, nullptr);
        if (cut && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", label);
        ImGui::SameLine(); ImGui::SetNextItemWidth(-FLT_MIN);
    }
    // grey text that wraps at the panel's edge
    void Hint(const char* fmt, ...) {
        char buf[1024]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)); ImGui::TextWrapped("%s", buf); ImGui::PopStyleColor();
    }
    // put the next item on this line only if it still fits (buttons wrap to the next line in narrow panels)
    void Flow(float nextWidth) {
        float right = ImGui::GetCurrentWindow()->WorkRect.Max.x;
        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + nextWidth <= right) ImGui::SameLine();
    }
    float ButtonW(const char* label, bool compact = false) { return ImGui::CalcTextSize(label, nullptr, true).x + (compact ? 0 : 2) * ImGui::GetStyle().FramePadding.x + (compact ? 2 * ImGui::GetStyle().FramePadding.x : 0); }
    bool FlowButton(const char* label, bool compact = false) { Flow(ButtonW(label, compact)); return compact ? ImGui::SmallButton(label) : ImGui::Button(label); }
    float Cols(int n) { float sp = ImGui::GetStyle().ItemSpacing.x; return std::floor((ImGui::GetContentRegionAvail().x - sp * (n - 1)) / n); }   // width of n buttons side by side

    // ================================================================== objects (G2)
    const BlockModel* ModelOf(const WorldObject& o) const { return models.find(o.model); }
    ModelGpu& GpuModel(const std::string& name) {   // the model's meshes, made the first time they are needed
        auto it = modelGpu.find(name); if (it != modelGpu.end()) return it->second;
        ModelGpu& g = modelGpu[name];
        const BlockModel* m = models.find(name); if (!m) return g;
        g.boxes = modelBoxes(*m, reg);
        for (int cy = 0; cy * CS < m->sy; cy++) for (int cz = 0; cz * CS < m->sz; cz++) for (int cx = 0; cx * CS < m->sx; cx++) {
            MeshInput in; in.pos = { cx, cy, cz }; in.blocks.assign(PAD * PAD * PAD, AIR);
            for (int y = -1; y <= CS; y++) for (int z = -1; z <= CS; z++) for (int x = -1; x <= CS; x++) {
                BlockId v = m->at(cx * CS + x, cy * CS + y, cz * CS + z); if (v != AIR && !reg.has(v)) v = AIR;
                in.blocks[padIdx(x, y, z)] = v;
            }
            ChunkMesh cm; buildMesh(reg, in, cm);
            GpuChunk gc; Upload(cm.opaque, gc.vbO, gc.ibO, gc.nO); Upload(cm.cutout, gc.vbC, gc.ibC, gc.nC); Upload(cm.water, gc.vbW, gc.ibW, gc.nW);
            if (gc.nO || gc.nC || gc.nW) g.parts.push_back({ { cx, cy, cz }, std::move(gc) });
        }
        return g;
    }
    void ModelChanged(const std::string& name) { modelGpu.erase(name); RebuildSolids(); }
    void RebuildSolids() {   // the collision boxes the player bumps into
        if (!world) return;
        world->objectSolids.clear();
        for (size_t i = 0; i < objects.size(); i++) {
            if ((int)i == movingObj) continue;
            const BlockModel* m = ModelOf(objects[i]); if (!m) continue;
            auto b = objectBoxes(objects[i], *m, GpuModel(objects[i].model).boxes);
            world->objectSolids.insert(world->objectSolids.end(), b.begin(), b.end());
        }
    }
    // draws one object (pass 0 solid, 1 cut-out, 2 water) with the current shaders
    void DrawObject(const WorldObject& o, int pass) {
        const BlockModel* m = ModelOf(o); if (!m) return;
        ModelGpu& g = GpuModel(o.model);
        double a = o.yaw * 3.14159265358979 / 180, c = std::cos(a), s = std::sin(a), k = o.scale;
        V3 ax{ c * k, 0, -s * k }, ay{ 0, k, 0 }, az{ s * k, 0, c * k };
        for (auto& part : g.parts) {
            const GpuChunk& gc = part.second; UINT n = pass == 0 ? gc.nO : pass == 1 ? gc.nC : gc.nW; if (!n) continue;
            V3 l{ part.first.x * (double)CS - m->pivot.x, part.first.y * (double)CS - m->pivot.y, part.first.z * (double)CS - m->pivot.z };
            V3 org = o.pos - camPos + ax * l.x + ay * l.y + az * l.z;
            ChunkCB cc{ { (float)org.x, (float)org.y, (float)org.z, 0 }, { (float)ax.x, (float)ax.y, (float)ax.z, 0 }, { (float)ay.x, (float)ay.y, (float)ay.z, 0 }, { (float)az.x, (float)az.y, (float)az.z, 0 } };
            ctx->UpdateSubresource(cbChunk.Get(), 0, nullptr, &cc, 0, 0);
            UINT stride = sizeof(Vtx), off = 0;
            ctx->IASetVertexBuffers(0, 1, pass == 0 ? gc.vbO.GetAddressOf() : pass == 1 ? gc.vbC.GetAddressOf() : gc.vbW.GetAddressOf(), &stride, &off);
            ctx->IASetIndexBuffer(pass == 0 ? gc.ibO.Get() : pass == 1 ? gc.ibC.Get() : gc.ibW.Get(), DXGI_FORMAT_R32_UINT, 0);
            ctx->DrawIndexed(n, 0, 0);
        }
    }
    void LoadWorldObjects() {
        objects.clear(); loadObjects(worldDir + "/objects.txt", objects);
        nextObjId = 1; for (auto& o : objects) nextObjId = std::max(nextObjId, o.id + 1);
        movingObj = -1; placing.clear(); RebuildSolids();
    }
    std::string UniqueObjectName(const std::string& base) const {
        for (int k = 1;; k++) { std::string n = base + " " + std::to_string(k); bool used = false; for (auto& o : objects) used |= o.name == n; if (!used) return n; }
    }
    // where a model would be placed: on the face you look at, lined up with the blocks (Alt: exactly where you look)
    bool PlacePose(WorldObject& o) {
        const BlockModel* m = models.find(placing); if (!m || !toolValid) return false;
        o.model = placing; o.yaw = placeYaw;
        if (Down(VK_MENU)) { o.pos = { toolHit.px, toolHit.py, toolHit.pz }; return true; }
        I3 a{ toolHit.x + toolHit.nx, toolHit.y + toolHit.ny, toolHit.z + toolHit.nz };
        bool side = std::fmod(std::fabs(placeYaw) + 0.001, 180.0) > 45 && std::fmod(std::fabs(placeYaw) + 0.001, 180.0) < 135;
        auto half = [&](double w) { double ws = w * o.scale; long r = std::lround(ws); return std::fabs(ws - r) < 1e-6 && (r % 2) == 0 ? 0.0 : 0.5; };
        o.pos = { a.x + half(side ? m->sz : m->sx), (double)a.y, a.z + half(side ? m->sx : m->sz) };
        return true;
    }
    void StartPlacing(const std::string& model) {
        placing = model; placeYaw = 0; placeScale = 1; movingObj = -1; dupFrom = -1; toolMode = true; toolSel = 2; sel = Sel::Tool;
        Toast("Placing " + model + ": click to put it down, R turns it, right click stops");
    }
    void PickUpObject(int i) {   // move: the object follows the crosshair until you click again
        if (i < 0 || i >= (int)objects.size()) return;
        placing = objects[i].model; placeYaw = objects[i].yaw; placeScale = objects[i].scale; movingObj = i; toolMode = true; toolSel = 2;
        RebuildSolids(); Toast("Moving " + objects[i].name + ": click to put it down, right click puts it back");
    }
    void DeleteObject(int i) {
        if (i < 0 || i >= (int)objects.size()) return;
        Toast("Deleted " + objects[i].name); objects.erase(objects.begin() + i);
        if (sel == Sel::Object) sel = Sel::None; movingObj = -1; RebuildSolids();
    }
    void DuplicateObject(int i) {
        if (i < 0 || i >= (int)objects.size()) return;
        placing = objects[i].model; placeYaw = objects[i].yaw; placeScale = objects[i].scale; movingObj = -1; dupFrom = i; toolMode = true; toolSel = 2;
        Toast("Placing a copy: click to put it down");
    }
    int ObjectUnderCrosshair(double maxT) const {   // the nearest object the view ray hits (if nearer than the blocks)
        int best = -1; double bt = maxT;
        for (size_t i = 0; i < objects.size(); i++) {
            const BlockModel* m = ModelOf(objects[i]); if (!m || (int)i == movingObj) continue;
            double t; if (rayBox(player.eye(), player.forward(), objectBounds(objects[i], *m), t) && t < bt) { bt = t; best = (int)i; }
        }
        return best;
    }
    // ---- selection box
    I3 SelLo() const { return { std::min(selA.x, selB.x), std::min(selA.y, selB.y), std::min(selA.z, selB.z) }; }
    I3 SelHi() const { return { std::max(selA.x, selB.x), std::max(selA.y, selB.y), std::max(selA.z, selB.z) }; }
    void ApplyEdits(std::vector<Edit>&& e, const std::string& what) {
        if (e.empty()) { Toast("Nothing to change"); return; }
        for (auto& x : e) world->set(x.x, x.y, x.z, x.after);
        size_t n = e.size(); history.push(std::move(e)); Toast(what + ": " + std::to_string(n) + " blocks (Ctrl+Z undoes it)");
    }
    void SelFill(BlockId v) { if (selStage == 2) ApplyEdits(fillEdits(*world, SelLo(), SelHi(), v), v ? "Filled" : "Deleted"); }
    void SelCopy() { if (selStage != 2) return; clipboard = modelFromWorld(*world, SelLo(), SelHi(), "copy", false); hasClip = true; Toast("Copied " + std::to_string(clipboard.sx) + " x " + std::to_string(clipboard.sy) + " x " + std::to_string(clipboard.sz)); }
    void SelPaste() {
        if (!hasClip || !toolValid) return;
        I3 at{ toolHit.x + toolHit.nx, toolHit.y + toolHit.ny, toolHit.z + toolHit.nz };
        ApplyEdits(pasteEdits(*world, clipboard, at), "Pasted");
        selA = at; selB = { at.x + clipboard.sx - 1, at.y + clipboard.sy - 1, at.z + clipboard.sz - 1 }; selStage = 2;
    }
    void MakeIntoObject(const std::string& wanted, bool replace) {
        if (selStage != 2) return;
        I3 corner; BlockModel m = modelFromWorld(*world, SelLo(), SelHi(), models.uniqueName(wanted.empty() ? "Model" : wanted), true, &corner);
        if (!m.blockCount()) { Toast("There are no blocks in the box"); return; }
        models.store(m); ModelChanged(m.name);
        WorldObject o; o.id = nextObjId++; o.name = UniqueObjectName(m.name); o.model = m.name;
        o.pos = { corner.x + m.pivot.x, (double)corner.y, corner.z + m.pivot.z };
        if (replace) { ApplyEdits(fillEdits(*world, corner, { corner.x + m.sx - 1, corner.y + m.sy - 1, corner.z + m.sz - 1 }, AIR), "Turned into an object"); objects.push_back(o); }
        else { o.pos.x += m.sx + 1; objects.push_back(o); Toast("Made the model " + m.name); }
        RebuildSolids(); sel = Sel::Object; selIndex = (int)objects.size() - 1; selStage = 0;
    }
    void ObjectToolClick(bool left, bool right) {
        if (!placing.empty()) {
            if (right) {   // stop placing (a moved object goes back where it was)
                if (movingObj >= 0) { int i = movingObj; movingObj = -1; sel = Sel::Object; selIndex = i; }
                placing.clear(); dupFrom = -1; RebuildSolids(); return;
            }
            WorldObject o; if (!left || !PlacePose(o)) return;
            o.scale = placeScale;
            PlacePose(o);
            if (movingObj >= 0) { WorldObject& t = objects[movingObj]; t.pos = o.pos; t.yaw = o.yaw; sel = Sel::Object; selIndex = movingObj; movingObj = -1; placing.clear(); }
            else {
                WorldObject n = dupFrom >= 0 && dupFrom < (int)objects.size() ? objects[dupFrom] : WorldObject{};
                n.id = nextObjId++; n.model = o.model; n.pos = o.pos; n.yaw = o.yaw; n.scale = o.scale;
                n.name = UniqueObjectName(o.model);
                objects.push_back(n); sel = Sel::Object; selIndex = (int)objects.size() - 1;
                if (dupFrom >= 0) { placing.clear(); dupFrom = -1; }
            }
            RebuildSolids(); return;
        }
        if (left) { int i = ObjectUnderCrosshair(toolValid ? toolHit.t + 0.01 : 160); if (i >= 0) { sel = Sel::Object; selIndex = i; } else if (sel == Sel::Object) sel = Sel::Tool; }
    }
    void ObjectIcon(ImDrawList* dl, const BlockModel& m, ImVec2 c, float s) {   // its most used block, as a stand-in picture
        std::map<BlockId, int> n; for (BlockId v : m.cells) if (v != AIR) n[typeOf(v)]++;
        BlockId best = AIR; int bc = 0; for (auto& k : n) if (k.second > bc) { bc = k.second; best = k.first; }
        if (best) { BlockIcon(dl, best, ImVec2(c.x - s * 0.12f, c.y + s * 0.08f), s * 0.62f); BlockIcon(dl, best, ImVec2(c.x + s * 0.16f, c.y - s * 0.14f), s * 0.42f); }
    }
    void DrawBox3(ImDrawList* dl, V3 lo, V3 hi, ImU32 col) {
        V3 q[8]; for (int i = 0; i < 8; i++) q[i] = { (i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z };
        const int e[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
        for (auto& k : e) Line3D(dl, q[k[0]], q[k[1]], col);
    }
    void DrawObjectGuides(ImDrawList* dl) {   // selection box, object outlines (editor only)
        if (playMode) return;
        if (toolMode && toolSel == 1 && (selStage == 1 || selStage == 2)) {
            I3 b = selStage == 1 && toolValid ? I3{ toolHit.x, toolHit.y, toolHit.z } : selB;
            I3 lo{ std::min(selA.x, b.x), std::min(selA.y, b.y), std::min(selA.z, b.z) }, hi{ std::max(selA.x, b.x), std::max(selA.y, b.y), std::max(selA.z, b.z) };
            DrawBox3(dl, { (double)lo.x, (double)lo.y, (double)lo.z }, { hi.x + 1.0, hi.y + 1.0, hi.z + 1.0 }, IM_COL32(90, 220, 255, 255));
            ImVec2 sp; if (Project({ hi.x + 1.0, hi.y + 1.2, hi.z + 1.0 }, sp)) {
                std::string t = std::to_string(hi.x - lo.x + 1) + " x " + std::to_string(hi.y - lo.y + 1) + " x " + std::to_string(hi.z - lo.z + 1);
                dl->AddText(sp, IM_COL32(150, 235, 255, 255), t.c_str());
            }
        }
        if (toolMode && toolSel == 1 && selStage == 0 && toolValid)
            DrawBox3(dl, { (double)toolHit.x, (double)toolHit.y, (double)toolHit.z }, { toolHit.x + 1.0, toolHit.y + 1.0, toolHit.z + 1.0 }, IM_COL32(90, 220, 255, 160));
        if (sel == Sel::Object && selIndex >= 0 && selIndex < (int)objects.size() && selIndex != movingObj)
            if (const BlockModel* m = ModelOf(objects[selIndex])) { Box3 b = objectBounds(objects[selIndex], *m); DrawBox3(dl, b.lo, b.hi, IM_COL32(255, 220, 90, 255)); }
        if (toolMode && toolSel == 2 && placing.empty()) {
            int h = ObjectUnderCrosshair(toolValid ? toolHit.t + 0.01 : 160);
            if (h >= 0 && !(sel == Sel::Object && selIndex == h)) if (const BlockModel* m = ModelOf(objects[h])) { Box3 b = objectBounds(objects[h], *m); DrawBox3(dl, b.lo, b.hi, IM_COL32(255, 255, 255, 140)); }
        }
        if (toolMode && toolSel == 2 && !placing.empty()) {
            WorldObject o; o.scale = placeScale;
            if (PlacePose(o)) if (const BlockModel* m = models.find(placing)) { Box3 b = objectBounds(o, *m); DrawBox3(dl, b.lo, b.hi, IM_COL32(120, 255, 150, 220)); }
        }
    }
    void ObjectInspector() {
        if (selIndex < 0 || selIndex >= (int)objects.size()) { Hint("Pick an object in the Outliner, or click one with the Objects tool."); return; }
        WorldObject& o = objects[selIndex]; const BlockModel* m = ModelOf(o); bool ch_ = false;
        ImGui::SeparatorText("Object");
        Prop("Name"); ImGui::InputText("##name", &o.name);
        Prop("Folder"); ImGui::InputText("##folder", &o.folder);
        Prop("Model");
        if (ImGui::BeginCombo("##model", o.model.c_str())) { for (auto& kv : models.models) if (ImGui::Selectable(kv.first.c_str(), kv.first == o.model)) { o.model = kv.first; ch_ = true; } ImGui::EndCombo(); }
        Prop("Position"); ch_ |= ImGui::InputScalarN("##pos", ImGuiDataType_Double, &o.pos.x, 3, nullptr, nullptr, "%.2f");
        { float y = (float)o.yaw; Prop("Turn"); if (ImGui::SliderFloat("##yaw", &y, -180, 180, "%.0f deg")) { o.yaw = y; ch_ = true; }
          if (ImGui::SmallButton("-90")) { o.yaw = std::fmod(o.yaw - 90 + 540, 360) - 180; ch_ = true; } if (FlowButton("+90", true)) { o.yaw = std::fmod(o.yaw + 90 + 540, 360) - 180; ch_ = true; } }
        { float sc = (float)o.scale; Prop("Size"); if (ImGui::SliderFloat("##scale", &sc, 0.1f, 8, "x %.2f", ImGuiSliderFlags_Logarithmic)) { o.scale = sc; ch_ = true; } }
        if (m) {
            double h = m->sy * o.scale; Prop("Height (blocks)");
            if (ImGui::InputDouble("##height", &h, 0.5, 1, "%.2f") && h > 0.05) { o.scale = h / m->sy; ch_ = true; }
            Hint("%.2f wide, %.2f tall, %.2f deep (blocks). The player is 1.8 tall, the morph ball 1.", m->sx * o.scale, m->sy * o.scale, m->sz * o.scale);
        }
        int cm = (int)o.collide; const char* cn[] = { collideName(Collide::Exact), collideName(Collide::OneBox), collideName(Collide::None) };
        Prop("Collision"); if (ImGui::Combo("##coll", &cm, cn, 3)) { o.collide = (Collide)cm; ch_ = true; }
        if (m) Hint("Made from its blocks by itself: %d boxes.", (int)objectBoxes(o, *m, GpuModel(o.model).boxes).size());
        if (ch_) RebuildSolids();
        ImGui::Spacing();
        if (ImGui::Button("Move (G)")) PickUpObject(selIndex);
        if (FlowButton("Copy (Ctrl+D)")) DuplicateObject(selIndex);
        if (FlowButton("Go there")) { player.feet = { o.pos.x, o.pos.y + (m ? m->sy * o.scale : 1) + 1, o.pos.z - 4 }; player.flying = true; player.yaw = 0; player.pitch = -0.4; }
        if (FlowButton("Delete (Del)")) { DeleteObject(selIndex); return; }
    }
    void SelectionInspector() {
        ImGui::SeparatorText("Selection box");
        if (selStage != 2) { Hint(selStage == 1 ? "Now click the other corner." : "Click one corner of a box of blocks, then the other corner. Right click clears it."); }
        else {
            I3 lo = SelLo(), hi = SelHi();
            Hint("%d x %d x %d blocks, from %d %d %d to %d %d %d", hi.x - lo.x + 1, hi.y - lo.y + 1, hi.z - lo.z + 1, lo.x, lo.y, lo.z, hi.x, hi.y, hi.z);
            float bw = Cols(2);
            if (ImGui::Button("Copy (Ctrl+C)", ImVec2(bw, 0))) SelCopy();
            ImGui::SameLine(); ImGui::BeginDisabled(!hasClip); if (ImGui::Button("Paste (Ctrl+V)", ImVec2(bw, 0))) SelPaste(); ImGui::EndDisabled();
            BlockId held = typeOf(hotbar[hotSel]);
            if (ImGui::Button("Fill (F)", ImVec2(bw, 0))) SelFill(reg.has(held) ? hotbar[hotSel] : reg.find("stone"));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("With the block in your hand: %s", reg.has(held) ? reg.get(held).name.c_str() : "stone");
            ImGui::SameLine(); if (ImGui::Button("Delete (Del)", ImVec2(bw, 0))) SelFill(AIR);
            ImGui::SeparatorText("Make into object");
            Prop("Name"); ImGui::InputTextWithHint("##mname", "Model name", &newModelName);
            Prop("Replace the blocks"); ImGui::Checkbox("##repl", &replaceWithObject);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The blocks become the object, right where they are.\nOff: the blocks stay, the object is put next to them.");
            if (ImGui::Button("Make into object", ImVec2(-FLT_MIN, 0))) MakeIntoObject(newModelName, replaceWithObject);
            Hint("The new model shows up in Assets > Models, ready to place again and again.");
        }
        if (hasClip) Hint("Copied: %d x %d x %d. Ctrl+V pastes it where you look.", clipboard.sx, clipboard.sy, clipboard.sz);
    }
    void ModelInspector() {
        const BlockModel* m = models.find(selModel); if (!m) { Hint("Pick a model in Assets > Models."); return; }
        ImGui::SeparatorText("Model");
        float s = std::min(ImGui::GetContentRegionAvail().x, 140 * settings.uiScale); ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + s, p.y + s), IM_COL32(30, 32, 38, 255), 4.f);
        ObjectIcon(ImGui::GetWindowDrawList(), *m, ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), s * 0.8f); ImGui::Dummy(ImVec2(s, s));
        ImGui::Text("%s", m->name.c_str());
        int used = 0; for (auto& o : objects) used += o.model == m->name;
        Hint("%d x %d x %d blocks, %d blocks in it. Used %d times in this world.", m->sx, m->sy, m->sz, m->blockCount(), used);
        if (ImGui::Button("Place it")) StartPlacing(m->name);
        if (FlowButton("Unpack as blocks")) { if (toolValid) { BlockModel c = *m; I3 at{ toolHit.x + toolHit.nx, toolHit.y + toolHit.ny, toolHit.z + toolHit.nz }; ApplyEdits(pasteEdits(*world, c, at), "Unpacked"); } else Toast("Look at where the blocks should go"); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Puts its blocks where you look, so you can change them\nand make a new object from them.");
        ImGui::BeginDisabled(used > 0);
        if (FlowButton("Delete model")) { std::string n = m->name; models.remove(n); modelGpu.erase(n); selModel.clear(); sel = Sel::None; }
        ImGui::EndDisabled();
        if (used > 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Delete the objects that use it first.");
    }

    // ---- tools
    BlockId BrushMaterial() const { BlockId m = brushMat ? brushMat : typeOf(hotbar[hotSel]); return reg.has(m) ? typeOf(m) : reg.find("stone"); }
    void EndStroke() {
        if (!stroking) return;
        stroking = false; auto g = stroke.finish();
        if (!g.empty()) { size_t n = g.size(); history.push(std::move(g)); Toast(std::to_string(n) + " blocks changed (Ctrl+Z undoes it)"); }
    }
    void ToolInteract(double dt) {
        hoverValid = false;
        bool L = Down(VK_LBUTTON), R = Down(VK_RBUTTON);
        if (!L && !R) { EndStroke(); lPrev = rPrev = false; return; }
        if (toolSel == 1) {   // selection box: first click one corner, then the other; right click clears
            if (L && !lPrev && toolValid) {
                I3 h{ toolHit.x, toolHit.y, toolHit.z };
                if (selStage == 1) { selB = h; selStage = 2; sel = Sel::Selection; } else { selA = selB = h; selStage = 1; sel = Sel::Selection; }
            }
            if (R && !rPrev) { selStage = 0; }
            lPrev = L; rPrev = R; return;
        }
        if (toolSel == 2) { ObjectToolClick(L && !lPrev, R && !rPrev); lPrev = L; rPrev = R; return; }
        if (toolSel != 0) { lPrev = L; rPrev = R; return; }   // more tools come later
        if (!stroking && ((L && !lPrev) || (R && !rPrev)) && toolValid) {
            stroking = true; strokeOp = L ? brush.op : opposite(brush.op); strokeY = toolHit.y; dabCool = 0; strokeSeed = (uint32_t)GetTickCount();
        }
        lPrev = L; rPrev = R;
        if (!stroking || !toolValid) return;
        if ((dabCool -= dt) > 0) return;
        bool slow = strokeOp == BrushOp::Raise || strokeOp == BrushOp::Lower || strokeOp == BrushOp::Smooth || strokeOp == BrushOp::Roughen;
        dabCool = slow ? 0.15 : 0.1;
        BrushSettings b = brush; b.op = strokeOp;
        I3 c{ toolHit.x, toolHit.y, toolHit.z };
        if (strokeOp == BrushOp::Add) c = { toolHit.x + toolHit.nx, toolHit.y + toolHit.ny, toolHit.z + toolHit.nz };   // grows out of the surface
        stroke.apply(*world, brushEdits(*world, b, c, BrushMaterial(), strokeY, strokeSeed));
    }
    std::string ToolName() const { return toolSel == 0 ? BrushLabel() : toolSel == 1 ? "Selection box" : toolSel == 2 ? (placing.empty() ? "Objects" : "Objects: placing " + placing) : "(empty tool slot)"; }
    std::string ToolLabel() const {
        switch (toolSel) {
        case 0: return BrushLabel() + "   (Q: brush options, T: back to blocks)";
        case 1: return selStage == 0 ? "Selection box: click a corner   (T: back to blocks)" : selStage == 1 ? "Selection box: click the other corner (right click: start over)"
                     : "Selection box   Ctrl+C copy, Ctrl+V paste, F fill, Del delete, make into object in the Inspector";
        case 2: return !placing.empty() ? "Click to put down " + placing + "   R turn (Shift+R a little), Alt: off the grid, right click: stop"
                     : "Objects: click one to pick it   G move, R turn, Ctrl+D copy, Del delete.   Assets > Models to place one";
        default: return "(more tools are coming here)   T: back to blocks";
        }
    }
    std::string BrushLabel() const {
        return std::string("Terrain brush: ") + shapeInfo(brush.shape).name + ", " + opInfo(brush.op).name + ", size " + std::to_string(brush.radius);
    }
    void ToolIcon(ImDrawList* dl, int tool, ImVec2 c, float s) {
        if (tool == 1) {   // selection box: a dashed square
            float h = s * 0.42f; ImU32 col = IM_COL32(120, 220, 255, 255);
            for (int i = 0; i < 4; i++) { float t0 = -h + i * h * 0.5f + h * 0.06f, t1 = t0 + h * 0.3f;
                dl->AddLine(ImVec2(c.x + t0, c.y - h), ImVec2(c.x + t1, c.y - h), col, 2); dl->AddLine(ImVec2(c.x + t0, c.y + h), ImVec2(c.x + t1, c.y + h), col, 2);
                dl->AddLine(ImVec2(c.x - h, c.y + t0), ImVec2(c.x - h, c.y + t1), col, 2); dl->AddLine(ImVec2(c.x + h, c.y + t0), ImVec2(c.x + h, c.y + t1), col, 2); }
            return;
        }
        if (tool == 2) {   // objects: a little crate
            float h = s * 0.36f; ImVec2 a(c.x - h, c.y - h * 0.6f), b(c.x + h * 0.6f, c.y + h);
            dl->AddRectFilled(a, b, IM_COL32(170, 120, 70, 255), 2.f); dl->AddRect(a, b, IM_COL32(90, 60, 30, 255), 2.f, 0, 2.f);
            dl->AddLine(a, b, IM_COL32(90, 60, 30, 255), 2); dl->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, a.y), IM_COL32(90, 60, 30, 255), 2);
            dl->AddQuadFilled(a, ImVec2(a.x + h * 0.4f, a.y - h * 0.4f), ImVec2(b.x + h * 0.4f, a.y - h * 0.4f), ImVec2(b.x, a.y), IM_COL32(200, 150, 95, 255));
            dl->AddQuadFilled(ImVec2(b.x, a.y), ImVec2(b.x + h * 0.4f, a.y - h * 0.4f), ImVec2(b.x + h * 0.4f, b.y - h * 0.4f), b, IM_COL32(130, 90, 50, 255));
            return;
        }
        if (tool != 0) return;
        float h = s * 0.5f;   // terrain brush: a little mountain with a snowy top
        dl->AddTriangleFilled(ImVec2(c.x - h, c.y + h * 0.7f), ImVec2(c.x - h * 0.15f, c.y - h * 0.75f), ImVec2(c.x + h * 0.55f, c.y + h * 0.7f), IM_COL32(110, 120, 95, 255));
        dl->AddTriangleFilled(ImVec2(c.x - h * 0.1f, c.y + h * 0.7f), ImVec2(c.x + h * 0.45f, c.y - h * 0.25f), ImVec2(c.x + h, c.y + h * 0.7f), IM_COL32(85, 140, 70, 255));
        dl->AddTriangleFilled(ImVec2(c.x - h * 0.4f, c.y - h * 0.25f), ImVec2(c.x - h * 0.15f, c.y - h * 0.75f), ImVec2(c.x + h * 0.1f, c.y - h * 0.25f), IM_COL32(240, 245, 250, 255));
        dl->AddRectFilled(ImVec2(c.x - h, c.y + h * 0.7f), ImVec2(c.x + h, c.y + h * 0.85f), IM_COL32(120, 85, 55, 255));
    }
    void BrushShapeIcon(ImDrawList* dl, BrushShape sh, ImVec2 c, float s, ImU32 col) {   // the shape's outline, seen from above at an angle
        BrushSettings b; b.shape = sh; b.radius = 3; b.height = 2; float k = s / 9.f;
        auto P = [&](V3 v) { return ImVec2(c.x + (float)(v.x - v.z) * 0.87f * k, c.y + (float)((v.x + v.z) * 0.45 - v.y) * k); };
        for (auto& l : brushOutline(b)) dl->AddLine(P(l.first), P(l.second), col, 1.5f);
    }
    ImU32 OpColor(BrushOp o) const {
        switch (o) {
        case BrushOp::Add: case BrushOp::Raise: case BrushOp::Fill: return IM_COL32(110, 255, 140, 255);
        case BrushOp::Remove: case BrushOp::Lower: return IM_COL32(255, 110, 100, 255);
        case BrushOp::Paint: case BrushOp::Replace: return IM_COL32(120, 190, 255, 255);
        default: return IM_COL32(255, 220, 110, 255);
        }
    }
    void BlockPicker(const char* id, BlockId& v, const char* none) {   // a button showing the block; click for a grid of all blocks
        float ui = settings.uiScale, s = 40 * ui;
        ImGui::PushID(id); ImVec2 p;
        if (Tile("pick", p, s)) ImGui::OpenPopup("grid");
        if (v) BlockIcon(ImGui::GetWindowDrawList(), v, ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), s * 0.7f);
        ImGui::SameLine(); { const char* nm = v && reg.has(v) ? reg.get(v).name.c_str() : none; ImVec2 q = ImGui::GetCursorScreenPos(); float r = ImGui::GetCurrentWindow()->WorkRect.Max.x;
          ImGui::Dummy(ImVec2(std::max(1.f, r - q.x), s)); ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), ImVec2(q.x, q.y + (s - ImGui::GetTextLineHeight()) * 0.5f), ImVec2(r, q.y + s), r, nm, nullptr, nullptr); }
        if (ImGui::BeginPopup("grid")) {
            if (ImGui::Selectable(none, v == 0)) v = 0;
            float t = 44 * ui; int n = 0;
            ImGui::BeginChild("g", ImVec2(GridChildWidth(t, 8), (t + ImGui::GetStyle().ItemSpacing.y) * 4 + 12), ImGuiChildFlags_Borders);
            for (BlockId b = 1; b < reg.count(); b++) {
                if (!reg.has(b)) continue; ImGui::PushID((int)b); ImVec2 q;
                if (Tile("t", q, t, typeOf(v) == b)) { v = b; ImGui::CloseCurrentPopup(); }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", reg.get(b).name.c_str());
                BlockIcon(ImGui::GetWindowDrawList(), b, ImVec2(q.x + t * 0.5f, q.y + t * 0.5f), t * 0.7f);
                ImGui::PopID(); if (++n % 8) ImGui::SameLine();
            }
            ImGui::EndChild(); ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    void BrushBody() {   // the terrain brush's options (Q menu and Inspector)
        float ui = settings.uiScale, s = std::min(58 * ui, Cols(3));
        ImGui::SeparatorText("Shape");
        for (int i = 0; i < (int)BrushShape::COUNT; i++) {
            ImGui::PushID(i); ImGui::BeginGroup(); ImVec2 p;
            if (Tile("sh", p, s, (int)brush.shape == i)) brush.shape = (BrushShape)i;
            BrushShapeIcon(ImGui::GetWindowDrawList(), (BrushShape)i, ImVec2(p.x + s * 0.5f, p.y + s * 0.52f), s, (int)brush.shape == i ? OpColor(brush.op) : IM_COL32(200, 200, 210, 255));
            ImGui::TextUnformatted(shapeInfo((BrushShape)i).name); ImGui::EndGroup();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", shapeInfo((BrushShape)i).help);
            ImGui::PopID();
            if (i % 3 != 2 && i + 1 < (int)BrushShape::COUNT) ImGui::SameLine();
        }
        ImGui::SeparatorText("What it does");
        float bw = Cols(3);
        for (int i = 0; i < (int)BrushOp::COUNT; i++) {
            bool on = (int)brush.op == i;
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(opInfo((BrushOp)i).name, ImVec2(bw, 0))) brush.op = (BrushOp)i;
            if (on) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", opInfo((BrushOp)i).help);
            if (i % 3 != 2 && i + 1 < (int)BrushOp::COUNT) ImGui::SameLine();
        }
        Hint("%s", opInfo(brush.op).help);
        ImGui::SeparatorText("Settings");
        BrushOp o = brush.op;
        Prop("Size"); ImGui::SliderInt("##Size", &brush.radius, 0, 32, brush.radius ? "%d blocks" : "1 block");
        if (brush.shape == BrushShape::Cylinder || brush.shape == BrushShape::Cube) { Prop("Height"); ImGui::SliderInt("##Height", &brush.height, 0, 32, "%d up and down"); }
        if (o == BrushOp::Smooth || o == BrushOp::Raise || o == BrushOp::Lower || o == BrushOp::Roughen) { Prop("Strength"); ImGui::SliderFloat("##Strength", &brush.strength, 0, 1, "%.2f"); }
        if (o == BrushOp::Paint) { Prop("Paint depth"); ImGui::SliderInt("##Paint depth", &brush.paintDepth, 1, 8, "%d blocks"); }
        if (o == BrushOp::Roughen) { Prop("Lump size"); ImGui::SliderFloat("##Lump size", &brush.noiseScale, 0.05f, 1, "%.2f"); }
        if (o == BrushOp::Remove || o == BrushOp::Smooth) { Prop("Water and lava too"); ImGui::Checkbox("##liq", &brush.liquidsToo); }
        if (o == BrushOp::Add || o == BrushOp::Paint || o == BrushOp::Replace || o == BrushOp::Flatten || o == BrushOp::Fill || o == BrushOp::Roughen) {
            Prop("Material"); BlockPicker("mat", brushMat, "The block in my hand");
        }
        if (o == BrushOp::Replace) {
            Prop("Only replace"); BlockPicker("from", brush.replaceFrom, "Anything");
            if (toolValid && FlowButton("The block I am looking at", true)) brush.replaceFrom = typeOf(world->get(toolHit.x, toolHit.y, toolHit.z));
        }
        ImGui::Spacing();
        Hint("Left click: %s. Right click: %s. Hold the button to keep going. Shift + wheel or + / - : size. Ctrl+Z undoes a whole stroke.",
            opInfo(brush.op).name, opInfo(opposite(brush.op)).name);
    }
    void BrushMenu() {
        float W = 440 * settings.uiScale; Center(ImVec2(W, 0)); ImGui::SetNextWindowSizeConstraints(ImVec2(W, 0), ImVec2(W, FLT_MAX));
        ImGui::Begin("Terrain brush###brushmenu", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
        BrushBody();
        if (ImGui::Button("Done (Q)", ImVec2(-1, 0))) { brushMenu = false; Capture(true); }
        ImGui::End();
    }
    void Interact(double dt) {
        hoverValid = world->raycast(player.eye(), player.forward(), 6.0, hover);
        bool L = Down(VK_LBUTTON), R = Down(VK_RBUTTON), M = Down(VK_MBUTTON);
        breakCool -= dt; placeCool -= dt;
        if (L && (!lPrev || breakCool <= 0) && hoverValid) {
            BlockId was = world->get(hover.x, hover.y, hover.z);
            world->set(hover.x, hover.y, hover.z, AIR); history.push({ { hover.x, hover.y, hover.z, was, AIR } }); breakCool = 0.25;
        }
        UpdateTarget();
        if (R && (!rPrev || placeCool <= 0) && targetValid) {
            world->set(tx, ty, tz, targetValue); history.push({ { tx, ty, tz, AIR, targetValue } });
            placeCool = 0.25;
        }
        if (M && !mPrev && hoverValid && reg.has(world->get(hover.x, hover.y, hover.z))) {
            // pick the block and its shape; with Ctrl also its rotation (like "copy shape and rotation")
            BlockId v = world->get(hover.x, hover.y, hover.z), want = makeBlock(typeOf(v), shapeOf(v)); int found = -1;
            for (int i = 0; i < 9; i++) if (hotbar[i] == want) found = i;
            if (found >= 0) hotSel = found; else hotbar[hotSel] = want;
            if (Down(VK_CONTROL)) { manualRot = true; manRot = rotOf(v); Toast("Copied shape and rotation (G: automatic again)"); }
        }
        if (!L) breakCool = 0;
        if (!R) placeCool = 0;
        lPrev = L; rPrev = R; mPrev = M;
    }
    int LookFace() const { V3 f = player.forward(); return std::fabs(f.x) > std::fabs(f.z) ? (f.x > 0 ? F_PX : F_NX) : (f.z > 0 ? F_PZ : F_NZ); }
    // where the held block would go, in which shape and rotation
    void UpdateTarget() {
        targetValid = false;
        BlockId held = hotbar[hotSel];
        if (!hoverValid || !reg.has(held)) return;
        int x = hover.x + hover.nx, y = hover.y + hover.ny, z = hover.z + hover.nz;
        if (!world->loaded(x, y, z)) return;
        { BlockId there = world->get(x, y, z); if (there != AIR && !reg.get(there).liquid) return; }   // you can build into water and lava
        const BlockDef& bd = reg.get(held); int shape = resolveShape(held, bd);
        int n = hover.nx > 0 ? F_PX : hover.nx < 0 ? F_NX : hover.ny > 0 ? F_PY : hover.ny < 0 ? F_NY : hover.nz > 0 ? F_PZ : F_NZ;
        int r = manualRot ? manRot : Shapes::placeRot(Shapes::get().info(shape).place, n, LookFace(), hover.py - hover.y > 0.5);
        BlockId v = makeBlock(typeOf(held), shapeOf(held), r);
        if (bd.solid) {   // never place a block inside yourself
            const Player& p = player;
            for (const SBox& b : Shapes::get().variant(shape, r).boxes)
                if (x + b.hi[0] > p.feet.x - Player::HALF_W && x + b.lo[0] < p.feet.x + Player::HALF_W && z + b.hi[2] > p.feet.z - Player::HALF_W && z + b.lo[2] < p.feet.z + Player::HALF_W && y + b.hi[1] > p.feet.y && y + b.lo[1] < p.feet.y + Player::HEIGHT) return;
        }
        targetValid = true; tx = x; ty = y; tz = z; targetValue = v;
    }
    void ScheduleMeshing(I3 pc) {
        if (world->dirty.empty()) return;
        std::vector<std::pair<int, I3>> todo; todo.reserve(world->dirty.size());
        for (auto& c : world->dirty) { int dx = c.x - pc.x, dy = c.y - pc.y, dz = c.z - pc.z; todo.push_back({ dx * dx + dy * dy + dz * dz, c }); }
        std::sort(todo.begin(), todo.end(), [](auto& a, auto& b) { return a.first < b.first; });
        int budget = 160;
        for (auto& t : todo) {
            if (budget-- <= 0) break;
            world->dirty.erase(t.second);
            MeshInput in; if (!gatherInput(*world, t.second, in)) continue;
            in.seq = ++seqCounter; latestSeq[t.second] = in.seq; workers->submit(std::move(in));
        }
    }
    void Upload(const MeshData& m, ComPtr<ID3D11Buffer>& vb, ComPtr<ID3D11Buffer>& ib, UINT& n) {
        vb.Reset(); ib.Reset(); n = (UINT)m.i.size(); if (!n) return;
        D3D11_BUFFER_DESC d{}; d.Usage = D3D11_USAGE_IMMUTABLE; d.ByteWidth = (UINT)(m.v.size() * sizeof(Vtx)); d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA s{ m.v.data(), 0, 0 }; HR(dev->CreateBuffer(&d, &s, &vb));
        d.ByteWidth = (UINT)(m.i.size() * 4); d.BindFlags = D3D11_BIND_INDEX_BUFFER; s.pSysMem = m.i.data(); HR(dev->CreateBuffer(&d, &s, &ib));
    }
    void CollectMeshes() {
        std::vector<ChunkMesh> done; workers->collect(done);
        for (auto& m : done) {
            auto it = latestSeq.find(m.pos);
            if (it == latestSeq.end() || it->second != m.seq || !world->chunk(m.pos)) continue;   // outdated or unloaded meanwhile
            if (m.opaque.empty() && m.cutout.empty() && m.water.empty()) { gpu.erase(m.pos); continue; }
            GpuChunk& g = gpu[m.pos]; Upload(m.opaque, g.vbO, g.ibO, g.nO); Upload(m.cutout, g.vbC, g.ibC, g.nC); Upload(m.water, g.vbW, g.ibW, g.nW);
        }
        for (auto it = gpu.begin(); it != gpu.end();) { if (!world->chunk(it->first)) { latestSeq.erase(it->first); it = gpu.erase(it); } else ++it; }
    }

    // ================================================================== rendering
    XMMATRIX view, proj; V3 camPos; float aspect = 1;
    static bool BoxVisible(const XMMATRIX& vp, float x0, float y0, float z0, float s) {
        int out[6] = {};
        for (int i = 0; i < 8; i++) {
            XMVECTOR c = XMVector4Transform(XMVectorSet(x0 + ((i & 1) ? s : 0), y0 + ((i & 2) ? s : 0), z0 + ((i & 4) ? s : 0), 1), vp);
            float x = XMVectorGetX(c), y = XMVectorGetY(c), z = XMVectorGetZ(c), w = XMVectorGetW(c);
            out[0] += x < -w; out[1] += x > w; out[2] += y < -w; out[3] += y > w; out[4] += z < 0; out[5] += z > w;
        }
        for (int k = 0; k < 6; k++) if (out[k] == 8) return false;
        return true;
    }
    // sun, sky and light colours for an hour of the day
    float exposure = 1;
    void SkyParams(FrameCB& fc, float hour) {
        auto mix = [](const float a[3], const float b[3], float t, float o[3]) { for (int i = 0; i < 3; i++) o[i] = a[i] + (b[i] - a[i]) * t; };
        auto sstep = [](float a, float b, float x) { float t = std::min(1.f, std::max(0.f, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
        float ang = (hour - 6) / 12 * XM_PI;                       // 0 = sunrise in the east (+X), noon high up, sunset in the west
        V3 sun = normalize(V3{ std::cos(ang), std::sin(ang), 0.35 });
        float h = (float)sun.y, day = sstep(-0.12f, 0.25f, h), low = sstep(-0.2f, 0.0f, h) * (1 - sstep(0.02f, 0.35f, h));
        bool moon = h < -0.04f; V3 L = moon ? sun * -1.0 : sun;
        fc.sunDir[0] = (float)L.x; fc.sunDir[1] = (float)L.y; fc.sunDir[2] = (float)L.z; fc.sunDir[3] = settings.shadowQuality > 0 ? 1.f : 0.f;
        const EnvLook& E = envState.now; const float atm = E.atmosphere;   // colours come from the environment you are in
        const float* sunLow = E.sunLow; const float* sunHigh = E.sunHigh; const float moonC[3] = { 0.10f, 0.13f, 0.22f };
        float sc[3]; mix(sunLow, sunHigh, sstep(0.05f, 0.45f, h), sc);
        float k = moon ? sstep(0.04f, 0.2f, -h) : sstep(-0.04f, 0.08f, h);
        for (int i = 0; i < 3; i++) fc.sunColor[i] = (moon ? moonC[i] : sc[i]) * k;
        const float ambNight[3] = { 0.035f, 0.05f, 0.10f };
        float amb[3]; mix(ambNight, E.ambDay, day, amb); mix(amb, E.ambLow, low * 0.6f, amb);
        for (int i = 0; i < 3; i++) fc.ambient[i] = E.ambDay[i] + (amb[i] - E.ambDay[i]) * atm;   // no air: only a faint, steady light
        const float topNight[3] = { 0.004f, 0.008f, 0.025f }, horNight[3] = { 0.02f, 0.03f, 0.07f };
        mix(topNight, E.dayTop, day, fc.skyTop); float hz[3]; mix(horNight, E.dayHorizon, day, hz); mix(hz, E.lowHorizon, low, fc.skyHorizon);
        const float glowMoon[3] = { 0.06f, 0.08f, 0.12f };
        float gl[3]; mix(E.glowDay, E.glowLow, low, gl); for (int i = 0; i < 3; i++) fc.sunGlow[i] = moon ? glowMoon[i] : gl[i] * k;
        for (int i = 0; i < 3; i++) { fc.skyTop[i] *= atm; fc.skyHorizon[i] *= atm; fc.sunGlow[i] *= atm; }
        if (atm < 0.5f && moon) for (int i = 0; i < 3; i++) fc.sunColor[i] = 0;   // in space there is no moon lighting the ground
        fc.sunGlow[3] = moon ? 2.f * atm : 40.f + 60.f * (1 - atm);
        exposure = 1.0f + 2.5f * (1 - day);                         // the eye adjusts a little at night
        // weather: clouds swallow sunlight, the sky turns grey; lightning flashes light everything up
        const WeatherLook W = EffWeather(); float dim = W.dim;
        auto lum = [](const float c[3]) { return c[0] * 0.2126f + c[1] * 0.7152f + c[2] * 0.0722f; };
        for (int i = 0; i < 3; i++) fc.sunColor[i] *= (1 - dim);
        float ga = lum(fc.ambient), gt = lum(fc.skyTop), gh = lum(fc.skyHorizon);
        for (int i = 0; i < 3; i++) {
            fc.ambient[i] = (fc.ambient[i] + (ga * 0.95f - fc.ambient[i]) * dim * 0.85f) * (1 - dim * 0.55f - W.dark * 0.2f);
            fc.skyTop[i] += (gh * 0.6f - fc.skyTop[i]) * dim; fc.skyHorizon[i] += (gh * 0.75f - fc.skyHorizon[i]) * dim;
            fc.skyTop[i] *= 1 - W.dark * 0.55f; fc.skyHorizon[i] *= 1 - W.dark * 0.5f;   // storms: a dark sky
            fc.sunGlow[i] *= (1 - dim * 0.9f);
        }
        (void)gt;
        const float fl[3] = { 0.55f, 0.6f, 0.85f };
        float flash = weather.flash * atm;
        for (int i = 0; i < 3; i++) { fc.ambient[i] += flash * fl[i] * 1.2f; fc.skyTop[i] += flash * fl[i] * 0.25f; fc.skyHorizon[i] += flash * fl[i] * 0.3f; }
        exposure *= 1 + dim * 0.15f;
        nightAmount = std::max(1 - sstep(-0.25f, 0.0f, h), 1 - atm);   // in space the stars are always out
    }
    // a lightning bolt: a jagged line from the clouds to the ground with a few branches, drawn as glowing ribbons
    void DrawBolt() {
        uint32_t s = weather.boltSeed; auto rnd = [&]() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xFFFFFF) / 16777216.f; };
        V3 origin = weather.boltPos, cam = camPos;
        std::vector<std::pair<V3, V3>> segs; std::vector<float> widths;
        auto bolt = [&](V3 from, V3 to, int n, float jag, float w) {
            V3 prev = from;
            for (int i = 1; i <= n; i++) {
                double t = (double)i / n; V3 p = from + (to - from) * t;
                if (i < n) { p.x += (rnd() - 0.5) * jag; p.z += (rnd() - 0.5) * jag; p.y += (rnd() - 0.5) * jag * 0.3; }
                segs.push_back({ prev, p }); widths.push_back(w); prev = p;
            }
        };
        double top = 185 - origin.y;
        bolt({ (rnd() - 0.5) * 30, top, (rnd() - 0.5) * 30 }, { 0, 0, 0 }, 26, 9, 0.9f);
        size_t mainCount = segs.size();
        for (int b = 0; b < 4; b++) { V3 from = segs[(size_t)(rnd() * (mainCount - 4))].second; V3 to = from + V3{ (rnd() - 0.5) * 70, -(20 + rnd() * 50), (rnd() - 0.5) * 70 }; bolt(from, to, 10, 6, 0.45f); }
        MeshData m;
        for (size_t i = 0; i < segs.size(); i++) {
            V3 a = segs[i].first, c = segs[i].second, dir = normalize(c - a), toCam = normalize((cam - origin) - a), side = normalize(cross(dir, toCam)) * widths[i];
            uint32_t base = (uint32_t)m.v.size();
            V3 q[4] = { a - side, a + side, c + side, c - side };
            for (auto& p : q) m.v.push_back({ (float)p.x, (float)p.y, (float)p.z, 0, 0, 0, 255u, 0u });
            m.i.insert(m.i.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        }
        Upload(m, vbBolt, ibBolt, nBolt);
        double a = weather.boltAge; float bright = (a < 0.07 || (a > 0.14 && a < 0.24)) ? 60.f : 18.f;
        bright *= (float)std::max(0.0, 1 - a / 0.45);
        ChunkCB cc{ { (float)(origin.x - cam.x), (float)(origin.y - cam.y), (float)(origin.z - cam.z), bright } }; ctx->UpdateSubresource(cbChunk.Get(), 0, nullptr, &cc, 0, 0);
        ctx->OMSetBlendState(bsAdd.Get(), nullptr, 0xFFFFFFFF); ctx->OMSetDepthStencilState(dsTestOnly.Get(), 0); ctx->RSSetState(rsNoCull.Get());
        ctx->IASetInputLayout(layout.Get()); ctx->VSSetShader(vsBlock.Get(), nullptr, 0); ctx->PSSetShader(psBolt.Get(), nullptr, 0);
        UINT stride = sizeof(Vtx), off = 0; ctx->IASetVertexBuffers(0, 1, vbBolt.GetAddressOf(), &stride, &off); ctx->IASetIndexBuffer(ibBolt.Get(), DXGI_FORMAT_R32_UINT, 0);
        ctx->DrawIndexed(nBolt, 0, 0);
        ctx->OMSetBlendState(bsOff.Get(), nullptr, 0xFFFFFFFF); ctx->OMSetDepthStencilState(dsOn.Get(), 0); ctx->RSSetState(rsCull.Get());
    }
    void RenderWorld(ID3D11RenderTargetView* out) {
        aspect = (float)rw / rh; camPos = player.eye();
        V3 f = player.forward(), r = normalize(cross({ 0, 1, 0 }, f)), u = cross(f, r);
        float farZ = (settings.renderDistance + 2) * CS * 1.5f;
        view = XMMatrixLookToLH(XMVectorZero(), XMVectorSet((float)f.x, (float)f.y, (float)f.z, 0), XMVectorSet(0, 1, 0, 0));   // camera at the origin
        proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(settings.fov), aspect, 0.05f, farZ);
        XMMATRIX vp = view * proj;
        FrameCB fc{}; XMStoreFloat4x4(&fc.viewProj, vp);
        SkyParams(fc, settings.timeOfDay);
        float fogEnd = settings.renderDistance * CS * 0.95f;
        fc.fogRange[0] = fogEnd * 0.45f; fc.fogRange[1] = fogEnd; fc.fogRange[2] = exposure;
        fc.camF[0] = (float)f.x; fc.camF[1] = (float)f.y; fc.camF[2] = (float)f.z; fc.camR[0] = (float)r.x; fc.camR[1] = (float)r.y; fc.camR[2] = (float)r.z;
        fc.camU[0] = (float)u.x; fc.camU[1] = (float)u.y; fc.camU[2] = (float)u.z; fc.proj[0] = std::tan(XMConvertToRadians(settings.fov) * 0.5f); fc.proj[1] = aspect;

        // ---- shadow map: the world seen from the sun, around the camera
        int wantShadow = settings.shadowQuality <= 0 ? 0 : settings.shadowQuality == 1 ? 2048 : 4096;
        if (wantShadow != shadowSize) MakeShadowMap(wantShadow);
        bool shadows = shadowSize > 0 && fc.sunDir[3] > 0;
        XMMATRIX svp = XMMatrixIdentity();
        if (shadows) {
            float R = std::min(112.f, settings.renderDistance * (float)CS);
            V3 L{ fc.sunDir[0], fc.sunDir[1], fc.sunDir[2] }, up = std::fabs(L.y) > 0.99 ? V3{ 0, 0, 1 } : V3{ 0, 1, 0 };
            V3 zx = L * -1.0, xx = normalize(cross(up, zx)), yx = cross(zx, xx);
            XMMATRIX lv = XMMatrixLookToLH(XMVectorSet((float)L.x * 300, (float)L.y * 300, (float)L.z * 300, 1), XMVectorSet((float)zx.x, (float)zx.y, (float)zx.z, 0), XMVectorSet((float)up.x, (float)up.y, (float)up.z, 0));
            double texel = 2.0 * R / shadowSize, px = dot(camPos, xx), py = dot(camPos, yx);   // keep shadow pixels fixed to the world: no shimmering
            float mx = (float)(px - std::floor(px / texel) * texel), my = (float)(py - std::floor(py / texel) * texel);
            svp = lv * XMMatrixTranslation(mx, my, 0) * XMMatrixOrthographicLH(2 * R, 2 * R, 1, 600);
            fc.shadowInfo[0] = 1.f / shadowSize; fc.shadowInfo[1] = R; fc.shadowInfo[2] = (float)texel * 1.5f; fc.shadowInfo[3] = 1.6f;
        } else fc.sunDir[3] = 0;
        XMStoreFloat4x4(&fc.shadowVP, svp);
        // volumetrics: where the camera is (wrapped, the cloud noise repeats every 14400 blocks), cloud cover, fog for this hour
        const double wrapP = 14400.0;
        fc.camPos[0] = (float)(camPos.x - std::floor(camPos.x / wrapP) * wrapP); fc.camPos[1] = (float)camPos.y; fc.camPos[2] = (float)(camPos.z - std::floor(camPos.z / wrapP) * wrapP);
        float hr = settings.timeOfDay, morning = std::exp(-std::pow((hr - 6.8f) / 1.5f, 2.f)), evening = std::exp(-std::pow((hr - 20.f) / 2.f, 2.f));
        const WeatherLook W = EffWeather(); const EnvLook& E = envState.now;
        fc.volume[0] = settings.cloudCover <= 0 ? 0.f : std::min(1.f, W.cover * settings.cloudCover / 0.5f);
        fc.volume[1] = settings.groundFog * ((0.002f + 0.009f * morning + 0.003f * evening) * W.fog + 0.004f * (W.rain + W.snow)) * E.atmosphere;
        fc.env[0] = E.atmosphere; fc.env[1] = E.shimmer; fc.env[2] = E.planet; fc.env[3] = E.haze * std::max(0.f, settings.groundFog);
        fc.envPart[0] = E.particleKind; fc.envPart[1] = E.particleAmount; fc.envPart[2] = (float)world->liquidAt(camPos);   // under water / in lava for (int i = 0; i < 3; i++) fc.envPartColor[i] = E.particleColor[i];
        fc.volume[2] = (float)std::fmod(cloudTime, 4800.0); fc.volume[3] = settings.cloudShadows ? 1.f : 0.f;
        fc.depth[0] = 0.05f; fc.depth[1] = farZ; fc.depth[2] = (float)rw; fc.depth[3] = (float)rh;
        fc.clipPlane[3] = 1;   // nothing cut away (except while drawing a mirror's view)
        lastMirror = settings.reflections > 0 ? world->nearestMirror(camPos, player.forward(), 48) : MirrorPlane{};
        if (lastMirror.valid) {
            float n[3] = { 0, 0, 0 }; n[lastMirror.axis] = (float)lastMirror.sign;
            double camA = lastMirror.axis == 0 ? camPos.x : lastMirror.axis == 1 ? camPos.y : camPos.z;
            for (int i = 0; i < 3; i++) fc.mirror[i] = n[i];
            fc.mirror[3] = (float)(-(lastMirror.coord - camA) * lastMirror.sign);   // plane in camera-relative space: dot(n, p) + w = 0
        }
        fc.refl[3] = lastMirror.valid ? 1.f : 0.f;
        fc.ao[0] = settings.ambientOcclusion > 0 ? 1.f : 0.f; fc.ao[1] = settings.ambientOcclusion >= 2 ? 3.0f : 2.0f;
        fc.ao[2] = std::clamp(settings.aoStrength, 0.f, 3.f); fc.ao[3] = settings.ambientOcclusion >= 2 ? 20.f : 12.f;
        fc.refl[0] = settings.reflections > 0 ? 1.f : 0.f; fc.refl[1] = settings.reflections >= 2 ? 48.f : 28.f; fc.refl[2] = settings.reflections >= 2 ? 120.f : 64.f;
        fc.extra[0] = nightAmount * (1 - W.cover * 0.8f); fc.extra[1] = 0.15f * std::max(0.f, settings.bloom); fc.extra[2] = settings.swaying ? 0.5f + W.wind : 0.f; fc.extra[3] = 1;
        // "what is above" map: only needed while it rains/snows or surfaces are wet/snowy
        bool skyMap = W.rain > 0.01f || W.snow > 0.01f || weather.wetness > 0.01f || weather.snowCover > 0.01f;
        XMMATRIX skyvp = XMMatrixIdentity();
        if (skyMap) {
            const float R = 64; double texel = 2.0 * R / 512, px = camPos.x, pz = camPos.z;
            float mx = (float)(px - std::floor(px / texel) * texel), mz = (float)(pz - std::floor(pz / texel) * texel);
            skyvp = XMMatrixLookToLH(XMVectorSet(0, 300, 0, 1), XMVectorSet(0, -1, 0, 0), XMVectorSet(0, 0, 1, 0)) * XMMatrixTranslation(mx, mz, 0) * XMMatrixOrthographicLH(2 * R, 2 * R, 1, 700);
        }
        XMStoreFloat4x4(&fc.skyVP, skyvp);
        fc.weather[0] = W.rain; fc.weather[1] = W.snow; fc.weather[2] = W.dark; fc.weather[3] = weather.wetness * E.atmosphere;
        fc.weather2[0] = weather.snowCover * E.atmosphere; fc.weather2[1] = weather.flash * E.atmosphere; fc.weather2[2] = W.wind; fc.weather2[3] = skyMap ? 1.f : 0.f;
        ctx->UpdateSubresource(cbFrame.Get(), 0, nullptr, &fc, 0, 0);
        ID3D11Buffer* cbs[2] = { cbFrame.Get(), cbChunk.Get() }; ctx->VSSetConstantBuffers(0, 2, cbs); ctx->PSSetConstantBuffers(0, 2, cbs);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); ctx->OMSetBlendState(bsOff.Get(), nullptr, 0xFFFFFFFF);
        auto drawChunks = [&](const XMMATRIX& m, int pass, bool counting) {
            for (auto& kv : gpu) {
                const GpuChunk& g = kv.second; UINT n = pass == 0 ? g.nO : pass == 1 ? g.nC : g.nW; if (!n) continue;
                // chunk origin relative to the camera, computed in double: no jitter however far you go
                float ox = (float)(kv.first.x * (double)CS - camPos.x), oy = (float)(kv.first.y * (double)CS - camPos.y), oz = (float)(kv.first.z * (double)CS - camPos.z);
                if (!BoxVisible(m, ox, oy, oz, (float)CS)) continue;
                ChunkCB cc{ { ox, oy, oz, 0 } }; ctx->UpdateSubresource(cbChunk.Get(), 0, nullptr, &cc, 0, 0);
                UINT stride = sizeof(Vtx), off = 0;
                ctx->IASetVertexBuffers(0, 1, pass == 0 ? g.vbO.GetAddressOf() : pass == 1 ? g.vbC.GetAddressOf() : g.vbW.GetAddressOf(), &stride, &off);
                ctx->IASetIndexBuffer(pass == 0 ? g.ibO.Get() : pass == 1 ? g.ibC.Get() : g.ibW.Get(), DXGI_FORMAT_R32_UINT, 0);
                ctx->DrawIndexed(n, 0, 0); if (counting) drawnChunks += pass == 0;
            }
            for (size_t oi = 0; oi < objects.size(); oi++) if ((int)oi != movingObj) DrawObject(objects[oi], pass);   // objects: same shaders, turned and sized
        };
        ID3D11ShaderResourceView* none[5] = {};
        ctx->PSSetShaderResources(0, 5, none);
        if (shadows) {
            ctx->OMSetRenderTargets(0, nullptr, shadowDSV.Get()); ctx->ClearDepthStencilView(shadowDSV.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
            D3D11_VIEWPORT sv{ 0, 0, (float)shadowSize, (float)shadowSize, 0, 1 }; ctx->RSSetViewports(1, &sv);
            ctx->RSSetState(rsShadow.Get()); ctx->OMSetDepthStencilState(dsOn.Get(), 0); ctx->IASetInputLayout(layout.Get()); ctx->VSSetShader(vsShadow.Get(), nullptr, 0);
            ctx->PSSetShaderResources(0, 1, texArray.GetAddressOf()); ctx->PSSetSamplers(0, 1, sampBlocks.GetAddressOf());
            ctx->PSSetShader(nullptr, nullptr, 0); drawChunks(svp, 0, false);
            ctx->PSSetShader(psShadowCut.Get(), nullptr, 0); drawChunks(svp, 1, false);
        }

        if (skyMap) {
            ctx->OMSetRenderTargets(0, nullptr, skyMapDSV.Get()); ctx->ClearDepthStencilView(skyMapDSV.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
            D3D11_VIEWPORT sv{ 0, 0, 512, 512, 0, 1 }; ctx->RSSetViewports(1, &sv);
            ctx->RSSetState(rsShadow.Get()); ctx->OMSetDepthStencilState(dsOn.Get(), 0); ctx->IASetInputLayout(layout.Get()); ctx->VSSetShader(vsSkyMap.Get(), nullptr, 0);
            ctx->PSSetShaderResources(0, 1, texArray.GetAddressOf()); ctx->PSSetSamplers(0, 1, sampBlocks.GetAddressOf());
            ctx->PSSetShader(nullptr, nullptr, 0); drawChunks(skyvp, 0, false);
            ctx->PSSetShader(psShadowCut.Get(), nullptr, 0); drawChunks(skyvp, 1, false);
            ctx->PSSetShaderResources(0, 1, none);
        }

        // ---- a mirror in view: draw the world once more, reflected in its plane (half size, only what is in front of the mirror)
        if (lastMirror.valid) {
            FrameCB mc = fc;
            float* n = fc.mirror; float w = fc.mirror[3];
            XMMATRIX R(1 - 2 * n[0] * n[0], -2 * n[0] * n[1], -2 * n[0] * n[2], 0,
                       -2 * n[1] * n[0], 1 - 2 * n[1] * n[1], -2 * n[1] * n[2], 0,
                       -2 * n[2] * n[0], -2 * n[2] * n[1], 1 - 2 * n[2] * n[2], 0,
                       -2 * w * n[0], -2 * w * n[1], -2 * w * n[2], 1);   // points mirrored in the plane
            XMMATRIX mvp = R * vp; XMStoreFloat4x4(&mc.viewProj, mvp);
            auto refl3 = [&](float v[4]) { float d = v[0] * n[0] + v[1] * n[1] + v[2] * n[2]; for (int i = 0; i < 3; i++) v[i] -= 2 * d * n[i]; };
            refl3(mc.camF); refl3(mc.camR); refl3(mc.camU);                // the sky seen in the mirror
            for (int i = 0; i < 4; i++) mc.clipPlane[i] = fc.mirror[i];    // nothing behind the mirror
            mc.refl[3] = 0;
            ctx->UpdateSubresource(cbFrame.Get(), 0, nullptr, &mc, 0, 0);
            const float black[4] = { 0, 0, 0, 1 };
            ctx->OMSetRenderTargets(1, mirRTV.GetAddressOf(), mirDSV.Get()); ctx->ClearRenderTargetView(mirRTV.Get(), black); ctx->ClearDepthStencilView(mirDSV.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
            D3D11_VIEWPORT mv{ 0, 0, (float)std::max(1, rw / 2), (float)std::max(1, rh / 2), 0, 1 }; ctx->RSSetViewports(1, &mv);
            ctx->RSSetState(rsNoCull.Get()); ctx->OMSetDepthStencilState(dsOff.Get(), 0); ctx->IASetInputLayout(nullptr);
            ctx->PSSetShaderResources(3, 1, noiseSRV.GetAddressOf()); ctx->PSSetSamplers(2, 1, sampLinear.GetAddressOf());
            ctx->VSSetShader(vsSky.Get(), nullptr, 0); ctx->PSSetShader(psSky.Get(), nullptr, 0); ctx->Draw(3, 0);
            ctx->RSSetState(rsCullFront.Get()); ctx->OMSetDepthStencilState(dsOn.Get(), 0); ctx->IASetInputLayout(layout.Get()); ctx->VSSetShader(vsBlock.Get(), nullptr, 0);
            ID3D11ShaderResourceView* ms[5] = { texArray.Get(), shadows ? shadowSRV.Get() : nullptr, nullptr, noiseSRV.Get(), skyMap ? skyMapSRV.Get() : nullptr }; ctx->PSSetShaderResources(0, 5, ms);
            ID3D11SamplerState* mss[3] = { sampBlocks.Get(), sampShadow.Get(), sampLinear.Get() }; ctx->PSSetSamplers(0, 3, mss);
            ctx->PSSetShader(psOpaque.Get(), nullptr, 0); drawChunks(mvp, 0, false);
            ctx->PSSetShader(psCutout.Get(), nullptr, 0); drawChunks(mvp, 1, false);
            ctx->PSSetShaderResources(0, 5, none);
            ctx->UpdateSubresource(cbFrame.Get(), 0, nullptr, &fc, 0, 0);
        }

        // ---- the world, into the HDR picture
        const float black[4] = { 0, 0, 0, 1 };
        ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), dsv.Get()); ctx->ClearRenderTargetView(hdrRTV.Get(), black); ctx->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
        D3D11_VIEWPORT v{ 0, 0, (float)rw, (float)rh, 0, 1 }; ctx->RSSetViewports(1, &v);
        ctx->RSSetState(rsNoCull.Get());
        ctx->OMSetDepthStencilState(dsOff.Get(), 0); ctx->IASetInputLayout(nullptr);
        ctx->PSSetShaderResources(3, 1, noiseSRV.GetAddressOf()); ctx->PSSetSamplers(2, 1, sampLinear.GetAddressOf());   // the planet uses the cloud noise
        ctx->VSSetShader(vsSky.Get(), nullptr, 0); ctx->PSSetShader(psSky.Get(), nullptr, 0); ctx->Draw(3, 0);
        ctx->RSSetState(rsCull.Get());
        { const float zero[4] = { 0, 0, 0, 0 }; ctx->ClearRenderTargetView(surfRTV.Get(), zero); ctx->ClearRenderTargetView(indRTV.Get(), zero);
          ID3D11RenderTargetView* three[3] = { hdrRTV.Get(), surfRTV.Get(), indRTV.Get() }; ctx->OMSetRenderTargets(3, three, dsv.Get()); }   // blocks also note direction + shininess, and their sky light
        ctx->OMSetDepthStencilState(dsOn.Get(), 0); ctx->IASetInputLayout(layout.Get()); ctx->VSSetShader(vsBlock.Get(), nullptr, 0);
        ID3D11ShaderResourceView* srvs[5] = { texArray.Get(), shadows ? shadowSRV.Get() : nullptr, nullptr, noiseSRV.Get(), skyMap ? skyMapSRV.Get() : nullptr }; ctx->PSSetShaderResources(0, 5, srvs);
        ID3D11SamplerState* samps[3] = { sampBlocks.Get(), sampShadow.Get(), sampLinear.Get() }; ctx->PSSetSamplers(0, 3, samps);
        drawnChunks = 0;
        ctx->PSSetShader(psOpaque.Get(), nullptr, 0); drawChunks(vp, 0, true);
        ctx->PSSetShader(psCutout.Get(), nullptr, 0); drawChunks(vp, 1, false);

        // ---- ambient occlusion: soft shade where things are close together (half size, then a careful blur that takes away sky light)
        ctx->PSSetShaderResources(0, 5, none);
        if (settings.ambientOcclusion > 0) {
            ctx->OMSetRenderTargets(1, aoRTV.GetAddressOf(), nullptr);
            ctx->OMSetDepthStencilState(dsOff.Get(), 0); ctx->IASetInputLayout(nullptr); ctx->RSSetState(rsNoCull.Get()); ctx->OMSetBlendState(bsOff.Get(), nullptr, 0xFFFFFFFF);
            D3D11_VIEWPORT hv{ 0, 0, (float)std::max(1, rw / 2), (float)std::max(1, rh / 2), 0, 1 }; ctx->RSSetViewports(1, &hv);
            ctx->VSSetShader(vsSky.Get(), nullptr, 0); ctx->PSSetShader(psAO.Get(), nullptr, 0);
            { ID3D11ShaderResourceView* r6[6] = { nullptr, nullptr, depthSRV.Get(), nullptr, nullptr, surfSRV.Get() }; ctx->PSSetShaderResources(0, 6, r6); }
            ctx->Draw(3, 0);
            ctx->RSSetViewports(1, &v);
            ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), nullptr); ctx->OMSetBlendState(bsAdd.Get(), nullptr, 0xFFFFFFFF);
            ctx->PSSetShader(psAOApply.Get(), nullptr, 0);
            { ID3D11ShaderResourceView* r9[9] = { nullptr, nullptr, depthSRV.Get(), nullptr, nullptr, nullptr, nullptr, indSRV.Get(), aoSRV.Get() }; ctx->PSSetShaderResources(0, 9, r9); }
            ctx->Draw(3, 0);
            { ID3D11ShaderResourceView* n9[9] = {}; ctx->PSSetShaderResources(0, 9, n9); }
            ctx->OMSetBlendState(bsOff.Get(), nullptr, 0xFFFFFFFF);
        }
        // ---- reflections: shiny pixels look for what they mirror in the picture (depth must not be a target while read)
        ctx->OMSetRenderTargets(1, reflRTV.GetAddressOf(), nullptr);
        ctx->OMSetDepthStencilState(dsOff.Get(), 0); ctx->IASetInputLayout(nullptr); ctx->RSSetState(rsNoCull.Get());
        ctx->VSSetShader(vsSky.Get(), nullptr, 0); ctx->PSSetShader(psReflect.Get(), nullptr, 0);
        { ID3D11ShaderResourceView* r7[7] = { hdrSRV.Get(), nullptr, depthSRV.Get(), nullptr, skyMap ? skyMapSRV.Get() : nullptr, surfSRV.Get(), lastMirror.valid ? mirSRV.Get() : nullptr }; ctx->PSSetShaderResources(0, 7, r7); }
        ctx->PSSetSamplers(3, 1, sampClamp.GetAddressOf());
        ctx->Draw(3, 0);
        { ID3D11ShaderResourceView* n7[7] = {}; ctx->PSSetShaderResources(0, 7, n7); }
        // everything after this draws over the reflected picture, still depth-tested against the world
        ctx->OMSetRenderTargets(1, reflRTV.GetAddressOf(), dsv.Get()); ctx->RSSetState(rsCull.Get()); ctx->OMSetDepthStencilState(dsOn.Get(), 0);
        ctx->IASetInputLayout(layout.Get()); ctx->VSSetShader(vsBlock.Get(), nullptr, 0);
        ctx->PSSetShaderResources(0, 5, srvs);
        if (settings.blockPreview && Playing() && targetValid) {   // see-through preview of the next block
            if (ghostKey != targetValue) {
                ghostKey = targetValue;
                MeshInput in; in.pos = { 0, 0, 0 }; in.blocks.assign(PAD * PAD * PAD, AIR); in.blocks[padIdx(0, 0, 0)] = targetValue;
                ChunkMesh cm; buildMesh(reg, in, cm);
                MeshData all = cm.opaque; uint32_t base = (uint32_t)all.v.size();
                all.v.insert(all.v.end(), cm.cutout.v.begin(), cm.cutout.v.end()); for (uint32_t i : cm.cutout.i) all.i.push_back(base + i);
                Upload(all, vbGhost, ibGhost, nGhost);
            }
            if (nGhost) {
                ChunkCB cc{ { (float)(tx - camPos.x), (float)(ty - camPos.y), (float)(tz - camPos.z), 0 } }; ctx->UpdateSubresource(cbChunk.Get(), 0, nullptr, &cc, 0, 0);
                ctx->OMSetBlendState(bsAlpha.Get(), nullptr, 0xFFFFFFFF); ctx->OMSetDepthStencilState(dsTestOnly.Get(), 0); ctx->PSSetShader(psGhost.Get(), nullptr, 0);
                UINT stride = sizeof(Vtx), off = 0; ctx->IASetVertexBuffers(0, 1, vbGhost.GetAddressOf(), &stride, &off); ctx->IASetIndexBuffer(ibGhost.Get(), DXGI_FORMAT_R32_UINT, 0);
                ctx->DrawIndexed(nGhost, 0, 0);
                ctx->OMSetBlendState(bsOff.Get(), nullptr, 0xFFFFFFFF); ctx->OMSetDepthStencilState(dsOn.Get(), 0);
            }
        }
        if (toolMode && toolSel == 2 && !placing.empty() && !playMode) {   // the object you are placing, see-through
            WorldObject o; o.scale = placeScale;
            if (PlacePose(o)) {
                ctx->OMSetBlendState(bsAlpha.Get(), nullptr, 0xFFFFFFFF); ctx->OMSetDepthStencilState(dsTestOnly.Get(), 0); ctx->PSSetShader(psGhost.Get(), nullptr, 0);
                DrawObject(o, 0); DrawObject(o, 1);
                ctx->OMSetBlendState(bsOff.Get(), nullptr, 0xFFFFFFFF); ctx->OMSetDepthStencilState(dsOn.Get(), 0);
            }
        }

        // ---- water: drawn over the finished world; it reads a copy of the depth (to know how deep it is) and writes its own
        {
            bool anyWater = false; for (auto& kv : gpu) if (kv.second.nW) { anyWater = true; break; }
            if (anyWater) {
                ctx->OMSetRenderTargets(0, nullptr, nullptr);
                ctx->CopyResource(depthCopy.Get(), depthTex.Get());
                ctx->OMSetRenderTargets(1, reflRTV.GetAddressOf(), dsv.Get());
                ctx->OMSetBlendState(bsOff.Get(), nullptr, 0xFFFFFFFF); ctx->OMSetDepthStencilState(dsOn.Get(), 0); ctx->RSSetState(rsNoCull.Get());
                ctx->IASetInputLayout(layout.Get()); ctx->VSSetShader(vsBlock.Get(), nullptr, 0); ctx->PSSetShader(psWater.Get(), nullptr, 0);
                ID3D11ShaderResourceView* ws[5] = { hdrSRV.Get(), shadows ? shadowSRV.Get() : nullptr, depthCopySRV.Get(), noiseSRV.Get(), skyMap ? skyMapSRV.Get() : nullptr }; ctx->PSSetShaderResources(0, 5, ws);
                ID3D11SamplerState* wss[4] = { sampBlocks.Get(), sampShadow.Get(), sampLinear.Get(), sampClamp.Get() }; ctx->PSSetSamplers(0, 4, wss);
                drawChunks(vp, 2, false);
                ctx->PSSetShaderResources(0, 5, none); ctx->RSSetState(rsCull.Get());
                ctx->PSSetShaderResources(0, 5, srvs);
            }
        }

        // ---- rain or snow, then lightning
        if (W.rain > 0.02f || W.snow > 0.02f) {
            ctx->OMSetBlendState(bsAlpha.Get(), nullptr, 0xFFFFFFFF); ctx->OMSetDepthStencilState(dsTestOnly.Get(), 0); ctx->RSSetState(rsNoCull.Get());
            ctx->IASetInputLayout(nullptr); ctx->VSSetShader(vsRain.Get(), nullptr, 0); ctx->PSSetShader(psRain.Get(), nullptr, 0);
            ctx->Draw(6 * 14000, 0);
            ctx->OMSetBlendState(bsOff.Get(), nullptr, 0xFFFFFFFF); ctx->OMSetDepthStencilState(dsOn.Get(), 0); ctx->RSSetState(rsCull.Get());
        }
        if (weather.boltAge < 0.45 && E.atmosphere > 0.5f) DrawBolt();
        if (E.particleKind > 0.5f && E.particleAmount > 0.01f) {   // dust, embers, spores or ice sparkles floating in the air
            ctx->OMSetBlendState(bsAlpha.Get(), nullptr, 0xFFFFFFFF); ctx->OMSetDepthStencilState(dsTestOnly.Get(), 0); ctx->RSSetState(rsNoCull.Get());
            ctx->IASetInputLayout(nullptr); ctx->VSSetShader(vsDust.Get(), nullptr, 0); ctx->PSSetShader(psRain.Get(), nullptr, 0);
            ctx->Draw(6 * 14000, 0);
            ctx->OMSetBlendState(bsOff.Get(), nullptr, 0xFFFFFFFF); ctx->OMSetDepthStencilState(dsOn.Get(), 0); ctx->RSSetState(rsCull.Get());
        }

        // ---- clouds and ground fog over the picture (needs the depth: unbind it as a target first)
        ctx->PSSetShaderResources(0, 4, none);
        ctx->OMSetRenderTargets(1, volRTV.GetAddressOf(), nullptr);
        ctx->OMSetDepthStencilState(dsOff.Get(), 0); ctx->IASetInputLayout(nullptr); ctx->RSSetState(rsNoCull.Get());
        ctx->VSSetShader(vsSky.Get(), nullptr, 0); ctx->PSSetShader(psVolume.Get(), nullptr, 0);
        { ID3D11ShaderResourceView* v4[4] = { reflSRV.Get(), shadows ? shadowSRV.Get() : nullptr, depthSRV.Get(), noiseSRV.Get() }; ctx->PSSetShaderResources(0, 4, v4); }
        ctx->Draw(3, 0);

        // ---- bloom: shrink the picture step by step, then add the blurry copies back up
        ctx->PSSetShaderResources(0, 4, none);
        bool bloom = settings.bloom > 0 && !bloomLv.empty();
        ctx->PSSetSamplers(3, 1, sampClamp.GetAddressOf());
        if (bloom) {
            ctx->PSSetShader(psBloomDown.Get(), nullptr, 0);
            for (size_t i = 0; i < bloomLv.size(); i++) {
                ID3D11ShaderResourceView* src = i == 0 ? volSRV.Get() : bloomLv[i - 1].srv.Get();
                float sw = i == 0 ? (float)rw : (float)bloomLv[i - 1].w, sh2 = i == 0 ? (float)rh : (float)bloomLv[i - 1].h;
                ChunkCB cc{ { 1 / sw, 1 / sh2, 1, 0 } }; ctx->UpdateSubresource(cbChunk.Get(), 0, nullptr, &cc, 0, 0);
                ctx->OMSetRenderTargets(1, bloomLv[i].rtv.GetAddressOf(), nullptr);
                D3D11_VIEWPORT bv{ 0, 0, (float)bloomLv[i].w, (float)bloomLv[i].h, 0, 1 }; ctx->RSSetViewports(1, &bv);
                ctx->PSSetShaderResources(0, 1, &src); ctx->Draw(3, 0); ctx->PSSetShaderResources(0, 1, none);
            }
            ctx->PSSetShader(psBloomUp.Get(), nullptr, 0); ctx->OMSetBlendState(bsAdd.Get(), nullptr, 0xFFFFFFFF);
            for (size_t i = bloomLv.size() - 1; i > 0; i--) {
                ChunkCB cc{ { 1.f / bloomLv[i].w, 1.f / bloomLv[i].h, 1, 0 } }; ctx->UpdateSubresource(cbChunk.Get(), 0, nullptr, &cc, 0, 0);
                ctx->OMSetRenderTargets(1, bloomLv[i - 1].rtv.GetAddressOf(), nullptr);
                D3D11_VIEWPORT bv{ 0, 0, (float)bloomLv[i - 1].w, (float)bloomLv[i - 1].h, 0, 1 }; ctx->RSSetViewports(1, &bv);
                ID3D11ShaderResourceView* src = bloomLv[i].srv.Get(); ctx->PSSetShaderResources(0, 1, &src); ctx->Draw(3, 0); ctx->PSSetShaderResources(0, 1, none);
            }
            ctx->OMSetBlendState(bsOff.Get(), nullptr, 0xFFFFFFFF);
            D3D11_VIEWPORT fv{ 0, 0, (float)rw, (float)rh, 0, 1 }; ctx->RSSetViewports(1, &fv);
        }

        // ---- final picture: exposure + filmic curve into the window (or the editor's viewport picture)
        ctx->OMSetRenderTargets(1, &out, nullptr);
        ctx->VSSetShader(vsSky.Get(), nullptr, 0); ctx->PSSetShader(psPost.Get(), nullptr, 0);
        { ID3D11ShaderResourceView* pv[2] = { volSRV.Get(), bloom ? bloomLv[0].srv.Get() : nullptr }; ctx->PSSetShaderResources(0, 2, pv); }
        ctx->Draw(3, 0);
        ctx->PSSetShaderResources(0, 4, none);
    }
    int drawnChunks = 0;
    bool Project(V3 p, ImVec2& out) const {   // world point -> screen (for the block outline)
        V3 rel = p - camPos;
        XMVECTOR c = XMVector4Transform(XMVectorSet((float)rel.x, (float)rel.y, (float)rel.z, 1), view * proj); float w = XMVectorGetW(c);
        if (w < 0.05f) return false;
        out = ImVec2(vx + (XMVectorGetX(c) / w * 0.5f + 0.5f) * rw, vy + (0.5f - XMVectorGetY(c) / w * 0.5f) * rh); return true;
    }
    void Line3D(ImDrawList* dl, V3 a, V3 b, ImU32 col) {
        // clip against the near plane in camera space, then project
        V3 ra = a - camPos, rb = b - camPos;
        XMVECTOR va = XMVector3TransformCoord(XMVectorSet((float)ra.x, (float)ra.y, (float)ra.z, 1), view), vb = XMVector3TransformCoord(XMVectorSet((float)rb.x, (float)rb.y, (float)rb.z, 1), view);
        float za = XMVectorGetZ(va), zb = XMVectorGetZ(vb), n = 0.06f; if (za < n && zb < n) return;
        if (za < n) va = XMVectorLerp(va, vb, (n - za) / (zb - za)); else if (zb < n) vb = XMVectorLerp(vb, va, (n - zb) / (za - zb));
        auto scr = [&](XMVECTOR v) { XMVECTOR c = XMVector4Transform(XMVectorSetW(v, 1), proj); float w = XMVectorGetW(c); return ImVec2(vx + (XMVectorGetX(c) / w * 0.5f + 0.5f) * rw, vy + (0.5f - XMVectorGetY(c) / w * 0.5f) * rh); };
        dl->AddLine(scr(va), scr(vb), col, 2.f);
    }

    // ================================================================== UI
    ImTextureID Icon(int layer) const { return (ImTextureID)(intptr_t)icons[std::max(0, std::min((int)icons.size() - 1, layer))].Get(); }
    static void PointSampling(const ImDrawList*, const ImDrawCmd*) { g_app->ctx->PSSetSamplers(0, 1, g_app->sampPoint.GetAddressOf()); }
    void BlockIcon(ImDrawList* dl, BlockId v, ImVec2 c, float s) { if (v != AIR) ShapeIcon(dl, reg.get(v), v, c, s); }
    // a little 3D picture of a block in its shape: its triangles seen from above-front, painted back to front
    void ShapeIcon(ImDrawList* dl, const BlockDef& d, BlockId v, ImVec2 c, float s) {
        const Shapes& S = Shapes::get(); int shape = resolveShape(v, d); bool two = S.info(shape).twoSided;
        const ShapeVariant& sv = S.variant(shape, rotOf(v));
        float h = s * 0.5f, q = s * 0.25f;
        auto proj = [&](const float p[3]) { return ImVec2(c.x + (p[0] + p[2] - 1) * h, c.y - h * p[1] + q * p[0] - q * p[2] + q); };
        struct T { const STri* t; float depth; bool flip; };
        std::vector<T> list;
        for (int g = 0; g < 7; g++) for (const STri& t : sv.tris[g]) {
            float facing = t.fn[0] + t.fn[1] - t.fn[2];   // towards the viewer at (+1, +1, -1)
            if (facing <= 0.001f && !two) continue;
            list.push_back({ &t, (t.p[0][0] + t.p[1][0] + t.p[2][0]) + (t.p[0][1] + t.p[1][1] + t.p[2][1]) - (t.p[0][2] + t.p[1][2] + t.p[2][2]), facing <= 0 });
        }
        std::sort(list.begin(), list.end(), [](const T& a, const T& b) { return a.depth < b.depth; });
        dl->AddCallback(PointSampling, nullptr);
        int cur = -1;
        for (const T& e : list) {
            int f = faceOfNormal(e.t->fn), layer = d.tex[f];
            if (layer != cur) { if (cur >= 0) dl->PopTexture(); dl->PushTexture(Icon(layer)); cur = layer; }
            dl->PrimReserve(3, 3);
            for (int k = 0; k < 3; k++) {
                float u, vv; faceUV(e.t->p[k][0], e.t->p[k][1], e.t->p[k][2], f, u, vv);
                float n[3] = { e.t->n[k][0], e.t->n[k][1], e.t->n[k][2] }; float l = normalShade(n);
                ImU32 col = IM_COL32((int)(255 * l), (int)(255 * l), (int)(255 * l), 255);
                dl->PrimVtx(proj(e.t->p[k]), ImVec2(u, vv), col);
            }
        }
        if (cur >= 0) dl->PopTexture();
        dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }
    void DrawHud(ImDrawList* dl) {
        const float cw = (float)rw, ch = (float)rh;   // the 3D picture's size (it sits at vx, vy in the window)
        if (envWin || (!playMode && showZones)) {   // zone boxes, coloured by environment
            static const ImU32 cols[] = { IM_COL32(120, 220, 120, 255), IM_COL32(255, 200, 90, 255), IM_COL32(150, 220, 255, 255), IM_COL32(200, 160, 255, 255), IM_COL32(255, 90, 50, 255), IM_COL32(120, 255, 170, 255), IM_COL32(230, 140, 90, 255) };
            for (const EnvZone& z : info.zones) {
                ImU32 col = cols[std::max(0, findEnv(z.env)) % 7];
                V3 c[8]; for (int i = 0; i < 8; i++) c[i] = { (double)((i & 1) ? z.hi.x + 1 : z.lo.x), (double)((i & 2) ? z.hi.y + 1 : z.lo.y), (double)((i & 4) ? z.hi.z + 1 : z.lo.z) };
                const int e[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
                for (auto& k : e) Line3D(dl, c[k[0]], c[k[1]], col);
            }
        }
        if (hoverValid && Playing()) {   // outline of the targeted block
            BlockId hv = world->get(hover.x, hover.y, hover.z);
            const SBox& bb = Shapes::get().variant(resolveShape(hv, reg.get(hv)), rotOf(hv)).bounds;
            double b0[3] = { bb.lo[0], bb.lo[1], bb.lo[2] }, b1[3] = { bb.hi[0], bb.hi[1], bb.hi[2] };
            V3 mn{ hover.x + b0[0] - 0.002, hover.y + b0[1] - 0.002, hover.z + b0[2] - 0.002 }, mx{ hover.x + b1[0] + 0.002, hover.y + b1[1] + 0.002, hover.z + b1[2] + 0.002 };
            V3 c[8]; for (int i = 0; i < 8; i++) c[i] = { (i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z };
            const int e[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
            for (auto& k : e) Line3D(dl, c[k[0]], c[k[1]], IM_COL32(10, 10, 10, 200));
        }
        if (toolMode && toolValid && toolSel == 0 && !playMode) {   // the brush, where it would dab
            BrushOp op = stroking ? strokeOp : brush.op; ImU32 col = OpColor(op);
            V3 c{ toolHit.x + 0.5, toolHit.y + 0.5, toolHit.z + 0.5 };
            if (op == BrushOp::Add) c = { c.x + toolHit.nx, c.y + toolHit.ny, c.z + toolHit.nz };
            for (auto& l : brushOutline(brush)) Line3D(dl, { c.x + l.first.x, c.y + l.first.y, c.z + l.first.z }, { c.x + l.second.x, c.y + l.second.y, c.z + l.second.z }, col);
            V3 a{ (double)toolHit.x, (double)toolHit.y, (double)toolHit.z };
            V3 q[8]; for (int i = 0; i < 8; i++) q[i] = { a.x + ((i & 1) ? 1.0 : 0.0), a.y + ((i & 2) ? 1.0 : 0.0), a.z + ((i & 4) ? 1.0 : 0.0) };
            const int e[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
            for (auto& k : e) Line3D(dl, q[k[0]], q[k[1]], IM_COL32(255, 255, 255, 160));
        }
        DrawObjectGuides(dl);
        if (!playMode && info.hasSpawn) {   // the spawn point: a green player-sized box with an arrow where it looks
            V3 f = info.spawn; double hw = Player::HALF_W, H = Player::HEIGHT;
            V3 c[8]; for (int i = 0; i < 8; i++) c[i] = { f.x + ((i & 1) ? hw : -hw), f.y + ((i & 2) ? H : 0), f.z + ((i & 4) ? hw : -hw) };
            const int e[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
            ImU32 col = sel == Sel::Spawn ? IM_COL32(255, 255, 120, 255) : IM_COL32(90, 255, 120, 255);
            for (auto& k : e) Line3D(dl, c[k[0]], c[k[1]], col);
            V3 eye{ f.x, f.y + Player::EYE, f.z }, look{ f.x + std::sin(info.spawnYaw) * 1.2, f.y + Player::EYE, f.z + std::cos(info.spawnYaw) * 1.2 };
            Line3D(dl, eye, look, col);
            ImVec2 sp; if (Project({ f.x, f.y + H + 0.3, f.z }, sp)) dl->AddText(ImVec2(sp.x - 18, sp.y - 14), col, "Spawn");
        }
        if (hideHud) return;
        ImVec2 m(vx + cw * 0.5f, vy + ch * 0.5f);   // crosshair
        if (EditorView() && !captured) {   // the mouse is free: say how to get into the picture
            const char* t = "Click here to fly and build.   Esc frees the mouse.   F5 plays the game.";
            ImVec2 ts = ImGui::CalcTextSize(t); dl->AddRectFilled(ImVec2(m.x - ts.x * 0.5f - 10, vy + 10), ImVec2(m.x + ts.x * 0.5f + 10, vy + 16 + ts.y), IM_COL32(0, 0, 0, 140), 4);
            dl->AddText(ImVec2(m.x - ts.x * 0.5f, vy + 13), IM_COL32(255, 255, 255, 230), t);
        }
        dl->AddRectFilled(ImVec2(m.x - 9, m.y - 1), ImVec2(m.x + 9, m.y + 1), IM_COL32(240, 240, 240, 220));
        dl->AddRectFilled(ImVec2(m.x - 1, m.y - 9), ImVec2(m.x + 1, m.y + 9), IM_COL32(240, 240, 240, 220));
        if (playMode) { PlayHud(dl); return; }
        float s = 44 * settings.uiScale, W = 9 * s; ImVec2 o(vx + (cw - W) * 0.5f, vy + ch - s - 10);   // hotbar
        dl->AddRectFilled(ImVec2(o.x - 3, o.y - 3), ImVec2(o.x + W + 3, o.y + s + 3), toolMode ? IM_COL32(30, 20, 50, 170) : IM_COL32(0, 0, 0, 140), 3);
        for (int i = 0; i < 9; i++) {
            ImVec2 a(o.x + i * s, o.y), b(a.x + s, a.y + s);
            dl->AddRect(a, b, toolMode ? IM_COL32(150, 120, 200, 200) : IM_COL32(120, 120, 120, 200), 0.f, 0, 2.f);
            if (toolMode) ToolIcon(dl, i, ImVec2(a.x + s * 0.5f, a.y + s * 0.5f), s * 0.7f);
            else BlockIcon(dl, hotbar[i], ImVec2(a.x + s * 0.5f, a.y + s * 0.5f), s * 0.62f);
        }
        int hs = toolMode ? toolSel : hotSel;
        dl->AddRect(ImVec2(o.x + hs * s - 2, o.y - 2), ImVec2(o.x + hs * s + s + 2, o.y + s + 2), IM_COL32(255, 255, 255, 255), 0.f, 0, 3.f);
        if (toolMode) {
            std::string label = ToolLabel();
            ImVec2 ts = ImGui::CalcTextSize(label.c_str()); dl->AddText(ImVec2(vx + (cw - ts.x) * 0.5f, o.y - ts.y - 8), IM_COL32(255, 255, 255, 230), label.c_str());
        } else if (reg.has(hotbar[hotSel])) {
            BlockId hv = hotbar[hotSel]; std::string label = reg.get(hv).name;
            if (shapeOf(hv) && shapeOf(hv) != Shapes::FULL) label += " - " + Shapes::get().info(shapeOf(hv)).name;
            if (manualRot) label += "   [turned by hand: R turn, Shift+R tip, G automatic]";
            ImVec2 ts = ImGui::CalcTextSize(label.c_str()); dl->AddText(ImVec2(vx + (cw - ts.x) * 0.5f, o.y - ts.y - 8), IM_COL32(255, 255, 255, 230), label.c_str());
        }
        if (toastT > 0) { ImVec2 ts = ImGui::CalcTextSize(toast.c_str()); dl->AddRectFilled(ImVec2(vx + (cw - ts.x) * 0.5f - 10, vy + 60), ImVec2(vx + (cw + ts.x) * 0.5f + 10, vy + 66 + ts.y + 6), IM_COL32(0, 0, 0, 150), 4); dl->AddText(ImVec2(vx + (cw - ts.x) * 0.5f, vy + 66), IM_COL32(255, 255, 255, 240), toast.c_str()); }
    }
    void DrawF3() {
        ImGui::SetNextWindowPos(ImVec2(8, 8)); ImGui::SetNextWindowBgAlpha(0.45f);
        ImGui::Begin("##f3", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav);
        V3 p = player.feet; I3 b{ ifloor(p.x), ifloor(p.y), ifloor(p.z) }; I3 c = chunkOf(b.x, b.y, b.z);
        const char* dirs[4] = { "south (+Z)", "west (-X)", "north (-Z)", "east (+X)" };
        double yd = std::fmod(-player.yaw * 57.29578 + 360 * 1000 + 45, 360.0);
        ImGui::Text("PRIMEngine  %.0f fps", fps);
        ImGui::Text("XYZ: %.3f / %.3f / %.3f", p.x, p.y, p.z);
        ImGui::Text("Block: %d %d %d   Chunk: %d %d %d", b.x, b.y, b.z, c.x, c.y, c.z);
        ImGui::Text("Facing: %s   %s", dirs[((int)(yd / 90)) % 4], player.flying ? "flying" : "walking");
        ImGui::Text("Chunks: %zu loaded, %zu with meshes, %d drawn", world->loadedCount(), gpu.size(), drawnChunks);
        ImGui::Text("Meshing: %zu waiting, %zu queued", world->dirty.size(), workers->pending());
        if (hoverValid) { BlockId hv = world->get(hover.x, hover.y, hover.z);
            ImGui::Text("Looking at: %s (%s, rotation %d) at %d %d %d", reg.get(hv).name.c_str(), Shapes::get().info(resolveShape(hv, reg.get(hv))).name.c_str(), rotOf(hv), hover.x, hover.y, hover.z); }
        ImGui::Text("Render distance: %d chunks   Undo steps: %zu", settings.renderDistance, history.undoCount());
        ImGui::Text("Environment: %s%s", envTypes()[envState.active].name.c_str(), envState.inZone ? (" (zone \"" + envState.zoneName + "\")").c_str() : "");
        ImGui::Text("Weather: %s%s   rain %.2f snow %.2f wind %.2f   wet %.2f snow cover %.2f", weather.type().name.c_str(), weather.autoChange ? " (changes by itself)" : "", weather.now.rain, weather.now.snow, weather.now.wind, weather.wetness, weather.snowCover);
        ImGui::End();
    }
    void Center(ImVec2 size) { ImGui::SetNextWindowPos(ImVec2(cw * 0.5f, ch * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f)); ImGui::SetNextWindowSize(size, ImGuiCond_Always); }
    void SettingsWindow() {
        Center(ImVec2(460 * settings.uiScale, 0));
        ImGui::Begin("Settings", &showSettings, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);
        bool ch_ = false;
        ch_ |= ImGui::SliderInt("Render distance", &settings.renderDistance, 2, 32, "%d chunks");
        ch_ |= ImGui::SliderInt("Up/down distance", &settings.verticalDistance, 1, 12, "%d chunks");
        ch_ |= ImGui::SliderFloat("Field of view", &settings.fov, 50, 110, "%.0f");
        ch_ |= ImGui::SliderFloat("Mouse sensitivity", &settings.mouseSensitivity, 0.1f, 3.f, "%.2f");
        ch_ |= ImGui::SliderFloat("UI size", &settings.uiScale, 0.75f, 2.f, "%.2f");
        ch_ |= ImGui::Checkbox("VSync", &settings.vsync);
        ch_ |= ImGui::Checkbox("Show where the next block goes (see-through preview)", &settings.blockPreview);
        ImGui::SeparatorText("Look");
        ch_ |= ImGui::SliderFloat("Time of day", &settings.timeOfDay, 0, 24, "%.1f h");
        ch_ |= ImGui::Checkbox("The sun moves (a day is 20 minutes)", &settings.dayCycle);
        const char* sq[] = { "Off", "Normal", "High" }; ch_ |= ImGui::Combo("Shadows", &settings.shadowQuality, sq, 3);
        ch_ |= ImGui::SliderFloat("Clouds", &settings.cloudCover, 0, 1, settings.cloudCover <= 0 ? "none" : "%.2f");
        ch_ |= ImGui::Checkbox("Cloud shadows on the ground", &settings.cloudShadows);
        ch_ |= ImGui::SliderFloat("Ground fog", &settings.groundFog, 0, 2, settings.groundFog <= 0 ? "none" : "%.2f");
        ch_ |= ImGui::SliderFloat("Glow (bloom)", &settings.bloom, 0, 1, settings.bloom <= 0 ? "off" : "%.2f");
        ch_ |= ImGui::Checkbox("Leaves and plants sway in the wind", &settings.swaying);
        const char* rq[] = { "Off (only the sky)", "Normal", "High" }; ch_ |= ImGui::Combo("Reflections", &settings.reflections, rq, 3);
        const char* aq[] = { "Off (only block corners)", "Normal", "High" }; ch_ |= ImGui::Combo("Ambient occlusion", &settings.ambientOcclusion, aq, 3);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Soft shade in corners, under overhangs and where things touch.\nWorks for everything on screen.");
        if (settings.ambientOcclusion > 0) ch_ |= ImGui::SliderFloat("Occlusion strength", &settings.aoStrength, 0.2f, 3.f, "%.1f");
        if (ch_) settings.save(settingsPath);
        if (ImGui::Button("Done", ImVec2(-1, 0))) showSettings = false;
        ImGui::End();
    }
    // ================================================================== project manager (the start screen)
    std::string ProjectsRoot() { std::string d = ExeDir() + "projects"; std::error_code ec; fs::create_directories(d, ec); return d; }
    void RefreshProjects() { projects = Projects::list(ProjectsRoot()); if (projSel >= (int)projects.size()) projSel = projects.empty() ? -1 : 0; }
    void OpenProject(const std::string& dir, int firstWorldType = 0) {
        if (world) CloseWorld();
        projectDir = dir;
        library = Library{}; library.dir = dir; library.load(reg); UploadTextures(); ghostKey = 0xFFFFFFFFu;   // this game's blocks and textures
        models.loadAll(dir + "/models"); modelGpu.clear();
        game = GameSettings{}; gamePath = dir + "/game.txt"; game.load(gamePath);
        RefreshWorlds();
        auto has = [&](const std::string& n) { return !n.empty() && std::find(worldList.begin(), worldList.end(), n) != worldList.end(); };
        std::string w = has(game.lastWorld) ? game.lastWorld : has(game.startWorld) ? game.startWorld : worldList.empty() ? "" : worldList[0];
        if (w.empty()) CreateWorld("Level 1", firstWorldType); else OpenWorld(w);
        SetWindowTextA(hwnd, ("PRIMEngine - " + game.name).c_str());
    }
    void CloseProject() {
        if (world) CloseWorld();
        if (!gamePath.empty()) game.save(gamePath);
        screen = Screen::Title; Capture(false); editorOpen = false; sel = Sel::World; RefreshProjects();
        SetWindowTextA(hwnd, "PRIMEngine");
    }
    static ImU32 NameColor(const std::string& s) {   // a steady colour per project name
        uint32_t h = 2166136261u; for (char c : s) h = (h ^ (uint8_t)c) * 16777619u;
        static const ImU32 cols[] = { IM_COL32(86, 156, 214, 255), IM_COL32(220, 120, 80, 255), IM_COL32(120, 190, 100, 255), IM_COL32(190, 110, 210, 255), IM_COL32(230, 180, 60, 255), IM_COL32(80, 190, 180, 255), IM_COL32(220, 90, 130, 255) };
        return cols[h % 7];
    }
    void ProjectManager() {
        float ui = settings.uiScale; const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos); ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28 * ui, 22 * ui));
        ImGui::Begin("##projects", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopStyleVar();
        ImGui::PushFont(nullptr, ImGui::GetFontSize() * 2.2f); ImGui::TextUnformatted("PRIMEngine"); ImGui::PopFont();
        ImGui::TextDisabled("Your game projects. Each one is a whole game: its blocks, textures, worlds and rules.");
        ImGui::Spacing();
        float side = 240 * ui, sp = ImGui::GetStyle().ItemSpacing.x;
        ImGui::BeginChild("list", ImVec2(-side - sp, 0), ImGuiChildFlags_Borders);
        std::time_t now = std::time(nullptr);
        if (projects.empty()) {
            ImGui::Dummy(ImVec2(0, 60 * ui)); ImGui::TextDisabled("No projects yet.");
            if (ImGui::Button("Make your first game", ImVec2(240 * ui, 40 * ui))) { newProjName = "My Game"; wantPopup = "New project"; }
        }
        for (int i = 0; i < (int)projects.size(); i++) {
            const ProjectEntry& p = projects[i]; ImGui::PushID(i);
            float h = 64 * ui;
            if (ImGui::Selectable("##card", projSel == i, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, h))) {
                projSel = i; if (ImGui::IsMouseDoubleClicked(0)) pending = [this, f = p.folder] { OpenProject(f); };
            }
            if (ImGui::BeginPopupContextItem("ctx")) {
                projSel = i;
                if (ImGui::MenuItem("Open")) pending = [this, f = p.folder] { OpenProject(f); };
                if (ImGui::MenuItem("Rename...")) { newProjName = p.name; wantPopup = "Rename project"; }
                if (ImGui::MenuItem("Show the folder")) ShellExecuteA(nullptr, "open", p.folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                ImGui::Separator();
                if (ImGui::MenuItem("Delete...")) wantPopup = "Delete project?";
                ImGui::EndPopup();
            }
            ImVec2 a = ImGui::GetItemRectMin(); ImDrawList* dl = ImGui::GetWindowDrawList();
            float ic = h - 12 * ui; ImVec2 i0(a.x + 6 * ui, a.y + 6 * ui);
            dl->AddRectFilled(i0, ImVec2(i0.x + ic, i0.y + ic), NameColor(p.name), 8.f * ui);
            std::string ini; for (size_t k = 0; k < p.name.size() && ini.size() < 2; k++) if (std::isalnum((unsigned char)p.name[k]) && (k == 0 || p.name[k - 1] == ' ')) ini += (char)std::toupper((unsigned char)p.name[k]);
            ImGui::PushFont(nullptr, ImGui::GetFontSize() * 1.6f);
            ImVec2 ts = ImGui::CalcTextSize(ini.c_str()); dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(i0.x + (ic - ts.x) * 0.5f, i0.y + (ic - ts.y) * 0.5f), IM_COL32(255, 255, 255, 240), ini.c_str());
            ImGui::PopFont();
            float tx = i0.x + ic + 14 * ui;
            ImGui::PushFont(nullptr, ImGui::GetFontSize() * 1.25f);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(tx, a.y + 8 * ui), IM_COL32(240, 240, 245, 255), p.name.c_str());
            ImGui::PopFont();
            std::string line = std::to_string(p.worlds) + (p.worlds == 1 ? " world" : " worlds") + "   -   opened " + Projects::ago(p.opened, now);
            dl->AddText(ImVec2(tx, a.y + 34 * ui), IM_COL32(160, 165, 175, 255), line.c_str());
            dl->AddText(ImVec2(tx + ImGui::CalcTextSize(line.c_str()).x + 24 * ui, a.y + 34 * ui), IM_COL32(110, 115, 125, 255), fs::path(p.folder).filename().string().c_str());
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("side", ImVec2(side, 0));
        ImVec2 bw(-1, 34 * ui);
        if (ImGui::Button("New project", bw)) { newProjName = "My Game"; newWorldType = 0; wantPopup = "New project"; }
        bool ok = projSel >= 0 && projSel < (int)projects.size();
        ImGui::BeginDisabled(!ok);
        if (ImGui::Button("Open", bw)) pending = [this, f = projects[projSel].folder] { OpenProject(f); };
        if (ImGui::Button("Rename...", ImVec2(-1, 0))) { newProjName = projects[projSel].name; wantPopup = "Rename project"; }
        if (ImGui::Button("Show the folder", ImVec2(-1, 0))) ShellExecuteA(nullptr, "open", projects[projSel].folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (ImGui::Button("Delete...", ImVec2(-1, 0))) wantPopup = "Delete project?";
        ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::Button("Open the projects folder", ImVec2(-1, 0))) ShellExecuteA(nullptr, "open", ProjectsRoot().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (ImGui::Button("Settings", ImVec2(-1, 0))) showSettings = true;
        if (ImGui::Button("Quit", ImVec2(-1, 0))) PostQuitMessage(0);
        ImGui::Spacing(); ImGui::TextDisabled("Double-click a project to open it.\nRight-click for more.");
        ImGui::EndChild();
        ImGui::End();
    }
    // ---- Play mode
    void StartPlay(bool grabMouse = true) {
        if (screen != Screen::Game || playMode) return;
        EndStroke(); brushMenu = false; placing.clear(); dupFrom = -1; if (movingObj >= 0) { movingObj = -1; RebuildSolids(); }
        editCam = player; playMode = true; paused = false; inventory = false; shapeMenu = false; editorOpen = false;
        playSpawn = info.hasSpawn ? info.spawn : player.feet; playYaw = info.hasSpawn ? info.spawnYaw : player.yaw;
        play = PlayState{}; play.start(player, game.player, playSpawn, playYaw);
        if (grabMouse) { Capture(false); captureNext = true; }   // grabbed next frame, once the picture fills the window
        Toast(info.hasSpawn ? "Playing. Esc: menu, F5: back to the editor" : "Playing from where the camera was (set a spawn point in the Outliner). F5: back to the editor");
    }
    void StopPlay() {
        if (!playMode) return;
        playMode = false; paused = false; player = editCam; Capture(false); captureNext = false;
        Toast("Back in the editor");
    }
    void PlayHud(ImDrawList* dl) {
        const float W = (float)rw, H = (float)rh, ui = settings.uiScale;
        if (play.hurtFlash > 0) {   // red edges when hurt
            ImU32 r = IM_COL32(200, 0, 0, (int)(play.hurtFlash / 0.4 * 120)), z = IM_COL32(200, 0, 0, 0); float e = 90 * ui;
            dl->AddRectFilledMultiColor(ImVec2(vx, vy), ImVec2(vx + W, vy + e), r, r, z, z);
            dl->AddRectFilledMultiColor(ImVec2(vx, vy + H - e), ImVec2(vx + W, vy + H), z, z, r, r);
            dl->AddRectFilledMultiColor(ImVec2(vx, vy), ImVec2(vx + e, vy + H), r, z, z, r);
            dl->AddRectFilledMultiColor(ImVec2(vx + W - e, vy), ImVec2(vx + W, vy + H), z, r, r, z);
        }
        float bw = 300 * ui, bh = 18 * ui; ImVec2 o(vx + (W - bw) * 0.5f, vy + H - bh - 24 * ui);   // health bar
        float f = game.player.maxHealth > 0 ? std::clamp(play.health / game.player.maxHealth, 0.f, 1.f) : 0.f;
        dl->AddRectFilled(ImVec2(o.x - 3, o.y - 3), ImVec2(o.x + bw + 3, o.y + bh + 3), IM_COL32(0, 0, 0, 160), 4.f);
        ImU32 col = f > 0.5f ? IM_COL32(70, 210, 90, 255) : f > 0.25f ? IM_COL32(240, 190, 50, 255) : IM_COL32(230, 60, 50, 255);
        dl->AddRectFilled(o, ImVec2(o.x + bw * f, o.y + bh), col, 3.f);
        char hb[64]; snprintf(hb, sizeof hb, "%.0f / %.0f", play.health, game.player.maxHealth);
        ImVec2 ts = ImGui::CalcTextSize(hb); dl->AddText(ImVec2(o.x + (bw - ts.x) * 0.5f, o.y + (bh - ts.y) * 0.5f), IM_COL32(255, 255, 255, 255), hb);
        dl->AddText(ImVec2(vx + 12, vy + 10), IM_COL32(255, 255, 255, 200), game.name.c_str());
        if (play.dead()) {
            dl->AddRectFilled(ImVec2(vx, vy), ImVec2(vx + W, vy + H), IM_COL32(60, 0, 0, 140));
            std::string a = "You died", b = std::string("You ") + play.cause + ". Back in " + std::to_string((int)std::ceil(std::max(0.0, game.player.respawnDelay - play.deadFor))) + " s";
            ImGui::PushFont(nullptr, ImGui::GetFontSize() * 2.5f); ImVec2 t1 = ImGui::CalcTextSize(a.c_str());
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(vx + (W - t1.x) * 0.5f, vy + H * 0.38f), IM_COL32(255, 230, 230, 255), a.c_str()); ImGui::PopFont();
            ImVec2 t2 = ImGui::CalcTextSize(b.c_str()); dl->AddText(ImVec2(vx + (W - t2.x) * 0.5f, vy + H * 0.38f + t1.y + 10), IM_COL32(255, 255, 255, 230), b.c_str());
        }
    }
    void PauseMenu() {   // Esc while playing
        Center(ImVec2(320 * settings.uiScale, 0));
        ImGui::Begin("Paused###play", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextDisabled("%s", game.name.c_str());
        if (ImGui::Button("Keep playing", ImVec2(-1, 0))) { paused = false; Capture(true); }
        if (ImGui::Button("Start over", ImVec2(-1, 0))) { play.start(player, game.player, playSpawn, playYaw); paused = false; Capture(true); }
        if (ImGui::Button("Settings", ImVec2(-1, 0))) showSettings = true;
        if (ImGui::Button("Stop playing (F5)", ImVec2(-1, 0))) StopPlay();
        ImGui::End();
    }
    // ---- blocks & textures window (E in game, "Blocks & textures" on the title screen)
    float GridChildWidth(float s, int cols) { ImGuiStyle& st = ImGui::GetStyle(); return cols * (s + st.ItemSpacing.x) + st.ScrollbarSize + st.WindowPadding.x * 2; }
    bool Tile(const char* id, ImVec2& p, float s, bool selected = false) {   // a square button; returns true when clicked
        p = ImGui::GetCursorScreenPos();
        bool clicked = ImGui::InvisibleButton(id, ImVec2(s, s), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        bool hov = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + s, p.y + s), hov ? IM_COL32(80, 90, 120, 255) : IM_COL32(45, 45, 50, 255), 3.f);
        if (selected) dl->AddRect(p, ImVec2(p.x + s, p.y + s), IM_COL32(255, 255, 255, 255), 3.f, 0, 2.f);
        return clicked;
    }
    void TextureImage(int layer, ImVec2 p, float s, float pad) {
        ImDrawList* dl = ImGui::GetWindowDrawList(); dl->AddCallback(PointSampling, nullptr);
        dl->AddImage(Icon(layer), ImVec2(p.x + pad, p.y + pad), ImVec2(p.x + s - pad, p.y + s - pad));
        dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }
    void MakeBlockFrom(const std::string& tex, ImportAs as) {
        BlockId id = AIR; ChangeBlocks([&] { id = reg.add(Library::blockFromTexture(reg, tex, as)); });
        library.saveBlocks(reg);
        if (id && screen == Screen::Game) hotbar[hotSel] = id;
        if (id) Toast("Made the block \"" + reg.get(id).name + "\"" + (screen == Screen::Game ? " (it is in your hotbar)" : ""));
    }
    void EditBlock(BlockId id) {
        draftId = id; draft = reg.get(id); pickSlot = -1; editorOpen = true;
        const auto& f = draft.faces;
        bool sidesSame = f[F_PX] == f[F_NX] && f[F_PX] == f[F_PZ] && f[F_PX] == f[F_NZ];
        faceMode = sidesSame && f[F_PY] == f[F_PX] && f[F_NY] == f[F_PX] ? 0 : sidesSame ? 1 : 2;
    }
    void NewBlock() {
        draftId = AIR; draft = BlockDef{}; draft.name = reg.uniqueName("New block"); pickSlot = -1; faceMode = 0; editorOpen = true;
        std::string first = reg.textureCount() > 1 ? reg.image(1).name : "";
        for (auto& f : draft.faces) f = first;
    }
    void InventoryScreen() {
        bool inGame = screen == Screen::Game; float ui = settings.uiScale, s = 52 * ui; const int cols = 10;
        ImGui::SetNextWindowPos(ImVec2(cw * 0.5f, ch * 0.45f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::Begin(inGame ? "Blocks###inv" : "Blocks & textures###inv", inGame ? nullptr : &libraryOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
        if (ImGui::Button("Import images...")) { auto f = PickImages(); if (!f.empty()) { importFiles = f; wantPopup = "Import images"; } }
        ImGui::SameLine(); if (ImGui::Button("New block")) NewBlock();
        ImGui::SameLine(); if (ImGui::Button("Open textures folder")) ShellExecuteA(nullptr, "open", library.texturesDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        ImGui::SameLine(); ImGui::TextDisabled("or drop PNG files onto the window");
        if (ImGui::BeginTabBar("tabs")) {
            if (ImGui::BeginTabItem("Blocks")) {
                ImGui::BeginChild("blocks", ImVec2(GridChildWidth(s, cols), (s + ImGui::GetStyle().ItemSpacing.y) * 5 + 12), ImGuiChildFlags_Borders);
                int n = 0;
                for (BlockId id = 1; id < reg.count(); id++) {
                    if (!reg.has(id)) continue;
                    ImGui::PushID(id); ImVec2 p;
                    if (Tile("b", p, s)) {
                        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) || !inGame) EditBlock(id); else hotbar[hotSel] = id;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", reg.get(id).name.c_str(), inGame ? "Left click: put in the hotbar.  Right click: edit." : "Click to edit.");
                    BlockIcon(ImGui::GetWindowDrawList(), id, ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), s * 0.7f);
                    ImGui::PopID();
                    if (++n % cols) ImGui::SameLine();
                }
                ImGui::EndChild();
                ImGui::TextDisabled(inGame ? "Left click: put it in hotbar slot %d.   Right click: edit the block." : "Click a block to edit it.", hotSel + 1);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Textures")) {
                ImGui::BeginChild("textures", ImVec2(GridChildWidth(s, cols), (s + ImGui::GetStyle().ItemSpacing.y) * 5 + 12), ImGuiChildFlags_Borders);
                for (int t = 1, n = 0; t < (int)reg.textureCount(); t++) {
                    ImGui::PushID(t); ImVec2 p;
                    if (Tile("t", p, s)) ImGui::OpenPopup("tex");
                    const TexImage& im = reg.image(t);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s  (%d x %d%s)\nClick for options", im.name.c_str(), im.w, im.h, im.transparent ? ", see-through" : "");
                    TextureImage(t, p, s, 4);
                    if (ImGui::BeginPopup("tex")) {
                        ImGui::TextDisabled("%s", im.name.c_str());
                        std::string name = im.name;
                        if (ImGui::MenuItem("Make a block")) MakeBlockFrom(name, ImportAs::Blocks);
                        if (ImGui::MenuItem("Make a cross plane (like flowers)")) MakeBlockFrom(name, ImportAs::CrossPlanes);
                        if (ImGui::MenuItem("Make a flat plane (like ladders)")) MakeBlockFrom(name, ImportAs::FlatPlanes);
                        ImGui::Separator();
                        if (ImGui::MenuItem("Delete texture...")) { deleteTexAsk = name; wantPopup = "Delete texture?"; }
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                    if (++n % cols) ImGui::SameLine();
                }
                ImGui::EndChild();
                ImGui::TextDisabled("Textures are the PNG files in the textures folder. Repaint one and it updates here by itself.");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        if (inGame) {
            ImGui::Separator(); ImGui::TextDisabled("Hotbar (click a slot to choose it, right-click to empty it)");
            for (int i = 0; i < 9; i++) {
                ImGui::PushID(1000 + i); ImVec2 p;
                if (Tile("h", p, s, i == hotSel)) { if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) hotbar[i] = AIR; else hotSel = i; }
                BlockIcon(ImGui::GetWindowDrawList(), hotbar[i], ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), s * 0.7f);
                ImGui::PopID(); if (i < 8) ImGui::SameLine();
            }
        }
        ImGui::End();
    }

    // ---- environment: the world's air and zones with their own (Inspector)
    void EnvCombo(const char* label, std::string& key) {
        const auto& types = envTypes(); int cur = std::max(0, findEnv(key));
        if (ImGui::BeginCombo(label, types[cur].name.c_str())) {
            for (int i = 0; i < (int)types.size(); i++) if (ImGui::Selectable(types[i].name.c_str(), i == cur)) key = types[i].key;
            ImGui::EndCombo();
        }
    }
    I3 Here() const { return { ifloor(player.feet.x), ifloor(player.feet.y), ifloor(player.feet.z) }; }
    bool ZoneFields(EnvZone& z) {   // returns true when "Delete" was clicked
        float ui = settings.uiScale; I3 here = Here(); bool del = false;
        (void)ui;
        Prop("Name"); ImGui::InputText("##name", &z.name);
        Prop("Air"); EnvCombo("##air", z.env);
        Prop("From"); ImGui::InputInt3("##from", &z.lo.x);
        Prop("To"); ImGui::InputInt3("##to", &z.hi.x);
        if (ImGui::SmallButton("From = here")) z.lo = here;
        if (FlowButton("To = here", true)) z.hi = here;
        if (FlowButton("Go there", true)) { player.feet = { (z.lo.x + z.hi.x) * 0.5 + 0.5, (double)std::min(z.lo.y, z.hi.y), (z.lo.z + z.hi.z) * 0.5 + 0.5 }; player.flying = true; }
        if (FlowButton("Delete", true)) del = true;
        if (z.lo.x > z.hi.x) std::swap(z.lo.x, z.hi.x);
        if (z.lo.y > z.hi.y) std::swap(z.lo.y, z.hi.y);
        if (z.lo.z > z.hi.z) std::swap(z.lo.z, z.hi.z);
        return del;
    }
    void AddZoneHere() {
        I3 here = Here();
        info.zones.push_back({ "Zone " + std::to_string(info.zones.size() + 1), "space", { here.x - 8, here.y - 4, here.z - 8 }, { here.x + 8, here.y + 12, here.z + 8 } });
        sel = Sel::Zone; selIndex = (int)info.zones.size() - 1;
    }
    void EnvBody() {
        Prop("Air"); EnvCombo("##air", info.environment);
        Hint("The air of this whole world. Zones (in the Outliner) can have their own.");
        Hint("You are in: %s", envState.inZone ? ("the zone \"" + envState.zoneName + "\"").c_str() : "the world's own air");
        if (ImGui::Button("Add a zone around me")) AddZoneHere();
    }
    void WeatherBody() {
        Hint("This world's weather (it fades over in about 20 seconds):");
        const auto& types = weatherTypes();
        for (int i = 0; i < (int)types.size(); i++) if (ImGui::RadioButton(types[i].name.c_str(), weather.target == i)) weather.set(i, false);
        ImGui::Separator();
        ImGui::Checkbox("Weather changes by itself", &weather.autoChange);
        if (weather.autoChange) Hint("Next change in about %d minutes.", (int)(weather.secondsToChange() / 60) + 1);
    }

    // ---- shape menu (Q): any block in any shape
    void ShapeMenu() {
        float ui = settings.uiScale, s = 64 * ui; const int cols = 9; const Shapes& S = Shapes::get();
        Center(ImVec2(0, 0));
        ImGui::Begin("Shapes###shapes", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
        BlockId held = hotbar[hotSel];
        if (!reg.has(held)) { ImGui::Text("Put a block into this hotbar slot first (press E)."); ImGui::End(); return; }
        const BlockDef& d = reg.get(held); int cur = resolveShape(held, d);
        ImGui::Text("Shapes for %s", d.name.c_str()); ImGui::SameLine(); ImGui::TextDisabled("(%d shapes, each in 24 rotations)", Shapes::get().count() - 1);
        ImGui::SetNextItemWidth(260 * ui); ImGui::InputTextWithHint("##search", "Search shapes...", &shapeSearch);
        std::vector<std::string> cats = S.categories(); cats.insert(cats.begin(), "All");
        if (ImGui::BeginTabBar("cats")) {
            for (int i = 0; i < (int)cats.size(); i++) if (ImGui::BeginTabItem(cats[i].c_str())) { shapeCat = i; ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        auto lower = [](std::string t) { for (auto& ch : t) ch = (char)std::tolower((unsigned char)ch); return t; };
        std::string q = lower(shapeSearch);
        ImGui::BeginChild("grid", ImVec2(GridChildWidth(s, cols), (s + ImGui::GetStyle().ItemSpacing.y) * 5 + 12), ImGuiChildFlags_Borders);
        int n = 0;
        for (int id = 1; id < S.count(); id++) {
            const ShapeInfo& si = S.info(id);
            if (shapeCat > 0 && shapeCat < (int)cats.size() && si.category != cats[shapeCat] && q.empty()) continue;
            if (!q.empty() && lower(si.name).find(q) == std::string::npos) continue;
            ImGui::PushID(id); ImVec2 p;
            if (Tile("s", p, s, id == cur)) { hotbar[hotSel] = makeBlock(typeOf(held), id); manualRot = false; shapeMenu = false; Capture(true); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", si.name.c_str());
            ShapeIcon(ImGui::GetWindowDrawList(), d, makeBlock(typeOf(held), id), ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), s * 0.72f);
            ImGui::PopID();
            if (++n % cols) ImGui::SameLine();
        }
        if (!n) ImGui::TextDisabled("No shape is called that.");
        ImGui::EndChild();
        ImGui::TextDisabled("Click a shape to build with it.   In the world: R turns it, Shift+R tips it over, G back to automatic,\nCtrl + middle click copies a placed block's shape and rotation.   Q or Esc closes this.");
        ImGui::End();
    }

    // ---- block editor
    void BlockEditor() {
        float W = 460 * settings.uiScale; Center(ImVec2(W, 0)); ImGui::SetNextWindowSizeConstraints(ImVec2(W, 0), ImVec2(W, FLT_MAX));
        ImGui::Begin(draftId ? "Edit block###editor" : "New block###editor", &editorOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
        BlockEditorBody();
        ImGui::End();
    }
    void BlockEditorBody() {
        float ui = settings.uiScale;
        for (int f = 0; f < 6; f++) { int t = reg.textureIndex(draft.faces[f]); draft.tex[f] = t < 0 ? 0 : t; }
        // the preview on top (in the middle), then one row per setting: label left, field right
        float pv = std::min(120 * ui, ImGui::GetContentRegionAvail().x);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, (ImGui::GetContentRegionAvail().x - pv) * 0.5f));
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + pv, p.y + pv), IM_COL32(30, 32, 38, 255), 4.f);
        ShapeIcon(ImGui::GetWindowDrawList(), draft, AIR, ImVec2(p.x + pv * 0.5f, p.y + pv * 0.5f), pv * 0.75f);
        ImGui::Dummy(ImVec2(pv, pv));
        Prop("Name"); ImGui::InputText("##name", &draft.name);
        Prop("Folder");
        { float bw = ImGui::GetFrameHeight(); ImGui::SetNextItemWidth(std::max(20.f, ImGui::GetContentRegionAvail().x - bw - 2)); ImGui::InputText("##cat", &draft.category); ImGui::SameLine(0, 2);
          if (ImGui::BeginCombo("##folders", nullptr, ImGuiComboFlags_NoPreview | ImGuiComboFlags_PopupAlignLeft)) {   // the block's folder in the asset browser
              for (const std::string& c : BlockCategories()) if (ImGui::Selectable(c.c_str(), c == draft.category)) draft.category = c;
              ImGui::EndCombo();
          } }
        int shape = (int)draft.shape; const char* shapes[] = { "Full block", "Cross plane (flowers, tall grass)", "Flat plane (ladders, vines, posters)" };
        Prop("Shape");
        if (ImGui::Combo("##shape", &shape, shapes, 3)) {
            draft.shape = (Shape)shape;
            if (draft.shape != Shape::Cube) { draft.render = Render::Cutout; draft.solid = false; faceMode = 0; for (auto& f : draft.faces) f = draft.faces[0]; }
            else { draft.solid = true; draft.render = reg.image(draft.tex[0]).transparent ? Render::Cutout : Render::Opaque; }
        }
        if (draft.shape == Shape::Cube) {
            bool see = draft.render == Render::Cutout;
            Prop("See-through"); if (ImGui::Checkbox("##see", &see)) draft.render = see ? Render::Cutout : Render::Opaque;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The picture's transparent pixels are see-through.");
            if (see) { Prop("Hide inner sides"); ImGui::Checkbox("##cull", &draft.cullSame); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hide the sides between two of these, like glass."); }
            Prop("Solid"); ImGui::Checkbox("##solid", &draft.solid); if (ImGui::IsItemHovered()) ImGui::SetTooltip("You bump into it.");
        }
        Prop("Glows"); ImGui::SliderInt("##light", &draft.light, 0, 15, draft.light ? "light level %d" : "no");
        if (draft.light) {
            float col[3] = { ((draft.lightColor >> 16) & 255) / 255.f, ((draft.lightColor >> 8) & 255) / 255.f, (draft.lightColor & 255) / 255.f };
            Prop("Light colour");
            if (ImGui::ColorEdit3("##lcol", col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel))
                draft.lightColor = ((uint32_t)std::lround(col[0] * 255) << 16) | ((uint32_t)std::lround(col[1] * 255) << 8) | (uint32_t)std::lround(col[2] * 255);
        }
        Prop("Sways in the wind"); ImGui::Checkbox("##waves", &draft.waves);
        Prop("Shininess"); ImGui::SliderFloat("##shine", &draft.shine, 0, 1, draft.shine <= 0 ? "matt" : "%.2f");
        Prop("Mirror"); ImGui::Checkbox("##mirror", &draft.mirror);
        if (draft.shape != Shape::Cube) Hint(draft.shape == Shape::Cross ? "Stands in the middle of its block. You walk through it." : "Sticks to the side of the block you click on. You walk through it.");

        ImGui::Separator();
        // texture slots
        struct Slot { const char* label; std::vector<int> faces; };
        std::vector<Slot> slots;
        if (draft.shape != Shape::Cube) slots = { { "Picture", { 0, 1, 2, 3, 4, 5 } } };
        else {
            const char* modes[] = { "Same picture on every side", "Top, bottom and sides", "Every side different" };
            Prop("Textures"); ImGui::Combo("##faces", &faceMode, modes, 3);
            if (faceMode == 0) slots = { { "All sides", { 0, 1, 2, 3, 4, 5 } } };
            else if (faceMode == 1) slots = { { "Top", { F_PY } }, { "Sides", { F_PX, F_NX, F_PZ, F_NZ } }, { "Bottom", { F_NY } } };
            else slots = { { "Top", { F_PY } }, { "Bottom", { F_NY } }, { "North (-Z)", { F_NZ } }, { "South (+Z)", { F_PZ } }, { "East (+X)", { F_PX } }, { "West (-X)", { F_NX } } };
        }
        float ts = 64 * ui;
        for (int i = 0; i < (int)slots.size(); i++) {
            if (i) Flow(ts);   // texture slots wrap to the next line when the panel is narrow
            ImGui::PushID(i); ImGui::BeginGroup();
            ImVec2 tp; if (Tile("slot", tp, ts)) { pickSlot = i; ImGui::OpenPopup("pick"); }
            TextureImage(draft.tex[slots[i].faces[0]], tp, ts, 4);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nClick to choose a texture", draft.faces[slots[i].faces[0]].empty() ? "(no texture)" : draft.faces[slots[i].faces[0]].c_str());
            ImGui::TextDisabled("%s", slots[i].label);
            ImGui::EndGroup();
            if (ImGui::BeginPopup("pick")) {   // choose a texture for this slot
                ImGui::TextDisabled("Choose a texture for: %s", slots[i].label);
                float s = 48 * ui; int n = 0;
                ImGui::BeginChild("pickgrid", ImVec2(GridChildWidth(s, 8), (s + ImGui::GetStyle().ItemSpacing.y) * 4 + 12), ImGuiChildFlags_Borders);
                for (int t = 1; t < (int)reg.textureCount(); t++) {
                    ImGui::PushID(t); ImVec2 q;
                    if (Tile("t", q, s)) { for (int f : slots[i].faces) draft.faces[f] = reg.image(t).name; if (draft.shape == Shape::Cube && faceMode == 0 && reg.image(t).transparent) draft.render = Render::Cutout; ImGui::CloseCurrentPopup(); }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", reg.image(t).name.c_str());
                    TextureImage(t, q, s, 3);
                    ImGui::PopID(); if (++n % 8) ImGui::SameLine();
                }
                ImGui::EndChild();
                if (ImGui::Button("Import images...")) { auto f = PickImages(); if (!f.empty()) { int add = 0; ChangeBlocks([&] { add = (int)library.import(f, ImportAs::TexturesOnly, reg).textures.size(); }); Toast(std::to_string(add) + " textures imported"); } }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }

        ImGui::Separator();
        bool ok = !draft.name.empty(); float bw = Cols(draftId ? 3 : 2);
        ImGui::BeginDisabled(!ok);
        if (ImGui::Button(draftId ? "Save" : "Create", ImVec2(bw, 0))) {
            draft.name = reg.uniqueName(draft.name, draftId); bool isNew = draftId == AIR;
            ChangeBlocks([&] { if (draftId) reg.put(draftId, draft); else draftId = reg.add(draft); });
            library.saveBlocks(reg);
            if (isNew && draftId && screen == Screen::Game) hotbar[hotSel] = draftId;
            Toast(isNew ? "Made the block \"" + draft.name + "\"" : "Saved \"" + draft.name + "\"");
            editorOpen = false; if (sel == Sel::Block) selIndex = (int)draftId;
        }
        ImGui::EndDisabled();
        ImGui::SameLine(); if (ImGui::Button("Cancel", ImVec2(bw, 0))) editorOpen = false;
        if (draftId) { ImGui::SameLine(); if (ImGui::Button("Delete...", ImVec2(bw, 0))) { deleteBlockAsk = draftId; wantPopup = "Delete block?"; } }
    }

    // ================================================================== the editor (G1)
    std::vector<std::string> BlockCategories() const {
        std::vector<std::string> c;
        for (BlockId id = 1; id < reg.count(); id++) if (reg.has(id)) { const std::string& k = reg.get(id).category; if (std::find(c.begin(), c.end(), k) == c.end()) c.push_back(k); }
        std::sort(c.begin(), c.end()); return c;
    }
    static std::string Lower(std::string t) { for (auto& ch : t) ch = (char)std::tolower((unsigned char)ch); return t; }
    void Select(Sel s, int i = 0) {
        sel = s; selIndex = i;
        if (s == Sel::Block && reg.has((BlockId)i)) EditBlock((BlockId)i); else if (s != Sel::Block) editorOpen = false;
    }
    void BuildLayout(ImGuiID dock) {   // the default arrangement: Outliner left, Inspector right, Assets below, the viewport in the middle
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::DockBuilderRemoveNode(dock);
        ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dock, vp->WorkSize);
        ImGuiID mid = dock, left, right, bottom;
        left = ImGui::DockBuilderSplitNode(mid, ImGuiDir_Left, 0.17f, nullptr, &mid);
        right = ImGui::DockBuilderSplitNode(mid, ImGuiDir_Right, 0.27f, nullptr, &mid);
        bottom = ImGui::DockBuilderSplitNode(mid, ImGuiDir_Down, 0.30f, nullptr, &mid);
        ImGui::DockBuilderDockWindow("Outliner", left);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Assets", bottom);
        ImGui::DockBuilderDockWindow("Viewport", mid);
        if (ImGuiDockNode* n = ImGui::DockBuilderGetNode(mid)) n->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
        ImGui::DockBuilderFinish(dock);
    }
    bool PlayButton(bool stop) {   // a button with a drawn triangle (or square)
        float h = ImGui::GetFrameHeight();
        ImGui::PushStyleColor(ImGuiCol_Button, stop ? ImVec4(0.55f, 0.18f, 0.16f, 1) : ImVec4(0.16f, 0.42f, 0.22f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, stop ? ImVec4(0.7f, 0.25f, 0.2f, 1) : ImVec4(0.2f, 0.55f, 0.28f, 1));
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool c = ImGui::Button(stop ? "      Stop  (F5)" : "      Play  (F5)");
        ImGui::PopStyleColor(2);
        ImDrawList* dl = ImGui::GetWindowDrawList(); float s = h * 0.32f; ImVec2 m(p.x + h * 0.6f, p.y + h * 0.5f);
        if (stop) dl->AddRectFilled(ImVec2(m.x - s, m.y - s), ImVec2(m.x + s, m.y + s), IM_COL32(255, 255, 255, 255));
        else dl->AddTriangleFilled(ImVec2(m.x - s * 0.8f, m.y - s), ImVec2(m.x - s * 0.8f, m.y + s), ImVec2(m.x + s, m.y), IM_COL32(255, 255, 255, 255));
        return c;
    }
    void MenuBar() {
        if (!ImGui::BeginMainMenuBar()) return;
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save", "Ctrl+S")) { SaveWorld(); Toast("Saved"); }
            if (ImGui::MenuItem("Import images...")) { auto f = PickImages(); if (!f.empty()) { importFiles = f; wantPopup = "Import images"; } }
            if (ImGui::MenuItem("Open textures folder")) ShellExecuteA(nullptr, "open", library.texturesDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            ImGui::Separator();
            if (ImGui::MenuItem("New world...")) { newName = "New World"; newType = 0; wantPopup = "New world"; }
            if (ImGui::MenuItem("Show the project folder")) ShellExecuteA(nullptr, "open", projectDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            ImGui::Separator();
            if (ImGui::MenuItem("Close project (back to the projects)")) pending = [this] { CloseProject(); };
            if (ImGui::MenuItem("Quit")) PostMessageA(hwnd, WM_CLOSE, 0, 0);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) { if (history.undo(*world)) Toast("Undo"); }
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) { if (history.redo(*world)) Toast("Redo"); }
            ImGui::Separator();
            if (ImGui::MenuItem("New block")) { NewBlock(); sel = Sel::Block; selIndex = 0; }
            if (ImGui::MenuItem("Settings...")) showSettings = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Game")) {
            if (ImGui::MenuItem("Play", "F5")) StartPlay();
            if (ImGui::MenuItem("Game settings")) Select(Sel::Game);
            if (ImGui::MenuItem("Set the spawn point here")) { info.hasSpawn = true; info.spawn = player.feet; info.spawnYaw = player.yaw; Select(Sel::Spawn); }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Show zones", nullptr, &showZones);
            ImGui::MenuItem("Show position info", "F3", &showF3);
            ImGui::MenuItem("Hide the hotbar and crosshair", "F1", &hideHud);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset the panel layout")) resetLayout = true;
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    void Toolbar() {
        ImGuiViewport* vp = ImGui::GetMainViewport(); float ui = settings.uiScale;
        ImGuiWindowFlags fl = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 5));
        if (ImGui::BeginViewportSideBar("##toolbar", vp, ImGuiDir_Up, ImGui::GetFrameHeight() + 10, fl)) {
            if (PlayButton(false)) StartPlay();
            ImGui::SameLine(); if (ImGui::Button("Save")) { SaveWorld(); Toast("Saved"); }
            ImGui::SameLine(); if (ImGui::Button("Undo")) { if (history.undo(*world)) Toast("Undo"); }
            ImGui::SameLine(); if (ImGui::Button("Redo")) { if (history.redo(*world)) Toast("Redo"); }
            ImGui::SameLine(); ImGui::TextDisabled("|");
            ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::Text("Time");
            ImGui::SameLine(); ImGui::SetNextItemWidth(120 * ui); ImGui::SliderFloat("##time", &settings.timeOfDay, 0, 24, "%.1f h");
            ImGui::SameLine(); ImGui::Checkbox("Sun moves", &settings.dayCycle);
            ImGui::SameLine(); ImGui::TextDisabled("|");
            ImGui::SameLine(); ImGui::Text("Weather");
            ImGui::SameLine(); ImGui::SetNextItemWidth(150 * ui);
            { const auto& types = weatherTypes();
              if (ImGui::BeginCombo("##weather", types[weather.target].name.c_str())) {
                  for (int i = 0; i < (int)types.size(); i++) if (ImGui::Selectable(types[i].name.c_str(), weather.target == i)) weather.set(i, false);
                  ImGui::EndCombo();
              } }
            ImGui::SameLine(); ImGui::TextDisabled("|");
            ImGui::SameLine(); bool fly = player.flying; if (ImGui::Checkbox("Fly", &fly)) player.toggleFly();
        }
        ImGui::End();
        if (ImGui::BeginViewportSideBar("##status", vp, ImGuiDir_Down, ImGui::GetFrameHeight(), fl)) {
            I3 b = Here();
            std::string held = reg.has(hotbar[hotSel]) ? reg.get(hotbar[hotSel]).name : "(empty hand)";
            if (reg.has(hotbar[hotSel]) && shapeOf(hotbar[hotSel]) && shapeOf(hotbar[hotSel]) != Shapes::FULL) held += " - " + Shapes::get().info(shapeOf(hotbar[hotSel])).name;
            if (toolMode) held = ToolName();
            ImGui::Text("%s   |   %s   |   %s   |   Block %d %d %d   |   %zu chunks   |   %.0f fps",
                info.name.c_str(), captured ? "In the viewport (Esc frees the mouse)" : "Editing", (toolMode ? held : "Building with: " + held).c_str(), b.x, b.y, b.z, gpu.size(), fps);
            if (toastT > 0) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "   %s", toast.c_str()); }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }
    // ---- Outliner: everything in the world and the game, in folders
    bool Leaf(const char* label, Sel s, int i = 0) {
        ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth | (sel == s && selIndex == i ? ImGuiTreeNodeFlags_Selected : 0);
        ImGui::TreeNodeEx((void*)(intptr_t)((int)s * 100000 + i), f, "%s", label);
        bool c = ImGui::IsItemClicked(); if (c) Select(s, i); return c;
    }
    void Outliner() {
        ImGui::Begin("Outliner");
        ImGuiTreeNodeFlags open = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (ImGui::TreeNodeEx("Worlds", open, "Worlds (%d)", (int)worldList.size())) {   // every world (level) of this game
            std::string cur = CurrentWorld();
            for (const std::string& w : worldList) {
                ImGui::PushID(w.c_str());
                bool isCur = w == cur;
                ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth | (isCur ? ImGuiTreeNodeFlags_Selected : 0);
                std::string label = w + (w == game.startWorld ? "   (start)" : "") + (isCur ? "   - open" : "");
                ImGui::TreeNodeEx("w", f, "%s", label.c_str());
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && !isCur) pending = [this, w] { OpenWorld(w); };
                if (ImGui::IsItemHovered() && !isCur) ImGui::SetTooltip("Double-click to open it");
                if (ImGui::BeginPopupContextItem("wctx")) {
                    if (ImGui::MenuItem("Open", nullptr, false, !isCur)) pending = [this, w] { OpenWorld(w); };
                    if (ImGui::MenuItem("The game starts here", nullptr, w == game.startWorld)) { game.startWorld = w == game.startWorld ? "" : w; game.save(gamePath); }
                    if (ImGui::MenuItem("Rename...")) { renameFrom = w; newName = w; wantPopup = "Rename world"; }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete...", nullptr, false, !isCur)) { deleteAsk = w; wantPopup = "Delete world?"; }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            if (ImGui::SmallButton("+ New world")) { newName = "New World"; newType = 0; wantPopup = "New world"; }
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("World", open, "This world: %s", info.name.c_str())) {
            Leaf("World settings", Sel::World);
            Leaf("Weather", Sel::Weather);
            Leaf("Air (environment)", Sel::Environment);
            Leaf(info.hasSpawn ? "Spawn point" : "Spawn point (not set)", Sel::Spawn);
            Leaf("Editor camera", Sel::Player);
            bool zo = ImGui::TreeNodeEx("zones", open, "Zones (%d)", (int)info.zones.size());
            if (ImGui::BeginPopupContextItem("zonesctx")) { if (ImGui::MenuItem("Add a zone around me")) AddZoneHere(); ImGui::EndPopup(); }
            if (zo) {
                int del = -1;
                for (int i = 0; i < (int)info.zones.size(); i++) {
                    ImGui::PushID(i); Leaf((info.zones[i].name + "  (" + envTypes()[std::max(0, findEnv(info.zones[i].env))].name + ")").c_str(), Sel::Zone, i);
                    if (ImGui::BeginPopupContextItem("zctx")) {
                        if (ImGui::MenuItem("Go there")) { const EnvZone& z = info.zones[i]; player.feet = { (z.lo.x + z.hi.x) * 0.5 + 0.5, (double)z.lo.y, (z.lo.z + z.hi.z) * 0.5 + 0.5 }; player.flying = true; }
                        if (ImGui::MenuItem("Delete")) del = i;
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
                if (del >= 0) { info.zones.erase(info.zones.begin() + del); if (sel == Sel::Zone) sel = Sel::Environment; }
                if (ImGui::SmallButton("+ Zone around me")) AddZoneHere();
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Objects", open, "Objects (%d)", (int)objects.size())) {   // by folder
            std::vector<std::string> folders; for (auto& o : objects) if (std::find(folders.begin(), folders.end(), o.folder) == folders.end()) folders.push_back(o.folder);
            std::sort(folders.begin(), folders.end());
            int act = -1, what = 0;
            for (const std::string& f : folders) {
                if (!ImGui::TreeNodeEx(f.c_str(), open)) continue;
                for (int i = 0; i < (int)objects.size(); i++) {
                    if (objects[i].folder != f) continue;
                    ImGui::PushID(i); Leaf(objects[i].name.c_str(), Sel::Object, i);
                    if (ImGui::BeginPopupContextItem("octx")) {
                        if (ImGui::MenuItem("Go there")) { sel = Sel::Object; selIndex = i; const WorldObject& o = objects[i]; player.feet = { o.pos.x, o.pos.y + 3, o.pos.z - 5 }; player.flying = true; player.yaw = 0; player.pitch = -0.4; }
                        if (ImGui::MenuItem("Move")) { act = i; what = 1; }
                        if (ImGui::MenuItem("Copy")) { act = i; what = 2; }
                        ImGui::Separator(); if (ImGui::MenuItem("Delete")) { act = i; what = 3; }
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            if (act >= 0) { if (what == 1) PickUpObject(act); else if (what == 2) DuplicateObject(act); else DeleteObject(act); }
            if (objects.empty()) Hint("Press T, take the Selection box (slot 2), box some blocks and press \"Make into object\".");
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Game", open, "Game: %s", game.name.c_str())) {
            Leaf("Game settings and player rules", Sel::Game);
            ImGui::TreePop();
        }
        ImGui::End();
    }
    // ---- Inspector: the settings of whatever is selected
    void Inspector() {
        ImGui::Begin("Inspector");
        float ui = settings.uiScale;
        auto title = [](const char* t) { ImGui::SeparatorText(t); };
        switch (sel) {
        case Sel::World: {
            title("World");
            Prop("Name"); ImGui::InputText("##Name", &info.name);
            Prop("Time of day"); ImGui::SliderFloat("##Time of day", &settings.timeOfDay, 0, 24, "%.1f h");
            Prop("The sun moves"); ImGui::Checkbox("##daycycle", &settings.dayCycle);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("A whole day lasts 20 minutes.");
            Hint("Folder: %s", fs::path(worldDir).filename().string().c_str());
            Hint("%s", info.genKind == Generator::Void ? "Empty world (void)" : "Flat world");
            if (ImGui::Button("Save the world now")) { SaveWorld(); Toast("Saved"); }
            break; }
        case Sel::Weather: title("Weather"); WeatherBody(); break;
        case Sel::Environment: title("Air (environment)"); EnvBody(); break;
        case Sel::Zone:
            if (selIndex < (int)info.zones.size()) {
                title("Zone"); ImGui::PushID(selIndex);
                if (ZoneFields(info.zones[selIndex])) { info.zones.erase(info.zones.begin() + selIndex); sel = Sel::Environment; }
                ImGui::PopID();
                Hint("A zone has its own air: a space station, a cave of ice, a volcano ...\nIf zones overlap, the smallest one wins.");
            } else sel = Sel::Environment;
            break;
        case Sel::Spawn:
            title("Spawn point");
            if (!info.hasSpawn) Hint("No spawn point yet. Playing starts wherever the editor camera is.");
            else {
                Prop("Position"); ImGui::InputScalarN("##Position", ImGuiDataType_Double, &info.spawn.x, 3, nullptr, nullptr, "%.1f");
                float deg = (float)(info.spawnYaw * 57.29578); Prop("Looks towards");
                if (ImGui::SliderFloat("##looks", &deg, -180, 180, "%.0f deg")) info.spawnYaw = deg / 57.29578;
            }
            if (ImGui::Button("Put it where I am")) { info.hasSpawn = true; info.spawn = player.feet; info.spawnYaw = player.yaw; }
            if (info.hasSpawn) {
                if (FlowButton("Go there")) { player.feet = info.spawn; player.yaw = info.spawnYaw; player.pitch = 0; }
                if (FlowButton("Remove")) info.hasSpawn = false;
            }
            Hint("The green box in the viewport. The player starts here,\nand comes back here after dying.");
            break;
        case Sel::Player:
            title("Editor camera");
            Prop("Position"); ImGui::InputScalarN("##Position", ImGuiDataType_Double, &player.feet.x, 3, nullptr, nullptr, "%.1f");
            { bool fly = player.flying; Prop("Flying"); if (ImGui::Checkbox("##fly", &fly)) player.toggleFly(); }
            Hint("This is you while building. Double-tap Space to fly or walk.");
            break;
        case Sel::Game: {
            title("Game"); bool ch_ = false;
            Prop("Name"); ch_ |= ImGui::InputText("##Name", &game.name);
            Prop("Starts in world");
            if (ImGui::BeginCombo("##startworld", game.startWorld.empty() ? "(the open world)" : game.startWorld.c_str())) {
                if (ImGui::Selectable("(the open world)", game.startWorld.empty())) { game.startWorld.clear(); ch_ = true; }
                for (auto& w : worldList) if (ImGui::Selectable(w.c_str(), w == game.startWorld)) { game.startWorld = w; ch_ = true; }
                ImGui::EndCombo();
            }
            title("Player rules"); PlayerRules& r = game.player;
            auto sl = [&](const char* l, float* v, float a, float b, const char* f) { Prop(l); ImGui::PushID(l); ch_ |= ImGui::SliderFloat("##v", v, a, b, f); ImGui::PopID(); };
            sl("Walk speed", &r.walkSpeed, 1, 15, "%.1f blocks/s");
            Prop("Can sprint (Ctrl)"); ch_ |= ImGui::Checkbox("##sprint", &r.canSprint);
            if (r.canSprint) sl("Sprint speed", &r.sprintSpeed, 1, 25, "%.1f blocks/s");
            sl("Jump height", &r.jumpHeight, 0.3f, 8, "%.2f blocks");
            sl("Gravity", &r.gravity, 4, 80, "%.0f");
            sl("Health", &r.maxHealth, 1, 500, "%.0f");
            sl("Lava damage", &r.lavaDamage, 0, 200, "%.0f per second");
            sl("Fall damage", &r.fallDamage, 0, 50, r.fallDamage <= 0 ? "off" : "%.0f per block (after 4)");
            sl("Death below height", &r.voidDepth, -512, 0, "y = %.0f");
            sl("Respawn after", &r.respawnDelay, 0, 10, "%.1f s");
            if (ImGui::Button("Reset the rules")) { r = PlayerRules{}; ch_ = true; }
            if (ch_) game.save(gamePath);
            Hint("Saved by itself. Press F5 to try it.");
            break; }
        case Sel::Block:
            if (!editorOpen && reg.has((BlockId)selIndex)) EditBlock((BlockId)selIndex);
            if (editorOpen) { title(draftId ? "Block" : "New block"); BlockEditorBody(); }
            else Hint("Pick something in the Outliner or the Assets.");
            break;
        case Sel::Texture:
            if (selIndex > 0 && selIndex < (int)reg.textureCount()) {
                const TexImage& im = reg.image(selIndex); std::string name = im.name;
                title("Texture");
                float s = std::min(ImGui::GetContentRegionAvail().x, 200 * ui); ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + s, p.y + s), IM_COL32(30, 32, 38, 255), 4.f);
                TextureImage(selIndex, p, s, 6); ImGui::Dummy(ImVec2(s, s));
                ImGui::Text("%s", name.c_str()); Hint("%d x %d pixels%s", im.w, im.h, im.transparent ? ", see-through parts" : "");
                if (ImGui::Button("Make a block")) MakeBlockFrom(name, ImportAs::Blocks);
                if (FlowButton("Cross plane")) MakeBlockFrom(name, ImportAs::CrossPlanes);
                if (FlowButton("Flat plane")) MakeBlockFrom(name, ImportAs::FlatPlanes);
                if (ImGui::Button("Delete texture...")) { deleteTexAsk = name; wantPopup = "Delete texture?"; }
            } else Hint("Pick something in the Outliner or the Assets.");
            break;
        case Sel::Shape:
            if (selIndex > 0 && selIndex < Shapes::get().count()) {
                const ShapeInfo& si = Shapes::get().info(selIndex); title("Shape");
                BlockId held = reg.has(hotbar[hotSel]) ? hotbar[hotSel] : reg.find("stone");
                float s = std::min(ImGui::GetContentRegionAvail().x, 160 * ui); ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + s, p.y + s), IM_COL32(30, 32, 38, 255), 4.f);
                if (held) ShapeIcon(ImGui::GetWindowDrawList(), reg.get(held), makeBlock(typeOf(held), selIndex), ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), s * 0.75f);
                ImGui::Dummy(ImVec2(s, s));
                ImGui::Text("%s", si.name.c_str()); Hint("Folder: %s.  Every shape turns 24 ways (R, T in the viewport).", si.category.c_str());
                if (reg.has(hotbar[hotSel]) && ImGui::Button("Build with this shape")) { hotbar[hotSel] = makeBlock(typeOf(hotbar[hotSel]), selIndex); manualRot = false; }
            }
            break;
        case Sel::Tool:
            if (!toolMode) { Hint("Press T in the viewport for the tools."); break; }
            ImGui::SeparatorText(toolSel == 0 ? "Terrain brush" : toolSel == 1 ? "Tools" : toolSel == 2 ? "Objects" : "Tools");
            if (toolSel == 0) BrushBody();
            else if (toolSel == 1) SelectionInspector();
            else if (toolSel == 2) { Hint("Click an object in the viewport to pick it. Pick a model in Assets > Models to place it."); if (!placing.empty()) Hint("Placing: %s. R turns it, Alt puts it off the grid, right click stops.", placing.c_str()); }
            else Hint("More tools are coming to this slot.");
            break;
        case Sel::Object: ObjectInspector(); break;
        case Sel::Selection: SelectionInspector(); break;
        case Sel::Model: ModelInspector(); break;
        default: Hint("Pick something in the Outliner or the Assets."); break;
        }
        ImGui::End();
    }
    // ---- Assets: blocks by folder, textures, shapes; thumbnails, search, size
    void Assets() {
        if (focusAssets) { ImGui::SetNextWindowFocus(); focusAssets = false; }
        ImGui::Begin("Assets");
        float ui = settings.uiScale;
        // folder tree on the left
        ImGui::BeginChild("folders", ImVec2(190 * ui, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        auto folder = [&](const char* label, int kind, const std::string& cat, int count) {
            ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth | (assetKind == kind && assetCat == cat ? ImGuiTreeNodeFlags_Selected : 0);
            ImGui::TreeNodeEx((std::to_string(kind) + label).c_str(), f, "%s  (%d)", label, count);
            if (ImGui::IsItemClicked()) { assetKind = kind; assetCat = cat; }
        };
        int nb = 0; for (BlockId id = 1; id < reg.count(); id++) nb += reg.has(id);
        if (ImGui::TreeNodeEx("Blocks", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
            folder("All blocks", 0, "", nb);
            for (const std::string& c : BlockCategories()) { int n = 0; for (BlockId id = 1; id < reg.count(); id++) n += reg.has(id) && reg.get(id).category == c; folder(c.c_str(), 0, c, n); }
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Textures", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) { folder("All textures", 1, "", (int)reg.textureCount() - 1); ImGui::TreePop(); }
        if (ImGui::TreeNodeEx("Models", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) { folder("All models", 3, "", (int)models.models.size()); ImGui::TreePop(); }
        if (ImGui::TreeNodeEx("Shapes", ImGuiTreeNodeFlags_SpanAvailWidth)) {
            const Shapes& S = Shapes::get(); folder("All shapes", 2, "", S.count() - 1);
            for (const std::string& c : S.categories()) { int n = 0; for (int i = 1; i < S.count(); i++) n += S.info(i).category == c; folder(c.c_str(), 2, c, n); }
            ImGui::TreePop();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        // grid on the right
        ImGui::BeginGroup();
        ImGui::SetNextItemWidth(220 * ui); ImGui::InputTextWithHint("##search", "Search...", &assetSearch);
        ImGui::SameLine(); ImGui::SetNextItemWidth(120 * ui); ImGui::SliderFloat("##size", &assetSize, 36, 128, "size %.0f");
        ImGui::SameLine(); if (ImGui::Button("Import images...")) { auto f = PickImages(); if (!f.empty()) { importFiles = f; wantPopup = "Import images"; } }
        ImGui::SameLine(); if (ImGui::Button("New block")) { NewBlock(); if (!assetCat.empty() && assetKind == 0) draft.category = assetCat; sel = Sel::Block; selIndex = 0; }
        ImGui::SameLine(); ImGui::TextDisabled(assetKind == 0 ? "Click: build with it.  Right click: more." : assetKind == 1 ? "Click: look at it.  Right click: more." : assetKind == 3 ? "Click: place it in the world." : "Click: give the held block this shape.");
        ImGui::BeginChild("grid", ImVec2(0, 0), ImGuiChildFlags_Borders);
        float s = assetSize * ui, labelH = ImGui::GetTextLineHeight() + 2, sp = ImGui::GetStyle().ItemSpacing.x;
        int cols = std::max(1, (int)((ImGui::GetContentRegionAvail().x + sp) / (s + sp))), n = 0;
        std::string q = Lower(assetSearch);
        auto cell = [&](const char* id, const std::string& name, bool selected, ImVec2& p) {   // a tile with its name under it
            if (n % cols) ImGui::SameLine(); n++;
            ImGui::BeginGroup(); bool c = Tile(id, p, s, selected);
            std::string lab = name; while (lab.size() > 1 && ImGui::CalcTextSize(lab.c_str()).x > s) lab.pop_back();
            if (lab.size() < name.size() && lab.size() > 2) lab = lab.substr(0, lab.size() - 2) + "..";
            ImGui::TextUnformatted(lab.c_str()); ImGui::EndGroup(); (void)labelH;
            return c;
        };
        if (assetKind == 0) {
            for (BlockId id = 1; id < reg.count(); id++) {
                if (!reg.has(id)) continue; const BlockDef& d = reg.get(id);
                if (!assetCat.empty() && d.category != assetCat) continue;
                if (!q.empty() && Lower(d.name).find(q) == std::string::npos) continue;
                ImGui::PushID((int)id); ImVec2 p; bool hot = typeOf(hotbar[hotSel]) == id;
                if (cell("b", d.name, sel == Sel::Block && selIndex == (int)id, p) && !ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
                    hotbar[hotSel] = shapeOf(hotbar[hotSel]) && hot ? hotbar[hotSel] : id; Select(Sel::Block, (int)id);
                }
                if (hot) ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + s - 7, p.y + 7), 4, IM_COL32(120, 220, 255, 255));
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nFolder: %s", d.name.c_str(), d.category.c_str());
                BlockIcon(ImGui::GetWindowDrawList(), id, ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), s * 0.7f);
                if (ImGui::BeginPopupContextItem("ctx")) {
                    if (ImGui::MenuItem("Build with it")) hotbar[hotSel] = id;
                    if (ImGui::MenuItem("Edit")) Select(Sel::Block, (int)id);
                    if (ImGui::MenuItem("Duplicate")) { BlockDef c = d; c.name = reg.uniqueName(d.name + " copy"); BlockId nid = AIR; ChangeBlocks([&] { nid = reg.add(c); }); library.saveBlocks(reg); if (nid) Select(Sel::Block, (int)nid); }
                    if (ImGui::BeginMenu("Move to folder")) {
                        for (const std::string& c : BlockCategories()) if (ImGui::MenuItem(c.c_str(), nullptr, c == d.category)) { BlockDef nd = d; nd.category = c; ChangeBlocks([&] { reg.put(id, nd); }); library.saveBlocks(reg); editorOpen = false; }
                        ImGui::EndMenu();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete...")) { deleteBlockAsk = id; wantPopup = "Delete block?"; }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
        } else if (assetKind == 1) {
            for (int t = 1; t < (int)reg.textureCount(); t++) {
                const TexImage& im = reg.image(t);
                if (!q.empty() && Lower(im.name).find(q) == std::string::npos) continue;
                ImGui::PushID(t); ImVec2 p; std::string name = im.name;
                if (cell("t", name, sel == Sel::Texture && selIndex == t, p)) Select(Sel::Texture, t);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s  (%d x %d%s)", name.c_str(), im.w, im.h, im.transparent ? ", see-through" : "");
                TextureImage(t, p, s, 4);
                if (ImGui::BeginPopupContextItem("ctx")) {
                    if (ImGui::MenuItem("Make a block")) MakeBlockFrom(name, ImportAs::Blocks);
                    if (ImGui::MenuItem("Make a cross plane (like flowers)")) MakeBlockFrom(name, ImportAs::CrossPlanes);
                    if (ImGui::MenuItem("Make a flat plane (like ladders)")) MakeBlockFrom(name, ImportAs::FlatPlanes);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete texture...")) { deleteTexAsk = name; wantPopup = "Delete texture?"; }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
        } else if (assetKind == 3) {
            int mi = 0;
            for (auto& kv : models.models) {
                const BlockModel& m = kv.second; mi++;
                if (!q.empty() && Lower(m.name).find(q) == std::string::npos) continue;
                ImGui::PushID(mi); ImVec2 p;
                if (cell("m", m.name, sel == Sel::Model && selModel == m.name, p) && !ImGui::IsMouseReleased(ImGuiMouseButton_Right)) { sel = Sel::Model; selModel = m.name; StartPlacing(m.name); sel = Sel::Model; }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%d x %d x %d blocks\nClick to place it", m.name.c_str(), m.sx, m.sy, m.sz);
                ObjectIcon(ImGui::GetWindowDrawList(), m, ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), s * 0.8f);
                if (ImGui::BeginPopupContextItem("ctx")) {
                    if (ImGui::MenuItem("Place it")) StartPlacing(m.name);
                    if (ImGui::MenuItem("Show in the Inspector")) { sel = Sel::Model; selModel = m.name; }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
        } else {
            const Shapes& S = Shapes::get(); BlockId held = reg.has(hotbar[hotSel]) ? hotbar[hotSel] : reg.find("stone");
            int cur = held ? resolveShape(held, reg.get(held)) : 0;
            for (int id = 1; id < S.count() && held; id++) {
                const ShapeInfo& si = S.info(id);
                if (!assetCat.empty() && si.category != assetCat && q.empty()) continue;
                if (!q.empty() && Lower(si.name).find(q) == std::string::npos) continue;
                ImGui::PushID(id); ImVec2 p;
                if (cell("s", si.name, id == cur, p)) { if (reg.has(hotbar[hotSel])) hotbar[hotSel] = makeBlock(typeOf(hotbar[hotSel]), id); manualRot = false; Select(Sel::Shape, id); }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", si.name.c_str());
                ShapeIcon(ImGui::GetWindowDrawList(), reg.get(held), makeBlock(typeOf(held), id), ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), s * 0.72f);
                ImGui::PopID();
            }
        }
        if (!n) ImGui::TextDisabled(q.empty() ? "Nothing here yet." : "Nothing is called that.");
        ImGui::EndChild();
        ImGui::EndGroup();
        ImGui::End();
    }
    // ---- the viewport: the 3D picture inside a panel
    void Viewport() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (focusView) { ImGui::SetNextWindowFocus(); focusView = false; }
        ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImVec2 avail = ImGui::GetContentRegionAvail(), p = ImGui::GetCursorScreenPos();
        viewSize = avail; vx = p.x; vy = p.y;
        ImGui::Image((ImTextureID)(intptr_t)sceneSRV.Get(), ImVec2(std::max(1.f, avail.x), std::max(1.f, avail.y)));
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !captured) captureNext = true;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(p, ImVec2(p.x + avail.x, p.y + avail.y), true);
        DrawHud(dl);
        dl->PopClipRect();
        ImGui::End();
        ImGui::PopStyleVar();
    }
    void EditorUI() {
        MenuBar();
        Toolbar();
        ImGuiID dock = ImGui::GetID("MainDock");
        if (resetLayout || !ImGui::DockBuilderGetNode(dock)) { BuildLayout(dock); resetLayout = false; }
        ImGui::DockSpaceOverViewport(dock, ImGui::GetMainViewport());
        Outliner();
        Inspector();
        Assets();
        Viewport();
    }

    // ---- questions (import, delete)
    void Popups() {
        if (wantPopup) { ImGui::OpenPopup(wantPopup); wantPopup = nullptr; }
        float ui = settings.uiScale;
        auto buttons = [&](const char* yes, bool enabled) {   // [yes] [Cancel]; true when yes was clicked
            ImGui::BeginDisabled(!enabled); bool y = ImGui::Button(yes, ImVec2(140 * ui, 0)) || (enabled && ImGui::IsKeyPressed(ImGuiKey_Enter)); ImGui::EndDisabled();
            ImGui::SameLine(); if (ImGui::Button("Cancel", ImVec2(140 * ui, 0))) ImGui::CloseCurrentPopup();
            if (y) ImGui::CloseCurrentPopup(); return y;
        };
        const char* worldTypes[] = { "Flat: grass, dirt, stone", "Flat: stone", "Flat: sand", "Void: empty (one block to start)" };
        if (ImGui::BeginPopupModal("New project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(300 * ui); ImGui::InputText("Name", &newProjName);
            ImGui::SetNextItemWidth(300 * ui); ImGui::Combo("First world", &newWorldType, worldTypes, 4);
            ImGui::TextDisabled("It starts with the starter blocks and textures. You can change them all.");
            if (buttons("Create", !newProjName.empty())) {
                std::string dir = Projects::create(ProjectsRoot(), newProjName); int t = newWorldType;
                pending = [this, dir, t] { OpenProject(dir, t); };
            }
            ImGui::EndPopup();
        }
        bool projOk = projSel >= 0 && projSel < (int)projects.size();
        if (ImGui::BeginPopupModal("Rename project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(300 * ui); ImGui::InputText("Name", &newProjName);
            if (buttons("Rename", !newProjName.empty() && projOk)) {
                GameSettings g; std::string f = projects[projSel].folder + "/game.txt"; g.load(f); g.name = newProjName; g.save(f); RefreshProjects();
            }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Delete project?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (projOk) {
                ImGui::Text("Delete the game \"%s\" for good?", projects[projSel].name.c_str());
                ImGui::TextDisabled("Its folder is deleted: all its worlds, blocks and textures.");
                if (buttons("Delete", true)) { Projects::remove(projects[projSel].folder); RefreshProjects(); }
            } else ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("New world", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(300 * ui); ImGui::InputText("Name", &newName);
            ImGui::SetNextItemWidth(300 * ui); ImGui::Combo("World type", &newType, worldTypes, 4);
            ImGui::TextDisabled("The world has no size limit. It loads around you as you move.\nThe world you have open now is saved first.");
            if (buttons("Create", !newName.empty())) { std::string n = newName; int t = newType; pending = [this, n, t] { CreateWorld(n, t); }; }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Rename world", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(300 * ui); ImGui::InputText("Name", &newName);
            if (buttons("Rename", !newName.empty() && newName != renameFrom)) { std::string a = renameFrom, b = newName; pending = [this, a, b] { RenameWorld(a, b); }; }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Delete world?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Delete the world \"%s\" for good?", deleteAsk.c_str());
            if (buttons("Delete", true)) DeleteWorld(deleteAsk);
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Import images", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%d image%s:", (int)importFiles.size(), importFiles.size() == 1 ? "" : "s");
            for (size_t i = 0; i < importFiles.size() && i < 8; i++) ImGui::BulletText("%s", fs::path(importFiles[i]).filename().string().c_str());
            if (importFiles.size() > 8) ImGui::TextDisabled("... and %d more", (int)importFiles.size() - 8);
            ImGui::Spacing(); ImGui::Text("Make them into:");
            ImGui::RadioButton("Blocks", &importAs, (int)ImportAs::Blocks);
            ImGui::RadioButton("Cross planes (like flowers and tall grass)", &importAs, (int)ImportAs::CrossPlanes);
            ImGui::RadioButton("Flat planes (stick to the side you click: ladders, vines, posters)", &importAs, (int)ImportAs::FlatPlanes);
            ImGui::RadioButton("Just textures (to use in the block editor)", &importAs, (int)ImportAs::TexturesOnly);
            ImGui::TextDisabled("Pictures with transparent pixels become see-through by themselves.\nA texture with the same name is replaced, and its blocks update.");
            if (ImGui::Button("Import", ImVec2(140 * ui, 0))) {
                Library::ImportResult res;
                ChangeBlocks([&] { res = library.import(importFiles, (ImportAs)importAs, reg); });
                if (!res.blocks.empty() && screen == Screen::Game) hotbar[hotSel] = res.blocks[0];
                std::string msg = std::to_string(res.textures.size()) + " imported";
                if (!res.blocks.empty()) msg += ", " + std::to_string(res.blocks.size()) + " new block" + (res.blocks.size() == 1 ? "" : "s");
                if (!res.failed.empty()) msg += ". Could not read: " + res.failed[0] + (res.failed.size() > 1 ? " and " + std::to_string(res.failed.size() - 1) + " more (PNG only)" : " (PNG only)");
                Toast(msg); importFiles.clear(); ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(); if (ImGui::Button("Cancel", ImVec2(140 * ui, 0))) { importFiles.clear(); ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Delete texture?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Delete the texture \"%s\"?", deleteTexAsk.c_str());
            ImGui::TextDisabled("Its PNG file is deleted. Blocks using it turn pink and black until you pick another texture.");
            if (ImGui::Button("Delete", ImVec2(140 * ui, 0))) { ChangeBlocks([&] { library.deleteTexture(deleteTexAsk, reg); }); Toast("Deleted " + deleteTexAsk); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine(); if (ImGui::Button("Cancel", ImVec2(140 * ui, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Delete block?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Delete the block \"%s\"?", reg.get(deleteBlockAsk).name.c_str());
            ImGui::TextDisabled("Where it is already placed in worlds, it turns into a pink and black \"missing block\".");
            if (ImGui::Button("Delete", ImVec2(140 * ui, 0))) {
                std::string nm = reg.get(deleteBlockAsk).name;
                ChangeBlocks([&] { reg.remove(deleteBlockAsk); }); library.saveBlocks(reg);
                editorOpen = false; Toast("Deleted the block " + nm); ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(); if (ImGui::Button("Cancel", ImVec2(140 * ui, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    // ================================================================== screenshot mode (--screenshots <folder>)
    std::string shotDir;
    void SaveShot(const std::string& path) {
        ComPtr<ID3D11Texture2D> bb; HR(swap->GetBuffer(0, IID_PPV_ARGS(&bb)));
        D3D11_TEXTURE2D_DESC d; bb->GetDesc(&d); d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> st; HR(dev->CreateTexture2D(&d, nullptr, &st)); ctx->CopyResource(st.Get(), bb.Get());
        D3D11_MAPPED_SUBRESOURCE m; HR(ctx->Map(st.Get(), 0, D3D11_MAP_READ, 0, &m));
        std::vector<uint8_t> px((size_t)d.Width * d.Height * 4);
        for (UINT y = 0; y < d.Height; y++) { memcpy(&px[(size_t)y * d.Width * 4], (const uint8_t*)m.pData + (size_t)y * m.RowPitch, d.Width * 4); for (UINT x = 0; x < d.Width; x++) px[((size_t)y * d.Width + x) * 4 + 3] = 255; }
        ctx->Unmap(st.Get(), 0);
        double sum = 0; for (size_t i = 0; i < px.size(); i += 4) sum += px[i] + px[i + 1] + px[i + 2];
        char b[96]; snprintf(b, sizeof b, "  average brightness %.1f of 255", sum / (px.size() / 4 * 3.0)); Log(b);
        if (!writePng(path, (int)d.Width, (int)d.Height, px)) Log("could not write " + path);
    }
    void RunScreenshots() {
        auto t0 = std::chrono::steady_clock::now();
        Log("PRIMEngine screenshots, drawing with: " + driverName + ", " + std::to_string(cw) + "x" + std::to_string(ch));
        settings = Settings{}; settings.renderDistance = 7; settings.verticalDistance = 3; settings.blockPreview = false; cloudTime = 37;
        plainView = true; ImGui::GetIO().IniFilename = nullptr;   // just the 3D picture; the editor gets its own picture at the end, in the default layout
        std::error_code ec;
        std::string shotProject = Projects::create(ProjectsRoot(), "Screenshot Game");
        OpenProject(shotProject);   // makes "Level 1"
        {   // a few coloured lamps for the pictures (only in memory, not saved to your blocks)
            BlockId lamp = reg.find("lamp");
            if (lamp) for (auto c : { std::make_pair("blue lamp", 0x3070FFu), std::make_pair("red lamp", 0xFF2A20u), std::make_pair("green lamp", 0x40FF60u) }) {
                BlockDef d = reg.get(lamp); d.name = c.first; d.lightColor = c.second; if (!reg.find(c.first)) reg.add(d);
            }
            BlockId metal = reg.find("metal");
            if (metal && !reg.find("mirror")) { BlockDef d = reg.get(metal); d.name = "mirror"; d.mirror = true; d.shine = 1; reg.add(d); }
        }
        CreateWorld("Screenshot demo", 0); Capture(false);
        buildDemo(*world, reg);
        int n = 0;
        auto waitLoaded = [&]() {
            if (!world) return 0;
            I3 pc = chunkOf(ifloor(player.feet.x), ifloor(player.feet.y), ifloor(player.feet.z)); int frames = 0;
            for (; frames < 3000; frames++) {   // wait until everything around is loaded and meshed
                size_t before = world->loadedCount(); world->stream(pc, settings.renderDistance, settings.verticalDistance, 64);
                ScheduleMeshing(pc); CollectMeshes();
                if (frames > 4 && world->loadedCount() == before && world->dirty.empty() && workers->pending() == 0) break;
                Sleep(2);
            }
            return frames;
        };
        std::vector<DemoShot> shots = demoShots();
        for (const char* nm : { "ambient_occlusion_off", "ambient_occlusion_on" }) { DemoShot d = shots[2]; d.name = nm; shots.push_back(d); }   // the same view without and with it
        for (const DemoShot& sh : shots) {
            settings.ambientOcclusion = sh.name == "ambient_occlusion_off" ? 0 : 1;
            V3 dlt = sh.target - sh.eye; double hz = std::sqrt(dlt.x * dlt.x + dlt.z * dlt.z);
            player.feet = { sh.eye.x, sh.eye.y - Player::EYE, sh.eye.z }; player.yaw = std::atan2(dlt.x, dlt.z); player.pitch = std::atan2(dlt.y, hz); player.vel = { 0, 0, 0 };
            settings.timeOfDay = sh.hour;
            for (int k = 0; k < sh.liquidTicks; k++) world->tickLiquids();   // let water and lava flow a bit before this picture
            info.environment = sh.env; info.zones.clear(); envState.update(0, player.eye(), info.environment, info.zones, true);
            { int wi = findWeather(sh.weather); weather.set(wi < 0 ? 1 : wi, true); weather.autoChange = false;
              weather.wetness = weather.now.rain > 0.1f ? 1.f : 0.f; weather.snowCover = weather.now.snow > 0.1f ? 1.f : 0.f; weather.flash = 0; weather.boltAge = 99;
              if (sh.lightning) {   // a bolt right in view
                  V3 dl = sh.target - sh.eye; double l = std::sqrt(dl.x * dl.x + dl.z * dl.z);
                  weather.boltPos = { sh.eye.x + dl.x / l * 120 + 25, 0, sh.eye.z + dl.z / l * 120 }; weather.boltAge = 0.03; weather.boltSeed = 12345; weather.flash = 0.7f;
              } }
            int frames = waitLoaded();
            auto s0 = std::chrono::steady_clock::now();
            RenderWorld(backRTV.Get());
            char name[256]; snprintf(name, sizeof name, "%s/%02d_%s.png", shotDir.c_str(), ++n, sh.name.c_str());
            SaveShot(std::string(name));
            swap->Present(0, 0);
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s0).count();
            char line[256]; snprintf(line, sizeof line, "%s: %zu chunks with meshes, %d drawn, %d waiting frames, frame %.0f ms", name, gpu.size(), drawnChunks, frames, ms); Log(line);
        }
        {   // the editor around the world, then Play mode's HUD
            auto poseAt = [&](const DemoShot& sh) {
                V3 dlt = sh.target - sh.eye; double hz = std::sqrt(dlt.x * dlt.x + dlt.z * dlt.z);
                player.feet = { sh.eye.x, sh.eye.y - Player::EYE, sh.eye.z }; player.yaw = std::atan2(dlt.x, dlt.z); player.pitch = std::atan2(dlt.y, hz); player.vel = { 0, 0, 0 };
                settings.timeOfDay = sh.hour; info.environment = "temperate"; envState.update(0, player.eye(), info.environment, info.zones, true);
                weather.set(findWeather("partly_cloudy"), true); weather.autoChange = false; weather.wetness = 0; weather.snowCover = 0; weather.flash = 0; weather.boltAge = 99;
            };
            auto frames = [&](int k, const char* name) {
                for (int f = 0; f < k; f++) { FrameBody(1.0 / 60); if (f + 1 < k) { swap->Present(0, 0); retired.clear(); } waitLoaded(); }
                char path[256]; snprintf(path, sizeof path, "%s/%02d_%s.png", shotDir.c_str(), ++n, name);
                SaveShot(std::string(path)); swap->Present(0, 0); retired.clear(); Log(std::string(path));
            };
            info.zones = { { "Space station", "space", { 9, 0, 9 }, { 14, 5, 14 } } };
            info.hasSpawn = true; info.spawn = { -3.5, 0, -5.5 }; info.spawnYaw = 0.6;
            game.name = "Screenshot Game";
            plainView = false; poseAt(demoShots()[0]);
            Select(Sel::Block, (int)reg.find("lamp")); assetKind = 0; assetCat.clear(); toastT = 0;
            frames(10, "editor");
            Select(Sel::Game); assetKind = 2; assetCat = "Slopes";
            frames(3, "editor_game_settings");
            {   // objects: build a little console from blocks, make it into an object, place copies (turned, bigger)
                auto ground = [&](int x, int z) { for (int y = 40; y > -40; y--) { BlockId v = world->get(x, y, z); if (v != AIR && !reg.get(v).liquid) return y; } return 0; };
                int gy = ground(36, 24) + 1;
                BlockId cob = reg.find("cobblestone"), met = reg.find("metal"), lmp = reg.find("lamp"), gls = reg.find("glass"), brk = reg.find("bricks");
                for (int x = 35; x <= 37; x++) for (int z = 24; z <= 25; z++) world->set(x, gy, z, cob);
                for (int x = 35; x <= 37; x++) { world->set(x, gy + 1, 25, met); world->set(x, gy + 2, 25, met); }
                world->set(36, gy + 1, 24, lmp); world->set(35, gy + 3, 25, gls); world->set(37, gy + 3, 25, gls);
                selA = { 34, gy, 23 }; selB = { 38, gy + 4, 26 }; selStage = 2; newModelName = "Console";
                MakeIntoObject("Console", true);
                WorldObject a = objects.back(); a.id = nextObjId++; a.name = UniqueObjectName("Console"); a.pos = { a.pos.x - 6, a.pos.y, a.pos.z + 1 }; a.yaw = 90; objects.push_back(a);
                WorldObject b = objects.back(); b.id = nextObjId++; b.name = "Big console"; b.folder = "Big things"; b.pos = { 45.5, (double)gy, 28 }; b.yaw = -30; b.scale = 2; objects.push_back(b);
                RebuildSolids();
                for (int y = gy; y < gy + 3; y++) for (int x = 30; x <= 31; x++) world->set(x, y, 30, brk);
                toolMode = true; toolSel = 2; sel = Sel::Object; selIndex = (int)objects.size() - 1; placing.clear(); assetKind = 3; assetCat.clear();
                poseAt(DemoShot{ "o", { 36.5, gy + 6.0, 13 }, { 39, gy + 1.5, 27 }, 14.0f });
                frames(6, "objects");
                toolSel = 1; selA = { 30, gy, 30 }; selB = { 31, gy + 2, 30 }; selStage = 2; sel = Sel::Selection; newModelName = "Brick pillar";
                frames(3, "selection_box");
                selStage = 0; toolMode = false; sel = Sel::World;
            }
            {   // tools: sculpt a little landscape with the terrain brush, then show it with the brush options open
                auto ground = [&](int x, int z) { for (int y = 40; y > -40; y--) { BlockId v = world->get(x, y, z); if (v != AIR && !reg.get(v).liquid) return y; } return 0; };
                int gy = ground(-34, 26); BrushStroke st;
                auto dab = [&](BrushShape sh, BrushOp op, int r, float strength, I3 c, BlockId mat, int fy, int times) {
                    BrushSettings b; b.shape = sh; b.op = op; b.radius = r; b.height = 3; b.strength = strength;
                    for (int k = 0; k < times; k++) st.apply(*world, brushEdits(*world, b, c, mat, fy, 3 + k));
                };
                dab(BrushShape::Sphere, BrushOp::Raise, 7, 1, { -36, gy, 30 }, AIR, 0, 6);
                dab(BrushShape::Sphere, BrushOp::Smooth, 8, 0.6f, { -36, ground(-36, 30), 30 }, AIR, 0, 2);
                dab(BrushShape::Dome, BrushOp::Paint, 3, 0, { -36, ground(-36, 30), 30 }, reg.find("cobblestone"), 0, 1);
                dab(BrushShape::Sphere, BrushOp::Roughen, 4, 1, { -40, ground(-40, 34), 34 }, reg.find("stone"), 0, 1);
                dab(BrushShape::Sphere, BrushOp::Paint, 6, 0, { -24, gy, 22 }, reg.find("sand"), 0, 1);
                dab(BrushShape::Sphere, BrushOp::Remove, 4, 0, { -24, gy, 22 }, AIR, 0, 1);
                dab(BrushShape::Cylinder, BrushOp::Fill, 4, 0, { -24, gy - 1, 22 }, reg.find("water"), gy - 1, 1);
                history.push(st.finish());
                toolMode = true; toolSel = 0; sel = Sel::Tool; brush = BrushSettings{}; brush.radius = 5;
                poseAt(DemoShot{ "t", { -27.5, gy + 9.0, 9.5 }, { -32, (double)gy, 28 }, 15.5f });
                frames(6, "terrain_tools");
                brushMenu = true; frames(3, "terrain_brush_options");
                brushMenu = false; toolMode = false; sel = Sel::World;
            }
            poseAt(demoShots()[1]);
            StartPlay(false); play.health = game.player.maxHealth * 0.65f; play.hurtFlash = 0.25; toastT = 0;
            frames(4, "play_hud");
            StopPlay();
        }
        {   // the start screen: the project manager
            std::string a = Projects::create(ProjectsRoot(), "Space Pirates"), b = Projects::create(ProjectsRoot(), "Cozy Village");
            for (const char* w : { "Ship", "Asteroid base", "Moon" }) { WorldInfo wi; wi.name = w; wi.save(a + "/worlds/" + w); }
            CloseProject(); projSel = 0;
            for (int f = 0; f < 4; f++) { FrameBody(1.0 / 60); if (f < 3) { swap->Present(0, 0); retired.clear(); } }
            char path[256]; snprintf(path, sizeof path, "%s/%02d_project_manager.png", shotDir.c_str(), ++n);
            SaveShot(std::string(path)); swap->Present(0, 0); retired.clear(); Log(std::string(path));
            Projects::remove(a); Projects::remove(b);
        }
        Projects::remove(shotProject);
        Log("done in " + std::to_string((int)std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()) + " s");
    }

    // ================================================================== frame
    void FrameBody(double dt) {   // everything but showing the picture (screenshots grab it before)
        if (resized) OnResize();
        if (pending) { auto f = std::move(pending); pending = nullptr; f(); }
        bool ev = EditorView();
        int wantW = ev ? (int)viewSize.x : cw, wantH = ev ? (int)viewSize.y : ch;   // the editor draws the world at the viewport panel's size
        wantW = std::max(16, wantW); wantH = std::max(16, wantH);
        if (wantW != rw || wantH != rh) { rw = wantW; rh = wantH; MakeTargets(); }
        if (!ev) vx = vy = 0;
        if (captureNext && screen == Screen::Game) { captureNext = false; Capture(true); }
        if (toastT > 0) toastT -= dt;
        fpsAcc += dt; fpsN++; if (fpsAcc > 0.5) { fps = fpsN / fpsAcc; fpsAcc = 0; fpsN = 0; }
        PollTextures(dt);
        if (screen == Screen::Game) UpdateGame(dt);

        ImGui::GetStyle().FontScaleMain = settings.uiScale;
        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
        ev = EditorView();
        if (screen == Screen::Title) ProjectManager();
        else if (ev) {
            EditorUI();
            if (showF3) DrawF3();
            if (inventory) InventoryScreen();
            if (shapeMenu) ShapeMenu();
            if (brushMenu) BrushMenu();
        } else {
            DrawHud(ImGui::GetForegroundDrawList());
            if (showF3) DrawF3();
            if (paused && !showSettings) PauseMenu();
        }
        if (showSettings) SettingsWindow();
        Popups();
        ImGui::Render();

        const float bg[4] = { 0.10f, 0.11f, 0.13f, 1 };
        if (screen == Screen::Game && rw > 0) RenderWorld(ev ? sceneRTV.Get() : backRTV.Get());
        ctx->OMSetRenderTargets(1, backRTV.GetAddressOf(), nullptr);
        if (screen != Screen::Game || ev) ctx->ClearRenderTargetView(backRTV.Get(), bg);
        D3D11_VIEWPORT v{ 0, 0, (float)cw, (float)ch, 0, 1 }; ctx->RSSetViewports(1, &v);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
    void Frame(double dt) {
        FrameBody(dt);
        swap->Present(settings.vsync ? 1 : 0, 0);
        retired.clear();
    }
};

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return 1;
    App* a = g_app;
    switch (m) {
    case WM_SIZE: if (a && w != SIZE_MINIMIZED) a->resized = true; return 0;
    case WM_INPUT:
        if (a && a->captured) {
            RAWINPUT ri; UINT sz = sizeof ri;
            if (GetRawInputData((HRAWINPUT)l, RID_INPUT, &ri, &sz, sizeof(RAWINPUTHEADER)) != (UINT)-1 && ri.header.dwType == RIM_TYPEMOUSE && !(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                a->mouseDX += ri.data.mouse.lLastX; a->mouseDY += ri.data.mouse.lLastY;
            }
        }
        break;
    case WM_LBUTTONDOWN:   // playing: a click grabs the mouse again (the editor's viewport does this itself)
        if (a && a->playMode && !a->captured && !a->paused && !(ImGui::GetIO().WantCaptureMouse)) a->Capture(true);
        return 0;
    case WM_DROPFILES: if (a) a->OnDrop((HDROP)w); return 0;
    case WM_MOUSEWHEEL: if (a) a->OnWheel(GET_WHEEL_DELTA_WPARAM(w)); return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN: if (a) a->OnKey(w, (l & (1 << 30)) != 0); if (m == WM_SYSKEYDOWN) break; return 0;
    case WM_KILLFOCUS: if (a && a->captured) { a->Capture(false); if (a->playMode) a->paused = true; } return 0;
    case WM_CLOSE: if (a && a->world) a->SaveWorld(); DestroyWindow(h); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR cmd, int) {
    SetProcessDPIAware();
    App app; g_app = &app;
    std::string args = cmd ? cmd : ""; size_t sp = args.find("--screenshots");
    if (sp != std::string::npos) {   // draw the demo scene from a few places, save PNGs, quit
        std::string dir = args.substr(sp + 13); while (!dir.empty() && dir[0] == ' ') dir.erase(0, 1); if (dir.empty()) dir = "screenshots";
        std::error_code ec; fs::create_directories(dir, ec); app.shotDir = fs::absolute(dir).string(); g_shotLog = app.shotDir + "/log.txt";
        std::ofstream(g_shotLog, std::ios::trunc).close();
    }
    WNDCLASSA wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = "PRIMEngineWnd"; RegisterClassA(&wc);
    RECT rc{ 0, 0, app.shotDir.empty() ? 1600 : 1280, app.shotDir.empty() ? 900 : 720 }; AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    app.hwnd = CreateWindowA(wc.lpszClassName, "PRIMEngine", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hi, nullptr);
    RECT cr; GetClientRect(app.hwnd, &cr); app.cw = std::max<int>(2, cr.right); app.ch = std::max<int>(2, cr.bottom);
    app.Init();
    if (!app.shotDir.empty()) { app.RunScreenshots(); ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext(); return 0; }
    auto last = std::chrono::steady_clock::now();
    for (bool run = true; run;) {
        MSG msg; while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) { if (msg.message == WM_QUIT) run = false; TranslateMessage(&msg); DispatchMessageA(&msg); }
        if (!run) break;
        auto now = std::chrono::steady_clock::now(); double dt = std::chrono::duration<double>(now - last).count(); last = now;
        app.Frame(std::min(dt, 0.1));
    }
    if (app.world) app.SaveWorld();
    app.workers.reset();
    ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    return 0;
}
