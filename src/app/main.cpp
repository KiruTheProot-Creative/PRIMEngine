// main.cpp : PRIMEngine, Windows + Direct3D 11.
// Title screen (worlds) -> creative mode in an unlimited block world. The world, mesher and player live in
// src/world and src/game (no Windows code there); this file is the window, the renderer and the menus.
#include "../world/mesher.h"
#include "../game/game.h"
#include "../world/library.h"

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

[[noreturn]] static void Fatal(const std::string& what, HRESULT hr = S_OK) {
    char b[160]; snprintf(b, sizeof b, "\n(HRESULT 0x%08X)", (unsigned)hr);
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

struct alignas(16) FrameCB { XMFLOAT4X4 viewProj; float fog[4], fogRange[4], skyTop[4], skyHorizon[4], camF[4], camR[4], camU[4], proj[4]; };
struct alignas(16) ChunkCB { float offset[4]; };

struct GpuChunk { ComPtr<ID3D11Buffer> vbO, ibO, vbC, ibC; UINT nO = 0, nC = 0; };

struct App;
static App* g_app = nullptr;

struct App {
    // ---- window + device
    HWND hwnd = nullptr; int cw = 1280, ch = 720; bool resized = false;
    ComPtr<ID3D11Device> dev; ComPtr<ID3D11DeviceContext> ctx; ComPtr<IDXGISwapChain> swap; ComPtr<ID3D11RenderTargetView> backRTV;
    ComPtr<ID3D11Texture2D> depthTex; ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11VertexShader> vsBlock, vsSky; ComPtr<ID3D11PixelShader> psOpaque, psCutout, psSky; ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> cbFrame, cbChunk;
    ComPtr<ID3D11RasterizerState> rsCull; ComPtr<ID3D11DepthStencilState> dsOn, dsOff; ComPtr<ID3D11BlendState> bsOff;
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
        HR(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1, D3D11_SDK_VERSION, &sd, &swap, &dev, &got, &ctx));
        MakeTargets();

        std::string src = ReadShader("block.hlsl");
        auto vb = Compile(src, "VS_Block", "vs_5_0"); HR(dev->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &vsBlock));
        auto b = Compile(src, "PS_Opaque", "ps_5_0"); HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psOpaque));
        b = Compile(src, "PS_Cutout", "ps_5_0"); HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psCutout));
        b = Compile(src, "VS_Sky", "vs_5_0"); HR(dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &vsSky));
        b = Compile(src, "PS_Sky", "ps_5_0"); HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psSky));
        D3D11_INPUT_ELEMENT_DESC el[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }, { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 }, { "TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 3, DXGI_FORMAT_R32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
        HR(dev->CreateInputLayout(el, 5, vb->GetBufferPointer(), vb->GetBufferSize(), &layout));
        auto cb = [&](UINT size, ComPtr<ID3D11Buffer>& out) { D3D11_BUFFER_DESC d{}; d.ByteWidth = size; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; HR(dev->CreateBuffer(&d, nullptr, &out)); };
        cb(sizeof(FrameCB), cbFrame); cb(sizeof(ChunkCB), cbChunk);

        D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_BACK; rd.FrontCounterClockwise = FALSE; rd.DepthClipEnable = TRUE; HR(dev->CreateRasterizerState(&rd, &rsCull));
        D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL; HR(dev->CreateDepthStencilState(&dd, &dsOn));
        dd.DepthEnable = FALSE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; HR(dev->CreateDepthStencilState(&dd, &dsOff));
        D3D11_BLEND_DESC bd{}; bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; HR(dev->CreateBlendState(&bd, &bsOff));
        D3D11_SAMPLER_DESC sm{}; sm.Filter = D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR; sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; sm.MaxLOD = D3D11_FLOAT32_MAX; HR(dev->CreateSamplerState(&sm, &sampBlocks));
        sm.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; HR(dev->CreateSamplerState(&sm, &sampPoint));

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
        D3D11_TEXTURE2D_DESC d{}; d.Width = (UINT)cw; d.Height = (UINT)ch; d.MipLevels = 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_D32_FLOAT; d.SampleDesc.Count = 1; d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        depthTex.Reset(); dsv.Reset(); HR(dev->CreateTexture2D(&d, nullptr, &depthTex)); HR(dev->CreateDepthStencilView(depthTex.Get(), nullptr, &dsv));
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
        D3D11_TEXTURE2D_DESC d{}; d.Width = d.Height = (UINT)S; d.MipLevels = (UINT)mips; d.ArraySize = (UINT)n; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET; d.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        ComPtr<ID3D11Texture2D> t; HR(dev->CreateTexture2D(&d, nullptr, &t));
        for (int i = 0; i < n; i++) ctx->UpdateSubresource(t.Get(), D3D11CalcSubresource(0, i, mips), nullptr, px[i].data(), S * 4, 0);
        if (texArray) retired.push_back(texArray);
        for (auto& i : icons) retired.push_back(i);
        texArray.Reset(); HR(dev->CreateShaderResourceView(t.Get(), nullptr, &texArray)); ctx->GenerateMips(texArray.Get());
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
        fn(); reg.resolve(); UploadTextures();
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
        for (int i = 0; i < 9; i++) hotbar[i] = reg.find(info.hotbar[i]);
        hotSel = info.hotSel;
        screen = Screen::Game; paused = false; inventory = false; autosave = 0;
        Capture(true);
        Toast("Welcome to " + info.name + ". Double-tap Space to fly, E for blocks, Esc for the menu");
    }
    void SaveWorld() {
        if (!world) return;
        info.player = player; info.hotSel = hotSel; for (int i = 0; i < 9; i++) info.hotbar[i] = reg.has(hotbar[i]) ? reg.get(hotbar[i]).name : "";
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
    bool Playing() const { return screen == Screen::Game && captured && !paused && !inventory; }
    static bool Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
    void OnKey(WPARAM k, bool repeat) {
        if (screen != Screen::Game) {
            if (k == VK_ESCAPE) { if (editorOpen) editorOpen = false; else if (showSettings) showSettings = false; else libraryOpen = false; }
            return;
        }
        if (k == VK_ESCAPE) {
            if (editorOpen) editorOpen = false;
            else if (inventory) { inventory = false; Capture(true); }
            else if (showSettings) showSettings = false;
            else { paused = !paused; Capture(!paused); }
            return;
        }
        if (k == 'E' && !paused && !ImGui::GetIO().WantTextInput) { inventory = !inventory; editorOpen = false; Capture(!inventory); return; }
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
        if (R && (!rPrev || placeCool <= 0) && hoverValid && reg.has(hotbar[hotSel])) {
            int x = hover.x + hover.nx, y = hover.y + hover.ny, z = hover.z + hover.nz;
            const BlockDef& bd = reg.get(hotbar[hotSel]);
            const Player& p = player;   // never place a block inside yourself
            bool inside = bd.solid && x + 1 > p.feet.x - Player::HALF_W && x < p.feet.x + Player::HALF_W && z + 1 > p.feet.z - Player::HALF_W && z < p.feet.z + Player::HALF_W && y + 1 > p.feet.y && y < p.feet.y + Player::HEIGHT;
            // a flat plane faces away from the side you clicked on
            int state = bd.shape != Shape::Plane ? 0 : hover.nx > 0 ? F_PX : hover.nx < 0 ? F_NX : hover.ny > 0 ? F_PY : hover.ny < 0 ? F_NY : hover.nz > 0 ? F_PZ : F_NZ;
            BlockId v = withState(hotbar[hotSel], state);
            if (!inside && world->get(x, y, z) == AIR && world->loaded(x, y, z)) { world->set(x, y, z, v); history.push({ { x, y, z, AIR, v } }); }
            placeCool = 0.25;
        }
        if (M && !mPrev && hoverValid && reg.has(world->get(hover.x, hover.y, hover.z))) {
            BlockId id = typeOf(world->get(hover.x, hover.y, hover.z)); int found = -1;
            for (int i = 0; i < 9; i++) if (hotbar[i] == id) found = i;
            if (found >= 0) hotSel = found; else hotbar[hotSel] = id;
        }
        if (!L) breakCool = 0;
        if (!R) placeCool = 0;
        lPrev = L; rPrev = R; mPrev = M;
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
    void RenderWorld() {
        aspect = (float)cw / ch; camPos = player.eye();
        V3 f = player.forward(), r = normalize(cross({ 0, 1, 0 }, f)), u = cross(f, r);
        float farZ = (settings.renderDistance + 2) * CS * 1.5f;
        view = XMMatrixLookToLH(XMVectorZero(), XMVectorSet((float)f.x, (float)f.y, (float)f.z, 0), XMVectorSet(0, 1, 0, 0));   // camera at the origin
        proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(settings.fov), aspect, 0.05f, farZ);
        XMMATRIX vp = view * proj;
        FrameCB fc{}; XMStoreFloat4x4(&fc.viewProj, vp);
        const float top[3] = { 0.42f, 0.62f, 0.98f }, hor[3] = { 0.74f, 0.84f, 0.98f };
        float fogEnd = settings.renderDistance * CS * 0.95f;
        for (int i = 0; i < 3; i++) { fc.fog[i] = hor[i]; fc.skyTop[i] = top[i]; fc.skyHorizon[i] = hor[i]; }
        fc.fogRange[0] = fogEnd * 0.55f; fc.fogRange[1] = fogEnd;
        fc.camF[0] = (float)f.x; fc.camF[1] = (float)f.y; fc.camF[2] = (float)f.z; fc.camR[0] = (float)r.x; fc.camR[1] = (float)r.y; fc.camR[2] = (float)r.z;
        fc.camU[0] = (float)u.x; fc.camU[1] = (float)u.y; fc.camU[2] = (float)u.z; fc.proj[0] = std::tan(XMConvertToRadians(settings.fov) * 0.5f); fc.proj[1] = aspect;
        ctx->UpdateSubresource(cbFrame.Get(), 0, nullptr, &fc, 0, 0);

        ID3D11Buffer* cbs[2] = { cbFrame.Get(), cbChunk.Get() }; ctx->VSSetConstantBuffers(0, 2, cbs); ctx->PSSetConstantBuffers(0, 2, cbs);
        ctx->RSSetState(rsCull.Get()); ctx->OMSetBlendState(bsOff.Get(), nullptr, 0xFFFFFFFF);
        // sky
        ctx->OMSetDepthStencilState(dsOff.Get(), 0); ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vsSky.Get(), nullptr, 0); ctx->PSSetShader(psSky.Get(), nullptr, 0); ctx->Draw(3, 0);
        // blocks: opaque pass, then cutout pass (glass, leaves)
        ctx->OMSetDepthStencilState(dsOn.Get(), 0); ctx->IASetInputLayout(layout.Get()); ctx->VSSetShader(vsBlock.Get(), nullptr, 0);
        ctx->PSSetShaderResources(0, 1, texArray.GetAddressOf()); ctx->PSSetSamplers(0, 1, sampBlocks.GetAddressOf());
        drawnChunks = 0;
        for (int pass = 0; pass < 2; pass++) {
            ctx->PSSetShader(pass == 0 ? psOpaque.Get() : psCutout.Get(), nullptr, 0);
            for (auto& kv : gpu) {
                const GpuChunk& g = kv.second; UINT n = pass == 0 ? g.nO : g.nC; if (!n) continue;
                // chunk origin relative to the camera, computed in double: no jitter however far you go
                float ox = (float)(kv.first.x * (double)CS - camPos.x), oy = (float)(kv.first.y * (double)CS - camPos.y), oz = (float)(kv.first.z * (double)CS - camPos.z);
                if (!BoxVisible(vp, ox, oy, oz, (float)CS)) continue;
                ChunkCB cc{ { ox, oy, oz, 0 } }; ctx->UpdateSubresource(cbChunk.Get(), 0, nullptr, &cc, 0, 0);
                UINT stride = sizeof(Vtx), off = 0;
                ctx->IASetVertexBuffers(0, 1, pass == 0 ? g.vbO.GetAddressOf() : g.vbC.GetAddressOf(), &stride, &off);
                ctx->IASetIndexBuffer(pass == 0 ? g.ibO.Get() : g.ibC.Get(), DXGI_FORMAT_R32_UINT, 0);
                ctx->DrawIndexed(n, 0, 0); drawnChunks += pass == 0;
            }
        }
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
    void BlockIcon(ImDrawList* dl, BlockId id, ImVec2 c, float s) { if (id != AIR) DefIcon(dl, reg.get(id), c, s); }
    void DefIcon(ImDrawList* dl, const BlockDef& d, ImVec2 c, float s) {   // a small isometric cube made of the block's textures
        dl->AddCallback(PointSampling, nullptr);
        if (d.shape != Shape::Cube) {   // planes: just the picture
            float h = s * 0.42f; dl->AddImage(Icon(d.tex[0]), ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h));
            dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr); return;
        }
        float h = s * 0.5f, q = s * 0.25f;
        ImVec2 T(c.x, c.y - h), L(c.x - h, c.y - q), R(c.x + h, c.y - q), C(c.x, c.y), BL(c.x - h, c.y + q), BR(c.x + h, c.y + q), B(c.x, c.y + h);
        dl->AddImageQuad(Icon(d.tex[F_PY]), T, R, C, L, ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), IM_COL32(255, 255, 255, 255));
        dl->AddImageQuad(Icon(d.tex[F_NZ]), L, C, B, BL, ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), IM_COL32(200, 200, 200, 255));
        dl->AddImageQuad(Icon(d.tex[F_PX]), C, R, BR, B, ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), IM_COL32(155, 155, 155, 255));
        dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }
    void DrawHud() {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        if (hoverValid && Playing()) {   // outline of the targeted block
            BlockId hv = world->get(hover.x, hover.y, hover.z); const BlockDef& hd = reg.get(hv);
            double b0[3] = { 0, 0, 0 }, b1[3] = { 1, 1, 1 }, t = 1.0 / 16;
            if (hd.shape == Shape::Cross) { b0[0] = b0[2] = 0.15; b1[0] = b1[2] = 0.85; }
            if (hd.shape == Shape::Plane) { int f = stateOf(hv) < 6 ? stateOf(hv) : F_PY, ax = f / 2; if (f % 2 == 0) b1[ax] = t; else b0[ax] = 1 - t; }
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
        if (reg.has(hotbar[hotSel])) { const char* nm = reg.get(hotbar[hotSel]).name.c_str(); ImVec2 ts = ImGui::CalcTextSize(nm); dl->AddText(ImVec2((cw - ts.x) * 0.5f, o.y - ts.y - 8), IM_COL32(255, 255, 255, 230), nm); }
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
        if (hoverValid) ImGui::Text("Looking at: %s at %d %d %d", reg.get(world->get(hover.x, hover.y, hover.z)).name.c_str(), hover.x, hover.y, hover.z);
        ImGui::Text("Render distance: %d chunks   Undo steps: %zu", settings.renderDistance, history.undoCount());
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

    // ---- block editor
    void BlockEditor() {
        float ui = settings.uiScale;
        for (int f = 0; f < 6; f++) { int t = reg.textureIndex(draft.faces[f]); draft.tex[f] = t < 0 ? 0 : t; }
        Center(ImVec2(520 * ui, 0));
        ImGui::Begin(draftId ? "Edit block###editor" : "New block###editor", &editorOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
        // preview on the left, settings on the right
        float pv = 120 * ui; ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + pv, p.y + pv), IM_COL32(30, 32, 38, 255), 4.f);
        DefIcon(ImGui::GetWindowDrawList(), draft, ImVec2(p.x + pv * 0.5f, p.y + pv * 0.5f), pv * 0.75f);
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
        } else ImGui::TextDisabled(draft.shape == Shape::Cross ? "Stands in the middle of its block. You walk through it." : "Sticks to the side of the block you click on. You walk through it.");
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
            if (paused && !showSettings) PauseMenu();
        }
        if (editorOpen) BlockEditor();
        if (showSettings) SettingsWindow();
        Popups();
        ImGui::Render();

        const float bg[4] = { 0.10f, 0.11f, 0.13f, 1 };
        ctx->OMSetRenderTargets(1, backRTV.GetAddressOf(), dsv.Get());
        D3D11_VIEWPORT v{ 0, 0, (float)cw, (float)ch, 0, 1 }; ctx->RSSetViewports(1, &v);
        ctx->ClearRenderTargetView(backRTV.Get(), bg); ctx->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
        if (screen == Screen::Game) RenderWorld();
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
        if (a && a->screen == App::Screen::Game && !a->captured && !a->paused && !a->inventory && !(ImGui::GetIO().WantCaptureMouse)) a->Capture(true);
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

int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int) {
    SetProcessDPIAware();
    App app; g_app = &app;
    WNDCLASSA wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = "PRIMEngineWnd"; RegisterClassA(&wc);
    RECT rc{ 0, 0, 1600, 900 }; AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    app.hwnd = CreateWindowA(wc.lpszClassName, "PRIMEngine", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hi, nullptr);
    RECT cr; GetClientRect(app.hwnd, &cr); app.cw = std::max<int>(2, cr.right); app.ch = std::max<int>(2, cr.bottom);
    app.Init();
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
