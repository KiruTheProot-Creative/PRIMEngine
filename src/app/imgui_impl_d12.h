#pragma once
// ImGui drawn through PRIMEngine's Direct3D 12 layer (the same as imgui_impl_dx11, on d12:: objects)
#include "../../third_party/imgui/imgui.h"
namespace d12 { class Device; class DeviceContext; }
bool ImGui_ImplD12_Init(d12::Device* device, d12::DeviceContext* ctx);
void ImGui_ImplD12_Shutdown();
void ImGui_ImplD12_NewFrame();
void ImGui_ImplD12_RenderDrawData(ImDrawData* draw_data);
void ImGui_ImplD12_UpdateTexture(ImTextureData* tex);
