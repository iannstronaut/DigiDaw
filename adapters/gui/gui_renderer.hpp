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
        COLORREF border = active ? t.accent_bright : t.border_dark;
        COLORREF txt_col = active ? RGB(255, 255, 255) : t.text_primary;

        draw_rounded_box(hdc, rc, bg, border, 4);
        draw_text(hdc, text, rc, txt_col, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    static void draw_svg_icon(HDC hdc, SvgIconType icon, const RECT& rc, COLORREF color, int size = 16) {
        int cx = (rc.left + rc.right) / 2;
        int cy = (rc.top + rc.bottom) / 2;
        float s = static_cast<float>(size) / 24.0f;
        float ox = static_cast<float>(cx) - 12.0f * s;
        float oy = static_cast<float>(cy) - 12.0f * s;

        auto P = [&](float x, float y) -> POINT {
            return POINT{static_cast<int>(std::round(ox + x * s)), static_cast<int>(std::round(oy + y * s))};
        };
        auto R = [&](float x, float y, float w, float h) -> RECT {
            return RECT{
                static_cast<int>(std::round(ox + x * s)),
                static_cast<int>(std::round(oy + y * s)),
                static_cast<int>(std::round(ox + (x + w) * s)),
                static_cast<int>(std::round(oy + (y + h) * s))
            };
        };

        HBRUSH br = CreateSolidBrush(color);
        HPEN pen = CreatePen(PS_SOLID, std::max(1, static_cast<int>(std::round(1.5f * s))), color);
        HGDIOBJ old_br = SelectObject(hdc, br);
        HGDIOBJ old_pen = SelectObject(hdc, pen);

        switch (icon) {
        case SvgIconType::Play: {
            POINT pts[3] = { P(7.0f, 4.5f), P(19.5f, 12.0f), P(7.0f, 19.5f) };
            Polygon(hdc, pts, 3);
            break;
        }
        case SvgIconType::Pause: {
            RECT b1 = R(6.0f, 4.0f, 4.0f, 16.0f);
            RECT b2 = R(14.0f, 4.0f, 4.0f, 16.0f);
            RoundRect(hdc, b1.left, b1.top, b1.right, b1.bottom, 3, 3);
            RoundRect(hdc, b2.left, b2.top, b2.right, b2.bottom, 3, 3);
            break;
        }
        case SvgIconType::Stop: {
            RECT sq = R(4.5f, 4.5f, 15.0f, 15.0f);
            RoundRect(hdc, sq.left, sq.top, sq.right, sq.bottom, 4, 4);
            break;
        }
        case SvgIconType::ChannelRack: {
            RECT r1 = R(3.0f, 4.0f, 5.0f, 4.0f);
            RECT r2 = R(10.0f, 4.0f, 11.0f, 4.0f);
            RECT r3 = R(3.0f, 10.0f, 5.0f, 4.0f);
            RECT r4 = R(10.0f, 10.0f, 11.0f, 4.0f);
            RECT r5 = R(3.0f, 16.0f, 5.0f, 4.0f);
            RECT r6 = R(10.0f, 16.0f, 11.0f, 4.0f);
            RoundRect(hdc, r1.left, r1.top, r1.right, r1.bottom, 2, 2);
            RoundRect(hdc, r2.left, r2.top, r2.right, r2.bottom, 2, 2);
            RoundRect(hdc, r3.left, r3.top, r3.right, r3.bottom, 2, 2);
            RoundRect(hdc, r4.left, r4.top, r4.right, r4.bottom, 2, 2);
            RoundRect(hdc, r5.left, r5.top, r5.right, r5.bottom, 2, 2);
            RoundRect(hdc, r6.left, r6.top, r6.right, r6.bottom, 2, 2);
            break;
        }
        case SvgIconType::Playlist: {
            MoveToEx(hdc, P(3.0f, 5.5f).x, P(3.0f, 5.5f).y, NULL);
            LineTo(hdc, P(21.0f, 5.5f).x, P(21.0f, 5.5f).y);
            MoveToEx(hdc, P(3.0f, 12.0f).x, P(3.0f, 12.0f).y, NULL);
            LineTo(hdc, P(21.0f, 12.0f).x, P(21.0f, 12.0f).y);
            MoveToEx(hdc, P(3.0f, 18.5f).x, P(3.0f, 18.5f).y, NULL);
            LineTo(hdc, P(21.0f, 18.5f).x, P(21.0f, 18.5f).y);

            RECT r1 = R(4.0f, 3.5f, 8.0f, 4.0f);
            RECT r2 = R(9.0f, 10.0f, 11.0f, 4.0f);
            RECT r3 = R(4.0f, 16.5f, 9.0f, 4.0f);
            RoundRect(hdc, r1.left, r1.top, r1.right, r1.bottom, 2, 2);
            RoundRect(hdc, r2.left, r2.top, r2.right, r2.bottom, 2, 2);
            RoundRect(hdc, r3.left, r3.top, r3.right, r3.bottom, 2, 2);
            break;
        }
        case SvgIconType::PianoRoll: {
            RECT out_r = R(3.0f, 3.0f, 18.0f, 18.0f);
            HGDIOBJ null_br = GetStockObject(NULL_BRUSH);
            SelectObject(hdc, null_br);
            RoundRect(hdc, out_r.left, out_r.top, out_r.right, out_r.bottom, 4, 4);
            SelectObject(hdc, br);

            MoveToEx(hdc, P(8.5f, 3.0f).x, P(8.5f, 3.0f).y, NULL);
            LineTo(hdc, P(8.5f, 21.0f).x, P(8.5f, 21.0f).y);
            MoveToEx(hdc, P(3.0f, 9.0f).x, P(3.0f, 9.0f).y, NULL);
            LineTo(hdc, P(8.5f, 9.0f).x, P(8.5f, 9.0f).y);
            MoveToEx(hdc, P(3.0f, 15.0f).x, P(3.0f, 15.0f).y, NULL);
            LineTo(hdc, P(8.5f, 15.0f).x, P(8.5f, 15.0f).y);

            RECT bk1 = R(5.5f, 5.5f, 3.0f, 2.5f);
            RECT bk2 = R(5.5f, 12.0f, 3.0f, 2.5f);
            RoundRect(hdc, bk1.left, bk1.top, bk1.right, bk1.bottom, 2, 2);
            RoundRect(hdc, bk2.left, bk2.top, bk2.right, bk2.bottom, 2, 2);

            RECT n1 = R(11.0f, 5.5f, 5.5f, 2.5f);
            RECT n2 = R(14.0f, 9.5f, 5.0f, 2.5f);
            RECT n3 = R(11.5f, 13.5f, 6.0f, 2.5f);
            RECT n4 = R(13.0f, 17.0f, 4.5f, 2.5f);
            RoundRect(hdc, n1.left, n1.top, n1.right, n1.bottom, 2, 2);
            RoundRect(hdc, n2.left, n2.top, n2.right, n2.bottom, 2, 2);
            RoundRect(hdc, n3.left, n3.top, n3.right, n3.bottom, 2, 2);
            RoundRect(hdc, n4.left, n4.top, n4.right, n4.bottom, 2, 2);
            break;
        }
        case SvgIconType::Inspector: {
            RECT out_r = R(3.0f, 3.0f, 18.0f, 18.0f);
            HGDIOBJ null_br = GetStockObject(NULL_BRUSH);
            SelectObject(hdc, null_br);
            RoundRect(hdc, out_r.left, out_r.top, out_r.right, out_r.bottom, 4, 4);
            SelectObject(hdc, br);

            // Right inspector divider
            MoveToEx(hdc, P(14.0f, 3.0f).x, P(14.0f, 3.0f).y, NULL);
            LineTo(hdc, P(14.0f, 21.0f).x, P(14.0f, 21.0f).y);

            // Left track lines
            MoveToEx(hdc, P(5.5f, 7.5f).x, P(5.5f, 7.5f).y, NULL);
            LineTo(hdc, P(11.5f, 7.5f).x, P(11.5f, 7.5f).y);
            MoveToEx(hdc, P(5.5f, 12.0f).x, P(5.5f, 12.0f).y, NULL);
            LineTo(hdc, P(11.5f, 12.0f).x, P(11.5f, 12.0f).y);
            MoveToEx(hdc, P(5.5f, 16.5f).x, P(5.5f, 16.5f).y, NULL);
            LineTo(hdc, P(11.5f, 16.5f).x, P(11.5f, 16.5f).y);

            // Right fader & thumb
            MoveToEx(hdc, P(17.5f, 6.0f).x, P(17.5f, 6.0f).y, NULL);
            LineTo(hdc, P(17.5f, 18.0f).x, P(17.5f, 18.0f).y);
            RECT th = R(15.5f, 9.5f, 4.0f, 3.0f);
            RoundRect(hdc, th.left, th.top, th.right, th.bottom, 2, 2);
            break;
        }
        case SvgIconType::TrackFx: {
            // Channel 1 Fader
            MoveToEx(hdc, P(6.0f, 4.0f).x, P(6.0f, 4.0f).y, NULL);
            LineTo(hdc, P(6.0f, 20.0f).x, P(6.0f, 20.0f).y);
            RECT t1 = R(4.0f, 7.0f, 4.0f, 3.0f);
            RoundRect(hdc, t1.left, t1.top, t1.right, t1.bottom, 2, 2);

            // Channel 2 Fader
            MoveToEx(hdc, P(12.0f, 4.0f).x, P(12.0f, 4.0f).y, NULL);
            LineTo(hdc, P(12.0f, 20.0f).x, P(12.0f, 20.0f).y);
            RECT t2 = R(10.0f, 14.0f, 4.0f, 3.0f);
            RoundRect(hdc, t2.left, t2.top, t2.right, t2.bottom, 2, 2);

            // Channel 3 Fader
            MoveToEx(hdc, P(18.0f, 4.0f).x, P(18.0f, 4.0f).y, NULL);
            LineTo(hdc, P(18.0f, 20.0f).x, P(18.0f, 20.0f).y);
            RECT t3 = R(16.0f, 9.0f, 4.0f, 3.0f);
            RoundRect(hdc, t3.left, t3.top, t3.right, t3.bottom, 2, 2);
            break;
        }
        case SvgIconType::Mixer: {
            // Track 1
            MoveToEx(hdc, P(5.0f, 3.0f).x, P(5.0f, 3.0f).y, NULL);
            LineTo(hdc, P(5.0f, 21.0f).x, P(5.0f, 21.0f).y);
            RECT m1 = R(3.0f, 11.0f, 4.0f, 5.0f);
            RoundRect(hdc, m1.left, m1.top, m1.right, m1.bottom, 2, 2);

            // Track 2
            MoveToEx(hdc, P(12.0f, 3.0f).x, P(12.0f, 3.0f).y, NULL);
            LineTo(hdc, P(12.0f, 21.0f).x, P(12.0f, 21.0f).y);
            RECT m2 = R(10.0f, 6.0f, 4.0f, 5.0f);
            RoundRect(hdc, m2.left, m2.top, m2.right, m2.bottom, 2, 2);

            // Track 3
            MoveToEx(hdc, P(19.0f, 3.0f).x, P(19.0f, 3.0f).y, NULL);
            LineTo(hdc, P(19.0f, 21.0f).x, P(19.0f, 21.0f).y);
            RECT m3 = R(17.0f, 14.0f, 4.0f, 5.0f);
            RoundRect(hdc, m3.left, m3.top, m3.right, m3.bottom, 2, 2);
            break;
        }
        case SvgIconType::Magnet: {
            POINT mag_pts[8] = {
                P(4.0f, 5.0f), P(8.0f, 5.0f), P(8.0f, 11.0f),
                P(16.0f, 11.0f), P(16.0f, 5.0f), P(20.0f, 5.0f),
                P(20.0f, 14.0f), P(4.0f, 14.0f)
            };
            HGDIOBJ null_br = GetStockObject(NULL_BRUSH);
            SelectObject(hdc, null_br);
            Polygon(hdc, mag_pts, 8);
            SelectObject(hdc, br);

            MoveToEx(hdc, P(4.0f, 8.0f).x, P(4.0f, 8.0f).y, NULL);
            LineTo(hdc, P(8.0f, 8.0f).x, P(8.0f, 8.0f).y);
            MoveToEx(hdc, P(16.0f, 8.0f).x, P(16.0f, 8.0f).y, NULL);
            LineTo(hdc, P(20.0f, 8.0f).x, P(20.0f, 8.0f).y);
            break;
        }
        case SvgIconType::Save: {
            POINT disk[5] = { P(5.0f, 20.0f), P(5.0f, 4.0f), P(16.0f, 4.0f), P(20.0f, 8.0f), P(20.0f, 20.0f) };
            HGDIOBJ null_br = GetStockObject(NULL_BRUSH);
            SelectObject(hdc, null_br);
            Polygon(hdc, disk, 5);
            SelectObject(hdc, br);

            RECT sh = R(7.5f, 4.0f, 8.0f, 5.0f);
            RECT lb = R(7.5f, 12.0f, 9.0f, 8.0f);
            RoundRect(hdc, sh.left, sh.top, sh.right, sh.bottom, 2, 2);
            RoundRect(hdc, lb.left, lb.top, lb.right, lb.bottom, 2, 2);
            break;
        }
        case SvgIconType::Export: {
            HGDIOBJ null_br = GetStockObject(NULL_BRUSH);
            SelectObject(hdc, null_br);
            int rx = static_cast<int>(std::round(7.5f * s));
            POINT center = P(11.0f, 13.0f);
            Ellipse(hdc, center.x - rx, center.y - rx, center.x + rx, center.y + rx);
            int hrx = static_cast<int>(std::round(2.2f * s));
            Ellipse(hdc, center.x - hrx, center.y - hrx, center.x + hrx, center.y + hrx);
            SelectObject(hdc, br);

            MoveToEx(hdc, P(13.0f, 11.0f).x, P(13.0f, 11.0f).y, NULL);
            LineTo(hdc, P(20.5f, 3.5f).x, P(20.5f, 3.5f).y);
            LineTo(hdc, P(16.0f, 3.5f).x, P(16.0f, 3.5f).y);
            MoveToEx(hdc, P(20.5f, 3.5f).x, P(20.5f, 3.5f).y, NULL);
            LineTo(hdc, P(20.5f, 8.0f).x, P(20.5f, 8.0f).y);
            break;
        }
        case SvgIconType::Folder: {
            POINT pts[6] = {
                P(3.0f, 6.0f),
                P(9.0f, 6.0f),
                P(11.5f, 8.5f),
                P(21.0f, 8.5f),
                P(21.0f, 19.0f),
                P(3.0f, 19.0f)
            };
            Polygon(hdc, pts, 6);
            MoveToEx(hdc, P(3.0f, 11.5f).x, P(3.0f, 11.5f).y, NULL);
            LineTo(hdc, P(21.0f, 11.5f).x, P(21.0f, 11.5f).y);
            break;
        }
        }

        SelectObject(hdc, old_br);
        SelectObject(hdc, old_pen);
        DeleteObject(br);
        DeleteObject(pen);
    }

    static void draw_icon_button(HDC hdc, const RECT& rc, SvgIconType icon, bool active,
                                 COLORREF active_color, COLORREF normal_bg,
                                 int radius = 4, int icon_size = 16) {
        const auto& t = get_theme();
        COLORREF bg = active ? active_color : normal_bg;
        COLORREF border = active ? t.accent_bright : t.border_dark;
        COLORREF icon_col = active ? RGB(255, 255, 255) : t.text_primary;

        draw_rounded_box(hdc, rc, bg, border, radius);
        draw_svg_icon(hdc, icon, rc, icon_col, icon_size);
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

    static void draw_meter_vertical_stereo(HDC hdc, const RECT& rc, float peak_l, float peak_r) {
        const auto& t = get_theme();
        fill_rect(hdc, rc, t.meter_bg);
        draw_border(hdc, rc, t.border_dark);

        int total_w = (rc.right - rc.left) - 2;
        int bar_w = std::max(2, (total_w - 2) / 2);
        int h = rc.bottom - rc.top - 2;

        float clamped_l = std::clamp(peak_l, 0.0f, 1.0f);
        float clamped_r = std::clamp(peak_r, 0.0f, 1.0f);
        int active_hl = static_cast<int>(h * clamped_l);
        int active_hr = static_cast<int>(h * clamped_r);

        if (active_hl > 0) {
            RECT rc_l{rc.left + 1, rc.bottom - 1 - active_hl, rc.left + 1 + bar_w, rc.bottom - 1};
            COLORREF col_l = (clamped_l > 0.85f) ? t.meter_red : ((clamped_l > 0.65f) ? t.meter_yellow : t.meter_green);
            fill_rect(hdc, rc_l, col_l);
        }

        if (active_hr > 0) {
            RECT rc_r{rc.right - 1 - bar_w, rc.bottom - 1 - active_hr, rc.right - 1, rc.bottom - 1};
            COLORREF col_r = (clamped_r > 0.85f) ? t.meter_red : ((clamped_r > 0.65f) ? t.meter_yellow : t.meter_green);
            fill_rect(hdc, rc_r, col_r);
        }

        HPEN divPen = CreatePen(PS_SOLID, 1, t.border_dark);
        HGDIOBJ oldPen = SelectObject(hdc, divPen);
        int mid_x = (rc.left + rc.right) / 2;
        MoveToEx(hdc, mid_x, rc.top + 1, NULL);
        LineTo(hdc, mid_x, rc.bottom - 1);
        SelectObject(hdc, oldPen);
        DeleteObject(divPen);
    }

    static void draw_pan_knob(HDC hdc, const RECT& rc, float pan_val, const std::string& label = "") {
        (void)label;
        const auto& t = get_theme();
        int cx = (rc.left + rc.right) / 2;
        int cy = rc.top + 13;
        int r = 11;

        HBRUSH bgBrush = CreateSolidBrush(t.bg_control);
        HPEN borderPen = CreatePen(PS_SOLID, 1, t.border_subtle);
        HGDIOBJ oldBrush = SelectObject(hdc, bgBrush);
        HGDIOBJ oldPen = SelectObject(hdc, borderPen);
        Ellipse(hdc, cx - r, cy - r, cx + r, cy + r);

        HPEN tickPen = CreatePen(PS_SOLID, 1, t.border_strong);
        SelectObject(hdc, tickPen);
        MoveToEx(hdc, cx, cy - r - 2, NULL);
        LineTo(hdc, cx, cy - r + 1);
        DeleteObject(tickPen);

        float clamped_pan = std::clamp(pan_val, -1.0f, 1.0f);
        float angle_deg = -90.0f + clamped_pan * 135.0f;
        float angle_rad = angle_deg * (3.141592653589793f / 180.0f);
        int nx = cx + static_cast<int>((r - 2.5f) * std::cos(angle_rad));
        int ny = cy + static_cast<int>((r - 2.5f) * std::sin(angle_rad));

        COLORREF needle_col = (std::abs(clamped_pan) < 0.02f) ? RGB(230, 230, 230) : t.accent;
        HPEN needlePen = CreatePen(PS_SOLID, 2, needle_col);
        SelectObject(hdc, needlePen);
        MoveToEx(hdc, cx, cy, NULL);
        LineTo(hdc, nx, ny);

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(bgBrush);
        DeleteObject(borderPen);
        DeleteObject(needlePen);

        std::string readout;
        if (std::abs(clamped_pan) < 0.02f) {
            readout = "C";
        } else if (clamped_pan < 0.0f) {
            int pct = static_cast<int>(std::round(-clamped_pan * 100.0f));
            readout = "L " + std::to_string(pct) + "%";
        } else {
            int pct = static_cast<int>(std::round(clamped_pan * 100.0f));
            readout = "R " + std::to_string(pct) + "%";
        }

        RECT txt_rc{rc.left, cy + r + 2, rc.right, rc.bottom};
        COLORREF txt_col = (std::abs(clamped_pan) < 0.02f) ? t.text_secondary : t.accent;
        draw_text(hdc, readout, txt_rc, txt_col, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    static void draw_slider_vertical(HDC hdc, const RECT& rc, float normalized_val, const std::string& label) {
        const auto& t = get_theme();
        // Track
        int track_x = (rc.left + rc.right) / 2;
        RECT track_rc{track_x - 3, rc.top + 6, track_x + 3, rc.bottom - 20};
        fill_rect(hdc, track_rc, t.bg_app);
        draw_border(hdc, track_rc, t.border_subtle);

        // Active fill from bottom to thumb
        int track_h = (rc.bottom - 20) - (rc.top + 6);
        float norm = std::clamp(normalized_val, 0.0f, 1.0f);
        int fill_h = static_cast<int>(track_h * norm);
        if (fill_h > 0) {
            RECT fill_rc{track_x - 2, (rc.bottom - 20) - fill_h, track_x + 2, rc.bottom - 20};
            fill_rect(hdc, fill_rc, t.accent);
        }

        // Thumb handle
        int thumb_y = (rc.bottom - 20) - fill_h;
        RECT thumb_rc{rc.left + 6, thumb_y - 5, rc.right - 6, thumb_y + 5};
        draw_rounded_box(hdc, thumb_rc, RGB(46, 51, 59), t.border_strong, 3);

        // Center notch line on thumb
        RECT notch_rc{rc.left + 10, thumb_y - 1, rc.right - 10, thumb_y + 1};
        fill_rect(hdc, notch_rc, t.accent_bright);

        // Readout label at bottom
        RECT text_rc{rc.left - 4, rc.bottom - 18, rc.right + 4, rc.bottom};
        draw_text(hdc, label, text_rc, t.text_secondary, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    static void draw_slider_horizontal(HDC hdc, const RECT& rc, float normalized_val, const std::string& label) {
        const auto& t = get_theme();
        draw_rounded_box(hdc, rc, t.bg_control, t.border_subtle, 3);

        int w = rc.right - rc.left - 2;
        float norm = std::clamp(normalized_val, 0.0f, 1.0f);
        int fill_w = static_cast<int>(w * norm);
        if (fill_w > 0) {
            RECT fill_rc{rc.left + 1, rc.top + 1, rc.left + 1 + fill_w, rc.bottom - 1};
            fill_rect(hdc, fill_rc, t.accent_deep);
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

    static void draw_knob(HDC hdc, const RECT& rc, float normalized_val, const std::string& label,
                          COLORREF indicator_col = RGB(168, 85, 247)) {
        int cx = (rc.left + rc.right) / 2;
        int cy = (rc.top + rc.bottom) / 2;
        int r = std::min((rc.right - rc.left), (rc.bottom - rc.top)) / 2 - 2;
        if (r < 5) r = 5;

        const auto& t = get_theme();
        HBRUSH br_knob = CreateSolidBrush(t.bg_control);
        HPEN pen_border = CreatePen(PS_SOLID, 1, t.border_default);
        HGDIOBJ old_br = SelectObject(hdc, br_knob);
        HGDIOBJ old_pen = SelectObject(hdc, pen_border);

        Ellipse(hdc, cx - r, cy - r, cx + r, cy + r);

        // Indicator line
        float norm = std::clamp(normalized_val, 0.0f, 1.0f);
        float angle_deg = -135.0f + norm * 270.0f;
        float angle_rad = angle_deg * 3.14159265f / 180.0f;
        int ix = cx + static_cast<int>(std::round(std::sin(angle_rad) * (r - 2)));
        int iy = cy - static_cast<int>(std::round(std::cos(angle_rad) * (r - 2)));

        HPEN pen_ind = CreatePen(PS_SOLID, 2, indicator_col);
        SelectObject(hdc, pen_ind);
        MoveToEx(hdc, cx, cy, NULL);
        LineTo(hdc, ix, iy);

        SelectObject(hdc, old_pen);
        SelectObject(hdc, old_br);
        DeleteObject(pen_ind);
        DeleteObject(pen_border);
        DeleteObject(br_knob);

        if (!label.empty()) {
            RECT lbl_rc = rc;
            lbl_rc.top = cy + r;
            draw_text(hdc, label, lbl_rc, t.text_muted, DT_CENTER | DT_SINGLELINE);
        }
    }
};

} // namespace digidaw::adapters::gui
