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
        SetTextColor(hdc, color);
        SetBkMode(hdc, TRANSPARENT);
        RECT temp_rc = rc;
        DrawTextA(hdc, text.c_str(), -1, &temp_rc, format);
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
};

} // namespace digidaw::adapters::gui
