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

namespace digidaw::adapters::gui {

enum class PlaybackMode { Pattern, Song };

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
        instance = nullptr;
    }

    bool create_and_show(HINSTANCE hinst, int width = 1100, int height = 750) {
        hinst_ = hinst;

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &DigiDawWindow::wnd_proc_static;
        wc.hInstance = hinst_;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL; // Handled in double-buffering
        wc.lpszClassName = L"DigiDawMainWindowClass";

        RegisterClassExW(&wc);

        // Center on screen
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

        // Create fonts
        font_main_ = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, VARIABLE_PITCH, "Segoe UI");
        font_bold_ = CreateFontA(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, VARIABLE_PITCH, "Segoe UI");
        font_title_ = CreateFontA(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, VARIABLE_PITCH, "Segoe UI");

        // 60 FPS Refresh Timer (16ms)
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
                return 1; // Prevent GDI flicker

            case WM_SIZE: {
                int w = LOWORD(lp);
                int h = HIWORD(lp);
                resize_backbuffer(w, h);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            case WM_TIMER: {
                // Update simulated/actual audio peak meters
                if (engine_.transport().is_playing()) {
                    // Step playhead advances
                    simulated_meter_l_ = 0.5f + 0.3f * std::sin(GetTickCount() * 0.015f);
                    simulated_meter_r_ = 0.5f + 0.3f * std::cos(GetTickCount() * 0.015f);
                    InvalidateRect(hwnd, NULL, FALSE);
                } else {
                    if (simulated_meter_l_ > 0.01f || simulated_meter_r_ > 0.01f) {
                        simulated_meter_l_ *= 0.85f;
                        simulated_meter_r_ *= 0.85f;
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
                return 0;
            }

            case WM_LBUTTONDOWN: {
                int mouse_x = LOWORD(lp);
                int mouse_y = HIWORD(lp);
                on_mouse_down(mouse_x, mouse_y);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            case WM_DROPFILES: {
                HDROP hdrop = reinterpret_cast<HDROP>(wp);
                wchar_t file_path[MAX_PATH];
                if (DragQueryFileW(hdrop, 0, file_path, MAX_PATH)) {
                    // Convert to UTF-8
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
                if (wp == VK_ESCAPE) {
                    stop_playback();
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

        // 1. Render Top Transport Bar
        render_transport_bar();

        // 2. Render Channel Rack & Step Sequencer (Center Area)
        render_channel_rack();

        // 3. Render Mixer & Master Meters (Bottom Area)
        render_mixer_panel();

        // 4. Render Status Bar
        render_status_bar();

        // Blit backbuffer to screen in 1 atomic operation (zero flicker!)
        BitBlt(target_hdc, 0, 0, client_w_, client_h_, mem_dc_, 0, 0, SRCCOPY);
    }

    void render_transport_bar() {
        const auto& t = get_theme();
        RECT bar_rc{0, 0, client_w_, 60};
        GuiRenderer::fill_rect(mem_dc_, bar_rc, t.bg_header);
        GuiRenderer::draw_border(mem_dc_, bar_rc, t.border_dark);

        // App Logo / Title
        SelectObject(mem_dc_, font_title_);
        RECT title_rc{15, 0, 160, 60};
        GuiRenderer::draw_text(mem_dc_, "DigiDAW", title_rc, t.accent_orange, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(mem_dc_, font_bold_);

        // Transport Buttons
        bool playing = engine_.transport().is_playing();
        RECT play_rc{170, 12, 235, 48};
        GuiRenderer::draw_button(mem_dc_, play_rc, playing ? "PAUSE" : "PLAY", playing, t.accent_green, t.bg_card);

        RECT stop_rc{245, 12, 305, 48};
        GuiRenderer::draw_button(mem_dc_, stop_rc, "STOP", false, t.bg_card, t.bg_card);

        // Tempo BPM Display & Buttons
        double bpm = engine_.session().project().time_map().get_bpm_at(0);
        std::stringstream ss_bpm;
        ss_bpm << std::fixed << std::setprecision(1) << bpm << " BPM";

        RECT bpm_minus_rc{325, 15, 350, 45};
        GuiRenderer::draw_button(mem_dc_, bpm_minus_rc, "-", false, t.bg_card, t.bg_card);

        RECT bpm_disp_rc{355, 15, 445, 45};
        GuiRenderer::draw_rounded_box(mem_dc_, bpm_disp_rc, t.bg_input, t.border_dark, 4);
        GuiRenderer::draw_text(mem_dc_, ss_bpm.str(), bpm_disp_rc, t.accent_amber, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT bpm_plus_rc{450, 15, 475, 45};
        GuiRenderer::draw_button(mem_dc_, bpm_plus_rc, "+", false, t.bg_card, t.bg_card);

        // Bar / Beat / Tick Counter
        auto tick = engine_.transport().current_tick();
        auto ppq = engine_.session().project().time_map().ppq();
        int bar = static_cast<int>(tick / (ppq * 4)) + 1;
        int beat = static_cast<int>((tick % (ppq * 4)) / ppq) + 1;
        int sub_tick = static_cast<int>(tick % ppq);

        std::stringstream ss_pos;
        ss_pos << std::setfill('0') << std::setw(3) << bar << ":"
               << std::setfill('0') << std::setw(2) << beat << ":"
               << std::setfill('0') << std::setw(3) << sub_tick;

        RECT pos_rc{495, 15, 605, 45};
        GuiRenderer::draw_rounded_box(mem_dc_, pos_rc, t.bg_input, t.border_dark, 4);
        GuiRenderer::draw_text(mem_dc_, ss_pos.str(), pos_rc, t.accent_cyan, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Action Buttons: Save, Render WAV
        SelectObject(mem_dc_, font_main_);
        RECT save_rc{client_w_ - 210, 15, client_w_ - 115, 45};
        GuiRenderer::draw_button(mem_dc_, save_rc, "SAVE .ODP", false, t.bg_card, t.bg_card);

        RECT rend_rc{client_w_ - 105, 15, client_w_ - 15, 45};
        GuiRenderer::draw_button(mem_dc_, rend_rc, "EXPORT WAV", false, t.bg_card, t.accent_orange);
    }

    void render_channel_rack() {
        const auto& t = get_theme();
        int rack_top = 70;
        int rack_bottom = client_h_ - 200;
        RECT rack_rc{15, rack_top, client_w_ - 15, rack_bottom};

        GuiRenderer::draw_rounded_box(mem_dc_, rack_rc, t.bg_panel, t.border_dark, 8);

        // Rack Header
        SelectObject(mem_dc_, font_bold_);
        RECT header_rc{25, rack_top + 10, 300, rack_top + 30};
        GuiRenderer::draw_text(mem_dc_, "CHANNEL RACK / STEP SEQUENCER (16 STEPS)", header_rc, t.text_secondary);

        // Render channels
        auto& proj = engine_.session().project();
        auto* pat = proj.get_pattern(1);
        int ch_y = rack_top + 45;

        // Step dimensions
        int step_w = std::min(38, (client_w_ - 360) / 16);
        int step_h = 32;

        int current_step = -1;
        if (engine_.transport().is_playing()) {
            auto tick = engine_.transport().current_tick();
            auto step_ticks = proj.time_map().ppq() / 4;
            current_step = static_cast<int>((tick / step_ticks) % 16);
        }

        for (size_t ch_idx = 0; ch_idx < proj.channels().size(); ++ch_idx) {
            auto& ch = proj.channels()[ch_idx];

            // Mute & Solo buttons
            RECT mute_rc{30, ch_y, 55, ch_y + step_h};
            GuiRenderer::draw_button(mem_dc_, mute_rc, "M", ch.settings().muted, t.accent_red, t.bg_card);

            RECT solo_rc{60, ch_y, 85, ch_y + step_h};
            GuiRenderer::draw_button(mem_dc_, solo_rc, "S", ch.settings().solo, t.accent_amber, t.bg_card);

            // Channel Name Tag
            RECT name_rc{95, ch_y, 250, ch_y + step_h};
            GuiRenderer::draw_rounded_box(mem_dc_, name_rc, t.bg_card, t.border_dark, 4);
            SelectObject(mem_dc_, font_bold_);
            RECT name_text_rc{105, ch_y, 245, ch_y + step_h};
            GuiRenderer::draw_text(mem_dc_, ch.settings().name, name_text_rc, t.text_primary, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // 16 Step Buttons
            for (int s = 0; s < 16; ++s) {
                int sx = 265 + s * (step_w + 3);
                RECT step_rc{sx, ch_y, sx + step_w, ch_y + step_h};

                bool note_active = false;
                if (pat) {
                    auto* note_set = pat->get_channel_notes(ch.id());
                    if (note_set) {
                        note_active = note_set->has_note_at_step(s, proj.time_map().ppq());
                    }
                }

                // Shading for 4-step groups
                COLORREF off_col = ((s / 4) % 2 == 0) ? t.step_off_light : t.step_off_dark;
                COLORREF fill_col = note_active ? t.step_on : off_col;
                COLORREF border_col = (s == current_step) ? t.step_playhead : t.border_dark;

                GuiRenderer::draw_rounded_box(mem_dc_, step_rc, fill_col, border_col, 4);

                // LED highlight indicator on active steps
                if (note_active) {
                    RECT led_rc{sx + step_w / 2 - 4, ch_y + 4, sx + step_w / 2 + 4, ch_y + 8};
                    GuiRenderer::fill_rect(mem_dc_, led_rc, RGB(255, 255, 200));
                }
            }

            ch_y += step_h + 10;
        }

        // Add Channel Button
        RECT add_ch_rc{30, ch_y + 5, 170, ch_y + 35};
        SelectObject(mem_dc_, font_main_);
        GuiRenderer::draw_button(mem_dc_, add_ch_rc, "+ Add 3xOsc Synth", false, t.bg_card, t.bg_card);
    }

    void render_mixer_panel() {
        const auto& t = get_theme();
        int mixer_top = client_h_ - 190;
        int mixer_bottom = client_h_ - 30;
        RECT mixer_rc{15, mixer_top, client_w_ - 15, mixer_bottom};

        GuiRenderer::fill_rect(mem_dc_, mixer_rc, t.bg_panel);
        GuiRenderer::draw_border(mem_dc_, mixer_rc, t.border_dark);

        SelectObject(mem_dc_, font_bold_);
        RECT title_rc{25, mixer_top + 8, 200, mixer_top + 25};
        GuiRenderer::draw_text(mem_dc_, "MIXER & OUTPUT BUS", title_rc, t.text_secondary);

        // Strip 0: Master Bus
        int strip_w = 90;
        int strip_x = 30;

        RECT master_rc{strip_x, mixer_top + 28, strip_x + strip_w, mixer_bottom - 10};
        GuiRenderer::draw_rounded_box(mem_dc_, master_rc, t.bg_card, t.border_dark, 4);

        RECT master_label{strip_x, mixer_top + 32, strip_x + strip_w, mixer_top + 48};
        GuiRenderer::draw_text(mem_dc_, "MASTER", master_label, t.accent_orange, DT_CENTER | DT_SINGLELINE);

        // VU Meter Master L & R
        RECT meter_l{strip_x + 20, mixer_top + 55, strip_x + 35, mixer_bottom - 20};
        RECT meter_r{strip_x + 40, mixer_top + 55, strip_x + 55, mixer_bottom - 20};

        GuiRenderer::draw_meter_vertical(mem_dc_, meter_l, simulated_meter_l_);
        GuiRenderer::draw_meter_vertical(mem_dc_, meter_r, simulated_meter_r_);

        // Channel Strips 1..4
        for (int i = 1; i <= 4; ++i) {
            strip_x += strip_w + 10;
            RECT strip_rc{strip_x, mixer_top + 28, strip_x + strip_w, mixer_bottom - 10};
            GuiRenderer::draw_rounded_box(mem_dc_, strip_rc, t.bg_card, t.border_dark, 4);

            std::string label = "TRACK " + std::to_string(i);
            RECT label_rc{strip_x, mixer_top + 32, strip_x + strip_w, mixer_top + 48};
            GuiRenderer::draw_text(mem_dc_, label, label_rc, t.text_secondary, DT_CENTER | DT_SINGLELINE);

            // Channel meter
            float trk_peak = (i == 1) ? simulated_meter_l_ * 0.9f : 0.0f;
            RECT trk_meter{strip_x + 35, mixer_top + 55, strip_x + 55, mixer_bottom - 20};
            GuiRenderer::draw_meter_vertical(mem_dc_, trk_meter, trk_peak);
        }
    }

    void render_status_bar() {
        const auto& t = get_theme();
        RECT status_rc{0, client_h_ - 26, client_w_, client_h_};
        GuiRenderer::fill_rect(mem_dc_, status_rc, t.bg_header);

        SelectObject(mem_dc_, font_main_);
        RECT text_rc{15, client_h_ - 24, client_w_ - 15, client_h_ - 2};

        std::string status = status_message_.empty()
            ? "Ready. Drag & Drop .odp project file to open. Click 16-step pads to edit notes. Space to Play/Pause."
            : status_message_;

        GuiRenderer::draw_text(mem_dc_, status, text_rc, t.text_secondary, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    void on_mouse_down(int x, int y) {
        // 1. Play button
        if (x >= 170 && x <= 235 && y >= 12 && y <= 48) {
            toggle_play();
            return;
        }

        // 2. Stop button
        if (x >= 245 && x <= 305 && y >= 12 && y <= 48) {
            stop_playback();
            return;
        }

        // 3. Tempo +/-
        if (x >= 325 && x <= 350 && y >= 15 && y <= 45) {
            double bpm = engine_.session().project().time_map().get_bpm_at(0);
            engine_.session().project().time_map().set_tempo(std::max(20.0, bpm - 1.0));
            return;
        }
        if (x >= 450 && x <= 475 && y >= 15 && y <= 45) {
            double bpm = engine_.session().project().time_map().get_bpm_at(0);
            engine_.session().project().time_map().set_tempo(std::min(999.0, bpm + 1.0));
            return;
        }

        // 4. Save button
        if (x >= client_w_ - 210 && x <= client_w_ - 115 && y >= 15 && y <= 45) {
            save_project();
            return;
        }

        // 5. Render WAV button
        if (x >= client_w_ - 105 && x <= client_w_ - 15 && y >= 15 && y <= 45) {
            render_wav();
            return;
        }

        // 6. Check Step Sequencer Buttons
        int rack_top = 70;
        int ch_y = rack_top + 45;
        int step_w = std::min(38, (client_w_ - 360) / 16);
        int step_h = 32;

        auto& proj = engine_.session().project();
        auto* pat = proj.get_pattern(1);

        for (size_t ch_idx = 0; ch_idx < proj.channels().size(); ++ch_idx) {
            auto& ch = proj.channels()[ch_idx];

            // Check Mute click
            if (x >= 30 && x <= 55 && y >= ch_y && y <= ch_y + step_h) {
                ch.settings().muted = !ch.settings().muted;
                return;
            }

            // Check Solo click
            if (x >= 60 && x <= 85 && y >= ch_y && y <= ch_y + step_h) {
                ch.settings().solo = !ch.settings().solo;
                return;
            }

            // Check Step buttons 0..15
            for (int s = 0; s < 16; ++s) {
                int sx = 265 + s * (step_w + 3);
                if (x >= sx && x <= sx + step_w && y >= ch_y && y <= ch_y + step_h) {
                    if (pat) {
                        auto& notes = pat->get_or_create_channel_notes(ch.id());
                        notes.toggle_step(s, proj.time_map().ppq(), 60 + static_cast<uint8_t>(ch_idx * 4), 100);
                        status_message_ = "Toggled Step " + std::to_string(s + 1) + " on " + ch.settings().name;
                    }
                    return;
                }
            }

            ch_y += step_h + 10;
        }

        // Add Channel button click
        if (x >= 30 && x <= 170 && y >= ch_y + 5 && y <= ch_y + 35) {
            add_channel();
            return;
        }
    }

    void toggle_play() {
        if (engine_.transport().is_playing()) {
            engine_.transport().pause();
            status_message_ = "Playback Paused";
        } else {
            engine_.transport().play();
            status_message_ = "Playback Started";
        }
    }

    void stop_playback() {
        engine_.transport().stop();
        status_message_ = "Playback Stopped (Rewound to tick 0)";
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
        auto duration = engine_.session().project().time_map().bar_to_tick(4);

        std::unordered_map<domain::ChannelId, std::shared_ptr<domain::IDevice>> devs;
        for (const auto& ch : engine_.session().project().channels()) {
            auto inst_res = engine_.plugin_manager().instantiate(ch.device_uid());
            if (inst_res.is_ok()) devs[ch.id()] = inst_res.value();
        }

        auto res = app::OfflineRenderer::render_to_wav(
            engine_.session().project(), devs, out_wav, duration, 44100.0);

        if (res.is_ok()) {
            status_message_ = "WAV Export completed: " + out_wav;
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
        status_message_ = "Added channel: " + s.name;
    }

    app::Engine& engine_;
    HINSTANCE hinst_{NULL};
    HWND hwnd_{NULL};
    HDC mem_dc_{NULL};
    HBITMAP mem_bmp_{NULL};
    HFONT font_main_{NULL};
    HFONT font_bold_{NULL};
    HFONT font_title_{NULL};

    int client_w_{0};
    int client_h_{0};
    float simulated_meter_l_{0.0f};
    float simulated_meter_r_{0.0f};
    std::string status_message_{""};
};

inline DigiDawWindow* DigiDawWindow::instance = nullptr;

} // namespace digidaw::adapters::gui
