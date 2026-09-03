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
    D2D1_COLOR_F bg_dark       {0.063f, 0.078f, 0.098f, 1.0f}; // #101419
    D2D1_COLOR_F bg_panel      {0.086f, 0.106f, 0.133f, 1.0f}; // #161B22
    D2D1_COLOR_F bg_card       {0.130f, 0.157f, 0.200f, 1.0f}; // #212833
    D2D1_COLOR_F bg_input      {0.051f, 0.067f, 0.090f, 1.0f}; // #0D1117
    D2D1_COLOR_F bg_header     {0.165f, 0.212f, 0.282f, 1.0f}; // #2A3648

    D2D1_COLOR_F accent_cyan   {0.000f, 0.898f, 1.000f, 1.0f}; // #00E5FF
    D2D1_COLOR_F accent_orange {1.000f, 0.702f, 0.000f, 1.0f}; // #FFB300
    D2D1_COLOR_F accent_amber  {0.984f, 0.573f, 0.235f, 1.0f}; // #FB923C
    D2D1_COLOR_F accent_lime   {0.686f, 0.882f, 0.353f, 1.0f}; // #AFE15A (FL Studio Playhead)
    D2D1_COLOR_F accent_red    {0.937f, 0.267f, 0.267f, 1.0f}; // #EF4444
    D2D1_COLOR_F accent_green  {0.133f, 0.773f, 0.369f, 1.0f}; // #22C55E

    D2D1_COLOR_F text_primary  {0.945f, 0.961f, 0.976f, 1.0f}; // #F1F5F9
    D2D1_COLOR_F text_secondary{0.580f, 0.639f, 0.722f, 1.0f}; // #94A3B8
    D2D1_COLOR_F text_muted    {0.392f, 0.451f, 0.545f, 1.0f}; // #64748B

    D2D1_COLOR_F border_dark   {0.200f, 0.255f, 0.333f, 1.0f}; // #334155
    D2D1_COLOR_F border_light  {0.278f, 0.333f, 0.416f, 1.0f}; // #475569
    D2D1_COLOR_F border_faint  {0.149f, 0.188f, 0.247f, 1.0f}; // #26303F

    // Clips & Notes
    D2D1_COLOR_F clip_bg       {0.102f, 0.141f, 0.188f, 1.0f}; // #1A2430
    D2D1_COLOR_F clip_hdr      {0.165f, 0.220f, 0.294f, 1.0f}; // #2A384B
    D2D1_COLOR_F clip_border   {0.243f, 0.322f, 0.424f, 1.0f}; // #3E526C
    D2D1_COLOR_F note_silver   {0.831f, 0.871f, 0.910f, 1.0f}; // #D4DEE8
    D2D1_COLOR_F note_border   {0.961f, 0.980f, 1.000f, 1.0f}; // #F5FAFF
};

class D2DRenderer {
public:
    static const D2DTheme& theme() {
        static D2DTheme t;
        return t;
    }

    // 1. Smooth Anti-Aliased Rounded Box
    static void draw_rounded_box(ID2D1RenderTarget* rt, const D2D1_RECT_F& rc,
                                 D2D1_COLOR_F fill_color, D2D1_COLOR_F border_color,
                                 float radius = 4.0f, float stroke_width = 1.0f) {
        if (!rt) return;

        ID2D1SolidColorBrush* fill_brush = nullptr;
        ID2D1SolidColorBrush* border_brush = nullptr;
        rt->CreateSolidColorBrush(fill_color, &fill_brush);
        rt->CreateSolidColorBrush(border_color, &border_brush);

        D2D1_ROUNDED_RECT rrc = D2D1::RoundedRect(rc, radius, radius);

        if (fill_brush) {
            rt->FillRoundedRectangle(rrc, fill_brush);
            fill_brush->Release();
        }

        if (border_brush && stroke_width > 0.0f) {
            rt->DrawRoundedRectangle(rrc, border_brush, stroke_width);
            border_brush->Release();
        }
    }

    // 2. Hardware Anti-Aliased DirectWrite Text
    static void draw_text(ID2D1RenderTarget* rt, IDWriteTextFormat* format,
                          const std::string& text, const D2D1_RECT_F& rc,
                          D2D1_COLOR_F color,
                          DWRITE_TEXT_ALIGNMENT align_h = DWRITE_TEXT_ALIGNMENT_LEADING,
                          DWRITE_PARAGRAPH_ALIGNMENT align_v = DWRITE_PARAGRAPH_ALIGNMENT_CENTER) {
        if (!rt || !format || text.empty()) return;

        std::wstring wtext = to_wide(text);
        format->SetTextAlignment(align_h);
        format->SetParagraphAlignment(align_v);

        ID2D1SolidColorBrush* brush = nullptr;
        rt->CreateSolidColorBrush(color, &brush);
        if (brush) {
            rt->DrawText(wtext.c_str(), static_cast<UINT32>(wtext.length()),
                         format, rc, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
            brush->Release();
        }
    }

    // 3. Modern Sleek Button
    static void draw_button(ID2D1RenderTarget* rt, IDWriteTextFormat* format,
                            const D2D1_RECT_F& rc, const std::string& text,
                            bool active, D2D1_COLOR_F active_col, D2D1_COLOR_F normal_col,
                            float radius = 4.0f) {
        D2D1_COLOR_F bg = active ? active_col : normal_col;
        const auto& t = theme();
        D2D1_COLOR_F border = active ? active_col : t.border_dark;
        D2D1_COLOR_F txt_col = active ? D2D1::ColorF(0.05f, 0.07f, 0.09f, 1.0f) : t.text_primary;

        draw_rounded_box(rt, rc, bg, border, radius);
        draw_text(rt, format, text, rc, txt_col, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // 4. Modern Glowing Mixer Fader
    static void draw_slider_vertical(ID2D1RenderTarget* rt, IDWriteTextFormat* font_main, IDWriteTextFormat* font_small,
                                     const D2D1_RECT_F& rc, float normalized_val, const std::string& label) {
        const auto& t = theme();
        draw_rounded_box(rt, rc, t.bg_input, t.border_dark, 4.0f);

        // Center groove
        float cx = rc.left + (rc.right - rc.left) * 0.5f;
        float track_top = rc.top + 6.0f;
        float track_bottom = rc.bottom - 22.0f;
        float track_h = track_bottom - track_top;

        D2D1_RECT_F groove_rc = D2D1::RectF(cx - 2.0f, track_top, cx + 2.0f, track_bottom);
        draw_rounded_box(rt, groove_rc, D2D1::ColorF(0.04f, 0.05f, 0.07f, 1.0f), t.border_faint, 1.0f);

        // Active fill bar
        float norm = std::clamp(normalized_val, 0.0f, 1.0f);
        float fill_h = track_h * norm;
        if (fill_h > 0.0f) {
            D2D1_RECT_F fill_rc = D2D1::RectF(cx - 2.0f, track_bottom - fill_h, cx + 2.0f, track_bottom);
            draw_rounded_box(rt, fill_rc, t.accent_orange, t.accent_orange, 1.0f);
        }

        // Metallic Glossy Thumb Handle
        float thumb_y = track_bottom - fill_h;
        D2D1_RECT_F thumb_rc = D2D1::RectF(rc.left + 5.0f, thumb_y - 6.0f, rc.right - 5.0f, thumb_y + 6.0f);
        draw_rounded_box(rt, thumb_rc, D2D1::ColorF(0.85f, 0.88f, 0.92f, 1.0f), t.border_light, 3.0f);

        // Center notch line
        D2D1_RECT_F notch_rc = D2D1::RectF(rc.left + 9.0f, thumb_y - 1.0f, rc.right - 9.0f, thumb_y + 1.0f);
        draw_rounded_box(rt, notch_rc, D2D1::ColorF(0.25f, 0.30f, 0.35f, 1.0f), D2D1::ColorF(0.25f, 0.30f, 0.35f, 1.0f), 0.5f);

        // Readout label at bottom
        D2D1_RECT_F lbl_rc = D2D1::RectF(rc.left - 6.0f, rc.bottom - 20.0f, rc.right + 6.0f, rc.bottom);
        draw_text(rt, font_small ? font_small : font_main, label, lbl_rc, t.text_secondary,
                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // 5. Linear Gradient Neon VU Meter Bar with Decay Animation
    static void draw_vu_meter(ID2D1RenderTarget* rt, const D2D1_RECT_F& rc, float peak_norm) {
        const auto& t = theme();
        draw_rounded_box(rt, rc, t.bg_input, t.border_dark, 2.0f);

        float h = rc.bottom - rc.top - 2.0f;
        float norm = std::clamp(peak_norm, 0.0f, 1.0f);
        float meter_h = h * norm;

        if (meter_h > 1.0f) {
            D2D1_RECT_F fill_rc = D2D1::RectF(rc.left + 1.0f, rc.bottom - 1.0f - meter_h, rc.right - 1.0f, rc.bottom - 1.0f);

            // Create linear gradient brush (Green -> Amber -> Red)
            ID2D1GradientStopCollection* stops = nullptr;
            D2D1_GRADIENT_STOP grad_stops[3];
            grad_stops[0].position = 0.0f; grad_stops[0].color = t.accent_green;
            grad_stops[1].position = 0.7f; grad_stops[1].color = t.accent_orange;
            grad_stops[2].position = 1.0f; grad_stops[2].color = t.accent_red;

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

    // 6. Anti-Aliased Vector Playhead Marker (Downward Triangle + 2px Line)
    static void draw_playhead(ID2D1RenderTarget* rt, float center_x, float top_y, float bottom_y,
                              D2D1_COLOR_F color, const std::string& label = "", IDWriteTextFormat* font = nullptr) {
        if (!rt) return;

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
                    sink->BeginFigure(D2D1::Point2F(center_x - 9.0f, top_y), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(D2D1::Point2F(center_x + 9.0f, top_y));
                    sink->AddLine(D2D1::Point2F(center_x, top_y + 15.0f));
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
                    rt->DrawLine(D2D1::Point2F(center_x, top_y + 15.0f),
                                 D2D1::Point2F(center_x, bottom_y), br, 2.0f);

                    // Foot cap
                    rt->DrawLine(D2D1::Point2F(center_x - 4.0f, bottom_y),
                                 D2D1::Point2F(center_x + 4.0f, bottom_y), br, 2.0f);

                    br->Release();
                }
                if (border_br) border_br->Release();
                path->Release();
            }
            factory->Release();
        }

        // Optional badge above triangle
        if (!label.empty() && font) {
            D2D1_RECT_F lbl_rc = D2D1::RectF(center_x - 30.0f, top_y - 14.0f, center_x + 30.0f, top_y);
            draw_text(rt, font, label, lbl_rc, color, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
};

} // namespace digidaw::adapters::gui
