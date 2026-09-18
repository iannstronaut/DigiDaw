#pragma once

#include "../plugins/xosc_device.hpp"
#include "../plugins/xosc_dsp.hpp"
#include "d2d_renderer.hpp"
#include "gui_renderer.hpp"
#include "theme.hpp"
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <functional>

namespace digidaw::adapters::gui {

class XOSCEditor {
public:
    // ========================================================================
    // Color Palette matching XOSC LookAndFeel (PluginEditor.cpp)
    // ========================================================================
    static inline D2D1_COLOR_F col_bg()     { return D2D1::ColorF(0x19 / 255.0f, 0x1a / 255.0f, 0x18 / 255.0f); } // #191a18 Charcoal
    static inline D2D1_COLOR_F col_panel()  { return D2D1::ColorF(0x2b / 255.0f, 0x2c / 255.0f, 0x28 / 255.0f); } // #2b2c28 Panel dark
    static inline D2D1_COLOR_F col_accent() { return D2D1::ColorF(0xe9 / 255.0f, 0xac / 255.0f, 0x55 / 255.0f); } // #e9ac55 Warm amber
    static inline D2D1_COLOR_F col_ink()    { return D2D1::ColorF(0xee / 255.0f, 0xe3 / 255.0f, 0xc9 / 255.0f); } // #eee3c9 Cream / ivory
    static inline D2D1_COLOR_F col_muted()  { return D2D1::ColorF(0xb6 / 255.0f, 0xad / 255.0f, 0x96 / 255.0f); } // #b6ad96 Muted khaki

    static inline COLORREF gdi_bg()     { return RGB(0x19, 0x1a, 0x18); }
    static inline COLORREF gdi_panel()  { return RGB(0x2b, 0x2c, 0x28); }
    static inline COLORREF gdi_accent() { return RGB(0xe9, 0xac, 0x55); }
    static inline COLORREF gdi_ink()    { return RGB(0xee, 0xe3, 0xc9); }
    static inline COLORREF gdi_muted()  { return RGB(0xb6, 0xad, 0x96); }

    static std::string format_param_val(const xosc::Spec& s, float plain) {
        std::ostringstream ss;
        if (s.unit == "dB") {
            ss << std::fixed << std::setprecision(1) << (plain > 0.05f ? "+" : "") << plain << " dB";
        } else if (s.unit == "Hz") {
            if (plain >= 1000.0f) {
                ss << std::fixed << std::setprecision(2) << (plain / 1000.0f) << " kHz";
            } else {
                ss << static_cast<int>(std::round(plain)) << " Hz";
            }
        } else if (s.unit == "deg") {
            ss << static_cast<int>(std::round(plain)) << " deg";
        } else if (s.unit == "ct") {
            ss << static_cast<int>(std::round(plain)) << " ct";
        } else if (s.unit == "st") {
            int st = static_cast<int>(std::round(plain));
            ss << (st > 0 ? "+" : "") << st << " st";
        } else if (s.unit == "s") {
            if (plain < 0.1f) {
                ss << static_cast<int>(std::round(plain * 1000.0f)) << " ms";
            } else {
                ss << std::fixed << std::setprecision(2) << plain << " s";
            }
        } else if (s.unit == "Q") {
            ss << std::fixed << std::setprecision(2) << plain << " Q";
        } else {
            ss << std::fixed << std::setprecision(2) << plain;
        }
        return ss.str();
    }

    // ========================================================================
    // Custom Fluted Rotary Dial with Printed Graduations (Direct2D)
    // ========================================================================
    static void draw_knob_d2d(ID2D1RenderTarget* rt, IDWriteTextFormat* /*font_bold*/, IDWriteTextFormat* font_small,
                              const D2D1_RECT_F& rc, float norm, const std::string& title,
                              const std::string& val_str, bool disabled = false) {
        if (!rt) return;
        float cx = (rc.left + rc.right) * 0.5f;
        float cy = rc.top + 16.0f + (rc.bottom - rc.top - 34.0f) * 0.5f;
        float max_r = std::min((rc.right - rc.left) * 0.5f - 4.0f, (rc.bottom - rc.top - 34.0f) * 0.5f - 2.0f);
        float radius = std::clamp(max_r, 8.0f, 22.0f);

        // Title above knob
        D2D1_RECT_F title_rc = D2D1::RectF(rc.left, rc.top, rc.right, rc.top + 15.0f);
        D2DRenderer::draw_text(rt, font_small, title, title_rc, disabled ? D2D1::ColorF(0.4f, 0.4f, 0.4f) : col_ink(),
                               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Dial graduations: 11 ticks from -135 deg to +135 deg
        const float start_ang = -2.35619f; // -135 deg
        const float end_ang   =  2.35619f; // +135 deg
        float pos = std::clamp(norm, 0.0f, 1.0f);

        ID2D1SolidColorBrush* br_grad_active = nullptr;
        ID2D1SolidColorBrush* br_grad_dim = nullptr;
        ID2D1SolidColorBrush* br_ink = nullptr;
        ID2D1SolidColorBrush* br_shadow = nullptr;
        ID2D1SolidColorBrush* br_knob = nullptr;
        ID2D1SolidColorBrush* br_rim = nullptr;

        rt->CreateSolidColorBrush(col_accent(), &br_grad_active);
        D2D1_COLOR_F dim_col = col_muted();
        dim_col.a = 0.35f;
        rt->CreateSolidColorBrush(dim_col, &br_grad_dim);
        rt->CreateSolidColorBrush(col_ink(), &br_ink);
        rt->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.55f), &br_shadow);
        rt->CreateSolidColorBrush(D2D1::ColorF(0x32 / 255.f, 0x34 / 255.f, 0x30 / 255.f), &br_knob);
        rt->CreateSolidColorBrush(D2D1::ColorF(0x11 / 255.f, 0x12 / 255.f, 0x0f / 255.f), &br_rim);

        for (int i = 0; i <= 10; ++i) {
            float a = start_ang + (static_cast<float>(i) / 10.0f) * (end_ang - start_ang);
            float sa = std::sin(a);
            float ca = -std::cos(a);
            float p1x = cx + sa * radius * 0.88f;
            float p1y = cy + ca * radius * 0.88f;
            float p2x = cx + sa * radius;
            float p2y = cy + ca * radius;
            auto* br = (i <= static_cast<int>(pos * 10.0f + 0.001f) && !disabled) ? br_grad_active : br_grad_dim;
            if (br) rt->DrawLine(D2D1::Point2F(p1x, p1y), D2D1::Point2F(p2x, p2y), br, 1.2f);
        }

        // Knob body
        float knob = std::max(4.0f, radius - 4.5f);
        // Shadow
        D2D1_ELLIPSE sh_el = D2D1::Ellipse(D2D1::Point2F(cx + 1.0f, cy + 2.0f), knob, knob);
        if (br_shadow) rt->FillEllipse(sh_el, br_shadow);

        // Body
        D2D1_ELLIPSE kn_el = D2D1::Ellipse(D2D1::Point2F(cx, cy), knob, knob);
        if (br_knob) rt->FillEllipse(kn_el, br_knob);
        if (br_rim) rt->DrawEllipse(kn_el, br_rim, 1.0f);

        // Fluted notches on rim
        for (int i = 0; i < 24; ++i) {
            float a = static_cast<float>(i) * 6.2831853f / 24.0f;
            float sa = std::sin(a);
            float ca = -std::cos(a);
            if (br_rim) {
                rt->DrawLine(D2D1::Point2F(cx + sa * knob * 0.82f, cy + ca * knob * 0.82f),
                             D2D1::Point2F(cx + sa * knob * 0.96f, cy + ca * knob * 0.96f), br_rim, 1.0f);
            }
        }

        // Pointer line
        float angle = start_ang + pos * (end_ang - start_ang);
        float sa = std::sin(angle);
        float ca = -std::cos(angle);
        if (br_ink && !disabled) {
            rt->DrawLine(D2D1::Point2F(cx + sa * knob * 0.30f, cy + ca * knob * 0.30f),
                         D2D1::Point2F(cx + sa * knob * 0.78f, cy + ca * knob * 0.78f), br_ink, 2.2f);
        }

        // Value text below knob
        D2D1_RECT_F val_rc = D2D1::RectF(rc.left, rc.bottom - 16.0f, rc.right, rc.bottom);
        D2DRenderer::draw_text(rt, font_small, val_str, val_rc, disabled ? D2D1::ColorF(0.4f, 0.4f, 0.4f) : col_accent(),
                               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        if (br_grad_active) br_grad_active->Release();
        if (br_grad_dim) br_grad_dim->Release();
        if (br_ink) br_ink->Release();
        if (br_shadow) br_shadow->Release();
        if (br_knob) br_knob->Release();
        if (br_rim) br_rim->Release();
    }

    // ========================================================================
    // Custom Rotary Dial (GDI Fallback)
    // ========================================================================
    static void draw_knob_gdi(HDC hdc, HFONT font_small, const RECT& rc, float norm,
                              const std::string& title, const std::string& val_str, bool disabled = false) {
        if (!hdc) return;
        int cx = (rc.left + rc.right) / 2;
        int cy = rc.top + 16 + (rc.bottom - rc.top - 34) / 2;
        int max_r = std::min((rc.right - rc.left) / 2 - 4, (rc.bottom - rc.top - 34) / 2 - 2);
        int radius = std::clamp(max_r, 8, 22);

        SelectObject(hdc, font_small);
        RECT title_rc{rc.left, rc.top, rc.right, rc.top + 15};
        GuiRenderer::draw_text(hdc, title, title_rc, disabled ? RGB(100, 100, 100) : gdi_ink(), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        const float start_ang = -2.35619f;
        const float end_ang   =  2.35619f;
        float pos = std::clamp(norm, 0.0f, 1.0f);

        HPEN pen_active = CreatePen(PS_SOLID, 1, gdi_accent());
        HPEN pen_dim = CreatePen(PS_SOLID, 1, RGB(70, 68, 62));
        for (int i = 0; i <= 10; ++i) {
            float a = start_ang + (static_cast<float>(i) / 10.0f) * (end_ang - start_ang);
            float sa = std::sin(a);
            float ca = -std::cos(a);
            int p1x = cx + static_cast<int>(sa * static_cast<float>(radius) * 0.88f);
            int p1y = cy + static_cast<int>(ca * static_cast<float>(radius) * 0.88f);
            int p2x = cx + static_cast<int>(sa * static_cast<float>(radius));
            int p2y = cy + static_cast<int>(ca * static_cast<float>(radius));
            HPEN old_p = static_cast<HPEN>(SelectObject(hdc, (i <= static_cast<int>(pos * 10.0f + 0.001f) && !disabled) ? pen_active : pen_dim));
            MoveToEx(hdc, p1x, p1y, nullptr);
            LineTo(hdc, p2x, p2y);
            SelectObject(hdc, old_p);
        }
        DeleteObject(pen_active);
        DeleteObject(pen_dim);

        int knob = std::max(4, radius - 4);
        HBRUSH br_knob = CreateSolidBrush(RGB(50, 52, 48));
        HPEN pen_rim = CreatePen(PS_SOLID, 1, RGB(17, 18, 15));
        HBRUSH old_b = static_cast<HBRUSH>(SelectObject(hdc, br_knob));
        HPEN old_p = static_cast<HPEN>(SelectObject(hdc, pen_rim));
        Ellipse(hdc, cx - knob, cy - knob, cx + knob, cy + knob);

        float angle = start_ang + pos * (end_ang - start_ang);
        HPEN pen_ptr = CreatePen(PS_SOLID, 2, gdi_ink());
        SelectObject(hdc, pen_ptr);
        MoveToEx(hdc, cx + static_cast<int>(std::sin(angle) * static_cast<float>(knob) * 0.30f),
                      cy - static_cast<int>(std::cos(angle) * static_cast<float>(knob) * 0.30f), nullptr);
        LineTo(hdc, cx + static_cast<int>(std::sin(angle) * static_cast<float>(knob) * 0.78f),
                    cy - static_cast<int>(std::cos(angle) * static_cast<float>(knob) * 0.78f));

        SelectObject(hdc, old_b);
        SelectObject(hdc, old_p);
        DeleteObject(br_knob);
        DeleteObject(pen_rim);
        DeleteObject(pen_ptr);

        RECT val_rc{rc.left, rc.bottom - 16, rc.right, rc.bottom};
        GuiRenderer::draw_text(hdc, val_str, val_rc, disabled ? RGB(100, 100, 100) : gdi_accent(), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // ========================================================================
    // Illuminated Radio/LED Style Toggle Switch (Direct2D)
    // ========================================================================
    static void draw_toggle_led_d2d(ID2D1RenderTarget* rt, const D2D1_RECT_F& rc, bool on) {
        if (!rt) return;
        float h = rc.bottom - rc.top;
        float radius = h * 0.5f;
        D2D1_ROUNDED_RECT pill = D2D1::RoundedRect(rc, radius, radius);
        ID2D1SolidColorBrush* br_track = nullptr;
        ID2D1SolidColorBrush* br_border = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(0x18 / 255.f, 0x1a / 255.f, 0x17 / 255.f), &br_track);
        rt->CreateSolidColorBrush(on ? D2D1::ColorF(0x80 / 255.f, 0x60 / 255.f, 0x30 / 255.f)
                                     : D2D1::ColorF(0x38 / 255.f, 0x3a / 255.f, 0x35 / 255.f), &br_border);
        if (br_track && br_border) {
            rt->FillRoundedRectangle(pill, br_track);
            rt->DrawRoundedRectangle(pill, br_border, 1.0f);
        }

        float bead_r = std::max(2.5f, radius - 2.5f);
        float cx = on ? (rc.right - radius) : (rc.left + radius);
        float cy = (rc.top + rc.bottom) * 0.5f;
        D2D1_ELLIPSE el = D2D1::Ellipse(D2D1::Point2F(cx, cy), bead_r, bead_r);

        if (on) {
            ID2D1SolidColorBrush* br_glow = nullptr;
            rt->CreateSolidColorBrush(D2D1::ColorF(0xe9 / 255.f, 0xac / 255.f, 0x55 / 255.f, 0.40f), &br_glow);
            if (br_glow) {
                D2D1_ELLIPSE glow_el = D2D1::Ellipse(D2D1::Point2F(cx, cy), bead_r + 2.5f, bead_r + 2.5f);
                rt->FillEllipse(glow_el, br_glow);
                br_glow->Release();
            }
            ID2D1SolidColorBrush* br_bead = nullptr;
            rt->CreateSolidColorBrush(col_accent(), &br_bead);
            if (br_bead) {
                rt->FillEllipse(el, br_bead);
                br_bead->Release();
            }
        } else {
            ID2D1SolidColorBrush* br_bead = nullptr;
            rt->CreateSolidColorBrush(D2D1::ColorF(0x32 / 255.f, 0x34 / 255.f, 0x30 / 255.f), &br_bead);
            if (br_bead) {
                rt->FillEllipse(el, br_bead);
                br_bead->Release();
            }
        }

        if (br_track) br_track->Release();
        if (br_border) br_border->Release();
    }

    // ========================================================================
    // Illuminated Radio/LED Style Toggle Switch (GDI Fallback)
    // ========================================================================
    static void draw_toggle_led_gdi(HDC hdc, const RECT& rc, bool on) {
        if (!hdc) return;
        int h = rc.bottom - rc.top;
        int radius = h / 2;
        HBRUSH br_track = CreateSolidBrush(RGB(24, 26, 23));
        HPEN pen_border = CreatePen(PS_SOLID, 1, on ? RGB(128, 96, 48) : RGB(56, 58, 53));
        HBRUSH old_b = static_cast<HBRUSH>(SelectObject(hdc, br_track));
        HPEN old_p = static_cast<HPEN>(SelectObject(hdc, pen_border));
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, h, h);

        int bead_r = std::max(2, radius - 3);
        int cx = on ? (rc.right - radius) : (rc.left + radius);
        int cy = (rc.top + rc.bottom) / 2;
        HBRUSH br_bead = CreateSolidBrush(on ? RGB(233, 172, 85) : RGB(50, 52, 48));
        HPEN pen_bead = CreatePen(PS_SOLID, 1, on ? RGB(255, 200, 120) : RGB(30, 32, 28));
        SelectObject(hdc, br_bead);
        SelectObject(hdc, pen_bead);
        Ellipse(hdc, cx - bead_r, cy - bead_r, cx + bead_r, cy + bead_r);

        SelectObject(hdc, old_b);
        SelectObject(hdc, old_p);
        DeleteObject(br_track);
        DeleteObject(pen_border);
        DeleteObject(br_bead);
        DeleteObject(pen_bead);
    }

    // ========================================================================
    // Crisp Vector Waveform Shapes (Direct2D & GDI)
    // ========================================================================
    static void draw_wave_icon_d2d(ID2D1RenderTarget* rt, const D2D1_RECT_F& rc, int wave, bool active) {
        if (!rt) return;
        D2D1_COLOR_F bg_col = active ? D2D1::ColorF(0x38 / 255.f, 0x30 / 255.f, 0x22 / 255.f)
                                     : D2D1::ColorF(0x20 / 255.f, 0x22 / 255.f, 0x1f / 255.f);
        D2D1_COLOR_F border_col = active ? col_accent() : D2D1::ColorF(0x35 / 255.f, 0x36 / 255.f, 0x32 / 255.f);
        D2DRenderer::draw_rounded_box(rt, rc, bg_col, border_col, 2.5f, active ? 1.5f : 1.0f);

        ID2D1SolidColorBrush* br_stroke = nullptr;
        rt->CreateSolidColorBrush(active ? col_accent() : col_ink(), &br_stroke);
        if (!br_stroke) return;

        float cx0 = rc.left + 4.0f;
        float cx1 = rc.right - 4.0f;
        float cy = (rc.top + rc.bottom) * 0.5f;
        float amp = (rc.bottom - rc.top) * 0.28f;
        float w = cx1 - cx0;

        if (wave == 0) {
            constexpr int kPts = 16;
            D2D1_POINT_2F pts[kPts];
            for (int i = 0; i < kPts; ++i) {
                float t = static_cast<float>(i) / static_cast<float>(kPts - 1);
                pts[i] = D2D1::Point2F(cx0 + t * w, cy - amp * std::sin(t * 6.2831853f));
            }
            for (int i = 0; i < kPts - 1; ++i)
                rt->DrawLine(pts[i], pts[i + 1], br_stroke, active ? 2.0f : 1.4f);
        } else if (wave == 1) {
            float mid_x = (cx0 + cx1) * 0.5f;
            rt->DrawLine(D2D1::Point2F(cx0, cy - amp), D2D1::Point2F(mid_x, cy - amp), br_stroke, active ? 2.0f : 1.4f);
            rt->DrawLine(D2D1::Point2F(mid_x, cy - amp), D2D1::Point2F(mid_x, cy + amp), br_stroke, active ? 2.0f : 1.4f);
            rt->DrawLine(D2D1::Point2F(mid_x, cy + amp), D2D1::Point2F(cx1, cy + amp), br_stroke, active ? 2.0f : 1.4f);
        } else {
            rt->DrawLine(D2D1::Point2F(cx0, cy + amp), D2D1::Point2F(cx1 - 1.5f, cy - amp), br_stroke, active ? 2.0f : 1.4f);
            rt->DrawLine(D2D1::Point2F(cx1 - 1.5f, cy - amp), D2D1::Point2F(cx1 - 1.5f, cy + amp), br_stroke, active ? 2.0f : 1.4f);
        }
        br_stroke->Release();
    }

    static void draw_wave_icon_gdi(HDC hdc, const RECT& rc, int wave, bool active) {
        if (!hdc) return;
        COLORREF bg_col = active ? RGB(56, 48, 34) : RGB(32, 34, 31);
        COLORREF border_col = active ? gdi_accent() : RGB(53, 54, 50);
        GuiRenderer::draw_rounded_box(hdc, rc, bg_col, border_col, 2);

        HPEN pen_stroke = CreatePen(PS_SOLID, active ? 2 : 1, active ? gdi_accent() : gdi_ink());
        HPEN old_p = static_cast<HPEN>(SelectObject(hdc, pen_stroke));

        int cx0 = rc.left + 4;
        int cx1 = rc.right - 4;
        int cy = (rc.top + rc.bottom) / 2;
        int amp = static_cast<int>((rc.bottom - rc.top) * 0.28f);
        int w = cx1 - cx0;

        if (wave == 0) {
            constexpr int kPts = 16;
            for (int i = 0; i < kPts - 1; ++i) {
                float t1 = static_cast<float>(i) / static_cast<float>(kPts - 1);
                float t2 = static_cast<float>(i + 1) / static_cast<float>(kPts - 1);
                MoveToEx(hdc, cx0 + static_cast<int>(t1 * w), cy - static_cast<int>(amp * std::sin(t1 * 6.2831853f)), nullptr);
                LineTo(hdc, cx0 + static_cast<int>(t2 * w), cy - static_cast<int>(amp * std::sin(t2 * 6.2831853f)));
            }
        } else if (wave == 1) {
            int mid_x = (cx0 + cx1) / 2;
            MoveToEx(hdc, cx0, cy - amp, nullptr);
            LineTo(hdc, mid_x, cy - amp);
            LineTo(hdc, mid_x, cy + amp);
            LineTo(hdc, cx1, cy + amp);
        } else {
            MoveToEx(hdc, cx0, cy + amp, nullptr);
            LineTo(hdc, cx1 - 1, cy - amp);
            LineTo(hdc, cx1 - 1, cy + amp);
        }
        SelectObject(hdc, old_p);
        DeleteObject(pen_stroke);
    }

    // ========================================================================
    // Crisp Vector Filter Curves (Direct2D & GDI)
    // ========================================================================
    static void draw_filter_curve_d2d(ID2D1RenderTarget* rt, const D2D1_RECT_F& rc, int type, bool active) {
        if (!rt) return;
        D2D1_COLOR_F bg_col = active ? D2D1::ColorF(0x38 / 255.f, 0x30 / 255.f, 0x22 / 255.f)
                                     : D2D1::ColorF(0x20 / 255.f, 0x22 / 255.f, 0x1f / 255.f);
        D2D1_COLOR_F border_col = active ? col_accent() : D2D1::ColorF(0x35 / 255.f, 0x36 / 255.f, 0x32 / 255.f);
        D2DRenderer::draw_rounded_box(rt, rc, bg_col, border_col, 2.5f, active ? 1.5f : 1.0f);

        ID2D1SolidColorBrush* br_stroke = nullptr;
        rt->CreateSolidColorBrush(active ? col_accent() : col_ink(), &br_stroke);
        if (!br_stroke) return;

        float cx0 = rc.left + 5.0f;
        float cx1 = rc.right - 5.0f;
        float cy = (rc.top + rc.bottom) * 0.5f;
        float amp = (rc.bottom - rc.top) * 0.28f;
        float mid_x = (cx0 + cx1) * 0.5f;

        if (type == 0) {
            D2D1_POINT_2F pts[4] = {
                D2D1::Point2F(cx0, cy - amp),
                D2D1::Point2F(cx0 + (cx1 - cx0) * 0.45f, cy - amp),
                D2D1::Point2F(cx0 + (cx1 - cx0) * 0.55f, cy - amp - 2.0f),
                D2D1::Point2F(cx1, cy + amp)
            };
            rt->DrawLine(pts[0], pts[1], br_stroke, active ? 2.0f : 1.4f);
            rt->DrawLine(pts[1], pts[2], br_stroke, active ? 2.0f : 1.4f);
            rt->DrawLine(pts[2], pts[3], br_stroke, active ? 2.0f : 1.4f);
        } else if (type == 1) {
            D2D1_POINT_2F pts[4] = {
                D2D1::Point2F(cx0, cy + amp),
                D2D1::Point2F(cx0 + (cx1 - cx0) * 0.45f, cy - amp - 2.0f),
                D2D1::Point2F(cx0 + (cx1 - cx0) * 0.55f, cy - amp),
                D2D1::Point2F(cx1, cy - amp)
            };
            rt->DrawLine(pts[0], pts[1], br_stroke, active ? 2.0f : 1.4f);
            rt->DrawLine(pts[1], pts[2], br_stroke, active ? 2.0f : 1.4f);
            rt->DrawLine(pts[2], pts[3], br_stroke, active ? 2.0f : 1.4f);
        } else if (type == 2) {
            rt->DrawLine(D2D1::Point2F(cx0, cy + amp), D2D1::Point2F(mid_x, cy - amp - 2.0f), br_stroke, active ? 2.0f : 1.4f);
            rt->DrawLine(D2D1::Point2F(mid_x, cy - amp - 2.0f), D2D1::Point2F(cx1, cy + amp), br_stroke, active ? 2.0f : 1.4f);
        } else {
            rt->DrawLine(D2D1::Point2F(cx0, cy - amp), D2D1::Point2F(mid_x - 3.0f, cy - amp), br_stroke, active ? 2.0f : 1.4f);
            rt->DrawLine(D2D1::Point2F(mid_x - 3.0f, cy - amp), D2D1::Point2F(mid_x, cy + amp), br_stroke, active ? 2.0f : 1.4f);
            rt->DrawLine(D2D1::Point2F(mid_x, cy + amp), D2D1::Point2F(mid_x + 3.0f, cy - amp), br_stroke, active ? 2.0f : 1.4f);
            rt->DrawLine(D2D1::Point2F(mid_x + 3.0f, cy - amp), D2D1::Point2F(cx1, cy - amp), br_stroke, active ? 2.0f : 1.4f);
        }
        br_stroke->Release();
    }

    static void draw_filter_curve_gdi(HDC hdc, const RECT& rc, int type, bool active) {
        if (!hdc) return;
        COLORREF bg_col = active ? RGB(56, 48, 34) : RGB(32, 34, 31);
        COLORREF border_col = active ? gdi_accent() : RGB(53, 54, 50);
        GuiRenderer::draw_rounded_box(hdc, rc, bg_col, border_col, 2);

        HPEN pen_stroke = CreatePen(PS_SOLID, active ? 2 : 1, active ? gdi_accent() : gdi_ink());
        HPEN old_p = static_cast<HPEN>(SelectObject(hdc, pen_stroke));

        int cx0 = rc.left + 5;
        int cx1 = rc.right - 5;
        int cy = (rc.top + rc.bottom) / 2;
        int amp = static_cast<int>((rc.bottom - rc.top) * 0.28f);
        int mid_x = (cx0 + cx1) / 2;

        if (type == 0) {
            MoveToEx(hdc, cx0, cy - amp, nullptr);
            LineTo(hdc, cx0 + static_cast<int>((cx1 - cx0) * 0.45f), cy - amp);
            LineTo(hdc, cx0 + static_cast<int>((cx1 - cx0) * 0.55f), cy - amp - 2);
            LineTo(hdc, cx1, cy + amp);
        } else if (type == 1) {
            MoveToEx(hdc, cx0, cy + amp, nullptr);
            LineTo(hdc, cx0 + static_cast<int>((cx1 - cx0) * 0.45f), cy - amp - 2);
            LineTo(hdc, cx0 + static_cast<int>((cx1 - cx0) * 0.55f), cy - amp);
            LineTo(hdc, cx1, cy - amp);
        } else if (type == 2) {
            MoveToEx(hdc, cx0, cy + amp, nullptr);
            LineTo(hdc, mid_x, cy - amp - 2);
            LineTo(hdc, cx1, cy + amp);
        } else {
            MoveToEx(hdc, cx0, cy - amp, nullptr);
            LineTo(hdc, mid_x - 3, cy - amp);
            LineTo(hdc, mid_x, cy + amp);
            LineTo(hdc, mid_x + 3, cy - amp);
            LineTo(hdc, cx1, cy - amp);
        }
        SelectObject(hdc, old_p);
        DeleteObject(pen_stroke);
    }

    // ========================================================================
    // Module Panel Frame with Screws & Amber Divider
    // ========================================================================
    static void draw_module_panel_d2d(ID2D1RenderTarget* rt, IDWriteTextFormat* font_bold,
                                      IDWriteTextFormat* font_small, const D2D1_RECT_F& rc,
                                      const std::string& title, const std::string& subtitle) {
        if (!rt) return;
        D2D1_COLOR_F border_col = col_muted();
        border_col.a = 0.30f;
        D2DRenderer::draw_rounded_box(rt, rc, col_panel(), border_col, 3.0f, 1.0f);

        // Amber line separator under title
        ID2D1SolidColorBrush* br_line = nullptr;
        D2D1_COLOR_F line_col = col_accent();
        line_col.a = 0.55f;
        rt->CreateSolidColorBrush(line_col, &br_line);
        if (br_line) {
            rt->DrawLine(D2D1::Point2F(rc.left + 10.0f, rc.top + 38.0f),
                         D2D1::Point2F(rc.right - 10.0f, rc.top + 38.0f), br_line, 1.0f);
            br_line->Release();
        }

        // Screws in corners
        ID2D1SolidColorBrush* br_screw = nullptr;
        ID2D1SolidColorBrush* br_slit = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(0x11 / 255.f, 0x12 / 255.f, 0x0f / 255.f), &br_screw);
        D2D1_COLOR_F slit_col = col_muted();
        slit_col.a = 0.5f;
        rt->CreateSolidColorBrush(slit_col, &br_slit);
        if (br_screw && br_slit) {
            for (float sx : {rc.left + 7.0f, rc.right - 9.0f}) {
                D2D1_ELLIPSE el = D2D1::Ellipse(D2D1::Point2F(sx + 2.0f, rc.top + 7.0f), 2.5f, 2.5f);
                rt->FillEllipse(el, br_screw);
                rt->DrawLine(D2D1::Point2F(sx + 0.5f, rc.top + 7.0f),
                             D2D1::Point2F(sx + 3.5f, rc.top + 7.0f), br_slit, 0.8f);
            }
            br_screw->Release();
            br_slit->Release();
        }

        // Title and Subtitle text
        D2D1_RECT_F t_rc = D2D1::RectF(rc.left + 14.0f, rc.top + 4.0f, rc.right - 24.0f, rc.top + 22.0f);
        D2DRenderer::draw_text(rt, font_bold, title, t_rc, col_accent());
        D2D1_RECT_F sub_rc = D2D1::RectF(rc.left + 14.0f, rc.top + 21.0f, rc.right - 24.0f, rc.top + 36.0f);
        D2DRenderer::draw_text(rt, font_small, subtitle, sub_rc, col_muted());
    }

    static void draw_module_panel_gdi(HDC hdc, HFONT font_bold, HFONT font_small,
                                      const RECT& rc, const std::string& title, const std::string& subtitle) {
        if (!hdc) return;
        GuiRenderer::draw_rounded_box(hdc, rc, gdi_panel(), RGB(80, 78, 70), 3);

        HPEN pen_line = CreatePen(PS_SOLID, 1, RGB(180, 130, 60));
        HPEN old_p = static_cast<HPEN>(SelectObject(hdc, pen_line));
        MoveToEx(hdc, rc.left + 10, rc.top + 38, nullptr);
        LineTo(hdc, rc.right - 10, rc.top + 38);
        SelectObject(hdc, old_p);
        DeleteObject(pen_line);

        SelectObject(hdc, font_bold);
        RECT t_rc{rc.left + 14, rc.top + 4, rc.right - 24, rc.top + 22};
        GuiRenderer::draw_text(hdc, title, t_rc, gdi_accent(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(hdc, font_small);
        RECT sub_rc{rc.left + 14, rc.top + 21, rc.right - 24, rc.top + 36};
        GuiRenderer::draw_text(hdc, subtitle, sub_rc, gdi_muted(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    // ========================================================================
    // Main Render Pipeline (Direct2D)
    // ========================================================================
    static void render_d2d(ID2D1RenderTarget* rt, IDWriteTextFormat* font_bold,
                           IDWriteTextFormat* font_small, plugins::XOSCDevice* synth,
                           const RECT& bounds, int active_tab) {
        if (!rt || !synth) return;
        float mx = static_cast<float>(bounds.left);
        float my = static_cast<float>(bounds.top);
        float mw = static_cast<float>(bounds.right - bounds.left);
        float mh = static_cast<float>(bounds.bottom - bounds.top);

        // 1. Charcoal Background
        D2D1_RECT_F bg_rc = D2D1::RectF(mx, my, mx + mw, my + mh);
        D2DRenderer::draw_rounded_box(rt, bg_rc, col_bg(), D2D1::ColorF(0x35 / 255.f, 0x36 / 255.f, 0x32 / 255.f), 4.0f, 1.5f);

        // 2. Walnut Cheeks on Left and Right edges
        for (float cx : {mx, mx + mw - 12.0f}) {
            D2D1_RECT_F cheek_rc = D2D1::RectF(cx, my, cx + 12.0f, my + mh);
            D2DRenderer::draw_rounded_box(rt, cheek_rc, D2D1::ColorF(0x52 / 255.f, 0x33 / 255.f, 0x20 / 255.f),
                                         D2D1::ColorF(0x36 / 255.f, 0x25 / 255.f, 0x1a / 255.f), 2.0f);
            ID2D1SolidColorBrush* br_wood_line = nullptr;
            rt->CreateSolidColorBrush(D2D1::ColorF(0xb0 / 255.f, 0x7a / 255.f, 0x47 / 255.f, 0.25f), &br_wood_line);
            if (br_wood_line) {
                for (int l = 0; l < 3; ++l) {
                    float lx = cx + 2.0f + static_cast<float>(l) * 3.5f;
                    rt->DrawLine(D2D1::Point2F(lx, my + 2.0f), D2D1::Point2F(lx, my + mh - 2.0f), br_wood_line, 1.0f);
                }
                br_wood_line->Release();
            }
        }

        // 3. Header Bar
        D2D1_RECT_F title_rc = D2D1::RectF(mx + 22.0f, my + 8.0f, mx + 120.0f, my + 48.0f);
        D2DRenderer::draw_text(rt, font_bold, "XOSC", title_rc, col_ink());
        D2D1_RECT_F sub_rc = D2D1::RectF(mx + 115.0f, my + 18.0f, mx + 460.0f, my + 44.0f);
        D2DRenderer::draw_text(rt, font_small, "FOUR-OSCILLATOR / POLYPHONIC SYNTHESIZER", sub_rc, col_muted());

        // Stereo Peak Meters
        float meter_x = mx + mw - 460.0f;
        float peak_l = synth->peak_l();
        float peak_r = synth->peak_r();
        for (int c = 0; c < 2; ++c) {
            float pk = (c == 0 ? peak_l : peak_r);
            float m_top = my + 16.0f + static_cast<float>(c) * 11.0f;
            D2D1_RECT_F slot_rc = D2D1::RectF(meter_x, m_top, meter_x + 90.0f, m_top + 7.0f);
            D2DRenderer::draw_rounded_box(rt, slot_rc, D2D1::ColorF(0x22 / 255.f, 0x24 / 255.f, 0x20 / 255.f),
                                         D2D1::ColorF(0x11 / 255.f, 0x12 / 255.f, 0x0f / 255.f), 2.0f);
            float bar_w = std::clamp(pk, 0.0f, 1.0f) * 88.0f;
            if (bar_w > 1.0f) {
                D2D1_RECT_F fill_rc = D2D1::RectF(meter_x + 1.0f, m_top + 1.0f, meter_x + 1.0f + bar_w, m_top + 6.0f);
                D2D1_COLOR_F bar_col = (pk > 0.95f) ? D2D1::ColorF(0xff / 255.f, 0x60 / 255.f, 0x50 / 255.f) : col_accent();
                D2DRenderer::draw_rounded_box(rt, fill_rc, bar_col, bar_col, 1.5f);
            }
        }

        // Factory Presets [Init] [Thick] [Bass] [Pad]
        const char* p_names[4] = {"Init", "Thick", "Bass", "Pad"};
        float p_start_x = mx + mw - 355.0f;
        for (int p = 0; p < 4; ++p) {
            D2D1_RECT_F p_rc = D2D1::RectF(p_start_x + p * 58.0f, my + 14.0f, p_start_x + (p + 1) * 58.0f - 4.0f, my + 42.0f);
            D2DRenderer::draw_button(rt, font_small, p_rc, p_names[p], false, col_accent(),
                                     D2D1::ColorF(0x28 / 255.f, 0x2a / 255.f, 0x26 / 255.f), 3.0f);
        }

        // PANIC Button
        D2D1_RECT_F panic_rc = D2D1::RectF(mx + mw - 116.0f, my + 14.0f, mx + mw - 52.0f, my + 42.0f);
        D2DRenderer::draw_button(rt, font_bold, panic_rc, "PANIC", false, col_accent(),
                                 D2D1::ColorF(0x40 / 255.f, 0x22 / 255.f, 0x20 / 255.f), 3.0f);

        // Close Button [✕]
        D2D1_RECT_F close_rc = D2D1::RectF(mx + mw - 46.0f, my + 14.0f, mx + mw - 18.0f, my + 42.0f);
        D2DRenderer::draw_button(rt, font_bold, close_rc, "✕", false, D2D1::ColorF(0xff / 255.f, 0x72 / 255.f, 0x6b / 255.f),
                                 D2D1::ColorF(0x24 / 255.f, 0x26 / 255.f, 0x22 / 255.f), 3.0f);

        // Amber Header Separator Line
        ID2D1SolidColorBrush* br_amber_bar = nullptr;
        rt->CreateSolidColorBrush(col_accent(), &br_amber_bar);
        if (br_amber_bar) {
            rt->DrawLine(D2D1::Point2F(mx + 20.0f, my + 52.0f),
                         D2D1::Point2F(mx + mw - 20.0f, my + 52.0f), br_amber_bar, 1.5f);
            br_amber_bar->Release();
        }

        // 5. Right Output Mixer Panel (Always Visible!)
        float mixer_x = mx + mw - 195.0f;
        float mixer_y = my + 94.0f;
        float mixer_w = 180.0f;
        float mixer_h = mh - 198.0f;

        // 6. Main Center Grid (2 Columns x 3 Rows)
        float grid_x = mx + 20.0f;
        float grid_y = my + 94.0f;
        float grid_w = mixer_x - grid_x - 12.0f;
        float grid_h = mixer_h;
        float top_h = (grid_h - 10.0f) * 0.64f;
        float flt_y = grid_y + top_h + 10.0f;
        float flt_h = grid_h - top_h - 10.0f;
        float osc_total_w = (grid_w - 10.0f) * 0.64f;
        float amp_x = grid_x + osc_total_w + 10.0f;
        float amp_w = grid_w - osc_total_w - 10.0f;
        float osc_col_w = (osc_total_w - 8.0f) * 0.5f;
        float osc_row_h = (top_h - 8.0f) * 0.5f;
        float flt_col_w = (grid_w - 10.0f) * 0.5f;

        float mod_col_w = (grid_w - 10.0f) * 0.5f;
        float mod_row_h = (grid_h - 8.0f) * 0.5f;

        // Legacy col_w / row_h for Tab 2
        float col_w = (grid_w - 10.0f) * 0.5f;
        float row_h = (grid_h - 12.0f) / 3.0f;

        // 4. Tabs Bar (aligned with grid_w)
        const char* tab_names[3] = {"OSCILLATORS + FILTERS", "MOD ENVELOPES + ROUTING", "EFFECTS + PERFORMANCE"};
        float tab_w = (grid_w - 16.0f) / 3.0f;
        for (int t = 0; t < 3; ++t) {
            float tx = grid_x + static_cast<float>(t) * (tab_w + 8.0f);
            D2D1_RECT_F t_rc = D2D1::RectF(tx, my + 58.0f, tx + tab_w, my + 88.0f);
            bool is_active = (active_tab == t);
            D2D1_COLOR_F tab_bg = is_active ? col_panel() : D2D1::ColorF(0x20 / 255.f, 0x22 / 255.f, 0x1f / 255.f);
            D2D1_COLOR_F tab_border = is_active ? col_accent() : D2D1::ColorF(0x35 / 255.f, 0x36 / 255.f, 0x32 / 255.f);
            D2DRenderer::draw_rounded_box(rt, t_rc, tab_bg, tab_border, 3.0f, is_active ? 1.5f : 1.0f);
            D2DRenderer::draw_text(rt, font_bold, tab_names[t], t_rc, is_active ? col_accent() : col_muted(),
                                   DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        D2D1_RECT_F mixer_rc = D2D1::RectF(mixer_x, mixer_y, mixer_x + mixer_w, mixer_y + mixer_h);
        draw_module_panel_d2d(rt, font_bold, font_small, mixer_rc, "OUTPUT MIXER", "Post FX / soft ceiling");

        // Mixer Power (Illuminated Radio/LED Switch)
        bool m_on = synth->get_param_by_id("master_on") > 0.5f;
        D2D1_RECT_F m_pwr_rc = D2D1::RectF(mixer_x + 20.0f, mixer_y + 48.0f, mixer_x + 50.0f, mixer_y + 66.0f);
        draw_toggle_led_d2d(rt, m_pwr_rc, m_on);
        D2D1_RECT_F m_lbl_rc = D2D1::RectF(mixer_x + 56.0f, mixer_y + 48.0f, mixer_x + mixer_w - 20.0f, mixer_y + 66.0f);
        D2DRenderer::draw_text(rt, font_small, m_on ? "OUTPUT ON" : "MUTED", m_lbl_rc, m_on ? col_accent() : col_muted(),
                               DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Master Gain Knob
        float m_gain = synth->get_param_by_id("master_gain");
        int m_gain_idx = xosc::index("master_gain");
        float norm_gain = synth->get_parameter(static_cast<uint32_t>(m_gain_idx));
        D2D1_RECT_F mg_rc = D2D1::RectF(mixer_x + 15.0f, mixer_y + 86.0f, mixer_x + mixer_w - 15.0f, mixer_y + 180.0f);
        draw_knob_d2d(rt, font_bold, font_small, mg_rc, norm_gain, "GAIN", format_param_val(xosc::specs()[m_gain_idx], m_gain));

        // Master Width Knob
        float m_width = synth->get_param_by_id("master_width");
        int m_width_idx = xosc::index("master_width");
        float norm_width = synth->get_parameter(static_cast<uint32_t>(m_width_idx));
        D2D1_RECT_F mw_rc = D2D1::RectF(mixer_x + 15.0f, mixer_y + 190.0f, mixer_x + mixer_w - 15.0f, mixer_y + 284.0f);
        draw_knob_d2d(rt, font_bold, font_small, mw_rc, norm_width, "WIDTH", format_param_val(xosc::specs()[m_width_idx], m_width));

        // Polyphony Choices [4] [8] [16] [32]
        D2D1_RECT_F poly_lbl = D2D1::RectF(mixer_x + 15.0f, mixer_y + 292.0f, mixer_x + mixer_w - 15.0f, mixer_y + 310.0f);
        D2DRenderer::draw_text(rt, font_small, "POLYPHONY", poly_lbl, col_ink(), DWRITE_TEXT_ALIGNMENT_CENTER);
        int poly_idx = static_cast<int>(std::round(synth->get_param_by_id("polyphony")));
        const char* poly_names[4] = {"4", "8", "16", "32"};
        for (int p = 0; p < 4; ++p) {
            float px = mixer_x + 16.0f + static_cast<float>(p) * 37.0f;
            D2D1_RECT_F prc = D2D1::RectF(px, mixer_y + 314.0f, px + 33.0f, mixer_y + 338.0f);
            D2DRenderer::draw_button(rt, font_small, prc, poly_names[p], (p == poly_idx), col_accent(),
                                     D2D1::ColorF(0x20 / 255.f, 0x22 / 255.f, 0x1f / 255.f), 3.0f);
        }

        auto render_osc = [&](int osc_idx, float px, float py, float pw, float ph) {
            std::string o_prefix = "o" + std::to_string(osc_idx) + "_";
            D2D1_RECT_F prc = D2D1::RectF(px, py, px + pw, py + ph);
            draw_module_panel_d2d(rt, font_bold, font_small, prc,
                                  "OSC " + std::to_string(osc_idx + 1), "PolyBLEP / 1-12 voices");

            bool on = synth->get_param_by_id(o_prefix + "on") > 0.5f;
            D2D1_RECT_F pwr_rc = D2D1::RectF(px + 6.0f, py + 42.0f, px + 34.0f, py + 60.0f);
            draw_toggle_led_d2d(rt, pwr_rc, on);

            // Wave buttons: Sine, Square, Saw (vector icons)
            int wave = static_cast<int>(std::round(synth->get_param_by_id(o_prefix + "wave")));
            for (int w = 0; w < 3; ++w) {
                float wx = px + 38.0f + static_cast<float>(w) * 24.0f;
                D2D1_RECT_F wrc = D2D1::RectF(wx, py + 40.0f, wx + 21.0f, py + 62.0f);
                draw_wave_icon_d2d(rt, wrc, w, (w == wave));
            }

            // Unison counter: [-] U N [+]
            int voices = static_cast<int>(std::round(synth->get_param_by_id(o_prefix + "voices"))) + 1;
            D2D1_RECT_F uni_dn = D2D1::RectF(px + 112.0f, py + 40.0f, px + 128.0f, py + 62.0f);
            D2D1_RECT_F uni_val = D2D1::RectF(px + 130.0f, py + 40.0f, px + 158.0f, py + 62.0f);
            D2D1_RECT_F uni_up = D2D1::RectF(px + 160.0f, py + 40.0f, px + 176.0f, py + 62.0f);
            D2DRenderer::draw_button(rt, font_bold, uni_dn, "-", false, col_accent(), D2D1::ColorF(0x20 / 255.f, 0x22 / 255.f, 0x1f / 255.f), 2.5f);
            D2DRenderer::draw_rounded_box(rt, uni_val, D2D1::ColorF(0x20 / 255.f, 0x22 / 255.f, 0x1f / 255.f), D2D1::ColorF(0x35 / 255.f, 0x36 / 255.f, 0x32 / 255.f), 2.5f);
            D2DRenderer::draw_text(rt, font_small, "U " + std::to_string(voices), uni_val, col_ink(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            D2DRenderer::draw_button(rt, font_bold, uni_up, "+", false, col_accent(), D2D1::ColorF(0x20 / 255.f, 0x22 / 255.f, 0x1f / 255.f), 2.5f);

            // Responsive Tune & Phase Knobs
            float tune_x = px + 180.0f;
            float rem_w = std::max(40.0f, (px + pw - 4.0f) - tune_x);
            float half_w = rem_w * 0.5f;

            int tune_idx = xosc::index(o_prefix + "tune");
            float tune_val = synth->get_param_by_id(o_prefix + "tune");
            D2D1_RECT_F tune_rc = D2D1::RectF(tune_x, py + 36.0f, tune_x + half_w - 2.0f, py + 92.0f);
            draw_knob_d2d(rt, font_bold, font_small, tune_rc, synth->get_parameter(static_cast<uint32_t>(tune_idx)),
                          "TUNE", format_param_val(xosc::specs()[tune_idx], tune_val), !on);

            int phase_idx = xosc::index(o_prefix + "phase");
            float phase_val = synth->get_param_by_id(o_prefix + "phase");
            D2D1_RECT_F phase_rc = D2D1::RectF(tune_x + half_w + 2.0f, py + 36.0f, px + pw - 4.0f, py + 92.0f);
            draw_knob_d2d(rt, font_bold, font_small, phase_rc, synth->get_parameter(static_cast<uint32_t>(phase_idx)),
                          "PHASE", format_param_val(xosc::specs()[phase_idx], phase_val), !on);

            // Bottom Knobs: Vol, Pan, Detune, Stereo
            float k_w = (pw - 12.0f) * 0.25f;
            const char* osc_knob_ids[4] = {"vol", "pan", "detune", "stereo"};
            const char* osc_knob_titles[4] = {"VOL", "PAN", "DETUNE", "STEREO"};
            for (int k = 0; k < 4; ++k) {
                int kidx = xosc::index(o_prefix + osc_knob_ids[k]);
                float kval = synth->get_param_by_id(o_prefix + osc_knob_ids[k]);
                float kx = px + 6.0f + static_cast<float>(k) * k_w;
                D2D1_RECT_F k_rc = D2D1::RectF(kx, py + 94.0f, kx + k_w, py + ph - 4.0f);
                draw_knob_d2d(rt, font_bold, font_small, k_rc, synth->get_parameter(static_cast<uint32_t>(kidx)),
                              osc_knob_titles[k], format_param_val(xosc::specs()[kidx], kval), !on);
            }
        };

        auto render_amp_env = [&](int amp_idx, float px, float py, float pw, float ph) {
            std::string a_prefix = "a" + std::to_string(amp_idx) + "_";
            D2D1_RECT_F prc = D2D1::RectF(px, py, px + pw, py + ph);
            draw_module_panel_d2d(rt, font_bold, font_small, prc,
                                  "AMP ENV " + std::to_string(amp_idx + 1),
                                  "After filters / multiple envelopes multiply");

            bool on = synth->get_param_by_id(a_prefix + "on") > 0.5f;
            D2D1_RECT_F pwr_rc = D2D1::RectF(px + 8.0f, py + 42.0f, px + 36.0f, py + 60.0f);
            draw_toggle_led_d2d(rt, pwr_rc, on);

            // OSC Routing: [O1] [O2] [O3] [O4]
            float r_start = px + 44.0f;
            for (int r = 0; r < 4; ++r) {
                bool routed = synth->get_param_by_id(a_prefix + "route" + std::to_string(r)) > 0.5f;
                float rx = r_start + static_cast<float>(r) * 34.0f;
                D2D1_RECT_F r_rc = D2D1::RectF(rx, py + 40.0f, rx + 30.0f, py + 62.0f);
                D2DRenderer::draw_button(rt, font_small, r_rc, "O" + std::to_string(r + 1), routed, col_accent(),
                                         D2D1::ColorF(0x20 / 255.f, 0x22 / 255.f, 0x1f / 255.f), 2.5f);
            }

            // ADSR Knobs
            float k_w = (pw - 16.0f) * 0.25f;
            const char* env_knobs[4] = {"attack", "decay", "sustain", "release"};
            const char* env_titles[4] = {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"};
            for (int k = 0; k < 4; ++k) {
                int kidx = xosc::index(a_prefix + env_knobs[k]);
                float kval = synth->get_param_by_id(a_prefix + env_knobs[k]);
                float kx = px + 8.0f + static_cast<float>(k) * k_w;
                D2D1_RECT_F k_rc = D2D1::RectF(kx, py + 68.0f, kx + k_w, py + ph - 4.0f);
                draw_knob_d2d(rt, font_bold, font_small, k_rc, synth->get_parameter(static_cast<uint32_t>(kidx)),
                              env_titles[k], format_param_val(xosc::specs()[kidx], kval), !on);
            }
        };

        auto render_filter = [&](int flt_idx, float px, float py, float pw, float ph) {
            std::string f_prefix = "f" + std::to_string(flt_idx) + "_";
            D2D1_RECT_F prc = D2D1::RectF(px, py, px + pw, py + ph);
            draw_module_panel_d2d(rt, font_bold, font_small, prc,
                                  "FILTER " + std::to_string(flt_idx + 1),
                                  flt_idx == 0 ? "Per oscillator / before Filter 2" : "Per oscillator / after Filter 1");

            bool on = synth->get_param_by_id(f_prefix + "on") > 0.5f;
            D2D1_RECT_F pwr_rc = D2D1::RectF(px + 8.0f, py + 42.0f, px + 36.0f, py + 60.0f);
            draw_toggle_led_d2d(rt, pwr_rc, on);

            // Filter Type: [LP] [HP] [BP] [NOTCH] (vector curve icons)
            int type = static_cast<int>(std::round(synth->get_param_by_id(f_prefix + "type")));
            for (int t = 0; t < 4; ++t) {
                float tx = px + 44.0f + static_cast<float>(t) * 34.0f;
                D2D1_RECT_F trc = D2D1::RectF(tx, py + 40.0f, tx + 30.0f, py + 62.0f);
                draw_filter_curve_d2d(rt, trc, t, (t == type));
            }

            // OSC Routing: [O1] [O2] [O3] [O4]
            float r_start = px + 186.0f;
            for (int r = 0; r < 4; ++r) {
                bool routed = synth->get_param_by_id(f_prefix + "route" + std::to_string(r)) > 0.5f;
                float rx = r_start + static_cast<float>(r) * 34.0f;
                D2D1_RECT_F r_rc = D2D1::RectF(rx, py + 40.0f, rx + 30.0f, py + 62.0f);
                D2DRenderer::draw_button(rt, font_small, r_rc, "O" + std::to_string(r + 1), routed, col_accent(),
                                         D2D1::ColorF(0x20 / 255.f, 0x22 / 255.f, 0x1f / 255.f), 2.5f);
            }

            // Knobs: Cutoff, Resonance, Drive
            float k_w = (pw - 16.0f) / 3.0f;
            const char* flt_knobs[3] = {"cutoff", "res", "drive"};
            const char* flt_titles[3] = {"CUTOFF", "RESONANCE", "DRIVE"};
            for (int k = 0; k < 3; ++k) {
                int kidx = xosc::index(f_prefix + flt_knobs[k]);
                float kval = synth->get_param_by_id(f_prefix + flt_knobs[k]);
                float kx = px + 8.0f + static_cast<float>(k) * k_w;
                D2D1_RECT_F k_rc = D2D1::RectF(kx, py + 68.0f, kx + k_w, py + ph - 4.0f);
                draw_knob_d2d(rt, font_bold, font_small, k_rc, synth->get_parameter(static_cast<uint32_t>(kidx)),
                              flt_titles[k], format_param_val(xosc::specs()[kidx], kval), !on);
            }
        };

        auto render_mod_env = [&](int env_idx, float px, float py, float pw, float ph) {
            std::string e_prefix = "e" + std::to_string(env_idx) + "_";
            D2D1_RECT_F prc = D2D1::RectF(px, py, px + pw, py + ph);
            draw_module_panel_d2d(rt, font_bold, font_small, prc,
                                  "MOD ENV " + std::to_string(env_idx + 1),
                                  "Target dropdown + mix / select destinations");

            bool on = synth->get_param_by_id(e_prefix + "on") > 0.5f;
            D2D1_RECT_F pwr_rc = D2D1::RectF(px + 8.0f, py + 42.0f, px + 36.0f, py + 60.0f);
            draw_toggle_led_d2d(rt, pwr_rc, on);

            // Target dropdown selector
            int target = std::clamp(static_cast<int>(std::round(synth->get_param_by_id(e_prefix + "target"))), 0, 8);
            const char* tgt_names[9] = {
                "Osc Volume", "Osc Pitch", "Cutoff", "Flt 1 Cutoff",
                "Flt 1 Res", "Flt 2 Cutoff", "Flt 2 Res", "Filter Drive", "Osc Pan"
            };
            D2D1_RECT_F dd_rc = D2D1::RectF(px + 42.0f, py + 40.0f, px + 175.0f, py + 62.0f);
            std::string dd_label = std::string(tgt_names[target]) + "  ▼";
            D2DRenderer::draw_button(rt, font_small, dd_rc, dd_label, on, col_accent(),
                             D2D1::ColorF(0x20 / 255.f, 0x22 / 255.f, 0x1f / 255.f), 2.5f);

            // OSC Routing: [O1] [O2] [O3] [O4]
            float r_start = px + 182.0f;
            for (int r = 0; r < 4; ++r) {
                bool routed = synth->get_param_by_id(e_prefix + "route" + std::to_string(r)) > 0.5f;
                float rx = r_start + static_cast<float>(r) * 32.0f;
                D2D1_RECT_F r_rc = D2D1::RectF(rx, py + 40.0f, rx + 28.0f, py + 62.0f);
                D2DRenderer::draw_button(rt, font_small, r_rc, "O" + std::to_string(r + 1), routed, col_accent(),
                                         D2D1::ColorF(0x20 / 255.f, 0x22 / 255.f, 0x1f / 255.f), 2.5f);
            }

            // Mix Knob
            int mix_idx = xosc::index(e_prefix + "mix");
            float mix_val = synth->get_param_by_id(e_prefix + "mix");
            D2D1_RECT_F mix_rc = D2D1::RectF(px + 316.0f, py + 36.0f, px + pw - 8.0f, py + 92.0f);
            draw_knob_d2d(rt, font_bold, font_small, mix_rc, synth->get_parameter(static_cast<uint32_t>(mix_idx)),
                          "MIX", format_param_val(xosc::specs()[mix_idx], mix_val), !on);

            // ADSR Knobs
            float k_w = (pw - 16.0f) * 0.25f;
            const char* env_knobs[4] = {"attack", "decay", "sustain", "release"};
            const char* env_titles[4] = {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"};
            for (int k = 0; k < 4; ++k) {
                int kidx = xosc::index(e_prefix + env_knobs[k]);
                float kval = synth->get_param_by_id(e_prefix + env_knobs[k]);
                float kx = px + 8.0f + static_cast<float>(k) * k_w;
                D2D1_RECT_F k_rc = D2D1::RectF(kx, py + 96.0f, kx + k_w, py + ph - 6.0f);
                draw_knob_d2d(rt, font_bold, font_small, k_rc, synth->get_parameter(static_cast<uint32_t>(kidx)),
                              env_titles[k], format_param_val(xosc::specs()[kidx], kval), !on);
            }
        };

        if (active_tab == 0) {
            // OSCILLATORS + FILTERS (Layout: 2x2 Osc on left, 2 Amp Envs on right, 2 Filters underneath)
            render_osc(0, grid_x, grid_y, osc_col_w, osc_row_h);
            render_osc(1, grid_x + osc_col_w + 8.0f, grid_y, osc_col_w, osc_row_h);
            render_osc(2, grid_x, grid_y + osc_row_h + 8.0f, osc_col_w, osc_row_h);
            render_osc(3, grid_x + osc_col_w + 8.0f, grid_y + osc_row_h + 8.0f, osc_col_w, osc_row_h);

            render_amp_env(0, amp_x, grid_y, amp_w, osc_row_h);
            render_amp_env(1, amp_x, grid_y + osc_row_h + 8.0f, amp_w, osc_row_h);

            render_filter(0, grid_x, flt_y, flt_col_w, flt_h);
            render_filter(1, grid_x + flt_col_w + 10.0f, flt_y, flt_col_w, flt_h);
        } else if (active_tab == 1) {
            // MOD ENVELOPES + ROUTING (2x2 Grid)
            render_mod_env(0, grid_x, grid_y, mod_col_w, mod_row_h);
            render_mod_env(1, grid_x + mod_col_w + 10.0f, grid_y, mod_col_w, mod_row_h);
            render_mod_env(2, grid_x, grid_y + mod_row_h + 8.0f, mod_col_w, mod_row_h);
            render_mod_env(3, grid_x + mod_col_w + 10.0f, grid_y + mod_row_h + 8.0f, mod_col_w, mod_row_h);
        } else {
            // EFFECTS + PERFORMANCE
            // Panel 1: DISTORTION
            {
                float px = grid_x;
                float py = grid_y;
                D2D1_RECT_F prc = D2D1::RectF(px, py, px + col_w, py + row_h);
                draw_module_panel_d2d(rt, font_bold, font_small, prc, "01 / DISTORTION", "Warm saturation / parallel mix");
                bool on = synth->get_param_by_id("dist_on") > 0.5f;
                D2D1_RECT_F pwr_rc = D2D1::RectF(px + 8.0f, py + 42.0f, px + 36.0f, py + 60.0f);
                draw_toggle_led_d2d(rt, pwr_rc, on);

                int d_idx = xosc::index("dist_drive");
                int m_idx = xosc::index("dist_mix");
                D2D1_RECT_F d_rc = D2D1::RectF(px + 60.0f, py + 70.0f, px + 210.0f, py + row_h - 4.0f);
                D2D1_RECT_F m_rc = D2D1::RectF(px + 220.0f, py + 70.0f, px + col_w - 20.0f, py + row_h - 4.0f);
                draw_knob_d2d(rt, font_bold, font_small, d_rc, synth->get_parameter(static_cast<uint32_t>(d_idx)),
                              "DRIVE", format_param_val(xosc::specs()[d_idx], synth->get_param_by_id("dist_drive")), !on);
                draw_knob_d2d(rt, font_bold, font_small, m_rc, synth->get_parameter(static_cast<uint32_t>(m_idx)),
                              "MIX", format_param_val(xosc::specs()[m_idx], synth->get_param_by_id("dist_mix")), !on);
            }

            // Panel 2: EQ
            {
                float px = grid_x + col_w + 10.0f;
                float py = grid_y;
                D2D1_RECT_F prc = D2D1::RectF(px, py, px + col_w, py + row_h);
                draw_module_panel_d2d(rt, font_bold, font_small, prc, "02 / EQ", "Three broad tone bands");
                bool on = synth->get_param_by_id("eq_on") > 0.5f;
                D2D1_RECT_F pwr_rc = D2D1::RectF(px + 8.0f, py + 42.0f, px + 36.0f, py + 60.0f);
                draw_toggle_led_d2d(rt, pwr_rc, on);

                float kw = (col_w - 20.0f) / 3.0f;
                const char* eq_ids[3] = {"eq_low", "eq_mid", "eq_high"};
                const char* eq_titles[3] = {"LOW 180Hz", "MID 1kHz", "HIGH 5kHz"};
                for (int k = 0; k < 3; ++k) {
                    int kidx = xosc::index(eq_ids[k]);
                    float kx = px + 10.0f + static_cast<float>(k) * kw;
                    D2D1_RECT_F k_rc = D2D1::RectF(kx, py + 70.0f, kx + kw, py + row_h - 4.0f);
                    draw_knob_d2d(rt, font_bold, font_small, k_rc, synth->get_parameter(static_cast<uint32_t>(kidx)),
                                  eq_titles[k], format_param_val(xosc::specs()[kidx], synth->get_param_by_id(eq_ids[k])), !on);
                }
            }

            // Panel 3: COMPRESSOR
            {
                float px = grid_x;
                float py = grid_y + row_h + 6.0f;
                D2D1_RECT_F prc = D2D1::RectF(px, py, px + col_w, py + row_h);
                draw_module_panel_d2d(rt, font_bold, font_small, prc, "03 / COMPRESSOR", "Stereo linked / peak detection");
                bool on = synth->get_param_by_id("comp_on") > 0.5f;
                D2D1_RECT_F pwr_rc = D2D1::RectF(px + 8.0f, py + 42.0f, px + 36.0f, py + 60.0f);
                draw_toggle_led_d2d(rt, pwr_rc, on);

                float kw = (col_w - 20.0f) / 5.0f;
                const char* comp_ids[5] = {"comp_threshold", "comp_ratio", "comp_attack", "comp_release", "comp_makeup"};
                const char* comp_titles[5] = {"THRESH", "RATIO", "ATTACK", "RELEASE", "MAKEUP"};
                for (int k = 0; k < 5; ++k) {
                    int kidx = xosc::index(comp_ids[k]);
                    float kx = px + 10.0f + static_cast<float>(k) * kw;
                    D2D1_RECT_F k_rc = D2D1::RectF(kx, py + 70.0f, kx + kw, py + row_h - 4.0f);
                    draw_knob_d2d(rt, font_bold, font_small, k_rc, synth->get_parameter(static_cast<uint32_t>(kidx)),
                                  comp_titles[k], format_param_val(xosc::specs()[kidx], synth->get_param_by_id(comp_ids[k])), !on);
                }
            }

            // Panel 4: DELAY
            {
                float px = grid_x + col_w + 10.0f;
                float py = grid_y + row_h + 6.0f;
                D2D1_RECT_F prc = D2D1::RectF(px, py, px + col_w, py + row_h);
                draw_module_panel_d2d(rt, font_bold, font_small, prc, "04 / DELAY", "Stereo / time in seconds");
                bool on = synth->get_param_by_id("delay_on") > 0.5f;
                D2D1_RECT_F pwr_rc = D2D1::RectF(px + 8.0f, py + 42.0f, px + 36.0f, py + 60.0f);
                draw_toggle_led_d2d(rt, pwr_rc, on);

                float kw = (col_w - 20.0f) / 3.0f;
                const char* dly_ids[3] = {"delay_time", "delay_feedback", "delay_mix"};
                const char* dly_titles[3] = {"TIME", "FEEDBACK", "MIX"};
                for (int k = 0; k < 3; ++k) {
                    int kidx = xosc::index(dly_ids[k]);
                    float kx = px + 10.0f + static_cast<float>(k) * kw;
                    D2D1_RECT_F k_rc = D2D1::RectF(kx, py + 70.0f, kx + kw, py + row_h - 4.0f);
                    draw_knob_d2d(rt, font_bold, font_small, k_rc, synth->get_parameter(static_cast<uint32_t>(kidx)),
                                  dly_titles[k], format_param_val(xosc::specs()[kidx], synth->get_param_by_id(dly_ids[k])), !on);
                }
            }

            // Panel 5: REVERB
            {
                float px = grid_x;
                float py = grid_y + (row_h + 6.0f) * 2.0f;
                D2D1_RECT_F prc = D2D1::RectF(px, py, px + col_w, py + row_h);
                draw_module_panel_d2d(rt, font_bold, font_small, prc, "05 / REVERB", "Stereo room / damped decay");
                bool on = synth->get_param_by_id("reverb_on") > 0.5f;
                D2D1_RECT_F pwr_rc = D2D1::RectF(px + 8.0f, py + 42.0f, px + 36.0f, py + 60.0f);
                draw_toggle_led_d2d(rt, pwr_rc, on);

                float kw = (col_w - 20.0f) / 3.0f;
                const char* rev_ids[3] = {"reverb_size", "reverb_damp", "reverb_mix"};
                const char* rev_titles[3] = {"SIZE", "DAMP", "MIX"};
                for (int k = 0; k < 3; ++k) {
                    int kidx = xosc::index(rev_ids[k]);
                    float kx = px + 10.0f + static_cast<float>(k) * kw;
                    D2D1_RECT_F k_rc = D2D1::RectF(kx, py + 70.0f, kx + kw, py + row_h - 4.0f);
                    draw_knob_d2d(rt, font_bold, font_small, k_rc, synth->get_parameter(static_cast<uint32_t>(kidx)),
                                  rev_titles[k], format_param_val(xosc::specs()[kidx], synth->get_param_by_id(rev_ids[k])), !on);
                }
            }

            // Panel 6: PERFORMANCE
            {
                float px = grid_x + col_w + 10.0f;
                float py = grid_y + (row_h + 6.0f) * 2.0f;
                D2D1_RECT_F prc = D2D1::RectF(px, py, px + col_w, py + row_h);
                draw_module_panel_d2d(rt, font_bold, font_small, prc, "06 / PERFORMANCE", "N: every transition / S: overlapping keys");

                bool mono = synth->get_param_by_id("mono_legato") > 0.5f;
                D2D1_RECT_F mono_rc = D2D1::RectF(px + 14.0f, py + 46.0f, px + 42.0f, py + 64.0f);
                draw_toggle_led_d2d(rt, mono_rc, mono);
                D2D1_RECT_F mono_lbl = D2D1::RectF(px + 46.0f, py + 44.0f, px + 130.0f, py + 66.0f);
                D2DRenderer::draw_text(rt, font_small, "MONO LEGATO", mono_lbl, mono ? col_accent() : col_muted(),
                                       DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

                bool glide = synth->get_param_by_id("glide_on") > 0.5f;
                D2D1_RECT_F glide_rc = D2D1::RectF(px + 136.0f, py + 46.0f, px + 164.0f, py + 64.0f);
                draw_toggle_led_d2d(rt, glide_rc, glide);
                D2D1_RECT_F glide_lbl = D2D1::RectF(px + 168.0f, py + 44.0f, px + 250.0f, py + 66.0f);
                D2DRenderer::draw_text(rt, font_small, "PORTAMENTO", glide_lbl, glide ? col_accent() : col_muted(),
                                       DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

                // Glide mode [N] [S]
                int gmode = static_cast<int>(std::round(synth->get_param_by_id("glide_mode")));
                D2D1_RECT_F gm_n = D2D1::RectF(px + 14.0f, py + 86.0f, px + 126.0f, py + 112.0f);
                D2D1_RECT_F gm_s = D2D1::RectF(px + 14.0f, py + 116.0f, px + 126.0f, py + 142.0f);
                D2DRenderer::draw_button(rt, font_small, gm_n, "N / NORMAL", (gmode == 0), col_accent(), D2D1::ColorF(0x20 / 255.f, 0x22 / 255.f, 0x1f / 255.f), 2.5f);
                D2DRenderer::draw_button(rt, font_small, gm_s, "S / SLIDE", (gmode == 1), col_accent(), D2D1::ColorF(0x20 / 255.f, 0x22 / 255.f, 0x1f / 255.f), 2.5f);

                // Glide time knob
                int gt_idx = xosc::index("glide_time");
                D2D1_RECT_F gt_rc = D2D1::RectF(px + 150.0f, py + 76.0f, px + col_w - 20.0f, py + row_h - 4.0f);
                draw_knob_d2d(rt, font_bold, font_small, gt_rc, synth->get_parameter(static_cast<uint32_t>(gt_idx)),
                              "GLIDE TIME", format_param_val(xosc::specs()[gt_idx], synth->get_param_by_id("glide_time")), !glide);
            }
        }

        // 7. Bottom Section: Status Bar & Virtual Keyboard
        float bot_y = my + mh - 96.0f;
        std::string status_txt = "MIDI  /  " + std::to_string(synth->active_voices()) +
                                 " active notes     |     Pitch wheel +/-2 st     |     Sustain CC64     |     Init: OSC 1 > AMP 1 > Output";
        D2D1_RECT_F st_rc = D2D1::RectF(mx + 20.0f, bot_y, mx + mw - 20.0f, bot_y + 18.0f);
        D2DRenderer::draw_text(rt, font_small, status_txt, st_rc, col_muted());

        // Piano Keyboard (C2 to C6 = 49 keys)
        float kb_y = bot_y + 20.0f;
        float kb_h = mh - (kb_y - my) - 8.0f;
        float kb_x = mx + 20.0f;
        float kb_w = mw - 40.0f;

        // Count white keys from MIDI 36 to 84 (49 keys, 29 white keys)
        const int kStartKey = 36;
        const int kEndKey = 84;
        auto is_black_key = [](int note) {
            int semi = note % 12;
            return semi == 1 || semi == 3 || semi == 6 || semi == 8 || semi == 10;
        };

        int white_count = 0;
        for (int k = kStartKey; k <= kEndKey; ++k) {
            if (!is_black_key(k)) ++white_count;
        }

        float wk_w = kb_w / static_cast<float>(white_count);
        float bk_w = wk_w * 0.62f;
        float bk_h = kb_h * 0.60f;

        // Draw White Keys
        float cur_wx = kb_x;
        for (int k = kStartKey; k <= kEndKey; ++k) {
            if (!is_black_key(k)) {
                D2D1_RECT_F wk_rc = D2D1::RectF(cur_wx, kb_y, cur_wx + wk_w - 1.0f, kb_y + kb_h);
                D2DRenderer::draw_rounded_box(rt, wk_rc, col_ink(), D2D1::ColorF(0x28 / 255.f, 0x26 / 255.f, 0x22 / 255.f), 2.0f);
                cur_wx += wk_w;
            }
        }

        // Draw Black Keys
        cur_wx = kb_x;
        for (int k = kStartKey; k <= kEndKey; ++k) {
            if (is_black_key(k)) {
                float bk_x = cur_wx - (bk_w * 0.5f);
                D2D1_RECT_F bk_rc = D2D1::RectF(bk_x, kb_y, bk_x + bk_w, kb_y + bk_h);
                D2DRenderer::draw_rounded_box(rt, bk_rc, D2D1::ColorF(0x17 / 255.f, 0x18 / 255.f, 0x15 / 255.f),
                                             D2D1::ColorF(0x0a / 255.f, 0x0a / 255.f, 0x09 / 255.f), 1.5f);
            } else {
                cur_wx += wk_w;
            }
        }
    }

    // ========================================================================
    // Main Render Pipeline (GDI Fallback)
    // ========================================================================
    static void render_gdi(HDC hdc, HFONT font_bold, HFONT font_small,
                           plugins::XOSCDevice* synth, const RECT& bounds, int active_tab) {
        if (!hdc || !synth) return;
        int mx = bounds.left;
        int my = bounds.top;
        int mw = bounds.right - bounds.left;
        int mh = bounds.bottom - bounds.top;

        GuiRenderer::draw_rounded_box(hdc, bounds, gdi_bg(), RGB(50, 52, 48), 4);

        // Walnut cheeks
        RECT cheek_l{mx, my, mx + 12, my + mh};
        RECT cheek_r{mx + mw - 12, my, mx + mw, my + mh};
        GuiRenderer::fill_rect(hdc, cheek_l, RGB(82, 51, 32));
        GuiRenderer::fill_rect(hdc, cheek_r, RGB(82, 51, 32));

        // Header
        SelectObject(hdc, font_bold);
        RECT title_rc{mx + 22, my + 8, mx + 120, my + 48};
        GuiRenderer::draw_text(hdc, "XOSC", title_rc, gdi_ink(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(hdc, font_small);
        RECT sub_rc{mx + 115, my + 18, mx + 460, my + 44};
        GuiRenderer::draw_text(hdc, "FOUR-OSCILLATOR / POLYPHONIC SYNTHESIZER", sub_rc, gdi_muted(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Presets
        const char* p_names[4] = {"Init", "Thick", "Bass", "Pad"};
        int p_start_x = mx + mw - 355;
        for (int p = 0; p < 4; ++p) {
            RECT p_rc{p_start_x + p * 58, my + 14, p_start_x + (p + 1) * 58 - 4, my + 42};
            GuiRenderer::draw_button(hdc, p_rc, p_names[p], false, gdi_accent(), RGB(40, 42, 38));
        }

        // PANIC
        RECT panic_rc{mx + mw - 116, my + 14, mx + mw - 52, my + 42};
        GuiRenderer::draw_button(hdc, panic_rc, "PANIC", false, gdi_accent(), RGB(64, 34, 32));

        // Close
        RECT close_rc{mx + mw - 46, my + 14, mx + mw - 18, my + 42};
        GuiRenderer::draw_button(hdc, close_rc, "✕", false, RGB(255, 114, 107), RGB(36, 38, 34));

        // Grid & Mixer dimensions
        int mixer_x = mx + mw - 195;
        int mixer_y = my + 94;
        int mixer_w = 180;
        int mixer_h = mh - 198;

        float grid_x = static_cast<float>(mx + 20);
        float grid_y = static_cast<float>(my + 94);
        float grid_w = static_cast<float>(mixer_x - (mx + 20) - 12);
        float grid_h = static_cast<float>(mixer_h);
        float top_h = (grid_h - 10.0f) * 0.64f;
        float flt_y = grid_y + top_h + 10.0f;
        float flt_h = grid_h - top_h - 10.0f;
        float osc_total_w = (grid_w - 10.0f) * 0.64f;
        float amp_x = grid_x + osc_total_w + 10.0f;
        float amp_w = grid_w - osc_total_w - 10.0f;
        float osc_col_w = (osc_total_w - 8.0f) * 0.5f;
        float osc_row_h = (top_h - 8.0f) * 0.5f;
        float flt_col_w = (grid_w - 10.0f) * 0.5f;

        float mod_col_w = (grid_w - 10.0f) * 0.5f;
        float mod_row_h = (grid_h - 8.0f) * 0.5f;

        // Legacy col_w / row_h for Tab 2
        float col_w = (grid_w - 10.0f) * 0.5f;
        float row_h = (grid_h - 12.0f) / 3.0f;

        // Tabs
        const char* tab_names[3] = {"OSCILLATORS + FILTERS", "MOD ENVELOPES + ROUTING", "EFFECTS + PERFORMANCE"};
        float tab_w = (grid_w - 16.0f) / 3.0f;
        for (int t = 0; t < 3; ++t) {
            int tx = static_cast<int>(grid_x + static_cast<float>(t) * (tab_w + 8.0f));
            int tw = static_cast<int>(tab_w);
            RECT t_rc{tx, my + 58, tx + tw, my + 88};
            bool is_active = (active_tab == t);
            GuiRenderer::draw_rounded_box(hdc, t_rc, is_active ? gdi_panel() : RGB(32, 34, 31),
                                          is_active ? gdi_accent() : RGB(50, 52, 48), 3);
            GuiRenderer::draw_text(hdc, tab_names[t], t_rc, is_active ? gdi_accent() : gdi_muted(), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        // Output Mixer Panel
        RECT mixer_rc{mixer_x, mixer_y, mixer_x + mixer_w, mixer_y + mixer_h};
        draw_module_panel_gdi(hdc, font_bold, font_small, mixer_rc, "OUTPUT MIXER", "Post FX / soft ceiling");

        bool m_on = synth->get_param_by_id("master_on") > 0.5f;
        RECT m_pwr_rc{mixer_x + 20, mixer_y + 48, mixer_x + 50, mixer_y + 66};
        draw_toggle_led_gdi(hdc, m_pwr_rc, m_on);
        RECT m_lbl_rc{mixer_x + 56, mixer_y + 48, mixer_x + mixer_w - 20, mixer_y + 66};
        GuiRenderer::draw_text(hdc, m_on ? "OUTPUT ON" : "MUTED", m_lbl_rc, m_on ? gdi_accent() : gdi_muted(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        float m_gain = synth->get_param_by_id("master_gain");
        int m_gain_idx = xosc::index("master_gain");
        float norm_gain = synth->get_parameter(static_cast<uint32_t>(m_gain_idx));
        RECT mg_rc{mixer_x + 15, mixer_y + 86, mixer_x + mixer_w - 15, mixer_y + 180};
        draw_knob_gdi(hdc, font_small, mg_rc, norm_gain, "GAIN", format_param_val(xosc::specs()[m_gain_idx], m_gain));

        float m_width = synth->get_param_by_id("master_width");
        int m_width_idx = xosc::index("master_width");
        float norm_width = synth->get_parameter(static_cast<uint32_t>(m_width_idx));
        RECT mw_rc{mixer_x + 15, mixer_y + 190, mixer_x + mixer_w - 15, mixer_y + 284};
        draw_knob_gdi(hdc, font_small, mw_rc, norm_width, "WIDTH", format_param_val(xosc::specs()[m_width_idx], m_width));

        SelectObject(hdc, font_small);
        RECT poly_lbl{mixer_x + 15, mixer_y + 292, mixer_x + mixer_w - 15, mixer_y + 310};
        GuiRenderer::draw_text(hdc, "POLYPHONY", poly_lbl, gdi_ink(), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        int poly_idx = static_cast<int>(std::round(synth->get_param_by_id("polyphony")));
        const char* poly_names[4] = {"4", "8", "16", "32"};
        for (int p = 0; p < 4; ++p) {
            int px = mixer_x + 16 + p * 37;
            RECT prc{px, mixer_y + 314, px + 33, mixer_y + 338};
            GuiRenderer::draw_button(hdc, prc, poly_names[p], (p == poly_idx), gdi_accent(), RGB(32, 34, 31));
        }

        // Module panel rendering helpers for GDI
        auto render_osc_gdi = [&](int osc_idx, float px, float py, float pw, float ph) {
            std::string o_prefix = "o" + std::to_string(osc_idx) + "_";
            RECT prc{static_cast<int>(px), static_cast<int>(py), static_cast<int>(px + pw), static_cast<int>(py + ph)};
            draw_module_panel_gdi(hdc, font_bold, font_small, prc,
                                  "OSC " + std::to_string(osc_idx + 1), "PolyBLEP / 1-12 voices");

            bool on = synth->get_param_by_id(o_prefix + "on") > 0.5f;
            RECT pwr_rc{static_cast<int>(px + 6.0f), static_cast<int>(py + 42.0f), static_cast<int>(px + 34.0f), static_cast<int>(py + 60.0f)};
            draw_toggle_led_gdi(hdc, pwr_rc, on);

            int wave_idx = static_cast<int>(std::round(synth->get_param_by_id(o_prefix + "wave")));
            for (int w = 0; w < 3; ++w) {
                float wx = px + 38.0f + static_cast<float>(w) * 24.0f;
                RECT wrc{static_cast<int>(wx), static_cast<int>(py + 40.0f), static_cast<int>(wx + 21.0f), static_cast<int>(py + 62.0f)};
                draw_wave_icon_gdi(hdc, wrc, w, (w == wave_idx));
            }

            int voices = static_cast<int>(std::round(synth->get_param_by_id(o_prefix + "voices"))) + 1;
            RECT uni_down{static_cast<int>(px + 112.0f), static_cast<int>(py + 40.0f), static_cast<int>(px + 128.0f), static_cast<int>(py + 62.0f)};
            RECT uni_txt{static_cast<int>(px + 130.0f), static_cast<int>(py + 40.0f), static_cast<int>(px + 158.0f), static_cast<int>(py + 62.0f)};
            RECT uni_up{static_cast<int>(px + 160.0f), static_cast<int>(py + 40.0f), static_cast<int>(px + 176.0f), static_cast<int>(py + 62.0f)};
            GuiRenderer::draw_button(hdc, uni_down, "-", false, gdi_accent(), RGB(32, 34, 31));
            GuiRenderer::draw_rounded_box(hdc, uni_txt, RGB(32, 34, 31), RGB(53, 54, 50), 2);
            GuiRenderer::draw_text(hdc, "U " + std::to_string(voices), uni_txt, gdi_ink(), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            GuiRenderer::draw_button(hdc, uni_up, "+", false, gdi_accent(), RGB(32, 34, 31));

            float tune_x = px + 180.0f;
            float rem_w = std::max(40.0f, (px + pw - 4.0f) - tune_x);
            float half_w = rem_w * 0.5f;

            int tune_idx = xosc::index(o_prefix + "tune");
            float tune_val = synth->get_param_by_id(o_prefix + "tune");
            RECT tune_rc{static_cast<int>(tune_x), static_cast<int>(py + 36.0f), static_cast<int>(tune_x + half_w - 2.0f), static_cast<int>(py + 92.0f)};
            draw_knob_gdi(hdc, font_small, tune_rc, synth->get_parameter(static_cast<uint32_t>(tune_idx)),
                          "TUNE", format_param_val(xosc::specs()[tune_idx], tune_val), !on);

            int phase_idx = xosc::index(o_prefix + "phase");
            float phase_val = synth->get_param_by_id(o_prefix + "phase");
            RECT phase_rc{static_cast<int>(tune_x + half_w + 2.0f), static_cast<int>(py + 36.0f), static_cast<int>(px + pw - 4.0f), static_cast<int>(py + 92.0f)};
            draw_knob_gdi(hdc, font_small, phase_rc, synth->get_parameter(static_cast<uint32_t>(phase_idx)),
                          "PHASE", format_param_val(xosc::specs()[phase_idx], phase_val), !on);

            float k_w = (pw - 12.0f) * 0.25f;
            const char* osc_knob_ids[4] = {"vol", "pan", "detune", "stereo"};
            const char* osc_knob_titles[4] = {"VOL", "PAN", "DETUNE", "STEREO"};
            for (int k = 0; k < 4; ++k) {
                int kidx = xosc::index(o_prefix + osc_knob_ids[k]);
                float kval = synth->get_param_by_id(o_prefix + osc_knob_ids[k]);
                float kx = px + 6.0f + static_cast<float>(k) * k_w;
                RECT k_rc{static_cast<int>(kx), static_cast<int>(py + 94.0f), static_cast<int>(kx + k_w), static_cast<int>(py + ph - 4.0f)};
                draw_knob_gdi(hdc, font_small, k_rc, synth->get_parameter(static_cast<uint32_t>(kidx)),
                              osc_knob_titles[k], format_param_val(xosc::specs()[kidx], kval), !on);
            }
        };

        auto render_amp_env_gdi = [&](int amp_idx, float px, float py, float pw, float ph) {
            std::string a_prefix = "a" + std::to_string(amp_idx) + "_";
            RECT prc{static_cast<int>(px), static_cast<int>(py), static_cast<int>(px + pw), static_cast<int>(py + ph)};
            draw_module_panel_gdi(hdc, font_bold, font_small, prc,
                                  "AMP ENV " + std::to_string(amp_idx + 1),
                                  "After filters / multiple envelopes multiply");

            bool on = synth->get_param_by_id(a_prefix + "on") > 0.5f;
            RECT pwr_rc{static_cast<int>(px + 8.0f), static_cast<int>(py + 42.0f), static_cast<int>(px + 36.0f), static_cast<int>(py + 60.0f)};
            draw_toggle_led_gdi(hdc, pwr_rc, on);

            float r_start = px + 44.0f;
            for (int r = 0; r < 4; ++r) {
                bool routed = synth->get_param_by_id(a_prefix + "route" + std::to_string(r)) > 0.5f;
                float rx = r_start + static_cast<float>(r) * 34.0f;
                RECT r_rc{static_cast<int>(rx), static_cast<int>(py + 40.0f), static_cast<int>(rx + 30.0f), static_cast<int>(py + 62.0f)};
                GuiRenderer::draw_button(hdc, r_rc, "O" + std::to_string(r + 1), routed, gdi_accent(), RGB(32, 34, 31));
            }

            float k_w = (pw - 16.0f) * 0.25f;
            const char* env_knobs[4] = {"attack", "decay", "sustain", "release"};
            const char* env_titles[4] = {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"};
            for (int k = 0; k < 4; ++k) {
                int kidx = xosc::index(a_prefix + env_knobs[k]);
                float kval = synth->get_param_by_id(a_prefix + env_knobs[k]);
                float kx = px + 8.0f + static_cast<float>(k) * k_w;
                RECT k_rc{static_cast<int>(kx), static_cast<int>(py + 68.0f), static_cast<int>(kx + k_w), static_cast<int>(py + ph - 4.0f)};
                draw_knob_gdi(hdc, font_small, k_rc, synth->get_parameter(static_cast<uint32_t>(kidx)),
                              env_titles[k], format_param_val(xosc::specs()[kidx], kval), !on);
            }
        };

        auto render_filter_gdi = [&](int flt_idx, float px, float py, float pw, float ph) {
            std::string f_prefix = "f" + std::to_string(flt_idx) + "_";
            RECT prc{static_cast<int>(px), static_cast<int>(py), static_cast<int>(px + pw), static_cast<int>(py + ph)};
            draw_module_panel_gdi(hdc, font_bold, font_small, prc,
                                  "FILTER " + std::to_string(flt_idx + 1),
                                  flt_idx == 0 ? "Per oscillator / before Filter 2" : "Per oscillator / after Filter 1");

            bool on = synth->get_param_by_id(f_prefix + "on") > 0.5f;
            RECT pwr_rc{static_cast<int>(px + 8.0f), static_cast<int>(py + 42.0f), static_cast<int>(px + 36.0f), static_cast<int>(py + 60.0f)};
            draw_toggle_led_gdi(hdc, pwr_rc, on);

            int type = static_cast<int>(std::round(synth->get_param_by_id(f_prefix + "type")));
            for (int t = 0; t < 4; ++t) {
                float tx = px + 44.0f + static_cast<float>(t) * 34.0f;
                RECT trc{static_cast<int>(tx), static_cast<int>(py + 40.0f), static_cast<int>(tx + 30.0f), static_cast<int>(py + 62.0f)};
                draw_filter_curve_gdi(hdc, trc, t, (t == type));
            }

            float r_start = px + 186.0f;
            for (int r = 0; r < 4; ++r) {
                bool routed = synth->get_param_by_id(f_prefix + "route" + std::to_string(r)) > 0.5f;
                float rx = r_start + static_cast<float>(r) * 34.0f;
                RECT r_rc{static_cast<int>(rx), static_cast<int>(py + 40.0f), static_cast<int>(rx + 30.0f), static_cast<int>(py + 62.0f)};
                GuiRenderer::draw_button(hdc, r_rc, "O" + std::to_string(r + 1), routed, gdi_accent(), RGB(32, 34, 31));
            }

            float k_w = (pw - 16.0f) / 3.0f;
            const char* flt_knobs[3] = {"cutoff", "res", "drive"};
            const char* flt_titles[3] = {"CUTOFF", "RESONANCE", "DRIVE"};
            for (int k = 0; k < 3; ++k) {
                int kidx = xosc::index(f_prefix + flt_knobs[k]);
                float kval = synth->get_param_by_id(f_prefix + flt_knobs[k]);
                float kx = px + 8.0f + static_cast<float>(k) * k_w;
                RECT k_rc{static_cast<int>(kx), static_cast<int>(py + 68.0f), static_cast<int>(kx + k_w), static_cast<int>(py + ph - 4.0f)};
                draw_knob_gdi(hdc, font_small, k_rc, synth->get_parameter(static_cast<uint32_t>(kidx)),
                              flt_titles[k], format_param_val(xosc::specs()[kidx], kval), !on);
            }
        };

        auto render_mod_env_gdi = [&](int env_idx, float px, float py, float pw, float ph) {
            std::string e_prefix = "e" + std::to_string(env_idx) + "_";
            RECT prc{static_cast<int>(px), static_cast<int>(py), static_cast<int>(px + pw), static_cast<int>(py + ph)};
            draw_module_panel_gdi(hdc, font_bold, font_small, prc,
                                  "MOD ENV " + std::to_string(env_idx + 1),
                                  "Target dropdown + mix / select destinations");

            bool on = synth->get_param_by_id(e_prefix + "on") > 0.5f;
            RECT pwr_rc{static_cast<int>(px + 8.0f), static_cast<int>(py + 42.0f), static_cast<int>(px + 36.0f), static_cast<int>(py + 60.0f)};
            draw_toggle_led_gdi(hdc, pwr_rc, on);

            int target = std::clamp(static_cast<int>(std::round(synth->get_param_by_id(e_prefix + "target"))), 0, 8);
            const char* tgt_names[9] = {
                "Osc Volume", "Osc Pitch", "Cutoff", "Flt 1 Cutoff",
                "Flt 1 Res", "Flt 2 Cutoff", "Flt 2 Res", "Filter Drive", "Osc Pan"
            };
            RECT dd_rc{static_cast<int>(px + 42.0f), static_cast<int>(py + 40.0f), static_cast<int>(px + 175.0f), static_cast<int>(py + 62.0f)};
            std::string dd_label = std::string(tgt_names[target]) + "  ▼";
            GuiRenderer::draw_button(hdc, dd_rc, dd_label, on, gdi_accent(), RGB(32, 34, 31));

            float r_start = px + 182.0f;
            for (int r = 0; r < 4; ++r) {
                bool routed = synth->get_param_by_id(e_prefix + "route" + std::to_string(r)) > 0.5f;
                float rx = r_start + static_cast<float>(r) * 32.0f;
                RECT r_rc{static_cast<int>(rx), static_cast<int>(py + 40.0f), static_cast<int>(rx + 28.0f), static_cast<int>(py + 62.0f)};
                GuiRenderer::draw_button(hdc, r_rc, "O" + std::to_string(r + 1), routed, gdi_accent(), RGB(32, 34, 31));
            }

            int mix_idx = xosc::index(e_prefix + "mix");
            float mix_val = synth->get_param_by_id(e_prefix + "mix");
            RECT mix_rc{static_cast<int>(px + 316.0f), static_cast<int>(py + 36.0f), static_cast<int>(px + pw - 8.0f), static_cast<int>(py + 92.0f)};
            draw_knob_gdi(hdc, font_small, mix_rc, synth->get_parameter(static_cast<uint32_t>(mix_idx)),
                          "MIX", format_param_val(xosc::specs()[mix_idx], mix_val), !on);

            float k_w = (pw - 16.0f) * 0.25f;
            const char* env_knobs[4] = {"attack", "decay", "sustain", "release"};
            const char* env_titles[4] = {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"};
            for (int k = 0; k < 4; ++k) {
                int kidx = xosc::index(e_prefix + env_knobs[k]);
                float kval = synth->get_param_by_id(e_prefix + env_knobs[k]);
                float kx = px + 8.0f + static_cast<float>(k) * k_w;
                RECT k_rc{static_cast<int>(kx), static_cast<int>(py + 96.0f), static_cast<int>(kx + k_w), static_cast<int>(py + ph - 6.0f)};
                draw_knob_gdi(hdc, font_small, k_rc, synth->get_parameter(static_cast<uint32_t>(kidx)),
                              env_titles[k], format_param_val(xosc::specs()[kidx], kval), !on);
            }
        };

        if (active_tab == 0) {
            render_osc_gdi(0, grid_x, grid_y, osc_col_w, osc_row_h);
            render_osc_gdi(1, grid_x + osc_col_w + 8.0f, grid_y, osc_col_w, osc_row_h);
            render_osc_gdi(2, grid_x, grid_y + osc_row_h + 8.0f, osc_col_w, osc_row_h);
            render_osc_gdi(3, grid_x + osc_col_w + 8.0f, grid_y + osc_row_h + 8.0f, osc_col_w, osc_row_h);

            render_amp_env_gdi(0, amp_x, grid_y, amp_w, osc_row_h);
            render_amp_env_gdi(1, amp_x, grid_y + osc_row_h + 8.0f, amp_w, osc_row_h);

            render_filter_gdi(0, grid_x, flt_y, flt_col_w, flt_h);
            render_filter_gdi(1, grid_x + flt_col_w + 10.0f, flt_y, flt_col_w, flt_h);
        } else if (active_tab == 1) {
            render_mod_env_gdi(0, grid_x, grid_y, mod_col_w, mod_row_h);
            render_mod_env_gdi(1, grid_x + mod_col_w + 10.0f, grid_y, mod_col_w, mod_row_h);
            render_mod_env_gdi(2, grid_x, grid_y + mod_row_h + 8.0f, mod_col_w, mod_row_h);
            render_mod_env_gdi(3, grid_x + mod_col_w + 10.0f, grid_y + mod_row_h + 8.0f, mod_col_w, mod_row_h);
        } else {
            // Tab 2: EFFECTS + PERFORMANCE
            // Panel 1: DISTORTION
            {
                float px = grid_x;
                float py = grid_y;
                RECT prc{static_cast<int>(px), static_cast<int>(py), static_cast<int>(px + col_w), static_cast<int>(py + row_h)};
                draw_module_panel_gdi(hdc, font_bold, font_small, prc, "01 / DISTORTION", "Warm saturation / parallel mix");
                bool on = synth->get_param_by_id("dist_on") > 0.5f;
                RECT pwr_rc{static_cast<int>(px + 8.0f), static_cast<int>(py + 42.0f), static_cast<int>(px + 36.0f), static_cast<int>(py + 60.0f)};
                draw_toggle_led_gdi(hdc, pwr_rc, on);

                int d_idx = xosc::index("dist_drive");
                int m_idx = xosc::index("dist_mix");
                RECT d_rc{static_cast<int>(px + 60.0f), static_cast<int>(py + 70.0f), static_cast<int>(px + 210.0f), static_cast<int>(py + row_h - 4.0f)};
                RECT m_rc{static_cast<int>(px + 220.0f), static_cast<int>(py + 70.0f), static_cast<int>(px + col_w - 20.0f), static_cast<int>(py + row_h - 4.0f)};
                draw_knob_gdi(hdc, font_small, d_rc, synth->get_parameter(static_cast<uint32_t>(d_idx)),
                              "DRIVE", format_param_val(xosc::specs()[d_idx], synth->get_param_by_id("dist_drive")), !on);
                draw_knob_gdi(hdc, font_small, m_rc, synth->get_parameter(static_cast<uint32_t>(m_idx)),
                              "MIX", format_param_val(xosc::specs()[m_idx], synth->get_param_by_id("dist_mix")), !on);
            }

            // Panel 2: EQ
            {
                float px = grid_x + col_w + 10.0f;
                float py = grid_y;
                RECT prc{static_cast<int>(px), static_cast<int>(py), static_cast<int>(px + col_w), static_cast<int>(py + row_h)};
                draw_module_panel_gdi(hdc, font_bold, font_small, prc, "02 / EQ", "Three broad tone bands");
                bool on = synth->get_param_by_id("eq_on") > 0.5f;
                RECT pwr_rc{static_cast<int>(px + 8.0f), static_cast<int>(py + 42.0f), static_cast<int>(px + 36.0f), static_cast<int>(py + 60.0f)};
                draw_toggle_led_gdi(hdc, pwr_rc, on);

                float kw = (col_w - 20.0f) / 3.0f;
                const char* eq_ids[3] = {"eq_low", "eq_mid", "eq_high"};
                const char* eq_titles[3] = {"LOW 180Hz", "MID 1kHz", "HIGH 5kHz"};
                for (int k = 0; k < 3; ++k) {
                    int kidx = xosc::index(eq_ids[k]);
                    float kx = px + 10.0f + static_cast<float>(k) * kw;
                    RECT k_rc{static_cast<int>(kx), static_cast<int>(py + 70.0f), static_cast<int>(kx + kw), static_cast<int>(py + row_h - 4.0f)};
                    draw_knob_gdi(hdc, font_small, k_rc, synth->get_parameter(static_cast<uint32_t>(kidx)),
                                  eq_titles[k], format_param_val(xosc::specs()[kidx], synth->get_param_by_id(eq_ids[k])), !on);
                }
            }

            // Panel 3: COMPRESSOR
            {
                float px = grid_x;
                float py = grid_y + row_h + 6.0f;
                RECT prc{static_cast<int>(px), static_cast<int>(py), static_cast<int>(px + col_w), static_cast<int>(py + row_h)};
                draw_module_panel_gdi(hdc, font_bold, font_small, prc, "03 / COMPRESSOR", "Stereo linked / peak detection");
                bool on = synth->get_param_by_id("comp_on") > 0.5f;
                RECT pwr_rc{static_cast<int>(px + 8.0f), static_cast<int>(py + 42.0f), static_cast<int>(px + 36.0f), static_cast<int>(py + 60.0f)};
                draw_toggle_led_gdi(hdc, pwr_rc, on);

                float kw = (col_w - 20.0f) / 5.0f;
                const char* comp_ids[5] = {"comp_threshold", "comp_ratio", "comp_attack", "comp_release", "comp_makeup"};
                const char* comp_titles[5] = {"THRESH", "RATIO", "ATTACK", "RELEASE", "MAKEUP"};
                for (int k = 0; k < 5; ++k) {
                    int kidx = xosc::index(comp_ids[k]);
                    float kx = px + 10.0f + static_cast<float>(k) * kw;
                    RECT k_rc{static_cast<int>(kx), static_cast<int>(py + 70.0f), static_cast<int>(kx + kw), static_cast<int>(py + row_h - 4.0f)};
                    draw_knob_gdi(hdc, font_small, k_rc, synth->get_parameter(static_cast<uint32_t>(kidx)),
                                  comp_titles[k], format_param_val(xosc::specs()[kidx], synth->get_param_by_id(comp_ids[k])), !on);
                }
            }

            // Panel 4: DELAY
            {
                float px = grid_x + col_w + 10.0f;
                float py = grid_y + row_h + 6.0f;
                RECT prc{static_cast<int>(px), static_cast<int>(py), static_cast<int>(px + col_w), static_cast<int>(py + row_h)};
                draw_module_panel_gdi(hdc, font_bold, font_small, prc, "04 / DELAY", "Stereo / time in seconds");
                bool on = synth->get_param_by_id("delay_on") > 0.5f;
                RECT pwr_rc{static_cast<int>(px + 8.0f), static_cast<int>(py + 42.0f), static_cast<int>(px + 36.0f), static_cast<int>(py + 60.0f)};
                draw_toggle_led_gdi(hdc, pwr_rc, on);

                float kw = (col_w - 20.0f) / 3.0f;
                const char* dly_ids[3] = {"delay_time", "delay_feedback", "delay_mix"};
                const char* dly_titles[3] = {"TIME", "FEEDBACK", "MIX"};
                for (int k = 0; k < 3; ++k) {
                    int kidx = xosc::index(dly_ids[k]);
                    float kx = px + 10.0f + static_cast<float>(k) * kw;
                    RECT k_rc{static_cast<int>(kx), static_cast<int>(py + 70.0f), static_cast<int>(kx + kw), static_cast<int>(py + row_h - 4.0f)};
                    draw_knob_gdi(hdc, font_small, k_rc, synth->get_parameter(static_cast<uint32_t>(kidx)),
                                  dly_titles[k], format_param_val(xosc::specs()[kidx], synth->get_param_by_id(dly_ids[k])), !on);
                }
            }

            // Panel 5: REVERB
            {
                float px = grid_x;
                float py = grid_y + (row_h + 6.0f) * 2.0f;
                RECT prc{static_cast<int>(px), static_cast<int>(py), static_cast<int>(px + col_w), static_cast<int>(py + row_h)};
                draw_module_panel_gdi(hdc, font_bold, font_small, prc, "05 / REVERB", "Stereo room / damped decay");
                bool on = synth->get_param_by_id("reverb_on") > 0.5f;
                RECT pwr_rc{static_cast<int>(px + 8.0f), static_cast<int>(py + 42.0f), static_cast<int>(px + 36.0f), static_cast<int>(py + 60.0f)};
                draw_toggle_led_gdi(hdc, pwr_rc, on);

                float kw = (col_w - 20.0f) / 3.0f;
                const char* rev_ids[3] = {"reverb_size", "reverb_damp", "reverb_mix"};
                const char* rev_titles[3] = {"SIZE", "DAMP", "MIX"};
                for (int k = 0; k < 3; ++k) {
                    int kidx = xosc::index(rev_ids[k]);
                    float kx = px + 10.0f + static_cast<float>(k) * kw;
                    RECT k_rc{static_cast<int>(kx), static_cast<int>(py + 70.0f), static_cast<int>(kx + kw), static_cast<int>(py + row_h - 4.0f)};
                    draw_knob_gdi(hdc, font_small, k_rc, synth->get_parameter(static_cast<uint32_t>(kidx)),
                                  rev_titles[k], format_param_val(xosc::specs()[kidx], synth->get_param_by_id(rev_ids[k])), !on);
                }
            }

            // Panel 6: PERFORMANCE
            {
                float px = grid_x + col_w + 10.0f;
                float py = grid_y + (row_h + 6.0f) * 2.0f;
                RECT prc{static_cast<int>(px), static_cast<int>(py), static_cast<int>(px + col_w), static_cast<int>(py + row_h)};
                draw_module_panel_gdi(hdc, font_bold, font_small, prc, "06 / PERFORMANCE", "N: every transition / S: overlapping keys");

                bool mono = synth->get_param_by_id("mono_legato") > 0.5f;
                RECT mono_rc{static_cast<int>(px + 14.0f), static_cast<int>(py + 46.0f), static_cast<int>(px + 42.0f), static_cast<int>(py + 64.0f)};
                draw_toggle_led_gdi(hdc, mono_rc, mono);
                RECT mono_lbl{static_cast<int>(px + 46.0f), static_cast<int>(py + 44.0f), static_cast<int>(px + 130.0f), static_cast<int>(py + 66.0f)};
                GuiRenderer::draw_text(hdc, "MONO LEGATO", mono_lbl, mono ? gdi_accent() : gdi_muted(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                bool glide = synth->get_param_by_id("glide_on") > 0.5f;
                RECT glide_rc{static_cast<int>(px + 136.0f), static_cast<int>(py + 46.0f), static_cast<int>(px + 164.0f), static_cast<int>(py + 64.0f)};
                draw_toggle_led_gdi(hdc, glide_rc, glide);
                RECT glide_lbl{static_cast<int>(px + 168.0f), static_cast<int>(py + 44.0f), static_cast<int>(px + 250.0f), static_cast<int>(py + 66.0f)};
                GuiRenderer::draw_text(hdc, "PORTAMENTO", glide_lbl, glide ? gdi_accent() : gdi_muted(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                int gmode = static_cast<int>(std::round(synth->get_param_by_id("glide_mode")));
                RECT gm_n{static_cast<int>(px + 14.0f), static_cast<int>(py + 86.0f), static_cast<int>(px + 126.0f), static_cast<int>(py + 112.0f)};
                RECT gm_s{static_cast<int>(px + 14.0f), static_cast<int>(py + 116.0f), static_cast<int>(px + 126.0f), static_cast<int>(py + 142.0f)};
                GuiRenderer::draw_button(hdc, gm_n, "N / NORMAL", (gmode == 0), gdi_accent(), RGB(32, 34, 31));
                GuiRenderer::draw_button(hdc, gm_s, "S / SLIDE", (gmode == 1), gdi_accent(), RGB(32, 34, 31));

                int gt_idx = xosc::index("glide_time");
                RECT gt_rc{static_cast<int>(px + 150.0f), static_cast<int>(py + 76.0f), static_cast<int>(px + col_w - 20.0f), static_cast<int>(py + row_h - 4.0f)};
                draw_knob_gdi(hdc, font_small, gt_rc, synth->get_parameter(static_cast<uint32_t>(gt_idx)),
                              "GLIDE TIME", format_param_val(xosc::specs()[gt_idx], synth->get_param_by_id("glide_time")), !glide);
            }
        }

        // Status & Keyboard
        int bot_y = my + mh - 96;
        std::string status_txt = "MIDI  /  " + std::to_string(synth->active_voices()) +
                                 " active notes     |     Pitch wheel +/-2 st     |     Sustain CC64     |     Init: OSC 1 > AMP 1 > Output";
        RECT st_rc{mx + 20, bot_y, mx + mw - 20, bot_y + 18};
        GuiRenderer::draw_text(hdc, status_txt, st_rc, gdi_muted(), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Keyboard
        int kb_y = bot_y + 20;
        int kb_h = mh - (kb_y - my) - 8;
        int kb_x = mx + 20;
        int kb_w = mw - 40;

        const int kStartKey = 36;
        const int kEndKey = 84;
        auto is_black_key = [](int note) {
            int semi = note % 12;
            return semi == 1 || semi == 3 || semi == 6 || semi == 8 || semi == 10;
        };

        int white_count = 0;
        for (int k = kStartKey; k <= kEndKey; ++k) {
            if (!is_black_key(k)) ++white_count;
        }

        float wk_w = static_cast<float>(kb_w) / static_cast<float>(white_count);
        float bk_w = wk_w * 0.62f;
        float bk_h = static_cast<float>(kb_h) * 0.60f;

        // White keys
        float cur_wx = static_cast<float>(kb_x);
        for (int k = kStartKey; k <= kEndKey; ++k) {
            if (!is_black_key(k)) {
                RECT wk_rc{static_cast<int>(cur_wx), kb_y, static_cast<int>(cur_wx + wk_w - 1.0f), kb_y + kb_h};
                GuiRenderer::draw_rounded_box(hdc, wk_rc, gdi_ink(), RGB(40, 38, 34), 2);
                cur_wx += wk_w;
            }
        }

        // Black keys
        cur_wx = static_cast<float>(kb_x);
        for (int k = kStartKey; k <= kEndKey; ++k) {
            if (is_black_key(k)) {
                float bk_x = cur_wx - (bk_w * 0.5f);
                RECT bk_rc{static_cast<int>(bk_x), kb_y, static_cast<int>(bk_x + bk_w), static_cast<int>(static_cast<float>(kb_y) + bk_h)};
                GuiRenderer::draw_rounded_box(hdc, bk_rc, RGB(23, 24, 21), RGB(10, 10, 9), 2);
            } else {
                cur_wx += wk_w;
            }
        }
    }

    // ========================================================================
    // Interaction Handlers (Click, Drag, Wheel)
    // ========================================================================
    static bool handle_click(HWND hwnd, plugins::XOSCDevice* synth, const RECT& bounds,
                             int x, int y, int& active_tab, int& dragging_param_idx,
                             int& drag_start_y, float& drag_orig_val, std::string& status_msg,
                             std::function<void(uint8_t)> audition_cb) {
        if (!synth) return false;
        int mx = bounds.left;
        int my = bounds.top;
        int mw = bounds.right - bounds.left;
        int mh = bounds.bottom - bounds.top;

        // Close button
        if (x >= mx + mw - 46 && x <= mx + mw - 18 && y >= my + 14 && y <= my + 42) {
            status_msg = "Closed XOSC Editor";
            return true; // request close
        }

        // PANIC button
        if (x >= mx + mw - 116 && x <= mx + mw - 52 && y >= my + 14 && y <= my + 42) {
            synth->panic();
            status_msg = "XOSC: PANIC - Released all voices and pedals";
            InvalidateRect(hwnd, &bounds, FALSE);
            return false;
        }

        // Factory Presets [Init] [Thick] [Bass] [Pad]
        int p_start_x = mx + mw - 355;
        for (int p = 0; p < 4; ++p) {
            if (x >= p_start_x + p * 58 && x <= p_start_x + (p + 1) * 58 - 4 && y >= my + 14 && y <= my + 42) {
                synth->load_factory(p);
                const char* p_names[4] = {"Init / one oscillator", "Thick / stereo stack", "Bass / punch", "Pad / slow bloom"};
                status_msg = std::string("XOSC: Loaded preset ") + p_names[p];
                InvalidateRect(hwnd, &bounds, FALSE);
                return false;
            }
        }

        // Dimensions
        int mixer_x = mx + mw - 195;
        int mixer_y = my + 94;
        int mixer_w = 180;
        int mixer_h = mh - 198;

        float grid_x = static_cast<float>(mx + 20);
        float grid_y = static_cast<float>(my + 94);
        float grid_w = static_cast<float>(mixer_x - (mx + 20) - 12);
        float grid_h = static_cast<float>(mixer_h);
        float col_w = (grid_w - 10.0f) * 0.5f;
        float row_h = (grid_h - 12.0f) / 3.0f;

        // Tabs Bar
        float tab_w = (grid_w - 16.0f) / 3.0f;
        for (int t = 0; t < 3; ++t) {
            float tx = grid_x + static_cast<float>(t) * (tab_w + 8.0f);
            if (static_cast<float>(x) >= tx && static_cast<float>(x) <= tx + tab_w && y >= my + 58 && y <= my + 88) {
                active_tab = t;
                InvalidateRect(hwnd, &bounds, FALSE);
                return false;
            }
        }

        // Output Mixer Controls
        // Output power toggle
        if (x >= mixer_x + 20 && x <= mixer_x + mixer_w - 20 && y >= mixer_y + 48 && y <= mixer_y + 74) {
            float cur = synth->get_param_by_id("master_on");
            synth->set_param_by_id("master_on", cur > 0.5f ? 0.0f : 1.0f);
            status_msg = "XOSC Output: " + std::string(cur > 0.5f ? "Muted" : "Active");
            InvalidateRect(hwnd, &bounds, FALSE);
            return false;
        }

        // Master gain knob
        if (x >= mixer_x + 15 && x <= mixer_x + mixer_w - 15 && y >= mixer_y + 86 && y <= mixer_y + 180) {
            int idx = xosc::index("master_gain");
            dragging_param_idx = idx;
            drag_start_y = y;
            drag_orig_val = synth->get_parameter(static_cast<uint32_t>(idx));
            status_msg = "XOSC Master Gain: " + format_param_val(xosc::specs()[idx], synth->get_param_by_id("master_gain"));
            return false;
        }

        // Master width knob
        if (x >= mixer_x + 15 && x <= mixer_x + mixer_w - 15 && y >= mixer_y + 190 && y <= mixer_y + 284) {
            int idx = xosc::index("master_width");
            dragging_param_idx = idx;
            drag_start_y = y;
            drag_orig_val = synth->get_parameter(static_cast<uint32_t>(idx));
            status_msg = "XOSC Master Width: " + format_param_val(xosc::specs()[idx], synth->get_param_by_id("master_width"));
            return false;
        }

        // Polyphony Choices
        for (int p = 0; p < 4; ++p) {
            int px = mixer_x + 16 + p * 37;
            if (x >= px && x <= px + 33 && y >= mixer_y + 314 && y <= mixer_y + 338) {
                synth->set_param_by_id("polyphony", static_cast<float>(p));
                const char* poly_names[4] = {"4", "8", "16", "32"};
                status_msg = "XOSC Polyphony: " + std::string(poly_names[p]) + " voices";
                InvalidateRect(hwnd, &bounds, FALSE);
                return false;
            }
        }

        // Grid Area Interaction
        float fx = static_cast<float>(x);
        float fy = static_cast<float>(y);
        float top_h = (grid_h - 10.0f) * 0.64f;
        float flt_y = grid_y + top_h + 10.0f;
        float flt_h = grid_h - top_h - 10.0f;
        float osc_total_w = (grid_w - 10.0f) * 0.64f;
        float amp_x = grid_x + osc_total_w + 10.0f;
        float amp_w = grid_w - osc_total_w - 10.0f;
        float osc_col_w = (osc_total_w - 8.0f) * 0.5f;
        float osc_row_h = (top_h - 8.0f) * 0.5f;
        float flt_col_w = (grid_w - 10.0f) * 0.5f;
        float mod_col_w = (grid_w - 10.0f) * 0.5f;
        float mod_row_h = (grid_h - 8.0f) * 0.5f;

        if (active_tab == 0) {
            // Tab 0: 2x2 Oscillators (top left), 2 Amp Envelopes (top right), 2 Filters (bottom)
            if (fx >= grid_x && fx <= grid_x + osc_total_w && fy >= grid_y && fy <= grid_y + top_h) {
                // 4 Oscillators
                int c = (fx < grid_x + osc_col_w + 4.0f) ? 0 : 1;
                int r = (fy < grid_y + osc_row_h + 4.0f) ? 0 : 1;
                int osc_idx = r * 2 + c;
                std::string o_prefix = "o" + std::to_string(osc_idx) + "_";
                float px = grid_x + (c == 0 ? 0.0f : (osc_col_w + 8.0f));
                float py = grid_y + (r == 0 ? 0.0f : (osc_row_h + 8.0f));
                float pw = osc_col_w;
                float ph = osc_row_h;

                // Power toggle (illuminated LED switch)
                if (fx >= px + 4.0f && fx <= px + 36.0f && fy >= py + 38.0f && fy <= py + 64.0f) {
                    float cur = synth->get_param_by_id(o_prefix + "on");
                    synth->set_param_by_id(o_prefix + "on", cur > 0.5f ? 0.0f : 1.0f);
                    status_msg = "XOSC Osc " + std::to_string(osc_idx + 1) + ": " + (cur > 0.5f ? "OFF" : "ON");
                    InvalidateRect(hwnd, &bounds, FALSE);
                    return false;
                }
                // Wave buttons (Sine, Square, Saw)
                for (int w = 0; w < 3; ++w) {
                    float wx = px + 38.0f + static_cast<float>(w) * 24.0f;
                    if (fx >= wx && fx <= wx + 23.0f && fy >= py + 38.0f && fy <= py + 64.0f) {
                        synth->set_param_by_id(o_prefix + "wave", static_cast<float>(w));
                        const char* wnames[3] = {"Sine", "Square", "Saw"};
                        status_msg = "XOSC Osc " + std::to_string(osc_idx + 1) + " Wave: " + wnames[w];
                        InvalidateRect(hwnd, &bounds, FALSE);
                        return false;
                    }
                }
                // Unison [-]
                if (fx >= px + 108.0f && fx <= px + 129.0f && fy >= py + 38.0f && fy <= py + 64.0f) {
                    float v = synth->get_param_by_id(o_prefix + "voices");
                    float nv = std::clamp(v - 1.0f, 0.0f, 11.0f);
                    synth->set_param_by_id(o_prefix + "voices", nv);
                    status_msg = "XOSC Osc " + std::to_string(osc_idx + 1) + " Unison: " + std::to_string(static_cast<int>(nv) + 1) + " voices";
                    InvalidateRect(hwnd, &bounds, FALSE);
                    return false;
                }
                // Unison [U N] middle value box (click left decrements, click right increments)
                if (fx > px + 129.0f && fx < px + 159.0f && fy >= py + 38.0f && fy <= py + 64.0f) {
                    float v = synth->get_param_by_id(o_prefix + "voices");
                    float nv = (fx < px + 144.0f) ? std::clamp(v - 1.0f, 0.0f, 11.0f) : std::clamp(v + 1.0f, 0.0f, 11.0f);
                    synth->set_param_by_id(o_prefix + "voices", nv);
                    status_msg = "XOSC Osc " + std::to_string(osc_idx + 1) + " Unison: " + std::to_string(static_cast<int>(nv) + 1) + " voices";
                    InvalidateRect(hwnd, &bounds, FALSE);
                    return false;
                }
                // Unison [+]
                if (fx >= px + 159.0f && fx <= px + 178.0f && fy >= py + 38.0f && fy <= py + 64.0f) {
                    float v = synth->get_param_by_id(o_prefix + "voices");
                    float nv = std::clamp(v + 1.0f, 0.0f, 11.0f);
                    synth->set_param_by_id(o_prefix + "voices", nv);
                    status_msg = "XOSC Osc " + std::to_string(osc_idx + 1) + " Unison: " + std::to_string(static_cast<int>(nv) + 1) + " voices";
                    InvalidateRect(hwnd, &bounds, FALSE);
                    return false;
                }
                // Tune knob
                float tune_x = px + 180.0f;
                float rem_w = std::max(40.0f, (px + pw - 4.0f) - tune_x);
                float half_w = rem_w * 0.5f;
                if (fx >= tune_x && fx <= tune_x + half_w - 2.0f && fy >= py + 34.0f && fy <= py + 94.0f) {
                    int idx = xosc::index(o_prefix + "tune");
                    dragging_param_idx = idx;
                    drag_start_y = y;
                    drag_orig_val = synth->get_parameter(static_cast<uint32_t>(idx));
                    return false;
                }
                // Phase knob
                if (fx >= tune_x + half_w + 2.0f && fx <= px + pw - 2.0f && fy >= py + 34.0f && fy <= py + 94.0f) {
                    int idx = xosc::index(o_prefix + "phase");
                    dragging_param_idx = idx;
                    drag_start_y = y;
                    drag_orig_val = synth->get_parameter(static_cast<uint32_t>(idx));
                    return false;
                }
                // Bottom knobs: Vol, Pan, Detune, Stereo
                float k_w = (pw - 12.0f) * 0.25f;
                const char* osc_knob_ids[4] = {"vol", "pan", "detune", "stereo"};
                for (int k = 0; k < 4; ++k) {
                    float kx = px + 6.0f + static_cast<float>(k) * k_w;
                    if (fx >= kx && fx <= kx + k_w && fy >= py + 94.0f && fy <= py + ph - 2.0f) {
                        int idx = xosc::index(o_prefix + osc_knob_ids[k]);
                        dragging_param_idx = idx;
                        drag_start_y = y;
                        drag_orig_val = synth->get_parameter(static_cast<uint32_t>(idx));
                        return false;
                    }
                }
            } else if (fx >= amp_x && fx <= amp_x + amp_w && fy >= grid_y && fy <= grid_y + top_h) {
                // 2 Amp Envelopes
                int r = (fy < grid_y + osc_row_h + 4.0f) ? 0 : 1;
                int amp_idx = r;
                std::string a_prefix = "a" + std::to_string(amp_idx) + "_";
                float px = amp_x;
                float py = grid_y + (r == 0 ? 0.0f : (osc_row_h + 8.0f));
                float pw = amp_w;
                float ph = osc_row_h;

                // Power toggle
                if (fx >= px + 6.0f && fx <= px + 38.0f && fy >= py + 38.0f && fy <= py + 64.0f) {
                    float cur = synth->get_param_by_id(a_prefix + "on");
                    synth->set_param_by_id(a_prefix + "on", cur > 0.5f ? 0.0f : 1.0f);
                    status_msg = "XOSC Amp Env " + std::to_string(amp_idx + 1) + ": " + (cur > 0.5f ? "OFF" : "ON");
                    InvalidateRect(hwnd, &bounds, FALSE);
                    return false;
                }
                // OSC Routing: [O1]..[O4]
                float r_start = px + 44.0f;
                for (int route = 0; route < 4; ++route) {
                    float rx = r_start + static_cast<float>(route) * 34.0f;
                    if (fx >= rx && fx <= rx + 32.0f && fy >= py + 38.0f && fy <= py + 64.0f) {
                        float cur = synth->get_param_by_id(a_prefix + "route" + std::to_string(route));
                        synth->set_param_by_id(a_prefix + "route" + std::to_string(route), cur > 0.5f ? 0.0f : 1.0f);
                        InvalidateRect(hwnd, &bounds, FALSE);
                        return false;
                    }
                }
                // ADSR knobs
                float k_w = (pw - 16.0f) * 0.25f;
                const char* env_knobs[4] = {"attack", "decay", "sustain", "release"};
                for (int k = 0; k < 4; ++k) {
                    float kx = px + 8.0f + static_cast<float>(k) * k_w;
                    if (fx >= kx && fx <= kx + k_w && fy >= py + 66.0f && fy <= py + ph - 2.0f) {
                        int idx = xosc::index(a_prefix + env_knobs[k]);
                        dragging_param_idx = idx;
                        drag_start_y = y;
                        drag_orig_val = synth->get_parameter(static_cast<uint32_t>(idx));
                        return false;
                    }
                }
            } else if (fx >= grid_x && fx <= grid_x + grid_w && fy >= flt_y && fy <= flt_y + flt_h) {
                // 2 Filters (bottom)
                int c = (fx < grid_x + flt_col_w + 5.0f) ? 0 : 1;
                int flt_idx = c;
                std::string f_prefix = "f" + std::to_string(flt_idx) + "_";
                float px = grid_x + (c == 0 ? 0.0f : (flt_col_w + 10.0f));
                float py = flt_y;
                float pw = flt_col_w;
                float ph = flt_h;

                // Power toggle
                if (fx >= px + 6.0f && fx <= px + 38.0f && fy >= py + 38.0f && fy <= py + 64.0f) {
                    float cur = synth->get_param_by_id(f_prefix + "on");
                    synth->set_param_by_id(f_prefix + "on", cur > 0.5f ? 0.0f : 1.0f);
                    status_msg = "XOSC Filter " + std::to_string(flt_idx + 1) + ": " + (cur > 0.5f ? "OFF" : "ON");
                    InvalidateRect(hwnd, &bounds, FALSE);
                    return false;
                }
                // Filter Types: [LP] [HP] [BP] [NOTCH]
                for (int t = 0; t < 4; ++t) {
                    float tx = px + 44.0f + static_cast<float>(t) * 34.0f;
                    if (fx >= tx && fx <= tx + 32.0f && fy >= py + 38.0f && fy <= py + 64.0f) {
                        synth->set_param_by_id(f_prefix + "type", static_cast<float>(t));
                        const char* tnames[4] = {"Low-Pass", "High-Pass", "Band-Pass", "Notch"};
                        status_msg = "XOSC Filter " + std::to_string(flt_idx + 1) + " Type: " + tnames[t];
                        InvalidateRect(hwnd, &bounds, FALSE);
                        return false;
                    }
                }
                // Routes: [O1]..[O4]
                float r_start = px + 186.0f;
                for (int route = 0; route < 4; ++route) {
                    float rx = r_start + static_cast<float>(route) * 34.0f;
                    if (fx >= rx && fx <= rx + 32.0f && fy >= py + 38.0f && fy <= py + 64.0f) {
                        float cur = synth->get_param_by_id(f_prefix + "route" + std::to_string(route));
                        synth->set_param_by_id(f_prefix + "route" + std::to_string(route), cur > 0.5f ? 0.0f : 1.0f);
                        InvalidateRect(hwnd, &bounds, FALSE);
                        return false;
                    }
                }
                // Knobs: Cutoff, Res, Drive
                float k_w = (pw - 16.0f) / 3.0f;
                const char* flt_knobs[3] = {"cutoff", "res", "drive"};
                for (int k = 0; k < 3; ++k) {
                    float kx = px + 8.0f + static_cast<float>(k) * k_w;
                    if (fx >= kx && fx <= kx + k_w && fy >= py + 66.0f && fy <= py + ph - 2.0f) {
                        int idx = xosc::index(f_prefix + flt_knobs[k]);
                        dragging_param_idx = idx;
                        drag_start_y = y;
                        drag_orig_val = synth->get_parameter(static_cast<uint32_t>(idx));
                        return false;
                    }
                }
            }
        } else if (active_tab == 1) {
            // Tab 1: 4 MOD ENVELOPES (2x2 Grid) with Target Dropdown & Destination List
            if (fx >= grid_x && fx <= grid_x + grid_w && fy >= grid_y && fy <= grid_y + grid_h) {
                int c = (fx < grid_x + mod_col_w + 5.0f) ? 0 : 1;
                int r = (fy < grid_y + mod_row_h + 4.0f) ? 0 : 1;
                int env_idx = r * 2 + c;
                std::string e_prefix = "e" + std::to_string(env_idx) + "_";
                float px = grid_x + (c == 0 ? 0.0f : (mod_col_w + 10.0f));
                float py = grid_y + (r == 0 ? 0.0f : (mod_row_h + 8.0f));
                float pw = mod_col_w;
                float ph = mod_row_h;

                // Power toggle
                if (fx >= px + 6.0f && fx <= px + 38.0f && fy >= py + 38.0f && fy <= py + 64.0f) {
                    float cur = synth->get_param_by_id(e_prefix + "on");
                    synth->set_param_by_id(e_prefix + "on", cur > 0.5f ? 0.0f : 1.0f);
                    status_msg = "XOSC Mod Env " + std::to_string(env_idx + 1) + ": " + (cur > 0.5f ? "OFF" : "ON");
                    InvalidateRect(hwnd, &bounds, FALSE);
                    return false;
                }
                // Target Dropdown Button [Target ▼]
                if (fx >= px + 40.0f && fx <= px + 178.0f && fy >= py + 38.0f && fy <= py + 64.0f) {
                    const char* tgt_names[9] = {
                        "Osc Volume", "Osc Pitch", "Cutoff (All)", "Filter 1 Cutoff",
                        "Filter 1 Res", "Filter 2 Cutoff", "Filter 2 Res", "Filter Drive", "Osc Pan"
                    };
                    int cur_target = std::clamp(static_cast<int>(std::round(synth->get_param_by_id(e_prefix + "target"))), 0, 8);
                    if (hwnd) {
                        HMENU hMenu = CreatePopupMenu();
                        if (hMenu) {
                            for (int i = 0; i < 9; ++i) {
                                UINT flags = MF_STRING;
                                if (i == cur_target) flags |= MF_CHECKED;
                                AppendMenuA(hMenu, flags, i + 1, tgt_names[i]);
                            }
                            POINT pt = { static_cast<LONG>(px + 42.0f), static_cast<LONG>(py + 64.0f) };
                            ClientToScreen(hwnd, &pt);
                            int selected = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hwnd, NULL);
                            DestroyMenu(hMenu);
                            if (selected >= 1 && selected <= 9) {
                                synth->set_param_by_id(e_prefix + "target", static_cast<float>(selected - 1));
                                status_msg = "XOSC Mod Env " + std::to_string(env_idx + 1) + " Target: " + tgt_names[selected - 1];
                                InvalidateRect(hwnd, &bounds, FALSE);
                            }
                            return false;
                        }
                    } else {
                        // Fallback cycle when hwnd is not available
                        int next_tgt = (cur_target + 1) % 9;
                        synth->set_param_by_id(e_prefix + "target", static_cast<float>(next_tgt));
                        status_msg = "XOSC Mod Env " + std::to_string(env_idx + 1) + " Target: " + tgt_names[next_tgt];
                        InvalidateRect(hwnd, &bounds, FALSE);
                        return false;
                    }
                }
                // OSC Routing: [O1]..[O4]
                float r_start = px + 182.0f;
                for (int route = 0; route < 4; ++route) {
                    float rx = r_start + static_cast<float>(route) * 32.0f;
                    if (fx >= rx && fx <= rx + 30.0f && fy >= py + 38.0f && fy <= py + 64.0f) {
                        float cur = synth->get_param_by_id(e_prefix + "route" + std::to_string(route));
                        synth->set_param_by_id(e_prefix + "route" + std::to_string(route), cur > 0.5f ? 0.0f : 1.0f);
                        InvalidateRect(hwnd, &bounds, FALSE);
                        return false;
                    }
                }
                // Mix knob
                if (fx >= px + 316.0f && fx <= px + pw - 6.0f && fy >= py + 34.0f && fy <= py + 94.0f) {
                    int idx = xosc::index(e_prefix + "mix");
                    dragging_param_idx = idx;
                    drag_start_y = y;
                    drag_orig_val = synth->get_parameter(static_cast<uint32_t>(idx));
                    return false;
                }
                // ADSR knobs
                float k_w = (pw - 16.0f) * 0.25f;
                const char* env_knobs[4] = {"attack", "decay", "sustain", "release"};
                for (int k = 0; k < 4; ++k) {
                    float kx = px + 8.0f + static_cast<float>(k) * k_w;
                    if (fx >= kx && fx <= kx + k_w && fy >= py + 94.0f && fy <= py + ph - 4.0f) {
                        int idx = xosc::index(e_prefix + env_knobs[k]);
                        dragging_param_idx = idx;
                        drag_start_y = y;
                        drag_orig_val = synth->get_parameter(static_cast<uint32_t>(idx));
                        return false;
                    }
                }
            }
        } else {
            // Tab 2: EFFECTS + PERFORMANCE (2 Columns x 3 Rows)
            if (fx >= grid_x && fx <= grid_x + grid_w && fy >= grid_y && fy <= grid_y + grid_h) {
                int col = (fx < grid_x + col_w) ? 0 : 1;
                int row = std::clamp(static_cast<int>((fy - grid_y) / (row_h + 6.0f)), 0, 2);
                float px = grid_x + (col == 0 ? 0.0f : (col_w + 10.0f));
                float py = grid_y + static_cast<float>(row) * (row_h + 6.0f);

                if (row == 0) {
                    if (col == 0) {
                        // Distortion
                        if (fx >= px + 6.0f && fx <= px + 40.0f && fy >= py + 38.0f && fy <= py + 64.0f) {
                            float cur = synth->get_param_by_id("dist_on");
                            synth->set_param_by_id("dist_on", cur > 0.5f ? 0.0f : 1.0f);
                            status_msg = "XOSC Distortion: " + std::string(cur > 0.5f ? "OFF" : "ON");
                            InvalidateRect(hwnd, &bounds, FALSE);
                            return false;
                        }
                        if (fx >= px + 60.0f && fx <= px + 210.0f && fy >= py + 70.0f && fy <= py + row_h - 4.0f) {
                            int idx = xosc::index("dist_drive");
                            dragging_param_idx = idx;
                            drag_start_y = y;
                            drag_orig_val = synth->get_parameter(static_cast<uint32_t>(idx));
                            return false;
                        }
                        if (fx >= px + 220.0f && fx <= px + col_w - 20.0f && fy >= py + 70.0f && fy <= py + row_h - 4.0f) {
                            int idx = xosc::index("dist_mix");
                            dragging_param_idx = idx;
                            drag_start_y = y;
                            drag_orig_val = synth->get_parameter(static_cast<uint32_t>(idx));
                            return false;
                        }
                    } else {
                        // EQ
                        if (fx >= px + 6.0f && fx <= px + 40.0f && fy >= py + 38.0f && fy <= py + 64.0f) {
                            float cur = synth->get_param_by_id("eq_on");
                            synth->set_param_by_id("eq_on", cur > 0.5f ? 0.0f : 1.0f);
                            status_msg = "XOSC EQ: " + std::string(cur > 0.5f ? "OFF" : "ON");
                            InvalidateRect(hwnd, &bounds, FALSE);
                            return false;
                        }
                        float kw = (col_w - 20.0f) / 3.0f;
                        const char* eq_ids[3] = {"eq_low", "eq_mid", "eq_high"};
                        for (int k = 0; k < 3; ++k) {
                            float kx = px + 10.0f + static_cast<float>(k) * kw;
                            if (fx >= kx && fx <= kx + kw && fy >= py + 70.0f && fy <= py + row_h - 4.0f) {
                                int idx = xosc::index(eq_ids[k]);
                                dragging_param_idx = idx;
                                drag_start_y = y;
                                drag_orig_val = synth->get_parameter(static_cast<uint32_t>(idx));
                                return false;
                            }
                        }
                    }
                } else if (row == 1) {
                    if (col == 0) {
                        // Compressor
                        if (fx >= px + 6.0f && fx <= px + 40.0f && fy >= py + 38.0f && fy <= py + 64.0f) {
                            float cur = synth->get_param_by_id("comp_on");
                            synth->set_param_by_id("comp_on", cur > 0.5f ? 0.0f : 1.0f);
                            status_msg = "XOSC Compressor: " + std::string(cur > 0.5f ? "OFF" : "ON");
                            InvalidateRect(hwnd, &bounds, FALSE);
                            return false;
                        }
                        float kw = (col_w - 20.0f) / 5.0f;
                        const char* comp_ids[5] = {"comp_threshold", "comp_ratio", "comp_attack", "comp_release", "comp_makeup"};
                        for (int k = 0; k < 5; ++k) {
                            float kx = px + 10.0f + static_cast<float>(k) * kw;
                            if (fx >= kx && fx <= kx + kw && fy >= py + 70.0f && fy <= py + row_h - 4.0f) {
                                int idx = xosc::index(comp_ids[k]);
                                dragging_param_idx = idx;
                                drag_start_y = y;
                                drag_orig_val = synth->get_parameter(static_cast<uint32_t>(idx));
                                return false;
                            }
                        }
                    } else {
                        // Delay
                        if (fx >= px + 6.0f && fx <= px + 40.0f && fy >= py + 38.0f && fy <= py + 64.0f) {
                            float cur = synth->get_param_by_id("delay_on");
                            synth->set_param_by_id("delay_on", cur > 0.5f ? 0.0f : 1.0f);
                            status_msg = "XOSC Delay: " + std::string(cur > 0.5f ? "OFF" : "ON");
                            InvalidateRect(hwnd, &bounds, FALSE);
                            return false;
                        }
                        float kw = (col_w - 20.0f) / 3.0f;
                        const char* dly_ids[3] = {"delay_time", "delay_feedback", "delay_mix"};
                        for (int k = 0; k < 3; ++k) {
                            float kx = px + 10.0f + static_cast<float>(k) * kw;
                            if (fx >= kx && fx <= kx + kw && fy >= py + 70.0f && fy <= py + row_h - 4.0f) {
                                int idx = xosc::index(dly_ids[k]);
                                dragging_param_idx = idx;
                                drag_start_y = y;
                                drag_orig_val = synth->get_parameter(static_cast<uint32_t>(idx));
                                return false;
                            }
                        }
                    }
                } else {
                    if (col == 0) {
                        // Reverb
                        if (fx >= px + 6.0f && fx <= px + 40.0f && fy >= py + 38.0f && fy <= py + 64.0f) {
                            float cur = synth->get_param_by_id("reverb_on");
                            synth->set_param_by_id("reverb_on", cur > 0.5f ? 0.0f : 1.0f);
                            status_msg = "XOSC Reverb: " + std::string(cur > 0.5f ? "OFF" : "ON");
                            InvalidateRect(hwnd, &bounds, FALSE);
                            return false;
                        }
                        float kw = (col_w - 20.0f) / 3.0f;
                        const char* rev_ids[3] = {"reverb_size", "reverb_damp", "reverb_mix"};
                        for (int k = 0; k < 3; ++k) {
                            float kx = px + 10.0f + static_cast<float>(k) * kw;
                            if (fx >= kx && fx <= kx + kw && fy >= py + 70.0f && fy <= py + row_h - 4.0f) {
                                int idx = xosc::index(rev_ids[k]);
                                dragging_param_idx = idx;
                                drag_start_y = y;
                                drag_orig_val = synth->get_parameter(static_cast<uint32_t>(idx));
                                return false;
                            }
                        }
                    } else {
                        // Performance
                        if (fx >= px + 8.0f && fx <= px + 130.0f && fy >= py + 40.0f && fy <= py + 68.0f) {
                            float cur = synth->get_param_by_id("mono_legato");
                            synth->set_param_by_id("mono_legato", cur > 0.5f ? 0.0f : 1.0f);
                            status_msg = "XOSC Mono/Legato: " + std::string(cur > 0.5f ? "OFF" : "ON");
                            InvalidateRect(hwnd, &bounds, FALSE);
                            return false;
                        }
                        if (fx >= px + 136.0f && fx <= px + 250.0f && fy >= py + 40.0f && fy <= py + 68.0f) {
                            float cur = synth->get_param_by_id("glide_on");
                            synth->set_param_by_id("glide_on", cur > 0.5f ? 0.0f : 1.0f);
                            status_msg = "XOSC Glide: " + std::string(cur > 0.5f ? "OFF" : "ON");
                            InvalidateRect(hwnd, &bounds, FALSE);
                            return false;
                        }
                        if (fx >= px + 14.0f && fx <= px + 126.0f && fy >= py + 86.0f && fy <= py + 112.0f) {
                            synth->set_param_by_id("glide_mode", 0.0f);
                            status_msg = "XOSC Glide Mode: Auto";
                            InvalidateRect(hwnd, &bounds, FALSE);
                            return false;
                        }
                        if (fx >= px + 14.0f && fx <= px + 126.0f && fy >= py + 116.0f && fy <= py + 142.0f) {
                            synth->set_param_by_id("glide_mode", 1.0f);
                            status_msg = "XOSC Glide Mode: Always";
                            InvalidateRect(hwnd, &bounds, FALSE);
                            return false;
                        }
                        if (fx >= px + 150.0f && fx <= px + col_w - 20.0f && fy >= py + 76.0f && fy <= py + row_h - 4.0f) {
                            int idx = xosc::index("glide_time");
                            dragging_param_idx = idx;
                            drag_start_y = y;
                            drag_orig_val = synth->get_parameter(static_cast<uint32_t>(idx));
                            return false;
                        }
                    }
                }
            }
        }

        // Virtual Keyboard
        float bot_y = static_cast<float>(my + mh - 96);
        float kb_y = bot_y + 20.0f;
        float kb_h = static_cast<float>(mh) - (kb_y - static_cast<float>(my)) - 8.0f;
        float kb_x = static_cast<float>(mx + 20);
        float kb_w = static_cast<float>(mw - 40);

        if (y >= kb_y && y <= kb_y + kb_h && x >= kb_x && x <= kb_x + kb_w) {
            const int kStartKey = 36;
            const int kEndKey = 84;
            auto is_black_key = [](int note) {
                int semi = note % 12;
                return semi == 1 || semi == 3 || semi == 6 || semi == 8 || semi == 10;
            };

            int white_count = 0;
            for (int k = kStartKey; k <= kEndKey; ++k) {
                if (!is_black_key(k)) ++white_count;
            }
            float wk_w = kb_w / static_cast<float>(white_count);
            float bk_w = wk_w * 0.62f;
            float bk_h = kb_h * 0.60f;

            // Check black keys first (they sit on top)
            if (y <= kb_y + bk_h) {
                float cur_wx = kb_x;
                for (int k = kStartKey; k <= kEndKey; ++k) {
                    if (is_black_key(k)) {
                        float bx = cur_wx - (bk_w * 0.5f);
                        if (x >= bx && x <= bx + bk_w) {
                            if (audition_cb) audition_cb(static_cast<uint8_t>(k));
                            return false;
                        }
                    } else {
                        cur_wx += wk_w;
                    }
                }
            }

            // Check white keys
            float cur_wx = kb_x;
            for (int k = kStartKey; k <= kEndKey; ++k) {
                if (!is_black_key(k)) {
                    if (x >= cur_wx && x <= cur_wx + wk_w) {
                        if (audition_cb) audition_cb(static_cast<uint8_t>(k));
                        return false;
                    }
                    cur_wx += wk_w;
                }
            }
        }

        return false;
    }

    static void handle_drag(plugins::XOSCDevice* synth, int dragging_param_idx,
                            int drag_start_y, float drag_orig_val, int current_y,
                            std::string& status_msg) {
        if (!synth || dragging_param_idx < 0 || dragging_param_idx >= 145) return;

        bool is_shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        float sens = is_shift ? 0.001f : 0.0065f;
        float delta = static_cast<float>(drag_start_y - current_y) * sens;
        float new_norm = std::clamp(drag_orig_val + delta, 0.0f, 1.0f);

        synth->set_parameter(static_cast<uint32_t>(dragging_param_idx), new_norm);
        const auto& s = xosc::specs()[dragging_param_idx];
        float plain = synth->get_param_plain(static_cast<size_t>(dragging_param_idx));
        status_msg = "XOSC " + s.name + ": " + format_param_val(s, plain);
    }

    static void handle_wheel(plugins::XOSCDevice* synth, const RECT& bounds,
                             int x, int y, int steps, std::string& status_msg,
                             int active_tab = 0) {
        if (!synth || steps == 0) return;

        int mx = bounds.left;
        int my = bounds.top;
        int mw = bounds.right - bounds.left;
        int mh = bounds.bottom - bounds.top;

        int mixer_x = mx + mw - 195;
        int mixer_y = my + 94;
        int mixer_w = 180;
        int mixer_h = mh - 198;

        int target_idx = -1;

        // 1. Output Mixer Knobs
        if (x >= mixer_x + 15 && x <= mixer_x + mixer_w - 15 && y >= mixer_y + 86 && y <= mixer_y + 180) {
            target_idx = xosc::index("master_gain");
        } else if (x >= mixer_x + 15 && x <= mixer_x + mixer_w - 15 && y >= mixer_y + 190 && y <= mixer_y + 284) {
            target_idx = xosc::index("master_width");
        } else {
            // 2. Main Grid
            float fx = static_cast<float>(x);
            float fy = static_cast<float>(y);
            float grid_x = static_cast<float>(mx + 20);
            float grid_y = static_cast<float>(my + 94);
            float grid_w = static_cast<float>(mixer_x - (mx + 20) - 12);
            float grid_h = static_cast<float>(mixer_h);
            float top_h = (grid_h - 10.0f) * 0.64f;
            float flt_y = grid_y + top_h + 10.0f;
            float flt_h = grid_h - top_h - 10.0f;
            float osc_total_w = (grid_w - 10.0f) * 0.64f;
            float amp_x = grid_x + osc_total_w + 10.0f;
            float amp_w = grid_w - osc_total_w - 10.0f;
            float osc_col_w = (osc_total_w - 8.0f) * 0.5f;
            float osc_row_h = (top_h - 8.0f) * 0.5f;
            float flt_col_w = (grid_w - 10.0f) * 0.5f;
            float mod_col_w = (grid_w - 10.0f) * 0.5f;
            float mod_row_h = (grid_h - 8.0f) * 0.5f;
            float col_w = (grid_w - 10.0f) * 0.5f;
            float row_h = (grid_h - 12.0f) / 3.0f;

            if (active_tab == 0) {
                if (fx >= grid_x && fx <= grid_x + osc_total_w && fy >= grid_y && fy <= grid_y + top_h) {
                    int c = (fx < grid_x + osc_col_w + 4.0f) ? 0 : 1;
                    int r = (fy < grid_y + osc_row_h + 4.0f) ? 0 : 1;
                    int osc_idx = r * 2 + c;
                    std::string o_prefix = "o" + std::to_string(osc_idx) + "_";
                    float px = grid_x + (c == 0 ? 0.0f : (osc_col_w + 8.0f));
                    float py = grid_y + (r == 0 ? 0.0f : (osc_row_h + 8.0f));
                    float pw = osc_col_w;
                    float ph = osc_row_h;

                    // Unison area mouse wheel (direct increment / decrement!)
                    if (fx >= px + 108.0f && fx <= px + 178.0f && fy >= py + 38.0f && fy <= py + 64.0f) {
                        float v = synth->get_param_by_id(o_prefix + "voices");
                        float nv = std::clamp(v + (steps > 0 ? 1.0f : -1.0f), 0.0f, 11.0f);
                        synth->set_param_by_id(o_prefix + "voices", nv);
                        status_msg = "XOSC Osc " + std::to_string(osc_idx + 1) + " Unison: " + std::to_string(static_cast<int>(nv) + 1) + " voices";
                        return;
                    }

                    float tune_x = px + 180.0f;
                    float rem_w = std::max(40.0f, (px + pw - 4.0f) - tune_x);
                    float half_w = rem_w * 0.5f;

                    if (fx >= tune_x && fx <= tune_x + half_w - 2.0f && fy >= py + 34.0f && fy <= py + 94.0f) {
                        target_idx = xosc::index(o_prefix + "tune");
                    } else if (fx >= tune_x + half_w + 2.0f && fx <= px + pw - 2.0f && fy >= py + 34.0f && fy <= py + 94.0f) {
                        target_idx = xosc::index(o_prefix + "phase");
                    } else if (fy >= py + 94.0f && fy <= py + ph - 2.0f) {
                        float k_w = (pw - 12.0f) * 0.25f;
                        int k = std::clamp(static_cast<int>((fx - (px + 6.0f)) / k_w), 0, 3);
                        const char* osc_knob_ids[4] = {"vol", "pan", "detune", "stereo"};
                        target_idx = xosc::index(o_prefix + osc_knob_ids[k]);
                    }
                } else if (fx >= amp_x && fx <= amp_x + amp_w && fy >= grid_y && fy <= grid_y + top_h) {
                    int r = (fy < grid_y + osc_row_h + 4.0f) ? 0 : 1;
                    int amp_idx = r;
                    std::string a_prefix = "a" + std::to_string(amp_idx) + "_";
                    float px = amp_x;
                    float py = grid_y + (r == 0 ? 0.0f : (osc_row_h + 8.0f));
                    float pw = amp_w;
                    float ph = osc_row_h;

                    if (fy >= py + 66.0f && fy <= py + ph - 2.0f) {
                        float k_w = (pw - 16.0f) * 0.25f;
                        int k = std::clamp(static_cast<int>((fx - (px + 8.0f)) / k_w), 0, 3);
                        const char* env_knobs[4] = {"attack", "decay", "sustain", "release"};
                        target_idx = xosc::index(a_prefix + env_knobs[k]);
                    }
                } else if (fx >= grid_x && fx <= grid_x + grid_w && fy >= flt_y && fy <= flt_y + flt_h) {
                    int c = (fx < grid_x + flt_col_w + 5.0f) ? 0 : 1;
                    int flt_idx = c;
                    std::string f_prefix = "f" + std::to_string(flt_idx) + "_";
                    float px = grid_x + (c == 0 ? 0.0f : (flt_col_w + 10.0f));
                    float py = flt_y;
                    float pw = flt_col_w;
                    float ph = flt_h;

                    if (fy >= py + 66.0f && fy <= py + ph - 2.0f) {
                        float k_w = (pw - 16.0f) / 3.0f;
                        int k = std::clamp(static_cast<int>((fx - (px + 8.0f)) / k_w), 0, 2);
                        const char* flt_knobs[3] = {"cutoff", "res", "drive"};
                        target_idx = xosc::index(f_prefix + flt_knobs[k]);
                    }
                }
            } else if (active_tab == 1) {
                if (fx >= grid_x && fx <= grid_x + grid_w && fy >= grid_y && fy <= grid_y + grid_h) {
                    int c = (fx < grid_x + mod_col_w + 5.0f) ? 0 : 1;
                    int r = (fy < grid_y + mod_row_h + 4.0f) ? 0 : 1;
                    int env_idx = r * 2 + c;
                    std::string e_prefix = "e" + std::to_string(env_idx) + "_";
                    float px = grid_x + (c == 0 ? 0.0f : (mod_col_w + 10.0f));
                    float py = grid_y + (r == 0 ? 0.0f : (mod_row_h + 8.0f));
                    float pw = mod_col_w;
                    float ph = mod_row_h;

                    if (fx >= px + 316.0f && fx <= px + pw - 6.0f && fy >= py + 34.0f && fy <= py + 94.0f) {
                        target_idx = xosc::index(e_prefix + "mix");
                    } else if (fy >= py + 94.0f && fy <= py + ph - 4.0f) {
                        float k_w = (pw - 16.0f) * 0.25f;
                        int k = std::clamp(static_cast<int>((fx - (px + 8.0f)) / k_w), 0, 3);
                        const char* env_knobs[4] = {"attack", "decay", "sustain", "release"};
                        target_idx = xosc::index(e_prefix + env_knobs[k]);
                    }
                }
            } else {
                // Tab 2 (active_tab == 2: Effects & Performance)
                if (fx >= grid_x && fx <= grid_x + grid_w && fy >= grid_y && fy <= grid_y + grid_h) {
                    int col = (fx < grid_x + col_w) ? 0 : 1;
                    int row = std::clamp(static_cast<int>((fy - grid_y) / (row_h + 6.0f)), 0, 2);
                    float px = grid_x + (col == 0 ? 0.0f : (col_w + 10.0f));
                    float py = grid_y + static_cast<float>(row) * (row_h + 6.0f);

                    if (row == 0) {
                        if (col == 0) {
                            if (fy >= py + 70.0f && fy <= py + row_h - 4.0f) {
                                target_idx = (fx <= px + 215.0f) ? xosc::index("dist_drive") : xosc::index("dist_mix");
                            }
                        } else {
                            if (fy >= py + 70.0f && fy <= py + row_h - 4.0f) {
                                float kw = (col_w - 20.0f) / 3.0f;
                                int k = std::clamp(static_cast<int>((fx - (px + 10.0f)) / kw), 0, 2);
                                const char* eq_ids[3] = {"eq_low", "eq_mid", "eq_high"};
                                target_idx = xosc::index(eq_ids[k]);
                            }
                        }
                    } else if (row == 1) {
                        if (col == 0) {
                            if (fy >= py + 70.0f && fy <= py + row_h - 4.0f) {
                                float kw = (col_w - 20.0f) / 5.0f;
                                int k = std::clamp(static_cast<int>((fx - (px + 10.0f)) / kw), 0, 4);
                                const char* comp_ids[5] = {"comp_threshold", "comp_ratio", "comp_attack", "comp_release", "comp_makeup"};
                                target_idx = xosc::index(comp_ids[k]);
                            }
                        } else {
                            if (fy >= py + 70.0f && fy <= py + row_h - 4.0f) {
                                float kw = (col_w - 20.0f) / 3.0f;
                                int k = std::clamp(static_cast<int>((fx - (px + 10.0f)) / kw), 0, 2);
                                const char* dly_ids[3] = {"delay_time", "delay_feedback", "delay_mix"};
                                target_idx = xosc::index(dly_ids[k]);
                            }
                        }
                    } else {
                        if (col == 0) {
                            if (fy >= py + 70.0f && fy <= py + row_h - 4.0f) {
                                float kw = (col_w - 20.0f) / 3.0f;
                                int k = std::clamp(static_cast<int>((fx - (px + 10.0f)) / kw), 0, 2);
                                const char* rev_ids[3] = {"reverb_size", "reverb_damp", "reverb_mix"};
                                target_idx = xosc::index(rev_ids[k]);
                            }
                        } else {
                            if (fx >= px + 150.0f && fx <= px + col_w - 20.0f &&
                                fy >= py + 76.0f && fy <= py + row_h - 4.0f) {
                                target_idx = xosc::index("glide_time");
                            }
                        }
                    }
                }
            }
        }

        if (target_idx < 0) {
            return;
        }

        float cur = synth->get_parameter(static_cast<uint32_t>(target_idx));
        bool is_shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        float step_val = (is_shift ? 0.005f : 0.02f) * static_cast<float>(steps);
        float new_norm = std::clamp(cur + step_val, 0.0f, 1.0f);
        synth->set_parameter(static_cast<uint32_t>(target_idx), new_norm);
        const auto& s = xosc::specs()[target_idx];
        float plain = synth->get_param_plain(static_cast<size_t>(target_idx));
        status_msg = "XOSC " + s.name + ": " + format_param_val(s, plain);
    }
};

} // namespace digidaw::adapters::gui
