// main_d3d11.cpp : standalone Windows renderer, Direct3D 11, no web stack.
// Same pipeline as web/index.html (WebGL2 test harness):
//   scene -> low-res RGBA8 target (480p "EFB") -> bright-pass/blur chain -> composite (bloom, grade, vignette, 6-bit dither) -> window
//
// Build: see CMakeLists.txt (needs the Windows SDK; no third-party libraries).
#include "scene.h"   // keep before windows.h

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>

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
    ComPtr<ID3D11SamplerState> sampWrap, sampClamp, sampPoint;

    px::Scene scene; GpuMesh meshes[px::M_COUNT];
    ComPtr<ID3D11ShaderResourceView> texSRV[px::T_COUNT];
    GpuMesh partMesh;

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
    }

    ComPtr<ID3D11ShaderResourceView> MakeTexture(const std::vector<uint8_t>& pixels, bool mips) {
        D3D11_TEXTURE2D_DESC d{}; d.Width = d.Height = px::TN; d.MipLevels = mips ? 0 : 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE | (mips ? D3D11_BIND_RENDER_TARGET : 0); d.MiscFlags = mips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
        ComPtr<ID3D11Texture2D> t;
        if (mips) { HR(dev->CreateTexture2D(&d, nullptr, &t)); ctx->UpdateSubresource(t.Get(), 0, nullptr, pixels.data(), px::TN * 4, 0); }
        else { D3D11_SUBRESOURCE_DATA sd{ pixels.data(), px::TN * 4, 0 }; HR(dev->CreateTexture2D(&d, &sd, &t)); }
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
        XMVECTOR m = XMVector3TransformCoord(XMVectorSet(0, 0, 0.92f, 1), CannonLocal() * CamWorld(cam.p, r, u, f));
        px::V3 mz{ XMVectorGetX(m), XMVectorGetY(m), XMVectorGetZ(m) };
        px::V3 aim = px::normalize(cam.p + f * 30.f - mz);
        sim.fire(mz, aim); cam.recoil = 0.07f;
    }
    void SetFrame(const XMMATRIX& view, const XMMATRIX& vp, px::V3 eye, const float lp[4][4], const float lc[4][4]) {
        FrameCB f{}; XMStoreFloat4x4(&f.viewProj, vp); XMStoreFloat4x4(&f.view, view);
        f.cam[0] = eye.x; f.cam[1] = eye.y; f.cam[2] = eye.z; f.cam[3] = T;
        memcpy(f.lightPos, lp, sizeof f.lightPos); memcpy(f.lightCol, lc, sizeof f.lightCol);
        f.fogColor[0] = 0.045f; f.fogColor[1] = 0.085f; f.fogColor[2] = 0.1f; f.fogColor[3] = S.fog; f.fogRange[0] = 6; f.fogRange[1] = 42;
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
        ctx->OMSetRenderTargets(1, dst ? dst->rtv.GetAddressOf() : backRTV.GetAddressOf(), nullptr);
        SetViewport(dst ? dst->w : cw, dst ? dst->h : ch);
        ctx->UpdateSubresource(cbPost.Get(), 0, nullptr, &cbv, 0, 0);
        ID3D11ShaderResourceView* srv[3] = { s0, s1, s2 }; ctx->PSSetShaderResources(0, 3, srv);
        ctx->PSSetShader(ps, nullptr, 0); ctx->Draw(3, 0);
    }

    void Render() {
        if (resized) OnResize();
        int ih = S.res ? S.res : ch, iw = std::max(2, (int)std::lround((double)ih * cw / ch)); EnsureRTs(iw, ih);

        px::V3 f, r, u; cam.basis(f, r, u); px::V3 eye = cam.eye();
        float asp = (float)iw / ih;
        XMMATRIX view = XMMatrixLookToLH(XMVectorSet(eye.x, eye.y, eye.z, 1), XMVectorSet(f.x, f.y, f.z, 0), XMVectorSet(u.x, u.y, u.z, 0));
        XMMATRIX vp = view * XMMatrixPerspectiveFovLH(XMConvertToRadians(60.f), asp, 0.1f, 80.f);
        XMMATRIX vp2 = view * XMMatrixPerspectiveFovLH(XMConvertToRadians(50.f), asp, 0.03f, 3.f);

        // dynamic lights (4, like a GX light mask)
        float pulse = 0.9f + 0.12f * std::sin(T * 3.f);
        px::V3 flP = sim.bolts.empty() ? sim.flashP : sim.bolts.back().p; float flK = sim.bolts.empty() ? std::max(0.f, sim.flashT) / 0.3f * 1.6f : 1.5f;
        struct L { px::V3 p, c; float r, k; } L4[4] = {
            { { 0, 2.0f, 10 }, { 1, 0.5f, 0.15f }, 9, pulse },
            { { 3 * std::sin(T * 0.6f), 2.5f, 18 + 3 * std::cos(T * 0.6f) }, { 0.1f, 0.55f, 0.8f }, 9, 1.f },
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
        for (int m : { px::M_FLOOR, px::M_WALL, px::M_TRIM, px::M_STRIP, px::M_CORE, px::M_DOOR }) DrawMat(m, XMMatrixIdentity());

        // viewmodel: own projection, squeezed into the nearest 5% of the depth range so it never clips into walls
        SetViewport(iw, ih, 0.f, 0.05f); SetFrame(view, vp2, eye, lp, lc);
        XMMATRIX cannonWorld = CannonLocal() * CamWorld(eye, r, u, f);
        DrawMat(px::M_CANNON, cannonWorld); DrawMat(px::M_CANNONGLOW, cannonWorld);
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
        quad({ 0, 1.75f, 10 }, 1.7f, { 1, 0.45f, 0.1f }, 0.5f * pulse);     // core halo
        quad({ 0, 2.2f, 27.6f }, 3.4f, { 0.1f, 0.7f, 1 }, 0.3f);            // door halo
        for (auto& b : sim.bolts) quad(b.p, 0.75f, { 1, 0.7f, 0.35f }, 1.f);
        for (auto& p : sim.parts) {
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
        HR(swap->Present(1, 0));
    }

    // ------------------------------------------------------------------ input / loop
    void Capture(bool on) {
        captured = on; ShowCursor(on ? FALSE : TRUE);
        if (on) { RECT r; GetClientRect(hwnd, &r); POINT a{ 0, 0 }; ClientToScreen(hwnd, &a); RECT cr{ a.x, a.y, a.x + r.right, a.y + r.bottom }; ClipCursor(&cr); Recenter(); }
        else ClipCursor(nullptr);
    }
    void Recenter() { RECT r; GetClientRect(hwnd, &r); POINT c{ r.right / 2, r.bottom / 2 }; ClientToScreen(hwnd, &c); SetCursorPos(c.x, c.y); }
    void MouseLook() {
        if (!captured) return;
        RECT r; GetClientRect(hwnd, &r); POINT c{ r.right / 2, r.bottom / 2 }, m; ClientToScreen(hwnd, &c); GetCursorPos(&m);
        cam.yaw += (m.x - c.x) * 0.0025f; cam.pitch = std::max(-1.4f, std::min(1.4f, cam.pitch - (m.y - c.y) * 0.0025f)); SetCursorPos(c.x, c.y);
    }
    void PollKeys() {
        auto down = [](int k) { return (GetAsyncKeyState(k) & 0x8000) != 0; };
        input.w = down('W'); input.a = down('A'); input.s = down('S'); input.d = down('D');
        input.left = down(VK_LEFT); input.right = down(VK_RIGHT); input.up = down(VK_UP); input.down = down(VK_DOWN);
    }
    void OnKey(WPARAM k) {
        static const int resTable[] = { 240, 360, 480, 720, 0 };
        if (k == VK_ESCAPE) { if (captured) Capture(false); }
        else if (k == VK_SPACE) Fire();
        else if (k == VK_F1) S.bloomOn = !S.bloomOn;
        else if (k == VK_F2) S.dither = !S.dither;
        else if (k == VK_F3) S.fog = S.fog > 0 ? 0.f : 0.85f;
        else if (k == VK_F4) S.nearest = !S.nearest;
        else if (k == VK_F5) S.dyn = !S.dyn;
        else if (k >= '1' && k <= '5') S.res = resTable[k - '1'];
        else if (k == VK_OEM_MINUS) S.bloom = std::max(0.f, S.bloom - 0.1f);
        else if (k == VK_OEM_PLUS) S.bloom = std::min(2.f, S.bloom + 0.1f);
    }
};

static App* g_app = nullptr;
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_SIZE: if (g_app && w != SIZE_MINIMIZED) { g_app->cw = std::max<int>(2, LOWORD(l)); g_app->ch = std::max<int>(2, HIWORD(l)); g_app->resized = true; } return 0;
    case WM_LBUTTONDOWN: if (g_app) { if (!g_app->captured) g_app->Capture(true); else g_app->Fire(); } return 0;
    case WM_KEYDOWN: if (g_app) g_app->OnKey(w); return 0;
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
    app.hwnd = CreateWindowA(wc.lpszClassName, "Prime-style renderer (D3D11)", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hi, nullptr);
    RECT cr; GetClientRect(app.hwnd, &cr); app.cw = cr.right; app.ch = cr.bottom;
    app.InitDevice();

    LARGE_INTEGER fq, t0; QueryPerformanceFrequency(&fq); QueryPerformanceCounter(&t0);
    double last = 0, fpsAcc = 0; int fpsN = 0;
    for (bool run = true; run;) {
        MSG msg; while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) { if (msg.message == WM_QUIT) run = false; TranslateMessage(&msg); DispatchMessageA(&msg); }
        if (!run) break;
        LARGE_INTEGER now; QueryPerformanceCounter(&now); double t = (double)(now.QuadPart - t0.QuadPart) / fq.QuadPart;
        float dt = (float)std::min(0.05, t - last); last = t; app.T += dt;

        app.PollKeys(); app.MouseLook(); app.cam.move(app.scene, app.input, dt); app.sim.update(dt);
        app.Render();

        fpsAcc += dt; fpsN++;
        if (fpsAcc > 0.5) {
            char title[256]; snprintf(title, sizeof title, "Prime-style renderer (D3D11) | %.0f fps | %dx%d | bloom %s %.2f | dither %s | haze %s | F1-F5 toggles, 1-5 res, -/+ bloom",
                fpsN / fpsAcc, app.rtScene.w, app.rtScene.h, app.S.bloomOn ? "on" : "off", app.S.bloom, app.S.dither ? "on" : "off", app.S.fog > 0 ? "on" : "off");
            SetWindowTextA(app.hwnd, title); fpsAcc = 0; fpsN = 0;
        }
    }
    return 0;
}
