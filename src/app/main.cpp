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
#include "../game/meshmodel.h"
#include "../game/platform.h"
#include "../game/particles.h"
#include "../game/events.h"
#include "../game/hud.h"
#include "../game/weapons.h"
#include "../game/sound.h"
#include "../game/breakable.h"
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
#include <xaudio2.h>
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
#include <cmath>
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
    float shadow2[4];   // x edge mode (0 sharp, 1 smooth, 2 realistic), y softness
};
struct alignas(16) ChunkCB { float offset[4]; float ax[4] = { 1, 0, 0, 0 }, ay[4] = { 0, 1, 0, 0 }, az[4] = { 0, 0, 1, 0 }; };   // axes: objects are turned and sized

struct GpuChunk { ComPtr<ID3D11Buffer> vbO, ibO, vbC, ibC, vbW, ibW; UINT nO = 0, nC = 0, nW = 0; };
struct ModelGpu { std::vector<std::pair<I3, GpuChunk>> parts; std::vector<Box3> boxes, boxesStill; bool mesh = false; ComPtr<ID3D11ShaderResourceView> tex; };   // boxesStill: without the moving parts (an open door)
struct alignas(16) PreviewCB { float vp[16]; float light[4]; };   // a block model's meshes (16^3 pieces) and collision

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
    bool focusEvents = false; bool captured = false, paused = false, inventory = false, showF3 = false, hideHud = false, showSettings = false;
    double mouseDX = 0, mouseDY = 0, lastSpace = -10, breakCool = 0, placeCool = 0, autosave = 0;
    bool lPrev = true, rPrev = true, mPrev = true;
    bool hoverValid = false; Hit hover{};
    std::string toast; double toastT = 0;
    double fps = 0; int fpsN = 0; double fpsAcc = 0;

    // ---- the editor (G1): docked panels around the viewport, and Play mode
    enum class Sel { None, World, Weather, Environment, Zone, Spawn, Player, Game, Block, Texture, Shape, Tool, Object, Selection, Model };
    // ---- objects (G2)
    ModelLibrary models; std::map<std::string, ModelGpu> modelGpu; std::map<std::string, MeshModel> meshModels;
    // the 3D viewer
    bool viewerOpen = false, viewerFit = true, viewerPeople = true, viewerBoxes = false, viewerBounds = true; std::string viewerModel;
    int viewerClip = -1; float viewerTime = 0; bool viewerLoop = false; ModelGpu viewerAnim;   // an animation playing in the viewer
    float viewerYaw = 0.6f, viewerPitch = 0.35f, viewerDist = 8; ImVec2 viewerWant{ 0, 0 }; int viewerW = 0, viewerH = 0; V3 viewerEye{ 0, 0, 0 }; XMMATRIX viewerVP = XMMatrixIdentity();
    ComPtr<ID3D11RenderTargetView> viewerRTV; ComPtr<ID3D11ShaderResourceView> viewerSRV; ComPtr<ID3D11DepthStencilView> viewerDSV;
    ComPtr<ID3D11VertexShader> vsPreview; ComPtr<ID3D11PixelShader> psPreview; ComPtr<ID3D11Buffer> cbPreview;
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
        b = Compile(src, "VS_Particle", "vs_5_0"); HR(dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vsParticle));
        { D3D11_INPUT_ELEMENT_DESC pe[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 }, { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 2, DXGI_FORMAT_R32G32B32_FLOAT, 0, 52, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
          HR(dev->CreateInputLayout(pe, 5, b->GetBufferPointer(), b->GetBufferSize(), &layoutParticle)); }
        b = Compile(src, "PS_Particle", "ps_5_0"); HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psParticle));
        b = Compile(src, "VS_Field", "vs_5_0"); HR(dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vsField));
        b = Compile(src, "PS_Field", "ps_5_0"); HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psField));
        b = Compile(src, "VS_FieldMesh", "vs_5_0"); HR(dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vsFieldMesh));
        { D3D11_INPUT_ELEMENT_DESC fe[] = { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
          HR(dev->CreateInputLayout(fe, 2, b->GetBufferPointer(), b->GetBufferSize(), &layoutFieldMesh)); }
        b = Compile(src, "PS_FieldMesh", "ps_5_0"); HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psFieldMesh));
        b = Compile(src, "PS_ImpactSphere", "ps_5_0"); HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psSphere));
        b = Compile(src, "PS_BeamOrb", "ps_5_0"); HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psOrb));
        b = Compile(src, "VS_Preview", "vs_5_0"); HR(dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vsPreview));
        b = Compile(src, "PS_Preview", "ps_5_0"); HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psPreview));
        InitHud(src);
        InitAudio();
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
        cb(sizeof(FrameCB), cbFrame); cb(sizeof(ChunkCB), cbChunk); cb(sizeof(PreviewCB), cbPreview); cb(sizeof(LightCB), cbLights);

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
        for (UINT i = 0; i < n; i++) { char path[MAX_PATH]; if (DragQueryFileA(d, i, path, MAX_PATH)) { if (isModelFile(path) && screen == Screen::Game) ImportModelFile(path); else importFiles.push_back(path); } }
        DragFinish(d); SetForegroundWindow(hwnd);
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
        screen = Screen::Game; paused = false; inventory = false; autosave = 0; if (!travelling) playMode = false; sel = Sel::World;
        Toast("Opened " + info.name + ". Click the viewport to fly and build, Esc frees the mouse, F5 plays");
    }
    void SaveWorld() {
        if (!world) return;
        info.player = playMode ? editCam : player; info.hotSel = hotSel; info.weather = weather.type().key; info.autoWeather = weather.autoChange; for (int i = 0; i < 9; i++) info.hotbar[i] = !reg.has(hotbar[i]) ? "" : reg.get(hotbar[i]).name + (shapeOf(hotbar[i]) ? "@" + Shapes::get().info(shapeOf(hotbar[i])).key : "");
        info.save(worldDir);
        { std::vector<Broken> keep = broken; RestoreBroken(); world->saveAll(); for (const Broken& b : keep) world->set(b.x, b.y, b.z, AIR); broken = keep; }   // Play mode: broken blocks are not saved broken
        if (!playMode) { std::vector<std::array<int, 3>> gone; for (auto& kv : breakables.marks) if (world->get(kv.first[0], kv.first[1], kv.first[2]) == AIR && world->loaded(kv.first[0], kv.first[1], kv.first[2])) gone.push_back(kv.first); for (auto& g : gone) breakables.marks.erase(g); }
        { std::error_code e2; if (breakables.marks.empty()) fs::remove(fs::u8path(worldDir + "/breakable.txt"), e2); else saveBreakables(worldDir + "/breakable.txt", breakables); }
        saveObjects(worldDir + "/objects.txt", objects); SaveHud();
        std::error_code ec; if (!events.empty()) saveEvents(worldDir + "/events.txt", events); else fs::remove(worldDir + "/events.txt", ec);
    }
    void CloseWorld() {   // the project stays open
        if (playMode && !travelling) StopPlay();
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
        if (playMode && !typing && !repeat && !paused) {   // play mode keys (before the return below, which keeps the building tools out of play)
            if (k == 'C' && !Down(VK_CONTROL)) { ToggleBall(); return; }                                    // the morph ball
            if (!weapons.empty() && k >= '1' && k <= '9') { if (weapons.switchMode == 0) SwitchBeam((int)(k - '1')); else if (weapons.switchMode == 2) Toast("Pick beams in the menu (Esc)"); return; }   // the beams
            if (k == 'Q' && weapons.missileTypes.size() > 1 && !player.ball) { if (weapons.switchMode == 0) NextMissile(); else if (weapons.switchMode == 2) Toast("Pick missiles in the menu (Esc)"); return; }   // the missiles
        }
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
        if (Playing() && playMode && weapons.beams.size() > 1) {   // Play mode: the next / previous beam you have (only when switching freely)
            if (weapons.switchMode != 0) return;
            int n = (int)weapons.beams.size();
            for (int k = 1; k < n; k++) { int i = ((weapState.current + (delta > 0 ? -k : k)) % n + n) % n; if (OwnsBeam(weapons.beams[i])) { SwitchBeam(i); break; } }
            return;
        }
        if (Playing() && !playMode && toolMode) {
            if (Down(VK_SHIFT)) brush.radius = std::clamp(brush.radius + (delta > 0 ? 1 : -1), 0, 32);
            else toolSel = (toolSel + (delta > 0 ? 8 : 1)) % 9;
            return;
        }
        if (Playing() && !playMode) hotSel = (hotSel + (delta > 0 ? 8 : 1)) % 9; }

    // ================================================================== game update
    void UpdateGame(double dt) {
        player.applySize(game.player);   // the size from the player rules (editor camera too)
        if (Playing()) {
            if (FreeAiming()) {   // free aim: the mouse moves a cursor; near the edges the camera turns
                float ty, tp; freeAim.update(weapons, (float)(mouseDX * settings.mouseSensitivity), (float)(mouseDY * settings.mouseSensitivity), (float)rw, (float)rh, dt, ty, tp);
                player.look(ty * 3.14159265358979 / 180, -tp * 3.14159265358979 / 180);
            } else { double s = 0.0022 * settings.mouseSensitivity; player.look(mouseDX * s, mouseDY * s); }
            Controls c; bool fg = GetForegroundWindow() == hwnd;
            if (fg) { c.forward = Down('W'); c.back = Down('S'); c.left = Down('A'); c.right = Down('D'); c.jump = Down(VK_SPACE); c.sneak = Down(VK_SHIFT); c.sprint = Down(VK_CONTROL) && !Down('Z') && !Down('Y'); }
            if (playMode) { c.sprint = c.sprint && game.player.canSprint; if (play.dead()) c = Controls{}; }
            player.update(*world, c, std::min(dt, 0.05), playMode ? game.player : PlayerRules{});
            if (playMode) hoverValid = false; else if (toolMode) ToolInteract(dt); else Interact(dt);
        } else { lPrev = rPrev = mPrev = true; if (stroking) EndStroke(); }
        toolValid = toolMode && !playMode && world->raycast(player.eye(), player.forward(), 160.0, toolHit);   // where the brush is (shown even with the mouse free)
        if (!paused) UpdatePlatforms(std::min(dt, 0.05));
        if (!paused) UpdateHelpers(std::min(dt, 0.05));
        if (!paused) UpdateParticles(std::min(dt, 0.05));
        if (playMode && !paused) { UpdateBall(std::min(dt, 0.05)); UpdateSpheres(std::min(dt, 0.05)); }
        if (!playMode && !spheres.empty()) spheres.clear();
        UpdateHudSway(std::min(dt, 0.05));
        if (playMode && !paused) UpdateDoors(std::min(dt, 0.05)); else if (!playMode && !doorRt.empty()) { doorRt.clear(); doorGpu.clear(); shots.clear(); RebuildSolids(); }
        if (playMode && !paused) {   // health, lava, falling out of the world, respawning
            PlayState::Event e = play.tick(*world, player, game.player, playSpawn, playYaw, std::min(dt, 0.05));
            if (e == PlayState::Died) { Toast(std::string("You ") + play.cause); Signal(When::Dies, ""); }
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

    // ---- imported models (G2 attempt 2)
    struct FrameRef { bool ok = false; ModelFrame f; explicit operator bool() const { return ok; } const ModelFrame* operator->() const { return &f; } const ModelFrame& operator*() const { return f; } };
    FrameRef ModelNamed(const std::string& n) const {
        if (const BlockModel* b = models.find(n)) return { true, *b };
        auto it = meshModels.find(n); if (it != meshModels.end()) return { true, it->second.frame() };
        return {};
    }
    FrameRef ModelOf(const WorldObject& o) const { return ModelNamed(o.model); }
    bool IsMesh(const std::string& n) const { return meshModels.count(n) > 0; }
    std::vector<std::string> AllModelNames() const {
        std::vector<std::string> v; for (auto& kv : models.models) v.push_back(kv.first); for (auto& kv : meshModels) v.push_back(kv.first);
        std::sort(v.begin(), v.end()); return v;
    }
    std::string UniqueModelName(const std::string& base) const {
        std::string b = base.empty() ? "Model" : base, n = b; int k = 2;
        while (models.find(n) || meshModels.count(n)) n = b + " " + std::to_string(k++);
        return n;
    }
    std::string MeshPath(const std::string& n) const { std::string f; for (char c : n) f += (std::isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_') ? c : '_'; return models.dir + "/" + f + ".pmesh"; }
    void LoadMeshModels() {
        meshModels.clear(); std::error_code ec;
        for (auto& e : fs::directory_iterator(fs::u8path(models.dir), ec)) {
            if (e.path().extension() != ".pmesh") continue;
            MeshModel m; if (m.load(e.path().u8string())) { if (m.name.empty()) m.name = e.path().stem().u8string(); meshModels[m.name] = m; }
        }
        AddBuiltins();
    }
    void SaveMesh(const MeshModel& m) { m.save(MeshPath(m.name)); }
    bool ImportModelFile(const std::string& path) {
        MeshModel m; std::string err;
        if (!importModel(path, m, err)) { Toast("Could not import " + fs::u8path(path).filename().u8string() + ": " + err); return false; }
        m.name = UniqueModelName(m.name); meshModels[m.name] = m; SaveMesh(m); ModelChanged(m.name);
        Toast("Imported " + m.name + ": " + std::to_string(m.triangles()) + " triangles");
        viewerModel = m.name; viewerOpen = true; viewerFit = true; viewerClip = -1; sel = Sel::Model; selModel = m.name; assetKind = 3; assetCat.clear();
        return true;
    }
    std::vector<std::string> PickModels() {
        std::vector<char> buf(1 << 16, 0);
        OPENFILENAMEA o{}; o.lStructSize = sizeof o; o.hwndOwner = hwnd; o.lpstrFilter = "3D models (*.bbmodel, *.obj, *.gltf, *.glb, *.fbx)\0*.bbmodel;*.obj;*.gltf;*.glb;*.fbx\0"; o.lpstrFile = buf.data(); o.nMaxFile = (DWORD)buf.size();
        o.lpstrTitle = "Import 3D models"; o.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        std::vector<std::string> out; if (!GetOpenFileNameA(&o)) return out;
        std::string first = buf.data(); const char* p = buf.data() + first.size() + 1;
        if (!*p) { out.push_back(first); return out; }
        for (; *p; p += strlen(p) + 1) out.push_back(first + "\\" + p);
        return out;
    }
    void ImportModelsDialog() { for (auto& f : PickModels()) ImportModelFile(f); }
    void BuildMeshGpu(ModelGpu& g, const MeshModel& m) {
        g.mesh = true; g.boxes = meshBoxes(m); g.boxesStill = m.parts.empty() ? g.boxes : meshBoxes(m, 24, true);
        UploadMeshVerts(g, m, m.raw);
        D3D11_TEXTURE2D_DESC d{}; d.Width = (UINT)m.tw; d.Height = (UINT)m.th; d.MipLevels = 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; d.SampleDesc.Count = 1; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd{ m.tex.data(), (UINT)m.tw * 4, 0 };
        ComPtr<ID3D11Texture2D> t; if (SUCCEEDED(dev->CreateTexture2D(&d, &sd, &t))) dev->CreateShaderResourceView(t.Get(), nullptr, &g.tex);
    }
    void UploadMeshVerts(ModelGpu& g, const MeshModel& m, const std::vector<MeshVert>& verts) {
        g.parts.clear();
        MeshData md; md.v.reserve(verts.size());
        int none[3] = { 0, 0, 0 };
        for (const MeshVert& v : verts) {
            V3 p = m.blockPos(v);
            md.v.push_back({ (float)p.x, (float)p.y, (float)p.z, v.uv[0], v.uv[1], 16383.f, packLit(1, none, false), packNormal(v.n[0], v.n[1], v.n[2], 0) });
        }
        md.i = m.idx;
        GpuChunk gc; if (m.transparent) Upload(md, gc.vbC, gc.ibC, gc.nC); else Upload(md, gc.vbO, gc.ibO, gc.nO);
        g.parts.push_back({ { 0, 0, 0 }, std::move(gc) });
    }
    // ---- the 3D viewer: the model on a floor grid next to the player and the morph ball, to see how big it is
    void ViewerProject(const XMMATRIX& vp, V3 eye, ImVec2 o, ImVec2 sz, V3 p, ImVec2& out, bool& ok) {
        XMVECTOR c = XMVector4Transform(XMVectorSet((float)(p.x - eye.x), (float)(p.y - eye.y), (float)(p.z - eye.z), 1), vp);
        float w = XMVectorGetW(c); ok = w > 0.05f;
        out = ImVec2(o.x + (XMVectorGetX(c) / w * 0.5f + 0.5f) * sz.x, o.y + (0.5f - XMVectorGetY(c) / w * 0.5f) * sz.y);
    }
    void ModelViewer() {
        if (!viewerOpen) return;
        FrameRef f = ModelNamed(viewerModel); if (!f) { viewerOpen = false; return; }
        float ui = settings.uiScale;
        ImGui::SetNextWindowSize(ImVec2(std::min(720 * ui, (float)cw * 0.8f), std::min(640 * ui, (float)ch * 0.85f)), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(cw * 0.5f, ch * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        if (!ImGui::Begin(("3D viewer: " + viewerModel + "###viewer").c_str(), &viewerOpen)) { ImGui::End(); return; }
        ImVec2 avail = ImGui::GetContentRegionAvail(); float ctrlH = 150 * ui;
        ImVec2 isz(std::max(64.f, avail.x), std::max(64.f, avail.y - ctrlH));
        viewerWant = isz;
        ImVec2 o = ImGui::GetCursorScreenPos();
        if (viewerSRV) ImGui::Image((ImTextureID)(intptr_t)viewerSRV.Get(), isz); else ImGui::Dummy(isz);
        ImGui::SetCursorScreenPos(o); ImGui::InvisibleButton("orbit", isz, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsItemActive()) { viewerYaw += io.MouseDelta.x * 0.01f; viewerPitch = std::clamp(viewerPitch + io.MouseDelta.y * 0.01f, -1.4f, 1.4f); }
        if (ImGui::IsItemHovered() && io.MouseWheel != 0) viewerDist = std::clamp(viewerDist * (io.MouseWheel > 0 ? 0.88f : 1.14f), 1.5f, 200.f);
        if (viewerFit) { viewerDist = (float)std::max({ f->sx, f->sy, f->sz, 2.0 }) * 1.6f + 3; viewerFit = false; }
        // camera
        V3 target{ -0.6, std::max(0.9, f->sy * 0.5), 0 };
        V3 eye{ target.x + std::sin(viewerYaw) * std::cos(viewerPitch) * viewerDist, target.y + std::sin(viewerPitch) * viewerDist, target.z - std::cos(viewerYaw) * std::cos(viewerPitch) * viewerDist };
        viewerEye = eye;
        XMVECTOR fwd = XMVector3Normalize(XMVectorSet((float)(target.x - eye.x), (float)(target.y - eye.y), (float)(target.z - eye.z), 0));
        viewerVP = XMMatrixLookToLH(XMVectorZero(), fwd, XMVectorSet(0, 1, 0, 0)) * XMMatrixPerspectiveFovLH(XMConvertToRadians(45), isz.x / isz.y, 0.05f, 600);
        // overlay: floor grid, size, player and morph ball for scale, collision boxes
        ImDrawList* dl = ImGui::GetWindowDrawList(); dl->PushClipRect(o, ImVec2(o.x + isz.x, o.y + isz.y), true);
        auto L = [&](V3 a, V3 b, ImU32 col, float th = 1.f) { ImVec2 p, q; bool ka, kb; ViewerProject(viewerVP, eye, o, isz, a, p, ka); ViewerProject(viewerVP, eye, o, isz, b, q, kb); if (ka && kb) dl->AddLine(p, q, col, th); };
        auto T = [&](V3 a, ImU32 col, const char* t) { ImVec2 p; bool k; ViewerProject(viewerVP, eye, o, isz, a, p, k); if (k) dl->AddText(p, col, t); };
        int R = (int)std::ceil(std::max({ f->sx, f->sz }) * 0.5) + 4;
        for (int k = -R; k <= R; k++) { ImU32 c = k == 0 ? IM_COL32(255, 255, 255, 70) : IM_COL32(255, 255, 255, 28); L({ (double)k, 0, (double)-R }, { (double)k, 0, (double)R }, c); L({ (double)-R, 0, (double)k }, { (double)R, 0, (double)k }, c); }
        double hx = f->sx * 0.5, hz = f->sz * 0.5;
        V3 lo{ -hx, 0, -hz }, hi{ hx, f->sy, hz };
        if (viewerBounds) { V3 q[8]; for (int i = 0; i < 8; i++) q[i] = { (i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z };
            const int e[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
            for (auto& k : e) L(q[k[0]], q[k[1]], IM_COL32(120, 200, 255, 120)); }
        char b[64];
        snprintf(b, sizeof b, "%.2f", f->sx); T({ 0, 0, lo.z - 0.4 }, IM_COL32(150, 210, 255, 255), b);
        snprintf(b, sizeof b, "%.2f", f->sz); T({ hi.x + 0.3, 0, 0 }, IM_COL32(150, 210, 255, 255), b);
        L({ hi.x + 0.6, 0, lo.z }, { hi.x + 0.6, f->sy, lo.z }, IM_COL32(150, 210, 255, 200), 2);   // height ruler, a tick every block
        for (int k = 0; k <= (int)f->sy; k++) L({ hi.x + 0.45, (double)k, lo.z }, { hi.x + 0.75, (double)k, lo.z }, IM_COL32(150, 210, 255, 200));
        snprintf(b, sizeof b, "%.2f tall", f->sy); T({ hi.x + 0.8, f->sy, lo.z }, IM_COL32(150, 210, 255, 255), b);
        if (viewerPeople) {   // the player and the morph ball, at the sizes in the player rules
            const PlayerRules& pr = game.player; double sw = pr.width / 0.6, sh = pr.height / 1.8, rb = pr.ballSize * 0.5;
            double px = lo.x - 0.5 - std::max(0.5, pr.width * 0.75);   // legs, body, arms, head
            ImU32 pc = IM_COL32(255, 210, 120, 230);
            auto box = [&](V3 a, V3 c) { a = { px + (a.x - px) * sw, a.y * sh, a.z * sw }; c = { px + (c.x - px) * sw, c.y * sh, c.z * sw };
                V3 q[8]; for (int i = 0; i < 8; i++) q[i] = { (i & 1) ? c.x : a.x, (i & 2) ? c.y : a.y, (i & 4) ? c.z : a.z };
                const int e[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
                for (auto& k : e) L(q[k[0]], q[k[1]], pc, 1.5f); };
            box({ px - 0.25, 0, -0.12 }, { px - 0.02, 0.8, 0.12 }); box({ px + 0.02, 0, -0.12 }, { px + 0.25, 0.8, 0.12 });
            box({ px - 0.27, 0.8, -0.15 }, { px + 0.27, 1.4, 0.15 }); box({ px - 0.42, 0.75, -0.1 }, { px - 0.28, 1.4, 0.1 }); box({ px + 0.28, 0.75, -0.1 }, { px + 0.42, 1.4, 0.1 });
            box({ px - 0.2, 1.4, -0.2 }, { px + 0.2, 1.8, 0.2 });
            char pl[48]; snprintf(pl, sizeof pl, "Player %.2f", pr.height); T({ px - 0.4, pr.height + 0.25, 0 }, pc, pl);
            double bx = px - std::max(0.5, pr.width * 0.75) - rb - 0.3; ImU32 bc = IM_COL32(255, 140, 200, 230);   // the morph ball
            for (int k = 0; k < 32; k++) { double a0 = k * 6.2832 / 32, a1 = (k + 1) * 6.2832 / 32;
                L({ bx + std::cos(a0) * rb, rb + std::sin(a0) * rb, 0 }, { bx + std::cos(a1) * rb, rb + std::sin(a1) * rb, 0 }, bc, 1.5f);
                L({ bx, rb + std::sin(a0) * rb, std::cos(a0) * rb }, { bx, rb + std::sin(a1) * rb, std::cos(a1) * rb }, bc, 1.5f);
                L({ bx + std::cos(a0) * rb, rb, std::sin(a0) * rb }, { bx + std::cos(a1) * rb, rb, std::sin(a1) * rb }, bc, 1.5f); }
            char bl[48]; snprintf(bl, sizeof bl, "Morph ball %.2f", pr.ballSize); T({ bx - rb, rb * 2 + 0.25, 0 }, bc, bl);
        }
        if (viewerBoxes) {
            WorldObject vo; vo.model = viewerModel;
            for (const Box3& bx : objectBoxes(vo, *f, GpuModel(viewerModel).boxes)) {
                V3 q[8]; for (int i = 0; i < 8; i++) q[i] = { (i & 1) ? bx.hi.x : bx.lo.x, (i & 2) ? bx.hi.y : bx.lo.y, (i & 4) ? bx.hi.z : bx.lo.z };
                const int e[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
                for (auto& k : e) L(q[k[0]], q[k[1]], IM_COL32(255, 150, 60, 150));
            }
        }
        dl->AddText(ImVec2(o.x + 8, o.y + 6), IM_COL32(200, 200, 210, 200), "Drag to turn, wheel to zoom");
        dl->PopClipRect();
        // settings
        ImGui::Spacing();
        auto it = meshModels.find(viewerModel);
        if (it == meshModels.end()) viewerClip = -1;
        if (it != meshModels.end()) {
            MeshModel& m = it->second; bool ch_ = false;
            Hint("From %s. %d triangles, picture %d x %d.", m.source.c_str(), (int)m.triangles(), m.tw, m.th);
            double hgt = m.sy; Prop("Height (blocks)");
            if (ImGui::InputDouble("##h", &hgt, 0.25, 1, "%.2f") && hgt > 0.01 && m.hi.y > m.lo.y) { m.unitScale = hgt / (m.hi.y - m.lo.y); ch_ = true; }
            double us = m.unitScale; Prop("Blocks per file unit");
            if (ImGui::InputDouble("##us", &us, 0, 0, "%.4f") && us > 1e-5) { m.unitScale = us; ch_ = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Blockbench: 1/16 (16 pixels = 1 block). glTF: 1 (1 metre = 1 block).");
            if (ch_) { m.update(); SaveMesh(m); ModelChanged(m.name); }
            if (!m.clips.empty()) {   // animations from the file (FBX): play them here; doors use them in Play mode
                ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Animations:");
                for (int c = 0; c < (int)m.clips.size(); c++) {
                    ImGui::SameLine(); ImGui::PushID(c);
                    bool on = viewerClip == c;
                    if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                    if (ImGui::Button((m.clips[c].name + " (" + std::to_string((int)std::lround(m.clips[c].length * 1000)) + " ms)").c_str())) { viewerClip = c; viewerTime = 0; }
                    if (on) ImGui::PopStyleColor();
                    ImGui::PopID();
                }
                ImGui::SameLine(); ImGui::Checkbox("Loop", &viewerLoop);
                ImGui::SameLine(); if (ImGui::SmallButton("As imported")) viewerClip = -1;
                if (viewerClip >= (int)m.clips.size()) viewerClip = -1;
                if (viewerClip >= 0) {
                    viewerTime += std::min(io.DeltaTime, 1.f / 30); float L = std::max(0.001f, m.clips[viewerClip].length);
                    if (viewerTime > L) viewerTime = viewerLoop ? std::fmod(viewerTime, L) : L;
                }
                Hint("%d moving parts. They play when the door opens and closes (next step).", (int)m.parts.size());
            }
        } else if (const BlockModel* bm = models.find(viewerModel)) Hint("Made of %d blocks. Block models are always 1 block per block; size objects in the Inspector.", bm->blockCount());
        ImGui::Checkbox("Player and morph ball", &viewerPeople); ImGui::SameLine(); ImGui::Checkbox("Collision boxes", &viewerBoxes); ImGui::SameLine(); ImGui::Checkbox("Size box", &viewerBounds);
        if (ImGui::Button("Place it")) StartPlacing(viewerModel);
        ImGui::SameLine(); if (ImGui::Button("Close")) viewerOpen = false;
        ImGui::End();
    }
    void RenderViewer() {   // into its own picture, with simple light (no world around it)
        if (!viewerOpen || viewerWant.x < 16 || !ModelNamed(viewerModel)) return;
        int w = (int)viewerWant.x, h = (int)viewerWant.y;
        if (w != viewerW || h != viewerH) {
            if (viewerSRV) retired.push_back(viewerSRV);
            viewerW = w; viewerH = h;
            D3D11_TEXTURE2D_DESC d{}; d.Width = (UINT)w; d.Height = (UINT)h; d.MipLevels = 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1; d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            ComPtr<ID3D11Texture2D> t; HR(dev->CreateTexture2D(&d, nullptr, &t)); viewerRTV.Reset(); viewerSRV.Reset();
            HR(dev->CreateRenderTargetView(t.Get(), nullptr, &viewerRTV)); HR(dev->CreateShaderResourceView(t.Get(), nullptr, &viewerSRV));
            d.Format = DXGI_FORMAT_D32_FLOAT; d.BindFlags = D3D11_BIND_DEPTH_STENCIL; ComPtr<ID3D11Texture2D> dt; HR(dev->CreateTexture2D(&d, nullptr, &dt)); viewerDSV.Reset(); HR(dev->CreateDepthStencilView(dt.Get(), nullptr, &viewerDSV));
        }
        const float bg[4] = { 0.36f, 0.40f, 0.46f, 1 };   // a light studio grey-blue, so dark models still stand out
        ctx->OMSetRenderTargets(1, viewerRTV.GetAddressOf(), viewerDSV.Get()); ctx->ClearRenderTargetView(viewerRTV.Get(), bg); ctx->ClearDepthStencilView(viewerDSV.Get(), D3D11_CLEAR_DEPTH, 1, 0);
        D3D11_VIEWPORT v{ 0, 0, (float)w, (float)h, 0, 1 }; ctx->RSSetViewports(1, &v);
        ctx->OMSetBlendState(bsOff.Get(), nullptr, 0xFFFFFFFF); ctx->OMSetDepthStencilState(dsOn.Get(), 0); ctx->RSSetState(rsCull.Get());
        PreviewCB pc{}; XMStoreFloat4x4((XMFLOAT4X4*)pc.vp, viewerVP);   // row_major in the shader, like the world pc.light[0] = -0.45f; pc.light[1] = 0.8f; pc.light[2] = -0.4f;
        ctx->UpdateSubresource(cbPreview.Get(), 0, nullptr, &pc, 0, 0);
        ctx->VSSetConstantBuffers(2, 1, cbPreview.GetAddressOf()); ctx->PSSetConstantBuffers(2, 1, cbPreview.GetAddressOf());
        ctx->VSSetConstantBuffers(1, 1, cbChunk.GetAddressOf());
        ctx->IASetInputLayout(layout.Get()); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vsPreview.Get(), nullptr, 0); ctx->PSSetShader(psPreview.Get(), nullptr, 0);
        ctx->PSSetShaderResources(0, 1, texArray.GetAddressOf()); ctx->PSSetSamplers(0, 1, sampBlocks.GetAddressOf());
        WorldObject vo; vo.model = viewerModel;
        const ModelGpu* use = nullptr;
        auto it = meshModels.find(viewerModel);
        if (viewerClip >= 0 && it != meshModels.end()) {   // the animation, this frame
            const ModelGpu& base = GpuModel(viewerModel);
            UploadMeshVerts(viewerAnim, it->second, it->second.posed(viewerClip, viewerTime)); viewerAnim.mesh = true; viewerAnim.tex = base.tex; use = &viewerAnim;
        }
        DrawObject(vo, 0, viewerEye, use); DrawObject(vo, 1, viewerEye, use);
        ID3D11ShaderResourceView* none[10] = {}; ctx->PSSetShaderResources(0, 10, none);
    }

    // ================================================================== lights and particles (G2 attempt 5)
    struct PVtx { float pos[3], corner[2], col[4], info[4], vel[3]; };   // info: size, turn, shape, streak
    struct alignas(16) LightCB { float count[4]; float pos[32][4], col[32][4], dir[32][4]; };
    std::map<uint32_t, ParticleEmitter> emitters;     // the particle emitters placed in the world, by object id
    std::vector<ParticleEmitter> fx;                  // one-off effects (sparks where a shot hits, a finished trail ...)
    ComPtr<ID3D11VertexShader> vsParticle; ComPtr<ID3D11PixelShader> psParticle; ComPtr<ID3D11InputLayout> layoutParticle;
    ComPtr<ID3D11Buffer> vbParticles, cbLights; UINT vbParticlesCap = 0; int particleCount = 0;
    static V3 ColorOf(uint32_t c) { return { ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0 }; }
    void MakeLight(WorldObject& o) { o.kind = ObjKind::Light; o.collide = Collide::None; if (o.folder == "Props") o.folder = "Lights"; }
    void MakeEmitter(WorldObject& o) { o.kind = ObjKind::Particles; o.collide = Collide::None; if (o.folder == "Props") o.folder = "Particles"; }
    V3 EmitterOrigin(const WorldObject& o) const { return { o.pos.x, o.pos.y + 0.3, o.pos.z }; }
    void SpawnFx(const ParticleSettings& s, V3 at, V3 dir, uint32_t tint = 0) {
        ParticleSettings p = s; p.dir = dir; if (tint) { p.color1 = tint; }
        if (fx.size() > 64) fx.erase(fx.begin());
        fx.emplace_back(p, (uint32_t)(GetTickCount() + fx.size() * 7919)); fx.back().burstNow(p.burst, at, 0);
    }
    void UpdateParticles(double dt) {
        // emitters placed in the world (they run in the editor too, so you see what you make)
        std::set<uint32_t> seen;
        for (const WorldObject& o : objects) {
            if (o.kind != ObjKind::Particles) continue;
            seen.insert(o.id);
            auto it = emitters.find(o.id); if (it == emitters.end()) it = emitters.emplace(o.id, ParticleEmitter(o.particles, o.id * 2654435761u)).first;
            it->second.s = o.particles;   // edits show at once
            it->second.update(dt, EmitterOrigin(o), o.yaw, world.get(), !SwitchedOff(o));
        }
        for (auto it = emitters.begin(); it != emitters.end();) { if (!seen.count(it->first)) it = emitters.erase(it); else ++it; }
        for (ParticleEmitter& e : fx) e.update(dt, { 0, 0, 0 }, 0, world.get(), false);
        fx.erase(std::remove_if(fx.begin(), fx.end(), [](const ParticleEmitter& e) { return e.ps.empty(); }), fx.end());
        for (Shot& s : shots) { V3 from = s.mtype >= 0 ? s.pos - s.dir * (double)s.size : s.pos; s.spin += (float)dt * 5; s.trail.update(dt, from, 0, nullptr, s.age < 50); }   // beam tails (missiles: from the back)
    }
    // every light that shines this frame: light objects, glowing emitters, shots (the nearest 32)
    void CollectLights(LightCB& lc) {
        struct L { V3 p; double range; V3 col; int spot; V3 dir; double cosCone; double dist; };
        std::vector<L> all;
        auto dist = [&](V3 p) { V3 d = p - camPos; return std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z); };
        for (const WorldObject& o : objects) {
            if (SwitchedOff(o)) continue;
            if (o.kind == ObjKind::Light) {
                const LightSettings& s = o.light; double k = s.intensity * flickerAt(s.flicker, animTime, o.id);
                V3 c = ColorOf(s.color); c = { std::pow(c.x, 2.2) * k, std::pow(c.y, 2.2) * k, std::pow(c.z, 2.2) * k };
                double a = o.yaw * 3.14159265358979 / 180, t = s.tilt * 3.14159265358979 / 180;
                V3 d{ std::sin(a) * std::cos(t), -std::sin(t), std::cos(a) * std::cos(t) };
                V3 p{ o.pos.x, o.pos.y + 0.3, o.pos.z };
                all.push_back({ p, std::max(0.5f, s.range), c, s.type == LightType::Spot ? 1 : 0, d, std::cos(s.cone * 3.14159265358979 / 180), dist(p) });
            } else if (o.kind == ObjKind::Particles && o.particles.light) {
                const ParticleSettings& s = o.particles; double k = s.lightIntensity * flickerAt(Flicker::Candle, animTime * 1.3, o.id);
                V3 c0 = ColorOf(s.color0), c1 = ColorOf(s.color1); V3 c{ std::pow((c0.x + c1.x) / 2, 2.2) * k, std::pow((c0.y + c1.y) / 2, 2.2) * k, std::pow((c0.z + c1.z) / 2, 2.2) * k };
                V3 p = EmitterOrigin(o); p.y += 0.4;
                all.push_back({ p, std::max(0.5f, s.lightRange), c, 0, { 0, -1, 0 }, -1, dist(p) });
            }
        }
        for (const Shot& s : shots) if (s.age < 50) all.push_back({ s.pos, s.missile ? 6.0 : 4.5, s.color * 0.6, 0, { 0, -1, 0 }, -1, dist(s.pos) });
        for (const Bomb& b : bombsOut) all.push_back({ b.pos, 2.5, V3{ 1.6, 1.0, 0.3 } * (0.6 + 0.4 * std::sin(animTime * 18)), 0, { 0, -1, 0 }, -1, dist(b.pos) });
        for (const Flash& fl : flashes) all.push_back({ fl.pos, fl.range, fl.col * (1 - fl.t / 0.35), 0, { 0, -1, 0 }, -1, dist(fl.pos) });
        for (const ImpactSphere& sp : spheres) { double a = sp.alphaNow(); if (a > 0.02) all.push_back({ sp.pos, 1.5 + sp.radiusNow() * 2.5, Linear(sp.color) * (3.5 * a), 0, { 0, -1, 0 }, -1, dist(sp.pos) }); }
        if (playMode && weapState.charging && weapState.charge > 0) if (const Beam* b = CurrentBeam()) all.push_back({ chargeAt, 2.0 + 2.5 * weapState.charge, Linear(b->color) * (0.6 + 1.6 * weapState.charge), 0, { 0, -1, 0 }, -1, dist(chargeAt) });
        std::sort(all.begin(), all.end(), [](const L& a, const L& b) { return a.dist - a.range < b.dist - b.range; });
        int n = 0;
        for (const L& l : all) {
            if (n >= 32) break; if (l.dist - l.range > 96) continue;
            V3 r = l.p - camPos;
            float* P = lc.pos[n]; P[0] = (float)r.x; P[1] = (float)r.y; P[2] = (float)r.z; P[3] = (float)l.range;
            float* C = lc.col[n]; C[0] = (float)l.col.x; C[1] = (float)l.col.y; C[2] = (float)l.col.z; C[3] = (float)l.spot;
            float* D = lc.dir[n]; D[0] = (float)l.dir.x; D[1] = (float)l.dir.y; D[2] = (float)l.dir.z; D[3] = (float)l.cosCone;
            n++;
        }
        lc.count[0] = (float)n;
    }
    void BindLights() {
        LightCB lc{}; CollectLights(lc);
        ctx->UpdateSubresource(cbLights.Get(), 0, nullptr, &lc, 0, 0);
        ctx->PSSetConstantBuffers(3, 1, cbLights.GetAddressOf());
    }
    // all particles as quads: glowing ones added on top, soft ones (smoke) blended, back to front
    void DrawParticles() {
        std::vector<PVtx> add, soft; std::vector<std::pair<double, size_t>> order;
        auto push = [&](const ParticleEmitter& e) {
            const ParticleSettings& s = e.s;
            for (const Particle& p : e.ps) {
                ParticleLook l = lookOf(e, p); if (l.a <= 0.003f || l.size <= 0.001f) continue;
                V3 r = l.pos - camPos; float k = s.additive ? s.glow * l.a : 1;
                float col[4] = { l.r * k, l.g * k, l.b * k, s.additive ? 1.f : l.a };
                float streak = (s.shape == PShape::Spark ? 1.f : 0.f) + (s.additive ? 0.f : 2.f);
                std::vector<PVtx>& out = s.additive ? add : soft;
                if (!s.additive) order.push_back({ -(r.x * r.x + r.y * r.y + r.z * r.z), out.size() });
                static const float c[6][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, -1 }, { 1, 1 }, { -1, 1 } };
                for (auto& cc : c) out.push_back({ { (float)r.x, (float)r.y, (float)r.z }, { cc[0], cc[1] }, { col[0], col[1], col[2], col[3] }, { l.size, l.rot, (float)s.shape, streak }, { (float)l.vel.x, (float)l.vel.y, (float)l.vel.z } });
            }
        };
        for (auto& kv : emitters) push(kv.second);
        for (auto& e : fx) push(e);
        if (playMode) push(chargeFx);   // the charge gathering at the muzzle
        for (auto& b : bombsOut) push(b.fx);
        for (auto& s : shots) push(s.trail);
        particleCount = (int)((add.size() + soft.size()) / 6);
        if (add.empty() && soft.empty()) return;
        if (!order.empty()) {   // soft ones: far ones first
            std::sort(order.begin(), order.end());
            std::vector<PVtx> sorted; sorted.reserve(soft.size());
            for (auto& o : order) sorted.insert(sorted.end(), soft.begin() + o.second, soft.begin() + o.second + 6);
            soft.swap(sorted);
        }
        UINT need = (UINT)(add.size() + soft.size());
        if (need > vbParticlesCap) {
            vbParticlesCap = std::max<UINT>(need, vbParticlesCap * 2); vbParticlesCap = std::max<UINT>(vbParticlesCap, 6 * 1024);
            D3D11_BUFFER_DESC d{}; d.ByteWidth = vbParticlesCap * sizeof(PVtx); d.Usage = D3D11_USAGE_DYNAMIC; d.BindFlags = D3D11_BIND_VERTEX_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            vbParticles.Reset(); HR(dev->CreateBuffer(&d, nullptr, &vbParticles));
        }
        D3D11_MAPPED_SUBRESOURCE ms; if (FAILED(ctx->Map(vbParticles.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
        memcpy(ms.pData, add.data(), add.size() * sizeof(PVtx)); memcpy((PVtx*)ms.pData + add.size(), soft.data(), soft.size() * sizeof(PVtx));
        ctx->Unmap(vbParticles.Get(), 0);
        // soft edges where particles meet the world: they read a copy of the depth
        ID3D11RenderTargetView* rt = nullptr; ID3D11DepthStencilView* ds = nullptr; ctx->OMGetRenderTargets(1, &rt, &ds);
        ctx->OMSetRenderTargets(1, &rt, nullptr); ctx->CopyResource(depthCopy.Get(), depthTex.Get()); ctx->OMSetRenderTargets(1, &rt, ds);
        ctx->PSSetShaderResources(2, 1, depthCopySRV.GetAddressOf()); ctx->PSSetShaderResources(3, 1, noiseSRV.GetAddressOf()); ctx->PSSetSamplers(2, 1, sampLinear.GetAddressOf());
        UINT stride = sizeof(PVtx), off = 0; ctx->IASetVertexBuffers(0, 1, vbParticles.GetAddressOf(), &stride, &off); ctx->IASetInputLayout(layoutParticle.Get());
        ctx->VSSetShader(vsParticle.Get(), nullptr, 0); ctx->PSSetShader(psParticle.Get(), nullptr, 0);
        ctx->OMSetDepthStencilState(dsTestOnly.Get(), 0); ctx->RSSetState(rsNoCull.Get());
        if (!add.empty()) { ctx->OMSetBlendState(bsAdd.Get(), nullptr, 0xFFFFFFFF); ctx->Draw((UINT)add.size(), 0); }
        if (!soft.empty()) { ctx->OMSetBlendState(bsAlpha.Get(), nullptr, 0xFFFFFFFF); ctx->Draw((UINT)soft.size(), (UINT)add.size()); }
        ctx->OMSetBlendState(bsOff.Get(), nullptr, 0xFFFFFFFF); ctx->OMSetDepthStencilState(dsOn.Get(), 0); ctx->RSSetState(rsCull.Get());
        ID3D11ShaderResourceView* none2[1] = {}; ctx->PSSetShaderResources(2, 1, none2);
        ctx->IASetInputLayout(layout.Get());
        if (rt) rt->Release(); if (ds) ds->Release();
    }
    void LightInspector(WorldObject& o) {
        LightSettings& L = o.light; ImGui::SeparatorText("Light");
        int t = (int)L.type; const char* tn[] = { "Point (shines all around)", "Spot (a cone)" }; Prop("Kind of light"); ImGui::Combo("##lt", &t, tn, 2); L.type = (LightType)t;
        float c[3] = { ((L.color >> 16) & 255) / 255.f, ((L.color >> 8) & 255) / 255.f, (L.color & 255) / 255.f };
        Prop("Colour"); if (ImGui::ColorEdit3("##lc", c, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel)) L.color = ((uint32_t)std::lround(c[0] * 255) << 16) | ((uint32_t)std::lround(c[1] * 255) << 8) | (uint32_t)std::lround(c[2] * 255);
        Prop("Brightness"); ImGui::SliderFloat("##li", &L.intensity, 0.1f, 10, "%.1f");
        Prop("Reach"); ImGui::SliderFloat("##lr", &L.range, 1, 40, "%.0f blocks");
        if (L.type == LightType::Spot) { Prop("Cone"); ImGui::SliderFloat("##lcone", &L.cone, 5, 80, "%.0f deg"); Prop("Looks down"); ImGui::SliderFloat("##ltilt", &L.tilt, -90, 90, "%.0f deg"); Hint("It points where the object faces (Turn), tilted down by this."); }
        int f = (int)L.flicker; const char* fn[] = { flickerName(Flicker::None), flickerName(Flicker::Candle), flickerName(Flicker::Broken), flickerName(Flicker::Pulse) };
        Prop("Flicker"); ImGui::Combo("##lf", &f, fn, 4); L.flicker = (Flicker)f;
        Hint("Lights shine on blocks and objects as you move (up to 32 near you at once). The bulb is only seen in the editor.");
    }
    void ParticleInspector(WorldObject& o) {
        ParticleSettings& P = o.particles; ImGui::SeparatorText("Particles");
        Prop("Start from");
        if (ImGui::BeginCombo("##preset", "a preset ...")) { for (auto& n : particlePresets()) if (ImGui::Selectable(n.c_str())) { P = particlePreset(n); emitters.erase(o.id); } ImGui::EndCombo(); }
        auto colour = [&](const char* label, uint32_t& v) { float c[3] = { ((v >> 16) & 255) / 255.f, ((v >> 8) & 255) / 255.f, (v & 255) / 255.f };
            Prop(label); ImGui::PushID(label); if (ImGui::ColorEdit3("##c", c, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel)) v = ((uint32_t)std::lround(c[0] * 255) << 16) | ((uint32_t)std::lround(c[1] * 255) << 8) | (uint32_t)std::lround(c[2] * 255); ImGui::PopID(); };
        if (ImGui::CollapsingHeader("Making them", ImGuiTreeNodeFlags_DefaultOpen)) {
            Prop("Per second"); ImGui::SliderFloat("##rate", &P.rate, 0, 500, "%.0f", ImGuiSliderFlags_Logarithmic);
            Prop("Burst of"); ImGui::SliderInt("##burst", &P.burst, 0, 300);
            if (P.burst > 0) { Prop("Burst every"); ImGui::SliderFloat("##be", &P.burstEvery, 0, 10, P.burstEvery <= 0 ? "once" : "%.1f s"); }
            int a = (int)P.area; const char* an[] = { areaName(PArea::Point), areaName(PArea::Sphere), areaName(PArea::Box), areaName(PArea::Disc) };
            Prop("Out of"); ImGui::Combo("##area", &a, an, 4); P.area = (PArea)a;
            if (P.area != PArea::Point) { Prop("Its size"); ImGui::InputScalarN("##asz", ImGuiDataType_Double, &P.areaSize.x, 3, nullptr, nullptr, "%.2f"); }
            Prop("Life"); ImGui::SliderFloat("##life", &P.life, 0.05f, 10, "%.2f s"); Prop("Life varies"); ImGui::SliderFloat("##lifev", &P.lifeVar, 0, 5, "+- %.2f s");
            Prop("Most at once"); ImGui::SliderInt("##max", &P.maxParticles, 10, 10000);
        }
        if (ImGui::CollapsingHeader("Moving", ImGuiTreeNodeFlags_DefaultOpen)) {
            Prop("Direction"); ImGui::InputScalarN("##dir", ImGuiDataType_Double, &P.dir.x, 3, nullptr, nullptr, "%.2f");
            Prop("Spread"); ImGui::SliderFloat("##spread", &P.spread, 0, 180, "%.0f deg");
            Prop("Speed"); ImGui::SliderFloat("##speed", &P.speed, 0, 30, "%.1f blocks/s"); Prop("Speed varies"); ImGui::SliderFloat("##speedv", &P.speedVar, 0, 15, "+- %.1f");
            Prop("Gravity"); ImGui::SliderFloat("##grav", &P.gravity, -30, 10, "%.1f"); Prop("Air drag"); ImGui::SliderFloat("##drag", &P.drag, 0, 5, "%.2f");
            Prop("Spin"); ImGui::SliderFloat("##spin", &P.spin, 0, 3, "%.2f turns/s");
            Prop("Hit blocks"); ImGui::Checkbox("##col", &P.collide); if (P.collide) { ImGui::SameLine(); ImGui::Checkbox("bounce", &P.bounce); }
        }
        if (ImGui::CollapsingHeader("Look", ImGuiTreeNodeFlags_DefaultOpen)) {
            int sh = (int)P.shape; const char* sn[] = { shapeName(PShape::Dot), shapeName(PShape::Spark), shapeName(PShape::Smoke), shapeName(PShape::Pixel), shapeName(PShape::Ring), shapeName(PShape::Flare) };
            Prop("Shape"); ImGui::Combo("##shape", &sh, sn, 6); P.shape = (PShape)sh;
            Prop("Glowing"); ImGui::Checkbox("##add", &P.additive); if (ImGui::IsItemHovered()) ImGui::SetTooltip("On: they glow and add light (sparks, fire).\nOff: soft and see-through (smoke, steam).");
            Prop("Size at start"); ImGui::SliderFloat("##s0", &P.size0, 0, 4, "%.2f"); Prop("Size at end"); ImGui::SliderFloat("##s1", &P.size1, 0, 4, "%.2f");
            colour("Colour at start", P.color0); colour("Colour at end", P.color1);
            Prop("See-through start"); ImGui::SliderFloat("##a0", &P.alpha0, 0, 1, "%.2f"); Prop("See-through end"); ImGui::SliderFloat("##a1", &P.alpha1, 0, 1, "%.2f");
            if (P.additive) { Prop("Brightness"); ImGui::SliderFloat("##glow", &P.glow, 0.1f, 10, "%.1f"); }
        }
        if (ImGui::CollapsingHeader("Light")) {
            Prop("Lights things up"); ImGui::Checkbox("##pl", &P.light);
            if (P.light) { Prop("Reach"); ImGui::SliderFloat("##plr", &P.lightRange, 1, 20, "%.0f blocks"); Prop("Brightness"); ImGui::SliderFloat("##pli", &P.lightIntensity, 0.1f, 6, "%.1f"); }
        }
        if (ImGui::Button("Restart it")) emitters.erase(o.id);
        Hint("%d particles on screen in all.", particleCount);
    }

    // ================================================================== pickups, checkpoints, teleporters, trigger zones (G2 attempt 4)
    PlayInventory inv; std::map<std::string, std::map<uint32_t, float>> takenByWorld;
    std::set<uint32_t> insideNow, firedOnce; uint32_t teleportLock = 0; double animTime = 0;
    std::string bigMsg; float bigMsgT = 0;
    std::string playStartWorld; Player startEditCam; bool travelling = false;
    static bool IsBuiltin(const std::string& n) { return !n.empty() && n[0] == '@'; }
    void AddBuiltins() { for (const std::string& n : builtinShapeNames()) { MeshModel m; if (builtinShape(n, m)) meshModels[n] = m; } MeshModel st; if (builtinShape("@visor-strut", st)) meshModels["@visor-strut"] = st; MeshModel ac; if (builtinShape("@arm-cannon", ac)) meshModels["@arm-cannon"] = ac; MeshModel mb; if (builtinShape("@morph-ball", mb)) meshModels["@morph-ball"] = mb; MeshModel ms; if (builtinShape("@missile", ms)) meshModels["@missile"] = ms; }
    // a new helper object: what it is follows from the built-in look it was placed with
    void ApplyPreset(WorldObject& o) {
        const std::string& m = o.model;
        if (m == "@health" || m == "@key" || m == "@gem") {
            o.kind = ObjKind::Pickup; o.collide = Collide::None; o.folder = "Pickups";
            o.pickup.type = m == "@health" ? PickupType::Health : m == "@key" ? PickupType::Key : PickupType::Collectible;
            o.pickup.name = m == "@key" ? "Red key" : "Gem";
        } else if (m == "@checkpoint") { o.kind = ObjKind::Checkpoint; o.folder = "Checkpoints"; }
        else if (m == "@teleporter") { o.kind = ObjKind::Teleporter; o.folder = "Teleporters"; }
        else if (m == "@zone") { o.kind = ObjKind::Trigger; o.collide = Collide::None; o.folder = "Trigger zones"; o.size = { 3, 3, 3 }; }
        else if (m == "@light") { MakeLight(o); o.folder = "Lights"; }
        else if (m == "@emitter") { MakeEmitter(o); o.folder = "Particles"; o.particles = particlePreset("Fire"); }
    }
    std::string PresetName(const WorldObject& o) const {
        switch (o.kind) {
        case ObjKind::Pickup: return o.pickup.type == PickupType::Health ? "Health" : o.pickup.name;
        case ObjKind::Checkpoint: return "Checkpoint"; case ObjKind::Teleporter: return "Teleporter"; case ObjKind::Trigger: return "Zone"; case ObjKind::Light: return "Light"; case ObjKind::Particles: return "Particles";
        default: return o.model;
        }
    }
    static bool Overlaps(const Box3& b, V3 f, double hw, double H, double grow) {
        return f.x + hw > b.lo.x - grow && f.x - hw < b.hi.x + grow && f.z + hw > b.lo.z - grow && f.z - hw < b.hi.z + grow && f.y + H > b.lo.y - grow && f.y < b.hi.y + grow;
    }
    void Message(const std::string& t) { bigMsg = t; bigMsgT = 3.5f; }
    // ================================================================== event cards (G3): "when ... only if ... do ..."
    std::vector<EventCard> events; EventRunner evRun;
    std::set<uint32_t> evHidden, evOff, evLocked, evHeld, evKick, touchNow; bool evSolids = false;
    std::map<int, double> evFlash;   // Play mode: when each card last ran (the panel lights it up)
    bool Gone(const WorldObject& o) const { return playMode && evHidden.count(o.id) > 0; }
    bool SwitchedOff(const WorldObject& o) const { return playMode && (evHidden.count(o.id) || evOff.count(o.id)); }
    struct EvHost : EventHost {
        App& a; explicit EvHost(App& app) : a(app) {}
        bool hasKey(const std::string& n) override { return a.inv.keys.count(n) > 0; }
        int count(const std::string& n) override { auto it = a.inv.counts.find(n); return it == a.inv.counts.end() ? 0 : it->second; }
        float health() override { return a.play.health; }
        void run(const EventDo& d) override { a.RunAction(d); }
    };
    void NoteFired() { for (int i = 0; i < (int)events.size(); i++) { int n = evRun.firedCount(i); if (n != evSeen[i]) { evSeen[i] = n; evFlash[i] = animTime; } } }
    std::map<int, int> evSeen;
    void Signal(When k, const std::string& subject, const std::string& also = "") {
        if (!playMode || events.empty()) return;
        EvHost h(*this); evRun.signal(events, { k, subject, also }, h); NoteFired();
    }
    void EvStart(bool keepFlags = false) {
        std::set<std::string> fl = evRun.flags;
        evHidden.clear(); evOff.clear(); evLocked.clear(); evHeld.clear(); evKick.clear(); touchNow.clear(); evSeen.clear(); evFlash.clear();
        EvHost h(*this); evRun.start(events, h);
        if (keepFlags) evRun.flags.insert(fl.begin(), fl.end());
        NoteFired(); evSolids = true;
    }
    void RunAction(const EventDo& d) {
        auto each = [&](auto fn) { for (WorldObject& o : objects) if (!d.target.empty() && o.name == d.target) fn(o); };
        const std::string nm = d.text.empty() ? d.target : d.text;
        switch (d.kind) {
        case Do::Message: Message(d.text); bigMsgT = d.number > 0 ? d.number : 3.5f; break;
        case Do::OpenDoor: each([&](WorldObject& o) { if (o.kind != ObjKind::Door) return; evHeld.insert(o.id); doorRt[o.id].open(); }); break;
        case Do::CloseDoor: each([&](WorldObject& o) {
            if (o.kind != ObjKind::Door) return; evHeld.erase(o.id); DoorRuntime& r = doorRt[o.id];
            if (r.state == DoorRuntime::Open || r.state == DoorRuntime::Opening || r.state == DoorRuntime::Dissolving) { r.state = DoorRuntime::Closing; r.t = 0; } }); break;
        case Do::LockDoor: each([&](WorldObject& o) { evLocked.insert(o.id); }); break;
        case Do::UnlockDoor: each([&](WorldObject& o) { evLocked.erase(o.id); }); break;
        case Do::MovePlatform: each([&](WorldObject& o) { evKick.insert(o.id); }); break;
        case Do::Show: each([&](WorldObject& o) { evHidden.erase(o.id); }); evSolids = true; break;
        case Do::Hide: each([&](WorldObject& o) { evHidden.insert(o.id); }); evSolids = true; break;
        case Do::LightOn: each([&](WorldObject& o) { evOff.erase(o.id); }); break;
        case Do::LightOff: each([&](WorldObject& o) { evOff.insert(o.id); }); break;
        case Do::Particles: each([&](WorldObject& o) {
            FrameRef f = ModelOf(o); V3 at = o.pos; if (f) { Box3 b = objectBounds(o, *f); at = (b.lo + b.hi) * 0.5; }
            ParticleSettings p = particlePreset(d.text.empty() ? "Sparks" : d.text); p.burst = std::max(p.burst, 30); SpawnFx(p, at, { 0, 1, 0 }); }); break;
        case Do::Teleport: { std::string t = d.target; pending = [this, t] { TravelTo("", t); }; break; }
        case Do::Heal: play.health = std::min(game.player.maxHealth, play.health + d.number); Toast("+" + std::to_string((int)d.number) + " health"); break;
        case Do::Hurt: if (!play.dead()) { play.health -= d.number; play.hurtFlash = 0.4; if (play.health <= 0) { play.health = 0; play.cause = "got hurt too much"; } } break;
        case Do::Kill: if (!play.dead()) { play.health = 0; play.cause = "were killed"; } break;
        case Do::Checkpoint: playSpawn = player.feet; playYaw = player.yaw; Toast("Checkpoint"); break;
        case Do::GiveKey: if (!nm.empty()) { inv.keys.insert(nm); Toast("You got the " + nm); } break;
        case Do::TakeKey: inv.keys.erase(nm); break;
        case Do::AddCount: if (!nm.empty()) inv.counts[nm] += (int)std::lround(d.number); break;
        case Do::Win: Message(d.text.empty() ? "You win!" : d.text); bigMsgT = 10; Toast("You won! F5: back to the editor"); break;
        case Do::HudShow: hudShown[nm] = true; break;
        case Do::HudHide: hudShown[nm] = false; break;
        case Do::HudFlash: hudFlashAt[nm] = animTime; hudShown[nm] = true; break;
        default: break;
        }
    }
    void UpdateHelpers(double dt) {
        animTime += dt;
        if (!playMode) return;
        inv.tick((float)dt); bigMsgT = std::max(0.f, bigMsgT - (float)dt);
        std::set<uint32_t> now;
        std::string travelWorld, travelTarget; bool travel = false; uint32_t from = 0;
        for (WorldObject& o : objects) {
            if (o.kind != ObjKind::Pickup && o.kind != ObjKind::Checkpoint && o.kind != ObjKind::Teleporter && o.kind != ObjKind::Trigger) continue;
            if (Gone(o)) continue;
            FrameRef f = ModelOf(o); if (!f) continue;
            Box3 b = objectBounds(o, *f);
            if (!Overlaps(b, player.feet, player.halfW, player.height, o.kind == ObjKind::Trigger ? 0.0 : 0.15)) continue;
            now.insert(o.id); bool entered = !insideNow.count(o.id);
            switch (o.kind) {
            case ObjKind::Pickup: { std::string said; if (!play.dead() && inv.collect(o, play.health, game.player.maxHealth, said)) { Toast(said); Signal(When::Pickup, o.name, o.pickup.type == PickupType::Health ? "Health" : o.pickup.name); } break; }
            case ObjKind::Checkpoint:
                if (entered) { playSpawn = { o.pos.x, b.hi.y + 0.01, o.pos.z }; playYaw = player.yaw; Toast("Checkpoint"); }
                break;
            case ObjKind::Teleporter:
                if (teleportLock != o.id && !travel) { travel = true; travelWorld = o.teleport.world; travelTarget = o.teleport.target; from = o.id; }
                break;
            case ObjKind::Trigger:
                if (entered) Signal(When::EnterZone, o.name);
                if (!entered || (o.trigger.once && firedOnce.count(o.id))) break;
                firedOnce.insert(o.id);
                switch (o.trigger.action) {
                case TriggerAction::Message: if (!o.trigger.text.empty()) Message(o.trigger.text); break;   // empty: the event cards do the talking
                case TriggerAction::Kill: if (!play.dead()) { play.health = 0; play.cause = "walked into a trap"; if (!o.trigger.text.empty()) Message(o.trigger.text); } break;
                case TriggerAction::Heal: play.health = std::min(game.player.maxHealth, play.health + o.trigger.amount); Toast("+" + std::to_string((int)o.trigger.amount) + " health"); break;
                case TriggerAction::Checkpoint: playSpawn = player.feet; playYaw = player.yaw; Toast("Checkpoint"); break;
                }
                break;
            default: break;
            }
        }
        if (teleportLock && !now.count(teleportLock)) teleportLock = 0;   // stepped off the pad you arrived on
        for (uint32_t id : insideNow) if (!now.count(id)) for (const WorldObject& o : objects) if (o.id == id && o.kind == ObjKind::Trigger) Signal(When::LeaveZone, o.name);
        insideNow = now;
        {   // touching named objects (standing on one counts)
            std::set<uint32_t> touching;
            for (const WorldObject& o : objects) {
                if (o.name.empty() || Gone(o) || o.kind == ObjKind::Trigger || o.kind == ObjKind::Pickup || o.kind == ObjKind::Light || o.kind == ObjKind::Particles) continue;
                FrameRef f = ModelOf(o); if (!f) continue;
                WorldObject at = o; at.pos = o.pos + PlatformOffsets(o)[0];
                if (!Overlaps(objectBounds(at, *f), player.feet, player.halfW, player.height, 0.08)) continue;
                touching.insert(o.id); if (!touchNow.count(o.id)) Signal(When::Touch, o.name);
            }
            touchNow = touching;
        }
        { EvHost h(*this); evRun.tick(events, dt, h); NoteFired(); }
        if (evSolids) { evSolids = false; RebuildSolids(); }
        if (travel) { (void)from; pending = [this, travelWorld, travelTarget] { TravelTo(travelWorld, travelTarget); }; }
    }
    // to another place: an object (by name) in this world or in another world of this game
    void TravelTo(const std::string& worldName, const std::string& target) {
        if (!playMode || !world) return;
        if (!worldName.empty() && worldName != CurrentWorld()) {
            if (std::find(worldList.begin(), worldList.end(), worldName) == worldList.end()) { Toast("There is no world called " + worldName); return; }
            takenByWorld[CurrentWorld()] = inv.taken;
            RestoreBroken(); bombsOut.clear();
            travelling = true; SaveWorld(); OpenWorld(worldName); travelling = false;
            editCam = player;   // this world's own editor camera stays as it was
            inv.taken = takenByWorld[worldName]; doorRt.clear(); doorGpu.clear(); platRun.clear(); shots.clear(); insideNow.clear();
            if (!shotDir.size()) captureNext = true;
            EvStart(true);
        }
        V3 to = info.hasSpawn ? info.spawn : player.feet; double yaw = info.hasSpawn ? info.spawnYaw : player.yaw; teleportLock = 0;
        if (!target.empty()) {
            bool found = false;
            for (const WorldObject& o : objects) if (o.name == target) {
                FrameRef f = ModelOf(o); if (!f) continue; Box3 b = objectBounds(o, *f);
                to = { o.pos.x, o.kind == ObjKind::Trigger ? b.lo.y : b.hi.y + 0.01, o.pos.z }; yaw = o.yaw * 3.14159265358979 / 180; teleportLock = o.id; found = true; break;
            }
            if (!found) Toast("Nothing called \"" + target + "\" there, so: the spawn point");
        }
        player.feet = to; player.vel = { 0, 0, 0 }; player.yaw = yaw; playSpawn = to; playYaw = yaw;
        RebuildSolids();
        Message(worldName.empty() || worldName == CurrentWorld() ? std::string() : info.name); if (bigMsg.empty()) bigMsgT = 0;
    }
    void HelperGuides(ImDrawList* dl) {   // editor: zones are boxes, helpers say what they do
        if (playMode) return;
        for (int i = 0; i < (int)objects.size(); i++) {
            const WorldObject& o = objects[i]; FrameRef f = ModelOf(o); if (!f) continue;
            Box3 b = objectBounds(o, *f); std::string label; ImU32 col = IM_COL32(255, 255, 255, 200);
            switch (o.kind) {
            case ObjKind::Trigger: col = IM_COL32(255, 160, 60, 230); DrawBox3(dl, b.lo, b.hi, col);
                label = std::string("Zone: ") + triggerName(o.trigger.action) + (o.trigger.action == TriggerAction::Message ? " \"" + o.trigger.text + "\"" : ""); break;
            case ObjKind::Teleporter: col = IM_COL32(200, 140, 255, 230); label = "To " + (o.teleport.world.empty() ? std::string("here") : o.teleport.world) + (o.teleport.target.empty() ? "" : ": " + o.teleport.target); break;
            case ObjKind::Checkpoint: col = IM_COL32(110, 240, 255, 230); label = "Checkpoint"; break;
            case ObjKind::Pickup: col = IM_COL32(255, 230, 120, 230); label = o.pickup.type == PickupType::Health ? "+" + std::to_string((int)o.pickup.amount) + " health" : o.pickup.name; break;
            case ObjKind::Door: if (!o.door.key.empty()) { col = IM_COL32(255, 210, 80, 230); label = "Needs: " + o.door.key; } break;
            case ObjKind::Light: {
                col = IM_COL32(255, 235, 150, 230); label = o.light.type == LightType::Spot ? "Spot light" : "Light";
                if (sel == Sel::Object && selIndex == i) {
                    V3 c{ o.pos.x, o.pos.y + 0.3, o.pos.z }; double r = o.light.range;
                    if (o.light.type == LightType::Point) for (int k = 0; k < 48; k++) { double a0 = k * 6.2832 / 48, a1 = (k + 1) * 6.2832 / 48; Line3D(dl, { c.x + std::cos(a0) * r, c.y, c.z + std::sin(a0) * r }, { c.x + std::cos(a1) * r, c.y, c.z + std::sin(a1) * r }, IM_COL32(255, 235, 150, 110)); }
                    else {
                        double a = o.yaw * 3.14159265358979 / 180, t = o.light.tilt * 3.14159265358979 / 180, cn = o.light.cone * 3.14159265358979 / 180;
                        V3 d{ std::sin(a) * std::cos(t), -std::sin(t), std::cos(a) * std::cos(t) }, up = std::fabs(d.y) < 0.9 ? V3{ 0, 1, 0 } : V3{ 1, 0, 0 };
                        V3 u{ up.y * d.z - up.z * d.y, up.z * d.x - up.x * d.z, up.x * d.y - up.y * d.x }; double ul = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z); u = u * (1 / ul);
                        V3 w{ d.y * u.z - d.z * u.y, d.z * u.x - d.x * u.z, d.x * u.y - d.y * u.x };
                        for (int k = 0; k < 8; k++) { double ph = k * 6.2832 / 8; V3 e = (d * std::cos(cn) + (u * std::cos(ph) + w * std::sin(ph)) * std::sin(cn)) * r; Line3D(dl, c, c + e, IM_COL32(255, 235, 150, 110)); }
                    }
                }
                break; }
            case ObjKind::Particles: col = IM_COL32(255, 160, 100, 230); label = "Particles"; break;
            default: break;
            }
            if (label.empty()) continue;
            { V3 d = (b.lo + b.hi) * 0.5 - camPos; bool picked = sel == Sel::Object && selIndex == i;   // labels only nearby (or when picked), so the view stays clear
              if (!picked && d.x * d.x + d.y * d.y + d.z * d.z > 14 * 14) continue; }
            ImVec2 sp; if (Project({ (b.lo.x + b.hi.x) * 0.5, b.hi.y + 0.35, (b.lo.z + b.hi.z) * 0.5 }, sp)) {
                ImVec2 ts = ImGui::CalcTextSize(label.c_str()); dl->AddRectFilled(ImVec2(sp.x - ts.x * 0.5f - 4, sp.y - 2), ImVec2(sp.x + ts.x * 0.5f + 4, sp.y + ts.y + 2), IM_COL32(0, 0, 0, 120), 3.f);
                dl->AddText(ImVec2(sp.x - ts.x * 0.5f, sp.y), col, label.c_str());
            }
        }
    }
    void HelperInspector(WorldObject& o) {
        switch (o.kind) {
        case ObjKind::Pickup: {
            ImGui::SeparatorText("Pickup");
            int t = (int)o.pickup.type; const char* tn[] = { pickupName(PickupType::Health), pickupName(PickupType::Key), pickupName(PickupType::Collectible) };
            Prop("Gives"); ImGui::Combo("##ptype", &t, tn, 3); o.pickup.type = (PickupType)t;
            if (o.pickup.type == PickupType::Health) { Prop("Health"); ImGui::SliderFloat("##pamt", &o.pickup.amount, 1, 200, "+%.0f"); }
            else { Prop(o.pickup.type == PickupType::Key ? "Key name" : "Counted as"); ImGui::InputText("##pname", &o.pickup.name); }
            if (o.pickup.type == PickupType::Key) Hint("Doors that need \"%s\" open once you have it (Door > Needs key).", o.pickup.name.c_str());
            Prop("Comes back after"); ImGui::SliderFloat("##presp", &o.pickup.respawn, 0, 120, o.pickup.respawn <= 0 ? "never" : "%.0f s");
            break; }
        case ObjKind::Checkpoint: ImGui::SeparatorText("Checkpoint"); Hint("Touch it in Play mode: after dying you come back here instead of the spawn point."); break;
        case ObjKind::Teleporter: {
            ImGui::SeparatorText("Teleporter");
            Prop("Goes to world");
            if (ImGui::BeginCombo("##tw", o.teleport.world.empty() ? "(this world)" : o.teleport.world.c_str())) {
                if (ImGui::Selectable("(this world)", o.teleport.world.empty())) o.teleport.world.clear();
                for (auto& w : worldList) if (w != CurrentWorld() && ImGui::Selectable(w.c_str(), w == o.teleport.world)) o.teleport.world = w;
                ImGui::EndCombo();
            }
            Prop("Arrives at");
            float bw = ImGui::GetFrameHeight(); ImGui::SetNextItemWidth(std::max(30.f, ImGui::GetContentRegionAvail().x - bw - 2)); ImGui::InputTextWithHint("##tt", "the spawn point", &o.teleport.target);
            ImGui::SameLine(0, 2);
            if (ImGui::BeginCombo("##tpick", nullptr, ImGuiComboFlags_NoPreview | ImGuiComboFlags_PopupAlignLeft)) {   // objects of this world (another world: type the name)
                if (o.teleport.world.empty() || o.teleport.world == CurrentWorld()) for (auto& x : objects) if (x.id != o.id && ImGui::Selectable(x.name.c_str(), x.name == o.teleport.target)) o.teleport.target = x.name;
                if (ImGui::Selectable("(the spawn point)", o.teleport.target.empty())) o.teleport.target.clear();
                ImGui::EndCombo();
            }
            Hint("Arrives at an object of that name (another teleporter is nice: put two pads in two worlds and point them at each other).");
            break; }
        case ObjKind::Trigger: {
            ImGui::SeparatorText("Trigger zone");
            Prop("Size (blocks)"); if (ImGui::InputScalarN("##zsize", ImGuiDataType_Double, &o.size.x, 3, nullptr, nullptr, "%.1f")) { o.size.x = std::max(0.25, o.size.x); o.size.y = std::max(0.25, o.size.y); o.size.z = std::max(0.25, o.size.z); }
            int a = (int)o.trigger.action; const char* an[] = { triggerName(TriggerAction::Message), triggerName(TriggerAction::Kill), triggerName(TriggerAction::Heal), triggerName(TriggerAction::Checkpoint) };
            Prop("When you walk in"); ImGui::Combo("##zact", &a, an, 4); o.trigger.action = (TriggerAction)a;
            if (o.trigger.action == TriggerAction::Message || o.trigger.action == TriggerAction::Kill) { Prop("Message"); ImGui::InputText("##ztext", &o.trigger.text); }
            if (o.trigger.action == TriggerAction::Heal) { Prop("Health"); ImGui::SliderFloat("##zamt", &o.trigger.amount, 1, 500, "+%.0f"); }
            Prop("Only once"); ImGui::Checkbox("##zonce", &o.trigger.once);
            Hint("Invisible in the game. For more than one thing, leave the message empty and use event cards (Events panel): \"when the player walks into %s\".", o.name.c_str());
            if (FlowButton("Make an event card for it")) { EventCard c; c.name = o.name + ": walked in"; c.when = { When::EnterZone, o.name }; c.dos = { { Do::Message, "", "Hello!", 3 } }; events.push_back(c); focusEvents = true; }
            break; }
        default: break;
        }
    }

    // ================================================================== platforms and elevators (G2 attempt 3b)
    std::map<uint32_t, PlatformRun> platRun;          // by object id: where each platform (and its copies) is on its path
    bool previewPlatforms = false;                    // editor: let them move to try the path
    bool PlatformsMoving() const { return playMode || previewPlatforms; }
    void MakePlatform(WorldObject& o) {
        o.kind = ObjKind::Platform; if (o.folder == "Props" || o.folder == "Doors") o.folder = "Platforms";
        if (o.platform.points.empty()) o.platform.points = { { 0, 0, 0 } };
    }
    // where the platform (and each copy) is right now: offsets from its own place
    std::vector<V3> PlatformOffsets(const WorldObject& o) {
        if (o.kind != ObjKind::Platform) return { { 0, 0, 0 } };
        if (!PlatformsMoving()) return { { 0, 0, 0 } };
        auto it = platRun.find(o.id); if (it == platRun.end()) { platRun[o.id].reset(o.platform); it = platRun.find(o.id); }
        return it->second.offsets(o.platform);
    }
    void UpdatePlatforms(double dt) {
        if (!PlatformsMoving()) { if (!platRun.empty()) { platRun.clear(); RebuildSolids(); } return; }
        bool any = false; double hw = player.halfW;
        V3 carry{ 0, 0, 0 }; bool carried = false;
        for (WorldObject& o : objects) {
            if (o.kind != ObjKind::Platform) continue;
            FrameRef f = ModelOf(o); if (!f) continue; any = true;
            auto it = platRun.find(o.id); if (it == platRun.end()) { platRun[o.id].reset(o.platform); it = platRun.find(o.id); }
            PlatformRun& run = it->second;
            std::vector<V3> before = run.offsets(o.platform);
            std::vector<bool> on(before.size(), false), hit(before.size(), false);
            const std::vector<Box3>& mb = GpuModel(o.model).boxes;
            for (size_t k = 0; k < before.size(); k++) {   // who stands on which copy (and which copies were shot)
                WorldObject at = o; at.pos = o.pos + before[k];
                for (const Box3& b : objectBoxes(at, *f, mb)) { if (standsOn(b, player.feet, hw) && player.vel.y <= 0.01) on[k] = true; for (const Shot& s : shots) { double t; if (rayBox(s.pos, s.dir, b, t) && t < 1.0) hit[k] = true; } }
            }
            if (o.platform.trigger == PathTrigger::Event) for (size_t k = 0; k < hit.size(); k++) hit[k] = evKick.count(o.id) > 0;
            evKick.erase(o.id);
            if (Gone(o)) std::fill(on.begin(), on.end(), false);
            run.tick(o.platform, dt, on, hit);
            std::vector<V3> after = run.offsets(o.platform);
            for (size_t k = 0; k < on.size() && k < after.size() && k < before.size(); k++) if (on[k] && !carried) { carry = after[k] - before[k]; carried = true; }
        }
        if (!any) return;
        RebuildSolids();
        if (carried && !player.flying) {   // ride along (up, down, sideways)
            V3 to = player.feet + carry;
            if (world->boxFree(to, player.halfW, player.height)) player.feet = to;
            else { V3 up = to; up.y += 0.02; if (world->boxFree(up, player.halfW, player.height)) player.feet = up; }
        }
        if (!player.flying && !world->boxFree(player.feet, player.halfW, player.height)) {   // a platform moved into you: you end up on top of it
            for (const Box3& b : world->objectSolids) {
                if (player.feet.x + hw > b.lo.x && player.feet.x - hw < b.hi.x && player.feet.z + hw > b.lo.z && player.feet.z - hw < b.hi.z && player.feet.y < b.hi.y && player.feet.y + player.height > b.lo.y && b.hi.y - player.feet.y < 0.9) {
                    V3 up = player.feet; up.y = b.hi.y + 0.001; if (world->boxFree(up, player.halfW, player.height)) { player.feet = up; player.vel.y = std::max(0.0, player.vel.y); }
                }
            }
        }
    }
    // the path tool (tool slot 4): click an object to pick it (it becomes a platform), then click where it should go
    void PathToolClick(bool left, bool right) {
        if (left) {
            int i = ObjectUnderCrosshair(toolValid ? toolHit.t + 0.01 : 160);
            bool selPlat = sel == Sel::Object && selIndex >= 0 && selIndex < (int)objects.size() && objects[selIndex].kind == ObjKind::Platform;
            if (i >= 0 && (!selPlat || i != selIndex) ) { sel = Sel::Object; selIndex = i; if (objects[i].kind != ObjKind::Platform) { MakePlatform(objects[i]); Toast(objects[i].name + " is a platform now: click where it should go"); } return; }
            if (selPlat && toolValid) {
                WorldObject& o = objects[selIndex];
                V3 at{ toolHit.x + toolHit.nx + 0.5, (double)(toolHit.y + toolHit.ny), toolHit.z + toolHit.nz + 0.5 }, d = at - o.pos;
                V3 off{ std::round(d.x), std::round(d.y), std::round(d.z) };
                if (o.platform.points.empty()) o.platform.points.push_back({ 0, 0, 0 });
                o.platform.points.push_back(off); platRun.erase(o.id);
                Toast("Point " + std::to_string(o.platform.points.size()) + " added");
            }
        }
        if (right && sel == Sel::Object && selIndex >= 0 && selIndex < (int)objects.size() && objects[selIndex].kind == ObjKind::Platform) {
            auto& pts = objects[selIndex].platform.points; if (pts.size() > 1) { pts.pop_back(); platRun.erase(objects[selIndex].id); Toast("Removed the last point"); }
        }
    }
    void DrawPaths(ImDrawList* dl) {   // editor: the paths, their points, and where the platform will be at each point
        if (playMode) return;
        bool tool = toolMode && toolSel == 3;
        for (int i = 0; i < (int)objects.size(); i++) {
            const WorldObject& o = objects[i]; if (o.kind != ObjKind::Platform) continue;
            bool selected = sel == Sel::Object && selIndex == i;
            if (!selected && !tool) continue;
            FrameRef f = ModelOf(o); if (!f) continue;
            ImU32 col = selected ? IM_COL32(120, 255, 220, 255) : IM_COL32(120, 255, 220, 110);
            const auto& P = o.platform.points; V3 lift{ 0, 0.05, 0 };
            std::vector<V3> pts; for (const V3& p : P) pts.push_back(o.pos + p + lift); if (pts.empty()) pts.push_back(o.pos + lift);
            for (size_t k = 1; k < pts.size(); k++) Line3D(dl, pts[k - 1], pts[k], col);
            if (o.platform.mode == PathMode::Loop && pts.size() > 2) Line3D(dl, pts.back(), pts.front(), col);
            for (size_t k = 0; k < pts.size(); k++) {
                WorldObject at = o; at.pos = o.pos + (k < P.size() ? P[k] : V3{ 0, 0, 0 });
                if (k > 0 && selected) { Box3 b = objectBounds(at, *f); DrawBox3(dl, b.lo, b.hi, IM_COL32(120, 255, 220, 70)); }
                ImVec2 sp; if (Project(pts[k], sp)) { dl->AddCircleFilled(sp, 5, col); char n[8]; snprintf(n, sizeof n, "%d", (int)k + 1); dl->AddText(ImVec2(sp.x + 7, sp.y - 7), col, n); }
            }
            if (selected && tool && toolValid && !pts.empty()) {   // where the next point would go
                V3 nx{ toolHit.x + toolHit.nx + 0.5, (double)(toolHit.y + toolHit.ny) + 0.05, toolHit.z + toolHit.nz + 0.5 };
                Line3D(dl, pts.back(), nx, IM_COL32(255, 255, 255, 120));
            }
        }
    }
    void PlatformInspector(WorldObject& o, bool& ch_) {
        PlatformSettings& p = o.platform; bool reset = false;
        ImGui::SeparatorText("Platform");
        int m = (int)p.mode; const char* mn[] = { pathModeName(PathMode::PingPong), pathModeName(PathMode::Loop), pathModeName(PathMode::Once) };
        Prop("Path"); if (ImGui::Combo("##pmode", &m, mn, 3)) { p.mode = (PathMode)m; reset = true; }
        int t = (int)p.trigger; const char* tn[] = { pathTriggerName(PathTrigger::Always), pathTriggerName(PathTrigger::StandOn), pathTriggerName(PathTrigger::Shot), pathTriggerName(PathTrigger::Event) };
        Prop("Starts"); if (ImGui::Combo("##ptrig", &t, tn, 4)) { p.trigger = (PathTrigger)t; reset = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Switches, buttons and other events come with the game logic (G3).");
        Prop("Speed"); ImGui::SliderFloat("##pspeed", &p.speed, 0.2f, 20, "%.1f blocks/s");
        Prop("Waits at stops"); ImGui::SliderFloat("##pwait", &p.wait, 0, 10, "%.1f s");
        Prop("Stops at every point"); reset |= ImGui::Checkbox("##pstop", &p.stopEvery);
        if (p.trigger != PathTrigger::Always) { Prop("Comes back after"); ImGui::SliderFloat("##pback", &p.comeBack, 0, 30, p.comeBack <= 0 ? "never" : "%.0f s empty"); }
        if (p.trigger == PathTrigger::Always) {
            Prop("Spawner"); reset |= ImGui::Checkbox("##pspawn", &p.spawner);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("A new one starts at the beginning every few seconds\nand is gone at the end (conveyor belts, falling blocks ...).");
            if (p.spawner) { Prop("A new one every"); reset |= ImGui::SliderFloat("##pevery", &p.every, 0.3f, 20, "%.1f s"); }
            else { Prop("Copies on the path"); reset |= ImGui::SliderInt("##pcopies", &p.copies, 1, 16); }
        }
        ImGui::SeparatorText("Points");
        if (p.points.empty()) p.points.push_back({ 0, 0, 0 });
        int del = -1;
        for (int k = 1; k < (int)p.points.size(); k++) {
            ImGui::PushID(k); char lab[24]; snprintf(lab, sizeof lab, "Point %d", k + 1);
            Prop(lab); ImGui::SetNextItemWidth(std::max(40.f, ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 4));
            if (ImGui::InputScalarN("##pt", ImGuiDataType_Double, &p.points[k].x, 3, nullptr, nullptr, "%.1f")) reset = true;
            ImGui::SameLine(0, 4); if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), 0))) del = k;
            ImGui::PopID();
        }
        if (del > 0) { p.points.erase(p.points.begin() + del); reset = true; }
        Hint(p.points.size() < 2 ? "No path yet. Take the Platform path tool (T, then slot 4) and click where it should go." : "Point 1 is where it stands. Numbers are blocks from there.");
        if (ImGui::Button("Draw its path")) { toolMode = true; toolSel = 3; }
        if (p.points.size() > 1 && FlowButton("Clear the path")) { p.points.resize(1); reset = true; }
        ImGui::Checkbox("Try it here (move in the editor)", &previewPlatforms);
        if (reset) platRun.erase(o.id);
        (void)ch_;
    }

    // ================================================================== doors (G2 attempt 3a)
    struct Shot { V3 pos, dir; float age = 0; ParticleEmitter trail{ particlePreset("Beam trail"), 11 };
                  float speed = 45, size = 0.07f, range = 80, travelled = 0; V3 color{ 0.4, 1.6, 3.0 }; uint32_t tint = 0; std::string weapon, hitLook = "Impact"; bool missile = false, charged = false;
                  int beam = -1, mtype = -1; bool orb = false; float spin = 0; };   // beam / mtype: what fired it (sounds, sphere); orb: drawn as a glowing ball; mtype: drawn as the missile's model
    std::map<uint32_t, DoorRuntime> doorRt;           // Play mode: each door's state, by object id
    std::map<uint32_t, ModelGpu> doorGpu;             // doors in the middle of an animation: their posed meshes
    std::vector<Shot> shots; bool shootPrev = true;
    ComPtr<ID3D11VertexShader> vsField; ComPtr<ID3D11PixelShader> psField;
    ComPtr<ID3D11VertexShader> vsFieldMesh; ComPtr<ID3D11PixelShader> psFieldMesh; ComPtr<ID3D11InputLayout> layoutFieldMesh; ComPtr<ID3D11Buffer> vbFieldMesh; size_t vbFieldMeshCap = 0;
    struct FMVtx { float p[3], n[3]; };
    // ---- impact spheres: glowing shells that grow from explosions and fade
    ComPtr<ID3D11PixelShader> psSphere; ComPtr<ID3D11Buffer> vbSphere; std::vector<ImpactSphere> spheres; std::vector<FMVtx> unitSphere;
    void AddSphere(V3 at, float across, uint32_t col, float life = 0.45f) {
        if (across <= 0.01f) return;
        ImpactSphere s; s.pos = at; s.radius = across * 0.5f; s.color = col; s.life = life; spheres.push_back(s);
        if (spheres.size() > 24) spheres.erase(spheres.begin());
    }
    void UpdateSpheres(double dt) { for (auto& s : spheres) s.t += (float)dt; spheres.erase(std::remove_if(spheres.begin(), spheres.end(), [](const ImpactSphere& s) { return s.done(); }), spheres.end()); }
    void EnsureUnitSphere() {   // a ball of triangles, 1 across ... built once
        if (!unitSphere.empty()) return;
        const int LON = 32, LAT = 18;
        auto P = [&](int i, int j) { double th = 3.14159265358979 * j / LAT, ph = 6.28318530717959 * i / LON; return V3{ std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph) }; };
        auto put = [&](V3 p) { unitSphere.push_back({ { (float)p.x, (float)p.y, (float)p.z }, { (float)p.x, (float)p.y, (float)p.z } }); };
        for (int j = 0; j < LAT; j++) for (int i = 0; i < LON; i++) { V3 a = P(i, j), b = P(i + 1, j), c = P(i + 1, j + 1), d = P(i, j + 1); put(a); put(b); put(c); put(a); put(c); put(d); }
        D3D11_BUFFER_DESC d{}; d.ByteWidth = (UINT)(unitSphere.size() * sizeof(FMVtx)); d.Usage = D3D11_USAGE_DYNAMIC; d.BindFlags = D3D11_BIND_VERTEX_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; HR(dev->CreateBuffer(&d, nullptr, &vbSphere));
    }
    void BeginSphereDraw(ID3D11PixelShader* ps) {   // glowing shells: added on top, tested against the world's depth
        EnsureUnitSphere();
        ctx->OMSetBlendState(bsAdd.Get(), nullptr, 0xFFFFFFFF); ctx->OMSetDepthStencilState(dsTestOnly.Get(), 0); ctx->RSSetState(rsNoCull.Get());
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); ctx->IASetInputLayout(layoutFieldMesh.Get()); ctx->VSSetShader(vsFieldMesh.Get(), nullptr, 0); ctx->PSSetShader(ps, nullptr, 0);
        ctx->PSSetShaderResources(3, 1, noiseSRV.GetAddressOf()); ctx->PSSetSamplers(2, 1, sampLinear.GetAddressOf());
        ID3D11Buffer* cbs[2] = { cbFrame.Get(), cbChunk.Get() }; ctx->VSSetConstantBuffers(0, 2, cbs); ctx->PSSetConstantBuffers(0, 2, cbs);
        UINT stride = sizeof(FMVtx), off = 0; ctx->IASetVertexBuffers(0, 1, vbSphere.GetAddressOf(), &stride, &off);
    }
    // one ball: c its middle (relative to the camera), stretched along ax / ay / az (their lengths: the radii)
    void DrawSphereMesh(V3 c, V3 ax, V3 ay, V3 az, const ChunkCB& cc) {
        D3D11_MAPPED_SUBRESOURCE m; if (FAILED(ctx->Map(vbSphere.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
        FMVtx* v = (FMVtx*)m.pData; V3 nx = Norm(ax), ny = Norm(ay), nz = Norm(az);
        for (size_t k = 0; k < unitSphere.size(); k++) {
            const float* u = unitSphere[k].p; V3 p = c + ax * u[0] + ay * u[1] + az * u[2], n = nx * u[0] + ny * u[1] + nz * u[2];
            v[k] = { { (float)p.x, (float)p.y, (float)p.z }, { (float)n.x, (float)n.y, (float)n.z } };
        }
        ctx->Unmap(vbSphere.Get(), 0);
        ctx->UpdateSubresource(cbChunk.Get(), 0, nullptr, &cc, 0, 0);
        ctx->Draw((UINT)unitSphere.size(), 0);
    }
    void EndSphereDraw() { ctx->IASetInputLayout(layout.Get()); ctx->OMSetBlendState(bsOff.Get(), nullptr, 0xFFFFFFFF); ctx->OMSetDepthStencilState(dsOn.Get(), 0); ctx->RSSetState(rsCull.Get()); }
    void DrawSpheres() {
        if (spheres.empty()) return;
        BeginSphereDraw(psSphere.Get());
        for (const ImpactSphere& s : spheres) {
            float r = std::max(0.01f, s.radiusNow()); V3 c = s.pos - camPos;
            V3 col = Linear(s.color); float grown = s.t / std::max(0.01f, s.life);
            ChunkCB cc{ { (float)c.x, (float)c.y, (float)c.z, grown }, { r, (float)(s.pos.x * 0.37 + s.pos.z * 0.11), 0, 0 }, { 0, 0, 0, 0 }, { (float)col.x * 2.2f, (float)col.y * 2.2f, (float)col.z * 2.2f, s.alphaNow() } };
            DrawSphereMesh(c, { r, 0, 0 }, { 0, r, 0 }, { 0, 0, r }, cc);
        }
        EndSphereDraw();
    }
    // beam shots as glowing orbs: a bright ball stretched a little along the way it flies, with a soft halo around it
    ComPtr<ID3D11PixelShader> psOrb;
    void DrawOrbs() {
        bool any = false; for (const Shot& s : shots) any |= s.orb && s.age < 50; if (!any) return;
        BeginSphereDraw(psOrb.Get());
        for (const Shot& s : shots) {
            if (!s.orb || s.age >= 50) continue;
            double r = std::max(0.02, s.size * 1.5); V3 c = s.pos - camPos, d = s.dir;
            V3 side = std::fabs(d.y) > 0.95 ? V3{ 1, 0, 0 } : Norm(cross({ 0, 1, 0 }, d)), up = cross(d, side);
            V3 col = Linear(s.tint); float seed = (float)((&s - shots.data()) * 0.618);
            for (int halo = 1; halo >= 0; halo--) {   // the halo first, then the ball itself
                double k = halo ? 2.4 : 1.0, stretch = halo ? 1.6 : 1.45;
                ctx->OMSetBlendState(halo ? bsAdd.Get() : bsPremul.Get(), nullptr, 0xFFFFFFFF);   // the halo adds light; the ball itself covers what is behind it
                ChunkCB cc{ { (float)c.x, (float)c.y, (float)c.z, (float)(r * k) }, { (float)d.x, (float)d.y, (float)d.z, s.charged ? 1.f : 0.f }, { seed, (float)halo, s.age, 0 }, { (float)col.x, (float)col.y, (float)col.z, 1 } };
                DrawSphereMesh(c - d * (r * 0.25), side * (r * k), up * (r * k), d * (r * k * stretch), cc);
            }
        }
        EndSphereDraw();
    }
    // missiles fly as their 3D model, nose first, spinning slowly
    void DrawMissiles(int pass, V3 cam) {
        for (const Shot& s : shots) {
            if (s.mtype < 0 || s.age >= 50 || s.mtype >= (int)weapons.missileTypes.size()) continue;
            const MissileType& mt = weapons.missileTypes[s.mtype]; const std::string& model = mt.model.empty() ? std::string("@missile") : mt.model;
            FrameRef f = ModelNamed(model); if (!f) continue;
            double k = std::max(0.05f, mt.size) / std::max(0.05, f->sz); V3 d = s.dir;
            V3 side = std::fabs(d.y) > 0.95 ? V3{ 1, 0, 0 } : Norm(cross({ 0, 1, 0 }, d)), up = cross(d, side);
            double a = s.spin, ca = std::cos(a), sa = std::sin(a); V3 ax = side * ca + up * sa, ay = up * ca - side * sa;
            DrawModelAxes(pass, model, s.pos - d * (mt.size * 0.5), ax * k, ay * k, d * k, cam);
        }
    }
    // the field as a shell just outside the door's chosen moving parts (closed: as imported)
    bool DrawFieldOnParts(const WorldObject& o, const ModelFrame& f, const DoorRuntime& r) {
        auto mi = meshModels.find(o.model); if (mi == meshModels.end()) return false;
        const MeshModel& mm = mi->second; if (mm.parts.empty() || mm.vpart.size() != mm.raw.size()) return false;
        std::vector<char> on(mm.parts.size()); for (size_t i = 0; i < on.size(); i++) on[i] = fieldPartOn(o.door, mm.parts[i].name);
        std::vector<FMVtx> v; double gap = std::max(0.005f, o.door.fieldGap);
        for (size_t k = 0; k + 2 < mm.idx.size(); k += 3) {
            uint16_t p = mm.vpart[mm.idx[k]]; if (p >= on.size() || !on[p]) continue;
            for (int c = 0; c < 3; c++) {
                const MeshVert& mv = mm.raw[mm.idx[k + c]]; V3 L = mm.blockPos(mv), n{ mv.n[0], mv.n[1], mv.n[2] };
                V3 w = objectPoint(o, f, L), wn = objectPoint(o, f, L + n * 0.01) - w; double l = std::sqrt(wn.x * wn.x + wn.y * wn.y + wn.z * wn.z); if (l > 1e-12) wn = wn * (1 / l);
                V3 q = w + wn * gap - camPos; v.push_back({ { (float)q.x, (float)q.y, (float)q.z }, { (float)wn.x, (float)wn.y, (float)wn.z } });
            }
        }
        if (v.empty()) return false;
        if (v.size() > vbFieldMeshCap) {
            vbFieldMeshCap = std::max<size_t>(v.size() * 2, 4096); vbFieldMesh.Reset();
            D3D11_BUFFER_DESC d{}; d.ByteWidth = (UINT)(vbFieldMeshCap * sizeof(FMVtx)); d.Usage = D3D11_USAGE_DYNAMIC; d.BindFlags = D3D11_BIND_VERTEX_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; HR(dev->CreateBuffer(&d, nullptr, &vbFieldMesh));
        }
        D3D11_MAPPED_SUBRESOURCE ms; if (FAILED(ctx->Map(vbFieldMesh.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return false;
        std::memcpy(ms.pData, v.data(), v.size() * sizeof(FMVtx)); ctx->Unmap(vbFieldMesh.Get(), 0);
        V3 org, ac, up; fieldQuad(o, f, OpeningOf(o.model), org, ac, up, 0);
        V3 hit = org + ac * r.hitU + up * r.hitV - camPos, col = Linear(o.door.color);
        double size = std::max(std::sqrt(ac.x * ac.x + ac.y * ac.y + ac.z * ac.z), std::sqrt(up.x * up.x + up.y * up.y + up.z * up.z));
        ChunkCB cc{ { (float)hit.x, (float)hit.y, (float)hit.z, (float)size }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { (float)col.x, (float)col.y, (float)col.z, r.field } };
        ctx->UpdateSubresource(cbChunk.Get(), 0, nullptr, &cc, 0, 0);
        ctx->IASetInputLayout(layoutFieldMesh.Get()); ctx->VSSetShader(vsFieldMesh.Get(), nullptr, 0); ctx->PSSetShader(psFieldMesh.Get(), nullptr, 0);
        UINT stride = sizeof(FMVtx), off = 0; ctx->IASetVertexBuffers(0, 1, vbFieldMesh.GetAddressOf(), &stride, &off);
        ctx->Draw((UINT)v.size(), 0);
        ctx->IASetInputLayout(nullptr); ctx->VSSetShader(vsField.Get(), nullptr, 0); ctx->PSSetShader(psField.Get(), nullptr, 0);
        return true;
    }
    static V3 Linear(uint32_t rgb) { auto c = [](uint32_t v) { return std::pow(((v) & 255) / 255.0, 2.2); }; return { c(rgb >> 16), c(rgb >> 8), c(rgb) }; }
    Box3 OpeningOf(const std::string& model) {   // where the forcefield goes (model space)
        auto it = meshModels.find(model); if (it != meshModels.end()) return doorOpening(it->second);
        FrameRef f = ModelNamed(model); return { { f->sx * 0.15, 0, 0 }, { f->sx * 0.85, f->sy * 0.85, f->sz } };
    }
    int ClipOf(const std::string& model, int which) {   // 0 open, 1 close -> the model's animation (or -1)
        auto it = meshModels.find(model); if (it == meshModels.end()) return -1;
        int c = it->second.findClip(which == 0 ? "open" : "close");
        if (c < 0 && which == 0 && !it->second.clips.empty()) c = 0;   // no "open": the first animation is the opening
        return c;
    }
    bool CloseIsReversed(const std::string& model) { return ClipOf(model, 1) < 0 && ClipOf(model, 0) >= 0; }   // no "close" animation: play "open" backwards
    float ClipLen(const std::string& model, int which) {
        int c = ClipOf(model, which); if (c < 0 && which == 1 && CloseIsReversed(model)) c = ClipOf(model, 0);
        return c < 0 ? 0.f : meshModels[model].clips[c].length;
    }
    DoorRuntime DoorNow(const WorldObject& o) const { auto it = doorRt.find(o.id); return playMode && it != doorRt.end() ? it->second : DoorRuntime{}; }
    void MakeDoor(WorldObject& o) {
        o.kind = ObjKind::Door; if (o.folder == "Props") o.folder = "Doors";
        if (o.door.w <= 0) o.door.w = 2; if (o.door.h <= 0) o.door.h = 3;
    }
    // the door's place in Play mode: animations, forcefield, collision
    void UpdateDoors(double dt) {
        if (!playMode) { doorRt.clear(); doorGpu.clear(); shots.clear(); return; }
        bool changed = false;
        for (WorldObject& o : objects) {
            if (o.kind != ObjKind::Door) continue;
            FrameRef f = ModelOf(o); if (!f) continue;
            DoorRuntime& r = doorRt[o.id]; bool before = r.blocks();
            Box3 b = objectBounds(o, *f); V3 c = (b.lo + b.hi) * 0.5, d = player.feet - c;
            bool nearby = std::sqrt(d.x * d.x + d.z * d.z) < std::max(o.door.w, o.door.h) * 0.5 + 1.2 && player.feet.y < b.hi.y && player.feet.y > b.lo.y - 1;
            if (!o.door.field && nearby && r.state == DoorRuntime::Closed && !evLocked.count(o.id) && (o.door.key.empty() || inv.keys.count(o.door.key))) r.open();   // a plain door opens when you walk up
            r.tick((float)dt, nearby || evHeld.count(o.id), ClipLen(o.model, 0), ClipLen(o.model, 1), o.door.stayOpen);
            changed |= before != r.blocks();
            if (before && !r.blocks()) Signal(When::DoorOpens, o.name); else if (!before && r.blocks()) Signal(When::DoorCloses, o.name);
            // the halves where the animation has them
            auto mi = meshModels.find(o.model);
            if (mi != meshModels.end() && r.clip >= 0) {
                int clip = ClipOf(o.model, r.clip); float t = r.clipTime;
                if (r.clip == 1 && clip < 0 && CloseIsReversed(o.model)) { clip = ClipOf(o.model, 0); t = std::max(0.f, ClipLen(o.model, 0) - r.clipTime); }
                ModelGpu& g = doorGpu[o.id]; UploadMeshVerts(g, mi->second, mi->second.posed(clip, t)); g.mesh = true; g.tex = GpuModel(o.model).tex;
            } else doorGpu.erase(o.id);
        }
        if (changed) RebuildSolids();
        // the test beam: click to shoot (G5 brings real weapons)
        if (!weapons.empty() || player.ball) FireWeapons(dt);   // the player's beams and missiles (and the morph ball's bombs)
        else {
            bool L = Down(VK_LBUTTON) && Playing() && !play.dead();
            if (L && !shootPrev) { Shot sh; sh.pos = player.eye() + player.forward() * 0.6; sh.dir = player.forward(); shots.push_back(sh); }
            shootPrev = Down(VK_LBUTTON);
        }
        for (Shot& s : shots) {
            double step = s.speed * dt; s.age += (float)dt;
            double best = step; int door = -1; Hit h; bool blockHit = false;
            if (world->raycast(s.pos, s.dir, step, h)) { best = h.t; blockHit = true; }
            for (size_t i = 0; i < objects.size(); i++) {   // forcefields and doors, then any object in the way
                const WorldObject& o = objects[i]; FrameRef f = ModelOf(o); if (!f) continue;
                double t;
                if (o.kind == ObjKind::Door && DoorNow(o).blocks() && !Gone(o)) {
                    Box3 fb = fieldBox(o, *f, OpeningOf(o.model));
                    if (rayBox(s.pos, s.dir, fb, t) && t < best) { best = t; door = (int)i; }
                }
            }
            for (const Box3& b : world->objectSolids) { double t; if (rayBox(s.pos, s.dir, b, t) && t < best - 1e-6) { best = t; door = -1; blockHit = false; } }
            if (door >= 0) blockHit = false;
            if (best < step && blockHit) TryBreak(h.x, h.y, h.z, s.weapon, s.missile, s.charged);   // a marked block: it may break
            if (best < step) {
                {   // sparks where it hits; its tail stays a moment
                    V3 at = s.pos + s.dir * best; uint32_t tint = door >= 0 ? objects[door].door.color : s.tint;
                    SpawnFx(particlePreset(s.hitLook.empty() ? "Impact" : s.hitLook), at - s.dir * 0.05, s.dir * -1, tint);
                    {   // the impact sphere (small for beam shots, bigger for charged shots and missiles) and the hit sound
                        Beam* wb = s.beam >= 0 && s.beam < (int)weapons.beams.size() ? &weapons.beams[s.beam] : nullptr;
                        MissileType* wm = s.mtype >= 0 && s.mtype < (int)weapons.missileTypes.size() ? &weapons.missileTypes[s.mtype] : nullptr;
                        float across = wm ? wm->sphere : !wb ? 0.f : s.charged ? wb->chargeSphere : wb->hitSphere;
                        AddSphere(at - s.dir * 0.05, across, s.tint, s.missile ? 0.55f : s.charged ? 0.45f : 0.22f);
                        V3 sa = at - s.dir * 0.05;
                        if (wm) PlaySound(wm->hit, 1, &sa);
                        else if (wb) PlaySound(s.charged && !wb->chargedHitSound.empty() ? wb->chargedHitSound : wb->hitSound, 1, &sa);
                    }
                    if (s.charged) { ParticleSettings bo = particlePreset(s.hitLook.empty() ? "Impact" : s.hitLook); bo.burst *= 3; bo.speed *= 1.6f; bo.size0 *= 2; SpawnFx(bo, at - s.dir * 0.08, s.dir * -1, s.tint); }
                    if (s.missile) { ParticleSettings bo = particlePreset("Fire"); bo.burst = 40; bo.life = 0.5f; SpawnFx(bo, at - s.dir * 0.1, s.dir * -1, s.tint); }
                    fx.push_back(s.trail);
                    for (const WorldObject& o : objects) {   // which named objects were hit (for "when something is shot")
                        if (o.name.empty() || Gone(o) || o.kind == ObjKind::Trigger) continue;
                        FrameRef f = ModelOf(o); if (!f) continue;
                        WorldObject wo = o; wo.pos = o.pos + PlatformOffsets(o)[0]; Box3 b = objectBounds(wo, *f); const double g = 0.06;
                        if (at.x > b.lo.x - g && at.x < b.hi.x + g && at.y > b.lo.y - g && at.y < b.hi.y + g && at.z > b.lo.z - g && at.z < b.hi.z + g) Signal(When::Shot, o.name);
                    }
                }
                if (door >= 0) {   // where on the field it hit: the dissolve starts there
                    WorldObject& o = objects[door]; FrameRef f = ModelOf(o); V3 org, ac, up; fieldQuad(o, *f, OpeningOf(o.model), org, ac, up);
                    V3 p = s.pos + s.dir * best, q = p - org;
                    auto dot = [](V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
                    float u = (float)std::clamp(dot(q, ac) / std::max(1e-9, dot(ac, ac)), 0.0, 1.0), v = (float)std::clamp(dot(q, up) / std::max(1e-9, dot(up, up)), 0.0, 1.0);
                    if (evLocked.count(o.id)) Toast("This door is locked");
                    else if (!doorOpensFor(o.door, s.weapon, s.missile)) Toast("This door opens with " + (o.door.openedBy == "Missile" ? std::string("a missile") : "the " + o.door.openedBy));
                    else if (!o.door.key.empty() && !inv.keys.count(o.door.key)) Toast("This door needs the " + o.door.key);
                    else doorRt[o.id].shoot(u, v);
                }
                s.age = 99;
            } else { s.pos = s.pos + s.dir * step; s.travelled += (float)step; if (s.travelled > s.range) { fx.push_back(s.trail); s.age = 99; } }
        }
        shots.erase(std::remove_if(shots.begin(), shots.end(), [](const Shot& s) { return s.age > 2.5f; }), shots.end());
    }
    // forcefields (and the beam): glowing see-through quads, added on top of the picture
    void DrawFields() {
        bool any = false; for (auto& o : objects) any |= o.kind == ObjKind::Door && o.door.field;
        if (!any && shots.empty()) return;
        ctx->OMSetBlendState(bsAdd.Get(), nullptr, 0xFFFFFFFF); ctx->OMSetDepthStencilState(dsTestOnly.Get(), 0); ctx->RSSetState(rsNoCull.Get());
        ctx->IASetInputLayout(nullptr); ctx->VSSetShader(vsField.Get(), nullptr, 0); ctx->PSSetShader(psField.Get(), nullptr, 0);
        ctx->PSSetShaderResources(3, 1, noiseSRV.GetAddressOf()); ctx->PSSetSamplers(2, 1, sampLinear.GetAddressOf());
        for (const WorldObject& o : objects) {
            if (o.kind != ObjKind::Door || !o.door.field || Gone(o)) continue;
            FrameRef f = ModelOf(o); if (!f) continue;
            DoorRuntime r = DoorNow(o); if (r.field <= 0.001f) continue;
            if (o.door.fieldOnParts && DrawFieldOnParts(o, *f, r)) continue;
            for (int side = 0; side < 2; side++) {   // a sheet just in front of the door, and / or just behind it
                if ((o.door.fieldSide == 1 && side == 1) || (o.door.fieldSide == 2 && side == 0)) continue;
                V3 org, ac, up; fieldQuad(o, *f, OpeningOf(o.model), org, ac, up, side); V3 rel = org - camPos, col = Linear(o.door.color);
                ChunkCB cc{ { (float)rel.x, (float)rel.y, (float)rel.z, 0 }, { (float)ac.x, (float)ac.y, (float)ac.z, r.hitU }, { (float)up.x, (float)up.y, (float)up.z, r.hitV }, { (float)col.x, (float)col.y, (float)col.z, r.field } };
                ctx->UpdateSubresource(cbChunk.Get(), 0, nullptr, &cc, 0, 0); ctx->Draw(6, 0);
            }
        }
        for (const Shot& s : shots) {   // a short bright streak, facing the camera
            V3 tail = s.pos - s.dir * std::min(s.missile ? 0.6 : 1.4, (double)s.speed * s.age), side{ 0, 0, 0 };
            V3 view = s.pos - camPos; V3 cr{ s.dir.y * view.z - s.dir.z * view.y, s.dir.z * view.x - s.dir.x * view.z, s.dir.x * view.y - s.dir.y * view.x };
            double l = std::sqrt(cr.x * cr.x + cr.y * cr.y + cr.z * cr.z); side = l > 1e-9 ? cr * (s.size / l) : V3{ s.size, 0, 0 };
            V3 org = tail - side - camPos, ac = s.pos - tail, up = side * 2;
            ChunkCB cc{ { (float)org.x, (float)org.y, (float)org.z, 1 }, { (float)ac.x, (float)ac.y, (float)ac.z, 0 }, { (float)up.x, (float)up.y, (float)up.z, 0 }, { (float)s.color.x, (float)s.color.y, (float)s.color.z, 1 } };
            ctx->UpdateSubresource(cbChunk.Get(), 0, nullptr, &cc, 0, 0); ctx->Draw(6, 0);
        }
        ctx->OMSetBlendState(bsOff.Get(), nullptr, 0xFFFFFFFF); ctx->OMSetDepthStencilState(dsOn.Get(), 0); ctx->RSSetState(rsCull.Get());
    }
    void DoorInspector(WorldObject& o, bool& ch_) {
        ImGui::SeparatorText("Door");
        double w = o.door.w, h = o.door.h;
        Prop("Width (blocks)"); if (ImGui::InputDouble("##dw", &w, 0.5, 1, "%.1f") && w >= 0.5 && w <= 64) { o.door.w = w; ch_ = true; }
        Prop("Height (blocks)"); if (ImGui::InputDouble("##dh", &h, 0.5, 1, "%.1f") && h >= 0.5 && h <= 64) { o.door.h = h; ch_ = true; }
        Hint("The model is stretched to fill this, and the forcefield fills its opening.");
        Prop("Forcefield"); ImGui::Checkbox("##field", &o.door.field);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("On: shoot it to open the door.\nOff: the door opens when you walk up to it.");
        if (o.door.field) {
            float c[3] = { ((o.door.color >> 16) & 255) / 255.f, ((o.door.color >> 8) & 255) / 255.f, (o.door.color & 255) / 255.f };
            Prop("Colour"); if (ImGui::ColorEdit3("##fc", c, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel))
                o.door.color = ((uint32_t)std::lround(c[0] * 255) << 16) | ((uint32_t)std::lround(c[1] * 255) << 8) | (uint32_t)std::lround(c[2] * 255);
            const std::pair<const char*, uint32_t> presets[] = { { "Blue", 0x3AA0FF }, { "Red", 0xFF3A3A }, { "Green", 0x3AFF7A }, { "Yellow", 0xFFD23A }, { "Purple", 0xB45AFF } };
            for (auto& p : presets) { ImGui::PushID(p.first); ImVec4 col(((p.second >> 16) & 255) / 255.f, ((p.second >> 8) & 255) / 255.f, (p.second & 255) / 255.f, 1);
                Flow(ImGui::GetFrameHeight()); if (ImGui::ColorButton(p.first, col)) o.door.color = p.second; ImGui::PopID(); }
            Hint("Later, the colour says which weapon opens it (like beam, missile ...). For now any shot does.");
            Prop("Opened by");
            if (ImGui::BeginCombo("##openby", o.door.openedBy.empty() ? "Any shot" : o.door.openedBy.c_str())) {
                if (ImGui::Selectable("Any shot", o.door.openedBy.empty())) { o.door.openedBy.clear(); ch_ = true; }
                for (const Beam& b : weapons.beams) if (ImGui::Selectable(b.name.c_str(), o.door.openedBy == b.name)) { o.door.openedBy = b.name; o.door.color = b.color; ch_ = true; }
                if (ImGui::Selectable("Missile (any kind)", o.door.openedBy == "Missile")) { o.door.openedBy = "Missile"; o.door.color = 0xFF3A3A; ch_ = true; }
                for (const MissileType& m : weapons.missileTypes) if (m.name != "Missile" && ImGui::Selectable(m.name.c_str(), o.door.openedBy == m.name)) { o.door.openedBy = m.name; o.door.color = m.color; ch_ = true; }
                if (ImGui::Selectable("Bomb", o.door.openedBy == "Bomb")) { o.door.openedBy = "Bomb"; o.door.color = 0xFFB040; ch_ = true; }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Picking a beam also takes its colour.");
            { int s = o.door.fieldSide; const char* sn[] = { "Both sides", "Front only", "Back only" }; Prop("Field on"); if (ImGui::Combo("##fside", &s, sn, 3)) { o.door.fieldSide = s; ch_ = true; } }
            Prop("In front of the door"); if (ImGui::SliderFloat("##fgap", &o.door.fieldGap, 0.01f, 0.5f, "%.2f blocks")) ch_ = true;
            auto mi = meshModels.find(o.model);
            if (mi != meshModels.end() && !mi->second.parts.empty()) {
                Prop("On the door's parts"); if (ImGui::Checkbox("##fparts", &o.door.fieldOnParts)) ch_ = true;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Off: a flat sheet across the opening.\nOn: the field shimmers on the door's moving parts themselves.");
                if (o.door.fieldOnParts) {
                    for (const MeshPart& p : mi->second.parts) {
                        std::string s = "," + o.door.fieldParts + ","; bool in = o.door.fieldParts.empty() || s.find("," + p.name + ",") != std::string::npos;
                        Flow(ImGui::CalcTextSize(p.name.c_str()).x + ImGui::GetFrameHeight() * 2);
                        if (ImGui::Checkbox(p.name.c_str(), &in)) {
                            std::vector<std::string> names; for (const MeshPart& q : mi->second.parts) { bool qi = q.name == p.name ? in : fieldPartOn(o.door, q.name); if (qi) names.push_back(q.name); }
                            o.door.fieldParts.clear(); if (names.size() < mi->second.parts.size()) for (auto& n : names) o.door.fieldParts += (o.door.fieldParts.empty() ? "" : ",") + n;
                            if (names.empty()) o.door.fieldParts = "-none-";
                            ch_ = true;
                        }
                    }
                    Hint("Every part ticked: the whole door glows. The moving parts come from the model's animation.");
                }
            }
        }
        Prop("Stays open (s)"); ImGui::SliderFloat("##stay", &o.door.stayOpen, 0.5f, 15, "%.1f s");
        Prop("Needs key"); ImGui::InputTextWithHint("##dkey", "no key needed", &o.door.key);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The name of a key pickup, like \"Red key\".");
        int op = ClipOf(o.model, 0), cl = ClipOf(o.model, 1);
        if (op >= 0 || cl >= 0) Hint("Its animations: %s%s. They play when it opens and closes.", op >= 0 ? "open" : "", cl >= 0 ? (op >= 0 ? ", close" : "close") : "");
        else Hint("This model has no \"open\" / \"close\" animations, so only the forcefield opens. Name them like that in Blockbench or Blender to have the door move.");
    }

    // ================================================================== objects (G2)
    ModelGpu& GpuModel(const std::string& name) {   // the model's meshes, made the first time they are needed
        auto it = modelGpu.find(name); if (it != modelGpu.end()) return it->second;
        ModelGpu& g = modelGpu[name];
        { auto mi = meshModels.find(name); if (mi != meshModels.end()) { BuildMeshGpu(g, mi->second); return g; } }
        const BlockModel* m = models.find(name); if (!m) return g;
        g.boxes = modelBoxes(*m, reg); g.boxesStill = g.boxes;
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
            if ((int)i == movingObj || Gone(objects[i])) continue;
            if (!playMode && objects[i].kind == ObjKind::Door) continue;   // editing: doors cannot be opened, so you go through them
            FrameRef m = ModelOf(objects[i]); if (!m) continue;
            const WorldObject& o = objects[i];
            if (o.kind == ObjKind::Door) {   // a door: its frame always; its halves and its forcefield only while it is shut
                DoorRuntime r = DoorNow(o); ModelGpu& g = GpuModel(o.model);
                auto b = objectBoxes(o, *m, r.blocks() ? g.boxes : g.boxesStill);
                world->objectSolids.insert(world->objectSolids.end(), b.begin(), b.end());
                if (r.blocks() && o.door.field) world->objectSolids.push_back(fieldBox(o, *m, OpeningOf(o.model)));
                continue;
            }
            for (const V3& off : PlatformOffsets(o)) {   // a platform: wherever it (and each copy) is now
                WorldObject at = o; at.pos = o.pos + off;
                auto b = objectBoxes(at, *m, GpuModel(o.model).boxes);
                world->objectSolids.insert(world->objectSolids.end(), b.begin(), b.end());
            }
        }
    }
    // draws one object (pass 0 solid, 1 cut-out, 2 water) with the current shaders
    void DrawObject(const WorldObject& o, int pass) { DrawObject(o, pass, camPos); }
    void DrawObject(const WorldObject& o, int pass, V3 cam, const ModelGpu* use = nullptr) {
        FrameRef m = ModelOf(o); if (!m) return;
        auto dg = use ? doorGpu.end() : doorGpu.find(o.id);
        const ModelGpu& g = use ? *use : dg != doorGpu.end() ? dg->second : GpuModel(o.model);
        if (g.mesh) ctx->PSSetShaderResources(9, 1, g.tex.GetAddressOf());   // the imported model's own picture
        double a = o.yaw * 3.14159265358979 / 180, c = std::cos(a), s = std::sin(a); V3 k = objectStretch(o, *m);   // doors: stretched to Width x Height
        V3 ax{ c * k.x, 0, -s * k.x }, ay{ 0, k.y, 0 }, az{ s * k.z, 0, c * k.z };
        for (auto& part : g.parts) {
            const GpuChunk& gc = part.second; UINT n = pass == 0 ? gc.nO : pass == 1 ? gc.nC : gc.nW; if (!n) continue;
            V3 l{ part.first.x * (double)CS - m->pivot.x, part.first.y * (double)CS - m->pivot.y, part.first.z * (double)CS - m->pivot.z };
            V3 org = o.pos - cam + ax * l.x + ay * l.y + az * l.z;
            ChunkCB cc{ { (float)org.x, (float)org.y, (float)org.z, 0 }, { (float)ax.x, (float)ax.y, (float)ax.z, 0 }, { (float)ay.x, (float)ay.y, (float)ay.z, 0 }, { (float)az.x, (float)az.y, (float)az.z, 0 } };
            ctx->UpdateSubresource(cbChunk.Get(), 0, nullptr, &cc, 0, 0);
            UINT stride = sizeof(Vtx), off = 0;
            ctx->IASetVertexBuffers(0, 1, pass == 0 ? gc.vbO.GetAddressOf() : pass == 1 ? gc.vbC.GetAddressOf() : gc.vbW.GetAddressOf(), &stride, &off);
            ctx->IASetIndexBuffer(pass == 0 ? gc.ibO.Get() : pass == 1 ? gc.ibC.Get() : gc.ibW.Get(), DXGI_FORMAT_R32_UINT, 0);
            ctx->DrawIndexed(n, 0, 0);
        }
    }
    void LoadWorldObjects() {
        objects.clear(); loadObjects(worldDir + "/objects.txt", objects); loadEvents(worldDir + "/events.txt", events); loadBreakables(worldDir + "/breakable.txt", breakables);
        nextObjId = 1; for (auto& o : objects) nextObjId = std::max(nextObjId, o.id + 1);
        movingObj = -1; placing.clear(); RebuildSolids();
    }
    std::string UniqueObjectName(const std::string& base) const {
        for (int k = 1;; k++) { std::string n = base + " " + std::to_string(k); bool used = false; for (auto& o : objects) used |= o.name == n; if (!used) return n; }
    }
    // where a model would be placed: on the face you look at, lined up with the blocks (Alt: exactly where you look)
    bool PlacePose(WorldObject& o) {
        FrameRef m = ModelNamed(placing); if (!m || !toolValid) return false;
        o.model = placing; o.yaw = placeYaw;
        if (ClipOf(placing, 0) >= 0 && o.kind == ObjKind::Prop && movingObj < 0 && dupFrom < 0) MakeDoor(o);
        if (IsBuiltin(placing) && movingObj < 0 && dupFrom < 0) ApplyPreset(o);
        if (movingObj >= 0 && movingObj < (int)objects.size()) { o.kind = objects[movingObj].kind; o.door = objects[movingObj].door; o.size = objects[movingObj].size; }
        if (dupFrom >= 0 && dupFrom < (int)objects.size()) { o.kind = objects[dupFrom].kind; o.door = objects[dupFrom].door; o.size = objects[dupFrom].size; }
        if (Down(VK_MENU)) { o.pos = { toolHit.px, toolHit.py, toolHit.pz }; return true; }
        I3 a{ toolHit.x + toolHit.nx, toolHit.y + toolHit.ny, toolHit.z + toolHit.nz };
        bool side = std::fmod(std::fabs(placeYaw) + 0.001, 180.0) > 45 && std::fmod(std::fabs(placeYaw) + 0.001, 180.0) < 135;
        auto half = [&](double w) { double ws = w * o.scale; long r = std::lround(ws); return std::fabs(ws - r) < 1e-6 && (r % 2) == 0 ? 0.0 : std::fabs(ws - r) < 1e-6 ? 0.5 : 0.5; };
        if (o.kind == ObjKind::Door) { auto hw = [](double w) { long r = std::lround(w); return std::fabs(w - r) < 1e-6 && (r % 2) == 0 ? 0.0 : 0.5; }; o.pos = { a.x + (side ? 0.5 : hw(o.door.w)), (double)a.y, a.z + (side ? hw(o.door.w) : 0.5) }; return true; }
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
            FrameRef m = ModelOf(objects[i]); if (!m || (int)i == movingObj) continue;
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
        I3 corner; BlockModel m = modelFromWorld(*world, SelLo(), SelHi(), UniqueModelName(wanted.empty() ? "Model" : wanted), true, &corner);
        if (!m.blockCount()) { Toast("There are no blocks in the box"); return; }
        models.store(m); ModelChanged(m.name);
        WorldObject o; o.id = nextObjId++; o.name = UniqueObjectName(m.name); o.model = m.name;
        o.pos = { corner.x + m.pivot.x, (double)corner.y, corner.z + m.pivot.z };
        if (replace) {   // the blocks become the object (not an undo step: undoing would leave both)
            for (auto& e : fillEdits(*world, corner, { corner.x + m.sx - 1, corner.y + m.sy - 1, corner.z + m.sz - 1 }, AIR)) world->set(e.x, e.y, e.z, AIR);
            objects.push_back(o); Toast("Turned " + std::to_string(m.blockCount()) + " blocks into the object " + o.name);
        }
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
                bool dup = dupFrom >= 0 && dupFrom < (int)objects.size();
                WorldObject n = dup ? objects[dupFrom] : o;
                n.id = nextObjId++; n.model = o.model; n.pos = o.pos; n.yaw = o.yaw; n.scale = o.scale; n.kind = o.kind; n.door = o.door; if (n.kind == ObjKind::Door && !dup) n.folder = "Doors";
                n.name = UniqueObjectName(dup ? PresetName(n) : IsBuiltin(o.model) ? PresetName(o) : o.model);
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
            if (FrameRef m = ModelOf(objects[selIndex])) { Box3 b = objectBounds(objects[selIndex], *m); DrawBox3(dl, b.lo, b.hi, IM_COL32(255, 220, 90, 255)); }
        if (toolMode && toolSel == 2 && placing.empty()) {
            int h = ObjectUnderCrosshair(toolValid ? toolHit.t + 0.01 : 160);
            if (h >= 0 && !(sel == Sel::Object && selIndex == h)) if (FrameRef m = ModelOf(objects[h])) { Box3 b = objectBounds(objects[h], *m); DrawBox3(dl, b.lo, b.hi, IM_COL32(255, 255, 255, 140)); }
        }
        if (toolMode && toolSel == 2 && !placing.empty()) {
            WorldObject o; o.scale = placeScale;
            if (PlacePose(o)) if (FrameRef m = ModelNamed(placing)) { Box3 b = objectBounds(o, *m); DrawBox3(dl, b.lo, b.hi, IM_COL32(120, 255, 150, 220)); }
        }
    }
    void ObjectInspector() {
        if (selIndex < 0 || selIndex >= (int)objects.size()) { Hint("Pick an object in the Outliner, or click one with the Objects tool."); return; }
        WorldObject& o = objects[selIndex]; FrameRef m = ModelOf(o); bool ch_ = false;
        ImGui::SeparatorText("Object");
        Prop("Name"); ImGui::InputText("##name", &o.name);
        Prop("Folder"); ImGui::InputText("##folder", &o.folder);
        Prop("Model");
        if (ImGui::BeginCombo("##model", o.model.c_str())) { for (auto& n : AllModelNames()) if (ImGui::Selectable(n.c_str(), n == o.model)) { o.model = n; ch_ = true; } ImGui::EndCombo(); }
        { int k = (int)o.kind; const char* kn[] = { "Prop (it just stands there)", "Door (forcefield, opens)", "Platform (moves along a path)", "Pickup", "Checkpoint", "Teleporter", "Trigger zone (invisible)", "Light", "Particles" }; Prop("Kind");
          if (ImGui::Combo("##kind", &k, kn, 9)) { if (k == 1) MakeDoor(o); else if (k == 2) MakePlatform(o); else { o.kind = (ObjKind)k; if (o.kind == ObjKind::Pickup || o.kind == ObjKind::Trigger || o.kind == ObjKind::Light || o.kind == ObjKind::Particles) o.collide = Collide::None; } platRun.erase(o.id); ch_ = true; } }
        Prop("Position"); ch_ |= ImGui::InputScalarN("##pos", ImGuiDataType_Double, &o.pos.x, 3, nullptr, nullptr, "%.2f");
        { float y = (float)o.yaw; Prop("Turn"); if (ImGui::SliderFloat("##yaw", &y, -180, 180, "%.0f deg")) { o.yaw = y; ch_ = true; }
          if (ImGui::SmallButton("-90")) { o.yaw = std::fmod(o.yaw - 90 + 540, 360) - 180; ch_ = true; } if (FlowButton("+90", true)) { o.yaw = std::fmod(o.yaw + 90 + 540, 360) - 180; ch_ = true; } }
        if (o.kind == ObjKind::Platform) PlatformInspector(o, ch_);
        HelperInspector(o);
        if (o.kind == ObjKind::Light) LightInspector(o);
        if (o.kind == ObjKind::Particles) ParticleInspector(o);
        if (o.kind == ObjKind::Door) DoorInspector(o, ch_);
        else if (o.kind != ObjKind::Trigger) { float sc = (float)o.scale; Prop("Size"); if (ImGui::SliderFloat("##scale", &sc, 0.1f, 8, "x %.2f", ImGuiSliderFlags_Logarithmic)) { o.scale = sc; ch_ = true; } }
        if (m && o.kind != ObjKind::Door && o.kind != ObjKind::Trigger) {
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
        if (FlowButton("3D viewer")) { viewerModel = o.model; viewerOpen = true; viewerFit = true; viewerClip = -1; }
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
            ImGui::SeparatorText("Breakable");
            Prop("Breaks with"); BreakerCombo("##sbt", breakTool);
            if (ImGui::Button("Mark the box", ImVec2(bw, 0))) { int n = 0; for (int x = lo.x; x <= hi.x; x++) for (int y = lo.y; y <= hi.y; y++) for (int z = lo.z; z <= hi.z; z++) if (world->get(x, y, z) != AIR) { breakables.mark(x, y, z, breakTool); n++; } Toast(std::to_string(n) + " blocks marked breakable"); }
            ImGui::SameLine(); if (ImGui::Button("Unmark the box", ImVec2(bw, 0))) { for (int x = lo.x; x <= hi.x; x++) for (int y = lo.y; y <= hi.y; y++) for (int z = lo.z; z <= hi.z; z++) breakables.unmark(x, y, z); }
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
        FrameRef f = ModelNamed(selModel); if (!f) { Hint("Pick a model in Assets > Models."); return; }
        const BlockModel* m = models.find(selModel); auto mm = meshModels.find(selModel);
        ImGui::SeparatorText("Model");
        float s = std::min(ImGui::GetContentRegionAvail().x, 140 * settings.uiScale); ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + s, p.y + s), IM_COL32(30, 32, 38, 255), 4.f);
        ModelIcon(ImGui::GetWindowDrawList(), selModel, ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), s * 0.8f); ImGui::Dummy(ImVec2(s, s));
        ImGui::Text("%s", selModel.c_str());
        int used = 0; for (auto& o : objects) used += o.model == selModel;
        if (m) Hint("Block model: %d x %d x %d blocks, %d blocks in it. Used %d times in this world.", m->sx, m->sy, m->sz, m->blockCount(), used);
        else Hint("Imported from %s: %d triangles, %.2f x %.2f x %.2f blocks. Used %d times in this world.", mm->second.source.c_str(), (int)mm->second.triangles(), f->sx, f->sy, f->sz, used);
        if (ImGui::Button("Place it")) StartPlacing(selModel);
        if (FlowButton("3D viewer")) { viewerModel = selModel; viewerOpen = true; viewerFit = true; viewerClip = -1; }
        if (m && FlowButton("Unpack as blocks")) { if (toolValid) { BlockModel c = *m; I3 at{ toolHit.x + toolHit.nx, toolHit.y + toolHit.ny, toolHit.z + toolHit.nz }; ApplyEdits(pasteEdits(*world, c, at), "Unpacked"); } else Toast("Look at where the blocks should go"); }
        if (m && ImGui::IsItemHovered()) ImGui::SetTooltip("Puts its blocks where you look, so you can change them\nand make a new object from them.");
        ImGui::BeginDisabled(used > 0 || IsBuiltin(selModel));
        if (FlowButton("Delete model")) {
            std::string n = selModel;
            if (m) models.remove(n); else { std::error_code ec; fs::remove(fs::u8path(MeshPath(n)), ec); meshModels.erase(n); }
            modelGpu.erase(n); selModel.clear(); sel = Sel::None; if (viewerModel == n) viewerOpen = false;
        }
        ImGui::EndDisabled();
        if (used > 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Delete the objects that use it first.");
    }
    void ModelIcon(ImDrawList* dl, const std::string& name, ImVec2 c, float s) {
        if (const BlockModel* m = models.find(name)) { ObjectIcon(dl, *m, c, s); return; }
        auto it = meshModels.find(name); if (it == meshModels.end()) return;
        const MeshModel& m = it->second;   // an imported model: a little cube in its average colour, marked 3D
        double r = 0, g = 0, b = 0, n = 0; for (size_t i = 0; i + 3 < m.tex.size(); i += 4) if (m.tex[i + 3] > 128) { r += m.tex[i]; g += m.tex[i + 1]; b += m.tex[i + 2]; n++; }
        if (n) { r /= n; g /= n; b /= n; } else r = g = b = 200;
        float h = s * 0.3f; ImVec2 t(c.x, c.y - h * 0.9f);
        auto col = [&](double k) { return IM_COL32((int)std::min(255.0, r * k), (int)std::min(255.0, g * k), (int)std::min(255.0, b * k), 255); };
        dl->AddQuadFilled(ImVec2(c.x, c.y - h * 1.3f), ImVec2(c.x + h, c.y - h * 0.7f), ImVec2(c.x, c.y - h * 0.1f), ImVec2(c.x - h, c.y - h * 0.7f), col(1.15));
        dl->AddQuadFilled(ImVec2(c.x - h, c.y - h * 0.7f), ImVec2(c.x, c.y - h * 0.1f), ImVec2(c.x, c.y + h), ImVec2(c.x - h, c.y + h * 0.4f), col(0.8));
        dl->AddQuadFilled(ImVec2(c.x, c.y - h * 0.1f), ImVec2(c.x + h, c.y - h * 0.7f), ImVec2(c.x + h, c.y + h * 0.4f), ImVec2(c.x, c.y + h), col(0.6));
        dl->AddText(ImVec2(c.x + h * 0.55f, c.y + h * 0.5f), IM_COL32(255, 255, 255, 230), "3D"); (void)t;
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
        if (toolSel == 3) { PathToolClick(L && !lPrev, R && !rPrev); lPrev = L; rPrev = R; return; }
        if (toolSel == 4) { BreakToolClick(L, R); lPrev = L; rPrev = R; return; }
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
    std::string ToolName() const { return toolSel == 0 ? BrushLabel() : toolSel == 1 ? "Selection box" : toolSel == 2 ? (placing.empty() ? "Objects" : "Objects: placing " + placing) : toolSel == 3 ? "Platform path" : toolSel == 4 ? "Breakable blocks: " + BreakerLabel(breakTool) : "(empty tool slot)"; }
    std::string ToolLabel() const {
        switch (toolSel) {
        case 0: return BrushLabel() + "   (Q: brush options, T: back to blocks)";
        case 1: return selStage == 0 ? "Selection box: click a corner" : selStage == 1 ? "Click the other corner (right click: start over)"
                     : "Ctrl+C copy, Ctrl+V paste, F fill, Del delete";
        case 2: return !placing.empty() ? "Click: put down   R: turn   Alt: off the grid   Right click: stop"
                     : "Click an object   G move, R turn, Del delete";
        case 3: return "Platform path: click an object to pick it, then click where it should go.   Right click: remove the last point";
        default: return "(more tools are coming here)   T: back to blocks";
        }
    }
    std::string BrushLabel() const {
        return std::string("Terrain brush: ") + shapeInfo(brush.shape).name + ", " + opInfo(brush.op).name + ", size " + std::to_string(brush.radius);
    }
    void ToolIcon(ImDrawList* dl, int tool, ImVec2 c, float s) {
        if (tool == 4) {   // breakable blocks: a cracked block
            float h = s * 0.4f; ImU32 col = IM_COL32(255, 170, 60, 255);
            dl->AddRect(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), col, 2.0f, 0, 2.0f);
            dl->AddLine(ImVec2(c.x - h * 0.2f, c.y - h), ImVec2(c.x + h * 0.1f, c.y - h * 0.2f), col, 2); dl->AddLine(ImVec2(c.x + h * 0.1f, c.y - h * 0.2f), ImVec2(c.x - h * 0.3f, c.y + h * 0.3f), col, 2);
            dl->AddLine(ImVec2(c.x - h * 0.3f, c.y + h * 0.3f), ImVec2(c.x + h * 0.2f, c.y + h), col, 2); dl->AddLine(ImVec2(c.x + h * 0.1f, c.y - h * 0.2f), ImVec2(c.x + h, c.y + h * 0.1f), col, 2);
            return;
        }
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
        if (tool == 3) {   // platform path: a dotted line with a little platform on it
            float h = s * 0.42f; ImU32 col = IM_COL32(120, 255, 220, 255);
            ImVec2 a(c.x - h, c.y + h * 0.6f), b(c.x - h * 0.1f, c.y - h * 0.2f), d(c.x + h, c.y - h * 0.5f);
            for (int k = 0; k < 6; k++) { float t0 = k / 6.f, t1 = (k + 0.5f) / 6.f; ImVec2 p0 = k < 3 ? ImVec2(a.x + (b.x - a.x) * t0 * 2, a.y + (b.y - a.y) * t0 * 2) : ImVec2(b.x + (d.x - b.x) * (t0 * 2 - 1), b.y + (d.y - b.y) * (t0 * 2 - 1));
                ImVec2 p1 = k < 3 ? ImVec2(a.x + (b.x - a.x) * t1 * 2, a.y + (b.y - a.y) * t1 * 2) : ImVec2(b.x + (d.x - b.x) * (t1 * 2 - 1), b.y + (d.y - b.y) * (t1 * 2 - 1)); dl->AddLine(p0, p1, col, 2); }
            dl->AddCircleFilled(a, 3, col); dl->AddCircleFilled(d, 3, col);
            dl->AddRectFilled(ImVec2(b.x - h * 0.45f, b.y - h * 0.2f), ImVec2(b.x + h * 0.45f, b.y + h * 0.05f), IM_COL32(180, 190, 200, 255), 2.f);
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
                if (x + b.hi[0] > p.feet.x - p.halfW && x + b.lo[0] < p.feet.x + p.halfW && z + b.hi[2] > p.feet.z - p.halfW && z + b.lo[2] < p.feet.z + p.halfW && y + b.hi[1] > p.feet.y && y + b.lo[1] < p.feet.y + p.height) return;
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
        V3 f = player.forward();
        if (playMode && player.ball) BallCamera(camPos, f);   // the morph ball: seen from behind
        V3 r = normalize(cross({ 0, 1, 0 }, f)), u = cross(f, r);
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
            fc.shadow2[0] = (float)std::clamp(settings.shadowSoft, 0, 2); fc.shadow2[1] = std::clamp(settings.shadowSoftness, 0.1f, 4.f);
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
        BindLights();   // light objects, glowing emitters, shots
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
            for (size_t oi = 0; oi < objects.size(); oi++) {   // objects: same shaders, turned and sized; platforms where they are on their path
                if ((int)oi == movingObj) continue;
                const WorldObject& o = objects[oi];
                if (o.kind == ObjKind::Trigger || Gone(o)) continue;   // zones: invisible (the editor shows their box)
                if ((o.kind == ObjKind::Light || o.kind == ObjKind::Particles) && (playMode || plainView) && IsBuiltin(o.model)) continue;   // the bulb / nozzle: editor only
                if (o.kind == ObjKind::Pickup) {   // pickups turn and bob; gone once taken
                    if (playMode && inv.has(o.id)) continue;
                    WorldObject at = o; at.yaw += animTime * 90; at.pos.y += 0.12 + 0.1 * std::sin(animTime * 2.5 + o.id); DrawObject(at, pass); continue;
                }
                if (o.kind != ObjKind::Platform) { DrawObject(o, pass); continue; }
                for (const V3& off : PlatformOffsets(o)) { WorldObject at = o; at.pos = o.pos + off; DrawObject(at, pass); }
            }
            DrawBall(pass, camPos); DrawMissiles(pass, camPos);
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
        DrawFields(); DrawSpheres(); DrawOrbs();   // door forcefields and test-beam shots
        DrawParticles();
        if (E.particleKind > 0.5f && E.particleAmount > 0.01f) {   // dust, embers, spores or ice sparkles floating in the air
            ctx->OMSetBlendState(bsAlpha.Get(), nullptr, 0xFFFFFFFF); ctx->OMSetDepthStencilState(dsTestOnly.Get(), 0); ctx->RSSetState(rsNoCull.Get());
            ctx->IASetInputLayout(nullptr); ctx->VSSetShader(vsDust.Get(), nullptr, 0); ctx->PSSetShader(psRain.Get(), nullptr, 0);
            ctx->Draw(6 * 14000, 0);
            ctx->OMSetBlendState(bsOff.Get(), nullptr, 0xFFFFFFFF); ctx->OMSetDepthStencilState(dsOn.Get(), 0); ctx->RSSetState(rsCull.Get());
        }

        // ---- Play mode: the HUD's 3D parts, lit like the world (sun, its shadows, sky light, light objects), in front of everything
        if (playMode && (!hud.parts.empty() || !weapons.empty())) DrawHudPartsLit(fc, f, r, u, srvs, samps);

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
        if (playMode) DrawHudInGame(out);   // the player's HUD over everything
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
        DrawPaths(dl); DrawBreakables(dl);
        HelperGuides(dl);
        if (!playMode && info.hasSpawn) {   // the spawn point: a green player-sized box with an arrow where it looks
            V3 f = info.spawn; double hw = player.halfW, H = player.height;
            V3 c[8]; for (int i = 0; i < 8; i++) c[i] = { f.x + ((i & 1) ? hw : -hw), f.y + ((i & 2) ? H : 0), f.z + ((i & 4) ? hw : -hw) };
            const int e[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
            ImU32 col = sel == Sel::Spawn ? IM_COL32(255, 255, 120, 255) : IM_COL32(90, 255, 120, 255);
            for (auto& k : e) Line3D(dl, c[k[0]], c[k[1]], col);
            V3 eye{ f.x, f.y + player.eyeH, f.z }, look{ f.x + std::sin(info.spawnYaw) * 1.2, f.y + player.eyeH, f.z + std::cos(info.spawnYaw) * 1.2 };
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
        if (FreeAiming()) m = ImVec2(vx + cw * (0.5f + 0.5f * freeAim.x), vy + ch * (0.5f - 0.5f * freeAim.y));   // free aim: the cursor
        if (!(playMode && player.ball)) {   // (no crosshair in the morph ball)
            dl->AddRectFilled(ImVec2(m.x - 9, m.y - 1), ImVec2(m.x + 9, m.y + 1), IM_COL32(240, 240, 240, 220));
            dl->AddRectFilled(ImVec2(m.x - 1, m.y - 9), ImVec2(m.x + 1, m.y + 9), IM_COL32(240, 240, 240, 220));
        }
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
        ch_ |= ImGui::SliderFloat("Sound volume", &settings.soundVolume, 0, 2, "%.2f");
        ch_ |= ImGui::SliderFloat("UI size", &settings.uiScale, 0.75f, 2.f, "%.2f");
        ch_ |= ImGui::Checkbox("VSync", &settings.vsync);
        ch_ |= ImGui::Checkbox("Show where the next block goes (see-through preview)", &settings.blockPreview);
        ImGui::SeparatorText("Look");
        ch_ |= ImGui::SliderFloat("Time of day", &settings.timeOfDay, 0, 24, "%.1f h");
        ch_ |= ImGui::Checkbox("The sun moves (a day is 20 minutes)", &settings.dayCycle);
        const char* sq[] = { "Off", "Normal", "High" }; ch_ |= ImGui::Combo("Shadows", &settings.shadowQuality, sq, 3);
        if (settings.shadowQuality > 0) {
            const char* se[] = { "Sharp", "Smooth", "Soft like sunlight" }; ch_ |= ImGui::Combo("Shadow edges", &settings.shadowSoft, se, 3);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Soft like sunlight: sharp right next to what casts the shadow, softer the farther away it falls.");
            if (settings.shadowSoft > 0) ch_ |= ImGui::SliderFloat("Shadow softness", &settings.shadowSoftness, 0.25f, 3, "%.2f");
        }
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
        models.loadAll(dir + "/models"); modelGpu.clear(); LoadMeshModels(); viewerOpen = false;
        game = GameSettings{}; gamePath = dir + "/game.txt"; game.load(gamePath); LoadHud();
        RefreshWorlds();
        auto has = [&](const std::string& n) { return !n.empty() && std::find(worldList.begin(), worldList.end(), n) != worldList.end(); };
        std::string w = has(game.lastWorld) ? game.lastWorld : has(game.startWorld) ? game.startWorld : worldList.empty() ? "" : worldList[0];
        if (w.empty()) CreateWorld("Level 1", firstWorldType); else OpenWorld(w);
        SetWindowTextA(hwnd, ("PRIMEngine - " + game.name).c_str());
    }
    void CloseProject() {
        if (world) CloseWorld();
        if (!gamePath.empty()) { game.save(gamePath); SaveHud(); }
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
        EndStroke(); brushMenu = false; placing.clear(); dupFrom = -1; platRun.clear(); previewPlatforms = false; if (movingObj >= 0) { movingObj = -1; RebuildSolids(); }
        editCam = player; playMode = true; paused = false; inventory = false; shapeMenu = false; editorOpen = false;
        inv = PlayInventory{};
        if (!weapons.missileTypes.empty() && !weapons.missileCounter.empty()) inv.counts[weapons.missileCounter] = weapons.missileStart;
        weapState.missile = 0; chargedOn = false;
        takenByWorld.clear(); insideNow.clear(); firedOnce.clear(); teleportLock = 0; bigMsgT = 0; playStartWorld = CurrentWorld(); startEditCam = editCam;
        playSpawn = info.hasSpawn ? info.spawn : player.feet; playYaw = info.hasSpawn ? info.spawnYaw : player.yaw;
        play = PlayState{}; play.start(player, game.player, playSpawn, playYaw);
        weapState = WeaponState{}; weapPose = WeaponPose{}; cannonClip = -1; firedPending = 0; missilePrev = true;
        activeTl.clear(); chargeTlOn = false; freeAim = FreeAim{}; aimYaw = aimPitch = 0; stepDist = 0;
        player.ball = false; bombsOut.clear(); flashes.clear(); broken.clear(); ballCamSet = false;
        for (int i = 0; i < (int)weapons.beams.size(); i++) if (weapons.beams[i].startsOwned) { weapState.current = i; break; }
        hudSway.reset(); hudShown.clear(); hudFlashAt.clear(); prevPlayYaw = player.yaw; prevPlayPitch = player.pitch; prevOnGround = true; prevVelY = 0;
        EvStart();
        if (grabMouse) { Capture(false); captureNext = true; }   // grabbed next frame, once the picture fills the window
        Toast(info.hasSpawn ? "Playing. Esc: menu, F5: back to the editor" : "Playing from where the camera was (set a spawn point in the Outliner). F5: back to the editor");
    }
    void StopPlay() {
        if (!playMode) return;
        if (!playStartWorld.empty() && playStartWorld != CurrentWorld()) {   // went through a teleporter: back to the world you were editing
            travelling = true; SaveWorld(); OpenWorld(playStartWorld); travelling = false; editCam = startEditCam;
        }
        RestoreBroken(); bombsOut.clear(); flashes.clear(); StopRoll();   // broken blocks come back
        playMode = false; paused = false; player = editCam; player.ball = false; Capture(false); captureNext = false; platRun.clear();
        evHidden.clear(); evOff.clear(); evLocked.clear(); evHeld.clear(); evKick.clear(); evFlash.clear(); RebuildSolids();
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
        if (hud.empty()) {
        float bw = 300 * ui, bh = 18 * ui; ImVec2 o(vx + (W - bw) * 0.5f, vy + H - bh - 24 * ui);   // health bar (until the game has its own HUD)
        float f = game.player.maxHealth > 0 ? std::clamp(play.health / game.player.maxHealth, 0.f, 1.f) : 0.f;
        dl->AddRectFilled(ImVec2(o.x - 3, o.y - 3), ImVec2(o.x + bw + 3, o.y + bh + 3), IM_COL32(0, 0, 0, 160), 4.f);
        ImU32 col = f > 0.5f ? IM_COL32(70, 210, 90, 255) : f > 0.25f ? IM_COL32(240, 190, 50, 255) : IM_COL32(230, 60, 50, 255);
        dl->AddRectFilled(o, ImVec2(o.x + bw * f, o.y + bh), col, 3.f);
        char hb[64]; snprintf(hb, sizeof hb, "%.0f / %.0f", play.health, game.player.maxHealth);
        ImVec2 ts = ImGui::CalcTextSize(hb); dl->AddText(ImVec2(o.x + (bw - ts.x) * 0.5f, o.y + (bh - ts.y) * 0.5f), IM_COL32(255, 255, 255, 255), hb);
        }
        dl->AddText(ImVec2(vx + 12, vy + 10), IM_COL32(255, 255, 255, 200), game.name.c_str());
        if (hud.empty()) {   // what you carry
            float y = vy + 10 + ImGui::GetTextLineHeight() + 6;
            for (const std::string& k : inv.keys) { dl->AddRectFilled(ImVec2(vx + 12, y + 3), ImVec2(vx + 22, y + 13), IM_COL32(255, 210, 60, 255), 2.f); dl->AddText(ImVec2(vx + 28, y), IM_COL32(255, 230, 150, 240), k.c_str()); y += ImGui::GetTextLineHeight() + 4; }
            for (auto& c : inv.counts) { std::string t = c.first + "  x " + std::to_string(c.second); dl->AddCircleFilled(ImVec2(vx + 17, y + 8), 5, IM_COL32(90, 230, 255, 255)); dl->AddText(ImVec2(vx + 28, y), IM_COL32(180, 240, 255, 240), t.c_str()); y += ImGui::GetTextLineHeight() + 4; }
        }
        if (bigMsgT > 0 && !bigMsg.empty()) {   // a trigger zone's message, big in the middle
            float a = std::min(1.f, bigMsgT / 0.6f);
            ImGui::PushFont(nullptr, ImGui::GetFontSize() * 1.8f); ImVec2 ts = ImGui::CalcTextSize(bigMsg.c_str()); ImVec2 p(vx + (W - ts.x) * 0.5f, vy + H * 0.25f);
            dl->AddRectFilled(ImVec2(p.x - 18, p.y - 10), ImVec2(p.x + ts.x + 18, p.y + ts.y + 10), IM_COL32(0, 0, 0, (int)(130 * a)), 6.f);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), p, IM_COL32(255, 255, 255, (int)(255 * a)), bigMsg.c_str()); ImGui::PopFont();
        }
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
        if (weapons.switchMode == 2 && !weapons.empty()) {   // like Super Metroid: the beam and missile are picked here
            ImGui::SeparatorText("Beam");
            for (int i = 0; i < (int)weapons.beams.size(); i++) {
                const Beam& b = weapons.beams[i]; bool own = OwnsBeam(b); ImGui::PushID(i);
                ImGui::BeginDisabled(!own); if (ImGui::Selectable(own ? b.name.c_str() : "???", weapState.current == i)) SwitchBeam(i); ImGui::EndDisabled(); ImGui::PopID();
            }
            if (weapons.missileTypes.size() > 1) {
                ImGui::SeparatorText("Missiles");
                for (int i = 0; i < (int)weapons.missileTypes.size(); i++) {
                    const MissileType& m = weapons.missileTypes[i]; bool own = OwnsMissile(m); ImGui::PushID(100 + i);
                    ImGui::BeginDisabled(!own); if (ImGui::Selectable(own ? m.name.c_str() : "???", weapState.missile == i)) SwitchMissile(i, true); ImGui::EndDisabled(); ImGui::PopID();
                }
            }
            ImGui::Separator();
        }
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
        ImGui::DockBuilderDockWindow("Events", bottom);
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
            if (ImGui::MenuItem("Import 3D models...")) ImportModelsDialog();
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
            if (ImGui::MenuItem("Player editor (HUD)")) { playerEdOpen = true; focusPlayerEd = true; }
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
            ImGui::SameLine(); if (ImGui::Button("Player")) { playerEdOpen = !playerEdOpen; focusPlayerEd = playerEdOpen; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The Player editor: HUD (more later)");
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
            {   // size
                float hb = r.height; sl("Height", &r.height, 0.3f, 6, "%.2f blocks");
                if (r.height != hb && hb > 0) r.eyeHeight = std::min(r.height - 0.05f, r.eyeHeight * r.height / hb);   // the eyes stay at the same place on the body
                sl("Width", &r.width, 0.2f, 4, "%.2f blocks");
                sl("Eye height", &r.eyeHeight, 0.1f, std::max(0.15f, r.height - 0.02f), "%.2f blocks");
                sl("Steps up by itself", &r.stepHeight, 0, 2, "%.2f blocks");
                sl("Morph ball size", &r.ballSize, 0.2f, 3, "%.2f blocks");
                { const char* mo[] = { "None", "From the start", "Found later (\"Morph Ball\" pickup)" }; Prop("Morph ball (C)"); ImGui::Combo("##mb", &r.morphBall, mo, 3); }
                if (r.morphBall) {
                    sl("Rolling speed", &r.ballSpeed, 1, 20, "%.1f blocks/s");
                    { const char* bo[] = { "None", "From the start", "Found later (\"Bombs\" pickup)" }; Prop("Bombs (fire button)"); ImGui::Combo("##bb", &r.bombs, bo, 3); }
                    if (r.bombs) {
                        sl("Bomb jump", &r.bombJump, 0, 6, "%.1f blocks");
                        Prop("Bombs at once"); ImGui::SliderInt("##bao", &r.bombsAtOnce, 1, 6);
                        sl("Fuse", &r.bombFuse, 0.2f, 3, "%.2f s"); sl("Reach", &r.bombReach, 0.5f, 4, "%.1f blocks");
                        sl("Impact sphere", &r.bombSphere, 0, 10, "%.1f blocks across");
                    }
                    Prop("Ball model"); if (ImGui::BeginCombo("##bm", r.ballModel.c_str())) { if (ImGui::Selectable("@morph-ball (built in)", r.ballModel == "@morph-ball")) r.ballModel = "@morph-ball"; for (const std::string& n : PartModels()) if (ImGui::Selectable(n.c_str(), r.ballModel == n)) r.ballModel = n; ImGui::EndCombo(); }
                }
                Hint("Fits through gaps at least %.2f wide and %.2f tall. The morph ball comes with the Samus controls; its size already shows in the 3D viewer.", r.width, r.height);
            }
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
            ImGui::SeparatorText(toolSel == 0 ? "Terrain brush" : toolSel == 1 ? "Tools" : toolSel == 2 ? "Objects" : toolSel == 3 ? "Platform path" : toolSel == 4 ? "Breakable blocks" : "Tools");
            if (toolSel == 4) BreakToolInspector();
            if (toolSel == 0) BrushBody();
            else if (toolSel == 1) SelectionInspector();
            else if (toolSel == 3) {
                if (sel == Sel::Object && selIndex >= 0 && selIndex < (int)objects.size() && objects[selIndex].kind == ObjKind::Platform) { bool c = false; PlatformInspector(objects[selIndex], c); }
                else Hint("Click any object in the viewport: it becomes a platform. Then click the blocks where it should go, one point after the other.");
            }
            else if (toolSel == 2) {
                ImGui::SeparatorText("Add");
                const std::pair<const char*, const char*> add[] = { { "Light", "@light" }, { "Particles", "@emitter" }, { "Health", "@health" }, { "Key", "@key" }, { "Gem", "@gem" }, { "Checkpoint", "@checkpoint" }, { "Teleporter", "@teleporter" }, { "Trigger zone", "@zone" } };
                for (int a = 0; a < 8; a++) { if (a) Flow(ButtonW(add[a].first)); if (ImGui::Button(add[a].first)) StartPlacing(add[a].second); }
                Hint("Click an object in the viewport to pick it. Pick a model in Assets > Models to place it."); if (!placing.empty()) Hint("Placing: %s. R turns it, Alt puts it off the grid, right click stops.", placing.c_str()); }
            else Hint("More tools are coming to this slot.");
            break;
        case Sel::Object: ObjectInspector(); break;
        case Sel::Selection: SelectionInspector(); break;
        case Sel::Model: ModelInspector(); break;
        default: Hint("Pick something in the Outliner or the Assets."); break;
        }
        ImGui::End();
    }
    // ================================================================== Player editor, part 1: the HUD (planes with 2D elements, 3D helmet parts, Prime sway)
    HudLayout hud; HudSway hudSway, previewSway; HudValues hudVals;
    std::map<std::string, bool> hudShown; std::map<std::string, double> hudFlashAt;   // Play mode: event cards showing / hiding / flashing elements by name
    struct HudTarget { ComPtr<ID3D11Texture2D> tex; ComPtr<ID3D11RenderTargetView> rtv; ComPtr<ID3D11ShaderResourceView> srv; int w = 0, h = 0; };
    std::vector<HudTarget> hudTargets; std::vector<std::unique_ptr<ImDrawList>> hudLists; bool hudListsReady = false;
    ComPtr<ID3D11VertexShader> vsHud, vsBlit; ComPtr<ID3D11PixelShader> psHud, psBlit; ComPtr<ID3D11InputLayout> layoutHud; ComPtr<ID3D11Buffer> vbHud; ComPtr<ID3D11BlendState> bsPremul;
    std::map<std::string, ComPtr<ID3D11ShaderResourceView>> hudImages;
    struct HVtx { float p[3], uv[2]; };
    static constexpr int HUD_GX = 40, HUD_GY = 20, HUD_VERTS = HUD_GX * HUD_GY * 6;
    bool playerEdOpen = false, focusPlayerEd = false; int hudSel = 0, hudPlane = 0, hudEl = -1, hudPart = 0;   // hudSel: 0 sway, 1 plane, 2 element, 3 part
    float hudOrbYaw = 2.45f, hudOrbPitch = 0.38f, hudOrbDist = 3.4f; XMMATRIX hudOrbVP = XMMatrixIdentity(); V3 hudOrbEye{ 0, 0, 0 };
    ImVec2 hudViewWant{ 0, 0 }, hudPrevWant{ 0, 0 };
    HudTarget hudView, hudPrev; ComPtr<ID3D11DepthStencilView> hudViewDSV, hudPrevDSV; int hudViewDW = 0, hudViewDH = 0, hudPrevDW = 0, hudPrevDH = 0;
    int hudDrag = 0; ImVec2 hudDragMouse; HudElement hudDragFrom; double hudWiggle = -1; double prevEdYaw = 0, prevEdPitch = 0;
    double prevPlayYaw = 0, prevPlayPitch = 0, prevVelY = 0; bool prevOnGround = true;

    std::string HudPath() const { return projectDir + "/hud.txt"; }
    void LoadHud() { soundCache.clear(); playerSounds = PlayerSounds{}; loadPlayerSounds(projectDir + "/player_sounds.txt", playerSounds); activeTl.clear();
        hud = HudLayout{}; loadHud(HudPath(), hud); weapons = WeaponSet{}; loadWeapons(projectDir + "/weapons.txt", weapons); weapSel = 0; weapState = WeaponState{}; hudImages.clear(); hudSel = 0; hudPlane = 0; hudEl = -1; hudPart = 0; }
    void SaveHud() {
        if (projectDir.empty()) return; std::error_code ec;
        if (hud.empty()) fs::remove(fs::u8path(HudPath()), ec); else saveHud(HudPath(), hud);
        if (weapons.empty()) fs::remove(fs::u8path(projectDir + "/weapons.txt"), ec); else saveWeapons(projectDir + "/weapons.txt", weapons);
    }
    void InitHud(const std::string& src) {
        auto b = Compile(src, "VS_Hud", "vs_5_0"); HR(dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vsHud));
        D3D11_INPUT_ELEMENT_DESC he[] = { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
        HR(dev->CreateInputLayout(he, 2, b->GetBufferPointer(), b->GetBufferSize(), &layoutHud));
        b = Compile(src, "PS_Hud", "ps_5_0"); HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psHud));
        b = Compile(src, "VS_Blit", "vs_5_0"); HR(dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vsBlit));
        b = Compile(src, "PS_Blit", "ps_5_0"); HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psBlit));
        D3D11_BUFFER_DESC d{}; d.ByteWidth = sizeof(HVtx) * HUD_VERTS; d.Usage = D3D11_USAGE_DYNAMIC; d.BindFlags = D3D11_BIND_VERTEX_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; HR(dev->CreateBuffer(&d, nullptr, &vbHud));
        D3D11_BLEND_DESC bd{}; auto& rt = bd.RenderTarget[0]; rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D11_BLEND_ONE; rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOp = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D11_BLEND_ONE; rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOpAlpha = D3D11_BLEND_OP_ADD; HR(dev->CreateBlendState(&bd, &bsPremul));
    }
    static void PlaneRes(const HudPlane& p, int& w, int& h) { w = 1024; h = std::clamp((int)std::lround(1024.0 * p.height / std::max(0.01f, p.width)), 32, 1024); }
    ID3D11ShaderResourceView* HudImage(const std::string& name) {
        if (name.empty()) return nullptr;
        auto it = hudImages.find(name); if (it != hudImages.end()) return it->second.Get();
        ComPtr<ID3D11ShaderResourceView>& s = hudImages[name]; int w, h; std::vector<uint8_t> px;
        if (!readImage(projectDir + "/hud/" + name, w, h, px)) return nullptr;
        D3D11_TEXTURE2D_DESC d{}; d.Width = (UINT)w; d.Height = (UINT)h; d.MipLevels = 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd{ px.data(), (UINT)w * 4, 0 }; ComPtr<ID3D11Texture2D> t; HR(dev->CreateTexture2D(&d, &sd, &t)); HR(dev->CreateShaderResourceView(t.Get(), nullptr, &s));
        return s.Get();
    }
    std::vector<std::string> HudImageFiles() const {
        std::vector<std::string> v; std::error_code ec;
        for (auto& e : fs::directory_iterator(fs::u8path(projectDir + "/hud"), ec)) { std::string ext = Lower(e.path().extension().string()); if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") v.push_back(e.path().filename().u8string()); }
        std::sort(v.begin(), v.end()); return v;
    }
    bool ElementShown(const HudElement& e) const {
        if (!playMode) return e.visible;
        auto it = hudShown.find(e.name); return it == hudShown.end() ? e.visible : it->second;
    }
    // one element drawn into its plane's picture (W x H pixels)
    void DrawHudElement(ImDrawList* dl, const HudElement& e, float W, float H, const HudValues& v) {
        if (!ElementShown(e)) return;
        float a = e.alpha;
        if (playMode) { auto f = hudFlashAt.find(e.name); if (f != hudFlashAt.end() && animTime - f->second < 1.5) a *= 0.25f + 0.75f * (0.5f + 0.5f * (float)std::cos((animTime - f->second) * 18)); }
        auto col = [&](float k) { return IM_COL32((e.color >> 16) & 255, (e.color >> 8) & 255, e.color & 255, (int)std::clamp(a * k * 255, 0.f, 255.f)); };
        ImVec2 p0(e.x * W, e.y * H), p1((e.x + e.w) * W, (e.y + e.h) * H);
        switch (e.kind) {
        case HudKind::Panel: if (e.filled) dl->AddRectFilled(p0, p1, col(1)); else dl->AddRect(p0, p1, col(1), 0.0f, 0, e.thickness); break;
        case HudKind::Bar: {
            float f = std::clamp(hudValue(e, v) / std::max(1e-3f, hudMax(e, v)), 0.f, 1.f); int n = std::max(1, e.segments); float gap = n > 1 ? 3.f : 0.f;
            float sw = ((p1.x - p0.x) - gap * (n - 1)) / n;
            for (int k = 0; k < n; k++) {
                float x0 = p0.x + k * (sw + gap), x1 = x0 + sw, fill = std::clamp(f * n - k, 0.f, 1.f);
                dl->AddRectFilled(ImVec2(x0, p0.y), ImVec2(x1, p1.y), col(0.22f));
                if (fill > 0) dl->AddRectFilled(ImVec2(x0, p0.y), ImVec2(x0 + (x1 - x0) * fill, p1.y), col(1));
            }
            break;
        }
        case HudKind::Text: {
            std::string t = hudText(e, v); ImFont* font = ImGui::GetFont(); float size = std::max(4.f, (p1.y - p0.y) * e.textSize);
            ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0, t.c_str());
            float x = e.align == 0 ? p0.x : e.align == 1 ? (p0.x + p1.x - ts.x) * 0.5f : p1.x - ts.x;
            dl->AddText(font, size, ImVec2(x, (p0.y + p1.y - ts.y) * 0.5f), col(1), t.c_str());
            break;
        }
        case HudKind::Image: if (ID3D11ShaderResourceView* s = HudImage(e.image)) dl->AddImage((ImTextureID)(intptr_t)s, p0, p1, ImVec2(0, 0), ImVec2(1, 1), col(1)); else dl->AddRect(p0, p1, col(0.5f)); break;
        case HudKind::Circle: {
            ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f); float r = std::min(p1.x - p0.x, p1.y - p0.y) * 0.5f;
            if (e.filled) dl->AddCircleFilled(c, r, col(1), 48); else dl->AddCircle(c, r - e.thickness * 0.5f, col(1), 48, e.thickness);
            break;
        }
        default: break;
        }
    }
    // every plane's picture as an ImGui draw list (inside the ImGui frame: text sizes get baked)
    void BuildHudLists() {
        hudListsReady = false;
        bool need = (playMode && (!hud.empty() || !weapons.empty())) || (playerEdOpen && screen == Screen::Game && EditorView());
        if (!need) return;
        if (playMode) { hudVals.health = play.health; hudVals.maxHealth = game.player.maxHealth; hudVals.counts = inv.counts; hudVals.keys = inv.keys; hudVals.charge = weapState.charge; }
        else { hudVals = HudValues{}; hudVals.charge = 0.6f; hudVals.maxHealth = game.player.maxHealth; hudVals.health = game.player.maxHealth * 0.76f; hudVals.counts["Missile"] = 25; hudVals.keys.insert("Red key"); }
        hudLists.resize(hud.planes.size());
        for (size_t i = 0; i < hud.planes.size(); i++) {
            const HudPlane& p = hud.planes[i]; int W, H; PlaneRes(p, W, H);
            if (!hudLists[i]) hudLists[i] = std::make_unique<ImDrawList>(ImGui::GetDrawListSharedData());
            ImDrawList* dl = hudLists[i].get(); dl->_ResetForNewFrame();
            dl->PushClipRect(ImVec2(0, 0), ImVec2((float)W, (float)H)); dl->PushTexture(ImGui::GetIO().Fonts->TexRef);
            for (const HudElement& e : p.elements) DrawHudElement(dl, e, (float)W, (float)H, hudVals);
            dl->PopTexture(); dl->PopClipRect();
        }
        hudListsReady = true;
    }
    void MakeTarget(HudTarget& t, int w, int h, ComPtr<ID3D11DepthStencilView>* dsvOut = nullptr) {
        if (t.w == w && t.h == h && t.rtv) return;
        if (t.srv) retired.push_back(t.srv);
        D3D11_TEXTURE2D_DESC d{}; d.Width = (UINT)w; d.Height = (UINT)h; d.MipLevels = 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1; d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        t.tex.Reset(); t.rtv.Reset(); t.srv.Reset(); HR(dev->CreateTexture2D(&d, nullptr, &t.tex)); HR(dev->CreateRenderTargetView(t.tex.Get(), nullptr, &t.rtv)); HR(dev->CreateShaderResourceView(t.tex.Get(), nullptr, &t.srv));
        t.w = w; t.h = h;
        if (dsvOut) { d.Format = DXGI_FORMAT_D32_FLOAT; d.BindFlags = D3D11_BIND_DEPTH_STENCIL; ComPtr<ID3D11Texture2D> dt; HR(dev->CreateTexture2D(&d, nullptr, &dt)); dsvOut->Reset(); HR(dev->CreateDepthStencilView(dt.Get(), nullptr, dsvOut->GetAddressOf())); }
    }
    // the draw lists -> each plane's picture (premultiplied: see-through where nothing is drawn)
    void RenderHudPlanes() {
        if (!hudListsReady) return;
        for (ImTextureData* t : ImGui::GetPlatformIO().Textures) if (t->Status == ImTextureStatus_WantCreate || t->Status == ImTextureStatus_WantUpdates) ImGui_ImplDX11_UpdateTexture(t);   // new letter sizes, before they are used
        hudTargets.resize(hud.planes.size());
        for (size_t i = 0; i < hud.planes.size() && i < hudLists.size(); i++) {
            int W, H; PlaneRes(hud.planes[i], W, H); MakeTarget(hudTargets[i], W, H);
            const float clear[4] = { 0, 0, 0, 0 };
            ctx->OMSetRenderTargets(1, hudTargets[i].rtv.GetAddressOf(), nullptr); ctx->ClearRenderTargetView(hudTargets[i].rtv.Get(), clear);
            ImDrawList* dl = hudLists[i].get();
            if (dl->CmdBuffer.Size == 0 || dl->VtxBuffer.Size == 0) continue;
            ImDrawData dd; dd.Valid = true; dd.CmdLists.push_back(dl); dd.TotalVtxCount = dl->VtxBuffer.Size; dd.TotalIdxCount = dl->IdxBuffer.Size;
            dd.DisplayPos = ImVec2(0, 0); dd.DisplaySize = ImVec2((float)W, (float)H); dd.FramebufferScale = ImVec2(1, 1); dd.Textures = nullptr;
            ImGui_ImplDX11_RenderDrawData(&dd);
        }
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
    }
    // the parts and planes, seen through vp (camera space in); sway on top
    // the HUD's parts and planes, and the arm cannon on its own layer (behind the HUD or in front of it, never cutting into it)
    void DrawHudScene(const XMMATRIX& vp, const HudSway& sw, bool editor, ID3D11DepthStencilView* ds) {
        ID3D11Buffer* cbs[3] = { cbFrame.Get(), cbChunk.Get(), cbPreview.Get() }; ctx->VSSetConstantBuffers(0, 3, cbs); ctx->PSSetConstantBuffers(0, 3, cbs);
        PreviewCB pc{}; XMStoreFloat4x4((XMFLOAT4X4*)pc.vp, vp); pc.light[0] = -0.35f; pc.light[1] = 0.75f; pc.light[2] = -0.55f;
        D3D11_VIEWPORT vpFull; UINT nvp = 1; ctx->RSGetViewports(&nvp, &vpFull);
        std::vector<CamPart> parts = CameraParts(sw, editor ? previewPose : weapPose);
        auto partState = [&] {
            ctx->UpdateSubresource(cbPreview.Get(), 0, nullptr, &pc, 0, 0);
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); ctx->IASetInputLayout(layout.Get());
            ctx->VSSetShader(vsPreview.Get(), nullptr, 0); ctx->PSSetShader(editor ? psPreview.Get() : nullptr, nullptr, 0);   // Play mode: already drawn lit, here only their depth
            ctx->PSSetShaderResources(0, 1, texArray.GetAddressOf()); ctx->PSSetSamplers(0, 1, sampBlocks.GetAddressOf());
            ctx->RSSetState(rsNoCull.Get()); ctx->OMSetDepthStencilState(dsOn.Get(), 0); ctx->OMSetBlendState(bsOff.Get(), nullptr, 0xFFFFFFFF);
        };
        auto drawParts = [&](bool cannon) {
            for (const CamPart& c : parts) {
                if (c.cannon != cannon) continue;
                FrameRef f = ModelNamed(c.model); if (!f) continue;
                const ModelGpu& g = c.gpu ? *c.gpu : GpuModel(c.model);
                if (g.mesh) ctx->PSSetShaderResources(9, 1, g.tex.GetAddressOf());
                for (int pass = 0; pass < 2; pass++) for (auto& part : g.parts) {
                    const GpuChunk& gc = part.second; UINT n = pass == 0 ? gc.nO : gc.nC; if (!n) continue;
                    V3 l{ part.first.x * (double)CS - f->pivot.x, part.first.y * (double)CS - f->pivot.y, part.first.z * (double)CS - f->pivot.z };
                    V3 org = c.base + c.ax * l.x + c.ay * l.y + c.az * l.z;
                    ChunkCB cc{ { (float)org.x, (float)org.y, (float)org.z, 0 }, { (float)c.ax.x, (float)c.ax.y, (float)c.ax.z, 0 }, { (float)c.ay.x, (float)c.ay.y, (float)c.ay.z, 0 }, { (float)c.az.x, (float)c.az.y, (float)c.az.z, 0 } };
                    ctx->UpdateSubresource(cbChunk.Get(), 0, nullptr, &cc, 0, 0);
                    UINT stride = sizeof(Vtx), off = 0;
                    ctx->IASetVertexBuffers(0, 1, pass == 0 ? gc.vbO.GetAddressOf() : gc.vbC.GetAddressOf(), &stride, &off);
                    ctx->IASetIndexBuffer(pass == 0 ? gc.ibO.Get() : gc.ibC.Get(), DXGI_FORMAT_R32_UINT, 0);
                    ctx->DrawIndexed(n, 0, 0);
                }
            }
        };
        auto drawPlanes = [&] {   // the farthest first; they do not hide each other, but parts in front of them do
            std::vector<int> order; for (int i = 0; i < (int)hud.planes.size() && i < (int)hudTargets.size(); i++) if (hud.planes[i].visible && hudTargets[i].srv) order.push_back(i);
            std::sort(order.begin(), order.end(), [&](int a, int b) { return hud.planes[a].pos.z > hud.planes[b].pos.z; });
            ctx->IASetInputLayout(layoutHud.Get()); ctx->VSSetShader(vsHud.Get(), nullptr, 0); ctx->PSSetShader(psHud.Get(), nullptr, 0);
            ctx->PSSetSamplers(3, 1, sampClamp.GetAddressOf()); ctx->OMSetDepthStencilState(dsTestOnly.Get(), 0); ctx->RSSetState(rsNoCull.Get());
            for (int i : order) {
                const HudPlane& p = hud.planes[i];
                D3D11_MAPPED_SUBRESOURCE m; if (FAILED(ctx->Map(vbHud.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) continue;
                HVtx* v = (HVtx*)m.pData; int n = 0;
                auto put = [&](int gx, int gy) { float u = (float)gx / HUD_GX, w = (float)gy / HUD_GY; V3 q = sw.apply(planePoint(p, u, w), p.sway); v[n++] = { { (float)q.x, (float)q.y, (float)q.z }, { u, w } }; };
                for (int gy = 0; gy < HUD_GY; gy++) for (int gx = 0; gx < HUD_GX; gx++) { put(gx, gy); put(gx + 1, gy); put(gx + 1, gy + 1); put(gx, gy); put(gx + 1, gy + 1); put(gx, gy + 1); }
                ctx->Unmap(vbHud.Get(), 0);
                PreviewCB pp = pc; pp.light[0] = std::clamp(p.opacity, 0.f, 1.f); ctx->UpdateSubresource(cbPreview.Get(), 0, nullptr, &pp, 0, 0);
                ctx->OMSetBlendState(p.glow ? bsAdd.Get() : bsPremul.Get(), nullptr, 0xFFFFFFFF);
                UINT stride = sizeof(HVtx), off = 0; ctx->IASetVertexBuffers(0, 1, vbHud.GetAddressOf(), &stride, &off);
                ctx->PSSetShaderResources(0, 1, hudTargets[i].srv.GetAddressOf());
                ctx->Draw(n, 0);
            }
            ID3D11ShaderResourceView* none = nullptr; ctx->PSSetShaderResources(0, 1, &none);
        };
        bool front = weapons.cannonInFront;
        if (editor) {   // colour: the lower layer first, then a fresh depth for the upper one
            partState();
            if (!front) { drawParts(true); if (ds) ctx->ClearDepthStencilView(ds, D3D11_CLEAR_DEPTH, 1, 0); partState(); drawParts(false); drawPlanes(); }
            else { drawParts(false); drawPlanes(); if (ds) ctx->ClearDepthStencilView(ds, D3D11_CLEAR_DEPTH, 1, 0); partState(); drawParts(true); }
        } else {        // Play mode (the parts are already lit in the picture): only depth, so the planes know what is in front of them
            partState(); drawParts(false);
            if (front) { D3D11_VIEWPORT z = vpFull; z.MinDepth = z.MaxDepth = 0; ctx->RSSetViewports(1, &z); drawParts(true); ctx->RSSetViewports(1, &vpFull); }   // the cannon hides the planes
            drawPlanes();
        }
        ID3D11ShaderResourceView* none[10] = {}; ctx->PSSetShaderResources(0, 10, none);
        ctx->OMSetBlendState(bsOff.Get(), nullptr, 0xFFFFFFFF); ctx->OMSetDepthStencilState(dsOn.Get(), 0); ctx->RSSetState(rsCull.Get());
    }
    // the parts in the HDR picture: positions turned from camera space into the world (so the world's light falls on them),
    // seen with the HUD's own view angle, squeezed into the front 0.2 % of the depth so the world never cuts into them
    void DrawHudPartsLit(const FrameCB& fc, V3 f, V3 r, V3 u, ID3D11ShaderResourceView* const* srvs, ID3D11SamplerState* const* samps) {
        FrameCB hc = fc; XMStoreFloat4x4(&hc.viewProj, view * HudProj((float)rw / std::max(1, rh)));
        hc.clipPlane[0] = hc.clipPlane[1] = hc.clipPlane[2] = 0; hc.clipPlane[3] = 1;
        ctx->UpdateSubresource(cbFrame.Get(), 0, nullptr, &hc, 0, 0);
        ctx->OMSetRenderTargets(1, reflRTV.GetAddressOf(), dsv.Get());
        D3D11_VIEWPORT v{ 0, 0, (float)rw, (float)rh, 0, 0.002f }; ctx->RSSetViewports(1, &v);
        ctx->RSSetState(rsNoCull.Get()); ctx->OMSetDepthStencilState(dsOn.Get(), 0); ctx->OMSetBlendState(bsOff.Get(), nullptr, 0xFFFFFFFF);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); ctx->IASetInputLayout(layout.Get()); ctx->VSSetShader(vsBlock.Get(), nullptr, 0);
        ctx->PSSetShaderResources(0, 5, srvs); ctx->PSSetSamplers(0, 3, samps);
        ID3D11Buffer* cbs[2] = { cbFrame.Get(), cbChunk.Get() }; ctx->VSSetConstantBuffers(0, 2, cbs); ctx->PSSetConstantBuffers(0, 2, cbs);
        auto W = [&](V3 c) { return r * c.x + u * c.y + f * c.z; };   // camera space -> the world's directions
        for (const CamPart& c : CameraParts(hudSway, weapPose)) {
            FrameRef fr = ModelNamed(c.model); if (!fr) continue;
            {   // two slices of depth: the cannon and the helmet never cut into each other
                bool upper = c.cannon == weapons.cannonInFront; D3D11_VIEWPORT sv{ 0, 0, (float)rw, (float)rh, upper ? 0.f : 0.001f, upper ? 0.001f : 0.002f }; ctx->RSSetViewports(1, &sv);
            }
            const ModelGpu& g = c.gpu ? *c.gpu : GpuModel(c.model);
            if (g.mesh) ctx->PSSetShaderResources(9, 1, g.tex.GetAddressOf());
            V3 ax = W(c.ax), ay = W(c.ay), az = W(c.az), base = W(c.base);
            for (int pass = 0; pass < 2; pass++) {
                ctx->PSSetShader(pass == 0 ? psOpaque.Get() : psCutout.Get(), nullptr, 0);
                for (auto& part : g.parts) {
                    const GpuChunk& gc = part.second; UINT n = pass == 0 ? gc.nO : gc.nC; if (!n) continue;
                    V3 l{ part.first.x * (double)CS - fr->pivot.x, part.first.y * (double)CS - fr->pivot.y, part.first.z * (double)CS - fr->pivot.z };
                    V3 org = base + ax * l.x + ay * l.y + az * l.z;
                    ChunkCB cc{ { (float)org.x, (float)org.y, (float)org.z, 0 }, { (float)ax.x, (float)ax.y, (float)ax.z, 0 }, { (float)ay.x, (float)ay.y, (float)ay.z, 0 }, { (float)az.x, (float)az.y, (float)az.z, 0 } };
                    ctx->UpdateSubresource(cbChunk.Get(), 0, nullptr, &cc, 0, 0);
                    UINT stride = sizeof(Vtx), off = 0;
                    ctx->IASetVertexBuffers(0, 1, pass == 0 ? gc.vbO.GetAddressOf() : gc.vbC.GetAddressOf(), &stride, &off);
                    ctx->IASetIndexBuffer(pass == 0 ? gc.ibO.Get() : gc.ibC.Get(), DXGI_FORMAT_R32_UINT, 0);
                    ctx->DrawIndexed(n, 0, 0);
                }
            }
        }
        ID3D11ShaderResourceView* none = nullptr; ctx->PSSetShaderResources(9, 1, &none);
        D3D11_VIEWPORT full{ 0, 0, (float)rw, (float)rh, 0, 1 }; ctx->RSSetViewports(1, &full);
        ctx->UpdateSubresource(cbFrame.Get(), 0, nullptr, &fc, 0, 0); ctx->RSSetState(rsCull.Get());
    }
    XMMATRIX HudProj(float aspect) const { return XMMatrixPerspectiveFovLH(XMConvertToRadians(hud.fov), aspect, 0.01f, 60.f); }   // its own view angle: the HUD keeps its place whatever the player's field of view
    // Play mode: the HUD over the finished picture
    void DrawHudInGame(ID3D11RenderTargetView* out) {
        if ((hud.empty() && weapons.empty()) || !hudListsReady) return;
        ctx->OMSetRenderTargets(1, &out, dsv.Get()); ctx->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
        D3D11_VIEWPORT v{ 0, 0, (float)rw, (float)rh, 0, 1 }; ctx->RSSetViewports(1, &v);
        DrawHudScene(HudProj((float)rw / rh), hudSway, false, dsv.Get());
    }
    // the editor's two pictures: around the camera (orbit), and through it (the preview, over the world as the editor camera sees it)
    void RenderHudViews() {
        if (!playerEdOpen || !hudListsReady) return;
        const float bg[4] = { 0.16f, 0.17f, 0.20f, 1 };
        if (hudViewWant.x >= 16 && hudViewWant.y >= 16) {
            int w = (int)hudViewWant.x, h = (int)hudViewWant.y; if (w != hudViewDW || h != hudViewDH) { MakeTarget(hudView, w, h, &hudViewDSV); hudViewDW = w; hudViewDH = h; }
            ctx->OMSetRenderTargets(1, hudView.rtv.GetAddressOf(), hudViewDSV.Get()); ctx->ClearRenderTargetView(hudView.rtv.Get(), bg); ctx->ClearDepthStencilView(hudViewDSV.Get(), D3D11_CLEAR_DEPTH, 1, 0);
            D3D11_VIEWPORT v{ 0, 0, (float)w, (float)h, 0, 1 }; ctx->RSSetViewports(1, &v);
            DrawHudScene(hudOrbVP, HudSway{}, true, hudViewDSV.Get());
        }
        if (hudPrevWant.x >= 16 && hudPrevWant.y >= 16) {
            int w = (int)hudPrevWant.x, h = (int)hudPrevWant.y; if (w != hudPrevDW || h != hudPrevDH) { MakeTarget(hudPrev, w, h, &hudPrevDSV); hudPrevDW = w; hudPrevDH = h; }
            ctx->OMSetRenderTargets(1, hudPrev.rtv.GetAddressOf(), hudPrevDSV.Get()); ctx->ClearRenderTargetView(hudPrev.rtv.Get(), bg); ctx->ClearDepthStencilView(hudPrevDSV.Get(), D3D11_CLEAR_DEPTH, 1, 0);
            D3D11_VIEWPORT v{ 0, 0, (float)w, (float)h, 0, 1 }; ctx->RSSetViewports(1, &v);
            if (sceneSRV) {   // the world behind it
                ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); ctx->RSSetState(rsNoCull.Get()); ctx->OMSetDepthStencilState(dsOff.Get(), 0); ctx->OMSetBlendState(bsOff.Get(), nullptr, 0xFFFFFFFF);
                ctx->VSSetShader(vsBlit.Get(), nullptr, 0); ctx->PSSetShader(psBlit.Get(), nullptr, 0); ctx->PSSetShaderResources(0, 1, sceneSRV.GetAddressOf()); ctx->PSSetSamplers(3, 1, sampClamp.GetAddressOf());
                ctx->Draw(3, 0); ID3D11ShaderResourceView* none = nullptr; ctx->PSSetShaderResources(0, 1, &none);
            }
            DrawHudScene(HudProj((float)w / h), previewSway, true, hudPrevDSV.Get());
        }
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
    }
    // the sway: Play mode follows the player, the editor's preview follows the editor camera (and the "Wiggle" button)
    void UpdateHudSway(double dt) {
        auto wrapDeg = [](double r) { double d = r * 180 / 3.14159265358979; while (d > 180) d -= 360; while (d < -180) d += 360; return (float)d; };
        if (playMode) {
            float ty = wrapDeg(player.yaw - prevPlayYaw), tp = wrapDeg(player.pitch - prevPlayPitch);
            bool landed = player.onGround && !prevOnGround; float fall = (float)std::max(0.0, -prevVelY);
            float speed = (float)std::sqrt(player.vel.x * player.vel.x + player.vel.z * player.vel.z) * (player.onGround ? 1.f : 0.f);
            hudSway.update(hud.sway, dt, ty, tp, speed, landed, fall);
            weapPose.update(weapons.motion, dt, speed, landed, fall, firedPending); firedPending = 0;
            UpdatePlayerSounds(dt, landed, fall, prevOnGround && !player.onGround && player.vel.y > 0.5);
        }
        prevPlayYaw = player.yaw; prevPlayPitch = player.pitch; prevVelY = player.vel.y; prevOnGround = player.onGround;
        if (playerEdOpen && !playMode) {
            float ty = wrapDeg(player.yaw - prevEdYaw), tp = wrapDeg(player.pitch - prevEdPitch), speed = 0;
            if (hudWiggle >= 0) {   // a quick look left and right, then still
                double t0 = hudWiggle; hudWiggle += dt;
                auto look = [](double t) { return t < 1.2 ? 35.0 * std::sin(t * 3.14159265358979 / 0.6) * (t < 0.6 ? 1 : 0.6) : 0.0; };
                ty += (float)(look(hudWiggle) - look(t0)); speed = hudWiggle < 1.6 ? 4.3f : 0.f;
                if (hudWiggle > 3) hudWiggle = -1;
            }
            previewSway.update(hud.sway, dt, ty, tp, speed, false, 0);
            previewPose.update(weapons.motion, dt, speed, false, 0, previewFired); previewFired = 0;
        }
        UpdateCannon(dt); UpdateAimCannon(dt);
        if (!playMode) UpdateTimelines(dt, false);   // the editor's preview plays timelines too (sounds, the kick)
        prevEdYaw = player.yaw; prevEdPitch = player.pitch;
    }

    // ---- the Player editor window (HUD tab)
    V3 HudOrbProject(V3 p, ImVec2 o, ImVec2 sz, bool& ok) const {
        XMVECTOR c = XMVector4Transform(XMVectorSet((float)p.x, (float)p.y, (float)p.z, 1), hudOrbVP); float w = XMVectorGetW(c); ok = w > 0.02f;
        return { o.x + (XMVectorGetX(c) / w * 0.5f + 0.5f) * sz.x, o.y + (0.5f - XMVectorGetY(c) / w * 0.5f) * sz.y, w };
    }
    std::vector<std::string> PartModels() {
        std::vector<std::string> v; for (auto& kv : meshModels) if (!IsBuiltin(kv.first) || kv.first == "@visor-strut" || kv.first == "@arm-cannon") v.push_back(kv.first);
        for (auto& kv : models.models) v.push_back(kv.first);
        std::sort(v.begin(), v.end()); return v;
    }
    bool HudSelValid() {
        hudPlane = std::clamp(hudPlane, 0, std::max(0, (int)hud.planes.size() - 1)); hudPart = std::clamp(hudPart, 0, std::max(0, (int)hud.parts.size() - 1));
        if (hudSel == 1 && hud.planes.empty()) hudSel = 0;
        if (hudSel == 2 && (hud.planes.empty() || hudEl < 0 || hudEl >= (int)hud.planes[hudPlane].elements.size())) hudSel = hud.planes.empty() ? 0 : 1;
        if (hudSel == 3 && hud.parts.empty()) hudSel = 0;
        return true;
    }
    void HudDuplicate(bool mirror) {
        if (hudSel == 1) { HudPlane p = hud.planes[hudPlane]; p.name += mirror ? " (mirrored)" : " copy"; if (mirror) mirrorPlane(p); else p.pos.y -= 0.1; hud.planes.insert(hud.planes.begin() + hudPlane + 1, p); hudPlane++; }
        else if (hudSel == 2) { auto& els = hud.planes[hudPlane].elements; HudElement e = els[hudEl]; e.name += mirror ? " (mirrored)" : " copy"; if (mirror) mirrorElement(e); else { e.x += 0.02f; e.y += 0.02f; } els.insert(els.begin() + hudEl + 1, e); hudEl++; }
        else if (hudSel == 3) { HudPart p = hud.parts[hudPart]; p.name += mirror ? " (mirrored)" : " copy"; if (mirror) mirrorPart(p); else p.pos.x += 0.1; hud.parts.insert(hud.parts.begin() + hudPart + 1, p); hudPart++; }
        SaveHud();
    }
    void HudMirrorInPlace() {
        if (hudSel == 1) mirrorPlane(hud.planes[hudPlane]); else if (hudSel == 2) mirrorElement(hud.planes[hudPlane].elements[hudEl]); else if (hudSel == 3) mirrorPart(hud.parts[hudPart]);
        SaveHud();
    }
    void HudDelete() {
        if (hudSel == 1) { hud.planes.erase(hud.planes.begin() + hudPlane); hudSel = 0; }
        else if (hudSel == 2) { auto& els = hud.planes[hudPlane].elements; els.erase(els.begin() + hudEl); hudSel = 1; hudEl = -1; }
        else if (hudSel == 3) { hud.parts.erase(hud.parts.begin() + hudPart); hudSel = 0; }
        SaveHud();
    }
    void HudAddElement(HudKind k) {
        if (hud.planes.empty()) { HudPlane p; p.name = "Visor"; hud.planes.push_back(p); hudPlane = 0; }
        HudElement e; e.kind = k; e.name = std::string(hudKindName(k)) + " " + std::to_string(hud.planes[hudPlane].elements.size() + 1);
        if (k == HudKind::Text) { e.text = "TEXT"; e.textSize = 1; e.h = 0.06f; e.w = 0.25f; }
        if (k == HudKind::Bar) { e.value = HudValue::Health; e.h = 0.025f; e.w = 0.3f; e.segments = 10; }
        if (k == HudKind::Image) { e.color = 0xFFFFFF; e.alpha = 1; e.w = 0.12f; e.h = 0.2f; auto f = HudImageFiles(); if (!f.empty()) e.image = f[0]; }
        if (k == HudKind::Circle) { e.w = 0.1f; e.h = 0.18f; e.filled = false; e.thickness = 3; }
        hud.planes[hudPlane].elements.push_back(e); hudSel = 2; hudEl = (int)hud.planes[hudPlane].elements.size() - 1; SaveHud();
    }
    void HudList() {
        float ui = settings.uiScale;
        if (ImGui::Selectable("Sway and view", hudSel == 0)) hudSel = 0;
        ImGui::Spacing(); ImGui::TextDisabled("PLANES (2D)");
        for (int i = 0; i < (int)hud.planes.size(); i++) {
            ImGui::PushID(i); HudPlane& p = hud.planes[i];
            if (!p.visible) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1));
            if (ImGui::Selectable(p.name.c_str(), hudSel == 1 && hudPlane == i)) { hudSel = 1; hudPlane = i; }
            if (!p.visible) ImGui::PopStyleColor();
            ImGui::Indent(12 * ui);
            for (int k = 0; k < (int)p.elements.size(); k++) {
                ImGui::PushID(k); const HudElement& e = p.elements[k];
                std::string label = std::string(e.name) + "  (" + hudKindName(e.kind) + ")";
                if (!e.visible) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1));
                if (ImGui::Selectable(label.c_str(), hudSel == 2 && hudPlane == i && hudEl == k)) { hudSel = 2; hudPlane = i; hudEl = k; }
                if (!e.visible) ImGui::PopStyleColor();
                ImGui::PopID();
            }
            ImGui::Unindent(12 * ui); ImGui::PopID();
        }
        if (ImGui::SmallButton("+ Plane")) { HudPlane p; p.name = "Plane " + std::to_string(hud.planes.size() + 1); p.elements.clear(); hud.planes.push_back(p); hudSel = 1; hudPlane = (int)hud.planes.size() - 1; SaveHud(); }
        ImGui::SameLine();
        if (ImGui::SmallButton("+ Element")) ImGui::OpenPopup("addel");
        if (ImGui::BeginPopup("addel")) { for (int k = 0; k < (int)HudKind::COUNT; k++) if (ImGui::MenuItem(hudKindName((HudKind)k))) HudAddElement((HudKind)k); ImGui::EndPopup(); }
        ImGui::Spacing(); ImGui::TextDisabled("3D PARTS");
        for (int i = 0; i < (int)hud.parts.size(); i++) {
            ImGui::PushID(1000 + i); const HudPart& p = hud.parts[i];
            std::string label = p.name + (p.mirror ? "  (mirrored)" : "");
            if (ImGui::Selectable(label.c_str(), hudSel == 3 && hudPart == i)) { hudSel = 3; hudPart = i; }
            ImGui::PopID();
        }
        if (ImGui::SmallButton("+ 3D part")) ImGui::OpenPopup("addpart");
        if (ImGui::BeginPopup("addpart")) {
            for (const std::string& n : PartModels()) if (ImGui::MenuItem(n.c_str())) { HudPart p; p.model = n; p.name = n == "@visor-strut" ? "Strut" : n; hud.parts.push_back(p); hudSel = 3; hudPart = (int)hud.parts.size() - 1; SaveHud(); }
            ImGui::Separator(); ImGui::TextDisabled("Import models in the Assets panel (Import...).");
            ImGui::EndPopup();
        }
        ImGui::Separator();
        bool can = hudSel != 0;
        ImGui::BeginDisabled(!can);
        if (FlowButton("Duplicate")) HudDuplicate(false);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Ctrl+D");
        if (FlowButton("Mirror copy")) HudDuplicate(true);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("A copy on the other side, flipped");
        if (FlowButton("Mirror")) HudMirrorInPlace();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Flip this one to the other side");
        if (FlowButton("Delete")) HudDelete();
        ImGui::EndDisabled();
        if (can && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) HudDuplicate(false);
    }
    bool DragV3(const char* label, V3& v, float speed, const char* fmt = "%.2f") {
        float f[3] = { (float)v.x, (float)v.y, (float)v.z }; Prop(label);
        if (ImGui::DragFloat3((std::string("##") + label).c_str(), f, speed, 0, 0, fmt)) { v = { f[0], f[1], f[2] }; return true; } return false;
    }
    void HudProps() {
        float ui = settings.uiScale; bool ch = false;
        if (hudSel == 0) {
            HudSwaySettings& s = hud.sway;
            ImGui::SeparatorText("Sway");
            Prop("On"); ch |= ImGui::Checkbox("##swon", &s.enabled);
            Prop("Strength"); ch |= ImGui::SliderFloat("##sws", &s.strength, 0, 3, "%.2f");
            Prop("Smoothness"); ch |= ImGui::SliderFloat("##swm", &s.smoothness, 0, 1, "%.2f");
            Prop("Walk bob"); ch |= ImGui::SliderFloat("##swb", &s.bob, 0, 2, "%.2f");
            Prop("Lean when turning"); ch |= ImGui::SliderFloat("##swt", &s.tilt, 0, 2, "%.2f");
            Prop("Landing shake"); ch |= ImGui::SliderFloat("##swl", &s.land, 0, 3, "%.2f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("A dip and a shake when you land; the faster you fall, the bigger.");
            Hint("When you turn, the HUD stays behind a little and springs back. Strength: how far. Smoothness: 0 snaps back, 1 floats back slowly. Each plane and part has its own sway amount too (helmet parts usually less than the visor).");
            if (ImGui::Button("Wiggle the preview")) hudWiggle = 0;
            ImGui::SameLine(); if (ImGui::Button("Land!")) previewSway.update(hud.sway, 0, 0, 0, 0, true, 24);
            ImGui::SameLine(); ImGui::TextDisabled("or fly and look around");
            ImGui::SeparatorText("View");
            Prop("HUD view angle"); ch |= ImGui::SliderFloat("##hfov", &hud.fov, 40, 120, "%.0f deg");
            Hint("The HUD is laid out for this view angle and always stays where you put it on the screen, whatever field of view the player picks in the settings.");
        } else if (hudSel == 1) {
            HudPlane& p = hud.planes[hudPlane];
            ImGui::SeparatorText("Plane");
            Prop("Name"); ch |= ImGui::InputText("##pn", &p.name);
            Prop("Shown"); ch |= ImGui::Checkbox("##pv", &p.visible);
            ch |= DragV3("Position", p.pos, 0.01f);
            Hint("x right, y up, z how far in front (blocks). Put it nearer or farther than the 3D parts to go in front of or behind them.");
            { float r[3] = { p.yaw, p.pitch, p.roll }; Prop("Turn"); if (ImGui::DragFloat3("##pr", r, 0.5f, -180, 180, "%.0f deg")) { p.yaw = r[0]; p.pitch = r[1]; p.roll = r[2]; ch = true; } }
            { float s[2] = { p.width, p.height }; Prop("Size"); if (ImGui::DragFloat2("##ps", s, 0.01f, 0.05f, 20, "%.2f")) { p.width = std::max(0.05f, s[0]); p.height = std::max(0.05f, s[1]); ch = true; } }
            if (ImGui::SmallButton("Fill the screen")) { double hh = 2 * p.pos.z * std::tan(XMConvertToRadians(hud.fov) * 0.5); p.height = (float)(hh * 0.94); p.width = (float)(hh * rw / std::max(1, rh) * 0.94); ch = true; }
            Prop("Bend side to side"); ch |= ImGui::SliderFloat("##pbx", &p.bendX, 0, 1, "%.2f");
            Prop("Bend top to bottom"); ch |= ImGui::SliderFloat("##pby", &p.bendY, 0, 1, "%.2f");
            Prop("Sway amount"); ch |= ImGui::SliderFloat("##psw", &p.sway, 0, 2, "%.2f");
            Prop("Opacity"); ch |= ImGui::SliderFloat("##pop", &p.opacity, 0, 1, "%.2f");
            Prop("Glow"); ch |= ImGui::Checkbox("##pgl", &p.glow); ImGui::SameLine(); ImGui::TextDisabled("(adds light, like a visor)");
        } else if (hudSel == 2) {
            HudElement& e = hud.planes[hudPlane].elements[hudEl];
            ImGui::SeparatorText(hudKindName(e.kind));
            Prop("Name"); ch |= ImGui::InputText("##en", &e.name);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Event cards show, hide and flash it by this name");
            { int k = (int)e.kind; const char* kn[] = { "Panel", "Bar", "Text", "Image", "Circle" }; Prop("Kind"); if (ImGui::Combo("##ek", &k, kn, 5)) { e.kind = (HudKind)k; ch = true; } }
            Prop("Shown at the start"); ch |= ImGui::Checkbox("##ev", &e.visible);
            { float r[4] = { e.x, e.y, e.w, e.h }; Prop("Place and size"); if (ImGui::DragFloat4("##er", r, 0.002f, 0, 1, "%.3f")) { e.x = r[0]; e.y = r[1]; e.w = std::max(0.002f, r[2]); e.h = std::max(0.002f, r[3]); ch = true; } }
            { float c[3] = { ((e.color >> 16) & 255) / 255.f, ((e.color >> 8) & 255) / 255.f, (e.color & 255) / 255.f }; Prop("Colour");
              if (ImGui::ColorEdit3("##ec", c)) { e.color = ((uint32_t)(c[0] * 255 + 0.5f) << 16) | ((uint32_t)(c[1] * 255 + 0.5f) << 8) | (uint32_t)(c[2] * 255 + 0.5f); ch = true; } }
            Prop("See-through"); ch |= ImGui::SliderFloat("##ea", &e.alpha, 0, 1, "%.2f");
            if (e.kind == HudKind::Text) {
                Prop("Text"); ch |= ImGui::InputTextWithHint("##et", "{value}", &e.text);
                Hint("{value} shows the value below, {max} the most it can be.");
                Prop("Letter size"); ch |= ImGui::SliderFloat("##ets", &e.textSize, 0.2f, 2, "%.2f");
                { int a = e.align; const char* an[] = { "Left", "Middle", "Right" }; Prop("Line up"); if (ImGui::Combo("##eal", &a, an, 3)) { e.align = a; ch = true; } }
            }
            if (e.kind == HudKind::Bar) { Prop("Pieces"); ch |= ImGui::SliderInt("##eseg", &e.segments, 1, 20); }
            if (e.kind == HudKind::Panel || e.kind == HudKind::Circle) {
                Prop("Filled"); ch |= ImGui::Checkbox("##ef", &e.filled);
                if (!e.filled) { Prop("Line"); ch |= ImGui::SliderFloat("##eth", &e.thickness, 1, 16, "%.0f px"); }
            }
            if (e.kind == HudKind::Image) {
                Prop("Picture");
                if (ImGui::BeginCombo("##eim", e.image.empty() ? "(none)" : e.image.c_str())) {
                    for (const std::string& f : HudImageFiles()) if (ImGui::Selectable(f.c_str(), e.image == f)) { e.image = f; ch = true; }
                    ImGui::EndCombo();
                }
                if (ImGui::SmallButton("Import pictures...")) {
                    std::error_code ec; fs::create_directories(fs::u8path(projectDir + "/hud"), ec); int n = 0;
                    for (const std::string& f : PickImages()) { fs::path s = fs::u8path(f); fs::copy_file(s, fs::u8path(projectDir + "/hud/" + s.filename().u8string()), fs::copy_options::overwrite_existing, ec); if (!ec) { n++; e.image = s.filename().u8string(); hudImages.erase(e.image); } }
                    if (n) { Toast(std::to_string(n) + " picture(s) added to the HUD folder"); ch = true; }
                }
            }
            if (e.kind == HudKind::Bar || e.kind == HudKind::Text) {
                int v = (int)e.value; const char* vn[] = { "Nothing", "Health", "A counter", "Has a key", "Beam charge" };
                Prop("Shows"); if (ImGui::Combo("##evl", &v, vn, 5)) { e.value = (HudValue)v; ch = true; }
                if (e.value == HudValue::Counter || e.value == HudValue::Key) { Prop(e.value == HudValue::Key ? "Key" : "Counter"); ch |= ImGui::InputTextWithHint("##evn", e.value == HudValue::Key ? "Red key" : "Missile", &e.valueName); }
                if (e.value == HudValue::Counter || e.value == HudValue::None) { Prop("Full at"); ch |= ImGui::DragFloat("##emx", &e.max, 1, 1, 100000, "%.0f"); }
                if (e.value == HudValue::Health) Hint("Full at the player's most health (Game settings).");
            }
            Hint("Drag it in the layout below the preview. Event cards can show, hide or flash \"%s\".", e.name.c_str());
        } else if (hudSel == 3) {
            HudPart& p = hud.parts[hudPart];
            ImGui::SeparatorText("3D part");
            Prop("Name"); ch |= ImGui::InputText("##qn", &p.name);
            Prop("Model");
            if (ImGui::BeginCombo("##qm", p.model.empty() ? "(none)" : p.model.c_str())) { for (const std::string& n : PartModels()) if (ImGui::Selectable(n.c_str(), p.model == n)) { p.model = n; ch = true; } ImGui::EndCombo(); }
            Prop("Shown"); ch |= ImGui::Checkbox("##qv", &p.visible);
            ch |= DragV3("Position", p.pos, 0.005f, "%.3f");
            { float r[3] = { p.yaw, p.pitch, p.roll }; Prop("Turn"); if (ImGui::DragFloat3("##qr", r, 0.5f, -180, 180, "%.0f deg")) { p.yaw = r[0]; p.pitch = r[1]; p.roll = r[2]; ch = true; } }
            { float s = (float)p.scale; Prop("Size"); if (ImGui::DragFloat("##qs", &s, 0.005f, 0.005f, 20, "x %.3f")) { p.scale = std::max(0.005f, s); ch = true; } }
            Prop("Mirrored"); ch |= ImGui::Checkbox("##qmi", &p.mirror);
            Prop("Sway amount"); ch |= ImGui::SliderFloat("##qsw", &p.sway, 0, 2, "%.2f");
            Hint("Camera space: x right, y up, z forward, in blocks. The camera is at 0, 0, 0.");
        }
        if (ch) SaveHud();
    }
    // the plane seen flat: drag elements to move them, their corner to size them
    void HudLayoutCanvas(ImVec2 size) {
        if (hud.planes.empty() || hudSel < 1 || hudSel > 2) { ImGui::Dummy(size); ImVec2 a = ImGui::GetItemRectMin(); ImGui::GetWindowDrawList()->AddText(ImVec2(a.x + 8, a.y + 8), IM_COL32(170, 170, 180, 200), "Pick a plane or an element to lay it out here."); return; }
        HudPlane& p = hud.planes[hudPlane]; float asp = p.width / std::max(0.01f, p.height);
        ImVec2 cs(size.x, size.x / asp); if (cs.y > size.y) cs = ImVec2(size.y * asp, size.y);
        ImVec2 o = ImGui::GetCursorScreenPos(); o.x += (size.x - cs.x) * 0.5f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(o, ImVec2(o.x + cs.x, o.y + cs.y), IM_COL32(18, 24, 32, 255));
        if (hudPlane < (int)hudTargets.size() && hudTargets[hudPlane].srv) {   // the picture is premultiplied: draw it with the matching blend
            dl->AddCallback([](const ImDrawList*, const ImDrawCmd* c) { App* a = (App*)c->UserCallbackData; a->ctx->OMSetBlendState(a->bsPremul.Get(), nullptr, 0xFFFFFFFF); }, this);
            dl->AddImage((ImTextureID)(intptr_t)hudTargets[hudPlane].srv.Get(), o, ImVec2(o.x + cs.x, o.y + cs.y));
            dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
        }
        dl->AddRect(o, ImVec2(o.x + cs.x, o.y + cs.y), IM_COL32(120, 130, 150, 160));
        ImGui::SetCursorScreenPos(o); ImGui::InvisibleButton("layout", cs);
        ImGuiIO& io = ImGui::GetIO(); ImVec2 m((io.MousePos.x - o.x) / cs.x, (io.MousePos.y - o.y) / cs.y);
        auto rectOf = [&](const HudElement& e) { return std::make_pair(ImVec2(o.x + e.x * cs.x, o.y + e.y * cs.y), ImVec2(o.x + (e.x + e.w) * cs.x, o.y + (e.y + e.h) * cs.y)); };
        for (int k = 0; k < (int)p.elements.size(); k++) { auto r = rectOf(p.elements[k]); bool s = hudSel == 2 && hudEl == k; dl->AddRect(r.first, r.second, s ? IM_COL32(255, 210, 80, 255) : IM_COL32(255, 255, 255, 50), 0, 0, s ? 2.f : 1.f);
            if (s) dl->AddRectFilled(ImVec2(r.second.x - 5, r.second.y - 5), ImVec2(r.second.x + 5, r.second.y + 5), IM_COL32(255, 210, 80, 255)); }
        if (ImGui::IsItemClicked()) {
            hudDrag = 0;
            if (hudSel == 2) { auto r = rectOf(p.elements[hudEl]); if (std::fabs(io.MousePos.x - r.second.x) < 8 && std::fabs(io.MousePos.y - r.second.y) < 8) hudDrag = 2; }
            if (!hudDrag) {
                int hit = -1; for (int k = (int)p.elements.size() - 1; k >= 0; k--) { const HudElement& e = p.elements[k]; if (m.x >= e.x && m.x <= e.x + e.w && m.y >= e.y && m.y <= e.y + e.h) { hit = k; break; } }
                if (hit >= 0) { hudSel = 2; hudEl = hit; hudDrag = 1; } else { hudSel = 1; }
            }
            if (hudDrag) { hudDragMouse = io.MousePos; hudDragFrom = p.elements[hudEl]; }
        }
        if (hudDrag && ImGui::IsItemActive() && hudSel == 2) {
            HudElement& e = p.elements[hudEl]; float dx = (io.MousePos.x - hudDragMouse.x) / cs.x, dy = (io.MousePos.y - hudDragMouse.y) / cs.y;
            auto snap = [&](float v) { return io.KeyShift ? std::round(v * 100) / 100 : v; };
            if (hudDrag == 1) { e.x = snap(hudDragFrom.x + dx); e.y = snap(hudDragFrom.y + dy); }
            else { e.w = std::max(0.005f, snap(hudDragFrom.w + dx)); e.h = std::max(0.005f, snap(hudDragFrom.h + dy)); }
        }
        if (hudDrag && !ImGui::IsItemActive()) { hudDrag = 0; SaveHud(); }
    }
    void PlayerEditor() {
        if (!playerEdOpen) return;
        float ui = settings.uiScale;
        const ImGuiViewport* mv = ImGui::GetMainViewport();
        ImGui::SetNextWindowSize(ImVec2(mv->WorkSize.x * 0.92f, mv->WorkSize.y * 0.9f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(mv->WorkPos.x + mv->WorkSize.x * 0.5f, mv->WorkPos.y + mv->WorkSize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        if (focusPlayerEd) { ImGui::SetNextWindowFocus(); focusPlayerEd = false; }
        hudViewWant = hudPrevWant = weapWant = ImVec2(0, 0);
        if (!ImGui::Begin("Player editor", &playerEdOpen)) { ImGui::End(); return; }
        if (ImGui::BeginTabBar("player")) {
            if (ImGui::BeginTabItem("HUD")) {
                if (hud.empty()) {
                    ImGui::Spacing(); Hint("Your HUD: flat pictures (energy, missiles, text, images...) on planes in front of the camera, which can bend like a visor, and 3D models fixed to the camera (your helmet). Everything sways behind the camera when you turn, like in Metroid Prime.");
                    if (ImGui::Button("Start with the example visor")) { hud = defaultHud(); hudSel = 1; hudPlane = 0; SaveHud(); }
                    ImGui::SameLine(); if (ImGui::Button("Start empty (one plane)")) { HudPlane p; p.elements.clear(); hud.planes.push_back(p); hudSel = 1; hudPlane = 0; SaveHud(); }
                } else {
                    HudSelValid();
                    float listW = 230 * ui, propW = 330 * ui, sp = ImGui::GetStyle().ItemSpacing.x;
                    ImGui::BeginChild("hudlist", ImVec2(listW, 0), ImGuiChildFlags_Borders); HudList(); ImGui::EndChild();
                    ImGui::SameLine();
                    float midW = std::max(200.f, ImGui::GetContentRegionAvail().x - propW - sp), midH = ImGui::GetContentRegionAvail().y;
                    ImGui::BeginChild("hudmid", ImVec2(midW, midH), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
                    {   // around the camera
                        ImVec2 isz(midW, std::floor(midH * 0.52f)); hudViewWant = isz;
                        ImVec2 o = ImGui::GetCursorScreenPos();
                        if (hudView.srv) ImGui::Image((ImTextureID)(intptr_t)hudView.srv.Get(), isz); else ImGui::Dummy(isz);
                        ImGui::SetCursorScreenPos(o); ImGui::InvisibleButton("orbit", isz, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
                        ImGuiIO& io = ImGui::GetIO();
                        if (ImGui::IsItemActive() && (std::fabs(io.MouseDelta.x) + std::fabs(io.MouseDelta.y) > 0)) { hudOrbYaw += io.MouseDelta.x * 0.01f; hudOrbPitch = std::clamp(hudOrbPitch + io.MouseDelta.y * 0.01f, -1.4f, 1.4f); }
                        if (ImGui::IsItemHovered() && io.MouseWheel != 0) hudOrbDist = std::clamp(hudOrbDist * (io.MouseWheel > 0 ? 0.88f : 1.14f), 0.4f, 30.f);
                        V3 target{ 0, 0, 0.7 };
                        hudOrbEye = { target.x + std::sin(hudOrbYaw) * std::cos(hudOrbPitch) * hudOrbDist, target.y + std::sin(hudOrbPitch) * hudOrbDist, target.z - std::cos(hudOrbYaw) * std::cos(hudOrbPitch) * hudOrbDist };
                        hudOrbVP = XMMatrixLookAtLH(XMVectorSet((float)hudOrbEye.x, (float)hudOrbEye.y, (float)hudOrbEye.z, 1), XMVectorSet((float)target.x, (float)target.y, (float)target.z, 1), XMVectorSet(0, 1, 0, 0))
                                 * XMMatrixPerspectiveFovLH(XMConvertToRadians(45), isz.x / isz.y, 0.02f, 200);
                        ImDrawList* dl = ImGui::GetWindowDrawList(); dl->PushClipRect(o, ImVec2(o.x + isz.x, o.y + isz.y), true);
                        auto L = [&](V3 a, V3 b, ImU32 c, float t = 1.f) { bool ka, kb; V3 p = HudOrbProject(a, o, isz, ka), q = HudOrbProject(b, o, isz, kb); if (ka && kb) dl->AddLine(ImVec2((float)p.x, (float)p.y), ImVec2((float)q.x, (float)q.y), c, t); };
                        // the camera: a little body and what it sees (out to 1.6 blocks)
                        double th = std::tan(XMConvertToRadians(hud.fov) * 0.5), asp = (double)rw / std::max(1, rh);
                        for (double z : { 1.0, 1.6 }) { V3 c[4] = { { -th * asp * z, th * z, z }, { th * asp * z, th * z, z }, { th * asp * z, -th * z, z }, { -th * asp * z, -th * z, z } };
                            for (int k = 0; k < 4; k++) { L(c[k], c[(k + 1) & 3], IM_COL32(255, 255, 255, z == 1.0 ? 90 : 50)); if (z > 1.5) L({ 0, 0, 0 }, c[k], IM_COL32(255, 255, 255, 50)); } }
                        L({ -0.08, -0.06, -0.12 }, { 0.08, -0.06, -0.12 }, IM_COL32(255, 200, 120, 200), 2); L({ 0.08, -0.06, -0.12 }, { 0.08, 0.06, -0.12 }, IM_COL32(255, 200, 120, 200), 2);
                        L({ 0.08, 0.06, -0.12 }, { -0.08, 0.06, -0.12 }, IM_COL32(255, 200, 120, 200), 2); L({ -0.08, 0.06, -0.12 }, { -0.08, -0.06, -0.12 }, IM_COL32(255, 200, 120, 200), 2);
                        { bool k; V3 p = HudOrbProject({ 0, 0.1, -0.12 }, o, isz, k); if (k) dl->AddText(ImVec2((float)p.x - 20, (float)p.y - 16), IM_COL32(255, 200, 120, 230), "Camera"); }
                        // what is picked
                        if (hudSel == 1 || hudSel == 2) {
                            const HudPlane& p = hud.planes[hudPlane]; float u0 = 0, v0 = 0, u1 = 1, v1 = 1; ImU32 c = IM_COL32(120, 200, 255, 220);
                            if (hudSel == 2) { const HudElement& e = p.elements[hudEl]; u0 = e.x; v0 = e.y; u1 = e.x + e.w; v1 = e.y + e.h; c = IM_COL32(255, 210, 80, 255); }
                            for (int k = 0; k < 24; k++) { float a = k / 24.f, b = (k + 1) / 24.f;
                                L(planePoint(p, u0 + (u1 - u0) * a, v0), planePoint(p, u0 + (u1 - u0) * b, v0), c, 2); L(planePoint(p, u0 + (u1 - u0) * a, v1), planePoint(p, u0 + (u1 - u0) * b, v1), c, 2);
                                L(planePoint(p, u0, v0 + (v1 - v0) * a), planePoint(p, u0, v0 + (v1 - v0) * b), c, 2); L(planePoint(p, u1, v0 + (v1 - v0) * a), planePoint(p, u1, v0 + (v1 - v0) * b), c, 2); }
                        }
                        if (hudSel == 3) { const HudPart& p = hud.parts[hudPart]; V3 ax, ay, az; partAxes(p, ax, ay, az); double s = 1.0 / std::max(1e-6, p.scale) * 0.15;
                            L(p.pos, p.pos + ax * s, IM_COL32(255, 90, 90, 255), 2); L(p.pos, p.pos + ay * s, IM_COL32(90, 255, 90, 255), 2); L(p.pos, p.pos + az * s, IM_COL32(90, 150, 255, 255), 2); }
                        // click (not drag): pick the nearest plane middle or part
                        if (ImGui::IsItemDeactivated() && std::fabs(io.MouseDragMaxDistanceSqr[0]) < 16) {
                            float best = 40 * 40 * ui * ui; int kind = 0, idx = -1;
                            for (int i = 0; i < (int)hud.planes.size(); i++) { bool k; V3 s = HudOrbProject(planePoint(hud.planes[i], 0.5f, 0.5f), o, isz, k); float d = (float)((s.x - io.MousePos.x) * (s.x - io.MousePos.x) + (s.y - io.MousePos.y) * (s.y - io.MousePos.y)); if (k && d < best) { best = d; kind = 1; idx = i; } }
                            for (int i = 0; i < (int)hud.parts.size(); i++) { bool k; V3 s = HudOrbProject(hud.parts[i].pos, o, isz, k); float d = (float)((s.x - io.MousePos.x) * (s.x - io.MousePos.x) + (s.y - io.MousePos.y) * (s.y - io.MousePos.y)); if (k && d < best) { best = d; kind = 3; idx = i; } }
                            if (kind == 1) { hudSel = 1; hudPlane = idx; } else if (kind == 3) { hudSel = 3; hudPart = idx; }
                        }
                        dl->AddText(ImVec2(o.x + 8, o.y + 6), IM_COL32(200, 200, 210, 200), "Around the camera: drag to turn, wheel to zoom, click a plane or part to pick it");
                        dl->PopClipRect();
                    }
                    {   // through the camera, and the plane laid out flat
                        float rowH = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing(), half = (midW - sp) * 0.5f;
                        float asp = (float)rw / std::max(1, rh); ImVec2 psz(half, half / asp); if (psz.y > rowH) psz = ImVec2(rowH * asp, rowH);
                        ImGui::TextDisabled("Camera preview"); if (ImGui::IsItemHovered()) ImGui::SetTooltip("The world as the editor camera sees it, with the HUD. Fly and look around to see the sway."); ImGui::SameLine(half + sp); ImGui::TextDisabled("Layout (Shift: snap)");
                        ImVec2 o = ImGui::GetCursorScreenPos(); hudPrevWant = ImVec2(std::floor(psz.x), std::floor(psz.y));
                        if (hudPrev.srv) ImGui::Image((ImTextureID)(intptr_t)hudPrev.srv.Get(), psz); else ImGui::Dummy(psz);
                        ImGui::SetCursorScreenPos(ImVec2(o.x + half + sp, o.y));
                        HudLayoutCanvas(ImVec2(half, rowH));
                    }
                    ImGui::EndChild();
                    ImGui::SameLine();
                    ImGui::BeginChild("hudprops", ImVec2(0, 0), ImGuiChildFlags_Borders); HudProps(); ImGui::EndChild();
                }
                ImGui::EndTabItem();
            }
            playerTabWeapons = false;
            if (ImGui::BeginTabItem("Weapons", nullptr, selectWeaponsTab ? ImGuiTabItemFlags_SetSelected : 0)) { playerTabWeapons = true; WeaponsTab(); ImGui::EndTabItem(); }
            selectWeaponsTab = false;
            if (ImGui::BeginTabItem("Sounds", nullptr, selectSoundsTab ? ImGuiTabItemFlags_SetSelected : 0)) { SoundsTab(); ImGui::EndTabItem(); }
            selectSoundsTab = false;
            if (ImGui::BeginTabItem("Body and controls")) { ImGui::Spacing(); Hint("Coming later: the player's model, size, moves and controls (Samus: morph ball, bombs, beams...)."); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }
    // ================================================================== weapons (Samus controls, part 1): beams on arm cannons, missiles
    WeaponSet weapons; WeaponState weapState; WeaponPose weapPose, previewPose;
    bool chargeTlOn = false; float cannonSpeed = 1;
    ParticleEmitter chargeFx{ particlePreset("Charge"), 77 }; V3 chargeAt{ 0, 0, 0 }; bool holdBeam = false;   // holdBeam: screenshots hold the button
    ModelGpu cannonGpu; int cannonClip = -1; float cannonT = 0; bool missilePrev = true; float firedPending = 0, previewFired = 0;
    bool selectWeaponsTab = false, selectSoundsTab = false; bool weapTimelineView = false;
    int weapSel = 0, weapPoint = 1;   // the beam picked in the editor; the point being edited (0 pivot, 1 muzzle, 2 emitter)
    float weapYaw = 2.3f, weapPitch = 0.35f, weapDist = 2.2f; XMMATRIX weapVP = XMMatrixIdentity(); V3 weapEye{ 0, 0, 0 }; ImVec2 weapWant{ 0, 0 };
    HudTarget weapView; ComPtr<ID3D11DepthStencilView> weapDSV; int weapDW = 0, weapDH = 0; int weapDrag = -1; bool playerTabWeapons = false;
    std::string WeaponsPath() const { return projectDir + "/weapons.txt"; }
    bool OwnsBeam(const Beam& b) const { return b.startsOwned || inv.keys.count(b.name) > 0; }
    const Beam* CurrentBeam() {
        if (weapons.empty()) return nullptr;
        weapState.current = std::clamp(weapState.current, 0, (int)weapons.beams.size() - 1);
        return &weapons.beams[weapState.current];
    }
    float ClipLenOf(const std::string& model, const std::string& clip) {
        auto it = meshModels.find(model); if (it == meshModels.end() || clip.empty()) return 0;
        int c = it->second.findClip(clip); return c < 0 ? 0.f : it->second.clips[c].length;
    }
    bool cannonLoop = false;
    float PlayCannonClip(const std::string& clip, bool loop = false) {   // one of the cannon's own animations (if the model has it)
        const Beam* b = CurrentBeam(); if (!b) return 0;
        auto it = meshModels.find(b->model); if (it == meshModels.end() || clip.empty()) return 0;
        int c = it->second.findClip(clip); if (c < 0) return 0;
        cannonClip = c; cannonT = 0; cannonSpeed = 1; cannonLoop = loop; return it->second.clips[c].length;
    }
    void UpdateCannon(double dt) {
        const Beam* b = CurrentBeam(); if (!b || cannonClip < 0) return;
        auto mi = meshModels.find(b->model); if (mi == meshModels.end() || cannonClip >= (int)mi->second.clips.size()) { cannonClip = -1; return; }
        float len = mi->second.clips[cannonClip].length; cannonT += (float)dt * cannonSpeed;
        if (cannonT >= len) { if (cannonLoop && len > 0) cannonT = std::fmod(cannonT, len); else { cannonClip = -1; return; } }
        UploadMeshVerts(cannonGpu, mi->second, mi->second.posed(cannonClip, cannonT)); cannonGpu.mesh = true; cannonGpu.tex = GpuModel(b->model).tex;
    }
    // where the cannon is in camera space: a point on the model (blocks from its corner) -> camera space = base + ax * l.x + ay * l.y + az * l.z
    struct CannonXf { V3 base, ax, ay, az; };
    bool CannonPlacement(const Beam& b, const HudSway& sw, const WeaponPose& wp, CannonXf& x) {
        FrameRef f = ModelNamed(b.model); if (!f) return false;
        WorldObject vo; vo.model = b.model;
        float y = b.viewYaw + wp.yaw + aimYaw, p = b.viewPitch + wp.pitch - aimPitch, r = b.viewRoll + wp.roll, k = b.sway;   // aim: free aim turns it toward the cursor
        x.ax = rotateDeg({ 1, 0, 0 }, y, p, r) * b.scale; x.ay = rotateDeg({ 0, 1, 0 }, y, p, r) * b.scale; x.az = rotateDeg({ 0, 0, 1 }, y, p, r) * b.scale;
        V3 pr = objectPoint(vo, *f, b.pivot.pos);
        x.base = b.viewPos + wp.offset - (x.ax * pr.x + x.ay * pr.y + x.az * pr.z);
        x.ax = rotateDeg(x.ax, sw.yaw * k, sw.pitch * k, sw.roll * k); x.ay = rotateDeg(x.ay, sw.yaw * k, sw.pitch * k, sw.roll * k); x.az = rotateDeg(x.az, sw.yaw * k, sw.pitch * k, sw.roll * k);
        x.base = sw.apply(x.base, k);
        return true;
    }
    V3 CannonPoint(const CannonXf& x, const Beam& b, V3 local) {
        FrameRef f = ModelNamed(b.model); if (!f) return x.base;
        WorldObject vo; vo.model = b.model; V3 l = objectPoint(vo, *f, local);
        return x.base + x.ax * l.x + x.ay * l.y + x.az * l.z;
    }
    static V3 Norm(V3 v) { double l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); return l > 1e-12 ? v * (1 / l) : V3{ 0, 0, 1 }; }
    V3 CannonDir(const CannonXf& x, V3 d) { return Norm(x.ax * d.x + x.ay * d.y + x.az * d.z); }
    // everything fixed to the camera: the HUD's 3D parts and the arm cannon
    struct CamPart { std::string model; V3 base, ax, ay, az; const ModelGpu* gpu = nullptr; bool cannon = false; };
    std::vector<CamPart> CameraParts(const HudSway& sw, const WeaponPose& wp) {
        std::vector<CamPart> out;
        if (playMode && player.ball) return out;   // the morph ball: no helmet, no cannon
        for (const HudPart& p : hud.parts) {
            if (!p.visible || !ModelNamed(p.model)) continue;
            CamPart c; c.model = p.model; partAxes(p, c.ax, c.ay, c.az); float k = p.sway;
            c.ax = rotateDeg(c.ax, sw.yaw * k, sw.pitch * k, sw.roll * k); c.ay = rotateDeg(c.ay, sw.yaw * k, sw.pitch * k, sw.roll * k); c.az = rotateDeg(c.az, sw.yaw * k, sw.pitch * k, sw.roll * k);
            c.base = sw.apply(p.pos, p.sway); out.push_back(c);
        }
        if (const Beam* b = CurrentBeam()) {
            CannonXf x; if (CannonPlacement(*b, sw, wp, x)) { CamPart c; c.model = b->model; c.base = x.base; c.ax = x.ax; c.ay = x.ay; c.az = x.az; c.gpu = cannonClip >= 0 ? &cannonGpu : nullptr; c.cannon = true; out.push_back(c); }
        }
        return out;
    }
    // Play mode: a shot from the cannon's emitter toward what the crosshair points at; a flash at the muzzle
    // a point on the cannon, in the world (as it shows on screen with the player's field of view)
    bool CannonWorld(const Beam& b, V3 local, V3& out, const V3* dirLocal = nullptr, V3* dirOut = nullptr) {
        V3 eye = player.eye(), fw = player.forward(), rr = Norm(cross({ 0, 1, 0 }, fw)), uu = cross(fw, rr);
        double fk = std::tan(XMConvertToRadians(settings.fov) * 0.5) / std::max(1e-3, std::tan(XMConvertToRadians(hud.fov) * 0.5));
        auto W = [&](V3 c) { return rr * (c.x * fk) + uu * (c.y * fk) + fw * c.z; };
        CannonXf x; if (!CannonPlacement(b, hudSway, weapPose, x)) return false;
        out = eye + W(CannonPoint(x, b, local)); if (dirLocal && dirOut) *dirOut = W(CannonDir(x, *dirLocal));
        return true;
    }
    void LaunchShot(const Beam& b, int beamIdx, const MissileType* m, bool charged = false) {
        bool missile = m != nullptr;
        V3 eye = player.eye(), fw = player.forward(), rr = Norm(cross({ 0, 1, 0 }, fw)), uu = cross(fw, rr);
        // the cannon is drawn with the HUD's view angle: points move to where they show on screen with the player's own field of view
        double fk = std::tan(XMConvertToRadians(settings.fov) * 0.5) / std::max(1e-3, std::tan(XMConvertToRadians(hud.fov) * 0.5));
        auto W = [&](V3 c) { return rr * (c.x * fk) + uu * (c.y * fk) + fw * c.z; };
        V3 from = eye + fw * 0.6, mpos = from, mdir = fw;
        CannonXf x;
        if (CannonPlacement(b, hudSway, weapPose, x)) { from = eye + W(CannonPoint(x, b, b.emitter.pos)); mpos = eye + W(CannonPoint(x, b, b.muzzle.pos)); mdir = W(CannonDir(x, pointDir(b.muzzle))); }
        V3 ad = AimDir();   // through the crosshair (the middle, or the free-aim cursor)
        double range = std::max(2.f, b.range), hitT = range; Hit h;
        if (world->raycast(eye, ad, range, h)) hitT = h.t;
        for (const Box3& bx : world->objectSolids) { double t; if (rayBox(eye, ad, bx, t) && t > 0.05 && t < hitT) hitT = t; }
        V3 target = eye + ad * hitT, d = target - from; double L = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        Shot sh; sh.pos = from; sh.dir = L > 0.8 ? d * (1 / L) : ad;
        sh.speed = missile ? m->speed : b.speed; sh.size = missile ? m->size : b.size; sh.range = (float)range; sh.weapon = missile ? m->name : b.name; sh.missile = missile;
        sh.hitLook = missile ? m->hitLook : b.hitLook; sh.beam = beamIdx; sh.orb = !missile && b.shotStyle == 1;
        if (missile) sh.mtype = (int)(m - weapons.missileTypes.data());
        if (charged) { sh.speed *= std::max(0.05f, b.chargeSpeed); sh.size *= std::max(0.1f, b.chargeSize); sh.charged = true; }
        uint32_t col = missile ? m->color : b.color; sh.tint = col; V3 lc = Linear(col); sh.color = lc * (charged ? 6.0 : 3.0);
        ParticleSettings tp = particlePreset(missile ? (m->trailLook.empty() ? "Smoke" : m->trailLook) : b.shotLook); tp.color0 = charged ? 0xFFFFFF : col; tp.color1 = col;
        if (charged) { tp.size0 *= 2.5f; tp.size1 *= 2.5f; tp.rate *= 1.5f; }
        if (sh.orb) { tp.size0 *= 0.7f; tp.size1 *= 0.7f; }   // the orb is the bright part now: a thinner trail behind it
        sh.trail = ParticleEmitter(tp, (uint32_t)(shots.size() * 7919 + GetTickCount()));
        shots.push_back(sh);
        ParticleSettings fp = particlePreset(b.flashLook); fp.burst = std::max(fp.burst, 8) * (charged ? 3 : 1); if (charged) { fp.size0 *= 2; fp.speed *= 1.6f; } SpawnFx(fp, mpos, mdir, col);
        if (missile) PlaySound(const_cast<MissileType*>(m)->launch);
    }
    // missiles: the kind picked now (one supply for all beams)
    bool OwnsMissile(const MissileType& m) const { return m.startsOwned || inv.keys.count(m.name) > 0; }
    const MissileType* CurrentMissile() {
        if (weapons.missileTypes.empty()) return nullptr;
        weapState.missile = std::clamp(weapState.missile, 0, (int)weapons.missileTypes.size() - 1);
        return &weapons.missileTypes[weapState.missile];
    }
    // switching mode 1 (no switching): always the best beam and missile you have
    void PickBestWeapons() {
        std::vector<char> ob, om; for (const Beam& b : weapons.beams) ob.push_back(OwnsBeam(b)); for (const MissileType& m : weapons.missileTypes) om.push_back(OwnsMissile(m));
        int bb = bestOwned(ob, weapState.current), bm = bestOwned(om, weapState.missile);
        if (bb != weapState.current) { StopChargeTimeline(); chargeTlOn = false; weapState.current = bb; weapState.charging = false; weapState.charge = 0; cannonClip = -1; firedPending += 0.4f; }
        weapState.missile = bm;
    }
    void SwitchMissile(int to, bool quiet = false) {
        if (to < 0 || to >= (int)weapons.missileTypes.size()) return;
        if (!OwnsMissile(weapons.missileTypes[to])) { if (!quiet) Toast("You have not found the " + weapons.missileTypes[to].name + " yet"); return; }
        weapState.missile = to; if (!quiet) Toast(weapons.missileTypes[to].name);
    }
    void NextMissile() {   // Q: the next missile kind you have
        int n = (int)weapons.missileTypes.size();
        for (int k = 1; k < n; k++) { int i = (weapState.missile + k) % n; if (OwnsMissile(weapons.missileTypes[i])) { SwitchMissile(i); return; } }
        if (n > 1) Toast("No other missiles yet");
    }
    void FireWeapons(double dt) {
        weapState.tick((float)dt);
        if (player.ball) {   // the morph ball: the fire button lays a bomb
            bool L = (Playing() && !play.dead() && Down(VK_LBUTTON)) || holdBeam;
            if (L && !shootPrev) DropBomb();
            shootPrev = L; missilePrev = Down(VK_RBUTTON); UpdateTimelines(dt, true); return;
        }
        if (weapons.switchMode == 1) PickBestWeapons();
        const Beam* b = CurrentBeam(); if (!b) return;
        bool can = Playing() && !play.dead() && OwnsBeam(*b);
        bool L = (can && Down(VK_LBUTTON)) || (holdBeam && OwnsBeam(*b)), R = (can && Down(VK_RBUTTON)) || forceMissile;
        const std::string& counter = weapons.missileCounter;
        const MissileType* mt = CurrentMissile();
        if (R && !missilePrev && mt) {
            int left = inv.counts.count(counter) ? inv.counts[counter] : 0;
            if (!OwnsMissile(*mt)) Toast("You have not found the " + mt->name + " yet");
            else if (weapState.fire(*b, mt, AnimLen(*b, Beam::MissileShot), left)) { inv.counts[counter] = left; StartTimeline(weapState.current, Beam::MissileShot); firedPending += mt->cost > 1 ? 3.5f : 2.5f; }
            else if (left < std::max(1, mt->cost)) Toast(left <= 0 ? std::string("No missiles left") : "Not enough missiles (a " + mt->name + " needs " + std::to_string(mt->cost) + ")");
        }
        {   // the beam: a press fires, holding charges (if the beam can), letting go when full fires the charged shot
            auto r = weapState.beamButton(*b, L, shootPrev, (float)dt, AnimLen(*b, Beam::Fire));
            if (r == WeaponState::Shot::Normal) { StartTimeline(weapState.current, Beam::Fire); firedPending += 1; }
            if (r == WeaponState::Shot::Charged) { StopChargeTimeline(); chargeTlOn = false; StartTimeline(weapState.current, Beam::Charged); firedPending += 2.2f; }
            bool chargingNow = weapState.charging && weapState.charge > 0;   // the charging sounds (and animation) while it fills
            if (chargingNow && !chargeTlOn) { StartTimeline(weapState.current, Beam::Charging); chargeTlOn = true; }
            if (!chargingNow && chargeTlOn) { StopChargeTimeline(); chargeTlOn = false; }
            if (weapState.full() && !chargedOn) StartCharged(*b);       // full: the charging sound stops, "done" + the charged loop start together
            if (!weapState.full() && chargedOn) StopCharged();
        }
        // the charge gathering at the muzzle: more and bigger as it fills
        {
            bool showing = weapState.charging && weapState.charge > 0;
            if (showing) {
                ParticleSettings cs = particlePreset(b->chargeLook.empty() ? "Charge" : b->chargeLook); float k = weapState.charge;
                cs.color1 = b->color; cs.rate *= 0.4f + k; cs.size1 *= 0.6f + k * 1.4f; cs.areaSize = cs.areaSize * (0.5 + k * 0.8);
                if (k >= 1) { cs.size1 *= 1.25f + 0.25f * (float)std::sin(animTime * 20); }   // full: it throbs
                chargeFx.s = cs; CannonWorld(*b, b->muzzle.pos, chargeAt);
            }
            chargeFx.update(dt, chargeAt, 0, nullptr, showing);
        }
        shootPrev = L; missilePrev = Down(VK_RBUTTON);
        UpdateTimelines(dt, true);
    }
    void SwitchBeam(int to) {
        if (weapons.empty() || to < 0 || to >= (int)weapons.beams.size() || to == weapState.current) return;
        if (!OwnsBeam(weapons.beams[to])) { Toast("You have not found the " + weapons.beams[to].name + " yet"); return; }
        if (playMode && weapons.switchMode == 1) return;   // no switching: always the best one
        StopChargeTimeline(); chargeTlOn = false;
        weapState.current = to; weapState.charging = false; weapState.charge = 0; cannonClip = -1; firedPending += 0.4f; Toast(weapons.beams[to].name);
    }

    // ---- the Weapons tab: the cannon in its own 3D view, its three points (drag them), what it shoots
    void RenderWeaponView() {
        if (!playerEdOpen || !playerTabWeapons || weapWant.x < 16 || weapWant.y < 16 || weapons.empty()) return;
        const Beam& b = weapons.beams[std::clamp(weapSel, 0, (int)weapons.beams.size() - 1)];
        if (!ModelNamed(b.model)) return;
        int w = (int)weapWant.x, h = (int)weapWant.y; if (w != weapDW || h != weapDH) { MakeTarget(weapView, w, h, &weapDSV); weapDW = w; weapDH = h; }
        const float bg[4] = { 0.30f, 0.33f, 0.38f, 1 };
        ctx->OMSetRenderTargets(1, weapView.rtv.GetAddressOf(), weapDSV.Get()); ctx->ClearRenderTargetView(weapView.rtv.Get(), bg); ctx->ClearDepthStencilView(weapDSV.Get(), D3D11_CLEAR_DEPTH, 1, 0);
        D3D11_VIEWPORT v{ 0, 0, (float)w, (float)h, 0, 1 }; ctx->RSSetViewports(1, &v);
        ctx->OMSetBlendState(bsOff.Get(), nullptr, 0xFFFFFFFF); ctx->OMSetDepthStencilState(dsOn.Get(), 0); ctx->RSSetState(rsNoCull.Get());
        PreviewCB pc{}; XMStoreFloat4x4((XMFLOAT4X4*)pc.vp, weapVP); pc.light[0] = -0.45f; pc.light[1] = 0.8f; pc.light[2] = -0.4f;
        ctx->UpdateSubresource(cbPreview.Get(), 0, nullptr, &pc, 0, 0);
        ctx->VSSetConstantBuffers(2, 1, cbPreview.GetAddressOf()); ctx->PSSetConstantBuffers(2, 1, cbPreview.GetAddressOf()); ctx->VSSetConstantBuffers(1, 1, cbChunk.GetAddressOf());
        ctx->IASetInputLayout(layout.Get()); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vsPreview.Get(), nullptr, 0); ctx->PSSetShader(psPreview.Get(), nullptr, 0);
        ctx->PSSetShaderResources(0, 1, texArray.GetAddressOf()); ctx->PSSetSamplers(0, 1, sampBlocks.GetAddressOf());
        WorldObject vo; vo.model = b.model; const ModelGpu* use = weapSel == weapState.current && cannonClip >= 0 ? &cannonGpu : nullptr;
        DrawObject(vo, 0, weapEye, use); DrawObject(vo, 1, weapEye, use);
        ID3D11ShaderResourceView* none[10] = {}; ctx->PSSetShaderResources(0, 10, none); ctx->RSSetState(rsCull.Get());
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
    }
    bool WeapPointEdit(const char* label, WeaponPoint& p, bool dir) {
        bool ch = DragV3(label, p.pos, 0.005f, "%.3f");
        if (dir) { float a[2] = { p.yaw, p.pitch }; Prop("  aims (turn, tilt)"); ImGui::PushID(label); if (ImGui::DragFloat2("##aim", a, 0.5f, -180, 180, "%.0f deg")) { p.yaw = a[0]; p.pitch = std::clamp(a[1], -89.f, 89.f); ch = true; } ImGui::PopID(); }
        return ch;
    }
    bool LookCombo(const char* id, std::string& v) {
        bool ch = false;
        if (ImGui::BeginCombo(id, v.c_str())) { for (auto& n : particlePresets()) if (ImGui::Selectable(n.c_str(), v == n)) { v = n; ch = true; } ImGui::EndCombo(); }
        return ch;
    }
    bool ClipCombo(const char* id, const std::string& model, std::string& v) {
        bool ch = false; auto it = meshModels.find(model);
        std::string shown = v.empty() ? "(none)" : v; if (!v.empty() && (it == meshModels.end() || it->second.findClip(v) < 0)) shown += "  (not in the model)";
        if (ImGui::BeginCombo(id, shown.c_str())) {
            if (ImGui::Selectable("(none)", v.empty())) { v.clear(); ch = true; }
            if (it != meshModels.end()) for (auto& c : it->second.clips) if (ImGui::Selectable(c.name.c_str(), v == c.name)) { v = c.name; ch = true; }
            ImGui::EndCombo();
        }
        return ch;
    }
    void WeaponsTab() {
        float ui = settings.uiScale; bool ch = false;
        if (weapons.empty()) {
            ImGui::Spacing(); Hint("Beams: each is an arm cannon model with three points (where it turns, where the flash comes from, where shots come out), and what it shoots. Missiles come from the cannon too. Idle, walking and landing movement is made by the engine; the firing animations come from the model.");
            if (ImGui::Button("Start with a Power Beam and a Wave Beam")) { weapons = defaultWeapons(); weapSel = 0; SaveHud(); }
            ImGui::SameLine(); if (ImGui::Button("Start empty (one beam)")) { Beam b; b.model = "@arm-cannon"; weapons.beams.push_back(b); weapSel = 0; SaveHud(); }
            return;
        }
        weapSel = std::clamp(weapSel, 0, (int)weapons.beams.size() - 1); weapState.current = weapSel;
        float listW = 210 * ui, propW = 340 * ui, sp = ImGui::GetStyle().ItemSpacing.x;
        ImGui::BeginChild("weaplist", ImVec2(listW, 0), ImGuiChildFlags_Borders);
        ImGui::TextDisabled("BEAMS (keys 1-9 in the game)");
        for (int i = 0; i < (int)weapons.beams.size(); i++) { ImGui::PushID(i); std::string l = std::to_string(i + 1) + "  " + weapons.beams[i].name; if (ImGui::Selectable(l.c_str(), weapSel == i && weapMis < 0)) { weapSel = i; weapMis = -1; cannonClip = -1; } ImGui::PopID(); }
        if (FlowButton("+ Beam")) { Beam b = weapons.beams[weapSel]; b.name = "Beam " + std::to_string(weapons.beams.size() + 1); weapons.beams.push_back(b); weapSel = (int)weapons.beams.size() - 1; ch = true; }
        if (FlowButton("Duplicate")) { Beam b = weapons.beams[weapSel]; b.name += " copy"; weapons.beams.insert(weapons.beams.begin() + weapSel + 1, b); weapSel++; ch = true; }
        ImGui::BeginDisabled(weapons.beams.size() < 2); if (FlowButton("Delete")) { weapons.beams.erase(weapons.beams.begin() + weapSel); weapSel = std::max(0, weapSel - 1); ch = true; } ImGui::EndDisabled();
        ImGui::BeginDisabled(weapSel == 0); if (FlowButton("Up")) { std::swap(weapons.beams[weapSel], weapons.beams[weapSel - 1]); weapSel--; ch = true; } ImGui::EndDisabled();
        ImGui::Separator(); ImGui::TextDisabled("MISSILES (all beams; Q: next kind)");
        for (int i = 0; i < (int)weapons.missileTypes.size(); i++) { ImGui::PushID(1000 + i); if (ImGui::Selectable(weapons.missileTypes[i].name.c_str(), weapMis == i)) weapMis = i; ImGui::PopID(); }
        if (FlowButton("+ Missile kind")) { MissileType m = weapons.missileTypes.empty() ? MissileType{} : weapons.missileTypes.back(); m.name = weapons.missileTypes.empty() ? "Missile" : "Missile " + std::to_string(weapons.missileTypes.size() + 1); m.startsOwned = weapons.missileTypes.empty(); weapons.missileTypes.push_back(m); weapMis = (int)weapons.missileTypes.size() - 1; ch = true; }
        if (weapMis >= 0) { ImGui::BeginDisabled(weapons.missileTypes.size() < 2); if (FlowButton("Delete##m")) { weapons.missileTypes.erase(weapons.missileTypes.begin() + weapMis); weapMis = std::min(weapMis, (int)weapons.missileTypes.size() - 1); ch = true; } ImGui::EndDisabled(); }
        ImGui::Separator(); ImGui::TextDisabled("ALL WEAPONS");
        { const char* sm[] = { "Freely (1-9, wheel, Q)", "Never: always the best one", "In the pause menu" }; Prop("Switching"); ch |= ImGui::Combo("##swm", &weapons.switchMode, sm, 3); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Freely: number keys and the mouse wheel pick beams, Q the missile kind.\nNever (like Prime 3): the best one you have, the last one in the list, so put the beams in upgrade order.\nPause menu (like Super Metroid): Esc, then pick the beam and missile kind there.");
        WeaponMotion& mo = weapons.motion;
        Prop("Idle breathing"); ch |= ImGui::SliderFloat("##mi", &mo.idle, 0, 3, "%.2f");
        Prop("Walk bob"); ch |= ImGui::SliderFloat("##mw", &mo.walk, 0, 3, "%.2f");
        Prop("Landing dip"); ch |= ImGui::SliderFloat("##ml", &mo.land, 0, 3, "%.2f");
        Prop("Recoil"); ch |= ImGui::SliderFloat("##mr", &mo.recoil, 0, 3, "%.2f");
        { const char* am[] = { "Classic (mouse turns the camera)", "Free aim (Wii style)" }; Prop("Aiming"); ch |= ImGui::Combo("##aim", &weapons.aimMode, am, 2); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Free aim: the mouse moves a cursor on the screen; the cannon and the shots follow it.\nNear the edges the camera turns, faster the closer you get.");
        if (weapons.aimMode == 1) {
            Prop("Dead zone"); ch |= ImGui::SliderFloat("##dz", &weapons.deadZone, 0, 0.9f, "%.2f");
            Prop("Turning speed"); ch |= ImGui::SliderFloat("##ts", &weapons.turnSpeed, 20, 400, "%.0f deg/s");
            Prop("Cursor speed"); ch |= ImGui::SliderFloat("##cs2", &weapons.cursorSpeed, 0.2f, 4, "%.2f");
            Prop("Cannon follows"); ch |= ImGui::SliderFloat("##cfw", &weapons.cannonFollow, 0, 1.5f, "%.2f");
        }
        Prop("Cannon in front of the HUD"); ch |= ImGui::Checkbox("##cf", &weapons.cannonInFront);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The cannon has its own layer, so it never cuts into the HUD.\nOff (like a visor): the HUD is drawn over the cannon.\nOn: the cannon covers the HUD.");
        ImGui::EndChild();
        ImGui::SameLine();
        Beam& b = weapons.beams[weapSel];
        float midW = std::max(200.f, ImGui::GetContentRegionAvail().x - propW - sp), midH = ImGui::GetContentRegionAvail().y;
        ImGui::BeginChild("weapmid", ImVec2(midW, midH), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        {   // the cannon alone: drag the points
            ImVec2 isz(midW, std::floor(midH * 0.55f)); weapWant = isz;
            ImVec2 o = ImGui::GetCursorScreenPos();
            if (weapView.srv) ImGui::Image((ImTextureID)(intptr_t)weapView.srv.Get(), isz); else ImGui::Dummy(isz);
            ImGui::SetCursorScreenPos(o); ImGui::InvisibleButton("worbit", isz, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            ImGuiIO& io = ImGui::GetIO();
            FrameRef f = ModelNamed(b.model); WorldObject vo; vo.model = b.model;
            V3 centre{ 0, 0.3, 0 }; if (f) { centre = { 0, f->sy * 0.5, 0 }; }
            weapEye = { centre.x + std::sin(weapYaw) * std::cos(weapPitch) * weapDist, centre.y + std::sin(weapPitch) * weapDist, centre.z - std::cos(weapYaw) * std::cos(weapPitch) * weapDist };
            XMVECTOR fwd = XMVector3Normalize(XMVectorSet((float)(centre.x - weapEye.x), (float)(centre.y - weapEye.y), (float)(centre.z - weapEye.z), 0));
            weapVP = XMMatrixLookToLH(XMVectorZero(), fwd, XMVectorSet(0, 1, 0, 0)) * XMMatrixPerspectiveFovLH(XMConvertToRadians(45), isz.x / isz.y, 0.02f, 200);
            ImDrawList* dl = ImGui::GetWindowDrawList(); dl->PushClipRect(o, ImVec2(o.x + isz.x, o.y + isz.y), true);
            auto P = [&](V3 a, ImVec2& s) { bool k; ViewerProject(weapVP, weapEye, o, isz, a, s, k); return k; };
            auto L = [&](V3 a, V3 c, ImU32 col, float t) { ImVec2 p, q; if (P(a, p) && P(c, q)) dl->AddLine(p, q, col, t); };
            WeaponPoint* pts[3] = { &b.pivot, &b.muzzle, &b.emitter }; const char* names[3] = { "Pivot", "Muzzle", "Emitter" };
            ImU32 cols[3] = { IM_COL32(255, 255, 255, 255), IM_COL32(255, 160, 60, 255), IM_COL32(90, 220, 255, 255) };
            ImVec2 sc[3]; bool vis[3] = {};
            if (f) for (int k = 0; k < 3; k++) {
                V3 wpos = objectPoint(vo, *f, pts[k]->pos); vis[k] = P(wpos, sc[k]);
                float s = weapPoint == k ? 2.5f : 1.5f; double len = std::max(0.25, std::max({ f->sx, f->sy, f->sz }) * 0.45);
                if (k == 0) { L(wpos - V3{ 0.06, 0, 0 }, wpos + V3{ 0.06, 0, 0 }, cols[k], s); L(wpos - V3{ 0, 0.06, 0 }, wpos + V3{ 0, 0.06, 0 }, cols[k], s); L(wpos - V3{ 0, 0, 0.06 }, wpos + V3{ 0, 0, 0.06 }, cols[k], s); }
                else {   // an arrow: where it aims
                    V3 d = pointDir(*pts[k]), tip = wpos + d * len, side = Norm(cross(d, std::fabs(d.y) > 0.9 ? V3{ 1, 0, 0 } : V3{ 0, 1, 0 }));
                    L(wpos, tip, cols[k], s); L(tip, tip - d * (len * 0.25) + side * (len * 0.12), cols[k], s); L(tip, tip - d * (len * 0.25) - side * (len * 0.12), cols[k], s);
                }
                if (vis[k]) { dl->AddCircleFilled(sc[k], weapPoint == k ? 6.f : 4.f, cols[k]); dl->AddText(ImVec2(sc[k].x + 8, sc[k].y - 8), cols[k], names[k]); }
            }
            // drag a point: it moves across the screen; plain drag elsewhere turns the view
            if (ImGui::IsItemActivated()) {
                weapDrag = -1; float best = 14 * 14 * ui * ui;
                for (int k = 0; k < 3; k++) if (vis[k]) { float dx = io.MousePos.x - sc[k].x, dy = io.MousePos.y - sc[k].y; if (dx * dx + dy * dy < best) { best = dx * dx + dy * dy; weapDrag = k; } }
                if (weapDrag >= 0) weapPoint = weapDrag;
            }
            if (ImGui::IsItemActive() && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
                if (weapDrag >= 0 && f) {
                    V3 fw = Norm(centre - weapEye), right = Norm(cross({ 0, 1, 0 }, fw)), up = cross(fw, right);
                    double px = 2 * weapDist * std::tan(XMConvertToRadians(45) * 0.5) / std::max(1.f, isz.y);
                    V3 mv = right * (io.MouseDelta.x * px) - up * (io.MouseDelta.y * px);
                    pts[weapDrag]->pos = pts[weapDrag]->pos + mv; ch = true;
                } else { weapYaw += io.MouseDelta.x * 0.01f; weapPitch = std::clamp(weapPitch + io.MouseDelta.y * 0.01f, -1.4f, 1.4f); }
            }
            if (ImGui::IsItemDeactivated()) weapDrag = -1;
            if (ImGui::IsItemHovered() && io.MouseWheel != 0) weapDist = std::clamp(weapDist * (io.MouseWheel > 0 ? 0.88f : 1.14f), 0.3f, 30.f);
            if (!f) dl->AddText(ImVec2(o.x + 10, o.y + 30), IM_COL32(255, 160, 120, 255), "Pick a model for this beam (on the right).");
            dl->AddText(ImVec2(o.x + 8, o.y + 6), IM_COL32(230, 230, 240, 210), "Drag a point to move it, drag elsewhere to turn, wheel to zoom");
            dl->PopClipRect();
        }
        if (ImGui::RadioButton("Camera preview", !weapTimelineView)) weapTimelineView = false; ImGui::SameLine();
        if (ImGui::RadioButton("Timeline (animation speed, sounds, when the shot leaves)", weapTimelineView)) weapTimelineView = true;
        if (weapTimelineView) TimelineEditor(b, ImGui::GetContentRegionAvail(), ch);
        else {   // through the camera: the cannon where it sits, with the HUD
            float rowH = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing();
            float asp = (float)rw / std::max(1, rh); ImVec2 psz(midW, midW / asp); if (psz.y > rowH) psz = ImVec2(rowH * asp, rowH);
            hudPrevWant = ImVec2(std::floor(psz.x), std::floor(psz.y));
            if (hudPrev.srv) ImGui::Image((ImTextureID)(intptr_t)hudPrev.srv.Get(), psz); else ImGui::Dummy(psz);
            if (ImGui::Button("Fire")) StartTimeline(weapSel, Beam::Fire);
            ImGui::SameLine(); if (ImGui::Button("Charged")) StartTimeline(weapSel, Beam::Charged);
            ImGui::SameLine(); if (ImGui::Button("Missile")) StartTimeline(weapSel, Beam::MissileShot);
            ImGui::SameLine(); if (ImGui::Button("Walk")) hudWiggle = 0;
            ImGui::SameLine(); if (ImGui::Button("Land")) { previewPose.update(weapons.motion, 0, 0, true, 24, 0); previewSway.update(hud.sway, 0, 0, 0, 0, true, 24); }
            ImGui::SameLine(); ImGui::TextDisabled("(the preview; shoot for real with F5)");
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("weapprops", ImVec2(0, 0), ImGuiChildFlags_Borders);
        if (weapMis >= 0 && weapMis < (int)weapons.missileTypes.size()) ch |= MissilePanel(weapons.missileTypes[weapMis]);
        else {
        weapMis = -1;
        ImGui::SeparatorText("Beam");
        Prop("Name"); ch |= ImGui::InputText("##bn", &b.name);
        Prop("Arm cannon model");
        if (ImGui::BeginCombo("##bm", b.model.empty() ? "(none)" : b.model.c_str())) {
            for (const std::string& n : PartModels()) if (ImGui::Selectable(n.c_str(), b.model == n)) { b.model = n; cannonClip = -1; ch = true; }
            if (ImGui::Selectable("@arm-cannon (built in)", b.model == "@arm-cannon")) { b.model = "@arm-cannon"; ch = true; }
            ImGui::EndCombo();
        }
        Prop("Have it from the start"); ch |= ImGui::Checkbox("##bo", &b.startsOwned);
        if (!b.startsOwned) Hint("Found later: a key pickup called \"%s\" gives it.", b.name.c_str());
        ImGui::SeparatorText("Points (drag them in the view)");
        const char* pn[3] = { "Pivot", "Muzzle", "Emitter" };
        for (int k = 0; k < 3; k++) { if (k) ImGui::SameLine(); if (ImGui::RadioButton(pn[k], weapPoint == k)) weapPoint = k; }
        if (weapPoint == 0) { ch |= WeapPointEdit("Pivot", b.pivot, false); Hint("The cannon sits and turns around this point."); }
        if (weapPoint == 1) { ch |= WeapPointEdit("Muzzle", b.muzzle, true); Hint("The flash comes from here, along its arrow."); }
        if (weapPoint == 2) { ch |= WeapPointEdit("Emitter", b.emitter, true); Hint("Shots start here and fly to where the crosshair points."); }
        ImGui::SeparatorText("In front of the camera");
        ch |= DragV3("Position", b.viewPos, 0.005f, "%.3f");
        { float r[3] = { b.viewYaw, b.viewPitch, b.viewRoll }; Prop("Turn"); if (ImGui::DragFloat3("##br", r, 0.5f, -180, 180, "%.0f deg")) { b.viewYaw = r[0]; b.viewPitch = r[1]; b.viewRoll = r[2]; ch = true; } }
        { float s = (float)b.scale; Prop("Size"); if (ImGui::DragFloat("##bs", &s, 0.005f, 0.01f, 20, "x %.3f")) { b.scale = std::max(0.01f, s); ch = true; } }
        Prop("Sway amount"); ch |= ImGui::SliderFloat("##bsw", &b.sway, 0, 2, "%.2f");
        if (ImGui::SmallButton("Same place for every beam")) { for (Beam& o : weapons.beams) { o.viewPos = b.viewPos; o.viewYaw = b.viewYaw; o.viewPitch = b.viewPitch; o.viewRoll = b.viewRoll; o.scale = b.scale; o.sway = b.sway; } ch = true; }
        ImGui::SeparatorText("Shots");
        { float c[3] = { ((b.color >> 16) & 255) / 255.f, ((b.color >> 8) & 255) / 255.f, (b.color & 255) / 255.f }; Prop("Colour"); if (ImGui::ColorEdit3("##bc", c)) { b.color = ((uint32_t)(c[0] * 255 + 0.5f) << 16) | ((uint32_t)(c[1] * 255 + 0.5f) << 8) | (uint32_t)(c[2] * 255 + 0.5f); ch = true; } }
        { const char* st[] = { "Particles only", "Glowing 3D orb" }; Prop("Looks like"); ch |= ImGui::Combo("##bsty", &b.shotStyle, st, 2); }
        Prop("Trail"); ch |= LookCombo("##bt", b.shotLook);
        Prop("Hit"); ch |= LookCombo("##bh", b.hitLook);
        Prop("Muzzle flash"); ch |= LookCombo("##bf", b.flashLook);
        Prop("Speed"); ch |= ImGui::SliderFloat("##bsp", &b.speed, 2, 200, "%.0f blocks/s");
        Prop("Shots per second"); ch |= ImGui::SliderFloat("##bfr", &b.fireRate, 0.2f, 30, "%.1f");
        ImGui::BeginDisabled(b.charge); Prop("Hold to keep firing"); ch |= ImGui::Checkbox("##ba", &b.auto_); ImGui::EndDisabled();
        Prop("Damage"); ch |= ImGui::SliderFloat("##bd", &b.damage, 0, 500, "%.0f");
        Prop("Range"); ch |= ImGui::SliderFloat("##brg", &b.range, 5, 300, "%.0f blocks");
        Prop("Thickness"); ch |= ImGui::SliderFloat("##bz", &b.size, 0.01f, 0.6f, "%.2f blocks");
        Prop("Impact sphere"); ch |= ImGui::SliderFloat("##bsph", &b.hitSphere, 0, 4, "%.2f blocks");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("A glowing see-through bubble that grows where the shot hits, then fades. 0: none.");
        Prop("Firing animation"); ch |= ClipCombo("##bfc", b.model, b.fireClip);
        Prop("Wait until it is done"); ch |= ImGui::Checkbox("##bw", &b.waitForAnim);
        ImGui::TextDisabled("Hit sound"); ch |= SoundSetEdit("bhit", b.hitSound);
        ImGui::SeparatorText("Charge shot (hold the button)");
        Prop("Can charge"); ch |= ImGui::Checkbox("##cc", &b.charge);
        if (b.charge) {
            Prop("Time to charge"); ch |= ImGui::SliderFloat("##ct", &b.chargeTime, 0.2f, 5, "%.1f s");
            Prop("Charge look"); ch |= LookCombo("##cl", b.chargeLook);
            Prop("Damage"); ch |= ImGui::SliderFloat("##cd", &b.chargeDamage, 1, 20, "x %.1f");
            Prop("Size"); ch |= ImGui::SliderFloat("##cs", &b.chargeSize, 1, 10, "x %.1f");
            Prop("Speed"); ch |= ImGui::SliderFloat("##csp", &b.chargeSpeed, 0.1f, 3, "x %.2f");
            Prop("Impact sphere"); ch |= ImGui::SliderFloat("##csph", &b.chargeSphere, 0, 8, "%.1f blocks");
            Prop("Charging animation"); ch |= ClipCombo("##cca", b.model, b.chargingClip);
            Prop("Full (loops)"); ch |= ClipCombo("##cfa", b.model, b.chargedClip);
            Prop("Charged shot animation"); ch |= ClipCombo("##csa", b.model, b.chargedShotClip);
            ImGui::TextDisabled("Charging sounds: on the timeline (Charging); they stop when it is full");
            ImGui::TextDisabled("Full: once"); ch |= SoundSetEdit("cdone", b.chargeDone);
            ImGui::TextDisabled("Full: a loop until you let go"); ch |= SoundSetEdit("cloop", b.chargedLoop);
            ImGui::TextDisabled("The charged shot: on the timeline (Charged shot)");
            ImGui::TextDisabled("Charged hit (none: the normal hit sound)"); ch |= SoundSetEdit("chit", b.chargedHitSound);
            Hint("A tap fires a normal shot. Hold: it charges at the muzzle; let go when it is full for the charged shot. (Holding to keep firing is off while a beam can charge.) The HUD can show it: Shows > Beam charge.");
        }
        ImGui::SeparatorText("Missiles from this cannon (right mouse button)");
        Prop("Missile animation"); ch |= ClipCombo("##mcl", b.model, b.missileClip);
        Prop("Wait until it is done"); ch |= ImGui::Checkbox("##mwt", &b.missileWait);
        Hint("The missiles themselves are the same for every beam: pick them in the list on the left (MISSILES).");
        Hint("Damage counts once there are enemies (G5). Doors can ask for a beam or missiles (door Inspector: Opened by).");
        }
        ImGui::EndChild();
        if (ch) SaveHud();
    }
    int weapMis = -1;   // the missile kind picked in the Weapons tab (-1: a beam is picked)
    bool MissilePanel(MissileType& m) {
        bool ch = false; int i = (int)(&m - weapons.missileTypes.data());
        ImGui::SeparatorText(i == 0 ? "Missile (the plain one)" : "Missile kind");
        Prop("Name"); ch |= ImGui::InputText("##mn", &m.name);
        Prop("Model");
        if (ImGui::BeginCombo("##mmod", m.model.empty() ? "(none)" : m.model.c_str())) {
            if (ImGui::Selectable("@missile (built in)", m.model == "@missile")) { m.model = "@missile"; ch = true; }
            for (const std::string& n : PartModels()) if (ImGui::Selectable(n.c_str(), m.model == n)) { m.model = n; ch = true; }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("It flies nose first: the model's front is +Z (its long side).");
        Prop("Length"); ch |= ImGui::SliderFloat("##mlen", &m.size, 0.1f, 3, "%.2f blocks");
        Prop("You have it"); { int o = m.startsOwned ? 0 : 1; const char* ow[] = { "From the start", "Found later (a key pickup with its name)" }; if (ImGui::Combo("##mown", &o, ow, 2)) { m.startsOwned = o == 0; ch = true; } }
        { float c[3] = { ((m.color >> 16) & 255) / 255.f, ((m.color >> 8) & 255) / 255.f, (m.color & 255) / 255.f }; Prop("Colour"); if (ImGui::ColorEdit3("##mcc", c)) { m.color = ((uint32_t)(c[0] * 255 + 0.5f) << 16) | ((uint32_t)(c[1] * 255 + 0.5f) << 8) | (uint32_t)(c[2] * 255 + 0.5f); ch = true; } }
        Prop("Uses up"); ch |= ImGui::SliderInt("##mcost", &m.cost, 1, 20, m.cost == 1 ? "1 missile" : "%d missiles");
        Prop("Speed"); ch |= ImGui::SliderFloat("##msp", &m.speed, 2, 200, "%.0f blocks/s");
        Prop("Per second (at most)"); ch |= ImGui::SliderFloat("##mrt", &m.rate, 0.2f, 10, "%.1f");
        Prop("Damage"); ch |= ImGui::SliderFloat("##mdm", &m.damage, 0, 1000, "%.0f");
        Prop("Trail"); ch |= LookCombo("##mtl", m.trailLook);
        Prop("Hit"); ch |= LookCombo("##mhl", m.hitLook);
        Prop("Impact sphere"); ch |= ImGui::SliderFloat("##msph", &m.sphere, 0, 10, "%.1f blocks");
        ImGui::TextDisabled("Launch sound"); ch |= SoundSetEdit("mlaunch", m.launch);
        ImGui::TextDisabled("Hit sound"); ch |= SoundSetEdit("mhit", m.hit);
        ImGui::SeparatorText("The missile supply (all kinds)");
        Prop("Counter"); ch |= ImGui::InputText("##mc", &weapons.missileCounter);
        Prop("At the start"); ch |= ImGui::SliderInt("##ms", &weapons.missileStart, 0, 255);
        Prop("Most"); ch |= ImGui::SliderInt("##mm", &weapons.missileMax, 0, 255);
        Hint("Every kind uses the same missiles. A door or breakable block set to \"Missile\" opens for any kind; one set to a kind's name (like Super Missile) only for that kind. The cannon's missile animation is set on each beam.");
        return ch;
    }
    // ================================================================== sounds (WAV, played with XAudio2, built into Windows)
    IXAudio2* xa = nullptr; IXAudio2MasteringVoice* master = nullptr; bool audioOk = false; int masterChannels = 2;
    std::map<std::string, std::shared_ptr<WavData>> soundCache;
    struct Voice { IXAudio2SourceVoice* v = nullptr; std::shared_ptr<WavData> keep; int tag = 0; };
    std::vector<Voice> voices; uint32_t soundSeed = 12345;
    PlayerSounds playerSounds; float stepDist = 0;
    void InitAudio() {
        if (FAILED(XAudio2Create(&xa, 0, XAUDIO2_DEFAULT_PROCESSOR))) { Log("sound: XAudio2 could not start"); return; }
        if (FAILED(xa->CreateMasteringVoice(&master))) { Log("sound: no sound device"); xa->Release(); xa = nullptr; return; }
        XAUDIO2_VOICE_DETAILS d; master->GetVoiceDetails(&d); masterChannels = (int)d.InputChannels; audioOk = true;
    }
    void ShutdownAudio() {
        for (Voice& v : voices) if (v.v) v.v->DestroyVoice(); voices.clear();
        if (master) master->DestroyVoice(); master = nullptr; if (xa) xa->Release(); xa = nullptr; audioOk = false;
    }
    std::string SoundsDir() const { return projectDir + "/sounds"; }
    std::shared_ptr<WavData> LoadSound(const std::string& file) {
        if (file.empty()) return nullptr;
        auto it = soundCache.find(file); if (it != soundCache.end()) return it->second;
        std::shared_ptr<WavData>& w = soundCache[file];
        std::ifstream f(fs::u8path(SoundsDir() + "/" + file), std::ios::binary); if (!f) return nullptr;
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        auto d = std::make_shared<WavData>(); std::string err;
        if (!readWav(bytes, *d, err)) { Toast(file + ": " + err); return nullptr; }
        w = d; return w;
    }
    float SoundSeconds(const std::string& file) { auto w = LoadSound(file); return w ? w->seconds() : 0.f; }
    std::vector<std::string> SoundFiles() const {
        std::vector<std::string> v; std::error_code ec;
        for (auto& e : fs::directory_iterator(fs::u8path(SoundsDir()), ec)) if (Lower(e.path().extension().string()) == ".wav") v.push_back(e.path().filename().u8string());
        std::sort(v.begin(), v.end()); return v;
    }
    // plays a sound; at: a place in the world (quieter far away, from the side it comes from); tag: to stop it later
    IXAudio2SourceVoice* PlaySound(const SoundUse& s, float vol = 1, const V3* at = nullptr, int tag = 0, bool loop = false) {
        if (!audioOk || s.empty() || vol <= 0.001f) return nullptr;
        std::shared_ptr<WavData> w = LoadSound(s.file); if (!w || w->samples.empty()) return nullptr;
        if (voices.size() >= 48) {   // too many: the oldest one stops (never the roll loop)
            auto old = std::find_if(voices.begin(), voices.end(), [](const Voice& v) { return v.tag != TAG_ROLL; });
            if (old != voices.end()) { old->v->DestroyVoice(); voices.erase(old); }
        }
        WAVEFORMATEX fmt{}; fmt.wFormatTag = WAVE_FORMAT_IEEE_FLOAT; fmt.nChannels = (WORD)w->channels; fmt.nSamplesPerSec = (DWORD)w->rate; fmt.wBitsPerSample = 32;
        fmt.nBlockAlign = (WORD)(4 * w->channels); fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
        IXAudio2SourceVoice* v = nullptr; if (FAILED(xa->CreateSourceVoice(&v, &fmt, 0, 4.f))) return nullptr;
        UINT32 frames = (UINT32)(w->samples.size() / w->channels), begin = (UINT32)std::clamp((double)s.trimStart * w->rate, 0.0, (double)frames - 1);
        UINT32 len = s.length > 0 ? (UINT32)std::min<double>(s.length * w->rate, frames - begin) : 0;
        XAUDIO2_BUFFER b{}; b.AudioBytes = (UINT32)(w->samples.size() * 4); b.pAudioData = (const BYTE*)w->samples.data(); b.Flags = XAUDIO2_END_OF_STREAM; b.PlayBegin = begin; b.PlayLength = len;
        if (loop) { b.LoopBegin = begin; b.LoopLength = len; b.LoopCount = XAUDIO2_LOOP_INFINITE; }
        float volume = s.volume * vol * std::clamp(settings.soundVolume, 0.f, 2.f);
        if (at) {   // in the world: quieter with distance, and from its side
            V3 d = *at - player.eye(); double dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            volume *= (float)std::clamp(1.0 - dist / 40.0, 0.0, 1.0); volume *= volume > 0 ? 1.f : 0.f;
            if (w->channels == 1 && masterChannels >= 2 && dist > 0.01) {
                V3 r = Norm(cross({ 0, 1, 0 }, player.forward())); float pan = (float)std::clamp((d.x * r.x + d.y * r.y + d.z * r.z) / dist, -1.0, 1.0);
                std::vector<float> m(masterChannels, 0.f); m[0] = std::sqrt(0.5f * (1 - pan)); m[1] = std::sqrt(0.5f * (1 + pan));
                v->SetOutputMatrix(master, 1, masterChannels, m.data());
            }
        }
        v->SetVolume(volume); v->SetFrequencyRatio(randomPitch(s, soundSeed));
        if (FAILED(v->SubmitSourceBuffer(&b))) { v->DestroyVoice(); return nullptr; }
        v->Start(); voices.push_back({ v, w, tag });
        return v;
    }
    // a set of sounds: one of them, at random (never the same twice in a row)
    IXAudio2SourceVoice* PlaySound(SoundSet& set, float vol = 1, const V3* at = nullptr, int tag = 0) { const SoundUse* u = set.pick(soundSeed); return u ? PlaySound(*u, vol, at, tag) : nullptr; }
    // the editor for a set: each sound with its own settings; add one, or import several at once
    bool SoundSetEdit(const char* id, SoundSet& set) {
        bool ch = false; ImGui::PushID(id); int drop = -1;
        for (int i = 0; i < (int)set.sounds.size(); i++) {
            ImGui::PushID(i);
            ImGui::BeginGroup();   // left: its number and the remove button
            if (set.sounds.size() > 1) ImGui::TextDisabled("%d", i + 1);
            if (ImGui::SmallButton("x")) drop = i;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove this sound");
            ImGui::EndGroup(); ImGui::SameLine();
            ImGui::BeginGroup(); ch |= SoundEdit("s", set.sounds[i], true); ImGui::EndGroup();
            ImGui::PopID();
        }
        if (drop >= 0) { set.sounds.erase(set.sounds.begin() + drop); set.last = -1; ch = true; }
        if (ImGui::SmallButton(set.sounds.empty() ? "+ Sound" : "+ Another sound")) {
            SoundUse u; if (!set.sounds.empty()) { u = set.sounds.back(); u.file.clear(); } set.sounds.push_back(u); ch = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("+ Import several...")) {   // pick many WAVs: each one becomes a variation
            std::error_code ec; fs::create_directories(fs::u8path(SoundsDir()), ec); int n = 0;
            for (const std::string& f : PickFiles("WAV sounds (*.wav)\0*.wav\0", "Import sounds (pick as many as you like)")) {
                fs::path src = fs::u8path(f); std::string name = src.filename().u8string();
                fs::copy_file(src, fs::u8path(SoundsDir() + "/" + name), fs::copy_options::overwrite_existing, ec);
                if (ec) continue; soundCache.erase(name);
                SoundUse u; if (!set.sounds.empty()) u = set.sounds.back(); u.file = name; set.sounds.push_back(u); n++;
            }
            set.sounds.erase(std::remove_if(set.sounds.begin(), set.sounds.end(), [](const SoundUse& u) { return u.empty(); }), set.sounds.end());
            if (n) { Toast(std::to_string(n) + " sound(s) added"); ch = true; }
        }
        if (set.sounds.size() > 1) { ImGui::SameLine(); ImGui::TextDisabled("%d sounds: a random one each time", (int)set.sounds.size()); }
        ImGui::PopID(); return ch;
    }
    void StopSounds(int tag) { for (Voice& v : voices) if (v.tag == tag && v.v) { v.v->Stop(); v.v->DestroyVoice(); v.v = nullptr; } voices.erase(std::remove_if(voices.begin(), voices.end(), [](const Voice& v) { return !v.v; }), voices.end()); }
    void UpdateAudio() {   // finished sounds let go
        for (Voice& v : voices) { XAUDIO2_VOICE_STATE st; v.v->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED); if (st.BuffersQueued == 0) { v.v->DestroyVoice(); v.v = nullptr; } }
        voices.erase(std::remove_if(voices.begin(), voices.end(), [](const Voice& v) { return !v.v; }), voices.end());
    }
    std::vector<std::string> PickFiles(const char* filter, const char* title) {
        std::vector<char> buf(1 << 16, 0);
        OPENFILENAMEA o{}; o.lStructSize = sizeof o; o.hwndOwner = hwnd; o.lpstrFilter = filter; o.lpstrFile = buf.data(); o.nMaxFile = (DWORD)buf.size();
        o.lpstrTitle = title; o.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        std::vector<std::string> out; if (!GetOpenFileNameA(&o)) return out;
        std::string first = buf.data(); const char* p = buf.data() + first.size() + 1;
        if (!*p) { out.push_back(first); return out; }
        for (; *p; p += strlen(p) + 1) out.push_back(first + "\\" + p);
        return out;
    }
    void ImportSounds() {
        std::error_code ec; fs::create_directories(fs::u8path(SoundsDir()), ec); int n = 0;
        for (const std::string& f : PickFiles("WAV sounds (*.wav)\0*.wav\0", "Import sounds")) {
            fs::path s = fs::u8path(f); std::string name = s.filename().u8string();
            fs::copy_file(s, fs::u8path(SoundsDir() + "/" + name), fs::copy_options::overwrite_existing, ec);
            if (!ec) { n++; soundCache.erase(name); }
        }
        if (n) Toast(std::to_string(n) + " sound(s) imported");
    }
    // picks a sound and sets how it plays: file, play button, volume, pitch, random pitch (and the cut, if asked)
    bool SoundEdit(const char* id, SoundUse& s, bool cut) {
        bool ch = false; ImGui::PushID(id); float bw = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(std::max(60.f, ImGui::GetContentRegionAvail().x - bw - ImGui::GetStyle().ItemSpacing.x));
        if (ImGui::BeginCombo("##file", s.file.empty() ? "(no sound)" : s.file.c_str())) {
            if (ImGui::Selectable("(no sound)", s.file.empty())) { s.file.clear(); ch = true; }
            for (const std::string& f : SoundFiles()) if (ImGui::Selectable(f.c_str(), s.file == f)) { s.file = f; ch = true; }
            ImGui::Separator(); if (ImGui::Selectable("Import sounds...")) ImportSounds();
            ImGui::EndCombo();
        }
        ImGui::SameLine(); ImGui::BeginDisabled(s.empty()); if (ImGui::ArrowButton("##play", ImGuiDir_Right)) PlaySound(s); ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(audioOk ? "Listen" : "No sound device found");
        if (!s.empty()) {
            float row[3] = { s.volume, s.pitch, s.pitchVar };
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat3("##vp", row, 0.01f, 0, 4, "%.2f")) { s.volume = std::clamp(row[0], 0.f, 4.f); s.pitch = std::clamp(row[1], 0.05f, 4.f); s.pitchVar = std::clamp(row[2], 0.f, 1.f); ch = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Volume, pitch, random pitch (how much it changes each time)");
            if (cut) {
                float c2[2] = { s.trimStart, s.length }; ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragFloat2("##cut", c2, 0.005f, 0, 600, "%.3f s")) { s.trimStart = std::max(0.f, c2[0]); s.length = std::max(0.f, c2[1]); ch = true; }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Start into the sound, and how long (0: to the end) - %.2f s in all", SoundSeconds(s.file));
            }
        }
        ImGui::PopID(); return ch;
    }
    void SavePlayerSounds() { if (!projectDir.empty()) savePlayerSounds(projectDir + "/player_sounds.txt", playerSounds); }
    void SoundsTab() {
        float ui = settings.uiScale; bool ch = false;
        ImGui::Spacing();
        if (ImGui::Button("Import sounds...")) ImportSounds();
        ImGui::SameLine(); ImGui::TextDisabled(audioOk ? "WAV files go into the project's sounds folder." : "WAV files go into the project's sounds folder. (No sound device found: they cannot play here.)");
        ImGui::Separator();
        if (ImGui::BeginTable("ps", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("a", ImGuiTableColumnFlags_WidthStretch, 1.2f); ImGui::TableSetupColumn("b", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableNextColumn();
            ImGui::SeparatorText("Steps"); ch |= SoundSetEdit("step", playerSounds.step);
            Prop("A step every"); ch |= ImGui::SliderFloat("##se", &playerSounds.stepEvery, 0.4f, 5, "%.1f blocks walked");
            ImGui::SeparatorText("Jump"); ch |= SoundSetEdit("jump", playerSounds.jump);
            ImGui::SeparatorText("Landing"); ch |= SoundSetEdit("land", playerSounds.land);
            ImGui::SeparatorText("Morph ball in / out"); ch |= SoundSetEdit("morph", playerSounds.morph);
            if (scrollToRoll) { ImGui::SetScrollHereY(0.1f); scrollToRoll = false; }
            ImGui::SeparatorText("Morph ball rolling (a loop)"); ch |= SoundSetEdit("roll", playerSounds.roll);
            Prop("Higher when fast"); ch |= ImGui::SliderFloat("##rp", &playerSounds.rollPitch, 0, 1, playerSounds.rollPitch <= 0 ? "no" : "%.2f");
            ImGui::SeparatorText("Morph ball landing"); ch |= SoundSetEdit("ball_land", playerSounds.ballLand);
            ImGui::SeparatorText("Bomb laid"); ch |= SoundSetEdit("bomb", playerSounds.bomb);
            ImGui::SeparatorText("Bomb goes off"); ch |= SoundSetEdit("boom", playerSounds.boom);
            ImGui::SeparatorText("A block breaks"); ch |= SoundSetEdit("crumble", playerSounds.crumble);
            { float r[2] = { playerSounds.landQuiet, playerSounds.landLoud }; Prop("Quiet .. loud at"); if (ImGui::DragFloat2("##ll", r, 0.1f, 0, 80, "%.1f blocks/s")) { playerSounds.landQuiet = r[0]; playerSounds.landLoud = std::max(r[0] + 0.5f, r[1]); ch = true; } }
            Hint("Each can have as many sounds as you like: a random one plays each time, never the same twice in a row (great for steps). Rolled up, the steps and jumps go quiet: the rolling loop plays instead, louder (and higher) the faster you roll, and silent in the air. Landing gets louder the faster you fall: silent below the first speed, full volume at the second. The numbers under each sound: volume, pitch, random pitch; then the cut (start into the sound, how long).");
            ImGui::TableNextColumn();
            ImGui::SeparatorText("All sounds in the project");
            for (const std::string& f : SoundFiles()) {
                ImGui::PushID(f.c_str()); SoundUse t; t.file = f; t.pitchVar = 0;
                if (ImGui::ArrowButton("##p", ImGuiDir_Right)) PlaySound(t);
                ImGui::SameLine(); ImGui::Text("%s", f.c_str()); ImGui::SameLine(); ImGui::TextDisabled("%.2f s", SoundSeconds(f));
                ImGui::PopID();
            }
            if (SoundFiles().empty()) ImGui::TextDisabled("None yet.");
            Hint("Weapon sounds go on each beam's timeline (Weapons > Timeline).");
            ImGui::EndTable();
        }
        if (ch) SavePlayerSounds();
    }
    // Play mode: steps, jumping, landing
    bool scrollToRoll = false;
    IXAudio2SourceVoice* rollVoice = nullptr; SoundUse rollUse; float rollVol = 0; int rollTests = 0;   // the morph ball's rolling loop
    void StopRoll() { if (rollVoice) StopSounds(TAG_ROLL); rollVoice = nullptr; rollVol = 0; }
    void UpdateRoll(double dt) {
        float speed = (float)std::sqrt(player.vel.x * player.vel.x + player.vel.z * player.vel.z), want = 0, pitch = 1;
        if (playMode && !paused && player.ball && player.onGround) playerSounds.rollMix(speed, game.player.ballSpeed, want, pitch);
        if (want > 0) rollTests++;
        rollVol += (want - rollVol) * (float)std::min(1.0, dt * 12);   // fades in and out quickly, no clicks
        if (rollVol < 0.01f && want <= 0) { StopRoll(); return; }
        if (!rollVoice) {
            const SoundUse* u = playerSounds.roll.pick(soundSeed); if (!u) return;
            rollUse = *u; rollVoice = PlaySound(rollUse, 1, nullptr, TAG_ROLL, true); if (!rollVoice) return;
        }
        rollVoice->SetVolume(rollUse.volume * rollVol * std::clamp(settings.soundVolume, 0.f, 2.f));
        rollVoice->SetFrequencyRatio(std::clamp(rollUse.pitch * pitch, 0.1f, 4.f));
    }
    void UpdatePlayerSounds(double dt, bool landed, float fallSpeed, bool jumped) {
        UpdateRoll(dt);
        if (!playMode) return;
        if (player.ball) {   // rolled up: no steps, no jumps; its own landing (bomb jumps land here too)
            stepDist = 0;
            if (landed) { float v = playerSounds.landVolume(fallSpeed); if (v > 0) PlaySound(playerSounds.ballLand, v); }
            return;
        }
        if (player.onGround) {
            stepDist += (float)(std::sqrt(player.vel.x * player.vel.x + player.vel.z * player.vel.z) * dt);
            if (stepDist >= std::max(0.2f, playerSounds.stepEvery)) { stepDist = 0; PlaySound(playerSounds.step); }
        }
        if (jumped) PlaySound(playerSounds.jump);
        if (landed) { float v = playerSounds.landVolume(fallSpeed); if (v > 0) PlaySound(playerSounds.land, v); stepDist = 0; }
    }

    // ---- free aim (Wii style)
    FreeAim freeAim; float aimYaw = 0, aimPitch = 0;   // aimYaw / aimPitch: how far the cannon has turned toward the cursor (degrees)
    bool FreeAiming() const { return playMode && !weapons.empty() && weapons.aimMode == 1; }
    V3 AimDir() {   // where shots go: through the cursor (free aim) or the middle of the screen
        V3 fw = player.forward(); if (!FreeAiming()) return fw;
        V3 r = Norm(cross({ 0, 1, 0 }, fw)), u = cross(fw, r), d = freeAim.dir(settings.fov, (float)rw / std::max(1, rh));
        return Norm(r * d.x + u * d.y + fw * d.z);
    }
    void UpdateAimCannon(double dt) {   // the cannon turns toward the cursor, smoothly
        float ty = 0, tp = 0;
        if (FreeAiming()) {
            V3 d = freeAim.dir(hud.fov, (float)rw / std::max(1, rh)); float k = std::clamp(weapons.cannonFollow, 0.f, 2.f);
            ty = (float)(std::atan2(d.x, d.z) * 180 / 3.14159265358979) * k; tp = (float)(std::atan2(d.y, d.z) * 180 / 3.14159265358979) * k;
        }
        float a = 1 - (float)std::exp(-dt * 14); aimYaw += (ty - aimYaw) * a; aimPitch += (tp - aimPitch) * a;
    }

    // ---- timelines: what a beam does over time (its animation's speed, its sounds, when the shot leaves)
    struct ActiveTl { TimelinePlay play; int beam = 0, kind = 0; double until = 0; };
    std::vector<ActiveTl> activeTl;
    static constexpr int TAG_CHARGE = 7;
    static constexpr int TAG_ROLL = 8;
    static constexpr int TAG_CHARGED = 9;
    float AnimLen(const Beam& b, int kind) {   // the cannon's animation for this timeline, at its speed (charging never holds up a shot)
        return kind == Beam::Charging ? 0.f : ClipLenOf(b.model, beamClip(b, kind)) / std::max(0.05f, b.timelines[kind].animSpeed);
    }
    float TimelineLen(const Beam& b, int kind) {
        const Timeline& t = b.timelines[kind]; std::vector<float> lens; for (const SoundClip& c : t.sounds) lens.push_back(SoundSeconds(c.sound.file));
        return t.length(ClipLenOf(b.model, beamClip(b, kind)), lens);
    }
    void StartTimeline(int beam, int kind) {   // the cannon's animation, then sounds and the shot as they come up
        if (beam < 0 || beam >= (int)weapons.beams.size()) return;
        const Beam& b = weapons.beams[beam];
        PlayCannonClip(beamClip(b, kind)); cannonSpeed = std::max(0.05f, b.timelines[kind].animSpeed);
        ActiveTl a; a.beam = beam; a.kind = kind; a.until = TimelineLen(b, kind); if (kind == Beam::Charging && playMode) a.until = 600;   // playing: charging runs until it is full or you let go (its sounds are cut then)
        a.play.start(b.timelines[kind]); activeTl.push_back(a);
        if (kind == Beam::Charging) activeTl.back().play.shotDone = true;   // charging shoots nothing
    }
    void UpdateTimelines(double dt, bool shoot) {
        std::vector<int> now; bool shot;
        for (ActiveTl& a : activeTl) {
            if (a.beam >= (int)weapons.beams.size()) { a.play.t = -1; continue; }
            a.play.step(dt, now, shot, a.until);
            const Beam& b = weapons.beams[a.beam];
            for (int i : now) PlaySound(b.timelines[a.kind].sounds[i].sound, 1, nullptr, a.kind == Beam::Charging ? TAG_CHARGE : 0);
            if (shot) {
                if (shoot && playMode) LaunchShot(b, a.beam, a.kind == Beam::MissileShot ? CurrentMissile() : nullptr, a.kind == Beam::Charged);
                else if (!playMode) previewFired += a.kind == Beam::MissileShot ? 2.5f : a.kind == Beam::Charged ? 2.2f : 1.f;   // the editor's preview: only the kick
            }
        }
        activeTl.erase(std::remove_if(activeTl.begin(), activeTl.end(), [](const ActiveTl& a) { return !a.play.running(); }), activeTl.end());
    }
    void StopChargeTimeline() { activeTl.erase(std::remove_if(activeTl.begin(), activeTl.end(), [](const ActiveTl& a) { return a.kind == Beam::Charging; }), activeTl.end()); StopSounds(TAG_CHARGE); StopCharged(); }
    // full charge: the charging sounds stop, a "done" sound plays once and a loop hums (and the cannon's "charged" animation loops) until you let go
    bool chargedOn = false, forceMissile = false;   // forceMissile: screenshots press the missile button
    void StartCharged(const Beam& b) {
        StopSounds(TAG_CHARGE); activeTl.erase(std::remove_if(activeTl.begin(), activeTl.end(), [](const ActiveTl& a) { return a.kind == Beam::Charging; }), activeTl.end());
        PlaySound(const_cast<Beam&>(b).chargeDone);
        if (const SoundUse* u = const_cast<Beam&>(b).chargedLoop.pick(soundSeed)) PlaySound(*u, 1, nullptr, TAG_CHARGED, true);
        PlayCannonClip(b.chargedClip, true); chargedOn = true;
    }
    void StopCharged() { if (!chargedOn) return; StopSounds(TAG_CHARGED); if (cannonLoop) { cannonClip = -1; cannonLoop = false; } chargedOn = false; }

    // ---- the timeline editor (Weapons tab): drag the animation's end (its speed), the shot marker and the sounds
    int tlKind = 0, tlSel = -1, tlDrag = 0; float tlZoom = 220; float tlDragFrom[3] = {}; ImVec2 tlDragMouse;
    void TimelineEditor(Beam& b, ImVec2 size, bool& ch) {
        float ui = settings.uiScale;
        ImGui::SetNextItemWidth(150 * ui);
        if (ImGui::BeginCombo("##tlk", timelineName(tlKind))) { for (int k = 0; k < Beam::TIMELINES; k++) if (ImGui::Selectable(timelineName(k), tlKind == k)) { tlKind = k; tlSel = -1; } ImGui::EndCombo(); }
        Timeline& t = b.timelines[tlKind];
        ImGui::SameLine(); if (ImGui::Button("Play")) { StartTimeline(weapSel, tlKind); }
        ImGui::SameLine(); if (ImGui::Button("+ Sound")) ImGui::OpenPopup("addsnd");
        if (ImGui::BeginPopup("addsnd")) {
            for (const std::string& f : SoundFiles()) if (ImGui::MenuItem(f.c_str())) { SoundClip c; c.sound.file = f; c.at = 0; t.sounds.push_back(c); tlSel = (int)t.sounds.size() - 1; ch = true; }
            ImGui::Separator(); if (ImGui::MenuItem("Import sounds...")) ImportSounds();
            ImGui::EndPopup();
        }
        ImGui::SameLine(); ImGui::SetNextItemWidth(120 * ui); ImGui::SliderFloat("##zoom", &tlZoom, 60, 900, "zoom %.0f");
        // the canvas
        const std::string& clip = beamClip(b, tlKind);
        float clipLen = tlKind == Beam::Charging ? 0.f : ClipLenOf(b.model, clip);
        float rowH = ImGui::GetFrameHeight() + 4 * ui, head = 22 * ui, labelW = 120 * ui;
        int rows = 2 + (int)t.sounds.size();
        float h = std::min(size.y - ImGui::GetFrameHeightWithSpacing() * 2.6f, head + rows * rowH + 6);
        ImVec2 o = ImGui::GetCursorScreenPos(), sz(size.x, std::max(head + rowH * 2, h));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(o, ImVec2(o.x + sz.x, o.y + sz.y), IM_COL32(24, 26, 32, 255));
        ImGui::InvisibleButton("tl", sz); bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive(); ImGuiIO& io = ImGui::GetIO();
        if (hov && io.MouseWheel != 0 && io.KeyCtrl) tlZoom = std::clamp(tlZoom * (io.MouseWheel > 0 ? 1.15f : 0.87f), 60.f, 900.f);
        float x0 = o.x + labelW; auto X = [&](float sec) { return x0 + sec * tlZoom; }; auto S = [&](float px) { return (px - x0) / tlZoom; };
        dl->PushClipRect(o, ImVec2(o.x + sz.x, o.y + sz.y), true);
        for (float s = 0; X(s) < o.x + sz.x; s += 0.1f) {   // the ruler
            bool big = std::fabs(s - std::round(s * 2) / 2) < 1e-3; float x = X(s);
            dl->AddLine(ImVec2(x, o.y + head - (big ? 9 : 4)), ImVec2(x, o.y + sz.y), big ? IM_COL32(255, 255, 255, 40) : IM_COL32(255, 255, 255, 16));
            if (big) { char lb[16]; snprintf(lb, sizeof lb, "%.1f s", s); dl->AddText(ImVec2(x + 3, o.y + 2), IM_COL32(200, 200, 210, 200), lb); }
        }
        auto rowY = [&](int r) { return o.y + head + r * rowH; };
        auto label = [&](int r, const char* text) { dl->AddText(ImVec2(o.x + 6, rowY(r) + (rowH - ImGui::GetTextLineHeight()) * 0.5f), IM_COL32(220, 220, 230, 230), text); };
        // row 0: the animation (drag its end: speed)
        label(0, "Animation");
        float animEnd = clipLen / std::max(0.05f, t.animSpeed);
        if (clipLen > 0) {
            ImU32 c = IM_COL32(110, 150, 230, 220); dl->AddRectFilled(ImVec2(X(0), rowY(0) + 3), ImVec2(X(animEnd), rowY(0) + rowH - 3), c, 3);
            char lb[96]; snprintf(lb, sizeof lb, "%s  x%.2f", clip.c_str(), t.animSpeed); dl->AddText(ImVec2(X(0) + 5, rowY(0) + 5), IM_COL32(255, 255, 255, 240), lb);
            dl->AddRectFilled(ImVec2(X(animEnd) - 4, rowY(0) + 3), ImVec2(X(animEnd), rowY(0) + rowH - 3), IM_COL32(255, 255, 255, 200), 2);
        } else dl->AddText(ImVec2(X(0) + 5, rowY(0) + 5), IM_COL32(160, 160, 170, 200), tlKind == Beam::Charging ? "(charging plays no animation)" : "(the model has no such animation)");
        // row 1: the shot leaves
        label(1, tlKind == Beam::Charging ? "" : "Shot leaves");
        if (tlKind != Beam::Charging) { float x = X(t.shotAt), y = rowY(1) + rowH * 0.5f; dl->AddQuadFilled(ImVec2(x, y - 7), ImVec2(x + 7, y), ImVec2(x, y + 7), ImVec2(x - 7, y), IM_COL32(255, 200, 80, 255)); }
        // the sounds
        for (int i = 0; i < (int)t.sounds.size(); i++) {
            const SoundClip& c = t.sounds[i]; float full = SoundSeconds(c.sound.file), len = (c.sound.length > 0 ? c.sound.length : std::max(0.f, full - c.sound.trimStart)) / std::max(0.05f, c.sound.pitch);
            int r = 2 + i; label(r, c.sound.file.c_str());
            bool sel = tlSel == i; ImU32 col = sel ? IM_COL32(120, 220, 140, 240) : IM_COL32(80, 170, 110, 220);
            dl->AddRectFilled(ImVec2(X(c.at), rowY(r) + 3), ImVec2(X(c.at + std::max(0.02f, len)), rowY(r) + rowH - 3), col, 3);
            if (auto w = LoadSound(c.sound.file)) {   // its waveform, roughly
                float a = X(c.at), bb = X(c.at + len); int n = std::max(2, (int)(bb - a) / 2); float mid = rowY(r) + rowH * 0.5f, amp = rowH * 0.38f;
                for (int k = 0; k < n; k++) {
                    double secIn = c.sound.trimStart + (double)k / n * len * c.sound.pitch; size_t idx = (size_t)(secIn * w->rate) * w->channels;
                    float v = 0; for (size_t q = idx; q < std::min(w->samples.size(), idx + 64 * w->channels); q++) v = std::max(v, std::fabs(w->samples[q]));
                    float x = a + (bb - a) * k / n; dl->AddLine(ImVec2(x, mid - v * amp), ImVec2(x, mid + v * amp), IM_COL32(20, 50, 30, 200));
                }
            }
            dl->AddRectFilled(ImVec2(X(c.at), rowY(r) + 3), ImVec2(X(c.at) + 4, rowY(r) + rowH - 3), IM_COL32(255, 255, 255, 170), 2);
            dl->AddRectFilled(ImVec2(X(c.at + len) - 4, rowY(r) + 3), ImVec2(X(c.at + len), rowY(r) + rowH - 3), IM_COL32(255, 255, 255, 170), 2);
        }
        // the playhead
        for (const ActiveTl& a : activeTl) if (a.beam == weapSel && a.kind == tlKind && a.play.running()) { float x = X((float)a.play.t); dl->AddLine(ImVec2(x, o.y), ImVec2(x, o.y + sz.y), IM_COL32(255, 80, 80, 255), 2); }
        // pressing: what is under the mouse
        if (ImGui::IsItemActivated()) {
            tlDrag = 0; tlDragMouse = io.MousePos; float mx = io.MousePos.x, my = io.MousePos.y;
            int r = (int)std::floor((my - o.y - head) / rowH);
            if (r == 0 && clipLen > 0 && std::fabs(mx - X(animEnd)) < 8) { tlDrag = 1; tlDragFrom[0] = animEnd; }
            else if (r == 1 && tlKind != Beam::Charging && std::fabs(mx - X(t.shotAt)) < 9) { tlDrag = 2; tlDragFrom[0] = t.shotAt; }
            else if (r >= 2 && r - 2 < (int)t.sounds.size()) {
                SoundClip& c = t.sounds[r - 2]; float full = SoundSeconds(c.sound.file), len = (c.sound.length > 0 ? c.sound.length : std::max(0.f, full - c.sound.trimStart)) / std::max(0.05f, c.sound.pitch);
                tlSel = r - 2; tlDragFrom[0] = c.at; tlDragFrom[1] = c.sound.trimStart; tlDragFrom[2] = len * c.sound.pitch;
                if (std::fabs(mx - X(c.at)) < 6) tlDrag = 4; else if (std::fabs(mx - X(c.at + len)) < 6) tlDrag = 5; else if (mx > X(c.at) && mx < X(c.at + len)) tlDrag = 3; else tlSel = -1;
            } else tlSel = -1;
        }
        if (act && tlDrag) {
            float d = (io.MousePos.x - tlDragMouse.x) / tlZoom; auto snap = [&](float v) { return io.KeyShift ? std::round(v * 20) / 20 : v; };
            if (tlDrag == 1 && clipLen > 0) { float e = std::max(0.05f, snap(tlDragFrom[0] + d)); t.animSpeed = std::clamp(clipLen / e, 0.05f, 20.f); }
            if (tlDrag == 2) t.shotAt = std::max(0.f, snap(tlDragFrom[0] + d));
            if (tlSel >= 0 && tlSel < (int)t.sounds.size()) {
                SoundClip& c = t.sounds[tlSel]; float full = SoundSeconds(c.sound.file), p = std::max(0.05f, c.sound.pitch);
                if (tlDrag == 3) c.at = std::max(0.f, snap(tlDragFrom[0] + d));
                if (tlDrag == 4) { float dd = std::clamp(d, -tlDragFrom[1] / p, tlDragFrom[2] / p - 0.02f); c.at = std::max(0.f, tlDragFrom[0] + dd); c.sound.trimStart = tlDragFrom[1] + dd * p; c.sound.length = std::max(0.02f, tlDragFrom[2] - dd * p); }
                if (tlDrag == 5) { c.sound.length = std::clamp(snap(tlDragFrom[2] + d * p), 0.02f, std::max(0.02f, full - c.sound.trimStart)); }
            }
        }
        if (ImGui::IsItemDeactivated() && tlDrag) { tlDrag = 0; ch = true; }
        if (hov) {
            float mx = io.MousePos.x; int r = (int)std::floor((io.MousePos.y - o.y - head) / rowH);
            if ((r == 0 && clipLen > 0 && std::fabs(mx - X(animEnd)) < 8)) ImGui::SetTooltip("Drag: the animation's speed (x%.2f)", t.animSpeed);
            else if (r == 1 && tlKind != Beam::Charging && std::fabs(mx - X(t.shotAt)) < 9) ImGui::SetTooltip("Drag: when the shot leaves (%.2f s)", t.shotAt);
        }
        dl->PopClipRect();
        // the picked sound
        if (tlSel >= 0 && tlSel < (int)t.sounds.size()) {
            SoundClip& c = t.sounds[tlSel];
            Prop("Starts at"); ch |= ImGui::DragFloat("##at", &c.at, 0.005f, 0, 60, "%.3f s");
            ch |= SoundEdit("tlsnd", c.sound, true);
            if (ImGui::SmallButton("Remove this sound") || (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete))) { t.sounds.erase(t.sounds.begin() + tlSel); tlSel = -1; ch = true; }
        } else Hint("Drag a sound to move it, its edges to cut it. Drag the end of the animation to speed it up or slow it down, and the yellow mark to set when the shot leaves. Shift snaps; Ctrl + wheel zooms.");
    }
    // ================================================================== the morph ball, bombs, breakable blocks
    Breakables breakables; std::string breakTool;            // breakTool: what the "Breakable blocks" tool marks with ("" any shot)
    struct Broken { int x, y, z; BlockId was; }; std::vector<Broken> broken;   // Play mode: blocks broken (they come back when Play ends)
    struct Bomb { V3 pos; float fuse; ParticleEmitter fx; }; std::vector<Bomb> bombsOut;
    struct Flash { V3 pos; float t; V3 col; double range; }; std::vector<Flash> flashes;
    V3 ballAx{ 1, 0, 0 }, ballAy{ 0, 1, 0 }, ballAz{ 0, 0, 1 }; V3 ballCamEye{ 0, 0, 0 }; bool ballCamSet = false;
    bool HasMorphBall() const { int m = game.player.morphBall; return m == 1 || (m == 2 && inv.keys.count("Morph Ball")); }
    bool HasBombs() const { int m = game.player.bombs; return HasMorphBall() && (m == 1 || (m == 2 && inv.keys.count("Bombs"))); }
    V3 BallCentre() const { return { player.feet.x, player.feet.y + player.height * 0.5, player.feet.z }; }
    void ToggleBall() {
        if (!playMode || play.dead()) return;
        if (!player.ball) {
            if (!HasMorphBall()) { Toast(game.player.morphBall == 0 ? "No morph ball in this game" : "You have not found the Morph Ball yet"); return; }
            player.ball = true; player.applySize(game.player); ballCamSet = false; PlaySound(playerSounds.morph);
            StopChargeTimeline(); chargeTlOn = false; weapState.charging = false; weapState.charge = 0;
        } else {
            PlayerRules r = game.player; Player stand = player; stand.ball = false; stand.applySize(r);
            if (!world->boxFree(player.feet, stand.halfW, stand.height)) { Toast("No room to stand up here"); return; }
            player.ball = false; player.applySize(game.player); PlaySound(playerSounds.morph, 0.8f);
        }
    }
    void BreakBlock(int x, int y, int z) {
        BlockId was = world->get(x, y, z); if (was == AIR) return;
        broken.push_back({ x, y, z, was }); world->set(x, y, z, AIR);
        V3 c{ x + 0.5, y + 0.5, z + 0.5 };
        ParticleSettings d = particlePreset("Impact"); d.burst = 40; d.color0 = 0xB0A898; d.color1 = 0x605850; d.glow = 0.3f; d.shape = PShape::Pixel; d.size0 = 0.09f; d.size1 = 0.05f; d.areaSize = { 0.5, 0.5, 0.5 }; d.area = PArea::Box; SpawnFx(d, c, { 0, 1, 0 });
        ParticleSettings sm = particlePreset("Smoke"); sm.rate = 0; sm.burst = 10; sm.burstEvery = 99; SpawnFx(sm, c, { 0, 1, 0 });
        PlaySound(playerSounds.crumble, 1, &c);
    }
    void RestoreBroken() { for (auto it = broken.rbegin(); it != broken.rend(); ++it) world->set(it->x, it->y, it->z, it->was); broken.clear(); }
    bool TryBreak(int x, int y, int z, const std::string& weapon, bool missile, bool charged) {
        const std::string* br = breakables.breaker(x, y, z); if (!br || !breaksWith(*br, weapon, missile, charged)) return false;
        BreakBlock(x, y, z); return true;
    }
    void DropBomb() {
        if (!HasBombs()) { if (game.player.bombs == 2) Toast("You have not found the Bombs yet"); return; }
        if ((int)bombsOut.size() >= std::max(1, game.player.bombsAtOnce)) return;
        ParticleSettings p = particlePreset("Charge"); p.color0 = 0xFFFFFF; p.color1 = 0xFFC040; p.areaSize = { 0.06, 0.06, 0.06 }; p.rate = 40; p.size1 = 0.06f;
        bombsOut.push_back({ BallCentre(), std::max(0.1f, game.player.bombFuse), ParticleEmitter(p, (uint32_t)(GetTickCount() + bombsOut.size())) });
        PlaySound(playerSounds.bomb);
    }
    void Explode(V3 at) {
        ParticleSettings b = particlePreset("Impact"); b.burst = 70; b.speed = 6; b.color0 = 0xFFFFE0; b.color1 = 0xFF9020; b.size0 = 0.08f; SpawnFx(b, at, { 0, 1, 0 });
        ParticleSettings f = particlePreset("Fire"); f.rate = 0; f.burst = 30; f.burstEvery = 99; f.life = 0.4f; SpawnFx(f, at, { 0, 1, 0 });
        flashes.push_back({ at, 0, { 3.0, 1.8, 0.6 }, 7 });
        AddSphere(at, game.player.bombSphere, 0xFFB850, 0.5f);
        PlaySound(playerSounds.boom, 1, &at);
        double reach = std::max(0.3f, game.player.bombReach);
        for (auto& p : blocksInReach(at, reach)) TryBreak(p[0], p[1], p[2], "Bomb", false, false);
        if (player.ball) { double up = bombLift(at, BallCentre(), reach + 0.35, game.player.bombJumpSpeed()); if (up > 0) { player.vel.y = std::max(player.vel.y, up); player.onGround = false; } }
        for (WorldObject& o : objects) {   // bomb doors
            if (o.kind != ObjKind::Door || o.door.openedBy != "Bomb" || Gone(o)) continue;
            FrameRef f = ModelOf(o); if (!f) continue; Box3 fb = fieldBox(o, *f, OpeningOf(o.model));
            V3 c{ std::clamp(at.x, fb.lo.x, fb.hi.x), std::clamp(at.y, fb.lo.y, fb.hi.y), std::clamp(at.z, fb.lo.z, fb.hi.z) }, d = c - at;
            if (d.x * d.x + d.y * d.y + d.z * d.z <= (reach + 0.4) * (reach + 0.4) && DoorNow(o).blocks()) doorRt[o.id].shoot(0.5f, 0.5f);
        }
        for (const WorldObject& o : objects) {   // "when something is shot" counts bombs too
            if (o.name.empty() || Gone(o) || o.kind == ObjKind::Trigger) continue;
            FrameRef f = ModelOf(o); if (!f) continue; Box3 b = objectBounds(o, *f); double g = reach * 0.6;
            if (at.x > b.lo.x - g && at.x < b.hi.x + g && at.y > b.lo.y - g && at.y < b.hi.y + g && at.z > b.lo.z - g && at.z < b.hi.z + g) Signal(When::Shot, o.name);
        }
    }
    void UpdateBall(double dt) {   // bombs tick, flashes fade, and the ball turns as it rolls
        for (Bomb& b : bombsOut) { b.fuse -= (float)dt; b.fx.update(dt, b.pos, 0, nullptr, b.fuse > 0); }
        for (size_t i = 0; i < bombsOut.size();) { if (bombsOut[i].fuse <= 0) { V3 at = bombsOut[i].pos; fx.push_back(bombsOut[i].fx); bombsOut.erase(bombsOut.begin() + i); Explode(at); } else i++; }
        for (Flash& f : flashes) f.t += (float)dt;
        flashes.erase(std::remove_if(flashes.begin(), flashes.end(), [](const Flash& f) { return f.t > 0.35f; }), flashes.end());
        if (!player.ball) return;
        double sp = std::sqrt(player.vel.x * player.vel.x + player.vel.z * player.vel.z), r = std::max(0.05, game.player.ballSize * 0.5);
        if (sp > 1e-4) {   // roll: turn around the axis across the way it goes
            V3 axis = Norm(cross({ 0, 1, 0 }, { player.vel.x, 0, player.vel.z })); double a = sp * dt / r, c = std::cos(a), s = std::sin(a);
            auto rot = [&](V3 v) { V3 kxv = cross(axis, v); double kd = axis.x * v.x + axis.y * v.y + axis.z * v.z; return v * c + kxv * s + axis * (kd * (1 - c)); };
            ballAx = Norm(rot(ballAx)); ballAy = Norm(rot(ballAy)); ballAz = Norm(cross(ballAx, ballAy)); ballAy = cross(ballAz, ballAx);
        }
    }
    // the third-person camera behind the ball (it does not go through walls)
    void BallCamera(V3& eye, V3& fwd) {
        V3 c = BallCentre(); double cp = std::clamp(player.pitch, -1.1, 0.4);
        V3 d{ std::sin(player.yaw) * std::cos(cp), std::sin(cp), std::cos(player.yaw) * std::cos(cp) };
        double dist = 2.6 + game.player.ballSize; V3 want = c - d * dist + V3{ 0, 0.5 + game.player.ballSize * 0.3, 0 };
        V3 e = want - c; double L = std::sqrt(e.x * e.x + e.y * e.y + e.z * e.z); e = e * (1 / std::max(1e-6, L));
        Hit h; if (world->raycast(c, e, L, h)) want = c + e * std::max(0.25, h.t - 0.25);
        if (!ballCamSet) { ballCamEye = want; ballCamSet = true; }
        ballCamEye = ballCamEye + (want - ballCamEye) * 0.35;
        eye = ballCamEye; fwd = Norm(c + V3{ 0, 0.25, 0 } - eye);
    }
    void DrawBall(int pass, V3 cam) {
        if (!playMode || !player.ball) return;
        const std::string& model = game.player.ballModel.empty() ? std::string("@morph-ball") : game.player.ballModel;
        FrameRef m = ModelNamed(model); if (!m) return;
        double s = game.player.ballSize / std::max(0.05, std::max({ m->sx, m->sy, m->sz }));   // scaled so its biggest side is the ball size
        DrawModelAxes(pass, model, BallCentre(), ballAx * s, ballAy * s, ballAz * s, cam);
    }
    // a model with its middle at centre (world), turned and sized by the axes (each one block of the model)
    void DrawModelAxes(int pass, const std::string& model, V3 centre, V3 ax, V3 ay, V3 az, V3 cam) {
        FrameRef m = ModelNamed(model); if (!m) return;
        const ModelGpu& g = GpuModel(model); if (g.mesh) ctx->PSSetShaderResources(9, 1, g.tex.GetAddressOf());
        V3 mc{ 0, m->sy * 0.5, 0 };   // the model's middle, from its pivot (bottom middle)
        V3 base = centre - cam - (ax * mc.x + ay * mc.y + az * mc.z);
        for (auto& part : g.parts) {
            const GpuChunk& gc = part.second; UINT n = pass == 0 ? gc.nO : pass == 1 ? gc.nC : gc.nW; if (!n) continue;
            V3 l{ part.first.x * (double)CS - m->pivot.x, part.first.y * (double)CS - m->pivot.y, part.first.z * (double)CS - m->pivot.z };
            V3 org = base + ax * l.x + ay * l.y + az * l.z;
            ChunkCB cc{ { (float)org.x, (float)org.y, (float)org.z, 0 }, { (float)ax.x, (float)ax.y, (float)ax.z, 0 }, { (float)ay.x, (float)ay.y, (float)ay.z, 0 }, { (float)az.x, (float)az.y, (float)az.z, 0 } };
            ctx->UpdateSubresource(cbChunk.Get(), 0, nullptr, &cc, 0, 0);
            UINT stride = sizeof(Vtx), off = 0;
            ctx->IASetVertexBuffers(0, 1, pass == 0 ? gc.vbO.GetAddressOf() : pass == 1 ? gc.vbC.GetAddressOf() : gc.vbW.GetAddressOf(), &stride, &off);
            ctx->IASetIndexBuffer(pass == 0 ? gc.ibO.Get() : pass == 1 ? gc.ibC.Get() : gc.ibW.Get(), DXGI_FORMAT_R32_UINT, 0);
            ctx->DrawIndexed(n, 0, 0);
        }
    }
    // the editor: marked blocks get a coloured outline (what breaks them: the colour)
    ImU32 BreakerColour(const std::string& b) {
        if (b.empty()) return IM_COL32(230, 235, 255, 220);
        if (b == "Bomb") return IM_COL32(255, 170, 60, 230);
        if (b == "Missile") return IM_COL32(255, 70, 70, 230);
        if (b == "Charged shot") return IM_COL32(255, 230, 90, 230);
        for (const Beam& w : weapons.beams) if (w.name == b) return IM_COL32((w.color >> 16) & 255, (w.color >> 8) & 255, w.color & 255, 230);
        for (const MissileType& m : weapons.missileTypes) if (m.name == b) return IM_COL32((m.color >> 16) & 255, (m.color >> 8) & 255, m.color & 255, 230);
        return IM_COL32(200, 120, 255, 230);
    }
    void DrawBreakables(ImDrawList* dl) {
        if (playMode || breakables.marks.empty()) return;
        bool tool = toolMode && toolSel == 4; double reach = tool ? 48 : 28; int drawn = 0; V3 e = player.eye();
        for (auto& kv : breakables.marks) {
            const auto& p = kv.first; double dx = p[0] + 0.5 - e.x, dy = p[1] + 0.5 - e.y, dz = p[2] + 0.5 - e.z;
            if (dx * dx + dy * dy + dz * dz > reach * reach || world->get(p[0], p[1], p[2]) == AIR) continue;
            if (++drawn > 4000) break;
            ImU32 col = BreakerColour(kv.second); const double g = 0.02;
            V3 a{ p[0] - g, p[1] - g, p[2] - g }, b{ p[0] + 1 + g, p[1] + 1 + g, p[2] + 1 + g };
            V3 c[8]; for (int i = 0; i < 8; i++) c[i] = { (i & 1) ? b.x : a.x, (i & 2) ? b.y : a.y, (i & 4) ? b.z : a.z };
            const int ed[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
            for (auto& k : ed) Line3D(dl, c[k[0]], c[k[1]], col);
            Line3D(dl, c[0], c[7], (col & 0x00FFFFFF) | 0x60000000); Line3D(dl, c[3], c[4], (col & 0x00FFFFFF) | 0x60000000);   // a cross: "this one breaks"
        }
    }
    std::vector<std::string> Breakers() { std::vector<std::string> v{ "", "Bomb", "Missile", "Charged shot" }; for (const Beam& b : weapons.beams) v.push_back(b.name); for (const MissileType& m : weapons.missileTypes) if (m.name != "Missile") v.push_back(m.name); return v; }
    static std::string BreakerLabel(const std::string& b) { return b.empty() ? "Any shot (or bomb, or missile)" : b == "Bomb" ? "Bombs only" : b == "Missile" ? "Missiles only (any kind)" : b == "Charged shot" ? "Charged shots only" : b + " only"; }
    bool BreakerCombo(const char* id, std::string& v) {
        bool ch = false;
        if (ImGui::BeginCombo(id, BreakerLabel(v).c_str())) { for (const std::string& b : Breakers()) if (ImGui::Selectable(BreakerLabel(b).c_str(), v == b)) { v = b; ch = true; } ImGui::EndCombo(); }
        return ch;
    }
    void BreakToolClick(bool L, bool R) {   // tool slot 5: left marks (hold and sweep to paint), right unmarks
        if (!toolValid) return;
        int x = toolHit.x, y = toolHit.y, z = toolHit.z;
        if (L) { const std::string* b = breakables.breaker(x, y, z); if (!b || *b != breakTool) breakables.mark(x, y, z, breakTool); }
        if (R) breakables.unmark(x, y, z);
    }
    void BreakToolInspector() {
        Prop("Breaks with"); BreakerCombo("##bt", breakTool);
        Hint("Left click marks a block (hold and sweep to mark many), right click unmarks it. In the game, marked blocks break when hit with what you picked, and come back when Play ends. The outline colour says what breaks them.");
        ImGui::Text("%d blocks marked in this world", (int)breakables.marks.size());
        if (!breakables.marks.empty() && ImGui::SmallButton("Unmark all")) { breakables.marks.clear(); Toast("All marks removed"); }
        ImGui::TextDisabled("A box of blocks: the selection box tool can mark it too.");
    }
    // ---- Events: the game's logic as cards, one card = "when ... only if ... do ..."
    static bool KindFits(const WorldObject& o, const std::string& k) {
        if (k == "zone") return o.kind == ObjKind::Trigger;
        if (k == "door") return o.kind == ObjKind::Door;
        if (k == "platform") return o.kind == ObjKind::Platform;
        if (k == "light") return o.kind == ObjKind::Light || o.kind == ObjKind::Particles;
        return true;
    }
    std::vector<std::string> NamesOf(const std::string& kind) const {
        std::vector<std::string> v;
        for (const WorldObject& o : objects) if (!o.name.empty() && KindFits(o, kind) && std::find(v.begin(), v.end(), o.name) == v.end()) v.push_back(o.name);
        std::sort(v.begin(), v.end()); return v;
    }
    std::string FirstName(const std::string& kind) const { auto v = NamesOf(kind); return v.empty() ? "" : v[0]; }
    // names the player can have: keys, counters (pickups), flags (from the cards)
    std::vector<std::string> KnownNames(char what) const {
        std::vector<std::string> v; auto add = [&](const std::string& n) { if (!n.empty() && std::find(v.begin(), v.end(), n) == v.end()) v.push_back(n); };
        for (const WorldObject& o : objects) {
            if (what == 'k') { if (o.kind == ObjKind::Pickup && o.pickup.type == PickupType::Key) add(o.pickup.name); if (o.kind == ObjKind::Door) add(o.door.key); }
            if (what == 'c' && o.kind == ObjKind::Pickup && o.pickup.type != PickupType::Health) add(o.pickup.name);
            if (what == 'p' && o.kind == ObjKind::Pickup) add(o.pickup.type == PickupType::Health ? "Health" : o.pickup.name);
        }
        if (what == 'f') for (const EventCard& c : events) { for (auto& i : c.ifs) if (i.kind == If::FlagOn || i.kind == If::FlagOff) add(i.name); for (auto& d : c.dos) if (d.kind == Do::FlagOn || d.kind == Do::FlagOff) add(d.text); }
        if (what == 'k') for (const EventCard& c : events) for (auto& d : c.dos) if (d.kind == Do::GiveKey) add(d.text);
        if (what == 'h') for (const HudPlane& p : hud.planes) for (const HudElement& e : p.elements) add(e.name);
        if (what == 'c') for (const EventCard& c : events) for (auto& d : c.dos) if (d.kind == Do::AddCount) add(d.text);
        std::sort(v.begin(), v.end()); return v;
    }
    bool ObjectPick(const char* id, std::string& v, const char* kind, bool allowAny) {
        std::vector<std::string> names = NamesOf(kind); bool ch = false;
        std::string shown = v.empty() ? (allowAny ? "(any)" : "(pick one)") : v;
        bool missing = !v.empty() && std::find(names.begin(), names.end(), v) == names.end();
        if (missing) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.55f, 0.4f, 1));
        if (ImGui::BeginCombo(id, shown.c_str())) {
            if (missing) ImGui::PopStyleColor();
            if (allowAny && ImGui::Selectable("(any)", v.empty())) { v.clear(); ch = true; }
            for (const std::string& n : names) if (ImGui::Selectable(n.c_str(), v == n)) { v = n; ch = true; }
            if (names.empty()) ImGui::TextDisabled(std::string(kind) == "any" ? "Name an object in the Inspector first" : "No %s with a name yet", kind);
            ImGui::EndCombo();
        } else if (missing) ImGui::PopStyleColor();
        if (missing && ImGui::IsItemHovered()) ImGui::SetTooltip("There is no object called \"%s\" in this world", v.c_str());
        return ch;
    }
    bool NamePick(const char* id, std::string& v, char what, const char* hint) {
        float bw = ImGui::GetFrameHeight(); bool ch = false;
        ImGui::SetNextItemWidth(std::max(40.f, ImGui::GetContentRegionAvail().x - bw - ImGui::GetStyle().ItemSpacing.x));
        ch |= ImGui::InputTextWithHint(id, hint, &v);
        ImGui::SameLine(); std::string pid = std::string(id) + "pick";
        if (ImGui::BeginCombo(pid.c_str(), "", ImGuiComboFlags_NoPreview | ImGuiComboFlags_PopupAlignLeft)) {
            auto names = KnownNames(what);
            for (const std::string& n : names) if (ImGui::Selectable(n.c_str(), v == n)) { v = n; ch = true; }
            if (names.empty()) ImGui::TextDisabled("Nothing yet: type a name");
            ImGui::EndCombo();
        }
        return ch;
    }
    static char NameKind(When w) { return w == When::Pickup ? 'p' : 'c'; }
    static char NameKind(If c) { return c == If::HasKey || c == If::NoKey ? 'k' : c == If::CountAtLeast ? 'c' : 'f'; }
    static char NameKind(Do d) { return d == Do::GiveKey || d == Do::TakeKey ? 'k' : d == Do::AddCount ? 'c' : d == Do::HudShow || d == Do::HudHide || d == Do::HudFlash ? 'h' : 'f'; }
    static const char* NameHint(char k) { return k == 'k' ? "Red key" : k == 'c' ? "Gem" : k == 'p' ? "Gem (empty: anything)" : k == 'h' ? "Energy" : "door-done"; }
    template <class E> bool KindCombo(const char* id, E& k) {
        int v = (int)k; bool ch = false;
        if (ImGui::BeginCombo(id, needOf(k).label)) {
            for (int i = 0; i < (int)E::COUNT; i++) if (ImGui::Selectable(needOf((E)i).label, v == i)) { k = (E)i; ch = true; }
            ImGui::EndCombo();
        }
        return ch;
    }
    void SectionLabel(const char* t, ImVec4 c) { ImGui::PushStyleColor(ImGuiCol_Text, c); ImGui::TextUnformatted(t); ImGui::PopStyleColor(); }
    EventCard Template(int t) {
        EventCard c; std::string zone = FirstName("zone"), door = FirstName("door"), light = FirstName("light"), plat = FirstName("platform");
        switch (t) {
        case 0: c.name = "Welcome message"; c.when = { When::EnterZone, zone }; c.once = true; c.dos = { { Do::Message, "", "Welcome!", 3 } }; break;
        case 1: c.name = "Key opens the door"; c.when = { When::EnterZone, zone }; c.ifs = { { If::HasKey, "Red key" } }; c.dos = { { Do::UnlockDoor, door }, { Do::OpenDoor, door } }; break;
        case 2: c.name = "Trap: doors shut"; c.when = { When::EnterZone, zone }; c.once = true;
            c.dos = { { Do::CloseDoor, door }, { Do::LockDoor, door }, { Do::Message, "", "It's a trap!", 2 }, { Do::Wait, "", "", 5 }, { Do::UnlockDoor, door }, { Do::OpenDoor, door } }; break;
        case 3: c.name = "Collect 3 gems"; c.when = { When::CountReaches, "Gem", 3 }; c.dos = { { Do::Message, "", "All gems found!", 3 }, { Do::OpenDoor, door } }; break;
        case 4: c.name = "Lights on"; c.when = { When::EnterZone, zone }; c.dos = { { Do::LightOn, light }, { Do::Particles, light, "Sparks" } }; break;
        case 5: c.name = "Call the lift"; c.when = { When::Shot, "" }; c.dos = { { Do::MovePlatform, plat } }; break;
        case 7: c.name = "Low energy warning"; c.when = { When::Every, "", 1 }; c.ifs = { { If::HealthBelow, "", 30 } }; c.dos = { { Do::HudFlash, "", "Energy" } }; break;
        case 6: c.name = "Hidden at the start"; c.when = { When::GameStarts }; c.dos = { { Do::Hide, FirstName("any") } }; break;
        }
        return c;
    }
    void ShowTab(const char* name) {   // bring a docked panel to the front (its tab selected)
        ImGuiWindow* w = ImGui::FindWindowByName(name);
        if (!w) { ImGui::SetNextWindowFocus(); return; }
        ImGui::FocusWindow(w);
        if (w->DockNode && w->DockNode->TabBar) w->DockNode->TabBar->NextSelectedTabId = w->TabId;
    }
    // the fields one part needs, on the same line as its kind (each field gets an equal share of the rest of the row)
    bool InlineFields(const Need& n, std::string& obj, std::string& name, std::string& text, float& num, char nk, bool anyObj, bool isWhen, bool isParticles, float width) {
        int count = (int)n.object + (int)n.name + (int)n.text + (int)n.number; if (!count) return false;
        float sp = ImGui::GetStyle().ItemSpacing.x, w = std::floor((width - sp * (count - 1)) / count); bool ch = false; bool first = true;
        auto next = [&] { if (!first) ImGui::SameLine(); first = false; ImGui::SetNextItemWidth(w); };
        if (n.object) { next(); ch |= ObjectPick("##obj", obj, n.objectKind, anyObj); if (ImGui::IsItemHovered() && !obj.empty()) ImGui::SetTooltip("%s", obj.c_str()); }
        if (n.name) { if (!first) ImGui::SameLine(); first = false; ImGui::BeginGroup(); ImGui::PushItemWidth(w); ch |= NamePickW("##nm", isWhen ? obj : name, nk, NameHint(nk), w); ImGui::PopItemWidth(); ImGui::EndGroup(); }
        if (n.text) {
            next();
            if (isParticles) { if (ImGui::BeginCombo("##fx", text.empty() ? "Sparks" : text.c_str())) { for (auto& pn : particlePresets()) if (ImGui::Selectable(pn.c_str(), text == pn)) { text = pn; ch = true; } ImGui::EndCombo(); } }
            else ch |= ImGui::InputTextWithHint("##txt", "what it says", &text);
        }
        if (n.number) { next(); std::string f = std::string("%.1f ") + (std::string(n.numberLabel) == "seconds" ? "s" : n.numberLabel); ch |= ImGui::DragFloat("##num", &num, 0.1f, 0, 10000, f.c_str()); }
        return ch;
    }
    bool NamePickW(const char* id, std::string& v, char what, const char* hint, float w) {
        float bw = ImGui::GetFrameHeight(); bool ch = false;
        ImGui::SetNextItemWidth(std::max(30.f, w - bw - ImGui::GetStyle().ItemSpacing.x));
        ch |= ImGui::InputTextWithHint(id, hint, &v);
        ImGui::SameLine(); std::string pid = std::string(id) + "pick";
        if (ImGui::BeginCombo(pid.c_str(), "", ImGuiComboFlags_NoPreview | ImGuiComboFlags_PopupAlignLeft)) {
            auto names = KnownNames(what);
            for (const std::string& n : names) if (ImGui::Selectable(n.c_str(), v == n)) { v = n; ch = true; }
            if (names.empty()) ImGui::TextDisabled("Nothing yet: type a name");
            ImGui::EndCombo();
        }
        return ch;
    }
    // one row: [kind | fields ... | buttons]; the kind gets 45% (all of it when there are no fields)
    template <class E> bool KindRow(E& k, float buttons, bool& kindChanged) {
        float avail = ImGui::GetContentRegionAvail().x - buttons; const Need& n = needOf(k);
        bool fields = n.object || n.name || n.text || n.number;
        ImGui::SetNextItemWidth(fields ? std::floor(avail * 0.45f) : avail);
        kindChanged = KindCombo("##k", k);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", needOf(k).label);
        if (fields) ImGui::SameLine();
        return kindChanged;
    }
    int evSel = 0, evShotPose = 0;
    void CardEditor(EventCard& c, bool& ch) {
        float ui = settings.uiScale, bw = ImGui::GetFrameHeight(), sp = ImGui::GetStyle().ItemSpacing.x;
        ch |= ImGui::Checkbox("##on", &c.enabled); if (ImGui::IsItemHovered()) ImGui::SetTooltip("On / off");
        ImGui::SameLine(); ImGui::SetNextItemWidth(std::min(260 * ui, ImGui::GetContentRegionAvail().x * 0.5f)); ch |= ImGui::InputTextWithHint("##name", "card name", &c.name);
        ImGui::SameLine(); ch |= ImGui::Checkbox("Only once", &c.once);
        if (playMode) { ImGui::SameLine(); ImGui::TextDisabled("  ran %d times", evRun.firedCount(evSel)); }
        bool wide = ImGui::GetContentRegionAvail().x > 760 * ui;
        auto whenCol = [&] {
            SectionLabel("WHEN", ImVec4(1, 0.75f, 0.35f, 1));
            ImGui::PushID("when"); bool kc; std::string dummy;
            KindRow(c.when.kind, 0, kc);
            if (kc) { const Need& n = needOf(c.when.kind); if (n.object) c.when.target = FirstName(n.objectKind); else if (!n.name) c.when.target.clear(); if (c.when.kind == When::Every || c.when.kind == When::After) c.when.number = 5; ch = true; }
            ch |= InlineFields(needOf(c.when.kind), c.when.target, dummy, dummy, c.when.number, NameKind(c.when.kind), true, true, false, ImGui::GetContentRegionAvail().x);
            ImGui::PopID();
        };
        auto ifCol = [&] {
            SectionLabel(c.ifs.empty() ? "ONLY IF  (always)" : "ONLY IF  (all of these)", ImVec4(0.55f, 0.8f, 1, 1));
            int drop = -1;
            for (int k = 0; k < (int)c.ifs.size(); k++) {
                EventIf& e = c.ifs[k]; ImGui::PushID(1000 + k); bool kc; std::string o, t;
                KindRow(e.kind, bw + sp, kc); ch |= kc;
                ch |= InlineFields(needOf(e.kind), o, e.name, t, e.number, NameKind(e.kind), false, false, false, ImGui::GetContentRegionAvail().x - bw - sp);
                ImGui::SameLine(); if (ImGui::Button("x", ImVec2(bw, 0))) drop = k;
                ImGui::PopID();
            }
            if (drop >= 0) { c.ifs.erase(c.ifs.begin() + drop); ch = true; }
            if (ImGui::SmallButton("+ condition")) { auto keys = KnownNames('k'); c.ifs.push_back({ If::HasKey, keys.empty() ? "Red key" : keys[0] }); ch = true; }
        };
        auto doCol = [&] {
            SectionLabel("DO  (in order)", ImVec4(0.6f, 1, 0.6f, 1));
            int drop = -1, up = -1;
            for (int k = 0; k < (int)c.dos.size(); k++) {
                EventDo& d = c.dos[k]; ImGui::PushID(2000 + k); bool kc;
                ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("%d", k + 1); ImGui::SameLine();
                float btn = bw * 2 + sp * 2;
                KindRow(d.kind, btn, kc);
                if (kc) { const Need& n = needOf(d.kind); if (n.object && d.target.empty()) d.target = FirstName(n.objectKind); if (d.kind == Do::Wait) d.number = 1; ch = true; }
                ch |= InlineFields(needOf(d.kind), d.target, d.text, d.text, d.number, NameKind(d.kind), false, false, d.kind == Do::Particles, ImGui::GetContentRegionAvail().x - btn);
                ImGui::SameLine(); ImGui::BeginDisabled(k == 0); if (ImGui::ArrowButton("##up", ImGuiDir_Up)) up = k; ImGui::EndDisabled();
                ImGui::SameLine(); if (ImGui::Button("x", ImVec2(bw, 0))) drop = k;
                ImGui::PopID();
            }
            if (drop >= 0) { c.dos.erase(c.dos.begin() + drop); ch = true; }
            if (up > 0) { std::swap(c.dos[up], c.dos[up - 1]); ch = true; }
            if (ImGui::SmallButton("+ action")) { c.dos.push_back({ Do::Message, "", "Hello!", 3 }); ch = true; }
        };
        if (wide && ImGui::BeginTable("parts", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_PadOuterX)) {
            ImGui::TableSetupColumn("w", ImGuiTableColumnFlags_WidthStretch, 1.0f); ImGui::TableSetupColumn("i", ImGuiTableColumnFlags_WidthStretch, 1.0f); ImGui::TableSetupColumn("d", ImGuiTableColumnFlags_WidthStretch, 1.6f);
            ImGui::TableNextColumn(); whenCol(); ImGui::TableNextColumn(); ifCol(); ImGui::TableNextColumn(); doCol();
            ImGui::EndTable();
        } else { whenCol(); ImGui::Spacing(); ifCol(); ImGui::Spacing(); doCol(); }
    }
    void EventsPanel() {
        if (ImGuiWindow* a = ImGui::FindWindowByName("Assets")) if (a->DockId) ImGui::SetNextWindowDockID(a->DockId, evShotPose == 2 ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
        if (evShotPose == 1) {   // screenshots: the panel floating big, to show the wide layout
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowDockID(0, ImGuiCond_Always); ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 30, vp->WorkPos.y + 120), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x - 60, vp->WorkSize.y * 0.55f), ImGuiCond_Always); ImGui::SetNextWindowFocus();
        }
        if (evShotPose == 2) evShotPose = 0;
        if (focusEvents) { ShowTab("Events"); focusEvents = false; }
        if (!ImGui::Begin("Events")) { ImGui::End(); return; }
        float ui = settings.uiScale; bool ch = false;
        if (ImGui::Button("+ New card")) { EventCard c; c.when.target = FirstName("zone"); c.name = "Event " + std::to_string(events.size() + 1); events.push_back(c); evSel = (int)events.size() - 1; ch = true; }
        ImGui::SameLine(); ImGui::SetNextItemWidth(200 * ui);
        if (ImGui::BeginCombo("##tpl", "+ From an example...")) {
            const char* tn[] = { "Message when you walk in", "A key opens a door", "Trap: the door shuts for 5 s", "Collect 3 gems, a door opens", "Lights on when you walk in", "Shoot it: the lift comes", "Hidden until something shows it", "Low energy: flash the HUD" };
            for (int t = 0; t < 8; t++) if (ImGui::Selectable(tn[t])) { events.push_back(Template(t)); evSel = (int)events.size() - 1; ch = true; }
            ImGui::EndCombo();
        }
        if (playMode && !evRun.flags.empty()) { std::string f; for (auto& x : evRun.flags) f += (f.empty() ? "" : ", ") + x; ImGui::SameLine(); ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "  Flags on: %s", f.c_str()); }
        else { ImGui::SameLine(); ImGui::TextDisabled(playMode ? "  Cards light up when they run." : "  Objects are found by name."); }
        if (events.empty()) { ImGui::Separator(); Hint("No event cards yet. A card says: when something happens (the player walks into a zone, a door is shot...), only if something is true (has a key...), do these things in order (open a door, show a message, switch on a light...). Try \"+ From an example...\"."); ImGui::End(); return; }
        evSel = std::clamp(evSel, 0, (int)events.size() - 1);
        // left: every card as one sentence; right: the picked card
        ImGui::BeginChild("list", ImVec2(std::max(220 * ui, ImGui::GetContentRegionAvail().x * 0.3f), 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        int drop = -1, dup = -1, moveBy = 0, moveFrom = -1;
        for (int i = 0; i < (int)events.size(); i++) {
            EventCard& c = events[i]; ImGui::PushID(i);
            double lit = playMode && evFlash.count(i) ? std::max(0.0, 1 - (animTime - evFlash[i]) / 1.2) : 0;
            ImVec2 p0 = ImGui::GetCursorScreenPos(); float rightX = p0.x + ImGui::GetContentRegionAvail().x;
            ImGui::BeginGroup();
            ImGui::PushStyleColor(ImGuiCol_Text, c.enabled ? ImVec4(1, 1, 1, 1) : ImVec4(0.5f, 0.5f, 0.5f, 1));
            if (ImGui::Selectable(c.name.empty() ? "(no name)" : c.name.c_str(), evSel == i)) evSel = i;
            ImGui::PopStyleColor();
            if (ImGui::BeginPopupContextItem("cardmenu")) {
                if (ImGui::MenuItem("Duplicate")) dup = i;
                if (ImGui::MenuItem("Move up", nullptr, false, i > 0)) { moveBy = -1; moveFrom = i; }
                if (ImGui::MenuItem("Move down", nullptr, false, i + 1 < (int)events.size())) { moveBy = 1; moveFrom = i; }
                if (ImGui::MenuItem(c.enabled ? "Switch off" : "Switch on")) { c.enabled = !c.enabled; ch = true; }
                ImGui::Separator(); if (ImGui::MenuItem("Delete")) drop = i;
                ImGui::EndPopup();
            }
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.64f, 0.7f, c.enabled ? 1.f : 0.5f));
            ImGui::Indent(8 * ui); ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(describe(c).c_str()); ImGui::PopTextWrapPos(); ImGui::Unindent(8 * ui);
            ImGui::PopStyleColor();
            ImGui::EndGroup();
            if (ImGui::IsItemClicked()) evSel = i;
            if (lit > 0) { ImVec2 p1(rightX, ImGui::GetItemRectMax().y);
                ImGui::GetWindowDrawList()->AddRect(ImVec2(p0.x - 3, p0.y - 2), ImVec2(p1.x, p1.y + 2), IM_COL32(255, 200, 80, (int)(255 * lit)), 4.0f, 0, 2.0f); }
            ImGui::Spacing();
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (dup >= 0) { EventCard c = events[dup]; c.name += " copy"; events.insert(events.begin() + dup + 1, c); evSel = dup + 1; ch = true; }
        if (drop >= 0) { events.erase(events.begin() + drop); ch = true; }
        if (moveFrom >= 0) { std::swap(events[moveFrom], events[moveFrom + moveBy]); evSel = moveFrom + moveBy; ch = true; }
        if (events.empty()) { ImGui::End(); return; }
        evSel = std::clamp(evSel, 0, (int)events.size() - 1);
        ImGui::SameLine();
        ImGui::BeginChild("edit", ImVec2(0, 0), ImGuiChildFlags_Borders);
        ImGui::PushID(evSel); CardEditor(events[evSel], ch); ImGui::PopID();
        ImGui::Spacing(); ImGui::TextDisabled("Right-click a card on the left to duplicate, move or delete it.");
        ImGui::EndChild();
        ImGui::End();
        (void)ch;
    }
    // ---- Assets: blocks by folder, textures, shapes; thumbnails, search, size
    void Assets() {
        if (focusAssets) { ShowTab("Assets"); focusAssets = false; }
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
        if (ImGui::TreeNodeEx("Models", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
            folder("All models", 3, "", (int)(models.models.size() + meshModels.size()));
            int nb = 0; for (auto& kv : meshModels) nb += IsBuiltin(kv.first);
            folder("Built from blocks", 3, "blocks", (int)models.models.size()); folder("Imported", 3, "imported", (int)meshModels.size() - nb); folder("Built in", 3, "builtin", nb);
            ImGui::TreePop();
        }
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
        ImGui::SameLine();
        if (assetKind == 3) { if (ImGui::Button("Import models...")) ImportModelsDialog(); if (ImGui::IsItemHovered()) ImGui::SetTooltip("Blockbench (.bbmodel), .obj, glTF (.gltf / .glb), FBX (.fbx)"); }
        else if (ImGui::Button("Import images...")) { auto f = PickImages(); if (!f.empty()) { importFiles = f; wantPopup = "Import images"; } }
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
            for (const std::string& name : AllModelNames()) {
                mi++; bool mesh = IsMesh(name);
                if (assetCat == "blocks" && mesh) continue; if (assetCat == "imported" && (!mesh || IsBuiltin(name))) continue; if (assetCat == "builtin" && !IsBuiltin(name)) continue;
                if (!q.empty() && Lower(name).find(q) == std::string::npos) continue;
                FrameRef f = ModelNamed(name);
                ImGui::PushID(mi); ImVec2 p;
                if (cell("m", name, sel == Sel::Model && selModel == name, p) && !ImGui::IsMouseReleased(ImGuiMouseButton_Right)) { StartPlacing(name); sel = Sel::Model; selModel = name; }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%.2f x %.2f x %.2f blocks%s\nClick to place it, double-click for the 3D viewer", name.c_str(), f->sx, f->sy, f->sz, mesh ? " (imported)" : "");
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) { viewerModel = name; viewerOpen = true; viewerFit = true; viewerClip = -1; placing.clear(); }
                ModelIcon(ImGui::GetWindowDrawList(), name, ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), s * 0.8f);
                if (ImGui::BeginPopupContextItem("ctx")) {
                    if (ImGui::MenuItem("Place it")) StartPlacing(name);
                    if (ImGui::MenuItem("3D viewer")) { viewerModel = name; viewerOpen = true; viewerFit = true; viewerClip = -1; }
                    if (ImGui::MenuItem("Show in the Inspector")) { sel = Sel::Model; selModel = name; }
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
        EventsPanel();
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
            player.feet = { sh.eye.x, sh.eye.y - player.eyeH, sh.eye.z }; player.yaw = std::atan2(dlt.x, dlt.z); player.pitch = std::atan2(dlt.y, hz); player.vel = { 0, 0, 0 };
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
                player.feet = { sh.eye.x, sh.eye.y - player.eyeH, sh.eye.z }; player.yaw = std::atan2(dlt.x, dlt.z); player.pitch = std::atan2(dlt.y, hz); player.vel = { 0, 0, 0 };
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
                // imported models: a Blockbench lamp post, an .obj rock, a glTF crystal
                for (auto& f : writeDemoModels(ExeDir() + "demo_models")) ImportModelFile(f);
                auto place = [&](const std::string& name, V3 pos, double yaw, double scale) { WorldObject o; o.id = nextObjId++; o.name = UniqueObjectName(name); o.model = name; o.pos = pos; o.yaw = yaw; o.scale = scale; objects.push_back(o); };
                place("Lamp post", { 33.5, (double)gy, 21.5 }, 0, 1.6); place("rock", { 41.5, (double)gy, 21.5 }, 20, 1); place("crystal", { 38.5, (double)gy, 20.5 }, 0, 1); place("crystal", { 39.6, (double)gy, 21.4 }, 30, 0.6);
                RebuildSolids();
                viewerModel = "Lamp post"; viewerOpen = true; viewerFit = true; viewerClip = -1; viewerYaw = 0.6f; viewerPitch = 0.3f; sel = Sel::Model; selModel = "Lamp post"; assetKind = 3; assetCat.clear(); placing.clear(); toastT = 0;
                frames(6, "model_viewer");
                viewerOpen = false; sel = Sel::Object; selIndex = (int)objects.size() - 3;
                // the door from Blockbench (FBX): in the viewer half open, and two of them in the world
                if (ImportModelFile(ExeDir() + "assets/examples/DOOR_01.fbx")) {
                    viewerFit = true; viewerYaw = -0.5f; viewerPitch = 0.25f; viewerClip = meshModels["DOOR_01"].findClip("open"); viewerLoop = false; viewerTime = 0.12f;
                    frames(1, "fbx_door_viewer_half_open");
                    viewerOpen = false; viewerClip = -1;
                    place("DOOR_01", { 35.5, (double)gy, 19.5 }, 0, 2.5); place("DOOR_01", { 31.5, (double)gy, 24.5 }, 90, 2);
                    RebuildSolids();
                }
                poseAt(DemoShot{ "i", { 37.5, gy + 3.5, 14 }, { 37.5, gy + 1.2, 22 }, 15.0f });
                frames(4, "imported_models");
                // doors: your door stretched to 2 x 3 (blue), 1 x 1 (purple, morph ball) and 4 x 2 (green)
                if (models.find("DOOR_01") || meshModels.count("DOOR_01")) {
                    auto door = [&](double x, double w, double h, uint32_t col) { WorldObject o; o.id = nextObjId++; o.model = "DOOR_01"; o.name = UniqueObjectName("Door"); MakeDoor(o);
                        o.pos = { x, (double)gy, 40 }; o.door.w = w; o.door.h = h; o.door.color = col; o.door.stayOpen = 5; objects.push_back(o); return o.id; };
                    uint32_t a = door(22, 2, 3, 0x3AA0FF), b = door(26.5, 1, 1, 0xB45AFF), c = door(31, 4, 2, 0x3AFF7A);
                    for (int x = 19; x <= 34; x++) for (int y = gy; y < gy + 4; y++) { bool hole = (x >= 21 && x <= 22 && y < gy + 3) || (x == 26 && y == gy) || (x >= 29 && x <= 32 && y < gy + 2); if (!hole) world->set(x, y, 41, reg.find("bricks")); }
                    RebuildSolids();
                    for (int i = 0; i < (int)objects.size(); i++) if (objects[i].id == a) { sel = Sel::Object; selIndex = i; }
                    toolMode = true; toolSel = 2; placing.clear();
                    poseAt(DemoShot{ "d", { 26.5, gy + 2.6, 31 }, { 26.5, gy + 1.3, 40 }, 15.5f });
                    frames(4, "doors_editor");
                    toolMode = false; sel = Sel::World;
                    StartPlay(false);
                    poseAt(DemoShot{ "d", { 25.5, gy + 1.7, 33 }, { 25.5, gy + 1.3, 40 }, 15.5f });
                    doorRt[a].shoot(0.3f, 0.6f); doorRt[a].t = 0.02f;
                    doorRt[c].state = DoorRuntime::Open; doorRt[c].t = 0;
                    { Shot sh; sh.pos = player.eye() + V3{ 0.4, -0.2, 2.0 }; sh.dir = { 0.07, -0.03, 1 }; this->shots.push_back(sh); }
                    play.health = game.player.maxHealth; toastT = 0;
                    frames(3, "doors_play");
                    StopPlay();
                }
                // your second door (parts inside moving parts): in the viewer mid-way, then in the world with the field on its parts and as sheets in front
                if (ImportModelFile(ExeDir() + "assets/examples/DOOR_02.fbx")) {
                    viewerModel = "DOOR_02"; viewerOpen = true; viewerFit = true; viewerYaw = -0.6f; viewerPitch = 0.25f; viewerClip = meshModels["DOOR_02"].findClip("open"); viewerLoop = false;
                    viewerTime = 0.62f; frames(1, "door2_viewer_turning");
                    viewerTime = 0.9f; frames(1, "door2_viewer_sliding_apart");
                    { PlayerRules keep = game.player; game.player.height = 1.2f; game.player.width = 0.5f; game.player.eyeHeight = 1.05f; game.player.ballSize = 0.75f; viewerClip = -1;
                      frames(1, "viewer_smaller_player_and_ball"); game.player = keep; }
                    viewerOpen = false; viewerClip = -1;
                    auto door2 = [&](double x, bool parts, uint32_t col) { WorldObject o; o.id = nextObjId++; o.model = "DOOR_02"; o.name = UniqueObjectName("Door"); MakeDoor(o);
                        o.pos = { x, (double)gy, 35.5 }; o.door.w = 2; o.door.h = 3; o.door.color = col; o.door.fieldOnParts = parts; o.door.stayOpen = 6; objects.push_back(o); return o.id; };
                    uint32_t p1 = door2(23, true, 0xFF8A3A), p2 = door2(28, false, 0x3AA0FF);
                    RebuildSolids();
                    StartPlay(false);
                    poseAt(DemoShot{ "d", { 25.5, gy + 2.2, 29.5 }, { 25.5, gy + 1.4, 35.5 }, 15.5f });
                    play.health = game.player.maxHealth; toastT = 0;
                    frames(3, "door2_fields_parts_and_sheets");
                    doorRt[p1].shoot(0.5f, 0.5f); doorRt[p2].shoot(0.5f, 0.5f);
                    for (int k = 0; k < 40; k++) { FrameBody(1.0 / 60); swap->Present(0, 0); retired.clear(); }
                    frames(1, "door2_opening_in_play");
                    StopPlay();
                }
                {   // platforms: an elevator built from blocks, and three floating platforms going round a loop
                    auto groundAt = [&](int x, int z) { for (int y = 40; y > -40; y--) { BlockId v = world->get(x, y, z); if (v != AIR && !reg.get(v).liquid) return y; } return 0; };
                    int g2 = groundAt(3, -26) + 1;
                    BlockId met = reg.find("metal"), cob = reg.find("cobblestone");
                    for (int x = 2; x <= 4; x++) for (int z = -27; z <= -25; z++) world->set(x, g2, z, (x == 3 && z == -26) ? met : cob);
                    selA = { 2, g2, -27 }; selB = { 4, g2, -25 }; selStage = 2; MakeIntoObject("Lift", true);
                    WorldObject& lift = objects.back(); MakePlatform(lift); lift.platform.points = { { 0, 0, 0 }, { 0, 6, 0 } }; lift.platform.trigger = PathTrigger::StandOn; lift.platform.wait = 0.4f; lift.platform.comeBack = 6; lift.platform.speed = 3;
                    uint32_t liftId = lift.id;
                    for (int x = 10; x <= 11; x++) for (int z = -27; z <= -26; z++) world->set(x, g2 + 3, z, met);
                    selA = { 10, g2 + 3, -27 }; selB = { 11, g2 + 3, -26 }; selStage = 2; MakeIntoObject("Floater", true);
                    WorldObject& fl = objects.back(); MakePlatform(fl); fl.platform.points = { { 0, 0, 0 }, { 7, 0, 0 }, { 7, 2, 7 }, { 0, 2, 7 } }; fl.platform.mode = PathMode::Loop; fl.platform.copies = 3; fl.platform.speed = 2; fl.platform.wait = 0;
                    RebuildSolids(); selStage = 0;
                    toolMode = true; toolSel = 3; sel = Sel::Object; selIndex = (int)objects.size() - 1;
                    poseAt(DemoShot{ "p", { 7, g2 + 7.5, -40 }, { 9, g2 + 2.5, -24 }, 15.5f });
                    frames(4, "platforms_editor");
                    toolMode = false; sel = Sel::World;
                    StartPlay(false);
                    for (auto& o : objects) if (o.id == liftId) { player.feet = { o.pos.x, g2 + 1.0, o.pos.z }; }
                    { V3 e = player.feet; e.y += player.eyeH; poseAt(DemoShot{ "p", e, { e.x + 8, e.y + 1.0, e.z + 2 }, 15.5f }); player.feet.y = g2 + 1.0; }
                    play.health = game.player.maxHealth; toastT = 0;
                    frames(45, "platforms_play_riding_the_lift");
                    Log("lift ride: feet at " + std::to_string(player.feet.y - g2) + " blocks above the ground");
                    StopPlay();
                }
                {   // pickups, a checkpoint, a teleporter and a trigger zone
                    auto groundAt = [&](int x, int z) { for (int y = 40; y > -40; y--) { BlockId v = world->get(x, y, z); if (v != AIR && !reg.get(v).liquid) return y; } return 0; };
                    int g3 = groundAt(-10, -16) + 1;
                    auto add = [&](const char* model, V3 pos) { WorldObject o; o.id = nextObjId++; o.model = model; ApplyPreset(o); o.name = UniqueObjectName(PresetName(o)); o.pos = pos; objects.push_back(o); return (int)objects.size() - 1; };
                    add("@health", { -13.5, (double)g3, -15.5 }); add("@key", { -11.5, (double)g3, -15.5 }); add("@gem", { -9.5, (double)g3, -15.5 }); add("@gem", { -8.5, (double)g3, -15.5 });
                    add("@checkpoint", { -14.5, (double)g3, -19.5 });
                    int tp = add("@teleporter", { -6.5, (double)g3, -19.5 }); objects[tp].teleport.world = "Level 1";
                    int zn = add("@zone", { -10.5, (double)g3, -12.5 }); objects[zn].size = { 5, 3, 2 }; objects[zn].trigger.text = "Welcome to the test level!";
                    RebuildSolids();
                    toolMode = true; toolSel = 2; sel = Sel::Object; selIndex = zn;
                    poseAt(DemoShot{ "h", { -10, g3 + 4.5, -28 }, { -10, g3 + 0.5, -14 }, 15.5f });
                    frames(4, "pickups_editor");
                    toolMode = false; sel = Sel::World;
                    StartPlay(false);
                    poseAt(DemoShot{ "h", { -10.5, g3 + 1.62, -22 }, { -10.5, g3 + 0.6, -15 }, 15.5f });
                    inv.keys.insert("Red key"); inv.counts["Gem"] = 2; Message("Welcome to the test level!"); play.health = game.player.maxHealth * 0.7f; toastT = 0;
                    frames(3, "pickups_play");
                    StopPlay();
                }
                {   // lights and particles at dusk: a campfire (fire, smoke, embers, its own light), sparks off a machine, a steam vent, magic, a blue spotlight, a pulsing pink light
                    auto groundAt = [&](int x, int z) { for (int y = 40; y > -40; y--) { BlockId v = world->get(x, y, z); if (v != AIR && !reg.get(v).liquid) return y; } return 0; };
                    int g4 = groundAt(-11, -36) + 1;
                    BlockId brk = reg.find("bricks"), met = reg.find("metal"), cob = reg.find("cobblestone");
                    for (int x = -17; x <= -5; x++) for (int y = g4; y < g4 + 4; y++) world->set(x, y, -31, brk);   // a wall behind
                    for (int x = -11; x <= -10; x++) world->set(x, g4, -34, cob);
                    world->set(-14, g4, -33, met); world->set(-14, g4 + 1, -33, met);
                    auto fxObj = [&](const char* model, V3 pos) { WorldObject o; o.id = nextObjId++; o.model = model; ApplyPreset(o); o.name = UniqueObjectName(PresetName(o)); o.pos = pos; objects.push_back(o); return (int)objects.size() - 1; };
                    int fire = fxObj("@emitter", { -10.5, (double)g4 + 1, -33.5 });
                    int smoke = fxObj("@emitter", { -10.5, (double)g4 + 2.2, -33.5 }); objects[smoke].particles = particlePreset("Smoke");
                    int emb = fxObj("@emitter", { -10.5, (double)g4 + 1, -33.5 }); objects[emb].particles = particlePreset("Embers");
                    int spk = fxObj("@emitter", { -14.5, (double)g4 + 2, -33.5 }); objects[spk].particles = particlePreset("Sparks"); objects[spk].particles.burstEvery = 0.5f; objects[spk].particles.dir = { 0.3, 1, 0.6 };
                    int st = fxObj("@emitter", { -7.5, (double)g4, -33.5 }); objects[st].particles = particlePreset("Steam");
                    int mg = fxObj("@emitter", { -16, (double)g4 + 1, -34.5 }); objects[mg].particles = particlePreset("Magic");
                    int sp = fxObj("@light", { -6.5, (double)g4 + 3.5, -36 }); objects[sp].light.type = LightType::Spot; objects[sp].light.color = 0x60A0FF; objects[sp].light.intensity = 4; objects[sp].light.range = 12; objects[sp].light.cone = 25; objects[sp].light.tilt = 35; objects[sp].yaw = 0;
                    int pl = fxObj("@light", { -16.5, (double)g4 + 2.5, -33 }); objects[pl].light.color = 0xFF60C0; objects[pl].light.flicker = Flicker::Pulse; objects[pl].light.range = 7;
                    RebuildSolids();
                    sel = Sel::Object; selIndex = fire; toolMode = false;
                    poseAt(DemoShot{ "l", { -11, g4 + 2.6, -44 }, { -11, g4 + 1.2, -32 }, 19.8f });
                    frames(60, "lights_and_particles");
                    (void)smoke; (void)emb; (void)st; (void)mg; (void)sp; (void)pl;
                    StartPlay(false);
                    poseAt(DemoShot{ "l", { -9.5, g4 + 1.62, -40 }, { -14, g4 + 1.4, -31.2 }, 19.8f });
                    { Shot sh; sh.pos = player.eye() + player.forward() * 0.6; sh.dir = player.forward(); this->shots.push_back(sh); }
                    play.health = game.player.maxHealth; toastT = 0;
                    frames(12, "beam_impact_sparks");
                    StopPlay(); sel = Sel::World;
                    // G3 event cards: the hall lights are off at the start and come on (with a burst of magic) when you walk into the zone
                    objects[sp].name = "Hall light"; objects[pl].name = "Hall light"; objects[fire].name = "Campfire";
                    int hz = fxObj("@zone", { -10.5, (double)g4, -37.5 }); objects[hz].name = "Hall"; objects[hz].trigger.text.clear(); objects[hz].size = { 6, 3, 3 };
                    events.clear();
                    { EventCard c; c.name = "Dark at the start"; c.when = { When::GameStarts }; c.dos = { { Do::LightOff, "Hall light" }, { Do::LightOff, "Campfire" } }; events.push_back(c); }
                    { EventCard c; c.name = "The hall wakes up"; c.when = { When::EnterZone, "Hall" }; c.once = true;
                      c.dos = { { Do::Message, "", "The hall wakes up...", 3 }, { Do::LightOn, "Hall light" }, { Do::Particles, "Hall light", "Magic" }, { Do::Wait, "", "", 0.5f }, { Do::LightOn, "Campfire" } }; events.push_back(c); }
                    { EventCard c; c.name = "Collect 3 gems"; c.when = { When::CountReaches, "Gem", 3 }; c.ifs = { { If::HasKey, "Red key" } };
                      c.dos = { { Do::OpenDoor, FirstName("door") }, { Do::Message, "", "A door opened somewhere", 3 }, { Do::FlagOn, "", "gems-done" } }; events.push_back(c); }
                    events.push_back(Template(2));
                    RebuildSolids();
                    focusEvents = true; evSel = 1; sel = Sel::Object; selIndex = hz;
                    poseAt(DemoShot{ "e", { -11, g4 + 4.6, -48 }, { -11, g4 + 0.8, -33 }, 19.8f });
                    frames(6, "events_editor");
                    evShotPose = 1; frames(4, "events_panel_wide");
                    evShotPose = 2; frames(2, "events_panel_docked_again");
                    StartPlay(false);
                    poseAt(DemoShot{ "e", { -10.5, g4 + 1.62, -44 }, { -12, g4 + 1.4, -31.2 }, 19.8f });
                    play.health = game.player.maxHealth; toastT = 0;
                    frames(20, "events_play_dark");
                    player.feet = { -10.5, (double)g4, -38 };
                    { V3 e = player.feet; e.y += player.eyeH; poseAt(DemoShot{ "e", e, { -12, g4 + 1.4, -31.2 }, 19.8f }); player.feet = { -10.5, (double)g4, -38 }; }
                    frames(50, "events_play_lights_on");
                    Log("event cards: 'hall wakes up' ran " + std::to_string(evRun.firedCount(1)) + " time(s), lights off: " + std::to_string(evOff.size()));
                    StopPlay(); sel = Sel::World; focusAssets = true;
                }
                sel = Sel::World;
            }
            {   // the Player editor (HUD): the example visor with two struts (one a mirrored copy), then in Play mode, then while turning (the sway)
                hud = defaultHud(); playerEdOpen = true; focusPlayerEd = true; hudSel = 1; hudPlane = 0; sel = Sel::World;
                poseAt(demoShots()[1]);
                frames(8, "player_editor_hud");
                hudSel = 3; hudPart = 1; hudOrbYaw = 3.6f; hudOrbPitch = 0.25f; hudOrbDist = 2.6f;
                frames(3, "player_editor_mirrored_strut");
                hudSel = 2; hudPlane = 0; hudEl = 2; hudWiggle = 0.25;
                frames(3, "player_editor_element_and_wiggle");
                playerEdOpen = false;
                StartPlay(false); poseAt(demoShots()[1]);
                inv.counts["Missile"] = 25; play.health = game.player.maxHealth * 0.76f; toastT = 0;
                hudSway.reset(); prevPlayYaw = player.yaw; prevPlayPitch = player.pitch;
                frames(4, "hud_play");
                for (int mode = 0; mode < 3; mode++) {   // shadow edges: sharp, smooth, soft like sunlight
                    settings.shadowSoft = mode; const char* nm[] = { "shadows_sharp", "shadows_smooth", "shadows_soft_like_sunlight" };
                    hud.planes[0].visible = false; frames(2, nm[mode]); hud.planes[0].visible = true;
                }
                settings.shadowSoft = 2;
                for (int k = 0; k < 6; k++) { player.yaw += 0.06; FrameBody(1.0 / 60); swap->Present(0, 0); retired.clear(); }
                Log("HUD sway while turning: " + std::to_string(hudSway.yaw) + " degrees");
                frames(1, "hud_play_turning_sway");
                StopPlay(); hud = HudLayout{}; SaveHud(); hudSway.reset();
            }
            {   // weapons: the Weapons tab (the cannon and its three points), then in Play mode: the Power Beam, a missile, the Wave Beam
                hud = defaultHud(); weapons = defaultWeapons(); playerEdOpen = true; focusPlayerEd = true; selectWeaponsTab = true; weapSel = 0; weapPoint = 1;
                poseAt(demoShots()[1]);
                frames(8, "weapons_tab");
                weapPoint = 2; weapYaw = 1.2f; frames(2, "weapons_tab_emitter");
                {   // sounds: a few made-up WAVs in the project, on the player and on the Power Beam's timelines
                    std::error_code ec; fs::create_directories(fs::u8path(SoundsDir()), ec);
                    auto tone = [&](const char* name, float secs, float f0, float f1, float noise) {
                        WavData w; w.channels = 1; w.rate = 22050; int n = (int)(secs * w.rate); uint32_t r = 99; double ph = 0;
                        for (int i = 0; i < n; i++) { float t = (float)i / n, f = f0 + (f1 - f0) * t; ph += 6.2831853 * f / w.rate; r = r * 1664525u + 1013904223u;
                            float env = std::min(1.f, t * 40) * (1 - t); w.samples.push_back(env * ((1 - noise) * (float)std::sin(ph) + noise * (((r >> 9) & 0xFFFF) / 32768.f - 1)) * 0.6f); }
                        std::vector<uint8_t> b = wavBytes(w); std::ofstream f(fs::u8path(SoundsDir() + "/" + name), std::ios::binary); f.write((const char*)b.data(), (std::streamsize)b.size());
                    };
                    tone("beam zap.wav", 0.25f, 1600, 300, 0.1f); tone("charge hum.wav", 1.4f, 150, 900, 0.05f); tone("charged blast.wav", 0.7f, 500, 60, 0.5f);
                    tone("missile launch.wav", 0.9f, 220, 90, 0.7f); tone("missile click.wav", 0.12f, 900, 700, 0.3f); tone("step.wav", 0.09f, 120, 80, 0.8f); tone("jump.wav", 0.2f, 300, 600, 0.3f); tone("land.wav", 0.3f, 90, 40, 0.8f); tone("ball roll.wav", 1.0f, 70, 70, 0.4f); tone("ball bounce.wav", 0.15f, 400, 200, 0.5f);
                    soundCache.clear();
                    Beam& pb = weapons.beams[0];
                    { SoundClip c; c.sound.file = "beam zap.wav"; pb.timelines[Beam::Fire].sounds = { c }; }
                    { SoundClip c; c.sound.file = "charge hum.wav"; c.sound.pitchVar = 0; pb.timelines[Beam::Charging].sounds = { c }; }
                    { SoundClip c; c.sound.file = "charged blast.wav"; pb.timelines[Beam::Charged].sounds = { c }; }
                    { Timeline& t = pb.timelines[Beam::MissileShot]; t.shotAt = 0.18f; SoundClip a; a.sound.file = "missile click.wav"; a.at = 0; SoundClip b2; b2.sound.file = "missile launch.wav"; b2.at = 0.18f; b2.sound.trimStart = 0.05f; b2.sound.length = 0.6f; t.sounds = { a, b2 }; }
                    { SoundUse a; a.file = "step.wav"; a.pitchVar = 0.12f; SoundUse b3 = a; b3.pitch = 0.9f; playerSounds.step.sounds = { a, b3 }; SoundUse j; j.file = "jump.wav"; playerSounds.jump.sounds = { j }; SoundUse l; l.file = "land.wav"; playerSounds.land.sounds = { l };
                      SoundUse r; r.file = "ball roll.wav"; r.pitchVar = 0; playerSounds.roll.sounds = { r }; SoundUse bb; bb.file = "ball bounce.wav"; playerSounds.ballLand.sounds = { bb }; }
                    weapTimelineView = true; tlKind = Beam::MissileShot; tlSel = 1; weapPoint = 1; weapYaw = 2.3f;
                    frames(3, "weapons_timeline");
                    weapTimelineView = false; selectSoundsTab = true;
                    frames(3, "player_sounds_tab");
                    scrollToRoll = true; frames(3, "player_sounds_morph_ball");
                    selectWeaponsTab = true; frames(1, "back_to_weapons");
                    weapMis = 1; frames(3, "weapons_missile_kind"); weapMis = -1;
                }
                playerEdOpen = false;
                StartPlay(false); poseAt(demoShots()[1]); hudSway.reset(); prevPlayYaw = player.yaw; prevPlayPitch = player.pitch; play.health = game.player.maxHealth; toastT = 0;
                frames(3, "weapons_play_cannon");
                auto run = [&](int n) { for (int k = 0; k < n; k++) { FrameBody(1.0 / 60); swap->Present(0, 0); retired.clear(); } };
                LaunchShot(weapons.beams[0], 0, nullptr); PlayCannonClip(weapons.beams[0].fireClip); firedPending += 1; run(3); LaunchShot(weapons.beams[0], 0, nullptr); firedPending += 1; run(4);
                frames(1, "weapons_power_beam");
                run(40); inv.counts["Missile"]--; LaunchShot(weapons.beams[0], 0, &weapons.missileTypes[0]); PlayCannonClip(weapons.beams[0].missileClip); firedPending += 2.5f; run(5);
                frames(1, "weapons_missile");
                run(40); inv.keys.insert("Super Missile"); inv.counts["Missile"] = 20; weapState.missile = 1; weapState.cooldown = 0; weapState.animLeft = 0;
                { int before = inv.counts["Missile"]; missilePrev = false; forceMissile = true; run(1); forceMissile = false; run(1);
                  Log("super missile: " + std::to_string(before - inv.counts["Missile"]) + " missiles used, kind " + weapons.missileTypes[weapState.missile].name); }
                run(7); frames(1, "weapons_super_missile");
                run(30); frames(1, "weapons_super_missile_hit");
                weapState.missile = 0;
                run(40); weapState.current = 1; LaunchShot(weapons.beams[1], 1, nullptr); firedPending += 1; run(6); LaunchShot(weapons.beams[1], 1, nullptr); firedPending += 1; run(3);
                frames(1, "weapons_wave_beam");
                run(40); weapState.current = 0; holdBeam = true; run(80);
                frames(1, "weapons_charging");
                Log("charge: " + std::to_string(weapState.charge) + (chargedOn ? ", full: the charging sound stopped, 'done' and the charged loop started" : ", NOT full yet"));
                holdBeam = false; run(5);
                frames(1, "weapons_charged_shot");
                run(40); weapons.aimMode = 1; freeAim.x = 0.55f; freeAim.y = 0.3f; run(20);   // free aim: the cursor up and to the right, the cannon turns to it
                LaunchShot(weapons.beams[0], 0, nullptr); firedPending += 1; run(5);
                frames(1, "weapons_free_aim");
                Log("free aim: cannon turned " + std::to_string(aimYaw) + " / " + std::to_string(aimPitch) + " degrees; sounds " + (audioOk ? "on" : "off (no device)"));
                weapons.aimMode = 0;
                {   // close-ups in the open air: shots held still in front of the camera (movement is off here, so nothing falls)
                    V3 keepF = player.feet; double ky = player.yaw, kp = player.pitch;
                    this->shots.clear(); spheres.clear(); player.feet.y += 7; player.yaw += 1.1; player.pitch = -0.12; hudSway.reset(); run(2);
                    V3 e = player.eye(), f = player.forward(), r = Norm(cross({ 0, 1, 0 }, f)), u = cross(f, r);
                    auto still = [&](int bi, int mi, bool charged, V3 at, V3 dir) {
                        Shot sh; sh.pos = at; sh.dir = Norm(dir); sh.speed = 0; sh.beam = bi; sh.charged = charged;
                        if (mi >= 0) { const MissileType& m = weapons.missileTypes[mi]; sh.mtype = mi; sh.missile = true; sh.size = m.size; sh.tint = m.color; }
                        else { const Beam& b = weapons.beams[bi]; sh.orb = true; sh.size = b.size * (charged ? b.chargeSize : 1.f); sh.tint = b.color; }
                        sh.color = Linear(sh.tint) * (charged ? 6.0 : 3.0); sh.trail.s.rate = 0; sh.trail.s.burst = 0; this->shots.push_back(sh);
                    };
                    still(0, -1, false, e + f * 1.3 - r * 0.32 - u * 0.05, f); still(1, -1, false, e + f * 1.9 + r * 0.05 + u * 0.1, f); still(0, -1, true, e + f * 2.6 + r * 0.75 + u * 0.05, f);
                    run(1); frames(2, "beam_orbs_closeup");
                    this->shots.clear();
                    still(0, 0, false, e + f * 1.25 - r * 0.35 + u * 0.08, r * 1.0 + f * 0.35); still(0, 1, false, e + f * 1.9 + r * 0.25 - u * 0.05, r * 1.0 - f * 0.25);
                    run(1); frames(2, "missile_models_closeup");
                    this->shots.clear();
                    AddSphere(e + f * 7 - r * 1.5, 4.5f, 0xFF8A3A, 1.4f); AddSphere(e + f * 9 + r * 3, 3.5f, 0x60FF70, 1.4f); run(14);
                    frames(1, "impact_sphere_plasma");
                    spheres.clear(); player.feet = keepF; player.yaw = ky; player.pitch = kp; hudSway.reset(); run(2);
                }
                weapons.switchMode = 2; paused = true; Capture(false); frames(2, "pause_menu_pick_beam"); paused = false; weapons.switchMode = 0;
                weapons.switchMode = 1; inv.keys.insert("Wave Beam"); run(2); Log(std::string("no switching: using the ") + weapons.beams[weapState.current].name + " (the best one owned)"); weapons.switchMode = 0;
                Log("weapons: " + std::to_string(shots.size()) + " shots flying, missiles left " + std::to_string(inv.counts["Missile"]));
                StopPlay(); hud = HudLayout{}; weapons = WeaponSet{}; SaveHud();
            }
            {   // breakable blocks, the morph ball and bombs: mark a little wall in the editor, then roll up to it and bomb it
                poseAt(demoShots()[1]);
                V3 e = player.eye(), fw = player.forward(); fw.y = 0; fw = Norm(fw); V3 side = Norm(cross({ 0, 1, 0 }, fw));
                int sx = (int)std::lround(side.x), sz = (int)std::lround(side.z);
                int gx = (int)std::floor(e.x + fw.x * 4.5), gz = (int)std::floor(e.z + fw.z * 4.5), gyy = (int)std::floor(e.y);
                while (gyy > e.y - 12 && world->get(gx, gyy - 1, gz) == AIR) gyy--;
                BlockId stone = reg.find("stone");
                for (int i = -1; i <= 1; i++) for (int j = 0; j < 2; j++) { world->set(gx + sx * i, gyy + j, gz + sz * i, stone); breakables.mark(gx + sx * i, gyy + j, gz + sz * i, i == 0 && j == 0 ? "Bomb" : j == 1 ? "" : "Missile"); }
                world->set(gx + sx * 2, gyy, gz + sz * 2, stone);   // one more that is not marked
                toolMode = true; toolSel = 4; sel = Sel::Tool; breakTool = "Bomb";
                frames(6, "breakable_tool");
                toolMode = false; sel = Sel::World;
                StartPlay(false); poseAt(demoShots()[1]); play.health = game.player.maxHealth; toastT = 0;
                player.feet = { gx + 0.5 - fw.x * 1.0, (double)gyy, gz + 0.5 - fw.z * 1.0 }; player.vel = { 0, 0, 0 }; player.pitch = -0.25;
                auto run = [&](int n) { for (int k = 0; k < n; k++) { FrameBody(1.0 / 60); swap->Present(0, 0); retired.clear(); } };
                OnKey('C', false); run(30);   // the real key, the way a player presses it
                Log(std::string("C key in play mode: ") + (player.ball ? "rolled up" : "NOT rolled up"));
                { V3 keep = player.feet; int before = rollTests; std::string why;   // a little roll backwards (away from the walls), to check the rolling loop wants to play
                  bool cap = captured; captured = true; run(10); before = rollTests;   // movement only runs with the mouse captured (as when really playing); settle on the ground first
                  player.vel.x = -fw.x * 5.0; player.vel.z = -fw.z * 5.0;
                  for (int k = 0; k < 4; k++) { run(1);
                      char b[96]; snprintf(b, sizeof b, " [speed %.2f ground %d ball %d paused %d]", std::sqrt(player.vel.x * player.vel.x + player.vel.z * player.vel.z), (int)player.onGround, (int)player.ball, (int)paused); why += b; }
                  Log("roll loop wanted on " + std::to_string(rollTests - before) + " of 4 rolling frame(s)" + why); captured = cap; player.feet = keep; player.vel = { 0, 0, 0 }; run(10); }
                frames(1, "morph_ball");
                holdBeam = true; run(2); holdBeam = false; run(25);
                frames(1, "bomb_laid");
                run(30);
                frames(1, "bomb_went_off");
                run(6); frames(1, "bomb_impact_sphere_growing");
                run(30); ToggleBall(); run(10); AddSphere(player.eye() + fw * 4.5 + V3{ 0, -0.3, 0 }, 3.5f, 0xFF8A3A, 0.55f); run(9);
                frames(1, "impact_sphere_missile_size");
                ToggleBall();
                Log("bomb: " + std::to_string(broken.size()) + " block(s) broken, ball " + (player.ball ? "rolled up" : "standing"));
                ToggleBall(); StopPlay();
                for (int i = -1; i <= 2; i++) for (int j = 0; j < 2; j++) { world->set(gx + sx * i, gyy + j, gz + sz * i, AIR); breakables.unmark(gx + sx * i, gyy + j, gz + sz * i); }
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
        if (audioOk) UpdateAudio();
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
            ModelViewer();
            PlayerEditor();
        } else {
            DrawHud(ImGui::GetForegroundDrawList());
            if (showF3) DrawF3();
            if (paused && !showSettings) PauseMenu();
        }
        if (showSettings) SettingsWindow();
        Popups();
        BuildHudLists();
        ImGui::Render();
        RenderHudPlanes();

        const float bg[4] = { 0.10f, 0.11f, 0.13f, 1 };
        if (screen == Screen::Game && rw > 0) RenderWorld(ev ? sceneRTV.Get() : backRTV.Get());
        if (ev) { RenderViewer(); RenderHudViews(); RenderWeaponView(); }
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
