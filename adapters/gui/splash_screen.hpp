#pragma once

#include <windows.h>
#include <dwmapi.h>
#include <string>
#include <chrono>
#include <thread>
#include <algorithm>

namespace digidaw::adapters::gui {

class SplashScreen {
public:
    SplashScreen() = default;
    ~SplashScreen() { close(); }

    bool show(HINSTANCE hInstance, int width = 560, int height = 300) {
        hinstance_ = hInstance;
        width_ = width;
        height_ = height;

        WNDCLASSEXA wc{};
        wc.cbSize = sizeof(WNDCLASSEXA);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = "DigiDawSplashScreenClass";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        RegisterClassExA(&wc);

        int screen_w = GetSystemMetrics(SM_CXSCREEN);
        int screen_h = GetSystemMetrics(SM_CYSCREEN);
        int x = (screen_w - width_) / 2;
        int y = (screen_h - height_) / 2;

        hwnd_ = CreateWindowExA(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            "DigiDawSplashScreenClass",
            "DigiDAW 2026 Startup",
            WS_POPUP,
            x, y, width_, height_,
            NULL, NULL, hInstance, this
        );

        if (!hwnd_) return false;

        // Dark mode DWM preference
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd_, 20, &dark, sizeof(dark));

        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
        pump_events();
        return true;
    }

    void update(const std::string& status, float progress_0_to_1) {
        status_text_ = status;
        progress_ = std::clamp(progress_0_to_1, 0.0f, 1.0f);
        if (hwnd_) {
            InvalidateRect(hwnd_, NULL, FALSE);
            UpdateWindow(hwnd_);
            pump_events();
        }
    }

    void close() {
        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = NULL;
        }
        if (hinstance_) {
            UnregisterClassA("DigiDawSplashScreenClass", hinstance_);
            hinstance_ = NULL;
        }
    }

    void pump_events() {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        SplashScreen* self = nullptr;
        if (msg == WM_CREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTA*>(lp);
            self = reinterpret_cast<SplashScreen*>(cs->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<SplashScreen*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }

        switch (msg) {
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                if (self) self->render(hdc);
                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_ERASEBKGND:
                return 1;
            default:
                return DefWindowProcA(hwnd, msg, wp, lp);
        }
    }

    void render(HDC hdc) {
        HDC mem_dc = CreateCompatibleDC(hdc);
        HBITMAP mem_bm = CreateCompatibleBitmap(hdc, width_, height_);
        HGDIOBJ old_bm = SelectObject(mem_dc, mem_bm);

        // Background dark surface (#0c0e12)
        HBRUSH bg_br = CreateSolidBrush(RGB(12, 14, 18));
        RECT full_rc = {0, 0, width_, height_};
        FillRect(mem_dc, &full_rc, bg_br);
        DeleteObject(bg_br);

        // Outer subtle border (#242830)
        HPEN border_pen = CreatePen(PS_SOLID, 1, RGB(36, 40, 48));
        HGDIOBJ old_pen = SelectObject(mem_dc, border_pen);
        SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
        Rectangle(mem_dc, 0, 0, width_, height_);
        SelectObject(mem_dc, old_pen);
        DeleteObject(border_pen);

        // Top glowing accent hairline (Electric Cyan #00d2ff)
        HPEN glow_pen = CreatePen(PS_SOLID, 2, RGB(0, 210, 255));
        HGDIOBJ old_gpen = SelectObject(mem_dc, glow_pen);
        MoveToEx(mem_dc, 0, 1, NULL);
        LineTo(mem_dc, width_, 1);
        SelectObject(mem_dc, old_gpen);
        DeleteObject(glow_pen);

        // Text mode
        SetBkMode(mem_dc, TRANSPARENT);

        // Logo / Title: DIGIDAW
        HFONT hTitleFont = CreateFontA(36, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                      DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        HGDIOBJ old_font = SelectObject(mem_dc, hTitleFont);
        SetTextColor(mem_dc, RGB(248, 250, 252));
        RECT title_rc = {36, 34, width_ - 36, 78};
        DrawTextA(mem_dc, "DIGIDAW 2026", -1, &title_rc, DT_LEFT | DT_SINGLELINE);

        // Subtitle / Architecture
        HFONT hSubFont = CreateFontA(13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        SelectObject(mem_dc, hSubFont);
        SetTextColor(mem_dc, RGB(0, 210, 255));
        RECT sub_rc = {36, 80, width_ - 36, 100};
        DrawTextA(mem_dc, "CLEAN-ROOM DIGITAL AUDIO WORKSTATION • C++20 HYBRID ENGINE", -1, &sub_rc, DT_LEFT | DT_SINGLELINE);

        // Description tags
        HFONT hTagFont = CreateFontA(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        SelectObject(mem_dc, hTagFont);
        SetTextColor(mem_dc, RGB(120, 134, 148));
        RECT tag_rc = {36, 108, width_ - 36, 126};
        DrawTextA(mem_dc, "High-DPI Per-Monitor V2 • Direct2D 60FPS • Integrated Audio Library", -1, &tag_rc, DT_LEFT | DT_SINGLELINE);

        // Status text
        SetTextColor(mem_dc, RGB(200, 210, 222));
        RECT stat_rc = {36, height_ - 88, width_ - 120, height_ - 68};
        DrawTextA(mem_dc, status_text_.c_str(), -1, &stat_rc, DT_LEFT | DT_SINGLELINE);

        // Percentage text
        int pct = static_cast<int>(progress_ * 100.0f);
        std::string pct_str = std::to_string(pct) + "%";
        SetTextColor(mem_dc, RGB(0, 210, 255));
        RECT pct_rc = {width_ - 100, height_ - 88, width_ - 36, height_ - 68};
        DrawTextA(mem_dc, pct_str.c_str(), -1, &pct_rc, DT_RIGHT | DT_SINGLELINE);

        // Progress track
        int bar_x = 36;
        int bar_y = height_ - 56;
        int bar_w = width_ - 72;
        int bar_h = 6;
        HBRUSH track_br = CreateSolidBrush(RGB(22, 26, 32));
        RECT bar_track_rc = {bar_x, bar_y, bar_x + bar_w, bar_y + bar_h};
        FillRect(mem_dc, &bar_track_rc, track_br);
        DeleteObject(track_br);

        // Progress fill (Electric cyan)
        int fill_w = static_cast<int>(bar_w * progress_);
        if (fill_w > 0) {
            HBRUSH fill_br = CreateSolidBrush(RGB(0, 210, 255));
            RECT bar_fill_rc = {bar_x, bar_y, bar_x + fill_w, bar_y + bar_h};
            FillRect(mem_dc, &bar_fill_rc, fill_br);
            DeleteObject(fill_br);
        }

        BitBlt(hdc, 0, 0, width_, height_, mem_dc, 0, 0, SRCCOPY);

        SelectObject(mem_dc, old_font);
        DeleteObject(hTitleFont);
        DeleteObject(hSubFont);
        DeleteObject(hTagFont);
        SelectObject(mem_dc, old_bm);
        DeleteObject(mem_bm);
        DeleteDC(mem_dc);
    }

    HWND hwnd_{NULL};
    HINSTANCE hinstance_{NULL};
    int width_{560};
    int height_{300};
    std::string status_text_{"Initializing DigiDAW..."};
    float progress_{0.0f};
};

} // namespace digidaw::adapters::gui
