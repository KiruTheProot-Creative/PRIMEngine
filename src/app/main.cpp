// main.cpp : PRIMEngine, Windows + Direct3D 11.
// Title screen (worlds) -> creative mode in an unlimited block world. The world, mesher and player live in
// src/world and src/game (no Windows code there); this file is the window, the renderer and the menus.
#include "../world/mesher.h"
#include "../game/game.h"
#include "../world/library.h"
#include "../world/shapes.h"
#include "../game/demo.h"
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
    float env[4], envPart[4], envPartColor[4];
};
struct alignas(16) ChunkCB { float offset[4]; };

struct GpuChunk { ComPtr<ID3D11Buffer> vbO, ibO, vbC, ibC; UINT nO = 0, nC = 0; };

struct App;
static App* g_app = nullptr;

struct App {
    // ---- window + device
    HWND hwnd = nullptr; int cw = 1280, ch = 720; bool resized = false;
    ComPtr<ID3D11Device> dev; ComPtr<ID3D11DeviceContext> ctx; ComPtr<IDXGISwapChain> swap; ComPtr<ID3D11RenderTargetView> backRTV;
    ComPtr<ID3D11Texture2D> depthTex; ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11VertexShader> vsBlock, vsSky, vsShadow; ComPtr<ID3D11PixelShader> psOpaque, psCutout, psSky, psGhost, psShadowCut, psPost, psVolume, psBloomDown, psBloomUp, psRain, psBolt;
    ComPtr<ID3D11VertexShader> vsSkyMap, vsRain, vsDust;
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
    ComPtr<ID3D11ShaderResourceView> depthSRV, noiseSRV; ComPtr<ID3D11SamplerState> sampLinear, sampClamp; double cloudTime = 0;
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
    bool createOpen = false; std::string newName = "New World"; int newType = 0; std::string deleteAsk;
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
        MakeTargets();

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
        library.dir = ExeDir() + "game"; library.load(reg);   // game\blocks.txt + game\textures\*.png (made the first time)
        UploadTextures();
        DragAcceptFiles(hwnd, TRUE);

        IMGUI_CHECKVERSION(); ImGui::CreateContext();
        static std::string ini = ExeDir() + "imgui.ini"; ImGui::GetIO().IniFilename = ini.c_str();
        Style();
        ImGui_ImplWin32_Init(hwnd); ImGui_ImplDX11_Init(dev.Get(), ctx.Get());

        settingsPath = ExeDir() + "settings.txt"; settings.load(settingsPath);
        RAWINPUTDEVICE rid{ 0x01, 0x02, 0, hwnd }; RegisterRawInputDevices(&rid, 1, sizeof rid);   // raw mouse movement for looking around
        RefreshWorlds();
    }
    void MakeTargets() {
        ComPtr<ID3D11Texture2D> bb; HR(swap->GetBuffer(0, IID_PPV_ARGS(&bb))); HR(dev->CreateRenderTargetView(bb.Get(), nullptr, &backRTV));
        D3D11_TEXTURE2D_DESC d{}; d.Width = (UINT)cw; d.Height = (UINT)ch; d.MipLevels = 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_R32_TYPELESS; d.SampleDesc.Count = 1; d.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        depthTex.Reset(); dsv.Reset(); depthSRV.Reset(); HR(dev->CreateTexture2D(&d, nullptr, &depthTex));
        { D3D11_DEPTH_STENCIL_VIEW_DESC dv{}; dv.Format = DXGI_FORMAT_D32_FLOAT; dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D; HR(dev->CreateDepthStencilView(depthTex.Get(), &dv, &dsv));
          D3D11_SHADER_RESOURCE_VIEW_DESC sv{}; sv.Format = DXGI_FORMAT_R32_FLOAT; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1; HR(dev->CreateShaderResourceView(depthTex.Get(), &sv, &depthSRV)); }
        d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        hdrTex.Reset(); hdrRTV.Reset(); hdrSRV.Reset();
        HR(dev->CreateTexture2D(&d, nullptr, &hdrTex)); HR(dev->CreateRenderTargetView(hdrTex.Get(), nullptr, &hdrRTV)); HR(dev->CreateShaderResourceView(hdrTex.Get(), nullptr, &hdrSRV));
        volTex.Reset(); volRTV.Reset(); volSRV.Reset();
        HR(dev->CreateTexture2D(&d, nullptr, &volTex)); HR(dev->CreateRenderTargetView(volTex.Get(), nullptr, &volRTV)); HR(dev->CreateShaderResourceView(volTex.Get(), nullptr, &volSRV));
        bloomLv.clear();   // half size, quarter size, ... six levels
        for (int i = 0, w = cw / 2, h = ch / 2; i < 6; i++, w = std::max(1, w / 2), h = std::max(1, h / 2)) {
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
        MakeTargets();
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
    std::string WorldsDir() { std::string d = ExeDir() + "worlds"; std::error_code ec; fs::create_directories(d, ec); return d; }
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
        worldDir = WorldsDir() + "/" + folder;
        info = WorldInfo{}; info.load(worldDir);
        world = std::make_unique<World>(reg); info.applyGenerator(world->gen, reg);
        world->store = std::make_unique<RegionStore>(worldDir + "/region");
        workers = std::make_unique<MeshWorkers>(reg); workers->start(Threads());
        player = info.player; history.clear(); gpu.clear(); latestSeq.clear();
        for (int i = 0; i < 9; i++) {   // "stone" or "stone@stairs"
            std::string e = info.hotbar[i], shape; size_t at = e.rfind('@'); if (at != std::string::npos) { shape = e.substr(at + 1); e = e.substr(0, at); }
            BlockId t = reg.find(e); hotbar[i] = t ? makeBlock(t, Shapes::get().find(shape)) : AIR;
        }
        hotSel = info.hotSel;
        envState.update(0, player.eye(), info.environment, info.zones, true);
        { int wi = findWeather(info.weather); weather = Weather{}; weather.set(wi < 0 ? 1 : wi, true); weather.autoChange = info.autoWeather;
          weather.wetness = weather.now.rain > 0.1f ? 1.f : 0.f; weather.snowCover = weather.now.snow > 0.1f ? 1.f : 0.f; weather.seed((uint32_t)std::time(nullptr)); }
        screen = Screen::Game; paused = false; inventory = false; autosave = 0;
        Capture(true);
        Toast("Welcome to " + info.name + ". Double-tap Space to fly, E for blocks, Esc for the menu");
    }
    void SaveWorld() {
        if (!world) return;
        info.player = player; info.hotSel = hotSel; info.weather = weather.type().key; info.autoWeather = weather.autoChange; for (int i = 0; i < 9; i++) info.hotbar[i] = !reg.has(hotbar[i]) ? "" : reg.get(hotbar[i]).name + (shapeOf(hotbar[i]) ? "@" + Shapes::get().info(shapeOf(hotbar[i])).key : "");
        info.save(worldDir); world->saveAll();
    }
    void CloseWorld() {
        SaveWorld(); workers.reset(); world.reset(); gpu.clear(); latestSeq.clear();
        screen = Screen::Title; Capture(false); RefreshWorlds();
    }

    // ================================================================== input
    void Capture(bool on) {
        if (captured == on) return;
        captured = on; ShowCursor(on ? FALSE : TRUE);
        ImGuiIO& io = ImGui::GetIO();
        if (on) {
            io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
            RECT r; GetClientRect(hwnd, &r); POINT a{ 0, 0 }; ClientToScreen(hwnd, &a); RECT cr{ a.x, a.y, a.x + r.right, a.y + r.bottom }; ClipCursor(&cr);
            POINT c{ r.right / 2, r.bottom / 2 }; ClientToScreen(hwnd, &c); SetCursorPos(c.x, c.y);
            lPrev = rPrev = mPrev = true;   // the click that captured the mouse must not also break a block
        } else { io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse; ClipCursor(nullptr); }
        mouseDX = mouseDY = 0;
    }
    bool Playing() const { return screen == Screen::Game && captured && !paused && !inventory && !shapeMenu; }
    static bool Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
    void OnKey(WPARAM k, bool repeat) {
        if (screen != Screen::Game) {
            if (k == VK_ESCAPE) { if (editorOpen) editorOpen = false; else if (showSettings) showSettings = false; else libraryOpen = false; }
            return;
        }
        if (k == VK_ESCAPE) {
            if (shapeMenu) { shapeMenu = false; Capture(true); }
            else if (editorOpen) editorOpen = false;
            else if (inventory) { inventory = false; Capture(true); }
            else if (showSettings) showSettings = false;
            else { paused = !paused; Capture(!paused); }
            return;
        }
        if (k == 'E' && !paused && !ImGui::GetIO().WantTextInput) { inventory = !inventory; editorOpen = false; shapeMenu = false; Capture(!inventory); return; }
        if (k == 'Q' && !paused && !inventory && !ImGui::GetIO().WantTextInput) { shapeMenu = !shapeMenu; shapeSearch.clear(); Capture(!shapeMenu); return; }
        if (k == VK_F3) { showF3 = !showF3; return; }
        if (k == VK_F1) { hideHud = !hideHud; return; }
        if (!Playing() || repeat) return;
        if (k >= '1' && k <= '9') hotSel = (int)(k - '1');
        if (k == VK_SPACE) {
            double now = ImGui::GetTime();
            if (now - lastSpace < 0.3) { player.toggleFly(); Toast(player.flying ? "Flying" : "Walking"); lastSpace = -10; } else lastSpace = now;
        }
        if (Down(VK_CONTROL) && k == 'Z') { if (history.undo(*world)) Toast("Undo"); }
        if (Down(VK_CONTROL) && k == 'Y') { if (history.redo(*world)) Toast("Redo"); }
        if (Down(VK_CONTROL) && k == 'S') { SaveWorld(); Toast("Saved"); }
        if (k == 'R' || k == 'T') {   // turn the next block by hand (starts from the automatic rotation)
            if (!manualRot) { manualRot = true; manRot = targetValid ? rotOf(targetValue) : 0; }
            int look = LookFace();
            manRot = Shapes::turn(manRot, k == 'R' ? 1 : (look == F_PZ || look == F_NZ) ? 0 : 2);
        }
        if (k == 'G' && manualRot) { manualRot = false; Toast("Rotation: automatic"); }
    }
    void OnWheel(int delta) { if (Playing()) hotSel = (hotSel + (delta > 0 ? 8 : 1)) % 9; }

    // ================================================================== game update
    void UpdateGame(double dt) {
        if (Playing()) {
            double s = 0.0022 * settings.mouseSensitivity; player.look(mouseDX * s, mouseDY * s);
            Controls c; bool fg = GetForegroundWindow() == hwnd;
            if (fg) { c.forward = Down('W'); c.back = Down('S'); c.left = Down('A'); c.right = Down('D'); c.jump = Down(VK_SPACE); c.sneak = Down(VK_SHIFT); c.sprint = Down(VK_CONTROL) && !Down('Z') && !Down('Y'); }
            player.update(*world, c, std::min(dt, 0.05));
            Interact(dt);
        } else { lPrev = rPrev = mPrev = true; }
        mouseDX = mouseDY = 0;
        weather.update(dt, player.feet);
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
        if (!world->loaded(x, y, z) || world->get(x, y, z) != AIR) return;
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
            if (m.opaque.empty() && m.cutout.empty()) { gpu.erase(m.pos); continue; }
            GpuChunk& g = gpu[m.pos]; Upload(m.opaque, g.vbO, g.ibO, g.nO); Upload(m.cutout, g.vbC, g.ibC, g.nC);
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
    void RenderWorld() {
        aspect = (float)cw / ch; camPos = player.eye();
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
        fc.envPart[0] = E.particleKind; fc.envPart[1] = E.particleAmount; for (int i = 0; i < 3; i++) fc.envPartColor[i] = E.particleColor[i];
        fc.volume[2] = (float)std::fmod(cloudTime, 4800.0); fc.volume[3] = settings.cloudShadows ? 1.f : 0.f;
        fc.depth[0] = 0.05f; fc.depth[1] = farZ; fc.depth[2] = (float)cw; fc.depth[3] = (float)ch;
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
                const GpuChunk& g = kv.second; UINT n = pass == 0 ? g.nO : g.nC; if (!n) continue;
                // chunk origin relative to the camera, computed in double: no jitter however far you go
                float ox = (float)(kv.first.x * (double)CS - camPos.x), oy = (float)(kv.first.y * (double)CS - camPos.y), oz = (float)(kv.first.z * (double)CS - camPos.z);
                if (!BoxVisible(m, ox, oy, oz, (float)CS)) continue;
                ChunkCB cc{ { ox, oy, oz, 0 } }; ctx->UpdateSubresource(cbChunk.Get(), 0, nullptr, &cc, 0, 0);
                UINT stride = sizeof(Vtx), off = 0;
                ctx->IASetVertexBuffers(0, 1, pass == 0 ? g.vbO.GetAddressOf() : g.vbC.GetAddressOf(), &stride, &off);
                ctx->IASetIndexBuffer(pass == 0 ? g.ibO.Get() : g.ibC.Get(), DXGI_FORMAT_R32_UINT, 0);
                ctx->DrawIndexed(n, 0, 0); if (counting) drawnChunks += pass == 0;
            }
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

        // ---- the world, into the HDR picture
        const float black[4] = { 0, 0, 0, 1 };
        ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), dsv.Get()); ctx->ClearRenderTargetView(hdrRTV.Get(), black); ctx->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
        D3D11_VIEWPORT v{ 0, 0, (float)cw, (float)ch, 0, 1 }; ctx->RSSetViewports(1, &v);
        ctx->RSSetState(rsNoCull.Get());
        ctx->OMSetDepthStencilState(dsOff.Get(), 0); ctx->IASetInputLayout(nullptr);
        ctx->PSSetShaderResources(3, 1, noiseSRV.GetAddressOf()); ctx->PSSetSamplers(2, 1, sampLinear.GetAddressOf());   // the planet uses the cloud noise
        ctx->VSSetShader(vsSky.Get(), nullptr, 0); ctx->PSSetShader(psSky.Get(), nullptr, 0); ctx->Draw(3, 0);
        ctx->RSSetState(rsCull.Get());
        ctx->OMSetDepthStencilState(dsOn.Get(), 0); ctx->IASetInputLayout(layout.Get()); ctx->VSSetShader(vsBlock.Get(), nullptr, 0);
        ID3D11ShaderResourceView* srvs[5] = { texArray.Get(), shadows ? shadowSRV.Get() : nullptr, nullptr, noiseSRV.Get(), skyMap ? skyMapSRV.Get() : nullptr }; ctx->PSSetShaderResources(0, 5, srvs);
        ID3D11SamplerState* samps[3] = { sampBlocks.Get(), sampShadow.Get(), sampLinear.Get() }; ctx->PSSetSamplers(0, 3, samps);
        drawnChunks = 0;
        ctx->PSSetShader(psOpaque.Get(), nullptr, 0); drawChunks(vp, 0, true);
        ctx->PSSetShader(psCutout.Get(), nullptr, 0); drawChunks(vp, 1, false);
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
        { ID3D11ShaderResourceView* v4[4] = { hdrSRV.Get(), shadows ? shadowSRV.Get() : nullptr, depthSRV.Get(), noiseSRV.Get() }; ctx->PSSetShaderResources(0, 4, v4); }
        ctx->Draw(3, 0);

        // ---- bloom: shrink the picture step by step, then add the blurry copies back up
        ctx->PSSetShaderResources(0, 4, none);
        bool bloom = settings.bloom > 0 && !bloomLv.empty();
        ctx->PSSetSamplers(3, 1, sampClamp.GetAddressOf());
        if (bloom) {
            ctx->PSSetShader(psBloomDown.Get(), nullptr, 0);
            for (size_t i = 0; i < bloomLv.size(); i++) {
                ID3D11ShaderResourceView* src = i == 0 ? volSRV.Get() : bloomLv[i - 1].srv.Get();
                float sw = i == 0 ? (float)cw : (float)bloomLv[i - 1].w, sh2 = i == 0 ? (float)ch : (float)bloomLv[i - 1].h;
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
            D3D11_VIEWPORT fv{ 0, 0, (float)cw, (float)ch, 0, 1 }; ctx->RSSetViewports(1, &fv);
        }

        // ---- final picture: exposure + filmic curve into the window
        ctx->OMSetRenderTargets(1, backRTV.GetAddressOf(), nullptr);
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
        out = ImVec2((XMVectorGetX(c) / w * 0.5f + 0.5f) * cw, (0.5f - XMVectorGetY(c) / w * 0.5f) * ch); return true;
    }
    void Line3D(ImDrawList* dl, V3 a, V3 b, ImU32 col) {
        // clip against the near plane in camera space, then project
        V3 ra = a - camPos, rb = b - camPos;
        XMVECTOR va = XMVector3TransformCoord(XMVectorSet((float)ra.x, (float)ra.y, (float)ra.z, 1), view), vb = XMVector3TransformCoord(XMVectorSet((float)rb.x, (float)rb.y, (float)rb.z, 1), view);
        float za = XMVectorGetZ(va), zb = XMVectorGetZ(vb), n = 0.06f; if (za < n && zb < n) return;
        if (za < n) va = XMVectorLerp(va, vb, (n - za) / (zb - za)); else if (zb < n) vb = XMVectorLerp(vb, va, (n - zb) / (za - zb));
        auto scr = [&](XMVECTOR v) { XMVECTOR c = XMVector4Transform(XMVectorSetW(v, 1), proj); float w = XMVectorGetW(c); return ImVec2((XMVectorGetX(c) / w * 0.5f + 0.5f) * cw, (0.5f - XMVectorGetY(c) / w * 0.5f) * ch); };
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
    void DrawHud() {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        if (envWin) {   // zone boxes, coloured by environment
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
        if (hideHud) return;
        ImVec2 m(cw * 0.5f, ch * 0.5f);   // crosshair
        dl->AddRectFilled(ImVec2(m.x - 9, m.y - 1), ImVec2(m.x + 9, m.y + 1), IM_COL32(240, 240, 240, 220));
        dl->AddRectFilled(ImVec2(m.x - 1, m.y - 9), ImVec2(m.x + 1, m.y + 9), IM_COL32(240, 240, 240, 220));
        float s = 44 * settings.uiScale, W = 9 * s; ImVec2 o((cw - W) * 0.5f, ch - s - 10);   // hotbar
        dl->AddRectFilled(ImVec2(o.x - 3, o.y - 3), ImVec2(o.x + W + 3, o.y + s + 3), IM_COL32(0, 0, 0, 140), 3);
        for (int i = 0; i < 9; i++) {
            ImVec2 a(o.x + i * s, o.y), b(a.x + s, a.y + s);
            dl->AddRect(a, b, IM_COL32(120, 120, 120, 200), 0.f, 0, 2.f);
            BlockIcon(dl, hotbar[i], ImVec2(a.x + s * 0.5f, a.y + s * 0.5f), s * 0.62f);
        }
        dl->AddRect(ImVec2(o.x + hotSel * s - 2, o.y - 2), ImVec2(o.x + hotSel * s + s + 2, o.y + s + 2), IM_COL32(255, 255, 255, 255), 0.f, 0, 3.f);
        if (reg.has(hotbar[hotSel])) {
            BlockId hv = hotbar[hotSel]; std::string label = reg.get(hv).name;
            if (shapeOf(hv) && shapeOf(hv) != Shapes::FULL) label += " - " + Shapes::get().info(shapeOf(hv)).name;
            if (manualRot) label += "   [turned by hand: R turn, T tip, G automatic]";
            ImVec2 ts = ImGui::CalcTextSize(label.c_str()); dl->AddText(ImVec2((cw - ts.x) * 0.5f, o.y - ts.y - 8), IM_COL32(255, 255, 255, 230), label.c_str());
        }
        if (toastT > 0) { ImVec2 ts = ImGui::CalcTextSize(toast.c_str()); dl->AddRectFilled(ImVec2((cw - ts.x) * 0.5f - 10, 60), ImVec2((cw + ts.x) * 0.5f + 10, 66 + ts.y + 6), IM_COL32(0, 0, 0, 150), 4); dl->AddText(ImVec2((cw - ts.x) * 0.5f, 66), IM_COL32(255, 255, 255, 240), toast.c_str()); }
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
        if (ch_) settings.save(settingsPath);
        if (ImGui::Button("Done", ImVec2(-1, 0))) showSettings = false;
        ImGui::End();
    }
    void TitleScreen() {
        Center(ImVec2(520 * settings.uiScale, 460 * settings.uiScale));
        ImGui::Begin("PRIMEngine", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        ImGui::TextDisabled("Worlds");
        ImGui::BeginChild("worlds", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 3 - 8), ImGuiChildFlags_Borders);
        if (worldList.empty()) ImGui::TextDisabled("No worlds yet. Create one below.");
        static int selected = -1;
        for (int i = 0; i < (int)worldList.size(); i++) {
            if (ImGui::Selectable(worldList[i].c_str(), selected == i, ImGuiSelectableFlags_AllowDoubleClick)) { selected = i; if (ImGui::IsMouseDoubleClicked(0)) OpenWorld(worldList[i]); }
        }
        ImGui::EndChild();
        float w3 = (ImGui::GetContentRegionAvail().x - 16) / 3;
        ImGui::BeginDisabled(selected < 0 || selected >= (int)worldList.size());
        if (ImGui::Button("Play", ImVec2(w3, 0))) OpenWorld(worldList[selected]);
        ImGui::SameLine(); if (ImGui::Button("Delete", ImVec2(w3, 0))) { deleteAsk = worldList[selected]; ImGui::OpenPopup("Delete world?"); }
        ImGui::EndDisabled();
        ImGui::SameLine(); if (ImGui::Button("Create new world", ImVec2(w3, 0))) { createOpen = true; ImGui::OpenPopup("Create world"); }
        if (ImGui::Button("Blocks & textures", ImVec2(w3, 0))) libraryOpen = true;
        ImGui::SameLine(); if (ImGui::Button("Settings", ImVec2(w3, 0))) showSettings = true;
        ImGui::SameLine(); if (ImGui::Button("Quit", ImVec2(w3, 0))) PostQuitMessage(0);
        if (ImGui::Button("Open worlds folder", ImVec2(w3, 0))) ShellExecuteA(nullptr, "open", WorldsDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);

        if (ImGui::BeginPopupModal("Create world", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::SetNextItemWidth(300); ImGui::InputText("Name", &newName);
            const char* types[] = { "Flat: grass, dirt, stone", "Flat: stone", "Flat: sand", "Void: empty (one block to start)" };
            ImGui::SetNextItemWidth(300); ImGui::Combo("World type", &newType, types, 4);
            ImGui::TextDisabled("The world has no size limit. It loads around you as you move.");
            if (ImGui::Button("Create", ImVec2(140, 0))) { ImGui::CloseCurrentPopup(); CreateWorld(newName.empty() ? "World" : newName, newType); }
            ImGui::SameLine(); if (ImGui::Button("Cancel", ImVec2(140, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Delete world?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Delete \"%s\" for good?", deleteAsk.c_str());
            if (ImGui::Button("Delete", ImVec2(140, 0))) { std::error_code ec; fs::remove_all(WorldsDir() + "/" + deleteAsk, ec); RefreshWorlds(); selected = -1; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine(); if (ImGui::Button("Cancel", ImVec2(140, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::End();
    }
    void PauseMenu() {
        Center(ImVec2(360 * settings.uiScale, 0));
        ImGui::Begin("Game menu", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
        if (ImGui::Button("Back to building", ImVec2(-1, 0))) { paused = false; Capture(true); }
        if (ImGui::Button("Settings", ImVec2(-1, 0))) showSettings = true;
        if (ImGui::Button("Weather", ImVec2(-1, 0))) weatherWin = true;
        if (ImGui::Button("Environment and zones", ImVec2(-1, 0))) envWin = true;
        if (ImGui::Button("Save", ImVec2(-1, 0))) { SaveWorld(); Toast("Saved"); }
        if (ImGui::Button("Save and quit to title", ImVec2(-1, 0))) CloseWorld();
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

    // ---- environment window (from the game menu): the world's air and zones with their own
    void EnvWindow() {
        float ui = settings.uiScale; const auto& types = envTypes();
        ImGui::SetNextWindowPos(ImVec2(cw * 0.5f, ch * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(480 * ui, 200), ImVec2(640 * ui, ch * 0.9f));
        ImGui::Begin("Environment", &envWin, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
        auto envCombo = [&](const char* label, std::string& key) {
            int cur = std::max(0, findEnv(key));
            if (ImGui::BeginCombo(label, types[cur].name.c_str())) {
                for (int i = 0; i < (int)types.size(); i++) if (ImGui::Selectable(types[i].name.c_str(), i == cur)) key = types[i].key;
                ImGui::EndCombo();
            }
        };
        ImGui::SetNextItemWidth(220 * ui); envCombo("The air of this world", info.environment);
        ImGui::SeparatorText("Zones: places with their own air (a space station, a volcano ...)");
        ImGui::TextDisabled("You are in: %s", envState.inZone ? ("the zone \"" + envState.zoneName + "\"").c_str() : "the world's own air");
        I3 here{ ifloor(player.feet.x), ifloor(player.feet.y), ifloor(player.feet.z) };
        int remove = -1;
        for (int i = 0; i < (int)info.zones.size(); i++) {
            EnvZone& z = info.zones[i]; ImGui::PushID(i);
            ImGui::SetNextItemWidth(150 * ui); ImGui::InputText("##name", &z.name); ImGui::SameLine(); ImGui::SetNextItemWidth(170 * ui); envCombo("##env", z.env);
            ImGui::SameLine(); if (ImGui::SmallButton("Delete")) remove = i;
            ImGui::SetNextItemWidth(220 * ui); ImGui::InputInt3("From", &z.lo.x); ImGui::SameLine(); if (ImGui::SmallButton("here##lo")) z.lo = here;
            ImGui::SetNextItemWidth(220 * ui); ImGui::InputInt3("To", &z.hi.x); ImGui::SameLine(); if (ImGui::SmallButton("here##hi")) z.hi = here;
            ImGui::SameLine(); if (ImGui::SmallButton("Go there")) { player.feet = { (z.lo.x + z.hi.x) * 0.5 + 0.5, (double)std::min(z.lo.y, z.hi.y), (z.lo.z + z.hi.z) * 0.5 + 0.5 }; player.flying = true; }
            if (z.lo.x > z.hi.x) std::swap(z.lo.x, z.hi.x);
            if (z.lo.y > z.hi.y) std::swap(z.lo.y, z.hi.y);
            if (z.lo.z > z.hi.z) std::swap(z.lo.z, z.hi.z);
            ImGui::Separator(); ImGui::PopID();
        }
        if (remove >= 0) info.zones.erase(info.zones.begin() + remove);
        if (ImGui::Button("Add a zone around me")) info.zones.push_back({ "Zone " + std::to_string(info.zones.size() + 1), "space", { here.x - 8, here.y - 4, here.z - 8 }, { here.x + 8, here.y + 12, here.z + 8 } });
        ImGui::TextDisabled("Zones show as coloured boxes while this window is open.\nIf zones overlap, the smallest one you stand in wins. Walking in or out fades over.");
        if (ImGui::Button("Done", ImVec2(-1, 0))) envWin = false;
        ImGui::End();
    }

    // ---- weather window (from the game menu)
    void WeatherWindow() {
        float ui = settings.uiScale;
        Center(ImVec2(360 * ui, 0));
        ImGui::Begin("Weather", &weatherWin, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextDisabled("This world's weather (it fades over in about 20 seconds):");
        const auto& types = weatherTypes();
        for (int i = 0; i < (int)types.size(); i++) if (ImGui::RadioButton(types[i].name.c_str(), weather.target == i)) weather.set(i, false);
        ImGui::Separator();
        ImGui::Checkbox("Weather changes by itself", &weather.autoChange);
        if (weather.autoChange) ImGui::TextDisabled("Next change in about %d minutes.", (int)(weather.secondsToChange() / 60) + 1);
        if (ImGui::Button("Done", ImVec2(-1, 0))) weatherWin = false;
        ImGui::End();
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
        ImGui::TextDisabled("Click a shape to build with it.   In the world: R turns it, T tips it over, G back to automatic,\nCtrl + middle click copies a placed block's shape and rotation.   Q or Esc closes this.");
        ImGui::End();
    }

    // ---- block editor
    void BlockEditor() {
        float ui = settings.uiScale;
        for (int f = 0; f < 6; f++) { int t = reg.textureIndex(draft.faces[f]); draft.tex[f] = t < 0 ? 0 : t; }
        Center(ImVec2(520 * ui, 0));
        ImGui::Begin(draftId ? "Edit block###editor" : "New block###editor", &editorOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
        // preview on the left, settings on the right
        float pv = 120 * ui; ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + pv, p.y + pv), IM_COL32(30, 32, 38, 255), 4.f);
        ShapeIcon(ImGui::GetWindowDrawList(), draft, AIR, ImVec2(p.x + pv * 0.5f, p.y + pv * 0.5f), pv * 0.75f);
        ImGui::Dummy(ImVec2(pv, pv)); ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::SetNextItemWidth(260 * ui); ImGui::InputText("Name", &draft.name);
        int shape = (int)draft.shape; const char* shapes[] = { "Full block", "Cross plane (flowers, tall grass)", "Flat plane (ladders, vines, posters)" };
        ImGui::SetNextItemWidth(260 * ui);
        if (ImGui::Combo("Shape", &shape, shapes, 3)) {
            draft.shape = (Shape)shape;
            if (draft.shape != Shape::Cube) { draft.render = Render::Cutout; draft.solid = false; faceMode = 0; for (auto& f : draft.faces) f = draft.faces[0]; }
            else { draft.solid = true; draft.render = reg.image(draft.tex[0]).transparent ? Render::Cutout : Render::Opaque; }
        }
        if (draft.shape == Shape::Cube) {
            bool see = draft.render == Render::Cutout;
            if (ImGui::Checkbox("See-through (the picture's transparent pixels)", &see)) draft.render = see ? Render::Cutout : Render::Opaque;
            if (see) ImGui::Checkbox("Hide the sides between two of these (like glass)", &draft.cullSame);
            ImGui::Checkbox("You bump into it", &draft.solid);
        }
        ImGui::SetNextItemWidth(160 * ui); ImGui::SliderInt("Glows (light level)", &draft.light, 0, 15, draft.light ? "%d" : "no");
        if (draft.light) {
            float col[3] = { ((draft.lightColor >> 16) & 255) / 255.f, ((draft.lightColor >> 8) & 255) / 255.f, (draft.lightColor & 255) / 255.f };
            if (ImGui::ColorEdit3("Light colour", col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel))
                draft.lightColor = ((uint32_t)std::lround(col[0] * 255) << 16) | ((uint32_t)std::lround(col[1] * 255) << 8) | (uint32_t)std::lround(col[2] * 255);
        }
        ImGui::Checkbox("Sways in the wind", &draft.waves);
        if (draft.shape != Shape::Cube) ImGui::TextDisabled(draft.shape == Shape::Cross ? "Stands in the middle of its block. You walk through it." : "Sticks to the side of the block you click on. You walk through it.");
        ImGui::EndGroup();

        ImGui::Separator();
        // texture slots
        struct Slot { const char* label; std::vector<int> faces; };
        std::vector<Slot> slots;
        if (draft.shape != Shape::Cube) slots = { { "Picture", { 0, 1, 2, 3, 4, 5 } } };
        else {
            const char* modes[] = { "Same picture on every side", "Top, bottom and sides", "Every side different" };
            ImGui::SetNextItemWidth(260 * ui); ImGui::Combo("Textures", &faceMode, modes, 3);
            if (faceMode == 0) slots = { { "All sides", { 0, 1, 2, 3, 4, 5 } } };
            else if (faceMode == 1) slots = { { "Top", { F_PY } }, { "Sides", { F_PX, F_NX, F_PZ, F_NZ } }, { "Bottom", { F_NY } } };
            else slots = { { "Top", { F_PY } }, { "Bottom", { F_NY } }, { "North (-Z)", { F_NZ } }, { "South (+Z)", { F_PZ } }, { "East (+X)", { F_PX } }, { "West (-X)", { F_NX } } };
        }
        float ts = 64 * ui;
        for (int i = 0; i < (int)slots.size(); i++) {
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
            if (i + 1 < (int)slots.size()) ImGui::SameLine();
        }

        ImGui::Separator();
        bool ok = !draft.name.empty();
        ImGui::BeginDisabled(!ok);
        if (ImGui::Button(draftId ? "Save" : "Create", ImVec2(140 * ui, 0))) {
            draft.name = reg.uniqueName(draft.name, draftId); bool isNew = draftId == AIR;
            ChangeBlocks([&] { if (draftId) reg.put(draftId, draft); else draftId = reg.add(draft); });
            library.saveBlocks(reg);
            if (isNew && draftId && screen == Screen::Game) hotbar[hotSel] = draftId;
            Toast(isNew ? "Made the block \"" + draft.name + "\"" : "Saved \"" + draft.name + "\"");
            editorOpen = false;
        }
        ImGui::EndDisabled();
        ImGui::SameLine(); if (ImGui::Button("Cancel", ImVec2(140 * ui, 0))) editorOpen = false;
        if (draftId) { ImGui::SameLine(); if (ImGui::Button("Delete block...", ImVec2(140 * ui, 0))) { deleteBlockAsk = draftId; wantPopup = "Delete block?"; } }
        ImGui::End();
    }

    // ---- questions (import, delete)
    void Popups() {
        if (wantPopup) { ImGui::OpenPopup(wantPopup); wantPopup = nullptr; }
        float ui = settings.uiScale;
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
        std::error_code ec; fs::remove_all(WorldsDir() + "/Screenshot demo", ec);
        {   // a few coloured lamps for the pictures (only in memory, not saved to your blocks)
            BlockId lamp = reg.find("lamp");
            if (lamp) for (auto c : { std::make_pair("blue lamp", 0x3070FFu), std::make_pair("red lamp", 0xFF2A20u), std::make_pair("green lamp", 0x40FF60u) }) {
                BlockDef d = reg.get(lamp); d.name = c.first; d.lightColor = c.second; if (!reg.find(c.first)) reg.add(d);
            }
        }
        CreateWorld("Screenshot demo", 0); Capture(false);
        buildDemo(*world, reg);
        int n = 0;
        for (const DemoShot& sh : demoShots()) {
            V3 dlt = sh.target - sh.eye; double hz = std::sqrt(dlt.x * dlt.x + dlt.z * dlt.z);
            player.feet = { sh.eye.x, sh.eye.y - Player::EYE, sh.eye.z }; player.yaw = std::atan2(dlt.x, dlt.z); player.pitch = std::atan2(dlt.y, hz); player.vel = { 0, 0, 0 };
            settings.timeOfDay = sh.hour;
            info.environment = sh.env; info.zones.clear(); envState.update(0, player.eye(), info.environment, info.zones, true);
            { int wi = findWeather(sh.weather); weather.set(wi < 0 ? 1 : wi, true); weather.autoChange = false;
              weather.wetness = weather.now.rain > 0.1f ? 1.f : 0.f; weather.snowCover = weather.now.snow > 0.1f ? 1.f : 0.f; weather.flash = 0; weather.boltAge = 99;
              if (sh.lightning) {   // a bolt right in view
                  V3 dl = sh.target - sh.eye; double l = std::sqrt(dl.x * dl.x + dl.z * dl.z);
                  weather.boltPos = { sh.eye.x + dl.x / l * 120 + 25, 0, sh.eye.z + dl.z / l * 120 }; weather.boltAge = 0.03; weather.boltSeed = 12345; weather.flash = 0.7f;
              } }
            I3 pc = chunkOf(ifloor(player.feet.x), ifloor(player.feet.y), ifloor(player.feet.z));
            int frames = 0;
            for (; frames < 3000; frames++) {   // wait until everything around is loaded and meshed
                size_t before = world->loadedCount(); world->stream(pc, settings.renderDistance, settings.verticalDistance, 64);
                ScheduleMeshing(pc); CollectMeshes();
                if (frames > 4 && world->loadedCount() == before && world->dirty.empty() && workers->pending() == 0) break;
                Sleep(2);
            }
            auto s0 = std::chrono::steady_clock::now();
            RenderWorld();
            char name[256]; snprintf(name, sizeof name, "%s/%02d_%s.png", shotDir.c_str(), ++n, sh.name.c_str());
            SaveShot(std::string(name));
            swap->Present(0, 0);
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s0).count();
            char line[256]; snprintf(line, sizeof line, "%s: %zu chunks with meshes, %d drawn, %d waiting frames, frame %.0f ms", name, gpu.size(), drawnChunks, frames, ms); Log(line);
        }
        workers.reset(); world.reset(); fs::remove_all(WorldsDir() + "/Screenshot demo", ec);
        Log("done in " + std::to_string((int)std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()) + " s");
    }

    // ================================================================== frame
    void Frame(double dt) {
        if (resized) OnResize();
        if (toastT > 0) toastT -= dt;
        fpsAcc += dt; fpsN++; if (fpsAcc > 0.5) { fps = fpsN / fpsAcc; fpsAcc = 0; fpsN = 0; }
        PollTextures(dt);
        if (screen == Screen::Game) UpdateGame(dt);

        ImGui::GetStyle().FontScaleMain = settings.uiScale;
        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
        if (screen == Screen::Title) { TitleScreen(); if (libraryOpen && !editorOpen) InventoryScreen(); }
        else {
            DrawHud();
            if (showF3) DrawF3();
            if (inventory && !editorOpen) InventoryScreen();
            if (shapeMenu) ShapeMenu();
            if (paused && !showSettings && !weatherWin && !envWin) PauseMenu();
            if (paused && envWin) EnvWindow();
            if (paused && weatherWin) WeatherWindow();
        }
        if (editorOpen) BlockEditor();
        if (showSettings) SettingsWindow();
        Popups();
        ImGui::Render();

        const float bg[4] = { 0.10f, 0.11f, 0.13f, 1 };
        D3D11_VIEWPORT v{ 0, 0, (float)cw, (float)ch, 0, 1 };
        if (screen == Screen::Game) RenderWorld();
        else { ctx->OMSetRenderTargets(1, backRTV.GetAddressOf(), nullptr); ctx->ClearRenderTargetView(backRTV.Get(), bg); }
        ctx->RSSetViewports(1, &v);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
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
    case WM_LBUTTONDOWN:
        if (a && a->screen == App::Screen::Game && !a->captured && !a->paused && !a->inventory && !a->shapeMenu && !(ImGui::GetIO().WantCaptureMouse)) a->Capture(true);
        return 0;
    case WM_DROPFILES: if (a) a->OnDrop((HDROP)w); return 0;
    case WM_MOUSEWHEEL: if (a) a->OnWheel(GET_WHEEL_DELTA_WPARAM(w)); return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN: if (a) a->OnKey(w, (l & (1 << 30)) != 0); if (m == WM_SYSKEYDOWN) break; return 0;
    case WM_KILLFOCUS: if (a && a->captured) { a->Capture(false); if (a->screen == App::Screen::Game && !a->inventory) a->paused = true; } return 0;
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
