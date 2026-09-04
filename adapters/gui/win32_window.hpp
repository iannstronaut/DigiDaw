#pragma once

#include "../../app/engine.hpp"
#include "theme.hpp"
#include "gui_renderer.hpp"
#include "d2d_renderer.hpp"
#include "d3d_shader_visualizer.hpp"
#include "dpi_awareness.hpp"
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <chrono>

namespace digidaw::adapters::gui {

enum class ViewMode : uint8_t {
    ChannelRack,
    PianoRoll
};

class InsertEffectCommand : public app::ICommand {
public:
    InsertEffectCommand(domain::MixerGraph& graph, uint32_t track_id, std::shared_ptr<domain::IDevice> dev)
        : graph_(graph), track_id_(track_id), dev_(std::move(dev)), slot_idx_(size_t(-1)) {
        label_ = "Add " + (dev_ ? dev_->name() : "Effect");
        saved_slot_ = domain::InsertSlot{dev_, true, 1.0f, false};
    }
    void execute() override {
        auto* trk = graph_.get_track(track_id_);
        if (trk && dev_) {
            if (slot_idx_ <= trk->inserts().size()) {
                trk->inserts().insert(trk->inserts().begin() + slot_idx_, saved_slot_);
            } else {
                trk->add_insert(dev_, saved_slot_.enabled, saved_slot_.wet_mix);
                slot_idx_ = trk->inserts().size() - 1;
                saved_slot_ = trk->inserts()[slot_idx_];
            }
        }
    }
    void undo() override {
        auto* trk = graph_.get_track(track_id_);
        if (trk && slot_idx_ < trk->inserts().size()) {
            saved_slot_ = trk->inserts()[slot_idx_];
            trk->inserts().erase(trk->inserts().begin() + slot_idx_);
        }
    }
    [[nodiscard]] std::string label() const override { return label_; }
private:
    domain::MixerGraph& graph_;
    uint32_t track_id_;
    std::shared_ptr<domain::IDevice> dev_;
    size_t slot_idx_{size_t(-1)};
    domain::InsertSlot saved_slot_{};
    std::string label_;
};

class RemoveEffectCommand : public app::ICommand {
public:
    RemoveEffectCommand(domain::MixerGraph& graph, uint32_t track_id, size_t slot_idx)
        : graph_(graph), track_id_(track_id), slot_idx_(slot_idx) {
        auto* trk = graph_.get_track(track_id_);
        if (trk && slot_idx_ < trk->inserts().size()) {
            saved_slot_ = trk->inserts()[slot_idx_];
            label_ = "Remove " + (saved_slot_.device ? saved_slot_.device->name() : "Effect");
        }
    }
    void execute() override {
        auto* trk = graph_.get_track(track_id_);
        if (trk && slot_idx_ < trk->inserts().size()) {
            trk->inserts().erase(trk->inserts().begin() + slot_idx_);
        }
    }
    void undo() override {
        auto* trk = graph_.get_track(track_id_);
        if (trk) {
            if (slot_idx_ <= trk->inserts().size()) {
                trk->inserts().insert(trk->inserts().begin() + slot_idx_, saved_slot_);
            } else {
                trk->inserts().push_back(saved_slot_);
            }
        }
    }
    [[nodiscard]] std::string label() const override { return label_; }
private:
    domain::MixerGraph& graph_;
    uint32_t track_id_;
    size_t slot_idx_{0};
    domain::InsertSlot saved_slot_{};
    std::string label_{"Remove Effect"};
};

class DigiDawWindow {
public:
    static DigiDawWindow* instance;

    explicit DigiDawWindow(app::Engine& engine)
        : engine_(engine) {
        instance = this;
    }

    ~DigiDawWindow() {
        release_gpu_resources();
        if (mem_dc_) DeleteDC(mem_dc_);
        if (mem_bmp_) DeleteObject(mem_bmp_);
        if (font_main_) DeleteObject(font_main_);
        if (font_bold_) DeleteObject(font_bold_);
        if (font_title_) DeleteObject(font_title_);
        if (font_small_) DeleteObject(font_small_);
        instance = nullptr;
    }

    void update_text_formats(float scale) {
        if (!dwrite_factory_) return;
        if (dwrite_small_) { dwrite_small_->Release(); dwrite_small_ = nullptr; }
        if (dwrite_title_) { dwrite_title_->Release(); dwrite_title_ = nullptr; }
        if (dwrite_bold_) { dwrite_bold_->Release(); dwrite_bold_ = nullptr; }
        if (dwrite_main_) { dwrite_main_->Release(); dwrite_main_ = nullptr; }

        float s = std::max(1.0f, scale);
        float sz_main = std::round(12.0f * s);
        float sz_bold = std::round(12.0f * s);
        float sz_title = std::round(14.0f * s);
        float sz_small = std::round(10.5f * s);

        dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, sz_main, L"en-us", &dwrite_main_);
        dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, sz_bold, L"en-us", &dwrite_bold_);
        dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, sz_title, L"en-us", &dwrite_title_);
        dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, sz_small, L"en-us", &dwrite_small_);
    }

    void update_gdi_fonts(float scale) {
        if (font_main_) { DeleteObject(font_main_); font_main_ = nullptr; }
        if (font_bold_) { DeleteObject(font_bold_); font_bold_ = nullptr; }
        if (font_title_) { DeleteObject(font_title_); font_title_ = nullptr; }
        if (font_small_) { DeleteObject(font_small_); font_small_ = nullptr; }

        float s = std::max(1.0f, scale);
        int h_main = static_cast<int>(std::round(13.0f * s));
        int h_bold = static_cast<int>(std::round(13.0f * s));
        int h_title = static_cast<int>(std::round(16.0f * s));
        int h_small = static_cast<int>(std::round(11.0f * s));

        font_main_ = CreateFontW(h_main, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
        font_bold_ = CreateFontW(h_bold, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
        font_title_ = CreateFontW(h_title, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
        font_small_ = CreateFontW(h_small, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
    }

    bool create_and_show(HINSTANCE hinst, int width = 1150, int height = 780) {
        hinst_ = hinst;
        DpiAwareness::enable_high_dpi_awareness();

        INITCOMMONCONTROLSEX icex{};
        icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
        icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
        InitCommonControlsEx(&icex);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &DigiDawWindow::wnd_proc_static;
        wc.hInstance = hinst_;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL; // Double-buffered GDI / D3D SwapChain
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
            WS_OVERLAPPEDWINDOW | WS_MAXIMIZE,
            x, y, width, height,
            NULL, NULL, hinst_, this
        );

        if (!hwnd_) return false;

        float scale = DpiAwareness::get_scale_factor(hwnd_);
        update_gdi_fonts(scale);

        // 60 FPS update timer (16ms)
        SetTimer(hwnd_, 1, 16, NULL);

        // Maximize window immediately to fill the entire screen
        ShowWindow(hwnd_, SW_SHOWMAXIMIZED);
        UpdateWindow(hwnd_);

        RECT cr{};
        GetClientRect(hwnd_, &cr);
        int cur_w = cr.right - cr.left;
        int cur_h = cr.bottom - cr.top;
        if (cur_w > 0 && cur_h > 0) {
            client_w_ = cur_w;
            client_h_ = cur_h;
            width = cur_w;
            height = cur_h;
        }

        // Initialize Direct3D 11 & Direct2D GPU Pipeline at full maximized screen resolution
        use_d2d_d3d_ = init_d3d_and_d2d(hwnd_, width, height);
        if (use_d2d_d3d_) {
            status_message_ = "Direct2D 1.1 + Direct3D 11 Shaders Active (Hardware Accelerated • Native HD)";
        } else {
            status_message_ = "Running on GDI Double-Buffered Fallback";
        }

        // Trigger immediate native HD redraw
        InvalidateRect(hwnd_, NULL, FALSE);
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

    static constexpr int kInspectorWidth = 260;
    static constexpr int kLayoutMargin = 10;

    int get_effective_workspace_w() const {
        return inspector_open_ ? (client_w_ - (kInspectorWidth + kLayoutMargin)) : client_w_;
    }

    int get_bars_per_view() const {
        int eff_w = get_effective_workspace_w();
        if (eff_w > 1600) return 16;
        if (eff_w > 1300) return 12;
        return 8;
    }

    int get_max_sequencer_bars() const {
        int max_b = 64;
        auto ppq = engine_.session().project().time_map().ppq();
        auto bar_ticks = 4 * ppq;
        for (const auto& track : engine_.session().project().tracks()) {
            for (const auto& clip : track.clips()) {
                int end_b = static_cast<int>((clip.end() + bar_ticks - 1) / bar_ticks);
                if (end_b + 16 > max_b) max_b = end_b + 16;
            }
        }
        return std::min(256, max_b);
    }

    struct PianoRollLayout {
        float rack_top{58.0f};
        float rack_bottom{0.0f};
        float toolbar_y{64.0f};
        float toolbar_h{26.0f};
        float ruler_y{94.0f};
        float ruler_h{20.0f};
        float grid_top{116.0f};
        float h_scroll_h{16.0f};
        float v_scroll_w{16.0f};
        float grid_bottom{0.0f};
        float grid_h{0.0f};
        float piano_x{20.0f};
        float piano_w{64.0f};
        float grid_x{86.0f};
        float grid_w{0.0f};
        float v_scroll_x{0.0f};
        float h_scroll_y{0.0f};
        float row_h{0.0f};
        float step_w{0.0f};

        void init(int client_w, int client_h, int num_pitches, int visible_steps) {
            rack_bottom = static_cast<float>(client_h - 268);
            grid_bottom = rack_bottom - h_scroll_h - 4.0f;
            grid_h = std::max(100.0f, grid_bottom - grid_top);
            v_scroll_x = static_cast<float>(client_w - 20) - v_scroll_w;
            grid_w = std::max(200.0f, v_scroll_x - 4.0f - grid_x);
            h_scroll_y = grid_bottom + 2.0f;
            row_h = grid_h / static_cast<float>(num_pitches);
            step_w = grid_w / static_cast<float>(visible_steps);
        }
    };

    int get_max_piano_roll_steps() const {
        int max_s = 64; // Default 4 bars (64 sixteenth notes)
        auto* pat = engine_.session().project().get_pattern(1);
        if (pat) {
            auto ppq = engine_.session().project().time_map().ppq();
            auto step_ticks = ppq / 4;
            auto* note_set = pat->get_channel_notes(piano_roll_channel_);
            if (note_set) {
                for (const auto& n : note_set->notes()) {
                    int end_s = static_cast<int>((n.start + n.length + step_ticks - 1) / step_ticks);
                    if (end_s + 16 > max_s) max_s = end_s + 16;
                }
            }
        }
        return std::min(512, max_s);
    }

    void toggle_fullscreen() {
        if (!is_fullscreen_) {
            GetWindowRect(hwnd_, &saved_win_rect_);
            saved_win_style_ = GetWindowLong(hwnd_, GWL_STYLE);

            MONITORINFO mi{};
            mi.cbSize = sizeof(MONITORINFO);
            if (GetMonitorInfo(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &mi)) {
                SetWindowLong(hwnd_, GWL_STYLE, saved_win_style_ & ~WS_OVERLAPPEDWINDOW);
                SetWindowPos(hwnd_, HWND_TOP,
                             mi.rcMonitor.left, mi.rcMonitor.top,
                             mi.rcMonitor.right - mi.rcMonitor.left,
                             mi.rcMonitor.bottom - mi.rcMonitor.top,
                             SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
                is_fullscreen_ = true;
                status_message_ = "Entered Fullscreen (Press F11 to restore)";
            }
        } else {
            SetWindowLong(hwnd_, GWL_STYLE, saved_win_style_);
            SetWindowPos(hwnd_, NULL,
                         saved_win_rect_.left, saved_win_rect_.top,
                         saved_win_rect_.right - saved_win_rect_.left,
                         saved_win_rect_.bottom - saved_win_rect_.top,
                         SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            ShowWindow(hwnd_, SW_SHOWMAXIMIZED);
            is_fullscreen_ = false;
            status_message_ = "Restored Maximized Window";
        }
    }

private:
    static LRESULT CALLBACK wnd_proc_static(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        DigiDawWindow* self = nullptr;
        if (msg == WM_NCCREATE) {
            DpiAwareness::enable_non_client_dpi_scaling(hwnd);
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
                if (use_d2d_d3d_) {
                    render_gpu();
                } else {
                    render(hdc);
                }
                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_ERASEBKGND:
                return 1;

            case WM_SIZE: {
                int w = LOWORD(lp);
                int h = HIWORD(lp);
                if (use_d2d_d3d_) {
                    resize_gpu_buffers(w, h);
                } else {
                    resize_backbuffer(w, h);
                }
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            case WM_DPICHANGED: {
                auto* prc = reinterpret_cast<RECT*>(lp);
                if (prc) {
                    SetWindowPos(hwnd, NULL,
                        prc->left, prc->top,
                        prc->right - prc->left, prc->bottom - prc->top,
                        SWP_NOZORDER | SWP_NOACTIVATE);
                }
                float new_scale = static_cast<float>(LOWORD(wp)) / 96.0f;
                update_text_formats(new_scale);
                update_gdi_fonts(new_scale);
                RECT cr{};
                GetClientRect(hwnd, &cr);
                int w = cr.right - cr.left;
                int h = cr.bottom - cr.top;
                if (w > 0 && h > 0) {
                    if (use_d2d_d3d_) {
                        resize_gpu_buffers(w, h);
                    } else {
                        resize_backbuffer(w, h);
                    }
                }
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            case WM_TIMER: {
                auto& proj = engine_.session().project();
                size_t num_poll = std::min(size_t(64), std::max(size_t(5), proj.channels().size() + 1));
                for (size_t i = 0; i < num_poll; ++i) {
                    auto [raw_l, raw_r] = engine_.get_track_peaks_stereo(i);
                    meter_peaks_l_[i] = std::max(raw_l, meter_peaks_l_[i] * 0.85f);
                    meter_peaks_r_[i] = std::max(raw_r, meter_peaks_r_[i] * 0.85f);
                }

                // Update Real-Time Audio Signal Oscilloscope Waveform & Spectrum
                std::array<float, 16> raw_spec{};
                engine_.get_spectrum(raw_spec);

                float master_p = std::max(meter_peaks_l_[0], meter_peaks_r_[0]);
                if (master_p < 0.001f) {
                    // Idle state: smoothly collapse to 0.0f (exact horizontal center line)
                    for (size_t i = 0; i < meter_waveform_.size(); ++i) {
                        meter_waveform_[i] *= 0.70f;
                        if (std::abs(meter_waveform_[i]) < 0.0001f) {
                            meter_waveform_[i] = 0.0f;
                        }
                    }
                } else {
                    std::array<float, 256> raw_wave{};
                    engine_.get_waveform(raw_wave);

                    // Find positive-slope zero-crossing for stable oscilloscope triggering
                    size_t trig = 0;
                    for (size_t i = 0; i < 64; ++i) {
                        if (raw_wave[i] <= 0.0f && raw_wave[i + 1] > 0.0f) {
                            trig = i;
                            break;
                        }
                    }
                    for (size_t i = 0; i < 160; ++i) {
                        float s = raw_wave[trig + i];
                        meter_waveform_[i] = meter_waveform_[i] * 0.20f + s * 0.80f;
                    }
                }

                for (size_t k = 0; k < 16; ++k) {
                    float val = raw_spec[k];
                    // Instant attack, exponential RC analog decay
                    if (val > meter_spectrum_[k]) {
                        meter_spectrum_[k] = val;
                    } else {
                        meter_spectrum_[k] = meter_spectrum_[k] * 0.82f;
                    }
                    // Floating peak-hold dots
                    if (meter_spectrum_[k] > meter_spectrum_peak_[k]) {
                        meter_spectrum_peak_[k] = meter_spectrum_[k];
                    } else {
                        meter_spectrum_peak_[k] = std::max(0.0f, meter_spectrum_peak_[k] - 0.015f);
                    }
                }

                if (use_d2d_d3d_) {
                    render_gpu();
                } else {
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return 0;
            }

            case WM_GETMINMAXINFO: {
                auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
                mmi->ptMinTrackSize.x = 960;
                mmi->ptMinTrackSize.y = 600;
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
                int mouse_x = LOWORD(lp);
                int mouse_y = HIWORD(lp);
                if (is_mouse_down_) {
                    on_mouse_move(mouse_x, mouse_y);
                    InvalidateRect(hwnd, NULL, FALSE);
                } else {
                    on_passive_mouse_move(mouse_x, mouse_y);
                }
                return 0;
            }

            case WM_SETCURSOR: {
                if (dragging_inspector_vol_ || dragging_inspector_pan_ || dragging_inspector_wet_slot_ >= 0) {
                    SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                    return TRUE;
                }
                if (dragging_inspector_scrollbar_) {
                    SetCursor(LoadCursor(NULL, IDC_SIZENS));
                    return TRUE;
                }
                if (dragging_mixer_track_ >= 0 || dragging_mixer_pan_track_ >= 0) {
                    SetCursor(LoadCursor(NULL, IDC_SIZENS));
                    return TRUE;
                }
                if (view_mode_ == ViewMode::ChannelRack) {
                    if (dragging_seq_v_scrollbar_ || is_hovering_seq_v_scrollbar_) {
                        SetCursor(LoadCursor(NULL, IDC_SIZENS));
                        return TRUE;
                    }
                    if (dragging_seq_scrollbar_ || is_hovering_seq_scrollbar_) {
                        SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                        return TRUE;
                    }
                    if (clip_drag_mode_ == ClipDragMode::Resize || is_hovering_clip_edge_) {
                        SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                        return TRUE;
                    }
                    if (clip_drag_mode_ == ClipDragMode::Move) {
                        SetCursor(LoadCursor(NULL, IDC_SIZEALL));
                        return TRUE;
                    }
                } else if (view_mode_ == ViewMode::PianoRoll) {
                    if (dragging_piano_v_scrollbar_ || is_hovering_piano_v_scrollbar_) {
                        SetCursor(LoadCursor(NULL, IDC_SIZENS));
                        return TRUE;
                    }
                    if (dragging_piano_h_scrollbar_ || is_hovering_piano_h_scrollbar_ ||
                        note_drag_mode_ == NoteDragMode::Resize || is_hovering_note_edge_) {
                        SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                        return TRUE;
                    }
                    if (note_drag_mode_ == NoteDragMode::Move || is_hovering_note_body_) {
                        SetCursor(LoadCursor(NULL, IDC_SIZEALL));
                        return TRUE;
                    }
                }
                return DefWindowProc(hwnd, msg, wp, lp);
            }

            case WM_LBUTTONUP: {
                is_mouse_down_ = false;
                dragging_spm_ = false;
                dragging_seq_scrollbar_ = false;
                dragging_seq_v_scrollbar_ = false;
                dragging_piano_v_scrollbar_ = false;
                dragging_piano_h_scrollbar_ = false;
                dragging_inspector_vol_ = false;
                dragging_inspector_pan_ = false;
                dragging_inspector_wet_slot_ = -1;
                dragging_inspector_scrollbar_ = false;
                dragging_mixer_track_ = -1;
                dragging_mixer_pan_track_ = -1;
                note_drag_mode_ = NoteDragMode::None;
                if (clip_drag_mode_ != ClipDragMode::None) {
                    if (drag_clip_track_idx_ < engine_.session().project().tracks().size()) {
                        engine_.session().project().tracks()[drag_clip_track_idx_].sort_clips();
                    }
                    clip_drag_mode_ = ClipDragMode::None;
                }
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
                bool is_shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

                POINT mouse_pt;
                GetCursorPos(&mouse_pt);
                ScreenToClient(hwnd, &mouse_pt);

                if (mouse_pt.y >= client_h_ - 262) {
                    // Mouse in Mixer Panel -> Horizontal insert track scrolling
                    auto& proj = engine_.session().project();
                    float track_w = 96.0f;
                    float gap = 12.0f;
                    float insert_start_x = 25.0f + track_w + gap;
                    float avail_w = (static_cast<float>(client_w_) - 25.0f) - insert_start_x;
                    int vis_inserts = std::max(1, static_cast<int>(avail_w / (track_w + gap)));
                    size_t total_inserts = std::max(size_t(4), proj.channels().size());
                    int max_mix_scroll = std::max(0, static_cast<int>(total_inserts) - vis_inserts);
                    if (max_mix_scroll > 0) {
                        mixer_scroll_track_ = std::clamp(mixer_scroll_track_ - steps, 0, max_mix_scroll);
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                } else if (inspector_open_ && mouse_pt.x >= client_w_ - (kInspectorWidth + kLayoutMargin) && mouse_pt.x <= client_w_ - kLayoutMargin && mouse_pt.y >= 58 && mouse_pt.y <= client_h_ - 268) {
                    // Mouse inside Right-Side Inspector Panel
                    int insp_x = client_w_ - (kInspectorWidth + kLayoutMargin);
                    int insp_r = client_w_ - kLayoutMargin;
                    int insp_y = 58;
                    int insp_bot = client_h_ - 268;
                    auto* track = engine_.session().project().mixer_graph().get_track(selected_mixer_track_);
                    if (track) {
                        std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
                        // Volume slider area: [insp_x + 14, insp_y + 90, insp_r - 14, insp_y + 108]
                        if (mouse_pt.x >= insp_x + 14 && mouse_pt.x <= insp_r - 14 && mouse_pt.y >= insp_y + 90 && mouse_pt.y <= insp_y + 108) {
                            float new_vol = std::clamp(track->volume() + steps * 0.05f, 0.0f, 1.25f);
                            track->set_volume(new_vol);
                            int vol_pct = static_cast<int>(std::round(track->volume() * 100.0f));
                            status_message_ = (selected_mixer_track_ == 0 ? "Master" : ("Track " + std::to_string(selected_mixer_track_))) +
                                              " Volume: " + std::to_string(vol_pct) + "%";
                        }
                        // Pan slider area: [insp_x + 14, insp_y + 112, insp_r - 14, insp_y + 130]
                        else if (mouse_pt.x >= insp_x + 14 && mouse_pt.x <= insp_r - 14 && mouse_pt.y >= insp_y + 112 && mouse_pt.y <= insp_y + 130) {
                            float new_pan = std::clamp(track->pan() + steps * 0.05f, -1.0f, 1.0f);
                            track->set_pan(new_pan);
                            std::string pan_str = (std::abs(new_pan) < 0.02f) ? "Center" :
                                (new_pan < 0.0f ? "Left " + std::to_string(static_cast<int>(std::round(-new_pan * 100.0f))) + "%"
                                                : "Right " + std::to_string(static_cast<int>(std::round(new_pan * 100.0f))) + "%");
                            status_message_ = (selected_mixer_track_ == 0 ? "Master" : ("Track " + std::to_string(selected_mixer_track_))) +
                                              " Pan: " + pan_str;
                        }
                        // FX slot list / wet mix / vertical scrolling
                        else {
                            size_t num_fx = track->inserts().size();
                            int slot_y_start = insp_y + 192;
                            int slot_h = 32;
                            int slot_gap = 4;
                            int fx_area_bot = insp_bot - 8;
                            int fx_avail_h = fx_area_bot - slot_y_start;
                            int vis_fx_items = std::max(1, fx_avail_h / (slot_h + slot_gap));
                            int total_fx_items = static_cast<int>(num_fx) + (num_fx < 10 ? 1 : 0);
                            int max_fx_scroll = std::max(0, total_fx_items - vis_fx_items);
                            int right_margin = (max_fx_scroll > 0) ? 14 : 8;

                            bool adjusted_wet = false;
                            for (int i = inspector_scroll_slot_; i < static_cast<int>(num_fx); ++i) {
                                int sy = slot_y_start + (i - inspector_scroll_slot_) * (slot_h + slot_gap);
                                if (sy + slot_h > fx_area_bot) break;
                                if (mouse_pt.x >= insp_r - (right_margin + 80) && mouse_pt.x <= insp_r - (right_margin + 26) &&
                                    mouse_pt.y >= sy + 5 && mouse_pt.y <= sy + slot_h - 5) {
                                    track->inserts()[i].wet_mix = std::clamp(track->inserts()[i].wet_mix + steps * 0.05f, 0.0f, 1.0f);
                                    int pct = static_cast<int>(std::round(track->inserts()[i].wet_mix * 100.0f));
                                    std::string name = track->inserts()[i].device ? track->inserts()[i].device->name() : "Effect";
                                    status_message_ = name + " Wet Mix: " + std::to_string(pct) + "%";
                                    adjusted_wet = true;
                                    break;
                                }
                            }
                            if (!adjusted_wet && max_fx_scroll > 0) {
                                inspector_scroll_slot_ = std::clamp(inspector_scroll_slot_ - steps, 0, max_fx_scroll);
                                status_message_ = "Inspector FX Scrolled (" + std::to_string(inspector_scroll_slot_ + 1) + "/" + std::to_string(max_fx_scroll + 1) + ")";
                            }
                        }
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                } else if (view_mode_ == ViewMode::PianoRoll) {
                    if (is_shift) {
                        int max_steps = get_max_piano_roll_steps();
                        int max_scroll = std::max(0, max_steps - piano_roll_steps_);
                        piano_roll_scroll_step_ = std::clamp(piano_roll_scroll_step_ - steps * 4, 0, max_scroll);
                        status_message_ = "Piano Roll Timeline: Step " + std::to_string(piano_roll_scroll_step_ + 1) + " / " + std::to_string(max_steps);
                    } else {
                        piano_roll_base_pitch_ = std::clamp(piano_roll_base_pitch_ + steps * 2, 0, 128 - PianoRollNumPitches);
                        status_message_ = "Pitch Range: " + get_midi_note_name(static_cast<uint8_t>(piano_roll_base_pitch_)) +
                                          " to " + get_midi_note_name(static_cast<uint8_t>(piano_roll_base_pitch_ + PianoRollNumPitches - 1));
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                } else if (view_mode_ == ViewMode::ChannelRack) {
                    if (is_shift) {
                        int max_bars = get_max_sequencer_bars();
                        int bars_per_view = get_bars_per_view();
                        int max_scroll = std::max(0, max_bars - bars_per_view);
                        sequencer_scroll_bar_ = std::clamp(sequencer_scroll_bar_ - steps, 0, max_scroll);
                        status_message_ = "Timeline scrolled to Bar " + std::to_string(sequencer_scroll_bar_ + 1) + " / " + std::to_string(max_bars);
                    } else {
                        auto& proj = engine_.session().project();
                        float row_h = 48.0f;
                        float rack_avail_h = (static_cast<float>(client_h_ - 268) - 26.0f) - 124.0f;
                        int visible_channels = std::max(1, static_cast<int>(rack_avail_h / row_h));
                        int max_ch_scroll = std::max(0, static_cast<int>(proj.channels().size()) - visible_channels + 1);
                        if (max_ch_scroll > 0) {
                            sequencer_scroll_track_ = std::clamp(sequencer_scroll_track_ - steps, 0, max_ch_scroll);
                        } else {
                            int max_bars = get_max_sequencer_bars();
                            int bars_per_view = get_bars_per_view();
                            int max_scroll = std::max(0, max_bars - bars_per_view);
                            sequencer_scroll_bar_ = std::clamp(sequencer_scroll_bar_ - steps, 0, max_scroll);
                        }
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return 0;
            }

            case WM_MOUSEHWHEEL: {
                short zDelta = GET_WHEEL_DELTA_WPARAM(wp);
                int steps = zDelta / WHEEL_DELTA;
                if (view_mode_ == ViewMode::PianoRoll) {
                    int max_steps = get_max_piano_roll_steps();
                    int max_scroll = std::max(0, max_steps - piano_roll_steps_);
                    piano_roll_scroll_step_ = std::clamp(piano_roll_scroll_step_ + steps * 4, 0, max_scroll);
                    status_message_ = "Piano Roll Timeline: Step " + std::to_string(piano_roll_scroll_step_ + 1) + " / " + std::to_string(max_steps);
                    InvalidateRect(hwnd, NULL, FALSE);
                } else if (view_mode_ == ViewMode::ChannelRack) {
                    int max_bars = get_max_sequencer_bars();
                    int bars_per_view = get_bars_per_view();
                    int max_scroll = std::max(0, max_bars - bars_per_view);
                    sequencer_scroll_bar_ = std::clamp(sequencer_scroll_bar_ + steps, 0, max_scroll);
                    status_message_ = "Timeline scrolled to Bar " + std::to_string(sequencer_scroll_bar_ + 1) + " / " + std::to_string(max_bars);
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
                if (wp == VK_F11) {
                    toggle_fullscreen();
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                if (wp == VK_F8) {
                    inspector_open_ = !inspector_open_;
                    status_message_ = inspector_open_ ? "Opened Inspector Panel (F8)" : "Collapsed Inspector Panel (F8)";
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                if (GetKeyState(VK_CONTROL) & 0x8000) {
                    if (wp == 'Z') {
                        if (GetKeyState(VK_SHIFT) & 0x8000) {
                            do_redo();
                        } else {
                            do_undo();
                        }
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                    if (wp == 'Y') {
                        do_redo();
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                    if (wp == 'S') {
                        save_project();
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                    if (wp == 'I') {
                        inspector_open_ = !inspector_open_;
                        status_message_ = inspector_open_ ? "Opened Inspector Panel (Ctrl+I)" : "Collapsed Inspector Panel (Ctrl+I)";
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                }
                if (wp == VK_SPACE) {
                    toggle_play();
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                if (wp == VK_F6) {
                    view_mode_ = ViewMode::ChannelRack;
                    status_message_ = "Switched to Playlist (F6)";
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                if (wp == VK_F7) {
                    view_mode_ = ViewMode::PianoRoll;
                    for (const auto& ch : engine_.session().project().channels()) {
                        if (ch.id() == piano_roll_channel_) {
                            selected_mixer_track_ = ch.settings().mixer_track;
                            break;
                        }
                    }
                    status_message_ = "Switched to Piano roll (F7)";
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

    // Direct3D 11 & Direct2D Hardware Accelerated GPU Pipeline
    bool init_d3d_and_d2d(HWND hwnd, int width, int height) {
        if (!hwnd || width <= 0 || height <= 0) return false;

        client_w_ = width;
        client_h_ = height;

        // 1. Setup DXGI SwapChain and D3D11 Device
        DXGI_SWAP_CHAIN_DESC scd = {};
        scd.BufferCount = 1;
        scd.BufferDesc.Width = width;
        scd.BufferDesc.Height = height;
        scd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        scd.BufferDesc.RefreshRate.Numerator = 60;
        scd.BufferDesc.RefreshRate.Denominator = 1;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.OutputWindow = hwnd;
        scd.SampleDesc.Count = 1;
        scd.SampleDesc.Quality = 0;
        scd.Windowed = TRUE;
        scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

        D3D_FEATURE_LEVEL feature_level;
        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
            D3D11_SDK_VERSION, &scd, &swap_chain_, &d3d_device_, &feature_level, &d3d_context_);

        if (FAILED(hr)) {
            hr = D3D11CreateDeviceAndSwapChain(
                nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                D3D11_SDK_VERSION, &scd, &swap_chain_, &d3d_device_, &feature_level, &d3d_context_);
        }

        if (FAILED(hr) || !swap_chain_ || !d3d_device_ || !d3d_context_) {
            return false;
        }

        // 2. Initialize Direct3D 11 Shader Visualizer
        shader_visualizer_.init(d3d_device_);

        // 3. Initialize Direct2D & DirectWrite Factories
        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory_);
        if (FAILED(hr) || !d2d_factory_) return false;

        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                reinterpret_cast<IUnknown**>(&dwrite_factory_));
        if (FAILED(hr) || !dwrite_factory_) return false;

        // 4. Create DirectWrite Text Formats scaled to window DPI
        update_text_formats(DpiAwareness::get_scale_factor(hwnd));

        // 5. Create Render Target Views
        create_gpu_render_targets(width, height);
        return (d2d_target_ != nullptr && d3d_rtv_ != nullptr);
    }

    void create_gpu_render_targets(int width, int height) {
        if (!swap_chain_ || !d3d_device_ || !d2d_factory_ || width <= 0 || height <= 0) return;

        // 1. Create D3D11 Render Target View from back buffer
        ID3D11Texture2D* back_buffer = nullptr;
        HRESULT hr = swap_chain_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back_buffer));
        if (SUCCEEDED(hr) && back_buffer) {
            d3d_device_->CreateRenderTargetView(back_buffer, nullptr, &d3d_rtv_);

            // 2. Query IDXGISurface
            IDXGISurface* dxgi_surface = nullptr;
            hr = back_buffer->QueryInterface(__uuidof(IDXGISurface), reinterpret_cast<void**>(&dxgi_surface));
            if (SUCCEEDED(hr) && dxgi_surface) {
                // 3. Create Direct2D Render Target with explicit 96.0f DPI for 1:1 physical pixel mapping
                // D2D1_ALPHA_MODE_IGNORE ensures full ClearType subpixel rendering without grayscale fallback
                D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
                    D2D1_RENDER_TARGET_TYPE_DEFAULT,
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
                    96.0f, 96.0f);

                hr = d2d_factory_->CreateDxgiSurfaceRenderTarget(dxgi_surface, &props, &d2d_target_);
                if (SUCCEEDED(hr) && d2d_target_) {
                    // Set 1:1 physical pixel mapping so Direct2D coordinates map to exact physical backbuffer pixels
                    d2d_target_->SetDpi(96.0f, 96.0f);

                    // High-quality per-primitive geometric antialiasing
                    d2d_target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

                    // Force ClearType subpixel text antialiasing for maximum sharpness
                    d2d_target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

                    // Configure DirectWrite text rendering parameters for maximum sharpness
                    setup_dwrite_rendering_params();
                }
                dxgi_surface->Release();
            }
            back_buffer->Release();
        }
    }

    void setup_dwrite_rendering_params() {
        if (!dwrite_factory_ || !d2d_target_) return;

        IDWriteRenderingParams* default_params = nullptr;
        HMONITOR hmon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        HRESULT hr = E_FAIL;
        if (hmon) {
            hr = dwrite_factory_->CreateMonitorRenderingParams(hmon, &default_params);
        }
        if (FAILED(hr) || !default_params) {
            hr = dwrite_factory_->CreateRenderingParams(&default_params);
        }

        if (SUCCEEDED(hr) && default_params) {
            IDWriteRenderingParams* custom_params = nullptr;
            // DWRITE_RENDERING_MODE_CLEARTYPE_GDI_NATURAL ensures razor-sharp glyph stems
            // snapped to physical pixel columns without the vertical blurriness of NATURAL_SYMMETRIC.
            // Ensure RGB pixel geometry (never FLAT) so DirectWrite does not fall back to blurry grayscale.
            DWRITE_PIXEL_GEOMETRY pixel_geom = default_params->GetPixelGeometry();
            if (pixel_geom == DWRITE_PIXEL_GEOMETRY_FLAT) {
                pixel_geom = DWRITE_PIXEL_GEOMETRY_RGB;
            }
            float gamma = (default_params->GetGamma() > 0.0f) ? default_params->GetGamma() : 1.8f;
            float contrast = std::clamp(std::max(1.0f, default_params->GetEnhancedContrast()), 1.0f, 2.0f);
            hr = dwrite_factory_->CreateCustomRenderingParams(
                gamma,
                contrast,
                1.0f, // 100% ClearType level
                pixel_geom,
                DWRITE_RENDERING_MODE_CLEARTYPE_GDI_NATURAL,
                &custom_params
            );
            if (SUCCEEDED(hr) && custom_params) {
                d2d_target_->SetTextRenderingParams(custom_params);
                custom_params->Release();
            } else {
                d2d_target_->SetTextRenderingParams(default_params);
            }
            default_params->Release();
        }
    }

    void resize_gpu_buffers(int width, int height) {
        if (width <= 0 || height <= 0 || !swap_chain_) return;
        client_w_ = width;
        client_h_ = height;

        if (d2d_target_) { d2d_target_->Release(); d2d_target_ = nullptr; }
        if (d3d_rtv_) { d3d_rtv_->Release(); d3d_rtv_ = nullptr; }

        swap_chain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        create_gpu_render_targets(width, height);
    }

    void release_gpu_resources() {
        if (dwrite_small_) { dwrite_small_->Release(); dwrite_small_ = nullptr; }
        if (dwrite_title_) { dwrite_title_->Release(); dwrite_title_ = nullptr; }
        if (dwrite_bold_) { dwrite_bold_->Release(); dwrite_bold_ = nullptr; }
        if (dwrite_main_) { dwrite_main_->Release(); dwrite_main_ = nullptr; }
        if (dwrite_factory_) { dwrite_factory_->Release(); dwrite_factory_ = nullptr; }

        if (d2d_target_) { d2d_target_->Release(); d2d_target_ = nullptr; }
        if (d2d_factory_) { d2d_factory_->Release(); d2d_factory_ = nullptr; }

        shader_visualizer_.release();

        if (d3d_rtv_) { d3d_rtv_->Release(); d3d_rtv_ = nullptr; }
        if (swap_chain_) { swap_chain_->Release(); swap_chain_ = nullptr; }
        if (d3d_context_) { d3d_context_->Release(); d3d_context_ = nullptr; }
        if (d3d_device_) { d3d_device_->Release(); d3d_device_ = nullptr; }
        use_d2d_d3d_ = false;
    }

    void render_gpu() {
        if (!swap_chain_ || !d2d_target_ || !d3d_rtv_ || client_w_ <= 0 || client_h_ <= 0) return;

        // 1. Direct3D 11 Pass: Render Audio Reactive Shader Visualizer on the GPU
        static auto start_time = std::chrono::steady_clock::now();
        float time_sec = std::chrono::duration<float>(std::chrono::steady_clock::now() - start_time).count();
        float master_peak = std::max(meter_peaks_l_[0], meter_peaks_r_[0]);
        bool is_playing = engine_.transport().is_playing();
        auto ppq = engine_.session().project().time_map().ppq();
        float spm_norm = float(song_position_marker_) / float(std::max(1LL, 4LL * 8LL * ppq));

        std::array<float, 8> viz_peaks{};
        for (size_t i = 0; i < 8; ++i) {
            viz_peaks[i] = std::max(meter_peaks_l_[i], meter_peaks_r_[i]);
        }

        shader_visualizer_.render(d3d_context_, d3d_rtv_, client_w_, client_h_,
                                 time_sec, master_peak, viz_peaks, is_playing, spm_norm);

        // 2. Direct2D Pass: Render vector UI with hardware subpixel anti-aliasing
        d2d_target_->BeginDraw();

        render_transport_bar_d2d();
        if (view_mode_ == ViewMode::ChannelRack) {
            render_channel_rack_d2d();
        } else {
            render_piano_roll_d2d();
        }
        render_inspector_d2d();
        render_mixer_panel_d2d();
        render_status_bar_d2d();

        if (active_editor_channel_ != 0) {
            render_plugin_editor_d2d();
        }

        HRESULT hr = d2d_target_->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) {
            resize_gpu_buffers(client_w_, client_h_);
        }

        // 3. Hardware VSync SwapChain Presentation
        swap_chain_->Present(1, 0);
    }

    void render_transport_bar_d2d() {
        const auto& t = D2DRenderer::theme();
        D2D1_RECT_F bar_rc = D2D1::RectF(0, 0, static_cast<float>(client_w_), 48.0f);
        // Glassy overlay allowing the D3D shader visualizer to shine through
        D2DRenderer::draw_rounded_box(d2d_target_, bar_rc, D2D1::ColorF(0.045f, 0.052f, 0.063f, 0.96f), t.border_subtle, 0.0f);

        // Crisp 1px bottom border
        ID2D1SolidColorBrush* br_border = nullptr;
        d2d_target_->CreateSolidColorBrush(t.border_subtle, &br_border);
        if (br_border) {
            d2d_target_->DrawLine(D2D1::Point2F(0.0f, 47.5f), D2D1::Point2F(static_cast<float>(client_w_), 47.5f), br_border, 1.0f);
            br_border->Release();
        }

        // Logo / Wordmark
        D2D1_RECT_F title_rc = D2D1::RectF(16.0f, 0.0f, 84.0f, 48.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_title_, "DigiDAW", title_rc, t.text_primary);

        // Project Name badge (e.g. "Untitled Project")
        std::string proj_name = engine_.session().project().name();
        if (proj_name.empty()) proj_name = "Untitled Project";
        D2D1_RECT_F proj_rc = D2D1::RectF(88.0f, 8.0f, 198.0f, 40.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, proj_rc, t.bg_control, t.border_subtle, 3.5f);
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, proj_name, proj_rc, t.accent_bright,
                              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Undo [↶] & Redo [↷] buttons
        bool can_undo = engine_.session().undo_stack().can_undo();
        bool can_redo = engine_.session().undo_stack().can_redo();
        D2D1_RECT_F undo_rc = D2D1::RectF(204.0f, 8.0f, 234.0f, 40.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, undo_rc, t.bg_control, can_undo ? t.border_default : t.border_subtle, 3.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, "↶", undo_rc,
                              can_undo ? t.text_primary : t.text_disabled,
                              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        D2D1_RECT_F redo_rc = D2D1::RectF(238.0f, 8.0f, 268.0f, 40.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, redo_rc, t.bg_control, can_redo ? t.border_default : t.border_subtle, 3.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, "↷", redo_rc,
                              can_redo ? t.text_primary : t.text_disabled,
                              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Buttons: Icon-first, small rectangular (DESIGN.md section 2)
        bool is_playing = engine_.transport().is_playing();
        bool is_paused = (engine_.transport().state() == app::TransportState::Paused);

        // 1. PLAY Button [▶] (active: purple accent)
        D2D1_RECT_F play_rc = D2D1::RectF(276.0f, 8.0f, 312.0f, 40.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_bold_, play_rc, "▶", is_playing, t.accent, t.bg_control, 3.5f);

        // 2. PAUSE Button [❚❚] (active: warning amber)
        D2D1_RECT_F pause_rc = D2D1::RectF(316.0f, 8.0f, 352.0f, 40.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_bold_, pause_rc, "❚❚", is_paused, t.warning, t.bg_control, 3.5f);

        // 3. STOP Button [■]
        D2D1_RECT_F stop_rc = D2D1::RectF(356.0f, 8.0f, 392.0f, 40.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_bold_, stop_rc, "■", false, t.danger, t.bg_control, 3.5f);

        // Tempo Controls (- BPM +)
        double bpm = engine_.session().project().time_map().get_bpm_at(0);
        std::stringstream ss_bpm;
        ss_bpm << std::fixed << std::setprecision(1) << bpm << " BPM";

        D2D1_RECT_F bpm_minus_rc = D2D1::RectF(400.0f, 8.0f, 424.0f, 40.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_bold_, bpm_minus_rc, "-", false, t.bg_control, t.bg_control, 3.0f);

        D2D1_RECT_F bpm_disp_rc = D2D1::RectF(428.0f, 8.0f, 508.0f, 40.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, bpm_disp_rc, t.bg_control, t.border_subtle, 3.5f);
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, ss_bpm.str(), bpm_disp_rc, t.text_primary,
                              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        D2D1_RECT_F bpm_plus_rc = D2D1::RectF(512.0f, 8.0f, 536.0f, 40.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_bold_, bpm_plus_rc, "+", false, t.bg_control, t.bg_control, 3.0f);

        // Real-Time Position Clock starting from 00:00.00
        auto tick = engine_.transport().current_tick();
        auto ppq = engine_.session().project().time_map().ppq();
        double tempo = engine_.session().project().time_map().get_bpm_at(tick);
        double beats = double(tick) / double(ppq);
        double seconds_total = (tempo > 0.0) ? (beats * 60.0 / tempo) : 0.0;
        int mins = static_cast<int>(seconds_total) / 60;
        int secs = static_cast<int>(seconds_total) % 60;
        int centis = static_cast<int>((seconds_total - std::floor(seconds_total)) * 100.0);
        int bar = static_cast<int>(tick / (ppq * 4)) + 1;

        std::stringstream ss_pos;
        ss_pos << std::setfill('0') << std::setw(2) << mins << ":"
               << std::setfill('0') << std::setw(2) << secs << "."
               << std::setfill('0') << std::setw(2) << centis
               << " | Bar " << bar;

        D2D1_RECT_F pos_rc = D2D1::RectF(544.0f, 8.0f, 676.0f, 40.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, pos_rc, t.bg_control, t.border_subtle, 3.5f);
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, ss_pos.str(), pos_rc, t.accent_bright,
                              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // View Mode Switcher: Icon-first [🎛], [🎹], and Inspector toggle [ℹ]
        D2D1_RECT_F rack_btn_rc = D2D1::RectF(684.0f, 8.0f, 722.0f, 40.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_bold_, rack_btn_rc, "🎛",
                                view_mode_ == ViewMode::ChannelRack, t.accent, t.bg_control, 3.5f);

        D2D1_RECT_F roll_btn_rc = D2D1::RectF(726.0f, 8.0f, 764.0f, 40.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_bold_, roll_btn_rc, "🎹",
                                view_mode_ == ViewMode::PianoRoll, t.accent, t.bg_control, 3.5f);

        D2D1_RECT_F insp_btn_rc = D2D1::RectF(768.0f, 8.0f, 806.0f, 40.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_bold_, insp_btn_rc, "ℹ",
                                inspector_open_, t.accent, t.bg_control, 3.5f);

        // 7. Real-Time Audio Signal Oscilloscope Section (Electric violet phosphor filament)
        float spec_x = 816.0f;
        float spec_max_right = static_cast<float>(client_w_ - 88);
        if (spec_max_right > spec_x + 50.0f) {
            float spec_w = std::min(240.0f, spec_max_right - spec_x);
            float spec_y = 8.0f;
            float spec_h = 32.0f;
            D2D1_RECT_F chassis_rc = D2D1::RectF(spec_x, spec_y, spec_x + spec_w, spec_y + spec_h);

            // Dark Analog Chassis Bezel
            D2DRenderer::draw_rounded_box(d2d_target_, chassis_rc, t.bg_control, t.border_subtle, 3.0f);

            // Inset Screen Glass
            D2D1_RECT_F screen_rc = D2D1::RectF(spec_x + 2.0f, spec_y + 2.0f, spec_x + spec_w - 2.0f, spec_y + spec_h - 2.0f);
            D2DRenderer::draw_rounded_box(d2d_target_, screen_rc, t.bg_app, t.border_subtle, 2.0f);

            float center_y = (screen_rc.top + screen_rc.bottom) * 0.5f;
            float max_amp = (screen_rc.bottom - screen_rc.top) * 0.5f - 2.0f;

            // Continuous Real-Time Audio Signal Oscilloscope Line
            float start_x = screen_rc.left + 2.0f;
            float end_x = screen_rc.right - 2.0f;
            constexpr size_t num_pts = 128;
            float step_x = (end_x - start_x) / static_cast<float>(num_pts - 1);

            ID2D1PathGeometry* scope_geo = nullptr;
            if (SUCCEEDED(d2d_factory_->CreatePathGeometry(&scope_geo))) {
                ID2D1GeometrySink* sink = nullptr;
                if (SUCCEEDED(scope_geo->Open(&sink))) {
                    float s0 = std::clamp(meter_waveform_[0], -1.0f, 1.0f);
                    float y0 = std::clamp(center_y - s0 * max_amp, screen_rc.top + 2.0f, screen_rc.bottom - 2.0f);
                    D2D1_POINT_2F p0 = D2D1::Point2F(start_x, y0);

                    sink->BeginFigure(p0, D2D1_FIGURE_BEGIN_HOLLOW);

                    for (size_t i = 1; i < num_pts; ++i) {
                        float s = std::clamp(meter_waveform_[i], -1.0f, 1.0f);
                        float y = std::clamp(center_y - s * max_amp, screen_rc.top + 2.0f, screen_rc.bottom - 2.0f);
                        D2D1_POINT_2F pt = D2D1::Point2F(start_x + static_cast<float>(i) * step_x, y);
                        sink->AddLine(pt);
                    }

                    sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    sink->Close();
                    sink->Release();

                    // Outer Phosphor Glow Bloom (Signature electric violet)
                    ID2D1SolidColorBrush* br_glow = nullptr;
                    d2d_target_->CreateSolidColorBrush(D2D1::ColorF(0.659f, 0.333f, 0.969f, 0.35f), &br_glow);
                    if (br_glow) {
                        d2d_target_->DrawGeometry(scope_geo, br_glow, 2.5f);
                        br_glow->Release();
                    }

                    // Inner Core Filament (Bright lavender filament)
                    ID2D1SolidColorBrush* br_core = nullptr;
                    d2d_target_->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.92f, 1.00f, 1.0f), &br_core);
                    if (br_core) {
                        d2d_target_->DrawGeometry(scope_geo, br_core, 1.5f);
                        br_core->Release();
                    }
                }
                if (scope_geo) scope_geo->Release();
            }
        }

        // Action Buttons: Icon-first [💾] and [💿]
        D2D1_RECT_F save_rc = D2D1::RectF(static_cast<float>(client_w_ - 80), 8.0f, static_cast<float>(client_w_ - 45), 40.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_bold_, save_rc, "💾", false, t.bg_control, t.bg_control, 3.5f);

        D2D1_RECT_F exp_rc = D2D1::RectF(static_cast<float>(client_w_ - 41), 8.0f, static_cast<float>(client_w_ - 10), 40.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_bold_, exp_rc, "💿", false, t.bg_control, t.bg_control, 3.5f);
    }

    void render_channel_rack_d2d() {
        const auto& t = D2DRenderer::theme();
        float eff_w = static_cast<float>(get_effective_workspace_w());
        D2D1_RECT_F rack_rc = D2D1::RectF(10.0f, 58.0f, eff_w - 10.0f, static_cast<float>(client_h_ - 268));
        D2DRenderer::draw_rounded_box(d2d_target_, rack_rc, t.bg_surface, t.border_subtle, 6.0f);

        // Header Title (Uppercase micro-label semibold per DESIGN.md section 1)
        D2D1_RECT_F title_rc = D2D1::RectF(25.0f, 66.0f, 105.0f, 90.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, "PLAYLIST",
                              title_rc, t.text_secondary);

        // Header [+ Add Instrument] button - always accessible
        D2D1_RECT_F add_tool_rc = D2D1::RectF(110.0f, 64.0f, 250.0f, 90.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, add_tool_rc, "+ Add Instrument", false, t.accent, t.bg_control, 3.5f);

        // Horizontal Bar Range Display
        auto ppq = engine_.session().project().time_map().ppq();
        auto bar_ticks = 4 * ppq;
        int bars_per_view = get_bars_per_view();
        int max_bars = get_max_sequencer_bars();

        int start_bar_num = sequencer_scroll_bar_ + 1;
        int end_bar_num = sequencer_scroll_bar_ + bars_per_view;
        std::string bar_lbl = "↔ Showing Bars " + std::to_string(start_bar_num) + "-" + std::to_string(end_bar_num) + " / " + std::to_string(max_bars);
        D2D1_RECT_F bar_num_rc = D2D1::RectF(eff_w - 260.0f, 64.0f, eff_w - 25.0f, 90.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, bar_num_rc, t.bg_control, t.border_subtle, 3.5f);
        D2DRenderer::draw_text(d2d_target_, dwrite_small_, bar_lbl, bar_num_rc, t.text_secondary,
                              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Timeline Ruler
        float ruler_x = 265.0f;
        float ruler_y = 96.0f;
        float ruler_w = static_cast<float>((eff_w - 25.0f) - 265.0f);
        float ruler_h = 22.0f;

        D2D1_RECT_F ruler_rc = D2D1::RectF(ruler_x, ruler_y, ruler_x + ruler_w, ruler_y + ruler_h);
        D2DRenderer::draw_rounded_box(d2d_target_, ruler_rc, t.bg_surface_2, t.border_subtle, 3.0f);

        float bar_w = ruler_w / static_cast<float>(bars_per_view);
        float beat_w = bar_w / 4.0f;

        ID2D1SolidColorBrush* br_border_faint = nullptr;
        ID2D1SolidColorBrush* br_border_dark = nullptr;
        ID2D1SolidColorBrush* br_border_light = nullptr;
        d2d_target_->CreateSolidColorBrush(t.border_subtle, &br_border_faint);
        d2d_target_->CreateSolidColorBrush(t.border_default, &br_border_dark);
        d2d_target_->CreateSolidColorBrush(t.border_strong, &br_border_light);

        for (int b = 0; b < bars_per_view; ++b) {
            float bx = ruler_x + b * bar_w;
            // Bar Number
            D2D1_RECT_F num_rc = D2D1::RectF(bx + 4.0f, ruler_y + 2.0f, bx + 36.0f, ruler_y + ruler_h - 2.0f);
            int cur_bar_idx = sequencer_scroll_bar_ + b + 1;
            D2DRenderer::draw_text(d2d_target_, dwrite_small_, std::to_string(cur_bar_idx), num_rc, t.text_muted);

            // Bar tick lines (half-pixel snapped for razor-sharp 1px line)
            if (br_border_dark && b > 0) {
                float snap_bx = std::floor(bx) + 0.5f;
                d2d_target_->DrawLine(D2D1::Point2F(snap_bx, ruler_y), D2D1::Point2F(snap_bx, ruler_y + ruler_h), br_border_dark, 1.0f);
            }
            if (br_border_faint) {
                for (int bt = 1; bt < 4; ++bt) {
                    float snap_btx = std::floor(bx + bt * beat_w) + 0.5f;
                    d2d_target_->DrawLine(D2D1::Point2F(snap_btx, ruler_y + ruler_h - 6.0f), D2D1::Point2F(snap_btx, ruler_y + ruler_h), br_border_faint, 1.0f);
                }
            }
        }

        // Tracks & Lanes
        float start_y = 124.0f;
        float row_h = 48.0f;
        float step_h = 42.0f;
        auto& channels = engine_.session().project().channels();
        auto& tracks = engine_.session().project().tracks();
        auto* pat = engine_.session().project().get_pattern(1);

        float rack_avail_h = (rack_rc.bottom - 26.0f) - start_y;
        int visible_channels = std::max(1, static_cast<int>(rack_avail_h / row_h));
        int max_ch_scroll = std::max(0, static_cast<int>(channels.size()) - visible_channels + 1);
        sequencer_scroll_track_ = std::clamp(sequencer_scroll_track_, 0, max_ch_scroll);

        domain::Tick view_start_tick = static_cast<domain::Tick>(sequencer_scroll_bar_) * bar_ticks;
        domain::Tick view_duration = static_cast<domain::Tick>(bars_per_view) * bar_ticks;
        domain::Tick view_end_tick = view_start_tick + view_duration;

        float max_track_bottom = start_y;

        // Clip channel tracks within rack area so scrolling never bleeds outside
        D2D1_RECT_F clip_rc = D2D1::RectF(12.0f, start_y, eff_w - 12.0f, rack_rc.bottom - 26.0f);
        d2d_target_->PushAxisAlignedClip(clip_rc, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        size_t start_ch = static_cast<size_t>(sequencer_scroll_track_);
        size_t end_ch = std::min(channels.size(), start_ch + static_cast<size_t>(visible_channels) + 1);

        for (size_t ch_idx = start_ch; ch_idx < end_ch; ++ch_idx) {
            const auto& ch = channels[ch_idx];
            float ch_y = start_y + static_cast<float>(ch_idx - start_ch) * row_h;
            max_track_bottom = ch_y + step_h;

            // Channel Left Controls
            // 1. Mute
            D2D1_RECT_F mute_rc = D2D1::RectF(25.0f, ch_y + 8.0f, 47.0f, ch_y + 34.0f);
            D2DRenderer::draw_button(d2d_target_, dwrite_small_, mute_rc, "M", ch.settings().muted, t.danger, t.bg_control, 3.0f);

            // 2. Solo
            D2D1_RECT_F solo_rc = D2D1::RectF(51.0f, ch_y + 8.0f, 73.0f, ch_y + 34.0f);
            D2DRenderer::draw_button(d2d_target_, dwrite_small_, solo_rc, "S", ch.settings().solo, t.accent, t.bg_control, 3.0f);

            // 3. Name & Settings Gear
            D2D1_RECT_F name_rc = D2D1::RectF(78.0f, ch_y + 5.0f, 218.0f, ch_y + 37.0f);
            std::string ch_label = ch.settings().name + " ⚙";
            D2DRenderer::draw_button(d2d_target_, dwrite_small_, name_rc, ch_label, false, t.bg_control, t.bg_control, 3.0f);

            // 4. Piano Roll Button
            D2D1_RECT_F roll_btn_rc = D2D1::RectF(223.0f, ch_y + 8.0f, 255.0f, ch_y + 34.0f);
            bool is_active_roll = (piano_roll_channel_ == ch.id());
            D2DRenderer::draw_button(d2d_target_, dwrite_small_, roll_btn_rc, "🎹", is_active_roll, t.accent, t.bg_control, 3.0f);

            // Track Arranger Lane (Alternating near-black dark surfaces)
            D2D1_RECT_F lane_rc = D2D1::RectF(ruler_x, ch_y, ruler_x + ruler_w, ch_y + step_h);
            D2D1_COLOR_F lane_bg = (ch_idx % 2 == 0) ? t.bg_surface : t.bg_app;
            D2DRenderer::draw_rounded_box(d2d_target_, lane_rc, lane_bg, t.border_subtle, 2.0f);

            // Beat grid lines across lane (half-pixel snapped for razor-sharp 1px line)
            for (int b = 0; b < bars_per_view; ++b) {
                float bx = ruler_x + b * bar_w;
                float snap_bx = std::floor(bx) + 0.5f;
                if (br_border_dark && b > 0) {
                    d2d_target_->DrawLine(D2D1::Point2F(snap_bx, ch_y), D2D1::Point2F(snap_bx, ch_y + step_h), br_border_dark, 1.0f);
                }
                if (br_border_faint) {
                    for (int bt = 1; bt < 4; ++bt) {
                        float snap_btx = std::floor(bx + bt * beat_w) + 0.5f;
                        d2d_target_->DrawLine(D2D1::Point2F(snap_btx, ch_y), D2D1::Point2F(snap_btx, ch_y + step_h), br_border_faint, 1.0f);
                    }
                }
            }

            // Render Track Clips (Creative / Track Colors as muted / translucent tints per DESIGN.md section 0)
            if (ch_idx < tracks.size()) {
                const auto& track = tracks[ch_idx];
                D2D1_COLOR_F track_accent = (ch_idx == 0 || ch_idx == 1) ? t.track_melody : ((ch_idx == 2) ? t.track_chords : t.track_drums);
                D2D1_COLOR_F clip_bg_col = D2D1::ColorF(0.065f + track_accent.r * 0.04f, 0.075f + track_accent.g * 0.04f, 0.09f + track_accent.b * 0.04f, 1.0f);
                D2D1_COLOR_F clip_hdr_col = D2D1::ColorF(0.085f + track_accent.r * 0.08f, 0.095f + track_accent.g * 0.08f, 0.11f + track_accent.b * 0.08f, 1.0f);
                D2D1_COLOR_F clip_border_col = D2D1::ColorF(0.14f + track_accent.r * 0.12f, 0.16f + track_accent.g * 0.12f, 0.20f + track_accent.b * 0.12f, 1.0f);

                for (const auto& clip : track.clips()) {
                    if (clip.end() <= view_start_tick || clip.start >= view_end_tick) continue;

                    domain::Tick draw_start = std::max(view_start_tick, clip.start);
                    domain::Tick draw_end = std::min(view_end_tick, clip.end());
                    double norm_start = double(draw_start - view_start_tick) / double(view_duration);
                    double norm_len = double(draw_end - draw_start) / double(view_duration);

                    float cx = ruler_x + static_cast<float>(norm_start * ruler_w);
                    float cw = std::max(16.0f, static_cast<float>(norm_len * ruler_w));

                    D2D1_RECT_F clip_rc = D2D1::RectF(cx, ch_y + 2.0f, cx + cw, ch_y + step_h - 2.0f);
                    D2DRenderer::draw_rounded_box(d2d_target_, clip_rc, clip_bg_col, clip_border_col, 3.5f, 1.0f);

                    // Top Header Strip
                    float hdr_h = 13.0f;
                    D2D1_RECT_F header_rc = D2D1::RectF(cx, ch_y + 2.0f, cx + cw, ch_y + 2.0f + hdr_h);
                    D2DRenderer::draw_rounded_box(d2d_target_, header_rc, clip_hdr_col, clip_hdr_col, 2.5f);

                    std::string pat_title = pat ? ("≡ " + pat->name()) : "≡ Pattern 1";
                    D2DRenderer::draw_text(d2d_target_, dwrite_small_, pat_title, header_rc, t.text_primary);

                    // Miniature Silver Melody Note Bars inside Clip Body
                    auto* note_set = pat ? pat->get_channel_notes(ch.id()) : nullptr;
                    if (note_set && !note_set->notes().empty()) {
                        float body_top = ch_y + 2.0f + hdr_h;
                        float body_h = step_h - 4.0f - hdr_h;

                        uint8_t min_p = 127, max_p = 0;
                        for (const auto& n : note_set->notes()) {
                            min_p = std::min(min_p, n.pitch);
                            max_p = std::max(max_p, n.pitch);
                        }
                        int p_range = std::max(1, (max_p > min_p) ? (max_p - min_p + 1) : 4);
                        domain::Tick pat_len = std::max(domain::Tick(4 * ppq), pat ? pat->length_ticks(ppq) : domain::Tick(4 * ppq));

                        for (domain::Tick rep = 0; rep < clip.length; rep += pat_len) {
                            for (const auto& n : note_set->notes()) {
                                if (rep + n.start >= clip.length) break;
                                domain::Tick abs_n_start = clip.start + rep + n.start;
                                domain::Tick abs_n_end = abs_n_start + n.length;

                                if (abs_n_end > draw_start && abs_n_start < draw_end) {
                                    double n_rel_start = double(abs_n_start - draw_start) / double(draw_end - draw_start);
                                    double n_rel_len = double(n.length) / double(draw_end - draw_start);

                                    float nx = cx + static_cast<float>(n_rel_start * cw);
                                    float nw = std::max(3.0f, static_cast<float>(n_rel_len * cw));

                                    float norm_p = float(n.pitch - min_p) / float(p_range);
                                    float ny = body_top + (1.0f - norm_p) * (body_h - 6.0f) + 1.0f;
                                    float nh = 3.0f;

                                    D2D1_RECT_F note_rc = D2D1::RectF(nx, ny, nx + nw, ny + nh);
                                    D2DRenderer::draw_rounded_box(d2d_target_, note_rc, t.note_silver, t.note_border, 1.0f);
                                }
                            }
                        }
                    }

                    // Right Resize Handle Grip (half-pixel snapped for crisp lines)
                    if (cw > 20.0f && br_border_light) {
                        float rx = std::floor(cx + cw - 4.0f) + 0.5f;
                        d2d_target_->DrawLine(D2D1::Point2F(rx - 2.0f, ch_y + 16.0f), D2D1::Point2F(rx - 2.0f, ch_y + step_h - 6.0f), br_border_light, 1.0f);
                        d2d_target_->DrawLine(D2D1::Point2F(rx, ch_y + 16.0f), D2D1::Point2F(rx, ch_y + step_h - 6.0f), br_border_light, 1.0f);
                    }
                }
            }
        }

        // Add Channel [+] Button in scrollable view
        if (channels.size() >= start_ch && channels.size() <= end_ch) {
            float add_y = start_y + static_cast<float>(channels.size() - start_ch) * row_h;
            D2D1_RECT_F add_btn_rc = D2D1::RectF(25.0f, add_y + 4.0f, 218.0f, add_y + 34.0f);
            D2DRenderer::draw_button(d2d_target_, dwrite_small_, add_btn_rc, "+ Add Instrument", false, t.bg_control, t.bg_control, 3.0f);
        }

        d2d_target_->PopAxisAlignedClip();

        // Vertical Scrollbar for channels if more channels than fit
        if (max_ch_scroll > 0) {
            float vbar_x = 258.0f;
            float vbar_y = start_y;
            float vbar_w = 4.0f;
            float vbar_h = rack_avail_h;
            D2D1_RECT_F v_track_rc = D2D1::RectF(vbar_x, vbar_y, vbar_x + vbar_w, vbar_y + vbar_h);
            D2DRenderer::draw_rounded_box(d2d_target_, v_track_rc, t.bg_control, t.border_subtle, 2.0f);

            float v_thumb_h = std::max(20.0f, (float(visible_channels) / float(channels.size() + 1)) * vbar_h);
            float v_scroll_ratio = float(sequencer_scroll_track_) / float(max_ch_scroll);
            float v_thumb_y = vbar_y + v_scroll_ratio * (vbar_h - v_thumb_h);
            D2D1_RECT_F v_thumb_rc = D2D1::RectF(vbar_x, v_thumb_y, vbar_x + vbar_w, v_thumb_y + v_thumb_h);
            D2DRenderer::draw_rounded_box(d2d_target_, v_thumb_rc, t.accent, t.accent_bright, 2.0f);
        }

        // Downward Electric Violet Playhead Marker (SPM / Playhead)
        auto cur_tick = engine_.transport().is_playing() ? engine_.transport().current_tick() : song_position_marker_;
        if (cur_tick >= view_start_tick && cur_tick <= view_end_tick) {
            double norm_pos = double(cur_tick - view_start_tick) / double(view_duration);
            float head_x = ruler_x + static_cast<float>(norm_pos * ruler_w);
            D2DRenderer::draw_playhead(d2d_target_, head_x, ruler_y, max_track_bottom, t.accent, "SPM", dwrite_small_);
        }

        // Sleek Horizontal Scrollbar Track & Thumb
        float scroll_track_y = rack_rc.bottom - 22.0f;
        float scroll_track_h = 14.0f;
        float scroll_track_x = ruler_x;
        float scroll_track_w = ruler_w;

        // Left Label for Scrollbar
        D2D1_RECT_F scroll_lbl_rc = D2D1::RectF(25.0f, scroll_track_y - 2.0f, ruler_x - 15.0f, scroll_track_y + scroll_track_h + 2.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_small_, "↔ TIMELINE SCROLL", scroll_lbl_rc, t.text_muted,
                              DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Scrollbar Track Container
        D2D1_RECT_F scroll_track_rc = D2D1::RectF(scroll_track_x, scroll_track_y, scroll_track_x + scroll_track_w, scroll_track_y + scroll_track_h);
        D2DRenderer::draw_rounded_box(d2d_target_, scroll_track_rc, t.bg_control, t.border_subtle, 3.0f);

        // Scrollbar Thumb
        float max_scroll = std::max(1.0f, float(max_bars - bars_per_view));
        float thumb_w = std::max(35.0f, (float(bars_per_view) / float(max_bars)) * scroll_track_w);
        float scroll_ratio = std::clamp(float(sequencer_scroll_bar_) / max_scroll, 0.0f, 1.0f);
        float thumb_x = scroll_track_x + scroll_ratio * (scroll_track_w - thumb_w);
        D2D1_RECT_F thumb_rc = D2D1::RectF(thumb_x, scroll_track_y + 1.0f, thumb_x + thumb_w, scroll_track_y + scroll_track_h - 1.0f);

        D2D1_COLOR_F thumb_col = dragging_seq_scrollbar_ ? t.accent_bright : t.border_strong;
        D2DRenderer::draw_rounded_box(d2d_target_, thumb_rc, thumb_col, t.border_default, 3.0f);

        // 3 Grip Notches on Thumb Center (half-pixel snapped for crisp lines)
        if (br_border_dark && thumb_w > 20.0f) {
            float mid_tx = std::floor(thumb_x + thumb_w * 0.5f) + 0.5f;
            d2d_target_->DrawLine(D2D1::Point2F(mid_tx - 4.0f, scroll_track_y + 3.0f),
                                  D2D1::Point2F(mid_tx - 4.0f, scroll_track_y + scroll_track_h - 3.0f), br_border_dark, 1.0f);
            d2d_target_->DrawLine(D2D1::Point2F(mid_tx, scroll_track_y + 3.0f),
                                  D2D1::Point2F(mid_tx, scroll_track_y + scroll_track_h - 3.0f), br_border_dark, 1.0f);
            d2d_target_->DrawLine(D2D1::Point2F(mid_tx + 4.0f, scroll_track_y + 3.0f),
                                  D2D1::Point2F(mid_tx + 4.0f, scroll_track_y + scroll_track_h - 3.0f), br_border_dark, 1.0f);
        }

        if (br_border_light) br_border_light->Release();
        if (br_border_faint) br_border_faint->Release();
        if (br_border_dark) br_border_dark->Release();
    }

    void render_piano_roll_d2d() {
        const auto& t = D2DRenderer::theme();
        PianoRollLayout lay;
        lay.init(get_effective_workspace_w(), client_h_, PianoRollNumPitches, piano_roll_steps_);

        D2D1_RECT_F roll_rc = D2D1::RectF(10.0f, lay.rack_top, static_cast<float>(get_effective_workspace_w() - 10), lay.rack_bottom);
        D2DRenderer::draw_rounded_box(d2d_target_, roll_rc, t.bg_surface, t.border_subtle, 6.0f);

        // 1. Toolbar: Section Title, Back to Playlist, Step Toggle, Note Length, Clear, and Shortcuts
        auto* ch = engine_.session().project().get_channel(piano_roll_channel_);
        std::string ch_name = ch ? ch->settings().name : "Instrument";

        // Section Title: "PIANO ROLL" (uppercase micro-label semibold per DESIGN.md section 1)
        D2D1_RECT_F title_rc = D2D1::RectF(25.0f, lay.toolbar_y, 125.0f, lay.toolbar_y + lay.toolbar_h);
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, "PIANO ROLL", title_rc, t.text_secondary,
                              DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Back to Playlist button
        D2D1_RECT_F back_rc = D2D1::RectF(135.0f, lay.toolbar_y, 255.0f, lay.toolbar_y + lay.toolbar_h);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, back_rc, "🎛 Back to Playlist", false, t.accent, t.bg_control, 3.5f);

        // Pitch Range Display Badge (Full 128 semitones C0..B10)
        int max_base_pitch = 128 - PianoRollNumPitches;
        std::string range_str = ch_name + "  |  " + get_midi_note_name(static_cast<uint8_t>(piano_roll_base_pitch_)) +
                                " — " + get_midi_note_name(static_cast<uint8_t>(piano_roll_base_pitch_ + PianoRollNumPitches - 1)) +
                                " (C0..B10)";
        D2D1_RECT_F range_rc = D2D1::RectF(263.0f, lay.toolbar_y, 475.0f, lay.toolbar_y + lay.toolbar_h);
        D2DRenderer::draw_rounded_box(d2d_target_, range_rc, t.bg_control, t.border_subtle, 3.5f);
        D2DRenderer::draw_text(d2d_target_, dwrite_small_, range_str, range_rc, t.text_secondary,
                              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Steps Toggle (16 or 32 visible)
        std::string step_str = (piano_roll_steps_ == 16) ? "16 Steps" : "32 Steps";
        D2D1_RECT_F step_rc = D2D1::RectF(483.0f, lay.toolbar_y, 565.0f, lay.toolbar_y + lay.toolbar_h);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, step_rc, step_str, (piano_roll_steps_ == 32), t.accent, t.bg_control, 3.5f);

        // Default Note Length
        std::string len_str = "📏 Len: " + std::to_string(piano_roll_note_len_steps_) + (piano_roll_note_len_steps_ == 1 ? " Stp" : " Stps");
        D2D1_RECT_F len_rc = D2D1::RectF(573.0f, lay.toolbar_y, 665.0f, lay.toolbar_y + lay.toolbar_h);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, len_rc, len_str, false, t.bg_control, t.bg_control, 3.5f);

        // Clear Notes button
        D2D1_RECT_F clear_rc = D2D1::RectF(673.0f, lay.toolbar_y, 735.0f, lay.toolbar_y + lay.toolbar_h);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, clear_rc, "Clear", false, t.danger, t.bg_control, 3.5f);

        // Keyboard & Mouse Controls Guide Hint
        D2D1_RECT_F hint_rc = D2D1::RectF(745.0f, lay.toolbar_y, static_cast<float>(get_effective_workspace_w() - 40), lay.toolbar_y + lay.toolbar_h);
        D2DRenderer::draw_text(d2d_target_, dwrite_small_,
                              "💡 Left: Add/Drag  •  Right: Delete  •  Right Edge: Resize  •  Wheel: Pitch/Scroll",
                              hint_rc, t.text_muted, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        ID2D1SolidColorBrush* br_border_faint = nullptr;
        ID2D1SolidColorBrush* br_border_dark = nullptr;
        ID2D1SolidColorBrush* br_border_strong = nullptr;
        d2d_target_->CreateSolidColorBrush(t.border_subtle, &br_border_faint);
        d2d_target_->CreateSolidColorBrush(t.border_default, &br_border_dark);
        d2d_target_->CreateSolidColorBrush(t.border_strong, &br_border_strong);

        auto* pat = engine_.session().project().get_pattern(1);
        auto ppq = engine_.session().project().time_map().ppq();
        auto step_ticks = ppq / 4;

        // 2. Ruler Header (Timeline Bar & Beat Indicators)
        for (int col = 0; col < piano_roll_steps_; ++col) {
            int abs_step = piano_roll_scroll_step_ + col;
            int bar = (abs_step / 16) + 1;
            int beat = ((abs_step % 16) / 4) + 1;
            int sub_step = (abs_step % 4) + 1;

            float rx = lay.grid_x + col * lay.step_w;
            D2D1_RECT_F r_cell = D2D1::RectF(rx, lay.ruler_y, rx + lay.step_w, lay.ruler_y + lay.ruler_h);

            bool is_bar_start = (sub_step == 1 && beat == 1);
            bool is_beat_start = (sub_step == 1);
            D2D1_COLOR_F r_bg = ((abs_step / 4) % 2 == 0) ? t.bg_surface_2 : t.bg_elevated;
            D2DRenderer::draw_rounded_box(d2d_target_, r_cell, r_bg, t.border_subtle, 1.0f);

            if (is_bar_start) {
                std::string lbl = std::to_string(bar) + "." + std::to_string(beat);
                D2DRenderer::draw_text(d2d_target_, dwrite_small_, lbl, r_cell, t.accent,
                                      DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            } else if (is_beat_start) {
                std::string lbl = std::to_string(beat);
                D2DRenderer::draw_text(d2d_target_, dwrite_small_, lbl, r_cell, t.text_secondary,
                                      DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
        }

        // 3. Render Pitch Rows & Piano Keys (Dark precision keys)
        for (int row = 0; row < PianoRollNumPitches; ++row) {
            int pitch_val = (piano_roll_base_pitch_ + PianoRollNumPitches - 1) - row;
            float ry = lay.grid_top + row * lay.row_h;
            bool is_black = is_midi_black_key(static_cast<uint8_t>(pitch_val));
            bool is_c_root = (pitch_val % 12 == 0);

            // A. Piano Key
            D2D1_RECT_F key_rc = D2D1::RectF(lay.piano_x, ry, lay.piano_x + lay.piano_w, ry + lay.row_h);
            D2D1_COLOR_F key_bg = is_black ? t.bg_surface : D2D1::ColorF(0.14f, 0.16f, 0.19f, 1.0f);
            D2D1_COLOR_F key_txt = is_c_root ? t.accent_bright : (is_black ? t.text_muted : t.text_secondary);
            D2D1_COLOR_F key_border = is_c_root ? t.accent : t.border_subtle;
            D2DRenderer::draw_rounded_box(d2d_target_, key_rc, key_bg, key_border, 2.0f);
            std::string note_name = get_midi_note_name(static_cast<uint8_t>(pitch_val));
            D2DRenderer::draw_text(d2d_target_, dwrite_small_, note_name, key_rc, key_txt,
                                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            // B. Grid Row Background
            D2D1_RECT_F r_row_rc = D2D1::RectF(lay.grid_x, ry, lay.grid_x + lay.grid_w, ry + lay.row_h);
            D2D1_COLOR_F row_bg = is_black ? t.bg_app : t.bg_surface_2;
            D2DRenderer::draw_rounded_box(d2d_target_, r_row_rc, row_bg, t.border_subtle, 0.0f);
        }

        // 4. Render Grid Columns (Steps) (half-pixel snapped for razor-sharp 1px line)
        for (int col = 0; col <= piano_roll_steps_; ++col) {
            float cx = lay.grid_x + col * lay.step_w;
            float snap_cx = std::floor(cx) + 0.5f;
            int abs_s = piano_roll_scroll_step_ + col;
            bool is_bar = (abs_s % 16 == 0);
            bool is_beat = (abs_s % 4 == 0);

            if (is_bar && br_border_strong) {
                d2d_target_->DrawLine(D2D1::Point2F(snap_cx, lay.grid_top), D2D1::Point2F(snap_cx, lay.grid_bottom), br_border_strong, 1.5f);
            } else if (is_beat && br_border_dark) {
                d2d_target_->DrawLine(D2D1::Point2F(snap_cx, lay.grid_top), D2D1::Point2F(snap_cx, lay.grid_bottom), br_border_dark, 1.0f);
            } else if (br_border_faint) {
                d2d_target_->DrawLine(D2D1::Point2F(snap_cx, lay.grid_top), D2D1::Point2F(snap_cx, lay.grid_bottom), br_border_faint, 0.5f);
            }
        }

        // 5. Render Active Notes (Electric violet accent with bright violet border)
        if (pat) {
            auto* note_set = pat->get_channel_notes(piano_roll_channel_);
            if (note_set) {
                int min_vis_p = piano_roll_base_pitch_;
                int max_vis_p = piano_roll_base_pitch_ + PianoRollNumPitches - 1;
                domain::Tick view_start_tick = piano_roll_scroll_step_ * step_ticks;
                domain::Tick view_end_tick = (piano_roll_scroll_step_ + piano_roll_steps_) * step_ticks;

                for (const auto& note : note_set->notes()) {
                    int p = note.pitch;
                    if (p < min_vis_p || p > max_vis_p) continue;
                    domain::Tick note_end = note.start + note.length;
                    if (note_end <= view_start_tick || note.start >= view_end_tick) continue;

                    int row = max_vis_p - p;
                    float ny = lay.grid_top + row * lay.row_h;

                    float rel_start = float(note.start - view_start_tick) / float(step_ticks);
                    float rel_len = float(note.length) / float(step_ticks);

                    float nx = lay.grid_x + rel_start * lay.step_w;
                    float nw = std::max(6.0f, rel_len * lay.step_w);

                    // Note body box
                    D2D1_RECT_F note_rc = D2D1::RectF(nx + 1.0f, ny + 1.0f, nx + nw - 1.0f, ny + lay.row_h - 1.0f);
                    bool is_dragged = (note_drag_mode_ != NoteDragMode::None &&
                                       drag_note_cur_start_ == note.start &&
                                       drag_note_cur_pitch_ == note.pitch);
                    D2D1_COLOR_F note_col = is_dragged ? t.accent_bright : t.accent;

                    D2DRenderer::draw_rounded_box(d2d_target_, note_rc, note_col, t.accent_bright, 3.0f);

                    // Note text label (crisp white text on purple note)
                    int dur_steps = std::max(1, static_cast<int>(note.length / step_ticks));
                    std::string n_lbl = get_midi_note_name(note.pitch);
                    if (dur_steps > 1) n_lbl += " (" + std::to_string(dur_steps) + ")";
                    D2DRenderer::draw_text(d2d_target_, dwrite_small_, n_lbl, note_rc, D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f));

                    // Right Edge Resize Handle Grip
                    if (nw > 14.0f) {
                        float hx = std::floor(nx + nw - 4.0f) + 0.5f;
                        ID2D1SolidColorBrush* br_handle = nullptr;
                        d2d_target_->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.5f), &br_handle);
                        if (br_handle) {
                            d2d_target_->DrawLine(D2D1::Point2F(hx - 2.0f, ny + 2.0f), D2D1::Point2F(hx - 2.0f, ny + lay.row_h - 2.0f), br_handle, 1.0f);
                            d2d_target_->DrawLine(D2D1::Point2F(hx, ny + 2.0f), D2D1::Point2F(hx, ny + lay.row_h - 2.0f), br_handle, 1.0f);
                            br_handle->Release();
                        }
                    }
                }
            }
        }

        // 6. Playhead Marker on Piano Roll (Electric Violet accent)
        auto cur_tick = engine_.transport().is_playing() ? engine_.transport().current_tick() : song_position_marker_;
        domain::Tick view_start_tick = piano_roll_scroll_step_ * step_ticks;
        domain::Tick view_end_tick = (piano_roll_scroll_step_ + piano_roll_steps_) * step_ticks;
        if (cur_tick >= view_start_tick && cur_tick <= view_end_tick) {
            float head_col = float(cur_tick - view_start_tick) / float(step_ticks);
            float head_x = lay.grid_x + head_col * lay.step_w;
            D2DRenderer::draw_playhead(d2d_target_, head_x, lay.ruler_y, lay.grid_bottom, t.accent, "SPM", dwrite_small_);
        }

        // 7. Vertical Scrollbar (Pitch Up / Down across full 128 Semitones)
        D2D1_RECT_F v_track_rc = D2D1::RectF(lay.v_scroll_x, lay.grid_top, lay.v_scroll_x + lay.v_scroll_w, lay.grid_bottom);
        D2DRenderer::draw_rounded_box(d2d_target_, v_track_rc, t.bg_control, t.border_subtle, 3.0f);

        float v_thumb_h = std::max(28.0f, (float(PianoRollNumPitches) / 128.0f) * lay.grid_h);
        float p_ratio = (max_base_pitch > 0) ? (float(max_base_pitch - piano_roll_base_pitch_) / float(max_base_pitch)) : 0.0f;
        float v_thumb_y = lay.grid_top + p_ratio * (lay.grid_h - v_thumb_h);
        D2D1_RECT_F v_thumb_rc = D2D1::RectF(lay.v_scroll_x + 1.0f, v_thumb_y, lay.v_scroll_x + lay.v_scroll_w - 1.0f, v_thumb_y + v_thumb_h);
        D2D1_COLOR_F v_thumb_col = dragging_piano_v_scrollbar_ ? t.accent_bright : t.border_strong;
        D2DRenderer::draw_rounded_box(d2d_target_, v_thumb_rc, v_thumb_col, t.border_subtle, 2.0f);

        // Notches on vertical thumb
        if (br_border_dark && v_thumb_h > 18.0f) {
            float mid_vy = std::floor(v_thumb_y + v_thumb_h * 0.5f) + 0.5f;
            d2d_target_->DrawLine(D2D1::Point2F(lay.v_scroll_x + 3.0f, mid_vy - 3.0f),
                                  D2D1::Point2F(lay.v_scroll_x + lay.v_scroll_w - 3.0f, mid_vy - 3.0f), br_border_dark, 1.0f);
            d2d_target_->DrawLine(D2D1::Point2F(lay.v_scroll_x + 3.0f, mid_vy),
                                  D2D1::Point2F(lay.v_scroll_x + lay.v_scroll_w - 3.0f, mid_vy), br_border_dark, 1.0f);
            d2d_target_->DrawLine(D2D1::Point2F(lay.v_scroll_x + 3.0f, mid_vy + 3.0f),
                                  D2D1::Point2F(lay.v_scroll_x + lay.v_scroll_w - 3.0f, mid_vy + 3.0f), br_border_dark, 1.0f);
        }

        // 8. Horizontal Scrollbar (Timeline Left / Right across all steps)
        int max_steps = get_max_piano_roll_steps();
        int max_scroll_step = std::max(0, max_steps - piano_roll_steps_);

        D2D1_RECT_F h_track_rc = D2D1::RectF(lay.grid_x, lay.h_scroll_y, lay.grid_x + lay.grid_w, lay.h_scroll_y + lay.h_scroll_h);
        D2DRenderer::draw_rounded_box(d2d_target_, h_track_rc, t.bg_control, t.border_subtle, 3.0f);

        float h_thumb_w = std::max(35.0f, (float(piano_roll_steps_) / float(max_steps)) * lay.grid_w);
        float s_ratio = (max_scroll_step > 0) ? (float(piano_roll_scroll_step_) / float(max_scroll_step)) : 0.0f;
        float h_thumb_x = lay.grid_x + s_ratio * (lay.grid_w - h_thumb_w);
        D2D1_RECT_F h_thumb_rc = D2D1::RectF(h_thumb_x, lay.h_scroll_y + 1.0f, h_thumb_x + h_thumb_w, lay.h_scroll_y + lay.h_scroll_h - 1.0f);
        D2D1_COLOR_F h_thumb_col = dragging_piano_h_scrollbar_ ? t.accent_bright : t.border_strong;
        D2DRenderer::draw_rounded_box(d2d_target_, h_thumb_rc, h_thumb_col, t.border_subtle, 2.0f);

        // Notches on horizontal thumb
        if (br_border_dark && h_thumb_w > 20.0f) {
            float mid_hx = std::floor(h_thumb_x + h_thumb_w * 0.5f) + 0.5f;
            d2d_target_->DrawLine(D2D1::Point2F(mid_hx - 3.0f, lay.h_scroll_y + 3.0f),
                                  D2D1::Point2F(mid_hx - 3.0f, lay.h_scroll_y + lay.h_scroll_h - 3.0f), br_border_dark, 1.0f);
            d2d_target_->DrawLine(D2D1::Point2F(mid_hx, lay.h_scroll_y + 3.0f),
                                  D2D1::Point2F(mid_hx, lay.h_scroll_y + lay.h_scroll_h - 3.0f), br_border_dark, 1.0f);
            d2d_target_->DrawLine(D2D1::Point2F(mid_hx + 3.0f, lay.h_scroll_y + 3.0f),
                                  D2D1::Point2F(mid_hx + 3.0f, lay.h_scroll_y + lay.h_scroll_h - 3.0f), br_border_dark, 1.0f);
        }

        if (br_border_faint) br_border_faint->Release();
        if (br_border_dark) br_border_dark->Release();
        if (br_border_strong) br_border_strong->Release();
    }

    void render_inspector_d2d() {
        if (!inspector_open_) return;
        const auto& t = D2DRenderer::theme();

        float insp_x = static_cast<float>(client_w_ - (kInspectorWidth + kLayoutMargin));
        float insp_y = 58.0f;
        float insp_r = static_cast<float>(client_w_ - kLayoutMargin);
        float insp_bot = static_cast<float>(client_h_ - 268);
        if (insp_bot <= insp_y + 100.0f) return;

        // Outer Panel Card
        D2D1_RECT_F panel_rc = D2D1::RectF(insp_x, insp_y, insp_r, insp_bot);
        D2DRenderer::draw_rounded_box(d2d_target_, panel_rc, t.bg_surface, t.border_subtle, 6.0f);

        // Header Title (Uppercase micro-label semibold per DESIGN.md section 1 & 3)
        D2D1_RECT_F hdr_title_rc = D2D1::RectF(insp_x + 12.0f, insp_y + 6.0f, insp_r - 36.0f, insp_y + 36.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, "INSPECTOR / TRACK FX", hdr_title_rc, t.text_secondary,
                              DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Close button [✕]
        D2D1_RECT_F close_btn_rc = D2D1::RectF(insp_r - 28.0f, insp_y + 9.0f, insp_r - 8.0f, insp_y + 29.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, close_btn_rc, "✕", false, t.bg_control, t.bg_control, 3.0f);

        // Header bottom divider (1px hairline)
        ID2D1SolidColorBrush* br_div = nullptr;
        d2d_target_->CreateSolidColorBrush(t.border_subtle, &br_div);
        if (br_div) {
            d2d_target_->DrawLine(D2D1::Point2F(insp_x, insp_y + 36.5f), D2D1::Point2F(insp_r, insp_y + 36.5f), br_div, 1.0f);
            br_div->Release();
        }

        // Track Information
        auto& proj = engine_.session().project();
        uint32_t tid = selected_mixer_track_;
        auto* track = proj.mixer_graph().get_track(tid);
        if (!track && tid > 0) {
            proj.mixer_graph().add_track(tid, "Track " + std::to_string(tid));
            track = proj.mixer_graph().get_track(tid);
        }

        std::string tr_name = track ? track->name() : (tid == 0 ? "Master" : ("Track " + std::to_string(tid)));
        std::string tr_badge = (tid == 0) ? "MASTER" : ("TRACK " + std::to_string(tid));

        // Find associated channel names
        std::string ch_source = "";
        for (const auto& ch : proj.channels()) {
            if (ch.settings().mixer_track == tid) {
                if (!ch_source.empty()) ch_source += ", ";
                ch_source += ch.settings().name;
            }
        }
        std::string route_str = (tid == 0) ? "Routing: Main Stereo Out"
            : (ch_source.empty() ? "Routing: Insert -> Master" : ("Input: " + ch_source + " -> Master"));

        // Summary Card Box
        D2D1_RECT_F sum_card_rc = D2D1::RectF(insp_x + 8.0f, insp_y + 44.0f, insp_r - 8.0f, insp_y + 162.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, sum_card_rc, t.bg_surface_2, t.border_subtle, 4.0f);

        // Track Badge & Name
        D2D1_RECT_F badge_rc = D2D1::RectF(insp_x + 14.0f, insp_y + 50.0f, insp_x + 80.0f, insp_y + 68.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, badge_rc, t.accent_deep, t.accent_bright, 3.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_small_, tr_badge, badge_rc, D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f),
                              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        D2D1_RECT_F name_rc = D2D1::RectF(insp_x + 86.0f, insp_y + 50.0f, insp_r - 14.0f, insp_y + 68.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, tr_name, name_rc, t.text_primary,
                              DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Routing Info
        D2D1_RECT_F route_rc = D2D1::RectF(insp_x + 14.0f, insp_y + 70.0f, insp_r - 14.0f, insp_y + 86.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_small_, route_str, route_rc, t.text_muted,
                              DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Volume Horizontal Slider
        float cur_vol = track ? track->volume() : 0.8f;
        float norm_vol = std::clamp(cur_vol / 1.25f, 0.0f, 1.0f);
        int vol_pct = static_cast<int>(std::round(cur_vol * 100.0f));
        std::string vol_lbl = "Vol: " + std::to_string(vol_pct) + "%";
        D2D1_RECT_F vol_rc = D2D1::RectF(insp_x + 14.0f, insp_y + 90.0f, insp_r - 14.0f, insp_y + 108.0f);
        D2DRenderer::draw_slider_horizontal(d2d_target_, dwrite_small_, vol_rc, norm_vol, vol_lbl);

        // Pan Horizontal Slider
        float cur_pan = track ? track->pan() : 0.0f;
        float norm_pan = (cur_pan + 1.0f) * 0.5f;
        std::string pan_lbl;
        if (std::abs(cur_pan) < 0.02f) {
            pan_lbl = "Pan: Center";
        } else if (cur_pan < 0.0f) {
            pan_lbl = "Pan: L " + std::to_string(static_cast<int>(std::round(-cur_pan * 100.0f))) + "%";
        } else {
            pan_lbl = "Pan: R " + std::to_string(static_cast<int>(std::round(cur_pan * 100.0f))) + "%";
        }
        D2D1_RECT_F pan_rc = D2D1::RectF(insp_x + 14.0f, insp_y + 112.0f, insp_r - 14.0f, insp_y + 130.0f);
        D2DRenderer::draw_slider_horizontal(d2d_target_, dwrite_small_, pan_rc, norm_pan, pan_lbl);

        // Mute & Solo Buttons
        bool is_mute = track ? track->muted() : false;
        bool is_solo = track ? track->solo() : false;
        float mid_w = (insp_r - 14.0f) - (insp_x + 14.0f);
        float btn_w = (mid_w - 6.0f) * 0.5f;

        D2D1_RECT_F mute_rc = D2D1::RectF(insp_x + 14.0f, insp_y + 134.0f, insp_x + 14.0f + btn_w, insp_y + 154.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, mute_rc, "MUTE", is_mute, t.danger, t.bg_control, 3.0f);

        D2D1_RECT_F solo_rc = D2D1::RectF(insp_x + 14.0f + btn_w + 6.0f, insp_y + 134.0f, insp_r - 14.0f, insp_y + 154.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, solo_rc, "SOLO", is_solo, t.warning, t.bg_control, 3.0f);

        // FX Inserts Section Header
        size_t num_fx = track ? track->inserts().size() : 0;
        std::string fx_sec_title = "CHANNEL FX INSERTS (" + std::to_string(num_fx) + "/10)";
        D2D1_RECT_F fx_title_rc = D2D1::RectF(insp_x + 12.0f, insp_y + 168.0f, insp_r - 12.0f, insp_y + 188.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, fx_sec_title, fx_title_rc, t.text_secondary,
                              DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Render Insert Slots (up to 10 slots per track)
        float slot_y_start = insp_y + 192.0f;
        float slot_h = 32.0f;
        float slot_gap = 4.0f;
        float fx_area_bot = insp_bot - 8.0f;
        float fx_avail_h = fx_area_bot - slot_y_start;
        int vis_fx_items = std::max(1, static_cast<int>(fx_avail_h / (slot_h + slot_gap)));
        int total_fx_items = static_cast<int>(num_fx) + (num_fx < 10 ? 1 : 0);
        int max_fx_scroll = std::max(0, total_fx_items - vis_fx_items);
        inspector_scroll_slot_ = std::clamp(inspector_scroll_slot_, 0, max_fx_scroll);

        // Clip FX region so slots never overflow the panel
        D2D1_RECT_F fx_clip_rc = D2D1::RectF(insp_x + 4.0f, slot_y_start, insp_r - 4.0f, fx_area_bot);
        d2d_target_->PushAxisAlignedClip(fx_clip_rc, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        float right_margin = (max_fx_scroll > 0) ? 14.0f : 8.0f;

        for (int i = inspector_scroll_slot_; i < total_fx_items; ++i) {
            float sy = slot_y_start + static_cast<float>(i - inspector_scroll_slot_) * (slot_h + slot_gap);
            if (sy >= fx_area_bot) break;

            if (i < static_cast<int>(num_fx)) {
                const auto& ins = track->inserts()[i];
                D2D1_RECT_F row_rc = D2D1::RectF(insp_x + 8.0f, sy, insp_r - right_margin, sy + slot_h);
                D2DRenderer::draw_rounded_box(d2d_target_, row_rc, t.bg_control, t.border_subtle, 3.5f);

                // Power Toggle Button [●] / [○]
                D2D1_RECT_F pwr_rc = D2D1::RectF(insp_x + 12.0f, sy + 4.0f, insp_x + 36.0f, sy + slot_h - 4.0f);
                D2DRenderer::draw_button(d2d_target_, dwrite_small_, pwr_rc, ins.enabled ? "●" : "○", ins.enabled, t.success, t.bg_app, 3.0f);

                // Effect Name
                std::string fx_name = ins.device ? ins.device->name() : "Effect";
                D2D1_RECT_F name_fx_rc = D2D1::RectF(insp_x + 40.0f, sy + 2.0f, insp_r - (right_margin + 84.0f), sy + slot_h - 2.0f);
                D2DRenderer::draw_text(d2d_target_, dwrite_small_, fx_name, name_fx_rc,
                                      ins.enabled ? t.text_primary : t.text_disabled,
                                      DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

                // Wet/Dry Mix mini slider
                D2D1_RECT_F mix_rc = D2D1::RectF(insp_r - (right_margin + 80.0f), sy + 5.0f, insp_r - (right_margin + 26.0f), sy + slot_h - 5.0f);
                std::string mix_lbl = std::to_string(static_cast<int>(std::round(ins.wet_mix * 100.0f))) + "%";
                D2DRenderer::draw_slider_horizontal(d2d_target_, dwrite_small_, mix_rc, ins.wet_mix, mix_lbl);

                // Remove Button [✕]
                D2D1_RECT_F rem_rc = D2D1::RectF(insp_r - (right_margin + 22.0f), sy + 5.0f, insp_r - (right_margin + 4.0f), sy + slot_h - 5.0f);
                D2DRenderer::draw_button(d2d_target_, dwrite_small_, rem_rc, "✕", false, t.danger, t.bg_app, 2.5f);
            } else {
                // "+ Insert Effect" Button
                D2D1_RECT_F add_fx_rc = D2D1::RectF(insp_x + 8.0f, sy + 2.0f, insp_r - right_margin, sy + 30.0f);
                D2DRenderer::draw_button(d2d_target_, dwrite_small_, add_fx_rc, "+ Insert Effect", false, t.accent, t.bg_surface_2, 3.5f);
            }
        }

        d2d_target_->PopAxisAlignedClip();

        // Render Slim Vertical Scrollbar on Right Edge if overflow occurs
        if (max_fx_scroll > 0 && total_fx_items > 0) {
            float sb_x = insp_r - 9.0f;
            float sb_w = 4.0f;
            float sb_h = fx_area_bot - slot_y_start;
            float thumb_h = std::max(20.0f, sb_h * (static_cast<float>(vis_fx_items) / static_cast<float>(total_fx_items)));
            float thumb_y = slot_y_start + (sb_h - thumb_h) * (static_cast<float>(inspector_scroll_slot_) / static_cast<float>(max_fx_scroll));

            // Track background
            D2D1_RECT_F track_rc = D2D1::RectF(sb_x, slot_y_start, sb_x + sb_w, fx_area_bot);
            D2DRenderer::draw_rounded_box(d2d_target_, track_rc, t.bg_control, t.border_subtle, 2.0f);

            // Thumb
            D2D1_RECT_F thumb_rc = D2D1::RectF(sb_x, thumb_y, sb_x + sb_w, thumb_y + thumb_h);
            D2D1_COLOR_F thumb_color = dragging_inspector_scrollbar_ ? t.accent_bright : t.text_muted;
            D2DRenderer::draw_rounded_box(d2d_target_, thumb_rc, thumb_color, thumb_color, 2.0f);
        }
    }

    void render_mixer_panel_d2d() {
        const auto& t = D2DRenderer::theme();
        D2D1_RECT_F mixer_rc = D2D1::RectF(10.0f, static_cast<float>(client_h_ - 262), static_cast<float>(client_w_ - 10), static_cast<float>(client_h_ - 30));
        D2DRenderer::draw_rounded_box(d2d_target_, mixer_rc, t.bg_surface, t.border_subtle, 6.0f);

        auto& proj = engine_.session().project();
        float track_w = 96.0f;
        float gap = 12.0f;
        float ty = static_cast<float>(client_h_ - 234);

        float insert_start_x = 25.0f + track_w + gap;
        float avail_w = (static_cast<float>(client_w_) - 25.0f) - insert_start_x;
        int vis_inserts = std::max(1, static_cast<int>(avail_w / (track_w + gap)));
        size_t total_inserts = std::max(size_t(4), proj.channels().size());
        int max_mix_scroll = std::max(0, static_cast<int>(total_inserts) - vis_inserts);
        mixer_scroll_track_ = std::clamp(mixer_scroll_track_, 0, max_mix_scroll);

        // Header Title (Uppercase micro-label semibold per DESIGN.md section 1)
        D2D1_RECT_F title_rc = D2D1::RectF(25.0f, static_cast<float>(client_h_ - 257), 120.0f, static_cast<float>(client_h_ - 237));
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, "MIXER",
                              title_rc, t.text_secondary);

        // Track Navigation Buttons & Range if tracks exceed window width
        if (max_mix_scroll > 0) {
            std::string mix_lbl = "Tracks " + std::to_string(1 + mixer_scroll_track_) + "-" +
                                  std::to_string(std::min(total_inserts, size_t(1 + mixer_scroll_track_ + vis_inserts - 1))) +
                                  " / " + std::to_string(total_inserts);
            D2D1_RECT_F mix_lbl_rc = D2D1::RectF(static_cast<float>(client_w_ - 240), static_cast<float>(client_h_ - 258),
                                                 static_cast<float>(client_w_ - 85), static_cast<float>(client_h_ - 238));
            D2DRenderer::draw_text(d2d_target_, dwrite_small_, mix_lbl, mix_lbl_rc, t.text_secondary,
                                  DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            D2D1_RECT_F btn_l = D2D1::RectF(static_cast<float>(client_w_ - 80), static_cast<float>(client_h_ - 259),
                                           static_cast<float>(client_w_ - 52), static_cast<float>(client_h_ - 237));
            D2D1_RECT_F btn_r = D2D1::RectF(static_cast<float>(client_w_ - 48), static_cast<float>(client_h_ - 259),
                                           static_cast<float>(client_w_ - 20), static_cast<float>(client_h_ - 237));
            D2DRenderer::draw_button(d2d_target_, dwrite_small_, btn_l, "◀", false, t.bg_control, t.bg_control, 3.0f);
            D2DRenderer::draw_button(d2d_target_, dwrite_small_, btn_r, "▶", false, t.bg_control, t.bg_control, 3.0f);
        }

        // Lambda to render a single mixer track strip
        auto render_strip = [&](int tid, float tx) {
            D2D1_RECT_F ch_box = D2D1::RectF(tx, ty, tx + track_w, ty + 198.0f);
            bool is_selected = (static_cast<uint32_t>(tid) == selected_mixer_track_);
            D2D1_COLOR_F border_c = is_selected ? t.accent_bright : t.border_subtle;
            D2DRenderer::draw_rounded_box(d2d_target_, ch_box, t.bg_surface_2, border_c, 4.0f, is_selected ? 1.5f : 1.0f);

            // Track Header Name (Uppercase micro-label per DESIGN.md section 1)
            std::string t_name = (tid == 0) ? "MASTER" : ("TRACK " + std::to_string(tid));
            D2D1_RECT_F hdr_rc = D2D1::RectF(tx, ty, tx + track_w, ty + 22.0f);
            D2D1_COLOR_F hdr_col = (tid == 0) ? t.accent : t.text_primary;
            D2DRenderer::draw_text(d2d_target_, dwrite_bold_, t_name, hdr_rc, hdr_col,
                                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            auto* mix_tr = proj.mixer_graph().get_track(tid);
            float vol = mix_tr ? mix_tr->volume() : 1.0f;
            float pan_val = mix_tr ? mix_tr->pan() : 0.0f;
            bool is_muted = mix_tr ? mix_tr->muted() : false;
            bool is_solo = mix_tr ? mix_tr->solo() : false;

            // Mute & Solo
            D2D1_RECT_F m_rc = D2D1::RectF(tx + 6.0f, ty + 24.0f, tx + 45.0f, ty + 42.0f);
            D2DRenderer::draw_button(d2d_target_, dwrite_small_, m_rc, "M", is_muted, t.danger, t.bg_control, 3.0f);

            D2D1_RECT_F s_rc = D2D1::RectF(tx + 51.0f, ty + 24.0f, tx + 90.0f, ty + 42.0f);
            D2DRenderer::draw_button(d2d_target_, dwrite_small_, s_rc, "S", is_solo, t.accent, t.bg_control, 3.0f);

            // Stereo VU Meter (Left & Right dual bars)
            float peak_l = (static_cast<size_t>(tid) < meter_peaks_l_.size()) ? meter_peaks_l_[tid] : 0.0f;
            float peak_r = (static_cast<size_t>(tid) < meter_peaks_r_.size()) ? meter_peaks_r_[tid] : 0.0f;
            D2D1_RECT_F meter_rc = D2D1::RectF(tx + 8.0f, ty + 46.0f, tx + 28.0f, ty + 144.0f);
            D2DRenderer::draw_vu_meter_stereo(d2d_target_, meter_rc, peak_l, peak_r);

            // Volume Fader & Decibel Readout
            float norm_gain = vol / 1.5f;
            float db = (vol > 0.0001f) ? (20.0f * std::log10(vol)) : -60.0f;
            std::stringstream ss_db;
            if (vol < 0.001f) ss_db << "-inf dB";
            else {
                if (db >= 0.0f) ss_db << "+";
                ss_db << std::fixed << std::setprecision(1) << db << " dB";
            }
            D2D1_RECT_F fader_rc = D2D1::RectF(tx + 33.0f, ty + 46.0f, tx + 88.0f, ty + 144.0f);
            D2DRenderer::draw_slider_vertical(d2d_target_, dwrite_main_, dwrite_small_, fader_rc, norm_gain, ss_db.str());

            // Rotary Panning Knob Underneath Fader & Meters
            D2D1_RECT_F pan_rc = D2D1::RectF(tx + 6.0f, ty + 148.0f, tx + track_w - 6.0f, ty + 194.0f);
            D2DRenderer::draw_pan_knob(d2d_target_, dwrite_small_, pan_rc, pan_val, "PAN");
        };

        // 1. Render Pinned Master Track (track 0)
        render_strip(0, 25.0f);

        // 2. Render Visible Insert Tracks
        for (int s = 0; s < vis_inserts; ++s) {
            int tid = 1 + mixer_scroll_track_ + s;
            if (static_cast<size_t>(tid) > total_inserts) break;
            float tx = insert_start_x + s * (track_w + gap);
            render_strip(tid, tx);
        }
    }

    void render_status_bar_d2d() {
        const auto& t = D2DRenderer::theme();
        D2D1_RECT_F status_rc = D2D1::RectF(0.0f, static_cast<float>(client_h_ - 25), static_cast<float>(client_w_), static_cast<float>(client_h_));
        D2DRenderer::draw_rounded_box(d2d_target_, status_rc, t.bg_surface_2, t.border_subtle, 0.0f);

        ID2D1SolidColorBrush* br_top = nullptr;
        d2d_target_->CreateSolidColorBrush(t.border_subtle, &br_top);
        if (br_top) {
            float sy = static_cast<float>(client_h_ - 25) + 0.5f;
            d2d_target_->DrawLine(D2D1::Point2F(0.0f, sy), D2D1::Point2F(static_cast<float>(client_w_), sy), br_top, 1.0f);
            br_top->Release();
        }

        std::string full_status = status_message_.empty() ? "Ready. [F6] Playlist  •  [F7] Piano Roll  •  [F8] Inspector  •  [Space] Play/Pause" : status_message_;
        D2D1_RECT_F txt_rc = D2D1::RectF(15.0f, static_cast<float>(client_h_ - 25), static_cast<float>(client_w_ - 15), static_cast<float>(client_h_));
        D2DRenderer::draw_text(d2d_target_, dwrite_small_, full_status, txt_rc, t.text_muted);
    }

    void render_plugin_editor_d2d() {
        if (active_editor_channel_ == 0) return;
        auto* dev = get_active_channel_synth();
        auto* synth = dynamic_cast<plugins::Synth3xOsc*>(dev);
        if (!synth) return;

        const auto& t = D2DRenderer::theme();

        float mw = 680.0f;
        float mh = 480.0f;
        float mx = (static_cast<float>(client_w_) - mw) * 0.5f;
        float my = (static_cast<float>(client_h_) - mh) * 0.5f;
        editor_bounds_ = {static_cast<LONG>(mx), static_cast<LONG>(my), static_cast<LONG>(mx + mw), static_cast<LONG>(my + mh)};

        // Modal Backdrop Box (Dark elevated surface with strong border per DESIGN.md)
        D2D1_RECT_F dlg_rc = D2D1::RectF(mx, my, mx + mw, my + mh);
        D2DRenderer::draw_rounded_box(d2d_target_, dlg_rc, t.bg_elevated, t.border_strong, 6.0f);

        // Header Bar
        D2D1_RECT_F hdr_rc = D2D1::RectF(mx, my, mx + mw, my + 38.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, hdr_rc, t.bg_surface, t.border_subtle, 6.0f);
        D2D1_RECT_F title_rc = D2D1::RectF(mx + 15.0f, my, mx + mw - 50.0f, my + 38.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, "3xOsc Synthesizer — Instrument Editor", title_rc, t.accent);

        // Close Button [✕]
        D2D1_RECT_F close_rc = D2D1::RectF(mx + mw - 38.0f, my + 6.0f, mx + mw - 8.0f, my + 32.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_bold_, close_rc, "✕", false, t.danger, t.bg_control, 3.5f);

        // Helper lambda for oscillator sections
        auto draw_osc_section_d2d = [&](const std::string& title, float y, float shape_norm, float vol_norm, int semi, bool has_semi) {
            D2D1_RECT_F sec_rc = D2D1::RectF(mx + 14.0f, y, mx + mw - 14.0f, y + 68.0f);
            D2DRenderer::draw_rounded_box(d2d_target_, sec_rc, t.bg_surface, t.border_subtle, 4.0f);

            D2D1_RECT_F label_rc = D2D1::RectF(mx + 20.0f, y + 4.0f, mx + 160.0f, y + 24.0f);
            D2DRenderer::draw_text(d2d_target_, dwrite_bold_, title, label_rc, t.text_secondary);

            int active_shape = static_cast<int>(shape_norm * 3.99f);
            const char* waves[4] = {"SINE", "SAW", "SQR", "NOISE"};

            for (int w = 0; w < 4; ++w) {
                D2D1_RECT_F wave_rc = D2D1::RectF(mx + 20.0f + w * 62.0f, y + 25.0f, mx + 20.0f + (w + 1) * 62.0f - 6.0f, y + 55.0f);
                D2DRenderer::draw_button(d2d_target_, dwrite_small_, wave_rc, waves[w], (w == active_shape), t.accent, t.bg_control, 3.0f);
            }

            if (has_semi) {
                D2D1_RECT_F semi_minus = D2D1::RectF(mx + 280.0f, y + 25.0f, mx + 310.0f, y + 55.0f);
                D2DRenderer::draw_button(d2d_target_, dwrite_bold_, semi_minus, "-", false, t.bg_control, t.bg_control, 3.0f);

                std::string semi_str = (semi >= 0 ? "+" : "") + std::to_string(semi) + " sem";
                D2D1_RECT_F semi_disp = D2D1::RectF(mx + 315.0f, y + 25.0f, mx + 395.0f, y + 55.0f);
                D2DRenderer::draw_rounded_box(d2d_target_, semi_disp, t.bg_control, t.border_subtle, 3.0f);
                D2DRenderer::draw_text(d2d_target_, dwrite_small_, semi_str, semi_disp, t.text_primary,
                                      DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

                D2D1_RECT_F semi_plus = D2D1::RectF(mx + 400.0f, y + 25.0f, mx + 430.0f, y + 55.0f);
                D2DRenderer::draw_button(d2d_target_, dwrite_bold_, semi_plus, "+", false, t.bg_control, t.bg_control, 3.0f);
            }

            std::string vol_str = "Vol: " + std::to_string(static_cast<int>(vol_norm * 100)) + "%";
            D2D1_RECT_F vol_rc = D2D1::RectF(mx + 450.0f, y + 25.0f, mx + mw - 20.0f, y + 55.0f);
            D2DRenderer::draw_slider_horizontal(d2d_target_, dwrite_small_, vol_rc, vol_norm, vol_str);
        };

        // --- OSCILLATOR 1 ---
        float osc1_y = my + 50.0f;
        draw_osc_section_d2d("OSCILLATOR 1", osc1_y, synth->get_parameter(1), synth->get_parameter(2), 0, false);

        // --- OSCILLATOR 2 ---
        float osc2_y = my + 130.0f;
        int osc2_semi = static_cast<int>((synth->get_parameter(5) - 0.5f) * 48.0f);
        draw_osc_section_d2d("OSCILLATOR 2", osc2_y, synth->get_parameter(3), synth->get_parameter(4), osc2_semi, true);

        // --- OSCILLATOR 3 ---
        float osc3_y = my + 210.0f;
        draw_osc_section_d2d("OSCILLATOR 3", osc3_y, 0.0f, 0.2f, 0, false);

        // --- FILTER & MASTER CONTROLS ---
        float filter_y = my + 290.0f;
        D2D1_RECT_F filt_sec_rc = D2D1::RectF(mx + 14.0f, filter_y, mx + mw - 14.0f, filter_y + 75.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, filt_sec_rc, t.bg_surface, t.border_subtle, 4.0f);

        D2D1_RECT_F filt_label = D2D1::RectF(mx + 20.0f, filter_y + 4.0f, mx + 250.0f, filter_y + 24.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, "LOWPASS FILTER & RESONANCE", filt_label, t.text_secondary);

        float cutoff_norm = synth->get_parameter(6);
        int cutoff_hz = static_cast<int>(20.0 + std::pow(cutoff_norm, 2.0) * 18000.0);
        std::string cutoff_label = "Cutoff: " + std::to_string(cutoff_hz) + " Hz";
        D2D1_RECT_F cutoff_rc = D2D1::RectF(mx + 20.0f, filter_y + 30.0f, mx + 220.0f, filter_y + 60.0f);
        D2DRenderer::draw_slider_horizontal(d2d_target_, dwrite_small_, cutoff_rc, cutoff_norm, cutoff_label);

        float q_norm = synth->get_parameter(7);
        std::stringstream ss_q;
        ss_q << "Reso Q: " << std::fixed << std::setprecision(1) << (0.5 + q_norm * 10.0);
        D2D1_RECT_F q_rc = D2D1::RectF(mx + 235.0f, filter_y + 30.0f, mx + 435.0f, filter_y + 60.0f);
        D2DRenderer::draw_slider_horizontal(d2d_target_, dwrite_small_, q_rc, q_norm, ss_q.str());

        // Master Volume
        float mvol = synth->get_parameter(0);
        std::string mvol_str = "Master: " + std::to_string(static_cast<int>(mvol * 100)) + "%";
        D2D1_RECT_F mvol_rc = D2D1::RectF(mx + 450.0f, filter_y + 30.0f, mx + mw - 20.0f, filter_y + 60.0f);
        D2DRenderer::draw_slider_horizontal(d2d_target_, dwrite_small_, mvol_rc, mvol, mvol_str);

        // Audition Button
        D2D1_RECT_F test_note_rc = D2D1::RectF(mx + 20.0f, my + 380.0f, mx + 220.0f, my + 420.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_bold_, test_note_rc, "♪ AUDITION NOTE (C4)", false, t.accent, t.bg_control, 3.5f);

        D2D1_RECT_F hint_rc = D2D1::RectF(mx + 240.0f, my + 380.0f, mx + mw - 20.0f, my + 420.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_small_, "Adjust waveforms, detune, or filter sliders to reshape timbre in real time.",
                              hint_rc, t.text_muted, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
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

        // 2b. Render Right-Side Collapsible Inspector / Effects Panel
        render_inspector_gdi();

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
        RECT bar_rc{0, 0, client_w_, 48};
        GuiRenderer::fill_rect(mem_dc_, bar_rc, t.bg_surface);

        HPEN bPen = CreatePen(PS_SOLID, 1, t.border_subtle);
        HGDIOBJ oldPen = SelectObject(mem_dc_, bPen);
        MoveToEx(mem_dc_, 0, 47, NULL);
        LineTo(mem_dc_, client_w_, 47);
        SelectObject(mem_dc_, oldPen);
        DeleteObject(bPen);

        // App Logo
        SelectObject(mem_dc_, font_title_);
        RECT title_rc{16, 0, 84, 48};
        GuiRenderer::draw_text(mem_dc_, "DigiDAW", title_rc, t.text_primary, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Project Name badge
        SelectObject(mem_dc_, font_bold_);
        std::string proj_name = engine_.session().project().name();
        if (proj_name.empty()) proj_name = "Untitled Project";
        RECT proj_rc{88, 8, 198, 40};
        GuiRenderer::draw_rounded_box(mem_dc_, proj_rc, t.bg_control, t.border_subtle, 3);
        GuiRenderer::draw_text(mem_dc_, proj_name, proj_rc, t.accent_bright, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        // Undo & Redo buttons
        SelectObject(mem_dc_, font_bold_);
        bool can_undo = engine_.session().undo_stack().can_undo();
        bool can_redo = engine_.session().undo_stack().can_redo();
        RECT undo_rc{204, 8, 234, 40};
        GuiRenderer::draw_rounded_box(mem_dc_, undo_rc, t.bg_control, can_undo ? t.border_default : t.border_subtle, 3);
        GuiRenderer::draw_text(mem_dc_, "↶", undo_rc, can_undo ? t.text_primary : t.text_disabled, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT redo_rc{238, 8, 268, 40};
        GuiRenderer::draw_rounded_box(mem_dc_, redo_rc, t.bg_control, can_redo ? t.border_default : t.border_subtle, 3);
        GuiRenderer::draw_text(mem_dc_, "↷", redo_rc, can_redo ? t.text_primary : t.text_disabled, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Transport Buttons: PLAY, PAUSE, STOP (Icon-only)
        bool is_playing = engine_.transport().is_playing();
        bool is_paused = (engine_.transport().state() == app::TransportState::Paused);

        // 1. PLAY Button [▶]
        RECT play_rc{276, 8, 312, 40};
        GuiRenderer::draw_button(mem_dc_, play_rc, "▶", is_playing, t.accent, t.bg_control);

        // 2. PAUSE Button [❚❚]
        RECT pause_rc{316, 8, 352, 40};
        GuiRenderer::draw_button(mem_dc_, pause_rc, "❚❚", is_paused, t.warning, t.bg_control);

        // 3. STOP Button [■]
        RECT stop_rc{356, 8, 392, 40};
        GuiRenderer::draw_button(mem_dc_, stop_rc, "■", false, t.danger, t.bg_control);

        // Tempo BPM Controls
        double bpm = engine_.session().project().time_map().get_bpm_at(0);
        std::stringstream ss_bpm;
        ss_bpm << std::fixed << std::setprecision(1) << bpm << " BPM";

        RECT bpm_minus_rc{400, 8, 424, 40};
        GuiRenderer::draw_button(mem_dc_, bpm_minus_rc, "-", false, t.bg_control, t.bg_control);

        RECT bpm_disp_rc{428, 8, 508, 40};
        GuiRenderer::draw_rounded_box(mem_dc_, bpm_disp_rc, t.bg_control, t.border_subtle, 3);
        GuiRenderer::draw_text(mem_dc_, ss_bpm.str(), bpm_disp_rc, t.text_primary, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT bpm_plus_rc{512, 8, 536, 40};
        GuiRenderer::draw_button(mem_dc_, bpm_plus_rc, "+", false, t.bg_control, t.bg_control);

        // Real-Time Position Clock starting from 00:00.00
        auto tick = engine_.transport().current_tick();
        auto ppq = engine_.session().project().time_map().ppq();
        double tempo = engine_.session().project().time_map().get_bpm_at(tick);
        double beats = double(tick) / double(ppq);
        double seconds_total = (tempo > 0.0) ? (beats * 60.0 / tempo) : 0.0;
        int mins = static_cast<int>(seconds_total) / 60;
        int secs = static_cast<int>(seconds_total) % 60;
        int centis = static_cast<int>((seconds_total - std::floor(seconds_total)) * 100.0);
        int bar = static_cast<int>(tick / (ppq * 4)) + 1;

        std::stringstream ss_pos;
        ss_pos << std::setfill('0') << std::setw(2) << mins << ":"
               << std::setfill('0') << std::setw(2) << secs << "."
               << std::setfill('0') << std::setw(2) << centis
               << " | Bar " << bar;

        RECT pos_rc{544, 8, 676, 40};
        GuiRenderer::draw_rounded_box(mem_dc_, pos_rc, t.bg_control, t.border_subtle, 3);
        GuiRenderer::draw_text(mem_dc_, ss_pos.str(), pos_rc, t.accent_bright, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // View Mode Tabs: Icon-only [🎛], [🎹], and [ℹ]
        RECT rack_tab_rc{684, 8, 722, 40};
        GuiRenderer::draw_button(mem_dc_, rack_tab_rc, "🎛", (view_mode_ == ViewMode::ChannelRack),
                                 t.accent, t.bg_control);

        RECT roll_tab_rc{726, 8, 764, 40};
        GuiRenderer::draw_button(mem_dc_, roll_tab_rc, "🎹", (view_mode_ == ViewMode::PianoRoll),
                                 t.accent, t.bg_control);

        RECT insp_tab_rc{768, 8, 806, 40};
        GuiRenderer::draw_button(mem_dc_, insp_tab_rc, "ℹ", inspector_open_,
                                 t.accent, t.bg_control);

        // Analog Real-Time Audio Signal Oscilloscope Section (Electric violet phosphor filament)
        int spec_x = 816;
        int spec_max_right = client_w_ - 88;
        if (spec_max_right > spec_x + 50) {
            int spec_w = std::min(240, spec_max_right - spec_x);
            int spec_y = 8;
            int spec_h = 32;
            RECT chassis_rc{spec_x, spec_y, spec_x + spec_w, spec_y + spec_h};
            GuiRenderer::draw_rounded_box(mem_dc_, chassis_rc, t.bg_control, t.border_subtle, 3);

            RECT screen_rc{spec_x + 2, spec_y + 2, spec_x + spec_w - 2, spec_y + spec_h - 2};
            GuiRenderer::fill_rect(mem_dc_, screen_rc, t.bg_app);

            int center_y = (screen_rc.top + screen_rc.bottom) / 2;
            int max_amp = (screen_rc.bottom - screen_rc.top) / 2 - 2;

            int disp_w = (screen_rc.right - 2) - (screen_rc.left + 2);
            constexpr size_t num_pts = 128;
            float step_x = float(disp_w) / float(num_pts - 1);

            POINT pts[num_pts];
            for (size_t i = 0; i < num_pts; ++i) {
                float px = float(screen_rc.left + 2) + float(i) * step_x;
                float s = std::clamp(meter_waveform_[i], -1.0f, 1.0f);
                int py = center_y - static_cast<int>(s * float(max_amp));
                pts[i].x = static_cast<int>(px);
                pts[i].y = py;
            }

            // Real-Time Oscilloscope Line (Electric Violet accent)
            HPEN pen_wave = CreatePen(PS_SOLID, 2, RGB(168, 85, 247));
            HPEN old_pen = (HPEN)SelectObject(mem_dc_, pen_wave);
            Polyline(mem_dc_, pts, num_pts);

            SelectObject(mem_dc_, old_pen);
            DeleteObject(pen_wave);
        }

        // Action Buttons: Icon-only [💾] and [💿]
        SelectObject(mem_dc_, font_main_);
        RECT save_rc{client_w_ - 80, 8, client_w_ - 45, 40};
        GuiRenderer::draw_button(mem_dc_, save_rc, "💾", false, t.bg_control, t.bg_control);

        RECT rend_rc{client_w_ - 41, 8, client_w_ - 10, 40};
        GuiRenderer::draw_button(mem_dc_, rend_rc, "💿", false, t.accent, t.bg_control);
    }

    void render_channel_rack() {
        const auto& t = get_theme();
        int rack_top = 58;
        int rack_bottom = client_h_ - 268;
        int eff_w = get_effective_workspace_w();
        RECT rack_rc{10, rack_top, eff_w - 10, rack_bottom};

        GuiRenderer::draw_rounded_box(mem_dc_, rack_rc, t.bg_surface, t.border_subtle, 6);

        auto& proj = engine_.session().project();
        auto* pat = proj.get_pattern(1);
        auto ppq = proj.time_map().ppq();
        auto bar_ticks = 4 * ppq;

        // Ensure tracks exist for all channels
        while (proj.tracks().size() < proj.channels().size()) {
            domain::TrackId tid = static_cast<domain::TrackId>(proj.tracks().size() + 1);
            proj.tracks().emplace_back(tid, "Track " + std::to_string(tid));
        }

        // Layout: Continuous Playlist Arranger
        int bars_per_view = get_bars_per_view();
        int start_x = 265;
        int total_seq_w = (eff_w - 25) - start_x;
        int bar_w = std::max(60, total_seq_w / bars_per_view);
        float beat_w = float(bar_w) / 4.0f;

        // Header Title (Uppercase micro-label per DESIGN.md section 1)
        SelectObject(mem_dc_, font_bold_);
        RECT header_rc{25, 66, 105, 90};
        GuiRenderer::draw_text(mem_dc_, "PLAYLIST", header_rc, t.text_secondary);

        // Header [+ Add Instrument] button - always accessible
        SelectObject(mem_dc_, font_small_);
        RECT add_tool_rc{110, 64, 250, 90};
        GuiRenderer::draw_button(mem_dc_, add_tool_rc, "+ Add Instrument", false, t.accent, t.bg_control);

        // Horizontal Bar Range Display
        int max_bars = get_max_sequencer_bars();
        SelectObject(mem_dc_, font_small_);
        RECT disp_b_rc{eff_w - 260, 64, eff_w - 25, 90};
        std::string b_range_str = "↔ Showing Bars " + std::to_string(sequencer_scroll_bar_ + 1) + "-" +
                                  std::to_string(sequencer_scroll_bar_ + bars_per_view) + " / " + std::to_string(max_bars);
        GuiRenderer::draw_rounded_box(mem_dc_, disp_b_rc, t.bg_control, t.border_subtle, 3);
        GuiRenderer::draw_text(mem_dc_, b_range_str, disp_b_rc, t.text_secondary, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // --- Timeline Ruler ---
        int ruler_y = 96;
        int ruler_h = 22;
        RECT ruler_bg_rc{start_x, ruler_y, start_x + total_seq_w, ruler_y + ruler_h};
        GuiRenderer::draw_rounded_box(mem_dc_, ruler_bg_rc, t.bg_surface_2, t.border_subtle, 3);

        domain::Tick view_start_tick = sequencer_scroll_bar_ * bar_ticks;
        domain::Tick view_duration = bars_per_view * bar_ticks;
        domain::Tick view_end_tick = view_start_tick + view_duration;

        for (int b = 0; b < bars_per_view; ++b) {
            int abs_bar = sequencer_scroll_bar_ + b;
            int bx = start_x + b * bar_w;

            // Bar Vertical Boundary Line
            if (b > 0) {
                HPEN bPen = CreatePen(PS_SOLID, 1, t.border_default);
                HGDIOBJ oldP = SelectObject(mem_dc_, bPen);
                MoveToEx(mem_dc_, bx, ruler_y, NULL);
                LineTo(mem_dc_, bx, ruler_y + ruler_h);
                SelectObject(mem_dc_, oldP);
                DeleteObject(bPen);
            }

            // Bar Number Label
            SelectObject(mem_dc_, font_small_);
            RECT num_rc{bx + 4, ruler_y + 2, bx + 36, ruler_y + ruler_h - 2};
            GuiRenderer::draw_text(mem_dc_, std::to_string(abs_bar + 1), num_rc, t.text_muted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // Minor Beat Ticks within Bar (4 beats per bar)
            HPEN tickPen = CreatePen(PS_SOLID, 1, t.border_subtle);
            HGDIOBJ oldP = SelectObject(mem_dc_, tickPen);
            for (int bt = 1; bt < 4; ++bt) {
                int tx = bx + static_cast<int>(bt * beat_w);
                MoveToEx(mem_dc_, tx, ruler_y + ruler_h - 6, NULL);
                LineTo(mem_dc_, tx, ruler_y + ruler_h);
            }
            SelectObject(mem_dc_, oldP);
            DeleteObject(tickPen);
        }

        // --- Channels List & Continuous Playlist Lanes ---
        int start_y = 124;
        int row_h = 48;
        int step_h_i = 42;
        int rack_avail_h = (rack_bottom - 26) - start_y;
        int visible_channels = std::max(1, rack_avail_h / row_h);
        int max_ch_scroll = std::max(0, static_cast<int>(proj.channels().size()) - visible_channels + 1);
        sequencer_scroll_track_ = std::clamp(sequencer_scroll_track_, 0, max_ch_scroll);

        size_t start_ch = static_cast<size_t>(sequencer_scroll_track_);
        size_t end_ch = std::min(proj.channels().size(), start_ch + static_cast<size_t>(visible_channels) + 1);

        int max_track_bottom = start_y;

        for (size_t ch_idx = start_ch; ch_idx < end_ch; ++ch_idx) {
            auto& ch = proj.channels()[ch_idx];
            auto& track = proj.tracks()[ch_idx];
            auto* note_set = pat ? pat->get_channel_notes(ch.id()) : nullptr;
            int ch_y = start_y + static_cast<int>((ch_idx - start_ch) * row_h);
            max_track_bottom = ch_y + step_h_i;

            // Mute Button [M]
            RECT mute_rc{25, ch_y + 8, 47, ch_y + 34};
            GuiRenderer::draw_button(mem_dc_, mute_rc, "M", ch.settings().muted, t.danger, t.bg_control);

            // Solo Button [S]
            RECT solo_rc{51, ch_y + 8, 73, ch_y + 34};
            GuiRenderer::draw_button(mem_dc_, solo_rc, "S", ch.settings().solo, t.accent, t.bg_control);

            // Channel / VST Button
            RECT name_rc{78, ch_y + 5, 218, ch_y + 37};
            bool is_editing = (active_editor_channel_ == ch.id());
            GuiRenderer::draw_button(mem_dc_, name_rc, ch.settings().name + " ⚙", is_editing, t.accent, t.bg_control);

            // Dedicated Piano Roll button for this channel
            RECT roll_btn_rc{223, ch_y + 8, 255, ch_y + 34};
            bool is_active_roll = (piano_roll_channel_ == ch.id());
            GuiRenderer::draw_button(mem_dc_, roll_btn_rc, "🎹", is_active_roll, t.accent, t.bg_control);

            // Playlist Grid Lane Background (Alternating near-black dark surfaces)
            RECT lane_rc{start_x, ch_y, start_x + total_seq_w, ch_y + step_h_i};
            COLORREF lane_bg = (ch_idx % 2 == 0) ? t.bg_app : t.bg_surface_2;
            GuiRenderer::fill_rect(mem_dc_, lane_rc, lane_bg);

            // Beat Grid Lines across entire track lane
            for (int b = 0; b < bars_per_view; ++b) {
                int bx = start_x + b * bar_w;
                // Bar boundary
                HPEN barPen = CreatePen(PS_SOLID, 1, t.border_default);
                HGDIOBJ oldP = SelectObject(mem_dc_, barPen);
                MoveToEx(mem_dc_, bx, ch_y, NULL);
                LineTo(mem_dc_, bx, ch_y + step_h_i);

                // Beat subdivisions
                HPEN beatPen = CreatePen(PS_SOLID, 1, t.border_subtle);
                SelectObject(mem_dc_, beatPen);
                for (int bt = 1; bt < 4; ++bt) {
                    int tx = bx + static_cast<int>(bt * beat_w);
                    MoveToEx(mem_dc_, tx, ch_y, NULL);
                    LineTo(mem_dc_, tx, ch_y + step_h_i);
                }
                SelectObject(mem_dc_, oldP);
                DeleteObject(beatPen);
                DeleteObject(barPen);
            }

            // Bottom Lane Border
            HPEN divPen = CreatePen(PS_SOLID, 1, t.border_subtle);
            HGDIOBJ oldDiv = SelectObject(mem_dc_, divPen);
            MoveToEx(mem_dc_, start_x, ch_y + step_h_i, NULL);
            LineTo(mem_dc_, start_x + total_seq_w, ch_y + step_h_i);
            SelectObject(mem_dc_, oldDiv);
            DeleteObject(divPen);

            // Creative track tint
            COLORREF clip_tint = (ch_idx % 3 == 0) ? t.track_melody : ((ch_idx % 3 == 1) ? t.track_chords : t.track_drums);

            // --- Render Clips for this Track ---
            for (const auto& clip : track.clips()) {
                if (clip.end() <= view_start_tick || clip.start >= view_end_tick) continue;

                double norm_start = double(clip.start - view_start_tick) / double(view_duration);
                double norm_len = double(clip.length) / double(view_duration);
                int cx = start_x + static_cast<int>(norm_start * total_seq_w);
                int cw = std::max(16, static_cast<int>(norm_len * total_seq_w));

                RECT clip_rc{cx, ch_y + 1, cx + cw, ch_y + step_h_i - 1};
                GuiRenderer::draw_rounded_box(mem_dc_, clip_rc, t.bg_elevated, t.border_default, 3);

                RECT clip_hdr_rc{cx + 1, ch_y + 2, cx + cw - 1, ch_y + 17};
                GuiRenderer::fill_rect(mem_dc_, clip_hdr_rc, clip_tint);

                SelectObject(mem_dc_, font_small_);
                std::string clip_title = " ≡ Pattern " + std::to_string(clip.pattern_id);
                if (pat && !pat->name().empty()) {
                    clip_title = " ≡ " + pat->name();
                }
                GuiRenderer::draw_text(mem_dc_, clip_title, clip_hdr_rc, RGB(255, 255, 255), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                // Render Miniature Melody Note Bars
                if (note_set && !note_set->notes().empty()) {
                    uint8_t min_p = 127, max_p = 0;
                    for (const auto& n : note_set->notes()) {
                        min_p = std::min(min_p, n.pitch);
                        max_p = std::max(max_p, n.pitch);
                    }
                    int p_range = std::max(1, (max_p > min_p) ? (max_p - min_p + 1) : 4);
                    domain::Tick pat_len = std::max(domain::Tick(4 * ppq), pat ? pat->length_ticks(ppq) : domain::Tick(4 * ppq));

                    for (domain::Tick rep = 0; rep < clip.length; rep += pat_len) {
                        for (const auto& n : note_set->notes()) {
                            if (rep + n.start >= clip.length) break;
                            domain::Tick abs_n_start = rep + n.start;
                            double n_rel_start = double(abs_n_start) / double(clip.length);
                            double n_rel_len = double(n.length) / double(clip.length);

                            int nx = cx + static_cast<int>(n_rel_start * cw) + 1;
                            int nw = std::max(6, static_cast<int>(n_rel_len * cw) - 1);
                            if (nx + nw > cx + cw - 2) nw = (cx + cw - 2) - nx;
                            if (nw < 3) nw = 3;

                            float norm_p = float(n.pitch - min_p) / float(p_range);
                            int ny = (ch_y + step_h_i - 7) - static_cast<int>(norm_p * (step_h_i - 28));
                            int nh = 5;

                            RECT note_bar_rc{nx, ny, nx + nw, ny + nh};
                            GuiRenderer::draw_rounded_box(mem_dc_, note_bar_rc, RGB(212, 222, 232), RGB(245, 250, 255), 1);
                        }
                    }
                }

                // Right Resize Edge Grip Handle
                HPEN gripPen = CreatePen(PS_SOLID, 1, RGB(100, 110, 125));
                HGDIOBJ oldGrip = SelectObject(mem_dc_, gripPen);
                MoveToEx(mem_dc_, cx + cw - 4, ch_y + 19, NULL);
                LineTo(mem_dc_, cx + cw - 4, ch_y + step_h_i - 3);
                SelectObject(mem_dc_, oldGrip);
                DeleteObject(gripPen);
            }
        }

        // Add Channel [+] Button in scrollable view
        if (proj.channels().size() >= start_ch && proj.channels().size() <= end_ch) {
            int add_y = start_y + static_cast<int>((proj.channels().size() - start_ch) * row_h);
            RECT add_btn_rc{25, add_y + 4, 218, add_y + 34};
            GuiRenderer::draw_button(mem_dc_, add_btn_rc, "+ Add Instrument", false, t.accent, t.bg_control);
        }

        // Vertical Scrollbar for channels
        if (max_ch_scroll > 0) {
            RECT v_track_rc{258, start_y, 262, start_y + rack_avail_h};
            GuiRenderer::fill_rect(mem_dc_, v_track_rc, t.bg_control);
            int v_thumb_h = std::max(20, static_cast<int>((float(visible_channels) / float(proj.channels().size() + 1)) * float(rack_avail_h)));
            int v_thumb_y = start_y + static_cast<int>((float(sequencer_scroll_track_) / float(max_ch_scroll)) * float(rack_avail_h - v_thumb_h));
            RECT v_thumb_rc{258, v_thumb_y, 262, v_thumb_y + v_thumb_h};
            GuiRenderer::fill_rect(mem_dc_, v_thumb_rc, t.border_strong);
        }

        // --- Render Playhead Indicator (Electric Violet signature accent) ---
        int seq_bottom = max_track_bottom + 2;
        COLORREF playhead_violet = t.accent;

        if (!engine_.transport().is_playing()) {
            if (song_position_marker_ >= view_start_tick && song_position_marker_ < view_end_tick) {
                int spm_x = start_x + static_cast<int>((double(song_position_marker_ - view_start_tick) / view_duration) * total_seq_w);
                GuiRenderer::draw_playhead(mem_dc_, spm_x, ruler_y, seq_bottom, playhead_violet);
            }
        } else {
            if (song_position_marker_ >= view_start_tick && song_position_marker_ < view_end_tick) {
                int spm_x = start_x + static_cast<int>((double(song_position_marker_ - view_start_tick) / view_duration) * total_seq_w);
                GuiRenderer::draw_playhead(mem_dc_, spm_x, ruler_y, seq_bottom, t.accent_bright, "SPM");
            }

            auto tick = engine_.transport().current_tick();
            if (tick >= view_start_tick && tick < view_end_tick) {
                int playhead_x = start_x + static_cast<int>((double(tick - view_start_tick) / view_duration) * total_seq_w);
                GuiRenderer::draw_playhead(mem_dc_, playhead_x, ruler_y, seq_bottom, playhead_violet);
            }
        }

        // 5. Horizontal Scrollbar Track & Thumb (GDI fallback)
        int scroll_y = rack_bottom - 22;
        int scroll_h = 16;
        RECT scroll_track_rc{start_x, scroll_y, start_x + total_seq_w, scroll_y + scroll_h};
        GuiRenderer::draw_rounded_box(mem_dc_, scroll_track_rc, t.bg_control, t.border_subtle, 3);

        RECT scroll_lbl_rc{25, scroll_y - 2, start_x - 15, scroll_y + scroll_h + 2};
        SelectObject(mem_dc_, font_small_);
        GuiRenderer::draw_text(mem_dc_, "↔ TIMELINE SCROLL", scroll_lbl_rc, t.text_muted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        float max_scroll = std::max(1.0f, float(max_bars - bars_per_view));
        float thumb_w = std::max(35.0f, (float(bars_per_view) / float(max_bars)) * float(total_seq_w));
        float scroll_ratio = std::clamp(float(sequencer_scroll_bar_) / max_scroll, 0.0f, 1.0f);
        float thumb_x = float(start_x) + scroll_ratio * (float(total_seq_w) - thumb_w);
        RECT thumb_rc{static_cast<int>(thumb_x), scroll_y + 1, static_cast<int>(thumb_x + thumb_w), scroll_y + scroll_h - 1};
        COLORREF thumb_col = dragging_seq_scrollbar_ ? t.accent_bright : t.border_strong;
        GuiRenderer::draw_rounded_box(mem_dc_, thumb_rc, thumb_col, t.border_subtle, 3);
    }

    // --- Interactive Piano Roll View (GDI Fallback) ---
    void render_piano_roll() {
        const auto& t = get_theme();
        PianoRollLayout lay;
        lay.init(get_effective_workspace_w(), client_h_, PianoRollNumPitches, piano_roll_steps_);

        RECT roll_rc{10, static_cast<int>(lay.rack_top), get_effective_workspace_w() - 10, static_cast<int>(lay.rack_bottom)};
        GuiRenderer::draw_rounded_box(mem_dc_, roll_rc, t.bg_surface, t.border_subtle, 6);

        auto& proj = engine_.session().project();
        auto* pat = proj.get_pattern(1);
        auto ppq = proj.time_map().ppq();
        auto step_ticks = ppq / 4;

        auto* cur_ch = proj.get_channel(piano_roll_channel_);
        std::string ch_name = cur_ch ? cur_ch->settings().name : "Instrument";

        // 1. Piano Roll Toolbar: Section Title, Back to Playlist, Step Toggle, Note Length, Clear, and Shortcuts
        SelectObject(mem_dc_, font_bold_);
        RECT title_rc{25, static_cast<int>(lay.toolbar_y), 125, static_cast<int>(lay.toolbar_y + lay.toolbar_h)};
        GuiRenderer::draw_text(mem_dc_, "PIANO ROLL", title_rc, t.text_secondary, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(mem_dc_, font_small_);
        RECT back_rc{135, static_cast<int>(lay.toolbar_y), 255, static_cast<int>(lay.toolbar_y + lay.toolbar_h)};
        GuiRenderer::draw_button(mem_dc_, back_rc, "🎛 Back to Playlist", false, t.accent, t.bg_control);

        // Pitch Range Display Badge (Full 128 semitones C0..B10)
        int max_base_pitch = 128 - PianoRollNumPitches;
        std::string range_str = ch_name + "  |  " + get_midi_note_name(static_cast<uint8_t>(piano_roll_base_pitch_)) +
                                " — " + get_midi_note_name(static_cast<uint8_t>(piano_roll_base_pitch_ + PianoRollNumPitches - 1)) +
                                " (C0..B10)";
        RECT range_rc{263, static_cast<int>(lay.toolbar_y), 475, static_cast<int>(lay.toolbar_y + lay.toolbar_h)};
        GuiRenderer::draw_rounded_box(mem_dc_, range_rc, t.bg_control, t.border_subtle, 3);
        GuiRenderer::draw_text(mem_dc_, range_str, range_rc, t.text_secondary, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Steps Toggle (16 or 32 visible)
        std::string step_str = (piano_roll_steps_ == 16) ? "16 Steps" : "32 Steps";
        RECT step_rc{483, static_cast<int>(lay.toolbar_y), 565, static_cast<int>(lay.toolbar_y + lay.toolbar_h)};
        GuiRenderer::draw_button(mem_dc_, step_rc, step_str, (piano_roll_steps_ == 32), t.accent, t.bg_control);

        // Default Note Length
        std::string len_str = "📏 Len: " + std::to_string(piano_roll_note_len_steps_) + (piano_roll_note_len_steps_ == 1 ? " Stp" : " Stps");
        RECT len_rc{573, static_cast<int>(lay.toolbar_y), 665, static_cast<int>(lay.toolbar_y + lay.toolbar_h)};
        GuiRenderer::draw_button(mem_dc_, len_rc, len_str, false, t.bg_control, t.bg_control);

        // Clear Notes button
        RECT clear_rc{673, static_cast<int>(lay.toolbar_y), 735, static_cast<int>(lay.toolbar_y + lay.toolbar_h)};
        GuiRenderer::draw_button(mem_dc_, clear_rc, "Clear", false, t.danger, t.bg_control);

        // Guide Hint
        SelectObject(mem_dc_, font_small_);
        RECT hint_rc{745, static_cast<int>(lay.toolbar_y), get_effective_workspace_w() - 40, static_cast<int>(lay.toolbar_y + lay.toolbar_h)};
        GuiRenderer::draw_text(mem_dc_, "💡 Left: Add/Drag  •  Right: Delete  •  Right Edge: Resize  •  Wheel: Pitch/Scroll",
                              hint_rc, t.text_muted, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        // 2. Ruler Header
        for (int col = 0; col < piano_roll_steps_; ++col) {
            int abs_step = piano_roll_scroll_step_ + col;
            int bar = (abs_step / 16) + 1;
            int beat = ((abs_step % 16) / 4) + 1;
            int sub_step = (abs_step % 4) + 1;

            int rx = static_cast<int>(lay.grid_x + col * lay.step_w);
            RECT r_cell{rx, static_cast<int>(lay.ruler_y), static_cast<int>(rx + lay.step_w), static_cast<int>(lay.ruler_y + lay.ruler_h)};

            bool is_bar_start = (sub_step == 1 && beat == 1);
            bool is_beat_start = (sub_step == 1);
            COLORREF r_bg = ((abs_step / 4) % 2 == 0) ? t.bg_surface_2 : t.bg_elevated;
            GuiRenderer::draw_rounded_box(mem_dc_, r_cell, r_bg, t.border_subtle, 1);

            if (is_bar_start) {
                std::string lbl = std::to_string(bar) + "." + std::to_string(beat);
                GuiRenderer::draw_text(mem_dc_, lbl, r_cell, t.accent, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            } else if (is_beat_start) {
                std::string lbl = std::to_string(beat);
                GuiRenderer::draw_text(mem_dc_, lbl, r_cell, t.text_secondary, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }

        // 3. Piano Keys & Grid Rows
        for (int r = 0; r < PianoRollNumPitches; ++r) {
            uint8_t pitch = static_cast<uint8_t>(piano_roll_base_pitch_ + (PianoRollNumPitches - 1 - r));
            int ry = static_cast<int>(lay.grid_top + r * lay.row_h);
            bool is_black = is_midi_black_key(pitch);
            bool is_c_root = (pitch % 12 == 0);

            // A. Piano Key
            RECT key_rc{static_cast<int>(lay.piano_x), ry, static_cast<int>(lay.piano_x + lay.piano_w), static_cast<int>(ry + lay.row_h)};
            COLORREF key_bg = is_black ? t.bg_surface : RGB(36, 41, 49);
            COLORREF key_border = is_c_root ? t.accent : t.border_subtle;
            GuiRenderer::draw_rounded_box(mem_dc_, key_rc, key_bg, key_border, 2);

            COLORREF key_txt = is_c_root ? t.accent_bright : (is_black ? t.text_muted : t.text_secondary);
            GuiRenderer::draw_text(mem_dc_, get_midi_note_name(pitch), key_rc, key_txt, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            // B. Grid Row Background
            RECT row_grid_rc{static_cast<int>(lay.grid_x), ry, static_cast<int>(lay.grid_x + lay.grid_w), static_cast<int>(ry + lay.row_h)};
            COLORREF grid_row_bg = is_black ? t.bg_app : t.bg_surface_2;
            GuiRenderer::fill_rect(mem_dc_, row_grid_rc, grid_row_bg);

            // Column Lines
            for (int col = 0; col < piano_roll_steps_; ++col) {
                int cx = static_cast<int>(lay.grid_x + col * lay.step_w);
                int abs_s = piano_roll_scroll_step_ + col;
                COLORREF grid_border = (abs_s % 16 == 0) ? t.border_strong : ((abs_s % 4 == 0) ? t.border_default : t.border_subtle);
                RECT cell_rc{cx, ry, static_cast<int>(cx + lay.step_w), static_cast<int>(ry + lay.row_h)};
                GuiRenderer::draw_border(mem_dc_, cell_rc, grid_border);
            }
        }

        // 4. Draw Active Notes
        if (pat) {
            auto* note_set = pat->get_channel_notes(piano_roll_channel_);
            if (note_set) {
                int min_vis_p = piano_roll_base_pitch_;
                int max_vis_p = piano_roll_base_pitch_ + PianoRollNumPitches - 1;
                domain::Tick view_start_tick = piano_roll_scroll_step_ * step_ticks;
                domain::Tick view_end_tick = (piano_roll_scroll_step_ + piano_roll_steps_) * step_ticks;

                for (const auto& n : note_set->notes()) {
                    if (n.pitch < min_vis_p || n.pitch > max_vis_p) continue;
                    domain::Tick note_end = n.start + n.length;
                    if (note_end <= view_start_tick || n.start >= view_end_tick) continue;

                    int r = max_vis_p - n.pitch;
                    int ny = static_cast<int>(lay.grid_top + r * lay.row_h + 1);

                    float rel_start = float(n.start - view_start_tick) / float(step_ticks);
                    float rel_len = float(n.length) / float(step_ticks);
                    int nx = static_cast<int>(lay.grid_x + rel_start * lay.step_w + 1);
                    int nw = std::max(6, static_cast<int>(rel_len * lay.step_w - 2));

                    RECT note_rc{nx, ny, nx + nw, static_cast<int>(ny + lay.row_h - 2)};
                    bool is_dragged = (note_drag_mode_ != NoteDragMode::None &&
                                       drag_note_cur_start_ == n.start &&
                                       drag_note_cur_pitch_ == n.pitch);
                    COLORREF fill_col = is_dragged ? t.accent_bright : t.accent;
                    COLORREF border_col = t.accent_bright;

                    GuiRenderer::draw_rounded_box(mem_dc_, note_rc, fill_col, border_col, 3);

                    // Resize Grip Handle on right edge of note
                    int handle_w = std::min(10, nw / 3);
                    if (handle_w >= 4) {
                        RECT handle_rc{nx + nw - handle_w, ny + 1, nx + nw - 1, static_cast<int>(ny + lay.row_h - 3)};
                        GuiRenderer::fill_rect(mem_dc_, handle_rc, t.accent_deep);
                    }

                    // Note label inside note block (white text)
                    if (nw >= 24) {
                        SelectObject(mem_dc_, font_small_);
                        std::string n_txt = get_midi_note_name(n.pitch);
                        int dur_steps = std::max(1, static_cast<int>(n.length / step_ticks));
                        if (dur_steps > 1) n_txt += " (" + std::to_string(dur_steps) + ")";
                        RECT lbl_rc{nx + 2, ny, nx + nw - handle_w, static_cast<int>(ny + lay.row_h - 2)};
                        GuiRenderer::draw_text(mem_dc_, n_txt, lbl_rc, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    }
                }
            }
        }

        // 5. Playhead & SPM (Electric Violet accent)
        auto cur_tick = engine_.transport().is_playing() ? engine_.transport().current_tick() : song_position_marker_;
        domain::Tick view_start_tick = piano_roll_scroll_step_ * step_ticks;
        domain::Tick view_end_tick = (piano_roll_scroll_step_ + piano_roll_steps_) * step_ticks;
        if (cur_tick >= view_start_tick && cur_tick <= view_end_tick) {
            float head_col = float(cur_tick - view_start_tick) / float(step_ticks);
            int head_x = static_cast<int>(lay.grid_x + head_col * lay.step_w);
            GuiRenderer::draw_playhead(mem_dc_, head_x, static_cast<int>(lay.ruler_y), static_cast<int>(lay.grid_bottom), t.accent, "SPM");
        }

        // 6. Vertical Scrollbar (Pitch Up / Down)
        RECT v_track_rc{static_cast<int>(lay.v_scroll_x), static_cast<int>(lay.grid_top),
                        static_cast<int>(lay.v_scroll_x + lay.v_scroll_w), static_cast<int>(lay.grid_bottom)};
        GuiRenderer::draw_rounded_box(mem_dc_, v_track_rc, t.bg_control, t.border_subtle, 3);

        float v_thumb_h = std::max(28.0f, (float(PianoRollNumPitches) / 128.0f) * lay.grid_h);
        float p_ratio = (max_base_pitch > 0) ? (float(max_base_pitch - piano_roll_base_pitch_) / float(max_base_pitch)) : 0.0f;
        float v_thumb_y = lay.grid_top + p_ratio * (lay.grid_h - v_thumb_h);
        RECT v_thumb_rc{static_cast<int>(lay.v_scroll_x + 1), static_cast<int>(v_thumb_y),
                        static_cast<int>(lay.v_scroll_x + lay.v_scroll_w - 1), static_cast<int>(v_thumb_y + v_thumb_h)};
        COLORREF v_thumb_col = dragging_piano_v_scrollbar_ ? t.accent_bright : t.border_strong;
        GuiRenderer::draw_rounded_box(mem_dc_, v_thumb_rc, v_thumb_col, t.border_subtle, 2);

        // 7. Horizontal Scrollbar (Timeline Left / Right)
        int max_steps = get_max_piano_roll_steps();
        int max_scroll_step = std::max(0, max_steps - piano_roll_steps_);
        RECT h_track_rc{static_cast<int>(lay.grid_x), static_cast<int>(lay.h_scroll_y),
                        static_cast<int>(lay.grid_x + lay.grid_w), static_cast<int>(lay.h_scroll_y + lay.h_scroll_h)};
        GuiRenderer::draw_rounded_box(mem_dc_, h_track_rc, t.bg_control, t.border_subtle, 3);

        float h_thumb_w = std::max(35.0f, (float(piano_roll_steps_) / float(max_steps)) * lay.grid_w);
        float s_ratio = (max_scroll_step > 0) ? (float(piano_roll_scroll_step_) / float(max_scroll_step)) : 0.0f;
        float h_thumb_x = lay.grid_x + s_ratio * (lay.grid_w - h_thumb_w);
        RECT h_thumb_rc{static_cast<int>(h_thumb_x), static_cast<int>(lay.h_scroll_y + 1),
                        static_cast<int>(h_thumb_x + h_thumb_w), static_cast<int>(lay.h_scroll_y + lay.h_scroll_h - 1)};
        COLORREF h_thumb_col = dragging_piano_h_scrollbar_ ? t.accent_bright : t.border_strong;
        GuiRenderer::draw_rounded_box(mem_dc_, h_thumb_rc, h_thumb_col, t.border_subtle, 2);
    }

    void render_inspector_gdi() {
        if (!inspector_open_) return;
        const auto& t = get_theme();

        int insp_x = client_w_ - (kInspectorWidth + kLayoutMargin);
        int insp_y = 58;
        int insp_r = client_w_ - kLayoutMargin;
        int insp_bot = client_h_ - 268;
        if (insp_bot <= insp_y + 100) return;

        // Outer Panel Card
        RECT panel_rc{insp_x, insp_y, insp_r, insp_bot};
        GuiRenderer::draw_rounded_box(mem_dc_, panel_rc, t.bg_surface, t.border_subtle, 6);

        // Header Title
        SelectObject(mem_dc_, font_bold_);
        RECT hdr_title_rc{insp_x + 12, insp_y + 6, insp_r - 36, insp_y + 36};
        GuiRenderer::draw_text(mem_dc_, "INSPECTOR / TRACK FX", hdr_title_rc, t.text_secondary);

        // Close button [✕]
        SelectObject(mem_dc_, font_small_);
        RECT close_btn_rc{insp_r - 28, insp_y + 9, insp_r - 8, insp_y + 29};
        GuiRenderer::draw_button(mem_dc_, close_btn_rc, "✕", false, t.bg_control, t.bg_control);

        // Divider
        HPEN divPen = CreatePen(PS_SOLID, 1, t.border_subtle);
        HGDIOBJ oldP = SelectObject(mem_dc_, divPen);
        MoveToEx(mem_dc_, insp_x, insp_y + 36, NULL);
        LineTo(mem_dc_, insp_r, insp_y + 36);
        SelectObject(mem_dc_, oldP);
        DeleteObject(divPen);

        // Track Information
        auto& proj = engine_.session().project();
        uint32_t tid = selected_mixer_track_;
        auto* track = proj.mixer_graph().get_track(tid);
        if (!track && tid > 0) {
            proj.mixer_graph().add_track(tid, "Track " + std::to_string(tid));
            track = proj.mixer_graph().get_track(tid);
        }

        std::string tr_name = track ? track->name() : (tid == 0 ? "Master" : ("Track " + std::to_string(tid)));
        std::string tr_badge = (tid == 0) ? "MASTER" : ("TRACK " + std::to_string(tid));

        std::string ch_source = "";
        for (const auto& ch : proj.channels()) {
            if (ch.settings().mixer_track == tid) {
                if (!ch_source.empty()) ch_source += ", ";
                ch_source += ch.settings().name;
            }
        }
        std::string route_str = (tid == 0) ? "Routing: Main Stereo Out"
            : (ch_source.empty() ? "Routing: Insert -> Master" : ("Input: " + ch_source + " -> Master"));

        // Summary Card Box
        RECT sum_card_rc{insp_x + 8, insp_y + 44, insp_r - 8, insp_y + 162};
        GuiRenderer::draw_rounded_box(mem_dc_, sum_card_rc, t.bg_surface_2, t.border_subtle, 4);

        // Track Badge & Name
        RECT badge_rc{insp_x + 14, insp_y + 50, insp_x + 80, insp_y + 68};
        GuiRenderer::draw_rounded_box(mem_dc_, badge_rc, t.accent_deep, t.accent_bright, 3);
        GuiRenderer::draw_text(mem_dc_, tr_badge, badge_rc, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SelectObject(mem_dc_, font_bold_);
        RECT name_rc{insp_x + 86, insp_y + 50, insp_r - 14, insp_y + 68};
        GuiRenderer::draw_text(mem_dc_, tr_name, name_rc, t.text_primary);

        // Routing Info
        SelectObject(mem_dc_, font_small_);
        RECT route_rc{insp_x + 14, insp_y + 70, insp_r - 14, insp_y + 86};
        GuiRenderer::draw_text(mem_dc_, route_str, route_rc, t.text_muted);

        // Volume Slider
        float cur_vol = track ? track->volume() : 0.8f;
        float norm_vol = std::clamp(cur_vol / 1.25f, 0.0f, 1.0f);
        std::string vol_lbl = "Vol: " + std::to_string(static_cast<int>(std::round(cur_vol * 100.0f))) + "%";
        RECT vol_rc{insp_x + 14, insp_y + 90, insp_r - 14, insp_y + 108};
        GuiRenderer::draw_slider_horizontal(mem_dc_, vol_rc, norm_vol, vol_lbl);

        // Pan Slider
        float cur_pan = track ? track->pan() : 0.0f;
        float norm_pan = (cur_pan + 1.0f) * 0.5f;
        std::string pan_lbl;
        if (std::abs(cur_pan) < 0.02f) {
            pan_lbl = "Pan: Center";
        } else if (cur_pan < 0.0f) {
            pan_lbl = "Pan: L " + std::to_string(static_cast<int>(std::round(-cur_pan * 100.0f))) + "%";
        } else {
            pan_lbl = "Pan: R " + std::to_string(static_cast<int>(std::round(cur_pan * 100.0f))) + "%";
        }
        RECT pan_rc{insp_x + 14, insp_y + 112, insp_r - 14, insp_y + 130};
        GuiRenderer::draw_slider_horizontal(mem_dc_, pan_rc, norm_pan, pan_lbl);

        // Mute & Solo Buttons
        bool is_mute = track ? track->muted() : false;
        bool is_solo = track ? track->solo() : false;
        int mid_w = (insp_r - 14) - (insp_x + 14);
        int btn_w = (mid_w - 6) / 2;

        RECT mute_rc{insp_x + 14, insp_y + 134, insp_x + 14 + btn_w, insp_y + 154};
        GuiRenderer::draw_button(mem_dc_, mute_rc, "MUTE", is_mute, t.danger, t.bg_control);

        RECT solo_rc{insp_x + 14 + btn_w + 6, insp_y + 134, insp_r - 14, insp_y + 154};
        GuiRenderer::draw_button(mem_dc_, solo_rc, "SOLO", is_solo, t.warning, t.bg_control);

        // FX Inserts Section Header
        size_t num_fx = track ? track->inserts().size() : 0;
        std::string fx_sec_title = "CHANNEL FX INSERTS (" + std::to_string(num_fx) + "/10)";
        SelectObject(mem_dc_, font_bold_);
        RECT fx_title_rc{insp_x + 12, insp_y + 168, insp_r - 12, insp_y + 188};
        GuiRenderer::draw_text(mem_dc_, fx_sec_title, fx_title_rc, t.text_secondary);

        // Render Insert Slots
        int slot_y_start = insp_y + 192;
        int slot_h = 32;
        int slot_gap = 4;
        int fx_area_bot = insp_bot - 8;
        int fx_avail_h = fx_area_bot - slot_y_start;
        int vis_fx_items = std::max(1, fx_avail_h / (slot_h + slot_gap));
        int total_fx_items = static_cast<int>(num_fx) + (num_fx < 10 ? 1 : 0);
        int max_fx_scroll = std::max(0, total_fx_items - vis_fx_items);
        inspector_scroll_slot_ = std::clamp(inspector_scroll_slot_, 0, max_fx_scroll);

        // GDI Region clipping
        HRGN fx_clip = CreateRectRgn(insp_x + 4, slot_y_start, insp_r - 4, fx_area_bot);
        SelectClipRgn(mem_dc_, fx_clip);

        int right_margin = (max_fx_scroll > 0) ? 14 : 8;

        SelectObject(mem_dc_, font_small_);
        for (int i = inspector_scroll_slot_; i < total_fx_items; ++i) {
            int sy = slot_y_start + (i - inspector_scroll_slot_) * (slot_h + slot_gap);
            if (sy >= fx_area_bot) break;

            if (i < static_cast<int>(num_fx)) {
                const auto& ins = track->inserts()[i];
                RECT row_rc{insp_x + 8, sy, insp_r - right_margin, sy + slot_h};
                GuiRenderer::draw_rounded_box(mem_dc_, row_rc, t.bg_control, t.border_subtle, 3);

                // Power button
                RECT pwr_rc{insp_x + 12, sy + 4, insp_x + 36, sy + slot_h - 4};
                GuiRenderer::draw_button(mem_dc_, pwr_rc, ins.enabled ? "●" : "○", ins.enabled, t.success, t.bg_app);

                // Name
                std::string fx_name = ins.device ? ins.device->name() : "Effect";
                RECT name_fx_rc{insp_x + 40, sy + 2, insp_r - (right_margin + 84), sy + slot_h - 2};
                GuiRenderer::draw_text(mem_dc_, fx_name, name_fx_rc, ins.enabled ? t.text_primary : t.text_disabled);

                // Wet/Dry mix slider
                RECT mix_rc{insp_r - (right_margin + 80), sy + 5, insp_r - (right_margin + 26), sy + slot_h - 5};
                std::string mix_lbl = std::to_string(static_cast<int>(std::round(ins.wet_mix * 100.0f))) + "%";
                GuiRenderer::draw_slider_horizontal(mem_dc_, mix_rc, ins.wet_mix, mix_lbl);

                // Remove button
                RECT rem_rc{insp_r - (right_margin + 22), sy + 5, insp_r - (right_margin + 4), sy + slot_h - 5};
                GuiRenderer::draw_button(mem_dc_, rem_rc, "✕", false, t.danger, t.bg_app);
            } else {
                // "+ Insert Effect" Button
                RECT add_fx_rc{insp_x + 8, sy + 2, insp_r - right_margin, sy + 30};
                GuiRenderer::draw_button(mem_dc_, add_fx_rc, "+ Insert Effect", false, t.accent, t.bg_surface_2);
            }
        }

        SelectClipRgn(mem_dc_, NULL);
        DeleteObject(fx_clip);

        // Render Slim Vertical Scrollbar on Right Edge if overflow occurs
        if (max_fx_scroll > 0 && total_fx_items > 0) {
            int sb_x = insp_r - 9;
            int sb_w = 4;
            int sb_h = fx_area_bot - slot_y_start;
            int thumb_h = std::max(20, static_cast<int>(sb_h * (static_cast<float>(vis_fx_items) / static_cast<float>(total_fx_items))));
            int thumb_y = slot_y_start + static_cast<int>((sb_h - thumb_h) * (static_cast<float>(inspector_scroll_slot_) / static_cast<float>(max_fx_scroll)));

            RECT track_rc{sb_x, slot_y_start, sb_x + sb_w, fx_area_bot};
            GuiRenderer::draw_rounded_box(mem_dc_, track_rc, t.bg_control, t.border_subtle, 2);

            RECT thumb_rc{sb_x, thumb_y, sb_x + sb_w, thumb_y + thumb_h};
            COLORREF thumb_col = dragging_inspector_scrollbar_ ? t.accent_bright : t.text_muted;
            GuiRenderer::draw_rounded_box(mem_dc_, thumb_rc, thumb_col, thumb_col, 2);
        }
    }

    void render_mixer_panel() {
        const auto& t = get_theme();
        int mixer_top = client_h_ - 262;
        int mixer_bottom = client_h_ - 30;
        RECT mixer_rc{10, mixer_top, client_w_ - 10, mixer_bottom};

        GuiRenderer::draw_rounded_box(mem_dc_, mixer_rc, t.bg_surface, t.border_subtle, 6);

        auto& proj = engine_.session().project();

        int strip_w = 96;
        int strip_gap = 12;
        int ty = client_h_ - 234;

        int insert_start_x = 25 + strip_w + strip_gap;
        int avail_w = (client_w_ - 25) - insert_start_x;
        int vis_inserts = std::max(1, avail_w / (strip_w + strip_gap));
        size_t total_inserts = std::max(size_t(4), proj.channels().size());
        int max_mix_scroll = std::max(0, static_cast<int>(total_inserts) - vis_inserts);
        mixer_scroll_track_ = std::clamp(mixer_scroll_track_, 0, max_mix_scroll);

        SelectObject(mem_dc_, font_bold_);
        RECT title_rc{25, mixer_top + 5, 120, mixer_top + 25};
        GuiRenderer::draw_text(mem_dc_, "MIXER", title_rc, t.text_secondary);

        if (max_mix_scroll > 0) {
            SelectObject(mem_dc_, font_small_);
            std::string mix_lbl = "Tracks " + std::to_string(1 + mixer_scroll_track_) + "-" +
                                  std::to_string(std::min(total_inserts, size_t(1 + mixer_scroll_track_ + vis_inserts - 1))) +
                                  " / " + std::to_string(total_inserts);
            RECT lbl_rc{client_w_ - 240, client_h_ - 258, client_w_ - 85, client_h_ - 238};
            GuiRenderer::draw_text(mem_dc_, mix_lbl, lbl_rc, t.text_secondary, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

            RECT btn_l{client_w_ - 80, client_h_ - 259, client_w_ - 52, client_h_ - 237};
            RECT btn_r{client_w_ - 48, client_h_ - 259, client_w_ - 20, client_h_ - 237};
            GuiRenderer::draw_button(mem_dc_, btn_l, "◀", false, t.bg_control, t.bg_control);
            GuiRenderer::draw_button(mem_dc_, btn_r, "▶", false, t.bg_control, t.bg_control);
        }

        auto render_gdi_strip = [&](int tid, int sx) {
            RECT strip_rc{sx, ty, sx + strip_w, ty + 198};
            bool is_selected = (static_cast<uint32_t>(tid) == selected_mixer_track_);
            COLORREF border_c = is_selected ? t.accent_bright : t.border_subtle;
            GuiRenderer::draw_rounded_box(mem_dc_, strip_rc, t.bg_surface_2, border_c, 4);

            std::string label = (tid == 0) ? "MASTER" : ("TRACK " + std::to_string(tid));
            COLORREF label_col = (tid == 0) ? t.accent : t.text_primary;
            RECT label_rc{sx, ty + 2, sx + strip_w, ty + 22};
            SelectObject(mem_dc_, font_bold_);
            GuiRenderer::draw_text(mem_dc_, label, label_rc, label_col, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            auto* trk = proj.mixer_graph().get_track(tid);
            float vol = trk ? trk->volume() : 1.0f;
            float pan_val = trk ? trk->pan() : 0.0f;
            bool is_muted = trk ? trk->muted() : false;
            bool is_solo = trk ? trk->solo() : false;

            // Mute & Solo buttons
            SelectObject(mem_dc_, font_small_);
            RECT m_rc{sx + 6, ty + 24, sx + 45, ty + 42};
            GuiRenderer::draw_button(mem_dc_, m_rc, "M", is_muted, t.danger, t.bg_control);

            RECT s_rc{sx + 51, ty + 24, sx + 90, ty + 42};
            GuiRenderer::draw_button(mem_dc_, s_rc, "S", is_solo, t.accent, t.bg_control);

            // Stereo VU Meter on Left (Dual bars)
            float peak_l = (static_cast<size_t>(tid) < meter_peaks_l_.size()) ? meter_peaks_l_[tid] : 0.0f;
            float peak_r = (static_cast<size_t>(tid) < meter_peaks_r_.size()) ? meter_peaks_r_[tid] : 0.0f;
            RECT meter_rc{sx + 8, ty + 46, sx + 28, ty + 144};
            GuiRenderer::draw_meter_vertical_stereo(mem_dc_, meter_rc, peak_l, peak_r);

            // Vertical Volume Fader on Right
            RECT fader_rc{sx + 33, ty + 46, sx + 88, ty + 144};
            float norm_vol = vol / 1.5f;

            std::string db_str;
            if (vol < 0.001f) {
                db_str = "-inf dB";
            } else {
                float db = 20.0f * std::log10(vol);
                std::stringstream ss;
                ss << (db >= 0.0f ? "+" : "") << std::fixed << std::setprecision(1) << db << " dB";
                db_str = ss.str();
            }
            GuiRenderer::draw_slider_vertical(mem_dc_, fader_rc, norm_vol, db_str);

            // Panning Knob Underneath
            RECT pan_rc{sx + 6, ty + 148, sx + strip_w - 6, ty + 194};
            GuiRenderer::draw_pan_knob(mem_dc_, pan_rc, pan_val, "PAN");
        };

        render_gdi_strip(0, 25);
        for (int s = 0; s < vis_inserts; ++s) {
            int tid = 1 + mixer_scroll_track_ + s;
            if (static_cast<size_t>(tid) > total_inserts) break;
            int sx = insert_start_x + s * (strip_w + strip_gap);
            render_gdi_strip(tid, sx);
        }
    }

    void render_status_bar() {
        const auto& t = get_theme();
        RECT status_rc{0, client_h_ - 25, client_w_, client_h_};
        GuiRenderer::fill_rect(mem_dc_, status_rc, t.bg_surface_2);

        HPEN top_pen = CreatePen(PS_SOLID, 1, t.border_subtle);
        HGDIOBJ old_pen = SelectObject(mem_dc_, top_pen);
        MoveToEx(mem_dc_, 0, client_h_ - 25, NULL);
        LineTo(mem_dc_, client_w_, client_h_ - 25);
        SelectObject(mem_dc_, old_pen);
        DeleteObject(top_pen);

        SelectObject(mem_dc_, font_small_);
        RECT text_rc{15, client_h_ - 25, client_w_ - 15, client_h_};

        std::string status = status_message_.empty()
            ? "Ready. [F6] Playlist  •  [F7] Piano Roll  •  [F8] Inspector  •  [Space] Play/Pause"
            : status_message_;

        GuiRenderer::draw_text(mem_dc_, status, text_rc, t.text_muted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    // --- Interactive Plugin GUI Editor (Modal Window) ---
    void render_plugin_editor() {
        const auto& t = get_theme();

        int mw = 680;
        int mh = 480;
        int mx = (client_w_ - mw) / 2;
        int my = (client_h_ - mh) / 2;
        editor_bounds_ = RECT{mx, my, mx + mw, my + mh};

        GuiRenderer::draw_rounded_box(mem_dc_, editor_bounds_, t.bg_elevated, t.border_strong, 6);

        // Title Bar
        RECT title_bar_rc{mx, my, mx + mw, my + 38};
        GuiRenderer::draw_rounded_box(mem_dc_, title_bar_rc, t.bg_surface, t.border_subtle, 6);

        SelectObject(mem_dc_, font_bold_);
        RECT title_text_rc{mx + 15, my, mx + mw - 50, my + 38};
        GuiRenderer::draw_text(mem_dc_, "3xOsc Synthesizer — Instrument Editor", title_text_rc, t.accent, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Close Button [✕]
        RECT close_rc{mx + mw - 38, my + 6, mx + mw - 8, my + 32};
        GuiRenderer::draw_button(mem_dc_, close_rc, "✕", false, t.danger, t.bg_control);

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
        GuiRenderer::draw_button(mem_dc_, test_note_rc, "♪ AUDITION NOTE (C4)", false, t.accent, t.bg_control);

        RECT hint_rc{mx + 240, my + 380, mx + mw - 20, my + 420};
        SelectObject(mem_dc_, font_small_);
        GuiRenderer::draw_text(mem_dc_, "Adjust waveforms, detune, or filter sliders to reshape timbre in real time.",
                               hint_rc, t.text_secondary, DT_LEFT | DT_VCENTER | DT_WORDBREAK);
    }

    void draw_osc_section(const std::string& title, int y, float shape_norm, float vol_norm, int semi, bool has_semi) {
        const auto& t = get_theme();
        int mx = editor_bounds_.left;
        int mw = editor_bounds_.right - editor_bounds_.left;

        RECT sec_rc{mx + 14, y, mx + mw - 14, y + 68};
        GuiRenderer::draw_rounded_box(mem_dc_, sec_rc, t.bg_surface, t.border_subtle, 4);

        RECT label_rc{mx + 20, y + 4, mx + 160, y + 24};
        GuiRenderer::draw_text(mem_dc_, title, label_rc, t.text_secondary);

        int active_shape = static_cast<int>(shape_norm * 3.99f);
        const char* waves[4] = {"SINE", "SAW", "SQR", "NOISE"};

        for (int w = 0; w < 4; ++w) {
            RECT wave_rc{mx + 20 + w * 62, y + 25, mx + 20 + (w + 1) * 62 - 6, y + 55};
            GuiRenderer::draw_button(mem_dc_, wave_rc, waves[w], (w == active_shape), t.accent, t.bg_control);
        }

        if (has_semi) {
            RECT semi_minus{mx + 280, y + 25, mx + 310, y + 55};
            GuiRenderer::draw_button(mem_dc_, semi_minus, "-", false, t.bg_control, t.bg_control);

            std::string semi_str = (semi >= 0 ? "+" : "") + std::to_string(semi) + " sem";
            RECT semi_disp{mx + 315, y + 25, mx + 395, y + 55};
            GuiRenderer::draw_rounded_box(mem_dc_, semi_disp, t.bg_control, t.border_subtle, 3);
            GuiRenderer::draw_text(mem_dc_, semi_str, semi_disp, t.text_primary, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            RECT semi_plus{mx + 400, y + 25, mx + 430, y + 55};
            GuiRenderer::draw_button(mem_dc_, semi_plus, "+", false, t.bg_control, t.bg_control);
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
        // Project Name badge click [ 88, 8, 198, 40 ]
        if (x >= 88 && x <= 198 && y >= 8 && y <= 40) {
            std::string cur_name = engine_.session().project().name();
            if (cur_name.empty()) cur_name = "Untitled Project";
            status_message_ = "Active Project: " + cur_name + " (" +
                              std::to_string(engine_.session().project().channels().size()) + " Channels, " +
                              std::to_string(static_cast<int>(engine_.session().project().time_map().get_bpm_at(0))) + " BPM)";
            return;
        }

        // Undo [↶] button
        if (x >= 204 && x <= 234 && y >= 8 && y <= 40) {
            do_undo();
            return;
        }

        // Redo [↷] button
        if (x >= 238 && x <= 268 && y >= 8 && y <= 40) {
            do_redo();
            return;
        }

        // 1. PLAY Button [▶]
        if (x >= 276 && x <= 312 && y >= 8 && y <= 40) {
            start_playback_from_spm();
            return;
        }

        // 2. PAUSE Button [❚❚]
        if (x >= 316 && x <= 352 && y >= 8 && y <= 40) {
            pause_playback();
            return;
        }

        // 3. STOP Button [■]
        if (x >= 356 && x <= 392 && y >= 8 && y <= 40) {
            stop_playback();
            return;
        }

        // 4. Tempo - / +
        if (x >= 400 && x <= 424 && y >= 8 && y <= 40) {
            double bpm = engine_.session().project().time_map().get_bpm_at(0);
            engine_.session().project().time_map().set_tempo(std::max(20.0, bpm - 1.0));
            return;
        }
        if (x >= 512 && x <= 536 && y >= 8 && y <= 40) {
            double bpm = engine_.session().project().time_map().get_bpm_at(0);
            engine_.session().project().time_map().set_tempo(std::min(999.0, bpm + 1.0));
            return;
        }

        // 5. View Mode Switcher: [ 🎛 ] / [ 🎹 ] / [ ℹ ]
        if (x >= 684 && x <= 722 && y >= 8 && y <= 40) {
            view_mode_ = ViewMode::ChannelRack;
            status_message_ = "Switched to Playlist (F6)";
            return;
        }
        if (x >= 726 && x <= 764 && y >= 8 && y <= 40) {
            view_mode_ = ViewMode::PianoRoll;
            for (const auto& ch : engine_.session().project().channels()) {
                if (ch.id() == piano_roll_channel_) {
                    selected_mixer_track_ = ch.settings().mixer_track;
                    break;
                }
            }
            status_message_ = "Switched to Piano roll (F7)";
            return;
        }
        if (x >= 768 && x <= 806 && y >= 8 && y <= 40) {
            inspector_open_ = !inspector_open_;
            status_message_ = inspector_open_ ? "Opened Inspector Panel (F8)" : "Collapsed Inspector Panel (F8)";
            return;
        }

        // 6. Save & Export WAV buttons: [ 💾 ] / [ 💿 ]
        if (x >= client_w_ - 80 && x <= client_w_ - 45 && y >= 8 && y <= 40) {
            save_project();
            return;
        }
        if (x >= client_w_ - 41 && x <= client_w_ - 10 && y >= 8 && y <= 40) {
            render_wav();
            return;
        }

        // C. Mixer Panel Interaction
        if (y >= client_h_ - 262 && y <= client_h_ - 30) {
            handle_mixer_click(x, y);
            return;
        }

        // D. Right-Side Inspector Panel Interaction
        if (inspector_open_ && x >= client_w_ - (kInspectorWidth + kLayoutMargin) && x <= client_w_ - kLayoutMargin && y >= 58 && y <= client_h_ - 268) {
            handle_inspector_click(x, y);
            return;
        }

        // E. Dispatch based on ViewMode
        if (view_mode_ == ViewMode::ChannelRack) {
            handle_channel_rack_click(x, y);
        } else {
            handle_piano_roll_click(x, y);
        }
    }

    void handle_inspector_click(int x, int y) {
        int insp_x = client_w_ - (kInspectorWidth + kLayoutMargin);
        int insp_y = 58;
        int insp_r = client_w_ - kLayoutMargin;
        int insp_bot = client_h_ - 268;

        // 1. Close button [✕]
        if (x >= insp_r - 28 && x <= insp_r - 8 && y >= insp_y + 9 && y <= insp_y + 29) {
            inspector_open_ = false;
            status_message_ = "Collapsed Inspector Panel (F8)";
            return;
        }

        uint32_t tid = selected_mixer_track_;
        auto& proj = engine_.session().project();
        auto* track = proj.mixer_graph().get_track(tid);
        if (!track && tid > 0) {
            proj.mixer_graph().add_track(tid, "Track " + std::to_string(tid));
            track = proj.mixer_graph().get_track(tid);
        }
        if (!track) return;

        size_t num_fx = track->inserts().size();
        int slot_y_start = insp_y + 192;
        int slot_h = 32;
        int slot_gap = 4;
        int fx_area_bot = insp_bot - 8;
        int fx_avail_h = fx_area_bot - slot_y_start;
        int vis_fx_items = std::max(1, fx_avail_h / (slot_h + slot_gap));
        int total_fx_items = static_cast<int>(num_fx) + (num_fx < 10 ? 1 : 0);
        int max_fx_scroll = std::max(0, total_fx_items - vis_fx_items);
        inspector_scroll_slot_ = std::clamp(inspector_scroll_slot_, 0, max_fx_scroll);
        int right_margin = (max_fx_scroll > 0) ? 14 : 8;

        // Check if clicked "+ Insert Effect" button FIRST (shows modal popup menu without audio lock)
        if (num_fx < 10) {
            int add_slot_idx = static_cast<int>(num_fx);
            if (add_slot_idx >= inspector_scroll_slot_) {
                int add_btn_y = slot_y_start + (add_slot_idx - inspector_scroll_slot_) * (slot_h + slot_gap);
                if (add_btn_y + 30 <= fx_area_bot && add_btn_y >= slot_y_start) {
                    if (x >= insp_x + 8 && x <= insp_r - right_margin && y >= add_btn_y + 2 && y <= add_btn_y + 30) {
                        HMENU hMenu = CreatePopupMenu();
                        AppendMenuA(hMenu, MF_STRING, 1001, "1. Parametric EQ");
                        AppendMenuA(hMenu, MF_STRING, 1002, "2. Stereo Delay");
                        AppendMenuA(hMenu, MF_STRING, 1003, "3. Algorithmic Reverb");
                        AppendMenuA(hMenu, MF_STRING, 1004, "4. Stereo Compressor");
                        AppendMenuA(hMenu, MF_STRING, 1005, "5. Master Limiter");

                        POINT pt{x, y};
                        ClientToScreen(hwnd_, &pt);
                        // Note: TrackPopupMenu runs without audio_mutex_ held, preventing audio dropout during menu browsing
                        int cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, pt.x, pt.y, 0, hwnd_, NULL);
                        DestroyMenu(hMenu);

                        if (cmd == 1001) insert_effect_to_track(tid, "core.fx.parametric_eq");
                        else if (cmd == 1002) insert_effect_to_track(tid, "core.fx.delay");
                        else if (cmd == 1003) insert_effect_to_track(tid, "core.fx.reverb");
                        else if (cmd == 1004) insert_effect_to_track(tid, "core.fx.compressor");
                        else if (cmd == 1005) insert_effect_to_track(tid, "core.fx.limiter");
                        return;
                    }
                }
            }
        }

        // Check vertical scrollbar click / drag
        if (max_fx_scroll > 0 && total_fx_items > 0) {
            float sb_h = static_cast<float>(fx_area_bot - slot_y_start);
            float thumb_h = std::max(20.0f, sb_h * (static_cast<float>(vis_fx_items) / static_cast<float>(total_fx_items)));
            float thumb_y = static_cast<float>(slot_y_start) + (sb_h - thumb_h) * (static_cast<float>(inspector_scroll_slot_) / static_cast<float>(max_fx_scroll));

            if (x >= insp_r - 12 && x <= insp_r - 2 && y >= slot_y_start && y <= fx_area_bot) {
                if (static_cast<float>(y) < thumb_y) {
                    inspector_scroll_slot_ = std::max(0, inspector_scroll_slot_ - 1);
                } else if (static_cast<float>(y) > thumb_y + thumb_h) {
                    inspector_scroll_slot_ = std::min(max_fx_scroll, inspector_scroll_slot_ + 1);
                } else {
                    dragging_inspector_scrollbar_ = true;
                    drag_inspector_scroll_start_y_ = y;
                    drag_inspector_scroll_orig_slot_ = inspector_scroll_slot_;
                    SetCapture(hwnd_);
                }
                return;
            }
        }

        // Lock audio_mutex_ for parameter adjustment and insert slot mutations
        std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());

        // 2. Volume slider: [insp_x + 14, insp_y + 90, insp_r - 14, insp_y + 108]
        if (x >= insp_x + 14 && x <= insp_r - 14 && y >= insp_y + 90 && y <= insp_y + 108) {
            dragging_inspector_vol_ = true;
            float norm = float(x - (insp_x + 14)) / float((insp_r - 14) - (insp_x + 14));
            norm = std::clamp(norm, 0.0f, 1.0f);
            track->set_volume(norm * 1.25f);
            int vol_pct = static_cast<int>(std::round(track->volume() * 100.0f));
            status_message_ = (tid == 0 ? "Master" : ("Track " + std::to_string(tid))) + " Volume: " + std::to_string(vol_pct) + "%";
            return;
        }

        // 3. Pan slider: [insp_x + 14, insp_y + 112, insp_r - 14, insp_y + 130]
        if (x >= insp_x + 14 && x <= insp_r - 14 && y >= insp_y + 112 && y <= insp_y + 130) {
            dragging_inspector_pan_ = true;
            float norm = float(x - (insp_x + 14)) / float((insp_r - 14) - (insp_x + 14));
            float new_pan = std::clamp(norm * 2.0f - 1.0f, -1.0f, 1.0f);
            track->set_pan(new_pan);
            std::string pan_str = (std::abs(new_pan) < 0.02f) ? "Center" :
                (new_pan < 0.0f ? "Left " + std::to_string(static_cast<int>(std::round(-new_pan * 100.0f))) + "%"
                                : "Right " + std::to_string(static_cast<int>(std::round(new_pan * 100.0f))) + "%");
            status_message_ = (tid == 0 ? "Master" : ("Track " + std::to_string(tid))) + " Pan: " + pan_str;
            return;
        }

        // 4. Mute & Solo buttons:
        int mid_w = (insp_r - 14) - (insp_x + 14);
        int btn_w = (mid_w - 6) / 2;
        if (y >= insp_y + 134 && y <= insp_y + 154) {
            if (x >= insp_x + 14 && x <= insp_x + 14 + btn_w) {
                track->set_muted(!track->muted());
                status_message_ = (tid == 0 ? "Master" : ("Track " + std::to_string(tid))) +
                                  (track->muted() ? " Muted" : " Unmuted");
                return;
            }
            if (x >= insp_x + 14 + btn_w + 6 && x <= insp_r - 14) {
                track->set_solo(!track->solo());
                status_message_ = (tid == 0 ? "Master" : ("Track " + std::to_string(tid))) +
                                  (track->solo() ? " Soloed" : " Unsoloed");
                return;
            }
        }

        // 5. FX Inserts
        for (int i = inspector_scroll_slot_; i < static_cast<int>(num_fx); ++i) {
            int sy = slot_y_start + (i - inspector_scroll_slot_) * (slot_h + slot_gap);
            if (sy + slot_h > fx_area_bot) break;

            // Power Toggle: [insp_x + 12, sy + 4, insp_x + 36, sy + slot_h - 4]
            if (x >= insp_x + 12 && x <= insp_x + 36 && y >= sy + 4 && y <= sy + slot_h - 4) {
                track->inserts()[i].enabled = !track->inserts()[i].enabled;
                std::string name = track->inserts()[i].device ? track->inserts()[i].device->name() : "Effect";
                status_message_ = (track->inserts()[i].enabled ? "Enabled " : "Bypassed ") + name;
                return;
            }

            // Wet/Dry mix slider: [insp_r - (right_margin + 80), sy + 5, insp_r - (right_margin + 26), sy + slot_h - 5]
            if (x >= insp_r - (right_margin + 80) && x <= insp_r - (right_margin + 26) && y >= sy + 5 && y <= sy + slot_h - 5) {
                dragging_inspector_wet_slot_ = i;
                float norm = float(x - (insp_r - (right_margin + 80))) / 54.0f;
                track->inserts()[i].wet_mix = std::clamp(norm, 0.0f, 1.0f);
                int pct = static_cast<int>(std::round(track->inserts()[i].wet_mix * 100.0f));
                std::string name = track->inserts()[i].device ? track->inserts()[i].device->name() : "Effect";
                status_message_ = name + " Wet Mix: " + std::to_string(pct) + "%";
                return;
            }

            // Effect Name / row body click: show detailed status info
            if (x >= insp_x + 40 && x <= insp_r - (right_margin + 84) && y >= sy + 2 && y <= sy + slot_h - 2) {
                std::string name = track->inserts()[i].device ? track->inserts()[i].device->name() : "Effect";
                int pct = static_cast<int>(std::round(track->inserts()[i].wet_mix * 100.0f));
                status_message_ = "Insert " + std::to_string(i + 1) + ": " + name + " (" + (track->inserts()[i].enabled ? "Active" : "Bypassed") + ", Wet Mix: " + std::to_string(pct) + "%)";
                return;
            }

            // Remove Button [✕]: [insp_r - (right_margin + 22), sy + 5, insp_r - (right_margin + 4), sy + slot_h - 5]
            if (x >= insp_r - (right_margin + 22) && x <= insp_r - (right_margin + 4) && y >= sy + 5 && y <= sy + slot_h - 5) {
                std::string name = track->inserts()[i].device ? track->inserts()[i].device->name() : "Effect";
                engine_.session().undo_stack().push_and_execute(
                    std::make_unique<RemoveEffectCommand>(proj.mixer_graph(), tid, static_cast<size_t>(i)));
                dragging_inspector_wet_slot_ = -1;
                status_message_ = "Removed insert effect: " + name;
                return;
            }
        }
    }

    void handle_inspector_right_click(int x, int y) {
        int insp_x = client_w_ - (kInspectorWidth + kLayoutMargin);
        int insp_r = client_w_ - kLayoutMargin;
        int insp_y = 58;
        int insp_bot = client_h_ - 268;

        uint32_t tid = selected_mixer_track_;
        auto& proj = engine_.session().project();
        auto* track = proj.mixer_graph().get_track(tid);
        if (!track) return;

        // Right-click on Volume slider -> reset to default unity (1.0f = 0 dB)
        if (x >= insp_x + 14 && x <= insp_r - 14 && y >= insp_y + 90 && y <= insp_y + 108) {
            track->set_volume(1.0f);
            status_message_ = (tid == 0 ? "Master" : ("Track " + std::to_string(tid))) + " Volume reset to 0 dB (100%)";
            return;
        }

        // Right-click on Pan slider -> reset to Center (0.0f)
        if (x >= insp_x + 14 && x <= insp_r - 14 && y >= insp_y + 112 && y <= insp_y + 130) {
            track->set_pan(0.0f);
            status_message_ = (tid == 0 ? "Master" : ("Track " + std::to_string(tid))) + " Pan reset to Center";
            return;
        }

        // Right-click on FX Inserts:
        size_t num_fx = track->inserts().size();
        int slot_y_start = insp_y + 192;
        int slot_h = 32;
        int slot_gap = 4;
        int fx_area_bot = insp_bot - 8;
        int fx_avail_h = fx_area_bot - slot_y_start;
        int vis_fx_items = std::max(1, fx_avail_h / (slot_h + slot_gap));
        int total_fx_items = static_cast<int>(num_fx) + (num_fx < 10 ? 1 : 0);
        int max_fx_scroll = std::max(0, total_fx_items - vis_fx_items);
        int right_margin = (max_fx_scroll > 0) ? 14 : 8;

        for (int i = inspector_scroll_slot_; i < static_cast<int>(num_fx); ++i) {
            int sy = slot_y_start + (i - inspector_scroll_slot_) * (slot_h + slot_gap);
            if (sy + slot_h > fx_area_bot) break;

            // Wet/Dry mix slider: right-click resets to 100%
            if (x >= insp_r - (right_margin + 80) && x <= insp_r - (right_margin + 26) && y >= sy + 5 && y <= sy + slot_h - 5) {
                track->inserts()[i].wet_mix = 1.0f;
                std::string name = track->inserts()[i].device ? track->inserts()[i].device->name() : "Effect";
                status_message_ = name + " Wet Mix reset to 100%";
                return;
            }

            // Right-click on slot row -> remove effect with undo
            if (x >= insp_x + 8 && x <= insp_r - right_margin && y >= sy && y <= sy + slot_h) {
                std::string name = track->inserts()[i].device ? track->inserts()[i].device->name() : "Effect";
                engine_.session().undo_stack().push_and_execute(
                    std::make_unique<RemoveEffectCommand>(proj.mixer_graph(), tid, static_cast<size_t>(i)));
                dragging_inspector_wet_slot_ = -1;
                status_message_ = "Removed insert effect: " + name;
                return;
            }
        }
    }

    void handle_mixer_click(int x, int y) {
        std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
        auto& proj = engine_.session().project();

        float track_w = 96.0f;
        float gap = 12.0f;
        float ty = static_cast<float>(client_h_ - 234);
        float insert_start_x = 25.0f + track_w + gap;
        float avail_w = (static_cast<float>(client_w_) - 25.0f) - insert_start_x;
        int vis_inserts = std::max(1, static_cast<int>(avail_w / (track_w + gap)));
        size_t total_inserts = std::max(size_t(4), proj.channels().size());
        int max_mix_scroll = std::max(0, static_cast<int>(total_inserts) - vis_inserts);

        // 1. Navigation buttons: [ ◀ ] and [ ▶ ]
        if (max_mix_scroll > 0) {
            if (x >= client_w_ - 80 && x <= client_w_ - 52 && y >= client_h_ - 259 && y <= client_h_ - 237) {
                mixer_scroll_track_ = std::max(0, mixer_scroll_track_ - 1);
                status_message_ = "Scrolled mixer to Track " + std::to_string(1 + mixer_scroll_track_);
                return;
            }
            if (x >= client_w_ - 48 && x <= client_w_ - 20 && y >= client_h_ - 259 && y <= client_h_ - 237) {
                mixer_scroll_track_ = std::min(max_mix_scroll, mixer_scroll_track_ + 1);
                status_message_ = "Scrolled mixer to Track " + std::to_string(1 + mixer_scroll_track_);
                return;
            }
        }

        // Helper lambda for track strip hit testing
        auto handle_strip_click = [&](int tid, float tx) -> bool {
            if (x < tx || x > tx + track_w || y < ty || y > ty + 198.0f) {
                return false;
            }

            selected_mixer_track_ = static_cast<uint32_t>(tid);
            status_message_ = "Selected " + (tid == 0 ? "Master" : ("Track " + std::to_string(tid)));
            auto* mix_tr = proj.mixer_graph().get_track(tid);

            // A. Mute button
            if (x >= tx + 6.0f && x <= tx + 45.0f && y >= ty + 24.0f && y <= ty + 42.0f) {
                if (mix_tr) {
                    mix_tr->set_muted(!mix_tr->muted());
                    status_message_ = (tid == 0 ? "Master" : "Track " + std::to_string(tid)) +
                                      (mix_tr->muted() ? " Muted" : " Unmuted");
                }
                return true;
            }

            // B. Solo button
            if (x >= tx + 51.0f && x <= tx + 90.0f && y >= ty + 24.0f && y <= ty + 42.0f) {
                if (mix_tr) {
                    mix_tr->set_solo(!mix_tr->solo());
                    status_message_ = (tid == 0 ? "Master" : "Track " + std::to_string(tid)) +
                                      (mix_tr->solo() ? " Soloed" : " Unsoloed");
                }
                return true;
            }

            // C. Fader drag / click
            if (x >= tx + 28.0f && x <= tx + 92.0f && y >= ty + 44.0f && y <= ty + 146.0f) {
                dragging_mixer_track_ = tid;
                RECT fader_rc{static_cast<int>(tx + 33.0f), static_cast<int>(ty + 46.0f),
                              static_cast<int>(tx + 88.0f), static_cast<int>(ty + 144.0f)};
                update_mixer_volume_from_mouse(tid, y, fader_rc);
                return true;
            }

            // D. Panning knob drag
            if (x >= tx + 6.0f && x <= tx + track_w - 6.0f && y >= ty + 146.0f && y <= ty + 196.0f) {
                dragging_mixer_pan_track_ = tid;
                pan_drag_start_y_ = y;
                pan_drag_start_val_ = mix_tr ? mix_tr->pan() : 0.0f;
                return true;
            }

            return true;
        };

        // Check Master Strip (tid = 0)
        if (handle_strip_click(0, 25.0f)) return;

        // Check Visible Insert Strips
        for (int s = 0; s < vis_inserts; ++s) {
            int tid = 1 + mixer_scroll_track_ + s;
            if (static_cast<size_t>(tid) > total_inserts) break;
            float tx = insert_start_x + s * (track_w + gap);
            if (handle_strip_click(tid, tx)) return;
        }
    }

    void handle_mixer_right_click(int x, int y) {
        std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
        auto& proj = engine_.session().project();

        float track_w = 96.0f;
        float gap = 12.0f;
        float ty = static_cast<float>(client_h_ - 234);
        float insert_start_x = 25.0f + track_w + gap;
        float avail_w = (static_cast<float>(client_w_) - 25.0f) - insert_start_x;
        int vis_inserts = std::max(1, static_cast<int>(avail_w / (track_w + gap)));
        size_t total_inserts = std::max(size_t(4), proj.channels().size());

        auto handle_strip_right_click = [&](int tid, float tx) -> bool {
            if (x < tx || x > tx + track_w || y < ty || y > ty + 198.0f) {
                return false;
            }

            selected_mixer_track_ = static_cast<uint32_t>(tid);
            auto* mix_tr = proj.mixer_graph().get_track(tid);
            if (!mix_tr) return false;

            // Right-click on Panning knob -> reset to Center (0.0)
            if (x >= tx + 6.0f && x <= tx + track_w - 6.0f && y >= ty + 146.0f && y <= ty + 196.0f) {
                mix_tr->set_pan(0.0f);
                status_message_ = (tid == 0 ? "Master" : "Track " + std::to_string(tid)) + " Pan reset to Center";
                return true;
            }

            // Right-click on Fader -> reset to Unity Gain (1.0 = 0 dB)
            if (x >= tx + 28.0f && x <= tx + 92.0f && y >= ty + 44.0f && y <= ty + 146.0f) {
                mix_tr->set_volume(1.0f);
                status_message_ = (tid == 0 ? "Master" : "Track " + std::to_string(tid)) + " Volume reset to 0 dB";
                return true;
            }

            return false;
        };

        if (handle_strip_right_click(0, 25.0f)) return;

        for (int s = 0; s < vis_inserts; ++s) {
            int tid = 1 + mixer_scroll_track_ + s;
            if (static_cast<size_t>(tid) > total_inserts) break;
            float tx = insert_start_x + s * (track_w + gap);
            if (handle_strip_right_click(tid, tx)) return;
        }
    }

    void handle_channel_rack_click(int x, int y) {
        std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
        int ruler_y = 96;
        int ruler_h = 22;
        auto ppq = engine_.session().project().time_map().ppq();
        auto bar_ticks = 4 * ppq;
        auto& proj = engine_.session().project();

        // 0. Top Toolbar [+ Add Instrument] button - always accessible
        if (x >= 110 && x <= 250 && y >= 64 && y <= 90) {
            add_channel();
            return;
        }

        int bars_per_view = get_bars_per_view();
        int max_bars = get_max_sequencer_bars();
        int start_x = 265;
        int total_seq_w = (get_effective_workspace_w() - 25) - start_x;
        domain::Tick view_start_tick = sequencer_scroll_bar_ * bar_ticks;
        domain::Tick view_duration = bars_per_view * bar_ticks;
        domain::Tick view_end_tick = view_start_tick + view_duration;

        // 1. Horizontal Scrollbar Track & Thumb Interaction
        int rack_bottom = client_h_ - 268;
        int scroll_y = rack_bottom - 22;
        int scroll_h = 16;
        float max_scroll = std::max(1.0f, float(max_bars - bars_per_view));
        float thumb_w = std::max(35.0f, (float(bars_per_view) / float(max_bars)) * float(total_seq_w));
        float available_w = float(total_seq_w) - thumb_w;
        float scroll_ratio = std::clamp(float(sequencer_scroll_bar_) / max_scroll, 0.0f, 1.0f);
        float thumb_x = float(start_x) + scroll_ratio * available_w;

        if (x >= start_x && x <= start_x + total_seq_w && y >= scroll_y - 4 && y <= scroll_y + scroll_h + 4) {
            if (x >= thumb_x && x <= thumb_x + thumb_w) {
                // Clicked directly on thumb
                dragging_seq_scrollbar_ = true;
                drag_seq_scroll_start_mouse_x_ = float(x);
                drag_seq_scroll_orig_bar_ = sequencer_scroll_bar_;
            } else {
                // Clicked on scroll track -> jump thumb center to clicked location
                float click_rel_x = std::clamp(float(x - start_x) - thumb_w * 0.5f, 0.0f, available_w);
                sequencer_scroll_bar_ = (available_w > 0.0f)
                    ? std::clamp(static_cast<int>(std::round((click_rel_x / available_w) * max_scroll)), 0, static_cast<int>(max_scroll))
                    : 0;
                dragging_seq_scrollbar_ = true;
                drag_seq_scroll_start_mouse_x_ = float(x);
                drag_seq_scroll_orig_bar_ = sequencer_scroll_bar_;
            }
            status_message_ = "Scrolled timeline to Bar " + std::to_string(sequencer_scroll_bar_ + 1) + " / " + std::to_string(max_bars);
            return;
        }

        // 2. Timeline Ruler Click (Snap SPM to clicked beat / ketukan)
        if (x >= start_x && x <= start_x + total_seq_w && y >= ruler_y && y <= ruler_y + ruler_h + 4) {
            dragging_spm_ = true;
            double norm_x = double(x - start_x) / double(total_seq_w);
            domain::Tick raw_tick = view_start_tick + static_cast<domain::Tick>(norm_x * view_duration);
            domain::Tick beat_ticks = ppq;
            song_position_marker_ = std::max(domain::Tick(0), (raw_tick / beat_ticks) * beat_ticks);
            engine_.transport().seek(song_position_marker_);

            int bar_num = static_cast<int>(song_position_marker_ / bar_ticks) + 1;
            int beat_num = static_cast<int>((song_position_marker_ % bar_ticks) / beat_ticks) + 1;
            status_message_ = "SPM set to Bar " + std::to_string(bar_num) + " Beat " + std::to_string(beat_num);
            return;
        }

        // 3. Channels List & Track Clip Interaction with Vertical Scrolling
        float start_y = 124.0f;
        float row_h = 48.0f;
        float step_h_f = 42.0f;
        float rack_avail_h = (float(client_h_ - 268) - 26.0f) - start_y;
        int visible_channels = std::max(1, static_cast<int>(rack_avail_h / row_h));
        int max_ch_scroll = std::max(0, static_cast<int>(proj.channels().size()) - visible_channels + 1);
        sequencer_scroll_track_ = std::clamp(sequencer_scroll_track_, 0, max_ch_scroll);

        // Vertical scrollbar click / drag
        if (max_ch_scroll > 0) {
            float vbar_x = 254.0f;
            float vbar_w = 10.0f;
            if (x >= vbar_x && x <= vbar_x + vbar_w && y >= start_y && y <= start_y + rack_avail_h) {
                float v_thumb_h = std::max(20.0f, (float(visible_channels) / float(proj.channels().size() + 1)) * rack_avail_h);
                float avail_scroll_h = rack_avail_h - v_thumb_h;
                float click_y = std::clamp(float(y) - start_y - v_thumb_h * 0.5f, 0.0f, avail_scroll_h);
                if (avail_scroll_h > 0.0f) {
                    sequencer_scroll_track_ = std::clamp(static_cast<int>(std::round((click_y / avail_scroll_h) * float(max_ch_scroll))), 0, max_ch_scroll);
                }
                dragging_seq_v_scrollbar_ = true;
                drag_seq_v_scroll_start_y_ = float(y);
                drag_seq_v_scroll_orig_track_ = sequencer_scroll_track_;
                return;
            }
        }

        size_t start_ch = static_cast<size_t>(sequencer_scroll_track_);
        size_t end_ch = std::min(proj.channels().size(), start_ch + static_cast<size_t>(visible_channels) + 1);

        for (size_t ch_idx = start_ch; ch_idx < end_ch; ++ch_idx) {
            auto& ch = proj.channels()[ch_idx];
            float ch_y = start_y + static_cast<float>(ch_idx - start_ch) * row_h;
            int ch_top = static_cast<int>(ch_y);
            int ch_bot = static_cast<int>(ch_y + step_h_f);

            while (proj.tracks().size() <= ch_idx) {
                domain::TrackId tid = static_cast<domain::TrackId>(proj.tracks().size() + 1);
                proj.tracks().emplace_back(tid, "Track " + std::to_string(tid));
            }
            auto& track = proj.tracks()[ch_idx];

            // Mute [M]
            if (x >= 25 && x <= 47 && y >= ch_top + 8 && y <= ch_bot - 8) {
                ch.settings().muted = !ch.settings().muted;
                selected_mixer_track_ = ch.settings().mixer_track;
                return;
            }

            // Solo [S]
            if (x >= 51 && x <= 73 && y >= ch_top + 8 && y <= ch_bot - 8) {
                ch.settings().solo = !ch.settings().solo;
                selected_mixer_track_ = ch.settings().mixer_track;
                return;
            }

            // Channel Name / VST GUI Editor button
            if (x >= 78 && x <= 218 && y >= ch_top + 5 && y <= ch_bot - 5) {
                active_editor_channel_ = ch.id();
                selected_mixer_track_ = ch.settings().mixer_track;
                status_message_ = "Opened Instrument Editor: " + ch.settings().name;
                return;
            }

            // Piano Roll Button [ 🎹 ]
            if (x >= 223 && x <= 255 && y >= ch_top + 8 && y <= ch_bot - 8) {
                piano_roll_channel_ = ch.id();
                selected_mixer_track_ = ch.settings().mixer_track;
                view_mode_ = ViewMode::PianoRoll;
                status_message_ = "Opened Piano Roll for " + ch.settings().name;
                return;
            }

            // Playlist Lane Clip Interaction (Move Drag, Resize Drag, or Place new Clip)
            if (x >= start_x && x <= start_x + total_seq_w && y >= ch_top && y <= ch_bot) {
                selected_mixer_track_ = ch.settings().mixer_track;
                // A. Check if clicked on an EXISTING clip
                bool found_clip = false;
                size_t hit_ci = 0;
                bool hit_resize = false;

                for (size_t ci = 0; ci < track.clips().size(); ++ci) {
                    const auto& clip = track.clips()[ci];
                    if (clip.end() <= view_start_tick || clip.start >= view_end_tick) continue;
                    double norm_start = double(clip.start - view_start_tick) / double(view_duration);
                    double norm_len = double(clip.length) / double(view_duration);
                    int cx = start_x + static_cast<int>(norm_start * total_seq_w);
                    int cw = std::max(16, static_cast<int>(norm_len * total_seq_w));

                    if (x >= cx && x <= cx + cw) {
                        found_clip = true;
                        hit_ci = ci;
                        if (x >= cx + cw - 10) {
                            hit_resize = true;
                        }
                        break;
                    }
                }

                if (found_clip) {
                    const auto& clip = track.clips()[hit_ci];
                    if (hit_resize) {
                        clip_drag_mode_ = ClipDragMode::Resize;
                        drag_clip_track_idx_ = ch_idx;
                        drag_clip_idx_ = hit_ci;
                        drag_clip_orig_start_ = clip.start;
                        drag_clip_orig_len_ = clip.length;
                        drag_clip_start_mouse_x_ = x;
                        status_message_ = "Resizing clip length (Drag to change beats/bars)";
                    } else {
                        clip_drag_mode_ = ClipDragMode::Move;
                        drag_clip_track_idx_ = ch_idx;
                        drag_clip_idx_ = hit_ci;
                        drag_clip_orig_start_ = clip.start;
                        drag_clip_orig_len_ = clip.length;
                        drag_clip_start_mouse_x_ = x;
                        status_message_ = "Moving clip (Drag to move across beats/bars)";
                    }
                    return;
                }

                // B. Clicked on EMPTY lane space -> Add new clip starting at this beat!
                double norm_x = double(x - start_x) / double(total_seq_w);
                domain::Tick raw_tick = view_start_tick + static_cast<domain::Tick>(norm_x * view_duration);
                domain::Tick beat_ticks = ppq;
                domain::Tick snapped_start = (raw_tick / beat_ticks) * beat_ticks;
                domain::Tick default_len = 4 * 4 * ppq; // 4 bars by default

                track.add_clip(domain::Clip{1, snapped_start, default_len, false});
                size_t new_clip_idx = track.clips().size() - 1;

                // Immediately engage resize dragging so user can drag right to stretch or left to shrink
                clip_drag_mode_ = ClipDragMode::Resize;
                drag_clip_track_idx_ = ch_idx;
                drag_clip_idx_ = new_clip_idx;
                drag_clip_orig_start_ = snapped_start;
                drag_clip_orig_len_ = default_len;
                drag_clip_start_mouse_x_ = x;

                int bar_num = static_cast<int>(snapped_start / bar_ticks) + 1;
                int beat_num = static_cast<int>((snapped_start % bar_ticks) / beat_ticks) + 1;
                status_message_ = "Placed clip at Bar " + std::to_string(bar_num) + " Beat " + std::to_string(beat_num) +
                                  " on " + ch.settings().name;
                return;
            }
        }

        // Add Channel Button in scrollable view
        if (proj.channels().size() >= start_ch && proj.channels().size() <= end_ch) {
            float add_y = start_y + static_cast<float>(proj.channels().size() - start_ch) * row_h;
            if (x >= 25 && x <= 218 && y >= add_y + 4.0f && y <= add_y + 34.0f) {
                add_channel();
                return;
            }
        }
    }

    void handle_piano_roll_click(int x, int y) {
        std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
        PianoRollLayout lay;
        lay.init(get_effective_workspace_w(), client_h_, PianoRollNumPitches, piano_roll_steps_);
        auto ppq = engine_.session().project().time_map().ppq();
        auto step_ticks = ppq / 4;
        int max_base_pitch = 128 - PianoRollNumPitches;
        int max_steps = get_max_piano_roll_steps();
        int max_scroll_step = std::max(0, max_steps - piano_roll_steps_);

        // 1. Toolbar clicks
        if (float(y) >= lay.toolbar_y && float(y) <= lay.toolbar_y + lay.toolbar_h) {
            // [ 🎛 Back to Rack ]
            if (x >= 135 && x <= 255) {
                view_mode_ = ViewMode::ChannelRack;
                status_message_ = "Returned to Playlist (F6)";
                return;
            }
            // [ 16 Steps / 32 Steps ]
            if (x >= 483 && x <= 565) {
                piano_roll_steps_ = (piano_roll_steps_ == 16) ? 32 : 16;
                status_message_ = "Grid resolution set to " + std::to_string(piano_roll_steps_) + " Steps";
                return;
            }
            // [ 📏 Len: X ]
            if (x >= 573 && x <= 665) {
                if (piano_roll_note_len_steps_ == 1) piano_roll_note_len_steps_ = 2;
                else if (piano_roll_note_len_steps_ == 2) piano_roll_note_len_steps_ = 4;
                else if (piano_roll_note_len_steps_ == 4) piano_roll_note_len_steps_ = 8;
                else piano_roll_note_len_steps_ = 1;

                status_message_ = "Default Note Length set to " + std::to_string(piano_roll_note_len_steps_) + " Steps";
                return;
            }
            // [ Clear ]
            if (x >= 673 && x <= 735) {
                auto& proj = engine_.session().project();
                auto* pat = proj.get_pattern(1);
                if (pat) {
                    auto& notes = pat->get_or_create_channel_notes(piano_roll_channel_);
                    notes.clear();
                    status_message_ = "Cleared all notes in Piano roll";
                }
                return;
            }
            return;
        }

        // 2. Vertical Scrollbar (Pitch Up / Down across full 128 semitones)
        if (float(x) >= lay.v_scroll_x && float(x) <= lay.v_scroll_x + lay.v_scroll_w &&
            float(y) >= lay.grid_top && float(y) <= lay.grid_bottom) {
            float thumb_h = std::max(28.0f, (float(PianoRollNumPitches) / 128.0f) * lay.grid_h);
            float p_ratio = (max_base_pitch > 0) ? (float(max_base_pitch - piano_roll_base_pitch_) / float(max_base_pitch)) : 0.0f;
            float thumb_y = lay.grid_top + p_ratio * (lay.grid_h - thumb_h);

            if (float(y) >= thumb_y && float(y) <= thumb_y + thumb_h) {
                // Clicked on thumb -> engage vertical dragging
                dragging_piano_v_scrollbar_ = true;
                drag_piano_v_scroll_start_y_ = float(y);
                drag_piano_v_scroll_orig_pitch_ = piano_roll_base_pitch_;
            } else if (float(y) < thumb_y) {
                // Page up (higher pitch by 1 octave)
                piano_roll_base_pitch_ = std::min(max_base_pitch, piano_roll_base_pitch_ + 12);
            } else {
                // Page down (lower pitch by 1 octave)
                piano_roll_base_pitch_ = std::max(0, piano_roll_base_pitch_ - 12);
            }
            status_message_ = "Pitch Range: " + get_midi_note_name(static_cast<uint8_t>(piano_roll_base_pitch_)) +
                              " to " + get_midi_note_name(static_cast<uint8_t>(piano_roll_base_pitch_ + PianoRollNumPitches - 1));
            return;
        }

        // 3. Horizontal Scrollbar (Timeline Left / Right)
        if (float(x) >= lay.grid_x && float(x) <= lay.grid_x + lay.grid_w &&
            float(y) >= lay.h_scroll_y && float(y) <= lay.h_scroll_y + lay.h_scroll_h) {
            float thumb_w = std::max(35.0f, (float(piano_roll_steps_) / float(max_steps)) * lay.grid_w);
            float s_ratio = (max_scroll_step > 0) ? (float(piano_roll_scroll_step_) / float(max_scroll_step)) : 0.0f;
            float thumb_x = lay.grid_x + s_ratio * (lay.grid_w - thumb_w);

            if (float(x) >= thumb_x && float(x) <= thumb_x + thumb_w) {
                // Clicked on thumb -> engage horizontal dragging
                dragging_piano_h_scrollbar_ = true;
                drag_piano_h_scroll_start_x_ = float(x);
                drag_piano_h_scroll_orig_step_ = piano_roll_scroll_step_;
            } else if (float(x) < thumb_x) {
                // Page left by 16 steps (1 bar)
                piano_roll_scroll_step_ = std::max(0, piano_roll_scroll_step_ - 16);
            } else {
                // Page right by 16 steps (1 bar)
                piano_roll_scroll_step_ = std::min(max_scroll_step, piano_roll_scroll_step_ + 16);
            }
            status_message_ = "Piano Roll Timeline: Step " + std::to_string(piano_roll_scroll_step_ + 1) + " / " + std::to_string(max_steps);
            return;
        }

        // 4. Ruler SPM Click
        if (float(x) >= lay.grid_x && float(x) <= lay.grid_x + lay.grid_w &&
            float(y) >= lay.ruler_y && float(y) <= lay.ruler_y + lay.ruler_h) {
            dragging_spm_ = true;
            int rel_step = std::clamp(static_cast<int>((float(x) - lay.grid_x) / lay.step_w), 0, piano_roll_steps_ - 1);
            int abs_step = piano_roll_scroll_step_ + rel_step;
            song_position_marker_ = abs_step * step_ticks;
            engine_.transport().seek(song_position_marker_);
            status_message_ = "SPM set to Step " + std::to_string(abs_step + 1);
            return;
        }

        // 5. Piano Key Click (Audition Sound)
        if (float(x) >= lay.piano_x && float(x) <= lay.piano_x + lay.piano_w &&
            float(y) >= lay.grid_top && float(y) <= lay.grid_bottom) {
            int r = std::clamp(static_cast<int>((float(y) - lay.grid_top) / lay.row_h), 0, PianoRollNumPitches - 1);
            uint8_t pitch = static_cast<uint8_t>(piano_roll_base_pitch_ + (PianoRollNumPitches - 1 - r));
            audition_note(pitch);
            status_message_ = "Auditioning " + get_midi_note_name(pitch) + " (MIDI " + std::to_string(pitch) + ")";
            return;
        }

        // 6. Note Grid Interaction (Add, Move, or Resize Note)
        if (float(x) >= lay.grid_x && float(x) <= lay.grid_x + lay.grid_w &&
            float(y) >= lay.grid_top && float(y) <= lay.grid_bottom) {
            auto& proj = engine_.session().project();
            auto* pat = proj.get_pattern(1);
            if (!pat) return;
            auto& notes = pat->get_or_create_channel_notes(piano_roll_channel_);

            int min_vis_p = piano_roll_base_pitch_;
            int max_vis_p = piano_roll_base_pitch_ + PianoRollNumPitches - 1;
            domain::Tick view_start_tick = piano_roll_scroll_step_ * step_ticks;
            domain::Tick view_end_tick = (piano_roll_scroll_step_ + piano_roll_steps_) * step_ticks;

            // Check if an existing note was clicked
            bool hit_note = false;
            domain::Note clicked_note{};
            bool hit_resize = false;

            for (const auto& n : notes.notes()) {
                if (n.pitch < min_vis_p || n.pitch > max_vis_p) continue;
                domain::Tick n_end = n.start + n.length;
                if (n_end <= view_start_tick || n.start >= view_end_tick) continue;

                int r = max_vis_p - n.pitch;
                float ny = lay.grid_top + r * lay.row_h;
                float rel_start = float(n.start - view_start_tick) / float(step_ticks);
                float rel_len = float(n.length) / float(step_ticks);
                float nx = lay.grid_x + rel_start * lay.step_w;
                float nw = std::max(6.0f, rel_len * lay.step_w);

                if (float(x) >= nx && float(x) <= nx + nw && float(y) >= ny && float(y) <= ny + lay.row_h) {
                    hit_note = true;
                    clicked_note = n;
                    if (float(x) >= nx + nw - 10.0f) {
                        hit_resize = true;
                    }
                    break;
                }
            }

            if (hit_note) {
                if (hit_resize) {
                    note_drag_mode_ = NoteDragMode::Resize;
                    drag_note_orig_start_ = clicked_note.start;
                    drag_note_orig_pitch_ = clicked_note.pitch;
                    drag_note_cur_start_ = clicked_note.start;
                    drag_note_cur_pitch_ = clicked_note.pitch;
                    drag_note_orig_len_ = clicked_note.length;
                    drag_note_start_mouse_x_ = x;
                    drag_note_start_mouse_y_ = y;
                    status_message_ = "Resizing note " + get_midi_note_name(clicked_note.pitch) + " (Drag right/left to adjust length)";
                } else {
                    note_drag_mode_ = NoteDragMode::Move;
                    drag_note_orig_start_ = clicked_note.start;
                    drag_note_orig_pitch_ = clicked_note.pitch;
                    drag_note_cur_start_ = clicked_note.start;
                    drag_note_cur_pitch_ = clicked_note.pitch;
                    drag_note_orig_len_ = clicked_note.length;
                    drag_note_start_mouse_x_ = x;
                    drag_note_start_mouse_y_ = y;
                    audition_note(clicked_note.pitch);
                    status_message_ = "Moving note " + get_midi_note_name(clicked_note.pitch) + " (Drag across grid to move/pitch)";
                }
                return;
            }

            // Clicked empty space -> Add new note at clicked pitch and step!
            int rel_step = std::clamp(static_cast<int>((float(x) - lay.grid_x) / lay.step_w), 0, piano_roll_steps_ - 1);
            int r = std::clamp(static_cast<int>((float(y) - lay.grid_top) / lay.row_h), 0, PianoRollNumPitches - 1);
            uint8_t pitch = static_cast<uint8_t>(piano_roll_base_pitch_ + (PianoRollNumPitches - 1 - r));
            domain::Tick note_start = (piano_roll_scroll_step_ + rel_step) * step_ticks;
            domain::Tick note_len = piano_roll_note_len_steps_ * step_ticks;

            notes.add_note(domain::Note{note_start, note_len, pitch, 100, 0, 0});
            audition_note(pitch);

            // Immediately engage resize so user can lengthen note if desired
            note_drag_mode_ = NoteDragMode::Resize;
            drag_note_orig_start_ = note_start;
            drag_note_orig_pitch_ = pitch;
            drag_note_cur_start_ = note_start;
            drag_note_cur_pitch_ = pitch;
            drag_note_orig_len_ = note_len;
            drag_note_start_mouse_x_ = x;
            drag_note_start_mouse_y_ = y;

            status_message_ = "Added " + get_midi_note_name(pitch) + " (" + std::to_string(piano_roll_note_len_steps_) + " Steps). Drag edge to lengthen!";
            return;
        }
    }

    void handle_channel_rack_right_click(int x, int y) {
        float start_y = 124.0f;
        float row_h = 48.0f;
        float step_h_f = 42.0f;
        float rack_avail_h = (float(client_h_ - 268) - 26.0f) - start_y;
        int start_x = 265;
        int bars_per_view = get_bars_per_view();
        int total_seq_w = (get_effective_workspace_w() - 25) - start_x;
        auto ppq = engine_.session().project().time_map().ppq();
        auto bar_ticks = 4 * ppq;
        domain::Tick view_start_tick = sequencer_scroll_bar_ * bar_ticks;
        domain::Tick view_duration = bars_per_view * bar_ticks;
        domain::Tick view_end_tick = view_start_tick + view_duration;
        auto& proj = engine_.session().project();

        int visible_channels = std::max(1, static_cast<int>(rack_avail_h / row_h));
        size_t start_ch = static_cast<size_t>(sequencer_scroll_track_);
        size_t end_ch = std::min(proj.channels().size(), start_ch + static_cast<size_t>(visible_channels) + 1);

        for (size_t ch_idx = start_ch; ch_idx < end_ch; ++ch_idx) {
            float ch_y = start_y + static_cast<float>(ch_idx - start_ch) * row_h;
            int ch_top = static_cast<int>(ch_y);
            int ch_bot = static_cast<int>(ch_y + step_h_f);

            if (ch_idx < proj.tracks().size() && y >= ch_top && y <= ch_bot && x >= start_x && x <= start_x + total_seq_w) {
                auto& track = proj.tracks()[ch_idx];
                for (size_t ci = 0; ci < track.clips().size(); ++ci) {
                    const auto& clip = track.clips()[ci];
                    if (clip.end() <= view_start_tick || clip.start >= view_end_tick) continue;
                    double norm_start = double(clip.start - view_start_tick) / double(view_duration);
                    double norm_len = double(clip.length) / double(view_duration);
                    int cx = start_x + static_cast<int>(norm_start * total_seq_w);
                    int cw = std::max(16, static_cast<int>(norm_len * total_seq_w));

                    if (x >= cx && x <= cx + cw) {
                        track.remove_clip(ci);
                        status_message_ = "Deleted clip from " + proj.channels()[ch_idx].settings().name;
                        return;
                    }
                }
                return;
            }
        }
    }

    void on_passive_mouse_move(int x, int y) {
        // 1. Header Bar buttons hover indicator & contextual hints
        if (y >= 8 && y <= 40) {
            if (x >= 88 && x <= 198) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                std::string cur_name = engine_.session().project().name();
                if (cur_name.empty()) cur_name = "Untitled Project";
                status_message_ = "Project: " + cur_name;
                return;
            }
            if (x >= 204 && x <= 234) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                std::string u_lbl = engine_.session().undo_stack().last_undo_label();
                status_message_ = engine_.session().undo_stack().can_undo() ? ("Undo: " + u_lbl + " (Ctrl+Z)") : "Undo (Ctrl+Z) - Nothing to undo";
                return;
            }
            if (x >= 238 && x <= 268) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                std::string r_lbl = engine_.session().undo_stack().last_redo_label();
                status_message_ = engine_.session().undo_stack().can_redo() ? ("Redo: " + r_lbl + " (Ctrl+Y)") : "Redo (Ctrl+Y) - Nothing to redo";
                return;
            }
            if (x >= 276 && x <= 312) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                status_message_ = "Start Audio Playback (Space)";
                return;
            }
            if (x >= 316 && x <= 352) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                status_message_ = "Pause Audio Playback (Space)";
                return;
            }
            if (x >= 356 && x <= 392) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                status_message_ = "Stop Audio Playback & Reset Playhead (Esc)";
                return;
            }
            if (x >= 400 && x <= 424) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                status_message_ = "Decrease Project Tempo (-1 BPM)";
                return;
            }
            if (x >= 512 && x <= 536) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                status_message_ = "Increase Project Tempo (+1 BPM)";
                return;
            }
            if (x >= 684 && x <= 722) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                status_message_ = "Switch to Playlist Arrangement (F6)";
                return;
            }
            if (x >= 726 && x <= 764) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                status_message_ = "Switch to Piano Roll Editor (F7)";
                return;
            }
            if (x >= 768 && x <= 806) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                status_message_ = inspector_open_ ? "Collapse Inspector / Track FX Panel (F8 / Ctrl+I)" : "Open Inspector / Track FX Panel (F8 / Ctrl+I)";
                return;
            }
            if (x >= client_w_ - 80 && x <= client_w_ - 45) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                status_message_ = "Save Project (.odp) (Ctrl+S)";
                return;
            }
            if (x >= client_w_ - 41 && x <= client_w_ - 10) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                status_message_ = "Export Project to WAV Audio File";
                return;
            }
        }

        // 2. Right-Side Inspector Panel Hover
        int insp_x = client_w_ - (kInspectorWidth + kLayoutMargin);
        int insp_r = client_w_ - kLayoutMargin;
        int insp_y = 58;
        int insp_bot = client_h_ - 268;
        if (inspector_open_ && x >= insp_x && x <= insp_r && y >= insp_y && y <= insp_bot) {
            uint32_t tid = selected_mixer_track_;
            auto* track = engine_.session().project().mixer_graph().get_track(tid);
            // Close button [✕]
            if (x >= insp_r - 28 && x <= insp_r - 8 && y >= insp_y + 9 && y <= insp_y + 29) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                status_message_ = "Close Inspector / Track FX Panel (F8 / Ctrl+I)";
                return;
            }
            // Volume slider
            if (x >= insp_x + 14 && x <= insp_r - 14 && y >= insp_y + 90 && y <= insp_y + 108) {
                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                status_message_ = "Drag or Wheel to adjust Track Volume (Right-click resets to 0 dB)";
                return;
            }
            // Pan slider
            if (x >= insp_x + 14 && x <= insp_r - 14 && y >= insp_y + 112 && y <= insp_y + 130) {
                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                status_message_ = "Drag or Wheel to adjust Track Panning (Right-click resets to Center)";
                return;
            }
            // Mute & Solo buttons
            int mid_w = (insp_r - 14) - (insp_x + 14);
            int btn_w = (mid_w - 6) / 2;
            if (y >= insp_y + 134 && y <= insp_y + 154) {
                if (x >= insp_x + 14 && x <= insp_x + 14 + btn_w) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    status_message_ = (track && track->muted()) ? "Unmute Track" : "Mute Track";
                    return;
                }
                if (x >= insp_x + 14 + btn_w + 6 && x <= insp_r - 14) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    status_message_ = (track && track->solo()) ? "Unsolo Track" : "Solo Track";
                    return;
                }
            }
            // FX items & Add button & Scrollbar
            size_t num_fx = track ? track->inserts().size() : 0;
            int slot_y_start = insp_y + 192;
            int slot_h = 32;
            int slot_gap = 4;
            int fx_area_bot = insp_bot - 8;
            int fx_avail_h = fx_area_bot - slot_y_start;
            int vis_fx_items = std::max(1, fx_avail_h / (slot_h + slot_gap));
            int total_fx_items = static_cast<int>(num_fx) + (num_fx < 10 ? 1 : 0);
            int max_fx_scroll = std::max(0, total_fx_items - vis_fx_items);
            int right_margin = (max_fx_scroll > 0) ? 14 : 8;

            // Scrollbar hover
            if (max_fx_scroll > 0 && total_fx_items > 0) {
                if (x >= insp_r - 12 && x <= insp_r - 2 && y >= slot_y_start && y <= fx_area_bot) {
                    SetCursor(LoadCursor(NULL, IDC_SIZENS));
                    status_message_ = "Drag vertical scrollbar to view more FX inserts";
                    return;
                }
            }

            for (int i = inspector_scroll_slot_; i < static_cast<int>(num_fx); ++i) {
                int sy = slot_y_start + (i - inspector_scroll_slot_) * (slot_h + slot_gap);
                if (sy + slot_h > fx_area_bot) break;
                if (x >= insp_x + 12 && x <= insp_x + 36 && y >= sy + 4 && y <= sy + slot_h - 4) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    status_message_ = track->inserts()[i].enabled ? "Click to bypass effect" : "Click to enable effect";
                    return;
                }
                if (x >= insp_r - (right_margin + 80) && x <= insp_r - (right_margin + 26) && y >= sy + 5 && y <= sy + slot_h - 5) {
                    SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                    status_message_ = "Drag or Wheel to adjust Wet/Dry Mix (Right-click resets to 100%)";
                    return;
                }
                if (x >= insp_r - (right_margin + 22) && x <= insp_r - (right_margin + 4) && y >= sy + 5 && y <= sy + slot_h - 5) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    status_message_ = "Remove Insert Effect Slot";
                    return;
                }
                if (x >= insp_x + 40 && x <= insp_r - (right_margin + 84) && y >= sy + 2 && y <= sy + slot_h - 2) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    std::string name = track->inserts()[i].device ? track->inserts()[i].device->name() : "Effect";
                    status_message_ = "Insert " + std::to_string(i + 1) + ": " + name;
                    return;
                }
            }
            if (num_fx < 10) {
                int add_slot_idx = static_cast<int>(num_fx);
                if (add_slot_idx >= inspector_scroll_slot_) {
                    int add_btn_y = slot_y_start + (add_slot_idx - inspector_scroll_slot_) * (slot_h + slot_gap);
                    if (add_btn_y + 30 <= fx_area_bot && add_btn_y >= slot_y_start) {
                        if (x >= insp_x + 8 && x <= insp_r - right_margin && y >= add_btn_y + 2 && y <= add_btn_y + 30) {
                            SetCursor(LoadCursor(NULL, IDC_HAND));
                            status_message_ = "Add Insert Effect (EQ, Delay, Reverb, Compressor, Limiter)";
                            return;
                        }
                    }
                }
            }
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return;
        }
        if (view_mode_ == ViewMode::ChannelRack) {
            float start_y = 124.0f;
            float row_h = 48.0f;
            float step_h_f = 42.0f;
            float rack_avail_h = (float(client_h_ - 268) - 26.0f) - start_y;
            int start_x = 265;
            int bars_per_view = get_bars_per_view();
            int total_seq_w = (get_effective_workspace_w() - 25) - start_x;
            auto ppq = engine_.session().project().time_map().ppq();
            auto bar_ticks = 4 * ppq;
            domain::Tick view_start_tick = sequencer_scroll_bar_ * bar_ticks;
            domain::Tick view_duration = bars_per_view * bar_ticks;
            domain::Tick view_end_tick = view_start_tick + view_duration;
            auto& proj = engine_.session().project();

            int visible_channels = std::max(1, static_cast<int>(rack_avail_h / row_h));
            size_t start_ch = static_cast<size_t>(sequencer_scroll_track_);
            size_t end_ch = std::min(proj.channels().size(), start_ch + static_cast<size_t>(visible_channels) + 1);

            bool hovering_edge = false;
            for (size_t ch_idx = start_ch; ch_idx < end_ch; ++ch_idx) {
                float ch_y = start_y + static_cast<float>(ch_idx - start_ch) * row_h;
                int ch_top = static_cast<int>(ch_y);
                int ch_bot = static_cast<int>(ch_y + step_h_f);

                if (ch_idx < proj.tracks().size() && y >= ch_top && y <= ch_bot && x >= start_x && x <= start_x + total_seq_w) {
                    const auto& track = proj.tracks()[ch_idx];
                    for (const auto& clip : track.clips()) {
                        if (clip.end() <= view_start_tick || clip.start >= view_end_tick) continue;
                        double norm_start = double(clip.start - view_start_tick) / double(view_duration);
                        double norm_len = double(clip.length) / double(view_duration);
                        int cx = start_x + static_cast<int>(norm_start * total_seq_w);
                        int cw = std::max(16, static_cast<int>(norm_len * total_seq_w));

                        if (x >= cx + cw - 8 && x <= cx + cw + 4) {
                            hovering_edge = true;
                            break;
                        }
                    }
                    break;
                }
            }

            int rack_bottom = client_h_ - 268;
            int scroll_y = rack_bottom - 22;
            int scroll_h = 16;
            is_hovering_seq_scrollbar_ = (x >= start_x && x <= start_x + total_seq_w && y >= scroll_y - 2 && y <= scroll_y + scroll_h + 2);

            int max_ch_scroll = std::max(0, static_cast<int>(proj.channels().size()) - visible_channels + 1);
            is_hovering_seq_v_scrollbar_ = (max_ch_scroll > 0 && x >= 254 && x <= 264 && y >= start_y && y <= start_y + rack_avail_h);

            if (is_hovering_seq_v_scrollbar_) {
                SetCursor(LoadCursor(NULL, IDC_SIZENS));
                return;
            }

            if (is_hovering_seq_scrollbar_) {
                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                return;
            }

            if (is_hovering_clip_edge_ != hovering_edge) {
                is_hovering_clip_edge_ = hovering_edge;
                SetCursor(LoadCursor(NULL, hovering_edge ? IDC_SIZEWE : IDC_ARROW));
            }
        } else if (view_mode_ == ViewMode::PianoRoll) {
            PianoRollLayout lay;
            lay.init(get_effective_workspace_w(), client_h_, PianoRollNumPitches, piano_roll_steps_);
            auto ppq = engine_.session().project().time_map().ppq();
            auto step_ticks = ppq / 4;

            is_hovering_note_edge_ = false;
            is_hovering_note_body_ = false;
            is_hovering_piano_v_scrollbar_ = false;
            is_hovering_piano_h_scrollbar_ = false;

            // Check vertical scrollbar hover
            if (float(x) >= lay.v_scroll_x && float(x) <= lay.v_scroll_x + lay.v_scroll_w &&
                float(y) >= lay.grid_top && float(y) <= lay.grid_bottom) {
                is_hovering_piano_v_scrollbar_ = true;
                SetCursor(LoadCursor(NULL, IDC_SIZENS));
                return;
            }

            // Check horizontal scrollbar hover
            if (float(x) >= lay.grid_x && float(x) <= lay.grid_x + lay.grid_w &&
                float(y) >= lay.h_scroll_y && float(y) <= lay.h_scroll_y + lay.h_scroll_h) {
                is_hovering_piano_h_scrollbar_ = true;
                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                return;
            }

            // Check note body or resize edge hover
            if (float(x) >= lay.grid_x && float(x) <= lay.grid_x + lay.grid_w &&
                float(y) >= lay.grid_top && float(y) <= lay.grid_bottom) {
                auto& proj = engine_.session().project();
                auto* pat = proj.get_pattern(1);
                if (pat) {
                    auto* note_set = pat->get_channel_notes(piano_roll_channel_);
                    if (note_set) {
                        int min_vis_p = piano_roll_base_pitch_;
                        int max_vis_p = piano_roll_base_pitch_ + PianoRollNumPitches - 1;
                        domain::Tick view_start_tick = piano_roll_scroll_step_ * step_ticks;
                        domain::Tick view_end_tick = (piano_roll_scroll_step_ + piano_roll_steps_) * step_ticks;

                        for (const auto& n : note_set->notes()) {
                            if (n.pitch < min_vis_p || n.pitch > max_vis_p) continue;
                            domain::Tick n_end = n.start + n.length;
                            if (n_end <= view_start_tick || n.start >= view_end_tick) continue;

                            int r = max_vis_p - n.pitch;
                            float ny = lay.grid_top + r * lay.row_h;
                            float rel_start = float(n.start - view_start_tick) / float(step_ticks);
                            float rel_len = float(n.length) / float(step_ticks);
                            float nx = lay.grid_x + rel_start * lay.step_w;
                            float nw = std::max(6.0f, rel_len * lay.step_w);

                            if (float(x) >= nx && float(x) <= nx + nw && float(y) >= ny && float(y) <= ny + lay.row_h) {
                                if (float(x) >= nx + nw - 10.0f) {
                                    is_hovering_note_edge_ = true;
                                    SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                                } else {
                                    is_hovering_note_body_ = true;
                                    SetCursor(LoadCursor(NULL, IDC_SIZEALL));
                                }
                                return;
                            }
                        }
                    }
                }
            }
            SetCursor(LoadCursor(NULL, IDC_ARROW));
        }
    }

    void handle_piano_roll_right_click(int x, int y) {
        PianoRollLayout lay;
        lay.init(get_effective_workspace_w(), client_h_, PianoRollNumPitches, piano_roll_steps_);
        auto ppq = engine_.session().project().time_map().ppq();
        auto step_ticks = ppq / 4;

        if (float(x) >= lay.grid_x && float(x) <= lay.grid_x + lay.grid_w &&
            float(y) >= lay.grid_top && float(y) <= lay.grid_bottom) {
            auto& proj = engine_.session().project();
            auto* pat = proj.get_pattern(1);
            if (pat) {
                auto& notes = pat->get_or_create_channel_notes(piano_roll_channel_);
                int min_vis_p = piano_roll_base_pitch_;
                int max_vis_p = piano_roll_base_pitch_ + PianoRollNumPitches - 1;
                domain::Tick view_start_tick = piano_roll_scroll_step_ * step_ticks;
                domain::Tick view_end_tick = (piano_roll_scroll_step_ + piano_roll_steps_) * step_ticks;

                for (const auto& n : notes.notes()) {
                    if (n.pitch < min_vis_p || n.pitch > max_vis_p) continue;
                    domain::Tick n_end = n.start + n.length;
                    if (n_end <= view_start_tick || n.start >= view_end_tick) continue;

                    int r = max_vis_p - n.pitch;
                    float ny = lay.grid_top + r * lay.row_h;
                    float rel_start = float(n.start - view_start_tick) / float(step_ticks);
                    float rel_len = float(n.length) / float(step_ticks);
                    float nx = lay.grid_x + rel_start * lay.step_w;
                    float nw = std::max(6.0f, rel_len * lay.step_w);

                    if (float(x) >= nx && float(x) <= nx + nw && float(y) >= ny && float(y) <= ny + lay.row_h) {
                        notes.remove_note(n.start, n.pitch);
                        status_message_ = "Removed note " + get_midi_note_name(n.pitch);
                        return;
                    }
                }
            }
        }
    }

    void on_right_click(int x, int y) {
        std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
        if (y >= client_h_ - 262 && y <= client_h_ - 30) {
            handle_mixer_right_click(x, y);
            return;
        }
        if (inspector_open_ && x >= client_w_ - (kInspectorWidth + kLayoutMargin) && x <= client_w_ - kLayoutMargin && y >= 58 && y <= client_h_ - 268) {
            handle_inspector_right_click(x, y);
            return;
        }
        if (view_mode_ == ViewMode::ChannelRack) {
            handle_channel_rack_right_click(x, y);
        } else {
            handle_piano_roll_right_click(x, y);
        }
    }

    void on_mouse_move(int x, int y) {
        if (dragging_inspector_vol_) {
            std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
            int insp_x = client_w_ - (kInspectorWidth + kLayoutMargin);
            int insp_r = client_w_ - kLayoutMargin;
            float norm = float(x - (insp_x + 14)) / float((insp_r - 14) - (insp_x + 14));
            norm = std::clamp(norm, 0.0f, 1.0f);
            auto* track = engine_.session().project().mixer_graph().get_track(selected_mixer_track_);
            if (track) {
                track->set_volume(norm * 1.25f);
                int vol_pct = static_cast<int>(std::round(track->volume() * 100.0f));
                status_message_ = (selected_mixer_track_ == 0 ? "Master" : ("Track " + std::to_string(selected_mixer_track_))) +
                                  " Volume: " + std::to_string(vol_pct) + "%";
            }
            return;
        }

        if (dragging_inspector_pan_) {
            std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
            int insp_x = client_w_ - (kInspectorWidth + kLayoutMargin);
            int insp_r = client_w_ - kLayoutMargin;
            float norm = float(x - (insp_x + 14)) / float((insp_r - 14) - (insp_x + 14));
            float new_pan = std::clamp(norm * 2.0f - 1.0f, -1.0f, 1.0f);
            auto* track = engine_.session().project().mixer_graph().get_track(selected_mixer_track_);
            if (track) {
                track->set_pan(new_pan);
                std::string pan_str = (std::abs(new_pan) < 0.02f) ? "Center" :
                    (new_pan < 0.0f ? "Left " + std::to_string(static_cast<int>(std::round(-new_pan * 100.0f))) + "%"
                                    : "Right " + std::to_string(static_cast<int>(std::round(new_pan * 100.0f))) + "%");
                status_message_ = (selected_mixer_track_ == 0 ? "Master" : ("Track " + std::to_string(selected_mixer_track_))) +
                                  " Pan: " + pan_str;
            }
            return;
        }

        if (dragging_inspector_scrollbar_) {
            int insp_y = 58;
            int insp_bot = client_h_ - 268;
            float slot_y_start = static_cast<float>(insp_y + 192);
            float fx_area_bot = static_cast<float>(insp_bot - 8);
            float sb_h = fx_area_bot - slot_y_start;
            auto* track = engine_.session().project().mixer_graph().get_track(selected_mixer_track_);
            size_t num_fx = track ? track->inserts().size() : 0;
            float slot_h = 32.0f;
            float slot_gap = 4.0f;
            int vis_fx_items = std::max(1, static_cast<int>(sb_h / (slot_h + slot_gap)));
            int total_fx_items = static_cast<int>(num_fx) + (num_fx < 10 ? 1 : 0);
            int max_fx_scroll = std::max(0, total_fx_items - vis_fx_items);
            if (max_fx_scroll > 0 && total_fx_items > 0) {
                float thumb_h = std::max(20.0f, sb_h * (static_cast<float>(vis_fx_items) / static_cast<float>(total_fx_items)));
                float travel = sb_h - thumb_h;
                if (travel > 1.0f) {
                    float dy = static_cast<float>(y - drag_inspector_scroll_start_y_);
                    int delta_slots = static_cast<int>(std::round((dy / travel) * static_cast<float>(max_fx_scroll)));
                    inspector_scroll_slot_ = std::clamp(drag_inspector_scroll_orig_slot_ + delta_slots, 0, max_fx_scroll);
                }
            }
            return;
        }

        if (dragging_inspector_wet_slot_ >= 0) {
            std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
            int insp_y = 58;
            int insp_bot = client_h_ - 268;
            int slot_y_start = insp_y + 192;
            int slot_h = 32;
            int slot_gap = 4;
            int fx_area_bot = insp_bot - 8;
            int fx_avail_h = fx_area_bot - slot_y_start;
            int vis_fx_items = std::max(1, fx_avail_h / (slot_h + slot_gap));
            auto* track = engine_.session().project().mixer_graph().get_track(selected_mixer_track_);
            size_t num_fx = track ? track->inserts().size() : 0;
            int total_fx_items = static_cast<int>(num_fx) + (num_fx < 10 ? 1 : 0);
            int max_fx_scroll = std::max(0, total_fx_items - vis_fx_items);
            int right_margin = (max_fx_scroll > 0) ? 14 : 8;

            int insp_r = client_w_ - kLayoutMargin;
            float norm = float(x - (insp_r - (right_margin + 80))) / 54.0f;
            norm = std::clamp(norm, 0.0f, 1.0f);
            if (track && static_cast<size_t>(dragging_inspector_wet_slot_) < track->inserts().size()) {
                track->inserts()[dragging_inspector_wet_slot_].wet_mix = norm;
                int pct = static_cast<int>(std::round(norm * 100.0f));
                std::string name = track->inserts()[dragging_inspector_wet_slot_].device ?
                    track->inserts()[dragging_inspector_wet_slot_].device->name() : "Effect";
                status_message_ = name + " Wet Mix: " + std::to_string(pct) + "%";
            }
            return;
        }
        if (dragging_seq_v_scrollbar_) {
            float start_y = 124.0f;
            float row_h = 48.0f;
            float rack_avail_h = (float(client_h_ - 268) - 26.0f) - start_y;
            auto& proj = engine_.session().project();
            int visible_channels = std::max(1, static_cast<int>(rack_avail_h / row_h));
            int max_ch_scroll = std::max(0, static_cast<int>(proj.channels().size()) - visible_channels + 1);

            if (max_ch_scroll > 0) {
                float v_thumb_h = std::max(20.0f, (float(visible_channels) / float(proj.channels().size() + 1)) * rack_avail_h);
                float avail_scroll_h = rack_avail_h - v_thumb_h;
                if (avail_scroll_h > 0.0f) {
                    float dy = float(y) - drag_seq_v_scroll_start_y_;
                    float delta_ch = (dy / avail_scroll_h) * float(max_ch_scroll);
                    int new_scroll = std::clamp(static_cast<int>(std::round(float(drag_seq_v_scroll_orig_track_) + delta_ch)), 0, max_ch_scroll);
                    if (new_scroll != sequencer_scroll_track_) {
                        sequencer_scroll_track_ = new_scroll;
                    }
                }
            }
            return;
        }

        if (dragging_seq_scrollbar_) {
            int max_bars = get_max_sequencer_bars();
            int bars_per_view = get_bars_per_view();
            float max_scroll = std::max(1.0f, float(max_bars - bars_per_view));
            int start_x = 265;
            int total_seq_w = (get_effective_workspace_w() - 25) - start_x;
            float thumb_w = std::max(35.0f, (float(bars_per_view) / float(max_bars)) * float(total_seq_w));
            float available_w = float(total_seq_w) - thumb_w;

            if (available_w > 0.0f) {
                float dx = float(x) - drag_seq_scroll_start_mouse_x_;
                float delta_bars = (dx / available_w) * max_scroll;
                int new_bar = std::clamp(static_cast<int>(std::round(float(drag_seq_scroll_orig_bar_) + delta_bars)), 0, static_cast<int>(max_scroll));
                if (new_bar != sequencer_scroll_bar_) {
                    sequencer_scroll_bar_ = new_bar;
                    status_message_ = "Scrolled timeline to Bar " + std::to_string(sequencer_scroll_bar_ + 1) + " / " + std::to_string(max_bars);
                }
            }
            return;
        }

        if (dragging_piano_v_scrollbar_) {
            PianoRollLayout lay;
            lay.init(get_effective_workspace_w(), client_h_, PianoRollNumPitches, piano_roll_steps_);
            int max_base_pitch = 128 - PianoRollNumPitches;
            float thumb_h = std::max(28.0f, (float(PianoRollNumPitches) / 128.0f) * lay.grid_h);
            float avail_h = lay.grid_h - thumb_h;
            if (avail_h > 0.0f) {
                float dy = float(y) - drag_piano_v_scroll_start_y_;
                float delta_ratio = dy / avail_h;
                int delta_pitch = static_cast<int>(std::round(-delta_ratio * float(max_base_pitch)));
                int new_pitch = std::clamp(drag_piano_v_scroll_orig_pitch_ + delta_pitch, 0, max_base_pitch);
                if (new_pitch != piano_roll_base_pitch_) {
                    piano_roll_base_pitch_ = new_pitch;
                    status_message_ = "Pitch Range: " + get_midi_note_name(static_cast<uint8_t>(piano_roll_base_pitch_)) +
                                      " to " + get_midi_note_name(static_cast<uint8_t>(piano_roll_base_pitch_ + PianoRollNumPitches - 1));
                }
            }
            return;
        }

        if (dragging_piano_h_scrollbar_) {
            PianoRollLayout lay;
            lay.init(get_effective_workspace_w(), client_h_, PianoRollNumPitches, piano_roll_steps_);
            int max_steps = get_max_piano_roll_steps();
            int max_scroll_step = std::max(0, max_steps - piano_roll_steps_);
            float thumb_w = std::max(35.0f, (float(piano_roll_steps_) / float(max_steps)) * lay.grid_w);
            float avail_w = lay.grid_w - thumb_w;
            if (avail_w > 0.0f) {
                float dx = float(x) - drag_piano_h_scroll_start_x_;
                float delta_steps = (dx / avail_w) * float(max_scroll_step);
                int new_step = std::clamp(drag_piano_h_scroll_orig_step_ + static_cast<int>(std::round(delta_steps)), 0, max_scroll_step);
                if (new_step != piano_roll_scroll_step_) {
                    piano_roll_scroll_step_ = new_step;
                    status_message_ = "Piano Roll Timeline: Step " + std::to_string(piano_roll_scroll_step_ + 1) + " / " + std::to_string(max_steps);
                }
            }
            return;
        }

        if (clip_drag_mode_ == ClipDragMode::Move) {
            auto& proj = engine_.session().project();
            if (drag_clip_track_idx_ < proj.tracks().size()) {
                auto& track = proj.tracks()[drag_clip_track_idx_];
                if (drag_clip_idx_ < track.clips().size()) {
                    auto& clip = track.clips_mut()[drag_clip_idx_];
                    auto ppq = engine_.session().project().time_map().ppq();
                    auto bar_ticks = 4 * ppq;
                    int bars_per_view = get_bars_per_view();
                    int total_seq_w = (get_effective_workspace_w() - 25) - 265;
                    domain::Tick view_duration = bars_per_view * bar_ticks;

                    int dx = x - drag_clip_start_mouse_x_;
                    double ticks_per_px = double(view_duration) / double(total_seq_w);
                    int64_t delta_ticks = static_cast<int64_t>(dx * ticks_per_px);
                    domain::Tick beat_ticks = ppq; // Snap to each beat (ketukan)

                    int64_t snapped_delta = (delta_ticks >= 0)
                        ? ((delta_ticks + beat_ticks / 2) / beat_ticks * beat_ticks)
                        : ((delta_ticks - beat_ticks / 2) / beat_ticks * beat_ticks);

                    int64_t new_start = std::max(int64_t(0), static_cast<int64_t>(drag_clip_orig_start_) + snapped_delta);
                    clip.start = static_cast<domain::Tick>(new_start);

                    int b_num = static_cast<int>(new_start / bar_ticks) + 1;
                    int bt_num = static_cast<int>((new_start % bar_ticks) / beat_ticks) + 1;
                    status_message_ = "Moved clip to Bar " + std::to_string(b_num) + " Beat " + std::to_string(bt_num);
                }
            }
            return;
        }

        if (clip_drag_mode_ == ClipDragMode::Resize) {
            auto& proj = engine_.session().project();
            if (drag_clip_track_idx_ < proj.tracks().size()) {
                auto& track = proj.tracks()[drag_clip_track_idx_];
                if (drag_clip_idx_ < track.clips().size()) {
                    auto& clip = track.clips_mut()[drag_clip_idx_];
                    auto ppq = engine_.session().project().time_map().ppq();
                    auto bar_ticks = 4 * ppq;
                    int bars_per_view = get_bars_per_view();
                    int total_seq_w = (get_effective_workspace_w() - 25) - 265;
                    domain::Tick view_duration = bars_per_view * bar_ticks;

                    int dx = x - drag_clip_start_mouse_x_;
                    double ticks_per_px = double(view_duration) / double(total_seq_w);
                    int64_t delta_ticks = static_cast<int64_t>(dx * ticks_per_px);
                    domain::Tick beat_ticks = ppq; // Snap to each beat (ketukan)

                    int64_t snapped_delta = (delta_ticks >= 0)
                        ? ((delta_ticks + beat_ticks / 2) / beat_ticks * beat_ticks)
                        : ((delta_ticks - beat_ticks / 2) / beat_ticks * beat_ticks);

                    int64_t new_len = std::max(int64_t(beat_ticks), static_cast<int64_t>(drag_clip_orig_len_) + snapped_delta);
                    clip.length = static_cast<domain::Tick>(new_len);

                    int beats_total = static_cast<int>(new_len / beat_ticks);
                    int bars_total = static_cast<int>(new_len / bar_ticks);
                    int rem_beats = static_cast<int>((new_len % bar_ticks) / beat_ticks);
                    status_message_ = "Resized clip to " + std::to_string(bars_total) + " Bars " +
                                      std::to_string(rem_beats) + " Beats (" + std::to_string(beats_total) + " Total Beats)";
                }
            }
            return;
        }

        if (note_drag_mode_ == NoteDragMode::Resize) {
            PianoRollLayout lay;
            lay.init(get_effective_workspace_w(), client_h_, PianoRollNumPitches, piano_roll_steps_);
            auto ppq = engine_.session().project().time_map().ppq();
            auto step_ticks = ppq / 4;

            int dx = x - drag_note_start_mouse_x_;
            int delta_steps = static_cast<int>(std::round(float(dx) / lay.step_w));
            int orig_steps = std::max(1, static_cast<int>(drag_note_orig_len_ / step_ticks));
            int new_steps = std::max(1, orig_steps + delta_steps);
            domain::Tick new_len = new_steps * step_ticks;

            auto& proj = engine_.session().project();
            auto* pat = proj.get_pattern(1);
            if (pat) {
                auto& notes = pat->get_or_create_channel_notes(piano_roll_channel_);
                notes.set_note_length(drag_note_cur_start_, drag_note_cur_pitch_, new_len);
                status_message_ = "Note " + get_midi_note_name(drag_note_cur_pitch_) +
                                  " Length: " + std::to_string(new_steps) + " Steps (" +
                                  std::to_string(new_len) + " Ticks)";
            }
            return;
        }

        if (note_drag_mode_ == NoteDragMode::Move) {
            PianoRollLayout lay;
            lay.init(get_effective_workspace_w(), client_h_, PianoRollNumPitches, piano_roll_steps_);
            auto ppq = engine_.session().project().time_map().ppq();
            auto step_ticks = ppq / 4;

            int dx = x - drag_note_start_mouse_x_;
            int dy = y - drag_note_start_mouse_y_;
            int delta_steps = static_cast<int>(std::round(float(dx) / lay.step_w));
            int delta_pitch = -static_cast<int>(std::round(float(dy) / lay.row_h));

            int64_t target_start_int = static_cast<int64_t>(drag_note_orig_start_) + delta_steps * step_ticks;
            domain::Tick target_start = std::max(domain::Tick(0), static_cast<domain::Tick>(target_start_int));
            int target_pitch_int = std::clamp(static_cast<int>(drag_note_orig_pitch_) + delta_pitch, 0, 127);
            uint8_t target_pitch = static_cast<uint8_t>(target_pitch_int);

            if (target_start != drag_note_cur_start_ || target_pitch != drag_note_cur_pitch_) {
                auto& proj = engine_.session().project();
                auto* pat = proj.get_pattern(1);
                if (pat) {
                    auto& notes = pat->get_or_create_channel_notes(piano_roll_channel_);
                    if (notes.move_note(drag_note_cur_start_, drag_note_cur_pitch_, target_start, target_pitch)) {
                        if (target_pitch != drag_note_cur_pitch_) {
                            audition_note(target_pitch);
                        }
                        drag_note_cur_start_ = target_start;
                        drag_note_cur_pitch_ = target_pitch;
                        int s_num = static_cast<int>(target_start / step_ticks) + 1;
                        status_message_ = "Moved note to " + get_midi_note_name(target_pitch) + " (Step " + std::to_string(s_num) + ")";
                    }
                }
            }
            return;
        }

        if (dragging_spm_) {
            auto ppq = engine_.session().project().time_map().ppq();
            auto bar_ticks = 4 * ppq;
            auto step_ticks = ppq / 4;

            if (view_mode_ == ViewMode::ChannelRack) {
                int bars_per_view = get_bars_per_view();
                int total_seq_w = (get_effective_workspace_w() - 25) - 265;
                domain::Tick view_duration = bars_per_view * bar_ticks;
                domain::Tick view_start_tick = sequencer_scroll_bar_ * bar_ticks;

                double norm_x = double(x - 265) / double(total_seq_w);
                domain::Tick raw_tick = view_start_tick + static_cast<domain::Tick>(norm_x * view_duration);
                domain::Tick beat_ticks = ppq;
                song_position_marker_ = std::max(domain::Tick(0), (raw_tick / beat_ticks) * beat_ticks);

                int b_num = static_cast<int>(song_position_marker_ / bar_ticks) + 1;
                int bt_num = static_cast<int>((song_position_marker_ % bar_ticks) / beat_ticks) + 1;
                status_message_ = "SPM dragged to Bar " + std::to_string(b_num) + " Beat " + std::to_string(bt_num);
            } else {
                PianoRollLayout lay;
                lay.init(get_effective_workspace_w(), client_h_, PianoRollNumPitches, piano_roll_steps_);
                int rel_s = std::clamp(static_cast<int>((float(x) - lay.grid_x) / lay.step_w), 0, piano_roll_steps_ - 1);
                int abs_s = piano_roll_scroll_step_ + rel_s;
                song_position_marker_ = abs_s * step_ticks;
                status_message_ = "SPM dragged to Step " + std::to_string(abs_s + 1);
            }

            engine_.transport().seek(song_position_marker_);
            return;
        }

        if (dragging_mixer_pan_track_ >= 0) {
            float dy = float(pan_drag_start_y_ - y);
            float new_pan = std::clamp(pan_drag_start_val_ + dy / 75.0f, -1.0f, 1.0f);
            auto* trk = engine_.session().project().mixer_graph().get_track(dragging_mixer_pan_track_);
            if (trk) {
                trk->set_pan(new_pan);
                std::string pan_str;
                if (std::abs(new_pan) < 0.02f) {
                    pan_str = "Center";
                } else if (new_pan < 0.0f) {
                    int pct = static_cast<int>(std::round(-new_pan * 100.0f));
                    pan_str = "Left " + std::to_string(pct) + "%";
                } else {
                    int pct = static_cast<int>(std::round(new_pan * 100.0f));
                    pan_str = "Right " + std::to_string(pct) + "%";
                }
                std::string name = (dragging_mixer_pan_track_ == 0) ? "Master" : ("Track " + std::to_string(dragging_mixer_pan_track_));
                status_message_ = name + " Pan: " + pan_str;
            }
            return;
        }

        if (dragging_mixer_track_ >= 0) {
            float track_w = 96.0f;
            float gap = 12.0f;
            float ty = static_cast<float>(client_h_ - 234);
            float insert_start_x = 25.0f + track_w + gap;
            float tx = (dragging_mixer_track_ == 0)
                ? 25.0f
                : (insert_start_x + static_cast<float>(dragging_mixer_track_ - 1 - mixer_scroll_track_) * (track_w + gap));

            RECT fader_rc{static_cast<int>(tx + 33.0f), static_cast<int>(ty + 46.0f),
                          static_cast<int>(tx + 88.0f), static_cast<int>(ty + 144.0f)};
            update_mixer_volume_from_mouse(dragging_mixer_track_, y, fader_rc);
            return;
        }
    }

    void update_mixer_volume_from_mouse(int track_idx, int y, const RECT& fader_rc) {
        float track_top = float(fader_rc.top + 6);
        float track_bottom = float(fader_rc.bottom - 22);
        float track_h = track_bottom - track_top;
        float norm = (track_bottom - float(y)) / std::max(1.0f, track_h);
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
        engine_.transport().seek(song_position_marker_);
        engine_.transport().play();
        status_message_ = "Playback started from Song Position Marker (SPM)";
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
        std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
        auto& proj = engine_.session().project();
        domain::ChannelSettings s;
        size_t next_idx = proj.channels().size() + 1;
        s.name = "3xOsc Synth #" + std::to_string(next_idx);
        s.volume = 0.8f;
        s.mixer_track = static_cast<uint8_t>(std::min(size_t(63), next_idx));
        auto new_cid = proj.add_channel("core.generator.3xosc", s);

        while (proj.tracks().size() < proj.channels().size()) {
            domain::TrackId tid = static_cast<domain::TrackId>(proj.tracks().size() + 1);
            proj.tracks().emplace_back(tid, "Track " + std::to_string(tid));
        }

        proj.mixer_graph().add_track(s.mixer_track, "Track " + std::to_string(s.mixer_track));

        // Pre-instantiate device on GUI thread so audio thread never has to allocate or load plugin during playback
        engine_.get_or_create_channel_device(new_cid);

        // Auto scroll sequencer tracks to make the newly added channel visible
        float row_h = 48.0f;
        float rack_avail_h = (static_cast<float>(client_h_ - 268) - 26.0f) - 124.0f;
        int visible_channels = std::max(1, static_cast<int>(rack_avail_h / row_h));
        int max_ch_scroll = std::max(0, static_cast<int>(proj.channels().size()) - visible_channels + 1);
        sequencer_scroll_track_ = max_ch_scroll;

        selected_mixer_track_ = s.mixer_track;

        status_message_ = "Added channel: " + s.name + " (Mapped to Track " + std::to_string(s.mixer_track) + ")";
    }

    void do_undo() {
        std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
        dragging_inspector_wet_slot_ = -1;
        if (engine_.session().undo_stack().undo()) {
            auto* trk = engine_.session().project().mixer_graph().get_track(selected_mixer_track_);
            size_t num = trk ? trk->inserts().size() : 0;
            int total_fx_items = static_cast<int>(num) + (num < 10 ? 1 : 0);
            int vis_items = std::max(1, static_cast<int>(((client_h_ - 268 - 8) - (58 + 192)) / 36));
            int max_scroll = std::max(0, total_fx_items - vis_items);
            inspector_scroll_slot_ = std::clamp(inspector_scroll_slot_, 0, max_scroll);
            status_message_ = "Undo: " + engine_.session().undo_stack().last_redo_label();
        } else {
            status_message_ = "Nothing to undo";
        }
    }

    void do_redo() {
        std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
        dragging_inspector_wet_slot_ = -1;
        if (engine_.session().undo_stack().redo()) {
            auto* trk = engine_.session().project().mixer_graph().get_track(selected_mixer_track_);
            size_t num = trk ? trk->inserts().size() : 0;
            int total_fx_items = static_cast<int>(num) + (num < 10 ? 1 : 0);
            int vis_items = std::max(1, static_cast<int>(((client_h_ - 268 - 8) - (58 + 192)) / 36));
            int max_scroll = std::max(0, total_fx_items - vis_items);
            inspector_scroll_slot_ = std::clamp(inspector_scroll_slot_, 0, max_scroll);
            status_message_ = "Redo: " + engine_.session().undo_stack().last_undo_label();
        } else {
            status_message_ = "Nothing to redo";
        }
    }

    void insert_effect_to_track(uint32_t tid, const std::string& uid) {
        std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
        auto* trk = engine_.session().project().mixer_graph().get_track(tid);
        if (!trk && tid > 0) {
            engine_.session().project().mixer_graph().add_track(tid, "Track " + std::to_string(tid));
            trk = engine_.session().project().mixer_graph().get_track(tid);
        }
        if (!trk) return;
        if (trk->inserts().size() >= 10) {
            status_message_ = "Insert limit reached (max 10 inserts per track)";
            return;
        }
        auto res = engine_.plugin_manager().instantiate(uid);
        if (res.is_ok()) {
            auto dev = res.value();
            dev->prepare(44100.0, 512);
            std::string name = dev->name();
            engine_.session().undo_stack().push_and_execute(
                std::make_unique<InsertEffectCommand>(engine_.session().project().mixer_graph(), tid, dev));
            size_t new_fx_count = trk->inserts().size();
            int total_fx_items = static_cast<int>(new_fx_count) + (new_fx_count < 10 ? 1 : 0);
            int vis_items = std::max(1, static_cast<int>(((client_h_ - 268 - 8) - (58 + 192)) / 36));
            inspector_scroll_slot_ = std::max(0, total_fx_items - vis_items);
            status_message_ = "Added " + name + " to " + (tid == 0 ? "Master" : ("Track " + std::to_string(tid)));
        } else {
            status_message_ = "Failed to load effect plugin: " + uid;
        }
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
    int dragging_mixer_pan_track_{-1};
    int pan_drag_start_y_{0};
    float pan_drag_start_val_{0.0f};

    // Inspector & Mixer Selection State
    bool inspector_open_{true};
    uint32_t selected_mixer_track_{1};
    bool dragging_inspector_vol_{false};
    bool dragging_inspector_pan_{false};
    int dragging_inspector_wet_slot_{-1};
    int inspector_scroll_slot_{0};
    bool dragging_inspector_scrollbar_{false};
    int drag_inspector_scroll_start_y_{0};
    int drag_inspector_scroll_orig_slot_{0};

    // View Mode & Piano Roll State
    ViewMode view_mode_{ViewMode::ChannelRack};
    domain::ChannelId piano_roll_channel_{1};
    int piano_roll_base_pitch_{48}; // C4 root
    static constexpr int PianoRollNumPitches = 20; // 20 semitones visible
    int piano_roll_steps_{16}; // 16 or 32 steps visible
    int piano_roll_scroll_step_{0}; // Horizontal step scroll offset
    int piano_roll_note_len_steps_{1}; // 1, 2, 4, 8 steps default
    bool dragging_piano_v_scrollbar_{false};
    float drag_piano_v_scroll_start_y_{0.0f};
    int drag_piano_v_scroll_orig_pitch_{0};
    bool dragging_piano_h_scrollbar_{false};
    float drag_piano_h_scroll_start_x_{0.0f};
    int drag_piano_h_scroll_orig_step_{0};
    bool is_hovering_piano_v_scrollbar_{false};
    bool is_hovering_piano_h_scrollbar_{false};

    // Note Interaction Drag State (Move / Resize)
    enum class NoteDragMode {
        None,
        Move,
        Resize
    };
    NoteDragMode note_drag_mode_{NoteDragMode::None};
    domain::Tick drag_note_orig_start_{0};
    uint8_t drag_note_orig_pitch_{0};
    domain::Tick drag_note_cur_start_{0};
    uint8_t drag_note_cur_pitch_{0};
    domain::Tick drag_note_orig_len_{0};
    int drag_note_start_mouse_x_{0};
    int drag_note_start_mouse_y_{0};
    bool is_hovering_note_edge_{false};
    bool is_hovering_note_body_{false};

    // Sequencer & Mixer State
    int sequencer_scroll_bar_{0}; // Horizontal bar offset (Bar 1, Bar 5, etc.)
    int sequencer_scroll_track_{0}; // Vertical channel track offset
    int mixer_scroll_track_{0}; // Horizontal track scroll offset for mixer insert tracks
    bool dragging_seq_scrollbar_{false};
    float drag_seq_scroll_start_mouse_x_{0.0f};
    int drag_seq_scroll_orig_bar_{0};
    bool is_hovering_seq_scrollbar_{false};
    bool dragging_seq_v_scrollbar_{false};
    float drag_seq_v_scroll_start_y_{0.0f};
    int drag_seq_v_scroll_orig_track_{0};
    bool is_hovering_seq_v_scrollbar_{false};
    bool is_fullscreen_{false};
    RECT saved_win_rect_{};
    DWORD saved_win_style_{0};
    std::array<float, 64> meter_peaks_l_{0.0f}; // Real-time stereo Left audio peaks (tracks 0..63)
    std::array<float, 64> meter_peaks_r_{0.0f}; // Real-time stereo Right audio peaks (tracks 0..63)
    std::array<float, 16> meter_spectrum_{0.0f}; // Real-time analog spectrum energy (16 bands)
    std::array<float, 16> meter_spectrum_peak_{0.0f}; // Analog peak hold dots
    std::array<float, 256> meter_waveform_{0.0f}; // Real-time oscilloscope trace

    // Playlist Arranger Clip Dragging State
    enum class ClipDragMode {
        None,
        Move,
        Resize
    };
    ClipDragMode clip_drag_mode_{ClipDragMode::None};
    size_t drag_clip_track_idx_{0};
    size_t drag_clip_idx_{0};
    domain::Tick drag_clip_orig_start_{0};
    domain::Tick drag_clip_orig_len_{0};
    int drag_clip_start_mouse_x_{0};
    bool is_hovering_clip_edge_{false};

    // Direct3D 11 & DXGI Pipeline
    ID3D11Device* d3d_device_{nullptr};
    ID3D11DeviceContext* d3d_context_{nullptr};
    IDXGISwapChain* swap_chain_{nullptr};
    ID3D11RenderTargetView* d3d_rtv_{nullptr};
    D3DShaderVisualizer shader_visualizer_;

    // Direct2D & DirectWrite Pipeline
    ID2D1Factory* d2d_factory_{nullptr};
    ID2D1RenderTarget* d2d_target_{nullptr};
    IDWriteFactory* dwrite_factory_{nullptr};
    IDWriteTextFormat* dwrite_main_{nullptr};
    IDWriteTextFormat* dwrite_bold_{nullptr};
    IDWriteTextFormat* dwrite_title_{nullptr};
    IDWriteTextFormat* dwrite_small_{nullptr};
    bool use_d2d_d3d_{false};
};

inline DigiDawWindow* DigiDawWindow::instance = nullptr;

} // namespace digidaw::adapters::gui
