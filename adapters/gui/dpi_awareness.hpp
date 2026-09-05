#pragma once

#include <windows.h>

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE ((DPI_AWARENESS_CONTEXT)-3)
#endif

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif

namespace digidaw::adapters::gui {

/**
 * @brief High-DPI Awareness Manager for Windows
 *
 * Ensures DigiDAW runs in Per-Monitor V2 DPI Awareness mode,
 * preventing Windows Desktop Window Manager (DWM) from bitmap-stretching
 * the window on high-resolution and scaled displays (125%, 150%, 175%, 200%).
 * Renders sharp, crisp native HD pixels on all monitors.
 */
class DpiAwareness {
public:
    /**
     * @brief Enable Per-Monitor V2 DPI awareness with fallback chain
     *
     * Fallback hierarchy:
     * 1. SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) [Win10 1703+]
     * 2. SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE)    [Win10 1607+]
     * 3. SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE)                     [Win8.1 / Win10]
     * 4. SetProcessDPIAware()                                                     [Win Vista / 7]
     */
    static bool enable_high_dpi_awareness() {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32) {
            typedef BOOL (WINAPI *SetProcessDpiAwarenessContextProc)(DPI_AWARENESS_CONTEXT);
            auto pSetProcessDpiAwarenessContext = reinterpret_cast<SetProcessDpiAwarenessContextProc>(
                GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
            if (pSetProcessDpiAwarenessContext) {
                if (pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
                    return true;
                }
                if (pSetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE)) {
                    return true;
                }
            }
        }

        // Fallback for Windows 8.1 / earlier Windows 10: shcore.dll SetProcessDpiAwareness
        HMODULE shcore = LoadLibraryW(L"shcore.dll");
        if (shcore) {
            typedef HRESULT (WINAPI *SetProcessDpiAwarenessProc)(int);
            auto pSetProcessDpiAwareness = reinterpret_cast<SetProcessDpiAwarenessProc>(
                GetProcAddress(shcore, "SetProcessDpiAwareness"));
            if (pSetProcessDpiAwareness) {
                // PROCESS_PER_MONITOR_DPI_AWARE = 2
                HRESULT hr = pSetProcessDpiAwareness(2);
                FreeLibrary(shcore);
                if (SUCCEEDED(hr)) {
                    return true;
                }
            } else {
                FreeLibrary(shcore);
            }
        }

        // Fallback for Windows Vista / 7: user32.dll SetProcessDPIAware
        if (user32) {
            typedef BOOL (WINAPI *SetProcessDPIAwareProc)();
            auto pSetProcessDPIAware = reinterpret_cast<SetProcessDPIAwareProc>(
                GetProcAddress(user32, "SetProcessDPIAware"));
            if (pSetProcessDPIAware) {
                return (pSetProcessDPIAware() != FALSE);
            }
        }

        return false;
    }

    /**
     * @brief Query current DPI for a given window, falling back safely to monitor or desktop DPI
     */
    static UINT get_dpi_for_window(HWND hwnd) {
        if (hwnd) {
            HMODULE user32 = GetModuleHandleW(L"user32.dll");
            if (user32) {
                typedef UINT (WINAPI *GetDpiForWindowProc)(HWND);
                auto pGetDpiForWindow = reinterpret_cast<GetDpiForWindowProc>(
                    GetProcAddress(user32, "GetDpiForWindow"));
                if (pGetDpiForWindow) {
                    UINT dpi = pGetDpiForWindow(hwnd);
                    if (dpi > 0) return dpi;
                }
            }

            HDC hdc = GetDC(hwnd);
            if (hdc) {
                int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
                ReleaseDC(hwnd, hdc);
                if (dpi > 0) return static_cast<UINT>(dpi);
            }
        }
        return 96;
    }

    /**
     * @brief Query DPI scale factor relative to baseline 96 DPI (e.g. 1.25 for 125%, 1.5 for 150%)
     */
    static float get_scale_factor(HWND hwnd) {
        return static_cast<float>(get_dpi_for_window(hwnd)) / 96.0f;
    }

    /**
     * @brief Enable non-client DPI scaling for a window (title bar, scrollbars, borders)
     */
    static bool enable_non_client_dpi_scaling(HWND hwnd) {
        if (!hwnd) return false;
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32) {
            typedef BOOL (WINAPI *EnableNonClientDpiScalingProc)(HWND);
            auto pEnableNonClientDpiScaling = reinterpret_cast<EnableNonClientDpiScalingProc>(
                GetProcAddress(user32, "EnableNonClientDpiScaling"));
            if (pEnableNonClientDpiScaling) {
                return (pEnableNonClientDpiScaling(hwnd) != FALSE);
            }
        }
        return false;
    }
};

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

} // namespace digidaw::adapters::gui
