#pragma once

#include "../../app/engine.hpp"
#include "theme.hpp"
#include "gui_renderer.hpp"
#include "d2d_renderer.hpp"
#include "d3d_shader_visualizer.hpp"
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

    bool create_and_show(HINSTANCE hinst, int width = 1150, int height = 780) {
        hinst_ = hinst;

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
            WS_OVERLAPPEDWINDOW | WS_MAXIMIZE | WS_VISIBLE,
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
            status_message_ = "Direct2D 1.1 + Direct3D 11 Shaders Active (Hardware Accelerated)";
        } else {
            status_message_ = "Running on GDI Double-Buffered Fallback";
        }

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

    int get_bars_per_view() const {
        if (client_w_ > 1600) return 16;
        if (client_w_ > 1300) return 12;
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

            case WM_TIMER: {
                for (size_t i = 0; i <= 4; ++i) {
                    float raw_p = engine_.get_track_peak(i);
                    meter_peaks_[i] = std::max(raw_p, meter_peaks_[i] * 0.85f);
                }

                // Update Real-Time Audio Signal Oscilloscope Waveform & Spectrum
                std::array<float, 16> raw_spec{};
                engine_.get_spectrum(raw_spec);

                float master_p = meter_peaks_[0];
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
                } else if (view_mode_ == ViewMode::ChannelRack) {
                    on_passive_mouse_move(mouse_x, mouse_y);
                }
                return 0;
            }

            case WM_SETCURSOR: {
                if (view_mode_ == ViewMode::ChannelRack) {
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
                }
                return DefWindowProc(hwnd, msg, wp, lp);
            }

            case WM_LBUTTONUP: {
                is_mouse_down_ = false;
                dragging_spm_ = false;
                dragging_seq_scrollbar_ = false;
                dragging_mixer_track_ = -1;
                resizing_note_ = false;
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
                if (view_mode_ == ViewMode::PianoRoll) {
                    piano_roll_base_pitch_ = std::clamp(piano_roll_base_pitch_ + steps * 2, 0, 128 - PianoRollNumPitches);
                    status_message_ = "Piano Roll Range: " + get_midi_note_name(piano_roll_base_pitch_) +
                                      " to " + get_midi_note_name(piano_roll_base_pitch_ + PianoRollNumPitches - 1);
                    InvalidateRect(hwnd, NULL, FALSE);
                } else if (view_mode_ == ViewMode::ChannelRack) {
                    int max_bars = get_max_sequencer_bars();
                    int bars_per_view = get_bars_per_view();
                    int max_scroll = std::max(0, max_bars - bars_per_view);
                    sequencer_scroll_bar_ = std::clamp(sequencer_scroll_bar_ - steps, 0, max_scroll);
                    status_message_ = "Timeline scrolled to Bar " + std::to_string(sequencer_scroll_bar_ + 1) + " / " + std::to_string(max_bars);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return 0;
            }

            case WM_MOUSEHWHEEL: {
                short zDelta = GET_WHEEL_DELTA_WPARAM(wp);
                int steps = zDelta / WHEEL_DELTA;
                if (view_mode_ == ViewMode::ChannelRack) {
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

        // 4. Create DirectWrite Text Formats
        dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-us", &dwrite_main_);
        dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-us", &dwrite_bold_);
        dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15.0f, L"en-us", &dwrite_title_);
        dwrite_factory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.0f, L"en-us", &dwrite_small_);

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
                // 3. Create Direct2D Render Target
                D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
                    D2D1_RENDER_TARGET_TYPE_DEFAULT,
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

                d2d_factory_->CreateDxgiSurfaceRenderTarget(dxgi_surface, &props, &d2d_target_);
                dxgi_surface->Release();
            }
            back_buffer->Release();
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
        float master_peak = meter_peaks_[0];
        bool is_playing = engine_.transport().is_playing();
        auto ppq = engine_.session().project().time_map().ppq();
        float spm_norm = float(song_position_marker_) / float(std::max(1LL, 4LL * 8LL * ppq));

        shader_visualizer_.render(d3d_context_, d3d_rtv_, client_w_, client_h_,
                                 time_sec, master_peak, meter_peaks_, is_playing, spm_norm);

        // 2. Direct2D Pass: Render vector UI with hardware subpixel anti-aliasing
        d2d_target_->BeginDraw();

        render_transport_bar_d2d();
        if (view_mode_ == ViewMode::ChannelRack) {
            render_channel_rack_d2d();
        } else {
            render_piano_roll_d2d();
        }
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
        D2D1_RECT_F bar_rc = D2D1::RectF(0, 0, static_cast<float>(client_w_), 60.0f);
        // Glassy overlay allowing the D3D shader neon visualizer to shine through
        D2DRenderer::draw_rounded_box(d2d_target_, bar_rc, D2D1::ColorF(0.08f, 0.10f, 0.13f, 0.78f), t.border_dark, 0.0f);

        // Logo
        D2D1_RECT_F title_rc = D2D1::RectF(15.0f, 0.0f, 110.0f, 60.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_title_, "DigiDAW", title_rc, t.accent_orange);

        // GPU Badge
        D2D1_RECT_F badge_rc = D2D1::RectF(115.0f, 18.0f, 205.0f, 42.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, badge_rc, D2D1::ColorF(0.0f, 0.25f, 0.35f, 0.6f), t.accent_cyan, 4.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_small_, "⚡ D3D11+D2D", badge_rc, t.accent_cyan,
                              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Buttons
        bool is_playing = engine_.transport().is_playing();
        bool is_paused = (engine_.transport().state() == app::TransportState::Paused);

        D2D1_RECT_F play_rc = D2D1::RectF(215.0f, 12.0f, 285.0f, 48.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_bold_, play_rc, "▶ PLAY", is_playing, t.accent_green, t.bg_card);

        D2D1_RECT_F pause_rc = D2D1::RectF(292.0f, 12.0f, 362.0f, 48.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_bold_, pause_rc, "❚❚ PAUSE", is_paused, t.accent_amber, t.bg_card);

        D2D1_RECT_F stop_rc = D2D1::RectF(369.0f, 12.0f, 434.0f, 48.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_bold_, stop_rc, "■ STOP", false, t.accent_red, t.bg_card);

        // Tempo Controls
        double bpm = engine_.session().project().time_map().get_bpm_at(0);
        std::stringstream ss_bpm;
        ss_bpm << std::fixed << std::setprecision(1) << bpm << " BPM";

        D2D1_RECT_F bpm_minus_rc = D2D1::RectF(445.0f, 15.0f, 470.0f, 45.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_bold_, bpm_minus_rc, "-", false, t.bg_card, t.bg_card);

        D2D1_RECT_F bpm_disp_rc = D2D1::RectF(475.0f, 15.0f, 565.0f, 45.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, bpm_disp_rc, t.bg_input, t.border_dark, 4.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, ss_bpm.str(), bpm_disp_rc, t.accent_amber,
                              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        D2D1_RECT_F bpm_plus_rc = D2D1::RectF(570.0f, 15.0f, 595.0f, 45.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_bold_, bpm_plus_rc, "+", false, t.bg_card, t.bg_card);

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
               << "  |  Bar " << bar;

        D2D1_RECT_F pos_rc = D2D1::RectF(605.0f, 15.0f, 765.0f, 45.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, pos_rc, t.bg_input, t.border_dark, 4.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, ss_pos.str(), pos_rc, t.accent_cyan,
                              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // View Mode Switcher
        D2D1_RECT_F rack_btn_rc = D2D1::RectF(775.0f, 15.0f, 865.0f, 45.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_bold_, rack_btn_rc, "🎛 RACK",
                                view_mode_ == ViewMode::ChannelRack, t.accent_orange, t.bg_card);

        D2D1_RECT_F roll_btn_rc = D2D1::RectF(875.0f, 15.0f, 985.0f, 45.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_bold_, roll_btn_rc, "🎹 PIANO ROLL",
                                view_mode_ == ViewMode::PianoRoll, t.accent_cyan, t.bg_card);

        // 7. Analog Real-Time Audio Signal Oscilloscope Section (Beside Piano Roll)
        float spec_x = 1000.0f;
        float spec_max_right = static_cast<float>(client_w_ - 225);
        if (spec_max_right > spec_x + 90.0f) {
            float spec_w = std::min(360.0f, spec_max_right - spec_x);
            float spec_y = 10.0f;
            float spec_h = 40.0f;
            D2D1_RECT_F chassis_rc = D2D1::RectF(spec_x, spec_y, spec_x + spec_w, spec_y + spec_h);

            // Dark Analog Chassis Bezel
            D2DRenderer::draw_rounded_box(d2d_target_, chassis_rc, D2D1::ColorF(0.045f, 0.06f, 0.08f, 0.95f),
                                          D2D1::ColorF(0.18f, 0.24f, 0.32f, 1.0f), 5.0f);

            // Inset CRT Screen Glass
            D2D1_RECT_F screen_rc = D2D1::RectF(spec_x + 3.0f, spec_y + 3.0f, spec_x + spec_w - 3.0f, spec_y + spec_h - 3.0f);
            D2DRenderer::draw_rounded_box(d2d_target_, screen_rc, D2D1::ColorF(0.015f, 0.03f, 0.045f, 1.0f),
                                          D2D1::ColorF(0.08f, 0.12f, 0.16f, 1.0f), 3.0f);

            float center_y = (screen_rc.top + screen_rc.bottom) * 0.5f;
            float max_amp = (screen_rc.bottom - screen_rc.top) * 0.5f - 4.0f;

            // Oscilloscope Graticule Reticle Lines
            ID2D1SolidColorBrush* br_grid = nullptr;
            ID2D1SolidColorBrush* br_center = nullptr;
            d2d_target_->CreateSolidColorBrush(D2D1::ColorF(0.08f, 0.14f, 0.18f, 0.5f), &br_grid);
            d2d_target_->CreateSolidColorBrush(D2D1::ColorF(0.08f, 0.22f, 0.28f, 0.7f), &br_center);

            if (br_grid && br_center) {
                // Upper (+0.5) and Lower (-0.5) Reference Rails
                float y_pos = center_y - max_amp * 0.5f;
                float y_neg = center_y + max_amp * 0.5f;
                d2d_target_->DrawLine(D2D1::Point2F(screen_rc.left + 4.0f, y_pos), D2D1::Point2F(screen_rc.right - 4.0f, y_pos), br_grid, 0.7f);
                d2d_target_->DrawLine(D2D1::Point2F(screen_rc.left + 4.0f, y_neg), D2D1::Point2F(screen_rc.right - 4.0f, y_neg), br_grid, 0.7f);

                // Center 0V Zero-Voltage Baseline (where line rests at idle)
                d2d_target_->DrawLine(D2D1::Point2F(screen_rc.left + 4.0f, center_y), D2D1::Point2F(screen_rc.right - 4.0f, center_y), br_center, 0.9f);
            }

            // Screen Header: Title Label & Peak LED Lamp
            D2D1_RECT_F lbl_rc = D2D1::RectF(screen_rc.left + 6.0f, screen_rc.top + 2.0f, screen_rc.left + 160.0f, screen_rc.top + 14.0f);
            D2DRenderer::draw_text(d2d_target_, dwrite_small_, "ANALOG OSCILLOSCOPE", lbl_rc, D2D1::ColorF(0.0f, 0.85f, 0.75f, 0.85f));

            // Peak / Clip LED Lamp
            float led_cx = screen_rc.right - 8.0f;
            float led_cy = screen_rc.top + 7.0f;
            bool is_peaking = (meter_peaks_[0] > 0.92f);
            D2D1_ELLIPSE led_el = D2D1::Ellipse(D2D1::Point2F(led_cx, led_cy), 3.0f, 3.0f);
            ID2D1SolidColorBrush* br_led = nullptr;
            d2d_target_->CreateSolidColorBrush(is_peaking ? D2D1::ColorF(1.0f, 0.1f, 0.2f, 1.0f) : D2D1::ColorF(0.35f, 0.08f, 0.10f, 0.8f), &br_led);
            if (br_led) {
                d2d_target_->FillEllipse(led_el, br_led);
                br_led->Release();
            }

            // Continuous Real-Time Audio Signal Oscilloscope Line (Rests in the middle at idle, forms waveform when active)
            float start_x = screen_rc.left + 6.0f;
            float end_x = screen_rc.right - 6.0f;
            constexpr size_t num_pts = 96;
            float step_x = (end_x - start_x) / static_cast<float>(num_pts - 1);

            ID2D1PathGeometry* scope_geo = nullptr;
            ID2D1PathGeometry* fill_geo = nullptr;
            if (SUCCEEDED(d2d_factory_->CreatePathGeometry(&scope_geo)) &&
                SUCCEEDED(d2d_factory_->CreatePathGeometry(&fill_geo))) {
                ID2D1GeometrySink* sink = nullptr;
                ID2D1GeometrySink* sink_fill = nullptr;
                if (SUCCEEDED(scope_geo->Open(&sink)) && SUCCEEDED(fill_geo->Open(&sink_fill))) {
                    float s0 = std::clamp(meter_waveform_[0], -1.0f, 1.0f);
                    float y0 = std::clamp(center_y - s0 * max_amp, screen_rc.top + 2.0f, screen_rc.bottom - 2.0f);
                    D2D1_POINT_2F p0 = D2D1::Point2F(start_x, y0);

                    sink->BeginFigure(p0, D2D1_FIGURE_BEGIN_HOLLOW);
                    sink_fill->BeginFigure(D2D1::Point2F(start_x, center_y), D2D1_FIGURE_BEGIN_FILLED);
                    sink_fill->AddLine(p0);

                    for (size_t i = 1; i < num_pts; ++i) {
                        float s = std::clamp(meter_waveform_[i], -1.0f, 1.0f);
                        float y = std::clamp(center_y - s * max_amp, screen_rc.top + 2.0f, screen_rc.bottom - 2.0f);
                        D2D1_POINT_2F pt = D2D1::Point2F(start_x + static_cast<float>(i) * step_x, y);
                        sink->AddLine(pt);
                        sink_fill->AddLine(pt);
                    }

                    sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    sink_fill->AddLine(D2D1::Point2F(end_x, center_y));
                    sink_fill->EndFigure(D2D1_FIGURE_END_CLOSED);

                    sink->Close();
                    sink->Release();
                    sink_fill->Close();
                    sink_fill->Release();

                    // Subtle Phosphor Persistence Glow under/above the center line
                    ID2D1SolidColorBrush* br_wave_fill = nullptr;
                    d2d_target_->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.90f, 1.0f, 0.12f), &br_wave_fill);
                    if (br_wave_fill) {
                        d2d_target_->FillGeometry(fill_geo, br_wave_fill);
                        br_wave_fill->Release();
                    }

                    // Outer Phosphor Halo Bloom Line (3.5px)
                    ID2D1SolidColorBrush* br_glow = nullptr;
                    d2d_target_->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.92f, 1.0f, 0.42f), &br_glow);
                    if (br_glow) {
                        d2d_target_->DrawGeometry(scope_geo, br_glow, 3.5f);
                        br_glow->Release();
                    }

                    // Inner Sharp Neon Laser Core Line (1.6px)
                    ID2D1SolidColorBrush* br_core = nullptr;
                    d2d_target_->CreateSolidColorBrush(D2D1::ColorF(0.92f, 1.0f, 0.98f, 1.0f), &br_core);
                    if (br_core) {
                        d2d_target_->DrawGeometry(scope_geo, br_core, 1.6f);
                        br_core->Release();
                    }
                }
                if (scope_geo) scope_geo->Release();
                if (fill_geo) fill_geo->Release();
            }

            if (br_grid) br_grid->Release();
            if (br_center) br_center->Release();
        }

        // Save & Export
        D2D1_RECT_F save_rc = D2D1::RectF(static_cast<float>(client_w_ - 210), 15.0f, static_cast<float>(client_w_ - 115), 45.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_bold_, save_rc, "💾 Save", false, t.bg_card, t.bg_card);

        D2D1_RECT_F exp_rc = D2D1::RectF(static_cast<float>(client_w_ - 105), 15.0f, static_cast<float>(client_w_ - 15), 45.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_bold_, exp_rc, "💿 Export", false, t.bg_card, t.bg_card);
    }

    void render_channel_rack_d2d() {
        const auto& t = D2DRenderer::theme();
        D2D1_RECT_F rack_rc = D2D1::RectF(10.0f, 70.0f, static_cast<float>(client_w_ - 10), static_cast<float>(client_h_ - 260));
        D2DRenderer::draw_rounded_box(d2d_target_, rack_rc, t.bg_panel, t.border_dark, 8.0f);

        // Header Title
        D2D1_RECT_F title_rc = D2D1::RectF(25.0f, 75.0f, 700.0f, 100.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, "PLAYLIST ARRANGER   [D3D11 SHADER • DIRECT2D VECTOR ENGINE • DRAG PER BEAT]",
                              title_rc, t.text_secondary);

        // Horizontal Bar Range Display (Replaces old Bar - / + buttons)
        auto ppq = engine_.session().project().time_map().ppq();
        auto bar_ticks = 4 * ppq;
        int bars_per_view = get_bars_per_view();
        int max_bars = get_max_sequencer_bars();

        int start_bar_num = sequencer_scroll_bar_ + 1;
        int end_bar_num = sequencer_scroll_bar_ + bars_per_view;
        std::string bar_lbl = "↔ Showing Bars " + std::to_string(start_bar_num) + "-" + std::to_string(end_bar_num) + " / " + std::to_string(max_bars);
        D2D1_RECT_F bar_num_rc = D2D1::RectF(static_cast<float>(client_w_ - 260), 75.0f, static_cast<float>(client_w_ - 25), 100.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, bar_num_rc, t.bg_input, t.border_dark, 4.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_small_, bar_lbl, bar_num_rc, t.accent_orange,
                              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Timeline Ruler
        float ruler_x = 265.0f;
        float ruler_y = 105.0f;
        float ruler_w = static_cast<float>((client_w_ - 25) - 265);
        float ruler_h = 24.0f;

        D2D1_RECT_F ruler_rc = D2D1::RectF(ruler_x, ruler_y, ruler_x + ruler_w, ruler_y + ruler_h);
        D2DRenderer::draw_rounded_box(d2d_target_, ruler_rc, D2D1::ColorF(0.055f, 0.07f, 0.09f, 1.0f), t.border_dark, 3.0f);

        float bar_w = ruler_w / static_cast<float>(bars_per_view);
        float beat_w = bar_w / 4.0f;

        ID2D1SolidColorBrush* br_border_faint = nullptr;
        ID2D1SolidColorBrush* br_border_dark = nullptr;
        ID2D1SolidColorBrush* br_border_light = nullptr;
        d2d_target_->CreateSolidColorBrush(t.border_faint, &br_border_faint);
        d2d_target_->CreateSolidColorBrush(t.border_dark, &br_border_dark);
        d2d_target_->CreateSolidColorBrush(t.border_light, &br_border_light);

        for (int b = 0; b < bars_per_view; ++b) {
            float bx = ruler_x + b * bar_w;
            // Bar Number
            D2D1_RECT_F num_rc = D2D1::RectF(bx + 4.0f, ruler_y + 2.0f, bx + 36.0f, ruler_y + ruler_h - 2.0f);
            int cur_bar_idx = sequencer_scroll_bar_ + b + 1;
            D2DRenderer::draw_text(d2d_target_, dwrite_small_, std::to_string(cur_bar_idx), num_rc, t.text_secondary);

            // Bar tick lines
            if (br_border_dark && b > 0) {
                d2d_target_->DrawLine(D2D1::Point2F(bx, ruler_y), D2D1::Point2F(bx, ruler_y + ruler_h), br_border_dark, 1.0f);
            }
            if (br_border_faint) {
                for (int bt = 1; bt < 4; ++bt) {
                    float btx = bx + bt * beat_w;
                    d2d_target_->DrawLine(D2D1::Point2F(btx, ruler_y + ruler_h - 6.0f), D2D1::Point2F(btx, ruler_y + ruler_h), br_border_faint, 1.0f);
                }
            }
        }

        // Tracks & Lanes
        float start_y = 135.0f;
        float step_h = 42.0f;
        auto& channels = engine_.session().project().channels();
        auto& tracks = engine_.session().project().tracks();
        auto* pat = engine_.session().project().get_pattern(1);

        domain::Tick view_start_tick = static_cast<domain::Tick>(sequencer_scroll_bar_) * bar_ticks;
        domain::Tick view_duration = static_cast<domain::Tick>(bars_per_view) * bar_ticks;
        domain::Tick view_end_tick = view_start_tick + view_duration;

        float max_track_bottom = start_y;

        for (size_t ch_idx = 0; ch_idx < channels.size(); ++ch_idx) {
            const auto& ch = channels[ch_idx];
            float ch_y = start_y + ch_idx * 50.0f;
            max_track_bottom = ch_y + step_h;

            // Channel Left Controls
            // 1. Mute
            D2D1_RECT_F mute_rc = D2D1::RectF(25.0f, ch_y + 8.0f, 47.0f, ch_y + 34.0f);
            D2DRenderer::draw_button(d2d_target_, dwrite_small_, mute_rc, "M", ch.settings().muted, t.accent_red, t.bg_card, 2.0f);

            // 2. Solo
            D2D1_RECT_F solo_rc = D2D1::RectF(51.0f, ch_y + 8.0f, 73.0f, ch_y + 34.0f);
            D2DRenderer::draw_button(d2d_target_, dwrite_small_, solo_rc, "S", ch.settings().solo, t.accent_green, t.bg_card, 2.0f);

            // 3. Name & Settings Gear
            D2D1_RECT_F name_rc = D2D1::RectF(78.0f, ch_y + 5.0f, 218.0f, ch_y + 37.0f);
            std::string ch_label = ch.settings().name + " ⚙";
            D2DRenderer::draw_button(d2d_target_, dwrite_small_, name_rc, ch_label, false, t.bg_card, t.bg_card, 3.0f);

            // 4. Piano Roll Button
            D2D1_RECT_F roll_btn_rc = D2D1::RectF(223.0f, ch_y + 8.0f, 255.0f, ch_y + 34.0f);
            bool is_active_roll = (piano_roll_channel_ == ch.id());
            D2DRenderer::draw_button(d2d_target_, dwrite_small_, roll_btn_rc, "🎹", is_active_roll, t.accent_cyan, t.bg_card, 2.0f);

            // Track Arranger Lane
            D2D1_RECT_F lane_rc = D2D1::RectF(ruler_x, ch_y, ruler_x + ruler_w, ch_y + step_h);
            D2D1_COLOR_F lane_bg = (ch_idx % 2 == 0) ? D2D1::ColorF(0.078f, 0.098f, 0.133f, 1.0f) : D2D1::ColorF(0.067f, 0.086f, 0.118f, 1.0f);
            D2DRenderer::draw_rounded_box(d2d_target_, lane_rc, lane_bg, t.border_dark, 2.0f);

            // Beat grid lines across lane
            for (int b = 0; b < bars_per_view; ++b) {
                float bx = ruler_x + b * bar_w;
                if (br_border_dark && b > 0) {
                    d2d_target_->DrawLine(D2D1::Point2F(bx, ch_y), D2D1::Point2F(bx, ch_y + step_h), br_border_dark, 1.0f);
                }
                if (br_border_faint) {
                    for (int bt = 1; bt < 4; ++bt) {
                        float btx = bx + bt * beat_w;
                        d2d_target_->DrawLine(D2D1::Point2F(btx, ch_y), D2D1::Point2F(btx, ch_y + step_h), br_border_faint, 0.5f);
                    }
                }
            }

            // Render Track Clips
            if (ch_idx < tracks.size()) {
                const auto& track = tracks[ch_idx];
                for (const auto& clip : track.clips()) {
                    if (clip.end() <= view_start_tick || clip.start >= view_end_tick) continue;

                    domain::Tick draw_start = std::max(view_start_tick, clip.start);
                    domain::Tick draw_end = std::min(view_end_tick, clip.end());
                    double norm_start = double(draw_start - view_start_tick) / double(view_duration);
                    double norm_len = double(draw_end - draw_start) / double(view_duration);

                    float cx = ruler_x + static_cast<float>(norm_start * ruler_w);
                    float cw = std::max(16.0f, static_cast<float>(norm_len * ruler_w));

                    D2D1_RECT_F clip_rc = D2D1::RectF(cx, ch_y + 2.0f, cx + cw, ch_y + step_h - 2.0f);
                    D2DRenderer::draw_rounded_box(d2d_target_, clip_rc, t.clip_bg, t.clip_border, 4.0f, 1.5f);

                    // Top Header Strip
                    float hdr_h = 13.0f;
                    D2D1_RECT_F header_rc = D2D1::RectF(cx, ch_y + 2.0f, cx + cw, ch_y + 2.0f + hdr_h);
                    D2DRenderer::draw_rounded_box(d2d_target_, header_rc, t.clip_hdr, t.clip_hdr, 3.0f);

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

                    // Right Resize Handle Grip
                    if (cw > 20.0f && br_border_light) {
                        float rx = cx + cw - 4.0f;
                        d2d_target_->DrawLine(D2D1::Point2F(rx - 2.0f, ch_y + 16.0f), D2D1::Point2F(rx - 2.0f, ch_y + step_h - 6.0f), br_border_light, 1.0f);
                        d2d_target_->DrawLine(D2D1::Point2F(rx, ch_y + 16.0f), D2D1::Point2F(rx, ch_y + step_h - 6.0f), br_border_light, 1.0f);
                    }
                }
            }
        }

        // Add Channel [+] Button
        float add_y = start_y + channels.size() * 50.0f;
        D2D1_RECT_F add_btn_rc = D2D1::RectF(25.0f, add_y + 5.0f, 218.0f, add_y + 35.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, add_btn_rc, "+ Add Channel", false, t.bg_card, t.bg_card, 3.0f);

        // Downward Lime-Green Playhead Marker (SPM / Playhead)
        auto cur_tick = engine_.transport().is_playing() ? engine_.transport().current_tick() : song_position_marker_;
        if (cur_tick >= view_start_tick && cur_tick <= view_end_tick) {
            double norm_pos = double(cur_tick - view_start_tick) / double(view_duration);
            float head_x = ruler_x + static_cast<float>(norm_pos * ruler_w);
            D2DRenderer::draw_playhead(d2d_target_, head_x, ruler_y, max_track_bottom, t.accent_lime, "SPM", dwrite_small_);
        }

        // Sleek Horizontal Scrollbar Track & Thumb
        float scroll_track_y = rack_rc.bottom - 22.0f;
        float scroll_track_h = 14.0f;
        float scroll_track_x = ruler_x;
        float scroll_track_w = ruler_w;

        // Left Label for Scrollbar
        D2D1_RECT_F scroll_lbl_rc = D2D1::RectF(25.0f, scroll_track_y - 2.0f, ruler_x - 15.0f, scroll_track_y + scroll_track_h + 2.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_small_, "↔ TIMELINE SCROLL", scroll_lbl_rc, t.text_secondary,
                              DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Scrollbar Track Container
        D2D1_RECT_F scroll_track_rc = D2D1::RectF(scroll_track_x, scroll_track_y, scroll_track_x + scroll_track_w, scroll_track_y + scroll_track_h);
        D2DRenderer::draw_rounded_box(d2d_target_, scroll_track_rc, t.bg_input, t.border_dark, 4.0f);

        // Scrollbar Thumb
        float max_scroll = std::max(1.0f, float(max_bars - bars_per_view));
        float thumb_w = std::max(35.0f, (float(bars_per_view) / float(max_bars)) * scroll_track_w);
        float scroll_ratio = std::clamp(float(sequencer_scroll_bar_) / max_scroll, 0.0f, 1.0f);
        float thumb_x = scroll_track_x + scroll_ratio * (scroll_track_w - thumb_w);
        D2D1_RECT_F thumb_rc = D2D1::RectF(thumb_x, scroll_track_y + 1.0f, thumb_x + thumb_w, scroll_track_y + scroll_track_h - 1.0f);

        D2D1_COLOR_F thumb_col = dragging_seq_scrollbar_ ? t.accent_amber : t.accent_orange;
        D2DRenderer::draw_rounded_box(d2d_target_, thumb_rc, thumb_col, t.accent_amber, 3.0f);

        // 3 Grip Notches on Thumb Center
        if (br_border_dark && thumb_w > 20.0f) {
            float mid_tx = thumb_x + thumb_w * 0.5f;
            d2d_target_->DrawLine(D2D1::Point2F(mid_tx - 4.0f, scroll_track_y + 3.0f),
                                  D2D1::Point2F(mid_tx - 4.0f, scroll_track_y + scroll_track_h - 3.0f), br_border_dark, 1.2f);
            d2d_target_->DrawLine(D2D1::Point2F(mid_tx, scroll_track_y + 3.0f),
                                  D2D1::Point2F(mid_tx, scroll_track_y + scroll_track_h - 3.0f), br_border_dark, 1.2f);
            d2d_target_->DrawLine(D2D1::Point2F(mid_tx + 4.0f, scroll_track_y + 3.0f),
                                  D2D1::Point2F(mid_tx + 4.0f, scroll_track_y + scroll_track_h - 3.0f), br_border_dark, 1.2f);
        }

        if (br_border_light) br_border_light->Release();
        if (br_border_faint) br_border_faint->Release();
        if (br_border_dark) br_border_dark->Release();
    }

    void render_piano_roll_d2d() {
        const auto& t = D2DRenderer::theme();
        D2D1_RECT_F roll_rc = D2D1::RectF(10.0f, 70.0f, static_cast<float>(client_w_ - 10), static_cast<float>(client_h_ - 260));
        D2DRenderer::draw_rounded_box(d2d_target_, roll_rc, t.bg_panel, t.border_dark, 8.0f);

        // Header Title & Controls
        auto* ch = engine_.session().project().get_channel(piano_roll_channel_);
        std::string roll_title = "PIANO ROLL: " + (ch ? ch->settings().name : "Track") +
                                 "   (C0-B10 Scrollable | Drag Note Edge to Lengthen)";
        D2D1_RECT_F title_rc = D2D1::RectF(25.0f, 75.0f, 500.0f, 100.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, roll_title, title_rc, t.text_secondary);

        // Octave Jump & Step Controls
        D2D1_RECT_F oct_m_rc = D2D1::RectF(static_cast<float>(client_w_ - 620), 75.0f, static_cast<float>(client_w_ - 560), 100.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, oct_m_rc, "◄ Oct -", false, t.bg_card, t.bg_card, 3.0f);

        int cur_oct = piano_roll_base_pitch_ / 12;
        std::string oct_lbl = "Oct " + std::to_string(cur_oct);
        D2D1_RECT_F oct_disp_rc = D2D1::RectF(static_cast<float>(client_w_ - 555), 75.0f, static_cast<float>(client_w_ - 505), 100.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, oct_disp_rc, t.bg_input, t.border_dark, 3.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_small_, oct_lbl, oct_disp_rc, t.accent_cyan,
                              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        D2D1_RECT_F oct_p_rc = D2D1::RectF(static_cast<float>(client_w_ - 500), 75.0f, static_cast<float>(client_w_ - 440), 100.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, oct_p_rc, "Oct + ►", false, t.bg_card, t.bg_card, 3.0f);

        D2D1_RECT_F semi_dn_rc = D2D1::RectF(static_cast<float>(client_w_ - 435), 75.0f, static_cast<float>(client_w_ - 405), 100.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, semi_dn_rc, "▼", false, t.bg_card, t.bg_card, 3.0f);

        D2D1_RECT_F semi_up_rc = D2D1::RectF(static_cast<float>(client_w_ - 400), 75.0f, static_cast<float>(client_w_ - 370), 100.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, semi_up_rc, "▲", false, t.bg_card, t.bg_card, 3.0f);

        std::string len_str = "📏 Len: " + std::to_string(piano_roll_note_len_steps_);
        D2D1_RECT_F len_rc = D2D1::RectF(static_cast<float>(client_w_ - 365), 75.0f, static_cast<float>(client_w_ - 290), 100.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, len_rc, len_str, false, t.accent_orange, t.bg_card, 3.0f);

        std::string step_str = (piano_roll_steps_ == 16) ? "16 Steps" : "32 Steps";
        D2D1_RECT_F step_rc = D2D1::RectF(static_cast<float>(client_w_ - 285), 75.0f, static_cast<float>(client_w_ - 210), 100.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, step_rc, step_str, false, t.bg_card, t.bg_card, 3.0f);

        D2D1_RECT_F clear_rc = D2D1::RectF(static_cast<float>(client_w_ - 205), 75.0f, static_cast<float>(client_w_ - 145), 100.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, clear_rc, "Clear", false, t.bg_card, t.bg_card, 3.0f);

        D2D1_RECT_F close_rc = D2D1::RectF(static_cast<float>(client_w_ - 140), 75.0f, static_cast<float>(client_w_ - 25), 100.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, close_rc, "🎛 Back to Rack", false, t.accent_cyan, t.bg_card, 3.0f);

        // Keyboard & Grid Dimensions
        float key_x = 25.0f;
        float key_w = 60.0f;
        float grid_x = 90.0f;
        float grid_w = static_cast<float>((client_w_ - 35) - 90);
        float grid_top = 110.0f;
        float grid_bottom = static_cast<float>(client_h_ - 270);
        float grid_h = grid_bottom - grid_top;
        float row_h = grid_h / static_cast<float>(PianoRollNumPitches);
        float col_w = grid_w / static_cast<float>(piano_roll_steps_);

        ID2D1SolidColorBrush* br_border_faint = nullptr;
        ID2D1SolidColorBrush* br_border_dark = nullptr;
        d2d_target_->CreateSolidColorBrush(t.border_faint, &br_border_faint);
        d2d_target_->CreateSolidColorBrush(t.border_dark, &br_border_dark);

        auto* pat = engine_.session().project().get_pattern(1);
        auto ppq = engine_.session().project().time_map().ppq();
        auto step_ticks = ppq / 4;

        // Render Pitch Rows & Piano Keys
        for (int row = 0; row < PianoRollNumPitches; ++row) {
            int pitch_val = (piano_roll_base_pitch_ + PianoRollNumPitches - 1) - row;
            float ry = grid_top + row * row_h;
            bool is_black = is_midi_black_key(static_cast<uint8_t>(pitch_val));

            // 1. Piano Key
            D2D1_RECT_F key_rc = D2D1::RectF(key_x, ry, key_x + key_w, ry + row_h);
            D2D1_COLOR_F key_bg = is_black ? D2D1::ColorF(0.12f, 0.15f, 0.20f, 1.0f) : D2D1::ColorF(0.92f, 0.94f, 0.96f, 1.0f);
            D2D1_COLOR_F key_txt = is_black ? t.text_secondary : D2D1::ColorF(0.1f, 0.12f, 0.15f, 1.0f);
            D2DRenderer::draw_rounded_box(d2d_target_, key_rc, key_bg, t.border_dark, 2.0f);
            std::string note_name = get_midi_note_name(static_cast<uint8_t>(pitch_val));
            D2DRenderer::draw_text(d2d_target_, dwrite_small_, note_name, key_rc, key_txt,
                                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            // 2. Grid Row Background
            D2D1_RECT_F r_row_rc = D2D1::RectF(grid_x, ry, grid_x + grid_w, ry + row_h);
            D2D1_COLOR_F row_bg = is_black ? D2D1::ColorF(0.063f, 0.078f, 0.106f, 1.0f) : D2D1::ColorF(0.082f, 0.102f, 0.137f, 1.0f);
            D2DRenderer::draw_rounded_box(d2d_target_, r_row_rc, row_bg, t.border_faint, 0.0f);
        }

        // Render Grid Columns (Steps)
        for (int col = 0; col <= piano_roll_steps_; ++col) {
            float cx = grid_x + col * col_w;
            bool is_beat = (col % 4 == 0);
            if (is_beat && br_border_dark) {
                d2d_target_->DrawLine(D2D1::Point2F(cx, grid_top), D2D1::Point2F(cx, grid_bottom), br_border_dark, 1.5f);
            } else if (br_border_faint) {
                d2d_target_->DrawLine(D2D1::Point2F(cx, grid_top), D2D1::Point2F(cx, grid_bottom), br_border_faint, 0.5f);
            }
        }

        // Render Active Notes
        if (pat) {
            auto* note_set = pat->get_channel_notes(piano_roll_channel_);
            if (note_set) {
                for (const auto& note : note_set->notes()) {
                    int p = note.pitch;
                    int min_vis_p = piano_roll_base_pitch_;
                    int max_vis_p = piano_roll_base_pitch_ + PianoRollNumPitches - 1;
                    if (p < min_vis_p || p > max_vis_p) continue;

                    int row = max_vis_p - p;
                    float ny = grid_top + row * row_h;

                    float start_col = static_cast<float>(note.start) / static_cast<float>(step_ticks);
                    float dur_cols = static_cast<float>(note.length) / static_cast<float>(step_ticks);

                    float nx = grid_x + start_col * col_w;
                    float nw = std::max(6.0f, dur_cols * col_w);

                    D2D1_RECT_F note_rc = D2D1::RectF(nx + 1.0f, ny + 1.0f, nx + nw - 1.0f, ny + row_h - 1.0f);
                    D2DRenderer::draw_rounded_box(d2d_target_, note_rc, t.accent_orange, t.accent_amber, 3.0f);

                    // Note text readout
                    int dur_steps = std::max(1, static_cast<int>(note.length / step_ticks));
                    std::string n_lbl = get_midi_note_name(note.pitch);
                    if (dur_steps > 1) n_lbl += " (" + std::to_string(dur_steps) + ")";
                    D2DRenderer::draw_text(d2d_target_, dwrite_small_, n_lbl, note_rc, D2D1::ColorF(0.08f, 0.1f, 0.12f, 1.0f));

                    // Right Edge Resize Handle Grip
                    if (nw > 14.0f) {
                        float hx = nx + nw - 4.0f;
                        ID2D1SolidColorBrush* br_handle = nullptr;
                        d2d_target_->CreateSolidColorBrush(D2D1::ColorF(0.1f, 0.12f, 0.15f, 0.6f), &br_handle);
                        if (br_handle) {
                            d2d_target_->DrawLine(D2D1::Point2F(hx, ny + 2.0f), D2D1::Point2F(hx, ny + row_h - 2.0f), br_handle, 1.5f);
                            br_handle->Release();
                        }
                    }
                }
            }
        }

        // Playhead Marker on Piano Roll
        auto cur_tick = engine_.transport().is_playing() ? engine_.transport().current_tick() : song_position_marker_;
        float head_col = static_cast<float>(cur_tick % (piano_roll_steps_ * step_ticks)) / static_cast<float>(step_ticks);
        float head_x = grid_x + head_col * col_w;
        D2DRenderer::draw_playhead(d2d_target_, head_x, grid_top - 15.0f, grid_bottom, t.accent_lime, "SPM", dwrite_small_);

        if (br_border_faint) br_border_faint->Release();
        if (br_border_dark) br_border_dark->Release();
    }

    void render_mixer_panel_d2d() {
        const auto& t = D2DRenderer::theme();
        D2D1_RECT_F mixer_rc = D2D1::RectF(10.0f, static_cast<float>(client_h_ - 250), static_cast<float>(client_w_ - 10), static_cast<float>(client_h_ - 30));
        D2DRenderer::draw_rounded_box(d2d_target_, mixer_rc, t.bg_panel, t.border_dark, 8.0f);

        // Header Title
        D2D1_RECT_F title_rc = D2D1::RectF(25.0f, static_cast<float>(client_h_ - 245), 600.0f, static_cast<float>(client_h_ - 225));
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, "MIXER CONSOLE   [D3D11 HARDWARE PEAK METERS • DIRECT2D VECTOR FADERS]",
                              title_rc, t.text_secondary);

        float track_w = 95.0f;
        float start_x = 25.0f;

        for (size_t i = 0; i <= 4; ++i) {
            float tx = start_x + i * (track_w + 12.0f);
            float ty = static_cast<float>(client_h_ - 220);

            // Channel Box Container
            D2D1_RECT_F ch_box = D2D1::RectF(tx, ty, tx + track_w, ty + 180.0f);
            D2DRenderer::draw_rounded_box(d2d_target_, ch_box, t.bg_card, t.border_faint, 4.0f);

            // Track Header Name
            std::string t_name = (i == 0) ? "MASTER" : ("Track " + std::to_string(i));
            D2D1_RECT_F hdr_rc = D2D1::RectF(tx, ty, tx + track_w, ty + 24.0f);
            D2D1_COLOR_F hdr_col = (i == 0) ? t.accent_orange : t.text_primary;
            D2DRenderer::draw_text(d2d_target_, dwrite_bold_, t_name, hdr_rc, hdr_col,
                                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            // Mute & Solo & Volume
            auto* mix_tr = engine_.session().project().mixer_graph().get_track(i);
            float vol = mix_tr ? mix_tr->volume() : 1.0f;
            bool is_muted = (vol <= 0.0001f);
            bool is_solo = false;

            D2D1_RECT_F m_rc = D2D1::RectF(tx + 6.0f, ty + 28.0f, tx + 44.0f, ty + 48.0f);
            D2DRenderer::draw_button(d2d_target_, dwrite_small_, m_rc, "M", is_muted, t.accent_red, t.bg_input, 2.0f);

            D2D1_RECT_F s_rc = D2D1::RectF(tx + 51.0f, ty + 28.0f, tx + 89.0f, ty + 48.0f);
            D2DRenderer::draw_button(d2d_target_, dwrite_small_, s_rc, "S", is_solo, t.accent_green, t.bg_input, 2.0f);

            // VU Meter (Glowing Gradient)
            float peak_val = (i < meter_peaks_.size()) ? meter_peaks_[i] : 0.0f;
            D2D1_RECT_F meter_rc = D2D1::RectF(tx + 10.0f, ty + 54.0f, tx + 24.0f, ty + 172.0f);
            D2DRenderer::draw_vu_meter(d2d_target_, meter_rc, peak_val);

            // Volume Fader & Decibel Readout
            float norm_gain = vol / 1.5f; // 0.0x to 1.5x

            float db = (vol > 0.0001f) ? (20.0f * std::log10(vol)) : -60.0f;
            std::stringstream ss_db;
            if (vol < 0.001f) ss_db << "-inf dB";
            else {
                if (db >= 0.0f) ss_db << "+";
                ss_db << std::fixed << std::setprecision(1) << db << " dB";
            }

            D2D1_RECT_F fader_rc = D2D1::RectF(tx + 32.0f, ty + 54.0f, tx + 87.0f, ty + 172.0f);
            D2DRenderer::draw_slider_vertical(d2d_target_, dwrite_main_, dwrite_small_, fader_rc, norm_gain, ss_db.str());
        }
    }

    void render_status_bar_d2d() {
        const auto& t = D2DRenderer::theme();
        D2D1_RECT_F status_rc = D2D1::RectF(0.0f, static_cast<float>(client_h_ - 25), static_cast<float>(client_w_), static_cast<float>(client_h_));
        D2DRenderer::draw_rounded_box(d2d_target_, status_rc, t.bg_header, t.border_dark, 0.0f);

        std::string full_status = "Status: " + status_message_ +
                                  " | Engine: Direct2D 1.1 + Direct3D 11 Shaders (Active)";
        D2D1_RECT_F txt_rc = D2D1::RectF(15.0f, static_cast<float>(client_h_ - 25), static_cast<float>(client_w_ - 15), static_cast<float>(client_h_));
        D2DRenderer::draw_text(d2d_target_, dwrite_small_, full_status, txt_rc, t.text_secondary);
    }

    void render_plugin_editor_d2d() {
        if (active_editor_channel_ == 0 || !active_synth_instance_) return;
        const auto& t = D2DRenderer::theme();

        float ew = 520.0f;
        float eh = 400.0f;
        float ex = (static_cast<float>(client_w_) - ew) * 0.5f;
        float ey = (static_cast<float>(client_h_) - eh) * 0.5f;
        editor_bounds_ = {static_cast<LONG>(ex), static_cast<LONG>(ey), static_cast<LONG>(ex + ew), static_cast<LONG>(ey + eh)};

        // Modal Container
        D2D1_RECT_F dlg_rc = D2D1::RectF(ex, ey, ex + ew, ey + eh);
        D2DRenderer::draw_rounded_box(d2d_target_, dlg_rc, t.bg_card, t.accent_cyan, 8.0f, 2.0f);

        // Header Bar
        D2D1_RECT_F hdr_rc = D2D1::RectF(ex, ey, ex + ew, ey + 36.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, hdr_rc, t.bg_header, t.border_dark, 8.0f);
        D2D1_RECT_F title_rc = D2D1::RectF(ex + 15.0f, ey, ex + ew - 40.0f, ey + 36.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, "3xOsc Synthesizer — Channel Editor", title_rc, t.accent_cyan);

        // Close Button [ ✕ ]
        D2D1_RECT_F close_rc = D2D1::RectF(ex + ew - 32.0f, ey + 6.0f, ex + ew - 8.0f, ey + 30.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_bold_, close_rc, "✕", false, t.accent_red, t.bg_card, 3.0f);

        // Oscillators 1, 2, 3 parameters
        float row_y = ey + 50.0f;
        for (int osc = 1; osc <= 3; ++osc) {
            D2D1_RECT_F osc_box = D2D1::RectF(ex + 15.0f, row_y, ex + ew - 15.0f, row_y + 80.0f);
            D2DRenderer::draw_rounded_box(d2d_target_, osc_box, t.bg_input, t.border_dark, 4.0f);

            std::string o_title = "Oscillator " + std::to_string(osc);
            D2D1_RECT_F o_title_rc = D2D1::RectF(ex + 25.0f, row_y + 5.0f, ex + 140.0f, row_y + 25.0f);
            D2DRenderer::draw_text(d2d_target_, dwrite_bold_, o_title, o_title_rc, t.accent_orange);

            // Shape Buttons
            const char* shapes[] = {"Sin", "Tri", "Saw", "Sqr", "Noise"};
            int cur_shape = static_cast<int>(active_synth_instance_->get_parameter((osc - 1) * 4));
            for (int s = 0; s < 5; ++s) {
                D2D1_RECT_F s_rc = D2D1::RectF(ex + 25.0f + s * 42.0f, row_y + 30.0f, ex + 63.0f + s * 42.0f, row_y + 52.0f);
                bool is_sel = (cur_shape == s);
                D2DRenderer::draw_button(d2d_target_, dwrite_small_, s_rc, shapes[s], is_sel, t.accent_cyan, t.bg_card, 2.0f);
            }

            // Coarse & Mix Dials / Sliders
            float coarse = active_synth_instance_->get_parameter((osc - 1) * 4 + 1);
            float mix = active_synth_instance_->get_parameter((osc - 1) * 4 + 3);

            std::string coarse_str = "Tune: " + std::to_string(static_cast<int>(coarse)) + " st";
            D2D1_RECT_F coarse_rc = D2D1::RectF(ex + 250.0f, row_y + 30.0f, ex + 370.0f, row_y + 65.0f);
            float norm_coarse = (coarse + 24.0f) / 48.0f;
            D2DRenderer::draw_slider_vertical(d2d_target_, dwrite_small_, dwrite_small_, coarse_rc, norm_coarse, coarse_str);

            std::string mix_str = "Vol: " + std::to_string(static_cast<int>(mix * 100.0f)) + "%";
            D2D1_RECT_F mix_rc = D2D1::RectF(ex + 380.0f, row_y + 30.0f, ex + 500.0f, row_y + 65.0f);
            D2DRenderer::draw_slider_vertical(d2d_target_, dwrite_small_, dwrite_small_, mix_rc, mix, mix_str);

            row_y += 88.0f;
        }

        // ADSR Envelope
        D2D1_RECT_F env_box = D2D1::RectF(ex + 15.0f, row_y, ex + ew - 15.0f, row_y + 40.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, env_box, t.bg_input, t.border_dark, 4.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_small_, "ADSR Envelope: A: 5ms | D: 80ms | S: 75% | R: 150ms",
                              env_box, t.text_secondary, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
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
        RECT title_rc{15, 0, 110, 60};
        GuiRenderer::draw_text(mem_dc_, "DigiDAW", title_rc, t.accent_orange, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // GDI Badge
        RECT badge_rc{115, 18, 205, 42};
        GuiRenderer::draw_rounded_box(mem_dc_, badge_rc, RGB(18, 24, 32), t.accent_cyan, 4);
        SelectObject(mem_dc_, font_small_);
        GuiRenderer::draw_text(mem_dc_, "💻 GDI BUFFER", badge_rc, t.accent_cyan, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SelectObject(mem_dc_, font_bold_);

        // Transport Buttons: PLAY, PAUSE, STOP
        bool is_playing = engine_.transport().is_playing();
        bool is_paused = (engine_.transport().state() == app::TransportState::Paused);

        // 1. PLAY Button
        RECT play_rc{215, 12, 285, 48};
        GuiRenderer::draw_button(mem_dc_, play_rc, "▶ PLAY", is_playing, t.accent_green, t.bg_card);

        // 2. PAUSE Button
        RECT pause_rc{292, 12, 362, 48};
        GuiRenderer::draw_button(mem_dc_, pause_rc, "❚❚ PAUSE", is_paused, t.accent_amber, t.bg_card);

        // 3. STOP Button (Rewinds to tick 0 & SPM)
        RECT stop_rc{369, 12, 434, 48};
        GuiRenderer::draw_button(mem_dc_, stop_rc, "■ STOP", false, t.accent_red, t.bg_card);

        // Tempo BPM Controls
        double bpm = engine_.session().project().time_map().get_bpm_at(0);
        std::stringstream ss_bpm;
        ss_bpm << std::fixed << std::setprecision(1) << bpm << " BPM";

        RECT bpm_minus_rc{445, 15, 470, 45};
        GuiRenderer::draw_button(mem_dc_, bpm_minus_rc, "-", false, t.bg_card, t.bg_card);

        RECT bpm_disp_rc{475, 15, 565, 45};
        GuiRenderer::draw_rounded_box(mem_dc_, bpm_disp_rc, t.bg_input, t.border_dark, 4);
        GuiRenderer::draw_text(mem_dc_, ss_bpm.str(), bpm_disp_rc, t.accent_amber, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT bpm_plus_rc{570, 15, 595, 45};
        GuiRenderer::draw_button(mem_dc_, bpm_plus_rc, "+", false, t.bg_card, t.bg_card);

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
               << "  |  Bar " << bar;

        RECT pos_rc{605, 15, 765, 45};
        GuiRenderer::draw_rounded_box(mem_dc_, pos_rc, t.bg_input, t.border_dark, 4);
        GuiRenderer::draw_text(mem_dc_, ss_pos.str(), pos_rc, t.accent_cyan, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // View Mode Tabs: [ 🎛 RACK ] & [ 🎹 PIANO ROLL ]
        RECT rack_tab_rc{775, 15, 865, 45};
        GuiRenderer::draw_button(mem_dc_, rack_tab_rc, "🎛 RACK", (view_mode_ == ViewMode::ChannelRack),
                                 t.accent_orange, t.bg_card);

        RECT roll_tab_rc{875, 15, 985, 45};
        GuiRenderer::draw_button(mem_dc_, roll_tab_rc, "🎹 PIANO ROLL", (view_mode_ == ViewMode::PianoRoll),
                                 t.accent_cyan, t.bg_card);

        // Analog Real-Time Audio Signal Oscilloscope Section (Beside Piano Roll)
        int spec_x = 1000;
        int spec_max_right = client_w_ - 225;
        if (spec_max_right > spec_x + 90) {
            int spec_w = std::min(360, spec_max_right - spec_x);
            int spec_y = 10;
            int spec_h = 40;
            RECT chassis_rc{spec_x, spec_y, spec_x + spec_w, spec_y + spec_h};
            GuiRenderer::draw_rounded_box(mem_dc_, chassis_rc, RGB(12, 16, 22), RGB(35, 48, 64), 5);

            RECT screen_rc{spec_x + 3, spec_y + 3, spec_x + spec_w - 3, spec_y + spec_h - 3};
            GuiRenderer::fill_rect(mem_dc_, screen_rc, RGB(6, 10, 15));

            int center_y = (screen_rc.top + screen_rc.bottom) / 2;
            int max_amp = (screen_rc.bottom - screen_rc.top) / 2 - 4;

            // Center Baseline Graticule (faint gray-cyan 0V line)
            HPEN pen_center = CreatePen(PS_DOT, 1, RGB(20, 50, 65));
            HPEN old_pen = (HPEN)SelectObject(mem_dc_, pen_center);
            MoveToEx(mem_dc_, screen_rc.left + 4, center_y, NULL);
            LineTo(mem_dc_, screen_rc.right - 4, center_y);

            RECT lbl_rc{screen_rc.left + 5, screen_rc.top + 2, screen_rc.left + 160, screen_rc.top + 14};
            SelectObject(mem_dc_, font_small_);
            GuiRenderer::draw_text(mem_dc_, "ANALOG OSCILLOSCOPE", lbl_rc, RGB(0, 210, 180), DT_LEFT | DT_SINGLELINE);

            int disp_w = (screen_rc.right - 6) - (screen_rc.left + 6);
            constexpr size_t num_pts = 96;
            float step_x = float(disp_w) / float(num_pts - 1);

            POINT pts[num_pts];
            for (size_t i = 0; i < num_pts; ++i) {
                float px = float(screen_rc.left + 6) + float(i) * step_x;
                float s = std::clamp(meter_waveform_[i], -1.0f, 1.0f);
                int py = center_y - static_cast<int>(s * float(max_amp));
                pts[i].x = static_cast<int>(px);
                pts[i].y = py;
            }

            // Real-Time Oscilloscope Line (Cyan) - Rests in the middle at idle, forms wave when active
            HPEN pen_cyan = CreatePen(PS_SOLID, 2, RGB(0, 229, 255));
            SelectObject(mem_dc_, pen_cyan);
            Polyline(mem_dc_, pts, num_pts);

            SelectObject(mem_dc_, old_pen);
            DeleteObject(pen_center);
            DeleteObject(pen_cyan);
        }

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

        // Layout: Continuous Playlist Arranger (matching FL Studio playlist screenshot)
        int bars_per_view = (client_w_ > 1400) ? 12 : 8;
        int start_x = 265;
        int total_seq_w = (client_w_ - 25) - start_x;
        int bar_w = std::max(60, total_seq_w / bars_per_view);
        float beat_w = float(bar_w) / 4.0f;
        int step_h = 52;

        // Header Title with Navigation
        SelectObject(mem_dc_, font_bold_);
        std::string header_title = "PLAYLIST ARRANGER   [DRAG CLIPS PER BEAT • DRAG RIGHT EDGE TO EXTEND • RIGHT-CLICK TO DELETE]";
        RECT header_rc{25, rack_top + 8, 620, rack_top + 28};
        GuiRenderer::draw_text(mem_dc_, header_title, header_rc, t.text_secondary);

        // Horizontal Bar Range Display
        int max_bars = get_max_sequencer_bars();
        SelectObject(mem_dc_, font_small_);
        RECT disp_b_rc{client_w_ - 260, rack_top + 6, client_w_ - 20, rack_top + 28};
        std::string b_range_str = "↔ Showing Bars " + std::to_string(sequencer_scroll_bar_ + 1) + "-" +
                                  std::to_string(sequencer_scroll_bar_ + bars_per_view) + " / " + std::to_string(max_bars);
        GuiRenderer::draw_rounded_box(mem_dc_, disp_b_rc, t.bg_input, t.border_dark, 4);
        GuiRenderer::draw_text(mem_dc_, b_range_str, disp_b_rc, t.accent_orange, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // --- Timeline Ruler (Bar Numbers 1, 2, 3, 4, 5, 6 with Beat Subdivisions) ---
        int ruler_y = rack_top + 32;
        int ruler_h = 24;
        RECT ruler_bg_rc{start_x, ruler_y, start_x + total_seq_w, ruler_y + ruler_h};
        GuiRenderer::fill_rect(mem_dc_, ruler_bg_rc, RGB(14, 18, 24));
        GuiRenderer::draw_border(mem_dc_, ruler_bg_rc, RGB(36, 45, 58));

        domain::Tick view_start_tick = sequencer_scroll_bar_ * bar_ticks;
        domain::Tick view_duration = bars_per_view * bar_ticks;
        domain::Tick view_end_tick = view_start_tick + view_duration;

        for (int b = 0; b < bars_per_view; ++b) {
            int abs_bar = sequencer_scroll_bar_ + b;
            int bx = start_x + b * bar_w;

            // Bar Vertical Boundary Line
            HPEN bPen = CreatePen(PS_SOLID, 1, RGB(55, 68, 86));
            HGDIOBJ oldP = SelectObject(mem_dc_, bPen);
            MoveToEx(mem_dc_, bx, ruler_y, NULL);
            LineTo(mem_dc_, bx, ruler_y + ruler_h);
            SelectObject(mem_dc_, oldP);
            DeleteObject(bPen);

            // Bar Number Label (Look at screenshot: 1, 2, 3, 4, 5, 6 positioned right above bar line!)
            SelectObject(mem_dc_, font_main_);
            RECT num_rc{bx + 3, ruler_y + 2, bx + 28, ruler_y + 22};
            GuiRenderer::draw_text(mem_dc_, std::to_string(abs_bar + 1), num_rc, RGB(215, 225, 238), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // Minor Beat Ticks within Bar (4 beats per bar)
            HPEN tickPen = CreatePen(PS_SOLID, 1, RGB(38, 48, 60));
            oldP = SelectObject(mem_dc_, tickPen);
            for (int bt = 1; bt < 4; ++bt) {
                int tx = bx + static_cast<int>(bt * beat_w);
                MoveToEx(mem_dc_, tx, ruler_y + 14, NULL);
                LineTo(mem_dc_, tx, ruler_y + ruler_h);
            }
            SelectObject(mem_dc_, oldP);
            DeleteObject(tickPen);
        }

        // --- Channels List & Continuous Playlist Lanes ---
        int ch_y = ruler_y + ruler_h + 4;

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

            // Playlist Grid Lane Background
            RECT lane_rc{start_x, ch_y, start_x + total_seq_w, ch_y + step_h};
            COLORREF lane_bg = (ch_idx % 2 == 0) ? RGB(20, 25, 34) : RGB(17, 22, 30);
            GuiRenderer::fill_rect(mem_dc_, lane_rc, lane_bg);

            // Beat Grid Lines across entire track lane
            for (int b = 0; b < bars_per_view; ++b) {
                int bx = start_x + b * bar_w;
                // Bar boundary
                HPEN barPen = CreatePen(PS_SOLID, 1, RGB(42, 52, 68));
                HGDIOBJ oldP = SelectObject(mem_dc_, barPen);
                MoveToEx(mem_dc_, bx, ch_y, NULL);
                LineTo(mem_dc_, bx, ch_y + step_h);

                // Beat subdivisions
                HPEN beatPen = CreatePen(PS_SOLID, 1, RGB(25, 32, 42));
                SelectObject(mem_dc_, beatPen);
                for (int bt = 1; bt < 4; ++bt) {
                    int tx = bx + static_cast<int>(bt * beat_w);
                    MoveToEx(mem_dc_, tx, ch_y, NULL);
                    LineTo(mem_dc_, tx, ch_y + step_h);
                }
                SelectObject(mem_dc_, oldP);
                DeleteObject(beatPen);
                DeleteObject(barPen);
            }
            // Bottom Lane Border
            HPEN divPen = CreatePen(PS_SOLID, 1, RGB(34, 42, 54));
            HGDIOBJ oldDiv = SelectObject(mem_dc_, divPen);
            MoveToEx(mem_dc_, start_x, ch_y + step_h, NULL);
            LineTo(mem_dc_, start_x + total_seq_w, ch_y + step_h);
            SelectObject(mem_dc_, oldDiv);
            DeleteObject(divPen);

            // --- Render Clips for this Track (FL Studio Screenshot Design) ---
            for (const auto& clip : track.clips()) {
                if (clip.end() <= view_start_tick || clip.start >= view_end_tick) continue;

                double norm_start = double(clip.start - view_start_tick) / double(view_duration);
                double norm_len = double(clip.length) / double(view_duration);
                int cx = start_x + static_cast<int>(norm_start * total_seq_w);
                int cw = std::max(16, static_cast<int>(norm_len * total_seq_w));

                RECT clip_rc{cx, ch_y + 1, cx + cw, ch_y + step_h - 1};

                // Clip Body Box
                COLORREF clip_bg = RGB(26, 36, 48);
                COLORREF clip_border = RGB(62, 82, 108);
                GuiRenderer::draw_rounded_box(mem_dc_, clip_rc, clip_bg, clip_border, 3);

                // Top Header Banner (Height 16px, with pattern icon and title)
                RECT clip_hdr_rc{cx + 1, ch_y + 2, cx + cw - 1, ch_y + 17};
                GuiRenderer::draw_rounded_box(mem_dc_, clip_hdr_rc, RGB(42, 56, 75), RGB(52, 70, 92), 2);

                SelectObject(mem_dc_, font_small_);
                std::string clip_title = " ≡ Pattern " + std::to_string(clip.pattern_id);
                if (pat && !pat->name().empty()) {
                    clip_title = " ≡ " + pat->name();
                }
                GuiRenderer::draw_text(mem_dc_, clip_title, clip_hdr_rc, RGB(225, 235, 245), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                // Faint Beat Lines inside Clip Body
                for (int b = 0; b < bars_per_view; ++b) {
                    int bx = start_x + b * bar_w;
                    for (int bt = 0; bt < 4; ++bt) {
                        int tx = bx + static_cast<int>(bt * beat_w);
                        if (tx > cx && tx < cx + cw - 1) {
                            HPEN cBeatPen = CreatePen(PS_SOLID, 1, (bt == 0) ? RGB(45, 60, 78) : RGB(32, 44, 58));
                            HGDIOBJ oldC = SelectObject(mem_dc_, cBeatPen);
                            MoveToEx(mem_dc_, tx, ch_y + 18, NULL);
                            LineTo(mem_dc_, tx, ch_y + step_h - 2);
                            SelectObject(mem_dc_, oldC);
                            DeleteObject(cBeatPen);
                        }
                    }
                }

                // Render Miniature Melody Note Bars (matching screenshot)
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
                            int ny = (ch_y + step_h - 7) - static_cast<int>(norm_p * (step_h - 28));
                            int nh = 5;

                            RECT note_bar_rc{nx, ny, nx + nw, ny + nh};
                            // Light silver/platinum note bars matching the user screenshot!
                            GuiRenderer::draw_rounded_box(mem_dc_, note_bar_rc, RGB(212, 222, 232), RGB(245, 250, 255), 1);
                        }
                    }
                }

                // Right Resize Edge Grip Handle (indicates drag-to-resize)
                HPEN gripPen = CreatePen(PS_SOLID, 1, RGB(80, 105, 135));
                HGDIOBJ oldGrip = SelectObject(mem_dc_, gripPen);
                MoveToEx(mem_dc_, cx + cw - 4, ch_y + 19, NULL);
                LineTo(mem_dc_, cx + cw - 4, ch_y + step_h - 3);
                SelectObject(mem_dc_, oldGrip);
                DeleteObject(gripPen);
            }

            ch_y += step_h + 4;
        }

        // --- Render Downward-Pointing Lime Playhead Indicator (matching screenshot at Bar 2) ---
        int seq_bottom = ch_y + 2;
        COLORREF playhead_lime = RGB(175, 225, 90);

        if (!engine_.transport().is_playing()) {
            if (song_position_marker_ >= view_start_tick && song_position_marker_ < view_end_tick) {
                int spm_x = start_x + static_cast<int>((double(song_position_marker_ - view_start_tick) / view_duration) * total_seq_w);
                GuiRenderer::draw_playhead(mem_dc_, spm_x, ruler_y, seq_bottom, playhead_lime);
            }
        } else {
            if (song_position_marker_ >= view_start_tick && song_position_marker_ < view_end_tick) {
                int spm_x = start_x + static_cast<int>((double(song_position_marker_ - view_start_tick) / view_duration) * total_seq_w);
                GuiRenderer::draw_playhead(mem_dc_, spm_x, ruler_y, seq_bottom, RGB(180, 110, 20), "SPM");
            }

            auto tick = engine_.transport().current_tick();
            if (tick >= view_start_tick && tick < view_end_tick) {
                int playhead_x = start_x + static_cast<int>((double(tick - view_start_tick) / view_duration) * total_seq_w);
                GuiRenderer::draw_playhead(mem_dc_, playhead_x, ruler_y, seq_bottom, playhead_lime);
            }
        }

        // 5. Horizontal Scrollbar Track & Thumb (GDI fallback)
        int scroll_y = rack_bottom - 22;
        int scroll_h = 14;
        RECT scroll_track_rc{start_x, scroll_y, start_x + total_seq_w, scroll_y + scroll_h};
        GuiRenderer::draw_rounded_box(mem_dc_, scroll_track_rc, RGB(20, 24, 30), RGB(40, 48, 60), 3);

        RECT scroll_lbl_rc{25, scroll_y - 2, start_x - 15, scroll_y + scroll_h + 2};
        SelectObject(mem_dc_, font_small_);
        GuiRenderer::draw_text(mem_dc_, "↔ TIMELINE SCROLL", scroll_lbl_rc, t.text_secondary, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        float max_scroll = std::max(1.0f, float(max_bars - bars_per_view));
        float thumb_w = std::max(35.0f, (float(bars_per_view) / float(max_bars)) * float(total_seq_w));
        float scroll_ratio = std::clamp(float(sequencer_scroll_bar_) / max_scroll, 0.0f, 1.0f);
        float thumb_x = float(start_x) + scroll_ratio * (float(total_seq_w) - thumb_w);
        RECT thumb_rc{static_cast<int>(thumb_x), scroll_y + 1, static_cast<int>(thumb_x + thumb_w), scroll_y + scroll_h - 1};
        COLORREF thumb_col = dragging_seq_scrollbar_ ? RGB(255, 180, 40) : t.accent_orange;
        GuiRenderer::draw_rounded_box(mem_dc_, thumb_rc, thumb_col, RGB(255, 200, 70), 3);
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
        if (x >= 215 && x <= 285 && y >= 12 && y <= 48) {
            start_playback_from_spm();
            return;
        }

        // 2. PAUSE Button
        if (x >= 292 && x <= 362 && y >= 12 && y <= 48) {
            pause_playback();
            return;
        }

        // 3. STOP Button
        if (x >= 369 && x <= 434 && y >= 12 && y <= 48) {
            stop_playback();
            return;
        }

        // 4. Tempo - / +
        if (x >= 445 && x <= 470 && y >= 15 && y <= 45) {
            double bpm = engine_.session().project().time_map().get_bpm_at(0);
            engine_.session().project().time_map().set_tempo(std::max(20.0, bpm - 1.0));
            return;
        }
        if (x >= 570 && x <= 595 && y >= 15 && y <= 45) {
            double bpm = engine_.session().project().time_map().get_bpm_at(0);
            engine_.session().project().time_map().set_tempo(std::min(999.0, bpm + 1.0));
            return;
        }

        // 5. View Mode Switcher: [ 🎛 RACK ] / [ 🎹 PIANO ROLL ]
        if (x >= 775 && x <= 865 && y >= 15 && y <= 45) {
            view_mode_ = ViewMode::ChannelRack;
            status_message_ = "Switched to Channel Rack View (F6)";
            return;
        }
        if (x >= 875 && x <= 985 && y >= 15 && y <= 45) {
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
        int ruler_h = 24;
        auto ppq = engine_.session().project().time_map().ppq();
        auto bar_ticks = 4 * ppq;
        auto& proj = engine_.session().project();

        int bars_per_view = get_bars_per_view();
        int max_bars = get_max_sequencer_bars();
        int start_x = 265;
        int total_seq_w = (client_w_ - 25) - start_x;
        int step_h = 52;
        domain::Tick view_start_tick = sequencer_scroll_bar_ * bar_ticks;
        domain::Tick view_duration = bars_per_view * bar_ticks;
        domain::Tick view_end_tick = view_start_tick + view_duration;

        // 1. Horizontal Scrollbar Track & Thumb Interaction
        int rack_bottom = client_h_ - 260;
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

        // 3. Channels List & Track Clip Interaction
        int ch_y = ruler_y + ruler_h + 4;

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

            // Playlist Lane Clip Interaction (Move Drag, Resize Drag, or Place new Clip)
            if (x >= start_x && x <= start_x + total_seq_w && y >= ch_y && y <= ch_y + step_h) {
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
                domain::Tick default_len = 4 * 4 * ppq; // 4 bars by default, matching FL Studio screenshot!

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

            ch_y += step_h + 4;
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
        int ruler_y = rack_top + 32;
        int ruler_h = 24;
        int ch_y = ruler_y + ruler_h + 4;
        int step_h = 52;
        int start_x = 265;
        int bars_per_view = get_bars_per_view();
        int total_seq_w = (client_w_ - 25) - start_x;
        auto ppq = engine_.session().project().time_map().ppq();
        auto bar_ticks = 4 * ppq;
        domain::Tick view_start_tick = sequencer_scroll_bar_ * bar_ticks;
        domain::Tick view_duration = bars_per_view * bar_ticks;
        domain::Tick view_end_tick = view_start_tick + view_duration;
        auto& proj = engine_.session().project();

        for (size_t ch_idx = 0; ch_idx < proj.channels().size(); ++ch_idx) {
            if (ch_idx < proj.tracks().size() && y >= ch_y && y <= ch_y + step_h && x >= start_x) {
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
            ch_y += step_h + 4;
        }
    }

    void on_passive_mouse_move(int x, int y) {
        int rack_top = 70;
        int ruler_y = rack_top + 32;
        int ruler_h = 24;
        int ch_y = ruler_y + ruler_h + 4;
        int step_h = 52;
        int start_x = 265;
        int bars_per_view = get_bars_per_view();
        int total_seq_w = (client_w_ - 25) - start_x;
        auto ppq = engine_.session().project().time_map().ppq();
        auto bar_ticks = 4 * ppq;
        domain::Tick view_start_tick = sequencer_scroll_bar_ * bar_ticks;
        domain::Tick view_duration = bars_per_view * bar_ticks;
        domain::Tick view_end_tick = view_start_tick + view_duration;
        auto& proj = engine_.session().project();

        bool hovering_edge = false;
        for (size_t ch_idx = 0; ch_idx < proj.channels().size(); ++ch_idx) {
            if (ch_idx < proj.tracks().size() && y >= ch_y && y <= ch_y + step_h && x >= start_x) {
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
            ch_y += step_h + 4;
        }

        int rack_bottom = client_h_ - 260;
        int scroll_y = rack_bottom - 22;
        int scroll_h = 16;
        is_hovering_seq_scrollbar_ = (x >= start_x && x <= start_x + total_seq_w && y >= scroll_y - 2 && y <= scroll_y + scroll_h + 2);

        if (is_hovering_seq_scrollbar_) {
            SetCursor(LoadCursor(NULL, IDC_SIZEWE));
            return;
        }

        if (is_hovering_clip_edge_ != hovering_edge) {
            is_hovering_clip_edge_ = hovering_edge;
            SetCursor(LoadCursor(NULL, hovering_edge ? IDC_SIZEWE : IDC_ARROW));
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
        if (dragging_seq_scrollbar_) {
            int max_bars = get_max_sequencer_bars();
            int bars_per_view = get_bars_per_view();
            float max_scroll = std::max(1.0f, float(max_bars - bars_per_view));
            int start_x = 265;
            int total_seq_w = (client_w_ - 25) - start_x;
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

        if (clip_drag_mode_ == ClipDragMode::Move) {
            auto& proj = engine_.session().project();
            if (drag_clip_track_idx_ < proj.tracks().size()) {
                auto& track = proj.tracks()[drag_clip_track_idx_];
                if (drag_clip_idx_ < track.clips().size()) {
                    auto& clip = track.clips_mut()[drag_clip_idx_];
                    auto ppq = engine_.session().project().time_map().ppq();
                    auto bar_ticks = 4 * ppq;
                    int bars_per_view = get_bars_per_view();
                    int total_seq_w = (client_w_ - 25) - 265;
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
                    int total_seq_w = (client_w_ - 25) - 265;
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
                int bars_per_view = get_bars_per_view();
                int total_seq_w = (client_w_ - 25) - 265;
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
    bool dragging_seq_scrollbar_{false};
    float drag_seq_scroll_start_mouse_x_{0.0f};
    int drag_seq_scroll_orig_bar_{0};
    bool is_hovering_seq_scrollbar_{false};
    bool is_fullscreen_{false};
    RECT saved_win_rect_{};
    DWORD saved_win_style_{0};
    std::array<float, 8> meter_peaks_{0.0f}; // Real-time audio peaks for tracks 0..4
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
