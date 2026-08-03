#include "OS-ImGui_External.h"
#include "../Core/Config.h"
#include "../Helpers/Logger.h"

// D3D11 Device
namespace OSImGui
{
#ifdef _CONSOLE
    bool D3DDevice::CreateDeviceD3D(HWND hWnd)
    {
        // Stack-initialize with designated initializers for better performance
        DXGI_SWAP_CHAIN_DESC sd = {
     .BufferDesc = {
         .Width = 0,
         .Height = 0,
         .RefreshRate = {.Numerator = 0, .Denominator = 1 },
         .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
         .ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED,
         .Scaling = DXGI_MODE_SCALING_UNSPECIFIED
     },
     .SampleDesc = {.Count = 1, .Quality = 0 },
     .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
     .BufferCount = 2,
     .OutputWindow = hWnd,
     .Windowed = TRUE,
     .SwapEffect = DXGI_SWAP_EFFECT_DISCARD,
     .Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH
        };

        constexpr UINT createDeviceFlags = 0;
        constexpr D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

        D3D_FEATURE_LEVEL featureLevel;
        HRESULT res = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
            featureLevels, _countof(featureLevels), D3D11_SDK_VERSION,
            &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext
        );

        // Fallback to WARP only if hardware fails
        if (res == DXGI_ERROR_UNSUPPORTED) {
            res = D3D11CreateDeviceAndSwapChain(
                nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
                featureLevels, _countof(featureLevels), D3D11_SDK_VERSION,
                &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext
            );
        }

        if (FAILED(res))
            return false;

        CreateRenderTarget();
        return true;
    }

    void D3DDevice::CleanupDeviceD3D()
    {
        CleanupRenderTarget();

        // Use RAII-style cleanup with immediate null assignment
        if (g_pSwapChain) {
            g_pSwapChain->Release();
            g_pSwapChain = nullptr;
        }
        if (g_pd3dDeviceContext) {
            g_pd3dDeviceContext->Release();
            g_pd3dDeviceContext = nullptr;
        }
        if (g_pd3dDevice) {
            g_pd3dDevice->Release();
            g_pd3dDevice = nullptr;
        }
    }

    void D3DDevice::CreateRenderTarget()
    {
        ID3D11Texture2D* pBackBuffer = nullptr;
        if (SUCCEEDED(g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer))) && pBackBuffer) {
            g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
            pBackBuffer->Release();
        }
    }

    void D3DDevice::CleanupRenderTarget()
    {
        if (g_mainRenderTargetView) {
            g_mainRenderTargetView->Release();
            g_mainRenderTargetView = nullptr;
        }
    }
#endif
}

// OSImGui External
namespace OSImGui
{
    LRESULT WINAPI WndProc_External(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void OSImGui_External::NewWindow(std::string WindowName, Vec2 WindowSize, std::function<void()> CallBack)
    {
        if (!CallBack)
            throw OSException("CallBack is empty");

        // Use move semantics and reserve string capacity
        Window.Name = WindowName.empty() ? "Window" : std::move(WindowName);
        Window.wName = StringToWstring(Window.Name);
        Window.ClassName = "WindowClass";
        Window.wClassName = StringToWstring(Window.ClassName);
        Window.Size = WindowSize;

        Type = NEW;
        CallBackFn = std::move(CallBack);

        if (!CreateMyWindow())
            throw OSException("CreateMyWindow() call failed");

        try {
            InitImGui(g_Device.g_pd3dDevice, g_Device.g_pd3dDeviceContext);
        }
        catch (const OSException&) {
            throw; // Re-throw without copying
        }

        MainLoop();
    }

    void RegisterRawInput(HWND hwnd) {
        RAWINPUTDEVICE rid;
        rid.usUsagePage = 0x01;			// Generic Desktop Controls
        rid.usUsage = 0x02;				// Mouse
        rid.dwFlags = RIDEV_INPUTSINK;	// Capture input even if the window is unfocused
        rid.hwndTarget = hwnd;

        if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
            Log::Warning("Failed to register raw input!");
        }
    }


    void OSImGui_External::AttachAnotherWindow(std::string DestWindowName, std::string DestWindowClassName, std::function<void()> CallBack)
    {
        if (DestWindowName.empty() && DestWindowClassName.empty())
            throw OSException("DestWindowName and DestWindowClassName are empty");

        const char* className = DestWindowClassName.empty() ? nullptr : DestWindowClassName.c_str();
        const char* windowName = DestWindowName.empty() ? nullptr : DestWindowName.c_str();
        const HWND destWindowHandle = FindWindowA(className, windowName);
        if (!destWindowHandle)
            throw OSException("DestWindow isn't exist");

        DestWindow.Name = std::move(DestWindowName);
        DestWindow.ClassName = std::move(DestWindowClassName);
        AttachAnotherWindow(destWindowHandle, std::move(CallBack));
    }

    void OSImGui_External::AttachAnotherWindow(HWND DestWindowHandle, std::function<void()> CallBack)
    {
        if (!CallBack)
            throw OSException("CallBack is empty");
        if (!IsWindow(DestWindowHandle))
            throw OSException("DestWindow isn't exist");

        Window.Name = "DragonBurn Kernel";
        Window.wName = StringToWstring(Window.Name);
        Window.ClassName = "WindowClass";
        Window.wClassName = StringToWstring(Window.ClassName);
        Window.BgColor = ImColor(0, 0, 0, 0);
        DestWindow.hWnd = DestWindowHandle;

        Type = ATTACH;
        CallBackFn = std::move(CallBack);

        if (!CreateMyWindow())
            throw OSException("CreateMyWindow() call failed");

        try {
            InitImGui(g_Device.g_pd3dDevice, g_Device.g_pd3dDeviceContext);
        }
        catch (const OSException&) {
            throw;
        }

        RegisterRawInput(Window.hWnd);
        g_keyboard_hook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
        MainLoop();
    }

    bool OSImGui_External::PeekEndMessage()
    {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                return true;
        }
        return false;
    }

    static std::map<int, std::pair<char, char>> keyMap = {
{VK_OEM_1, {';', ':'}},         // Semicolon
{VK_OEM_PLUS, {'=', '+'}},      // Equals
{VK_OEM_COMMA, {',', '<'}},     // Comma
{VK_OEM_MINUS, {'-', '_'}},     // Minus
{VK_OEM_PERIOD, {'.', '>'}},    // Period
{VK_OEM_2, {'/', '?'}},         // Slash
{VK_OEM_3, {'`', '~'}},         // Backtick
{VK_OEM_4, {'[', '{'}},         // Left bracket
{VK_OEM_5, {'\\', '|'}},        // Backslash
{VK_OEM_6, {']', '}'}},         // Right bracket
{VK_OEM_7, {'\'', '\"'}},       // Quote
{'1', {'1', '!'}},              // 1
{'2', {'2', '@'}},              // 2
{'3', {'3', '#'}},              // 3
{'4', {'4', '$'}},              // 4
{'5', {'5', '%'}},              // 5
{'6', {'6', '^'}},              // 6
{'7', {'7', '&'}},              // 7
{'8', {'8', '*'}},              // 8
{'9', {'9', '('}},              // 9
{'0', {'0', ')'}}               // 0
    };

    void toggleKey(int vkKey, bool& toggleState, ImGuiIO& io) {

        bool shiftHeld = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) || (GetAsyncKeyState(VK_RSHIFT) & 0x8000);

        char charCode = 0;

        if (vkKey >= 'A' && vkKey <= 'Z') {
            charCode = shiftHeld ? (char)vkKey : (char)(vkKey + 32); // A-Z
        }
        else if (vkKey >= '0' && vkKey <= '9') {
            charCode = shiftHeld ? keyMap[(char)vkKey].second : keyMap[(char)vkKey].first; // 0-9
        }
        else if (keyMap.find(vkKey) != keyMap.end()) { // Special
            charCode = shiftHeld ? keyMap[vkKey].second : keyMap[vkKey].first;
        }
        else if (vkKey == VK_SPACE) {
            charCode = (char)VK_SPACE;
        }

        if (toggleState && (GetAsyncKeyState(vkKey) & 0x8000)) {
            if (charCode != 0)
                io.AddInputCharacter(charCode);
            toggleState = false;
        }
        else if (!toggleState && !(GetAsyncKeyState(vkKey) & 0x8000)) {
            toggleState = true;
        }
    }

    void toggleKey(int vkKey, bool& toggleState, ImGuiMouseButton_ imguiKey, ImGuiIO& io) {
        if (toggleState && (GetAsyncKeyState(vkKey) & 0x8000)) {
            io.AddMouseButtonEvent(imguiKey, true);
            toggleState = false;
        }
        else if (!toggleState && !(GetAsyncKeyState(vkKey) & 0x8000)) {
            io.AddMouseButtonEvent(imguiKey, false);
            toggleState = true;
        }
    }

    void toggleKey(int vkKey, bool& toggleState, ImGuiKey imguiKey, ImGuiKey imguiModKey, ImGuiIO& io) {
        if (toggleState && (GetAsyncKeyState(vkKey) & 0x8000)) {
            io.AddKeyEvent(imguiKey, true);

            if (imguiModKey) {
                ImGui::GetKeyData(imguiModKey)->Down = true;
            }
            toggleState = false;
        }
        else if (!toggleState && !(GetAsyncKeyState(vkKey) & 0x8000)) {
            io.AddKeyEvent(imguiKey, false);

            if (imguiModKey) {
                ImGui::GetKeyData(imguiModKey)->Down = false;
            }
            toggleState = true;
        }
    }

    void OSImGui_External::MainLoop()
    {

        // Cache frequently used values
        constexpr DWORD excludeCapture = WDA_EXCLUDEFROMCAPTURE;
        constexpr DWORD includeCapture = WDA_NONE;

        while (!EndFlag)
        {
            if (PeekEndMessage()) 
                break;

            if (Type == ATTACH && !UpdateWindowData()) 
                break;

            // Keep the frame callback running while the attached game window is
            // unfocused. Cheats::Run uses this path to refresh the web radar,
            // while draw submission remains disabled so the overlay stays hidden.
            bool renderFrame = true;
            if (Type == ATTACH)
            {
                const HWND foregroundWindow = GetForegroundWindow();
                renderFrame = foregroundWindow == DestWindow.hWnd || foregroundWindow == Window.hWnd;
            }

            ImGuiIO& io = ImGui::GetIO();

            static bool keyState[256] = { true }; // Track keys

            if (MenuConfig::ShowMenu) {
                for (int vkKey = 'A'; vkKey <= 'Z'; ++vkKey) {
                    toggleKey(vkKey, keyState[vkKey], io); // A-Z
                }

                for (int vkKey = '0'; vkKey <= '9'; ++vkKey) {
                    toggleKey(vkKey, keyState[vkKey], io); // 0-9
                }

                static bool LMouseState = true, RMouseState = true, MMouseState = true;
                static bool LeftCtrl = true, LeftShift = true, Backspace = true, Enter = true, Tab = true, Delete = true, ArrowUp = true, ArrowDown = true, ArrowLeft = true, ArrowRight = true;

                toggleKey(VK_LBUTTON, LMouseState, ImGuiMouseButton_Left, io);
                toggleKey(VK_RBUTTON, RMouseState, ImGuiMouseButton_Right, io);
                toggleKey(VK_MBUTTON, MMouseState, ImGuiMouseButton_Middle, io);
                toggleKey(VK_LCONTROL, LeftCtrl, ImGuiKey_LeftCtrl, ImGuiMod_Ctrl, io);
                toggleKey(VK_LSHIFT, LeftShift, ImGuiKey_LeftShift, ImGuiMod_Shift, io);
                toggleKey(VK_BACK, Backspace, ImGuiKey_Backspace, ImGuiMod_None, io);
                toggleKey(VK_RETURN, Enter, ImGuiKey_Enter, ImGuiMod_None, io);
                toggleKey(VK_TAB, Tab, ImGuiKey_Tab, ImGuiMod_None, io);
                toggleKey(VK_DELETE, Delete, ImGuiKey_Delete, ImGuiMod_None, io);
                toggleKey(VK_UP, ArrowUp, ImGuiKey_UpArrow, ImGuiMod_None, io);
                toggleKey(VK_DOWN, ArrowDown, ImGuiKey_DownArrow, ImGuiMod_None, io);
                toggleKey(VK_LEFT, ArrowLeft, ImGuiKey_LeftArrow, ImGuiMod_None, io);
                toggleKey(VK_RIGHT, ArrowRight, ImGuiKey_RightArrow, ImGuiMod_None, io);

                toggleKey(VK_OEM_1, keyState[VK_OEM_1], io);			// ;
                toggleKey(VK_OEM_PLUS, keyState[VK_OEM_PLUS], io);		// =
                toggleKey(VK_OEM_COMMA, keyState[VK_OEM_COMMA], io);	// <
                toggleKey(VK_OEM_MINUS, keyState[VK_OEM_MINUS], io);	// -
                toggleKey(VK_OEM_PERIOD, keyState[VK_OEM_PERIOD], io);	// >
                toggleKey(VK_SPACE, keyState[VK_SPACE], io);			// Space
                toggleKey(VK_OEM_2, keyState[VK_OEM_2], io);			// /
                toggleKey(VK_OEM_3, keyState[VK_OEM_3], io);			// `
                toggleKey(VK_OEM_4, keyState[VK_OEM_4], io);			// [
                toggleKey(VK_OEM_5, keyState[VK_OEM_5], io);			// |
                toggleKey(VK_OEM_6, keyState[VK_OEM_6], io);			// ]
                toggleKey(VK_OEM_7, keyState[VK_OEM_7], io);			// /
            } else { io.ClearInputKeys(); }


            // ImGui frame setup
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            CallBackFn();

            ImGui::Render();

            // Present once per vertical blank. A zero refresh-rate numerator lets
            // DXGI follow the desktop refresh rate of the current monitor.
            SetWindowDisplayAffinity(Window.hWnd, MenuConfig::BypassOBS ? excludeCapture : includeCapture);

            const float* actualClearColor = reinterpret_cast<const float*>(&Window.BgColor.Value);

            g_Device.g_pd3dDeviceContext->OMSetRenderTargets(1, &g_Device.g_mainRenderTargetView, nullptr);
            g_Device.g_pd3dDeviceContext->ClearRenderTargetView(g_Device.g_mainRenderTargetView, actualClearColor);
            if (renderFrame)
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            g_Device.g_pSwapChain->Present(1, 0);
        }
        CleanImGui();
    }

    bool OSImGui_External::CreateMyWindow()
    {
        // Use constexpr for compile-time constants
        constexpr WNDCLASSEXW wc = {
            .cbSize = sizeof(WNDCLASSEXW),
            .style = CS_CLASSDC,
            .lpfnWndProc = WndProc_External,
            .cbClsExtra = 0L,
            .cbWndExtra = 0L,
            .hInstance = nullptr, // Will be set below
            .hIcon = nullptr,
            .hCursor = nullptr,
            .hbrBackground = nullptr,
            .lpszMenuName = nullptr,
            .lpszClassName = nullptr, // Will be set below
            .hIconSm = nullptr
        };

        // Cache module handle
        static HINSTANCE hInstance = GetModuleHandle(nullptr);

        // Prepare window class with dynamic values
        WNDCLASSEXW windowClass = wc;
        windowClass.hInstance = hInstance;
        windowClass.lpszClassName = Window.wClassName.c_str();

        // Load CreateWindowInBand function pointer (cache it statically)
        static auto pCreateWindowInBand = []() -> auto {
            using CreateWindowInBandFunc = HWND(WINAPI*)(
                DWORD, ATOM, LPCWSTR, DWORD, int, int, int, int,
                HWND, HMENU, HINSTANCE, LPVOID, DWORD);

            return reinterpret_cast<CreateWindowInBandFunc>(
                GetProcAddress(GetModuleHandleW(L"user32.dll"), "CreateWindowInBand"));
            }();

        if (!pCreateWindowInBand) {
            MessageBoxW(nullptr, L"CreateWindowInBand is not supported on this OS.", L"Error", MB_OK | MB_ICONERROR);
            return false;
        }

        // Register the window class
        ATOM classAtom = RegisterClassExW(&windowClass);
        if (!classAtom) {
            return false;
        }

//do not use uaicess for debugging/profiling (uiacess restarts the cheat)
#ifndef DBDEBUG
        // Create the window with cached values
        constexpr DWORD exStyle = WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT;
        constexpr DWORD style = WS_POPUP;

        Window.hWnd = pCreateWindowInBand(
            exStyle, classAtom, Window.wName.c_str(), style,
            static_cast<int>(Window.Pos.x), static_cast<int>(Window.Pos.y),
            static_cast<int>(Window.Size.x), static_cast<int>(Window.Size.y),
            nullptr, nullptr, hInstance, nullptr, 2
        );

#else
        if (Type == ATTACH)
        {
            Window.hWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW, Window.wClassName.c_str(), Window.wName.c_str(), WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, nullptr, nullptr, hInstance, nullptr);
            SetLayeredWindowAttributes(Window.hWnd, 0, 255, LWA_ALPHA);
        }
        else
        {
            Window.hWnd = CreateWindowW(Window.wClassName.c_str(), Window.wName.c_str(), WS_OVERLAPPED | WS_MINIMIZEBOX | WS_SYSMENU, static_cast<int>(Window.Pos.x), static_cast<int>(Window.Pos.y), static_cast<int>(Window.Size.x), static_cast<int>(Window.Size.y), nullptr, nullptr, hInstance, nullptr);
        }
#endif

        Window.hInstance = hInstance;

        if (!Window.hWnd) {
            MessageBoxW(nullptr, L"CreateWindowInBand failed.", L"Error", MB_OK | MB_ICONERROR);
            UnregisterClassW(windowClass.lpszClassName, hInstance);
            return false;
        }

        // Setup D3D device
        if (!g_Device.CreateDeviceD3D(Window.hWnd)) {
            g_Device.CleanupDeviceD3D();
            UnregisterClassW(windowClass.lpszClassName, hInstance);
            return false;
        }

        ShowWindow(Window.hWnd, SW_SHOWDEFAULT);
        UpdateWindow(Window.hWnd);

        return true;
    }

    bool OSImGui_External::UpdateWindowData()
    {
        if (!IsWindow(DestWindow.hWnd))
        {
            if (DestWindow.Name.empty() && DestWindow.ClassName.empty())
                return false;

            const char* className = DestWindow.ClassName.empty() ? nullptr : DestWindow.ClassName.c_str();
            const char* windowName = DestWindow.Name.empty() ? nullptr : DestWindow.Name.c_str();
            DestWindow.hWnd = FindWindowA(className, windowName);
            if (!DestWindow.hWnd)
                return false;
        }

        RECT rect;
        POINT point = { 0, 0 };

        GetClientRect(DestWindow.hWnd, &rect);
        ClientToScreen(DestWindow.hWnd, &point);

        // Update positions and sizes
        const Vec2 newPos(static_cast<float>(point.x), static_cast<float>(point.y));
        const Vec2 newSize(static_cast<float>(rect.right), static_cast<float>(rect.bottom));

        Window.Pos = DestWindow.Pos = newPos;
        Window.Size = DestWindow.Size = newSize;


        static bool init = true;
        if (init) {
            SetWindowPos(Window.hWnd, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            init = false;
        }
        SetWindowPos(Window.hWnd, nullptr, 
            static_cast<int>(newPos.x), static_cast<int>(newPos.y),
            static_cast<int>(newSize.x), static_cast<int>(newSize.y),
            SWP_SHOWWINDOW | SWP_NOZORDER);

        // Update mouse position for ImGui
        POINT mousePos;
        GetCursorPos(&mousePos);
        ScreenToClient(Window.hWnd, &mousePos);

        auto& io = ImGui::GetIO();
        io.MousePos.x = static_cast<float>(mousePos.x);
        io.MousePos.y = static_cast<float>(mousePos.y);

        // Optimize window layering based on mouse capture
        const LONG currentExStyle = GetWindowLong(Window.hWnd, GWL_EXSTYLE);
        if (io.WantCaptureMouse) {
            SetWindowLong(Window.hWnd, GWL_EXSTYLE, currentExStyle & (~WS_EX_LAYERED));
        }
        else {
            SetWindowLong(Window.hWnd, GWL_EXSTYLE, currentExStyle | WS_EX_LAYERED);
        }

        return true;
    }

    LRESULT WINAPI WndProc_External(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return true;

        switch (msg)
        {
        case WM_CREATE:
        {
            constexpr MARGINS margin = { -1, -1, -1, -1 };
            DwmExtendFrameIntoClientArea(hWnd, &margin);
            break;
        }
        case WM_SIZE:
            if (g_Device.g_pd3dDevice && wParam != SIZE_MINIMIZED)
            {
                g_Device.CleanupRenderTarget();
                g_Device.g_pSwapChain->ResizeBuffers(0,
                    static_cast<UINT>(LOWORD(lParam)),
                    static_cast<UINT>(HIWORD(lParam)),
                    DXGI_FORMAT_UNKNOWN, 0);
                g_Device.CreateRenderTarget();
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU)
                return 0;
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

LRESULT CALLBACK OSImGui::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        auto* p = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        if (wParam == WM_KEYDOWN) {
            if (p->vkCode == MenuConfig::HotKey) {
                MenuConfig::ShowMenu = !MenuConfig::ShowMenu;
            }
            if (p->vkCode == ESPConfig::HotKey) {
                ESPConfig::ESPenabled = !ESPConfig::ESPenabled;
            }
        }
    }
    return CallNextHookEx(g_keyboard_hook, nCode, wParam, lParam);
}