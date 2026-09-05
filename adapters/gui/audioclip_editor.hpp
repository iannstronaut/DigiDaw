#pragma once

#include "../plugins/audioclip_device.hpp"
#include "theme.hpp"
#include "d2d_renderer.hpp"
#include "gui_renderer.hpp"
#include <windows.h>
#include <commdlg.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace digidaw::adapters::gui {

enum class ClipperKnobId : int {
    None = -1,
    HeaderPan = 0,
    HeaderVol,
    HeaderPitch,
    TimePitch,
    TimeMul,
    TimeTime,
    SmpStart,
    SmpLength,
    InFade,
    OutFade,
    Crossfade,
    Trim,
    StartOffset,
    EnvDelay,
    EnvAtt,
    EnvHold,
    EnvDec,
    EnvSus,
    EnvRel,
    EnvTensionAtt,
    EnvTensionDec,
    LfoDelay,
    LfoAtt,
    LfoAmt,
    LfoSpeed,
    FilterModX,
    FilterModY,
    FineTune
};

class AudioClipEditor {
public:
    static std::string midi_note_to_name(uint8_t note) {
        const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        int oct = static_cast<int>(note) / 12;
        int semi = static_cast<int>(note) % 12;
        return std::string(names[semi]) + std::to_string(oct);
    }

    // --- Direct2D Rendering ---
    static void render_d2d(ID2D1RenderTarget* rt, IDWriteTextFormat* font_bold, IDWriteTextFormat* font_small,
                           plugins::AudioClipDevice* clipper, const RECT& bounds,
                           int active_tab, int env_subtab, uint8_t mixer_track) {
        if (!rt || !clipper) return;
        const auto& t = D2DRenderer::theme();

        float mx = static_cast<float>(bounds.left);
        float my = static_cast<float>(bounds.top);
        float mw = static_cast<float>(bounds.right - bounds.left);
        float mh = static_cast<float>(bounds.bottom - bounds.top);

        // 1. Modal Window Backdrop
        D2D1_RECT_F dlg_rc = D2D1::RectF(mx, my, mx + mw, my + mh);
        D2DRenderer::draw_rounded_box(rt, dlg_rc, t.bg_elevated, t.border_strong, 6.0f);

        // 2. Header Bar
        D2D1_RECT_F hdr_rc = D2D1::RectF(mx, my, mx + mw, my + 38.0f);
        D2DRenderer::draw_rounded_box(rt, hdr_rc, t.bg_surface, t.border_subtle, 6.0f);

        // Channel Title
        std::string title = "Clipper — " + clipper->filename();
        D2D1_RECT_F title_rc = D2D1::RectF(mx + 14.0f, my + 6.0f, mx + 220.0f, my + 32.0f);
        D2DRenderer::draw_text(rt, font_bold, title, title_rc, t.accent);

        // Mixer Track LCD Box
        D2D1_RECT_F trk_rc = D2D1::RectF(mx + 230.0f, my + 6.0f, mx + 300.0f, my + 32.0f);
        std::string trk_str = (mixer_track == 0) ? "MST" : ("TRK " + std::to_string(mixer_track));
        D2DRenderer::draw_button(rt, font_small, trk_rc, trk_str, false, t.accent, t.bg_control, 3.0f);

        // Header Knobs: PAN, VOL, PITCH
        D2D1_RECT_F pan_rc = D2D1::RectF(mx + 310.0f, my + 4.0f, mx + 348.0f, my + 34.0f);
        float norm_pan = (clipper->pan() + 1.0f) * 0.5f;
        D2DRenderer::draw_knob(rt, font_small, pan_rc, norm_pan, "PAN", t.accent);

        D2D1_RECT_F vol_rc = D2D1::RectF(mx + 356.0f, my + 4.0f, mx + 394.0f, my + 34.0f);
        D2DRenderer::draw_knob(rt, font_small, vol_rc, clipper->master_volume(), "VOL", t.accent);

        D2D1_RECT_F pitch_rc = D2D1::RectF(mx + 402.0f, my + 4.0f, mx + 440.0f, my + 34.0f);
        float norm_pitch = (clipper->pitch_shift() / 24.0f) + 0.5f;
        D2DRenderer::draw_knob(rt, font_small, pitch_rc, norm_pitch, "PITCH", t.accent);

        // Tabs switcher: Tab 0 (Sample/Waveform), Tab 1 (Env/Inst), Tab 2 (Misc)
        D2D1_RECT_F tab0_rc = D2D1::RectF(mx + 458.0f, my + 6.0f, mx + 538.0f, my + 32.0f);
        D2DRenderer::draw_button(rt, font_small, tab0_rc, "〰 Sample", (active_tab == 0), t.accent, t.bg_control, 3.0f);

        D2D1_RECT_F tab1_rc = D2D1::RectF(mx + 544.0f, my + 6.0f, mx + 634.0f, my + 32.0f);
        D2DRenderer::draw_button(rt, font_small, tab1_rc, "📈 Env / Inst", (active_tab == 1), t.accent, t.bg_control, 3.0f);

        D2D1_RECT_F tab2_rc = D2D1::RectF(mx + 640.0f, my + 6.0f, mx + 710.0f, my + 32.0f);
        D2DRenderer::draw_button(rt, font_small, tab2_rc, "🔧 Misc", (active_tab == 2), t.accent, t.bg_control, 3.0f);

        // Close Button [✕]
        D2D1_RECT_F close_rc = D2D1::RectF(mx + mw - 38.0f, my + 6.0f, mx + mw - 8.0f, my + 32.0f);
        D2DRenderer::draw_button(rt, font_bold, close_rc, "✕", false, t.danger, t.bg_control, 3.5f);

        // --- Render Tab Content ---
        if (active_tab == 0) {
            render_tab_sample_d2d(rt, font_bold, font_small, clipper, mx, my, mw, mh);
        } else if (active_tab == 1) {
            render_tab_envelope_d2d(rt, font_bold, font_small, clipper, mx, my, mw, mh, env_subtab);
        } else {
            render_tab_misc_d2d(rt, font_bold, font_small, clipper, mx, my, mw, mh);
        }
    }

    // --- Tab 0: Sample / Waveform (Screenshot 1) ---
    static void render_tab_sample_d2d(ID2D1RenderTarget* rt, IDWriteTextFormat* font_bold, IDWriteTextFormat* font_small,
                                      plugins::AudioClipDevice* clipper, float mx, float my, float mw, float mh) {
        const auto& t = D2DRenderer::theme();

        // 1. File Section Box (Top Left)
        D2D1_RECT_F file_box = D2D1::RectF(mx + 14.0f, my + 44.0f, mx + 372.0f, my + 78.0f);
        D2DRenderer::draw_rounded_box(rt, file_box, t.bg_surface, t.border_subtle, 4.0f);

        D2D1_RECT_F file_lbl = D2D1::RectF(mx + 22.0f, my + 48.0f, mx + 280.0f, my + 74.0f);
        std::string file_text = "File  " + clipper->filename();
        D2DRenderer::draw_text(rt, font_bold, file_text, file_lbl, t.text_primary, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Buttons: Browse [📁], Reload [⟳], Clear [✕]
        D2D1_RECT_F btn_browse = D2D1::RectF(mx + 290.0f, my + 48.0f, mx + 316.0f, my + 74.0f);
        D2DRenderer::draw_button(rt, font_bold, btn_browse, "📁", false, t.accent, t.bg_control, 3.0f);

        D2D1_RECT_F btn_reload = D2D1::RectF(mx + 320.0f, my + 48.0f, mx + 344.0f, my + 74.0f);
        D2DRenderer::draw_button(rt, font_bold, btn_reload, "⟳", false, t.accent, t.bg_control, 3.0f);

        D2D1_RECT_F btn_clear = D2D1::RectF(mx + 348.0f, my + 48.0f, mx + 368.0f, my + 74.0f);
        D2DRenderer::draw_button(rt, font_bold, btn_clear, "✕", false, t.text_muted, t.bg_control, 3.0f);

        // 2. Content Box (Below File)
        D2D1_RECT_F content_box = D2D1::RectF(mx + 14.0f, my + 84.0f, mx + 372.0f, my + 204.0f);
        D2DRenderer::draw_rounded_box(rt, content_box, t.bg_surface, t.border_subtle, 4.0f);

        D2D1_RECT_F content_title = D2D1::RectF(mx + 22.0f, my + 88.0f, mx + 150.0f, my + 106.0f);
        D2DRenderer::draw_text(rt, font_bold, "Content", content_title, t.text_secondary);

        // Toggles in Content Box
        D2D1_RECT_F t_disk = D2D1::RectF(mx + 22.0f, my + 110.0f, mx + 180.0f, my + 130.0f);
        D2DRenderer::draw_button(rt, font_small, t_disk, clipper->keep_on_disk() ? "● Keep on disk" : "○ Keep on disk",
                                clipper->keep_on_disk(), t.accent, t.bg_control, 2.0f);

        D2D1_RECT_F t_reg = D2D1::RectF(mx + 195.0f, my + 110.0f, mx + 362.0f, my + 130.0f);
        D2DRenderer::draw_button(rt, font_small, t_reg, clipper->load_regions() ? "● Load regions" : "○ Load regions",
                                clipper->load_regions(), t.accent, t.bg_control, 2.0f);

        D2D1_RECT_F t_resamp = D2D1::RectF(mx + 22.0f, my + 136.0f, mx + 180.0f, my + 156.0f);
        D2DRenderer::draw_button(rt, font_small, t_resamp, clipper->resample_enabled() ? "● Resample" : "○ Resample",
                                clipper->resample_enabled(), D2D1::ColorF(1.0f, 0.45f, 0.35f), t.bg_control, 2.0f);

        D2D1_RECT_F t_slice = D2D1::RectF(mx + 195.0f, my + 136.0f, mx + 362.0f, my + 156.0f);
        D2DRenderer::draw_button(rt, font_small, t_slice, clipper->load_slice_markers() ? "● Load slice markers" : "○ Load slice markers",
                                clipper->load_slice_markers(), t.accent, t.bg_control, 2.0f);

        // Declicking Mode Dropdown
        D2D1_RECT_F decl_lbl = D2D1::RectF(mx + 22.0f, my + 168.0f, mx + 140.0f, my + 196.0f);
        D2DRenderer::draw_text(rt, font_small, "Declicking mode", decl_lbl, t.text_secondary, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        D2D1_RECT_F decl_box = D2D1::RectF(mx + 145.0f, my + 168.0f, mx + 362.0f, my + 196.0f);
        const char* decl_modes[] = {"Out only (no bleeding)  ▼", "Transient (no bleeding)  ▼", "Smooth (bleeding)  ▼"};
        int dmode = std::clamp(clipper->declicking_mode(), 0, 2);
        D2DRenderer::draw_button(rt, font_small, decl_box, decl_modes[dmode], false, t.accent, t.bg_control, 3.0f);

        // 3. Playback Box (Below Content)
        D2D1_RECT_F play_box = D2D1::RectF(mx + 14.0f, my + 210.0f, mx + 372.0f, my + 300.0f);
        D2DRenderer::draw_rounded_box(rt, play_box, t.bg_surface, t.border_subtle, 4.0f);

        D2D1_RECT_F play_title = D2D1::RectF(mx + 22.0f, my + 214.0f, mx + 150.0f, my + 232.0f);
        D2DRenderer::draw_text(rt, font_bold, "Playback", play_title, t.text_secondary);

        // Start Offset Knob
        D2D1_RECT_F offset_knob_rc = D2D1::RectF(mx + 45.0f, my + 236.0f, mx + 95.0f, my + 288.0f);
        D2DRenderer::draw_knob(rt, font_small, offset_knob_rc, clipper->start_offset(), "START OFFSET", t.accent);

        // Toggles in Playback Box
        D2D1_RECT_F t_loop = D2D1::RectF(mx + 160.0f, my + 236.0f, mx + 360.0f, my + 258.0f);
        D2DRenderer::draw_button(rt, font_small, t_loop, clipper->use_loop_points() ? "● Use loop points" : "○ Use loop points",
                                clipper->use_loop_points(), D2D1::ColorF(1.0f, 0.45f, 0.35f), t.bg_control, 2.0f);

        D2D1_RECT_F t_pingpong = D2D1::RectF(mx + 160.0f, my + 264.0f, mx + 360.0f, my + 286.0f);
        D2DRenderer::draw_button(rt, font_small, t_pingpong, clipper->ping_pong_loop() ? "● Ping pong loop" : "○ Ping pong loop",
                                clipper->ping_pong_loop(), t.accent, t.bg_control, 2.0f);

        // 4. Time Stretching Box (Top Right)
        D2D1_RECT_F time_box = D2D1::RectF(mx + 386.0f, my + 44.0f, mx + mw - 14.0f, my + 144.0f);
        D2DRenderer::draw_rounded_box(rt, time_box, t.bg_surface, t.border_subtle, 4.0f);

        D2D1_RECT_F time_title = D2D1::RectF(mx + 396.0f, my + 48.0f, mx + 550.0f, my + 66.0f);
        D2DRenderer::draw_text(rt, font_bold, "Time stretching", time_title, t.text_secondary);

        // Knobs: PITCH, MUL, TIME
        D2D1_RECT_F k_pitch = D2D1::RectF(mx + 406.0f, my + 72.0f, mx + 456.0f, my + 128.0f);
        float norm_tpitch = (clipper->pitch_shift() / 24.0f) + 0.5f;
        D2DRenderer::draw_knob(rt, font_small, k_pitch, norm_tpitch, "PITCH", t.accent);

        D2D1_RECT_F k_mul = D2D1::RectF(mx + 476.0f, my + 72.0f, mx + 526.0f, my + 128.0f);
        float norm_mul = (clipper->time_mul() - 0.5f) / 1.5f;
        D2DRenderer::draw_knob(rt, font_small, k_mul, norm_mul, "MUL", t.accent);

        D2D1_RECT_F k_time = D2D1::RectF(mx + 546.0f, my + 72.0f, mx + 596.0f, my + 128.0f);
        D2DRenderer::draw_knob(rt, font_small, k_time, clipper->time_length(), "TIME", t.accent);

        // Mode Dropdown
        D2D1_RECT_F mode_lbl = D2D1::RectF(mx + 610.0f, my + 72.0f, mx + 655.0f, my + 100.0f);
        D2DRenderer::draw_text(rt, font_small, "Mode", mode_lbl, t.text_secondary, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        D2D1_RECT_F mode_btn = D2D1::RectF(mx + 660.0f, my + 72.0f, mx + mw - 24.0f, my + 100.0f);
        const char* stretch_modes[] = {"Resample  ▼", "Auto  ▼", "Stretch  ▼", "Slice  ▼"};
        int smode = std::clamp(clipper->stretch_mode(), 0, 3);
        D2DRenderer::draw_button(rt, font_small, mode_btn, stretch_modes[smode], false, t.accent, t.bg_control, 3.0f);

        // 5. Precomputed Effects Box (Middle Right)
        D2D1_RECT_F pre_box = D2D1::RectF(mx + 386.0f, my + 150.0f, mx + mw - 14.0f, my + 300.0f);
        D2DRenderer::draw_rounded_box(rt, pre_box, t.bg_surface, t.border_subtle, 4.0f);

        D2D1_RECT_F pre_title = D2D1::RectF(mx + 396.0f, my + 154.0f, mx + 600.0f, my + 172.0f);
        D2DRenderer::draw_text(rt, font_bold, "🔧 📈 Precomputed effects", pre_title, t.text_secondary);

        // 6 Toggles in Precomputed Effects
        D2D1_RECT_F t_dc = D2D1::RectF(mx + 396.0f, my + 176.0f, mx + 555.0f, my + 196.0f);
        D2DRenderer::draw_button(rt, font_small, t_dc, clipper->remove_dc() ? "● Remove DC offset" : "○ Remove DC offset",
                                clipper->remove_dc(), t.accent, t.bg_control, 2.0f);

        D2D1_RECT_F t_pol = D2D1::RectF(mx + 565.0f, my + 176.0f, mx + mw - 20.0f, my + 196.0f);
        D2DRenderer::draw_button(rt, font_small, t_pol, clipper->reverse_polarity() ? "● Reverse polarity" : "○ Reverse polarity",
                                clipper->reverse_polarity(), t.accent, t.bg_control, 2.0f);

        D2D1_RECT_F t_norm = D2D1::RectF(mx + 396.0f, my + 199.0f, mx + 555.0f, my + 219.0f);
        D2DRenderer::draw_button(rt, font_small, t_norm, clipper->normalize() ? "● Normalize" : "○ Normalize",
                                clipper->normalize(), t.accent, t.bg_control, 2.0f);

        D2D1_RECT_F t_fade = D2D1::RectF(mx + 565.0f, my + 199.0f, mx + mw - 20.0f, my + 219.0f);
        D2DRenderer::draw_button(rt, font_small, t_fade, clipper->fade_stereo() ? "● Fade stereo" : "○ Fade stereo",
                                clipper->fade_stereo(), t.accent, t.bg_control, 2.0f);

        D2D1_RECT_F t_rev = D2D1::RectF(mx + 396.0f, my + 222.0f, mx + 555.0f, my + 242.0f);
        D2DRenderer::draw_button(rt, font_small, t_rev, clipper->reverse() ? "● Reverse" : "○ Reverse",
                                clipper->reverse(), t.accent, t.bg_control, 2.0f);

        D2D1_RECT_F t_swap = D2D1::RectF(mx + 565.0f, my + 222.0f, mx + mw - 20.0f, my + 242.0f);
        D2DRenderer::draw_button(rt, font_small, t_swap, clipper->swap_stereo() ? "● Swap stereo" : "○ Swap stereo",
                                clipper->swap_stereo(), t.accent, t.bg_control, 2.0f);

        // 6 Knobs Row: SMP START, LENGTH (orange ring!), IN, OUT, CROSSFADE, TRIM
        float knob_w = (mw - 420.0f) / 6.0f;
        float knob_base_x = mx + 396.0f;

        D2D1_RECT_F k_sstart = D2D1::RectF(knob_base_x, my + 246.0f, knob_base_x + knob_w - 4.0f, my + 294.0f);
        D2DRenderer::draw_knob(rt, font_small, k_sstart, clipper->smp_start(), "SMP START", t.accent);

        D2D1_RECT_F k_slen = D2D1::RectF(knob_base_x + knob_w, my + 244.0f, knob_base_x + knob_w * 2.0f - 4.0f, my + 296.0f);
        // Orange circular ring highlight on LENGTH knob matching screenshot 1!
        D2DRenderer::draw_knob(rt, font_small, k_slen, clipper->smp_length(), "LENGTH", D2D1::ColorF(1.0f, 0.6f, 0.0f));

        D2D1_RECT_F k_in = D2D1::RectF(knob_base_x + knob_w * 2.0f, my + 246.0f, knob_base_x + knob_w * 3.0f - 4.0f, my + 294.0f);
        D2DRenderer::draw_knob(rt, font_small, k_in, clipper->in_fade(), "IN", t.accent);

        D2D1_RECT_F k_out = D2D1::RectF(knob_base_x + knob_w * 3.0f, my + 246.0f, knob_base_x + knob_w * 4.0f - 4.0f, my + 294.0f);
        D2DRenderer::draw_knob(rt, font_small, k_out, clipper->out_fade(), "OUT", t.accent);

        D2D1_RECT_F k_xfade = D2D1::RectF(knob_base_x + knob_w * 4.0f, my + 246.0f, knob_base_x + knob_w * 5.0f - 4.0f, my + 294.0f);
        D2DRenderer::draw_knob(rt, font_small, k_xfade, clipper->crossfade(), "CROSSFADE", t.accent);

        D2D1_RECT_F k_trim = D2D1::RectF(knob_base_x + knob_w * 5.0f, my + 246.0f, knob_base_x + knob_w * 6.0f - 4.0f, my + 294.0f);
        D2DRenderer::draw_knob(rt, font_small, k_trim, clipper->trim(), "TRIM", t.accent);

        // 6. Waveform Preview Display (Bottom Full Width)
        D2D1_RECT_F wave_box = D2D1::RectF(mx + 14.0f, my + 306.0f, mx + mw - 14.0f, my + mh - 14.0f);
        D2DRenderer::draw_rounded_box(rt, wave_box, D2D1::ColorF(0.06f, 0.08f, 0.10f), t.border_subtle, 4.0f);

        // Center 0-axis line
        float mid_y = (wave_box.top + wave_box.bottom) * 0.5f;
        ID2D1SolidColorBrush* br_axis = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.22f, 0.26f), &br_axis);
        if (br_axis) {
            rt->DrawLine(D2D1::Point2F(wave_box.left + 2.0f, mid_y), D2D1::Point2F(wave_box.right - 2.0f, mid_y), br_axis, 1.0f);
            br_axis->Release();
        }

        // Watermark "Sampler"
        D2D1_RECT_F watermark_rc = D2D1::RectF(wave_box.right - 180.0f, wave_box.top + 10.0f, wave_box.right - 15.0f, wave_box.top + 45.0f);
        D2DRenderer::draw_text(rt, font_bold, "Sampler", watermark_rc, D2D1::ColorF(0.22f, 0.28f, 0.33f), DWRITE_TEXT_ALIGNMENT_TRAILING);

        // Bottom right badge: "16 🗖"
        D2D1_RECT_F badge_rc = D2D1::RectF(wave_box.right - 80.0f, wave_box.bottom - 28.0f, wave_box.right - 10.0f, wave_box.bottom - 6.0f);
        std::string badge_str = std::to_string(clipper->bit_depth()) + " 🗖";
        D2DRenderer::draw_text(rt, font_small, badge_str, badge_rc, D2D1::ColorF(0.38f, 0.46f, 0.52f), DWRITE_TEXT_ALIGNMENT_TRAILING);

        // Draw audio sample waveform peaks
        const auto& sample = clipper->sample_l();
        if (!sample.empty()) {
            ID2D1SolidColorBrush* br_wave = nullptr;
            rt->CreateSolidColorBrush(D2D1::ColorF(0.82f, 0.86f, 0.89f), &br_wave);
            if (br_wave) {
                float wave_w = wave_box.right - wave_box.left - 6.0f;
                float wave_h = (wave_box.bottom - wave_box.top) * 0.44f;
                int num_pts = static_cast<int>(wave_w);
                size_t total_samples = sample.size();
                for (int p = 0; p < num_pts; ++p) {
                    size_t idx_start = static_cast<size_t>((p * total_samples) / num_pts);
                    size_t idx_end = static_cast<size_t>(((p + 1) * total_samples) / num_pts);
                    idx_end = std::min(idx_end, total_samples);
                    float min_v = 0.0f, max_v = 0.0f;
                    for (size_t k = idx_start; k < idx_end; ++k) {
                        min_v = std::min(min_v, sample[k]);
                        max_v = std::max(max_v, sample[k]);
                    }
                    float x_coord = wave_box.left + 3.0f + p;
                    float y_top = mid_y - max_v * wave_h;
                    float y_bot = mid_y - min_v * wave_h;
                    if (std::abs(y_bot - y_top) < 1.0f) y_bot = y_top + 1.0f;
                    rt->DrawLine(D2D1::Point2F(x_coord, y_top), D2D1::Point2F(x_coord, y_bot), br_wave, 1.0f);
                }
                br_wave->Release();
            }
        }
    }

    // --- Tab 1: Envelope & Instrument Settings (Screenshot 2) ---
    static void render_tab_envelope_d2d(ID2D1RenderTarget* rt, IDWriteTextFormat* font_bold, IDWriteTextFormat* font_small,
                                        plugins::AudioClipDevice* clipper, float mx, float my, float mw, float mh, int env_subtab) {
        const auto& t = D2DRenderer::theme();

        // 1. Top Sub-Tabs Bar: Panning, Volume, Mod X, Mod Y, Pitch
        const char* subtabs[] = {"Panning", "Volume", "Mod X", "Mod Y", "Pitch"};
        float stab_w = 72.0f;
        for (int i = 0; i < 5; ++i) {
            D2D1_RECT_F stab_rc = D2D1::RectF(mx + 14.0f + i * (stab_w + 4.0f), my + 44.0f, mx + 14.0f + (i + 1) * (stab_w + 4.0f) - 4.0f, my + 70.0f);
            bool is_active = (i == env_subtab);
            D2DRenderer::draw_button(rt, font_small, stab_rc, subtabs[i], is_active, t.accent, t.bg_control, 3.0f);
        }

        // 2. Left Panel: Envelope
        D2D1_RECT_F env_panel = D2D1::RectF(mx + 14.0f, my + 76.0f, mx + 386.0f, my + 346.0f);
        D2DRenderer::draw_rounded_box(rt, env_panel, t.bg_surface, t.border_subtle, 4.0f);

        // Header: LED toggle + "Envelope"
        D2D1_RECT_F env_led = D2D1::RectF(mx + 22.0f, my + 82.0f, mx + 36.0f, my + 96.0f);
        D2DRenderer::draw_button(rt, font_small, env_led, clipper->env_enabled() ? "●" : "○", clipper->env_enabled(),
                                D2D1::ColorF(0.2f, 0.85f, 0.5f), t.bg_control, 7.0f);

        D2D1_RECT_F env_title = D2D1::RectF(mx + 42.0f, my + 80.0f, mx + 200.0f, my + 98.0f);
        D2DRenderer::draw_text(rt, font_bold, "Envelope", env_title, t.text_primary);

        // Visual Envelope Curve Box
        D2D1_RECT_F env_box = D2D1::RectF(mx + 22.0f, my + 102.0f, mx + 378.0f, my + 198.0f);
        D2DRenderer::draw_rounded_box(rt, env_box, D2D1::ColorF(0.06f, 0.08f, 0.10f), t.border_subtle, 2.0f);

        // Compute and draw green ADSR curve
        float bw = env_box.right - env_box.left;
        float bh = env_box.bottom - env_box.top;
        float p0_x = env_box.left + 8.0f;
        float p0_y = env_box.bottom - 8.0f;

        float del_w = std::clamp(clipper->env_delay() / 2.0f, 0.0f, 0.15f) * bw;
        float att_w = std::clamp(clipper->env_attack() / 2.0f, 0.05f, 0.25f) * bw;
        float hold_w = std::clamp(clipper->env_hold() / 2.0f, 0.02f, 0.2f) * bw;
        float dec_w = std::clamp(clipper->env_decay() / 5.0f, 0.05f, 0.3f) * bw;
        float sus_y = (env_box.bottom - 8.0f) - clipper->env_sustain() * (bh - 16.0f);
        float rel_w = std::clamp(clipper->env_release() / 5.0f, 0.05f, 0.3f) * bw;

        float p1_x = p0_x + del_w;
        float p1_y = p0_y;
        float p2_x = p1_x + att_w;
        float p2_y = env_box.top + 8.0f;
        float p3_x = p2_x + hold_w;
        float p3_y = p2_y;
        float p4_x = p3_x + dec_w;
        float p4_y = sus_y;
        float p5_x = std::min(env_box.right - 8.0f, p4_x + rel_w);
        float p5_y = env_box.bottom - 8.0f;

        ID2D1SolidColorBrush* br_env = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.85f, 0.52f), &br_env);
        if (br_env) {
            rt->DrawLine(D2D1::Point2F(p0_x, p0_y), D2D1::Point2F(p1_x, p1_y), br_env, 2.0f);
            rt->DrawLine(D2D1::Point2F(p1_x, p1_y), D2D1::Point2F(p2_x, p2_y), br_env, 2.0f);
            rt->DrawLine(D2D1::Point2F(p2_x, p2_y), D2D1::Point2F(p3_x, p3_y), br_env, 2.0f);
            rt->DrawLine(D2D1::Point2F(p3_x, p3_y), D2D1::Point2F(p4_x, p4_y), br_env, 2.0f);
            rt->DrawLine(D2D1::Point2F(p4_x, p4_y), D2D1::Point2F(p5_x, p5_y), br_env, 2.0f);

            auto draw_node = [&](float nx, float ny) {
                D2D1_ELLIPSE el = D2D1::Ellipse(D2D1::Point2F(nx, ny), 3.5f, 3.5f);
                rt->FillEllipse(el, br_env);
            };
            draw_node(p0_x, p0_y);
            draw_node(p1_x, p1_y);
            draw_node(p2_x, p2_y);
            draw_node(p3_x, p3_y);
            draw_node(p4_x, p4_y);
            draw_node(p5_x, p5_y);
            br_env->Release();
        }

        // Knobs Row 1: DELAY, ATT, HOLD, DEC, SUS, REL
        float e_kw = (356.0f) / 6.0f;
        D2D1_RECT_F k_del = D2D1::RectF(mx + 22.0f, my + 206.0f, mx + 22.0f + e_kw - 4.0f, my + 256.0f);
        D2DRenderer::draw_knob(rt, font_small, k_del, clipper->env_delay() / 2.0f, "DELAY", t.accent);

        D2D1_RECT_F k_att = D2D1::RectF(mx + 22.0f + e_kw, my + 206.0f, mx + 22.0f + e_kw * 2.0f - 4.0f, my + 256.0f);
        D2DRenderer::draw_knob(rt, font_small, k_att, (clipper->env_attack() - 0.001f) / 1.999f, "ATT", t.accent);

        D2D1_RECT_F k_hld = D2D1::RectF(mx + 22.0f + e_kw * 2.0f, my + 206.0f, mx + 22.0f + e_kw * 3.0f - 4.0f, my + 256.0f);
        D2DRenderer::draw_knob(rt, font_small, k_hld, clipper->env_hold() / 2.0f, "HOLD", t.accent);

        D2D1_RECT_F k_dec = D2D1::RectF(mx + 22.0f + e_kw * 3.0f, my + 206.0f, mx + 22.0f + e_kw * 4.0f - 4.0f, my + 256.0f);
        D2DRenderer::draw_knob(rt, font_small, k_dec, (clipper->env_decay() - 0.001f) / 4.999f, "DEC", t.accent);

        D2D1_RECT_F k_sus = D2D1::RectF(mx + 22.0f + e_kw * 4.0f, my + 206.0f, mx + 22.0f + e_kw * 5.0f - 4.0f, my + 256.0f);
        D2DRenderer::draw_knob(rt, font_small, k_sus, clipper->env_sustain(), "SUS", t.accent);

        D2D1_RECT_F k_rel = D2D1::RectF(mx + 22.0f + e_kw * 5.0f, my + 206.0f, mx + 22.0f + e_kw * 6.0f - 4.0f, my + 256.0f);
        D2DRenderer::draw_knob(rt, font_small, k_rel, (clipper->env_release() - 0.001f) / 4.999f, "REL", t.accent);

        // Knobs Row 2: TENSION (under ATT), TENSION (under DEC), Tempo toggle
        D2D1_RECT_F k_tatt = D2D1::RectF(mx + 22.0f + e_kw, my + 264.0f, mx + 22.0f + e_kw * 2.0f - 4.0f, my + 314.0f);
        float norm_tatt = (clipper->env_att_tension() + 1.0f) * 0.5f;
        D2DRenderer::draw_knob(rt, font_small, k_tatt, norm_tatt, "TENSION", t.accent);

        D2D1_RECT_F k_tdec = D2D1::RectF(mx + 22.0f + e_kw * 3.0f, my + 264.0f, mx + 22.0f + e_kw * 4.0f - 4.0f, my + 314.0f);
        float norm_tdec = (clipper->env_dec_tension() + 1.0f) * 0.5f;
        D2DRenderer::draw_knob(rt, font_small, k_tdec, norm_tdec, "TENSION", t.accent);

        D2D1_RECT_F t_tempo = D2D1::RectF(mx + 22.0f + e_kw * 4.5f, my + 274.0f, mx + 372.0f, my + 304.0f);
        D2DRenderer::draw_button(rt, font_small, t_tempo, clipper->env_tempo_sync() ? "● Tempo" : "○ Tempo",
                                clipper->env_tempo_sync(), t.accent, t.bg_control, 3.0f);

        // 3. Middle Panel: LFO
        D2D1_RECT_F lfo_panel = D2D1::RectF(mx + 396.0f, my + 76.0f, mx + 596.0f, my + 346.0f);
        D2DRenderer::draw_rounded_box(rt, lfo_panel, t.bg_surface, t.border_subtle, 4.0f);

        // Wave Shape Buttons & Title
        D2D1_RECT_F shape_sine = D2D1::RectF(mx + 404.0f, my + 80.0f, mx + 424.0f, my + 98.0f);
        D2DRenderer::draw_button(rt, font_small, shape_sine, "~", (clipper->lfo_shape() == 0), t.accent, t.bg_control, 2.0f);

        D2D1_RECT_F shape_tri = D2D1::RectF(mx + 428.0f, my + 80.0f, mx + 448.0f, my + 98.0f);
        D2DRenderer::draw_button(rt, font_small, shape_tri, "⋀", (clipper->lfo_shape() == 1), t.accent, t.bg_control, 2.0f);

        D2D1_RECT_F shape_sqr = D2D1::RectF(mx + 452.0f, my + 80.0f, mx + 472.0f, my + 98.0f);
        D2DRenderer::draw_button(rt, font_small, shape_sqr, "⊓", (clipper->lfo_shape() == 2), t.accent, t.bg_control, 2.0f);

        D2D1_RECT_F lfo_title = D2D1::RectF(mx + 480.0f, my + 80.0f, mx + 580.0f, my + 98.0f);
        D2DRenderer::draw_text(rt, font_bold, "LFO", lfo_title, t.text_primary);

        // Visual LFO Display Box
        D2D1_RECT_F lfo_box = D2D1::RectF(mx + 404.0f, my + 102.0f, mx + 588.0f, my + 198.0f);
        D2DRenderer::draw_rounded_box(rt, lfo_box, D2D1::ColorF(0.06f, 0.08f, 0.10f), t.border_subtle, 2.0f);

        float lfo_mid_y = (lfo_box.top + lfo_box.bottom) * 0.5f;
        ID2D1SolidColorBrush* br_lfo = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.85f, 0.52f), &br_lfo);
        if (br_lfo) {
            float amt = clipper->lfo_amount();
            float prev_x = lfo_box.left + 2.0f;
            float prev_y = lfo_mid_y;
            for (float lx = lfo_box.left + 2.0f; lx <= lfo_box.right - 2.0f; lx += 2.0f) {
                float ph = (lx - lfo_box.left) * 0.04f * clipper->lfo_speed();
                float cur_y = lfo_mid_y - std::sin(ph) * amt * 35.0f;
                rt->DrawLine(D2D1::Point2F(prev_x, prev_y), D2D1::Point2F(lx, cur_y), br_lfo, 1.5f);
                prev_x = lx;
                prev_y = cur_y;
            }
            br_lfo->Release();
        }

        // LFO Knobs: DELAY, ATT, AMT, SPEED
        float l_kw = (184.0f) / 4.0f;
        D2D1_RECT_F k_ldel = D2D1::RectF(mx + 404.0f, my + 206.0f, mx + 404.0f + l_kw - 4.0f, my + 256.0f);
        D2DRenderer::draw_knob(rt, font_small, k_ldel, clipper->lfo_delay() / 2.0f, "DELAY", t.accent);

        D2D1_RECT_F k_latt = D2D1::RectF(mx + 404.0f + l_kw, my + 206.0f, mx + 404.0f + l_kw * 2.0f - 4.0f, my + 256.0f);
        D2DRenderer::draw_knob(rt, font_small, k_latt, clipper->lfo_attack() / 2.0f, "ATT", t.accent);

        D2D1_RECT_F k_lamt = D2D1::RectF(mx + 404.0f + l_kw * 2.0f, my + 206.0f, mx + 404.0f + l_kw * 3.0f - 4.0f, my + 256.0f);
        D2DRenderer::draw_knob(rt, font_small, k_lamt, clipper->lfo_amount(), "AMT", t.accent);

        D2D1_RECT_F k_lspd = D2D1::RectF(mx + 404.0f + l_kw * 3.0f, my + 206.0f, mx + 404.0f + l_kw * 4.0f - 4.0f, my + 256.0f);
        float norm_lspd = (clipper->lfo_speed() - 0.1f) / 19.9f;
        D2DRenderer::draw_knob(rt, font_small, k_lspd, norm_lspd, "SPEED", t.accent);

        // Toggles: Tempo, Global
        D2D1_RECT_F t_ltempo = D2D1::RectF(mx + 404.0f, my + 274.0f, mx + 490.0f, my + 304.0f);
        D2DRenderer::draw_button(rt, font_small, t_ltempo, clipper->lfo_tempo_sync() ? "● Tempo" : "○ Tempo",
                                clipper->lfo_tempo_sync(), t.accent, t.bg_control, 3.0f);

        D2D1_RECT_F t_lglobal = D2D1::RectF(mx + 498.0f, my + 274.0f, mx + 588.0f, my + 304.0f);
        D2DRenderer::draw_button(rt, font_small, t_lglobal, clipper->lfo_global() ? "● Global" : "○ Global",
                                clipper->lfo_global(), t.accent, t.bg_control, 3.0f);

        // 4. Right Panel: Filter
        D2D1_RECT_F flt_panel = D2D1::RectF(mx + 606.0f, my + 76.0f, mx + mw - 14.0f, my + 346.0f);
        D2DRenderer::draw_rounded_box(rt, flt_panel, t.bg_surface, t.border_subtle, 4.0f);

        D2D1_RECT_F flt_title = D2D1::RectF(mx + 616.0f, my + 80.0f, mx + 710.0f, my + 98.0f);
        D2DRenderer::draw_text(rt, font_bold, "Filter", flt_title, t.text_primary);

        D2D1_RECT_F k_modx = D2D1::RectF(mx + 640.0f, my + 115.0f, mx + 710.0f, my + 175.0f);
        D2DRenderer::draw_knob(rt, font_small, k_modx, clipper->filter_mod_x(), "MOD X", t.accent);

        D2D1_RECT_F k_mody = D2D1::RectF(mx + 640.0f, my + 190.0f, mx + 710.0f, my + 250.0f);
        D2DRenderer::draw_knob(rt, font_small, k_mody, clipper->filter_mod_y(), "MOD Y", t.accent);

        D2D1_RECT_F flt_type_btn = D2D1::RectF(mx + 618.0f, my + 274.0f, mx + mw - 24.0f, my + 304.0f);
        const char* flt_names[] = {"Fast LP  ▼", "Fast HP  ▼", "Fast BP  ▼"};
        int ftype = std::clamp(clipper->filter_type(), 0, 2);
        D2DRenderer::draw_button(rt, font_small, flt_type_btn, flt_names[ftype], false, t.accent, t.bg_control, 3.0f);

        // 5. Bottom Section: Root Note & Virtual Piano Keyboard
        D2D1_RECT_F kb_panel = D2D1::RectF(mx + 14.0f, my + 352.0f, mx + mw - 14.0f, my + mh - 14.0f);
        D2DRenderer::draw_rounded_box(rt, kb_panel, t.bg_surface, t.border_subtle, 4.0f);

        // Root note label
        std::string root_str = "Root note: " + midi_note_to_name(clipper->root_key());
        D2D1_RECT_F root_lbl = D2D1::RectF(mx + 22.0f, my + 356.0f, mx + 130.0f, my + 380.0f);
        D2DRenderer::draw_text(rt, font_bold, root_str, root_lbl, t.text_primary, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Reset Button
        D2D1_RECT_F btn_reset = D2D1::RectF(mx + 138.0f, my + 356.0f, mx + 198.0f, my + 380.0f);
        D2DRenderer::draw_button(rt, font_small, btn_reset, "Reset", false, t.accent, t.bg_control, 3.0f);

        // Enable main pitch toggle
        D2D1_RECT_F t_main_pitch = D2D1::RectF(mx + 208.0f, my + 356.0f, mx + 348.0f, my + 380.0f);
        D2DRenderer::draw_button(rt, font_small, t_main_pitch, clipper->enable_main_pitch() ? "● Enable main pitch" : "○ Enable main pitch",
                                clipper->enable_main_pitch(), D2D1::ColorF(1.0f, 0.45f, 0.35f), t.bg_control, 3.0f);

        // Add to key toggle
        D2D1_RECT_F t_add_key = D2D1::RectF(mx + 356.0f, my + 356.0f, mx + 456.0f, my + 380.0f);
        D2DRenderer::draw_button(rt, font_small, t_add_key, clipper->add_to_key() ? "● Add to key" : "○ Add to key",
                                clipper->add_to_key(), t.accent, t.bg_control, 3.0f);

        // Fine tune slider
        D2D1_RECT_F fine_rc = D2D1::RectF(mx + 466.0f, my + 356.0f, mx + mw - 24.0f, my + 380.0f);
        float norm_fine = (clipper->fine_tune_cents() / 200.0f) + 0.5f;
        std::stringstream ss_fine;
        ss_fine << "Fine tune: " << (clipper->fine_tune_cents() >= 0.0f ? "+" : "") << static_cast<int>(clipper->fine_tune_cents()) << " ct";
        D2DRenderer::draw_slider_horizontal(rt, font_small, fine_rc, norm_fine, ss_fine.str());

        // Virtual Piano Keyboard (Octaves C2 to C8 = 7 octaves, 49 white keys)
        float kb_x = mx + 20.0f;
        float kb_y = my + 388.0f;
        float kb_w = mw - 40.0f;
        float kb_h = (my + mh - 20.0f) - kb_y;

        int num_white = 49; // 7 octaves * 7 white keys
        float wk_w = kb_w / static_cast<float>(num_white);
        float wk_h = kb_h;
        float bk_w = wk_w * 0.65f;
        float bk_h = wk_h * 0.62f;

        const int white_semis[7] = {0, 2, 4, 5, 7, 9, 11};
        const int black_semis[5] = {1, 3, 6, 8, 10};
        const float black_offsets[5] = {0.68f, 1.72f, 3.65f, 4.68f, 5.72f};

        ID2D1SolidColorBrush* br_white = nullptr;
        ID2D1SolidColorBrush* br_white_border = nullptr;
        ID2D1SolidColorBrush* br_black = nullptr;
        ID2D1SolidColorBrush* br_root = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(0.85f, 0.86f, 0.88f), &br_white);
        rt->CreateSolidColorBrush(D2D1::ColorF(0.2f, 0.22f, 0.25f), &br_white_border);
        rt->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.14f, 0.16f), &br_black);
        // Soft blue / cyan highlight for active Root Note (C5 by default, matching screenshot 2!)
        rt->CreateSolidColorBrush(D2D1::ColorF(0.44f, 0.63f, 1.0f), &br_root);

        // Draw White Keys
        int wk_idx = 0;
        for (int oct = 2; oct <= 8; ++oct) {
            for (int w = 0; w < 7; ++w) {
                uint8_t note_val = static_cast<uint8_t>(oct * 12 + white_semis[w]);
                float kx = kb_x + wk_idx * wk_w;
                D2D1_RECT_F k_rc = D2D1::RectF(kx, kb_y, kx + wk_w - 1.0f, kb_y + wk_h);

                bool is_root = (note_val == clipper->root_key());
                if (is_root && br_root) {
                    rt->FillRectangle(k_rc, br_root);
                } else if (br_white) {
                    rt->FillRectangle(k_rc, br_white);
                }
                if (br_white_border) {
                    rt->DrawRectangle(k_rc, br_white_border, 1.0f);
                }

                // Draw Note label on C keys (C2, C3, C4, C5, C6, C7, C8)
                if (w == 0) {
                    D2D1_RECT_F lbl_rc = D2D1::RectF(kx, kb_y + 4.0f, kx + wk_w, kb_y + 20.0f);
                    std::string c_name = "C" + std::to_string(oct);
                    D2DRenderer::draw_text(rt, font_small, c_name, lbl_rc, D2D1::ColorF(0.25f, 0.28f, 0.32f),
                                          DWRITE_TEXT_ALIGNMENT_CENTER);
                }

                wk_idx++;
            }
        }

        // Draw Black Keys (On top)
        for (int oct = 2; oct <= 8; ++oct) {
            float oct_base_x = kb_x + (oct - 2) * 7.0f * wk_w;
            for (int b = 0; b < 5; ++b) {
                uint8_t note_val = static_cast<uint8_t>(oct * 12 + black_semis[b]);
                float bk_x = oct_base_x + black_offsets[b] * wk_w - bk_w * 0.5f;
                D2D1_RECT_F bk_rc = D2D1::RectF(bk_x, kb_y, bk_x + bk_w, kb_y + bk_h);

                bool is_root = (note_val == clipper->root_key());
                if (is_root && br_root) {
                    rt->FillRectangle(bk_rc, br_root);
                } else if (br_black) {
                    rt->FillRectangle(bk_rc, br_black);
                }
                if (br_white_border) {
                    rt->DrawRectangle(bk_rc, br_white_border, 1.0f);
                }
            }
        }

        if (br_white) br_white->Release();
        if (br_white_border) br_white_border->Release();
        if (br_black) br_black->Release();
        if (br_root) br_root->Release();
    }

    // --- Tab 2: Miscellaneous Settings ---
    static void render_tab_misc_d2d(ID2D1RenderTarget* rt, IDWriteTextFormat* font_bold, IDWriteTextFormat* font_small,
                                    plugins::AudioClipDevice* clipper, float mx, float my, float mw, float mh) {
        const auto& t = D2DRenderer::theme();

        D2D1_RECT_F panel_rc = D2D1::RectF(mx + 14.0f, my + 44.0f, mx + mw - 14.0f, my + mh - 14.0f);
        D2DRenderer::draw_rounded_box(rt, panel_rc, t.bg_surface, t.border_subtle, 4.0f);

        D2D1_RECT_F title_rc = D2D1::RectF(mx + 24.0f, my + 54.0f, mx + 300.0f, my + 80.0f);
        D2DRenderer::draw_text(rt, font_bold, "Miscellaneous Channel Settings", title_rc, t.accent);

        D2D1_RECT_F desc_rc = D2D1::RectF(mx + 24.0f, my + 90.0f, mx + mw - 24.0f, my + 130.0f);
        D2DRenderer::draw_text(rt, font_small, "Configure sample playback routing, interpolation mode, and MIDI channel routing.", desc_rc, t.text_secondary);

        // Info box
        D2D1_RECT_F info_rc = D2D1::RectF(mx + 24.0f, my + 140.0f, mx + 400.0f, my + 260.0f);
        D2DRenderer::draw_rounded_box(rt, info_rc, t.bg_control, t.border_subtle, 2.0f);

        std::string info_text = "Sample Info:\n"
                                "• File: " + clipper->filename() + "\n"
                                "• Bit Depth: " + std::to_string(clipper->bit_depth()) + "-bit PCM\n"
                                "• Frames: " + std::to_string(clipper->sample_frames()) + "\n"
                                "• Root Key: " + midi_note_to_name(clipper->root_key()) + " (MIDI " + std::to_string(clipper->root_key()) + ")\n"
                                "• Resampling: Hermite 4-Point Cubic Interpolation";

        D2DRenderer::draw_text(rt, font_small, info_text, info_rc, t.text_primary);
    }

    // --- GDI Fallback Rendering ---
    static void render_gdi(HDC hdc, plugins::AudioClipDevice* clipper, const RECT& bounds,
                           int active_tab, int env_subtab, uint8_t mixer_track) {
        if (!hdc || !clipper) return;
        const auto& t = get_theme();

        // Background
        GuiRenderer::draw_rounded_box(hdc, bounds, t.bg_elevated, t.border_strong, 6);

        // Header
        RECT hdr_rc{bounds.left, bounds.top, bounds.right, bounds.top + 38};
        GuiRenderer::draw_rounded_box(hdc, hdr_rc, t.bg_surface, t.border_subtle, 6);

        RECT title_rc{bounds.left + 14, bounds.top + 6, bounds.left + 220, bounds.top + 32};
        std::string title = "Clipper — " + clipper->filename();
        GuiRenderer::draw_text(hdc, title, title_rc, t.accent, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Mixer Track LCD
        RECT trk_rc{bounds.left + 230, bounds.top + 6, bounds.left + 300, bounds.top + 32};
        std::string trk_str = (mixer_track == 0) ? "MST" : ("TRK " + std::to_string(mixer_track));
        GuiRenderer::draw_button(hdc, trk_rc, trk_str, false, t.accent, t.bg_control);

        // Header Knobs: PAN, VOL, PITCH
        RECT pan_rc{bounds.left + 310, bounds.top + 4, bounds.left + 348, bounds.top + 34};
        float norm_pan = (clipper->pan() + 1.0f) * 0.5f;
        GuiRenderer::draw_knob(hdc, pan_rc, norm_pan, "PAN", t.accent);

        RECT vol_rc{bounds.left + 356, bounds.top + 4, bounds.left + 394, bounds.top + 34};
        GuiRenderer::draw_knob(hdc, vol_rc, clipper->master_volume(), "VOL", t.accent);

        RECT pitch_rc{bounds.left + 402, bounds.top + 4, bounds.left + 440, bounds.top + 34};
        float norm_pitch = (clipper->pitch_shift() / 24.0f) + 0.5f;
        GuiRenderer::draw_knob(hdc, pitch_rc, norm_pitch, "PITCH", t.accent);

        // Tabs
        RECT tab0_rc{bounds.left + 458, bounds.top + 6, bounds.left + 538, bounds.top + 32};
        GuiRenderer::draw_button(hdc, tab0_rc, "Sample", (active_tab == 0), t.accent, t.bg_control);

        RECT tab1_rc{bounds.left + 544, bounds.top + 6, bounds.left + 634, bounds.top + 32};
        GuiRenderer::draw_button(hdc, tab1_rc, "Env/Inst", (active_tab == 1), t.accent, t.bg_control);

        RECT tab2_rc{bounds.left + 640, bounds.top + 6, bounds.left + 710, bounds.top + 32};
        GuiRenderer::draw_button(hdc, tab2_rc, "Misc", (active_tab == 2), t.accent, t.bg_control);

        // Close Button
        RECT close_rc{bounds.right - 38, bounds.top + 6, bounds.right - 8, bounds.top + 32};
        GuiRenderer::draw_button(hdc, close_rc, "X", false, t.danger, t.bg_control);

        // Tab Content
        if (active_tab == 0) {
            // File Box
            RECT file_box{bounds.left + 14, bounds.top + 44, bounds.left + 372, bounds.top + 78};
            GuiRenderer::draw_rounded_box(hdc, file_box, t.bg_surface, t.border_subtle, 4);
            RECT file_lbl{bounds.left + 22, bounds.top + 48, bounds.left + 280, bounds.top + 74};
            GuiRenderer::draw_text(hdc, "File  " + clipper->filename(), file_lbl, t.text_primary, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            RECT b_browse{bounds.left + 290, bounds.top + 48, bounds.left + 316, bounds.top + 74};
            GuiRenderer::draw_button(hdc, b_browse, "Load", false, t.accent, t.bg_control);
            RECT b_reload{bounds.left + 320, bounds.top + 48, bounds.left + 344, bounds.top + 74};
            GuiRenderer::draw_button(hdc, b_reload, "Rld", false, t.accent, t.bg_control);
            RECT b_clr{bounds.left + 348, bounds.top + 48, bounds.left + 368, bounds.top + 74};
            GuiRenderer::draw_button(hdc, b_clr, "X", false, t.text_muted, t.bg_control);

            // Waveform Box
            RECT wave_box{bounds.left + 14, bounds.top + 306, bounds.right - 14, bounds.bottom - 14};
            GuiRenderer::draw_rounded_box(hdc, wave_box, RGB(16, 20, 24), t.border_subtle, 2);

            RECT wmark_rc{wave_box.right - 140, wave_box.top + 10, wave_box.right - 10, wave_box.top + 40};
            GuiRenderer::draw_text(hdc, "Sampler", wmark_rc, RGB(55, 71, 79), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

            int mid_y = (wave_box.top + wave_box.bottom) / 2;
            HPEN pen_axis = CreatePen(PS_SOLID, 1, RGB(46, 56, 66));
            HGDIOBJ old_pen = SelectObject(hdc, pen_axis);
            MoveToEx(hdc, wave_box.left + 2, mid_y, NULL);
            LineTo(hdc, wave_box.right - 2, mid_y);

            const auto& sample = clipper->sample_l();
            if (!sample.empty()) {
                HPEN pen_wave = CreatePen(PS_SOLID, 1, RGB(210, 220, 228));
                SelectObject(hdc, pen_wave);
                int num_pts = wave_box.right - wave_box.left - 6;
                float wave_h = (wave_box.bottom - wave_box.top) * 0.44f;
                size_t total = sample.size();
                for (int p = 0; p < num_pts; ++p) {
                    size_t s_idx = (p * total) / num_pts;
                    float val = sample[s_idx];
                    int y_top = mid_y - static_cast<int>(val * wave_h);
                    MoveToEx(hdc, wave_box.left + 3 + p, mid_y, NULL);
                    LineTo(hdc, wave_box.left + 3 + p, y_top);
                }
                SelectObject(hdc, old_pen);
                DeleteObject(pen_wave);
            } else {
                SelectObject(hdc, old_pen);
            }
            DeleteObject(pen_axis);
        } else if (active_tab == 1) {
            // Subtabs
            const char* subtabs[] = {"Panning", "Volume", "Mod X", "Mod Y", "Pitch"};
            for (int i = 0; i < 5; ++i) {
                RECT stab_rc{bounds.left + 14 + i * 76, bounds.top + 44, bounds.left + 14 + (i + 1) * 76 - 4, bounds.top + 70};
                GuiRenderer::draw_button(hdc, stab_rc, subtabs[i], (i == env_subtab), t.accent, t.bg_control);
            }

            // Virtual Keyboard
            int kb_x = bounds.left + 20;
            int kb_y = bounds.top + 388;
            int kb_w = (bounds.right - bounds.left) - 40;
            int kb_h = (bounds.bottom - 20) - kb_y;

            int num_white = 49;
            float wk_w = static_cast<float>(kb_w) / static_cast<float>(num_white);
            const int white_semis[7] = {0, 2, 4, 5, 7, 9, 11};

            int wk_idx = 0;
            for (int oct = 2; oct <= 8; ++oct) {
                for (int w = 0; w < 7; ++w) {
                    uint8_t note_val = static_cast<uint8_t>(oct * 12 + white_semis[w]);
                    int kx = kb_x + static_cast<int>(wk_idx * wk_w);
                    RECT k_rc{kx, kb_y, kx + static_cast<int>(wk_w) - 1, kb_y + kb_h};

                    bool is_root = (note_val == clipper->root_key());
                    COLORREF fill_col = is_root ? RGB(112, 161, 255) : RGB(220, 221, 225);
                    GuiRenderer::draw_rounded_box(hdc, k_rc, fill_col, RGB(50, 55, 65), 1);
                    wk_idx++;
                }
            }
        }
    }

    // --- Click Handling ---
    static bool handle_click(HWND hwnd, plugins::AudioClipDevice* clipper, const RECT& bounds,
                             int x, int y, int& active_tab, int& env_subtab, uint8_t& mixer_track,
                             ClipperKnobId& dragging_knob, int& drag_start_y, float& drag_orig_val,
                             std::string& status_msg, std::function<void(uint8_t)> audition_cb) {
        if (!clipper) return false;

        float mx = static_cast<float>(bounds.left);
        float my = static_cast<float>(bounds.top);
        float mw = static_cast<float>(bounds.right - bounds.left);
        float mh = static_cast<float>(bounds.bottom - bounds.top);

        // 1. Close Button
        if (x >= bounds.right - 38 && x <= bounds.right - 8 && y >= bounds.top + 6 && y <= bounds.top + 32) {
            return true; // Signals to close editor
        }

        // 2. Tabs Switcher
        if (y >= bounds.top + 6 && y <= bounds.top + 32) {
            if (x >= mx + 458.0f && x <= mx + 538.0f) {
                active_tab = 0;
                status_msg = "Switched to Sample & Waveform Settings";
                return false;
            }
            if (x >= mx + 544.0f && x <= mx + 634.0f) {
                active_tab = 1;
                status_msg = "Switched to Envelope & Instrument Settings";
                return false;
            }
            if (x >= mx + 640.0f && x <= mx + 710.0f) {
                active_tab = 2;
                status_msg = "Switched to Misc Channel Settings";
                return false;
            }
            // Header Mixer Track LCD
            if (x >= mx + 230.0f && x <= mx + 300.0f) {
                mixer_track = (mixer_track + 1) % 64;
                status_msg = "Target Mixer Track set to: " + (mixer_track == 0 ? "Master" : std::to_string(mixer_track));
                return false;
            }
        }

        // 3. Header Knobs
        if (y >= bounds.top + 4 && y <= bounds.top + 34) {
            if (x >= mx + 310.0f && x <= mx + 348.0f) {
                dragging_knob = ClipperKnobId::HeaderPan;
                drag_start_y = y;
                drag_orig_val = clipper->pan();
                status_msg = "Adjusting Channel Pan";
                return false;
            }
            if (x >= mx + 356.0f && x <= mx + 394.0f) {
                dragging_knob = ClipperKnobId::HeaderVol;
                drag_start_y = y;
                drag_orig_val = clipper->master_volume();
                status_msg = "Adjusting Channel Volume";
                return false;
            }
            if (x >= mx + 402.0f && x <= mx + 440.0f) {
                dragging_knob = ClipperKnobId::HeaderPitch;
                drag_start_y = y;
                drag_orig_val = clipper->pitch_shift();
                status_msg = "Adjusting Channel Pitch";
                return false;
            }
        }

        // --- Tab 0 Clicks (Sample / Waveform) ---
        if (active_tab == 0) {
            // Browse File [📁]
            if (x >= mx + 290.0f && x <= mx + 316.0f && y >= my + 48.0f && y <= my + 74.0f) {
                OPENFILENAMEA ofn;
                char szFile[MAX_PATH] = "";
                ZeroMemory(&ofn, sizeof(ofn));
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFile = szFile;
                ofn.nMaxFile = sizeof(szFile);
                ofn.lpstrFilter = "WAVE Audio Files (*.wav)\0*.wav\0All Files (*.*)\0*.*\0";
                ofn.nFilterIndex = 1;
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
                if (GetOpenFileNameA(&ofn)) {
                    if (clipper->load_wav_file(szFile)) {
                        status_msg = "Loaded Audio File: " + clipper->filename() + " (Default Root Key: C5)";
                    } else {
                        status_msg = "Failed to load audio file!";
                    }
                }
                return false;
            }

            // Reload Sample [⟳]
            if (x >= mx + 320.0f && x <= mx + 344.0f && y >= my + 48.0f && y <= my + 74.0f) {
                clipper->init_default_sample();
                status_msg = "Reloaded default 808 Kick audio clip (Default Root Key: C5)";
                return false;
            }

            // Clear Sample [✕]
            if (x >= mx + 348.0f && x <= mx + 368.0f && y >= my + 48.0f && y <= my + 74.0f) {
                clipper->clear_sample();
                status_msg = "Cleared Audio Clip Sample";
                return false;
            }

            // Content Toggles
            if (x >= mx + 22.0f && x <= mx + 180.0f && y >= my + 110.0f && y <= my + 130.0f) {
                clipper->set_keep_on_disk(!clipper->keep_on_disk());
                status_msg = clipper->keep_on_disk() ? "Enabled Keep On Disk" : "Disabled Keep On Disk";
                return false;
            }
            if (x >= mx + 195.0f && x <= mx + 362.0f && y >= my + 110.0f && y <= my + 130.0f) {
                clipper->set_load_regions(!clipper->load_regions());
                status_msg = clipper->load_regions() ? "Enabled Load Regions" : "Disabled Load Regions";
                return false;
            }
            if (x >= mx + 22.0f && x <= mx + 180.0f && y >= my + 136.0f && y <= my + 156.0f) {
                clipper->set_resample_enabled(!clipper->resample_enabled());
                status_msg = clipper->resample_enabled() ? "Resampling Mode Active (Hermite)" : "Direct Mode Active (Linear)";
                return false;
            }
            if (x >= mx + 195.0f && x <= mx + 362.0f && y >= my + 136.0f && y <= my + 156.0f) {
                clipper->set_load_slice_markers(!clipper->load_slice_markers());
                status_msg = clipper->load_slice_markers() ? "Enabled Slice Markers" : "Disabled Slice Markers";
                return false;
            }

            // Declicking Mode Dropdown
            if (x >= mx + 145.0f && x <= mx + 362.0f && y >= my + 168.0f && y <= my + 196.0f) {
                clipper->set_declicking_mode((clipper->declicking_mode() + 1) % 3);
                const char* decl_names[] = {"Out only (no bleeding)", "Transient (no bleeding)", "Smooth (bleeding)"};
                status_msg = "Declicking Mode: " + std::string(decl_names[clipper->declicking_mode()]);
                return false;
            }

            // Playback Toggles & Knobs
            if (x >= mx + 45.0f && x <= mx + 95.0f && y >= my + 236.0f && y <= my + 288.0f) {
                dragging_knob = ClipperKnobId::StartOffset;
                drag_start_y = y;
                drag_orig_val = clipper->start_offset();
                status_msg = "Adjusting Start Offset";
                return false;
            }
            if (x >= mx + 160.0f && x <= mx + 360.0f && y >= my + 236.0f && y <= my + 258.0f) {
                clipper->set_use_loop_points(!clipper->use_loop_points());
                status_msg = clipper->use_loop_points() ? "Enabled Loop Points" : "Disabled Loop Points";
                return false;
            }
            if (x >= mx + 160.0f && x <= mx + 360.0f && y >= my + 264.0f && y <= my + 286.0f) {
                clipper->set_ping_pong_loop(!clipper->ping_pong_loop());
                status_msg = clipper->ping_pong_loop() ? "Enabled Ping-Pong Looping" : "Disabled Ping-Pong Looping";
                return false;
            }

            // Time Stretching Knobs: PITCH, MUL, TIME
            if (y >= my + 72.0f && y <= my + 128.0f) {
                if (x >= mx + 406.0f && x <= mx + 456.0f) {
                    dragging_knob = ClipperKnobId::TimePitch;
                    drag_start_y = y;
                    drag_orig_val = clipper->pitch_shift();
                    status_msg = "Adjusting Time Stretch Pitch";
                    return false;
                }
                if (x >= mx + 476.0f && x <= mx + 526.0f) {
                    dragging_knob = ClipperKnobId::TimeMul;
                    drag_start_y = y;
                    drag_orig_val = clipper->time_mul();
                    status_msg = "Adjusting Time Stretch Mul";
                    return false;
                }
                if (x >= mx + 546.0f && x <= mx + 596.0f) {
                    dragging_knob = ClipperKnobId::TimeTime;
                    drag_start_y = y;
                    drag_orig_val = clipper->time_length();
                    status_msg = "Adjusting Time Duration";
                    return false;
                }
            }

            // Time Stretching Mode Dropdown
            if (x >= mx + 660.0f && x <= mx + mw - 24.0f && y >= my + 72.0f && y <= my + 100.0f) {
                clipper->set_stretch_mode((clipper->stretch_mode() + 1) % 4);
                const char* sm_names[] = {"Resample", "Auto", "Stretch", "Slice"};
                status_msg = "Time Stretch Mode: " + std::string(sm_names[clipper->stretch_mode()]);
                return false;
            }

            // Precomputed Effects Toggles
            if (x >= mx + 396.0f && x <= mx + 555.0f && y >= my + 176.0f && y <= my + 196.0f) {
                clipper->set_remove_dc(!clipper->remove_dc());
                status_msg = clipper->remove_dc() ? "Enabled Remove DC Offset" : "Disabled Remove DC Offset";
                return false;
            }
            if (x >= mx + 565.0f && x <= mx + mw - 20.0f && y >= my + 176.0f && y <= my + 196.0f) {
                clipper->set_reverse_polarity(!clipper->reverse_polarity());
                status_msg = clipper->reverse_polarity() ? "Inverted Phase (Reverse Polarity)" : "Normal Phase";
                return false;
            }
            if (x >= mx + 396.0f && x <= mx + 555.0f && y >= my + 199.0f && y <= my + 219.0f) {
                clipper->set_normalize(!clipper->normalize());
                status_msg = clipper->normalize() ? "Normalized Audio Sample to 0 dBFS" : "Original Gain";
                return false;
            }
            if (x >= mx + 565.0f && x <= mx + mw - 20.0f && y >= my + 199.0f && y <= my + 219.0f) {
                clipper->set_fade_stereo(!clipper->fade_stereo());
                status_msg = clipper->fade_stereo() ? "Applied Stereo Fade" : "Stereo Fade Off";
                return false;
            }
            if (x >= mx + 396.0f && x <= mx + 555.0f && y >= my + 222.0f && y <= my + 242.0f) {
                clipper->set_reverse(!clipper->reverse());
                status_msg = clipper->reverse() ? "Reversed Sample" : "Forward Sample";
                return false;
            }
            if (x >= mx + 565.0f && x <= mx + mw - 20.0f && y >= my + 222.0f && y <= my + 242.0f) {
                clipper->set_swap_stereo(!clipper->swap_stereo());
                status_msg = clipper->swap_stereo() ? "Swapped Left/Right Channels" : "Normal Stereo";
                return false;
            }

            // Precomputed Knobs Row
            if (y >= my + 244.0f && y <= my + 296.0f) {
                float kw = (mw - 420.0f) / 6.0f;
                float kbase = mx + 396.0f;
                if (x >= kbase && x <= kbase + kw) {
                    dragging_knob = ClipperKnobId::SmpStart;
                    drag_start_y = y;
                    drag_orig_val = clipper->smp_start();
                    status_msg = "Adjusting Sample Start Position";
                    return false;
                }
                if (x >= kbase + kw && x <= kbase + kw * 2.0f) {
                    dragging_knob = ClipperKnobId::SmpLength;
                    drag_start_y = y;
                    drag_orig_val = clipper->smp_length();
                    status_msg = "Adjusting Sample Length Window";
                    return false;
                }
                if (x >= kbase + kw * 2.0f && x <= kbase + kw * 3.0f) {
                    dragging_knob = ClipperKnobId::InFade;
                    drag_start_y = y;
                    drag_orig_val = clipper->in_fade();
                    status_msg = "Adjusting In-Fade Length";
                    return false;
                }
                if (x >= kbase + kw * 3.0f && x <= kbase + kw * 4.0f) {
                    dragging_knob = ClipperKnobId::OutFade;
                    drag_start_y = y;
                    drag_orig_val = clipper->out_fade();
                    status_msg = "Adjusting Out-Fade Length";
                    return false;
                }
                if (x >= kbase + kw * 4.0f && x <= kbase + kw * 5.0f) {
                    dragging_knob = ClipperKnobId::Crossfade;
                    drag_start_y = y;
                    drag_orig_val = clipper->crossfade();
                    status_msg = "Adjusting Crossfade";
                    return false;
                }
                if (x >= kbase + kw * 5.0f && x <= kbase + kw * 6.0f) {
                    dragging_knob = ClipperKnobId::Trim;
                    drag_start_y = y;
                    drag_orig_val = clipper->trim();
                    status_msg = "Adjusting Trim Threshold";
                    return false;
                }
            }

            // Waveform Preview Click -> Audition sample at root key!
            if (x >= mx + 14.0f && x <= mx + mw - 14.0f && y >= my + 306.0f && y <= my + mh - 14.0f) {
                if (audition_cb) audition_cb(clipper->root_key());
                status_msg = "Auditioning " + clipper->filename() + " at Root Key " + midi_note_to_name(clipper->root_key());
                return false;
            }
        }

        // --- Tab 1 Clicks (Envelope & Instrument Settings / Keyboard) ---
        else if (active_tab == 1) {
            // Sub-tabs: Panning, Volume, Mod X, Mod Y, Pitch
            if (y >= my + 44.0f && y <= my + 70.0f) {
                float stab_w = 72.0f;
                for (int i = 0; i < 5; ++i) {
                    if (x >= mx + 14.0f + i * (stab_w + 4.0f) && x <= mx + 14.0f + (i + 1) * (stab_w + 4.0f) - 4.0f) {
                        env_subtab = i;
                        const char* subnames[] = {"Panning", "Volume", "Mod X", "Mod Y", "Pitch"};
                        status_msg = "Editing Envelope: " + std::string(subnames[i]);
                        return false;
                    }
                }
            }

            // Envelope LED Toggle
            if (x >= mx + 22.0f && x <= mx + 36.0f && y >= my + 82.0f && y <= my + 96.0f) {
                clipper->set_env_enabled(!clipper->env_enabled());
                status_msg = clipper->env_enabled() ? "Volume Envelope Enabled" : "Volume Envelope Disabled (One-Shot)";
                return false;
            }

            // Envelope Knobs: DELAY, ATT, HOLD, DEC, SUS, REL
            if (y >= my + 206.0f && y <= my + 256.0f && x >= mx + 22.0f && x <= mx + 378.0f) {
                float e_kw = 356.0f / 6.0f;
                int k_idx = static_cast<int>((x - (mx + 22.0f)) / e_kw);
                switch (k_idx) {
                    case 0: dragging_knob = ClipperKnobId::EnvDelay; drag_orig_val = clipper->env_delay(); break;
                    case 1: dragging_knob = ClipperKnobId::EnvAtt; drag_orig_val = clipper->env_attack(); break;
                    case 2: dragging_knob = ClipperKnobId::EnvHold; drag_orig_val = clipper->env_hold(); break;
                    case 3: dragging_knob = ClipperKnobId::EnvDec; drag_orig_val = clipper->env_decay(); break;
                    case 4: dragging_knob = ClipperKnobId::EnvSus; drag_orig_val = clipper->env_sustain(); break;
                    case 5: dragging_knob = ClipperKnobId::EnvRel; drag_orig_val = clipper->env_release(); break;
                    default: break;
                }
                drag_start_y = y;
                status_msg = "Adjusting Envelope Parameter";
                return false;
            }

            // Envelope Knobs Row 2: TENSION (under ATT), TENSION (under DEC), Tempo toggle
            if (y >= my + 264.0f && y <= my + 314.0f) {
                float e_kw = 356.0f / 6.0f;
                if (x >= mx + 22.0f + e_kw && x <= mx + 22.0f + e_kw * 2.0f - 4.0f) {
                    dragging_knob = ClipperKnobId::EnvTensionAtt;
                    drag_start_y = y;
                    drag_orig_val = clipper->env_att_tension();
                    status_msg = "Adjusting Envelope Attack Tension";
                    return false;
                }
                if (x >= mx + 22.0f + e_kw * 3.0f && x <= mx + 22.0f + e_kw * 4.0f - 4.0f) {
                    dragging_knob = ClipperKnobId::EnvTensionDec;
                    drag_start_y = y;
                    drag_orig_val = clipper->env_dec_tension();
                    status_msg = "Adjusting Envelope Decay Tension";
                    return false;
                }
                if (x >= mx + 22.0f + e_kw * 4.5f && x <= mx + 372.0f) {
                    clipper->set_env_tempo_sync(!clipper->env_tempo_sync());
                    status_msg = clipper->env_tempo_sync() ? "Envelope Tempo Sync Enabled" : "Envelope Tempo Sync Disabled";
                    return false;
                }
            }

            // LFO Shape Buttons
            if (y >= my + 80.0f && y <= my + 98.0f) {
                if (x >= mx + 404.0f && x <= mx + 424.0f) { clipper->set_lfo_shape(0); status_msg = "LFO Shape: Sine"; return false; }
                if (x >= mx + 428.0f && x <= mx + 448.0f) { clipper->set_lfo_shape(1); status_msg = "LFO Shape: Triangle"; return false; }
                if (x >= mx + 452.0f && x <= mx + 472.0f) { clipper->set_lfo_shape(2); status_msg = "LFO Shape: Square"; return false; }
            }

            // LFO Knobs: DELAY, ATT, AMT, SPEED
            if (y >= my + 206.0f && y <= my + 256.0f && x >= mx + 404.0f && x <= mx + 588.0f) {
                float l_kw = 184.0f / 4.0f;
                int k_idx = static_cast<int>((x - (mx + 404.0f)) / l_kw);
                switch (k_idx) {
                    case 0: dragging_knob = ClipperKnobId::LfoDelay; drag_orig_val = clipper->lfo_delay(); break;
                    case 1: dragging_knob = ClipperKnobId::LfoAtt; drag_orig_val = clipper->lfo_attack(); break;
                    case 2: dragging_knob = ClipperKnobId::LfoAmt; drag_orig_val = clipper->lfo_amount(); break;
                    case 3: dragging_knob = ClipperKnobId::LfoSpeed; drag_orig_val = clipper->lfo_speed(); break;
                    default: break;
                }
                drag_start_y = y;
                status_msg = "Adjusting LFO Parameter";
                return false;
            }

            // LFO Toggles: Tempo, Global
            if (y >= my + 274.0f && y <= my + 304.0f) {
                if (x >= mx + 404.0f && x <= mx + 490.0f) {
                    clipper->set_lfo_tempo_sync(!clipper->lfo_tempo_sync());
                    status_msg = clipper->lfo_tempo_sync() ? "LFO Tempo Sync Enabled" : "LFO Tempo Sync Disabled";
                    return false;
                }
                if (x >= mx + 498.0f && x <= mx + 588.0f) {
                    clipper->set_lfo_global(!clipper->lfo_global());
                    status_msg = clipper->lfo_global() ? "LFO Global Mode Active" : "LFO Per-Voice Mode Active";
                    return false;
                }
            }

            // Filter Knobs: MOD X, MOD Y
            if (x >= mx + 640.0f && x <= mx + 710.0f) {
                if (y >= my + 115.0f && y <= my + 175.0f) {
                    dragging_knob = ClipperKnobId::FilterModX;
                    drag_start_y = y;
                    drag_orig_val = clipper->filter_mod_x();
                    status_msg = "Adjusting Filter MOD X (Cutoff)";
                    return false;
                }
                if (y >= my + 190.0f && y <= my + 250.0f) {
                    dragging_knob = ClipperKnobId::FilterModY;
                    drag_start_y = y;
                    drag_orig_val = clipper->filter_mod_y();
                    status_msg = "Adjusting Filter MOD Y (Resonance)";
                    return false;
                }
            }

            // Filter Type Button
            if (x >= mx + 618.0f && x <= mx + mw - 24.0f && y >= my + 274.0f && y <= my + 304.0f) {
                clipper->set_filter_type((clipper->filter_type() + 1) % 3);
                const char* fn[] = {"Fast LP (Lowpass)", "Fast HP (Highpass)", "Fast BP (Bandpass)"};
                status_msg = "Filter Type: " + std::string(fn[clipper->filter_type()]);
                return false;
            }

            // Bottom Controls Bar: Reset Button, Enable Main Pitch, Add To Key, Fine Tune
            if (y >= my + 356.0f && y <= my + 380.0f) {
                // Reset Button: resets root note to C5 (MIDI 60) per prompt requirement!
                if (x >= mx + 138.0f && x <= mx + 198.0f) {
                    clipper->set_root_key(60); // C5 default
                    clipper->set_pitch_shift(0.0f);
                    clipper->set_fine_tune_cents(0.0f);
                    status_msg = "Reset Root Note to C5 (MIDI 60)";
                    return false;
                }
                if (x >= mx + 208.0f && x <= mx + 348.0f) {
                    clipper->set_enable_main_pitch(!clipper->enable_main_pitch());
                    status_msg = clipper->enable_main_pitch() ? "Enabled Main Pitch" : "Disabled Main Pitch";
                    return false;
                }
                if (x >= mx + 356.0f && x <= mx + 456.0f) {
                    clipper->set_add_to_key(!clipper->add_to_key());
                    status_msg = clipper->add_to_key() ? "Enabled Add To Key" : "Disabled Add To Key";
                    return false;
                }
                if (x >= mx + 466.0f && x <= mx + mw - 24.0f) {
                    // Click directly jumps to slider value!
                    float norm = std::clamp((static_cast<float>(x) - (mx + 466.0f)) / ((mx + mw - 24.0f) - (mx + 466.0f)), 0.0f, 1.0f);
                    clipper->set_fine_tune_cents((norm - 0.5f) * 200.0f);
                    dragging_knob = ClipperKnobId::FineTune;
                    drag_start_y = y;
                    drag_orig_val = clipper->fine_tune_cents();
                    status_msg = std::string("Fine Tune: ") + (clipper->fine_tune_cents() >= 0.0f ? "+" : "") +
                                 std::to_string(static_cast<int>(clipper->fine_tune_cents())) + " cents";
                    return false;
                }
            }

            // Virtual Piano Keyboard Click: Sets Root Note & Auditions Note!
            float kb_x = mx + 20.0f;
            float kb_y = my + 388.0f;
            float kb_w = mw - 40.0f;
            float kb_h = (my + mh - 20.0f) - kb_y;

            if (x >= kb_x && x <= kb_x + kb_w && y >= kb_y && y <= kb_y + kb_h) {
                int num_white = 49;
                float wk_w = kb_w / static_cast<float>(num_white);
                float wk_h = kb_h;
                float bk_w = wk_w * 0.65f;
                float bk_h = wk_h * 0.62f;

                const int white_semis[7] = {0, 2, 4, 5, 7, 9, 11};
                const int black_semis[5] = {1, 3, 6, 8, 10};
                const float black_offsets[5] = {0.68f, 1.72f, 3.65f, 4.68f, 5.72f};

                uint8_t selected_note = 60;
                bool hit_black = false;

                // Check Black Keys First
                if (y <= kb_y + bk_h) {
                    for (int oct = 2; oct <= 8 && !hit_black; ++oct) {
                        float oct_base_x = kb_x + (oct - 2) * 7.0f * wk_w;
                        for (int b = 0; b < 5; ++b) {
                            float bx = oct_base_x + black_offsets[b] * wk_w - bk_w * 0.5f;
                            if (x >= bx && x <= bx + bk_w) {
                                selected_note = static_cast<uint8_t>(oct * 12 + black_semis[b]);
                                hit_black = true;
                                break;
                            }
                        }
                    }
                }

                // Check White Keys
                if (!hit_black) {
                    int w_idx = std::clamp(static_cast<int>((x - kb_x) / wk_w), 0, num_white - 1);
                    int oct = 2 + (w_idx / 7);
                    int semi_idx = w_idx % 7;
                    selected_note = static_cast<uint8_t>(oct * 12 + white_semis[semi_idx]);
                }

                // Set Root Key to selected note!
                clipper->set_root_key(selected_note);
                if (audition_cb) audition_cb(selected_note);
                status_msg = "Root Note set to " + midi_note_to_name(selected_note) + " (MIDI " + std::to_string(selected_note) + ")";
                return false;
            }
        }

        return false;
    }

    // --- Drag Handling ---
    static void handle_drag(plugins::AudioClipDevice* clipper, ClipperKnobId dragging_knob,
                            int drag_start_y, float drag_orig_val, int cur_y, std::string& status_msg) {
        if (!clipper || dragging_knob == ClipperKnobId::None) return;

        float delta = static_cast<float>(drag_start_y - cur_y) / 120.0f;

        switch (dragging_knob) {
            case ClipperKnobId::HeaderPan:
                clipper->set_pan(std::clamp(drag_orig_val + delta * 2.0f, -1.0f, 1.0f));
                status_msg = "Pan: " + std::to_string(static_cast<int>(clipper->pan() * 100)) + "%";
                break;
            case ClipperKnobId::HeaderVol:
                clipper->set_master_volume(std::clamp(drag_orig_val + delta, 0.0f, 1.0f));
                status_msg = "Volume: " + std::to_string(static_cast<int>(clipper->master_volume() * 100)) + "%";
                break;
            case ClipperKnobId::HeaderPitch:
            case ClipperKnobId::TimePitch:
                clipper->set_pitch_shift(std::clamp(drag_orig_val + delta * 24.0f, -12.0f, 12.0f));
                status_msg = std::string("Pitch Shift: ") + (clipper->pitch_shift() >= 0 ? "+" : "") +
                             std::to_string(static_cast<int>(clipper->pitch_shift())) + " semitones";
                break;
            case ClipperKnobId::TimeMul:
                clipper->set_time_mul(std::clamp(drag_orig_val + delta * 1.5f, 0.5f, 2.0f));
                status_msg = "Time Mul: " + std::to_string(clipper->time_mul()) + "x";
                break;
            case ClipperKnobId::TimeTime:
                clipper->set_time_length(std::clamp(drag_orig_val + delta, 0.0f, 1.0f));
                status_msg = "Time Length: " + std::to_string(static_cast<int>(clipper->time_length() * 100)) + "%";
                break;
            case ClipperKnobId::StartOffset:
                clipper->set_start_offset(std::clamp(drag_orig_val + delta, 0.0f, 1.0f));
                status_msg = "Start Offset: " + std::to_string(static_cast<int>(clipper->start_offset() * 100)) + "%";
                break;
            case ClipperKnobId::SmpStart:
                clipper->set_smp_start(std::clamp(drag_orig_val + delta, 0.0f, 1.0f));
                status_msg = "Sample Start: " + std::to_string(static_cast<int>(clipper->smp_start() * 100)) + "%";
                break;
            case ClipperKnobId::SmpLength:
                clipper->set_smp_length(std::clamp(drag_orig_val + delta, 0.01f, 1.0f));
                status_msg = "Sample Length: " + std::to_string(static_cast<int>(clipper->smp_length() * 100)) + "%";
                break;
            case ClipperKnobId::InFade:
                clipper->set_in_fade(std::clamp(drag_orig_val + delta, 0.0f, 1.0f));
                status_msg = "In Fade: " + std::to_string(static_cast<int>(clipper->in_fade() * 100)) + "%";
                break;
            case ClipperKnobId::OutFade:
                clipper->set_out_fade(std::clamp(drag_orig_val + delta, 0.0f, 1.0f));
                status_msg = "Out Fade: " + std::to_string(static_cast<int>(clipper->out_fade() * 100)) + "%";
                break;
            case ClipperKnobId::Crossfade:
                clipper->set_crossfade(std::clamp(drag_orig_val + delta, 0.0f, 1.0f));
                status_msg = "Crossfade: " + std::to_string(static_cast<int>(clipper->crossfade() * 100)) + "%";
                break;
            case ClipperKnobId::Trim:
                clipper->set_trim(std::clamp(drag_orig_val + delta, 0.0f, 1.0f));
                status_msg = "Trim Threshold: " + std::to_string(static_cast<int>(clipper->trim() * 100)) + "%";
                break;
            case ClipperKnobId::EnvDelay:
                clipper->set_env_delay(std::clamp(drag_orig_val + delta * 2.0f, 0.0f, 2.0f));
                status_msg = "Env Delay: " + std::to_string(static_cast<int>(clipper->env_delay() * 1000)) + " ms";
                break;
            case ClipperKnobId::EnvAtt:
                clipper->set_env_attack(std::clamp(drag_orig_val + delta * 2.0f, 0.001f, 2.0f));
                status_msg = "Attack: " + std::to_string(static_cast<int>(clipper->env_attack() * 1000)) + " ms";
                break;
            case ClipperKnobId::EnvHold:
                clipper->set_env_hold(std::clamp(drag_orig_val + delta * 2.0f, 0.0f, 2.0f));
                status_msg = "Env Hold: " + std::to_string(static_cast<int>(clipper->env_hold() * 1000)) + " ms";
                break;
            case ClipperKnobId::EnvDec:
                clipper->set_env_decay(std::clamp(drag_orig_val + delta * 5.0f, 0.001f, 5.0f));
                status_msg = "Decay: " + std::to_string(static_cast<int>(clipper->env_decay() * 1000)) + " ms";
                break;
            case ClipperKnobId::EnvSus:
                clipper->set_env_sustain(std::clamp(drag_orig_val + delta, 0.0f, 1.0f));
                status_msg = "Sustain: " + std::to_string(static_cast<int>(clipper->env_sustain() * 100)) + "%";
                break;
            case ClipperKnobId::EnvRel:
                clipper->set_env_release(std::clamp(drag_orig_val + delta * 5.0f, 0.001f, 5.0f));
                status_msg = "Release: " + std::to_string(static_cast<int>(clipper->env_release() * 1000)) + " ms";
                break;
            case ClipperKnobId::EnvTensionAtt:
                clipper->set_env_att_tension(std::clamp(drag_orig_val + delta * 2.0f, -1.0f, 1.0f));
                status_msg = "Attack Tension: " + std::to_string(static_cast<int>(clipper->env_att_tension() * 100)) + "%";
                break;
            case ClipperKnobId::EnvTensionDec:
                clipper->set_env_dec_tension(std::clamp(drag_orig_val + delta * 2.0f, -1.0f, 1.0f));
                status_msg = "Decay Tension: " + std::to_string(static_cast<int>(clipper->env_dec_tension() * 100)) + "%";
                break;
            case ClipperKnobId::LfoDelay:
                clipper->set_lfo_delay(std::clamp(drag_orig_val + delta * 2.0f, 0.0f, 2.0f));
                status_msg = "LFO Delay: " + std::to_string(static_cast<int>(clipper->lfo_delay() * 1000)) + " ms";
                break;
            case ClipperKnobId::LfoAtt:
                clipper->set_lfo_attack(std::clamp(drag_orig_val + delta * 2.0f, 0.0f, 2.0f));
                status_msg = "LFO Attack: " + std::to_string(static_cast<int>(clipper->lfo_attack() * 1000)) + " ms";
                break;
            case ClipperKnobId::LfoAmt:
                clipper->set_lfo_amount(std::clamp(drag_orig_val + delta, 0.0f, 1.0f));
                status_msg = "LFO Amount: " + std::to_string(static_cast<int>(clipper->lfo_amount() * 100)) + "%";
                break;
            case ClipperKnobId::LfoSpeed:
                clipper->set_lfo_speed(std::clamp(drag_orig_val + delta * 20.0f, 0.1f, 20.0f));
                status_msg = "LFO Speed: " + std::to_string(static_cast<int>(clipper->lfo_speed())) + " Hz";
                break;
            case ClipperKnobId::FilterModX:
                clipper->set_filter_mod_x(std::clamp(drag_orig_val + delta, 0.0f, 1.0f));
                status_msg = "Filter Cutoff: " + std::to_string(static_cast<int>(clipper->filter_mod_x() * 100)) + "%";
                break;
            case ClipperKnobId::FilterModY:
                clipper->set_filter_mod_y(std::clamp(drag_orig_val + delta, 0.0f, 1.0f));
                status_msg = "Filter Resonance: " + std::to_string(static_cast<int>(clipper->filter_mod_y() * 100)) + "%";
                break;
            case ClipperKnobId::FineTune:
                clipper->set_fine_tune_cents(std::clamp(drag_orig_val + delta * 150.0f, -100.0f, 100.0f));
                status_msg = std::string("Fine Tune: ") + (clipper->fine_tune_cents() >= 0 ? "+" : "") +
                             std::to_string(static_cast<int>(clipper->fine_tune_cents())) + " cents";
                break;
            default:
                break;
        }
    }
};

} // namespace digidaw::adapters::gui
