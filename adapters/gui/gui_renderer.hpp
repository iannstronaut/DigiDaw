#pragma once

#include "theme.hpp"
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

namespace digidaw::adapters::gui {

class ScopedGdiObject {
public:
    ScopedGdiObject(HDC hdc, HGDIOBJ obj) : hdc_(hdc), prev_(SelectObject(hdc, obj)), obj_(obj) {}
    ~ScopedGdiObject() {
        if (hdc_ && prev_) {
            SelectObject(hdc_, prev_);
        }
        if (obj_) {
            DeleteObject(obj_);
        }
    }
    ScopedGdiObject(const ScopedGdiObject&) = delete;
    ScopedGdiObject& operator=(const ScopedGdiObject&) = delete;

private:
    HDC hdc_;
    HGDIOBJ prev_;
    HGDIOBJ obj_;
};

class GuiRenderer {
public:
    static void fill_rect(HDC hdc, const RECT& rc, COLORREF color) {
        HBRUSH br = CreateSolidBrush(color);
        FillRect(hdc, &rc, br);
        DeleteObject(br);
    }

    static void draw_border(HDC hdc, const RECT& rc, COLORREF color, int thickness = 1) {
        HPEN pen = CreatePen(PS_SOLID, thickness, color);
        HGDIOBJ old_pen = SelectObject(hdc, pen);
        HGDIOBJ old_br = SelectObject(hdc, GetStockObject(NULL_BRUSH));

        Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);

        SelectObject(hdc, old_br);
        SelectObject(hdc, old_pen);
        DeleteObject(pen);
    }

    static void draw_rounded_box(HDC hdc, const RECT& rc, COLORREF bg_color, COLORREF border_color, int radius = 6) {
        HBRUSH br = CreateSolidBrush(bg_color);
        HPEN pen = CreatePen(PS_SOLID, 1, border_color);

        HGDIOBJ old_br = SelectObject(hdc, br);
        HGDIOBJ old_pen = SelectObject(hdc, pen);

        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);

        SelectObject(hdc, old_pen);
        SelectObject(hdc, old_br);
        DeleteObject(pen);
        DeleteObject(br);
    }

    static void draw_text(HDC hdc, const std::string& text, const RECT& rc, COLORREF color,
                          UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
        if (text.empty()) return;
        SetTextColor(hdc, color);
        SetBkMode(hdc, TRANSPARENT);
        RECT temp_rc = rc;
        int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        if (len > 0) {
            std::wstring wstr(len, 0);
            MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wstr[0], len);
            if (!wstr.empty() && wstr.back() == L'\0') wstr.pop_back();
            DrawTextW(hdc, wstr.c_str(), -1, &temp_rc, format);
        }
    }

    static void draw_button(HDC hdc, const RECT& rc, const std::string& text, bool active,
                            COLORREF active_color, COLORREF normal_bg) {
        const auto& t = get_theme();
        COLORREF bg = active ? active_color : normal_bg;
        COLORREF border = active ? t.accent_amber : t.border_dark;
        COLORREF txt_col = active ? RGB(20, 20, 20) : t.text_primary;

        draw_rounded_box(hdc, rc, bg, border, 6);
        draw_text(hdc, text, rc, txt_col, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    static void draw_meter_vertical(HDC hdc, const RECT& rc, float peak_level) {
        const auto& t = get_theme();
        fill_rect(hdc, rc, t.meter_bg);
        draw_border(hdc, rc, t.border_dark);

        float clamped = std::clamp(peak_level, 0.0f, 1.0f);
        int h = rc.bottom - rc.top - 2;
        int active_h = static_cast<int>(h * clamped);
        if (active_h <= 0) return;

        RECT active_rc = rc;
        active_rc.top = rc.bottom - 1 - active_h;
        active_rc.bottom = rc.bottom - 1;
        active_rc.left += 1;
        active_rc.right -= 1;

        COLORREF meter_col = t.meter_green;
        if (clamped > 0.85f) {
            meter_col = t.meter_red;
        } else if (clamped > 0.65f) {
            meter_col = t.meter_yellow;
        }

        fill_rect(hdc, active_rc, meter_col);
    }

    static void draw_slider_vertical(HDC hdc, const RECT& rc, float normalized_val, const std::string& label) {
        const auto& t = get_theme();
        // Track
        int track_x = (rc.left + rc.right) / 2;
        RECT track_rc{track_x - 3, rc.top + 6, track_x + 3, rc.bottom - 20};
        fill_rect(hdc, track_rc, t.bg_input);
        draw_border(hdc, track_rc, t.border_dark);

        // Active fill from bottom to thumb
        int track_h = (rc.bottom - 20) - (rc.top + 6);
        float norm = std::clamp(normalized_val, 0.0f, 1.0f);
        int fill_h = static_cast<int>(track_h * norm);
        if (fill_h > 0) {
            RECT fill_rc{track_x - 2, (rc.bottom - 20) - fill_h, track_x + 2, rc.bottom - 20};
            fill_rect(hdc, fill_rc, t.accent_orange);
        }

        // Thumb handle
        int thumb_y = (rc.bottom - 20) - fill_h;
        RECT thumb_rc{rc.left + 6, thumb_y - 5, rc.right - 6, thumb_y + 5};
        draw_rounded_box(hdc, thumb_rc, RGB(220, 220, 220), t.border_dark, 3);

        // Center notch line on thumb
        RECT notch_rc{rc.left + 10, thumb_y - 1, rc.right - 10, thumb_y + 1};
        fill_rect(hdc, notch_rc, RGB(80, 80, 80));

        // Readout label at bottom
        RECT text_rc{rc.left - 4, rc.bottom - 18, rc.right + 4, rc.bottom};
        draw_text(hdc, label, text_rc, t.text_secondary, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    static void draw_slider_horizontal(HDC hdc, const RECT& rc, float normalized_val, const std::string& label) {
        const auto& t = get_theme();
        draw_rounded_box(hdc, rc, t.bg_input, t.border_dark, 4);

        int w = rc.right - rc.left - 2;
        float norm = std::clamp(normalized_val, 0.0f, 1.0f);
        int fill_w = static_cast<int>(w * norm);
        if (fill_w > 0) {
            RECT fill_rc{rc.left + 1, rc.top + 1, rc.left + 1 + fill_w, rc.bottom - 1};
            fill_rect(hdc, fill_rc, t.border_light);
        }

        draw_text(hdc, label, rc, t.text_primary, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // Downward-pointing triangle playhead marker with vertical line extending across the sequencer
    static void draw_playhead(HDC hdc, int center_x, int top_y, int bottom_y, COLORREF color, const std::string& label = "") {
        // 1. Downward-pointing triangle (apex pointing down!)
        POINT pts[3];
        pts[0] = {center_x - 10, top_y};
        pts[1] = {center_x + 10, top_y};
        pts[2] = {center_x,      top_y + 16}; // Apex pointing straight down into the sequencer

        HBRUSH br = CreateSolidBrush(color);
        HPEN border_pen = CreatePen(PS_SOLID, 1, RGB(15, 15, 15));
        HGDIOBJ old_br = SelectObject(hdc, br);
        HGDIOBJ old_pen = SelectObject(hdc, border_pen);

        Polygon(hdc, pts, 3);

        // 2. Vertical line extending all the way down through the sequencer
        HPEN line_pen = CreatePen(PS_SOLID, 2, color);
        SelectObject(hdc, line_pen);
        MoveToEx(hdc, center_x, top_y + 16, NULL);
        LineTo(hdc, center_x, bottom_y);

        // Base foot cap
        MoveToEx(hdc, center_x - 5, bottom_y, NULL);
        LineTo(hdc, center_x + 6, bottom_y);

        // 3. Optional badge/label above the triangle
        if (!label.empty()) {
            RECT lbl_rc{center_x - 35, top_y - 14, center_x + 35, top_y};
            SetTextColor(hdc, color);
            SetBkMode(hdc, TRANSPARENT);
            DrawTextA(hdc, label.c_str(), -1, &lbl_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        SelectObject(hdc, old_pen);
        SelectObject(hdc, old_br);
        DeleteObject(line_pen);
        DeleteObject(border_pen);
        DeleteObject(br);
    }
};

} // namespace digidaw::adapters::gui
