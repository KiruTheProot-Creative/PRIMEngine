// ImGui drawn through PRIMEngine's Direct3D 12 layer: what imgui_impl_dx11 did, on d12:: objects (ImTextureID: a d12::ShaderResourceView*)
#include "imgui_impl_d12.h"
#include "d3d12shim.h"
#include <d3dcompiler.h>
#include <cstring>

namespace {
using namespace d12;
struct Tex { ComPtr<Texture2D> t; ComPtr<ShaderResourceView> v; };
struct Data {
    Device* dev = nullptr; DeviceContext* ctx = nullptr;
    ComPtr<Buffer> vb, ib, cb; int vbSize = 0, ibSize = 0;
    ComPtr<VertexShader> vs; ComPtr<PixelShader> ps; ComPtr<InputLayout> il;
    ComPtr<SamplerState> smp; ComPtr<RasterizerState> rs; ComPtr<BlendState> bs; ComPtr<DepthStencilState> ds;
};
Data* bd = nullptr;
const char* kShader = R"(
cbuffer vertexBuffer : register(b0) { float4x4 ProjectionMatrix; };
struct VS_INPUT { float2 pos : POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };
struct PS_INPUT { float4 pos : SV_POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };
PS_INPUT VS(VS_INPUT i) { PS_INPUT o; o.pos = mul(ProjectionMatrix, float4(i.pos.xy, 0.f, 1.f)); o.col = i.col; o.uv = i.uv; return o; }
sampler sampler0 : register(s0); Texture2D texture0 : register(t0);
float4 PS(PS_INPUT i) : SV_Target { return i.col * texture0.Sample(sampler0, i.uv); }
)";
void SetupRenderState(ImDrawData* dd) {
    DeviceContext* c = bd->ctx;
    D3D11_VIEWPORT vp{ 0, 0, (float)(int)(dd->DisplaySize.x * dd->FramebufferScale.x + 0.5f), (float)(int)(dd->DisplaySize.y * dd->FramebufferScale.y + 0.5f), 0, 1 };
    c->RSSetViewports(1, &vp);
    float L = dd->DisplayPos.x, R = dd->DisplayPos.x + dd->DisplaySize.x, T = dd->DisplayPos.y, B = dd->DisplayPos.y + dd->DisplaySize.y;
    float mvp[4][4] = { { 2.0f / (R - L), 0, 0, 0 }, { 0, 2.0f / (T - B), 0, 0 }, { 0, 0, 0.5f, 0 }, { (R + L) / (L - R), (T + B) / (B - T), 0.5f, 1.0f } };
    c->UpdateSubresource(bd->cb.Get(), 0, nullptr, mvp, 0, 0);
    UINT stride = sizeof(ImDrawVert), offset = 0;
    c->IASetInputLayout(bd->il.Get()); c->IASetVertexBuffers(0, 1, bd->vb.GetAddressOf(), &stride, &offset);
    c->IASetIndexBuffer(bd->ib.Get(), sizeof(ImDrawIdx) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(bd->vs.Get()); c->VSSetConstantBuffers(0, 1, bd->cb.GetAddressOf()); c->PSSetShader(bd->ps.Get()); c->PSSetSamplers(0, 1, bd->smp.GetAddressOf());
    const float bf[4] = { 0, 0, 0, 0 }; c->OMSetBlendState(bd->bs.Get(), bf, 0xFFFFFFFF); c->OMSetDepthStencilState(bd->ds.Get(), 0); c->RSSetState(bd->rs.Get());
}
void DestroyTexture(ImTextureData* tex) {
    if (Tex* t = (Tex*)tex->BackendUserData) { IM_DELETE(t); tex->SetTexID(ImTextureID_Invalid); tex->BackendUserData = nullptr; }
    tex->SetStatus(ImTextureStatus_Destroyed);
}
} // namespace

bool ImGui_ImplD12_Init(d12::Device* device, d12::DeviceContext* ctx) {
    ImGuiIO& io = ImGui::GetIO(); bd = IM_NEW(Data)(); bd->dev = device; bd->ctx = ctx;
    io.BackendRendererUserData = bd; io.BackendRendererName = "imgui_impl_d12 (PRIMEngine)";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
    ComPtr<ID3DBlob> vsb, psb, err;
    if (FAILED(D3DCompile(kShader, std::strlen(kShader), "imgui", nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vsb, &err))) return false;
    if (FAILED(D3DCompile(kShader, std::strlen(kShader), "imgui", nullptr, nullptr, "PS", "ps_5_0", 0, 0, &psb, &err))) return false;
    device->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &bd->vs);
    device->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &bd->ps);
    D3D11_INPUT_ELEMENT_DESC el[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, (UINT)offsetof(ImDrawVert, pos), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, (UINT)offsetof(ImDrawVert, uv), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, (UINT)offsetof(ImDrawVert, col), D3D11_INPUT_PER_VERTEX_DATA, 0 } };
    device->CreateInputLayout(el, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &bd->il);
    D3D11_BUFFER_DESC cd{}; cd.ByteWidth = 64; cd.Usage = D3D11_USAGE_DEFAULT; cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; device->CreateBuffer(&cd, nullptr, &bd->cb);
    D3D11_BLEND_DESC b{}; b.RenderTarget[0].BlendEnable = TRUE; b.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; b.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; b.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    b.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; b.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; b.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD; b.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device->CreateBlendState(&b, &bd->bs);
    D3D11_RASTERIZER_DESC r{}; r.FillMode = D3D11_FILL_SOLID; r.CullMode = D3D11_CULL_NONE; r.ScissorEnable = TRUE; r.DepthClipEnable = TRUE; device->CreateRasterizerState(&r, &bd->rs);
    D3D11_DEPTH_STENCIL_DESC d{}; d.DepthEnable = FALSE; d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; d.DepthFunc = D3D11_COMPARISON_ALWAYS; d.FrontFace.StencilFailOp = d.FrontFace.StencilDepthFailOp = d.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    d.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS; d.BackFace = d.FrontFace; device->CreateDepthStencilState(&d, &bd->ds);
    D3D11_SAMPLER_DESC s{}; s.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; s.ComparisonFunc = D3D11_COMPARISON_ALWAYS; s.MaxLOD = D3D11_FLOAT32_MAX;
    device->CreateSamplerState(&s, &bd->smp);
    return bd->vs && bd->ps && bd->il;
}
void ImGui_ImplD12_Shutdown() {
    if (!bd) return;
    for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) if (tex->RefCount == 1) DestroyTexture(tex);
    ImGuiIO& io = ImGui::GetIO(); io.BackendRendererName = nullptr; io.BackendRendererUserData = nullptr; io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
    IM_DELETE(bd); bd = nullptr;
}
void ImGui_ImplD12_NewFrame() {}
void ImGui_ImplD12_UpdateTexture(ImTextureData* tex) {
    if (tex->Status == ImTextureStatus_WantCreate) {
        Tex* t = IM_NEW(Tex)();
        D3D11_TEXTURE2D_DESC d{}; d.Width = (UINT)tex->Width; d.Height = (UINT)tex->Height; d.MipLevels = 1; d.ArraySize = 1; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd{ tex->GetPixels(), d.Width * 4, 0 };
        bd->dev->CreateTexture2D(&d, &sd, &t->t);
        D3D11_SHADER_RESOURCE_VIEW_DESC v{}; v.Format = DXGI_FORMAT_R8G8B8A8_UNORM; v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; v.Texture2D.MipLevels = 1;
        if (t->t) bd->dev->CreateShaderResourceView(t->t.Get(), &v, &t->v);
        tex->SetTexID((ImTextureID)(intptr_t)t->v.Get()); tex->SetStatus(ImTextureStatus_OK); tex->BackendUserData = t;
    } else if (tex->Status == ImTextureStatus_WantUpdates) {
        Tex* t = (Tex*)tex->BackendUserData;
        if (t && t->t) for (ImTextureRect& r : tex->Updates) {
            D3D11_BOX box{ (UINT)r.x, (UINT)r.y, 0, (UINT)(r.x + r.w), (UINT)(r.y + r.h), 1 };
            bd->ctx->UpdateSubresource(t->t.Get(), 0, &box, tex->GetPixelsAt(r.x, r.y), (UINT)tex->GetPitch(), 0);
        }
        tex->SetStatus(ImTextureStatus_OK);
    }
    if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) DestroyTexture(tex);
}
void ImGui_ImplD12_RenderDrawData(ImDrawData* dd) {
    if (!bd || dd->DisplaySize.x <= 0.0f || dd->DisplaySize.y <= 0.0f) return;
    DeviceContext* c = bd->ctx;
    if (dd->Textures) for (ImTextureData* tex : *dd->Textures) if (tex->Status != ImTextureStatus_OK) ImGui_ImplD12_UpdateTexture(tex);
    if (!bd->vb || bd->vbSize < dd->TotalVtxCount) {
        bd->vb.Reset(); bd->vbSize = dd->TotalVtxCount + 5000;
        D3D11_BUFFER_DESC d{}; d.Usage = D3D11_USAGE_DYNAMIC; d.ByteWidth = (UINT)bd->vbSize * sizeof(ImDrawVert); d.BindFlags = D3D11_BIND_VERTEX_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(bd->dev->CreateBuffer(&d, nullptr, &bd->vb))) return;
    }
    if (!bd->ib || bd->ibSize < dd->TotalIdxCount) {
        bd->ib.Reset(); bd->ibSize = dd->TotalIdxCount + 10000;
        D3D11_BUFFER_DESC d{}; d.Usage = D3D11_USAGE_DYNAMIC; d.ByteWidth = (UINT)bd->ibSize * sizeof(ImDrawIdx); d.BindFlags = D3D11_BIND_INDEX_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(bd->dev->CreateBuffer(&d, nullptr, &bd->ib))) return;
    }
    D3D11_MAPPED_SUBRESOURCE vr, ir;
    if (c->Map(bd->vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &vr) != S_OK) return;
    if (c->Map(bd->ib.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ir) != S_OK) { c->Unmap(bd->vb.Get(), 0); return; }
    ImDrawVert* vd = (ImDrawVert*)vr.pData; ImDrawIdx* id = (ImDrawIdx*)ir.pData;
    for (const ImDrawList* l : dd->CmdLists) { std::memcpy(vd, l->VtxBuffer.Data, l->VtxBuffer.Size * sizeof(ImDrawVert)); std::memcpy(id, l->IdxBuffer.Data, l->IdxBuffer.Size * sizeof(ImDrawIdx)); vd += l->VtxBuffer.Size; id += l->IdxBuffer.Size; }
    c->Unmap(bd->vb.Get(), 0); c->Unmap(bd->ib.Get(), 0);
    DeviceContext::Saved* old = c->SaveState();   // (everything the engine had bound comes back afterwards)
    SetupRenderState(dd);
    int gi = 0, gv = 0; ImVec2 off = dd->DisplayPos, sc = dd->FramebufferScale;
    for (const ImDrawList* l : dd->CmdLists) {
        for (int i = 0; i < l->CmdBuffer.Size; i++) {
            const ImDrawCmd* pc = &l->CmdBuffer[i];
            if (pc->UserCallback) { if (pc->UserCallback == ImDrawCallback_ResetRenderState) SetupRenderState(dd); else pc->UserCallback(l, pc); continue; }
            ImVec2 mn((pc->ClipRect.x - off.x) * sc.x, (pc->ClipRect.y - off.y) * sc.y), mx((pc->ClipRect.z - off.x) * sc.x, (pc->ClipRect.w - off.y) * sc.y);
            if (mx.x <= mn.x || mx.y <= mn.y) continue;
            D3D11_RECT r{ (LONG)mn.x, (LONG)mn.y, (LONG)mx.x, (LONG)mx.y }; c->RSSetScissorRects(1, &r);
            ShaderResourceView* srv = (ShaderResourceView*)(intptr_t)pc->GetTexID(); c->PSSetShaderResources(0, 1, &srv);
            c->DrawIndexed(pc->ElemCount, pc->IdxOffset + gi, (INT)(pc->VtxOffset + gv));
        }
        gi += l->IdxBuffer.Size; gv += l->VtxBuffer.Size;
    }
    c->RestoreState(old);
}
