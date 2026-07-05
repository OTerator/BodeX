// BodeX — Win32 + DirectX 11 host for the Dear ImGui application.
//
// This file owns the platform: it creates the window, the D3D11 device and
// swap chain, pumps the message loop, and drives ImGui's per-frame render. All
// application logic lives behind App::render() (src/app/App.*) so this host code
// stays stable. Based on Dear ImGui's example_win32_directx11 backbone.

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>

#include "app/App.h"
#include "ui/ImageStore.h"
#include "ui/widgets.h"
#include "model/AppConfig.h" // gt::fileExists

// ---- D3D11 globals --------------------------------------------------------
static ID3D11Device*           g_pd3dDevice = nullptr;
static ID3D11DeviceContext*    g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*         g_pSwapChain = nullptr;
static bool                    g_SwapChainOccluded = false;
static UINT                    g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
// Set when the user clicks the window's close box; routed to App so the
// unsaved-changes guard runs instead of the window closing immediately.
static bool                    g_closeRequested = false;
// Set when the app loses foreground activation (alt-tab away); routed to App so
// it can flush a pending autosave immediately rather than waiting for the tick.
static bool                    g_appDeactivated = false;

// ---- forward decls --------------------------------------------------------
static bool CreateDeviceD3D(HWND hWnd);
static void CleanupDeviceD3D();
static void CreateRenderTarget();
static void CleanupRenderTarget();
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Message handler implemented in imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int main(int, char**)
{
    // High-DPI awareness before creating the window (defined in the win32 backend).
    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L,
                       GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr,
                       L"BodeXWndClass", nullptr };
    // Application icon (embedded via resources/app.rc, id 100) + default cursor.
    HICON hAppIcon = (HICON)::LoadImageW(wc.hInstance, MAKEINTRESOURCEW(100), IMAGE_ICON,
                                         0, 0, LR_DEFAULTSIZE | LR_SHARED);
    wc.hIcon = hAppIcon;
    wc.hIconSm = hAppIcon;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"BodeX",
                                WS_OVERLAPPEDWINDOW, 100, 100, 1280, 820,
                                nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        ::MessageBoxW(nullptr, L"Failed to create a Direct3D 11 device.", L"BodeX", MB_ICONERROR);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // keep the app self-contained; no imgui.ini on disk

    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    gt::ui::imageStoreInit(g_pd3dDevice, g_pd3dDeviceContext); // for question images

    // Scale the whole UI (sizes + fonts) to the monitor DPI so it renders crisp
    // and correctly sized on high-DPI displays (process is DPI-aware via manifest).
    {
        const float dpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
        ImGuiStyle& style = ImGui::GetStyle();
        if (dpiScale > 1.0f)
            style.ScaleAllSizes(dpiScale);
        style.FontScaleMain = dpiScale; // 1.92 global font scale
    }

    // Fonts: keep ProggyClean (ASCII-only) as the main UI font, and load a scalable
    // Hebrew-capable system font as a *separate* font used only for cell-note text
    // (see gt::ui::pushNotesFont). That renders Hebrew where notes appear without
    // changing the rest of the UI. If the file is missing we simply skip it and
    // notes fall back to the default font (Hebrew glyphs won't show, no crash).
    {
        io.Fonts->AddFontDefault();
        ImFont* notesFont = nullptr;
        const char* candidates[] = { "C:\\Windows\\Fonts\\arial.ttf",
                                     "C:\\Windows\\Fonts\\segoeui.ttf",
                                     "C:\\Windows\\Fonts\\tahoma.ttf" };
        for (const char* path : candidates)
            if (gt::fileExists(path)) { notesFont = io.Fonts->AddFontFromFileTTF(path, 16.0f); break; }
        gt::ui::setNotesFont(notesFont);
    }

    App app;

    const ImVec4 clear_color = ImVec4(0.09f, 0.10f, 0.12f, 1.00f);

    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // Skip rendering while occluded (minimized / locked screen).
        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        {
            ::Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // The entire application UI is drawn here.
        app.render();

        // Route a window-close click through the app's unsaved-changes guard.
        if (g_closeRequested) {
            g_closeRequested = false;
            app.requestQuit();
        }

        // Losing foreground activation flushes a pending autosave immediately.
        if (g_appDeactivated) {
            g_appDeactivated = false;
            app.flushAutosave();
        }

        ImGui::Render();
        const float clear[4] = { clear_color.x, clear_color.y, clear_color.z, clear_color.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        HRESULT hr = g_pSwapChain->Present(1, 0); // present with vsync
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);

        if (app.wantsQuit()) // guard resolved -> leave the loop and clean up
            done = true;
    }

    gt::ui::imageStoreReleaseAll(); // free image textures while the device is alive
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}

// ---- D3D11 helpers --------------------------------------------------------
static bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
        featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
        &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) // fall back to the WARP software renderer
        res = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
            featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
            &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain)        { g_pSwapChain->Release();        g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice)        { g_pd3dDevice->Release();        g_pd3dDevice = nullptr; }
}

static void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer)
    {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

static void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // disable ALT application menu
            return 0;
        break;
    case WM_CLOSE:
        // Defer to the app's unsaved-changes guard rather than closing now.
        g_closeRequested = true;
        return 0;
    case WM_ACTIVATEAPP:
        // wParam == FALSE: another app took the foreground -> flush any autosave.
        // Fall through to DefWindowProc so default activation handling still runs.
        if (wParam == FALSE)
            g_appDeactivated = true;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
