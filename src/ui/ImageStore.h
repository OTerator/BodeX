#pragma once

// Loads image files into Direct3D 11 textures for display via ImGui::Image.
// Textures are cached by absolute path and freed on project close / shutdown.
// The DX11 device is provided by main.cpp after the backend is initialized.

#include <string>
#include "imgui.h" // ImTextureID

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace gt::ui {

struct Tex {
    ImTextureID id = 0; // pass to ImGui::Image(...)
    int  w  = 0;
    int  h  = 0;
    bool ok = false;
};

void imageStoreInit(ID3D11Device* dev, ID3D11DeviceContext* ctx);
Tex  imageStoreGet(const std::string& absPath); // lazy-load + cache (UTF-8 path)
void imageStoreReleaseAll();                    // free all cached textures

} // namespace gt::ui
