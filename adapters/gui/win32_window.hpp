#pragma once

#include "../../app/engine.hpp"
#include "theme.hpp"
#include "gui_renderer.hpp"
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace digidaw::adapters::gui {

enum class ViewMode : uint8_t {
    ChannelRack,
    PianoRoll
};

class DigiDawWindow {
public:
    static DigiDawWindow* instance;

    explicit DigiDawWindow(app::Engine& engine)
        : engine_(engine) {
        instance = this;
    }

    ~DigiDawWindow() {
        if (mem_dc_) DeleteDC(mem_dc_);
        if (mem_bmp_) DeleteObject(mem_bmp_);
        if (font_main_) DeleteObject(font_main_);
        if (font_bold_) DeleteObject(font_bold_);
        if (font_title_) DeleteObject(font_title_);
        if (font_small_) DeleteObject(font_small_);
        instance = nullptr;
    }

    bool create_and_show(HINSTANCE hinst, int width = 1150, int height = 780) {
        hinst_ = hinst;

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &DigiDawWindow::wnd_proc_static;
        wc.hInstance = hinst_;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL; // Double-buffered GDI
        wc.lpszClassName = L"DigiDawMainWindowClass";

        RegisterClassExW(&wc);

        int screen_w = GetSystemMetrics(SM_CXSCREEN);
        int screen_h = GetSystemMetrics(SM_CYSCREEN);
        int x = std::max(0, (screen_w - width) / 2);
        int y = std::max(0, (screen_h - height) / 2);

        hwnd_ = CreateWindowExW(
            WS_EX_ACCEPTFILES,
            wc.lpszClassName,
            L"DigiDAW 2026 - Desktop Audio Workstation",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            x, y, width, height,
            NULL, NULL, hinst_, this
        );

        if (!hwnd_) return false;

        font_main_ = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, VARIABLE_PITCH, "Segoe UI");
        font_bold_ = CreateFontA(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, VARIABLE_PITCH, "Segoe UI");
        font_title_ = CreateFontA(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, VARIABLE_PITCH, "Segoe UI");
        font_small_ = CreateFontA(11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, VARIABLE_PITCH, "Segoe UI");

        // 60 FPS update timer (16ms)
        SetTimer(hwnd_, 1, 16, NULL);

        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);

        return true;
    }

    void run_message_loop() {
        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    static std::string get_midi_note_name(uint8_t pitch) {
        static const char* note_names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        int octave = static_cast<int>(pitch / 12);
        return std::string(note_names[pitch % 12]) + std::to_string(octave);
    }

    static bool is_midi_black_key(uint8_t pitch) {
        int semi = pitch % 12;
        return semi == 1 || semi == 3 || semi == 6 || semi == 8 || semi == 10;
    }

private:
    static LRESULT CALLBACK wnd_proc_static(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        DigiDawWindow* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = reinterpret_cast<DigiDawWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<DigiDawWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (self) {
            return self->wnd_proc(hwnd, msg, wp, lp);
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    LRESULT wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                render(hdc);
                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_ERASEBKGND:
                return 1;

            case WM_SIZE: {
                int w = LOWORD(lp);
                int h = HIWORD(lp);
                resize_backbuffer(w, h);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            case WM_TIMER: {
                for (size_t i = 0; i <= 4; ++i) {
                    float raw_p = engine_.get_track_peak(i);
                    meter_peaks_[i] = std::max(raw_p, meter_peaks_[i] * 0.85f);
                }
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            case WM_LBUTTONDOWN: {
                int mouse_x = LOWORD(lp);
                int mouse_y = HIWORD(lp);
                is_mouse_down_ = true;
                SetCapture(hwnd);
                on_mouse_down(mouse_x, mouse_y);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            case WM_MOUSEMOVE: {
                if (is_mouse_down_) {
                    int mouse_x = LOWORD(lp);
                    int mouse_y = HIWORD(lp);
                    on_mouse_move(mouse_x, mouse_y);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return 0;
            }

            case WM_LBUTTONUP: {
                is_mouse_down_ = false;
                dragging_spm_ = false;
                dragging_mixer_track_ = -1;
                resizing_note_ = false;
                ReleaseCapture();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            case WM_RBUTTONDOWN: {
                int mouse_x = LOWORD(lp);
                int mouse_y = HIWORD(lp);
                on_right_click(mouse_x, mouse_y);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            case WM_MOUSEWHEEL: {
                short zDelta = GET_WHEEL_DELTA_WPARAM(wp);
                int steps = zDelta / WHEEL_DELTA;
                if (view_mode_ == ViewMode::PianoRoll) {
                    piano_roll_base_pitch_ = std::clamp(piano_roll_base_pitch_ + steps * 2, 0, 128 - PianoRollNumPitches);
                    status_message_ = "Piano Roll Range: " + get_midi_note_name(piano_roll_base_pitch_) +
                                      " to " + get_midi_note_name(piano_roll_base_pitch_ + PianoRollNumPitches - 1);
                    InvalidateRect(hwnd, NULL, FALSE);
                } else if (view_mode_ == ViewMode::ChannelRack) {
                    sequencer_scroll_bar_ = std::clamp(sequencer_scroll_bar_ - steps, 0, 128);
                    status_message_ = "Sequencer scrolled to Bar " + std::to_string(sequencer_scroll_bar_ + 1);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return 0;
            }

            case WM_DROPFILES: {
                HDROP hdrop = reinterpret_cast<HDROP>(wp);
                wchar_t file_path[MAX_PATH];
                if (DragQueryFileW(hdrop, 0, file_path, MAX_PATH)) {
                    char mb_path[MAX_PATH];
                    WideCharToMultiByte(CP_UTF8, 0, file_path, -1, mb_path, MAX_PATH, NULL, NULL);
                    engine_.session().open_project(mb_path);
                    status_message_ = std::string("Loaded project: ") + mb_path;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                DragFinish(hdrop);
                return 0;
            }

            case WM_KEYDOWN: {
                if (wp == VK_SPACE) {
                    toggle_play();
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                if (wp == VK_F6) {
                    view_mode_ = ViewMode::ChannelRack;
                    status_message_ = "Switched to Channel Rack View (F6)";
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                if (wp == VK_F7) {
                    view_mode_ = ViewMode::PianoRoll;
                    status_message_ = "Switched to Piano Roll View (F7)";
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                if (wp == VK_ESCAPE) {
                    if (active_editor_channel_ != 0) {
                        active_editor_channel_ = 0; // Close plugin editor
                    } else if (view_mode_ == ViewMode::PianoRoll) {
                        view_mode_ = ViewMode::ChannelRack; // Return to rack
                    } else {
                        stop_playback();
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                break;
            }

            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    void resize_backbuffer(int w, int h) {
        client_w_ = w;
        client_h_ = h;

        if (mem_dc_) DeleteDC(mem_dc_);
        if (mem_bmp_) DeleteObject(mem_bmp_);

        HDC screen_dc = GetDC(hwnd_);
        mem_dc_ = CreateCompatibleDC(screen_dc);
        mem_bmp_ = CreateCompatibleBitmap(screen_dc, std::max(1, w), std::max(1, h));
        SelectObject(mem_dc_, mem_bmp_);
        ReleaseDC(hwnd_, screen_dc);
    }

    void render(HDC target_hdc) {
        if (!mem_dc_ || client_w_ <= 0 || client_h_ <= 0) return;

        const auto& t = get_theme();
        RECT full_rc{0, 0, client_w_, client_h_};
        GuiRenderer::fill_rect(mem_dc_, full_rc, t.bg_main);

        // 1. Render Top Transport Bar (Play, Pause, Stop, Tempo, Time, SPM Readout, View Tabs)
        render_transport_bar();

        // 2. Render Main View: Channel Rack OR Piano Roll
        if (view_mode_ == ViewMode::ChannelRack) {
            render_channel_rack();
        } else {
            render_piano_roll();
        }

        // 3. Render Mixer with Interactive Volume Faders & Meters
        render_mixer_panel();

        // 4. Render Status Bar
        render_status_bar();

        // 5. Render Floating Plugin Editor Modal if open
        if (active_editor_channel_ != 0) {
            render_plugin_editor();
        }

        BitBlt(target_hdc, 0, 0, client_w_, client_h_, mem_dc_, 0, 0, SRCCOPY);
    }

    void render_transport_bar() {
        const auto& t = get_theme();
        RECT bar_rc{0, 0, client_w_, 60};
        GuiRenderer::fill_rect(mem_dc_, bar_rc, t.bg_header);
        GuiRenderer::draw_border(mem_dc_, bar_rc, t.border_dark);

        // App Logo
        SelectObject(mem_dc_, font_title_);
        RECT title_rc{15, 0, 150, 60};
        GuiRenderer::draw_text(mem_dc_, "DigiDAW", title_rc, t.accent_orange, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(mem_dc_, font_bold_);

        // Transport Buttons: PLAY, PAUSE, STOP
        bool is_playing = engine_.transport().is_playing();
        bool is_paused = (engine_.transport().state() == app::TransportState::Paused);

        // 1. PLAY Button
        RECT play_rc{160, 12, 230, 48};
        GuiRenderer::draw_button(mem_dc_, play_rc, "▶ PLAY", is_playing, t.accent_green, t.bg_card);

        // 2. PAUSE Button
        RECT pause_rc{238, 12, 308, 48};
        GuiRenderer::draw_button(mem_dc_, pause_rc, "❚❚ PAUSE", is_paused, t.accent_amber, t.bg_card);

        // 3. STOP Button (Rewinds to tick 0 & SPM)
        RECT stop_rc{316, 12, 376, 48};
        GuiRenderer::draw_button(mem_dc_, stop_rc, "■ STOP", false, t.accent_red, t.bg_card);

        // Tempo BPM Controls
        double bpm = engine_.session().project().time_map().get_bpm_at(0);
        std::stringstream ss_bpm;
        ss_bpm << std::fixed << std::setprecision(1) << bpm << " BPM";

        RECT bpm_minus_rc{395, 15, 420, 45};
        GuiRenderer::draw_button(mem_dc_, bpm_minus_rc, "-", false, t.bg_card, t.bg_card);

        RECT bpm_disp_rc{425, 15, 515, 45};
        GuiRenderer::draw_rounded_box(mem_dc_, bpm_disp_rc, t.bg_input, t.border_dark, 4);
        GuiRenderer::draw_text(mem_dc_, ss_bpm.str(), bpm_disp_rc, t.accent_amber, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT bpm_plus_rc{520, 15, 545, 45};
        GuiRenderer::draw_button(mem_dc_, bpm_plus_rc, "+", false, t.bg_card, t.bg_card);

        // Position Counter: Bar : Beat : Tick
        auto tick = engine_.transport().current_tick();
        auto ppq = engine_.session().project().time_map().ppq();
        int bar = static_cast<int>(tick / (ppq * 4)) + 1;
        int beat = static_cast<int>((tick % (ppq * 4)) / ppq) + 1;
        int sub_tick = static_cast<int>(tick % ppq);

        std::stringstream ss_pos;
        ss_pos << std::setfill('0') << std::setw(3) << bar << ":"
               << std::setfill('0') << std::setw(2) << beat << ":"
               << std::setfill('0') << std::setw(3) << sub_tick;

        RECT pos_rc{560, 15, 670, 45};
        GuiRenderer::draw_rounded_box(mem_dc_, pos_rc, t.bg_input, t.border_dark, 4);
        GuiRenderer::draw_text(mem_dc_, ss_pos.str(), pos_rc, t.accent_cyan, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // SPM Indicator in Transport
        int spm_step = static_cast<int>((song_position_marker_ / (ppq / 4)) % 16) + 1;
        std::string spm_text = "SPM: Step " + std::to_string(spm_step);
        RECT spm_disp_rc{680, 15, 780, 45};
        GuiRenderer::draw_rounded_box(mem_dc_, spm_disp_rc, t.bg_input, t.border_dark, 4);
        GuiRenderer::draw_text(mem_dc_, spm_text, spm_disp_rc, t.accent_orange, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // View Mode Tabs: [ 🎛 RACK ] & [ 🎹 PIANO ROLL ]
        RECT rack_tab_rc{795, 15, 885, 45};
        GuiRenderer::draw_button(mem_dc_, rack_tab_rc, "🎛 RACK", (view_mode_ == ViewMode::ChannelRack),
                                 t.accent_orange, t.bg_card);

        RECT roll_tab_rc{895, 15, 1005, 45};
        GuiRenderer::draw_button(mem_dc_, roll_tab_rc, "🎹 PIANO ROLL", (view_mode_ == ViewMode::PianoRoll),
                                 t.accent_cyan, t.bg_card);

        // Action Buttons: Save & Export WAV
        SelectObject(mem_dc_, font_main_);
        RECT save_rc{client_w_ - 210, 15, client_w_ - 115, 45};
        GuiRenderer::draw_button(mem_dc_, save_rc, "SAVE .ODP", false, t.bg_card, t.bg_card);

        RECT rend_rc{client_w_ - 105, 15, client_w_ - 15, 45};
        GuiRenderer::draw_button(mem_dc_, rend_rc, "EXPORT WAV", false, t.bg_card, t.accent_orange);
    }

    void render_channel_rack() {
        const auto& t = get_theme();
        int rack_top = 70;
        int rack_bottom = client_h_ - 225;
        RECT rack_rc{15, rack_top, client_w_ - 15, rack_bottom};

        GuiRenderer::draw_rounded_box(mem_dc_, rack_rc, t.bg_panel, t.border_dark, 8);

        auto& proj = engine_.session().project();
        auto* pat = proj.get_pattern(1);
        auto ppq = proj.time_map().ppq();
        auto bar_ticks = 4 * ppq;

        // Ensure tracks exist for all channels
        while (proj.tracks().size() < proj.channels().size()) {
            domain::TrackId tid = static_cast<domain::TrackId>(proj.tracks().size() + 1);
            proj.tracks().emplace_back(tid, "Track " + std::to_string(tid));
        }

        // Layout: 8 Bars visible per view (FL Studio Arranger style)
        int bars_per_view = (client_w_ > 1400) ? 12 : 8;
        int start_x = 265;
        int avail_w = (client_w_ - 25) - start_x;
        int bar_gap = 4;
        int bar_w = std::max(60, (avail_w - (bars_per_view - 1) * bar_gap) / bars_per_view);
        int total_seq_w = bars_per_view * bar_w + (bars_per_view - 1) * bar_gap;
        int step_h = 36;

        // Header Title with Flexible Bar Scrolling
        SelectObject(mem_dc_, font_bold_);
        std::string header_title = "SEQUENCER & PLAYLIST ARRANGER   [PLACEMENT BLOCKS — CLICK TO ADD / REMOVE]";
        RECT header_rc{25, rack_top + 8, 480, rack_top + 28};
        GuiRenderer::draw_text(mem_dc_, header_title, header_rc, t.text_secondary);

        // Horizontal Bar Navigation Buttons
        SelectObject(mem_dc_, font_small_);
        RECT prev_b_rc{client_w_ - 260, rack_top + 6, client_w_ - 180, rack_top + 28};
        GuiRenderer::draw_button(mem_dc_, prev_b_rc, "◄ Bar -", false, t.bg_card, t.bg_card);

        RECT disp_b_rc{client_w_ - 175, rack_top + 6, client_w_ - 95, rack_top + 28};
        std::string b_range_str = "Bars " + std::to_string(sequencer_scroll_bar_ + 1) + "-" +
                                  std::to_string(sequencer_scroll_bar_ + bars_per_view);
        GuiRenderer::draw_rounded_box(mem_dc_, disp_b_rc, t.bg_input, t.border_dark, 4);
        GuiRenderer::draw_text(mem_dc_, b_range_str, disp_b_rc, t.accent_cyan, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT next_b_rc{client_w_ - 90, rack_top + 6, client_w_ - 20, rack_top + 28};
        GuiRenderer::draw_button(mem_dc_, next_b_rc, "Bar + ►", false, t.bg_card, t.bg_card);

        // --- Timeline Ruler ---
        int ruler_y = rack_top + 32;
        SelectObject(mem_dc_, font_small_);

        RECT spm_label_rc{180, ruler_y, 255, ruler_y + 20};
        GuiRenderer::draw_text(mem_dc_, "TIMELINE / BARS:", spm_label_rc, t.text_secondary, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        domain::Tick view_start_tick = sequencer_scroll_bar_ * bar_ticks;
        domain::Tick view_end_tick = (sequencer_scroll_bar_ + bars_per_view) * bar_ticks;
        domain::Tick view_duration = bars_per_view * bar_ticks;

        for (int b = 0; b < bars_per_view; ++b) {
            int abs_bar = sequencer_scroll_bar_ + b;
            int bx = start_x + b * (bar_w + bar_gap);
            RECT ruler_cell_rc{bx, ruler_y, bx + bar_w, ruler_y + 20};

            domain::Tick b_start = abs_bar * bar_ticks;
            domain::Tick b_end = (abs_bar + 1) * bar_ticks;

            bool is_cur = (engine_.transport().is_playing() &&
                           engine_.transport().current_tick() >= b_start &&
                           engine_.transport().current_tick() < b_end);
            bool is_spm = (song_position_marker_ >= b_start && song_position_marker_ < b_end);

            COLORREF bg = (abs_bar % 2 == 0) ? RGB(26, 32, 40) : RGB(20, 24, 30);
            COLORREF border = is_spm ? t.accent_orange : (is_cur ? t.accent_cyan : t.border_dark);
            GuiRenderer::draw_rounded_box(mem_dc_, ruler_cell_rc, bg, border, 3);

            std::string label = "BAR " + std::to_string(abs_bar + 1);
            COLORREF txt_col = is_spm ? t.accent_orange : (is_cur ? t.accent_cyan : t.text_secondary);
            GuiRenderer::draw_text(mem_dc_, label, ruler_cell_rc, txt_col, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        // --- Channels List & Placement Blocks ---
        int ch_y = rack_top + 60;

        for (size_t ch_idx = 0; ch_idx < proj.channels().size(); ++ch_idx) {
            auto& ch = proj.channels()[ch_idx];
            auto& track = proj.tracks()[ch_idx];
            auto* note_set = pat ? pat->get_channel_notes(ch.id()) : nullptr;

            // Mute Button [M]
            RECT mute_rc{30, ch_y, 55, ch_y + step_h};
            GuiRenderer::draw_button(mem_dc_, mute_rc, "M", ch.settings().muted, t.accent_red, t.bg_card);

            // Solo Button [S]
            RECT solo_rc{60, ch_y, 85, ch_y + step_h};
            GuiRenderer::draw_button(mem_dc_, solo_rc, "S", ch.settings().solo, t.accent_amber, t.bg_card);

            // Channel / VST Button (Click to open GUI editor!)
            RECT name_rc{95, ch_y, 205, ch_y + step_h};
            bool is_editing = (active_editor_channel_ == ch.id());
            GuiRenderer::draw_rounded_box(mem_dc_, name_rc, is_editing ? t.bg_header : t.bg_card,
                                          is_editing ? t.accent_orange : t.border_dark, 4);

            SelectObject(mem_dc_, font_bold_);
            RECT name_text_rc{102, ch_y, 175, ch_y + step_h};
            GuiRenderer::draw_text(mem_dc_, ch.settings().name, name_text_rc,
                                  is_editing ? t.accent_orange : t.text_primary, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(mem_dc_, font_small_);
            RECT edit_tag_rc{176, ch_y, 203, ch_y + step_h};
            GuiRenderer::draw_text(mem_dc_, "⚙", edit_tag_rc, t.accent_orange, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            // Dedicated Piano Roll button for this channel
            RECT roll_btn_rc{210, ch_y, 255, ch_y + step_h};
            GuiRenderer::draw_button(mem_dc_, roll_btn_rc, "🎹", false, t.accent_cyan, t.bg_card);

            // Render Bar Slots (Placement Blocks)
            for (int b = 0; b < bars_per_view; ++b) {
                int abs_bar = sequencer_scroll_bar_ + b;
                domain::Tick b_start = abs_bar * bar_ticks;
                int bx = start_x + b * (bar_w + bar_gap);
                RECT bar_slot_rc{bx, ch_y, bx + bar_w, ch_y + step_h};

                bool has_clip = track.has_clip_at(b_start);

                if (!has_clip) {
                    // Empty slot: Click to place block!
                    GuiRenderer::draw_rounded_box(mem_dc_, bar_slot_rc, RGB(18, 22, 28), RGB(36, 42, 54), 4);
                    SelectObject(mem_dc_, font_bold_);
                    GuiRenderer::draw_text(mem_dc_, "+", bar_slot_rc, RGB(55, 65, 80), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                } else {
                    // Placement Block Active! (Shows mini melody preview from Piano Roll)
                    COLORREF block_bg = (ch_idx % 2 == 0) ? RGB(22, 44, 68) : RGB(34, 38, 56);
                    COLORREF border_col = (ch_idx % 2 == 0) ? t.accent_cyan : t.accent_orange;
                    GuiRenderer::draw_rounded_box(mem_dc_, bar_slot_rc, block_bg, border_col, 4);

                    // Block Header & Close Button
                    SelectObject(mem_dc_, font_small_);
                    RECT b_hdr_rc{bx + 4, ch_y + 2, bx + bar_w - 18, ch_y + 14};
                    GuiRenderer::draw_text(mem_dc_, "Bar " + std::to_string(abs_bar + 1), b_hdr_rc, border_col, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                    RECT close_rc{bx + bar_w - 16, ch_y + 2, bx + bar_w - 2, ch_y + 14};
                    GuiRenderer::draw_text(mem_dc_, "✕", close_rc, RGB(220, 90, 90), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                    // Mini Note Preview inside block
                    if (note_set && !note_set->notes().empty()) {
                        uint8_t min_p = 127, max_p = 0;
                        for (const auto& n : note_set->notes()) {
                            min_p = std::min(min_p, n.pitch);
                            max_p = std::max(max_p, n.pitch);
                        }
                        int p_range = std::max(1, (max_p > min_p) ? (max_p - min_p + 1) : 4);

                        for (const auto& n : note_set->notes()) {
                            double rel_start = double(n.start % bar_ticks) / double(bar_ticks);
                            double rel_len = double(n.length) / double(bar_ticks);
                            int nx = bx + 4 + static_cast<int>(rel_start * (bar_w - 8));
                            int nw = std::max(4, static_cast<int>(rel_len * (bar_w - 8)));
                            if (nx + nw > bx + bar_w - 4) nw = (bx + bar_w - 4) - nx;
                            if (nw < 3) nw = 3;

                            float norm_y = float(n.pitch - min_p) / float(p_range);
                            int ny = ch_y + step_h - 7 - static_cast<int>(norm_y * (step_h - 22));
                            int nh = 4;

                            RECT n_rc{nx, ny, nx + nw, ny + nh};
                            GuiRenderer::draw_rounded_box(mem_dc_, n_rc, t.accent_orange, RGB(255, 200, 80), 1);
                        }
                    } else {
                        RECT empty_pr_rc{bx + 4, ch_y + 14, bx + bar_w - 4, ch_y + step_h - 2};
                        GuiRenderer::draw_text(mem_dc_, "♪ [PR Empty]", empty_pr_rc, RGB(80, 95, 115), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    }
                }
            }

            ch_y += step_h + 8;
        }

        // --- Render Song Position Marker (SPM) & Playhead Time Indicator ---
        int seq_bottom = ch_y + 4;

        if (!engine_.transport().is_playing()) {
            if (song_position_marker_ >= view_start_tick && song_position_marker_ < view_end_tick) {
                int spm_x = start_x + static_cast<int>((double(song_position_marker_ - view_start_tick) / view_duration) * total_seq_w);
                GuiRenderer::draw_playhead(mem_dc_, spm_x, ruler_y - 2, seq_bottom, t.accent_orange, "SPM ▼");
            }
        } else {
            if (song_position_marker_ >= view_start_tick && song_position_marker_ < view_end_tick) {
                int spm_x = start_x + static_cast<int>((double(song_position_marker_ - view_start_tick) / view_duration) * total_seq_w);
                GuiRenderer::draw_playhead(mem_dc_, spm_x, ruler_y - 2, seq_bottom, RGB(180, 100, 10), "SPM");
            }

            auto tick = engine_.transport().current_tick();
            if (tick >= view_start_tick && tick < view_end_tick) {
                int playhead_x = start_x + static_cast<int>((double(tick - view_start_tick) / view_duration) * total_seq_w);
                GuiRenderer::draw_playhead(mem_dc_, playhead_x, ruler_y - 2, seq_bottom, RGB(0, 230, 255), "▶ PLAY");
            }
        }

        // Add Channel Button
        RECT add_ch_rc{30, ch_y + 5, 170, ch_y + 35};
        SelectObject(mem_dc_, font_main_);
        GuiRenderer::draw_button(mem_dc_, add_ch_rc, "+ Add 3xOsc Synth", false, t.bg_card, t.bg_card);
    }

    // --- Interactive Piano Roll View ---
    void render_piano_roll() {
        const auto& t = get_theme();
        int rack_top = 70;
        int rack_bottom = client_h_ - 225;
        RECT roll_rc{15, rack_top, client_w_ - 15, rack_bottom};

        GuiRenderer::draw_rounded_box(mem_dc_, roll_rc, t.bg_panel, t.border_dark, 8);

        auto& proj = engine_.session().project();
        auto* pat = proj.get_pattern(1);
        auto ppq = proj.time_map().ppq();
        auto step_ticks = ppq / 4;

        auto* cur_ch = proj.get_channel(piano_roll_channel_);
        std::string ch_name = cur_ch ? cur_ch->settings().name : "3xOsc Synth #1";

        // 1. Piano Roll Toolbar
        SelectObject(mem_dc_, font_bold_);
        RECT title_rc{25, rack_top + 8, 230, rack_top + 28};
        GuiRenderer::draw_text(mem_dc_, "PIANO ROLL — " + ch_name, title_rc, t.accent_cyan);

        SelectObject(mem_dc_, font_main_);
        RECT back_rc{235, rack_top + 5, 345, rack_top + 29};
        GuiRenderer::draw_button(mem_dc_, back_rc, "🎛 Back to Rack", false, t.bg_card, t.bg_card);

        // Octave / Pitch Scroll Buttons (Covering full C0 to B10)
        int num_pitches = PianoRollNumPitches;
        RECT oct_dn_rc{355, rack_top + 5, 425, rack_top + 29};
        GuiRenderer::draw_button(mem_dc_, oct_dn_rc, "◄ Oct -", false, t.bg_card, t.bg_card);

        RECT semi_dn_rc{430, rack_top + 5, 465, rack_top + 29};
        GuiRenderer::draw_button(mem_dc_, semi_dn_rc, "▼", false, t.bg_card, t.bg_card);

        std::string oct_str = get_midi_note_name(piano_roll_base_pitch_) + "—" +
                              get_midi_note_name(std::min(127, piano_roll_base_pitch_ + num_pitches - 1)) + " (C0..B10)";
        RECT oct_disp_rc{470, rack_top + 5, 615, rack_top + 29};
        GuiRenderer::draw_rounded_box(mem_dc_, oct_disp_rc, t.bg_input, t.border_dark, 4);
        GuiRenderer::draw_text(mem_dc_, oct_str, oct_disp_rc, t.accent_amber, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT semi_up_rc{620, rack_top + 5, 655, rack_top + 29};
        GuiRenderer::draw_button(mem_dc_, semi_up_rc, "▲", false, t.bg_card, t.bg_card);

        RECT oct_up_rc{660, rack_top + 5, 730, rack_top + 29};
        GuiRenderer::draw_button(mem_dc_, oct_up_rc, "Oct + ►", false, t.bg_card, t.bg_card);

        // Steps Toggle (16 or 32)
        RECT steps_rc{740, rack_top + 5, 825, rack_top + 29};
        std::string steps_str = std::to_string(piano_roll_steps_) + " Steps";
        GuiRenderer::draw_button(mem_dc_, steps_rc, steps_str, (piano_roll_steps_ == 32), t.accent_cyan, t.bg_card);

        // Note Length Toggle (1, 2, 4, 8 Steps)
        RECT len_btn_rc{835, rack_top + 5, 930, rack_top + 29};
        std::string len_str = "📏 Len: " + std::to_string(piano_roll_note_len_steps_) + (piano_roll_note_len_steps_ == 1 ? " Stp" : " Stps");
        GuiRenderer::draw_button(mem_dc_, len_btn_rc, len_str, (piano_roll_note_len_steps_ > 1), t.accent_orange, t.bg_card);

        // Clear Notes button
        RECT clear_rc{940, rack_top + 5, 1005, rack_top + 29};
        GuiRenderer::draw_button(mem_dc_, clear_rc, "Clear", false, t.accent_red, t.bg_card);

        // 2. Geometry Setup
        int ruler_y = rack_top + 34;
        int grid_top = ruler_y + 22;
        int grid_bottom = rack_bottom - 10;
        int grid_h = grid_bottom - grid_top;

        int piano_x = 25;
        int piano_w = 68;
        int grid_x = piano_x + piano_w + 4;
        int grid_w = (client_w_ - 45) - grid_x;

        int row_h = std::max(12, grid_h / num_pitches);
        int step_w = std::max(16, grid_w / piano_roll_steps_);

        // Current Playhead & SPM
        int current_step = -1;
        if (engine_.transport().is_playing()) {
            auto tick = engine_.transport().current_tick();
            current_step = static_cast<int>((tick / step_ticks) % piano_roll_steps_);
        }
        int spm_step = static_cast<int>((song_position_marker_ / step_ticks) % piano_roll_steps_);

        // 3. Ruler Bar
        SelectObject(mem_dc_, font_small_);
        for (int s = 0; s < piano_roll_steps_; ++s) {
            int sx = grid_x + s * step_w;
            RECT ruler_cell_rc{sx, ruler_y, sx + step_w - 1, ruler_y + 18};

            bool is_spm = (s == spm_step);
            bool is_cur = (s == current_step);
            COLORREF bg = ((s / 4) % 2 == 0) ? t.step_off_light : t.step_off_dark;
            COLORREF border = is_spm ? t.accent_orange : (is_cur ? t.accent_cyan : t.border_dark);
            GuiRenderer::draw_rounded_box(mem_dc_, ruler_cell_rc, bg, border, 2);

            int bar = (s / 4) + 1;
            int beat = (s % 4) + 1;
            std::string label = std::to_string(bar) + "." + std::to_string(beat);
            COLORREF txt_col = is_spm ? t.accent_orange : t.text_secondary;
            GuiRenderer::draw_text(mem_dc_, label, ruler_cell_rc, txt_col, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        // 4. Piano Keys on Left & Grid Rows on Right
        for (int r = 0; r < num_pitches; ++r) {
            uint8_t pitch = static_cast<uint8_t>(piano_roll_base_pitch_ + (num_pitches - 1 - r));
            int ry = grid_top + r * row_h;
            bool is_black = is_midi_black_key(pitch);
            bool is_c_root = (pitch % 12 == 0);

            // Piano Key
            RECT key_rc{piano_x, ry, piano_x + piano_w, ry + row_h - 1};
            COLORREF key_bg = is_black ? RGB(32, 35, 42) : RGB(235, 238, 245);
            COLORREF key_border = is_c_root ? t.accent_orange : RGB(60, 64, 75);
            GuiRenderer::draw_rounded_box(mem_dc_, key_rc, key_bg, key_border, 2);

            COLORREF key_txt = is_black ? t.accent_cyan : RGB(20, 20, 20);
            if (is_c_root) key_txt = t.accent_orange;
            GuiRenderer::draw_text(mem_dc_, get_midi_note_name(pitch), key_rc, key_txt,
                                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            // Row Grid Strip
            RECT row_grid_rc{grid_x, ry, grid_x + piano_roll_steps_ * step_w, ry + row_h - 1};
            COLORREF grid_row_bg = is_black ? RGB(22, 24, 30) : RGB(30, 34, 42);
            GuiRenderer::fill_rect(mem_dc_, row_grid_rc, grid_row_bg);

            // Grid column boundaries
            for (int s = 0; s < piano_roll_steps_; ++s) {
                int sx = grid_x + s * step_w;
                RECT step_box_rc{sx, ry, sx + step_w - 1, ry + row_h - 1};
                COLORREF grid_border = (s % 4 == 0) ? RGB(55, 60, 72) : RGB(38, 42, 50);
                GuiRenderer::draw_border(mem_dc_, step_box_rc, grid_border);
            }
        }

        // 5. Vertical Scrollbar for full C0..B10 Pitch Range
        int scroll_x = grid_x + piano_roll_steps_ * step_w + 5;
        int track_h = num_pitches * row_h;
        RECT scroll_track_rc{scroll_x, grid_top, scroll_x + 14, grid_top + track_h};
        GuiRenderer::draw_rounded_box(mem_dc_, scroll_track_rc, RGB(22, 25, 32), RGB(42, 46, 58), 2);

        int max_base_pitch = 128 - num_pitches;
        int thumb_h = std::max(20, static_cast<int>((float(num_pitches) / 128.0f) * track_h));
        float scroll_ratio = float(max_base_pitch - piano_roll_base_pitch_) / float(max_base_pitch);
        int thumb_y = grid_top + static_cast<int>(scroll_ratio * (track_h - thumb_h));
        RECT thumb_rc{scroll_x + 1, thumb_y, scroll_x + 13, thumb_y + thumb_h};
        GuiRenderer::draw_rounded_box(mem_dc_, thumb_rc, t.accent_orange, RGB(255, 190, 60), 3);

        // 6. Draw Existing Notes for this Channel
        if (pat) {
            auto* note_set = pat->get_channel_notes(piano_roll_channel_);
            if (note_set) {
                for (const auto& n : note_set->notes()) {
                    if (n.pitch >= piano_roll_base_pitch_ &&
                        n.pitch < piano_roll_base_pitch_ + num_pitches) {
                        int r = (piano_roll_base_pitch_ + num_pitches - 1) - n.pitch;
                        int ny = grid_top + r * row_h + 1;

                        float s_start = float(n.start) / float(step_ticks);
                        float s_len = float(n.length) / float(step_ticks);
                        if (s_start < float(piano_roll_steps_)) {
                            int nx = grid_x + static_cast<int>(s_start * step_w) + 1;
                            int nw = std::max(8, static_cast<int>(s_len * step_w) - 2);

                            RECT note_rc{nx, ny, nx + nw, ny + row_h - 3};

                            bool is_resizing_this = (resizing_note_ && n.start == resize_note_start_ && n.pitch == resize_note_pitch_);
                            COLORREF border_col = is_resizing_this ? RGB(0, 255, 255) : RGB(255, 255, 220);
                            COLORREF fill_col = is_resizing_this ? RGB(255, 175, 40) : t.step_on;

                            GuiRenderer::draw_rounded_box(mem_dc_, note_rc, fill_col, border_col, 3);

                            // Resize Grip Handle on right edge of note
                            int handle_w = std::min(10, nw / 3);
                            if (handle_w >= 4) {
                                RECT handle_rc{nx + nw - handle_w, ny + 1, nx + nw - 1, ny + row_h - 4};
                                GuiRenderer::fill_rect(mem_dc_, handle_rc, is_resizing_this ? RGB(255, 255, 255) : RGB(240, 140, 20));
                                HPEN gPen = CreatePen(PS_SOLID, 1, RGB(50, 50, 50));
                                HGDIOBJ oldPen = SelectObject(mem_dc_, gPen);
                                MoveToEx(mem_dc_, nx + nw - handle_w / 2, ny + 2, NULL);
                                LineTo(mem_dc_, nx + nw - handle_w / 2, ny + row_h - 5);
                                SelectObject(mem_dc_, oldPen);
                                DeleteObject(gPen);
                            }

                            // Note label inside note block
                            if (nw >= 26) {
                                SelectObject(mem_dc_, font_small_);
                                std::string n_txt = get_midi_note_name(n.pitch);
                                if (s_len > 1.05f) {
                                    int steps = static_cast<int>(std::round(s_len));
                                    n_txt += " (" + std::to_string(steps) + ")";
                                }
                                RECT lbl_rc{nx + 2, ny, nx + nw - handle_w, ny + row_h - 3};
                                GuiRenderer::draw_text(mem_dc_, n_txt, lbl_rc,
                                                      RGB(20, 20, 20), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                            }
                        }
                    }
                }
            }
        }

        // 7. Draw Downward-Pointing Triangle SPM & Playhead Indicator across all rows
        int spm_x = grid_x + spm_step * step_w + step_w / 2;
        int grid_actual_bottom = grid_top + num_pitches * row_h;

        if (!engine_.transport().is_playing()) {
            GuiRenderer::draw_playhead(mem_dc_, spm_x, ruler_y - 2, grid_actual_bottom, t.accent_orange, "SPM ▼");
        } else {
            GuiRenderer::draw_playhead(mem_dc_, spm_x, ruler_y - 2, grid_actual_bottom, RGB(180, 100, 10), "SPM");

            auto tick = engine_.transport().current_tick();
            float fractional_step = float(tick % (step_ticks * piano_roll_steps_)) / float(step_ticks);
            int playhead_x = grid_x + static_cast<int>(fractional_step * step_w) + step_w / 2;
            GuiRenderer::draw_playhead(mem_dc_, playhead_x, ruler_y - 2, grid_actual_bottom, RGB(0, 230, 255), "▶");
        }
    }

    void render_mixer_panel() {
        const auto& t = get_theme();
        int mixer_top = client_h_ - 215;
        int mixer_bottom = client_h_ - 30;
        RECT mixer_rc{15, mixer_top, client_w_ - 15, mixer_bottom};

        GuiRenderer::fill_rect(mem_dc_, mixer_rc, t.bg_panel);
        GuiRenderer::draw_border(mem_dc_, mixer_rc, t.border_dark);

        SelectObject(mem_dc_, font_bold_);
        RECT title_rc{25, mixer_top + 6, 300, mixer_top + 24};
        GuiRenderer::draw_text(mem_dc_, "MIXER & GAIN CONTROLS (FADERS & METERS)", title_rc, t.text_secondary);

        auto& proj = engine_.session().project();

        // Render 5 Mixer Strips: Master (0) and Tracks 1..4
        int strip_w = 110;
        int strip_gap = 12;
        int start_x = 30;

        for (int i = 0; i <= 4; ++i) {
            int sx = start_x + i * (strip_w + strip_gap);
            RECT strip_rc{sx, mixer_top + 26, sx + strip_w, mixer_bottom - 10};
            GuiRenderer::draw_rounded_box(mem_dc_, strip_rc, t.bg_card, t.border_dark, 4);

            // Strip Header Label
            std::string label = (i == 0) ? "MASTER" : ("TRACK " + std::to_string(i));
            COLORREF label_col = (i == 0) ? t.accent_orange : t.text_secondary;
            RECT label_rc{sx, mixer_top + 30, sx + strip_w, mixer_top + 46};
            GuiRenderer::draw_text(mem_dc_, label, label_rc, label_col, DT_CENTER | DT_SINGLELINE);

            auto* trk = proj.mixer_graph().get_track(i);
            float vol = trk ? trk->volume() : 1.0f;

            // Stereo VU Meter on Left (Live Real-Time Audio Peak for Master & Tracks 1..4)
            float peak = (static_cast<size_t>(i) < meter_peaks_.size()) ? meter_peaks_[i] : 0.0f;
            RECT meter_rc{sx + 10, mixer_top + 50, sx + 25, mixer_bottom - 20};
            GuiRenderer::draw_meter_vertical(mem_dc_, meter_rc, peak);

            // Vertical Volume Fader on Right
            RECT fader_rc{sx + 35, mixer_top + 50, sx + 95, mixer_bottom - 20};
            float norm_vol = vol / 1.5f; // Scale 0..1.5 (unity 1.0 = ~66% height)

            // Format dB readout
            std::string db_str;
            if (vol < 0.01f) {
                db_str = "-inf dB";
            } else {
                float db = 20.0f * std::log10(vol);
                std::stringstream ss;
                ss << (db >= 0.0f ? "+" : "") << std::fixed << std::setprecision(1) << db << " dB";
                db_str = ss.str();
            }

            GuiRenderer::draw_slider_vertical(mem_dc_, fader_rc, norm_vol, db_str);
        }
    }

    void render_status_bar() {
        const auto& t = get_theme();
        RECT status_rc{0, client_h_ - 26, client_w_, client_h_};
        GuiRenderer::fill_rect(mem_dc_, status_rc, t.bg_header);

        SelectObject(mem_dc_, font_main_);
        RECT text_rc{15, client_h_ - 24, client_w_ - 15, client_h_ - 2};

        std::string status = status_message_.empty()
            ? "Ready. [F6] Channel Rack | [F7] Piano Roll | [Space] Play/Pause | Drag faders to adjust volume."
            : status_message_;

        GuiRenderer::draw_text(mem_dc_, status, text_rc, t.text_secondary, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    // --- Interactive Plugin GUI Editor (Modal Window) ---
    void render_plugin_editor() {
        const auto& t = get_theme();

        int mw = 680;
        int mh = 480;
        int mx = (client_w_ - mw) / 2;
        int my = (client_h_ - mh) / 2;
        editor_bounds_ = RECT{mx, my, mx + mw, my + mh};

        GuiRenderer::draw_rounded_box(mem_dc_, editor_bounds_, t.bg_panel, t.accent_orange, 8);

        // Title Bar
        RECT title_bar_rc{mx, my, mx + mw, my + 38};
        GuiRenderer::fill_rect(mem_dc_, title_bar_rc, t.bg_header);
        GuiRenderer::draw_border(mem_dc_, title_bar_rc, t.border_dark);

        SelectObject(mem_dc_, font_title_);
        RECT title_text_rc{mx + 15, my, mx + mw - 50, my + 38};
        GuiRenderer::draw_text(mem_dc_, "3xOsc Synthesizer — Instrument Editor", title_text_rc, t.accent_orange, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Close Button [✕]
        RECT close_rc{mx + mw - 38, my + 6, mx + mw - 8, my + 32};
        GuiRenderer::draw_button(mem_dc_, close_rc, "✕", false, t.accent_red, t.bg_card);

        auto* dev = get_active_channel_synth();
        auto* synth = dynamic_cast<plugins::Synth3xOsc*>(dev);

        SelectObject(mem_dc_, font_bold_);

        // --- OSCILLATOR 1 ---
        int osc1_y = my + 50;
        draw_osc_section("OSCILLATOR 1", osc1_y, synth ? synth->get_parameter(1) : 0.0f,
                         synth ? synth->get_parameter(2) : 1.0f, 0, false);

        // --- OSCILLATOR 2 ---
        int osc2_y = my + 130;
        int osc2_semi = synth ? static_cast<int>((synth->get_parameter(5) - 0.5f) * 48.0f) : 0;
        draw_osc_section("OSCILLATOR 2", osc2_y, synth ? synth->get_parameter(3) : 0.25f,
                         synth ? synth->get_parameter(4) : 0.5f, osc2_semi, true);

        // --- OSCILLATOR 3 ---
        int osc3_y = my + 210;
        draw_osc_section("OSCILLATOR 3", osc3_y, 0.0f, 0.2f, 0, false);

        // --- FILTER & MASTER CONTROLS ---
        int filter_y = my + 290;
        RECT filt_label{mx + 20, filter_y, mx + 200, filter_y + 20};
        GuiRenderer::draw_text(mem_dc_, "LOWPASS FILTER & RESONANCE", filt_label, t.text_secondary);

        float cutoff_norm = synth ? synth->get_parameter(6) : 0.8f;
        int cutoff_hz = static_cast<int>(20.0 + std::pow(cutoff_norm, 2.0) * 18000.0);
        std::string cutoff_label = "Cutoff: " + std::to_string(cutoff_hz) + " Hz";
        RECT cutoff_rc{mx + 20, filter_y + 25, mx + 220, filter_y + 55};
        GuiRenderer::draw_slider_horizontal(mem_dc_, cutoff_rc, cutoff_norm, cutoff_label);

        float q_norm = synth ? synth->get_parameter(7) : 0.2f;
        std::stringstream ss_q;
        ss_q << "Reso Q: " << std::fixed << std::setprecision(1) << (0.5 + q_norm * 10.0);
        RECT q_rc{mx + 235, filter_y + 25, mx + 435, filter_y + 55};
        GuiRenderer::draw_slider_horizontal(mem_dc_, q_rc, q_norm, ss_q.str());

        // Master Volume
        float mvol = synth ? synth->get_parameter(0) : 0.85f;
        std::string mvol_str = "Master: " + std::to_string(static_cast<int>(mvol * 100)) + "%";
        RECT mvol_rc{mx + 450, filter_y + 25, mx + mw - 20, filter_y + 55};
        GuiRenderer::draw_slider_horizontal(mem_dc_, mvol_rc, mvol, mvol_str);

        // Audition Button
        RECT test_note_rc{mx + 20, my + 380, mx + 220, my + 420};
        GuiRenderer::draw_button(mem_dc_, test_note_rc, "♪ AUDITION NOTE (C4)", false, t.accent_cyan, t.bg_card);

        RECT hint_rc{mx + 240, my + 380, mx + mw - 20, my + 420};
        SelectObject(mem_dc_, font_main_);
        GuiRenderer::draw_text(mem_dc_, "Adjust waveforms, detune, or filter sliders to reshape timbre in real time.",
                               hint_rc, t.text_secondary, DT_LEFT | DT_VCENTER | DT_WORDBREAK);
    }

    void draw_osc_section(const std::string& title, int y, float shape_norm, float vol_norm, int semi, bool has_semi) {
        const auto& t = get_theme();
        int mx = editor_bounds_.left;

        RECT label_rc{mx + 20, y, mx + 160, y + 20};
        GuiRenderer::draw_text(mem_dc_, title, label_rc, t.text_secondary);

        int active_shape = static_cast<int>(shape_norm * 3.99f);
        const char* waves[4] = {"SINE", "SAW", "SQR", "NOISE"};

        for (int w = 0; w < 4; ++w) {
            RECT wave_rc{mx + 20 + w * 62, y + 25, mx + 20 + (w + 1) * 62 - 6, y + 55};
            GuiRenderer::draw_button(mem_dc_, wave_rc, waves[w], (w == active_shape), t.accent_green, t.bg_input);
        }

        if (has_semi) {
            RECT semi_minus{mx + 280, y + 25, mx + 310, y + 55};
            GuiRenderer::draw_button(mem_dc_, semi_minus, "-", false, t.bg_card, t.bg_input);

            std::string semi_str = (semi >= 0 ? "+" : "") + std::to_string(semi) + " sem";
            RECT semi_disp{mx + 315, y + 25, mx + 395, y + 55};
            GuiRenderer::draw_rounded_box(mem_dc_, semi_disp, t.bg_input, t.border_dark, 4);
            GuiRenderer::draw_text(mem_dc_, semi_str, semi_disp, t.accent_amber, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            RECT semi_plus{mx + 400, y + 25, mx + 430, y + 55};
            GuiRenderer::draw_button(mem_dc_, semi_plus, "+", false, t.bg_card, t.bg_input);
        }

        std::string vol_str = "Vol: " + std::to_string(static_cast<int>(vol_norm * 100)) + "%";
        RECT vol_rc{mx + 450, y + 25, editor_bounds_.right - 20, y + 55};
        GuiRenderer::draw_slider_horizontal(mem_dc_, vol_rc, vol_norm, vol_str);
    }

    void on_mouse_down(int x, int y) {
        // A. If plugin editor modal is open, handle modal clicks
        if (active_editor_channel_ != 0) {
            handle_editor_click(x, y);
            return;
        }

        // B. Top Transport Bar
        // 1. PLAY Button
        if (x >= 160 && x <= 230 && y >= 12 && y <= 48) {
            start_playback_from_spm();
            return;
        }

        // 2. PAUSE Button
        if (x >= 238 && x <= 308 && y >= 12 && y <= 48) {
            pause_playback();
            return;
        }

        // 3. STOP Button
        if (x >= 316 && x <= 376 && y >= 12 && y <= 48) {
            stop_playback();
            return;
        }

        // 4. Tempo - / +
        if (x >= 395 && x <= 420 && y >= 15 && y <= 45) {
            double bpm = engine_.session().project().time_map().get_bpm_at(0);
            engine_.session().project().time_map().set_tempo(std::max(20.0, bpm - 1.0));
            return;
        }
        if (x >= 520 && x <= 545 && y >= 15 && y <= 45) {
            double bpm = engine_.session().project().time_map().get_bpm_at(0);
            engine_.session().project().time_map().set_tempo(std::min(999.0, bpm + 1.0));
            return;
        }

        // 5. View Mode Switcher: [ 🎛 RACK ] / [ 🎹 PIANO ROLL ]
        if (x >= 795 && x <= 885 && y >= 15 && y <= 45) {
            view_mode_ = ViewMode::ChannelRack;
            status_message_ = "Switched to Channel Rack View (F6)";
            return;
        }
        if (x >= 895 && x <= 1005 && y >= 15 && y <= 45) {
            view_mode_ = ViewMode::PianoRoll;
            status_message_ = "Switched to Piano Roll View (F7)";
            return;
        }

        // 6. Save & Export WAV buttons
        if (x >= client_w_ - 210 && x <= client_w_ - 115 && y >= 15 && y <= 45) {
            save_project();
            return;
        }
        if (x >= client_w_ - 105 && x <= client_w_ - 15 && y >= 15 && y <= 45) {
            render_wav();
            return;
        }

        // C. Dispatch based on ViewMode
        if (view_mode_ == ViewMode::ChannelRack) {
            handle_channel_rack_click(x, y);
        } else {
            handle_piano_roll_click(x, y);
        }

        // D. Mixer Panel Fader Drag / Click
        int mixer_top = client_h_ - 215;
        int mixer_bottom = client_h_ - 30;
        int start_x = 30;
        int strip_w = 110;
        int strip_gap = 12;

        for (int i = 0; i <= 4; ++i) {
            int sx = start_x + i * (strip_w + strip_gap);
            RECT fader_rc{sx + 35, mixer_top + 50, sx + 95, mixer_bottom - 20};

            if (x >= fader_rc.left && x <= fader_rc.right && y >= fader_rc.top && y <= fader_rc.bottom) {
                dragging_mixer_track_ = i;
                update_mixer_volume_from_mouse(i, y, fader_rc);
                return;
            }
        }
    }

    void handle_channel_rack_click(int x, int y) {
        std::lock_guard<std::mutex> lock(engine_.audio_mutex());
        int rack_top = 70;
        int ruler_y = rack_top + 32;
        auto ppq = engine_.session().project().time_map().ppq();
        auto bar_ticks = 4 * ppq;
        auto& proj = engine_.session().project();

        int bars_per_view = (client_w_ > 1400) ? 12 : 8;
        int start_x = 265;
        int avail_w = (client_w_ - 25) - start_x;
        int bar_gap = 4;
        int bar_w = std::max(60, (avail_w - (bars_per_view - 1) * bar_gap) / bars_per_view);
        int total_seq_w = bars_per_view * bar_w + (bars_per_view - 1) * bar_gap;
        int step_h = 36;

        // 1. Bar Navigation Scroll Buttons (◄ Bar - / Bar + ►)
        if (x >= client_w_ - 260 && x <= client_w_ - 180 && y >= rack_top + 6 && y <= rack_top + 28) {
            sequencer_scroll_bar_ = std::max(0, sequencer_scroll_bar_ - 4);
            status_message_ = "Sequencer scrolled to Bar " + std::to_string(sequencer_scroll_bar_ + 1);
            return;
        }
        if (x >= client_w_ - 90 && x <= client_w_ - 20 && y >= rack_top + 6 && y <= rack_top + 28) {
            sequencer_scroll_bar_ = std::min(124, sequencer_scroll_bar_ + 4);
            status_message_ = "Sequencer scrolled to Bar " + std::to_string(sequencer_scroll_bar_ + 1);
            return;
        }

        // 2. SPM Timeline Ruler Click (Set playback start to bar)
        if (x >= start_x && x <= start_x + total_seq_w && y >= ruler_y - 8 && y <= ruler_y + 22) {
            dragging_spm_ = true;
            int b = std::clamp((x - start_x) / (bar_w + bar_gap), 0, bars_per_view - 1);
            int abs_bar = sequencer_scroll_bar_ + b;
            song_position_marker_ = abs_bar * bar_ticks;
            engine_.transport().seek(song_position_marker_);
            status_message_ = "Song Position Marker (SPM) set to Bar " + std::to_string(abs_bar + 1) +
                              " (Tick " + std::to_string(song_position_marker_) + ")";
            return;
        }

        // 3. Channels List & Bar Placement Blocks
        int ch_y = rack_top + 60;

        for (size_t ch_idx = 0; ch_idx < proj.channels().size(); ++ch_idx) {
            auto& ch = proj.channels()[ch_idx];
            while (proj.tracks().size() <= ch_idx) {
                domain::TrackId tid = static_cast<domain::TrackId>(proj.tracks().size() + 1);
                proj.tracks().emplace_back(tid, "Track " + std::to_string(tid));
            }
            auto& track = proj.tracks()[ch_idx];

            // Mute [M]
            if (x >= 30 && x <= 55 && y >= ch_y && y <= ch_y + step_h) {
                ch.settings().muted = !ch.settings().muted;
                return;
            }

            // Solo [S]
            if (x >= 60 && x <= 85 && y >= ch_y && y <= ch_y + step_h) {
                ch.settings().solo = !ch.settings().solo;
                return;
            }

            // Channel Name / VST GUI Editor button
            if (x >= 95 && x <= 205 && y >= ch_y && y <= ch_y + step_h) {
                active_editor_channel_ = ch.id();
                status_message_ = "Opened Instrument Editor: " + ch.settings().name;
                return;
            }

            // Piano Roll Button [ 🎹 ]
            if (x >= 210 && x <= 255 && y >= ch_y && y <= ch_y + step_h) {
                piano_roll_channel_ = ch.id();
                view_mode_ = ViewMode::PianoRoll;
                status_message_ = "Opened Piano Roll for " + ch.settings().name;
                return;
            }

            // Placement Block Click (Add or Remove Placement Block on this Bar)
            if (x >= start_x && x <= start_x + total_seq_w && y >= ch_y && y <= ch_y + step_h) {
                int b = (x - start_x) / (bar_w + bar_gap);
                if (b >= 0 && b < bars_per_view) {
                    int abs_bar = sequencer_scroll_bar_ + b;
                    domain::Tick b_start = abs_bar * bar_ticks;

                    if (track.has_clip_at(b_start)) {
                        track.remove_clip_at(b_start);
                        status_message_ = "Removed melody block at Bar " + std::to_string(abs_bar + 1) +
                                          " from " + ch.settings().name;
                    } else {
                        track.add_clip(domain::Clip{1, b_start, bar_ticks, false});
                        status_message_ = "Placed melody block at Bar " + std::to_string(abs_bar + 1) +
                                          " for " + ch.settings().name;
                    }
                    return;
                }
            }

            ch_y += step_h + 8;
        }

        // Add Channel Button
        if (x >= 30 && x <= 170 && y >= ch_y + 5 && y <= ch_y + 35) {
            add_channel();
            return;
        }
    }

    void handle_piano_roll_click(int x, int y) {
        std::lock_guard<std::mutex> lock(engine_.audio_mutex());
        int rack_top = 70;
        int rack_bottom = client_h_ - 225;

        // 1. Toolbar buttons
        // Back to Rack
        if (x >= 235 && x <= 345 && y >= rack_top + 5 && y <= rack_top + 29) {
            view_mode_ = ViewMode::ChannelRack;
            status_message_ = "Returned to Channel Rack View (F6)";
            return;
        }

        int num_pitches = PianoRollNumPitches;
        int max_base_pitch = 128 - num_pitches;

        // Octave Down (-12)
        if (x >= 355 && x <= 425 && y >= rack_top + 5 && y <= rack_top + 29) {
            piano_roll_base_pitch_ = std::max(0, piano_roll_base_pitch_ - 12);
            status_message_ = "Octave shifted down: " + get_midi_note_name(piano_roll_base_pitch_) +
                              " to " + get_midi_note_name(piano_roll_base_pitch_ + num_pitches - 1);
            return;
        }

        // Semitone Down (-1)
        if (x >= 430 && x <= 465 && y >= rack_top + 5 && y <= rack_top + 29) {
            piano_roll_base_pitch_ = std::max(0, piano_roll_base_pitch_ - 1);
            status_message_ = "Pitch shifted down: " + get_midi_note_name(piano_roll_base_pitch_) +
                              " to " + get_midi_note_name(piano_roll_base_pitch_ + num_pitches - 1);
            return;
        }

        // Semitone Up (+1)
        if (x >= 620 && x <= 655 && y >= rack_top + 5 && y <= rack_top + 29) {
            piano_roll_base_pitch_ = std::min(max_base_pitch, piano_roll_base_pitch_ + 1);
            status_message_ = "Pitch shifted up: " + get_midi_note_name(piano_roll_base_pitch_) +
                              " to " + get_midi_note_name(piano_roll_base_pitch_ + num_pitches - 1);
            return;
        }

        // Octave Up (+12)
        if (x >= 660 && x <= 730 && y >= rack_top + 5 && y <= rack_top + 29) {
            piano_roll_base_pitch_ = std::min(max_base_pitch, piano_roll_base_pitch_ + 12);
            status_message_ = "Octave shifted up: " + get_midi_note_name(piano_roll_base_pitch_) +
                              " to " + get_midi_note_name(piano_roll_base_pitch_ + num_pitches - 1);
            return;
        }

        // Steps Toggle (16 or 32)
        if (x >= 740 && x <= 825 && y >= rack_top + 5 && y <= rack_top + 29) {
            piano_roll_steps_ = (piano_roll_steps_ == 16) ? 32 : 16;
            status_message_ = "Grid resolution set to " + std::to_string(piano_roll_steps_) + " Steps";
            return;
        }

        // Note Length Toggle (1 -> 2 -> 4 -> 8 -> 1)
        if (x >= 835 && x <= 930 && y >= rack_top + 5 && y <= rack_top + 29) {
            if (piano_roll_note_len_steps_ == 1) piano_roll_note_len_steps_ = 2;
            else if (piano_roll_note_len_steps_ == 2) piano_roll_note_len_steps_ = 4;
            else if (piano_roll_note_len_steps_ == 4) piano_roll_note_len_steps_ = 8;
            else piano_roll_note_len_steps_ = 1;

            status_message_ = "Default Note Length set to " + std::to_string(piano_roll_note_len_steps_) +
                              " Steps (" + std::to_string(piano_roll_note_len_steps_ * 240) + " Ticks)";
            return;
        }

        // Clear Notes
        if (x >= 940 && x <= 1005 && y >= rack_top + 5 && y <= rack_top + 29) {
            auto& proj = engine_.session().project();
            auto* pat = proj.get_pattern(1);
            if (pat) {
                auto& notes = pat->get_or_create_channel_notes(piano_roll_channel_);
                notes.clear();
                status_message_ = "Cleared all notes in Piano Roll";
            }
            return;
        }

        // 2. Geometry
        int ruler_y = rack_top + 34;
        int grid_top = ruler_y + 22;
        int grid_bottom = rack_bottom - 10;
        int grid_h = grid_bottom - grid_top;

        int piano_x = 25;
        int piano_w = 68;
        int grid_x = piano_x + piano_w + 4;
        int grid_w = (client_w_ - 45) - grid_x;

        int row_h = std::max(12, grid_h / num_pitches);
        int step_w = std::max(16, grid_w / piano_roll_steps_);
        auto ppq = engine_.session().project().time_map().ppq();
        auto step_ticks = ppq / 4;

        // 3. Vertical Scrollbar Click (Covering C0 to B10)
        int scroll_x = grid_x + piano_roll_steps_ * step_w + 5;
        int track_h = num_pitches * row_h;
        if (x >= scroll_x && x <= scroll_x + 16 && y >= grid_top && y <= grid_top + track_h) {
            float ratio = float(y - grid_top) / float(track_h);
            piano_roll_base_pitch_ = std::clamp(static_cast<int>((1.0f - ratio) * max_base_pitch), 0, max_base_pitch);
            status_message_ = "Piano Roll Range: " + get_midi_note_name(piano_roll_base_pitch_) +
                              " to " + get_midi_note_name(piano_roll_base_pitch_ + num_pitches - 1);
            return;
        }

        // 4. Ruler SPM Click
        if (x >= grid_x && x <= grid_x + piano_roll_steps_ * step_w && y >= ruler_y - 8 && y <= ruler_y + 20) {
            dragging_spm_ = true;
            int s = std::clamp((x - grid_x) / step_w, 0, piano_roll_steps_ - 1);
            song_position_marker_ = s * step_ticks;
            engine_.transport().seek(song_position_marker_);
            status_message_ = "Piano Roll SPM set to Step " + std::to_string(s + 1) +
                              " (Tick " + std::to_string(song_position_marker_) + ")";
            return;
        }

        // 5. Piano Key Clicks (Audition sound across full C0..B10 range)
        if (x >= piano_x && x <= piano_x + piano_w && y >= grid_top && y <= grid_top + num_pitches * row_h) {
            int r = std::clamp((y - grid_top) / row_h, 0, num_pitches - 1);
            uint8_t pitch = static_cast<uint8_t>(piano_roll_base_pitch_ + (num_pitches - 1 - r));
            audition_note(pitch);
            status_message_ = "Auditioning " + get_midi_note_name(pitch) + " (MIDI " + std::to_string(pitch) + ")";
            return;
        }

        // 6. Note Grid Click (Add / Resize / Remove Note)
        if (x >= grid_x && x <= grid_x + piano_roll_steps_ * step_w && y >= grid_top && y <= grid_top + num_pitches * row_h) {
            auto& proj = engine_.session().project();
            auto* pat = proj.get_pattern(1);
            if (pat) {
                auto& notes = pat->get_or_create_channel_notes(piano_roll_channel_);

                // Check if an existing note was clicked
                domain::Tick hit_start = 0;
                uint8_t hit_pitch = 0;
                bool hit_note = false;
                bool hit_resize_handle = false;

                for (const auto& n : notes.notes()) {
                    if (n.pitch >= piano_roll_base_pitch_ &&
                        n.pitch < piano_roll_base_pitch_ + num_pitches) {
                        int r = (piano_roll_base_pitch_ + num_pitches - 1) - n.pitch;
                        int ny = grid_top + r * row_h + 1;
                        float s_start = float(n.start) / float(step_ticks);
                        float s_len = float(n.length) / float(step_ticks);
                        int nx = grid_x + static_cast<int>(s_start * step_w) + 1;
                        int nw = std::max(8, static_cast<int>(s_len * step_w) - 2);

                        if (x >= nx && x <= nx + nw && y >= ny && y <= ny + row_h - 3) {
                            hit_note = true;
                            hit_start = n.start;
                            hit_pitch = n.pitch;
                            int handle_w = std::max(8, std::min(14, nw / 2));
                            if (x >= nx + nw - handle_w) {
                                hit_resize_handle = true;
                            }
                            break;
                        }
                    }
                }

                if (hit_note) {
                    if (hit_resize_handle) {
                        resizing_note_ = true;
                        resize_note_start_ = hit_start;
                        resize_note_pitch_ = hit_pitch;
                        resize_start_x_ = x;
                        status_message_ = "Resizing note " + get_midi_note_name(hit_pitch) +
                                          " (Drag mouse rightward to lengthen)";
                    } else {
                        notes.remove_note(hit_start, hit_pitch);
                        status_message_ = "Removed note " + get_midi_note_name(hit_pitch);
                    }
                    return;
                }

                // Add new note with length = piano_roll_note_len_steps_
                int s = std::clamp((x - grid_x) / step_w, 0, piano_roll_steps_ - 1);
                int r = std::clamp((y - grid_top) / row_h, 0, num_pitches - 1);
                uint8_t pitch = static_cast<uint8_t>(piano_roll_base_pitch_ + (num_pitches - 1 - r));
                domain::Tick note_start = s * step_ticks;
                domain::Tick note_len = piano_roll_note_len_steps_ * step_ticks;

                notes.add_note(domain::Note{note_start, note_len, pitch, 100, 0, 0});
                audition_note(pitch);

                // Enable drag-resizing immediately
                resizing_note_ = true;
                resize_note_start_ = note_start;
                resize_note_pitch_ = pitch;
                resize_start_x_ = x;

                status_message_ = "Added " + get_midi_note_name(pitch) + " (" +
                                  std::to_string(piano_roll_note_len_steps_) + " Steps). Drag right to lengthen!";
            }
            return;
        }
    }

    void handle_channel_rack_right_click(int x, int y) {
        int rack_top = 70;
        int ch_y = rack_top + 60;
        int step_h = 36;
        int start_x = 265;
        int bars_per_view = (client_w_ > 1400) ? 12 : 8;
        int bar_gap = 4;
        int avail_w = (client_w_ - 25) - start_x;
        int bar_w = std::max(60, (avail_w - (bars_per_view - 1) * bar_gap) / bars_per_view);
        auto ppq = engine_.session().project().time_map().ppq();
        auto bar_ticks = 4 * ppq;
        auto& proj = engine_.session().project();

        for (size_t ch_idx = 0; ch_idx < proj.channels().size(); ++ch_idx) {
            if (ch_idx < proj.tracks().size() && y >= ch_y && y <= ch_y + step_h && x >= start_x) {
                int b = (x - start_x) / (bar_w + bar_gap);
                if (b >= 0 && b < bars_per_view) {
                    int abs_bar = sequencer_scroll_bar_ + b;
                    domain::Tick bar_start = abs_bar * bar_ticks;
                    auto& track = proj.tracks()[ch_idx];
                    if (track.has_clip_at(bar_start)) {
                        track.remove_clip_at(bar_start);
                        status_message_ = "Removed block at Bar " + std::to_string(abs_bar + 1);
                    }
                }
                return;
            }
            ch_y += step_h + 8;
        }
    }

    void handle_piano_roll_right_click(int x, int y) {
        int rack_top = 70;
        int ruler_y = rack_top + 34;
        int grid_top = ruler_y + 22;
        int grid_bottom = client_h_ - 235;
        int grid_h = grid_bottom - grid_top;
        int piano_x = 25;
        int piano_w = 68;
        int grid_x = piano_x + piano_w + 4;
        int grid_w = (client_w_ - 45) - grid_x;
        int num_pitches = PianoRollNumPitches;
        int row_h = std::max(12, grid_h / num_pitches);
        int step_w = std::max(16, grid_w / piano_roll_steps_);
        auto ppq = engine_.session().project().time_map().ppq();
        auto step_ticks = ppq / 4;

        if (x >= grid_x && x <= grid_x + piano_roll_steps_ * step_w && y >= grid_top && y <= grid_top + num_pitches * row_h) {
            auto& proj = engine_.session().project();
            auto* pat = proj.get_pattern(1);
            if (pat) {
                auto& notes = pat->get_or_create_channel_notes(piano_roll_channel_);
                for (const auto& n : notes.notes()) {
                    if (n.pitch >= piano_roll_base_pitch_ && n.pitch < piano_roll_base_pitch_ + num_pitches) {
                        int r = (piano_roll_base_pitch_ + num_pitches - 1) - n.pitch;
                        int ny = grid_top + r * row_h + 1;
                        float s_start = float(n.start) / float(step_ticks);
                        float s_len = float(n.length) / float(step_ticks);
                        int nx = grid_x + static_cast<int>(s_start * step_w) + 1;
                        int nw = std::max(8, static_cast<int>(s_len * step_w) - 2);

                        if (x >= nx && x <= nx + nw && y >= ny && y <= ny + row_h - 3) {
                            notes.remove_note(n.start, n.pitch);
                            status_message_ = "Removed note " + get_midi_note_name(n.pitch);
                            return;
                        }
                    }
                }
            }
        }
    }

    void on_right_click(int x, int y) {
        std::lock_guard<std::mutex> lock(engine_.audio_mutex());
        if (view_mode_ == ViewMode::ChannelRack) {
            handle_channel_rack_right_click(x, y);
        } else {
            handle_piano_roll_right_click(x, y);
        }
    }

    void on_mouse_move(int x, int y) {
        if (resizing_note_) {
            int piano_w = 68;
            int grid_x = 25 + piano_w + 4;
            int grid_w = (client_w_ - 45) - grid_x;
            int step_w = std::max(16, grid_w / piano_roll_steps_);
            auto ppq = engine_.session().project().time_map().ppq();
            auto step_ticks = ppq / 4;

            int start_step = static_cast<int>(resize_note_start_ / step_ticks);
            int current_step = std::clamp((x - grid_x) / step_w, 0, piano_roll_steps_ - 1);

            int new_steps = std::max(1, current_step - start_step + 1);
            domain::Tick new_length = new_steps * step_ticks;

            auto& proj = engine_.session().project();
            auto* pat = proj.get_pattern(1);
            if (pat) {
                auto& notes = pat->get_or_create_channel_notes(piano_roll_channel_);
                notes.set_note_length(resize_note_start_, resize_note_pitch_, new_length);
                status_message_ = "Note " + get_midi_note_name(resize_note_pitch_) +
                                  " Length: " + std::to_string(new_steps) + " Steps (" +
                                  std::to_string(new_length) + " Ticks)";
            }
            return;
        }

        if (dragging_spm_) {
            auto ppq = engine_.session().project().time_map().ppq();
            auto bar_ticks = 4 * ppq;
            auto step_ticks = ppq / 4;

            if (view_mode_ == ViewMode::ChannelRack) {
                int bars_per_view = (client_w_ > 1400) ? 12 : 8;
                int bar_gap = 4;
                int bar_w = std::max(60, ((client_w_ - 25) - 265 - (bars_per_view - 1) * bar_gap) / bars_per_view);
                int b = std::clamp((x - 265) / (bar_w + bar_gap), 0, bars_per_view - 1);
                song_position_marker_ = (sequencer_scroll_bar_ + b) * bar_ticks;
                status_message_ = "SPM dragged to Bar " + std::to_string(sequencer_scroll_bar_ + b + 1);
            } else {
                int piano_w = 68;
                int grid_x = 25 + piano_w + 4;
                int grid_w = (client_w_ - 45) - grid_x;
                int step_w = std::max(16, grid_w / piano_roll_steps_);
                int s = std::clamp((x - grid_x) / step_w, 0, piano_roll_steps_ - 1);
                song_position_marker_ = s * step_ticks;
                status_message_ = "SPM dragged to Step " + std::to_string(s + 1);
            }

            engine_.transport().seek(song_position_marker_);
            return;
        }

        if (dragging_mixer_track_ >= 0 && dragging_mixer_track_ <= 4) {
            int mixer_top = client_h_ - 215;
            int sx = 30 + dragging_mixer_track_ * (110 + 12);
            RECT fader_rc{sx + 35, mixer_top + 50, sx + 95, client_h_ - 50};
            update_mixer_volume_from_mouse(dragging_mixer_track_, y, fader_rc);
        }
    }

    void update_mixer_volume_from_mouse(int track_idx, int y, const RECT& fader_rc) {
        int track_h = (fader_rc.bottom - 20) - (fader_rc.top + 6);
        float norm = float((fader_rc.bottom - 20) - y) / float(std::max(1, track_h));
        norm = std::clamp(norm, 0.0f, 1.0f);
        float new_vol = norm * 1.5f;

        auto* trk = engine_.session().project().mixer_graph().get_track(track_idx);
        if (trk) {
            trk->set_volume(new_vol);
            float db = (new_vol > 0.01f) ? (20.0f * std::log10(new_vol)) : -120.0f;
            std::string name = (track_idx == 0) ? "Master" : ("Track " + std::to_string(track_idx));
            status_message_ = name + " Volume set to " + (db >= 0.0f ? "+" : "") +
                              std::to_string(static_cast<int>(db)) + " dB";
        }
    }

    void handle_editor_click(int x, int y) {
        int mx = editor_bounds_.left;
        int my = editor_bounds_.top;
        int mw = editor_bounds_.right - editor_bounds_.left;

        if ((x >= mx + mw - 38 && x <= mx + mw - 8 && y >= my + 6 && y <= my + 32) ||
            x < mx || x > editor_bounds_.right || y < my || y > editor_bounds_.bottom) {
            active_editor_channel_ = 0;
            status_message_ = "Closed Instrument Editor";
            return;
        }

        auto* dev = get_active_channel_synth();
        auto* synth = dynamic_cast<plugins::Synth3xOsc*>(dev);
        if (!synth) return;

        // 1. Oscillator 1 Waveform
        int osc1_y = my + 75;
        if (y >= osc1_y && y <= osc1_y + 30) {
            for (int w = 0; w < 4; ++w) {
                if (x >= mx + 20 + w * 62 && x <= mx + 20 + (w + 1) * 62 - 6) {
                    synth->set_parameter(1, (w + 0.5f) / 4.0f);
                    status_message_ = "Osc 1 Shape set to " + std::string(w == 0 ? "Sine" : (w == 1 ? "Saw" : (w == 2 ? "Square" : "Noise")));
                    return;
                }
            }
        }

        // 2. Oscillator 2 Waveform & Semitones
        int osc2_y = my + 155;
        if (y >= osc2_y && y <= osc2_y + 30) {
            for (int w = 0; w < 4; ++w) {
                if (x >= mx + 20 + w * 62 && x <= mx + 20 + (w + 1) * 62 - 6) {
                    synth->set_parameter(3, (w + 0.5f) / 4.0f);
                    status_message_ = "Osc 2 Shape set to " + std::string(w == 0 ? "Sine" : (w == 1 ? "Saw" : (w == 2 ? "Square" : "Noise")));
                    return;
                }
            }

            if (x >= mx + 280 && x <= mx + 310) {
                int semi = static_cast<int>((synth->get_parameter(5) - 0.5f) * 48.0f);
                semi = std::max(-24, semi - 1);
                synth->set_parameter(5, (semi / 48.0f) + 0.5f);
                status_message_ = "Osc 2 Pitch: " + std::to_string(semi) + " semitones";
                return;
            }
            if (x >= mx + 400 && x <= mx + 430) {
                int semi = static_cast<int>((synth->get_parameter(5) - 0.5f) * 48.0f);
                semi = std::min(24, semi + 1);
                synth->set_parameter(5, (semi / 48.0f) + 0.5f);
                status_message_ = "Osc 2 Pitch: " + std::to_string(semi) + " semitones";
                return;
            }
        }

        // 3. Filter Cutoff Slider
        int filt_y = my + 315;
        if (y >= filt_y && y <= filt_y + 30 && x >= mx + 20 && x <= mx + 220) {
            float norm = float(x - (mx + 20)) / 200.0f;
            synth->set_parameter(6, std::clamp(norm, 0.05f, 1.0f));
            status_message_ = "Cutoff adjusted";
            return;
        }

        // 4. Filter Resonance Slider
        if (y >= filt_y && y <= filt_y + 30 && x >= mx + 235 && x <= mx + 435) {
            float norm = float(x - (mx + 235)) / 200.0f;
            synth->set_parameter(7, std::clamp(norm, 0.0f, 1.0f));
            status_message_ = "Resonance adjusted";
            return;
        }

        // 5. Master Volume Slider
        if (y >= filt_y && y <= filt_y + 30 && x >= mx + 450 && x <= mx + mw - 20) {
            float norm = float(x - (mx + 450)) / float(mw - 470);
            synth->set_parameter(0, std::clamp(norm, 0.0f, 1.0f));
            status_message_ = "Master Volume: " + std::to_string(static_cast<int>(norm * 100)) + "%";
            return;
        }

        // 6. Test Note Audition
        if (x >= mx + 20 && x <= mx + 220 && y >= my + 380 && y <= my + 420) {
            audition_note(60);
            status_message_ = "Auditioning note C4 (MIDI 60)...";
            return;
        }
    }

    void audition_note(uint8_t pitch) {
        domain::ChannelId cid = (active_editor_channel_ != 0) ? active_editor_channel_ : piano_roll_channel_;
        engine_.audition_note(cid, pitch, 100);
    }

    domain::IDevice* get_active_channel_synth() {
        domain::ChannelId cid = (active_editor_channel_ != 0) ? active_editor_channel_ : piano_roll_channel_;
        auto dev = engine_.get_or_create_channel_device(cid);
        return dev.get();
    }

    void start_playback_from_spm() {
        if (engine_.transport().state() == app::TransportState::Paused) {
            engine_.transport().seek(song_position_marker_);
            engine_.transport().play();
            status_message_ = "Resumed Playback from paused position";
        } else {
            engine_.transport().seek(song_position_marker_);
            engine_.transport().play();
            status_message_ = "Playback started from Song Position Marker (SPM)";
        }
    }

    void pause_playback() {
        if (engine_.transport().is_playing()) {
            song_position_marker_ = engine_.transport().current_tick();
        }
        engine_.transport().pause();
        engine_.all_notes_off();
        status_message_ = "Playback Paused at position (SPM preserved)";
    }

    void stop_playback() {
        engine_.transport().stop();
        engine_.all_notes_off();
        song_position_marker_ = 0;
        status_message_ = "Playback Stopped (Audio silenced, rewound SPM to start)";
    }

    void toggle_play() {
        if (engine_.transport().is_playing()) {
            pause_playback();
        } else {
            start_playback_from_spm();
        }
    }

    void save_project() {
        std::string filename = engine_.session().project().name() + ".odp";
        auto res = engine_.session().save_project(filename);
        if (res.is_ok()) {
            status_message_ = "Project successfully saved to: " + filename;
        } else {
            status_message_ = "Save failed: " + std::string(res.error().message());
        }
    }

    void render_wav() {
        std::string out_wav = engine_.session().project().name() + "_export.wav";
        auto ppq = engine_.session().project().time_map().ppq();
        domain::Tick max_end_tick = 4 * ppq; // Minimum 1 bar
        for (const auto& trk : engine_.session().project().tracks()) {
            for (const auto& clp : trk.clips()) {
                max_end_tick = std::max(max_end_tick, clp.end());
            }
        }
        auto duration = max_end_tick;

        std::unordered_map<domain::ChannelId, std::shared_ptr<domain::IDevice>> devs;
        for (const auto& ch : engine_.session().project().channels()) {
            auto inst_res = engine_.plugin_manager().instantiate(ch.device_uid());
            if (inst_res.is_ok()) devs[ch.id()] = inst_res.value();
        }

        auto res = app::OfflineRenderer::render_to_wav(
            engine_.session().project(), devs, out_wav, duration, 44100.0);

        if (res.is_ok()) {
            int total_bars = static_cast<int>((duration + 4 * ppq - 1) / (4 * ppq));
            status_message_ = "WAV Export completed (" + std::to_string(total_bars) + " Bars): " + out_wav;
        } else {
            status_message_ = "Render failed: " + std::string(res.error().message());
        }
    }

    void add_channel() {
        auto& proj = engine_.session().project();
        domain::ChannelSettings s;
        s.name = "3xOsc Synth #" + std::to_string(proj.channels().size() + 1);
        s.volume = 0.8f;
        s.mixer_track = static_cast<uint8_t>(std::min(size_t(4), proj.channels().size() + 1));
        proj.add_channel("core.generator.3xosc", s);

        while (proj.tracks().size() < proj.channels().size()) {
            domain::TrackId tid = static_cast<domain::TrackId>(proj.tracks().size() + 1);
            proj.tracks().emplace_back(tid, "Track " + std::to_string(tid));
        }

        status_message_ = "Added channel: " + s.name + " (Mapped to Track " + std::to_string(s.mixer_track) + ")";
    }

    app::Engine& engine_;
    HINSTANCE hinst_{NULL};
    HWND hwnd_{NULL};
    HDC mem_dc_{NULL};
    HBITMAP mem_bmp_{NULL};
    HFONT font_main_{NULL};
    HFONT font_bold_{NULL};
    HFONT font_title_{NULL};
    HFONT font_small_{NULL};

    int client_w_{0};
    int client_h_{0};
    float simulated_meter_l_{0.0f};
    float simulated_meter_r_{0.0f};
    std::string status_message_{""};

    // Transport & SPM
    domain::Tick song_position_marker_{0}; // Song Position Marker (SPM)
    domain::ChannelId active_editor_channel_{0}; // Channel currently being edited in VST GUI
    std::shared_ptr<domain::IDevice> active_synth_instance_{nullptr};
    RECT editor_bounds_{0, 0, 0, 0};
    bool is_mouse_down_{false};
    bool dragging_spm_{false};
    int dragging_mixer_track_{-1};

    // View Mode & Piano Roll State
    ViewMode view_mode_{ViewMode::ChannelRack};
    domain::ChannelId piano_roll_channel_{1};
    int piano_roll_base_pitch_{48}; // C4 root
    static constexpr int PianoRollNumPitches = 20; // 20 semitones visible
    int piano_roll_steps_{16}; // 16 or 32 steps
    int piano_roll_note_len_steps_{1}; // 1, 2, 4, 8 steps
    bool resizing_note_{false};
    domain::Tick resize_note_start_{0};
    uint8_t resize_note_pitch_{0};
    int resize_start_x_{0};

    // Sequencer & Mixer State
    int sequencer_scroll_bar_{0}; // Horizontal bar offset (Bar 1, Bar 5, etc.)
    std::array<float, 8> meter_peaks_{0.0f}; // Real-time audio peaks for tracks 0..4
};

inline DigiDawWindow* DigiDawWindow::instance = nullptr;

} // namespace digidaw::adapters::gui
