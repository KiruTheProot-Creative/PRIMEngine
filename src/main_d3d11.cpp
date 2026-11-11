// main_d3d11.cpp : standalone Windows renderer, Direct3D 11, no web stack.
// Same pipeline as web/index.html (WebGL2 test harness):
//   scene -> low-res RGBA8 target (480p "EFB") -> bright-pass/blur chain -> composite (bloom, grade, vignette, 6-bit dither) -> window
//
// Build: see CMakeLists.txt (needs the Windows SDK; no third-party libraries).
#include "scene.h"   // keep before windows.h
#include "model.h"
#include "assetmeta.h"
#include "room.h"
#include "terrain.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#include "../third_party/imgui/imgui.h"
#include "../third_party/imgui/imgui_stdlib.h"
#include "../third_party/imgui/imgui_internal.h"   // DockBuilder (default layout)
#include "../third_party/imgui/backends/imgui_impl_win32.h"
#include "../third_party/imgui/backends/imgui_impl_dx11.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#include <algorithm>
#include <functional>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

using namespace DirectX;
using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------- helpers
[[noreturn]] static void Fatal(const char* what, HRESULT hr = S_OK) {
    char buf[1024]; snprintf(buf, sizeof buf, "%s\n(HRESULT 0x%08X)", what, (unsigned)hr);
    MessageBoxA(nullptr, buf, "Prime-style renderer", MB_ICONERROR | MB_OK);
    ExitProcess(1);
}
#define HR(x) do { HRESULT _hr = (x); if (FAILED(_hr)) Fatal("D3D call failed: " #x, _hr); } while (0)

static std::string ExeDir() {
    char p[MAX_PATH]; GetModuleFileNameA(nullptr, p, MAX_PATH); std::string s(p);
    return s.substr(0, s.find_last_of("\\/") + 1);
}
static std::string ReadShader(const char* file) {
    const char* rel[] = { "shaders\\", "..\\shaders\\", "..\\..\\shaders\\", "..\\..\\..\\shaders\\" };
    for (auto r : rel) {
        std::ifstream f(ExeDir() + r + file, std::ios::binary);
        if (f) { std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
    }
    Fatal((std::string("Could not find shaders\\") + file + " next to the exe").c_str());
}
static ComPtr<ID3DBlob> Compile(const std::string& src, const char* name, const char* entry, const char* target) {
    ComPtr<ID3DBlob> code, err;
    HRESULT hr = D3DCompile(src.data(), src.size(), name, nullptr, nullptr, entry, target, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
    if (FAILED(hr)) Fatal(err ? (const char*)err->GetBufferPointer() : "shader compile failed", hr);
    return code;
}

// ---------------------------------------------------------------------------- constant buffers (must match the cbuffers in the .hlsl files)
struct alignas(16) FrameCB { XMFLOAT4X4 viewProj, view; float cam[4]; float lightPos[4][4]; float lightCol[4][4]; float fogColor[4]; float fogRange[4]; };
struct alignas(16) ObjectCB { XMFLOAT4X4 world; float tint[4]; float params[4]; };
struct alignas(16) PostCB { float p0[4], p1[4], p2[4]; };

struct GpuMesh { ComPtr<ID3D11Buffer> vb, ib; UINT count = 0; };
struct RT { ComPtr<ID3D11Texture2D> tex; ComPtr<ID3D11RenderTargetView> rtv; ComPtr<ID3D11ShaderResourceView> srv; int w = 0, h = 0; };

struct Settings { float bloom = 0.85f; float fog = 0.85f; int res = 480; bool bloomOn = true, dither = true, nearest = false, dyn = true; };

static const int MAXQ = 4096;

struct App {
    HWND hwnd = nullptr; int cw = 1280, ch = 720; bool resized = false, captured = false;
    ComPtr<ID3D11Device> dev; ComPtr<ID3D11DeviceContext> ctx; ComPtr<IDXGISwapChain> swap; ComPtr<ID3D11RenderTargetView> backRTV;

    ComPtr<ID3D11VertexShader> vsWorld, vsPart, vsFull; ComPtr<ID3D11PixelShader> psWorld, psPart, psBright, psBlur, psComp;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> cbFrame, cbObject, cbPost;
    ComPtr<ID3D11RasterizerState> rsCull, rsNoCull;
    ComPtr<ID3D11DepthStencilState> dsOn, dsNoWrite, dsOff;
    ComPtr<ID3D11BlendState> bsOff, bsAdd;
    ComPtr<ID3D11SamplerState> sampWrap, sampClamp, sampPoint, sampPointWrap;

    px::Scene scene; GpuMesh meshes[px::M_COUNT];
    ComPtr<ID3D11ShaderResourceView> texSRV[px::T_COUNT];
    GpuMesh partMesh;

    // ---- imported viewmodel (FBX) + editor state
    struct GpuModelMesh { GpuMesh g; int node = -1, image = -1; };
    std::string assetDir, modelPath, loadError, status;
    std::vector<std::string> assetFiles;
    FILETIME modelTime{};
    px::Model model; bool modelLoaded = false;
    px::AssetMeta meta; px::Animator anim;
    std::vector<px::ClipSettings> clipSet; std::vector<std::string> clipNames;
    std::vector<GpuModelMesh> modelMeshes; std::vector<ComPtr<ID3D11ShaderResourceView>> modelTex;
    bool editor = true, autoReload = true, metaDirty = false;
    float metaTimer = 0, reloadPoll = 0, statusTimer = 0;

    RT rtScene, rtB0, rtB1, rtC0, rtC1; ComPtr<ID3D11Texture2D> depthTex; ComPtr<ID3D11DepthStencilView> dsv;

    px::Camera cam; px::Sim sim; px::Input input; Settings S; float T = 0;

    // ------------------------------------------------------------------ init
    void InitDevice() {
        DXGI_SWAP_CHAIN_DESC sd{}; sd.BufferCount = 2; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow = hwnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        D3D_FEATURE_LEVEL fl[] = { D3D_FEATURE_LEVEL_11_0 }; D3D_FEATURE_LEVEL got;
        HR(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, fl, 1, D3D11_SDK_VERSION, &sd, &swap, &dev, &got, &ctx));
        MakeBackbufferView();

        // shaders
        std::string w = ReadShader("world.hlsl"), p = ReadShader("post.hlsl");
        auto vsb = Compile(w, "world.hlsl", "VS_World", "vs_5_0");
        HR(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vsWorld));
        auto pvb = Compile(w, "world.hlsl", "VS_Particle", "vs_5_0");
        HR(dev->CreateVertexShader(pvb->GetBufferPointer(), pvb->GetBufferSize(), nullptr, &vsPart));
        auto b = Compile(w, "world.hlsl", "PS_World", "ps_5_0");    HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psWorld));
        b = Compile(w, "world.hlsl", "PS_Particle", "ps_5_0");      HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psPart));
        auto fvs = Compile(p, "post.hlsl", "VS_Full", "vs_5_0");    HR(dev->CreateVertexShader(fvs->GetBufferPointer(), fvs->GetBufferSize(), nullptr, &vsFull));
        b = Compile(p, "post.hlsl", "PS_Bright", "ps_5_0");         HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psBright));
        b = Compile(p, "post.hlsl", "PS_Blur", "ps_5_0");           HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psBlur));
        b = Compile(p, "post.hlsl", "PS_Composite", "ps_5_0");      HR(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psComp));

        D3D11_INPUT_ELEMENT_DESC el[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
        HR(dev->CreateInputLayout(el, 4, vsb->GetBufferPointer(), vsb->GetBufferSize(), &layout));

        auto cb = [&](UINT size, ComPtr<ID3D11Buffer>& out) {
            D3D11_BUFFER_DESC d{}; d.ByteWidth = size; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_CONSTANT_BUFFER; HR(dev->CreateBuffer(&d, nullptr, &out)); };
        cb(sizeof(FrameCB), cbFrame); cb(sizeof(ObjectCB), cbObject); cb(sizeof(PostCB), cbPost);

        // fixed-function state: clockwise-front, back-face culling (matches the winding the mesh builder emits)
        D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_BACK; rd.FrontCounterClockwise = FALSE; rd.DepthClipEnable = TRUE;
        HR(dev->CreateRasterizerState(&rd, &rsCull)); rd.CullMode = D3D11_CULL_NONE; HR(dev->CreateRasterizerState(&rd, &rsNoCull));
        D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL; HR(dev->CreateDepthStencilState(&dd, &dsOn));
        dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; HR(dev->CreateDepthStencilState(&dd, &dsNoWrite));
        dd.DepthEnable = FALSE; HR(dev->CreateDepthStencilState(&dd, &dsOff));
        D3D11_BLEND_DESC bd{}; bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; HR(dev->CreateBlendState(&bd, &bsOff));
        bd.RenderTarget[0].BlendEnable = TRUE; bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE; bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE; bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        HR(dev->CreateBlendState(&bd, &bsAdd));
        D3D11_SAMPLER_DESC sm{}; sm.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; sm.MaxLOD = D3D11_FLOAT32_MAX; sm.ComparisonFunc = D3D11_COMPARISON_NEVER;
        HR(dev->CreateSamplerState(&sm, &sampWrap));
        sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; HR(dev->CreateSamplerState(&sm, &sampClamp));
        sm.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; HR(dev->CreateSamplerState(&sm, &sampPoint));
        sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; HR(dev->CreateSamplerState(&sm, &sampPointWrap));

        // textures + meshes (all procedural, from scene.h)
        for (int k = 0; k < px::T_COUNT; k++) {
            bool mips = (k != px::T_ENV);
            texSRV[k] = MakeTexture(px::makeTex((px::TexKind)k), mips);
        }
        for (int m = 0; m < px::M_COUNT; m++) {
            const auto& md = scene.mats[m].mesh; GpuMesh& g = meshes[m]; g.count = (UINT)md.i.size();
            D3D11_BUFFER_DESC d{}; d.Usage = D3D11_USAGE_IMMUTABLE; d.ByteWidth = (UINT)(md.v.size() * sizeof(px::Vertex)); d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA sdt{ md.v.data(), 0, 0 }; HR(dev->CreateBuffer(&d, &sdt, &g.vb));
            d.ByteWidth = (UINT)(md.i.size() * sizeof(uint32_t)); d.BindFlags = D3D11_BIND_INDEX_BUFFER; sdt.pSysMem = md.i.data(); HR(dev->CreateBuffer(&d, &sdt, &g.ib));
        }
        {   // dynamic particle quads
            D3D11_BUFFER_DESC d{}; d.Usage = D3D11_USAGE_DYNAMIC; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            d.ByteWidth = MAXQ * 4 * sizeof(px::Vertex); HR(dev->CreateBuffer(&d, nullptr, &partMesh.vb));
            std::vector<uint32_t> ix(MAXQ * 6); for (uint32_t q = 0; q < MAXQ; q++) { uint32_t o[6] = { q * 4, q * 4 + 1, q * 4 + 2, q * 4, q * 4 + 2, q * 4 + 3 }; std::copy(o, o + 6, ix.begin() + q * 6); }
            D3D11_BUFFER_DESC id{}; id.Usage = D3D11_USAGE_IMMUTABLE; id.ByteWidth = (UINT)(ix.size() * 4); id.BindFlags = D3D11_BIND_INDEX_BUFFER;
            D3D11_SUBRESOURCE_DATA sdt{ ix.data(), 0, 0 }; HR(dev->CreateBuffer(&id, &sdt, &partMesh.ib));
        }

        // editor UI
        IMGUI_CHECKVERSION(); ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        static std::string ini = ExeDir() + "editor_layout.ini"; io.IniFilename = ini.c_str();
        iniHadLayout = GetFileAttributesA(ini.c_str()) != INVALID_FILE_ATTRIBUTES;
        edCam.p = { 0, 2.6f, 1.0f }; edCam.pitch = -0.12f;
        StyleUI();
        ImGui_ImplWin32_Init(hwnd); ImGui_ImplDX11_Init(dev.Get(), ctx.Get());

        // assets: load the first model found
        assetDir = FindDir("assets");
        FindAssets();
        Log("PRIMEngine started. Assets folder: " + assetDir);
        LoadEditorSettings();
        if (!lastRoom.empty() && GetFileAttributesA(lastRoom.c_str()) != INVALID_FILE_ATTRIBUTES) OpenRoom(lastRoom);
        if (!assetFiles.empty()) LoadModel(assetFiles[0]); else Log("No FBX files in the assets folder yet");
    }

    ComPtr<ID3D11ShaderResourceView> MakeTexture(const std::vector<uint8_t>& pixels, bool mips) { return MakeTextureRGBA(px::TN, px::TN, pixels.data(), mips); }
    ComPtr<ID3D11ShaderResourceView> MakeTextureRGBA(int w, int h, const uint8_t* pixels, bool mips) {
        D3D11_TEXTURE2D_DESC d{}; d.Width = (UINT)w; d.Height = (UINT)h; d.MipLevels = mips ? 0 : 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE | (mips ? D3D11_BIND_RENDER_TARGET : 0); d.MiscFlags = mips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
        ComPtr<ID3D11Texture2D> t;
        if (mips) { HR(dev->CreateTexture2D(&d, nullptr, &t)); ctx->UpdateSubresource(t.Get(), 0, nullptr, pixels, (UINT)w * 4, 0); }
        else { D3D11_SUBRESOURCE_DATA sd{ pixels, (UINT)w * 4, 0 }; HR(dev->CreateTexture2D(&d, &sd, &t)); }
        ComPtr<ID3D11ShaderResourceView> srv; HR(dev->CreateShaderResourceView(t.Get(), nullptr, &srv));
        if (mips) ctx->GenerateMips(srv.Get());
        return srv;
    }
    void MakeBackbufferView() {
        ComPtr<ID3D11Texture2D> bb; HR(swap->GetBuffer(0, IID_PPV_ARGS(&bb))); HR(dev->CreateRenderTargetView(bb.Get(), nullptr, &backRTV));
    }
    void OnResize() {
        resized = false; ctx->OMSetRenderTargets(0, nullptr, nullptr); backRTV.Reset();
        HR(swap->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0)); MakeBackbufferView();
        RECT r; GetClientRect(hwnd, &r); cw = std::max<int>(2, r.right - r.left); ch = std::max<int>(2, r.bottom - r.top);
    }
    void MakeRT(RT& rt, int w, int h) {
        rt.w = w; rt.h = h; rt.tex.Reset(); rt.rtv.Reset(); rt.srv.Reset();
        D3D11_TEXTURE2D_DESC d{}; d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        HR(dev->CreateTexture2D(&d, nullptr, &rt.tex)); HR(dev->CreateRenderTargetView(rt.tex.Get(), nullptr, &rt.rtv)); HR(dev->CreateShaderResourceView(rt.tex.Get(), nullptr, &rt.srv));
    }
    void EnsureRTs(int w, int h) {
        if (rtScene.w == w && rtScene.h == h) return;
        MakeRT(rtScene, w, h);
        auto q = [](int a, int s) { return std::max(1, a / s); };
        MakeRT(rtB0, q(w, 4), q(h, 4)); MakeRT(rtB1, q(w, 4), q(h, 4)); MakeRT(rtC0, q(w, 8), q(h, 8)); MakeRT(rtC1, q(w, 8), q(h, 8));
        D3D11_TEXTURE2D_DESC d{}; d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        depthTex.Reset(); dsv.Reset(); HR(dev->CreateTexture2D(&d, nullptr, &depthTex)); HR(dev->CreateDepthStencilView(depthTex.Get(), nullptr, &dsv));
    }

    // ------------------------------------------------------------------ per-frame
    XMMATRIX CannonLocal() const {
        float bx = std::sin(cam.bob) * 0.006f, by = std::fabs(std::cos(cam.bob)) * 0.006f;
        return XMMatrixRotationX(0.02f + cam.recoil * 0.8f) * XMMatrixRotationY(-0.16f) * XMMatrixTranslation(0.27f + bx, -0.26f - by, 0.62f - cam.recoil);
    }
    static XMMATRIX CamWorld(px::V3 p, px::V3 r, px::V3 u, px::V3 f) {
        return XMMATRIX(XMVectorSet(r.x, r.y, r.z, 0), XMVectorSet(u.x, u.y, u.z, 0), XMVectorSet(f.x, f.y, f.z, 0), XMVectorSet(p.x, p.y, p.z, 1));
    }
    void Fire() {
        px::V3 f, r, u; cam.basis(f, r, u);
        XMVECTOR m = modelLoaded
            ? XMVector3TransformCoord(XMVectorSet((model.bmin.x + model.bmax.x) * 0.5f, (model.bmin.y + model.bmax.y) * 0.5f, model.bmax.z, 1), ModelPlacement() * CamWorld(cam.p, r, u, f))
            : XMVector3TransformCoord(XMVectorSet(0, 0, 0.92f, 1), CannonLocal() * CamWorld(cam.p, r, u, f));
        px::V3 mz{ XMVectorGetX(m), XMVectorGetY(m), XMVectorGetZ(m) };
        px::V3 aim = px::normalize(cam.p + f * 30.f - mz);
        sim.fire(mz, aim); cam.recoil = 0.07f;
    }
    void SetFrame(const XMMATRIX& view, const XMMATRIX& vp, px::V3 eye, const float lp[4][4], const float lc[4][4]) {
        FrameCB f{}; XMStoreFloat4x4(&f.viewProj, vp); XMStoreFloat4x4(&f.view, view);
        f.cam[0] = eye.x; f.cam[1] = eye.y; f.cam[2] = eye.z; f.cam[3] = T;
        memcpy(f.lightPos, lp, sizeof f.lightPos); memcpy(f.lightCol, lc, sizeof f.lightCol);
        f.fogColor[0] = 0.045f; f.fogColor[1] = 0.085f; f.fogColor[2] = 0.1f; f.fogColor[3] = S.fog; f.fogRange[0] = roomOpen ? 18.f : 6.f; f.fogRange[1] = roomOpen ? 170.f : 42.f;
        ctx->UpdateSubresource(cbFrame.Get(), 0, nullptr, &f, 0, 0);
    }
    void DrawMat(int m, const XMMATRIX& world) {
        const px::Material& mat = scene.mats[m]; ObjectCB o{}; XMStoreFloat4x4(&o.world, world);
        o.tint[0] = mat.tint[0]; o.tint[1] = mat.tint[1]; o.tint[2] = mat.tint[2]; o.tint[3] = 1; o.params[0] = mat.env; o.params[1] = mat.glow; o.params[2] = mat.dyn;
        ctx->UpdateSubresource(cbObject.Get(), 0, nullptr, &o, 0, 0);
        ID3D11ShaderResourceView* srv[2] = { texSRV[mat.tex].Get(), texSRV[px::T_ENV].Get() }; ctx->PSSetShaderResources(0, 2, srv);
        UINT stride = sizeof(px::Vertex), off = 0; ctx->IASetVertexBuffers(0, 1, meshes[m].vb.GetAddressOf(), &stride, &off);
        ctx->IASetIndexBuffer(meshes[m].ib.Get(), DXGI_FORMAT_R32_UINT, 0); ctx->DrawIndexed(meshes[m].count, 0, 0);
    }
    void SetViewport(int w, int h, float zmin = 0, float zmax = 1) { D3D11_VIEWPORT v{ 0, 0, (float)w, (float)h, zmin, zmax }; ctx->RSSetViewports(1, &v); }

    void PostPass(RT* dst, ID3D11PixelShader* ps, ID3D11ShaderResourceView* s0, ID3D11ShaderResourceView* s1, ID3D11ShaderResourceView* s2, const PostCB& cbv) {
        ID3D11ShaderResourceView* none[3] = {}; ctx->PSSetShaderResources(0, 3, none);          // avoid SRV/RTV hazards
        ID3D11RenderTargetView* target = dst ? dst->rtv.Get() : outRTV; ctx->OMSetRenderTargets(1, &target, nullptr);
        SetViewport(dst ? dst->w : ow, dst ? dst->h : oh);
        ctx->UpdateSubresource(cbPost.Get(), 0, nullptr, &cbv, 0, 0);
        ID3D11ShaderResourceView* srv[3] = { s0, s1, s2 }; ctx->PSSetShaderResources(0, 3, srv);
        ctx->PSSetShader(ps, nullptr, 0); ctx->Draw(3, 0);
    }

    ID3D11RenderTargetView* outRTV = nullptr; int ow = 2, oh = 2;
    void Render() {
        if (resized) OnResize();
        // the game is drawn into the Viewport panel (editor) or straight into the window (F11, game only)
        bool toPanel = editor;
        if (toPanel && rtView.w >= 8) { outRTV = rtView.rtv.Get(); ow = rtView.w; oh = rtView.h; } else if (!toPanel) { outRTV = backRTV.Get(); ow = cw; oh = ch; } else outRTV = nullptr;
        if (outRTV) RenderScene();
        ImGui::Render();
        ctx->OMSetRenderTargets(1, backRTV.GetAddressOf(), nullptr); SetViewport(cw, ch);
        if (editor) { const float bg[4] = { 0.02f, 0.05f, 0.06f, 1 }; ctx->ClearRenderTargetView(backRTV.Get(), bg); }
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        HR(swap->Present(1, 0));
    }
    void RenderScene() {
        int ih = S.res ? S.res : oh, iw = std::max(2, (int)std::lround((double)ih * ow / oh)); EnsureRTs(iw, ih);

        const px::Camera& C = ActiveCam();
        px::V3 f, r, u; C.basis(f, r, u); px::V3 eye = C.eye();
        float asp = (float)iw / ih;
        XMMATRIX view = XMMatrixLookToLH(XMVectorSet(eye.x, eye.y, eye.z, 1), XMVectorSet(f.x, f.y, f.z, 0), XMVectorSet(u.x, u.y, u.z, 0));
        XMMATRIX vp = view * XMMatrixPerspectiveFovLH(XMConvertToRadians(60.f), asp, 0.1f, roomOpen ? 420.f : 80.f);
        XMMATRIX vp2 = view * XMMatrixPerspectiveFovLH(XMConvertToRadians(50.f), asp, 0.03f, 3.f);

        // dynamic lights (4, like a GX light mask)
        float pulse = 0.9f + 0.12f * std::sin(T * 3.f);
        px::V3 flP = sim.bolts.empty() ? sim.flashP : sim.bolts.back().p; float flK = sim.bolts.empty() ? std::max(0.f, sim.flashT) / 0.3f * 1.6f : 1.5f;
        struct L { px::V3 p, c; float r, k; } L4[4] = {
            { roomOpen ? px::V3{ 0, 7.f, 0 } : px::V3{ 0, 2.0f, 10 }, roomOpen ? px::V3{ 0.9f, 0.75f, 0.55f } : px::V3{ 1, 0.5f, 0.15f }, roomOpen ? 22.f : 9.f, roomOpen ? 0.55f : pulse },
            { { 3 * std::sin(T * 0.6f), 2.5f, 18 + 3 * std::cos(T * 0.6f) }, { 0.1f, 0.55f, 0.8f }, 9, roomOpen ? 0.f : 1.f },
            { flP, { 1, 0.7f, 0.35f }, 8, flK },
            { eye, { 0.25f, 0.3f, 0.32f }, 5, 0.6f } };
        float lp[4][4], lc[4][4]; float on = S.dyn ? 1.f : 0.f;
        for (int i = 0; i < 4; i++) {
            lp[i][0] = L4[i].p.x; lp[i][1] = L4[i].p.y; lp[i][2] = L4[i].p.z; lp[i][3] = L4[i].r;
            lc[i][0] = L4[i].c.x * L4[i].k * on; lc[i][1] = L4[i].c.y * L4[i].k * on; lc[i][2] = L4[i].c.z * L4[i].k * on; lc[i][3] = 0;
        }

        // ---- 1) scene @ internal resolution
        ID3D11Buffer* cbs[3] = { cbFrame.Get(), cbObject.Get(), cbPost.Get() };
        ctx->VSSetConstantBuffers(0, 2, cbs); ctx->PSSetConstantBuffers(0, 3, cbs);
        ID3D11SamplerState* ss[3] = { sampWrap.Get(), sampClamp.Get(), sampClamp.Get() }; ctx->PSSetSamplers(0, 3, ss);
        ID3D11ShaderResourceView* none[3] = {}; ctx->PSSetShaderResources(0, 3, none);
        ctx->OMSetRenderTargets(1, rtScene.rtv.GetAddressOf(), dsv.Get()); SetViewport(iw, ih);
        const float black[4] = { 0, 0, 0, 1 }; ctx->ClearRenderTargetView(rtScene.rtv.Get(), black); ctx->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, 1.f, 0);
        ctx->OMSetDepthStencilState(dsOn.Get(), 0); ctx->OMSetBlendState(bsOff.Get(), nullptr, 0xFFFFFFFF); ctx->RSSetState(rsCull.Get());
        ctx->IASetInputLayout(layout.Get()); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vsWorld.Get(), nullptr, 0); ctx->PSSetShader(psWorld.Get(), nullptr, 0);
        SetFrame(view, vp, eye, lp, lc);
        if (roomOpen) DrawRoom(vp);
        else for (int m : { px::M_FLOOR, px::M_WALL, px::M_TRIM, px::M_STRIP, px::M_CORE, px::M_DOOR }) DrawMat(m, XMMatrixIdentity());

        // viewmodel: own projection, squeezed into the nearest 5% of the depth range so it never clips into walls
        SetViewport(iw, ih, 0.f, 0.05f); SetFrame(view, vp2, eye, lp, lc);
        if (!playing && !showViewmodel) {}
        else if (modelLoaded) DrawModel(CamWorld(eye, r, u, f));
        else { XMMATRIX cannonWorld = CannonLocal() * CamWorld(eye, r, u, f); DrawMat(px::M_CANNON, cannonWorld); DrawMat(px::M_CANNONGLOW, cannonWorld); }
        SetViewport(iw, ih); SetFrame(view, vp, eye, lp, lc);

        // particles: additive camera-facing quads, depth-tested, no depth write
        std::vector<px::Vertex> qv; qv.reserve(2048 * 4);
        auto quad = [&](px::V3 c, float size, px::V3 col, float a) {
            if (qv.size() >= (size_t)MAXQ * 4) return;
            static const float cs[4][4] = { { -1, -1, 0, 1 }, { 1, -1, 1, 1 }, { 1, 1, 1, 0 }, { -1, 1, 0, 0 } };
            for (auto& k : cs) {
                px::V3 p = c + (r * k[0] + u * k[1]) * size;
                qv.push_back({ { p.x, p.y, p.z }, { 0, 0, 0 }, { k[2], k[3] }, { col.x, col.y, col.z, a } });
            }
        };
        if (!roomOpen) {
            quad({ 0, 1.75f, 10 }, 1.7f, { 1, 0.45f, 0.1f }, 0.5f * pulse);     // core halo
            quad({ 0, 2.2f, 27.6f }, 3.4f, { 0.1f, 0.7f, 1 }, 0.3f);            // door halo
        }
        for (auto& b : sim.bolts) quad(b.p, 0.75f, { 1, 0.7f, 0.35f }, 1.f);
        for (auto& p : sim.parts) {
            if (p.mote && roomOpen) continue;
            float k = p.mote ? (0.7f + 0.3f * std::sin(T * 2.f + p.ph)) : std::min(1.f, p.life / p.max * 1.5f);
            quad(p.p, p.size, p.c, p.a * k);
        }
        if (!qv.empty()) {
            D3D11_MAPPED_SUBRESOURCE ms; HR(ctx->Map(partMesh.vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms)); memcpy(ms.pData, qv.data(), qv.size() * sizeof(px::Vertex)); ctx->Unmap(partMesh.vb.Get(), 0);
            ctx->VSSetShader(vsPart.Get(), nullptr, 0); ctx->PSSetShader(psPart.Get(), nullptr, 0);
            ctx->OMSetDepthStencilState(dsNoWrite.Get(), 0); ctx->OMSetBlendState(bsAdd.Get(), nullptr, 0xFFFFFFFF); ctx->RSSetState(rsNoCull.Get());
            ID3D11ShaderResourceView* spr = texSRV[px::T_SPRITE].Get(); ctx->PSSetShaderResources(0, 1, &spr);
            UINT stride = sizeof(px::Vertex), off = 0; ctx->IASetVertexBuffers(0, 1, partMesh.vb.GetAddressOf(), &stride, &off);
            ctx->IASetIndexBuffer(partMesh.ib.Get(), DXGI_FORMAT_R32_UINT, 0); ctx->DrawIndexed((UINT)(qv.size() / 4 * 6), 0, 0);
        }

        // ---- 2) bloom chain: 1/4 res bright-pass + blur, then 1/8 res wide blur
        ctx->OMSetDepthStencilState(dsOff.Get(), 0); ctx->OMSetBlendState(bsOff.Get(), nullptr, 0xFFFFFFFF); ctx->RSSetState(rsNoCull.Get());
        ctx->IASetInputLayout(nullptr); ctx->VSSetShader(vsFull.Get(), nullptr, 0);
        auto bright = [&](RT& src, RT& dst, float thresh) { PostCB c{}; c.p0[0] = 1.f / src.w; c.p0[1] = 1.f / src.h; c.p0[2] = thresh; c.p0[3] = 1.f; PostPass(&dst, psBright.Get(), src.srv.Get(), nullptr, nullptr, c); };
        auto blur = [&](RT& src, RT& dst, float dx, float dy) { PostCB c{}; c.p0[0] = 1.f / src.w; c.p0[1] = 1.f / src.h; c.p2[2] = dx; c.p2[3] = dy; PostPass(&dst, psBlur.Get(), src.srv.Get(), nullptr, nullptr, c); };
        bright(rtScene, rtB0, 0.55f); blur(rtB0, rtB1, 1, 0); blur(rtB1, rtB0, 0, 1);
        bright(rtB0, rtC0, -1.f);
        blur(rtC0, rtC1, 1, 0); blur(rtC1, rtC0, 0, 1); blur(rtC0, rtC1, 1, 0); blur(rtC1, rtC0, 0, 1);

        // ---- 3) composite into the window
        ID3D11SamplerState* sc = S.nearest ? sampPoint.Get() : sampClamp.Get(); ctx->PSSetSamplers(2, 1, &sc);
        PostCB c{}; c.p1[0] = S.bloomOn ? S.bloom : 0.f; c.p1[1] = S.dither ? 1.f : 0.f; c.p1[2] = 1.12f; c.p1[3] = 0.8f; c.p2[0] = (float)iw; c.p2[1] = (float)ih;
        PostPass(nullptr, psComp.Get(), rtScene.srv.Get(), rtB0.srv.Get(), rtC0.srv.Get(), c);
        ID3D11ShaderResourceView* none2[3] = {}; ctx->PSSetShaderResources(0, 3, none2);

    }

    // ------------------------------------------------------------------ assets
    static std::string FindDir(const char* name) {
        const char* rel[] = { "", "..\\", "..\\..\\", "..\\..\\..\\" };
        for (auto r : rel) {
            std::string d = ExeDir() + r + name;
            DWORD a = GetFileAttributesA(d.c_str());
            if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) return d + "\\";
        }
        std::string d = ExeDir() + name; CreateDirectoryA(d.c_str(), nullptr); return d + "\\";
    }
    static std::string FileName(const std::string& p) { return p.substr(p.find_last_of("\\/") + 1); }
    static FILETIME FileTime(const std::string& p) {
        WIN32_FILE_ATTRIBUTE_DATA a{}; if (GetFileAttributesExA(p.c_str(), GetFileExInfoStandard, &a)) return a.ftLastWriteTime; return FILETIME{};
    }
    void FindAssets() {
        assetFiles.clear();
        WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((assetDir + "*.fbx").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) { do { assetFiles.push_back(assetDir + fd.cFileName); } while (FindNextFileA(h, &fd)); FindClose(h); }
        std::sort(assetFiles.begin(), assetFiles.end());
    }
    void SetStatus(const std::string& s) { status = s; statusTimer = 4.f; Log(s); }
    void LoadModel(const std::string& path) {
        if (path.empty()) return;
        px::AssetMeta m2; if (!m2.load(path + ".meta")) m2 = px::AssetMeta{};
        px::Model mdl; std::string err;
        if (!mdl.loadFBX(path, err, m2.importScale)) { loadError = FileName(path) + ": " + err; SetStatus("Import failed: " + loadError); return; }
        loadError.clear(); meta = m2; model = std::move(mdl); modelPath = path; modelTime = FileTime(path);
        meta.applyTo(model, clipSet, clipNames);
        anim.mode = meta.blendMode; anim.curve = meta.blendCurve; anim.bind(&model, &clipSet);
        int start = meta.defaultClip.empty() ? -1 : model.findClip(meta.defaultClip);
        if (start < 0) for (size_t i = 0; i < clipSet.size(); i++) if (clipSet[i].loop) { start = (int)i; break; }
        if (start >= 0) anim.play(start, 0);
        UploadModel(); modelLoaded = true; metaDirty = true;
        SetStatus("Imported " + FileName(path) + ": " + std::to_string(model.clips.size()) + " animations found");
    }
    void UploadModel() {
        modelMeshes.clear(); modelTex.clear();
        for (auto& img : model.images) modelTex.push_back(MakeTextureRGBA(img.w, img.h, img.rgba.data(), true));
        for (auto& mm : model.meshes) {
            GpuModelMesh g; g.node = mm.node; g.image = mm.image; g.g.count = (UINT)mm.i.size();
            D3D11_BUFFER_DESC d{}; d.Usage = D3D11_USAGE_IMMUTABLE; d.ByteWidth = (UINT)(mm.v.size() * sizeof(px::Vertex)); d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA sdt{ mm.v.data(), 0, 0 }; HR(dev->CreateBuffer(&d, &sdt, &g.g.vb));
            d.ByteWidth = (UINT)(mm.i.size() * sizeof(uint32_t)); d.BindFlags = D3D11_BIND_INDEX_BUFFER; sdt.pSysMem = mm.i.data(); HR(dev->CreateBuffer(&d, &sdt, &g.g.ib));
            modelMeshes.push_back(std::move(g));
        }
    }
    void SaveMeta() {
        if (!modelLoaded) return;
        meta.blendMode = anim.mode; meta.blendCurve = anim.curve;
        meta.capture(model, clipSet, clipNames);
        if (meta.save(modelPath + ".meta")) SetStatus("Saved"); else SetStatus("Could not save settings");
    }
    void Tick(float dt) {
        anim.update(dt);
        if (statusTimer > 0) statusTimer -= dt;
        if (roomOpen && roomDirty && !terrainPreview && (roomAutosave += dt) > 30.f) { roomAutosave = 0; if (room.save(roomPath)) { roomDirty = false; SetStatus("Autosaved room " + room.name); } }
        if (metaDirty) { metaTimer += dt; if (metaTimer > 0.6f) { SaveMeta(); metaDirty = false; metaTimer = 0; } }
        if (autoReload && modelLoaded && (reloadPoll += dt) > 1.f) {
            reloadPoll = 0; FILETIME t = FileTime(modelPath);
            if (CompareFileTime(&t, &modelTime) != 0) { Sleep(150); LoadModel(modelPath); }    // file re-exported from Blockbench
        }
    }
    void Dirty() { metaDirty = true; metaTimer = 0; }

    // model placement in camera space: centre, auto-fit scale, rotation, offset (+ walk bob and recoil)
    XMMATRIX ModelPlacement() const {
        px::V3 c = (model.bmin + model.bmax) * 0.5f, e = model.bmax - model.bmin;
        float longest = std::max(e.x, std::max(e.y, e.z));
        float s = (meta.autoFit && longest > 1e-4f ? meta.fitLength / longest : 1.f) * meta.viewScale;
        float bx = std::sin(cam.bob) * 0.006f, by = std::fabs(std::cos(cam.bob)) * 0.006f;
        return XMMatrixTranslation(-c.x, -c.y, -c.z) * XMMatrixScaling(s, s, s) *
               XMMatrixRotationRollPitchYaw(XMConvertToRadians(meta.viewRotDeg.x), XMConvertToRadians(meta.viewRotDeg.y), XMConvertToRadians(meta.viewRotDeg.z)) *
               XMMatrixTranslation(meta.viewPos.x + bx, meta.viewPos.y - by, meta.viewPos.z - cam.recoil);
    }
    static XMMATRIX ToXM(const px::Mat4& m) {   // column-vector storage m[col][row] == row-vector rows
        return XMMATRIX(m.m[0][0], m.m[0][1], m.m[0][2], m.m[0][3], m.m[1][0], m.m[1][1], m.m[1][2], m.m[1][3],
                        m.m[2][0], m.m[2][1], m.m[2][2], m.m[2][3], m.m[3][0], m.m[3][1], m.m[3][2], m.m[3][3]);
    }
    void DrawModel(const XMMATRIX& camWorld) {
        std::vector<px::Mat4> W; model.worldMatrices(anim.out, W);
        XMMATRIX place = ModelPlacement() * camWorld;
        ID3D11SamplerState* s0 = meta.pixelFilter ? sampPointWrap.Get() : sampWrap.Get(); ctx->PSSetSamplers(0, 1, &s0);
        for (auto& g : modelMeshes) {
            ObjectCB o{}; XMStoreFloat4x4(&o.world, ToXM(W[g.node]) * place);
            o.tint[0] = o.tint[1] = o.tint[2] = o.tint[3] = 1; o.params[0] = meta.reflect; o.params[1] = 0; o.params[2] = 1; o.params[3] = 0.5f;
            ctx->UpdateSubresource(cbObject.Get(), 0, nullptr, &o, 0, 0);
            ID3D11ShaderResourceView* srv[2] = { g.image >= 0 ? modelTex[g.image].Get() : texSRV[px::T_STRIP].Get(), texSRV[px::T_ENV].Get() };
            ctx->PSSetShaderResources(0, 2, srv);
            UINT stride = sizeof(px::Vertex), off = 0; ctx->IASetVertexBuffers(0, 1, g.g.vb.GetAddressOf(), &stride, &off);
            ctx->IASetIndexBuffer(g.g.ib.Get(), DXGI_FORMAT_R32_UINT, 0); ctx->DrawIndexed(g.g.count, 0, 0);
        }
        ID3D11SamplerState* back = sampWrap.Get(); ctx->PSSetSamplers(0, 1, &back);
    }

    // ------------------------------------------------------------------ editor panels
    void StyleUI() {
        ImGui::StyleColorsDark();
        ImGuiStyle& st = ImGui::GetStyle();
        st.WindowRounding = 6; st.FrameRounding = 4; st.GrabRounding = 4; st.WindowBorderSize = 1;
        ImVec4* c = st.Colors;
        c[ImGuiCol_WindowBg] = ImVec4(0.03f, 0.07f, 0.08f, 0.90f);
        c[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.16f, 0.18f, 1); c[ImGuiCol_TitleBgActive] = ImVec4(0.06f, 0.30f, 0.34f, 1);
        c[ImGuiCol_Border] = ImVec4(0.15f, 0.55f, 0.55f, 0.6f);
        c[ImGuiCol_FrameBg] = ImVec4(0.06f, 0.16f, 0.18f, 1); c[ImGuiCol_FrameBgHovered] = ImVec4(0.09f, 0.28f, 0.30f, 1); c[ImGuiCol_FrameBgActive] = ImVec4(0.12f, 0.38f, 0.40f, 1);
        c[ImGuiCol_Button] = ImVec4(0.08f, 0.30f, 0.33f, 1); c[ImGuiCol_ButtonHovered] = ImVec4(0.12f, 0.45f, 0.48f, 1); c[ImGuiCol_ButtonActive] = ImVec4(0.95f, 0.50f, 0.12f, 1);
        c[ImGuiCol_Header] = ImVec4(0.08f, 0.30f, 0.33f, 1); c[ImGuiCol_HeaderHovered] = ImVec4(0.12f, 0.45f, 0.48f, 1); c[ImGuiCol_HeaderActive] = ImVec4(0.95f, 0.50f, 0.12f, 1);
        c[ImGuiCol_CheckMark] = ImVec4(0.98f, 0.60f, 0.15f, 1); c[ImGuiCol_SliderGrab] = ImVec4(0.98f, 0.60f, 0.15f, 1); c[ImGuiCol_SliderGrabActive] = ImVec4(1, 0.75f, 0.3f, 1);
        c[ImGuiCol_Text] = ImVec4(0.78f, 0.96f, 0.94f, 1);
    }
    // ------------------------------------------------------------------ editor (docked engine UI)
    enum class Sel { None, World, Player, Model, Node, Asset };
    Sel sel = Sel::Model; int selNode = -1; std::string selAsset;
    bool playing = false, pausedGame = false;
    px::Camera edCam;                         // editor fly camera (separate from the player)
    RT rtView; ImVec2 viewMin{ 0, 0 }, viewMax{ 0, 0 }; bool viewHovered = false;
    bool showHier = true, showInsp = true, showAssets = true, showAnim = true, showConsole = true, showViewmodel = true, firstLayout = true;
    std::vector<std::string> logLines; std::string assetFilter;

    void Log(const std::string& s) {
        SYSTEMTIME t; GetLocalTime(&t); char b[16]; snprintf(b, sizeof b, "[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
        logLines.push_back(b + s); if (logLines.size() > 500) logLines.erase(logLines.begin());
    }
    const px::Camera& ActiveCam() const { return playing ? cam : edCam; }
    void StartPlay() {
        if (playing) return;
        if (terrainPreview) { SetStatus("Apply or cancel the terrain preview first"); return; }
        playing = true; pausedGame = false; sim = px::Sim{}; sim.demo = !roomOpen;
        if (roomOpen) { sim.solidAt = &App::SolidAt; sim.solidCtx = this; RoomPlayerSpawn(); Log("Play (from the editor camera)"); } else Log("Play");
    }
    void StopPlay() { if (!playing) return; playing = false; pausedGame = false; if (captured) Capture(false); cam = px::Camera{}; sim = px::Sim{}; Log("Stop (player reset)"); }
    void EnsureViewRT(int w, int h) { w = std::max(2, w); h = std::max(2, h); if (rtView.w != w || rtView.h != h) MakeRT(rtView, w, h); }

    void FlyCam(float dt) {   // editor camera: hold right mouse in the viewport, WASD + QE, Shift = fast (Unity / Godot style)
        ImGuiIO& io = ImGui::GetIO();
        static bool flying = false;
        if (viewHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) flying = true;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) flying = false;
        if (!flying) return;
        edCam.yaw += io.MouseDelta.x * 0.0035f; edCam.pitch = std::max(-1.5f, std::min(1.5f, edCam.pitch - io.MouseDelta.y * 0.0035f));
        px::V3 f, r, u; edCam.basis(f, r, u); px::V3 mv{ 0, 0, 0 };
        if (ImGui::IsKeyDown(ImGuiKey_W)) mv = mv + f; if (ImGui::IsKeyDown(ImGuiKey_S)) mv = mv - f;
        if (ImGui::IsKeyDown(ImGuiKey_D)) mv = mv + r; if (ImGui::IsKeyDown(ImGuiKey_A)) mv = mv - r;
        if (ImGui::IsKeyDown(ImGuiKey_E)) mv = mv + px::V3{ 0, 1, 0 }; if (ImGui::IsKeyDown(ImGuiKey_Q)) mv = mv - px::V3{ 0, 1, 0 };
        float spd = ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 12.f : 4.f;
        if (px::length(mv) > 0) edCam.p = edCam.p + px::normalize(mv) * (spd * dt);
    }

    void DefaultLayout(ImGuiID dock) {
        ImGui::DockBuilderRemoveNode(dock); ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->WorkSize);
        ImGuiID left, right, bottom, center = dock;
        left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.17f, nullptr, &center);
        right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.27f, nullptr, &center);
        bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.34f, nullptr, &center);
        ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.45f, nullptr, &left);
        ImGui::DockBuilderDockWindow("Hierarchy", left);
        ImGui::DockBuilderDockWindow("Inventory", leftBottom);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Terrain", right);
        ImGui::DockBuilderDockWindow("Assets", bottom); ImGui::DockBuilderDockWindow("Animation", bottom); ImGui::DockBuilderDockWindow("Console", bottom);
        ImGui::DockBuilderDockWindow("Viewport", center);
        ImGui::DockBuilderFinish(dock);
    }

    void MenuBar() {
        if (!ImGui::BeginMainMenuBar()) return;
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New room")) NewRoom();
            if (ImGui::BeginMenu("Open room")) {
                auto rooms = ListRooms(); if (rooms.empty()) ImGui::TextDisabled("No rooms yet");
                for (auto& r : rooms) if (ImGui::MenuItem(FileName(r).c_str(), nullptr, roomOpen && r == roomPath)) OpenRoom(r);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Close room", nullptr, false, roomOpen)) CloseRoom();
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S")) { SaveMeta(); SaveRoom(); }
            if (ImGui::MenuItem("Open assets folder")) ShellExecuteA(nullptr, "open", assetDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            if (ImGui::MenuItem("Rescan assets")) FindAssets();
            ImGui::Separator(); if (ImGui::MenuItem("Exit")) PostQuitMessage(0);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !undoStack.empty())) Undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !redoStack.empty())) Redo();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Hierarchy", nullptr, &showHier); ImGui::MenuItem("Inspector", nullptr, &showInsp);
            ImGui::MenuItem("Assets", nullptr, &showAssets); ImGui::MenuItem("Animation", nullptr, &showAnim); ImGui::MenuItem("Console", nullptr, &showConsole);
            ImGui::MenuItem("Inventory", nullptr, &showInv);
            ImGui::MenuItem("Terrain", nullptr, &showTerrain);
            ImGui::MenuItem("Show viewmodel while editing", nullptr, &showViewmodel);
            if (ImGui::BeginMenu("UI size")) {
                for (float sc : { 1.f, 1.25f, 1.5f, 1.75f, 2.f }) { char b[16]; snprintf(b, sizeof b, "%d%%", (int)(sc * 100 + 0.5f)); if (ImGui::MenuItem(b, nullptr, std::fabs(uiScale - sc) < 0.01f)) { uiScale = sc; SaveEditorSettings(); } }
                ImGui::EndMenu();
            }
            ImGui::Separator(); if (ImGui::MenuItem("Reset layout")) firstLayout = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Game")) {
            if (ImGui::MenuItem(playing ? "Stop" : "Play", "F5")) playing ? StopPlay() : StartPlay();
            if (ImGui::MenuItem("Pause", "F6", pausedGame, playing)) pausedGame = !pausedGame;
            if (ImGui::MenuItem("Hide editor (game only)", "F11")) { editor = false; }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            ImGui::TextUnformatted("Viewport, editing: hold right mouse + WASD/QE to fly, Shift = fast");
            ImGui::TextUnformatted("Building a room: right-click places a block, left-click breaks it, Ctrl+Z / Ctrl+Y undo / redo");
            ImGui::TextUnformatted("Viewport, playing: click to control, WASD move, Space jump (rooms) or fire (demo), LMB fire, Esc release");
            ImGui::TextUnformatted("F5 play/stop, F6 pause, F11 hide editor, Ctrl+S save");
            ImGui::EndMenu();
        }
        // play controls in the middle of the bar
        float bw = 64; ImGui::SetCursorPosX(ImGui::GetWindowWidth() * 0.5f - bw * 1.5f);
        ImGui::PushStyleColor(ImGuiCol_Button, playing ? ImVec4(0.95f, 0.5f, 0.12f, 1) : ImGui::GetStyle().Colors[ImGuiCol_Button]);
        if (ImGui::Button(playing ? "Stop" : "Play", ImVec2(bw, 0))) playing ? StopPlay() : StartPlay();
        ImGui::PopStyleColor();
        ImGui::BeginDisabled(!playing); if (ImGui::Button(pausedGame ? "Resume" : "Pause", ImVec2(bw, 0))) pausedGame = !pausedGame; ImGui::EndDisabled();
        if (statusTimer > 0) { ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(status.c_str()).x - 16); ImGui::TextDisabled("%s", status.c_str()); }
        ImGui::EndMainMenuBar();
    }

    void HierarchyPanel() {
        if (!ImGui::Begin("Hierarchy", &showHier)) { ImGui::End(); return; }
        auto leaf = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        ImGui::TreeNodeEx("##room", leaf | (sel == Sel::World ? ImGuiTreeNodeFlags_Selected : 0), "%s", roomOpen ? ("Room: " + room.name + (roomDirty ? " *" : "")).c_str() : "Room (demo)"); if (ImGui::IsItemClicked()) sel = Sel::World;
        bool open = ImGui::TreeNodeEx("Player", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | (sel == Sel::Player ? ImGuiTreeNodeFlags_Selected : 0));
        if (ImGui::IsItemClicked()) sel = Sel::Player;
        if (open) {
            std::string label = modelLoaded ? "Arm cannon (" + FileName(modelPath) + ")" : "Arm cannon (built-in demo)";
            bool mopen = ImGui::TreeNodeEx("##cannon", ImGuiTreeNodeFlags_SpanAvailWidth | (sel == Sel::Model ? ImGuiTreeNodeFlags_Selected : 0) | (modelLoaded ? 0 : ImGuiTreeNodeFlags_Leaf), "%s", label.c_str());
            if (ImGui::IsItemClicked()) sel = Sel::Model;
            if (mopen) {
                if (modelLoaded) {
                    std::function<void(int)> tree = [&](int n) {
                        std::vector<int> kids; for (int i = 0; i < (int)model.nodes.size(); i++) if (model.nodes[i].parent == n) kids.push_back(i);
                        for (int k : kids) {
                            bool hasKids = false; for (auto& q : model.nodes) if (q.parent == k) { hasKids = true; break; }
                            ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_SpanAvailWidth | (hasKids ? 0 : ImGuiTreeNodeFlags_Leaf) | (sel == Sel::Node && selNode == k ? ImGuiTreeNodeFlags_Selected : 0);
                            bool o = ImGui::TreeNodeEx((void*)(intptr_t)(k + 1), fl, "%s", model.nodes[k].name.c_str());
                            if (ImGui::IsItemClicked()) { sel = Sel::Node; selNode = k; }
                            if (o) { tree(k); ImGui::TreePop(); }
                        }
                    };
                    tree(0);   // ufbx node 0 is the scene root
                }
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
        ImGui::End();
    }

    void InspectorPanel() {
        if (!ImGui::Begin("Inspector", &showInsp)) { ImGui::End(); return; }
        if (sel == Sel::World) {
            ImGui::SeparatorText("Room");
            if (roomOpen) {
                if (ImGui::InputText("Name", &room.name)) roomDirty = true;
                ImGui::Text("%zu blocks in %d x %d x %d", room.count(), room.sx, room.sy, room.sz);
                if (rsW == 0) { rsW = room.sx; rsH = room.sy; rsD = room.sz; }
                ImGui::SetNextItemWidth(200); ImGui::DragInt3("##resize", &rsW, 1.f, 8, px::Room::kMaxW); ImGui::SameLine();
                rsH = std::max(8, std::min(px::Room::kMaxH, rsH));
                ImGui::BeginDisabled(terrainPreview || (rsW == room.sx && rsH == room.sy && rsD == room.sz));
                if (ImGui::Button("Resize")) { room.resizeKeep(rsW, rsH, rsD); undoStack.clear(); redoStack.clear(); AfterRoomChange();
                    char b2[96]; snprintf(b2, sizeof b2, "Room is now %d x %d x %d (undo history cleared)", room.sx, room.sy, room.sz); SetStatus(b2); }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Width / height / depth in blocks. Blocks stay where they are; shrinking cuts off the edges.");
                ImGui::TextDisabled("%s", FileName(roomPath).c_str());
                if (ImGui::Button("Save room")) SaveRoom(); ImGui::SameLine(); if (ImGui::Button("Close room")) CloseRoom();
            } else {
                ImGui::TextWrapped("This is the built-in demo room. Make your own to build with blocks.");
                if (ImGui::Button("New room")) NewRoom();
            }
            ImGui::SeparatorText("Look (Wii-style rendering)");
            ImGui::Checkbox("Bloom", &S.bloomOn); ImGui::SameLine(); ImGui::SetNextItemWidth(-1); ImGui::SliderFloat("##bloom", &S.bloom, 0.f, 2.f);
            ImGui::SliderFloat("Haze", &S.fog, 0.f, 1.f);
            int ri = S.res == 240 ? 0 : S.res == 360 ? 1 : S.res == 480 ? 2 : S.res == 720 ? 3 : 4; const char* rs[] = { "240p", "360p", "480p (Wii)", "720p", "Native" };
            if (ImGui::Combo("Internal resolution", &ri, rs, 5)) { const int t[] = { 240, 360, 480, 720, 0 }; S.res = t[ri]; }
            ImGui::Checkbox("6-bit dither", &S.dither); ImGui::Checkbox("Sharp upscale", &S.nearest); ImGui::Checkbox("Dynamic lights", &S.dyn);
        } else if (sel == Sel::Player) {
            const px::Camera& c = cam;
            ImGui::SeparatorText("Player");
            ImGui::Text("Position  %.2f  %.2f  %.2f", c.p.x, c.p.y, c.p.z);
            ImGui::TextDisabled("Movement, morph ball and abilities arrive with the player editor (Phase 3).");
        } else if (sel == Sel::Model) {
            if (!modelLoaded) { ImGui::TextWrapped("Built-in demo cannon. Double-click an FBX in the Assets panel to use your own model."); ImGui::End(); return; }
            ImGui::Text("%s", FileName(modelPath).c_str()); ImGui::TextDisabled("Made with %s", model.source.c_str());
            bool ch = false;
            ImGui::SeparatorText("Transform (camera attachment)");
            ch |= ImGui::DragFloat3("Position", &meta.viewPos.x, 0.002f, -2.f, 2.f, "%.3f");
            ch |= ImGui::DragFloat3("Rotation", &meta.viewRotDeg.x, 0.25f, -180.f, 180.f, "%.1f");
            ch |= ImGui::DragFloat("Scale", &meta.viewScale, 0.005f, 0.01f, 20.f, "%.3f");
            ch |= ImGui::Checkbox("Auto-fit", &meta.autoFit);
            if (meta.autoFit) { ImGui::SameLine(); ImGui::SetNextItemWidth(-60); ch |= ImGui::DragFloat("Length", &meta.fitLength, 0.005f, 0.05f, 5.f, "%.2f m"); }
            ImGui::SeparatorText("Material");
            ch |= ImGui::SliderFloat("Shininess", &meta.reflect, 0.f, 1.f);
            ch |= ImGui::Checkbox("Pixel-art textures", &meta.pixelFilter);
            ImGui::SeparatorText("Import");
            ImGui::SetNextItemWidth(120); ImGui::DragFloat("Import scale (0 = auto)", &meta.importScale, 0.001f, 0.f, 10.f, "%.4f");
            if (ImGui::Button("Re-import with these settings")) { SaveMeta(); LoadModel(modelPath); }
            size_t tris = 0; for (auto& m : model.meshes) tris += m.i.size() / 3;
            ImGui::TextDisabled("%zu parts, %zu triangles, %zu textures, %zu animations", model.meshes.size(), tris, model.images.size(), model.clips.size());
            if (ch) Dirty();
        } else if (sel == Sel::Node && modelLoaded && selNode >= 0 && selNode < (int)model.nodes.size()) {
            const px::ModelNode& n = model.nodes[selNode];
            ImGui::Text("%s", n.name.c_str()); ImGui::TextDisabled("Part of %s", FileName(modelPath).c_str());
            ImGui::SeparatorText("Current pose");
            const px::TRS& t = anim.out[selNode]; px::V3 e = px::qlog(t.r);
            ImGui::Text("Position  %.3f  %.3f  %.3f", t.t.x, t.t.y, t.t.z);
            ImGui::Text("Rotation  %.1f  %.1f  %.1f (axis-angle, deg)", e.x * 57.2958f, e.y * 57.2958f, e.z * 57.2958f);
            int animated = 0; for (auto& c : model.clips) for (auto& tr : c.tracks) if (tr.node == selNode) { animated++; break; }
            ImGui::Text("Moved by %d of %zu animations", animated, model.clips.size());
            ImGui::TextDisabled("Parts come from your Blockbench groups and cubes.");
        } else if (sel == Sel::Asset && !selAsset.empty()) {
            ImGui::Text("%s", FileName(selAsset).c_str()); ImGui::TextDisabled("FBX model");
            if (ImGui::Button("Use as arm cannon")) LoadModel(selAsset);
            ImGui::TextWrapped("Tip: double-click it in the Assets panel.");
        } else ImGui::TextDisabled("Select something in the Hierarchy or Assets panel.");
        ImGui::End();
    }

    void AssetsPanel() {
        if (!ImGui::Begin("Assets", &showAssets)) { ImGui::End(); return; }
        if (ImGui::Button("Open folder")) ShellExecuteA(nullptr, "open", assetDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        ImGui::SameLine(); if (ImGui::Button("Rescan")) FindAssets();
        ImGui::SameLine(); ImGui::Checkbox("Auto re-import", &autoReload);
        ImGui::SameLine(); ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##filter", "Search", &assetFilter);
        ImGui::Separator();
        if (assetFiles.empty()) ImGui::TextWrapped("No models yet. In Blockbench: File > Export > Export FBX, save into the assets folder, then press Rescan.");
        float tile = 96, avail = ImGui::GetContentRegionAvail().x; int cols = std::max(1, (int)(avail / (tile + 8)));
        int col = 0;
        for (auto& f : assetFiles) {
            std::string name = FileName(f);
            if (!assetFilter.empty()) { std::string a = name, b = assetFilter; for (auto& c : a) c = (char)tolower(c); for (auto& c : b) c = (char)tolower(c); if (a.find(b) == std::string::npos) continue; }
            ImGui::PushID(f.c_str()); ImGui::BeginGroup();
            bool isSel = (sel == Sel::Asset && selAsset == f) || (f == modelPath);
            ImGui::PushStyleColor(ImGuiCol_Button, isSel ? ImVec4(0.95f, 0.5f, 0.12f, 0.8f) : ImVec4(0.08f, 0.22f, 0.25f, 1));
            if (ImGui::Button("FBX", ImVec2(tile, tile * 0.7f))) { sel = Sel::Asset; selAsset = f; }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) LoadModel(f);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nDouble-click: use as arm cannon", name.c_str());
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + tile); ImGui::TextWrapped("%s", name.c_str()); ImGui::PopTextWrapPos();
            ImGui::EndGroup(); ImGui::PopID();
            if (++col < cols) ImGui::SameLine(); else col = 0;
        }
        if (!loadError.empty()) { ImGui::NewLine(); ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", loadError.c_str()); }
        ImGui::End();
    }

    void AnimationPanel() {
        if (!ImGui::Begin("Animation", &showAnim)) { ImGui::End(); return; }
        if (!modelLoaded) { ImGui::TextWrapped("Load a model to see its animations."); ImGui::End(); return; }
        if (ImGui::BeginTable("anim", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("clips", ImGuiTableColumnFlags_WidthStretch, 0.62f); ImGui::TableSetupColumn("blend", ImGuiTableColumnFlags_WidthStretch, 0.38f);
            ImGui::TableNextRow(); ImGui::TableNextColumn();
            if (ImGui::BeginTable("clips", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 44); ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthFixed, 56); ImGui::TableSetupColumn("Loop", ImGuiTableColumnFlags_WidthFixed, 36);
                ImGui::TableSetupColumn("Speed", ImGuiTableColumnFlags_WidthFixed, 90); ImGui::TableSetupColumn("Start", ImGuiTableColumnFlags_WidthFixed, 40);
                ImGui::TableHeadersRow();
                for (int i = 0; i < (int)model.clips.size(); i++) {
                    ImGui::PushID(i); ImGui::TableNextRow();
                    ImGui::TableNextColumn(); if (ImGui::Button(anim.cur == i ? " >> " : "Play")) { anim.paused = false; anim.play(i, meta.blendTime); }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Right-click: play on top as a layer");
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) anim.playLayer(i);
                    ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1); if (ImGui::InputText("##name", &clipNames[i])) Dirty();
                    if (clipNames[i] != model.clips[i].name && ImGui::IsItemHovered()) ImGui::SetTooltip("In the file: %s", model.clips[i].name.c_str());
                    ImGui::TableNextColumn(); ImGui::Text("%.2f s", model.clips[i].duration);
                    ImGui::TableNextColumn(); if (ImGui::Checkbox("##loop", &clipSet[i].loop)) Dirty();
                    ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1); if (ImGui::SliderFloat("##speed", &clipSet[i].speed, 0.1f, 3.f, "%.2fx")) Dirty();
                    ImGui::TableNextColumn(); bool def = meta.defaultClip == model.clips[i].name;
                    if (ImGui::RadioButton("##def", def)) { meta.defaultClip = model.clips[i].name; Dirty(); }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play this clip when the model loads");
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (ImGui::Button(anim.paused ? "Resume" : "Pause", ImVec2(70, 0))) anim.paused = !anim.paused;
            ImGui::SameLine(); ImGui::SetNextItemWidth(-1);
            float len = anim.clipLength(); std::string lbl = anim.cur >= 0 ? clipNames[anim.cur] : std::string("-");
            for (size_t p = 0; (p = lbl.find('%', p)) != std::string::npos; p += 2) lbl.insert(p, "%");
            if (ImGui::SliderFloat("##time", &anim.time, 0.f, std::max(len, 0.001f), (lbl + "  %.2f s").c_str())) anim.paused = true;
            ImGui::TableNextColumn();
            ImGui::SeparatorText("Transitions");
            int mode = (int)anim.mode; const char* modes[] = { "Inertialization", "Crossfade", "Cut" };
            ImGui::SetNextItemWidth(-90); if (ImGui::Combo("Blend type", &mode, modes, 3)) { anim.mode = (px::BlendMode)mode; Dirty(); }
            ImGui::SetNextItemWidth(-90); if (ImGui::SliderFloat("Blend time", &meta.blendTime, 0.f, 1.f, "%.2f s")) Dirty();
            ImGui::BeginDisabled(anim.mode != px::BlendMode::Crossfade);
            int curve = (int)anim.curve; const char* curves[] = { "Linear", "Ease in", "Ease out", "Smooth" };
            ImGui::SetNextItemWidth(-90); if (ImGui::Combo("Curve", &curve, curves, 4)) { anim.curve = (px::BlendCurve)curve; Dirty(); }
            ImGui::EndDisabled();
            ImGui::TextWrapped("Press Play on another clip to watch the transition. Inertialization stays smooth even when you switch mid-transition.");
            ImGui::TextDisabled("Next: the state machine editor.");
            ImGui::EndTable();
        }
        ImGui::End();
    }

    void ConsolePanel() {
        if (!ImGui::Begin("Console", &showConsole)) { ImGui::End(); return; }
        if (ImGui::Button("Clear")) logLines.clear();
        ImGui::Separator(); ImGui::BeginChild("log");
        for (auto& l : logLines) {
            bool bad = l.find("failed") != std::string::npos || l.find("Could not") != std::string::npos;
            if (bad) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", l.c_str()); else ImGui::TextUnformatted(l.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.f);
        ImGui::EndChild(); ImGui::End();
    }

    void ViewportPanel() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        bool vis = ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        viewHovered = false;
        if (vis) {
            ImVec2 sz = ImGui::GetContentRegionAvail();
            if (sz.x >= 8 && sz.y >= 8) {
                EnsureViewRT((int)sz.x, (int)sz.y);
                ImVec2 p0 = ImGui::GetCursorScreenPos();
                ImGui::Image((ImTextureID)(intptr_t)rtView.srv.Get(), sz);
                viewHovered = ImGui::IsItemHovered(); viewMin = p0; viewMax = ImVec2(p0.x + sz.x, p0.y + sz.y);
                { px::V3 f, r, u; edCam.basis(f, r, u); px::V3 e = edCam.eye();
                  edView = XMMatrixLookToLH(XMVectorSet(e.x, e.y, e.z, 1), XMVectorSet(f.x, f.y, f.z, 0), XMVectorSet(u.x, u.y, u.z, 0));
                  edProj = XMMatrixPerspectiveFovLH(XMConvertToRadians(60.f), sz.x / sz.y, 0.1f, 80.f); }
                RoomEditing();
                if (viewHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !captured) { if (playing) Capture(true); else if (roomOpen && !terrainPreview) EnterBuild(); else if (terrainPreview) SetStatus("Apply or cancel the terrain preview to build"); }
                // overlay
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const char* txt = playing ? (captured ? "PLAYING  (Esc to release the mouse)" : "PLAYING  (click to control)")
                                          : roomOpen ? (captured ? "BUILDING  (E or Esc: mouse back to the editor, double-tap Space: fly on/off, Ctrl: sprint)" : "BUILDING  (click to step into the room)") : "EDITING  (hold right mouse + WASD to fly)";
                dl->AddRectFilled(ImVec2(p0.x + 8, p0.y + 8), ImVec2(p0.x + 16 + ImGui::CalcTextSize(txt).x, p0.y + 30), IM_COL32(0, 20, 24, 170), 4);
                dl->AddText(ImVec2(p0.x + 12, p0.y + 11), playing ? IM_COL32(255, 160, 50, 255) : IM_COL32(150, 240, 230, 255), txt);
                if (roomOpen && !playing) DrawHotbar(dl, p0, viewMax);
                if (playing || Building()) { ImVec2 c((p0.x + viewMax.x) * 0.5f, (p0.y + viewMax.y) * 0.5f); dl->AddLine(ImVec2(c.x - 7, c.y), ImVec2(c.x + 7, c.y), IM_COL32(127, 255, 212, 180), 2); dl->AddLine(ImVec2(c.x, c.y - 7), ImVec2(c.x, c.y + 7), IM_COL32(127, 255, 212, 180), 2); }
            }
        }
        ImGui::End();
    }

    void BuildUI() {
        ImGui::GetStyle().FontScaleMain = uiScale;
        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) { SaveMeta(); SaveRoom(); }
        if (!playing && !io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) Undo();
        if (!playing && !io.WantTextInput && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) Redo();
        if (!editor) {
            ImGui::SetNextWindowPos(ImVec2(10, 10)); ImGui::SetNextWindowBgAlpha(0.35f);
            ImGui::Begin("##hint", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);
            ImGui::TextUnformatted("F11: back to the editor   click: control   WASD move   LMB/Space fire   Esc release"); ImGui::End();
            return;
        }
        MenuBar();
        ImGuiID dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
        if (firstLayout) { firstLayout = false; ImGuiDockNode* n = ImGui::DockBuilderGetNode(dock); if (!n || n->IsLeafNode() || ImGui::GetIO().IniFilename == nullptr || !iniHadLayout) DefaultLayout(dock); iniHadLayout = false; }
        ViewportPanel();
        if (showHier) HierarchyPanel();
        if (showInsp) InspectorPanel();
        if (showAssets) AssetsPanel();
        if (showAnim) AnimationPanel();
        if (showConsole) ConsolePanel();
        if (showInv) InventoryPanel();
        if (showTerrain) TerrainPanel();
        NewRoomWindow();
        if (!playing && !roomOpen) FlyCam(io.DeltaTime);
    }
    bool iniHadLayout = false;

    // ------------------------------------------------------------------ rooms (block building)
    px::Room room; bool roomOpen = false, roomDirty = false, roomMeshDirty = false; std::string roomPath; float roomAutosave = 0;
    struct GpuSub { GpuMesh g; uint8_t tex = 0; float env = 0, glow = 0; };
    static constexpr int CS = 16;                       // chunk size: only edited chunks are rebuilt, only visible ones drawn
    struct Chunk { std::vector<GpuSub> subs; bool dirty = true; };
    std::vector<Chunk> chunks; int ncx = 0, ncy = 0, ncz = 0;
    bool newRoomPopup = false; int nrW = 128, nrH = 48, nrD = 128; bool nrFloor = true; uint8_t nrMat = px::MAT_METAL;
    int rsW = 0, rsH = 0, rsD = 0;
    struct BlockEdit { int x, y, z; px::Block before, after; };
    std::vector<std::vector<BlockEdit>> undoStack, redoStack;
    bool hoverValid = false; px::RayHit hover{};
    XMMATRIX edView = XMMatrixIdentity(), edProj = XMMatrixIdentity();
    px::V3 feet{ 0, 0, 0 }, pvel{ 0, 0, 0 }, spawnFeet{ 0, 0, 0 }; bool onGround = false;
    float uiScale = 1.f;

    std::string RoomsDir() { std::string d = assetDir + "rooms\\"; CreateDirectoryA(d.c_str(), nullptr); return d; }
    std::vector<std::string> ListRooms() {
        std::vector<std::string> out; WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((RoomsDir() + "*.room").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) { do { out.push_back(RoomsDir() + fd.cFileName); } while (FindNextFileA(h, &fd)); FindClose(h); }
        std::sort(out.begin(), out.end()); return out;
    }
    void MarkCell(int x, int y, int z) {   // a block change can affect neighbours' shading: mark every chunk touching its 3x3x3
        if (roomMeshDirty || chunks.empty()) return;
        for (int dz = -1; dz <= 1; dz++) for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            int cx = (x + dx - room.ox) / CS, cy = (y + dy - room.oy) / CS, cz = (z + dz - room.oz) / CS;
            if (x + dx < room.ox || y + dy < room.oy || z + dz < room.oz || cx >= ncx || cy >= ncy || cz >= ncz) continue;
            chunks[((size_t)cy * ncz + cz) * ncx + cx].dirty = true;
        }
    }
    template <class G> void AfterRoomChange(const G& g) { for (auto& e : g) MarkCell(e.x, e.y, e.z); roomDirty = true; roomAutosave = 0; }
    void AfterRoomChange() { roomMeshDirty = true; roomDirty = true; roomAutosave = 0; }
    void NewRoom() { newRoomPopup = true; }   // opens the "New room" window (size, floor)
    void CreateRoom(int w, int h, int d, bool floor, uint8_t mat) {
        TerrainCancel();
        if (roomOpen && roomDirty) SaveRoom();
        int n = 1; std::string path; do { path = RoomsDir() + "Room " + std::to_string(n++) + ".room"; } while (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES);
        room = px::Room{}; room.create(w, h, d, floor, mat); room.name = FileName(path).substr(0, FileName(path).size() - 5);
        roomPath = path; roomOpen = true; undoStack.clear(); redoStack.clear(); rsW = 0; AfterRoomChange(); SaveRoom();
        edCam.p = { 0, 6, -std::min(40.f, d * 0.3f) }; edCam.yaw = 0; edCam.pitch = -0.3f; sel = Sel::World;
        tp.x0 = -8; tp.z0 = -8; tp.x1 = 7; tp.z1 = 7;
        char b2[160]; snprintf(b2, sizeof b2, "New room: %s (%d x %d x %d). Click the viewport to step in", room.name.c_str(), room.sx, room.sy, room.sz); SetStatus(b2);
    }
    void NewRoomWindow() {
        if (newRoomPopup) { ImGui::OpenPopup("New room"); newRoomPopup = false; }
        ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("New room", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
        struct Pre { const char* name; int w, h, d; } pre[] = { { "Small  64 x 32 x 64", 64, 32, 64 }, { "Medium  128 x 48 x 128", 128, 48, 128 },
                                                             { "Large  256 x 64 x 256", 256, 64, 256 }, { "Huge  512 x 96 x 512", 512, 96, 512 } };
        for (auto& p : pre) { if (ImGui::RadioButton(p.name, nrW == p.w && nrH == p.h && nrD == p.d)) { nrW = p.w; nrH = p.h; nrD = p.d; } }
        ImGui::SeparatorText("Or your own size (blocks)");
        ImGui::SetNextItemWidth(260); ImGui::DragInt3("Width / height / depth", &nrW, 1.f, 8, px::Room::kMaxW);
        nrH = std::max(8, std::min(px::Room::kMaxH, nrH)); nrW = std::max(8, std::min(px::Room::kMaxW, nrW)); nrD = std::max(8, std::min(px::Room::kMaxW, nrD));
        ImGui::TextDisabled("Height is at most %d. 1 block = 1 metre.", px::Room::kMaxH);
        ImGui::Checkbox("Start with a floor", &nrFloor); if (nrFloor) { ImGui::SameLine(); ImGui::SetNextItemWidth(160); MatCombo("##floormat", nrMat); }
        double mb = (double)nrW * nrH * nrD * 4 / 1e6; if (mb > 60) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "Big room: about %.0f MB of memory, terrain takes a moment", mb);
        if (ImGui::Button("Create", ImVec2(120, 0))) { CreateRoom(nrW, nrH, nrD, nrFloor, nrMat); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine(); if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    void OpenRoom(const std::string& path) {
        TerrainCancel();
        if (roomOpen && roomDirty) SaveRoom();
        px::Room r; std::string err;
        if (!r.load(path, err)) { SetStatus("Could not open " + FileName(path) + ": " + err); return; }
        room = std::move(r); roomPath = path; roomOpen = true; roomDirty = false; roomMeshDirty = true; undoStack.clear(); redoStack.clear(); rsW = 0;
        SetStatus("Opened room " + room.name);
    }
    void SaveRoom() {
        if (!roomOpen) return;
        if (terrainPreview) { SetStatus("Apply or cancel the terrain preview first"); return; }
        if (room.save(roomPath)) { roomDirty = false; SetStatus("Saved room " + room.name); }
        else SetStatus("Could not save room " + FileName(roomPath));
    }
    void CloseRoom() { TerrainCancel(); if (roomDirty) SaveRoom(); roomOpen = false; chunks.clear(); SetStatus("Back to the demo room"); }
    void RebuildRoomMesh() {   // (re)build every dirty chunk
        if (roomMeshDirty) {
            roomMeshDirty = false;
            ncx = (room.sx + CS - 1) / CS; ncy = (room.sy + CS - 1) / CS; ncz = (room.sz + CS - 1) / CS;
            chunks.clear(); chunks.resize((size_t)ncx * ncy * ncz);
        }
        std::vector<px::SubMesh> subs;
        for (int cy = 0; cy < ncy; cy++) for (int cz = 0; cz < ncz; cz++) for (int cx = 0; cx < ncx; cx++) {
            Chunk& c = chunks[((size_t)cy * ncz + cz) * ncx + cx];
            if (!c.dirty) continue;
            c.dirty = false; c.subs.clear();
            int x0 = room.ox + cx * CS, y0 = room.oy + cy * CS, z0 = room.oz + cz * CS;
            room.buildMeshesBox(x0, y0, z0, x0 + CS, y0 + CS, z0 + CS, subs);
            for (auto& sm : subs) {
                if (sm.mesh.i.empty()) continue;
                GpuSub g; g.tex = sm.tex; g.env = sm.env; g.glow = sm.glow; g.g.count = (UINT)sm.mesh.i.size();
                D3D11_BUFFER_DESC d{}; d.Usage = D3D11_USAGE_IMMUTABLE; d.ByteWidth = (UINT)(sm.mesh.v.size() * sizeof(px::Vertex)); d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                D3D11_SUBRESOURCE_DATA sdt{ sm.mesh.v.data(), 0, 0 }; HR(dev->CreateBuffer(&d, &sdt, &g.g.vb));
                d.ByteWidth = (UINT)(sm.mesh.i.size() * 4); d.BindFlags = D3D11_BIND_INDEX_BUFFER; sdt.pSysMem = sm.mesh.i.data(); HR(dev->CreateBuffer(&d, &sdt, &g.g.ib));
                c.subs.push_back(std::move(g));
            }
        }
    }
    static bool BoxVisible(const XMMATRIX& vp, px::V3 mn, px::V3 mx) {   // all 8 corners outside one clip plane = hidden
        int out[6] = { 0, 0, 0, 0, 0, 0 };
        for (int i = 0; i < 8; i++) {
            XMVECTOR c = XMVector4Transform(XMVectorSet((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z, 1), vp);
            float x = XMVectorGetX(c), y = XMVectorGetY(c), z = XMVectorGetZ(c), w = XMVectorGetW(c);
            out[0] += x < -w; out[1] += x > w; out[2] += y < -w; out[3] += y > w; out[4] += z < 0; out[5] += z > w;
        }
        for (int k = 0; k < 6; k++) if (out[k] == 8) return false;
        return true;
    }
    void DrawRoom(const XMMATRIX& vp) {
        RebuildRoomMesh();
        ObjectCB o{}; XMStoreFloat4x4(&o.world, XMMatrixIdentity()); o.tint[0] = o.tint[1] = o.tint[2] = o.tint[3] = 1; o.params[2] = 1;
        float lastEnv = -1, lastGlow = -1;
        for (int cy = 0; cy < ncy; cy++) for (int cz = 0; cz < ncz; cz++) for (int cx = 0; cx < ncx; cx++) {
            Chunk& c = chunks[((size_t)cy * ncz + cz) * ncx + cx];
            if (c.subs.empty()) continue;
            px::V3 mn{ (float)(room.ox + cx * CS), (float)(room.oy + cy * CS), (float)(room.oz + cz * CS) };
            if (!BoxVisible(vp, mn, mn + px::V3{ (float)CS, (float)CS, (float)CS })) continue;
            for (auto& g : c.subs) {
                if (g.env != lastEnv || g.glow != lastGlow) { o.params[0] = g.env; o.params[1] = g.glow; ctx->UpdateSubresource(cbObject.Get(), 0, nullptr, &o, 0, 0); lastEnv = g.env; lastGlow = g.glow; }
                ID3D11ShaderResourceView* srv[2] = { texSRV[g.tex].Get(), texSRV[px::T_ENV].Get() }; ctx->PSSetShaderResources(0, 2, srv);
                UINT stride = sizeof(px::Vertex), off = 0; ctx->IASetVertexBuffers(0, 1, g.g.vb.GetAddressOf(), &stride, &off);
                ctx->IASetIndexBuffer(g.g.ib.Get(), DXGI_FORMAT_R32_UINT, 0); ctx->DrawIndexed(g.g.count, 0, 0);
            }
        }
    }
    void EditBlock(std::vector<BlockEdit>& group, int x, int y, int z, px::Block b) {
        if (!room.inBounds(x, y, z)) return;
        px::Block was = room.block(x, y, z); if (was == b) return;
        room.set(x, y, z, b); group.push_back({ x, y, z, was, room.block(x, y, z) });
    }
    void Commit(std::vector<BlockEdit>& g) {
        if (g.empty()) return;
        if (g.size() > 4096) AfterRoomChange(); else AfterRoomChange(g);
        undoStack.push_back(std::move(g)); if (undoStack.size() > 500) undoStack.erase(undoStack.begin());
        redoStack.clear();
    }
    void Undo() { if (undoStack.empty()) return; auto g = undoStack.back(); undoStack.pop_back(); for (auto it = g.rbegin(); it != g.rend(); ++it) room.set(it->x, it->y, it->z, it->before); if (g.size() > 4096) AfterRoomChange(); else AfterRoomChange(g); redoStack.push_back(g); }
    void Redo() { if (redoStack.empty()) return; auto g = redoStack.back(); redoStack.pop_back(); for (auto& e : g) room.set(e.x, e.y, e.z, e.after); if (g.size() > 4096) AfterRoomChange(); else AfterRoomChange(g); undoStack.push_back(g); }
    static bool SolidAt(void* c, px::V3 p) { App* a = (App*)c; return a->room.pointSolid(p) || p.y < a->room.oy - 30; }

    // player in a room: Samus is about 2 blocks tall
    static constexpr float kHalfW = 0.35f, kHeight = 1.9f, kEye = 1.75f, kGravity = 24.f, kJump = 8.f, kSpeed = 5.f;
    void RoomPlayerSpawn() {
        feet = { edCam.p.x, edCam.p.y - kEye, edCam.p.z };
        for (int i = 0; i < 64 && !room.boxFree(feet, kHalfW, kHeight); i++) feet.y += 1.f;
        pvel = { 0, 0, 0 }; spawnFeet = feet;
        cam = px::Camera{}; cam.yaw = edCam.yaw; cam.pitch = edCam.pitch; cam.p = { feet.x, feet.y + kEye, feet.z };
    }
    void RoomMove(float dt) {
        px::V3 f, r, u; cam.basis(f, r, u); px::V3 fw = px::normalize({ f.x, 0, f.z }), mv{ 0, 0, 0 };
        if (input.w) mv = mv + fw;
        if (input.s) mv = mv - fw;
        if (input.d) mv = mv + r;
        if (input.a) mv = mv - r;
        if (input.left) cam.yaw -= 1.8f * dt;
        if (input.right) cam.yaw += 1.8f * dt;
        bool moving = px::length(mv) > 0; if (moving) mv = px::normalize(mv) * kSpeed;
        pvel.x = mv.x; pvel.z = mv.z; pvel.y -= kGravity * dt; pvel.y = std::max(pvel.y, -40.f);
        if (input.jump && onGround) pvel.y = kJump;
        feet = room.moveBox(feet, pvel, kHalfW, kHeight, dt, onGround, 0.55f);
        if (feet.y < room.oy - 30) { feet = spawnFeet; pvel = { 0, 0, 0 }; SetStatus("Fell out of the room: back to the start"); }
        if (moving && onGround) cam.bob += dt * 9;
        cam.p = { feet.x, feet.y + kEye, feet.z }; cam.recoil = std::max(0.f, cam.recoil - dt * 0.5f);
    }

    // 3D -> viewport overlay drawing (edit highlights)
    void Line3D(ImDrawList* dl, px::V3 a, px::V3 b, ImU32 col, float th) {
        XMVECTOR va = XMVector3TransformCoord(XMVectorSet(a.x, a.y, a.z, 1), edView), vb = XMVector3TransformCoord(XMVectorSet(b.x, b.y, b.z, 1), edView);
        float za = XMVectorGetZ(va), zb = XMVectorGetZ(vb), n = 0.12f;
        if (za < n && zb < n) return;
        if (za < n) va = XMVectorLerp(va, vb, (n - za) / (zb - za)); else if (zb < n) vb = XMVectorLerp(vb, va, (n - zb) / (za - zb));
        auto scr = [&](XMVECTOR v) {
            XMVECTOR c = XMVector4Transform(XMVectorSetW(v, 1), edProj); float w = XMVectorGetW(c);
            float x = XMVectorGetX(c) / w, y = XMVectorGetY(c) / w;
            return ImVec2(viewMin.x + (x * 0.5f + 0.5f) * (viewMax.x - viewMin.x), viewMin.y + (0.5f - y * 0.5f) * (viewMax.y - viewMin.y));
        };
        dl->AddLine(scr(va), scr(vb), col, th);
    }
    void BoxLines(ImDrawList* dl, px::V3 mn, px::V3 mx, ImU32 col, float th) {
        px::V3 c[8]; for (int i = 0; i < 8; i++) c[i] = { (i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z };
        const int e[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
        for (auto& k : e) Line3D(dl, c[k[0]], c[k[1]], col, th);
    }
    // ------------------------------------------------------------------ creative mode (Minecraft-style building)
    struct Slot { uint8_t type, size, mat = px::MAT_METAL; };
    Slot hotbar[9] = { { px::B_CUBE, 8 }, { px::B_SLAB, 4 }, { px::B_STAIRS, 2 }, { px::B_SLOPE, 8 }, { px::B_CORNER, 8 },
                       { px::B_INNER, 8 }, { px::B_BEAM, 4 }, { px::B_PILLAR, 6 }, { px::B_PANEL, 2 } };
    int hotSel = 0, spinOffset = 0; bool flipPlace = false;
    px::Block PlannedBlock() const {   // what a right-click would place right now
        px::V3 f, r, u; edCam.basis(f, r, u);
        px::Block b; b.type = hotbar[hotSel].type; b.size = hotbar[hotSel].size; b.mat = hotbar[hotSel].mat;
        b.rot = px::chooseRot(b.type, hover.nx, hover.ny, hover.nz, f, spinOffset, flipPlace);
        return b;
    }
    px::V3 bFeet{ 0, 0, 0 }, bVel{ 0, 0, 0 }; bool bGround = false, bFly = true;
    float lastSpaceTap = -10.f, breakCool = 0, placeCool = 0; bool lHeld = false, rHeld = false, mHeld = false;
    static constexpr float kReach = 8.f;
    bool Building() const { return roomOpen && !playing && captured; }

    void EnterBuild() {
        bFeet = { edCam.p.x, edCam.p.y - kEye, edCam.p.z };
        for (int i = 0; i < 64 && !room.boxFree(bFeet, kHalfW, kHeight); i++) bFeet.y += 1.f;
        bVel = { 0, 0, 0 }; lHeld = rHeld = mHeld = true;   // the click that captured the mouse must not also break a block
        Capture(true);
    }
    void RoomEditing() {   // aim with the crosshair, draw the outline (called from the Viewport panel)
        hoverValid = false;
        if (!roomOpen || playing) return;
        px::V3 f, r, u; edCam.basis(f, r, u);
        hoverValid = room.raycast(edCam.eye(), f, kReach, hover);   // a block, or the room floor when it is empty there
        if (!hoverValid || !captured) return;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (!hover.onGround) BoxLines(dl, { hover.x - 0.003f, hover.y - 0.003f, hover.z - 0.003f }, { hover.x + 1.003f, hover.y + 1.003f, hover.z + 1.003f }, IM_COL32(10, 12, 14, 235), 2.f);
        if (hotbar[hotSel].type) {   // orange preview of the exact shape a right-click would place
            int x = hover.x + hover.nx, y = hover.y + hover.ny, z = hover.z + hover.nz;
            if (room.inBounds(x, y, z) && room.get(x, y, z) == px::B_AIR) {
                std::vector<px::Box3> bx; px::shapeBoxes(PlannedBlock(), x, y, z, bx);
                for (auto& q : bx) BoxLines(dl, q.mn, q.mx, IM_COL32(255, 160, 50, 170), 1.f);
            }
        }
    }
    bool OverlapsBuilder(const px::Block& b, int x, int y, int z) const {
        std::vector<px::Box3> bx; px::shapeBoxes(b, x, y, z, bx);
        for (auto& q : bx) if (bFeet.x + kHalfW > q.mn.x && bFeet.x - kHalfW < q.mx.x && bFeet.z + kHalfW > q.mn.z && bFeet.z - kHalfW < q.mx.z && bFeet.y + kHeight > q.mn.y && bFeet.y < q.mx.y) return true;
        return false;
    }
    void BuilderTick(float dt) {
        if (!Building()) { lHeld = rHeld = mHeld = true; return; }
        auto down = [](int k) { return (GetAsyncKeyState(k) & 0x8000) != 0; };
        // ---- movement: walk with gravity, or creative flight (double-tap Space)
        px::V3 f, r, u; edCam.basis(f, r, u); px::V3 fw = px::normalize({ f.x, 0, f.z }), mv{ 0, 0, 0 };
        if (down('W')) mv = mv + fw;
        if (down('S')) mv = mv - fw;
        if (down('D')) mv = mv + r;
        if (down('A')) mv = mv - r;
        bool sprint = down(VK_CONTROL), sneak = down(VK_SHIFT);
        float spd = bFly ? (sprint ? 22.f : 11.f) : (sprint ? 6.5f : sneak ? 1.6f : 4.6f);
        bool moving = px::length(mv) > 0; if (moving) mv = px::normalize(mv) * spd;
        bVel.x = mv.x; bVel.z = mv.z;
        if (bFly) { bVel.y = (down(VK_SPACE) ? spd * 0.8f : 0.f) - (sneak ? spd * 0.8f : 0.f); }
        else { bVel.y -= kGravity * dt; bVel.y = std::max(bVel.y, -40.f); if (down(VK_SPACE) && bGround) bVel.y = kJump; }
        bFeet = room.moveBox(bFeet, bVel, kHalfW, kHeight, dt, bGround, bFly ? 0.f : 0.55f);
        if (bFly && bGround && sneak) { bFly = false; SetStatus("Flying off (landed)"); }
        if (bFeet.y < room.oy - 30) { bFeet.y = (float)(room.oy + room.sy); bFly = true; }
        if (moving && bGround && !bFly) edCam.bob += dt * 9;
        edCam.p = { bFeet.x, bFeet.y + kEye, bFeet.z };

        // ---- blocks: left breaks, right places, middle picks; holding repeats like Minecraft
        bool L = down(VK_LBUTTON), R = down(VK_RBUTTON), M = down(VK_MBUTTON);
        breakCool -= dt; placeCool -= dt;
        if (L && (!lHeld || breakCool <= 0) && hoverValid && !hover.onGround) {
            std::vector<BlockEdit> g; EditBlock(g, hover.x, hover.y, hover.z, px::Block{}); Commit(g); breakCool = 0.22f;
        }
        if (R && (!rHeld || placeCool <= 0) && hoverValid && hotbar[hotSel].type != px::B_AIR) {
            int x = hover.x + hover.nx, y = hover.y + hover.ny, z = hover.z + hover.nz;
            px::Block nb = PlannedBlock();
            if (room.get(x, y, z) == px::B_AIR && !OverlapsBuilder(nb, x, y, z)) { std::vector<BlockEdit> g; EditBlock(g, x, y, z, nb); Commit(g); }
            placeCool = 0.22f;
        }
        if (M && !mHeld && hoverValid && !hover.onGround) {
            px::Block b = room.block(hover.x, hover.y, hover.z); int found = -1;
            for (int i = 0; i < 9; i++) if (hotbar[i].type == b.type && hotbar[i].size == b.size && hotbar[i].mat == b.mat) { found = i; break; }
            if (found >= 0) hotSel = found; else hotbar[hotSel] = { b.type, b.size, b.mat };
        }
        if (!L) breakCool = 0;
        if (!R) placeCool = 0;
        lHeld = L; rHeld = R; mHeld = M;
    }
    void OnSpace() {   // double-tap Space toggles flight
        float now = (float)ImGui::GetTime();
        if (now - lastSpaceTap < 0.3f) { bFly = !bFly; bVel.y = 0; SetStatus(bFly ? "Flying" : "Walking"); lastSpaceTap = -10.f; }
        else lastSpaceTap = now;
    }
    static const char* BlockName(uint8_t b) { return b ? px::shapeInfo(b).name : ""; }
    std::string SlotLabel(const Slot& sl) const {
        const px::ShapeInfo& si = px::shapeInfo(sl.type); std::string s = std::string(px::materialInfo(sl.mat).name) + " " + si.name;
        if (si.mn != si.mx) { char b[64]; if (si.sizeDiv) snprintf(b, sizeof b, "  %s %d/%d", si.sizeWhat, sl.size, si.sizeDiv); else snprintf(b, sizeof b, "  %d %s", sl.size, si.sizeWhat); s += b; }
        return s;
    }
    void ChangeSize(int d) {
        Slot& sl = hotbar[hotSel]; const px::ShapeInfo& si = px::shapeInfo(sl.type);
        int v = std::max((int)si.mn, std::min((int)si.mx, sl.size + d)); sl.size = (uint8_t)v;
    }
    void DrawHotbar(ImDrawList* dl, ImVec2 p0, ImVec2 p1) {
        float s = 44.f * uiScale, gap = 4.f, W = 9 * s + 8 * gap; ImVec2 o((p0.x + p1.x - W) * 0.5f, p1.y - s - 14.f);
        dl->AddRectFilled(ImVec2(o.x - 6, o.y - 6), ImVec2(o.x + W + 6, o.y + s + 6), IM_COL32(0, 18, 22, 170), 6);
        for (int i = 0; i < 9; i++) {
            ImVec2 a(o.x + i * (s + gap), o.y), b(a.x + s, a.y + s);
            dl->AddRectFilled(a, b, hotbar[i].type ? IM_COL32(52, 62, 68, 230) : IM_COL32(20, 40, 46, 200), 4);
            if (hotbar[i].type) {   // tiny side-view icon of the shape
                std::vector<px::Box3> bx; px::Block ib; ib.type = hotbar[i].type; ib.size = hotbar[i].size; ib.rot = px::makeRot(2, 1); px::shapeBoxes(ib, 0, 0, 0, bx);
                float pad = 8, sz = s - 2 * pad;
                const float* sw = px::materialInfo(hotbar[i].mat).swatch; ImU32 col = IM_COL32((int)(sw[0] * 255), (int)(sw[1] * 255), (int)(sw[2] * 255), 255);
                for (auto& q : bx) dl->AddRectFilled(ImVec2(a.x + pad + q.mn.z * sz, b.y - pad - q.mx.y * sz), ImVec2(a.x + pad + q.mx.z * sz, b.y - pad - q.mn.y * sz), col);
            }
            dl->AddRect(a, b, i == hotSel ? IM_COL32(255, 170, 60, 255) : IM_COL32(40, 120, 125, 200), 4.f, 0, i == hotSel ? 3.f : 1.f);
            char n[2] = { (char)('1' + i), 0 }; dl->AddText(ImVec2(a.x + 4, a.y + 2), IM_COL32(180, 230, 225, 200), n);
        }
        if (hotbar[hotSel].type) {
            std::string nm = SlotLabel(hotbar[hotSel]);
            ImVec2 ts = ImGui::CalcTextSize(nm.c_str()); dl->AddText(ImVec2((p0.x + p1.x - ts.x) * 0.5f, o.y - 40), IM_COL32(230, 250, 245, 255), nm.c_str());
            const char* keys = "R rotate   F flip   + / - size   middle-click pick";
            ImVec2 ks = ImGui::CalcTextSize(keys); dl->AddText(ImVec2((p0.x + p1.x - ks.x) * 0.5f, o.y - 22), IM_COL32(150, 200, 195, 200), keys);
        }
    }

    void InventoryPanel() {
        if (!ImGui::Begin("Inventory", &showInv)) { ImGui::End(); return; }
        if (ImGui::BeginTabBar("inv")) {
            if (ImGui::BeginTabItem("Blocks")) {
                float avail = ImGui::GetContentRegionAvail().x; int cols = std::max(1, (int)(avail / 84.f)), col = 0;
                for (int t = px::B_CUBE; t < px::B_COUNT; t++) {
                    const px::ShapeInfo& si = px::shapeInfo((uint8_t)t);
                    bool on = hotbar[hotSel].type == t;
                    ImGui::PushID(t);
                    ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(0.95f, 0.5f, 0.12f, 0.85f) : ImVec4(0.08f, 0.22f, 0.25f, 1));
                    if (ImGui::Button(si.name, ImVec2(78, 46))) hotbar[hotSel] = { (uint8_t)t, si.def, hotbar[hotSel].type ? hotbar[hotSel].mat : (uint8_t)px::MAT_METAL };
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(si.mn != si.mx ? "%s\nSize changes the %s (+ / - while building)" : "%s", si.name, si.sizeWhat);
                    ImGui::PopID();
                    if (++col < cols) ImGui::SameLine(); else col = 0;
                }
                if (col) ImGui::NewLine();
                if (hotbar[hotSel].type) {
                    const px::ShapeInfo& si = px::shapeInfo(hotbar[hotSel].type);
                    ImGui::Text("Slot %d: %s", hotSel + 1, SlotLabel(hotbar[hotSel]).c_str());
                    if (si.mn != si.mx) { int v = hotbar[hotSel].size; ImGui::SetNextItemWidth(-1); if (ImGui::SliderInt("##size", &v, si.mn, si.mx, si.sizeWhat)) hotbar[hotSel].size = (uint8_t)v; }
                    if (ImGui::Button("Clear slot")) hotbar[hotSel] = { 0, 8, 0 };
                    ImGui::SeparatorText("Material");
                    int mc = 0;
                    for (int m = 0; m < px::MAT_COUNT; m++) {
                        const px::MaterialInfo& mi = px::materialInfo((uint8_t)m); ImGui::PushID(100 + m);
                        bool on = hotbar[hotSel].mat == m;
                        if (on) ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 0.65f, 0.2f, 1));
                        if (ImGui::ColorButton(mi.name, ImVec4(mi.swatch[0], mi.swatch[1], mi.swatch[2], 1), on ? 0 : ImGuiColorEditFlags_NoBorder, ImVec2(34, 34))) hotbar[hotSel].mat = (uint8_t)m;
                        if (on) ImGui::PopStyleColor();
                        ImGui::PopID(); if (++mc % 6) ImGui::SameLine();
                    }
                    if (mc % 6) ImGui::NewLine();
                    ImGui::Text("%s", px::materialInfo(hotbar[hotSel].mat).name);
                }
                ImGui::Separator();
                if (!roomOpen) { ImGui::TextWrapped("Open or make a room to build: File > New room."); if (ImGui::Button("New room")) NewRoom(); }
                else ImGui::TextDisabled("Click a shape to put it in hotbar slot %d.\nClick the viewport to build: right-click place,\nleft-click break, R rotate, F flip, +/- size,\ndouble-tap Space to fly, E for the mouse.", hotSel + 1);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Objects")) { ImGui::TextWrapped("Your FBX models, doors, pickups and spawns go here (room editor step 6)."); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Connectors")) { ImGui::TextWrapped("Switches, elevators, zones and wiring (Phase 4)."); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Curves")) { ImGui::TextWrapped("Morph ball tunnels, pipes and cables drawn along a path."); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }
    bool showInv = true;

    void LoadEditorSettings() {
        std::ifstream f(ExeDir() + "editor_settings.txt"); std::string k;
        while (f >> k) { if (k == "ui_scale") f >> uiScale; else if (k == "last_room") { f >> std::ws; std::getline(f, lastRoom); } }
        uiScale = std::max(0.75f, std::min(2.5f, uiScale));
    }
    void SaveEditorSettings() { std::ofstream f(ExeDir() + "editor_settings.txt"); f << "ui_scale " << uiScale << "\nlast_room " << (roomOpen ? roomPath : std::string()) << "\n"; }
    std::string lastRoom;

    // ------------------------------------------------------------------ terrain generator panel
    px::TerrainParams tp; bool terrainPreview = false, showTerrain = true; px::Room terrainBackup;
    void TerrainRegenerate() {
        room = terrainBackup; px::generateTerrain(room, tp); roomMeshDirty = true;
    }
    void TerrainStartPreview() { if (!terrainPreview) { terrainBackup = room; terrainPreview = true; } TerrainRegenerate(); }
    void TerrainApply() {
        if (!terrainPreview) return;
        std::vector<BlockEdit> g;
        for (int y = room.oy; y < room.oy + room.sy; y++) for (int z = room.oz; z < room.oz + room.sz; z++) for (int x = room.ox; x < room.ox + room.sx; x++) {
            px::Block a = terrainBackup.block(x, y, z), b = room.block(x, y, z);
            if (!(a == b)) g.push_back({ x, y, z, a, b });
        }
        terrainPreview = false; terrainBackup = px::Room{};
        size_t n = g.size(); Commit(g); SetStatus("Terrain applied: " + std::to_string(n) + " blocks changed (Ctrl+Z undoes it)");
    }
    void TerrainCancel() { if (!terrainPreview) return; room = terrainBackup; terrainBackup = px::Room{}; terrainPreview = false; roomMeshDirty = true; SetStatus("Terrain preview cancelled"); }
    bool MatCombo(const char* label, uint8_t& m) {
        bool ch = false;
        if (ImGui::BeginCombo(label, px::materialInfo(m).name)) {
            for (int i = 0; i < px::MAT_COUNT; i++) {
                const float* c = px::materialInfo((uint8_t)i).swatch;
                ImGui::ColorButton("##sw", ImVec4(c[0], c[1], c[2], 1), ImGuiColorEditFlags_NoTooltip, ImVec2(14, 14)); ImGui::SameLine();
                if (ImGui::Selectable(px::materialInfo((uint8_t)i).name, m == i)) { m = (uint8_t)i; ch = true; }
            }
            ImGui::EndCombo();
        }
        return ch;
    }
    void TerrainPanel() {
        if (!ImGui::Begin("Terrain", &showTerrain)) { ImGui::End(); return; }
        if (!roomOpen) { ImGui::TextWrapped("Make or open a room first (File > New room)."); if (ImGui::Button("New room")) NewRoom(); ImGui::End(); return; }
        bool ch = false;
        int kind = tp.kind;
        if (ImGui::Combo("Preset", &kind, px::kTerrainNames, px::TK_COUNT)) { px::terrainPreset(kind, tp); ch = true; }
        int seed = (int)tp.seed; ImGui::SetNextItemWidth(120); if (ImGui::InputInt("Seed", &seed)) { tp.seed = (uint32_t)std::max(0, seed); ch = true; }
        ImGui::SameLine(); if (ImGui::Button("Shuffle")) { tp.seed = (uint32_t)(GetTickCount() * 2654435761u) % 100000; ch = true; }
        if (ImGui::CollapsingHeader("Shape", ImGuiTreeNodeFlags_DefaultOpen)) {
            ch |= ImGui::SliderFloat("Ground height", &tp.base, 0.f, (float)room.sy - 4, "%.0f blocks");
            ch |= ImGui::SliderFloat("Hills height", &tp.height, 0.f, 24.f, "%.0f blocks");
            ch |= ImGui::SliderFloat("Feature size", &tp.scale, 4.f, 64.f, "%.0f blocks");
            ch |= ImGui::SliderFloat("Roughness", &tp.rough, 0.2f, 0.8f);
            ch |= ImGui::SliderFloat("Cliffs", &tp.cliffs, 0.f, 1.f);
            ch |= ImGui::SliderFloat("Overhangs", &tp.overhang, 0.f, 1.f);
            if (tp.kind == px::TK_CANYON || tp.kind == px::TK_CRATER) {
                ch |= ImGui::SliderFloat(tp.kind == px::TK_CANYON ? "Canyon depth" : "Crater depth", &tp.featDepth, 2.f, 20.f, "%.0f blocks");
                ch |= ImGui::SliderFloat(tp.kind == px::TK_CANYON ? "Canyon width" : "Crater radius", &tp.featSize, 3.f, 30.f, "%.0f blocks");
            }
            ch |= ImGui::Checkbox("Smooth edges with slopes", &tp.smooth);
        }
        if (ImGui::CollapsingHeader("Caves", ImGuiTreeNodeFlags_DefaultOpen)) {
            ch |= ImGui::SliderInt("Caves", &tp.caves, 0, 20);
            ch |= ImGui::SliderFloat("Cave size", &tp.caveSize, 1.f, 5.f, "%.1f");
            ch |= ImGui::SliderInt("Morph ball tunnels", &tp.tunnels, 0, 12);
            ch |= ImGui::Checkbox("Ceiling (closed cavern)", &tp.ceiling);
            if (tp.ceiling) ch |= ImGui::SliderFloat("Ceiling height", &tp.ceilHeight, 6.f, (float)room.sy - 2, "%.0f blocks");
            bool lava = tp.fluidLevel >= 0; if (ImGui::Checkbox("Lava pools", &lava)) { tp.fluidLevel = lava ? 3.f : -1.f; ch = true; }
            if (lava) ch |= ImGui::SliderFloat("Lava level", &tp.fluidLevel, 1.f, 16.f, "%.0f blocks");
        }
        if (ImGui::CollapsingHeader("Layers", ImGuiTreeNodeFlags_DefaultOpen)) {
            ch |= MatCombo("Top", tp.top); ch |= MatCombo("Below the top", tp.mid);
            ch |= ImGui::SliderInt("Layer depth", &tp.midDepth, 0, 8, "%d blocks"); ch |= MatCombo("Deep rock", tp.deep);
        }
        if (ImGui::CollapsingHeader("Extras")) {
            ch |= ImGui::SliderInt("Ice spikes", &tp.spikes, 0, 40);
            ch |= ImGui::SliderInt("Chozo ruins", &tp.ruins, 0, 30);
        }
        if (ImGui::CollapsingHeader("Area")) {
            ch |= ImGui::Checkbox("Whole room", &tp.wholeRoom);
            if (!tp.wholeRoom) {
                ch |= ImGui::DragInt2("From (x, z)", &tp.x0, 0.2f, room.ox, room.ox + room.sx - 1);
                ch |= ImGui::DragInt2("To (x, z)", &tp.x1, 0.2f, room.ox, room.ox + room.sx - 1);
                if (ImGui::Button("Around me (16 blocks)")) { int cx = (int)std::floor(edCam.p.x), cz = (int)std::floor(edCam.p.z); tp.x0 = cx - 8; tp.z0 = cz - 8; tp.x1 = cx + 7; tp.z1 = cz + 7; ch = true; }
                ImGui::TextDisabled("Everything inside the box is replaced, top to bottom.");
            }
        }
        ImGui::Separator();
        if (!terrainPreview) {
            if (ImGui::Button("Preview", ImVec2(-1, 30))) TerrainStartPreview();
            ImGui::TextDisabled("Shows the result in the viewport. Nothing is kept until you press Apply.");
        } else {
            if (ch) TerrainRegenerate();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.95f, 0.5f, 0.12f, 1));
            if (ImGui::Button("Apply", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f - 4, 30))) TerrainApply();
            ImGui::PopStyleColor(); ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(-1, 30))) TerrainCancel();
            ImGui::TextDisabled("Previewing: change the sliders and watch the room update.");
        }
        ImGui::End();
    }

    // ------------------------------------------------------------------ input / loop
    // mouse capture for playing: cursor hidden, locked to the game view, recentred every frame
    POINT ViewCenter() {
        POINT c;
        if (editor) { c.x = (LONG)((viewMin.x + viewMax.x) * 0.5f); c.y = (LONG)((viewMin.y + viewMax.y) * 0.5f); }
        else { RECT r; GetClientRect(hwnd, &r); c.x = r.right / 2; c.y = r.bottom / 2; }
        ClientToScreen(hwnd, &c); return c;
    }
    void Capture(bool on) {
        if (captured == on) return;
        captured = on; ShowCursor(on ? FALSE : TRUE);
        ImGuiIO& io = ImGui::GetIO();
        if (on) { io.ConfigFlags |= ImGuiConfigFlags_NoMouse; RECT r; GetClientRect(hwnd, &r); POINT a{ 0, 0 }; ClientToScreen(hwnd, &a); RECT cr{ a.x, a.y, a.x + r.right, a.y + r.bottom }; ClipCursor(&cr); Recenter(); }
        else { io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse; ClipCursor(nullptr); }
    }
    void Recenter() { POINT c = ViewCenter(); SetCursorPos(c.x, c.y); }
    void MouseLook() {
        if (!captured) return;
        POINT c = ViewCenter(), m; GetCursorPos(&m);
        px::Camera& C = playing ? cam : edCam;
        C.yaw += (m.x - c.x) * 0.0025f; C.pitch = std::max(-1.5f, std::min(1.5f, C.pitch - (m.y - c.y) * 0.0025f)); SetCursorPos(c.x, c.y);
    }
    void PollKeys() {
        input = px::Input{};
        if (!playing || pausedGame || !captured) return;      // the player only moves while you control it
        auto down = [](int k) { return (GetAsyncKeyState(k) & 0x8000) != 0; };
        input.w = down('W'); input.a = down('A'); input.s = down('S'); input.d = down('D');
        input.left = down(VK_LEFT); input.right = down(VK_RIGHT); input.up = down(VK_UP); input.down = down(VK_DOWN);
        input.jump = roomOpen && down(VK_SPACE);
    }
    void OnKey(WPARAM k) {
        if (k == VK_F5) { playing ? StopPlay() : StartPlay(); return; }
        if (k == VK_F6) { if (playing) pausedGame = !pausedGame; return; }
        if (k == VK_F11) { editor = !editor; if (captured) Capture(false); return; }
        if (k == VK_ESCAPE) { if (captured) Capture(false); return; }
        if (Building()) {
            if (k == 'E') { Capture(false); return; }
            if (k >= '1' && k <= '9') { hotSel = (int)(k - '1'); return; }
            if (k == VK_SPACE) { OnSpace(); return; }
            if (k == 'R') { spinOffset = (spinOffset + 1) & 3; return; }
            if (k == 'F') { flipPlace = !flipPlace; SetStatus(flipPlace ? "Placing flipped" : "Placing normal"); return; }
            if (k == VK_OEM_PLUS || k == VK_ADD) { ChangeSize(+1); return; }
            if (k == VK_OEM_MINUS || k == VK_SUBTRACT) { ChangeSize(-1); return; }
        }
        if (k == VK_SPACE && playing && captured && !pausedGame && !roomOpen) Fire();
    }
};

static App* g_app = nullptr;
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return 1;
    switch (m) {
    case WM_SIZE: if (g_app && w != SIZE_MINIMIZED) { g_app->cw = std::max<int>(2, LOWORD(l)); g_app->ch = std::max<int>(2, HIWORD(l)); g_app->resized = true; } return 0;
    case WM_LBUTTONDOWN:
        if (g_app) {
            if (g_app->captured) { if (g_app->playing && !g_app->pausedGame) g_app->Fire(); }
            else if (!g_app->editor && g_app->playing) g_app->Capture(true);   // game-only view (F11)
            else if (!g_app->editor && !g_app->playing) { g_app->StartPlay(); g_app->Capture(true); }
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (g_app && g_app->Building()) { int d = GET_WHEEL_DELTA_WPARAM(w) > 0 ? -1 : 1; g_app->hotSel = (g_app->hotSel + d + 9) % 9; return 0; }
        break;
    case WM_KEYDOWN: case WM_SYSKEYDOWN: if (g_app && !(l & (1 << 30))) g_app->OnKey(w); if (m == WM_SYSKEYDOWN) break; return 0;
    case WM_KILLFOCUS: if (g_app && g_app->captured) g_app->Capture(false); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int) {
    SetProcessDPIAware();
    App app; g_app = &app;
    WNDCLASSA wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = "PrimeStyleWnd";
    RegisterClassA(&wc);
    RECT rc{ 0, 0, 1280, 720 }; AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    app.hwnd = CreateWindowA(wc.lpszClassName, "PRIMEngine", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hi, nullptr);
    RECT cr; GetClientRect(app.hwnd, &cr); app.cw = cr.right; app.ch = cr.bottom;
    app.InitDevice();

    LARGE_INTEGER fq, t0; QueryPerformanceFrequency(&fq); QueryPerformanceCounter(&t0);
    double last = 0, fpsAcc = 0; int fpsN = 0;
    for (bool run = true; run;) {
        MSG msg; while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) { if (msg.message == WM_QUIT) run = false; TranslateMessage(&msg); DispatchMessageA(&msg); }
        if (!run) break;
        LARGE_INTEGER now; QueryPerformanceCounter(&now); double t = (double)(now.QuadPart - t0.QuadPart) / fq.QuadPart;
        float dt = (float)std::min(0.05, t - last); last = t; app.T += dt;

        app.PollKeys(); app.MouseLook();
        if (app.playing && !app.pausedGame) { if (app.roomOpen) app.RoomMove(dt); else app.cam.move(app.scene, app.input, dt); app.sim.update(dt); }
        app.BuilderTick(dt); app.Tick(dt);
        app.BuildUI();
        app.Render();

        fpsAcc += dt; fpsN++;
        if (fpsAcc > 0.5) {
            char title[256]; snprintf(title, sizeof title, "PRIMEngine | %s | %.0f fps", app.roomOpen ? app.room.name.c_str() : "demo room", fpsN / fpsAcc);
            SetWindowTextA(app.hwnd, title); fpsAcc = 0; fpsN = 0;
        }
    }
    if (app.roomDirty) app.SaveRoom();
    app.SaveMeta(); app.SaveEditorSettings();
    ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    return 0;
}
