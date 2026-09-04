#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

namespace digidaw::adapters::gui {

inline std::wstring to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), NULL, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &wstr[0], size_needed);
    return wstr;
}

struct D2DTheme {
    // 0. DESIGN.md Color Tokens
    // Backgrounds (Near-black layered surfaces)
    D2D1_COLOR_F bg_app        {0.0353f, 0.0392f, 0.0471f, 1.0f}; // #090a0c
    D2D1_COLOR_F bg_surface    {0.0510f, 0.0588f, 0.0667f, 1.0f}; // #0d0f11
    D2D1_COLOR_F bg_surface_2  {0.0667f, 0.0745f, 0.0863f, 1.0f}; // #111316
    D2D1_COLOR_F bg_elevated   {0.0824f, 0.0941f, 0.1059f, 1.0f}; // #15181b
    D2D1_COLOR_F bg_control    {0.0980f, 0.1098f, 0.1255f, 1.0f}; // #191c20

    // Compatibility aliases
    D2D1_COLOR_F bg_dark       {0.0353f, 0.0392f, 0.0471f, 1.0f}; // #090a0c
    D2D1_COLOR_F bg_panel      {0.0510f, 0.0588f, 0.0667f, 1.0f}; // #0d0f11
    D2D1_COLOR_F bg_card       {0.0667f, 0.0745f, 0.0863f, 1.0f}; // #111316
    D2D1_COLOR_F bg_input      {0.0980f, 0.1098f, 0.1255f, 1.0f}; // #191c20
    D2D1_COLOR_F bg_header     {0.0824f, 0.0941f, 0.1059f, 1.0f}; // #15181b

    // Borders & Dividers (Low-contrast 1px hairlines)
    D2D1_COLOR_F border_subtle {0.1255f, 0.1373f, 0.1569f, 1.0f}; // #202328
    D2D1_COLOR_F border_default{0.1647f, 0.1804f, 0.2039f, 1.0f}; // #2a2e34
    D2D1_COLOR_F border_strong {0.2039f, 0.2196f, 0.2431f, 1.0f}; // #34383e
    D2D1_COLOR_F border_dark   {0.1255f, 0.1373f, 0.1569f, 1.0f}; // #202328
    D2D1_COLOR_F border_light  {0.1647f, 0.1804f, 0.2039f, 1.0f}; // #2a2e34
    D2D1_COLOR_F border_faint  {0.0863f, 0.0941f, 0.1098f, 1.0f}; // hairline divider

    // Text Hierarchy
    D2D1_COLOR_F text_primary  {0.9098f, 0.9098f, 0.9176f, 1.0f}; // #e8e8ea
    D2D1_COLOR_F text_secondary{0.6039f, 0.6157f, 0.6392f, 1.0f}; // #9a9da3
    D2D1_COLOR_F text_muted    {0.3843f, 0.4000f, 0.4275f, 1.0f}; // #62666d
    D2D1_COLOR_F text_disabled {0.2667f, 0.2824f, 0.3059f, 1.0f}; // #44484e

    // Primary Accents (Electric violet / purple signature)
    D2D1_COLOR_F accent        {0.6588f, 0.3333f, 0.9686f, 1.0f}; // #a855f7
    D2D1_COLOR_F accent_bright {0.7176f, 0.4000f, 1.0000f, 1.0f}; // #b766ff
    D2D1_COLOR_F accent_deep   {0.4863f, 0.2275f, 0.9294f, 1.0f}; // #7c3aed

    // Compatibility accent aliases
    D2D1_COLOR_F accent_orange {0.6588f, 0.3333f, 0.9686f, 1.0f}; // mapped to signature violet
    D2D1_COLOR_F accent_cyan   {0.7176f, 0.4000f, 1.0000f, 1.0f}; // mapped to bright violet
    D2D1_COLOR_F accent_amber  {0.9020f, 0.7216f, 0.2902f, 1.0f}; // #e6b84a (warning)
    D2D1_COLOR_F accent_lime   {0.6588f, 0.3333f, 0.9686f, 1.0f}; // signature violet playhead
    D2D1_COLOR_F accent_green  {0.4824f, 0.8902f, 0.4157f, 1.0f}; // #7be36a (success)
    D2D1_COLOR_F accent_red    {0.8824f, 0.3569f, 0.3922f, 1.0f}; // #e15b64 (danger)

    // Semantic
    D2D1_COLOR_F success       {0.4824f, 0.8902f, 0.4157f, 1.0f}; // #7be36a
    D2D1_COLOR_F warning       {0.9020f, 0.7216f, 0.2902f, 1.0f}; // #e6b84a
    D2D1_COLOR_F danger        {0.8824f, 0.3569f, 0.3922f, 1.0f}; // #e15b64

    // Creative / Track Colors (Muted / translucent tints)
    D2D1_COLOR_F track_melody  {0.8471f, 0.4588f, 0.3843f, 1.0f}; // #d87562
    D2D1_COLOR_F track_chords  {0.8392f, 0.6627f, 0.2118f, 1.0f}; // #d6a936
    D2D1_COLOR_F track_drums   {0.6078f, 0.4235f, 0.8588f, 1.0f}; // #9b6cdb

    // Clips & Notes
    D2D1_COLOR_F clip_bg       {0.0824f, 0.0941f, 0.1059f, 1.0f}; // #15181b
    D2D1_COLOR_F clip_hdr      {0.0980f, 0.1098f, 0.1255f, 1.0f}; // #191c20
    D2D1_COLOR_F clip_border   {0.1647f, 0.1804f, 0.2039f, 1.0f}; // #2a2e34
    D2D1_COLOR_F note_silver   {0.9098f, 0.9098f, 0.9176f, 1.0f}; // #e8e8ea
    D2D1_COLOR_F note_border   {0.6588f, 0.3333f, 0.9686f, 0.7f}; // subtle violet accent
};

class D2DRenderer {
public:
    static const D2DTheme& theme() {
        static D2DTheme t;
        return t;
    }

    // Pixel-snapping helpers for razor-sharp 1px line rendering
    static inline float snap_pixel(float v) {
        return std::floor(v);
    }

    static inline float snap_half_pixel(float v) {
        return std::floor(v) + 0.5f;
    }

    // 1. Smooth Anti-Aliased Rounded Box with half-pixel border inset
    static void draw_rounded_box(ID2D1RenderTarget* rt, const D2D1_RECT_F& rc,
                                 D2D1_COLOR_F fill_color, D2D1_COLOR_F border_color,
                                 float radius = 4.0f, float stroke_width = 1.0f) {
        if (!rt) return;

        ID2D1SolidColorBrush* fill_brush = nullptr;
        ID2D1SolidColorBrush* border_brush = nullptr;
        rt->CreateSolidColorBrush(fill_color, &fill_brush);
        if (stroke_width > 0.0f) {
            rt->CreateSolidColorBrush(border_color, &border_brush);
        }

        if (radius <= 0.0f) {
            if (fill_brush) {
                rt->FillRectangle(rc, fill_brush);
            }
            if (border_brush && stroke_width > 0.0f) {
                float half_w = stroke_width * 0.5f;
                D2D1_RECT_F stroke_rc = D2D1::RectF(
                    rc.left + half_w, rc.top + half_w,
                    rc.right - half_w, rc.bottom - half_w
                );
                rt->DrawRectangle(stroke_rc, border_brush, stroke_width);
            }
        } else {
            D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rc, radius, radius);
            if (fill_brush) {
                rt->FillRoundedRectangle(rrc, fill_brush);
            }
            if (border_brush && stroke_width > 0.0f) {
                float half_w = stroke_width * 0.5f;
                D2D1_ROUNDED_RECT stroke_rrc = D2D1::RoundedRect(
                    D2D1::RectF(rc.left + half_w, rc.top + half_w,
                                rc.right - half_w, rc.bottom - half_w),
                    std::max(0.0f, radius - half_w),
                    std::max(0.0f, radius - half_w)
                );
                rt->DrawRoundedRectangle(stroke_rrc, border_brush, stroke_width);
            }
        }

        if (fill_brush) fill_brush->Release();
        if (border_brush) border_brush->Release();
    }

    // 2. Hardware Anti-Aliased DirectWrite Text with pixel-snapped layout rect
    static void draw_text(ID2D1RenderTarget* rt, IDWriteTextFormat* format,
                          const std::string& text, const D2D1_RECT_F& rc,
                          D2D1_COLOR_F color,
                          DWRITE_TEXT_ALIGNMENT align_h = DWRITE_TEXT_ALIGNMENT_LEADING,
                          DWRITE_PARAGRAPH_ALIGNMENT align_v = DWRITE_PARAGRAPH_ALIGNMENT_CENTER) {
        if (!rt || !format || text.empty()) return;

        std::wstring wtext = to_wide(text);
        format->SetTextAlignment(align_h);
        format->SetParagraphAlignment(align_v);

        // Snap text layout bounds to physical pixel coordinates to eliminate fractional stem blur
        D2D1_RECT_F snapped_rc = D2D1::RectF(
            std::floor(rc.left),
            std::floor(rc.top),
            std::ceil(rc.right),
            std::ceil(rc.bottom)
        );

        ID2D1SolidColorBrush* brush = nullptr;
        rt->CreateSolidColorBrush(color, &brush);
        if (brush) {
            rt->DrawText(wtext.c_str(), static_cast<UINT32>(wtext.length()),
                         format, snapped_rc, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            brush->Release();
        }
    }

    // 3. Modern Sleek Button (DESIGN.md: small rectangular buttons, dark surface, subtle border, purple active state)
    static void draw_button(ID2D1RenderTarget* rt, IDWriteTextFormat* format,
                            const D2D1_RECT_F& rc, const std::string& text,
                            bool active, D2D1_COLOR_F active_col, D2D1_COLOR_F normal_col,
                            float radius = 3.5f) {
        const auto& t = theme();
        D2D1_COLOR_F bg = active ? active_col : normal_col;
        D2D1_COLOR_F border = active ? t.accent_bright : t.border_subtle;
        D2D1_COLOR_F txt_col = active ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f) : t.text_primary;

        draw_rounded_box(rt, rc, bg, border, radius);
        draw_text(rt, format, text, rc, txt_col, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // 4. Modern Precision Mixer Fader (DESIGN.md: compact, dark slot, purple active fill, metallic thumb)
    static void draw_slider_vertical(ID2D1RenderTarget* rt, IDWriteTextFormat* font_main, IDWriteTextFormat* font_small,
                                     const D2D1_RECT_F& rc, float normalized_val, const std::string& label) {
        const auto& t = theme();
        draw_rounded_box(rt, rc, t.bg_control, t.border_subtle, 3.0f);

        // Center groove
        float cx = rc.left + (rc.right - rc.left) * 0.5f;
        float track_top = rc.top + 6.0f;
        float track_bottom = rc.bottom - 22.0f;
        float track_h = track_bottom - track_top;

        D2D1_RECT_F groove_rc = D2D1::RectF(cx - 2.0f, track_top, cx + 2.0f, track_bottom);
        draw_rounded_box(rt, groove_rc, t.bg_app, t.border_faint, 1.0f);

        // Active fill bar (Electric violet signature)
        float norm = std::clamp(normalized_val, 0.0f, 1.0f);
        float fill_h = track_h * norm;
        if (fill_h > 0.0f) {
            D2D1_RECT_F fill_rc = D2D1::RectF(cx - 2.0f, track_bottom - fill_h, cx + 2.0f, track_bottom);
            draw_rounded_box(rt, fill_rc, t.accent, t.accent, 1.0f);
        }

        // Metallic Glossy Precision Thumb Handle
        float thumb_y = track_bottom - fill_h;
        D2D1_RECT_F thumb_rc = D2D1::RectF(rc.left + 5.0f, thumb_y - 6.0f, rc.right - 5.0f, thumb_y + 6.0f);
        draw_rounded_box(rt, thumb_rc, D2D1::ColorF(0.18f, 0.20f, 0.24f, 1.0f), t.border_strong, 3.0f);

        // Center notch line
        D2D1_RECT_F notch_rc = D2D1::RectF(rc.left + 9.0f, thumb_y - 1.0f, rc.right - 9.0f, thumb_y + 1.0f);
        draw_rounded_box(rt, notch_rc, t.accent_bright, t.accent_bright, 0.5f);

        // Readout label at bottom
        D2D1_RECT_F lbl_rc = D2D1::RectF(rc.left - 6.0f, rc.bottom - 20.0f, rc.right + 6.0f, rc.bottom);
        draw_text(rt, font_small ? font_small : font_main, label, lbl_rc, t.text_secondary,
                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // 4b. Precision Horizontal Slider (Used for Filter & Synth parameters)
    static void draw_slider_horizontal(ID2D1RenderTarget* rt, IDWriteTextFormat* font,
                                       const D2D1_RECT_F& rc, float normalized_val, const std::string& label) {
        const auto& t = theme();
        draw_rounded_box(rt, rc, t.bg_control, t.border_subtle, 3.5f);

        float w = rc.right - rc.left - 2.0f;
        float norm = std::clamp(normalized_val, 0.0f, 1.0f);
        float fill_w = w * norm;
        if (fill_w > 0.0f) {
            D2D1_RECT_F fill_rc = D2D1::RectF(rc.left + 1.0f, rc.top + 1.0f, rc.left + 1.0f + fill_w, rc.bottom - 1.0f);
            draw_rounded_box(rt, fill_rc, t.accent_deep, t.accent, 2.5f);
        }

        draw_text(rt, font, label, rc, t.text_primary,
                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // 5. Linear Gradient Precision VU Meter Bar with Decay Animation
    static void draw_vu_meter(ID2D1RenderTarget* rt, const D2D1_RECT_F& rc, float peak_norm) {
        const auto& t = theme();
        draw_rounded_box(rt, rc, t.bg_app, t.border_subtle, 2.0f);

        float h = rc.bottom - rc.top - 2.0f;
        float norm = std::clamp(peak_norm, 0.0f, 1.0f);
        float meter_h = h * norm;

        if (meter_h > 1.0f) {
            D2D1_RECT_F fill_rc = D2D1::RectF(rc.left + 1.0f, rc.bottom - 1.0f - meter_h, rc.right - 1.0f, rc.bottom - 1.0f);

            // Create linear gradient brush (Success -> Warning -> Danger)
            ID2D1GradientStopCollection* stops = nullptr;
            D2D1_GRADIENT_STOP grad_stops[3];
            grad_stops[0].position = 0.0f; grad_stops[0].color = t.success;
            grad_stops[1].position = 0.70f; grad_stops[1].color = t.warning;
            grad_stops[2].position = 1.0f; grad_stops[2].color = t.danger;

            HRESULT hr = rt->CreateGradientStopCollection(grad_stops, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &stops);
            if (SUCCEEDED(hr) && stops) {
                ID2D1LinearGradientBrush* grad_brush = nullptr;
                D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES grad_props = D2D1::LinearGradientBrushProperties(
                    D2D1::Point2F(0, rc.bottom), D2D1::Point2F(0, rc.top));

                hr = rt->CreateLinearGradientBrush(grad_props, stops, &grad_brush);
                if (SUCCEEDED(hr) && grad_brush) {
                    rt->FillRectangle(fill_rc, grad_brush);
                    grad_brush->Release();
                }
                stops->Release();
            }
        }
    }

    // 5b. Linear Gradient Precision Dual Stereo VU Meter Bars (Left & Right) with Decay Animation
    static void draw_vu_meter_stereo(ID2D1RenderTarget* rt, const D2D1_RECT_F& rc, float peak_l, float peak_r) {
        const auto& t = theme();
        draw_rounded_box(rt, rc, t.bg_app, t.border_subtle, 2.0f);

        float total_w = rc.right - rc.left - 2.0f;
        float bar_w = std::max(2.0f, std::floor((total_w - 2.0f) * 0.5f));
        float h = rc.bottom - rc.top - 2.0f;

        float norm_l = std::clamp(peak_l, 0.0f, 1.0f);
        float norm_r = std::clamp(peak_r, 0.0f, 1.0f);
        float meter_h_l = h * norm_l;
        float meter_h_r = h * norm_r;

        D2D1_RECT_F fill_rc_l = D2D1::RectF(rc.left + 1.0f, rc.bottom - 1.0f - meter_h_l, rc.left + 1.0f + bar_w, rc.bottom - 1.0f);
        D2D1_RECT_F fill_rc_r = D2D1::RectF(rc.right - 1.0f - bar_w, rc.bottom - 1.0f - meter_h_r, rc.right - 1.0f, rc.bottom - 1.0f);

        ID2D1GradientStopCollection* stops = nullptr;
        D2D1_GRADIENT_STOP grad_stops[3];
        grad_stops[0].position = 0.0f; grad_stops[0].color = t.success;
        grad_stops[1].position = 0.70f; grad_stops[1].color = t.warning;
        grad_stops[2].position = 1.0f; grad_stops[2].color = t.danger;

        HRESULT hr = rt->CreateGradientStopCollection(grad_stops, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &stops);
        if (SUCCEEDED(hr) && stops) {
            ID2D1LinearGradientBrush* grad_brush = nullptr;
            D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES grad_props = D2D1::LinearGradientBrushProperties(
                D2D1::Point2F(0, rc.bottom), D2D1::Point2F(0, rc.top));

            hr = rt->CreateLinearGradientBrush(grad_props, stops, &grad_brush);
            if (SUCCEEDED(hr) && grad_brush) {
                if (meter_h_l > 1.0f) {
                    rt->FillRectangle(fill_rc_l, grad_brush);
                }
                if (meter_h_r > 1.0f) {
                    rt->FillRectangle(fill_rc_r, grad_brush);
                }
                grad_brush->Release();
            }
            stops->Release();
        }

        // Crisp 1px center divider between L and R
        ID2D1SolidColorBrush* br_div = nullptr;
        rt->CreateSolidColorBrush(t.border_subtle, &br_div);
        if (br_div) {
            float mid_x = std::floor((rc.left + rc.right) * 0.5f) + 0.5f;
            rt->DrawLine(D2D1::Point2F(mid_x, rc.top + 1.0f), D2D1::Point2F(mid_x, rc.bottom - 1.0f), br_div, 1.0f);
            br_div->Release();
        }
    }

    // 5c. Rotary Panning Knob with Center Notch, Range Arc, Needle, and Dynamic Readout
    static void draw_pan_knob(ID2D1RenderTarget* rt, IDWriteTextFormat* font_small,
                              const D2D1_RECT_F& rc, float pan_val, const std::string& label = "") {
        (void)label;
        const auto& t = theme();
        float cx = std::floor((rc.left + rc.right) * 0.5f);
        float cy = rc.top + 13.0f;
        float r = 11.0f;

        ID2D1SolidColorBrush* br_bg = nullptr;
        ID2D1SolidColorBrush* br_border = nullptr;
        ID2D1SolidColorBrush* br_accent = nullptr;
        ID2D1SolidColorBrush* br_needle = nullptr;
        ID2D1SolidColorBrush* br_dim = nullptr;

        rt->CreateSolidColorBrush(t.bg_control, &br_bg);
        rt->CreateSolidColorBrush(t.border_subtle, &br_border);
        rt->CreateSolidColorBrush(t.accent, &br_accent);
        rt->CreateSolidColorBrush(D2D1::ColorF(0.90f, 0.90f, 0.92f, 1.0f), &br_needle);
        rt->CreateSolidColorBrush(t.border_strong, &br_dim);

        // Circular knob disc
        D2D1_ELLIPSE disc = D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r);
        if (br_bg) rt->FillEllipse(disc, br_bg);
        if (br_border) rt->DrawEllipse(disc, br_border, 1.0f);

        // Center 12 o'clock tick mark (straight up = Center)
        if (br_dim) {
            rt->DrawLine(D2D1::Point2F(cx, cy - r - 2.0f), D2D1::Point2F(cx, cy - r + 1.0f), br_dim, 1.0f);
        }

        // Pointer Needle line: 0.0 pan = straight up (-90 deg)
        float clamped_pan = std::clamp(pan_val, -1.0f, 1.0f);
        float angle_deg = -90.0f + clamped_pan * 135.0f;
        float angle_rad = angle_deg * (3.141592653589793f / 180.0f);

        float nx = cx + (r - 2.5f) * std::cos(angle_rad);
        float ny = cy + (r - 2.5f) * std::sin(angle_rad);

        ID2D1SolidColorBrush* needle_brush = (std::abs(clamped_pan) < 0.02f) ? br_needle : br_accent;
        if (needle_brush) {
            rt->DrawLine(D2D1::Point2F(cx, cy), D2D1::Point2F(nx, ny), needle_brush, 1.5f);
        }

        // Dynamic Pan readout text below knob: C, L 45%, R 50%
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

        D2D1_RECT_F txt_rc = D2D1::RectF(rc.left, cy + r + 2.0f, rc.right, rc.bottom);
        D2D1_COLOR_F txt_col = (std::abs(clamped_pan) < 0.02f) ? t.text_secondary : t.accent;
        draw_text(rt, font_small, readout, txt_rc, txt_col,
                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        if (br_bg) br_bg->Release();
        if (br_border) br_border->Release();
        if (br_accent) br_accent->Release();
        if (br_needle) br_needle->Release();
        if (br_dim) br_dim->Release();
    }

    // 6. Anti-Aliased Vector Playhead Marker (Downward Triangle + 2px Line)
    static void draw_playhead(ID2D1RenderTarget* rt, float center_x, float top_y, float bottom_y,
                              D2D1_COLOR_F color, const std::string& label = "", IDWriteTextFormat* font = nullptr) {
        if (!rt) return;

        // Snap center_x to whole integer for symmetric crisp 2.0px stroke
        float snap_cx = std::round(center_x);

        // Downward-pointing vector triangle
        ID2D1PathGeometry* path = nullptr;
        ID2D1Factory* factory = nullptr;
        rt->GetFactory(&factory);
        if (factory) {
            factory->CreatePathGeometry(&path);
            if (path) {
                ID2D1GeometrySink* sink = nullptr;
                path->Open(&sink);
                if (sink) {
                    sink->BeginFigure(D2D1::Point2F(snap_cx - 9.0f, top_y), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(D2D1::Point2F(snap_cx + 9.0f, top_y));
                    sink->AddLine(D2D1::Point2F(snap_cx, top_y + 15.0f));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();
                    sink->Release();
                }

                ID2D1SolidColorBrush* br = nullptr;
                ID2D1SolidColorBrush* border_br = nullptr;
                rt->CreateSolidColorBrush(color, &br);
                rt->CreateSolidColorBrush(D2D1::ColorF(0.08f, 0.10f, 0.12f, 1.0f), &border_br);

                if (br) {
                    rt->FillGeometry(path, br);
                    if (border_br) rt->DrawGeometry(path, border_br, 1.0f);

                    // Vertical line descending all the way down through tracks
                    rt->DrawLine(D2D1::Point2F(snap_cx, top_y + 15.0f),
                                 D2D1::Point2F(snap_cx, bottom_y), br, 2.0f);

                    // Foot cap
                    rt->DrawLine(D2D1::Point2F(snap_cx - 4.0f, bottom_y),
                                 D2D1::Point2F(snap_cx + 4.0f, bottom_y), br, 2.0f);

                    br->Release();
                }
                if (border_br) border_br->Release();
                path->Release();
            }
            factory->Release();
        }

        // Optional badge above triangle
        if (!label.empty() && font) {
            D2D1_RECT_F lbl_rc = D2D1::RectF(snap_cx - 30.0f, top_y - 14.0f, snap_cx + 30.0f, top_y);
            draw_text(rt, font, label, lbl_rc, color, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
};

} // namespace digidaw::adapters::gui
