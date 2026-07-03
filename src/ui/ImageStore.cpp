#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8   // let stbi_load() accept UTF-8 paths (Unicode-safe)
#include "stb_image.h"

#include "ui/ImageStore.h"

#include <d3d11.h>
#include <cstdint>
#include <cstring>
#include <unordered_map>

namespace gt::ui {

namespace {

ID3D11Device*        g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;

struct Entry {
    ID3D11Texture2D*          tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    int w = 0, h = 0;
};
std::unordered_map<std::string, Entry> g_cache;

// Decode absPath (UTF-8) to RGBA8 and upload to a shader-resource texture.
bool loadEntry(const std::string& absPath, Entry& out)
{
    if (!g_dev)
        return false;
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load(absPath.c_str(), &w, &h, &n, 4);
    if (!px)
        return false;

    D3D11_TEXTURE2D_DESC desc;
    std::memset(&desc, 0, sizeof(desc));
    desc.Width = static_cast<UINT>(w);
    desc.Height = static_cast<UINT>(h);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sub;
    std::memset(&sub, 0, sizeof(sub));
    sub.pSysMem = px;
    sub.SysMemPitch = static_cast<UINT>(w) * 4;

    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = g_dev->CreateTexture2D(&desc, &sub, &tex);
    stbi_image_free(px);
    if (FAILED(hr) || !tex)
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvd;
    std::memset(&srvd, 0, sizeof(srvd));
    srvd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvd.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* srv = nullptr;
    hr = g_dev->CreateShaderResourceView(tex, &srvd, &srv);
    if (FAILED(hr) || !srv) {
        tex->Release();
        return false;
    }

    out.tex = tex;
    out.srv = srv;
    out.w = w;
    out.h = h;
    return true;
}

} // namespace

void imageStoreInit(ID3D11Device* dev, ID3D11DeviceContext* ctx)
{
    g_dev = dev;
    g_ctx = ctx;
}

Tex imageStoreGet(const std::string& absPath)
{
    Tex t;
    auto it = g_cache.find(absPath);
    if (it == g_cache.end()) {
        Entry e;
        if (!loadEntry(absPath, e))
            return t; // failure is not cached, so a later attempt can retry
        it = g_cache.emplace(absPath, e).first;
    }
    const Entry& e = it->second;
    t.id = static_cast<ImTextureID>(reinterpret_cast<intptr_t>(e.srv));
    t.w = e.w;
    t.h = e.h;
    t.ok = true;
    return t;
}

void imageStoreReleaseAll()
{
    for (auto& kv : g_cache) {
        if (kv.second.srv) kv.second.srv->Release();
        if (kv.second.tex) kv.second.tex->Release();
    }
    g_cache.clear();
}

} // namespace gt::ui
