// Stacks IDE — Windows desktop shell
// - Hosts the web UI via WebView2
// - Applies Mica backdrop (Windows 11) for the Fluent 2020s look
// - Spawns the local stacksd daemon on startup; points WebView2 at the local UI
//
// Build prerequisites:
//   * Visual Studio 2022 (Desktop C++ workload)
//   * WebView2 SDK NuGet package (Microsoft.Web.WebView2)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <shlwapi.h>
#include <wrl.h>
#include <WebView2.h>

#include <filesystem>
#include <string>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "user32.lib")

using namespace Microsoft::WRL;
namespace fs = std::filesystem;

static HWND g_hWnd = nullptr;
static ComPtr<ICoreWebView2Controller> g_controller;
static ComPtr<ICoreWebView2> g_webview;
static PROCESS_INFORMATION g_daemon{};

static std::wstring app_dir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    PathRemoveFileSpecW(path);
    return std::wstring(path);
}

// Enable Mica (Windows 11 22H2+) + dark mode title bar.
static void apply_mica(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    int backdrop = 2; // DWMSBT_MAINWINDOW (Mica)
    DwmSetWindowAttribute(hwnd, 38 /* DWMWA_SYSTEMBACKDROP_TYPE */, &backdrop, sizeof(backdrop));
    MARGINS m{ -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(hwnd, &m);
}

static void start_daemon() {
    std::wstring exe = app_dir() + L"\\stacksd.exe";
    if (!fs::exists(exe)) return; // graceful: web UI will run offline
    STARTUPINFOW si{ sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    std::wstring cmd = L"\"" + exe + L"\"";
    CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                   CREATE_NO_WINDOW, nullptr, nullptr, &si, &g_daemon);
}

static void stop_daemon() {
    if (g_daemon.hProcess) {
        TerminateProcess(g_daemon.hProcess, 0);
        CloseHandle(g_daemon.hProcess);
        CloseHandle(g_daemon.hThread);
        g_daemon = {};
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        if (g_controller) {
            RECT r; GetClientRect(hwnd, &r);
            g_controller->put_Bounds(r);
        }
        return 0;
    case WM_DESTROY:
        stop_daemon();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void init_webview() {
    std::wstring html = L"file:///" + app_dir() + L"\\web\\index.html";
    CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [html](HRESULT, ICoreWebView2Environment* env) -> HRESULT {
                env->CreateCoreWebView2Controller(g_hWnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [html](HRESULT, ICoreWebView2Controller* c) -> HRESULT {
                            if (!c) return S_OK;
                            g_controller = c;
                            g_controller->get_CoreWebView2(&g_webview);
                            // Transparent BG for Mica
                            COREWEBVIEW2_COLOR bg{ 0, 0, 0, 0 };
                            ComPtr<ICoreWebView2Controller2> c2;
                            g_controller.As(&c2);
                            if (c2) c2->put_DefaultBackgroundColor(bg);
                            RECT r; GetClientRect(g_hWnd, &r);
                            g_controller->put_Bounds(r);
                            g_webview->Navigate(html.c_str());
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nShow) {
    WNDCLASSW wc{};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"StacksIDE";
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    g_hWnd = CreateWindowExW(0, L"StacksIDE", L"Stacks IDE",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        1280, 820, nullptr, nullptr, hInst, nullptr);

    apply_mica(g_hWnd);
    ShowWindow(g_hWnd, nShow);

    start_daemon();
    init_webview();

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
