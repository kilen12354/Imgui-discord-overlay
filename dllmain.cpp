#include <Windows.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <d3d11.h>
#include <dxgi.h>
#include <Psapi.h> 
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Globalne zmienne
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// Oryginalna funkcja Present
typedef HRESULT(__stdcall* Present) (IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
Present g_OriginalPresent = nullptr;

// Zmienne dla menu
bool g_ShowMenu = true; // Ustawione na true, aby menu było widoczne od razu
bool g_IsInitialized = false;

// Funkcja do logowania
std::ofstream g_LogFile;
#define LOG(x) if(g_LogFile.is_open()) { g_LogFile << x << std::endl; g_LogFile.flush(); }

// Hook dla WndProc
WNDPROC g_OriginalWndProcHandler = nullptr;
LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
        return true;

    return CallWindowProc(g_OriginalWndProcHandler, hWnd, uMsg, wParam, lParam);
}

// Hooked Present function
HRESULT APIENTRY HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
    static bool first_call = true;
    if (first_call) {
        LOG("HookedPresent called for the first time");
        first_call = false;
    }

    LOG("HookedPresent called"); // Nowa linia

    if (!g_IsInitialized)
    {
        LOG("Initializing ImGui");
        if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_pd3dDevice)))
        {
            g_pd3dDevice->GetImmediateContext(&g_pd3dDeviceContext);
            DXGI_SWAP_CHAIN_DESC sd;
            pSwapChain->GetDesc(&sd);
            g_pSwapChain = pSwapChain;

            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO(); (void)io;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

            if (!ImGui_ImplWin32_Init(sd.OutputWindow))
            {
                LOG("ImGui_ImplWin32_Init failed");
                return g_OriginalPresent(pSwapChain, SyncInterval, Flags);
            }

            if (!ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext))
            {
                LOG("ImGui_ImplDX11_Init failed");
                return g_OriginalPresent(pSwapChain, SyncInterval, Flags);
            }

            g_OriginalWndProcHandler = (WNDPROC)SetWindowLongPtr(sd.OutputWindow, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);

            ID3D11Texture2D* pBackBuffer;
            pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
            g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
            pBackBuffer->Release();

            g_IsInitialized = true;
            LOG("ImGui initialized successfully");
        }
        else
        {
            LOG("Failed to get device from swap chain");
            return g_OriginalPresent(pSwapChain, SyncInterval, Flags);
        }
    }

    if (GetAsyncKeyState(VK_INSERT) & 1)
    {
        g_ShowMenu = !g_ShowMenu;
        LOG("Menu toggled: " << (g_ShowMenu ? "ON" : "OFF"));
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (g_ShowMenu)
    {
        LOG("Attempting to render ImGui DemoWindow");
        ImGui::ShowDemoWindow(&g_ShowMenu);
        LOG("ImGui DemoWindow rendering completed");
    }

    ImGui::Render();
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    HRESULT result = g_OriginalPresent(pSwapChain, SyncInterval, Flags);
    LOG("Original Present called, result: " << std::hex << result);
    return result;
}

void CleanupImGui()
{
    LOG("Cleaning up ImGui");
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

DWORD WINAPI MainThread(LPVOID lpReserved)
{
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);

    g_LogFile.open("debug_log.txt");
    LOG("MainThread started");

    while (GetModuleHandleA("DiscordHook64.dll") == nullptr)
    {
        LOG("Waiting for DiscordHook64.dll to load");
        Sleep(100);
    }

    HMODULE hMod = GetModuleHandleA("DiscordHook64.dll");
    if (hMod == nullptr)
    {
        LOG("Failed to get DiscordHook64.dll module handle");
        return 1;
    }

    uintptr_t presentAddr = reinterpret_cast<uintptr_t>(hMod) + 0xE9090; //E9090
    Present* discord_present = reinterpret_cast<Present*>(presentAddr);

    if (discord_present == nullptr || *discord_present == nullptr)
    {
        LOG("Failed to find Present function in DiscordHook64.dll");
        return 1;
    }

    g_OriginalPresent = *discord_present;
    LOG("Original Present address: " << std::hex << (void*)g_OriginalPresent);
    LOG("Hooked Present address: " << std::hex << (void*)HookedPresent);

    DWORD oldProtect;
    VirtualProtect(discord_present, sizeof(Present), PAGE_EXECUTE_READWRITE, &oldProtect);
    *discord_present = HookedPresent;
    VirtualProtect(discord_present, sizeof(Present), oldProtect, &oldProtect);

    LOG("Hook installed successfully");

    // Pętla, aby utrzymać DLL załadowane i logować aktywność
    int counter = 0;
    while (true)
    {
        Sleep(1000);
        if (++counter % 10 == 0) {
            LOG("DLL still active, counter: " << counter);
        }
    }

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        CleanupImGui();
        if (g_LogFile.is_open())
        {
            g_LogFile.close();
        }
        break;
    }
    return TRUE;
}
