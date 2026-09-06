#pragma once

#include "../plugins/xaudio_devices.hpp"
#include "../plugins/xaudio_dsp.hpp"
#include "d2d_renderer.hpp"
#include "gui_renderer.hpp"
#include "theme.hpp"
#include "../../domain/devices/device.hpp"
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
#include <unordered_map>

namespace digidaw::adapters::gui {

class XAudioEditor {
public:
    // ========================================================================
    // Color Palette & Visual Constants (faithfully matched from XAudio)
    // ========================================================================

    // Plugin Kind Accents (exact hex codes from XAudio Source/Plugin.cpp line 9)
    // Kind 0 (X-Eq):         0xff52dfbd
    // Kind 1 (X-Compressor): 0xffffb866
    // Kind 2 (X-Multiband):  0xffaa9cff
    // Kind 3 (X-Reverb):     0xff70baff
    // Kind 4 (X-Distortion): 0xffff8576
    // Kind 5 (X-Limiter):    0xffd5ed78
    static inline D2D1_COLOR_F get_accent_d2d(int kind) {
        switch (kind) {
            case 0: return D2D1::ColorF(0x52 / 255.0f, 0xdf / 255.0f, 0xbd / 255.0f); // #52dfbd
            case 1: return D2D1::ColorF(0xff / 255.0f, 0xb8 / 255.0f, 0x66 / 255.0f); // #ffb866
            case 2: return D2D1::ColorF(0xaa / 255.0f, 0x9c / 255.0f, 0xff / 255.0f); // #aa9cff
            case 3: return D2D1::ColorF(0x70 / 255.0f, 0xba / 255.0f, 0xff / 255.0f); // #70baff
            case 4: return D2D1::ColorF(0xff / 255.0f, 0x85 / 255.0f, 0x76 / 255.0f); // #ff8576
            case 5: return D2D1::ColorF(0xd5 / 255.0f, 0xed / 255.0f, 0x78 / 255.0f); // #d5ed78
            default: return D2D1::ColorF(0x52 / 255.0f, 0xdf / 255.0f, 0xbd / 255.0f);
        }
    }

    static inline COLORREF get_accent_gdi(int kind) {
        switch (kind) {
            case 0: return RGB(0x52, 0xdf, 0xbd);
            case 1: return RGB(0xff, 0xb8, 0x66);
            case 2: return RGB(0xaa, 0x9c, 0xff);
            case 3: return RGB(0x70, 0xba, 0xff);
            case 4: return RGB(0xff, 0x85, 0x76);
            case 5: return RGB(0xd5, 0xed, 0x78);
            default: return RGB(0x52, 0xdf, 0xbd);
        }
    }

    // Band Colors (from XAudio Source/Plugin.cpp line 561)
    // 0: 0xffffba69 (orange), 1: 0xffe5db72 (yellow), 2: 0xff69d4ac (teal/green)
    // 3: 0xff70baff (blue),   4: 0xffad9aff (purple), 5: 0xffeb91b5 (pink)
    static inline D2D1_COLOR_F get_band_color_d2d(int b) {
        const D2D1_COLOR_F c[]{
            D2D1::ColorF(0xff / 255.f, 0xba / 255.f, 0x69 / 255.f),
            D2D1::ColorF(0xe5 / 255.f, 0xdb / 255.f, 0x72 / 255.f),
            D2D1::ColorF(0x69 / 255.f, 0xd4 / 255.f, 0xac / 255.f),
            D2D1::ColorF(0x70 / 255.f, 0xba / 255.f, 0xff / 255.f),
            D2D1::ColorF(0xad / 255.f, 0x9a / 255.f, 0xff / 255.f),
            D2D1::ColorF(0xeb / 255.f, 0x91 / 255.f, 0xb5 / 255.f)
        };
        return c[std::abs(b) % 6];
    }

    static inline COLORREF get_band_color_gdi(int b) {
        const COLORREF c[]{
            RGB(0xff, 0xba, 0x69),
            RGB(0xe5, 0xdb, 0x72),
            RGB(0x69, 0xd4, 0xac),
            RGB(0x70, 0xba, 0xff),
            RGB(0xad, 0x9a, 0xff),
            RGB(0xeb, 0x91, 0xb5)
        };
        return c[std::abs(b) % 6];
    }

    // ========================================================================
    // Parameter Display Formatting (from XAudio Plugin.cpp lines 338-351)
    // ========================================================================
    static std::string format_param_display(const std::string& unit, double v) {
        if (unit == "Hz") {
            if (v >= 1000.0) {
                std::ostringstream ss;
                ss << std::fixed << std::setprecision(1) << (v / 1000.0) << " kHz";
                return ss.str();
            } else {
                std::ostringstream ss;
                ss << std::fixed << std::setprecision(0) << v << " Hz";
                return ss.str();
            }
        }
        if (unit == "%") {
            return std::to_string(static_cast<int>(std::round(v))) + "%";
        }
        if (unit == ":1") {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1) << v << " : 1";
            return ss.str();
        }
        if (unit == "dB") {
            if (std::abs(v) < 0.05) return "0.0 dB";
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1) << v << " dB";
            return ss.str();
        }
        if (unit == "s") {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1) << v << " s";
            return ss.str();
        }
        if (unit == "ms") {
            std::ostringstream ss;
            if (v < 10.0) ss << std::fixed << std::setprecision(1) << v << " ms";
            else ss << std::fixed << std::setprecision(0) << v << " ms";
            return ss.str();
        }
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << v << (unit.empty() ? "" : (" " + unit));
        return ss.str();
    }

    static std::string format_1dec(double v) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << v;
        return ss.str();
    }

    // ========================================================================
    // Items Structure & Factory (from XAudio Plugin.cpp lines 455-557)
    // ========================================================================
    struct Item {
        std::string id;
        std::string title;
        std::string hint;
    };

    static std::vector<Item> get_items(int kind, bool detail, int selected) {
        std::vector<Item> res;
        auto add = [&](std::string id, std::string title, std::string hint) {
            res.push_back({std::move(id), std::move(title), std::move(hint)});
        };
        std::string prefix = kind == 0 ? "eq" + std::to_string(selected + 1)
                           : kind == 2 ? "mb" + std::to_string(selected + 1)
                                       : "";
        if (kind == 0) {
            add(prefix + "freq", "Frequency", "Set the center or cutoff frequency of the selected band.");
            add(prefix + "gain", "Gain", "Boost or cut the selected frequency range. Not used by low-pass or high-pass filters.");
            add(prefix + "q", "Q / Resonance", "Higher Q narrows the band. For shelves and pass filters, Q controls resonance.");
            add(prefix + "type", "Filter type", "Choose a bell, shelf, low-pass, or high-pass filter.");
        }
        if (kind == 1 || kind == 2) {
            if (kind == 2 && !detail) {
                const char* names[]{"Low threshold", "Low-mid threshold", "High-mid threshold", "High threshold"};
                for (int b = 0; b < 4; ++b) {
                    add("mb" + std::to_string(b + 1) + "threshold", names[b],
                        "Lower the threshold to apply compression to more of the signal.");
                }
            } else {
                add(prefix + "threshold", "Threshold", "Compression begins when the detector level exceeds the threshold.");
                add(prefix + "ratio", "Ratio", "Higher ratios apply stronger compression above the threshold.");
                add(prefix + "attack", "Attack", "Set how quickly compression responds. Longer times preserve more of the initial transient.");
                add(prefix + "release", "Release", "Set how quickly gain reduction recovers as the signal falls.");
                if (detail) {
                    add(prefix + "knee", "Knee", "Soften the transition into compression around the threshold.");
                    add(prefix + "makeup", "Makeup", "Adjust gain after compression.");
                    if (kind == 1) {
                        add("schp", "Sidechain HP", "Filter low frequencies from the detector signal only.");
                        add("external", "Sidechain", "Route audio to the sidechain input bus in your DAW.");
                    } else {
                        add(prefix + "solo", "Solo", "Listen to the selected band in isolation.");
                        add(prefix + "mute", "Mute", "Mute the selected band.");
                        for (int b = 1; b <= 3; ++b) {
                            add("cross" + std::to_string(b), "Crossover " + std::to_string(b),
                                "Set the crossover frequency between adjacent bands.");
                        }
                    }
                }
            }
        }
        if (kind == 3) {
            add("decay", "Decay", "Set the nominal reverb decay time (RT60).");
            add("predelay", "Pre-delay", "Set the delay before the reverb begins.");
            add("damping", "Damping", "Higher damping frequencies preserve more high-frequency energy.");
            add("width", "Width", "Adjust the stereo width of the reverb.");
            if (detail) {
                add("diffusion", "Diffusion", "Spread reflections for a denser reverb texture.");
                add("lowcut", "Low cut", "Remove low frequencies from the wet signal only.");
            }
        }
        if (kind == 4) {
            add("drive", "Drive", "Increase saturation and harmonic distortion.");
            add("tone", "Tone", "Low-pass filter after distortion. Lower values darken the sound.");
            add("bias", "Asymmetry", "Offset the shaping curve for asymmetric saturation. DC is filtered out.");
        }
        if (kind == 5) {
            add("threshold", "Threshold", "Lower the threshold to boost the signal into the limiter.");
            add("ceiling", "Ceiling", "Maximum sample-peak level while fully active. Not a true-peak limit.");
            add("release", "Release", "Set how quickly gain recovers after a peak. Short times can distort bass.");
        }
        if (detail) {
            add("input", "Input", "Adjust input gain before processing.");
        }
        return res;
    }

    static const char* get_description(int kind) {
        const char* d[] = {
            "6-BAND PARAMETRIC EQ",
            "STEREO COMPRESSOR",
            "4-BAND DYNAMICS",
            "STEREO REVERB",
            "SATURATION / DISTORTION",
            "SAMPLE-PEAK LIMITER"
        };
        return (kind >= 0 && kind < 6) ? d[kind] : "AUDIO EFFECT";
    }

    static const char* get_graph_title(int kind) {
        const char* t[] = {
            "EQ RESPONSE",
            "TRANSFER CURVE",
            "GAIN REDUCTION",
            "DECAY ENVELOPE (SCHEMATIC)",
            "SHAPING CURVE (BEFORE TONE / DC FILTER)",
            "LIMITING CURVE / ZERO LATENCY"
        };
        return (kind >= 0 && kind < 6) ? t[kind] : "AUDIO RESPONSE";
    }

    static std::vector<std::string> get_preset_labels(int kind) {
        switch (kind) {
            case 0: return {"Default / Reset", "Vocal Clarity", "Gentle Polish"};
            case 1: return {"Default / Reset", "Vocal Leveling", "Mix Glue"};
            case 2: return {"Default / Reset", "Gentle Control", "Firm Control"};
            case 3: return {"Default / Reset", "Small Room", "Large Hall"};
            case 4: return {"Default / Reset", "Warm Saturation", "Heavy Drive"};
            case 5: return {"Default / Reset", "Gentle Peaks", "Loud and Tight"};
            default: return {"Default / Reset", "Preset 1", "Preset 2"};
        }
    }

    static int find_param_idx(domain::IDevice* dev, const std::string& id) {
        auto* xfx = dynamic_cast<plugins::XAudioEffectDevice*>(dev);
        if (!xfx) return -1;
        const auto& spec = xfx->spec();
        for (size_t i = 0; i < spec.size(); ++i) {
            if (spec[i].id == id) return static_cast<int>(i);
        }
        return -1;
    }

    // ========================================================================
    // UI State Persistence per Device Instance
    // ========================================================================
    struct PluginUiState {
        bool detail{false};
        int selected_band{0};
        uint32_t last_click_ms{0};
        int last_click_item{-1};
        int drag_start_x{0};
        int drag_start_y{0};
        float drag_orig_val{0.0f};
    };

    static inline std::unordered_map<void*, PluginUiState> ui_states_;

    static inline PluginUiState& get_state(domain::IDevice* dev) {
        auto it = ui_states_.find(dev);
        if (it == ui_states_.end()) {
            PluginUiState s;
            auto* xfx = dynamic_cast<plugins::XAudioEffectDevice*>(dev);
            int k = xfx ? xfx->kind() : 0;
            s.selected_band = (k == 0 ? 2 : 0); // EQ defaults to Band 3 (idx 2: Low mid), Multiband to Low (idx 0)
            ui_states_[dev] = s;
            return ui_states_[dev];
        }
        return it->second;
    }

    // ========================================================================
    // Rotary Knob Drawing in Direct2D (Authentic JUCE LookAndFeel_V4 Arc Gauge)
    // ========================================================================
    static void draw_knob_d2d(ID2D1RenderTarget* rt, IDWriteTextFormat* font_bold, IDWriteTextFormat* font_small,
                              const D2D1_RECT_F& rc, float norm, const std::string& title,
                              const std::string& val_str, D2D1_COLOR_F accent, bool disabled = false) {
        if (!rt) return;
        float cx = (rc.left + rc.right) * 0.5f;
        float cy = rc.top + 26.0f + (rc.bottom - rc.top - 52.0f) * 0.5f;
        float max_r = std::min((rc.right - rc.left) * 0.5f - 14.0f, (rc.bottom - rc.top - 52.0f) * 0.5f - 4.0f);
        float r = std::clamp(max_r, 18.0f, 32.0f);

        // Title above (centered, bold, #e5ecef)
        D2D1_RECT_F title_rc = D2D1::RectF(rc.left, rc.top + 4.0f, rc.right, rc.top + 24.0f);
        D2DRenderer::draw_text(rt, font_bold, title, title_rc, disabled ? D2D1::ColorF(0.38f, 0.40f, 0.43f) : D2D1::ColorF(0.90f, 0.93f, 0.94f),
                               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Arc constants matching JUCE LookAndFeel_V4
        // Arc starts at ~7 o'clock (140 deg = 2.44346 rad) and sweeps 260 deg (4.53786 rad) clockwise to ~5 o'clock (400 deg)
        const float start_ang = 2.44346f;
        const float sweep_ang = 4.53786f;
        const float end_ang = start_ang + sweep_ang;
        float val_norm = std::clamp(norm, 0.0f, 1.0f);
        float cur_ang = start_ang + val_norm * sweep_ang;

        ID2D1Factory* factory = nullptr;
        rt->GetFactory(&factory);

        ID2D1SolidColorBrush* br_track = nullptr;
        ID2D1SolidColorBrush* br_accent = nullptr;
        rt->CreateSolidColorBrush(disabled ? D2D1::ColorF(0x26 / 255.f, 0x30 / 255.f, 0x3a / 255.f) : D2D1::ColorF(0x30 / 255.f, 0x3d / 255.f, 0x49 / 255.f), &br_track);
        rt->CreateSolidColorBrush(disabled ? D2D1::ColorF(0x44 / 255.f, 0x4d / 255.f, 0x58 / 255.f) : accent, &br_accent);

        if (factory && br_track && br_accent) {
            ID2D1StrokeStyle* stroke_round = nullptr;
            D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(
                D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND
            );
            factory->CreateStrokeStyle(sp, nullptr, 0, &stroke_round);

            // 1. Background Track Arc (260 deg > 180 deg, so D2D1_ARC_SIZE_LARGE)
            ID2D1PathGeometry* track_geo = nullptr;
            factory->CreatePathGeometry(&track_geo);
            if (track_geo) {
                ID2D1GeometrySink* sink = nullptr;
                track_geo->Open(&sink);
                if (sink) {
                    float sx = cx + r * std::cos(start_ang);
                    float sy = cy + r * std::sin(start_ang);
                    float ex = cx + r * std::cos(end_ang);
                    float ey = cy + r * std::sin(end_ang);
                    sink->BeginFigure(D2D1::Point2F(sx, sy), D2D1_FIGURE_BEGIN_HOLLOW);
                    sink->AddArc(D2D1::ArcSegment(
                        D2D1::Point2F(ex, ey),
                        D2D1::SizeF(r, r),
                        0.0f,
                        D2D1_SWEEP_DIRECTION_CLOCKWISE,
                        D2D1_ARC_SIZE_LARGE
                    ));
                    sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    sink->Close();
                    sink->Release();
                }
                if (stroke_round) {
                    rt->DrawGeometry(track_geo, br_track, 4.5f, stroke_round);
                }
                track_geo->Release();
            }

            // 2. Active Value Arc
            if (val_norm > 0.005f && !disabled) {
                ID2D1PathGeometry* val_geo = nullptr;
                factory->CreatePathGeometry(&val_geo);
                if (val_geo) {
                    ID2D1GeometrySink* sink = nullptr;
                    val_geo->Open(&sink);
                    if (sink) {
                        float sx = cx + r * std::cos(start_ang);
                        float sy = cy + r * std::sin(start_ang);
                        float ex = cx + r * std::cos(cur_ang);
                        float ey = cy + r * std::sin(cur_ang);
                        sink->BeginFigure(D2D1::Point2F(sx, sy), D2D1_FIGURE_BEGIN_HOLLOW);
                        sink->AddArc(D2D1::ArcSegment(
                            D2D1::Point2F(ex, ey),
                            D2D1::SizeF(r, r),
                            0.0f,
                            D2D1_SWEEP_DIRECTION_CLOCKWISE,
                            (val_norm * sweep_ang > 3.14159265f) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL
                        ));
                        sink->EndFigure(D2D1_FIGURE_END_OPEN);
                        sink->Close();
                        sink->Release();
                    }
                    if (stroke_round) {
                        rt->DrawGeometry(val_geo, br_accent, 4.5f, stroke_round);
                    }
                    val_geo->Release();
                }

                // 3. Round Thumb Pip at the Tip
                float tx = cx + r * std::cos(cur_ang);
                float ty = cy + r * std::sin(cur_ang);
                rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(tx, ty), 4.5f, 4.5f), br_accent);
            }

            if (stroke_round) stroke_round->Release();
        }

        if (br_track) br_track->Release();
        if (br_accent) br_accent->Release();
        if (factory) factory->Release();

        // Value text below (centered, #abb3c0)
        D2D1_RECT_F val_rc = D2D1::RectF(rc.left, rc.bottom - 24.0f, rc.right, rc.bottom - 4.0f);
        D2DRenderer::draw_text(rt, font_small, disabled ? "(Inactive)" : val_str, val_rc,
                               disabled ? D2D1::ColorF(0.38f, 0.40f, 0.43f) : D2D1::ColorF(0.67f, 0.70f, 0.75f),
                               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // ========================================================================
    // Rotary Knob Drawing in GDI (Authentic JUCE LookAndFeel_V4 Arc Gauge)
    // ========================================================================
    static void draw_knob_gdi(HDC hdc, HFONT font_bold, HFONT font_small,
                             const RECT& rc, float norm, const std::string& title,
                             const std::string& val_str, COLORREF accent, bool disabled = false) {
        if (!hdc) return;
        int cx = static_cast<int>((rc.left + rc.right) / 2);
        int cy = static_cast<int>(rc.top + 26 + (rc.bottom - rc.top - 52) / 2);
        int max_r = static_cast<int>(std::min((rc.right - rc.left) / 2 - 14, (rc.bottom - rc.top - 52) / 2 - 4));
        int r = std::clamp(max_r, 18, 32);

        // Title
        SelectObject(hdc, font_bold);
        RECT title_rc{rc.left, rc.top + 4, rc.right, rc.top + 24};
        GuiRenderer::draw_text(hdc, title, title_rc, disabled ? RGB(98, 102, 109) : RGB(229, 236, 239), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        const float start_ang = 2.44346f;
        const float sweep_ang = 4.53786f;
        float val_norm = std::clamp(norm, 0.0f, 1.0f);

        LOGBRUSH lb_track{BS_SOLID, disabled ? RGB(38, 48, 58) : RGB(48, 61, 73), 0};
        HPEN pen_track = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND, 4, &lb_track, 0, NULL);
        LOGBRUSH lb_accent{BS_SOLID, disabled ? RGB(68, 75, 88) : accent, 0};
        HPEN pen_accent = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND, 4, &lb_accent, 0, NULL);

        // Background track polyline
        const int segs = 32;
        POINT track_pts[segs + 1];
        for (int i = 0; i <= segs; ++i) {
            float a = start_ang + (static_cast<float>(i) / segs) * sweep_ang;
            track_pts[i].x = cx + static_cast<int>(std::round(r * std::cos(a)));
            track_pts[i].y = cy + static_cast<int>(std::round(r * std::sin(a)));
        }
        HGDIOBJ old_pen = SelectObject(hdc, pen_track ? pen_track : GetStockObject(BLACK_PEN));
        Polyline(hdc, track_pts, segs + 1);

        // Active value polyline
        if (val_norm > 0.01f && !disabled && pen_accent) {
            int v_segs = std::max(2, static_cast<int>(std::round(segs * val_norm)));
            std::vector<POINT> val_pts(v_segs + 1);
            for (int i = 0; i <= v_segs; ++i) {
                float a = start_ang + (static_cast<float>(i) / v_segs) * (val_norm * sweep_ang);
                val_pts[i].x = cx + static_cast<int>(std::round(r * std::cos(a)));
                val_pts[i].y = cy + static_cast<int>(std::round(r * std::sin(a)));
            }
            SelectObject(hdc, pen_accent);
            Polyline(hdc, val_pts.data(), v_segs + 1);

            // Thumb dot
            float cur_ang = start_ang + val_norm * sweep_ang;
            int tx = cx + static_cast<int>(std::round(r * std::cos(cur_ang)));
            int ty = cy + static_cast<int>(std::round(r * std::sin(cur_ang)));
            HBRUSH br_dot = CreateSolidBrush(accent);
            HGDIOBJ old_br = SelectObject(hdc, br_dot);
            SelectObject(hdc, GetStockObject(NULL_PEN));
            Ellipse(hdc, tx - 4, ty - 4, tx + 5, ty + 5);
            SelectObject(hdc, old_br);
            DeleteObject(br_dot);
        }

        SelectObject(hdc, old_pen);
        if (pen_track) DeleteObject(pen_track);
        if (pen_accent) DeleteObject(pen_accent);

        // Value text
        SelectObject(hdc, font_small);
        RECT val_rc{rc.left, rc.bottom - 24, rc.right, rc.bottom - 4};
        GuiRenderer::draw_text(hdc, disabled ? "(Inactive)" : val_str, val_rc,
                               disabled ? RGB(98, 102, 109) : RGB(171, 179, 192), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // ========================================================================
    // Direct2D Rendering for Effect Plugins (Full XAudio Suite Fidelity)
    // ========================================================================
    static void render_effect_d2d(ID2D1RenderTarget* rt, IDWriteTextFormat* font_bold, IDWriteTextFormat* font_small,
                                 domain::IDevice* dev, const RECT& bounds,
                                 bool enabled, float wet_mix, int /*track_id*/, int /*slot_idx*/, int /*scroll_offset*/ = 0) {
        if (!rt || !dev) return;
        auto* xfx = dynamic_cast<plugins::XAudioEffectDevice*>(dev);
        int kind = xfx ? xfx->kind() : 0;
        auto& state = get_state(dev);

        float mx = static_cast<float>(bounds.left);
        float my = static_cast<float>(bounds.top);
        float mw = static_cast<float>(bounds.right - bounds.left);
        float mh = static_cast<float>(bounds.bottom - bounds.top);

        D2D1_COLOR_F accent = get_accent_d2d(kind);

        // 1. Modal Dialog Box Backdrop (XAudio 0xff14171c)
        D2D1_RECT_F dlg_rc = D2D1::RectF(mx, my, mx + mw, my + mh);
        D2DRenderer::draw_rounded_box(rt, dlg_rc, D2D1::ColorF(0x14 / 255.f, 0x17 / 255.f, 0x1c / 255.f),
                                     D2D1::ColorF(0x2a / 255.f, 0x32 / 255.f, 0x3d / 255.f), 6.0f);

        // 2. Header Bar (XAudio 0xff20242b, height 66px)
        D2D1_RECT_F hdr_rc = D2D1::RectF(mx, my, mx + mw, my + 66.0f);
        D2DRenderer::draw_rounded_box(rt, hdr_rc, D2D1::ColorF(0x20 / 255.f, 0x24 / 255.f, 0x2b / 255.f),
                                     D2D1::ColorF(0x28 / 255.f, 0x2e / 255.f, 0x38 / 255.f), 6.0f);

        // Title: 25pt Bold
        std::string title = dev->name();
        D2D1_RECT_F title_rc = D2D1::RectF(mx + 24.0f, my + 16.0f, mx + 240.0f, my + 50.0f);
        D2DRenderer::draw_text(rt, font_bold, title, title_rc, accent);

        // Subtitle / Description
        D2D1_RECT_F desc_rc = D2D1::RectF(mx + 250.0f, my + 20.0f, mx + 520.0f, my + 46.0f);
        D2DRenderer::draw_text(rt, font_small, get_description(kind), desc_rc, D2D1::ColorF(0xab / 255.f, 0xb3 / 255.f, 0xc0 / 255.f));

        // Preset Dropdown Box: [mx + mw - 410, my + 17, mx + mw - 180, my + 49]
        int active_p = xfx ? xfx->active_preset() : 0;
        auto p_labels = get_preset_labels(kind);
        std::string cur_preset = (active_p < static_cast<int>(p_labels.size())) ? p_labels[active_p] : "Factory presets";

        D2D1_RECT_F preset_rc = D2D1::RectF(mx + mw - 410.0f, my + 17.0f, mx + mw - 180.0f, my + 49.0f);
        D2DRenderer::draw_rounded_box(rt, preset_rc, D2D1::ColorF(0x24 / 255.f, 0x32 / 255.f, 0x3e / 255.f),
                                     D2D1::ColorF(0x38 / 255.f, 0x46 / 255.f, 0x53 / 255.f), 4.0f);
        D2D1_RECT_F preset_txt_rc = D2D1::RectF(preset_rc.left + 10.0f, preset_rc.top + 6.0f, preset_rc.right - 26.0f, preset_rc.bottom - 6.0f);
        D2DRenderer::draw_text(rt, font_small, "Preset: " + cur_preset, preset_txt_rc, D2D1::ColorF(0xe5 / 255.f, 0xec / 255.f, 0xef / 255.f));
        D2D1_RECT_F arrow_rc = D2D1::RectF(preset_rc.right - 22.0f, preset_rc.top + 6.0f, preset_rc.right - 6.0f, preset_rc.bottom - 6.0f);
        D2DRenderer::draw_text(rt, font_bold, "▼", arrow_rc, accent);

        // Bypass Button: [mx + mw - 170, my + 17, mx + mw - 50, my + 49]
        D2D1_RECT_F byp_rc = D2D1::RectF(mx + mw - 170.0f, my + 17.0f, mx + mw - 50.0f, my + 49.0f);
        D2D1_COLOR_F byp_bg = (!enabled) ? D2D1::ColorF(accent.r * 0.4f, accent.g * 0.4f, accent.b * 0.4f)
                                         : D2D1::ColorF(0x20 / 255.f, 0x2e / 255.f, 0x39 / 255.f);
        D2D1_COLOR_F byp_txt = (!enabled) ? accent : D2D1::ColorF(0xe5 / 255.f, 0xec / 255.f, 0xef / 255.f);
        D2DRenderer::draw_button(rt, font_bold, byp_rc, enabled ? "ACTIVE" : "BYPASS", !enabled, byp_txt, byp_bg, 4.0f);

        // Close Button [✕]: [mx + mw - 42, my + 17, mx + mw - 12, my + 49]
        D2D1_RECT_F close_rc = D2D1::RectF(mx + mw - 42.0f, my + 17.0f, mx + mw - 12.0f, my + 49.0f);
        D2DRenderer::draw_button(rt, font_bold, close_rc, "✕", false, D2D1::ColorF(0xff / 255.f, 0x72 / 255.f, 0x6b / 255.f),
                                D2D1::ColorF(0x20 / 255.f, 0x2e / 255.f, 0x39 / 255.f), 4.0f);

        // 3. Graph Display Area
        float ctrl_top = my + mh - (state.detail ? 350.0f : 260.0f);
        D2D1_RECT_F p = D2D1::RectF(mx + 64.0f, my + 112.0f, mx + mw - 126.0f, ctrl_top - 40.0f);

        // Graph backdrop: p.expanded(40, 36) in 0xff101318
        D2D1_RECT_F graph_bg_rc = D2D1::RectF(p.left - 40.0f, p.top - 36.0f, p.right + 40.0f, p.bottom + 36.0f);
        D2DRenderer::draw_rounded_box(rt, graph_bg_rc, D2D1::ColorF(0x10 / 255.f, 0x13 / 255.f, 0x18 / 255.f),
                                     D2D1::ColorF(0x20 / 255.f, 0x24 / 255.f, 0x2b / 255.f), 6.0f);

        // Graph Title at (mx + 32, my + 76)
        D2D1_RECT_F gr_title_rc = D2D1::RectF(mx + 32.0f, my + 76.0f, mx + 432.0f, my + 98.0f);
        D2DRenderer::draw_text(rt, font_small, get_graph_title(kind), gr_title_rc, D2D1::ColorF(0xab / 255.f, 0xb3 / 255.f, 0xc0 / 255.f));

        // GR readout on header (Kind 1 and 5)
        if (kind == 1 || kind == 5) {
            float gr_val = xfx ? xfx->gain_reduction_db(0) : 0.0f;
            std::string gr_str = "GR  " + format_1dec(gr_val) + " dB";
            D2D1_RECT_F gr_hdr_rc = D2D1::RectF(p.right - 170.0f, my + 76.0f, p.right, my + 98.0f);
            D2DRenderer::draw_text(rt, font_bold, gr_str, gr_hdr_rc, accent);
        }

        // Horizontal Grid Lines & Y-Axis Labels
        ID2D1SolidColorBrush* br_grid_norm = nullptr;
        ID2D1SolidColorBrush* br_grid_zero = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(0x29 / 255.f, 0x2e / 255.f, 0x38 / 255.f), &br_grid_norm);
        rt->CreateSolidColorBrush(D2D1::ColorF(0x44 / 255.f, 0x4b / 255.f, 0x58 / 255.f), &br_grid_zero);

        for (int i = 0; i <= 4; ++i) {
            float y = p.top + (p.bottom - p.top) * (static_cast<float>(i) / 4.0f);
            auto* br = (i == 2) ? br_grid_zero : br_grid_norm;
            if (br) rt->DrawLine(D2D1::Point2F(p.left, y), D2D1::Point2F(p.right, y), br, 1.0f);

            // Y label
            int db = (kind == 0 ? 24 - i * 12 : (kind == 3 ? -i * 20 : -i * 15));
            std::string y_str = (kind == 4 ? format_1dec(1.5 - i * 0.75) : std::to_string(db));
            if (kind != 2) {
                D2D1_RECT_F y_rc = D2D1::RectF(mx + 20.0f, y - 8.0f, mx + 58.0f, y + 8.0f);
                D2DRenderer::draw_text(rt, font_small, y_str, y_rc, D2D1::ColorF(0x92 / 255.f, 0x9c / 255.f, 0xac / 255.f));
            }
        }

        // Vertical Grid Lines & X-Axis Labels
        if (kind == 0) { // EQ frequency grid
            for (float f : {20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f}) {
                float x = p.left + static_cast<float>(std::log(f / 20.0) / std::log(1000.0)) * (p.right - p.left);
                if (br_grid_norm) rt->DrawLine(D2D1::Point2F(x, p.top), D2D1::Point2F(x, p.bottom), br_grid_norm, 1.0f);
                std::string f_str = (f >= 1000.0f ? std::to_string(static_cast<int>(f / 1000.0f)) + "k" : std::to_string(static_cast<int>(f)));
                D2D1_RECT_F f_rc = D2D1::RectF(x - 22.0f, p.bottom + 9.0f, x + 22.0f, p.bottom + 27.0f);
                D2DRenderer::draw_text(rt, font_small, f_str, f_rc, D2D1::ColorF(0x92 / 255.f, 0x9c / 255.f, 0xac / 255.f));
            }
        } else if (kind != 2) {
            for (int i = 0; i <= 4; ++i) {
                float x = p.left + (p.right - p.left) * (static_cast<float>(i) / 4.0f);
                if (br_grid_norm) rt->DrawLine(D2D1::Point2F(x, p.top), D2D1::Point2F(x, p.bottom), br_grid_norm, 1.0f);
                std::string x_str = (kind == 4 ? format_1dec(-1.0 + i * 0.5)
                                    : kind == 3 ? std::to_string(i * 3) + " s"
                                                : std::to_string(-60 + i * 15) + " dB");
                D2D1_RECT_F x_rc = D2D1::RectF(x - 26.0f, p.bottom + 9.0f, x + 26.0f, p.bottom + 27.0f);
                D2DRenderer::draw_text(rt, font_small, x_str, x_rc, D2D1::ColorF(0x92 / 255.f, 0x9c / 255.f, 0xac / 255.f));
            }
        }

        if (br_grid_norm) br_grid_norm->Release();
        if (br_grid_zero) br_grid_zero->Release();

        // Specific Curves per Plugin Kind
        if (kind == 0) {
            auto* eq = dynamic_cast<plugins::XEqDevice*>(dev);
            if (eq) {
                render_eq_curve_d2d(rt, p, eq, state.selected_band, font_bold);
            }
        } else if (kind == 1) {
            render_comp_curve_d2d(rt, p, xfx, accent);
        } else if (kind == 2) {
            render_multiband_graph_d2d(rt, p, xfx, font_bold, font_small, accent);
        } else if (kind == 3) {
            render_reverb_curve_d2d(rt, p, xfx, accent);
        } else if (kind == 4) {
            render_distortion_curve_d2d(rt, p, xfx, accent);
        } else if (kind == 5) {
            render_limiter_curve_d2d(rt, p, xfx, accent);
        }

        // Live signal waveform overlay (input dim, output accent-colored)
        render_signal_waveform_d2d(rt, p, xfx, accent, font_small);

        // Peak Meters (IN and OUT) on the right
        float m_in = xfx ? xfx->meter_in() : 0.0f;
        float m_out = xfx ? xfx->meter_out() : 0.0f;
        render_peak_meters_d2d(rt, mx, mw, p, m_in, m_out, accent, font_small);

        // 4. Middle Controls Bar (Band selection tabs + More controls button)
        if (kind == 0 || kind == 2) {
            render_bands_bar_d2d(rt, mx, mw, ctrl_top, kind, state.selected_band, font_bold);
        } else {
            D2D1_RECT_F proc_lbl = D2D1::RectF(mx + 24.0f, ctrl_top + 4.0f, mx + 240.0f, ctrl_top + 28.0f);
            D2DRenderer::draw_text(rt, font_bold, "PROCESSING", proc_lbl, D2D1::ColorF(0xab / 255.f, 0xb3 / 255.f, 0xc0 / 255.f));
        }

        // "More controls" / "Less controls" button
        D2D1_RECT_F detail_rc = D2D1::RectF(mx + mw - 174.0f, ctrl_top, mx + mw - 24.0f, ctrl_top + 30.0f);
        D2DRenderer::draw_button(rt, font_bold, detail_rc, state.detail ? "Less controls" : "More controls",
                                state.detail, D2D1::ColorF(0xe5 / 255.f, 0xec / 255.f, 0xef / 255.f),
                                D2D1::ColorF(0x20 / 255.f, 0x2e / 255.f, 0x39 / 255.f), 4.0f);

        // 5. Parameter Cards Grid
        auto items = get_items(kind, state.detail, state.selected_band);
        int cols = state.detail ? 4 : std::max(1, static_cast<int>(items.size()));
        int rows = (static_cast<int>(items.size()) + cols - 1) / cols;
        float card_w = (mw - 48.0f - 8.0f * (cols - 1)) / static_cast<float>(cols);
        float card_h = (mh - 102.0f - (ctrl_top - my) - 42.0f - 8.0f * (rows - 1)) / static_cast<float>(rows);

        for (size_t i = 0; i < items.size(); ++i) {
            const auto& item = items[i];
            int c_col = static_cast<int>(i) % cols;
            int c_row = static_cast<int>(i) / cols;
            float bx = mx + 24.0f + static_cast<float>(c_col) * (card_w + 8.0f);
            float by = ctrl_top + 42.0f + static_cast<float>(c_row) * (card_h + 8.0f);
            D2D1_RECT_F card_rc = D2D1::RectF(bx, by, bx + card_w, by + card_h);

            // Card box
            D2DRenderer::draw_rounded_box(rt, card_rc, D2D1::ColorF(0x20 / 255.f, 0x24 / 255.f, 0x2b / 255.f),
                                         D2D1::ColorF(0x2a / 255.f, 0x32 / 255.f, 0x3d / 255.f), 6.0f);

            int p_idx = find_param_idx(dev, item.id);
            if (p_idx < 0) continue;
            const auto& spec = xfx->spec()[p_idx];
            double plain_val = dev ? xfx->get_plain(p_idx) : spec.initial;
            float norm_val = dev ? dev->get_parameter(p_idx) : 0.5f;

            // If Choice (Filter Type)
            if (item.id.find("type") != std::string::npos) {
                D2D1_RECT_F title_rc = D2D1::RectF(card_rc.left, card_rc.top + 6.0f, card_rc.right, card_rc.top + 26.0f);
                D2DRenderer::draw_text(rt, font_bold, item.title, title_rc, D2D1::ColorF(0xe5 / 255.f, 0xec / 255.f, 0xef / 255.f));

                int t_idx = std::clamp(static_cast<int>(std::round(plain_val)), 0, 4);
                const char* types[] = {"Bell", "Low shelf", "High shelf", "Low pass", "High pass"};
                D2D1_RECT_F btn_choice = D2D1::RectF(card_rc.left + 12.0f, card_rc.top + (card_h - 32.0f) * 0.5f,
                                                     card_rc.right - 12.0f, card_rc.top + (card_h + 32.0f) * 0.5f);
                D2DRenderer::draw_button(rt, font_bold, btn_choice, types[t_idx], true, accent,
                                        D2D1::ColorF(0x24 / 255.f, 0x32 / 255.f, 0x3e / 255.f), 4.0f);
            }
            // If Toggle Button (Solo, Mute, External)
            else if (item.id == "external" || item.id.find("solo") != std::string::npos || item.id.find("mute") != std::string::npos) {
                D2D1_RECT_F title_rc = D2D1::RectF(card_rc.left, card_rc.top + 6.0f, card_rc.right, card_rc.top + 26.0f);
                D2DRenderer::draw_text(rt, font_bold, item.title, title_rc, D2D1::ColorF(0xe5 / 255.f, 0xec / 255.f, 0xef / 255.f));

                bool is_active = (plain_val > 0.5);
                D2D1_RECT_F btn_tog = D2D1::RectF(card_rc.left + 12.0f, card_rc.top + (card_h - 32.0f) * 0.5f,
                                                  card_rc.right - 12.0f, card_rc.top + (card_h + 32.0f) * 0.5f);
                D2D1_COLOR_F bg_act = (item.id.find("mute") != std::string::npos)
                                      ? D2D1::ColorF(0xff / 255.f, 0x72 / 255.f, 0x6b / 255.f)
                                      : accent;
                D2DRenderer::draw_button(rt, font_bold, btn_tog, is_active ? "ON" : "OFF", is_active,
                                        bg_act, D2D1::ColorF(0x20 / 255.f, 0x2e / 255.f, 0x39 / 255.f), 4.0f);
            }
            // Otherwise, Slider / Knob
            else {
                bool is_gain_disabled = false;
                if (kind == 0 && item.id.find("gain") != std::string::npos) {
                    std::string pref = "eq" + std::to_string(state.selected_band + 1) + "type";
                    int t_id = find_param_idx(dev, pref);
                    if (t_id >= 0 && xfx->get_plain(t_id) >= 3.0) is_gain_disabled = true;
                }

                std::string val_txt = format_param_display(spec.unit, plain_val);

                if (state.detail) {
                    // Detail Mode: Compact Horizontal Slider
                    D2D1_RECT_F title_rc = D2D1::RectF(card_rc.left + 8.0f, card_rc.top + 4.0f, card_rc.right - 8.0f, card_rc.top + 22.0f);
                    D2DRenderer::draw_text(rt, font_bold, item.title, title_rc, D2D1::ColorF(0xe5 / 255.f, 0xec / 255.f, 0xef / 255.f));

                    float tr_left = card_rc.left + 12.0f;
                    float tr_right = card_rc.right - 12.0f;
                    float tr_y = card_rc.top + (card_h - 10.0f) * 0.5f;
                    D2D1_RECT_F tr_rc = D2D1::RectF(tr_left, tr_y, tr_right, tr_y + 8.0f);
                    D2DRenderer::draw_rounded_box(rt, tr_rc, D2D1::ColorF(0x30 / 255.f, 0x3d / 255.f, 0x49 / 255.f),
                                                 D2D1::ColorF(0x20 / 255.f, 0x24 / 255.f, 0x2b / 255.f), 3.0f);

                    float fill_w = (tr_right - tr_left) * std::clamp(norm_val, 0.0f, 1.0f);
                    if (fill_w > 1.0f && !is_gain_disabled) {
                        D2D1_RECT_F fill_rc = D2D1::RectF(tr_left, tr_y, tr_left + fill_w, tr_y + 8.0f);
                        D2DRenderer::draw_rounded_box(rt, fill_rc, accent, accent, 3.0f);
                    }

                    D2D1_RECT_F val_rc = D2D1::RectF(card_rc.left, card_rc.bottom - 22.0f, card_rc.right, card_rc.bottom - 4.0f);
                    D2DRenderer::draw_text(rt, font_small, is_gain_disabled ? "(Inactive)" : val_txt, val_rc,
                                           D2D1::ColorF(0xab / 255.f, 0xb3 / 255.f, 0xc0 / 255.f));
                } else {
                    // Simple Mode: Large Beautiful Rotary Knob
                    draw_knob_d2d(rt, font_bold, font_small, card_rc, norm_val, item.title, val_txt, accent, is_gain_disabled);
                }
            }
        }

        // 6. Bottom Row: Mix and Output Sliders
        float bot_y = my + mh - 90.0f;
        float bot_w = (mw - 64.0f) * 0.5f;

        // Mix Slider (Left)
        D2D1_RECT_F mix_card_rc = D2D1::RectF(mx + 24.0f, bot_y, mx + 24.0f + bot_w, bot_y + 62.0f);
        D2DRenderer::draw_rounded_box(rt, mix_card_rc, D2D1::ColorF(0x20 / 255.f, 0x24 / 255.f, 0x2b / 255.f),
                                     D2D1::ColorF(0x2a / 255.f, 0x32 / 255.f, 0x3d / 255.f), 6.0f);
        int p_mix = find_param_idx(dev, "mix");
        float mix_norm = (p_mix >= 0) ? dev->get_parameter(p_mix) : wet_mix;
        std::string mix_title = (kind == 5 ? "Drive mix" : "Mix");
        std::string mix_val_str = std::to_string(static_cast<int>(std::round(mix_norm * 100.0f))) + "%";

        D2D1_RECT_F mix_lbl_rc = D2D1::RectF(mix_card_rc.left + 14.0f, bot_y + 6.0f, mix_card_rc.left + 160.0f, bot_y + 24.0f);
        D2DRenderer::draw_text(rt, font_bold, mix_title, mix_lbl_rc, D2D1::ColorF(0xe5 / 255.f, 0xec / 255.f, 0xef / 255.f));
        D2D1_RECT_F mix_v_rc = D2D1::RectF(mix_card_rc.right - 90.0f, bot_y + 6.0f, mix_card_rc.right - 14.0f, bot_y + 24.0f);
        D2DRenderer::draw_text(rt, font_small, mix_val_str, mix_v_rc, D2D1::ColorF(0xab / 255.f, 0xb3 / 255.f, 0xc0 / 255.f));

        D2D1_RECT_F mix_tr_rc = D2D1::RectF(mix_card_rc.left + 14.0f, bot_y + 32.0f, mix_card_rc.right - 14.0f, bot_y + 44.0f);
        D2DRenderer::draw_rounded_box(rt, mix_tr_rc, D2D1::ColorF(0x30 / 255.f, 0x3d / 255.f, 0x49 / 255.f),
                                     D2D1::ColorF(0x20 / 255.f, 0x24 / 255.f, 0x2b / 255.f), 3.0f);
        float mix_fill = (mix_tr_rc.right - mix_tr_rc.left) * std::clamp(mix_norm, 0.0f, 1.0f);
        if (mix_fill > 1.0f) {
            D2D1_RECT_F fill_rc = D2D1::RectF(mix_tr_rc.left, mix_tr_rc.top, mix_tr_rc.left + mix_fill, mix_tr_rc.bottom);
            D2DRenderer::draw_rounded_box(rt, fill_rc, accent, accent, 3.0f);
        }

        // Output Slider (Right)
        D2D1_RECT_F out_card_rc = D2D1::RectF(mx + 40.0f + bot_w, bot_y, mx + 40.0f + 2.0f * bot_w, bot_y + 62.0f);
        D2DRenderer::draw_rounded_box(rt, out_card_rc, D2D1::ColorF(0x20 / 255.f, 0x24 / 255.f, 0x2b / 255.f),
                                     D2D1::ColorF(0x2a / 255.f, 0x32 / 255.f, 0x3d / 255.f), 6.0f);
        int p_out = find_param_idx(dev, "output");
        float out_norm = (p_out >= 0) ? dev->get_parameter(p_out) : 0.5f;
        double out_db = (p_out >= 0) ? xfx->get_plain(p_out) : 0.0;
        std::string out_title = (kind == 5 ? "Pre-limit gain" : "Output");
        std::string out_val_str = format_param_display("dB", out_db);

        D2D1_RECT_F out_lbl_rc = D2D1::RectF(out_card_rc.left + 14.0f, bot_y + 6.0f, out_card_rc.left + 160.0f, bot_y + 24.0f);
        D2DRenderer::draw_text(rt, font_bold, out_title, out_lbl_rc, D2D1::ColorF(0xe5 / 255.f, 0xec / 255.f, 0xef / 255.f));
        D2D1_RECT_F out_v_rc = D2D1::RectF(out_card_rc.right - 90.0f, bot_y + 6.0f, out_card_rc.right - 14.0f, bot_y + 24.0f);
        D2DRenderer::draw_text(rt, font_small, out_val_str, out_v_rc, D2D1::ColorF(0xab / 255.f, 0xb3 / 255.f, 0xc0 / 255.f));

        D2D1_RECT_F out_tr_rc = D2D1::RectF(out_card_rc.left + 14.0f, bot_y + 32.0f, out_card_rc.right - 14.0f, bot_y + 44.0f);
        D2DRenderer::draw_rounded_box(rt, out_tr_rc, D2D1::ColorF(0x30 / 255.f, 0x3d / 255.f, 0x49 / 255.f),
                                     D2D1::ColorF(0x20 / 255.f, 0x24 / 255.f, 0x2b / 255.f), 3.0f);
        float out_fill = (out_tr_rc.right - out_tr_rc.left) * std::clamp(out_norm, 0.0f, 1.0f);
        if (out_fill > 1.0f) {
            D2D1_RECT_F fill_rc = D2D1::RectF(out_tr_rc.left, out_tr_rc.top, out_tr_rc.left + out_fill, out_tr_rc.bottom);
            D2DRenderer::draw_rounded_box(rt, fill_rc, accent, accent, 3.0f);
        }

        // 7. Bottom Footer Bar (Divider at my + mh - 98)
        ID2D1SolidColorBrush* br_div = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(0x26 / 255.f, 0x37 / 255.f, 0x43 / 255.f), &br_div);
        if (br_div) {
            rt->DrawLine(D2D1::Point2F(mx + 32.0f, my + mh - 98.0f), D2D1::Point2F(mx + mw - 32.0f, my + mh - 98.0f), br_div, 1.0f);
            br_div->Release();
        }

        // Left guide text
        std::string guide = (kind == 0)
            ? "Double-click: reset  |  Click value: type  |  EQ: drag node, wheel adjusts Q"
            : "Double-click: reset  |  Click value: type  |  Hover for help";
        D2D1_RECT_F guide_rc = D2D1::RectF(mx + 32.0f, my + mh - 26.0f, mx + mw - 260.0f, my + mh - 6.0f);
        D2DRenderer::draw_text(rt, font_small, guide, guide_rc, D2D1::ColorF(0x90 / 255.f, 0xa5 / 255.f, 0xb0 / 255.f));

        // Right status output text
        std::string out_status = (m_out >= 1.0f) ? "Output above 0 dBFS"
                               : (m_out < 0.00001f) ? "No input signal"
                               : ("Output " + format_1dec(xaudio::gainDb(m_out)) + " dB");
        D2D1_COLOR_F out_col = (m_out >= 1.0f) ? D2D1::ColorF(1.0f, 0.6f, 0.2f) : D2D1::ColorF(0x90 / 255.f, 0xa5 / 255.f, 0xb0 / 255.f);
        D2D1_RECT_F out_st_rc = D2D1::RectF(mx + mw - 250.0f, my + mh - 26.0f, mx + mw - 32.0f, my + mh - 6.0f);
        D2DRenderer::draw_text(rt, font_small, out_status, out_st_rc, out_col);
    }

    // ========================================================================
    // GDI Rendering for Effect Plugins (Full XAudio Suite Fidelity)
    // ========================================================================
    static void render_effect_gdi(HDC hdc, HFONT font_bold, HFONT font_small,
                                 domain::IDevice* dev, const RECT& bounds,
                                 bool enabled, float wet_mix, int /*track_id*/, int /*slot_idx*/, int /*scroll_offset*/ = 0) {
        if (!hdc || !dev) return;
        auto* xfx = dynamic_cast<plugins::XAudioEffectDevice*>(dev);
        int kind = xfx ? xfx->kind() : 0;
        auto& state = get_state(dev);

        int mx = bounds.left;
        int my = bounds.top;
        int mw = bounds.right - bounds.left;
        int mh = bounds.bottom - bounds.top;

        COLORREF accent = get_accent_gdi(kind);

        // 1. Modal Dialog Box Backdrop (0xff14171c -> RGB(20, 23, 28))
        RECT dlg_rc{mx, my, mx + mw, my + mh};
        GuiRenderer::draw_rounded_box(hdc, dlg_rc, RGB(20, 23, 28), RGB(42, 50, 61), 6);

        // 2. Header Bar (0xff20242b -> RGB(32, 36, 43))
        RECT hdr_rc{mx, my, mx + mw, my + 66};
        GuiRenderer::draw_rounded_box(hdc, hdr_rc, RGB(32, 36, 43), RGB(40, 46, 56), 6);

        // Title: 25pt Bold
        SelectObject(hdc, font_bold);
        RECT title_rc{mx + 24, my + 16, mx + 240, my + 50};
        GuiRenderer::draw_text(hdc, dev->name(), title_rc, accent, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Subtitle / Description
        SelectObject(hdc, font_small);
        RECT desc_rc{mx + 250, my + 20, mx + 520, my + 46};
        GuiRenderer::draw_text(hdc, get_description(kind), desc_rc, RGB(171, 179, 192), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Preset Dropdown Box
        int active_p = xfx ? xfx->active_preset() : 0;
        auto p_labels = get_preset_labels(kind);
        std::string cur_preset = (active_p < static_cast<int>(p_labels.size())) ? p_labels[active_p] : "Factory presets";

        RECT preset_rc{mx + mw - 410, my + 17, mx + mw - 180, my + 49};
        GuiRenderer::draw_rounded_box(hdc, preset_rc, RGB(36, 50, 62), RGB(56, 70, 83), 4);
        RECT preset_txt_rc{preset_rc.left + 10, preset_rc.top + 6, preset_rc.right - 26, preset_rc.bottom - 6};
        GuiRenderer::draw_text(hdc, "Preset: " + cur_preset, preset_txt_rc, RGB(229, 236, 239), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, font_bold);
        RECT arrow_rc{preset_rc.right - 22, preset_rc.top + 6, preset_rc.right - 6, preset_rc.bottom - 6};
        GuiRenderer::draw_text(hdc, "▼", arrow_rc, accent, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Bypass Button
        RECT byp_rc{mx + mw - 170, my + 17, mx + mw - 50, my + 49};
        COLORREF byp_bg = (!enabled) ? RGB(GetRValue(accent) / 3, GetGValue(accent) / 3, GetBValue(accent) / 3)
                                    : RGB(32, 46, 57);
        COLORREF byp_txt = (!enabled) ? accent : RGB(229, 236, 239);
        GuiRenderer::draw_button(hdc, byp_rc, enabled ? "ACTIVE" : "BYPASS", !enabled, byp_txt, byp_bg);

        // Close Button [✕]
        RECT close_rc{mx + mw - 42, my + 17, mx + mw - 12, my + 49};
        GuiRenderer::draw_button(hdc, close_rc, "✕", false, RGB(255, 114, 107), RGB(32, 46, 57));

        // 3. Graph Display Area
        int ctrl_top = my + mh - (state.detail ? 350 : 260);
        RECT p{mx + 64, my + 112, mx + mw - 126, ctrl_top - 40};

        // Graph backdrop
        RECT graph_bg_rc{p.left - 40, p.top - 36, p.right + 40, p.bottom + 36};
        GuiRenderer::draw_rounded_box(hdc, graph_bg_rc, RGB(16, 19, 24), RGB(32, 36, 43), 6);

        // Graph Title
        SelectObject(hdc, font_small);
        RECT gr_title_rc{mx + 32, my + 76, mx + 432, my + 98};
        GuiRenderer::draw_text(hdc, get_graph_title(kind), gr_title_rc, RGB(171, 179, 192), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // GR readout on header
        if (kind == 1 || kind == 5) {
            float gr_val = xfx ? xfx->gain_reduction_db(0) : 0.0f;
            std::string gr_str = "GR  " + format_1dec(gr_val) + " dB";
            SelectObject(hdc, font_bold);
            RECT gr_hdr_rc{p.right - 170, my + 76, p.right, my + 98};
            GuiRenderer::draw_text(hdc, gr_str, gr_hdr_rc, accent, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, font_small);
        }

        // Horizontal Grid Lines & Y-Axis Labels
        HPEN pen_grid_norm = CreatePen(PS_SOLID, 1, RGB(41, 46, 56));
        HPEN pen_grid_zero = CreatePen(PS_SOLID, 1, RGB(68, 75, 88));

        for (int i = 0; i <= 4; ++i) {
            int y = p.top + (p.bottom - p.top) * i / 4;
            HPEN pen = (i == 2) ? pen_grid_zero : pen_grid_norm;
            HGDIOBJ old_pen = SelectObject(hdc, pen);
            MoveToEx(hdc, p.left, y, NULL);
            LineTo(hdc, p.right, y);
            SelectObject(hdc, old_pen);

            int db = (kind == 0 ? 24 - i * 12 : (kind == 3 ? -i * 20 : -i * 15));
            std::string y_str = (kind == 4 ? format_1dec(1.5 - i * 0.75) : std::to_string(db));
            if (kind != 2) {
                RECT y_rc{mx + 20, y - 8, mx + 58, y + 8};
                GuiRenderer::draw_text(hdc, y_str, y_rc, RGB(146, 156, 172), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            }
        }

        // Vertical Grid Lines & X-Axis Labels
        if (kind == 0) {
            for (float f : {20.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f}) {
                int x = p.left + static_cast<int>(std::round(std::log(f / 20.0) / std::log(1000.0) * float(p.right - p.left)));
                HGDIOBJ old_pen = SelectObject(hdc, pen_grid_norm);
                MoveToEx(hdc, x, p.top, NULL);
                LineTo(hdc, x, p.bottom);
                SelectObject(hdc, old_pen);

                std::string f_str = (f >= 1000.0f ? std::to_string(static_cast<int>(f / 1000.0f)) + "k" : std::to_string(static_cast<int>(f)));
                RECT f_rc{x - 22, p.bottom + 9, x + 22, p.bottom + 27};
                GuiRenderer::draw_text(hdc, f_str, f_rc, RGB(146, 156, 172), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        } else if (kind != 2) {
            for (int i = 0; i <= 4; ++i) {
                int x = p.left + (p.right - p.left) * i / 4;
                HGDIOBJ old_pen = SelectObject(hdc, pen_grid_norm);
                MoveToEx(hdc, x, p.top, NULL);
                LineTo(hdc, x, p.bottom);
                SelectObject(hdc, old_pen);

                std::string x_str = (kind == 4 ? format_1dec(-1.0 + i * 0.5)
                                    : kind == 3 ? std::to_string(i * 3) + " s"
                                                : std::to_string(-60 + i * 15) + " dB");
                RECT x_rc{x - 26, p.bottom + 9, x + 26, p.bottom + 27};
                GuiRenderer::draw_text(hdc, x_str, x_rc, RGB(146, 156, 172), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
        DeleteObject(pen_grid_norm);
        DeleteObject(pen_grid_zero);

        // Render Specific Graph Visualizer Curves
        if (kind == 0) {
            auto* eq = dynamic_cast<plugins::XEqDevice*>(dev);
            if (eq) {
                render_eq_curve_gdi(hdc, p, eq, state.selected_band, font_bold);
            }
        } else if (kind == 1) {
            render_comp_curve_gdi(hdc, p, xfx, accent);
        } else if (kind == 2) {
            render_multiband_graph_gdi(hdc, p, xfx, font_bold, font_small, accent);
        } else if (kind == 3) {
            render_reverb_curve_gdi(hdc, p, xfx, accent);
        } else if (kind == 4) {
            render_distortion_curve_gdi(hdc, p, xfx, accent);
        } else if (kind == 5) {
            render_limiter_curve_gdi(hdc, p, xfx, accent);
        }

        // Live signal waveform overlay
        render_signal_waveform_gdi(hdc, p, xfx, accent);

        // Peak Meters (IN and OUT)
        float m_in = xfx ? xfx->meter_in() : 0.0f;
        float m_out = xfx ? xfx->meter_out() : 0.0f;
        render_peak_meters_gdi(hdc, mx, mw, p, m_in, m_out, accent, font_small);

        // 4. Middle Controls Bar
        if (kind == 0 || kind == 2) {
            render_bands_bar_gdi(hdc, mx, mw, ctrl_top, kind, state.selected_band, font_bold);
        } else {
            SelectObject(hdc, font_bold);
            RECT proc_lbl{mx + 24, ctrl_top + 4, mx + 240, ctrl_top + 28};
            GuiRenderer::draw_text(hdc, "PROCESSING", proc_lbl, RGB(171, 179, 192), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        // "More controls" / "Less controls" button
        RECT detail_rc{mx + mw - 174, ctrl_top, mx + mw - 24, ctrl_top + 30};
        GuiRenderer::draw_button(hdc, detail_rc, state.detail ? "Less controls" : "More controls",
                                state.detail, RGB(229, 236, 239), RGB(32, 46, 57));

        // 5. Parameter Cards Grid
        auto items = get_items(kind, state.detail, state.selected_band);
        int cols = state.detail ? 4 : std::max(1, static_cast<int>(items.size()));
        int rows = (static_cast<int>(items.size()) + cols - 1) / cols;
        int card_w = (mw - 48 - 8 * (cols - 1)) / cols;
        int card_h = (mh - 102 - (ctrl_top - my) - 42 - 8 * (rows - 1)) / rows;

        for (size_t i = 0; i < items.size(); ++i) {
            const auto& item = items[i];
            int c_col = static_cast<int>(i) % cols;
            int c_row = static_cast<int>(i) / cols;
            int bx = mx + 24 + c_col * (card_w + 8);
            int by = ctrl_top + 42 + c_row * (card_h + 8);
            RECT card_rc{bx, by, bx + card_w, by + card_h};

            GuiRenderer::draw_rounded_box(hdc, card_rc, RGB(32, 36, 43), RGB(42, 50, 61), 6);

            int p_idx = find_param_idx(dev, item.id);
            if (p_idx < 0) continue;
            const auto& spec = xfx->spec()[p_idx];
            double plain_val = dev ? xfx->get_plain(p_idx) : spec.initial;
            float norm_val = dev ? dev->get_parameter(p_idx) : 0.5f;

            if (item.id.find("type") != std::string::npos) {
                SelectObject(hdc, font_bold);
                RECT title_rc{card_rc.left, card_rc.top + 6, card_rc.right, card_rc.top + 26};
                GuiRenderer::draw_text(hdc, item.title, title_rc, RGB(229, 236, 239), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                int t_idx = std::clamp(static_cast<int>(std::round(plain_val)), 0, 4);
                const char* types[] = {"Bell", "Low shelf", "High shelf", "Low pass", "High pass"};
                RECT btn_choice{card_rc.left + 12, card_rc.top + (card_h - 32) / 2, card_rc.right - 12, card_rc.top + (card_h + 32) / 2};
                GuiRenderer::draw_button(hdc, btn_choice, types[t_idx], true, accent, RGB(36, 50, 62));
            } else if (item.id == "external" || item.id.find("solo") != std::string::npos || item.id.find("mute") != std::string::npos) {
                SelectObject(hdc, font_bold);
                RECT title_rc{card_rc.left, card_rc.top + 6, card_rc.right, card_rc.top + 26};
                GuiRenderer::draw_text(hdc, item.title, title_rc, RGB(229, 236, 239), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                bool is_active = (plain_val > 0.5);
                RECT btn_tog{card_rc.left + 12, card_rc.top + (card_h - 32) / 2, card_rc.right - 12, card_rc.top + (card_h + 32) / 2};
                COLORREF bg_act = (item.id.find("mute") != std::string::npos) ? RGB(255, 114, 107) : accent;
                GuiRenderer::draw_button(hdc, btn_tog, is_active ? "ON" : "OFF", is_active, bg_act, RGB(32, 46, 57));
            } else {
                bool is_gain_disabled = false;
                if (kind == 0 && item.id.find("gain") != std::string::npos) {
                    std::string pref = "eq" + std::to_string(state.selected_band + 1) + "type";
                    int t_id = find_param_idx(dev, pref);
                    if (t_id >= 0 && xfx->get_plain(t_id) >= 3.0) is_gain_disabled = true;
                }

                std::string val_txt = format_param_display(spec.unit, plain_val);

                if (state.detail) {
                    SelectObject(hdc, font_bold);
                    RECT title_rc{card_rc.left + 8, card_rc.top + 4, card_rc.right - 8, card_rc.top + 22};
                    GuiRenderer::draw_text(hdc, item.title, title_rc, RGB(229, 236, 239), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                    int tr_left = card_rc.left + 12;
                    int tr_right = card_rc.right - 12;
                    int tr_y = card_rc.top + (card_h - 10) / 2;
                    RECT tr_rc{tr_left, tr_y, tr_right, tr_y + 8};
                    GuiRenderer::draw_rounded_box(hdc, tr_rc, RGB(48, 61, 73), RGB(32, 36, 43), 3);

                    int fill_w = static_cast<int>(float(tr_right - tr_left) * std::clamp(norm_val, 0.0f, 1.0f));
                    if (fill_w > 1 && !is_gain_disabled) {
                        RECT fill_rc{tr_left, tr_y, tr_left + fill_w, tr_y + 8};
                        GuiRenderer::fill_rect(hdc, fill_rc, accent);
                    }

                    SelectObject(hdc, font_small);
                    RECT val_rc{card_rc.left, card_rc.bottom - 22, card_rc.right, card_rc.bottom - 4};
                    GuiRenderer::draw_text(hdc, is_gain_disabled ? "(Inactive)" : val_txt, val_rc,
                                           RGB(171, 179, 192), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                } else {
                    draw_knob_gdi(hdc, font_bold, font_small, card_rc, norm_val, item.title, val_txt, accent, is_gain_disabled);
                }
            }
        }

        // 6. Bottom Row: Mix and Output Sliders
        int bot_y = my + mh - 90;
        int bot_w = (mw - 64) / 2;

        // Mix Slider
        RECT mix_card_rc{mx + 24, bot_y, mx + 24 + bot_w, bot_y + 62};
        GuiRenderer::draw_rounded_box(hdc, mix_card_rc, RGB(32, 36, 43), RGB(42, 50, 61), 6);
        int p_mix = find_param_idx(dev, "mix");
        float mix_norm = (p_mix >= 0) ? dev->get_parameter(p_mix) : wet_mix;
        std::string mix_title = (kind == 5 ? "Drive mix" : "Mix");
        std::string mix_val_str = std::to_string(static_cast<int>(std::round(mix_norm * 100.0f))) + "%";

        SelectObject(hdc, font_bold);
        RECT mix_lbl_rc{mix_card_rc.left + 14, bot_y + 6, mix_card_rc.left + 160, bot_y + 24};
        GuiRenderer::draw_text(hdc, mix_title, mix_lbl_rc, RGB(229, 236, 239), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, font_small);
        RECT mix_v_rc{mix_card_rc.right - 90, bot_y + 6, mix_card_rc.right - 14, bot_y + 24};
        GuiRenderer::draw_text(hdc, mix_val_str, mix_v_rc, RGB(171, 179, 192), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        RECT mix_tr_rc{mix_card_rc.left + 14, bot_y + 32, mix_card_rc.right - 14, bot_y + 44};
        GuiRenderer::draw_rounded_box(hdc, mix_tr_rc, RGB(48, 61, 73), RGB(32, 36, 43), 3);
        int mix_fill = static_cast<int>(float(mix_tr_rc.right - mix_tr_rc.left) * std::clamp(mix_norm, 0.0f, 1.0f));
        if (mix_fill > 1) {
            RECT fill_rc{mix_tr_rc.left, mix_tr_rc.top, mix_tr_rc.left + mix_fill, mix_tr_rc.bottom};
            GuiRenderer::fill_rect(hdc, fill_rc, accent);
        }

        // Output Slider
        RECT out_card_rc{mx + 40 + bot_w, bot_y, mx + 40 + 2 * bot_w, bot_y + 62};
        GuiRenderer::draw_rounded_box(hdc, out_card_rc, RGB(32, 36, 43), RGB(42, 50, 61), 6);
        int p_out = find_param_idx(dev, "output");
        float out_norm = (p_out >= 0) ? dev->get_parameter(p_out) : 0.5f;
        double out_db = (p_out >= 0) ? xfx->get_plain(p_out) : 0.0;
        std::string out_title = (kind == 5 ? "Pre-limit gain" : "Output");
        std::string out_val_str = format_param_display("dB", out_db);

        SelectObject(hdc, font_bold);
        RECT out_lbl_rc{out_card_rc.left + 14, bot_y + 6, out_card_rc.left + 160, bot_y + 24};
        GuiRenderer::draw_text(hdc, out_title, out_lbl_rc, RGB(229, 236, 239), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, font_small);
        RECT out_v_rc{out_card_rc.right - 90, bot_y + 6, out_card_rc.right - 14, bot_y + 24};
        GuiRenderer::draw_text(hdc, out_val_str, out_v_rc, RGB(171, 179, 192), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        RECT out_tr_rc{out_card_rc.left + 14, bot_y + 32, out_card_rc.right - 14, bot_y + 44};
        GuiRenderer::draw_rounded_box(hdc, out_tr_rc, RGB(48, 61, 73), RGB(32, 36, 43), 3);
        int out_fill = static_cast<int>(float(out_tr_rc.right - out_tr_rc.left) * std::clamp(out_norm, 0.0f, 1.0f));
        if (out_fill > 1) {
            RECT fill_rc{out_tr_rc.left, out_tr_rc.top, out_tr_rc.left + out_fill, out_tr_rc.bottom};
            GuiRenderer::fill_rect(hdc, fill_rc, accent);
        }

        // 7. Bottom Footer Bar
        HPEN pen_div = CreatePen(PS_SOLID, 1, RGB(38, 55, 67));
        HGDIOBJ old_pen = SelectObject(hdc, pen_div);
        MoveToEx(hdc, mx + 32, my + mh - 98, NULL);
        LineTo(hdc, mx + mw - 32, my + mh - 98);
        SelectObject(hdc, old_pen);
        DeleteObject(pen_div);

        std::string guide = (kind == 0)
            ? "Double-click: reset  |  Click value: type  |  EQ: drag node, wheel adjusts Q"
            : "Double-click: reset  |  Click value: type  |  Hover for help";
        SelectObject(hdc, font_small);
        RECT guide_rc{mx + 32, my + mh - 26, mx + mw - 260, my + mh - 6};
        GuiRenderer::draw_text(hdc, guide, guide_rc, RGB(144, 165, 176), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        std::string out_status = (m_out >= 1.0f) ? "Output above 0 dBFS"
                               : (m_out < 0.00001f) ? "No input signal"
                               : ("Output " + format_1dec(xaudio::gainDb(m_out)) + " dB");
        COLORREF out_col = (m_out >= 1.0f) ? RGB(255, 150, 50) : RGB(144, 165, 176);
        RECT out_st_rc{mx + mw - 250, my + mh - 26, mx + mw - 32, my + mh - 6};
        GuiRenderer::draw_text(hdc, out_status, out_st_rc, out_col, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    // ========================================================================
    // Graph Curves Rendering Helpers (D2D & GDI)
    // ========================================================================
    static void render_eq_curve_d2d(ID2D1RenderTarget* rt, const D2D1_RECT_F& p,
                                   plugins::XEqDevice* eq, int selected_band,
                                   IDWriteTextFormat* font_bold) {
        if (!rt || !eq) return;
        ID2D1SolidColorBrush* br_curve = nullptr;
        rt->CreateSolidColorBrush(get_accent_d2d(0), &br_curve);

        float prev_x = p.left, prev_y = (p.top + p.bottom) * 0.5f;
        bool first = true;
        const int steps = 240;
        for (int i = 0; i <= steps; ++i) {
            double f = 20.0 * std::pow(1000.0, static_cast<double>(i) / static_cast<double>(steps));
            double db = eq->evaluate_response_db(f);
            float x = p.left + (p.right - p.left) * (static_cast<float>(i) / static_cast<float>(steps));
            float y = (p.top + p.bottom) * 0.5f - static_cast<float>(std::clamp(db, -24.0, 24.0)) / 48.0f * (p.bottom - p.top);
            if (!first && br_curve) {
                rt->DrawLine(D2D1::Point2F(prev_x, prev_y), D2D1::Point2F(x, y), br_curve, 2.5f);
            }
            prev_x = x;
            prev_y = y;
            first = false;
        }

        // Draw 6 interactive colored nodes
        for (int b = 0; b < 6; ++b) {
            double f = eq->band_freq(b);
            double gain = (eq->band_type(b) < 3) ? eq->band_gain(b) : 0.0;
            float nx = p.left + static_cast<float>(std::log(f / 20.0) / std::log(1000.0)) * (p.right - p.left);
            float ny = (p.top + p.bottom) * 0.5f - static_cast<float>(gain) / 48.0f * (p.bottom - p.top);

            D2D1_COLOR_F bc = get_band_color_d2d(b);
            ID2D1SolidColorBrush* br_node = nullptr;
            ID2D1SolidColorBrush* br_halo = nullptr;
            rt->CreateSolidColorBrush(bc, &br_node);
            rt->CreateSolidColorBrush(D2D1::ColorF(bc.r, bc.g, bc.b, 0.18f), &br_halo);

            if (br_node && br_halo) {
                if (selected_band == b) {
                    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(nx, ny), 18.0f, 18.0f), br_halo);
                    rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(nx, ny), 18.0f, 18.0f), br_node, 1.0f);
                }
                rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(nx, ny), 10.0f, 10.0f), br_node);

                D2D1_RECT_F txt_rc = D2D1::RectF(nx - 10.0f, ny - 10.0f, nx + 10.0f, ny + 10.0f);
                D2DRenderer::draw_text(rt, font_bold, std::to_string(b + 1), txt_rc, D2D1::ColorF(0x10 / 255.f, 0x13 / 255.f, 0x18 / 255.f));
            }
            if (br_node) br_node->Release();
            if (br_halo) br_halo->Release();
        }
        if (br_curve) br_curve->Release();
    }

    static void render_eq_curve_gdi(HDC hdc, const RECT& p,
                                   plugins::XEqDevice* eq, int selected_band,
                                   HFONT font_bold) {
        if (!hdc || !eq) return;
        HPEN pen_curve = CreatePen(PS_SOLID, 2, get_accent_gdi(0));
        HGDIOBJ old_pen = SelectObject(hdc, pen_curve);

        int prev_x = p.left, prev_y = (p.top + p.bottom) / 2;
        bool first = true;
        const int steps = 200;
        int pw = p.right - p.left;
        int ph = p.bottom - p.top;
        int mid_y = p.top + ph / 2;

        for (int i = 0; i <= steps; ++i) {
            double f = 20.0 * std::pow(1000.0, static_cast<double>(i) / static_cast<double>(steps));
            double db = eq->evaluate_response_db(f);
            int x = p.left + static_cast<int>(std::round(pw * (static_cast<float>(i) / static_cast<float>(steps))));
            int y = mid_y - static_cast<int>(std::round(std::clamp(db, -24.0, 24.0) / 48.0 * ph));
            if (!first) {
                MoveToEx(hdc, prev_x, prev_y, NULL);
                LineTo(hdc, x, y);
            }
            prev_x = x;
            prev_y = y;
            first = false;
        }
        SelectObject(hdc, old_pen);
        DeleteObject(pen_curve);

        SelectObject(hdc, font_bold);
        for (int b = 0; b < 6; ++b) {
            double f = eq->band_freq(b);
            double gain = (eq->band_type(b) < 3) ? eq->band_gain(b) : 0.0;
            int nx = p.left + static_cast<int>(std::round(std::log(f / 20.0) / std::log(1000.0) * pw));
            int ny = mid_y - static_cast<int>(std::round(gain / 48.0 * ph));

            COLORREF bc = get_band_color_gdi(b);
            if (selected_band == b) {
                HPEN pen_halo = CreatePen(PS_SOLID, 1, bc);
                HGDIOBJ op = SelectObject(hdc, pen_halo);
                HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Ellipse(hdc, nx - 17, ny - 17, nx + 17, ny + 17);
                SelectObject(hdc, ob);
                SelectObject(hdc, op);
                DeleteObject(pen_halo);
            }

            HBRUSH br_node = CreateSolidBrush(bc);
            HPEN pen_node = CreatePen(PS_SOLID, 1, bc);
            HGDIOBJ op = SelectObject(hdc, pen_node);
            HGDIOBJ ob = SelectObject(hdc, br_node);
            Ellipse(hdc, nx - 10, ny - 10, nx + 10, ny + 10);
            SelectObject(hdc, ob);
            SelectObject(hdc, op);
            DeleteObject(br_node);
            DeleteObject(pen_node);

            RECT trc{nx - 10, ny - 10, nx + 10, ny + 10};
            GuiRenderer::draw_text(hdc, std::to_string(b + 1), trc, RGB(16, 19, 24), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    static void render_comp_curve_d2d(ID2D1RenderTarget* rt, const D2D1_RECT_F& p,
                                      plugins::XAudioEffectDevice* dev, D2D1_COLOR_F accent) {
        if (!rt || !dev) return;
        double thresh = dev->get_plain(4);
        double ratio = dev->get_plain(5);
        double knee = dev->get_plain(8);
        double makeup = dev->get_plain(9);

        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(accent, &br);
        if (!br) return;

        float prev_x = p.left, prev_y = p.bottom;
        bool first = true;
        const int steps = 160;
        for (int i = 0; i <= steps; ++i) {
            double t = static_cast<double>(i) / static_cast<double>(steps);
            double db = -60.0 + 60.0 * t;
            double red = xaudio::reduction(db, thresh, ratio, knee);
            double v = 1.0 - std::clamp((db - red + makeup + 60.0) / 60.0, 0.0, 1.0);
            float x = p.left + (p.right - p.left) * static_cast<float>(t);
            float y = p.top + (p.bottom - p.top) * static_cast<float>(v);
            if (!first) {
                rt->DrawLine(D2D1::Point2F(prev_x, prev_y), D2D1::Point2F(x, y), br, 2.5f);
            }
            prev_x = x;
            prev_y = y;
            first = false;
        }
        br->Release();
    }

    static void render_comp_curve_gdi(HDC hdc, const RECT& p,
                                      plugins::XAudioEffectDevice* dev, COLORREF accent) {
        if (!hdc || !dev) return;
        double thresh = dev->get_plain(4);
        double ratio = dev->get_plain(5);
        double knee = dev->get_plain(8);
        double makeup = dev->get_plain(9);

        HPEN pen = CreatePen(PS_SOLID, 2, accent);
        HGDIOBJ old_pen = SelectObject(hdc, pen);

        int prev_x = p.left, prev_y = p.bottom;
        bool first = true;
        const int steps = 160;
        int pw = p.right - p.left;
        int ph = p.bottom - p.top;

        for (int i = 0; i <= steps; ++i) {
            double t = static_cast<double>(i) / static_cast<double>(steps);
            double db = -60.0 + 60.0 * t;
            double red = xaudio::reduction(db, thresh, ratio, knee);
            double v = 1.0 - std::clamp((db - red + makeup + 60.0) / 60.0, 0.0, 1.0);
            int x = p.left + static_cast<int>(pw * t);
            int y = p.top + static_cast<int>(ph * v);
            if (!first) {
                MoveToEx(hdc, prev_x, prev_y, NULL);
                LineTo(hdc, x, y);
            }
            prev_x = x;
            prev_y = y;
            first = false;
        }
        SelectObject(hdc, old_pen);
        DeleteObject(pen);
    }

    static void render_multiband_graph_d2d(ID2D1RenderTarget* rt, const D2D1_RECT_F& p,
                                          plugins::XAudioEffectDevice* dev,
                                          IDWriteTextFormat* font_bold, IDWriteTextFormat* font_small,
                                          D2D1_COLOR_F accent) {
        if (!rt || !dev) return;
        const char* area[] = {"Low", "Low mid", "High mid", "High"};
        float w = (p.right - p.left) / 4.0f;

        ID2D1SolidColorBrush* br_slot = nullptr;
        ID2D1SolidColorBrush* br_fill = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(0x30 / 255.f, 0x42 / 255.f, 0x4e / 255.f), &br_slot);
        rt->CreateSolidColorBrush(accent, &br_fill);

        for (int b = 0; b < 4; ++b) {
            float x = p.left + static_cast<float>(b) * w;
            float gr = dev->gain_reduction_db(b);

            // Slot
            D2D1_RECT_F slot_rc = D2D1::RectF(x + 14.0f, p.top + 25.0f, x + w - 14.0f, p.top + 33.0f);
            if (br_slot) rt->FillRoundedRectangle(D2D1::RoundedRect(slot_rc, 4.0f, 4.0f), br_slot);

            // Fill
            float fill_w = (w - 28.0f) * std::clamp(gr / 24.0f, 0.0f, 1.0f);
            if (fill_w > 0.5f && br_fill) {
                D2D1_RECT_F fill_rc = D2D1::RectF(x + 14.0f, p.top + 25.0f, x + 14.0f + fill_w, p.top + 33.0f);
                rt->FillRoundedRectangle(D2D1::RoundedRect(fill_rc, 4.0f, 4.0f), br_fill);
            }

            // dB text (large bold readout)
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1) << gr << " dB";
            D2D1_RECT_F db_rc = D2D1::RectF(x, p.top + 46.0f, x + w, p.top + 76.0f);
            D2DRenderer::draw_text(rt, font_bold, ss.str(), db_rc, D2D1::ColorF(0xe5 / 255.f, 0xec / 255.f, 0xef / 255.f));

            // Area name
            D2D1_RECT_F area_rc = D2D1::RectF(x, p.top + 85.0f, x + w, p.top + 107.0f);
            D2DRenderer::draw_text(rt, font_small, area[b], area_rc, D2D1::ColorF(0xa9 / 255.f, 0xba / 255.f, 0xc3 / 255.f));
        }

        if (br_slot) br_slot->Release();
        if (br_fill) br_fill->Release();
    }

    static void render_multiband_graph_gdi(HDC hdc, const RECT& p,
                                          plugins::XAudioEffectDevice* dev,
                                          HFONT font_bold, HFONT font_small,
                                          COLORREF accent) {
        if (!hdc || !dev) return;
        const char* area[] = {"Low", "Low mid", "High mid", "High"};
        int pw = p.right - p.left;
        int w = pw / 4;

        for (int b = 0; b < 4; ++b) {
            int x = p.left + b * w;
            float gr = dev->gain_reduction_db(b);

            RECT slot_rc{x + 14, p.top + 25, x + w - 14, p.top + 33};
            GuiRenderer::draw_rounded_box(hdc, slot_rc, RGB(48, 66, 78), RGB(32, 36, 43), 4);

            int fill_w = static_cast<int>((w - 28) * std::clamp(gr / 24.0f, 0.0f, 1.0f));
            if (fill_w > 1) {
                RECT fill_rc{x + 14, p.top + 25, x + 14 + fill_w, p.top + 33};
                GuiRenderer::fill_rect(hdc, fill_rc, accent);
            }

            SelectObject(hdc, font_bold);
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1) << gr << " dB";
            RECT db_rc{x, p.top + 46, x + w, p.top + 76};
            GuiRenderer::draw_text(hdc, ss.str(), db_rc, RGB(229, 236, 239), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdc, font_small);
            RECT area_rc{x, p.top + 85, x + w, p.top + 107};
            GuiRenderer::draw_text(hdc, area[b], area_rc, RGB(169, 186, 195), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    static void render_reverb_curve_d2d(ID2D1RenderTarget* rt, const D2D1_RECT_F& p,
                                       plugins::XAudioEffectDevice* dev, D2D1_COLOR_F accent) {
        if (!rt || !dev) return;
        double predelay = dev->get_plain(4);
        double decay = dev->get_plain(5);

        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(accent, &br);
        if (!br) return;

        float prev_x = p.left, prev_y = p.top;
        bool first = true;
        const int steps = 160;
        for (int i = 0; i <= steps; ++i) {
            double t = static_cast<double>(i) / static_cast<double>(steps);
            double v = std::clamp(60.0 * std::max(0.0, t * 12.0 - predelay * 0.001) / decay / 80.0, 0.0, 1.0);
            float x = p.left + (p.right - p.left) * static_cast<float>(t);
            float y = p.top + static_cast<float>(v) * (p.bottom - p.top);
            if (!first) {
                rt->DrawLine(D2D1::Point2F(prev_x, prev_y), D2D1::Point2F(x, y), br, 2.5f);
            }
            prev_x = x;
            prev_y = y;
            first = false;
        }
        br->Release();
    }

    static void render_reverb_curve_gdi(HDC hdc, const RECT& p,
                                       plugins::XAudioEffectDevice* dev, COLORREF accent) {
        if (!hdc || !dev) return;
        double predelay = dev->get_plain(4);
        double decay = dev->get_plain(5);

        HPEN pen = CreatePen(PS_SOLID, 2, accent);
        HGDIOBJ old_pen = SelectObject(hdc, pen);

        int prev_x = p.left, prev_y = p.top;
        bool first = true;
        const int steps = 160;
        int pw = p.right - p.left;
        int ph = p.bottom - p.top;

        for (int i = 0; i <= steps; ++i) {
            double t = static_cast<double>(i) / static_cast<double>(steps);
            double v = std::clamp(60.0 * std::max(0.0, t * 12.0 - predelay * 0.001) / decay / 80.0, 0.0, 1.0);
            int x = p.left + static_cast<int>(pw * t);
            int y = p.top + static_cast<int>(ph * v);
            if (!first) {
                MoveToEx(hdc, prev_x, prev_y, NULL);
                LineTo(hdc, x, y);
            }
            prev_x = x;
            prev_y = y;
            first = false;
        }
        SelectObject(hdc, old_pen);
        DeleteObject(pen);
    }

    static void render_distortion_curve_d2d(ID2D1RenderTarget* rt, const D2D1_RECT_F& p,
                                           plugins::XAudioEffectDevice* dev, D2D1_COLOR_F accent) {
        if (!rt || !dev) return;
        double drive = dev->get_plain(4);
        double bias = dev->get_plain(6);

        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(accent, &br);
        if (!br) return;

        float prev_x = p.left, prev_y = p.top;
        bool first = true;
        const int steps = 160;
        for (int i = 0; i <= steps; ++i) {
            double t = static_cast<double>(i) / static_cast<double>(steps);
            double normalY = 0.5 - xaudio::saturate(2.0 * t - 1.0, drive, bias) / 3.0;
            float x = p.left + (p.right - p.left) * static_cast<float>(t);
            float y = p.top + (p.bottom - p.top) * static_cast<float>(std::clamp(normalY, 0.0, 1.0));
            if (!first) {
                rt->DrawLine(D2D1::Point2F(prev_x, prev_y), D2D1::Point2F(x, y), br, 2.5f);
            }
            prev_x = x;
            prev_y = y;
            first = false;
        }
        br->Release();
    }

    static void render_distortion_curve_gdi(HDC hdc, const RECT& p,
                                           plugins::XAudioEffectDevice* dev, COLORREF accent) {
        if (!hdc || !dev) return;
        double drive = dev->get_plain(4);
        double bias = dev->get_plain(6);

        HPEN pen = CreatePen(PS_SOLID, 2, accent);
        HGDIOBJ old_pen = SelectObject(hdc, pen);

        int prev_x = p.left, prev_y = p.top;
        bool first = true;
        const int steps = 160;
        int pw = p.right - p.left;
        int ph = p.bottom - p.top;

        for (int i = 0; i <= steps; ++i) {
            double t = static_cast<double>(i) / static_cast<double>(steps);
            double normalY = 0.5 - xaudio::saturate(2.0 * t - 1.0, drive, bias) / 3.0;
            int x = p.left + static_cast<int>(pw * t);
            int y = p.top + static_cast<int>(ph * std::clamp(normalY, 0.0, 1.0));
            if (!first) {
                MoveToEx(hdc, prev_x, prev_y, NULL);
                LineTo(hdc, x, y);
            }
            prev_x = x;
            prev_y = y;
            first = false;
        }
        SelectObject(hdc, old_pen);
        DeleteObject(pen);
    }

    static void render_limiter_curve_d2d(ID2D1RenderTarget* rt, const D2D1_RECT_F& p,
                                        plugins::XAudioEffectDevice* dev, D2D1_COLOR_F accent) {
        if (!rt || !dev) return;
        double thresh = dev->get_plain(4);
        double ceiling = dev->get_plain(5);
        double input = dev->get_plain(1);
        double output = dev->get_plain(2);
        double mix = dev->get_plain(3);

        ID2D1SolidColorBrush* br = nullptr;
        rt->CreateSolidColorBrush(accent, &br);
        if (!br) return;

        float prev_x = p.left, prev_y = p.top;
        bool first = true;
        const int steps = 160;
        for (int i = 0; i <= steps; ++i) {
            double t = static_cast<double>(i) / static_cast<double>(steps);
            double boost = (1.0 - mix * 0.01) + mix * 0.01 * xaudio::dbGain(-thresh);
            double level = std::min(-60.0 + 60.0 * t + input + output + xaudio::gainDb(boost), ceiling);
            double normalY = 1.0 - (level + 60.0) / 60.0;
            float x = p.left + (p.right - p.left) * static_cast<float>(t);
            float y = p.top + (p.bottom - p.top) * static_cast<float>(std::clamp(normalY, 0.0, 1.0));
            if (!first) {
                rt->DrawLine(D2D1::Point2F(prev_x, prev_y), D2D1::Point2F(x, y), br, 2.5f);
            }
            prev_x = x;
            prev_y = y;
            first = false;
        }
        br->Release();
    }

    static void render_limiter_curve_gdi(HDC hdc, const RECT& p,
                                        plugins::XAudioEffectDevice* dev, COLORREF accent) {
        if (!hdc || !dev) return;
        double thresh = dev->get_plain(4);
        double ceiling = dev->get_plain(5);
        double input = dev->get_plain(1);
        double output = dev->get_plain(2);
        double mix = dev->get_plain(3);

        HPEN pen = CreatePen(PS_SOLID, 2, accent);
        HGDIOBJ old_pen = SelectObject(hdc, pen);

        int prev_x = p.left, prev_y = p.top;
        bool first = true;
        const int steps = 160;
        int pw = p.right - p.left;
        int ph = p.bottom - p.top;

        for (int i = 0; i <= steps; ++i) {
            double t = static_cast<double>(i) / static_cast<double>(steps);
            double boost = (1.0 - mix * 0.01) + mix * 0.01 * xaudio::dbGain(-thresh);
            double level = std::min(-60.0 + 60.0 * t + input + output + xaudio::gainDb(boost), ceiling);
            double normalY = 1.0 - (level + 60.0) / 60.0;
            int x = p.left + static_cast<int>(pw * t);
            int y = p.top + static_cast<int>(ph * std::clamp(normalY, 0.0, 1.0));
            if (!first) {
                MoveToEx(hdc, prev_x, prev_y, NULL);
                LineTo(hdc, x, y);
            }
            prev_x = x;
            prev_y = y;
            first = false;
        }
        SelectObject(hdc, old_pen);
        DeleteObject(pen);
    }

    // ========================================================================
    // Live Signal Waveform Rendering (overlaid on graph area)
    // ========================================================================
    static void render_signal_waveform_d2d(ID2D1RenderTarget* rt, const D2D1_RECT_F& p,
                                           plugins::XAudioEffectDevice* dev, D2D1_COLOR_F accent,
                                           IDWriteTextFormat* font_small = nullptr) {
        if (!rt || !dev) return;
        const float* in_data = dev->vis_in_data();
        const float* out_data = dev->vis_out_data();
        size_t wp = dev->vis_write_pos();
        constexpr size_t N = plugins::XAudioEffectDevice::kVisBufSize;

        // Check if there's any signal at all
        float max_abs = 0.0f;
        for (size_t i = 0; i < N; ++i) {
            max_abs = std::max(max_abs, std::max(std::abs(in_data[i]), std::abs(out_data[i])));
        }
        if (max_abs < 0.0001f) return; // No signal, don't draw

        // Auto-scale: normalize to peak, with a minimum of 0.05 to avoid division noise
        float scale = 1.0f / std::max(max_abs, 0.05f);
        scale = std::min(scale, 4.0f); // Cap amplification at 4x

        float graph_w = p.right - p.left;
        float graph_h = p.bottom - p.top;
        float mid_y = (p.top + p.bottom) * 0.5f;

        // Draw input waveform (dim, semi-transparent)
        ID2D1SolidColorBrush* br_in = nullptr;
        ID2D1SolidColorBrush* br_out = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.58f, 0.62f, 0.3f), &br_in);
        rt->CreateSolidColorBrush(D2D1::ColorF(accent.r, accent.g, accent.b, 0.55f), &br_out);

        // We display the last 256 samples (half the buffer) for a clean window
        constexpr size_t display_count = 256;
        size_t start = (wp + N - display_count) % N;

        auto draw_waveform = [&](const float* data, ID2D1SolidColorBrush* br, float thickness) {
            if (!br) return;
            float prev_x = p.left;
            float prev_y = mid_y;
            bool first = true;
            for (size_t i = 0; i < display_count; ++i) {
                size_t idx = (start + i) % N;
                float sample = std::clamp(data[idx] * scale, -1.0f, 1.0f);
                float x = p.left + graph_w * (static_cast<float>(i) / static_cast<float>(display_count - 1));
                float y = mid_y - sample * (graph_h * 0.42f);
                if (!first) {
                    rt->DrawLine(D2D1::Point2F(prev_x, prev_y), D2D1::Point2F(x, y), br, thickness);
                }
                prev_x = x;
                prev_y = y;
                first = false;
            }
        };

        draw_waveform(in_data, br_in, 1.0f);
        draw_waveform(out_data, br_out, 1.5f);

        if (br_in) br_in->Release();
        if (br_out) br_out->Release();

        // Signal label in top-right of graph
        if (font_small) {
            D2D1_RECT_F sig_lbl_rc = D2D1::RectF(p.right - 120.0f, p.top + 4.0f, p.right - 8.0f, p.top + 20.0f);
            D2DRenderer::draw_text(rt, font_small, "▬ IN  ▬ OUT", sig_lbl_rc,
                                   D2D1::ColorF(0.55f, 0.58f, 0.62f, 0.5f));
        }
    }

    static void render_signal_waveform_gdi(HDC hdc, const RECT& p,
                                           plugins::XAudioEffectDevice* dev, COLORREF accent) {
        if (!hdc || !dev) return;
        const float* in_data = dev->vis_in_data();
        const float* out_data = dev->vis_out_data();
        size_t wp = dev->vis_write_pos();
        constexpr size_t N = plugins::XAudioEffectDevice::kVisBufSize;

        // Check if there's any signal
        float max_abs = 0.0f;
        for (size_t i = 0; i < N; ++i) {
            max_abs = std::max(max_abs, std::max(std::abs(in_data[i]), std::abs(out_data[i])));
        }
        if (max_abs < 0.0001f) return;

        float scale = 1.0f / std::max(max_abs, 0.05f);
        scale = std::min(scale, 4.0f);

        int pw = p.right - p.left;
        int ph = p.bottom - p.top;
        int mid_y = p.top + ph / 2;

        constexpr size_t display_count = 256;
        size_t start = (wp + N - display_count) % N;

        auto draw_waveform_gdi = [&](const float* data, COLORREF color, int thickness) {
            HPEN pen = CreatePen(PS_SOLID, thickness, color);
            HGDIOBJ old_pen = SelectObject(hdc, pen);
            bool first = true;
            for (size_t i = 0; i < display_count; ++i) {
                size_t idx = (start + i) % N;
                float sample = std::clamp(data[idx] * scale, -1.0f, 1.0f);
                int x = p.left + static_cast<int>(pw * static_cast<float>(i) / static_cast<float>(display_count - 1));
                int y = mid_y - static_cast<int>(sample * (ph * 0.42f));
                if (first) {
                    MoveToEx(hdc, x, y, NULL);
                    first = false;
                } else {
                    LineTo(hdc, x, y);
                }
            }
            SelectObject(hdc, old_pen);
            DeleteObject(pen);
        };

        // Input: dim gray
        COLORREF in_color = RGB(100, 106, 115);
        draw_waveform_gdi(in_data, in_color, 1);
        // Output: accent color
        draw_waveform_gdi(out_data, accent, 1);
    }

    // ========================================================================
    // Peak Meters Rendering Helpers
    // ========================================================================
    static void render_peak_meters_d2d(ID2D1RenderTarget* rt, float mx, float mw, const D2D1_RECT_F& p,
                                      float meter_in, float meter_out, D2D1_COLOR_F accent,
                                      IDWriteTextFormat* font_small) {
        if (!rt) return;
        ID2D1SolidColorBrush* br_slot = nullptr;
        ID2D1SolidColorBrush* br_accent = nullptr;
        ID2D1SolidColorBrush* br_clip = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(0x2c / 255.f, 0x32 / 255.f, 0x3d / 255.f), &br_slot);
        rt->CreateSolidColorBrush(accent, &br_accent);
        rt->CreateSolidColorBrush(D2D1::ColorF(0xff / 255.f, 0x72 / 255.f, 0x6b / 255.f), &br_clip);

        for (int m = 0; m < 2; ++m) {
            float x = mx + mw - 94.0f + static_cast<float>(m) * 42.0f;
            float lvl = (m == 0 ? meter_in : meter_out);
            float normal = std::clamp(static_cast<float>((xaudio::gainDb(lvl) + 60.0) / 60.0), 0.0f, 1.0f);

            // Slot
            D2D1_RECT_F slot_rc = D2D1::RectF(x, p.top, x + 12.0f, p.bottom);
            if (br_slot) rt->FillRoundedRectangle(D2D1::RoundedRect(slot_rc, 3.0f, 3.0f), br_slot);

            // Fill
            float fill_h = normal * (p.bottom - p.top);
            if (fill_h > 1.0f) {
                D2D1_RECT_F fill_rc = D2D1::RectF(x, p.bottom - fill_h, x + 12.0f, p.bottom);
                auto* br = (lvl >= 1.0f ? br_clip : br_accent);
                if (br) rt->FillRoundedRectangle(D2D1::RoundedRect(fill_rc, 3.0f, 3.0f), br);
            }

            // Top text "IN" / "OUT"
            D2D1_RECT_F top_rc = D2D1::RectF(x - 12.0f, p.top - 29.0f, x + 24.0f, p.top - 9.0f);
            D2DRenderer::draw_text(rt, font_small, m == 0 ? "IN" : "OUT", top_rc, D2D1::ColorF(0xab / 255.f, 0xb3 / 255.f, 0xc0 / 255.f));

            // Bottom text dB readout
            std::string db_str = (lvl < 0.00001f) ? "-inf" : format_1dec(xaudio::gainDb(lvl));
            D2D1_RECT_F bot_rc = D2D1::RectF(x - 14.0f, p.bottom + 9.0f, x + 28.0f, p.bottom + 27.0f);
            D2DRenderer::draw_text(rt, font_small, db_str, bot_rc, D2D1::ColorF(0xab / 255.f, 0xb3 / 255.f, 0xc0 / 255.f));
        }

        if (br_slot) br_slot->Release();
        if (br_accent) br_accent->Release();
        if (br_clip) br_clip->Release();
    }

    static void render_peak_meters_gdi(HDC hdc, int mx, int mw, const RECT& p,
                                      float meter_in, float meter_out, COLORREF accent,
                                      HFONT font_small) {
        if (!hdc) return;
        for (int m = 0; m < 2; ++m) {
            int x = mx + mw - 94 + m * 42;
            float lvl = (m == 0 ? meter_in : meter_out);
            float normal = std::clamp(static_cast<float>((xaudio::gainDb(lvl) + 60.0) / 60.0), 0.0f, 1.0f);

            RECT slot_rc{x, p.top, x + 12, p.bottom};
            GuiRenderer::draw_rounded_box(hdc, slot_rc, RGB(44, 50, 61), RGB(32, 36, 43), 3);

            int fill_h = static_cast<int>(normal * float(p.bottom - p.top));
            if (fill_h > 1) {
                RECT fill_rc{x, p.bottom - fill_h, x + 12, p.bottom};
                GuiRenderer::fill_rect(hdc, fill_rc, (lvl >= 1.0f ? RGB(255, 114, 107) : accent));
            }

            SelectObject(hdc, font_small);
            RECT top_rc{x - 12, p.top - 29, x + 24, p.top - 9};
            GuiRenderer::draw_text(hdc, m == 0 ? "IN" : "OUT", top_rc, RGB(171, 179, 192), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            std::string db_str = (lvl < 0.00001f) ? "-inf" : format_1dec(xaudio::gainDb(lvl));
            RECT bot_rc{x - 14, p.bottom + 9, x + 28, p.bottom + 27};
            GuiRenderer::draw_text(hdc, db_str, bot_rc, RGB(171, 179, 192), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    // ========================================================================
    // Band Selection Buttons (D2D & GDI)
    // ========================================================================
    static void render_bands_bar_d2d(ID2D1RenderTarget* rt, float mx, float mw, float ctrl_top,
                                    int kind, int selected_band, IDWriteTextFormat* font_bold) {
        if (!rt) return;
        int count = (kind == 0 ? 6 : (kind == 2 ? 4 : 0));
        if (count <= 0) return;

        const char* eqNames[]{"1  Sub", "2  Bass", "3  Low mid", "4  Mid", "5  Presence", "6  Air"};
        const char* mbNames[]{"Low", "Low mid", "High mid", "High"};

        float btn_w = (mw - 220.0f) / static_cast<float>(count);
        for (int b = 0; b < count; ++b) {
            float bx = mx + 24.0f + static_cast<float>(b) * btn_w;
            D2D1_RECT_F btn_rc = D2D1::RectF(bx, ctrl_top, bx + btn_w - 6.0f, ctrl_top + 30.0f);

            bool sel = (selected_band == b);
            D2D1_COLOR_F bc = get_band_color_d2d(b);
            D2D1_COLOR_F bg_col = sel ? D2D1::ColorF(bc.r * 0.35f, bc.g * 0.35f, bc.b * 0.35f)
                                      : D2D1::ColorF(0x20 / 255.f, 0x24 / 255.f, 0x2b / 255.f);
            D2DRenderer::draw_button(rt, font_bold, btn_rc, kind == 0 ? eqNames[b] : mbNames[b],
                                    sel, bc, bg_col, 4.0f);
        }
    }

    static void render_bands_bar_gdi(HDC hdc, int mx, int mw, int ctrl_top,
                                    int kind, int selected_band, HFONT font_bold) {
        if (!hdc) return;
        int count = (kind == 0 ? 6 : (kind == 2 ? 4 : 0));
        if (count <= 0) return;

        const char* eqNames[]{"1  Sub", "2  Bass", "3  Low mid", "4  Mid", "5  Presence", "6  Air"};
        const char* mbNames[]{"Low", "Low mid", "High mid", "High"};

        int btn_w = (mw - 220) / count;
        SelectObject(hdc, font_bold);
        for (int b = 0; b < count; ++b) {
            int bx = mx + 24 + b * btn_w;
            RECT btn_rc{bx, ctrl_top, bx + btn_w - 6, ctrl_top + 30};

            bool sel = (selected_band == b);
            COLORREF bc = get_band_color_gdi(b);
            COLORREF bg_col = sel ? RGB(GetRValue(bc) / 3, GetGValue(bc) / 3, GetBValue(bc) / 3)
                                  : RGB(32, 36, 43);
            GuiRenderer::draw_button(hdc, btn_rc, kind == 0 ? eqNames[b] : mbNames[b], sel, bc, bg_col);
        }
    }

    // ========================================================================
    // Direct2D Rendering for X-Synth Instrument
    // ========================================================================
    static void render_xsynth_d2d(ID2D1RenderTarget* rt, IDWriteTextFormat* font_bold, IDWriteTextFormat* font_small,
                                 plugins::XSynthDevice* synth, const RECT& bounds) {
        if (!rt || !synth) return;
        float mx = static_cast<float>(bounds.left);
        float my = static_cast<float>(bounds.top);
        float mw = static_cast<float>(bounds.right - bounds.left);
        float mh = static_cast<float>(bounds.bottom - bounds.top);

        D2D1_COLOR_F accent = D2D1::ColorF(0x70 / 255.0f, 0xba / 255.0f, 0xff / 255.0f); // #70baff

        // Backdrop
        D2D1_RECT_F dlg_rc = D2D1::RectF(mx, my, mx + mw, my + mh);
        D2DRenderer::draw_rounded_box(rt, dlg_rc, D2D1::ColorF(0x14 / 255.f, 0x17 / 255.f, 0x1c / 255.f),
                                     D2D1::ColorF(0x2a / 255.f, 0x32 / 255.f, 0x3d / 255.f), 6.0f);

        // Header
        D2D1_RECT_F hdr_rc = D2D1::RectF(mx, my, mx + mw, my + 54.0f);
        D2DRenderer::draw_rounded_box(rt, hdr_rc, D2D1::ColorF(0x20 / 255.f, 0x24 / 255.f, 0x2b / 255.f),
                                     D2D1::ColorF(0x28 / 255.f, 0x2e / 255.f, 0x38 / 255.f), 6.0f);
        D2D1_RECT_F title_rc = D2D1::RectF(mx + 20.0f, my + 14.0f, mx + 450.0f, my + 44.0f);
        D2DRenderer::draw_text(rt, font_bold, "X-Synth — Polyphonic Subtractive Synthesizer", title_rc, accent);

        // Close Button [✕]
        D2D1_RECT_F close_rc = D2D1::RectF(mx + mw - 38.0f, my + 12.0f, mx + mw - 10.0f, my + 42.0f);
        D2DRenderer::draw_button(rt, font_bold, close_rc, "✕", false, D2D1::ColorF(0xff / 255.f, 0x72 / 255.f, 0x6b / 255.f),
                                D2D1::ColorF(0x20 / 255.f, 0x2e / 255.f, 0x39 / 255.f), 4.0f);

        auto draw_osc_sec = [&](const std::string& osc_title, float y, uint32_t shape_id, uint32_t oct_id, uint32_t vol_id, int detune_param_id = -1) {
            D2D1_RECT_F sec_rc = D2D1::RectF(mx + 16.0f, y, mx + mw - 16.0f, y + 74.0f);
            D2DRenderer::draw_rounded_box(rt, sec_rc, D2D1::ColorF(0x20 / 255.f, 0x24 / 255.f, 0x2b / 255.f),
                                         D2D1::ColorF(0x2a / 255.f, 0x32 / 255.f, 0x3d / 255.f), 6.0f);

            D2D1_RECT_F lbl_rc = D2D1::RectF(mx + 26.0f, y + 6.0f, mx + 260.0f, y + 26.0f);
            D2DRenderer::draw_text(rt, font_bold, osc_title, lbl_rc, D2D1::ColorF(0xab / 255.f, 0xb3 / 255.f, 0xc0 / 255.f));

            float shape_norm = synth->get_parameter(shape_id);
            int active_shape = std::clamp(static_cast<int>(std::round(shape_norm * 3.0f)), 0, 3);
            const char* waves[4] = {"SINE", "TRI", "SAW", "SQR"};

            for (int w = 0; w < 4; ++w) {
                D2D1_RECT_F w_rc = D2D1::RectF(mx + 26.0f + w * 62.0f, y + 30.0f, mx + 26.0f + (w + 1) * 62.0f - 6.0f, y + 64.0f);
                D2DRenderer::draw_button(rt, font_small, w_rc, waves[w], (w == active_shape), accent,
                                        D2D1::ColorF(0x20 / 255.f, 0x2e / 255.f, 0x39 / 255.f), 4.0f);
            }

            // Octave Buttons [-] and [+]
            float oct_norm = synth->get_parameter(oct_id);
            int oct_val = std::clamp(static_cast<int>(std::round((oct_norm - 0.5f) * 4.0f)), -2, 2);
            D2D1_RECT_F oct_lbl = D2D1::RectF(mx + 290.0f, y + 8.0f, mx + 380.0f, y + 28.0f);
            std::string oct_str = "OCT: " + std::string(oct_val > 0 ? "+" : "") + std::to_string(oct_val);
            D2DRenderer::draw_text(rt, font_small, oct_str, oct_lbl, D2D1::ColorF(0xe5 / 255.f, 0xec / 255.f, 0xef / 255.f));

            D2D1_RECT_F oct_dn = D2D1::RectF(mx + 290.0f, y + 32.0f, mx + 330.0f, y + 64.0f);
            D2DRenderer::draw_button(rt, font_bold, oct_dn, "▼", false, accent, D2D1::ColorF(0x20 / 255.f, 0x2e / 255.f, 0x39 / 255.f), 3.0f);
            D2D1_RECT_F oct_up = D2D1::RectF(mx + 338.0f, y + 32.0f, mx + 378.0f, y + 64.0f);
            D2DRenderer::draw_button(rt, font_bold, oct_up, "▲", false, accent, D2D1::ColorF(0x20 / 255.f, 0x2e / 255.f, 0x39 / 255.f), 3.0f);

            // Knobs
            if (detune_param_id >= 0) {
                float det_norm = synth->get_parameter(static_cast<uint32_t>(detune_param_id));
                D2D1_RECT_F det_rc = D2D1::RectF(mx + mw - 165.0f, y + 4.0f, mx + mw - 95.0f, y + 70.0f);
                draw_knob_d2d(rt, font_bold, font_small, det_rc, det_norm, "DETUNE", format_1dec((det_norm - 0.5f) * 24.0f) + " st",
                              D2D1::ColorF(0xff / 255.f, 0xb8 / 255.f, 0x66 / 255.f));
            }
            float vol_norm = synth->get_parameter(vol_id);
            D2D1_RECT_F vol_rc = D2D1::RectF(mx + mw - 90.0f, y + 4.0f, mx + mw - 20.0f, y + 70.0f);
            draw_knob_d2d(rt, font_bold, font_small, vol_rc, vol_norm, "VOL", std::to_string(static_cast<int>(std::round(vol_norm * 100.0f))) + "%", accent);
        };

        // OSC 1
        draw_osc_sec("OSCILLATOR 1 (PRIMARY)", my + 62.0f, 1, 2, 3, -1);
        // OSC 2
        draw_osc_sec("OSCILLATOR 2 (DETUNE / HARMONIC)", my + 144.0f, 4, 5, 7, 6);

        // Filter Section
        D2D1_RECT_F flt_rc = D2D1::RectF(mx + 16.0f, my + 226.0f, mx + mw - 16.0f, my + 316.0f);
        D2DRenderer::draw_rounded_box(rt, flt_rc, D2D1::ColorF(0x20 / 255.f, 0x24 / 255.f, 0x2b / 255.f),
                                     D2D1::ColorF(0x2a / 255.f, 0x32 / 255.f, 0x3d / 255.f), 6.0f);

        D2D1_RECT_F flt_lbl = D2D1::RectF(mx + 26.0f, my + 232.0f, mx + 300.0f, my + 252.0f);
        D2DRenderer::draw_text(rt, font_bold, "SUB-OSC & 24dB RESONANT FILTER", flt_lbl, D2D1::ColorF(0xab / 255.f, 0xb3 / 255.f, 0xc0 / 255.f));

        float sub_vol = synth->get_parameter(8);
        draw_knob_d2d(rt, font_bold, font_small, D2D1::RectF(mx + 30.0f, my + 246.0f, mx + 105.0f, my + 312.0f),
                      sub_vol, "SUB", std::to_string(static_cast<int>(std::round(sub_vol * 100.0f))) + "%", accent);

        float cut_norm = synth->get_parameter(9);
        draw_knob_d2d(rt, font_bold, font_small, D2D1::RectF(mx + 115.0f, my + 246.0f, mx + 190.0f, my + 312.0f),
                      cut_norm, "CUTOFF", format_param_display("Hz", 20.0 * std::pow(1000.0, cut_norm)), accent);

        float res_norm = synth->get_parameter(10);
        draw_knob_d2d(rt, font_bold, font_small, D2D1::RectF(mx + 200.0f, my + 246.0f, mx + 275.0f, my + 312.0f),
                      res_norm, "RES", format_1dec(0.1 + res_norm * 9.9), accent);

        float ftype_norm = synth->get_parameter(11);
        int ftype_idx = std::clamp(static_cast<int>(std::round(ftype_norm * 2.0f)), 0, 2);
        const char* ftype_names[3] = {"LOWPASS", "HIGHPASS", "BANDPASS"};
        D2D1_RECT_F ftype_rc = D2D1::RectF(mx + 295.0f, my + 260.0f, mx + 395.0f, my + 300.0f);
        D2DRenderer::draw_button(rt, font_bold, ftype_rc, ftype_names[ftype_idx], true, accent,
                                D2D1::ColorF(0x20 / 255.f, 0x2e / 255.f, 0x39 / 255.f), 4.0f);

        float drive_norm = synth->get_parameter(12);
        draw_knob_d2d(rt, font_bold, font_small, D2D1::RectF(mx + 415.0f, my + 246.0f, mx + 490.0f, my + 312.0f),
                      drive_norm, "DRIVE", format_1dec(drive_norm * 36.0f) + " dB", D2D1::ColorF(0xff / 255.f, 0x85 / 255.f, 0x76 / 255.f));

        // ADSR & Master Section
        D2D1_RECT_F adsr_sec = D2D1::RectF(mx + 16.0f, my + 324.0f, mx + mw - 16.0f, my + 414.0f);
        D2DRenderer::draw_rounded_box(rt, adsr_sec, D2D1::ColorF(0x20 / 255.f, 0x24 / 255.f, 0x2b / 255.f),
                                     D2D1::ColorF(0x2a / 255.f, 0x32 / 255.f, 0x3d / 255.f), 6.0f);

        D2D1_RECT_F adsr_lbl = D2D1::RectF(mx + 26.0f, my + 330.0f, mx + 300.0f, my + 350.0f);
        D2DRenderer::draw_text(rt, font_bold, "AMPLITUDE ENVELOPE (ADSR) & MASTER", adsr_lbl, D2D1::ColorF(0xab / 255.f, 0xb3 / 255.f, 0xc0 / 255.f));

        const char* adsr_names[4] = {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"};
        for (int a = 0; a < 4; ++a) {
            float val = synth->get_parameter(static_cast<uint32_t>(13 + a));
            D2D1_RECT_F k_rc = D2D1::RectF(mx + 30.0f + a * 85.0f, my + 344.0f, mx + 105.0f + a * 85.0f, my + 410.0f);
            draw_knob_d2d(rt, font_bold, font_small, k_rc, val, adsr_names[a], format_1dec(val * 100.0f) + "%", accent);
        }

        float m_vol = synth->get_parameter(0);
        draw_knob_d2d(rt, font_bold, font_small, D2D1::RectF(mx + mw - 95.0f, my + 344.0f, mx + mw - 25.0f, my + 410.0f),
                      m_vol, "MASTER", std::to_string(static_cast<int>(std::round(m_vol * 100.0f))) + "%", accent);

        // Audition Strip & Voice Readout
        D2D1_RECT_F aud_sec = D2D1::RectF(mx + 16.0f, my + 422.0f, mx + mw - 16.0f, my + mh - 16.0f);
        D2DRenderer::draw_rounded_box(rt, aud_sec, D2D1::ColorF(0x20 / 255.f, 0x24 / 255.f, 0x2b / 255.f),
                                     D2D1::ColorF(0x2a / 255.f, 0x32 / 255.f, 0x3d / 255.f), 6.0f);

        D2D1_RECT_F btn_aud = D2D1::RectF(mx + 28.0f, my + 434.0f, mx + 230.0f, my + 474.0f);
        D2DRenderer::draw_button(rt, font_bold, btn_aud, "♪ AUDITION NOTE (C4)", false, accent,
                                D2D1::ColorF(0x20 / 255.f, 0x2e / 255.f, 0x39 / 255.f), 4.0f);

        size_t active_vc = synth->active_voices();
        D2D1_RECT_F vc_lbl = D2D1::RectF(mx + 250.0f, my + 440.0f, mx + 460.0f, my + 468.0f);
        std::string vc_str = "ACTIVE VOICES: " + std::to_string(active_vc) + " / 16";
        D2DRenderer::draw_text(rt, font_bold, vc_str, vc_lbl, (active_vc > 0 ? accent : D2D1::ColorF(0x62 / 255.f, 0x66 / 255.f, 0x6d / 255.f)));
    }

    // ========================================================================
    // GDI Rendering for X-Synth Instrument
    // ========================================================================
    static void render_xsynth_gdi(HDC hdc, HFONT font_bold, HFONT font_small,
                                 plugins::XSynthDevice* synth, const RECT& bounds) {
        if (!hdc || !synth) return;
        int mx = bounds.left;
        int my = bounds.top;
        int mw = bounds.right - bounds.left;
        int mh = bounds.bottom - bounds.top;

        COLORREF accent = RGB(112, 186, 255);

        RECT dlg_rc{mx, my, mx + mw, my + mh};
        GuiRenderer::draw_rounded_box(hdc, dlg_rc, RGB(20, 23, 28), RGB(42, 50, 61), 6);

        RECT hdr_rc{mx, my, mx + mw, my + 54};
        GuiRenderer::draw_rounded_box(hdc, hdr_rc, RGB(32, 36, 43), RGB(40, 46, 56), 6);

        SelectObject(hdc, font_bold);
        RECT title_rc{mx + 20, my + 14, mx + 450, my + 44};
        GuiRenderer::draw_text(hdc, "X-Synth — Polyphonic Synthesizer", title_rc, accent, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT close_rc{mx + mw - 38, my + 12, mx + mw - 10, my + 42};
        GuiRenderer::draw_button(hdc, close_rc, "✕", false, RGB(255, 114, 107), RGB(32, 46, 57));

        auto draw_osc_sec = [&](const std::string& osc_title, int y, uint32_t shape_id, uint32_t oct_id, uint32_t vol_id, int detune_param_id = -1) {
            RECT sec_rc{mx + 16, y, mx + mw - 16, y + 74};
            GuiRenderer::draw_rounded_box(hdc, sec_rc, RGB(32, 36, 43), RGB(42, 50, 61), 6);

            SelectObject(hdc, font_bold);
            RECT lbl_rc{mx + 26, y + 6, mx + 260, y + 26};
            GuiRenderer::draw_text(hdc, osc_title, lbl_rc, RGB(171, 179, 192), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdc, font_small);
            float shape_norm = synth->get_parameter(shape_id);
            int active_shape = std::clamp(static_cast<int>(std::round(shape_norm * 3.0f)), 0, 3);
            const char* waves[4] = {"SINE", "TRI", "SAW", "SQR"};

            for (int w = 0; w < 4; ++w) {
                RECT w_rc{mx + 26 + w * 62, y + 30, mx + 26 + (w + 1) * 62 - 6, y + 64};
                GuiRenderer::draw_button(hdc, w_rc, waves[w], (w == active_shape), accent, RGB(32, 46, 57));
            }

            float oct_norm = synth->get_parameter(oct_id);
            int oct_val = std::clamp(static_cast<int>(std::round((oct_norm - 0.5f) * 4.0f)), -2, 2);
            RECT oct_lbl{mx + 290, y + 8, mx + 380, y + 28};
            std::string oct_str = "OCT: " + std::string(oct_val > 0 ? "+" : "") + std::to_string(oct_val);
            GuiRenderer::draw_text(hdc, oct_str, oct_lbl, RGB(229, 236, 239), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdc, font_bold);
            RECT oct_dn{mx + 290, y + 32, mx + 330, y + 64};
            GuiRenderer::draw_button(hdc, oct_dn, "▼", false, accent, RGB(32, 46, 57));
            RECT oct_up{mx + 338, y + 32, mx + 378, y + 64};
            GuiRenderer::draw_button(hdc, oct_up, "▲", false, accent, RGB(32, 46, 57));

            if (detune_param_id >= 0) {
                float det_norm = synth->get_parameter(static_cast<uint32_t>(detune_param_id));
                RECT det_rc{mx + mw - 165, y + 4, mx + mw - 95, y + 70};
                draw_knob_gdi(hdc, font_bold, font_small, det_rc, det_norm, "DETUNE",
                              format_1dec((det_norm - 0.5f) * 24.0f) + " st", RGB(255, 184, 102));
            }
            float vol_norm = synth->get_parameter(vol_id);
            RECT vol_rc{mx + mw - 90, y + 4, mx + mw - 20, y + 70};
            draw_knob_gdi(hdc, font_bold, font_small, vol_rc, vol_norm, "VOL",
                          std::to_string(static_cast<int>(std::round(vol_norm * 100.0f))) + "%", accent);
        };

        draw_osc_sec("OSCILLATOR 1 (PRIMARY)", my + 62, 1, 2, 3, -1);
        draw_osc_sec("OSCILLATOR 2 (DETUNE)", my + 144, 4, 5, 7, 6);

        // Filter
        RECT flt_rc{mx + 16, my + 226, mx + mw - 16, my + 316};
        GuiRenderer::draw_rounded_box(hdc, flt_rc, RGB(32, 36, 43), RGB(42, 50, 61), 6);
        SelectObject(hdc, font_bold);
        RECT flt_lbl{mx + 26, my + 232, mx + 300, my + 252};
        GuiRenderer::draw_text(hdc, "SUB-OSC & FILTER", flt_lbl, RGB(171, 179, 192), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        draw_knob_gdi(hdc, font_bold, font_small, RECT{mx + 30, my + 246, mx + 105, my + 312},
                      synth->get_parameter(8), "SUB", std::to_string(static_cast<int>(std::round(synth->get_parameter(8) * 100.0f))) + "%", accent);
        draw_knob_gdi(hdc, font_bold, font_small, RECT{mx + 115, my + 246, mx + 190, my + 312},
                      synth->get_parameter(9), "CUTOFF", format_param_display("Hz", 20.0 * std::pow(1000.0, synth->get_parameter(9))), accent);
        draw_knob_gdi(hdc, font_bold, font_small, RECT{mx + 200, my + 246, mx + 275, my + 312},
                      synth->get_parameter(10), "RES", format_1dec(0.1 + synth->get_parameter(10) * 9.9), accent);

        int ftype_idx = std::clamp(static_cast<int>(std::round(synth->get_parameter(11) * 2.0f)), 0, 2);
        const char* ftype_names[3] = {"LOWPASS", "HIGHPASS", "BANDPASS"};
        RECT ftype_rc{mx + 295, my + 260, mx + 395, my + 300};
        GuiRenderer::draw_button(hdc, ftype_rc, ftype_names[ftype_idx], true, accent, RGB(32, 46, 57));

        draw_knob_gdi(hdc, font_bold, font_small, RECT{mx + 415, my + 246, mx + 490, my + 312},
                      synth->get_parameter(12), "DRIVE", format_1dec(synth->get_parameter(12) * 36.0f) + " dB", RGB(255, 133, 118));

        // ADSR
        RECT adsr_sec{mx + 16, my + 324, mx + mw - 16, my + 414};
        GuiRenderer::draw_rounded_box(hdc, adsr_sec, RGB(32, 36, 43), RGB(42, 50, 61), 6);
        SelectObject(hdc, font_bold);
        RECT adsr_lbl{mx + 26, my + 330, mx + 300, my + 350};
        GuiRenderer::draw_text(hdc, "AMPLITUDE ENVELOPE (ADSR) & MASTER", adsr_lbl, RGB(171, 179, 192), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        const char* adsr_names[4] = {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"};
        for (int a = 0; a < 4; ++a) {
            RECT k_rc{mx + 30 + a * 85, my + 344, mx + 105 + a * 85, my + 410};
            draw_knob_gdi(hdc, font_bold, font_small, k_rc, synth->get_parameter(static_cast<uint32_t>(13 + a)),
                          adsr_names[a], format_1dec(synth->get_parameter(static_cast<uint32_t>(13 + a)) * 100.0f) + "%", accent);
        }
        draw_knob_gdi(hdc, font_bold, font_small, RECT{mx + mw - 95, my + 344, mx + mw - 25, my + 410},
                      synth->get_parameter(0), "MASTER", std::to_string(static_cast<int>(std::round(synth->get_parameter(0) * 100.0f))) + "%", accent);

        // Audition
        RECT aud_sec{mx + 16, my + 422, mx + mw - 16, my + mh - 16};
        GuiRenderer::draw_rounded_box(hdc, aud_sec, RGB(32, 36, 43), RGB(42, 50, 61), 6);
        RECT btn_aud{mx + 28, my + 434, mx + 230, my + 474};
        GuiRenderer::draw_button(hdc, btn_aud, "♪ AUDITION NOTE", false, accent, RGB(32, 46, 57));
    }

    // ========================================================================
    // Mouse Interaction Handlers
    // ========================================================================
    static bool handle_effect_click(domain::IDevice* dev, const RECT& bounds,
                                   int x, int y, bool& enabled, float& wet_mix,
                                   int& /*scroll_offset*/, int& dragging_param_idx,
                                   int& drag_start_x, int& drag_start_y, float& drag_orig_val,
                                   std::string& status_msg) {
        if (!dev) return true;
        auto* xfx = dynamic_cast<plugins::XAudioEffectDevice*>(dev);
        int kind = xfx ? xfx->kind() : 0;
        auto& state = get_state(dev);

        int mx = bounds.left;
        int my = bounds.top;
        int mw = bounds.right - bounds.left;
        int mh = bounds.bottom - bounds.top;

        // Click outside bounds -> Close modal
        if (x < mx || x > mx + mw || y < my || y > my + mh) {
            status_msg = "Closed " + dev->name() + " Editor";
            return true;
        }

        // Close Button [✕]: [mx + mw - 42, my + 17, mx + mw - 12, my + 49]
        if (x >= mx + mw - 42 && x <= mx + mw - 12 && y >= my + 17 && y <= my + 49) {
            status_msg = "Closed " + dev->name() + " Editor";
            return true;
        }

        // Bypass Button: [mx + mw - 170, my + 17, mx + mw - 50, my + 49]
        if (x >= mx + mw - 170 && x <= mx + mw - 50 && y >= my + 17 && y <= my + 49) {
            int p_byp = find_param_idx(dev, "bypass");
            if (p_byp >= 0) {
                float cur = dev->get_parameter(p_byp);
                float nxt = (cur > 0.5f ? 0.0f : 1.0f);
                dev->set_parameter(p_byp, nxt);
                enabled = (nxt <= 0.5f);
            } else {
                enabled = !enabled;
            }
            status_msg = (enabled ? "Active: " : "Bypassed: ") + dev->name();
            return false;
        }

        // Preset Dropdown Box: [mx + mw - 410, my + 17, mx + mw - 180, my + 49]
        if (x >= mx + mw - 410 && x <= mx + mw - 180 && y >= my + 17 && y <= my + 49) {
            if (xfx) {
                int next_p = (xfx->active_preset() + 1) % 3;
                xfx->load_preset(next_p);
                auto names = xfx->preset_names();
                status_msg = dev->name() + " Preset: " + (next_p < static_cast<int>(names.size()) ? names[next_p] : "Default");
            }
            return false;
        }

        // Middle Controls Bar
        float ctrl_top = my + mh - (state.detail ? 350.0f : 260.0f);

        // "More controls" / "Less controls" button: [mx + mw - 174, ctrl_top, mx + mw - 24, ctrl_top + 30]
        if (x >= mx + mw - 174 && x <= mx + mw - 24 && y >= ctrl_top && y <= ctrl_top + 30) {
            state.detail = !state.detail;
            status_msg = dev->name() + (state.detail ? ": Showing all controls" : ": Simplified view");
            return false;
        }

        // Band Selection Buttons (Kind 0: 6 bands, Kind 2: 4 bands)
        if ((kind == 0 || kind == 2) && y >= ctrl_top && y <= ctrl_top + 30) {
            int count = (kind == 0 ? 6 : 4);
            float btn_w = (mw - 220.0f) / static_cast<float>(count);
            int b = static_cast<int>((x - (mx + 24)) / btn_w);
            if (b >= 0 && b < count) {
                state.selected_band = b;
                const char* eqNames[]{"Sub", "Bass", "Low mid", "Mid", "Presence", "Air"};
                const char* mbNames[]{"Low", "Low mid", "High mid", "High"};
                status_msg = "Selected Band: " + std::string(kind == 0 ? eqNames[b] : mbNames[b]);
                return false;
            }
        }

        // EQ Node Selection & Dragging (Kind 0)
        if (kind == 0) {
            auto* eq = dynamic_cast<plugins::XEqDevice*>(dev);
            if (eq) {
                D2D1_RECT_F p = D2D1::RectF(mx + 64.0f, my + 112.0f, mx + mw - 126.0f, ctrl_top - 40.0f);
                float pw = p.right - p.left;
                float ph = p.bottom - p.top;
                float mid_y = (p.top + p.bottom) * 0.5f;

                for (int b = 0; b < 6; ++b) {
                    double f = eq->band_freq(b);
                    double gain = (eq->band_type(b) < 3) ? eq->band_gain(b) : 0.0;
                    float nx = p.left + static_cast<float>(std::log(f / 20.0) / std::log(1000.0)) * pw;
                    float ny = mid_y - static_cast<float>(gain) / 48.0f * ph;

                    float dist = std::hypot(static_cast<float>(x) - nx, static_cast<float>(y) - ny);
                    if (dist <= 22.0f) {
                        state.selected_band = b;
                        dragging_param_idx = 2000 + b; // Special code for EQ Node Drag
                        drag_start_x = x;
                        drag_start_y = y;
                        drag_orig_val = static_cast<float>(gain);
                        status_msg = "Band " + std::to_string(b + 1) + ": " +
                                     format_param_display("Hz", f) + ", " + format_param_display("dB", gain);
                        return false;
                    }
                }
            }
        }

        // Bottom Row Sliders: Mix & Output
        float bot_y = my + mh - 90.0f;
        float bot_w = (mw - 64.0f) * 0.5f;

        // Mix Slider: [mx + 24, bot_y, mx + 24 + bot_w, bot_y + 62]
        if (x >= mx + 24 && x <= mx + 24 + bot_w && y >= bot_y && y <= bot_y + 62) {
            int p_mix = find_param_idx(dev, "mix");
            if (p_mix >= 0) {
                float norm = std::clamp(static_cast<float>(x - (mx + 38)) / (bot_w - 28.0f), 0.0f, 1.0f);
                dev->set_parameter(p_mix, norm);
                wet_mix = norm;
                dragging_param_idx = p_mix;
                drag_start_x = x;
                drag_start_y = y;
                drag_orig_val = norm;
                status_msg = (kind == 5 ? "Drive mix: " : "Mix: ") + std::to_string(static_cast<int>(std::round(norm * 100.0f))) + "%";
                return false;
            }
        }

        // Output Slider: [mx + 40 + bot_w, bot_y, mx + 40 + 2*bot_w, bot_y + 62]
        if (x >= mx + 40 + bot_w && x <= mx + 40 + 2 * bot_w && y >= bot_y && y <= bot_y + 62) {
            int p_out = find_param_idx(dev, "output");
            if (p_out >= 0) {
                float norm = std::clamp(static_cast<float>(x - (mx + 54 + bot_w)) / (bot_w - 28.0f), 0.0f, 1.0f);
                dev->set_parameter(p_out, norm);
                dragging_param_idx = p_out;
                drag_start_x = x;
                drag_start_y = y;
                drag_orig_val = norm;
                status_msg = (kind == 5 ? "Pre-limit gain: " : "Output: ") + format_param_display("dB", xfx ? xfx->get_plain(p_out) : 0.0);
                return false;
            }
        }

        // Parameter Cards Grid Interaction
        auto items = get_items(kind, state.detail, state.selected_band);
        int cols = state.detail ? 4 : std::max(1, static_cast<int>(items.size()));
        int rows = (static_cast<int>(items.size()) + cols - 1) / cols;
        float card_w = (mw - 48.0f - 8.0f * (cols - 1)) / static_cast<float>(cols);
        float card_h = (mh - 102.0f - (ctrl_top - my) - 42.0f - 8.0f * (rows - 1)) / static_cast<float>(rows);

        for (size_t i = 0; i < items.size(); ++i) {
            const auto& item = items[i];
            int c_col = static_cast<int>(i) % cols;
            int c_row = static_cast<int>(i) / cols;
            float bx = mx + 24.0f + static_cast<float>(c_col) * (card_w + 8.0f);
            float by = ctrl_top + 42.0f + static_cast<float>(c_row) * (card_h + 8.0f);

            if (x >= bx && x <= bx + card_w && y >= by && y <= by + card_h) {
                int p_idx = find_param_idx(dev, item.id);
                if (p_idx < 0) break;

                // Choice combo click (Filter Type)
                if (item.id.find("type") != std::string::npos) {
                    int cur_t = static_cast<int>(std::round(xfx->get_plain(p_idx)));
                    int next_t = (cur_t + 1) % 5;
                    xfx->set_plain(p_idx, next_t);
                    const char* types[] = {"Bell", "Low shelf", "High shelf", "Low pass", "High pass"};
                    status_msg = item.title + ": " + types[next_t];
                    return false;
                }

                // Toggle button click (Solo, Mute, External)
                if (item.id == "external" || item.id.find("solo") != std::string::npos || item.id.find("mute") != std::string::npos) {
                    float cur = dev->get_parameter(p_idx);
                    float nxt = (cur > 0.5f ? 0.0f : 1.0f);
                    dev->set_parameter(p_idx, nxt);
                    status_msg = item.title + (nxt > 0.5f ? ": ON" : ": OFF");
                    return false;
                }

                // Double Click Detection -> Reset to default initial value
                uint32_t now = GetTickCount();
                if (now - state.last_click_ms < 350 && state.last_click_item == p_idx) {
                    const auto& spec = xfx->spec()[p_idx];
                    xfx->set_plain(p_idx, spec.initial);
                    status_msg = item.title + ": Reset to default (" + format_param_display(spec.unit, spec.initial) + ")";
                    state.last_click_ms = 0;
                    return false;
                }
                state.last_click_ms = now;
                state.last_click_item = p_idx;

                // Start Knob / Slider Drag
                dragging_param_idx = p_idx;
                drag_start_x = x;
                drag_start_y = y;
                drag_orig_val = dev->get_parameter(p_idx);
                status_msg = item.title + ": " + format_param_display(xfx->spec()[p_idx].unit, xfx->get_plain(p_idx));
                return false;
            }
        }

        return false;
    }

    // Compatibility overload for older callers without drag_start_y
    static bool handle_effect_click(domain::IDevice* dev, const RECT& bounds,
                                   int x, int y, bool& enabled, float& wet_mix,
                                   int& scroll_offset, int& dragging_param_idx,
                                   int& drag_start_x, float& drag_orig_val,
                                   std::string& status_msg) {
        int dummy_y = y;
        return handle_effect_click(dev, bounds, x, y, enabled, wet_mix, scroll_offset,
                                   dragging_param_idx, drag_start_x, dummy_y, drag_orig_val, status_msg);
    }

    static void handle_effect_drag(domain::IDevice* dev, const RECT& bounds,
                                  int dragging_param_idx, int x, int y, std::string& status_msg) {
        if (!dev || dragging_param_idx < 0) return;
        auto* xfx = dynamic_cast<plugins::XAudioEffectDevice*>(dev);
        int kind = xfx ? xfx->kind() : 0;
        auto& state = get_state(dev);

        int mx = bounds.left;
        int my = bounds.top;
        int mw = bounds.right - bounds.left;
        int mh = bounds.bottom - bounds.top;

        // 1. EQ Node Dragging (dragging_param_idx >= 2000)
        if (dragging_param_idx >= 2000) {
            int b = dragging_param_idx - 2000;
            auto* eq = dynamic_cast<plugins::XEqDevice*>(dev);
            if (eq && b >= 0 && b < 6) {
                float ctrl_top = my + mh - (state.detail ? 350.0f : 260.0f);
                D2D1_RECT_F p = D2D1::RectF(mx + 64.0f, my + 112.0f, mx + mw - 126.0f, ctrl_top - 40.0f);
                float pw = p.right - p.left;
                float ph = p.bottom - p.top;

                double f = 20.0 * std::pow(1000.0, std::clamp(static_cast<double>(x - p.left) / pw, 0.0, 1.0));
                double g = std::clamp(static_cast<double>((p.top + ph * 0.5f - y) / ph * 48.0), -18.0, 18.0);

                int p_f = find_param_idx(dev, "eq" + std::to_string(b + 1) + "freq");
                int p_g = find_param_idx(dev, "eq" + std::to_string(b + 1) + "gain");
                if (p_f >= 0) xfx->set_plain(p_f, f);
                if (p_g >= 0 && eq->band_type(b) < 3) xfx->set_plain(p_g, g);

                status_msg = "Band " + std::to_string(b + 1) + ": " +
                             format_param_display("Hz", f) + ", " + format_param_display("dB", g);
            }
            return;
        }

        // 2. Bottom Mix or Output Slider Drag
        int p_mix = find_param_idx(dev, "mix");
        int p_out = find_param_idx(dev, "output");
        float bot_w = (mw - 64.0f) * 0.5f;

        if (dragging_param_idx == p_mix) {
            float norm = std::clamp(static_cast<float>(x - (mx + 38)) / (bot_w - 28.0f), 0.0f, 1.0f);
            dev->set_parameter(p_mix, norm);
            status_msg = (kind == 5 ? "Drive mix: " : "Mix: ") + std::to_string(static_cast<int>(std::round(norm * 100.0f))) + "%";
            return;
        }
        if (dragging_param_idx == p_out) {
            float norm = std::clamp(static_cast<float>(x - (mx + 54 + bot_w)) / (bot_w - 28.0f), 0.0f, 1.0f);
            dev->set_parameter(p_out, norm);
            status_msg = (kind == 5 ? "Pre-limit gain: " : "Output: ") + format_param_display("dB", xfx->get_plain(p_out));
            return;
        }

        // 3. Card Sliders / Knobs
        if (state.detail) {
            // Horizontal slider drag in detail mode
            float delta = static_cast<float>(x - (mx + 200)) / 220.0f;
            float new_norm = std::clamp(delta, 0.0f, 1.0f);
            dev->set_parameter(static_cast<uint32_t>(dragging_param_idx), new_norm);
        } else {
            // Vertical Rotary Knob drag in simple mode
            float delta = static_cast<float>(bounds.top + 300 - y) / 180.0f;
            float new_norm = std::clamp(delta, 0.0f, 1.0f);
            dev->set_parameter(static_cast<uint32_t>(dragging_param_idx), new_norm);
        }

        if (static_cast<size_t>(dragging_param_idx) < xfx->spec().size()) {
            const auto& p = xfx->spec()[dragging_param_idx];
            status_msg = p.name + ": " + format_param_display(p.unit, xfx->get_plain(dragging_param_idx));
        }
    }

    // Compatibility overload for older callers without y
    static void handle_effect_drag(domain::IDevice* dev, const RECT& bounds,
                                  int dragging_param_idx, int x, std::string& status_msg) {
        handle_effect_drag(dev, bounds, dragging_param_idx, x, bounds.top + 300, status_msg);
    }

    // Mouse Wheel support for Q resonance adjustment on EQ graph
    static void handle_effect_wheel(domain::IDevice* dev, const RECT& bounds,
                                   int x, int y, int wheel_steps, std::string& status_msg) {
        if (!dev || wheel_steps == 0) return;
        auto* xfx = dynamic_cast<plugins::XAudioEffectDevice*>(dev);
        if (!xfx || xfx->kind() != 0) return;
        auto& state = get_state(dev);

        float ctrl_top = bounds.top + (bounds.bottom - bounds.top) - (state.detail ? 350.0f : 260.0f);
        D2D1_RECT_F p = D2D1::RectF(bounds.left + 64.0f, bounds.top + 112.0f, bounds.right - 126.0f, ctrl_top - 40.0f);

        // If mouse is inside EQ graph area, adjust selected band's Q
        if (x >= p.left && x <= p.right && y >= p.top && y <= p.bottom) {
            int b = state.selected_band;
            int p_q = find_param_idx(dev, "eq" + std::to_string(b + 1) + "q");
            if (p_q >= 0) {
                double cur_q = xfx->get_plain(p_q);
                double factor = std::exp(static_cast<double>(wheel_steps) * 0.15);
                double new_q = std::clamp(cur_q * factor, 0.15, 12.0);
                xfx->set_plain(p_q, new_q);
                status_msg = "Band " + std::to_string(b + 1) + " Q: " + format_param_display("", new_q);
            }
        }
    }

    // ========================================================================
    // X-Synth Interactions
    // ========================================================================
    template <typename AuditionFn = std::function<void(uint8_t)>>
    static bool handle_xsynth_click(plugins::XSynthDevice* synth, const RECT& bounds,
                                   int x, int y, int& dragging_param_idx,
                                   int& drag_start_y, float& drag_orig_val,
                                   std::string& status_msg, AuditionFn&& audition_fn = AuditionFn{}) {
        if (!synth) return true;
        int mx = bounds.left;
        int my = bounds.top;
        int mw = bounds.right - bounds.left;
        int mh = bounds.bottom - bounds.top;

        if (x < mx || x > mx + mw || y < my || y > my + mh) {
            status_msg = "Closed X-Synth Editor";
            return true;
        }

        // Close Button [✕]
        if (x >= mx + mw - 38 && x <= mx + mw - 10 && y >= my + 12 && y <= my + 42) {
            status_msg = "Closed X-Synth Editor";
            return true;
        }

        auto start_knob = [&](uint32_t pid, const std::string& name_prefix = "") {
            dragging_param_idx = static_cast<int>(pid);
            drag_start_y = y;
            drag_orig_val = synth->get_parameter(pid);
            for (const auto& p : synth->parameters()) {
                if (p.id == pid) {
                    status_msg = (name_prefix.empty() ? p.name : name_prefix) + ": " + format_param_display(p.unit, drag_orig_val);
                    break;
                }
            }
        };

        // Osc 1
        int osc1_y = my + 62;
        if (y >= osc1_y && y <= osc1_y + 74) {
            if (y >= osc1_y + 30 && y <= osc1_y + 64) {
                for (int w = 0; w < 4; ++w) {
                    if (x >= mx + 26 + w * 62 && x <= mx + 26 + (w + 1) * 62 - 6) {
                        synth->set_parameter(1, static_cast<float>(w) / 3.0f);
                        const char* waves[4] = {"Sine", "Triangle", "Saw", "Square"};
                        status_msg = "Osc 1 Waveform: " + std::string(waves[w]);
                        return false;
                    }
                }
            }
            if (y >= osc1_y + 32 && y <= osc1_y + 64) {
                if (x >= mx + 290 && x <= mx + 330) {
                    float oct = synth->get_parameter(2);
                    synth->set_parameter(2, std::clamp(oct - 0.25f, 0.0f, 1.0f));
                    status_msg = "Osc 1 Octave: " + std::to_string(synth->osc1_octave());
                    return false;
                }
                if (x >= mx + 338 && x <= mx + 378) {
                    float oct = synth->get_parameter(2);
                    synth->set_parameter(2, std::clamp(oct + 0.25f, 0.0f, 1.0f));
                    status_msg = "Osc 1 Octave: " + std::to_string(synth->osc1_octave());
                    return false;
                }
            }
            if (x >= mx + mw - 90 && x <= mx + mw - 20) {
                start_knob(3, "Osc 1 Volume");
                return false;
            }
        }

        // Osc 2
        int osc2_y = my + 144;
        if (y >= osc2_y && y <= osc2_y + 74) {
            if (y >= osc2_y + 30 && y <= osc2_y + 64) {
                for (int w = 0; w < 4; ++w) {
                    if (x >= mx + 26 + w * 62 && x <= mx + 26 + (w + 1) * 62 - 6) {
                        synth->set_parameter(4, static_cast<float>(w) / 3.0f);
                        const char* waves[4] = {"Sine", "Triangle", "Saw", "Square"};
                        status_msg = "Osc 2 Waveform: " + std::string(waves[w]);
                        return false;
                    }
                }
            }
            if (y >= osc2_y + 32 && y <= osc2_y + 64) {
                if (x >= mx + 290 && x <= mx + 330) {
                    float oct = synth->get_parameter(5);
                    synth->set_parameter(5, std::clamp(oct - 0.25f, 0.0f, 1.0f));
                    status_msg = "Osc 2 Octave: " + std::to_string(synth->osc2_octave());
                    return false;
                }
                if (x >= mx + 338 && x <= mx + 378) {
                    float oct = synth->get_parameter(5);
                    synth->set_parameter(5, std::clamp(oct + 0.25f, 0.0f, 1.0f));
                    status_msg = "Osc 2 Octave: " + std::to_string(synth->osc2_octave());
                    return false;
                }
            }
            if (x >= mx + mw - 165 && x <= mx + mw - 95) {
                start_knob(6, "Osc 2 Detune");
                return false;
            }
            if (x >= mx + mw - 90 && x <= mx + mw - 20) {
                start_knob(7, "Osc 2 Volume");
                return false;
            }
        }

        // Filter Section
        if (y >= my + 246 && y <= my + 312) {
            if (x >= mx + 30 && x <= mx + 105) { start_knob(8, "Sub-Osc Level"); return false; }
            if (x >= mx + 115 && x <= mx + 190) { start_knob(9, "Filter Cutoff"); return false; }
            if (x >= mx + 200 && x <= mx + 275) { start_knob(10, "Filter Resonance"); return false; }
            if (x >= mx + 295 && x <= mx + 395 && y >= my + 260 && y <= my + 300) {
                float cur_f = synth->get_parameter(11);
                int cur_idx = std::clamp(static_cast<int>(std::round(cur_f * 2.0f)), 0, 2);
                int next_idx = (cur_idx + 1) % 3;
                synth->set_parameter(11, static_cast<float>(next_idx) / 2.0f);
                const char* types[3] = {"Lowpass (24dB)", "Highpass (24dB)", "Bandpass"};
                status_msg = "Filter Mode: " + std::string(types[next_idx]);
                return false;
            }
            if (x >= mx + 415 && x <= mx + 490) { start_knob(12, "Drive Saturation"); return false; }
        }

        // ADSR & Master Section
        if (y >= my + 344 && y <= my + 410) {
            if (x >= mx + 30 && x <= mx + 105) { start_knob(13, "Env Attack"); return false; }
            if (x >= mx + 115 && x <= mx + 190) { start_knob(14, "Env Decay"); return false; }
            if (x >= mx + 200 && x <= mx + 275) { start_knob(15, "Env Sustain"); return false; }
            if (x >= mx + 285 && x <= mx + 360) { start_knob(16, "Env Release"); return false; }
            if (x >= mx + mw - 95 && x <= mx + mw - 25) { start_knob(0, "Master Volume"); return false; }
        }

        // Audition Button
        if (x >= mx + 28 && x <= mx + 230 && y >= my + 434 && y <= my + 474) {
            status_msg = "Auditioning Note C4 (261.6 Hz)...";
            if constexpr (!std::is_same_v<std::decay_t<AuditionFn>, std::nullptr_t>) {
                audition_fn(60);
            }
            return false;
        }

        return false;
    }

    static bool handle_xsynth_click(plugins::XSynthDevice* synth, const RECT& bounds,
                                   int x, int y, std::string& status_msg) {
        int dummy_idx = -1;
        int dummy_y = 0;
        float dummy_val = 0.0f;
        return handle_xsynth_click(synth, bounds, x, y, dummy_idx, dummy_y, dummy_val, status_msg, [](uint8_t){});
    }

    static void handle_xsynth_drag(plugins::XSynthDevice* synth,
                                  int dragging_param_idx, int drag_start_y,
                                  float drag_orig_val, int y,
                                  std::string& status_msg) {
        if (!synth || dragging_param_idx < 0) return;
        float delta = static_cast<float>(drag_start_y - y) / 150.0f;
        float new_val = std::clamp(drag_orig_val + delta, 0.0f, 1.0f);
        synth->set_parameter(static_cast<uint32_t>(dragging_param_idx), new_val);

        for (const auto& p : synth->parameters()) {
            if (p.id == static_cast<uint32_t>(dragging_param_idx)) {
                status_msg = p.name + ": " + format_param_display(p.unit, new_val);
                break;
            }
        }
    }
};

} // namespace digidaw::adapters::gui
