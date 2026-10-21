#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <chrono>
#include <ctime>
#include <filesystem>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

struct Config {
    int hotkey_vk = VK_F6;
    int hotkey_modifiers = 0;
    std::string output_dir = "";
    std::string filename_pattern = "screenshot_%Y-%m-%d_%H-%M-%S";
    bool show_notifications = true;
};

static Config g_config;

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace gdi {
    struct HBITMAP_Deleter { void operator()(HBITMAP h) noexcept { if (h) DeleteObject(h); } };
    struct HDC_Deleter { void operator()(HDC h) noexcept { if (h) DeleteDC(h); } };
    struct HGDIOBJ_Deleter { void operator()(HGDIOBJ h) noexcept { if (h) DeleteObject(h); } };
    using unique_hbitmap = std::unique_ptr<std::remove_pointer<HBITMAP>::type, HBITMAP_Deleter>;
    using unique_hdc = std::unique_ptr<std::remove_pointer<HDC>::type, HDC_Deleter>;
    using unique_hgdiobj = std::unique_ptr<std::remove_pointer<HGDIOBJ>::type, HGDIOBJ_Deleter>;
}

namespace fs = std::filesystem;

static fs::path get_exe_directory() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return fs::path(path).parent_path();
}

static std::string make_filename() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
    localtime_s(&tm_buf, &time_t_now);

    fs::path dir = g_config.output_dir.empty() ? get_exe_directory() : fs::path(g_config.output_dir);
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) dir = get_exe_directory();

    char filename[128];
    strftime(filename, sizeof(filename), g_config.filename_pattern.c_str(), &tm_buf);
    return (dir / filename).string() + ".png";
}

static void convert_bgr_to_rgb(void* pBits, int width, int height) {
    auto* row = static_cast<uint8_t*>(pBits);
    const int stride = width * 4;
    for (int y = 0; y < height; ++y) {
        uint8_t* p = row;
        for (int x = 0; x < width; ++x) {
            std::swap(p[0], p[2]);
            p[3] = 255;
            p += 4;
        }
        row += stride;
    }
}

static bool capture_screen(const std::string& filename) {
    gdi::unique_hdc hdcScreen(GetDC(nullptr));
    if (!hdcScreen) return false;

    int cx = GetDeviceCaps(hdcScreen.get(), HORZRES);
    int cy = GetDeviceCaps(hdcScreen.get(), VERTRES);

    gdi::unique_hdc hdcMem(CreateCompatibleDC(hdcScreen.get()));
    if (!hdcMem) return false;

    BITMAPINFOHEADER bi = {};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = cx;
    bi.biHeight = cy;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    void* pBits = nullptr;
    gdi::unique_hbitmap hbm(CreateDIBSection(hdcScreen.get(),
        reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &pBits, nullptr, 0));
    if (!hbm || !pBits) return false;

    HGDIOBJ oldObj = SelectObject(hdcMem.get(), hbm.get());
    BitBlt(hdcMem.get(), 0, 0, cx, cy, hdcScreen.get(), 0, 0, SRCCOPY);
    SelectObject(hdcMem.get(), oldObj);

    convert_bgr_to_rgb(pBits, cx, cy);

    stbi_write_png_compression_level = 9;
    stbi_flip_vertically_on_write(1);
    bool ok = stbi_write_png(filename.c_str(), cx, cy, 4, pBits, cx * 4);
    stbi_flip_vertically_on_write(0);

    return ok;
}

#define TRAY_ICON_ID 1
#define WM_TRAYICON (WM_APP + 1)
#define HOTKEY_ID_SCREENSHOT 1
#define IDM_CAPTURE 40001
#define IDM_OPEN_FOLDER 40002
#define IDM_EXIT 40003

static HWND g_hwnd = nullptr;
static NOTIFYICONDATAW g_nid = {};
static std::atomic<bool> g_running{ true };

static void show_balloon(const wchar_t* title, const wchar_t* message, UINT flags = NIIF_INFO) {
    if (!g_config.show_notifications) return;
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = TRAY_ICON_ID;
    nid.uFlags = NIF_INFO;
    wcscpy_s(nid.szInfo, 256, message);
    wcscpy_s(nid.szInfoTitle, 64, title);
    nid.dwInfoFlags = flags | NIIF_NOSOUND;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void do_capture(HWND hwnd) {
    std::string filename = make_filename();
    if (capture_screen(filename)) {
        fs::path p(filename);
        std::wstring wname;
        wname.reserve(p.filename().string().size());
        for (auto c : p.filename().string()) wname += (wchar_t)c;
        show_balloon(L"Screenshot Captured", wname.c_str(), NIIF_INFO);
    } else {
        show_balloon(L"Screenshot Failed", L"Could not save screenshot", NIIF_ERROR);
    }
}

static void open_output_folder() {
    fs::path dir = g_config.output_dir.empty() ? get_exe_directory() : fs::path(g_config.output_dir);
    std::wstring wdir;
    wdir.reserve(dir.wstring().size());
    for (auto c : dir.wstring()) wdir += c;
    ShellExecuteW(nullptr, L"open", wdir.c_str(), nullptr, nullptr, SW_SHOW);
}

static void show_tray_menu(HWND hwnd) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_CAPTURE, L"Capture Screenshot (F6)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_OPEN_FOLDER, L"Open Output Folder");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(hMenu);

    switch (cmd) {
    case IDM_CAPTURE:
        do_capture(hwnd);
        break;
    case IDM_OPEN_FOLDER:
        open_output_folder();
        break;
    case IDM_EXIT:
        g_running.store(false);
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        break;
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAYICON:
        if (lParam == WM_LBUTTONUP || lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            show_tray_menu(hwnd);
        }
        break;

    case WM_HOTKEY:
        if (wParam == HOTKEY_ID_SCREENSHOT) {
            do_capture(hwnd);
        }
        break;

    case WM_CLOSE:
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        UnregisterHotKey(hwnd, HOTKEY_ID_SCREENSHOT);
        PostQuitMessage(0);
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool is_single_instance() {
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Screenshoter_SingleInstance_Mutex");
    if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return false;
    }
    return true;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    if (!is_single_instance()) {
        MessageBoxW(nullptr, L"Screenshoter is already running.\nCheck the system tray.",
                    L"Screenshoter", MB_OK | MB_ICONINFORMATION);
        return 1;
    }

    const wchar_t CLASS_NAME[] = L"ScreenshoterTrayApp";
    WNDCLASSW wc = {};
    wc.hInstance = hInstance;
    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassW(&wc);

    g_hwnd = CreateWindowExW(0, CLASS_NAME, L"Screenshoter", 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);
    if (!g_hwnd) return 1;

    if (!RegisterHotKey(g_hwnd, HOTKEY_ID_SCREENSHOT, g_config.hotkey_modifiers, g_config.hotkey_vk)) {
        MessageBoxW(nullptr, L"Failed to register hotkey (F6).\nAnother app may be using it.",
                    L"Screenshoter", MB_ICONERROR | MB_OK);
    }

    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = TRAY_ICON_ID;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, 128, L"Screenshoter");

    Shell_NotifyIconW(NIM_ADD, &g_nid);

    show_balloon(L"Screenshoter", L"Running in background. Press F6 to capture.", NIIF_INFO);

    MSG msg;
    while (g_running.load(std::memory_order_relaxed)) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        } else {
            Sleep(10);
        }
    }

    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    UnregisterHotKey(g_hwnd, HOTKEY_ID_SCREENSHOT);

    return 0;
}
