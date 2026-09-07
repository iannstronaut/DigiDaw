#pragma once

#include "../../app/engine.hpp"
#include "../../domain/sequencing/channel_rack_layout.hpp"
#include "theme.hpp"
#include "gui_renderer.hpp"
#include "d2d_renderer.hpp"
#include "d3d_shader_visualizer.hpp"
#include "dpi_awareness.hpp"
#include "audioclip_editor.hpp"
#include "xaudio_editor.hpp"
#include "../plugins/audioclip_device.hpp"
#include "../plugins/xaudio_devices.hpp"
#include "../../app/usecases/sample_library.hpp"
#include "../../app/ports/config_store.hpp"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <chrono>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1
#define DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 19
#endif

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif

#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif

#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif

namespace digidaw::adapters::gui {

enum class ViewMode : uint8_t {
    ChannelRack,
    PianoRoll
};

enum class WindowId : uint8_t {
    ChannelRack = 0,
    Playlist = 1,
    PianoRoll = 2,
    Mixer = 3,
    Inspector = 4,
    AudioLibrary = 5
};

enum class ResizeEdge : uint8_t {
    None = 0,
    Left, Right, Top, Bottom,
    TopLeft, TopRight, BottomLeft, BottomRight
};

enum class WindowDragMode : uint8_t {
    None = 0,
    Move,
    Resize
};

struct DawWindow {
    WindowId id{WindowId::Playlist};
    std::string title{"Window"};
    SvgIconType icon{SvgIconType::Playlist};
    float x{10.0f};
    float y{52.0f};
    float w{800.0f};
    float h{420.0f};
    float min_w{340.0f};
    float min_h{200.0f};
    bool visible{true};

    static constexpr float kTitleBarHeight = 28.0f;
    static constexpr float kBorderMargin = 6.0f;

    D2D1_RECT_F get_rect() const {
        return D2D1::RectF(x, y, x + w, y + h);
    }
    D2D1_RECT_F get_title_bar_rect() const {
        return D2D1::RectF(x, y, x + w, y + kTitleBarHeight);
    }
    D2D1_RECT_F get_content_rect() const {
        return D2D1::RectF(x, y + kTitleBarHeight, x + w, y + h);
    }
    D2D1_RECT_F get_close_btn_rect() const {
        return D2D1::RectF(x + w - 24.0f, y + 4.0f, x + w - 4.0f, y + kTitleBarHeight - 4.0f);
    }

    bool contains(float px, float py) const {
        return (px >= x && px <= x + w && py >= y && py <= y + h);
    }

    bool is_in_title_bar(float px, float py) const {
        return (px >= x && px <= x + w && py >= y && py <= y + kTitleBarHeight);
    }

    bool is_in_close_btn(float px, float py) const {
        auto rc = get_close_btn_rect();
        return (px >= rc.left && px <= rc.right && py >= rc.top && py <= rc.bottom);
    }
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

    explicit DigiDawWindow(app::Engine& engine, app::IConfigStore* config = nullptr, app::SampleLibrary init_lib = {})
        : engine_(engine), config_(config), sample_library_(std::move(init_lib)) {
        instance = this;
        if (sample_library_.empty() && config_) {
            std::string saved = config_->get_string("SampleLibrary", "RootDirectory");
            if (!saved.empty() && std::filesystem::exists(saved)) {
                sample_library_.scan(saved);
            }
        }
    }

    void set_sample_library(app::SampleLibrary lib) {
        sample_library_ = std::move(lib);
        audio_lib_scroll_idx_ = 0;
        selected_sample_idx_ = -1;
        hover_sample_idx_ = -1;
    }

    void set_config(app::IConfigStore* cfg) {
        config_ = cfg;
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

        if (dwrite_main_) dwrite_main_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        if (dwrite_bold_) dwrite_bold_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        if (dwrite_title_) dwrite_title_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        if (dwrite_small_) dwrite_small_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
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

    static void apply_system_window_theme(HWND hwnd) {
        if (!hwnd) return;

        // 1. Enable Immersive Dark Mode for Windows 10 (1809+) & Windows 11
        // Attribute 20 (Win10 20H1+ & Win11), Attribute 19 (Win10 1809-1909)
        BOOL dark_mode = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark_mode, sizeof(dark_mode));
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1, &dark_mode, sizeof(dark_mode));

        // 2. Custom Title Bar Background Color (Windows 11 build 22000+)
        // Matches theme bg_header / bg_surface_2 (#111316 -> RGB(17, 19, 22))
        COLORREF caption_color = RGB(17, 19, 22);
        DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &caption_color, sizeof(caption_color));

        // 3. Custom Title Bar Text Color (Windows 11 build 22000+)
        // Matches theme text_primary (#e8e8ea -> RGB(232, 232, 234))
        COLORREF text_color = RGB(232, 232, 234);
        DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &text_color, sizeof(text_color));

        // 4. Custom Window Frame Border Color (Windows 11 build 22000+)
        // Matches theme border_subtle (#202328 -> RGB(32, 35, 40))
        COLORREF border_color = RGB(32, 35, 40);
        DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border_color, sizeof(border_color));

        // 5. Modern Rounded Window Corners (Windows 11 build 22000+)
        // DWMWCP_ROUND = 2
        DWORD corner_preference = 2;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner_preference, sizeof(corner_preference));
    }

    void update_window_title() {
        if (!hwnd_) return;
        std::string name = engine_.session().project().name();
        if (name.empty()) name = "Untitled Project";
        std::wstring wname(name.begin(), name.end());
        std::wstring title = L"DigiDAW 2026                                                                    —   [ " + wname + L" ]";
        SetWindowTextW(hwnd_, title.c_str());
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

        // Apply theme-matching colors to Windows OS Title Bar / Header (DWM)
        apply_system_window_theme(hwnd_);
        update_window_title();

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
            init_default_window_layout();
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

    app::Engine& engine_;
    app::IConfigStore* config_{nullptr};

    DawWindow win_channel_rack_{WindowId::ChannelRack, "CHANNEL RACK", SvgIconType::ChannelRack, 8.0f, 52.0f, 1856.0f, 400.0f, 380.0f, 220.0f, true};
    DawWindow win_playlist_{WindowId::Playlist, "PLAYLIST", SvgIconType::Playlist, 556.0f, 52.0f, 480.0f, 400.0f, 360.0f, 220.0f, true};
    DawWindow win_pianoroll_{WindowId::PianoRoll, "PIANO ROLL", SvgIconType::PianoRoll, 24.0f, 64.0f, 780.0f, 420.0f, 460.0f, 240.0f, false};
    DawWindow win_mixer_{WindowId::Mixer, "MIXER", SvgIconType::Mixer, 8.0f, 460.0f, 800.0f, 260.0f, 360.0f, 200.0f, true};
    DawWindow win_inspector_{WindowId::Inspector, "TRACK FX / INSPECTOR", SvgIconType::TrackFx, 816.0f, 52.0f, 260.0f, 688.0f, 240.0f, 260.0f, true};
    DawWindow win_audio_library_{WindowId::AudioLibrary, "AUDIO LIBRARY", SvgIconType::Folder, 1084.0f, 52.0f, 320.0f, 540.0f, 260.0f, 200.0f, true};

    std::vector<WindowId> z_order_{WindowId::AudioLibrary, WindowId::Mixer, WindowId::Playlist, WindowId::ChannelRack, WindowId::Inspector, WindowId::PianoRoll};
    WindowId active_window_{WindowId::ChannelRack};

    // Audio Library Browser State & Drag-and-Drop
    app::SampleLibrary sample_library_;
    int audio_lib_scroll_idx_{0};
    int hover_sample_idx_{-1};
    int selected_sample_idx_{-1};
    bool is_potential_sample_drag_{false};
    bool is_dragging_sample_{false};
    std::string dragged_sample_path_;
    std::string dragged_sample_name_;
    int sample_down_idx_{-1};
    int sample_down_x_{0};
    int sample_down_y_{0};
    int current_mouse_x_{0};
    int current_mouse_y_{0};

    bool magnet_enabled_{true};
    bool is_dragging_window_{false};
    WindowId dragging_window_id_{WindowId::ChannelRack};
    WindowDragMode window_drag_mode_{WindowDragMode::None};
    ResizeEdge resize_edge_{ResizeEdge::None};
    int drag_win_start_mouse_x_{0};
    int drag_win_start_mouse_y_{0};
    float drag_win_orig_x_{0.0f};
    float drag_win_orig_y_{0.0f};
    float drag_win_orig_w_{0.0f};
    float drag_win_orig_h_{0.0f};
    bool layout_initialized_{false};
    int hover_close_win_id_{-1};

    DawWindow* get_window(WindowId id) {
        switch (id) {
            case WindowId::ChannelRack: return &win_channel_rack_;
            case WindowId::Playlist: return &win_playlist_;
            case WindowId::PianoRoll: return &win_pianoroll_;
            case WindowId::Mixer: return &win_mixer_;
            case WindowId::Inspector: return &win_inspector_;
            case WindowId::AudioLibrary: return &win_audio_library_;
        }
        return nullptr;
    }

    const DawWindow* get_window(WindowId id) const {
        switch (id) {
            case WindowId::ChannelRack: return &win_channel_rack_;
            case WindowId::Playlist: return &win_playlist_;
            case WindowId::PianoRoll: return &win_pianoroll_;
            case WindowId::Mixer: return &win_mixer_;
            case WindowId::Inspector: return &win_inspector_;
            case WindowId::AudioLibrary: return &win_audio_library_;
        }
        return nullptr;
    }

    std::array<DawWindow*, 6> get_all_windows() {
        return {&win_channel_rack_, &win_playlist_, &win_pianoroll_, &win_mixer_, &win_inspector_, &win_audio_library_};
    }

    std::array<const DawWindow*, 6> get_all_windows() const {
        return {&win_channel_rack_, &win_playlist_, &win_pianoroll_, &win_mixer_, &win_inspector_, &win_audio_library_};
    }

    void bring_to_front(WindowId id) {
        auto it = std::find(z_order_.begin(), z_order_.end(), id);
        if (it != z_order_.end()) {
            z_order_.erase(it);
        }
        z_order_.push_back(id);
        active_window_ = id;
        auto* w = get_window(id);
        if (w) w->visible = true;
        if (id == WindowId::Inspector) inspector_open_ = true;
    }

    void update_top_active_window() {
        for (auto it = z_order_.rbegin(); it != z_order_.rend(); ++it) {
            auto* w = get_window(*it);
            if (w && w->visible) {
                active_window_ = *it;
                return;
            }
        }
    }

    void toggle_or_focus_window(WindowId id) {
        auto* win = get_window(id);
        if (!win) return;

        if (!win->visible) {
            win->visible = true;
            bring_to_front(id);
            status_message_ = "Opened and focused " + win->title;
        } else {
            if (active_window_ == id) {
                win->visible = false;
                if (id == WindowId::Inspector) inspector_open_ = false;
                update_top_active_window();
                status_message_ = "Closed " + win->title;
            } else {
                bring_to_front(id);
                status_message_ = "Focused " + win->title + " (Brought to front)";
            }
        }
    }

    void toggle_playback_mode() {
        if (engine_.transport().mode() == app::PlaybackMode::Pattern) {
            engine_.transport().set_mode(app::PlaybackMode::Song);
            status_message_ = "Switched to SONG Mode (Arrangement Playlist)";
        } else {
            engine_.transport().set_mode(app::PlaybackMode::Pattern);
            status_message_ = "Switched to PATTERN Mode (Active Pattern Loop)";
        }
    }

    void select_pattern(uint32_t pat_id) {
        auto* pat = engine_.session().project().get_pattern(pat_id);
        if (pat) {
            engine_.session().project().ui_state().selected_pattern_id = pat_id;
            status_message_ = "Selected Pattern: " + pat->name();
        }
    }

    void prev_pattern() {
        auto& proj = engine_.session().project();
        if (proj.patterns().empty()) return;
        uint32_t cur = proj.ui_state().selected_pattern_id;
        for (size_t i = 0; i < proj.patterns().size(); ++i) {
            if (proj.patterns()[i].id() == cur) {
                size_t prev_idx = (i == 0) ? (proj.patterns().size() - 1) : (i - 1);
                select_pattern(proj.patterns()[prev_idx].id());
                return;
            }
        }
        select_pattern(proj.patterns().front().id());
    }

    void next_pattern() {
        auto& proj = engine_.session().project();
        if (proj.patterns().empty()) return;
        uint32_t cur = proj.ui_state().selected_pattern_id;
        for (size_t i = 0; i < proj.patterns().size(); ++i) {
            if (proj.patterns()[i].id() == cur) {
                size_t next_idx = (i + 1) % proj.patterns().size();
                select_pattern(proj.patterns()[next_idx].id());
                return;
            }
        }
        select_pattern(proj.patterns().front().id());
    }

    void add_new_pattern() {
        auto& proj = engine_.session().project();
        uint32_t new_id = proj.add_pattern();
        select_pattern(new_id);
        status_message_ = "Created New Pattern " + std::to_string(new_id);
    }

    void delete_current_pattern() {
        auto& proj = engine_.session().project();
        if (proj.patterns().size() <= 1) {
            status_message_ = "Cannot delete the only remaining pattern";
            return;
        }
        uint32_t cur = proj.ui_state().selected_pattern_id;
        proj.remove_pattern(cur);
        select_pattern(proj.patterns().front().id());
        status_message_ = "Deleted pattern";
    }

    static bool is_channel_piano_roll(const domain::NoteSet& notes, domain::Tick ppq) {
        return domain::ChannelRackLayout::is_channel_piano_roll(notes, ppq);
    }

    domain::Pattern* get_active_pattern() {
        auto& proj = engine_.session().project();
        auto* pat = proj.get_pattern(proj.ui_state().selected_pattern_id);
        if (!pat && !proj.patterns().empty()) pat = &proj.patterns().front();
        return pat;
    }

    const domain::Pattern* get_active_pattern() const {
        const auto& proj = engine_.session().project();
        const auto* pat = proj.get_pattern(proj.ui_state().selected_pattern_id);
        if (!pat && !proj.patterns().empty()) pat = &proj.patterns().front();
        return pat;
    }

    static constexpr float kPadComfortableWidth = domain::ChannelRackLayout::kPadComfortableWidth; // comfortable width ~24px - 26px

    [[nodiscard]] int get_channel_rack_num_bars(float grid_w) const noexcept {
        return domain::ChannelRackLayout::num_bars_for_width(grid_w);
    }

    [[nodiscard]] int get_channel_rack_num_steps(float grid_w) const noexcept {
        return domain::ChannelRackLayout::num_steps_for_width(grid_w);
    }

    [[nodiscard]] float get_channel_rack_pad_width(float grid_w, int num_steps) const noexcept {
        return domain::ChannelRackLayout::pad_width(grid_w, num_steps);
    }

    ResizeEdge get_resize_edge(const DawWindow& win, float px, float py) const {
        if (!win.contains(px, py)) return ResizeEdge::None;
        const float m = DawWindow::kBorderMargin;

        bool on_l = (px >= win.x && px <= win.x + m);
        bool on_r = (px >= win.x + win.w - m && px <= win.x + win.w);
        bool on_t = (py >= win.y && py <= win.y + m);
        bool on_b = (py >= win.y + win.h - m && py <= win.y + win.h);

        if (on_t && on_l) return ResizeEdge::TopLeft;
        if (on_t && on_r) return ResizeEdge::TopRight;
        if (on_b && on_l) return ResizeEdge::BottomLeft;
        if (on_b && on_r) return ResizeEdge::BottomRight;
        if (on_l) return ResizeEdge::Left;
        if (on_r) return ResizeEdge::Right;
        if (on_t) return ResizeEdge::Top;
        if (on_b) return ResizeEdge::Bottom;

        return ResizeEdge::None;
    }

    void apply_magnet_snapping(float& new_x, float& new_y, float w, float h, WindowId current_id) {
        if (!magnet_enabled_) return;
        const float kSnapDist = 14.0f;

        float ws_l = 8.0f;
        float ws_t = 52.0f;
        float ws_r = static_cast<float>(client_w_ - 8);
        float ws_b = static_cast<float>(client_h_ - 30);

        // X-axis snapping
        // 1. Workspace edges
        if (std::abs(new_x - ws_l) < kSnapDist) {
            new_x = ws_l;
        } else if (std::abs((new_x + w) - ws_r) < kSnapDist) {
            new_x = ws_r - w;
        }

        // 2. Other windows
        for (const auto* other : get_all_windows()) {
            if (!other || other->id == current_id || !other->visible) continue;

            if (std::abs(new_x - (other->x + other->w)) < kSnapDist) {
                new_x = other->x + other->w;
            } else if (std::abs((new_x + w) - other->x) < kSnapDist) {
                new_x = other->x - w;
            } else if (std::abs(new_x - other->x) < kSnapDist) {
                new_x = other->x;
            } else if (std::abs((new_x + w) - (other->x + other->w)) < kSnapDist) {
                new_x = other->x + other->w - w;
            }
        }

        // Y-axis snapping
        // 1. Workspace edges
        if (std::abs(new_y - ws_t) < kSnapDist) {
            new_y = ws_t;
        } else if (std::abs((new_y + h) - ws_b) < kSnapDist) {
            new_y = ws_b - h;
        }

        // 2. Other windows
        for (const auto* other : get_all_windows()) {
            if (!other || other->id == current_id || !other->visible) continue;

            if (std::abs(new_y - (other->y + other->h)) < kSnapDist) {
                new_y = other->y + other->h;
            } else if (std::abs((new_y + h) - other->y) < kSnapDist) {
                new_y = other->y - h;
            } else if (std::abs(new_y - other->y) < kSnapDist) {
                new_y = other->y;
            } else if (std::abs((new_y + h) - (other->y + other->h)) < kSnapDist) {
                new_y = other->y + other->h - h;
            }
        }
    }

    void apply_resize_dragging(DawWindow& win, float dx, float dy) {
        const float kSnapDist = 14.0f;
        float ws_l = 8.0f;
        float ws_t = 52.0f;
        float ws_r = static_cast<float>(client_w_ - 8);
        float ws_b = static_cast<float>(client_h_ - 30);

        float new_x = drag_win_orig_x_;
        float new_y = drag_win_orig_y_;
        float new_w = drag_win_orig_w_;
        float new_h = drag_win_orig_h_;

        // Handle horizontal resizing
        if (resize_edge_ == ResizeEdge::Right || resize_edge_ == ResizeEdge::TopRight || resize_edge_ == ResizeEdge::BottomRight) {
            float cand_r = drag_win_orig_x_ + drag_win_orig_w_ + dx;
            if (magnet_enabled_) {
                if (std::abs(cand_r - ws_r) < kSnapDist) cand_r = ws_r;
                for (const auto* other : get_all_windows()) {
                    if (!other || other->id == win.id || !other->visible) continue;
                    if (std::abs(cand_r - other->x) < kSnapDist) cand_r = other->x;
                    else if (std::abs(cand_r - (other->x + other->w)) < kSnapDist) cand_r = other->x + other->w;
                }
            }
            cand_r = std::min(cand_r, ws_r);
            new_w = std::max(win.min_w, cand_r - new_x);
        } else if (resize_edge_ == ResizeEdge::Left || resize_edge_ == ResizeEdge::TopLeft || resize_edge_ == ResizeEdge::BottomLeft) {
            float cand_l = drag_win_orig_x_ + dx;
            if (magnet_enabled_) {
                if (std::abs(cand_l - ws_l) < kSnapDist) cand_l = ws_l;
                for (const auto* other : get_all_windows()) {
                    if (!other || other->id == win.id || !other->visible) continue;
                    if (std::abs(cand_l - (other->x + other->w)) < kSnapDist) cand_l = other->x + other->w;
                    else if (std::abs(cand_l - other->x) < kSnapDist) cand_l = other->x;
                }
            }
            cand_l = std::max(cand_l, ws_l);
            float right_pos = drag_win_orig_x_ + drag_win_orig_w_;
            if (right_pos - cand_l >= win.min_w) {
                new_x = cand_l;
                new_w = right_pos - cand_l;
            } else {
                new_x = right_pos - win.min_w;
                new_w = win.min_w;
            }
        }

        // Handle vertical resizing
        if (resize_edge_ == ResizeEdge::Bottom || resize_edge_ == ResizeEdge::BottomLeft || resize_edge_ == ResizeEdge::BottomRight) {
            float cand_b = drag_win_orig_y_ + drag_win_orig_h_ + dy;
            if (magnet_enabled_) {
                if (std::abs(cand_b - ws_b) < kSnapDist) cand_b = ws_b;
                for (const auto* other : get_all_windows()) {
                    if (!other || other->id == win.id || !other->visible) continue;
                    if (std::abs(cand_b - other->y) < kSnapDist) cand_b = other->y;
                    else if (std::abs(cand_b - (other->y + other->h)) < kSnapDist) cand_b = other->y + other->h;
                }
            }
            cand_b = std::min(cand_b, ws_b);
            new_h = std::max(win.min_h, cand_b - new_y);
        } else if (resize_edge_ == ResizeEdge::Top || resize_edge_ == ResizeEdge::TopLeft || resize_edge_ == ResizeEdge::TopRight) {
            float cand_t = drag_win_orig_y_ + dy;
            if (magnet_enabled_) {
                if (std::abs(cand_t - ws_t) < kSnapDist) cand_t = ws_t;
                for (const auto* other : get_all_windows()) {
                    if (!other || other->id == win.id || !other->visible) continue;
                    if (std::abs(cand_t - (other->y + other->h)) < kSnapDist) cand_t = other->y + other->h;
                    else if (std::abs(cand_t - other->y) < kSnapDist) cand_t = other->y;
                }
            }
            cand_t = std::max(cand_t, ws_t);
            float bottom_pos = drag_win_orig_y_ + drag_win_orig_h_;
            if (bottom_pos - cand_t >= win.min_h) {
                new_y = cand_t;
                new_h = bottom_pos - cand_t;
            } else {
                new_y = bottom_pos - win.min_h;
                new_h = win.min_h;
            }
        }

        win.x = new_x;
        win.y = new_y;
        win.w = new_w;
        win.h = new_h;
    }

    void init_default_window_layout() {
        if (client_w_ <= 0 || client_h_ <= 0) return;

        float top_bound = 52.0f;
        float bot_bound = std::max(top_bound + 100.0f, static_cast<float>(client_h_ - 30));
        float ws_h = bot_bound - top_bound;
        float ws_w = std::max(300.0f, static_cast<float>(client_w_ - 16));

        float insp_w = 260.0f;
        float gap = 6.0f;
        float main_w = std::max(420.0f, ws_w - insp_w - gap);
        float upper_h = std::max(220.0f, std::round(ws_h * 0.58f));
        float mixer_h = std::max(200.0f, ws_h - upper_h - gap);

        // 1. Channel Rack: default window width shows 4 bars (64 steps @ 25px pad = 1600px grid + 256px controls = 1856px)
        float default_rack_w = std::min(ws_w, 1856.0f);
        win_channel_rack_.x = 8.0f;
        win_channel_rack_.y = top_bound;
        win_channel_rack_.w = default_rack_w;
        win_channel_rack_.h = upper_h;
        win_channel_rack_.visible = true;

        // 2. Playlist: Upper arrangement area
        win_playlist_.x = 8.0f;
        win_playlist_.y = top_bound;
        win_playlist_.w = main_w;
        win_playlist_.h = upper_h;
        win_playlist_.visible = true;

        // 3. Mixer: Bottom Left (docked under Channel Rack & Playlist)
        win_mixer_.x = 8.0f;
        win_mixer_.y = top_bound + upper_h + gap;
        win_mixer_.w = main_w;
        win_mixer_.h = mixer_h;
        win_mixer_.visible = true;

        // 4. Inspector: Right column (docked to right edge)
        win_inspector_.x = 8.0f + main_w + gap;
        win_inspector_.y = top_bound;
        win_inspector_.w = insp_w;
        win_inspector_.h = ws_h;
        win_inspector_.visible = true;
        inspector_open_ = true;

        // 5. Piano Roll: Center workspace (initially hidden)
        win_pianoroll_.x = 24.0f;
        win_pianoroll_.y = top_bound + 16.0f;
        win_pianoroll_.w = std::max(460.0f, main_w - 32.0f);
        win_pianoroll_.h = std::max(240.0f, ws_h - 32.0f);
        win_pianoroll_.visible = false;

        // 6. Audio Library: Dockable Sample Browser Window
        float lib_w = 280.0f;
        win_audio_library_.x = std::max(8.0f, win_inspector_.x - lib_w - gap);
        win_audio_library_.y = top_bound + 20.0f;
        win_audio_library_.w = lib_w;
        win_audio_library_.h = std::max(220.0f, ws_h - 40.0f);
        win_audio_library_.visible = true;

        active_window_ = WindowId::ChannelRack;
        layout_initialized_ = true;
    }

    void clamp_windows_to_screen() {
        float top_bound = 52.0f;
        float bot_bound = static_cast<float>(client_h_ - 30);
        float right_bound = static_cast<float>(client_w_ - 8);

        for (auto* win : get_all_windows()) {
            if (!win) continue;
            if (win->x + win->w > right_bound) {
                win->x = std::max(8.0f, right_bound - win->w);
            }
            if (win->y + win->h > bot_bound) {
                win->y = std::max(top_bound, bot_bound - win->h);
            }
        }
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

        void init(float win_x, float win_y, float win_w, float win_h, int num_pitches, int visible_steps) {
            rack_top = win_y + DawWindow::kTitleBarHeight;
            rack_bottom = win_y + win_h;
            toolbar_y = rack_top + 4.0f;
            toolbar_h = 24.0f;
            ruler_y = toolbar_y + toolbar_h + 4.0f;
            ruler_h = 20.0f;
            grid_top = ruler_y + ruler_h + 2.0f;
            h_scroll_h = 16.0f;
            v_scroll_w = 16.0f;
            grid_bottom = rack_bottom - h_scroll_h - 4.0f;
            grid_h = std::max(60.0f, grid_bottom - grid_top);
            piano_x = win_x + 8.0f;
            piano_w = 64.0f;
            grid_x = piano_x + piano_w + 2.0f;
            v_scroll_x = (win_x + win_w - 8.0f) - v_scroll_w;
            grid_w = std::max(100.0f, v_scroll_x - 4.0f - grid_x);
            h_scroll_y = grid_bottom + 2.0f;
            row_h = grid_h / static_cast<float>(num_pitches);
            step_w = grid_w / static_cast<float>(visible_steps);
        }

        void init(const DawWindow& win, int num_pitches, int visible_steps) {
            init(win.x, win.y, win.w, win.h, num_pitches, visible_steps);
        }

        void init(int client_w, int client_h, int num_pitches, int visible_steps) {
            init(10.0f, 52.0f, static_cast<float>(client_w - 20), static_cast<float>(client_h - 320), num_pitches, visible_steps);
        }
    };

    int get_max_piano_roll_steps() const {
        int max_s = 64; // Default 4 bars (64 sixteenth notes)
        auto* pat = get_active_pattern();
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
                if (w > 0 && h > 0) {
                    client_w_ = w;
                    client_h_ = h;
                    if (!layout_initialized_) {
                        init_default_window_layout();
                    } else {
                        clamp_windows_to_screen();
                    }
                }
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
                    client_w_ = w;
                    client_h_ = h;
                    clamp_windows_to_screen();
                    if (use_d2d_d3d_) {
                        resize_gpu_buffers(w, h);
                    } else {
                        resize_backbuffer(w, h);
                    }
                }
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            case WM_SETTINGCHANGE:
            case WM_THEMECHANGED: {
                apply_system_window_theme(hwnd);
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
                current_mouse_x_ = mouse_x;
                current_mouse_y_ = mouse_y;
                if (is_potential_sample_drag_) {
                    int dx = mouse_x - sample_down_x_;
                    int dy = mouse_y - sample_down_y_;
                    if (dx * dx + dy * dy > 25) {
                        is_dragging_sample_ = true;
                    }
                }
                if (is_mouse_down_) {
                    on_mouse_move(mouse_x, mouse_y);
                    InvalidateRect(hwnd, NULL, FALSE);
                } else {
                    on_passive_mouse_move(mouse_x, mouse_y);
                }
                return 0;
            }

            case WM_SETCURSOR: {
                if (is_dragging_sample_) {
                    SetCursor(LoadCursor(NULL, IDC_CROSS));
                    return TRUE;
                }
                if (is_dragging_window_) {
                    if (window_drag_mode_ == WindowDragMode::Move) {
                        SetCursor(LoadCursor(NULL, IDC_SIZEALL));
                        return TRUE;
                    }
                    if (window_drag_mode_ == WindowDragMode::Resize) {
                        switch (resize_edge_) {
                            case ResizeEdge::Left:
                            case ResizeEdge::Right:
                                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                                return TRUE;
                            case ResizeEdge::Top:
                            case ResizeEdge::Bottom:
                                SetCursor(LoadCursor(NULL, IDC_SIZENS));
                                return TRUE;
                            case ResizeEdge::TopLeft:
                            case ResizeEdge::BottomRight:
                                SetCursor(LoadCursor(NULL, IDC_SIZENWSE));
                                return TRUE;
                            case ResizeEdge::TopRight:
                            case ResizeEdge::BottomLeft:
                                SetCursor(LoadCursor(NULL, IDC_SIZENESW));
                                return TRUE;
                            default:
                                break;
                        }
                    }
                }
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
                return DefWindowProc(hwnd, msg, wp, lp);
            }

            case WM_LBUTTONUP: {
                is_mouse_down_ = false;
                if (is_dragging_window_) {
                    is_dragging_window_ = false;
                    window_drag_mode_ = WindowDragMode::None;
                    resize_edge_ = ResizeEdge::None;
                }
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
                dragging_channel_pan_idx_ = -1;
                dragging_channel_vol_idx_ = -1;
                dragging_channel_target_track_idx_ = -1;
                dragging_clipper_knob_ = ClipperKnobId::None;
                dragging_xsynth_param_idx_ = -1;
                dragging_effect_param_idx_ = -1;
                note_drag_mode_ = NoteDragMode::None;
                if (clip_drag_mode_ != ClipDragMode::None) {
                    if (drag_clip_track_idx_ < engine_.session().project().tracks().size()) {
                        engine_.session().project().tracks()[drag_clip_track_idx_].sort_clips();
                    }
                    clip_drag_mode_ = ClipDragMode::None;
                }
                if (is_dragging_sample_) {
                    is_dragging_sample_ = false;
                    is_potential_sample_drag_ = false;
                    int drop_x = LOWORD(lp);
                    int drop_y = HIWORD(lp);
                    handle_sample_drop(dragged_sample_path_, dragged_sample_name_, drop_x, drop_y);
                } else if (is_potential_sample_drag_) {
                    is_potential_sample_drag_ = false;
                    if (sample_down_idx_ >= 0 && sample_down_idx_ < static_cast<int>(sample_library_.size())) {
                        audition_sample(sample_library_.samples()[sample_down_idx_].path);
                    }
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

                if (active_editor_effect_track_ >= 0 && active_editor_effect_slot_ >= 0) {
                    if (mouse_pt.x >= effect_editor_bounds_.left && mouse_pt.x <= effect_editor_bounds_.right &&
                        mouse_pt.y >= effect_editor_bounds_.top && mouse_pt.y <= effect_editor_bounds_.bottom) {
                        auto* track = engine_.session().project().mixer_graph().get_track(static_cast<uint32_t>(active_editor_effect_track_));
                        if (track && static_cast<size_t>(active_editor_effect_slot_) < track->inserts().size()) {
                            auto& ins = track->inserts()[active_editor_effect_slot_];
                            if (ins.device) {
                                XAudioEditor::handle_effect_wheel(ins.device.get(), effect_editor_bounds_, mouse_pt.x, mouse_pt.y, steps, status_message_);
                            }
                        }
                        effect_editor_scroll_idx_ = std::max(0, effect_editor_scroll_idx_ - steps);
                        InvalidateRect(hwnd, &effect_editor_bounds_, FALSE);
                        break;
                    }
                }

                for (auto it = z_order_.rbegin(); it != z_order_.rend(); ++it) {
                    auto* win = get_window(*it);
                    if (!win || !win->visible) continue;
                    if (!win->contains(static_cast<float>(mouse_pt.x), static_cast<float>(mouse_pt.y))) continue;

                    if (*it == WindowId::Mixer) {
                        auto& proj = engine_.session().project();
                        float track_w = 96.0f;
                        float gap = 10.0f;
                        float master_x = win->x + 12.0f;
                        float insert_start_x = master_x + track_w + gap;
                        float avail_w = (win->x + win->w - 12.0f) - insert_start_x;
                        int vis_inserts = std::max(1, static_cast<int>(avail_w / (track_w + gap)));
                        size_t total_inserts = std::max(size_t(4), proj.channels().size());
                        int max_mix_scroll = std::max(0, static_cast<int>(total_inserts) - vis_inserts);
                        if (max_mix_scroll > 0) {
                            mixer_scroll_track_ = std::clamp(mixer_scroll_track_ - steps, 0, max_mix_scroll);
                        }
                    } else if (*it == WindowId::Inspector) {
                        float insp_x = win->x;
                        float insp_y = win->y + DawWindow::kTitleBarHeight;
                        float insp_r = win->x + win->w;
                        float insp_bot = win->y + win->h;
                        auto* track = engine_.session().project().mixer_graph().get_track(selected_mixer_track_);
                        if (track) {
                            std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
                            // Volume slider area
                            if (mouse_pt.x >= insp_x + 14 && mouse_pt.x <= insp_r - 14 && mouse_pt.y >= insp_y + 54 && mouse_pt.y <= insp_y + 72) {
                                float new_vol = std::clamp(track->volume() + steps * 0.05f, 0.0f, 1.25f);
                                track->set_volume(new_vol);
                                int vol_pct = static_cast<int>(std::round(track->volume() * 100.0f));
                                status_message_ = (selected_mixer_track_ == 0 ? "Master" : ("Track " + std::to_string(selected_mixer_track_))) +
                                                  " Volume: " + std::to_string(vol_pct) + "%";
                            }
                            // Pan slider area
                            else if (mouse_pt.x >= insp_x + 14 && mouse_pt.x <= insp_r - 14 && mouse_pt.y >= insp_y + 76 && mouse_pt.y <= insp_y + 94) {
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
                                float slot_y_start = insp_y + 156.0f;
                                float slot_h = 32.0f;
                                float slot_gap = 4.0f;
                                float fx_area_bot = insp_bot - 8.0f;
                                float fx_avail_h = fx_area_bot - slot_y_start;
                                int vis_fx_items = std::max(1, static_cast<int>(fx_avail_h / (slot_h + slot_gap)));
                                int total_fx_items = static_cast<int>(num_fx) + (num_fx < 10 ? 1 : 0);
                                int max_fx_scroll = std::max(0, total_fx_items - vis_fx_items);
                                int right_margin = (max_fx_scroll > 0) ? 14 : 8;

                                bool adjusted_wet = false;
                                for (int i = inspector_scroll_slot_; i < static_cast<int>(num_fx); ++i) {
                                    float sy = slot_y_start + static_cast<float>(i - inspector_scroll_slot_) * (slot_h + slot_gap);
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
                    } else if (*it == WindowId::PianoRoll) {
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
                    } else if (*it == WindowId::ChannelRack) {
                        auto& proj = engine_.session().project();
                        float px = win->x;
                        float py = win->y + DawWindow::kTitleBarHeight;
                        float start_y = py + 38.0f;
                        float row_h = 38.0f;
                        int mx = mouse_pt.x;
                        int my = mouse_pt.y;

                        bool handled = false;
                        if (my >= start_y) {
                            int rel_row = static_cast<int>((my - start_y) / row_h);
                            size_t ch_idx = static_cast<size_t>(channel_rack_scroll_ch_ + rel_row);
                            if (ch_idx < proj.channels().size()) {
                                std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
                                auto& ch = proj.channels()[ch_idx];
                                float row_y = start_y + static_cast<float>(rel_row) * row_h;
                                if (mx >= px + 26 && mx <= px + 50 && my >= row_y + 4 && my <= row_y + 30) {
                                    ch.settings().pan = std::clamp(ch.settings().pan + steps * 0.05f, -1.0f, 1.0f);
                                    handled = true;
                                } else if (mx >= px + 54 && mx <= px + 78 && my >= row_y + 4 && my <= row_y + 30) {
                                    ch.settings().volume = std::clamp(ch.settings().volume + steps * 0.05f, 0.0f, domain::kMaxChannelVolume);
                                    int pct = static_cast<int>(std::round((ch.settings().volume / domain::kMaxChannelVolume) * 100.0f));
                                    status_message_ = ch.settings().name + " Volume: " + std::to_string(pct) + "%";
                                    handled = true;
                                } else if (mx >= px + 82 && mx <= px + 110 && my >= row_y + 5 && my <= row_y + 29) {
                                    int new_trk = std::clamp(static_cast<int>(ch.settings().mixer_track) + steps, 0, 64);
                                    ch.settings().mixer_track = static_cast<uint8_t>(new_trk);
                                    if (new_trk > 0 && !proj.mixer_graph().get_track(new_trk)) {
                                        proj.mixer_graph().add_track(new_trk, "Track " + std::to_string(new_trk));
                                    }
                                    selected_mixer_track_ = ch.settings().mixer_track;
                                    status_message_ = ch.settings().name + " Mixer Track: " + (new_trk == 0 ? "Master" : ("Track " + std::to_string(new_trk)));
                                    handled = true;
                                }
                            }
                        }
                        if (!handled) {
                            float ph = win->h - DawWindow::kTitleBarHeight;
                            int vis_ch = std::max(1, static_cast<int>((ph - 48.0f) / row_h));
                            int max_scroll = std::max(0, static_cast<int>(proj.channels().size()) - vis_ch);
                            channel_rack_scroll_ch_ = std::clamp(channel_rack_scroll_ch_ - steps, 0, max_scroll);
                        }
                    } else if (*it == WindowId::Playlist) {
                        if (is_shift || mouse_pt.y < win->y + DawWindow::kTitleBarHeight + 62.0f) {
                            int max_bars = get_max_sequencer_bars();
                            float ruler_x = win->x + 110.0f;
                            float ruler_w = std::max(60.0f, (win->x + win->w - 12.0f) - ruler_x);
                            int bars_per_view = (ruler_w > 900.0f) ? 16 : ((ruler_w > 550.0f) ? 12 : 8);
                            int max_scroll = std::max(0, max_bars - bars_per_view);
                            sequencer_scroll_bar_ = std::clamp(sequencer_scroll_bar_ - steps, 0, max_scroll);
                            status_message_ = "Playlist timeline scrolled to Bar " + std::to_string(sequencer_scroll_bar_ + 1) + " / " + std::to_string(max_bars);
                        } else {
                            auto& proj = engine_.session().project();
                            float ph = win->h - DawWindow::kTitleBarHeight;
                            int vis_tracks = std::max(1, static_cast<int>((ph - 86.0f) / 48.0f));
                            int max_track_scroll = std::max(0, static_cast<int>(proj.tracks().size()) - vis_tracks);
                            if (max_track_scroll > 0) {
                                sequencer_scroll_track_ = std::clamp(sequencer_scroll_track_ - steps, 0, max_track_scroll);
                            }
                        }
                    } else if (*it == WindowId::AudioLibrary) {
                        float list_h = (win->h - DawWindow::kTitleBarHeight) - 76.0f;
                        int vis_rows = std::max(1, static_cast<int>(list_h / 36.0f));
                        int max_scroll = std::max(0, static_cast<int>(sample_library_.size()) - vis_rows);
                        if (max_scroll > 0) {
                            audio_lib_scroll_idx_ = std::clamp(audio_lib_scroll_idx_ - steps, 0, max_scroll);
                        }
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                return 0;
            }

            case WM_MOUSEHWHEEL: {
                short zDelta = GET_WHEEL_DELTA_WPARAM(wp);
                int steps = zDelta / WHEEL_DELTA;

                POINT mouse_pt;
                GetCursorPos(&mouse_pt);
                ScreenToClient(hwnd, &mouse_pt);

                for (auto it = z_order_.rbegin(); it != z_order_.rend(); ++it) {
                    auto* win = get_window(*it);
                    if (!win || !win->visible) continue;
                    if (!win->contains(static_cast<float>(mouse_pt.x), static_cast<float>(mouse_pt.y))) continue;

                    if (*it == WindowId::PianoRoll) {
                        int max_steps = get_max_piano_roll_steps();
                        int max_scroll = std::max(0, max_steps - piano_roll_steps_);
                        piano_roll_scroll_step_ = std::clamp(piano_roll_scroll_step_ + steps * 4, 0, max_scroll);
                        status_message_ = "Piano Roll Timeline: Step " + std::to_string(piano_roll_scroll_step_ + 1) + " / " + std::to_string(max_steps);
                    } else if (*it == WindowId::Playlist) {
                        int max_bars = get_max_sequencer_bars();
                        float ruler_x = win->x + 110.0f;
                        float ruler_w = std::max(60.0f, (win->x + win->w - 12.0f) - ruler_x);
                        int bars_per_view = (ruler_w > 900.0f) ? 16 : ((ruler_w > 550.0f) ? 12 : 8);
                        int max_scroll = std::max(0, max_bars - bars_per_view);
                        sequencer_scroll_bar_ = std::clamp(sequencer_scroll_bar_ + steps, 0, max_scroll);
                        status_message_ = "Playlist timeline scrolled to Bar " + std::to_string(sequencer_scroll_bar_ + 1) + " / " + std::to_string(max_bars);
                    } else if (*it == WindowId::Mixer) {
                        auto& proj = engine_.session().project();
                        float track_w = 96.0f;
                        float gap = 10.0f;
                        float master_x = win->x + 12.0f;
                        float insert_start_x = master_x + track_w + gap;
                        float avail_w = (win->x + win->w - 12.0f) - insert_start_x;
                        int vis_inserts = std::max(1, static_cast<int>(avail_w / (track_w + gap)));
                        size_t total_inserts = std::max(size_t(4), proj.channels().size());
                        int max_mix_scroll = std::max(0, static_cast<int>(total_inserts) - vis_inserts);
                        if (max_mix_scroll > 0) {
                            mixer_scroll_track_ = std::clamp(mixer_scroll_track_ + steps, 0, max_mix_scroll);
                        }
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
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
                if (wp == VK_F4) {
                    toggle_or_focus_window(WindowId::AudioLibrary);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                if (wp == VK_F5) {
                    toggle_or_focus_window(WindowId::Playlist);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                if (wp == VK_F6) {
                    toggle_or_focus_window(WindowId::ChannelRack);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                if (wp == VK_F7) {
                    toggle_or_focus_window(WindowId::PianoRoll);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                if (wp == VK_F8) {
                    toggle_or_focus_window(WindowId::Inspector);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                if (wp == VK_F9) {
                    toggle_or_focus_window(WindowId::Mixer);
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
                    if (wp == 'M') {
                        magnet_enabled_ = !magnet_enabled_;
                        status_message_ = magnet_enabled_ ? "Magnetic Snapping: Enabled" : "Magnetic Snapping: Disabled";
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                    if (wp == 'I') {
                        toggle_or_focus_window(WindowId::Inspector);
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                    if (wp == 'B') {
                        toggle_or_focus_window(WindowId::AudioLibrary);
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                }
                if (wp == VK_SPACE) {
                    toggle_play();
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                if (wp == VK_ESCAPE) {
                    if (active_editor_channel_ != 0) {
                        active_editor_channel_ = 0; // Close plugin editor
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

    void render_window_frame_d2d(const DawWindow& win) {
        const auto& t = D2DRenderer::theme();
        bool is_active = (win.id == active_window_);

        // 0. Solid opaque window background (prevents background bleed-through when overlapping)
        D2D1_RECT_F full_rc = win.get_rect();
        D2DRenderer::draw_rounded_box(d2d_target_, full_rc, t.bg_surface, t.border_subtle, 0.0f);

        // 1. Title bar background
        D2D1_RECT_F title_bar_rc = win.get_title_bar_rect();
        D2D1_COLOR_F title_bg = is_active ? t.bg_surface_2 : t.bg_surface;
        D2DRenderer::draw_rounded_box(d2d_target_, title_bar_rc, title_bg, is_active ? t.accent : t.border_subtle, 4.0f);

        // 2. Active Accent Line on top of title bar
        if (is_active) {
            ID2D1SolidColorBrush* br_top = nullptr;
            d2d_target_->CreateSolidColorBrush(t.accent_bright, &br_top);
            if (br_top) {
                d2d_target_->DrawLine(D2D1::Point2F(win.x + 2.0f, win.y + 1.0f),
                                      D2D1::Point2F(win.x + win.w - 2.0f, win.y + 1.0f),
                                      br_top, 2.0f);
                br_top->Release();
            }
        }

        // 3. Window Icon Pill
        D2D1_RECT_F icon_pill = D2D1::RectF(win.x + 6.0f, win.y + 4.0f, win.x + 26.0f, win.y + 24.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, icon_pill, is_active ? t.bg_elevated : t.bg_control, t.border_subtle, 3.0f);
        D2D1_RECT_F icon_rc = D2D1::RectF(win.x + 8.0f, win.y + 6.0f, win.x + 24.0f, win.y + 22.0f);
        D2DRenderer::draw_svg_icon(d2d_target_, win.icon, icon_rc, is_active ? t.accent_bright : t.text_secondary, 13.0f);

        // 4. Window Title Text
        D2D1_RECT_F text_rc = D2D1::RectF(win.x + 30.0f, win.y + 2.0f, win.x + win.w - 32.0f, win.y + DawWindow::kTitleBarHeight);
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, win.title, text_rc,
                              is_active ? t.text_primary : t.text_secondary,
                              DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // 5. Close Button [✕] with Danger/Red hover effect
        D2D1_RECT_F close_rc = win.get_close_btn_rect();
        bool is_close_hover = (hover_close_win_id_ == static_cast<int>(win.id));
        D2D1_COLOR_F close_bg = is_close_hover ? D2D1::ColorF(0.88f, 0.35f, 0.39f, 0.90f) : t.bg_control;
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, close_rc, "✕", false, close_bg, is_close_hover ? t.danger : t.border_subtle, 3.0f);

        // 6. Outer Border around entire window
        ID2D1SolidColorBrush* br_border = nullptr;
        d2d_target_->CreateSolidColorBrush(is_active ? t.accent : t.border_subtle, &br_border);
        if (br_border) {
            d2d_target_->DrawRectangle(full_rc, br_border, is_active ? 1.5f : 1.0f);
            br_border->Release();
        }
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

        // Render each visible window in z_order_ (back to front)
        for (WindowId wid : z_order_) {
            auto* win = get_window(wid);
            if (!win || !win->visible) continue;

            render_window_frame_d2d(*win);

            d2d_target_->PushAxisAlignedClip(win->get_content_rect(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            switch (wid) {
                case WindowId::ChannelRack:
                    render_channel_rack_d2d(*win);
                    break;
                case WindowId::Playlist:
                    render_playlist_d2d(*win);
                    break;
                case WindowId::PianoRoll:
                    render_piano_roll_d2d(*win);
                    break;
                case WindowId::Mixer:
                    render_mixer_panel_d2d(*win);
                    break;
                case WindowId::Inspector:
                    render_inspector_d2d(*win);
                    break;
                case WindowId::AudioLibrary:
                    render_audio_library_d2d(*win);
                    break;
            }
            d2d_target_->PopAxisAlignedClip();
        }

        render_status_bar_d2d();

        if (active_editor_channel_ != 0) {
            render_plugin_editor_d2d();
        }
        if (active_editor_effect_track_ >= 0 && active_editor_effect_slot_ >= 0) {
            render_effect_editor_d2d();
        }

        if (is_dragging_sample_) {
            const auto& t = D2DRenderer::theme();
            float bx = static_cast<float>(current_mouse_x_ + 14);
            float by = static_cast<float>(current_mouse_y_ + 14);
            D2D1_RECT_F badge_rc = D2D1::RectF(bx, by, bx + 200.0f, by + 28.0f);
            D2DRenderer::draw_rounded_box(d2d_target_, badge_rc, D2D1::ColorF(0.08f, 0.12f, 0.16f, 0.94f), t.accent, 4.0f, 1.5f);
            std::string badge_txt = "🎵 " + dragged_sample_name_;
            D2DRenderer::draw_text(d2d_target_, dwrite_small_, badge_txt, badge_rc, t.accent_bright,
                                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
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

        // Logo / Wordmark (wide bounds so "DigiDAW" never wraps down)
        D2D1_RECT_F title_rc = D2D1::RectF(16.0f, 0.0f, 130.0f, 48.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_title_, "DigiDAW", title_rc, t.text_primary);

        // Elegant vertical divider between brand and project info
        ID2D1SolidColorBrush* br_div = nullptr;
        d2d_target_->CreateSolidColorBrush(t.border_subtle, &br_div);
        if (br_div) {
            d2d_target_->DrawLine(D2D1::Point2F(138.0f, 14.0f), D2D1::Point2F(138.0f, 34.0f), br_div, 1.0f);
            br_div->Release();
        }

        // Project Name badge (with generous breathing space from DigiDAW)
        std::string proj_name = engine_.session().project().name();
        if (proj_name.empty()) proj_name = "Untitled Project";
        D2D1_RECT_F proj_rc = D2D1::RectF(200.0f, 8.0f, 310.0f, 40.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, proj_rc, t.bg_control, t.border_subtle, 3.5f);
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, proj_name, proj_rc, t.accent_bright,
                              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Undo [↶] & Redo [↷] buttons
        bool can_undo = engine_.session().undo_stack().can_undo();
        bool can_redo = engine_.session().undo_stack().can_redo();
        D2D1_RECT_F undo_rc = D2D1::RectF(316.0f, 8.0f, 346.0f, 40.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, undo_rc, t.bg_control, can_undo ? t.border_default : t.border_subtle, 3.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, "↶", undo_rc,
                              can_undo ? t.text_primary : t.text_disabled,
                              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        D2D1_RECT_F redo_rc = D2D1::RectF(350.0f, 8.0f, 380.0f, 40.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, redo_rc, t.bg_control, can_redo ? t.border_default : t.border_subtle, 3.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, "↷", redo_rc,
                              can_redo ? t.text_primary : t.text_disabled,
                              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Buttons: Icon-first, small rectangular (DESIGN.md section 2)
        bool is_playing = engine_.transport().is_playing();
        bool is_paused = (engine_.transport().state() == app::TransportState::Paused);

        // 1. PLAY Button (active: purple accent)
        D2D1_RECT_F play_rc = D2D1::RectF(388.0f, 8.0f, 424.0f, 40.0f);
        D2DRenderer::draw_icon_button(d2d_target_, play_rc, SvgIconType::Play, is_playing, t.accent, t.bg_control, 3.5f, 16.0f);

        // 2. PAUSE Button (active: warning amber)
        D2D1_RECT_F pause_rc = D2D1::RectF(428.0f, 8.0f, 464.0f, 40.0f);
        D2DRenderer::draw_icon_button(d2d_target_, pause_rc, SvgIconType::Pause, is_paused, t.warning, t.bg_control, 3.5f, 16.0f);

        // 3. STOP Button
        D2D1_RECT_F stop_rc = D2D1::RectF(468.0f, 8.0f, 504.0f, 40.0f);
        D2DRenderer::draw_icon_button(d2d_target_, stop_rc, SvgIconType::Stop, false, t.danger, t.bg_control, 3.5f, 16.0f);

        // 4. Playback Mode (PAT / SONG) Toggle Button
        bool is_pat_mode = (engine_.transport().mode() == app::PlaybackMode::Pattern);
        D2D1_RECT_F mode_rc = D2D1::RectF(508.0f, 8.0f, 568.0f, 40.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, mode_rc, t.bg_control, is_pat_mode ? t.warning : t.accent, 3.5f);
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, is_pat_mode ? "PAT" : "SONG", mode_rc,
                              is_pat_mode ? t.warning : t.accent_bright,
                              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Tempo Controls (- BPM +)
        double bpm = engine_.session().project().time_map().get_bpm_at(0);
        std::stringstream ss_bpm;
        ss_bpm << std::fixed << std::setprecision(1) << bpm << " BPM";

        D2D1_RECT_F bpm_minus_rc = D2D1::RectF(574.0f, 8.0f, 598.0f, 40.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_bold_, bpm_minus_rc, "-", false, t.bg_control, t.bg_control, 3.0f);

        D2D1_RECT_F bpm_disp_rc = D2D1::RectF(602.0f, 8.0f, 682.0f, 40.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, bpm_disp_rc, t.bg_control, t.border_subtle, 3.5f);
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, ss_bpm.str(), bpm_disp_rc, t.text_primary,
                              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        D2D1_RECT_F bpm_plus_rc = D2D1::RectF(686.0f, 8.0f, 710.0f, 40.0f);
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

        D2D1_RECT_F pos_rc = D2D1::RectF(716.0f, 8.0f, 848.0f, 40.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, pos_rc, t.bg_control, t.border_subtle, 3.5f);
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, ss_pos.str(), pos_rc, t.accent_bright,
                              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Window / View Buttons: Channel Rack, Playlist, Piano Roll, Mixer, Inspector, and Magnet Snapping
        D2D1_RECT_F rack_btn_rc = D2D1::RectF(854.0f, 8.0f, 890.0f, 40.0f);
        bool rack_top = (active_window_ == WindowId::ChannelRack && win_channel_rack_.visible);
        D2DRenderer::draw_icon_button(d2d_target_, rack_btn_rc, SvgIconType::ChannelRack,
                                      rack_top, t.accent, win_channel_rack_.visible ? t.bg_surface_2 : t.bg_control, 3.5f, 16.0f);

        D2D1_RECT_F pl_btn_rc = D2D1::RectF(894.0f, 8.0f, 930.0f, 40.0f);
        bool pl_top = (active_window_ == WindowId::Playlist && win_playlist_.visible);
        D2DRenderer::draw_icon_button(d2d_target_, pl_btn_rc, SvgIconType::Playlist,
                                      pl_top, t.accent, win_playlist_.visible ? t.bg_surface_2 : t.bg_control, 3.5f, 16.0f);

        D2D1_RECT_F roll_btn_rc = D2D1::RectF(934.0f, 8.0f, 970.0f, 40.0f);
        bool pr_top = (active_window_ == WindowId::PianoRoll && win_pianoroll_.visible);
        D2DRenderer::draw_icon_button(d2d_target_, roll_btn_rc, SvgIconType::PianoRoll,
                                      pr_top, t.accent, win_pianoroll_.visible ? t.bg_surface_2 : t.bg_control, 3.5f, 16.0f);

        D2D1_RECT_F mix_btn_rc = D2D1::RectF(974.0f, 8.0f, 1010.0f, 40.0f);
        bool mx_top = (active_window_ == WindowId::Mixer && win_mixer_.visible);
        D2DRenderer::draw_icon_button(d2d_target_, mix_btn_rc, SvgIconType::Mixer,
                                      mx_top, t.accent, win_mixer_.visible ? t.bg_surface_2 : t.bg_control, 3.5f, 16.0f);

        D2D1_RECT_F insp_btn_rc = D2D1::RectF(1014.0f, 8.0f, 1050.0f, 40.0f);
        bool insp_top = (active_window_ == WindowId::Inspector && win_inspector_.visible);
        D2DRenderer::draw_icon_button(d2d_target_, insp_btn_rc, SvgIconType::TrackFx,
                                      insp_top, t.accent, win_inspector_.visible ? t.bg_surface_2 : t.bg_control, 3.5f, 16.0f);

        D2D1_RECT_F lib_btn_rc = D2D1::RectF(1054.0f, 8.0f, 1090.0f, 40.0f);
        bool lib_top = (active_window_ == WindowId::AudioLibrary && win_audio_library_.visible);
        D2DRenderer::draw_icon_button(d2d_target_, lib_btn_rc, SvgIconType::Folder,
                                      lib_top, t.accent, win_audio_library_.visible ? t.bg_surface_2 : t.bg_control, 3.5f, 16.0f);

        D2D1_RECT_F mag_btn_rc = D2D1::RectF(1094.0f, 8.0f, 1130.0f, 40.0f);
        D2DRenderer::draw_icon_button(d2d_target_, mag_btn_rc, SvgIconType::Magnet,
                                      magnet_enabled_, t.accent, t.bg_control, 3.5f, 16.0f);

        // 7. Real-Time Audio Signal Oscilloscope Section (Electric violet phosphor filament)
        float spec_x = 1138.0f;
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

        // Action Buttons: Save [Save SVG] and Export [Export SVG]
        D2D1_RECT_F save_rc = D2D1::RectF(static_cast<float>(client_w_ - 80), 8.0f, static_cast<float>(client_w_ - 45), 40.0f);
        D2DRenderer::draw_icon_button(d2d_target_, save_rc, SvgIconType::Save, false, t.bg_control, t.bg_control, 3.5f, 16.0f);

        D2D1_RECT_F exp_rc = D2D1::RectF(static_cast<float>(client_w_ - 41), 8.0f, static_cast<float>(client_w_ - 10), 40.0f);
        D2DRenderer::draw_icon_button(d2d_target_, exp_rc, SvgIconType::Export, false, t.accent, t.bg_control, 3.5f, 16.0f);
    }

    void render_channel_rack_d2d(const DawWindow& win) {
        const auto& t = D2DRenderer::theme();
        D2D1_RECT_F rack_rc = win.get_content_rect();
        D2DRenderer::draw_rounded_box(d2d_target_, rack_rc, t.bg_surface, t.border_subtle, 0.0f);

        float px = win.x;
        float py = win.y + DawWindow::kTitleBarHeight;
        float pw = win.w;
        float ph = win.h - DawWindow::kTitleBarHeight;

        auto& proj = engine_.session().project();
        auto ppq = proj.time_map().ppq();
        auto step_ticks = ppq / 4;
        auto* pat = get_active_pattern();

        // 1. Header Toolbar (py + 4 .. py + 32)
        // Pattern Selector: [ ◄ ] [ Pattern X ] [ ► ] [ + ] [ − ]
        D2D1_RECT_F prev_pat_rc = D2D1::RectF(px + 8.0f, py + 4.0f, px + 28.0f, py + 30.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, prev_pat_rc, "◄", false, t.bg_control, t.bg_control, 3.0f);

        std::string pat_name = pat ? pat->name() : "Pattern 1";
        D2D1_RECT_F pat_name_rc = D2D1::RectF(px + 32.0f, py + 4.0f, px + 150.0f, py + 30.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, pat_name_rc, t.bg_control, t.border_subtle, 3.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_small_, pat_name, pat_name_rc, t.accent_bright,
                              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        D2D1_RECT_F next_pat_rc = D2D1::RectF(px + 154.0f, py + 4.0f, px + 174.0f, py + 30.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, next_pat_rc, "►", false, t.bg_control, t.bg_control, 3.0f);

        D2D1_RECT_F add_pat_rc = D2D1::RectF(px + 178.0f, py + 4.0f, px + 200.0f, py + 30.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, add_pat_rc, "+", false, t.bg_control, t.bg_control, 3.0f);

        D2D1_RECT_F del_pat_rc = D2D1::RectF(px + 204.0f, py + 4.0f, px + 226.0f, py + 30.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, del_pat_rc, "−", false, t.bg_control, t.bg_control, 3.0f);

        // Dynamic Steps Display
        float grid_w_est = std::max(60.0f, (px + pw - 12.0f) - (px + 240.0f));
        int cur_bars = get_channel_rack_num_bars(grid_w_est);
        channel_rack_steps_ = cur_bars * 16;
        std::string steps_lbl = std::to_string(channel_rack_steps_) + " Steps (" + std::to_string(cur_bars) + " Bars)";
        D2D1_RECT_F steps_rc = D2D1::RectF(px + 232.0f, py + 4.0f, px + 342.0f, py + 30.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, steps_rc, steps_lbl, true, t.accent, t.bg_control, 3.0f);

        // [+ Add Instrument] button
        D2D1_RECT_F add_inst_rc = D2D1::RectF(px + 348.0f, py + 4.0f, px + 460.0f, py + 30.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, add_inst_rc, "+ Add Instrument", false, t.accent, t.bg_control, 3.0f);

        // 2. Channel Rows (starts at py + 38)
        float start_y = py + 38.0f;
        float row_h = 38.0f;
        float rack_avail_h = (py + ph - 8.0f) - start_y;
        int visible_channels = std::max(1, static_cast<int>(rack_avail_h / row_h));
        int max_ch_scroll = std::max(0, static_cast<int>(proj.channels().size()) - visible_channels);
        channel_rack_scroll_ch_ = std::clamp(channel_rack_scroll_ch_, 0, max_ch_scroll);

        D2D1_RECT_F clip_rc = D2D1::RectF(px + 4.0f, start_y, px + pw - 4.0f, py + ph - 4.0f);
        d2d_target_->PushAxisAlignedClip(clip_rc, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        size_t start_ch = static_cast<size_t>(channel_rack_scroll_ch_);
        size_t end_ch = std::min(proj.channels().size(), start_ch + static_cast<size_t>(visible_channels) + 1);

        domain::Tick pat_len_ticks = pat ? pat->length_ticks(ppq) : (4 * ppq);
        if (pat_len_ticks <= 0) pat_len_ticks = 4 * ppq;
        domain::Tick cur_tick = engine_.transport().current_tick();
        int cur_step = engine_.transport().is_playing() ? static_cast<int>((cur_tick % pat_len_ticks) / step_ticks) : -1;

        ID2D1SolidColorBrush* br_led_on = nullptr;
        ID2D1SolidColorBrush* br_led_off = nullptr;
        d2d_target_->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.90f, 0.40f, 1.0f), &br_led_on);
        d2d_target_->CreateSolidColorBrush(D2D1::ColorF(0.25f, 0.25f, 0.28f, 1.0f), &br_led_off);

        for (size_t ch_idx = start_ch; ch_idx < end_ch; ++ch_idx) {
            auto& ch = proj.channels()[ch_idx];
            float row_y = start_y + static_cast<float>(ch_idx - start_ch) * row_h;
            if (row_y + row_h > py + ph) break;

            D2D1_RECT_F row_bg_rc = D2D1::RectF(px + 6.0f, row_y + 1.0f, px + pw - 6.0f, row_y + row_h - 1.0f);
            D2D1_COLOR_F row_bg_col = (ch_idx % 2 == 0) ? t.bg_surface : t.bg_app;
            D2DRenderer::draw_rounded_box(d2d_target_, row_bg_rc, row_bg_col, t.border_subtle, 2.0f);

            // 1. Mute / Active LED indicator
            bool is_active = !ch.settings().muted;
            D2D1_ELLIPSE led_el = D2D1::Ellipse(D2D1::Point2F(px + 15.0f, row_y + 18.0f), 5.5f, 5.5f);
            d2d_target_->FillEllipse(led_el, is_active ? br_led_on : br_led_off);
            ID2D1SolidColorBrush* br_led_border = nullptr;
            d2d_target_->CreateSolidColorBrush(is_active ? D2D1::ColorF(0.4f, 1.0f, 0.6f, 0.8f) : t.border_subtle, &br_led_border);
            if (br_led_border) {
                d2d_target_->DrawEllipse(led_el, br_led_border, 1.0f);
                br_led_border->Release();
            }

            // 2. Pan Knob
            D2D1_RECT_F pan_rc = D2D1::RectF(px + 26.0f, row_y + 6.0f, px + 50.0f, row_y + 30.0f);
            float pan_norm = (ch.settings().pan + 1.0f) * 0.5f;
            D2DRenderer::draw_knob(d2d_target_, dwrite_small_, pan_rc, pan_norm, "PAN", t.accent);

            // 3. Vol Knob
            D2D1_RECT_F vol_rc = D2D1::RectF(px + 54.0f, row_y + 6.0f, px + 78.0f, row_y + 30.0f);
            float vol_norm = std::clamp(ch.settings().volume / domain::kMaxChannelVolume, 0.0f, 1.0f);
            D2DRenderer::draw_knob(d2d_target_, dwrite_small_, vol_rc, vol_norm, "VOL", t.accent_bright);

            // 4. Target Mixer Track LCD Box with spin indicator
            D2D1_RECT_F trk_rc = D2D1::RectF(px + 82.0f, row_y + 6.0f, px + 110.0f, row_y + 30.0f);
            D2DRenderer::draw_rounded_box(d2d_target_, trk_rc, D2D1::ColorF(0.16f, 0.18f, 0.20f, 1.0f), t.border_subtle, 2.5f);
            std::string trk_str = (ch.settings().mixer_track == 0) ? "--" : std::to_string(ch.settings().mixer_track);
            D2D1_RECT_F trk_txt_rc = D2D1::RectF(px + 82.0f, row_y + 6.0f, px + 104.0f, row_y + 30.0f);
            D2DRenderer::draw_text(d2d_target_, dwrite_small_, trk_str, trk_txt_rc, t.accent_bright,
                                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            D2D1_RECT_F caret_rc = D2D1::RectF(px + 102.0f, row_y + 7.0f, px + 109.0f, row_y + 29.0f);
            D2DRenderer::draw_text(d2d_target_, dwrite_small_, "⬍", caret_rc, t.text_muted,
                                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            // 5. Instrument Name Button
            D2D1_RECT_F name_rc = D2D1::RectF(px + 114.0f, row_y + 5.0f, px + 208.0f, row_y + 31.0f);
            D2DRenderer::draw_button(d2d_target_, dwrite_small_, name_rc, ch.settings().name, false, t.bg_surface_2, t.bg_surface_2, 3.0f);

            // 6. Channel Selection Indicator Strip (matching Screenshot 1 row 2)
            bool is_selected_ch = (piano_roll_channel_ == ch.id());
            D2D1_RECT_F sel_bar_rc = D2D1::RectF(px + 211.0f, row_y + 6.0f, px + 216.0f, row_y + 30.0f);
            if (is_selected_ch) {
                D2DRenderer::draw_rounded_box(d2d_target_, sel_bar_rc, D2D1::ColorF(0.12f, 0.20f, 0.14f, 1.0f),
                                              D2D1::ColorF(0.40f, 0.95f, 0.35f, 1.0f), 1.5f, 1.5f);
            } else {
                D2DRenderer::draw_rounded_box(d2d_target_, sel_bar_rc, D2D1::ColorF(0.18f, 0.20f, 0.22f, 1.0f),
                                              t.border_subtle, 1.5f, 1.0f);
            }

            // Piano Roll Button [ 🎹 ]
            D2D1_RECT_F roll_btn_rc = D2D1::RectF(px + 218.0f, row_y + 6.0f, px + 236.0f, row_y + 30.0f);
            D2DRenderer::draw_icon_button(d2d_target_, roll_btn_rc, SvgIconType::PianoRoll, is_selected_ch, t.accent, t.bg_control, 3.0f, 13.0f);

            // 7. Beat Pattern Step Sequencer / Mini Piano Roll
            float grid_x = px + 240.0f;
            float grid_w = std::max(60.0f, (px + pw - 12.0f) - grid_x);

            auto* note_set = pat ? pat->get_channel_notes(ch.id()) : nullptr;
            bool is_melody = note_set && is_channel_piano_roll(*note_set, ppq);

            int num_bars = get_channel_rack_num_bars(grid_w);
            int num_steps = num_bars * 16;
            channel_rack_steps_ = num_steps;
            float pad_w = get_channel_rack_pad_width(grid_w, num_steps);
            float total_pads_w = float(num_steps) * pad_w;

            if (is_melody) {
                // FL Studio Dark Blue-Slate lane with pastel green note bars
                D2D1_COLOR_F lane_bg = D2D1::ColorF(0.12f, 0.16f, 0.19f, 1.0f);
                D2D1_COLOR_F green_note = D2D1::ColorF(0.48f, 0.85f, 0.55f, 1.0f);
                D2D1_COLOR_F green_border = D2D1::ColorF(0.35f, 0.70f, 0.42f, 1.0f);

                float lane_w = std::min(grid_w, total_pads_w);
                D2D1_RECT_F lane_rc = D2D1::RectF(grid_x, row_y + 5.0f, grid_x + lane_w, row_y + 31.0f);
                D2DRenderer::draw_rounded_box(d2d_target_, lane_rc, lane_bg, t.border_subtle, 2.5f);

                // Bar and beat grid dividers to visually align 1:1 with step pads below
                ID2D1SolidColorBrush* br_bar_div = nullptr;
                ID2D1SolidColorBrush* br_beat_div = nullptr;
                d2d_target_->CreateSolidColorBrush(D2D1::ColorF(0.24f, 0.28f, 0.34f, 0.9f), &br_bar_div);
                d2d_target_->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.21f, 0.25f, 0.6f), &br_beat_div);

                for (int s = 1; s < num_steps; ++s) {
                    float div_x = grid_x + s * pad_w;
                    if (div_x >= lane_rc.right) break;
                    float snap_x = std::floor(div_x) + 0.5f;
                    if (s % 16 == 0) {
                        if (br_bar_div) {
                            d2d_target_->DrawLine(D2D1::Point2F(snap_x, lane_rc.top + 1.0f),
                                                  D2D1::Point2F(snap_x, lane_rc.bottom - 1.0f),
                                                  br_bar_div, 1.5f);
                        }
                    } else if (s % 4 == 0) {
                        if (br_beat_div) {
                            d2d_target_->DrawLine(D2D1::Point2F(snap_x, lane_rc.top + 3.0f),
                                                  D2D1::Point2F(snap_x, lane_rc.bottom - 3.0f),
                                                  br_beat_div, 1.0f);
                        }
                    }
                }
                if (br_bar_div) br_bar_div->Release();
                if (br_beat_div) br_beat_div->Release();

                D2D1_RECT_F tag_rc = D2D1::RectF(grid_x + 6.0f, lane_rc.top + 1.0f, grid_x + 75.0f, lane_rc.bottom - 1.0f);
                D2DRenderer::draw_text(d2d_target_, dwrite_small_, "Piano roll", tag_rc, t.text_muted,
                                      DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

                uint8_t min_p = 127, max_p = 0;
                for (const auto& n : note_set->notes()) {
                    min_p = std::min(min_p, n.pitch);
                    max_p = std::max(max_p, n.pitch);
                }
                int p_range = std::max(1, (max_p > min_p) ? (max_p - min_p + 1) : 4);

                for (const auto& n : note_set->notes()) {
                    float nx = domain::ChannelRackLayout::tick_to_x(grid_x, n.start, ppq, pad_w);
                    float nw = std::max(4.0f, domain::ChannelRackLayout::ticks_to_width(n.length, ppq, pad_w));
                    if (nx >= lane_rc.right) continue;
                    if (nx + nw > lane_rc.right) nw = lane_rc.right - nx;

                    float norm_p = (max_p > min_p) ? (float(n.pitch - min_p) / float(p_range)) : 0.5f;
                    float ny = lane_rc.top + (1.0f - norm_p) * (lane_rc.bottom - lane_rc.top - 6.0f) + 1.0f;
                    D2D1_RECT_F n_rc = D2D1::RectF(nx + 0.5f, ny, nx + nw - 0.5f, ny + 3.5f);
                    D2DRenderer::draw_rounded_box(d2d_target_, n_rc, green_note, green_border, 1.0f);
                }

                // Active step playhead marker
                if (cur_step >= 0 && cur_step < num_steps) {
                    float cur_sx = grid_x + cur_step * pad_w;
                    if (cur_sx < lane_rc.right) {
                        float cur_ex = std::min(lane_rc.right, cur_sx + pad_w);
                        D2D1_RECT_F head_rc = D2D1::RectF(cur_sx, lane_rc.top, cur_ex, lane_rc.top + 2.5f);
                        D2DRenderer::draw_rounded_box(d2d_target_, head_rc, t.accent_bright, t.accent_bright, 1.0f);
                    }
                }
            } else {
                for (int s = 0; s < num_steps; ++s) {
                    float sx = grid_x + s * pad_w;
                    if (sx + 2.0f >= grid_x + grid_w) break;
                    float ex = std::min(grid_x + grid_w, sx + pad_w);
                    D2D1_RECT_F pad_rc = D2D1::RectF(sx + 1.5f, row_y + 5.0f, ex - 1.5f, row_y + 31.0f);
                    bool step_on = note_set && note_set->has_note_at_step(s, ppq, 60);
                    bool step_cur = (s == cur_step);

                    // FL Studio 4-beat alternating palette:
                    // Beats 1 & 3: Charcoal / Silver-white
                    // Beats 2 & 4: Warm Reddish-Brown / Salmon-Coral
                    int beat_grp = (s / 4) % 2;
                    D2D1_COLOR_F inact_col = (beat_grp == 0)
                        ? D2D1::ColorF(0.24f, 0.26f, 0.29f, 1.0f)
                        : D2D1::ColorF(0.36f, 0.23f, 0.23f, 1.0f);
                    D2D1_COLOR_F act_col = (beat_grp == 0)
                        ? D2D1::ColorF(0.88f, 0.92f, 0.96f, 1.0f)
                        : D2D1::ColorF(0.96f, 0.62f, 0.62f, 1.0f);
                    D2D1_COLOR_F inact_border = (beat_grp == 0)
                        ? D2D1::ColorF(0.18f, 0.19f, 0.21f, 1.0f)
                        : D2D1::ColorF(0.26f, 0.17f, 0.17f, 1.0f);
                    D2D1_COLOR_F act_border = (beat_grp == 0)
                        ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f)
                        : D2D1::ColorF(1.0f, 0.75f, 0.75f, 1.0f);

                    D2D1_COLOR_F pad_col = step_on ? act_col : inact_col;
                    D2D1_COLOR_F pad_border = step_cur ? t.accent_bright : (step_on ? act_border : inact_border);

                    D2DRenderer::draw_rounded_box(d2d_target_, pad_rc, pad_col, pad_border, 3.0f);

                    // Central tactile notch pip on inactive pads
                    if (!step_on) {
                        float cx = (pad_rc.left + pad_rc.right) * 0.5f;
                        float cy = (pad_rc.top + pad_rc.bottom) * 0.5f;
                        D2D1_RECT_F pip_rc = D2D1::RectF(cx - 1.0f, cy - 3.0f, cx + 1.0f, cy + 3.0f);
                        D2DRenderer::draw_rounded_box(d2d_target_, pip_rc, D2D1::ColorF(0.12f, 0.13f, 0.15f, 0.8f),
                                                      D2D1::ColorF(0.12f, 0.13f, 0.15f, 0.8f), 0.5f);
                    }
                    if (step_cur) {
                        D2D1_RECT_F bar_rc = D2D1::RectF(pad_rc.left, pad_rc.top, pad_rc.right, pad_rc.top + 2.5f);
                        D2DRenderer::draw_rounded_box(d2d_target_, bar_rc, t.accent_bright, t.accent_bright, 1.0f);
                    }
                }
            }
        }

        if (br_led_on) br_led_on->Release();
        if (br_led_off) br_led_off->Release();

        d2d_target_->PopAxisAlignedClip();
    }

    void render_playlist_d2d(const DawWindow& win) {
        const auto& t = D2DRenderer::theme();
        D2D1_RECT_F rack_rc = win.get_content_rect();
        D2DRenderer::draw_rounded_box(d2d_target_, rack_rc, t.bg_surface, t.border_subtle, 0.0f);

        float px = win.x;
        float py = win.y + DawWindow::kTitleBarHeight;
        float pw = win.w;
        float ph = win.h - DawWindow::kTitleBarHeight;

        auto& proj = engine_.session().project();
        auto ppq = proj.time_map().ppq();
        auto bar_ticks = 4 * ppq;

        // 1. Header Toolbar
        // Brush Pattern Selector: [ ◄ ] [ Brush: Pattern X ] [ ► ]
        D2D1_RECT_F prev_b_rc = D2D1::RectF(px + 8.0f, py + 4.0f, px + 28.0f, py + 30.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, prev_b_rc, "◄", false, t.bg_control, t.bg_control, 3.0f);

        auto* brush_pat = get_active_pattern();
        std::string b_name = brush_pat ? ("Brush: " + brush_pat->name()) : "Brush: Pattern 1";
        D2D1_RECT_F brush_rc = D2D1::RectF(px + 32.0f, py + 4.0f, px + 160.0f, py + 30.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, brush_rc, t.bg_control, t.border_subtle, 3.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_small_, b_name, brush_rc, t.accent_bright,
                              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        D2D1_RECT_F next_b_rc = D2D1::RectF(px + 164.0f, py + 4.0f, px + 184.0f, py + 30.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, next_b_rc, "►", false, t.bg_control, t.bg_control, 3.0f);

        // [+ Add Track] and [− Del Track] buttons
        D2D1_RECT_F add_trk_rc = D2D1::RectF(px + 190.0f, py + 4.0f, px + 270.0f, py + 30.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, add_trk_rc, "+ Add Track", false, t.accent, t.bg_control, 3.0f);

        D2D1_RECT_F del_trk_rc = D2D1::RectF(px + 274.0f, py + 4.0f, px + 354.0f, py + 30.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, del_trk_rc, "− Del Track", false, t.danger, t.bg_control, 3.0f);

        // Bar Range Display
        float ruler_x = px + 110.0f;
        float ruler_w = std::max(60.0f, (px + pw - 12.0f) - ruler_x);
        float ruler_y = py + 36.0f;
        float ruler_h = 22.0f;

        int bars_per_view = (ruler_w > 900.0f) ? 16 : ((ruler_w > 550.0f) ? 12 : 8);
        int max_bars = get_max_sequencer_bars();
        int start_bar_num = sequencer_scroll_bar_ + 1;
        int end_bar_num = sequencer_scroll_bar_ + bars_per_view;
        std::string bar_lbl = "↔ Bars " + std::to_string(start_bar_num) + "-" + std::to_string(end_bar_num) + " / " + std::to_string(max_bars);
        D2D1_RECT_F bar_num_rc = D2D1::RectF(std::max(px + 362.0f, px + pw - 200.0f), py + 4.0f, px + pw - 12.0f, py + 30.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, bar_num_rc, t.bg_control, t.border_subtle, 3.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_small_, bar_lbl, bar_num_rc, t.text_secondary,
                              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // 2. Timeline Ruler
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
            D2D1_RECT_F num_rc = D2D1::RectF(bx + 4.0f, ruler_y + 2.0f, bx + 36.0f, ruler_y + ruler_h - 2.0f);
            int cur_bar_idx = sequencer_scroll_bar_ + b + 1;
            D2DRenderer::draw_text(d2d_target_, dwrite_small_, std::to_string(cur_bar_idx), num_rc, t.text_muted);

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

        // 3. Arrangement Tracks & Lanes
        float start_y = py + 62.0f;
        float row_h = 48.0f;
        float step_h = 42.0f;
        float avail_h = (py + ph - 24.0f) - start_y;
        int vis_tracks = std::max(1, static_cast<int>(avail_h / row_h));

        if (proj.tracks().empty()) {
            proj.add_track("Track 1");
        }

        int max_track_scroll = std::max(0, static_cast<int>(proj.tracks().size()) - vis_tracks);
        sequencer_scroll_track_ = std::clamp(sequencer_scroll_track_, 0, max_track_scroll);

        domain::Tick view_start_tick = static_cast<domain::Tick>(sequencer_scroll_bar_) * bar_ticks;
        domain::Tick view_duration = static_cast<domain::Tick>(bars_per_view) * bar_ticks;
        domain::Tick view_end_tick = view_start_tick + view_duration;

        float max_track_bottom = start_y;

        D2D1_RECT_F trk_clip_rc = D2D1::RectF(px + 4.0f, start_y, px + pw - 4.0f, py + ph - 24.0f);
        d2d_target_->PushAxisAlignedClip(trk_clip_rc, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        size_t start_t = static_cast<size_t>(sequencer_scroll_track_);
        size_t end_t = std::min(proj.tracks().size(), start_t + static_cast<size_t>(vis_tracks) + 1);

        for (size_t t_idx = start_t; t_idx < end_t; ++t_idx) {
            auto& track = proj.tracks()[t_idx];
            float t_y = start_y + static_cast<float>(t_idx - start_t) * row_h;
            max_track_bottom = t_y + step_h;

            // Track Header: Mute [M], Solo [S], Track Name, Green Active LED (matching Screenshot 2)
            D2D1_RECT_F mute_rc = D2D1::RectF(px + 8.0f, t_y + 8.0f, px + 26.0f, t_y + 34.0f);
            D2DRenderer::draw_button(d2d_target_, dwrite_small_, mute_rc, "M", track.is_muted(), t.danger, t.bg_control, 3.0f);

            D2D1_RECT_F solo_rc = D2D1::RectF(px + 29.0f, t_y + 8.0f, px + 47.0f, t_y + 34.0f);
            D2DRenderer::draw_button(d2d_target_, dwrite_small_, solo_rc, "S", track.solo(), t.accent, t.bg_control, 3.0f);

            D2D1_RECT_F name_rc = D2D1::RectF(px + 50.0f, t_y + 8.0f, px + 97.0f, t_y + 34.0f);
            D2DRenderer::draw_rounded_box(d2d_target_, name_rc, t.bg_control, t.border_subtle, 3.0f);
            D2DRenderer::draw_text(d2d_target_, dwrite_small_, track.name(), name_rc, t.text_primary,
                                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            // Active LED indicator circle on track header right edge (Screenshot 2)
            bool trk_active = !track.is_muted();
            D2D1_ELLIPSE trk_led_el = D2D1::Ellipse(D2D1::Point2F(px + 104.0f, t_y + 21.0f), 3.5f, 3.5f);
            ID2D1SolidColorBrush* br_trk_led = nullptr;
            d2d_target_->CreateSolidColorBrush(trk_active ? D2D1::ColorF(0.20f, 0.90f, 0.40f, 1.0f) : D2D1::ColorF(0.25f, 0.25f, 0.28f, 1.0f), &br_trk_led);
            if (br_trk_led) {
                d2d_target_->FillEllipse(trk_led_el, br_trk_led);
                br_trk_led->Release();
            }

            // Track Arranger Lane
            D2D1_RECT_F lane_rc = D2D1::RectF(ruler_x, t_y, ruler_x + ruler_w, t_y + step_h);
            D2D1_COLOR_F lane_bg = (t_idx % 2 == 0) ? t.bg_surface : t.bg_app;
            D2DRenderer::draw_rounded_box(d2d_target_, lane_rc, lane_bg, t.border_subtle, 2.0f);

            // Grid lines across lane
            for (int b = 0; b < bars_per_view; ++b) {
                float bx = ruler_x + b * bar_w;
                float snap_bx = std::floor(bx) + 0.5f;
                if (br_border_dark && b > 0) {
                    d2d_target_->DrawLine(D2D1::Point2F(snap_bx, t_y), D2D1::Point2F(snap_bx, t_y + step_h), br_border_dark, 1.0f);
                }
                if (br_border_faint) {
                    for (int bt = 1; bt < 4; ++bt) {
                        float snap_btx = std::floor(bx + bt * beat_w) + 0.5f;
                        d2d_target_->DrawLine(D2D1::Point2F(snap_btx, t_y), D2D1::Point2F(snap_btx, t_y + step_h), br_border_faint, 1.0f);
                    }
                }
            }

            // Render Clips in this track
            for (const auto& clip : track.clips()) {
                if (clip.end() <= view_start_tick || clip.start >= view_end_tick) continue;

                domain::Tick draw_start = std::max(view_start_tick, clip.start);
                domain::Tick draw_end = std::min(view_end_tick, clip.end());
                double norm_start = double(draw_start - view_start_tick) / double(view_duration);
                double norm_len = double(draw_end - draw_start) / double(view_duration);

                float cx = ruler_x + static_cast<float>(norm_start * ruler_w);
                float cw = std::max(16.0f, static_cast<float>(norm_len * ruler_w));

                auto* clip_pat = proj.get_pattern(clip.pattern_id);
                uint32_t p_id = clip.pattern_id;
                D2D1_COLOR_F track_accent = (p_id % 3 == 0) ? t.track_drums : ((p_id % 3 == 1) ? t.track_melody : t.track_chords);
                D2D1_COLOR_F clip_bg_col = D2D1::ColorF(0.065f + track_accent.r * 0.06f, 0.075f + track_accent.g * 0.06f, 0.09f + track_accent.b * 0.06f, 1.0f);
                D2D1_COLOR_F clip_hdr_col = D2D1::ColorF(0.085f + track_accent.r * 0.12f, 0.095f + track_accent.g * 0.12f, 0.11f + track_accent.b * 0.12f, 1.0f);
                D2D1_COLOR_F clip_border_col = D2D1::ColorF(0.14f + track_accent.r * 0.20f, 0.16f + track_accent.g * 0.20f, 0.20f + track_accent.b * 0.20f, 1.0f);

                D2D1_RECT_F clip_rc = D2D1::RectF(cx, t_y + 2.0f, cx + cw, t_y + step_h - 2.0f);
                D2DRenderer::draw_rounded_box(d2d_target_, clip_rc, clip_bg_col, clip_border_col, 3.5f, 1.0f);

                // Header Strip with Pattern Title
                float hdr_h = 13.0f;
                D2D1_RECT_F header_rc = D2D1::RectF(cx, t_y + 2.0f, cx + cw, t_y + 2.0f + hdr_h);
                D2DRenderer::draw_rounded_box(d2d_target_, header_rc, clip_hdr_col, clip_hdr_col, 2.5f);

                std::string clip_title = clip_pat ? ("≡ " + clip_pat->name()) : ("≡ Pat " + std::to_string(clip.pattern_id));
                D2DRenderer::draw_text(d2d_target_, dwrite_small_, clip_title, header_rc, t.text_primary);

                // Previews inside clip body: Waveform signal for AudioClipDevice or Note blocks for synths
                if (clip_pat) {
                    float body_top = t_y + 2.0f + hdr_h;
                    float body_h = step_h - 4.0f - hdr_h;

                    bool is_audio_clip = false;
                    plugins::AudioClipDevice* clipper_dev = nullptr;
                    if (!clip_pat->channel_notes().empty()) {
                        bool all_audio_clips = true;
                        for (const auto& [cid, nset] : clip_pat->channel_notes()) {
                            if (nset.notes().empty()) continue;
                            auto* ch = proj.get_channel(cid);
                            if (!ch || ch->device_uid() != "core.generator.audioclip") {
                                all_audio_clips = false;
                                break;
                            }
                            if (!clipper_dev) {
                                auto dev = engine_.get_or_create_channel_device(cid);
                                clipper_dev = dynamic_cast<plugins::AudioClipDevice*>(dev.get());
                            }
                        }
                        if (all_audio_clips && clipper_dev && !clipper_dev->sample_l().empty()) {
                            is_audio_clip = true;
                        }
                    }

                    if (is_audio_clip && clipper_dev) {
                        // Render Waveform Signal Peak Display
                        const auto& smp_l = clipper_dev->sample_l();
                        const size_t num_smp = smp_l.size();
                        float mid_y = body_top + body_h * 0.5f;
                        float amp_h = (body_h * 0.5f) - 1.0f;

                        // Center zero-crossing baseline
                        ID2D1SolidColorBrush* br_wave_base = nullptr;
                        d2d_target_->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.45f, 0.55f, 0.40f), &br_wave_base);
                        if (br_wave_base) {
                            d2d_target_->DrawLine(D2D1::Point2F(cx + 2.0f, mid_y), D2D1::Point2F(cx + cw - 2.0f, mid_y), br_wave_base, 1.0f);
                            br_wave_base->Release();
                        }

                        ID2D1SolidColorBrush* br_wave = nullptr;
                        d2d_target_->CreateSolidColorBrush(D2D1::ColorF(0.20f, 0.92f, 0.88f, 0.85f), &br_wave);
                        if (br_wave && num_smp > 0) {
                            int x_start = static_cast<int>(cx + 2.0f);
                            int x_end = static_cast<int>(cx + cw - 2.0f);

                            double sr = clipper_dev->file_sample_rate() > 0 ? static_cast<double>(clipper_dev->file_sample_rate()) : 44100.0;
                            double bpm = proj.time_map().get_bpm_at(clip.start);
                            double ppq = static_cast<double>(proj.time_map().ppq());
                            double sample_dur_sec = static_cast<double>(num_smp) / sr;
                            double sample_total_ticks = sample_dur_sec * (bpm / 60.0) * ppq;
                            if (sample_total_ticks < 1.0) sample_total_ticks = 1.0;

                            double draw_tick_span = double(draw_end - draw_start);
                            bool looping = clipper_dev->use_loop_points() || clipper_dev->ping_pong_loop();

                            for (int px_col = x_start; px_col < x_end; ++px_col) {
                                double rel_draw = (cw > 0.0f) ? (double(px_col - cx) / double(cw)) : 0.0;
                                domain::Tick tick_at_px = draw_start + static_cast<domain::Tick>(rel_draw * draw_tick_span);
                                domain::Tick tick_in_clip = tick_at_px - clip.start;
                                if (tick_in_clip < 0) continue;

                                double sample_prog = double(tick_in_clip) / sample_total_ticks;
                                if (looping) {
                                    sample_prog = std::fmod(sample_prog, 1.0);
                                    if (sample_prog < 0.0) sample_prog += 1.0;
                                } else if (sample_prog >= 1.0) {
                                    continue;
                                }

                                size_t idx_s = static_cast<size_t>(sample_prog * double(num_smp));
                                double ticks_per_col = (cw > 0.0f) ? (draw_tick_span / double(cw)) : 1.0;
                                double frames_per_tick = sr / ((bpm / 60.0) * ppq);
                                size_t span_frames = std::max(size_t(1), static_cast<size_t>(ticks_per_col * frames_per_tick));
                                size_t idx_e = std::min(num_smp, idx_s + span_frames);

                                float min_v = smp_l[idx_s];
                                float max_v = smp_l[idx_s];
                                size_t step = std::max(size_t(1), (idx_e - idx_s) / 16);
                                for (size_t si = idx_s; si < idx_e; si += step) {
                                    float val = smp_l[si];
                                    if (val < min_v) min_v = val;
                                    if (val > max_v) max_v = val;
                                }

                                float y_top = mid_y - std::clamp(max_v, -1.0f, 1.0f) * amp_h;
                                float y_bot = mid_y - std::clamp(min_v, -1.0f, 1.0f) * amp_h;
                                if (y_top > y_bot) std::swap(y_top, y_bot);
                                if ((y_bot - y_top) < 1.0f) {
                                    y_top = mid_y - 0.5f;
                                    y_bot = mid_y + 0.5f;
                                }
                                d2d_target_->DrawLine(D2D1::Point2F(float(px_col), y_top), D2D1::Point2F(float(px_col), y_bot), br_wave, 1.0f);
                            }
                            br_wave->Release();
                        }
                    } else {
                        // Standard note blocks preview
                        for (const auto& [cid, nset] : clip_pat->channel_notes()) {
                            for (const auto& n : nset.notes()) {
                                if (n.start >= clip.length) continue;
                                domain::Tick abs_n_start = clip.start + n.start;
                                domain::Tick abs_n_end = abs_n_start + n.length;

                                if (abs_n_end > draw_start && abs_n_start < draw_end) {
                                    double n_rel_start = double(abs_n_start - draw_start) / double(draw_end - draw_start);
                                    double n_rel_len = double(n.length) / double(draw_end - draw_start);

                                    float raw_nx = cx + static_cast<float>(n_rel_start * cw);
                                    float raw_nw = std::max(3.0f, static_cast<float>(n_rel_len * cw));
                                    float nx = std::clamp(raw_nx, cx + 1.0f, cx + cw - 2.0f);
                                    float max_r = cx + cw - 1.0f;
                                    float nw = std::max(2.0f, std::min(raw_nw, max_r - nx));
                                    float norm_p = float(n.pitch % 24) / 24.0f;
                                    float ny = body_top + (1.0f - norm_p) * (body_h - 5.0f) + 1.0f;

                                    D2D1_RECT_F note_rc = D2D1::RectF(nx, ny, nx + nw, ny + 2.5f);
                                    D2DRenderer::draw_rounded_box(d2d_target_, note_rc, t.note_silver, t.note_border, 1.0f);
                                }
                            }
                        }
                    }
                }

                // Right Resize Handle
                if (cw > 20.0f && br_border_light) {
                    float rx = std::floor(cx + cw - 4.0f) + 0.5f;
                    d2d_target_->DrawLine(D2D1::Point2F(rx - 2.0f, t_y + 16.0f), D2D1::Point2F(rx - 2.0f, t_y + step_h - 6.0f), br_border_light, 1.0f);
                    d2d_target_->DrawLine(D2D1::Point2F(rx, t_y + 16.0f), D2D1::Point2F(rx, t_y + step_h - 6.0f), br_border_light, 1.0f);
                }
            }
        }

        d2d_target_->PopAxisAlignedClip();

        // 4. Playhead (SPM)
        auto cur_tick = engine_.transport().is_playing() ? engine_.transport().current_tick() : song_position_marker_;
        if (cur_tick >= view_start_tick && cur_tick <= view_end_tick) {
            double norm_pos = double(cur_tick - view_start_tick) / double(view_duration);
            float head_x = ruler_x + static_cast<float>(norm_pos * ruler_w);
            D2DRenderer::draw_playhead(d2d_target_, head_x, ruler_y, max_track_bottom, t.accent, "SPM", dwrite_small_);
        }

        // 5. Horizontal Scrollbar
        float scroll_track_y = py + ph - 20.0f;
        float scroll_track_h = 14.0f;
        float scroll_track_x = ruler_x;
        float scroll_track_w = ruler_w;

        D2D1_RECT_F scroll_lbl_rc = D2D1::RectF(px + 8.0f, scroll_track_y - 2.0f, ruler_x - 6.0f, scroll_track_y + scroll_track_h + 2.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_small_, "↔ TIMELINE", scroll_lbl_rc, t.text_muted,
                              DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        D2D1_RECT_F scroll_track_rc = D2D1::RectF(scroll_track_x, scroll_track_y, scroll_track_x + scroll_track_w, scroll_track_y + scroll_track_h);
        D2DRenderer::draw_rounded_box(d2d_target_, scroll_track_rc, t.bg_control, t.border_subtle, 3.0f);

        float max_scroll = std::max(1.0f, float(max_bars - bars_per_view));
        float thumb_w = std::max(35.0f, (float(bars_per_view) / float(max_bars)) * scroll_track_w);
        float scroll_ratio = std::clamp(float(sequencer_scroll_bar_) / max_scroll, 0.0f, 1.0f);
        float thumb_x = scroll_track_x + scroll_ratio * (scroll_track_w - thumb_w);
        D2D1_RECT_F thumb_rc = D2D1::RectF(thumb_x, scroll_track_y + 1.0f, thumb_x + thumb_w, scroll_track_y + scroll_track_h - 1.0f);

        D2D1_COLOR_F thumb_col = dragging_seq_scrollbar_ ? t.accent_bright : t.border_strong;
        D2DRenderer::draw_rounded_box(d2d_target_, thumb_rc, thumb_col, t.border_default, 3.0f);

        if (br_border_light) br_border_light->Release();
        if (br_border_faint) br_border_faint->Release();
        if (br_border_dark) br_border_dark->Release();
    }

    void render_piano_roll_d2d(const DawWindow& win) {
        const auto& t = D2DRenderer::theme();
        PianoRollLayout lay;
        lay.init(win, PianoRollNumPitches, piano_roll_steps_);

        D2D1_RECT_F roll_rc = win.get_content_rect();
        D2DRenderer::draw_rounded_box(d2d_target_, roll_rc, t.bg_surface, t.border_subtle, 0.0f);

        // 1. Toolbar: Pitch Range, Steps Toggle, Note Length, Clear, and Shortcuts
        auto* ch = engine_.session().project().get_channel(piano_roll_channel_);
        std::string ch_name = ch ? ch->settings().name : "Instrument";

        float tx = win.x + 12.0f;
        // Pitch Range Display Badge (Full 128 semitones C0..B10)
        int max_base_pitch = 128 - PianoRollNumPitches;
        std::string range_str = ch_name + " | " + get_midi_note_name(static_cast<uint8_t>(piano_roll_base_pitch_)) +
                                " — " + get_midi_note_name(static_cast<uint8_t>(piano_roll_base_pitch_ + PianoRollNumPitches - 1));
        D2D1_RECT_F range_rc = D2D1::RectF(tx, lay.toolbar_y, tx + 180.0f, lay.toolbar_y + lay.toolbar_h);
        D2DRenderer::draw_rounded_box(d2d_target_, range_rc, t.bg_control, t.border_subtle, 3.5f);
        D2DRenderer::draw_text(d2d_target_, dwrite_small_, range_str, range_rc, t.accent_bright,
                              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        tx += 186.0f;

        // Steps Toggle (16 or 32 visible)
        std::string step_str = (piano_roll_steps_ == 16) ? "16 Steps" : "32 Steps";
        D2D1_RECT_F step_rc = D2D1::RectF(tx, lay.toolbar_y, tx + 80.0f, lay.toolbar_y + lay.toolbar_h);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, step_rc, step_str, (piano_roll_steps_ == 32), t.accent, t.bg_control, 3.5f);
        tx += 86.0f;

        // Default Note Length
        std::string len_str = "📏 " + std::to_string(piano_roll_note_len_steps_) + (piano_roll_note_len_steps_ == 1 ? " Stp" : " Stps");
        D2D1_RECT_F len_rc = D2D1::RectF(tx, lay.toolbar_y, tx + 80.0f, lay.toolbar_y + lay.toolbar_h);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, len_rc, len_str, false, t.bg_control, t.bg_control, 3.5f);
        tx += 86.0f;

        // Clear Notes button
        D2D1_RECT_F clear_rc = D2D1::RectF(tx, lay.toolbar_y, tx + 60.0f, lay.toolbar_y + lay.toolbar_h);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, clear_rc, "Clear", false, t.danger, t.bg_control, 3.5f);
        tx += 66.0f;

        // Keyboard & Mouse Controls Guide Hint
        if (win.x + win.w - 12.0f > tx + 80.0f) {
            D2D1_RECT_F hint_rc = D2D1::RectF(tx, lay.toolbar_y, win.x + win.w - 12.0f, lay.toolbar_y + lay.toolbar_h);
            D2DRenderer::draw_text(d2d_target_, dwrite_small_,
                                  "💡 Left: Add/Drag • Right: Delete • Wheel: Pitch",
                                  hint_rc, t.text_muted, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        ID2D1SolidColorBrush* br_border_faint = nullptr;
        ID2D1SolidColorBrush* br_border_dark = nullptr;
        ID2D1SolidColorBrush* br_border_strong = nullptr;
        d2d_target_->CreateSolidColorBrush(t.border_subtle, &br_border_faint);
        d2d_target_->CreateSolidColorBrush(t.border_default, &br_border_dark);
        d2d_target_->CreateSolidColorBrush(t.border_strong, &br_border_strong);

        auto* pat = get_active_pattern();
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

    void render_piano_roll_d2d() {
        render_piano_roll_d2d(win_pianoroll_);
    }

    void render_inspector_d2d(const DawWindow& win) {
        if (!win.visible) return;
        const auto& t = D2DRenderer::theme();

        float insp_x = win.x;
        float insp_y = win.y + DawWindow::kTitleBarHeight;
        float insp_r = win.x + win.w;
        float insp_bot = win.y + win.h;
        if (insp_bot <= insp_y + 80.0f) return;

        // Outer Content Box
        D2D1_RECT_F panel_rc = win.get_content_rect();
        D2DRenderer::draw_rounded_box(d2d_target_, panel_rc, t.bg_surface, t.border_subtle, 0.0f);

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
        D2D1_RECT_F sum_card_rc = D2D1::RectF(insp_x + 8.0f, insp_y + 8.0f, insp_r - 8.0f, insp_y + 126.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, sum_card_rc, t.bg_surface_2, t.border_subtle, 4.0f);

        // Track Badge & Name
        D2D1_RECT_F badge_rc = D2D1::RectF(insp_x + 14.0f, insp_y + 14.0f, insp_x + 80.0f, insp_y + 32.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, badge_rc, t.accent_deep, t.accent_bright, 3.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_small_, tr_badge, badge_rc, D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f),
                              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        D2D1_RECT_F name_rc = D2D1::RectF(insp_x + 86.0f, insp_y + 14.0f, insp_r - 14.0f, insp_y + 32.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, tr_name, name_rc, t.text_primary,
                              DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Routing Info
        D2D1_RECT_F route_rc = D2D1::RectF(insp_x + 14.0f, insp_y + 34.0f, insp_r - 14.0f, insp_y + 50.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_small_, route_str, route_rc, t.text_muted,
                              DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Volume Horizontal Slider
        float cur_vol = track ? track->volume() : 0.8f;
        float norm_vol = std::clamp(cur_vol / 1.25f, 0.0f, 1.0f);
        int vol_pct = static_cast<int>(std::round(cur_vol * 100.0f));
        std::string vol_lbl = "Vol: " + std::to_string(vol_pct) + "%";
        D2D1_RECT_F vol_rc = D2D1::RectF(insp_x + 14.0f, insp_y + 54.0f, insp_r - 14.0f, insp_y + 72.0f);
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
        D2D1_RECT_F pan_rc = D2D1::RectF(insp_x + 14.0f, insp_y + 76.0f, insp_r - 14.0f, insp_y + 94.0f);
        D2DRenderer::draw_slider_horizontal(d2d_target_, dwrite_small_, pan_rc, norm_pan, pan_lbl);

        // Mute & Solo Buttons
        bool is_mute = track ? track->muted() : false;
        bool is_solo = track ? track->solo() : false;
        float mid_w = (insp_r - 14.0f) - (insp_x + 14.0f);
        float btn_w = (mid_w - 6.0f) * 0.5f;

        D2D1_RECT_F mute_rc = D2D1::RectF(insp_x + 14.0f, insp_y + 98.0f, insp_x + 14.0f + btn_w, insp_y + 118.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, mute_rc, "MUTE", is_mute, t.danger, t.bg_control, 3.0f);

        D2D1_RECT_F solo_rc = D2D1::RectF(insp_x + 14.0f + btn_w + 6.0f, insp_y + 98.0f, insp_r - 14.0f, insp_y + 118.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, solo_rc, "SOLO", is_solo, t.warning, t.bg_control, 3.0f);

        // FX Inserts Section Header
        size_t num_fx = track ? track->inserts().size() : 0;
        std::string fx_sec_title = "CHANNEL FX INSERTS (" + std::to_string(num_fx) + "/10)";
        D2D1_RECT_F fx_title_rc = D2D1::RectF(insp_x + 12.0f, insp_y + 132.0f, insp_r - 12.0f, insp_y + 152.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_bold_, fx_sec_title, fx_title_rc, t.text_secondary,
                              DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Render Insert Slots (up to 10 slots per track)
        float slot_y_start = insp_y + 156.0f;
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

    void render_inspector_d2d() {
        render_inspector_d2d(win_inspector_);
    }

    void render_audio_library_d2d(const DawWindow& win) {
        const auto& t = D2DRenderer::theme();
        D2D1_RECT_F content_rc = win.get_content_rect();
        D2DRenderer::draw_rounded_box(d2d_target_, content_rc, t.bg_surface, t.border_subtle, 0.0f);

        float px = win.x;
        float py = win.y + DawWindow::kTitleBarHeight;
        float pw = win.w;
        float ph = win.h - DawWindow::kTitleBarHeight;

        // 1. Top Action Toolbar
        // [📂 Browse...]
        D2D1_RECT_F browse_rc = D2D1::RectF(px + 8.0f, py + 6.0f, px + 96.0f, py + 30.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, browse_rc, "📂 Browse...", false, t.accent, t.bg_control, 3.0f);

        // [🔄 Rescan]
        D2D1_RECT_F rescan_rc = D2D1::RectF(px + 102.0f, py + 6.0f, px + 174.0f, py + 30.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, rescan_rc, "🔄 Rescan", false, t.bg_control, t.bg_control, 3.0f);

        // [⚡ Quick Samples]
        D2D1_RECT_F quick_rc = D2D1::RectF(px + 180.0f, py + 6.0f, px + pw - 8.0f, py + 30.0f);
        D2DRenderer::draw_button(d2d_target_, dwrite_small_, quick_rc, "⚡ Quick Load", false, t.accent_bright, t.bg_control, 3.0f);

        // 2. Directory Path & Summary Bar
        float dir_y = py + 34.0f;
        D2D1_RECT_F dir_rc = D2D1::RectF(px + 8.0f, dir_y, px + pw - 8.0f, dir_y + 24.0f);
        D2DRenderer::draw_rounded_box(d2d_target_, dir_rc, t.bg_surface_2, t.border_subtle, 3.0f);

        std::string cur_dir = sample_library_.current_directory();
        std::string dir_display = cur_dir.empty() ? "Folder: (No folder selected - Click Browse)" : ("📁 " + cur_dir);
        if (dir_display.length() > 36) {
            dir_display = dir_display.substr(0, 16) + "..." + dir_display.substr(dir_display.length() - 17);
        }
        D2D1_RECT_F dir_txt_rc = D2D1::RectF(px + 14.0f, dir_y + 2.0f, px + pw - 80.0f, dir_y + 22.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_small_, dir_display, dir_txt_rc, t.text_secondary,
                              DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        std::string count_txt = std::to_string(sample_library_.size()) + " files";
        D2D1_RECT_F count_rc = D2D1::RectF(px + pw - 78.0f, dir_y + 2.0f, px + pw - 12.0f, dir_y + 22.0f);
        D2DRenderer::draw_text(d2d_target_, dwrite_small_, count_txt, count_rc, t.accent_bright,
                              DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Divider
        ID2D1SolidColorBrush* br_div = nullptr;
        d2d_target_->CreateSolidColorBrush(t.border_subtle, &br_div);
        if (br_div) {
            d2d_target_->DrawLine(D2D1::Point2F(px + 8.0f, py + 62.0f), D2D1::Point2F(px + pw - 8.0f, py + 62.0f), br_div, 1.0f);
            br_div->Release();
        }

        // 3. Sample List Content Area
        float list_top = py + 66.0f;
        float list_bot = py + ph - 8.0f;
        float list_h = list_bot - list_top;
        float row_h = 36.0f;
        int vis_rows = std::max(1, static_cast<int>(list_h / row_h));

        if (sample_library_.empty()) {
            D2D1_RECT_F empty_rc1 = D2D1::RectF(px + 16.0f, list_top + 40.0f, px + pw - 16.0f, list_top + 70.0f);
            D2DRenderer::draw_text(d2d_target_, dwrite_bold_, "📁 Sample Library Empty", empty_rc1, t.text_secondary,
                                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            D2D1_RECT_F empty_rc2 = D2D1::RectF(px + 16.0f, list_top + 75.0f, px + pw - 16.0f, list_top + 140.0f);
            std::string hint = "Click [Browse...] above to choose a folder of samples,\nor click [Quick Load] to load project sounds.\n\nDrag any audio file to Channel Rack or Playlist!";
            D2DRenderer::draw_text(d2d_target_, dwrite_small_, hint, empty_rc2, t.text_muted,
                                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            return;
        }

        int max_scroll = std::max(0, static_cast<int>(sample_library_.size()) - vis_rows);
        audio_lib_scroll_idx_ = std::clamp(audio_lib_scroll_idx_, 0, max_scroll);

        // List clip rect
        D2D1_RECT_F list_clip_rc = D2D1::RectF(px + 4.0f, list_top, px + pw - 4.0f, list_bot);
        d2d_target_->PushAxisAlignedClip(list_clip_rc, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        for (int r = 0; r < vis_rows; ++r) {
            size_t s_idx = static_cast<size_t>(audio_lib_scroll_idx_ + r);
            if (s_idx >= sample_library_.size()) break;
            const auto& smp = sample_library_.samples()[s_idx];
            float ry = list_top + float(r) * row_h;

            bool is_sel = (selected_sample_idx_ == static_cast<int>(s_idx));
            bool is_hov = (hover_sample_idx_ == static_cast<int>(s_idx));

            D2D1_RECT_F row_rc = D2D1::RectF(px + 8.0f, ry, px + pw - 22.0f, ry + row_h - 2.0f);
            D2D1_COLOR_F bg_col = is_sel ? D2D1::ColorF(0.12f, 0.28f, 0.38f, 0.90f)
                                        : (is_hov ? t.bg_surface_2
                                                  : (r % 2 == 0 ? t.bg_surface : t.bg_app));
            D2D1_COLOR_F bdr_col = is_sel ? t.accent_bright : (is_hov ? t.border_default : t.border_subtle);
            D2DRenderer::draw_rounded_box(d2d_target_, row_rc, bg_col, bdr_col, 3.0f, is_sel ? 1.5f : 1.0f);

            // Preview Play button [▶]
            D2D1_RECT_F play_btn_rc = D2D1::RectF(px + 12.0f, ry + 5.0f, px + 36.0f, ry + row_h - 7.0f);
            D2DRenderer::draw_button(d2d_target_, dwrite_small_, play_btn_rc, "▶", false, t.accent_bright, t.bg_control, 3.0f);

            // Format Badge (e.g. WAV, MP3, etc.)
            std::string ext_badge = smp.extension;
            if (!ext_badge.empty() && ext_badge[0] == '.') ext_badge = ext_badge.substr(1);
            std::transform(ext_badge.begin(), ext_badge.end(), ext_badge.begin(), ::toupper);
            D2D1_RECT_F badge_rc = D2D1::RectF(px + 40.0f, ry + 7.0f, px + 76.0f, ry + row_h - 9.0f);
            D2DRenderer::draw_rounded_box(d2d_target_, badge_rc, t.bg_control, t.border_subtle, 2.0f);
            D2DRenderer::draw_text(d2d_target_, dwrite_small_, ext_badge, badge_rc, t.accent,
                                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            // Sample Name
            D2D1_RECT_F name_rc = D2D1::RectF(px + 82.0f, ry + 2.0f, px + pw - 28.0f, ry + 19.0f);
            D2DRenderer::draw_text(d2d_target_, dwrite_bold_, smp.name, name_rc, t.text_primary,
                                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            // Secondary Details (Duration, Channels, Size)
            std::string details = smp.formatted_duration() + " • " + smp.formatted_channels() + " • " + smp.formatted_size();
            D2D1_RECT_F detail_rc = D2D1::RectF(px + 82.0f, ry + 18.0f, px + pw - 28.0f, ry + row_h - 4.0f);
            D2DRenderer::draw_text(d2d_target_, dwrite_small_, details, detail_rc, t.text_secondary,
                                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        d2d_target_->PopAxisAlignedClip();

        // Vertical Scrollbar if needed
        if (max_scroll > 0) {
            float sb_x = px + pw - 18.0f;
            float sb_w = 8.0f;
            D2D1_RECT_F track_rc = D2D1::RectF(sb_x, list_top, sb_x + sb_w, list_bot);
            D2DRenderer::draw_rounded_box(d2d_target_, track_rc, t.bg_control, t.border_subtle, 2.0f);

            float thumb_h = std::max(20.0f, (float(vis_rows) / float(sample_library_.size())) * list_h);
            float scroll_ratio = float(audio_lib_scroll_idx_) / float(max_scroll);
            float thumb_y = list_top + scroll_ratio * (list_h - thumb_h);
            D2D1_RECT_F thumb_rc = D2D1::RectF(sb_x, thumb_y, sb_x + sb_w, thumb_y + thumb_h);
            D2DRenderer::draw_rounded_box(d2d_target_, thumb_rc, t.accent_bright, t.accent_bright, 2.0f);
        }
    }

    void render_mixer_panel_d2d(const DawWindow& win) {
        const auto& t = D2DRenderer::theme();
        D2D1_RECT_F mixer_rc = win.get_content_rect();
        D2DRenderer::draw_rounded_box(d2d_target_, mixer_rc, t.bg_surface, t.border_subtle, 0.0f);

        auto& proj = engine_.session().project();
        float mx = win.x;
        float my = win.y + DawWindow::kTitleBarHeight;
        float mw = win.w;
        float mh = win.h - DawWindow::kTitleBarHeight;

        float track_w = 96.0f;
        float gap = 10.0f;
        float ty = my + 26.0f;

        float master_x = mx + 12.0f;
        float insert_start_x = master_x + track_w + gap;
        float avail_w = (mx + mw - 12.0f) - insert_start_x;
        int vis_inserts = std::max(1, static_cast<int>(avail_w / (track_w + gap)));
        size_t total_inserts = std::max(size_t(4), proj.channels().size());
        int max_mix_scroll = std::max(0, static_cast<int>(total_inserts) - vis_inserts);
        mixer_scroll_track_ = std::clamp(mixer_scroll_track_, 0, max_mix_scroll);

        // Header Track Navigation Buttons & Range if tracks exceed window width
        if (max_mix_scroll > 0) {
            std::string mix_lbl = "Tracks " + std::to_string(1 + mixer_scroll_track_) + "-" +
                                  std::to_string(std::min(total_inserts, size_t(1 + mixer_scroll_track_ + vis_inserts - 1))) +
                                  " / " + std::to_string(total_inserts);
            D2D1_RECT_F mix_lbl_rc = D2D1::RectF(mx + mw - 240.0f, my + 4.0f,
                                                 mx + mw - 85.0f, my + 24.0f);
            D2DRenderer::draw_text(d2d_target_, dwrite_small_, mix_lbl, mix_lbl_rc, t.text_secondary,
                                  DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            D2D1_RECT_F btn_l = D2D1::RectF(mx + mw - 80.0f, my + 3.0f,
                                           mx + mw - 52.0f, my + 23.0f);
            D2D1_RECT_F btn_r = D2D1::RectF(mx + mw - 48.0f, my + 3.0f,
                                           mx + mw - 20.0f, my + 23.0f);
            D2DRenderer::draw_button(d2d_target_, dwrite_small_, btn_l, "◀", false, t.bg_control, t.bg_control, 3.0f);
            D2DRenderer::draw_button(d2d_target_, dwrite_small_, btn_r, "▶", false, t.bg_control, t.bg_control, 3.0f);
        }

        // Lambda to render a single mixer track strip
        auto render_strip = [&](int tid, float tx) {
            float strip_h = std::max(120.0f, mh - 30.0f);
            D2D1_RECT_F ch_box = D2D1::RectF(tx, ty, tx + track_w, ty + strip_h);
            bool is_selected = (static_cast<uint32_t>(tid) == selected_mixer_track_);
            D2D1_COLOR_F border_c = is_selected ? t.accent_bright : t.border_subtle;
            D2DRenderer::draw_rounded_box(d2d_target_, ch_box, t.bg_surface_2, border_c, 4.0f, is_selected ? 1.5f : 1.0f);

            // Track Header Name
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
            float fader_bot = ty + strip_h - 52.0f;
            float peak_l = (static_cast<size_t>(tid) < meter_peaks_l_.size()) ? meter_peaks_l_[tid] : 0.0f;
            float peak_r = (static_cast<size_t>(tid) < meter_peaks_r_.size()) ? meter_peaks_r_[tid] : 0.0f;
            D2D1_RECT_F meter_rc = D2D1::RectF(tx + 8.0f, ty + 46.0f, tx + 28.0f, fader_bot);
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
            D2D1_RECT_F fader_rc = D2D1::RectF(tx + 33.0f, ty + 46.0f, tx + 88.0f, fader_bot);
            D2DRenderer::draw_slider_vertical(d2d_target_, dwrite_main_, dwrite_small_, fader_rc, norm_gain, ss_db.str());

            // Rotary Panning Knob Underneath Fader & Meters
            D2D1_RECT_F pan_rc = D2D1::RectF(tx + 6.0f, fader_bot + 4.0f, tx + track_w - 6.0f, ty + strip_h - 4.0f);
            D2DRenderer::draw_pan_knob(d2d_target_, dwrite_small_, pan_rc, pan_val, "PAN");
        };

        // 1. Render Pinned Master Track (track 0)
        render_strip(0, master_x);

        // 2. Render Visible Insert Tracks
        for (int s = 0; s < vis_inserts; ++s) {
            int tid = 1 + mixer_scroll_track_ + s;
            if (static_cast<size_t>(tid) > total_inserts) break;
            float tx = insert_start_x + s * (track_w + gap);
            render_strip(tid, tx);
        }
    }

    void render_mixer_panel_d2d() {
        render_mixer_panel_d2d(win_mixer_);
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
        if (!dev) return;

        auto* clipper = dynamic_cast<plugins::AudioClipDevice*>(dev);
        if (clipper) {
            float mw = 760.0f;
            float mh = 540.0f;
            float mx = (static_cast<float>(client_w_) - mw) * 0.5f;
            float my = (static_cast<float>(client_h_) - mh) * 0.5f;
            editor_bounds_ = {static_cast<LONG>(mx), static_cast<LONG>(my), static_cast<LONG>(mx + mw), static_cast<LONG>(my + mh)};

            uint8_t m_track = 0;
            auto* ch = engine_.session().project().get_channel(active_editor_channel_);
            if (ch) m_track = ch->settings().mixer_track;

            AudioClipEditor::render_d2d(d2d_target_, dwrite_bold_, dwrite_small_, clipper, editor_bounds_,
                                       clipper_active_tab_, clipper_env_subtab_, m_track);
            return;
        }

        auto* xsynth = dynamic_cast<plugins::XSynthDevice*>(dev);
        if (xsynth) {
            float mw = 700.0f;
            float mh = 480.0f;
            float mx = (static_cast<float>(client_w_) - mw) * 0.5f;
            float my = (static_cast<float>(client_h_) - mh) * 0.5f;
            editor_bounds_ = {static_cast<LONG>(mx), static_cast<LONG>(my), static_cast<LONG>(mx + mw), static_cast<LONG>(my + mh)};
            XAudioEditor::render_xsynth_d2d(d2d_target_, dwrite_bold_, dwrite_small_, xsynth, editor_bounds_);
            return;
        }

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

    void render_effect_editor_d2d() {
        if (active_editor_effect_track_ < 0 || active_editor_effect_slot_ < 0) return;
        auto* track = engine_.session().project().mixer_graph().get_track(static_cast<uint32_t>(active_editor_effect_track_));
        if (!track || static_cast<size_t>(active_editor_effect_slot_) >= track->inserts().size()) return;
        auto& ins = track->inserts()[active_editor_effect_slot_];
        if (!ins.device) return;

        float mw = std::clamp(static_cast<float>(client_w_) - 60.0f, 840.0f, 1100.0f);
        float mh = std::clamp(static_cast<float>(client_h_) - 60.0f, 640.0f, 780.0f);
        float mx = (static_cast<float>(client_w_) - mw) * 0.5f;
        float my = (static_cast<float>(client_h_) - mh) * 0.5f;
        effect_editor_bounds_ = {static_cast<LONG>(mx), static_cast<LONG>(my), static_cast<LONG>(mx + mw), static_cast<LONG>(my + mh)};

        XAudioEditor::render_effect_d2d(d2d_target_, dwrite_bold_, dwrite_small_,
                                       ins.device.get(), effect_editor_bounds_,
                                       ins.enabled, ins.wet_mix, active_editor_effect_track_,
                                       active_editor_effect_slot_, effect_editor_scroll_idx_);
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

    void render_window_frame_gdi(const DawWindow& win) {
        const auto& t = get_theme();
        bool is_active = (active_window_ == win.id);

        int wx = static_cast<int>(win.x);
        int wy = static_cast<int>(win.y);
        int ww = static_cast<int>(win.w);
        int wh = static_cast<int>(win.h);
        int tbh = static_cast<int>(DawWindow::kTitleBarHeight);

        // 0. Fill solid opaque background
        RECT full_rc{wx, wy, wx + ww, wy + wh};
        GuiRenderer::fill_rect(mem_dc_, full_rc, t.bg_surface);

        // 1. Title bar background
        RECT tb_rc{wx, wy, wx + ww, wy + tbh};
        GuiRenderer::draw_rounded_box(mem_dc_, tb_rc, is_active ? t.bg_surface_2 : t.bg_surface, is_active ? t.accent : t.border_subtle, 3);

        // 2. Active Accent Line at top
        if (is_active) {
            RECT acc_rc{wx, wy, wx + ww, wy + 2};
            GuiRenderer::fill_rect(mem_dc_, acc_rc, t.accent);
        }

        // 3. SVG Window Icon
        RECT icon_rc{wx + 6, wy + 5, wx + 24, wy + 23};
        GuiRenderer::draw_svg_icon(mem_dc_, win.icon, icon_rc, is_active ? t.accent_bright : t.text_secondary, 15);

        // 4. Title text
        SelectObject(mem_dc_, font_bold_);
        RECT title_rc{wx + 28, wy, wx + ww - 32, wy + tbh};
        GuiRenderer::draw_text(mem_dc_, win.title, title_rc, is_active ? t.text_primary : t.text_muted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // 5. Close button [✕] with Danger hover
        RECT close_rc{wx + ww - 24, wy + 4, wx + ww - 4, wy + tbh - 4};
        bool is_close_hover = (hover_close_win_id_ == static_cast<int>(win.id));
        GuiRenderer::draw_button(mem_dc_, close_rc, "✕", false, is_close_hover ? t.danger : t.bg_control, is_close_hover ? t.danger : t.border_subtle);

        // 6. Outer border
        HPEN border_pen = CreatePen(PS_SOLID, is_active ? 2 : 1, is_active ? t.accent : t.border_subtle);
        HGDIOBJ old_pen = SelectObject(mem_dc_, border_pen);
        HGDIOBJ old_brush = SelectObject(mem_dc_, GetStockObject(NULL_BRUSH));
        Rectangle(mem_dc_, wx, wy, wx + ww, wy + wh);
        SelectObject(mem_dc_, old_brush);
        SelectObject(mem_dc_, old_pen);
        DeleteObject(border_pen);
    }

    void render(HDC target_hdc) {
        if (!mem_dc_ || client_w_ <= 0 || client_h_ <= 0) return;

        const auto& t = get_theme();
        RECT full_rc{0, 0, client_w_, client_h_};
        GuiRenderer::fill_rect(mem_dc_, full_rc, t.bg_main);

        // 1. Render Top Transport Bar (Play, Pause, Stop, Tempo, Time, SPM Readout, View Tabs)
        render_transport_bar();

        // 2. Render each visible window in z_order_ (back to front)
        for (WindowId wid : z_order_) {
            auto* win = get_window(wid);
            if (!win || !win->visible) continue;

            render_window_frame_gdi(*win);

            int cx = static_cast<int>(win->x);
            int cy = static_cast<int>(win->y + DawWindow::kTitleBarHeight);
            int cw = static_cast<int>(win->w);
            int ch = static_cast<int>(win->h - DawWindow::kTitleBarHeight);

            HRGN clip_rgn = CreateRectRgn(cx, cy, cx + cw, cy + ch);
            SelectClipRgn(mem_dc_, clip_rgn);

            switch (wid) {
                case WindowId::ChannelRack:
                    render_channel_rack(*win);
                    break;
                case WindowId::Playlist:
                    render_playlist(*win);
                    break;
                case WindowId::PianoRoll:
                    render_piano_roll(*win);
                    break;
                case WindowId::Mixer:
                    render_mixer_panel(*win);
                    break;
                case WindowId::Inspector:
                    render_inspector_gdi(*win);
                    break;
                case WindowId::AudioLibrary:
                    render_audio_library(*win);
                    break;
            }

            SelectClipRgn(mem_dc_, NULL);
            DeleteObject(clip_rgn);
        }

        // 4. Render Status Bar
        render_status_bar();

        // 5. Render Floating Plugin Editor Modal if open
        if (active_editor_channel_ != 0) {
            render_plugin_editor();
        }
        if (active_editor_effect_track_ >= 0 && active_editor_effect_slot_ >= 0) {
            render_effect_editor_gdi();
        }

        if (is_dragging_sample_) {
            int bx = current_mouse_x_ + 14;
            int by = current_mouse_y_ + 14;
            RECT badge_rc{bx, by, bx + 200, by + 28};
            GuiRenderer::draw_rounded_box(mem_dc_, badge_rc, RGB(20, 30, 40), t.accent, 4);
            std::string badge_txt = "🎵 " + dragged_sample_name_;
            GuiRenderer::draw_text(mem_dc_, badge_txt, badge_rc, t.accent_bright, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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

        // App Logo (wide bounds so "DigiDAW" never wraps down)
        SelectObject(mem_dc_, font_title_);
        RECT title_rc{16, 0, 130, 48};
        GuiRenderer::draw_text(mem_dc_, "DigiDAW", title_rc, t.text_primary, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Elegant vertical divider between brand and project info
        HPEN divPen = CreatePen(PS_SOLID, 1, t.border_subtle);
        HGDIOBJ oldDivPen = SelectObject(mem_dc_, divPen);
        MoveToEx(mem_dc_, 138, 14, NULL);
        LineTo(mem_dc_, 138, 34);
        SelectObject(mem_dc_, oldDivPen);
        DeleteObject(divPen);

        // Project Name badge (with generous breathing space from DigiDAW)
        SelectObject(mem_dc_, font_bold_);
        std::string proj_name = engine_.session().project().name();
        if (proj_name.empty()) proj_name = "Untitled Project";
        RECT proj_rc{200, 8, 310, 40};
        GuiRenderer::draw_rounded_box(mem_dc_, proj_rc, t.bg_control, t.border_subtle, 3);
        GuiRenderer::draw_text(mem_dc_, proj_name, proj_rc, t.accent_bright, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        // Undo & Redo buttons
        SelectObject(mem_dc_, font_bold_);
        bool can_undo = engine_.session().undo_stack().can_undo();
        bool can_redo = engine_.session().undo_stack().can_redo();
        RECT undo_rc{316, 8, 346, 40};
        GuiRenderer::draw_rounded_box(mem_dc_, undo_rc, t.bg_control, can_undo ? t.border_default : t.border_subtle, 3);
        GuiRenderer::draw_text(mem_dc_, "↶", undo_rc, can_undo ? t.text_primary : t.text_disabled, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT redo_rc{350, 8, 380, 40};
        GuiRenderer::draw_rounded_box(mem_dc_, redo_rc, t.bg_control, can_redo ? t.border_default : t.border_subtle, 3);
        GuiRenderer::draw_text(mem_dc_, "↷", redo_rc, can_redo ? t.text_primary : t.text_disabled, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Transport Buttons: PLAY, PAUSE, STOP (Icon-only)
        bool is_playing = engine_.transport().is_playing();
        bool is_paused = (engine_.transport().state() == app::TransportState::Paused);

        // 1. PLAY Button
        RECT play_rc{388, 8, 424, 40};
        GuiRenderer::draw_icon_button(mem_dc_, play_rc, SvgIconType::Play, is_playing, t.accent, t.bg_control);

        // 2. PAUSE Button
        RECT pause_rc{428, 8, 464, 40};
        GuiRenderer::draw_icon_button(mem_dc_, pause_rc, SvgIconType::Pause, is_paused, t.warning, t.bg_control);

        // 3. STOP Button
        RECT stop_rc{468, 8, 504, 40};
        GuiRenderer::draw_icon_button(mem_dc_, stop_rc, SvgIconType::Stop, false, t.danger, t.bg_control);

        // 4. Playback Mode (PAT / SONG) Toggle Button
        bool is_pat_mode = (engine_.transport().mode() == app::PlaybackMode::Pattern);
        RECT mode_rc{508, 8, 568, 40};
        GuiRenderer::draw_rounded_box(mem_dc_, mode_rc, t.bg_control, is_pat_mode ? t.warning : t.accent, 3);
        GuiRenderer::draw_text(mem_dc_, is_pat_mode ? "PAT" : "SONG", mode_rc,
                              is_pat_mode ? t.warning : t.accent_bright, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Tempo BPM Controls
        double bpm = engine_.session().project().time_map().get_bpm_at(0);
        std::stringstream ss_bpm;
        ss_bpm << std::fixed << std::setprecision(1) << bpm << " BPM";

        RECT bpm_minus_rc{574, 8, 598, 40};
        GuiRenderer::draw_button(mem_dc_, bpm_minus_rc, "-", false, t.bg_control, t.bg_control);

        RECT bpm_disp_rc{602, 8, 682, 40};
        GuiRenderer::draw_rounded_box(mem_dc_, bpm_disp_rc, t.bg_control, t.border_subtle, 3);
        GuiRenderer::draw_text(mem_dc_, ss_bpm.str(), bpm_disp_rc, t.text_primary, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT bpm_plus_rc{686, 8, 710, 40};
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

        RECT pos_rc{716, 8, 848, 40};
        GuiRenderer::draw_rounded_box(mem_dc_, pos_rc, t.bg_control, t.border_subtle, 3);
        GuiRenderer::draw_text(mem_dc_, ss_pos.str(), pos_rc, t.accent_bright, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Window / View Buttons: Channel Rack, Playlist, Piano Roll, Mixer, Inspector, and Magnet Snapping
        RECT rack_btn_rc{854, 8, 890, 40};
        bool rack_top = (active_window_ == WindowId::ChannelRack && win_channel_rack_.visible);
        GuiRenderer::draw_icon_button(mem_dc_, rack_btn_rc, SvgIconType::ChannelRack,
                                      rack_top, t.accent, win_channel_rack_.visible ? t.bg_surface_2 : t.bg_control);

        RECT pl_btn_rc{894, 8, 930, 40};
        bool pl_top = (active_window_ == WindowId::Playlist && win_playlist_.visible);
        GuiRenderer::draw_icon_button(mem_dc_, pl_btn_rc, SvgIconType::Playlist,
                                      pl_top, t.accent, win_playlist_.visible ? t.bg_surface_2 : t.bg_control);

        RECT roll_btn_rc{934, 8, 970, 40};
        bool pr_top = (active_window_ == WindowId::PianoRoll && win_pianoroll_.visible);
        GuiRenderer::draw_icon_button(mem_dc_, roll_btn_rc, SvgIconType::PianoRoll,
                                      pr_top, t.accent, win_pianoroll_.visible ? t.bg_surface_2 : t.bg_control);

        RECT mix_btn_rc{974, 8, 1010, 40};
        bool mx_top = (active_window_ == WindowId::Mixer && win_mixer_.visible);
        GuiRenderer::draw_icon_button(mem_dc_, mix_btn_rc, SvgIconType::Mixer,
                                      mx_top, t.accent, win_mixer_.visible ? t.bg_surface_2 : t.bg_control);

        RECT insp_btn_rc{1014, 8, 1050, 40};
        bool insp_top = (active_window_ == WindowId::Inspector && win_inspector_.visible);
        GuiRenderer::draw_icon_button(mem_dc_, insp_btn_rc, SvgIconType::TrackFx,
                                      insp_top, t.accent, win_inspector_.visible ? t.bg_surface_2 : t.bg_control);

        RECT lib_btn_rc{1054, 8, 1090, 40};
        bool lib_top = (active_window_ == WindowId::AudioLibrary && win_audio_library_.visible);
        GuiRenderer::draw_icon_button(mem_dc_, lib_btn_rc, SvgIconType::Folder,
                                      lib_top, t.accent, win_audio_library_.visible ? t.bg_surface_2 : t.bg_control);

        RECT mag_btn_rc{1094, 8, 1130, 40};
        GuiRenderer::draw_icon_button(mem_dc_, mag_btn_rc, SvgIconType::Magnet,
                                      magnet_enabled_, t.accent, t.bg_control);

        // Analog Real-Time Audio Signal Oscilloscope Section (Electric violet phosphor filament)
        int spec_x = 1138;
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

        // Action Buttons: Save [Save SVG] and Export [Export SVG]
        SelectObject(mem_dc_, font_main_);
        RECT save_rc{client_w_ - 80, 8, client_w_ - 45, 40};
        GuiRenderer::draw_icon_button(mem_dc_, save_rc, SvgIconType::Save, false, t.bg_control, t.bg_control);

        RECT rend_rc{client_w_ - 41, 8, client_w_ - 10, 40};
        GuiRenderer::draw_icon_button(mem_dc_, rend_rc, SvgIconType::Export, false, t.accent, t.bg_control);
    }

    void render_channel_rack(const DawWindow& win) {
        const auto& t = get_theme();
        int px = static_cast<int>(win.x);
        int py = static_cast<int>(win.y + DawWindow::kTitleBarHeight);
        int pw = static_cast<int>(win.w);
        int ph = static_cast<int>(win.h - DawWindow::kTitleBarHeight);

        RECT rack_rc{px, py, px + pw, py + ph};
        GuiRenderer::draw_rounded_box(mem_dc_, rack_rc, t.bg_surface, t.border_subtle, 0);

        auto& proj = engine_.session().project();
        auto ppq = proj.time_map().ppq();
        auto step_ticks = ppq / 4;
        auto* pat = get_active_pattern();

        // 1. Header Toolbar (py + 4 .. py + 30)
        SelectObject(mem_dc_, font_small_);
        RECT prev_pat_rc{px + 8, py + 4, px + 28, py + 30};
        GuiRenderer::draw_button(mem_dc_, prev_pat_rc, "◄", false, t.bg_control, t.bg_control);

        std::string pat_name = pat ? pat->name() : "Pattern 1";
        RECT pat_name_rc{px + 32, py + 4, px + 150, py + 30};
        GuiRenderer::draw_rounded_box(mem_dc_, pat_name_rc, t.bg_control, t.border_subtle, 3);
        GuiRenderer::draw_text(mem_dc_, pat_name, pat_name_rc, t.accent_bright, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT next_pat_rc{px + 154, py + 4, px + 174, py + 30};
        GuiRenderer::draw_button(mem_dc_, next_pat_rc, "►", false, t.bg_control, t.bg_control);

        RECT add_pat_rc{px + 178, py + 4, px + 200, py + 30};
        GuiRenderer::draw_button(mem_dc_, add_pat_rc, "+", false, t.bg_control, t.bg_control);

        RECT del_pat_rc{px + 204, py + 4, px + 226, py + 30};
        GuiRenderer::draw_button(mem_dc_, del_pat_rc, "−", false, t.bg_control, t.bg_control);

        // Dynamic Steps Display
        float grid_w_est = std::max(60.0f, float((px + pw - 12) - (px + 240)));
        int cur_bars = get_channel_rack_num_bars(grid_w_est);
        channel_rack_steps_ = cur_bars * 16;
        std::string steps_lbl = std::to_string(channel_rack_steps_) + " Steps (" + std::to_string(cur_bars) + " Bars)";
        RECT steps_rc{px + 232, py + 4, px + 342, py + 30};
        GuiRenderer::draw_button(mem_dc_, steps_rc, steps_lbl, true, t.accent, t.bg_control);

        RECT add_inst_rc{px + 348, py + 4, px + 460, py + 30};
        GuiRenderer::draw_button(mem_dc_, add_inst_rc, "+ Add Instrument", false, t.accent, t.bg_control);

        // 2. Channel Rows
        int start_y = py + 38;
        int row_h = 38;
        int rack_avail_h = (py + ph - 8) - start_y;
        int visible_channels = std::max(1, rack_avail_h / row_h);
        int max_ch_scroll = std::max(0, static_cast<int>(proj.channels().size()) - visible_channels);
        channel_rack_scroll_ch_ = std::clamp(channel_rack_scroll_ch_, 0, max_ch_scroll);

        HRGN clip_rgn = CreateRectRgn(px + 4, start_y, px + pw - 4, py + ph - 4);
        SelectClipRgn(mem_dc_, clip_rgn);

        size_t start_ch = static_cast<size_t>(channel_rack_scroll_ch_);
        size_t end_ch = std::min(proj.channels().size(), start_ch + static_cast<size_t>(visible_channels) + 1);

        domain::Tick pat_len_ticks = pat ? pat->length_ticks(ppq) : (4 * ppq);
        if (pat_len_ticks <= 0) pat_len_ticks = 4 * ppq;
        domain::Tick cur_tick = engine_.transport().current_tick();
        int cur_step = engine_.transport().is_playing() ? static_cast<int>((cur_tick % pat_len_ticks) / step_ticks) : -1;

        for (size_t ch_idx = start_ch; ch_idx < end_ch; ++ch_idx) {
            auto& ch = proj.channels()[ch_idx];
            int row_y = start_y + static_cast<int>(ch_idx - start_ch) * row_h;
            if (row_y + row_h > py + ph) break;

            RECT row_bg_rc{px + 6, row_y + 1, px + pw - 6, row_y + row_h - 1};
            COLORREF row_bg_col = (ch_idx % 2 == 0) ? t.bg_surface : t.bg_app;
            GuiRenderer::draw_rounded_box(mem_dc_, row_bg_rc, row_bg_col, t.border_subtle, 2);

            // Mute / Active LED indicator
            bool is_active = !ch.settings().muted;
            COLORREF led_col = is_active ? RGB(50, 230, 100) : RGB(65, 65, 72);
            HBRUSH br_led = CreateSolidBrush(led_col);
            HPEN pen_led = CreatePen(PS_SOLID, 1, is_active ? RGB(100, 255, 150) : t.border_subtle);
            HGDIOBJ old_br = SelectObject(mem_dc_, br_led);
            HGDIOBJ old_pen = SelectObject(mem_dc_, pen_led);
            Ellipse(mem_dc_, px + 10, row_y + 13, px + 21, row_y + 24);
            SelectObject(mem_dc_, old_pen);
            SelectObject(mem_dc_, old_br);
            DeleteObject(pen_led);
            DeleteObject(br_led);

            // Pan Knob
            RECT pan_rc{px + 26, row_y + 6, px + 50, row_y + 30};
            float pan_norm = (ch.settings().pan + 1.0f) * 0.5f;
            GuiRenderer::draw_knob(mem_dc_, pan_rc, pan_norm, "", t.accent);

            // Vol Knob
            RECT vol_rc{px + 54, row_y + 6, px + 78, row_y + 30};
            float vol_norm = std::clamp(ch.settings().volume / domain::kMaxChannelVolume, 0.0f, 1.0f);
            GuiRenderer::draw_knob(mem_dc_, vol_rc, vol_norm, "", t.accent_bright);

            // Target Mixer Track LCD Box with spin indicator
            RECT trk_rc{px + 82, row_y + 6, px + 110, row_y + 30};
            GuiRenderer::draw_rounded_box(mem_dc_, trk_rc, RGB(40, 45, 52), t.border_subtle, 2);
            std::string trk_str = (ch.settings().mixer_track == 0) ? "--" : std::to_string(ch.settings().mixer_track);
            RECT trk_txt_rc{px + 82, row_y + 6, px + 104, row_y + 30};
            GuiRenderer::draw_text(mem_dc_, trk_str, trk_txt_rc, t.accent_bright, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            RECT caret_rc{px + 102, row_y + 6, px + 109, row_y + 30};
            GuiRenderer::draw_text(mem_dc_, "v", caret_rc, t.text_muted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            // Instrument Name Button
            RECT name_rc{px + 114, row_y + 5, px + 208, row_y + 31};
            GuiRenderer::draw_button(mem_dc_, name_rc, ch.settings().name, false, t.bg_surface_2, t.bg_surface_2);

            // Channel Selection Indicator Strip (matching Screenshot 1 row 2)
            bool is_selected_ch = (piano_roll_channel_ == ch.id());
            RECT sel_bar_rc{px + 211, row_y + 6, px + 216, row_y + 30};
            GuiRenderer::draw_rounded_box(mem_dc_, sel_bar_rc, is_selected_ch ? RGB(30, 60, 35) : RGB(46, 50, 56),
                                          is_selected_ch ? RGB(100, 240, 90) : t.border_subtle, 1);

            // Piano Roll Button
            RECT roll_btn_rc{px + 218, row_y + 6, px + 236, row_y + 30};
            GuiRenderer::draw_icon_button(mem_dc_, roll_btn_rc, SvgIconType::PianoRoll, is_selected_ch, t.accent, t.bg_control);

            // Beat Pattern Step Sequencer / Mini Piano Roll
            int grid_x = px + 240;
            int grid_w = std::max(60, (px + pw - 12) - grid_x);

            auto* note_set = pat ? pat->get_channel_notes(ch.id()) : nullptr;
            bool is_melody = note_set && is_channel_piano_roll(*note_set, ppq);

            int num_bars = get_channel_rack_num_bars(static_cast<float>(grid_w));
            int num_steps = num_bars * 16;
            channel_rack_steps_ = num_steps;
            float pad_w = get_channel_rack_pad_width(static_cast<float>(grid_w), num_steps);
            int total_pads_w = static_cast<int>(float(num_steps) * pad_w);

            if (is_melody) {
                // FL Studio Dark Blue-Slate lane with pastel green note bars
                int lane_w = std::min(grid_w, total_pads_w);
                RECT lane_rc{grid_x, row_y + 5, grid_x + lane_w, row_y + 31};
                GuiRenderer::draw_rounded_box(mem_dc_, lane_rc, RGB(30, 40, 48), t.border_subtle, 2);

                // Bar and beat grid dividers
                HPEN pen_bar = CreatePen(PS_SOLID, 1, RGB(55, 65, 80));
                HPEN pen_beat = CreatePen(PS_SOLID, 1, RGB(42, 48, 58));
                for (int s = 1; s < num_steps; ++s) {
                    int div_x = grid_x + static_cast<int>(float(s) * pad_w);
                    if (div_x >= lane_rc.right) break;
                    if (s % 16 == 0) {
                        HGDIOBJ old = SelectObject(mem_dc_, pen_bar);
                        MoveToEx(mem_dc_, div_x, lane_rc.top + 1, NULL);
                        LineTo(mem_dc_, div_x, lane_rc.bottom - 1);
                        SelectObject(mem_dc_, old);
                    } else if (s % 4 == 0) {
                        HGDIOBJ old = SelectObject(mem_dc_, pen_beat);
                        MoveToEx(mem_dc_, div_x, lane_rc.top + 3, NULL);
                        LineTo(mem_dc_, div_x, lane_rc.bottom - 3);
                        SelectObject(mem_dc_, old);
                    }
                }
                DeleteObject(pen_bar);
                DeleteObject(pen_beat);

                RECT tag_rc{grid_x + 6, lane_rc.top + 1, grid_x + 75, lane_rc.bottom - 1};
                GuiRenderer::draw_text(mem_dc_, "Piano roll", tag_rc, t.text_muted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                uint8_t min_p = 127, max_p = 0;
                for (const auto& n : note_set->notes()) {
                    min_p = std::min(min_p, n.pitch);
                    max_p = std::max(max_p, n.pitch);
                }
                int p_range = std::max(1, (max_p > min_p) ? (max_p - min_p + 1) : 4);

                for (const auto& n : note_set->notes()) {
                    int nx = static_cast<int>(domain::ChannelRackLayout::tick_to_x(static_cast<float>(grid_x), n.start, ppq, pad_w));
                    int nw = std::max(4, static_cast<int>(domain::ChannelRackLayout::ticks_to_width(n.length, ppq, pad_w)));
                    if (nx >= lane_rc.right) continue;
                    if (nx + nw > lane_rc.right) nw = lane_rc.right - nx;

                    float norm_p = (max_p > min_p) ? (float(n.pitch - min_p) / float(p_range)) : 0.5f;
                    int ny = lane_rc.top + static_cast<int>((1.0f - norm_p) * float(lane_rc.bottom - lane_rc.top - 6)) + 1;
                    RECT n_rc{nx, ny, nx + nw, ny + 3};
                    GuiRenderer::draw_rounded_box(mem_dc_, n_rc, RGB(120, 220, 140), RGB(90, 180, 110), 1);
                }

                if (cur_step >= 0 && cur_step < num_steps) {
                    int cur_sx = grid_x + static_cast<int>(float(cur_step) * pad_w);
                    if (cur_sx < lane_rc.right) {
                        int cur_ex = std::min(static_cast<int>(lane_rc.right), grid_x + static_cast<int>(float(cur_step + 1) * pad_w));
                        RECT bar_rc{cur_sx, lane_rc.top, cur_ex, lane_rc.top + 3};
                        GuiRenderer::draw_rounded_box(mem_dc_, bar_rc, t.accent_bright, t.accent_bright, 1);
                    }
                }
            } else {
                for (int s = 0; s < num_steps; ++s) {
                    int sx = grid_x + static_cast<int>(float(s) * pad_w);
                    if (sx + 2 >= grid_x + grid_w) break;
                    int ex = std::min(grid_x + grid_w, grid_x + static_cast<int>(float(s + 1) * pad_w));
                    RECT pad_rc{sx + 1, row_y + 5, ex - 1, row_y + 31};
                    bool step_on = note_set && note_set->has_note_at_step(s, ppq, 60);
                    bool step_cur = (s == cur_step);

                    // FL Studio 4-beat alternating palette:
                    // Beats 1 & 3: Charcoal / Silver-white
                    // Beats 2 & 4: Warm Reddish-Brown / Salmon-Coral
                    int beat_grp = (s / 4) % 2;
                    COLORREF inact_col = (beat_grp == 0) ? RGB(60, 65, 72) : RGB(92, 58, 58);
                    COLORREF act_col = (beat_grp == 0) ? RGB(225, 235, 245) : RGB(245, 155, 155);
                    COLORREF inact_border = (beat_grp == 0) ? RGB(45, 48, 54) : RGB(68, 42, 42);
                    COLORREF act_border = (beat_grp == 0) ? RGB(255, 255, 255) : RGB(255, 190, 190);

                    COLORREF pad_col = step_on ? act_col : inact_col;
                    COLORREF pad_border = step_cur ? t.accent_bright : (step_on ? act_border : inact_border);

                    GuiRenderer::draw_rounded_box(mem_dc_, pad_rc, pad_col, pad_border, 3);

                    if (!step_on) {
                        int cx = (pad_rc.left + pad_rc.right) / 2;
                        int cy = (pad_rc.top + pad_rc.bottom) / 2;
                        RECT pip_rc{cx - 1, cy - 3, cx + 1, cy + 3};
                        GuiRenderer::draw_rounded_box(mem_dc_, pip_rc, RGB(30, 32, 36), RGB(30, 32, 36), 1);
                    }
                    if (step_cur) {
                        RECT bar_rc{pad_rc.left, pad_rc.top, pad_rc.right, pad_rc.top + 3};
                        GuiRenderer::draw_rounded_box(mem_dc_, bar_rc, t.accent_bright, t.accent_bright, 1);
                    }
                }
            }
        }

        SelectClipRgn(mem_dc_, NULL);
        DeleteObject(clip_rgn);
    }

    void render_playlist(const DawWindow& win) {
        const auto& t = get_theme();
        int px = static_cast<int>(win.x);
        int py = static_cast<int>(win.y + DawWindow::kTitleBarHeight);
        int pw = static_cast<int>(win.w);
        int ph = static_cast<int>(win.h - DawWindow::kTitleBarHeight);

        RECT rack_rc{px, py, px + pw, py + ph};
        GuiRenderer::draw_rounded_box(mem_dc_, rack_rc, t.bg_surface, t.border_subtle, 0);

        auto& proj = engine_.session().project();
        auto ppq = proj.time_map().ppq();
        auto bar_ticks = 4 * ppq;

        // 1. Header Toolbar
        SelectObject(mem_dc_, font_small_);
        RECT prev_b_rc{px + 8, py + 4, px + 28, py + 30};
        GuiRenderer::draw_button(mem_dc_, prev_b_rc, "◄", false, t.bg_control, t.bg_control);

        auto* brush_pat = get_active_pattern();
        std::string b_name = brush_pat ? ("Brush: " + brush_pat->name()) : "Brush: Pattern 1";
        RECT brush_rc{px + 32, py + 4, px + 160, py + 30};
        GuiRenderer::draw_rounded_box(mem_dc_, brush_rc, t.bg_control, t.border_subtle, 3);
        GuiRenderer::draw_text(mem_dc_, b_name, brush_rc, t.accent_bright, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT next_b_rc{px + 164, py + 4, px + 184, py + 30};
        GuiRenderer::draw_button(mem_dc_, next_b_rc, "►", false, t.bg_control, t.bg_control);

        RECT add_trk_rc{px + 190, py + 4, px + 270, py + 30};
        GuiRenderer::draw_button(mem_dc_, add_trk_rc, "+ Add Track", false, t.accent, t.bg_control);

        RECT del_trk_rc{px + 274, py + 4, px + 354, py + 30};
        GuiRenderer::draw_button(mem_dc_, del_trk_rc, "− Del Track", false, t.danger, t.bg_control);

        int ruler_x = px + 110;
        int ruler_w = std::max(60, (px + pw - 12) - ruler_x);
        int ruler_y = py + 36;
        int ruler_h = 22;

        int bars_per_view = (ruler_w > 900) ? 16 : ((ruler_w > 550) ? 12 : 8);
        int max_bars = get_max_sequencer_bars();
        int start_bar_num = sequencer_scroll_bar_ + 1;
        int end_bar_num = sequencer_scroll_bar_ + bars_per_view;
        std::string bar_lbl = "↔ Bars " + std::to_string(start_bar_num) + "-" + std::to_string(end_bar_num) + " / " + std::to_string(max_bars);
        RECT bar_num_rc{std::max(px + 362, px + pw - 200), py + 4, px + pw - 12, py + 30};
        GuiRenderer::draw_rounded_box(mem_dc_, bar_num_rc, t.bg_control, t.border_subtle, 3);
        GuiRenderer::draw_text(mem_dc_, bar_lbl, bar_num_rc, t.text_secondary, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // 2. Timeline Ruler
        RECT ruler_rc{ruler_x, ruler_y, ruler_x + ruler_w, ruler_y + ruler_h};
        GuiRenderer::draw_rounded_box(mem_dc_, ruler_rc, t.bg_surface_2, t.border_subtle, 3);

        float bar_w = float(ruler_w) / float(bars_per_view);
        for (int b = 0; b < bars_per_view; ++b) {
            int bx = ruler_x + static_cast<int>(float(b) * bar_w);
            RECT num_rc{bx + 4, ruler_y + 2, bx + 36, ruler_y + ruler_h - 2};
            int cur_bar_idx = sequencer_scroll_bar_ + b + 1;
            GuiRenderer::draw_text(mem_dc_, std::to_string(cur_bar_idx), num_rc, t.text_muted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        // 3. Arrangement Tracks & Lanes
        int start_y = py + 62;
        int row_h = 48;
        int step_h = 42;
        int avail_h = (py + ph - 24) - start_y;
        int vis_tracks = std::max(1, avail_h / row_h);

        if (proj.tracks().empty()) {
            proj.add_track("Track 1");
        }

        int max_track_scroll = std::max(0, static_cast<int>(proj.tracks().size()) - vis_tracks);
        sequencer_scroll_track_ = std::clamp(sequencer_scroll_track_, 0, max_track_scroll);

        domain::Tick view_start_tick = static_cast<domain::Tick>(sequencer_scroll_bar_) * bar_ticks;
        domain::Tick view_duration = static_cast<domain::Tick>(bars_per_view) * bar_ticks;
        domain::Tick view_end_tick = view_start_tick + view_duration;

        int max_track_bottom = start_y;

        HRGN trk_clip = CreateRectRgn(px + 4, start_y, px + pw - 4, py + ph - 24);
        SelectClipRgn(mem_dc_, trk_clip);

        size_t start_t = static_cast<size_t>(sequencer_scroll_track_);
        size_t end_t = std::min(proj.tracks().size(), start_t + static_cast<size_t>(vis_tracks) + 1);

        for (size_t t_idx = start_t; t_idx < end_t; ++t_idx) {
            auto& track = proj.tracks()[t_idx];
            int t_y = start_y + static_cast<int>((t_idx - start_t) * row_h);
            max_track_bottom = t_y + step_h;

            // Track Header: Mute [M], Solo [S], Track Name, Green Active LED (matching Screenshot 2)
            RECT mute_rc{px + 8, t_y + 8, px + 26, t_y + 34};
            GuiRenderer::draw_button(mem_dc_, mute_rc, "M", track.is_muted(), t.danger, t.bg_control);

            RECT solo_rc{px + 29, t_y + 8, px + 47, t_y + 34};
            GuiRenderer::draw_button(mem_dc_, solo_rc, "S", track.solo(), t.accent, t.bg_control);

            RECT name_rc{px + 50, t_y + 8, px + 97, t_y + 34};
            GuiRenderer::draw_rounded_box(mem_dc_, name_rc, t.bg_control, t.border_subtle, 3);
            GuiRenderer::draw_text(mem_dc_, track.name(), name_rc, t.text_primary, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            // Active LED indicator circle on track header right edge (Screenshot 2)
            bool trk_active = !track.is_muted();
            HBRUSH br_t_led = CreateSolidBrush(trk_active ? RGB(50, 230, 100) : RGB(65, 65, 72));
            HPEN pen_t_led = CreatePen(PS_SOLID, 1, trk_active ? RGB(100, 255, 150) : t.border_subtle);
            HGDIOBJ old_br = SelectObject(mem_dc_, br_t_led);
            HGDIOBJ old_pen = SelectObject(mem_dc_, pen_t_led);
            Ellipse(mem_dc_, px + 101, t_y + 18, px + 108, t_y + 25);
            SelectObject(mem_dc_, old_pen);
            SelectObject(mem_dc_, old_br);
            DeleteObject(pen_t_led);
            DeleteObject(br_t_led);

            // Track Arranger Lane
            RECT lane_rc{ruler_x, t_y, ruler_x + ruler_w, t_y + step_h};
            COLORREF lane_bg = (t_idx % 2 == 0) ? t.bg_surface : t.bg_app;
            GuiRenderer::draw_rounded_box(mem_dc_, lane_rc, lane_bg, t.border_subtle, 2);

            // Render Clips
            for (const auto& clip : track.clips()) {
                if (clip.end() <= view_start_tick || clip.start >= view_end_tick) continue;

                domain::Tick draw_start = std::max(view_start_tick, clip.start);
                domain::Tick draw_end = std::min(view_end_tick, clip.end());
                double norm_start = double(draw_start - view_start_tick) / double(view_duration);
                double norm_len = double(draw_end - draw_start) / double(view_duration);

                int cx = ruler_x + static_cast<int>(norm_start * float(ruler_w));
                int cw = std::max(16, static_cast<int>(norm_len * float(ruler_w)));

                auto* clip_pat = proj.get_pattern(clip.pattern_id);
                RECT clip_rc{cx, t_y + 2, cx + cw, t_y + step_h - 2};
                GuiRenderer::draw_rounded_box(mem_dc_, clip_rc, RGB(25, 30, 42), RGB(60, 75, 100), 3);

                int hdr_h = 13;
                RECT header_rc{cx, t_y + 2, cx + cw, t_y + 2 + hdr_h};
                GuiRenderer::draw_rounded_box(mem_dc_, header_rc, RGB(35, 45, 62), RGB(35, 45, 62), 2);

                std::string clip_title = clip_pat ? ("≡ " + clip_pat->name()) : ("≡ Pat " + std::to_string(clip.pattern_id));
                GuiRenderer::draw_text(mem_dc_, clip_title, header_rc, t.text_primary, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                if (clip_pat) {
                    int body_top = t_y + 2 + hdr_h;
                    int body_h = step_h - 4 - hdr_h;

                    bool is_audio_clip = false;
                    plugins::AudioClipDevice* clipper_dev = nullptr;
                    if (!clip_pat->channel_notes().empty()) {
                        bool all_audio_clips = true;
                        for (const auto& [cid, nset] : clip_pat->channel_notes()) {
                            if (nset.notes().empty()) continue;
                            auto* ch = proj.get_channel(cid);
                            if (!ch || ch->device_uid() != "core.generator.audioclip") {
                                all_audio_clips = false;
                                break;
                            }
                            if (!clipper_dev) {
                                auto dev = engine_.get_or_create_channel_device(cid);
                                clipper_dev = dynamic_cast<plugins::AudioClipDevice*>(dev.get());
                            }
                        }
                        if (all_audio_clips && clipper_dev && !clipper_dev->sample_l().empty()) {
                            is_audio_clip = true;
                        }
                    }

                    if (is_audio_clip && clipper_dev) {
                        const auto& smp_l = clipper_dev->sample_l();
                        const size_t num_smp = smp_l.size();
                        int mid_y = body_top + body_h / 2;
                        float amp_h = float(body_h / 2) - 1.0f;

                        // Center zero-crossing baseline
                        HPEN base_pen = CreatePen(PS_SOLID, 1, RGB(35, 65, 80));
                        HGDIOBJ old_base = SelectObject(mem_dc_, base_pen);
                        MoveToEx(mem_dc_, cx + 2, mid_y, NULL);
                        LineTo(mem_dc_, cx + cw - 2, mid_y);
                        SelectObject(mem_dc_, old_base);
                        DeleteObject(base_pen);

                        HPEN wave_pen = CreatePen(PS_SOLID, 1, RGB(50, 220, 205));
                        HGDIOBJ old_pen = SelectObject(mem_dc_, wave_pen);

                        if (num_smp > 0) {
                            int x_start = cx + 2;
                            int x_end = cx + cw - 2;

                            double sr = clipper_dev->file_sample_rate() > 0 ? static_cast<double>(clipper_dev->file_sample_rate()) : 44100.0;
                            double bpm = proj.time_map().get_bpm_at(clip.start);
                            double ppq = static_cast<double>(proj.time_map().ppq());
                            double sample_dur_sec = static_cast<double>(num_smp) / sr;
                            double sample_total_ticks = sample_dur_sec * (bpm / 60.0) * ppq;
                            if (sample_total_ticks < 1.0) sample_total_ticks = 1.0;

                            double draw_tick_span = double(draw_end - draw_start);
                            bool looping = clipper_dev->use_loop_points() || clipper_dev->ping_pong_loop();

                            for (int px_col = x_start; px_col < x_end; ++px_col) {
                                double rel_draw = (cw > 0) ? (double(px_col - cx) / double(cw)) : 0.0;
                                domain::Tick tick_at_px = draw_start + static_cast<domain::Tick>(rel_draw * draw_tick_span);
                                domain::Tick tick_in_clip = tick_at_px - clip.start;
                                if (tick_in_clip < 0) continue;

                                double sample_prog = double(tick_in_clip) / sample_total_ticks;
                                if (looping) {
                                    sample_prog = std::fmod(sample_prog, 1.0);
                                    if (sample_prog < 0.0) sample_prog += 1.0;
                                } else if (sample_prog >= 1.0) {
                                    continue;
                                }

                                size_t idx_s = static_cast<size_t>(sample_prog * double(num_smp));
                                double ticks_per_col = (cw > 0) ? (draw_tick_span / double(cw)) : 1.0;
                                double frames_per_tick = sr / ((bpm / 60.0) * ppq);
                                size_t span_frames = std::max(size_t(1), static_cast<size_t>(ticks_per_col * frames_per_tick));
                                size_t idx_e = std::min(num_smp, idx_s + span_frames);

                                float min_v = smp_l[idx_s];
                                float max_v = smp_l[idx_s];
                                size_t step = std::max(size_t(1), (idx_e - idx_s) / 16);
                                for (size_t si = idx_s; si < idx_e; si += step) {
                                    float val = smp_l[si];
                                    if (val < min_v) min_v = val;
                                    if (val > max_v) max_v = val;
                                }

                                int y_top = mid_y - static_cast<int>(std::round(std::clamp(max_v, -1.0f, 1.0f) * amp_h));
                                int y_bot = mid_y - static_cast<int>(std::round(std::clamp(min_v, -1.0f, 1.0f) * amp_h));
                                if (y_top > y_bot) std::swap(y_top, y_bot);
                                if (y_top == y_bot) { y_top = mid_y - 1; y_bot = mid_y + 1; }
                                MoveToEx(mem_dc_, px_col, y_top, NULL);
                                LineTo(mem_dc_, px_col, y_bot);
                            }
                        }
                        SelectObject(mem_dc_, old_pen);
                        DeleteObject(wave_pen);
                    } else {
                        // Standard note blocks
                        for (const auto& [cid, nset] : clip_pat->channel_notes()) {
                            for (const auto& n : nset.notes()) {
                                if (n.start >= clip.length) continue;
                                domain::Tick abs_n_start = clip.start + n.start;
                                domain::Tick abs_n_end = abs_n_start + n.length;

                                if (abs_n_end > draw_start && abs_n_start < draw_end) {
                                    double n_rel_start = double(abs_n_start - draw_start) / double(draw_end - draw_start);
                                    double n_rel_len = double(n.length) / double(draw_end - draw_start);
                                    int raw_nx = cx + static_cast<int>(n_rel_start * float(cw));
                                    int raw_nw = std::max(3, static_cast<int>(n_rel_len * float(cw)));
                                    int nx = std::clamp(raw_nx, cx + 1, cx + cw - 2);
                                    int max_r = cx + cw - 1;
                                    int nw = std::max(2, std::min(raw_nw, max_r - nx));
                                    float norm_p = float(n.pitch % 24) / 24.0f;
                                    int ny = body_top + static_cast<int>((1.0f - norm_p) * float(body_h - 5)) + 1;
                                    RECT n_rc{nx, ny, nx + nw, ny + 2};
                                    GuiRenderer::draw_rounded_box(mem_dc_, n_rc, t.note_silver, t.border_subtle, 1);
                                }
                            }
                        }
                    }
                }
            }
        }

        SelectClipRgn(mem_dc_, NULL);
        DeleteObject(trk_clip);

        // 4. Playhead (SPM)
        auto cur_tick = engine_.transport().is_playing() ? engine_.transport().current_tick() : song_position_marker_;
        if (cur_tick >= view_start_tick && cur_tick <= view_end_tick) {
            double norm_pos = double(cur_tick - view_start_tick) / double(view_duration);
            int head_x = ruler_x + static_cast<int>(norm_pos * float(ruler_w));
            GuiRenderer::draw_playhead(mem_dc_, head_x, ruler_y, max_track_bottom, t.accent, "SPM");
        }

        // 5. Horizontal Scrollbar
        int scroll_y = py + ph - 20;
        int scroll_h = 14;
        RECT scroll_track_rc{ruler_x, scroll_y, ruler_x + ruler_w, scroll_y + scroll_h};
        GuiRenderer::draw_rounded_box(mem_dc_, scroll_track_rc, t.bg_control, t.border_subtle, 3);

        float max_scroll = std::max(1.0f, float(max_bars - bars_per_view));
        float thumb_w = std::max(35.0f, (float(bars_per_view) / float(max_bars)) * float(ruler_w));
        float scroll_ratio = std::clamp(float(sequencer_scroll_bar_) / max_scroll, 0.0f, 1.0f);
        float thumb_x = float(ruler_x) + scroll_ratio * (float(ruler_w) - thumb_w);
        RECT thumb_rc{static_cast<int>(thumb_x), scroll_y + 1, static_cast<int>(thumb_x + thumb_w), scroll_y + scroll_h - 1};
        COLORREF thumb_col = dragging_seq_scrollbar_ ? t.accent_bright : t.border_strong;
        GuiRenderer::draw_rounded_box(mem_dc_, thumb_rc, thumb_col, t.border_subtle, 3);
    }

    // --- Interactive Piano Roll View (GDI Fallback) ---
    void render_piano_roll(const DawWindow& win) {
        const auto& t = get_theme();
        PianoRollLayout lay;
        lay.init(win, PianoRollNumPitches, piano_roll_steps_);

        int px = static_cast<int>(win.x);
        int pw = static_cast<int>(win.w);
        RECT roll_rc{px, static_cast<int>(lay.rack_top), px + pw, static_cast<int>(lay.rack_bottom)};
        GuiRenderer::draw_rounded_box(mem_dc_, roll_rc, t.bg_surface, t.border_subtle, 0);

        auto& proj = engine_.session().project();
        auto* pat = get_active_pattern();
        auto ppq = proj.time_map().ppq();
        auto step_ticks = ppq / 4;

        auto* cur_ch = proj.get_channel(piano_roll_channel_);
        std::string ch_name = cur_ch ? cur_ch->settings().name : "Instrument";

        // 1. Piano Roll Toolbar: Section Title, Back to Playlist, Step Toggle, Note Length, Clear, and Shortcuts
        int ty = static_cast<int>(lay.toolbar_y);
        int th = static_cast<int>(lay.toolbar_h);

        SelectObject(mem_dc_, font_small_);
        int max_base_pitch = 128 - PianoRollNumPitches;
        std::string range_str = ch_name + " | " + get_midi_note_name(static_cast<uint8_t>(piano_roll_base_pitch_)) +
                                " — " + get_midi_note_name(static_cast<uint8_t>(piano_roll_base_pitch_ + PianoRollNumPitches - 1));
        RECT range_rc{px + 12, ty, px + 192, ty + th};
        GuiRenderer::draw_rounded_box(mem_dc_, range_rc, t.bg_control, t.border_subtle, 3);
        GuiRenderer::draw_text(mem_dc_, range_str, range_rc, t.accent_bright, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Steps Toggle (16 or 32 visible)
        std::string step_str = (piano_roll_steps_ == 16) ? "16 Steps" : "32 Steps";
        RECT step_rc{px + 198, ty, px + 278, ty + th};
        GuiRenderer::draw_button(mem_dc_, step_rc, step_str, (piano_roll_steps_ == 32), t.accent, t.bg_control);

        // Default Note Length
        std::string len_str = "📏 " + std::to_string(piano_roll_note_len_steps_) + (piano_roll_note_len_steps_ == 1 ? " Stp" : " Stps");
        RECT len_rc{px + 284, ty, px + 364, ty + th};
        GuiRenderer::draw_button(mem_dc_, len_rc, len_str, false, t.bg_control, t.bg_control);

        // Clear Notes button
        RECT clear_rc{px + 370, ty, px + 430, ty + th};
        GuiRenderer::draw_button(mem_dc_, clear_rc, "Clear", false, t.danger, t.bg_control);

        // Guide Hint (shown if enough width)
        if (pw > 500) {
            SelectObject(mem_dc_, font_small_);
            RECT hint_rc{px + 440, ty, px + pw - 8, ty + th};
            GuiRenderer::draw_text(mem_dc_, "💡 Left: Add/Drag • Right: Del • Wheel: Pitch",
                                  hint_rc, t.text_muted, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }

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

    void render_piano_roll() {
        render_piano_roll(win_pianoroll_);
    }

    void render_inspector_gdi(const DawWindow& win) {
        if (!win.visible) return;
        const auto& t = get_theme();

        int insp_x = static_cast<int>(win.x);
        int insp_y = static_cast<int>(win.y + DawWindow::kTitleBarHeight);
        int insp_r = static_cast<int>(win.x + win.w);
        int insp_bot = static_cast<int>(win.y + win.h);
        if (insp_bot <= insp_y + 80) return;

        // Outer Content Box
        RECT panel_rc{insp_x, insp_y, insp_r, insp_bot};
        GuiRenderer::draw_rounded_box(mem_dc_, panel_rc, t.bg_surface, t.border_subtle, 0);

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
        RECT sum_card_rc{insp_x + 8, insp_y + 8, insp_r - 8, insp_y + 126};
        GuiRenderer::draw_rounded_box(mem_dc_, sum_card_rc, t.bg_surface_2, t.border_subtle, 4);

        // Track Badge & Name
        RECT badge_rc{insp_x + 14, insp_y + 14, insp_x + 80, insp_y + 32};
        GuiRenderer::draw_rounded_box(mem_dc_, badge_rc, t.accent_deep, t.accent_bright, 3);
        GuiRenderer::draw_text(mem_dc_, tr_badge, badge_rc, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SelectObject(mem_dc_, font_bold_);
        RECT name_rc{insp_x + 86, insp_y + 14, insp_r - 14, insp_y + 32};
        GuiRenderer::draw_text(mem_dc_, tr_name, name_rc, t.text_primary);

        // Routing Info
        SelectObject(mem_dc_, font_small_);
        RECT route_rc{insp_x + 14, insp_y + 34, insp_r - 14, insp_y + 50};
        GuiRenderer::draw_text(mem_dc_, route_str, route_rc, t.text_muted);

        // Volume Slider
        float cur_vol = track ? track->volume() : 0.8f;
        float norm_vol = std::clamp(cur_vol / 1.25f, 0.0f, 1.0f);
        std::string vol_lbl = "Vol: " + std::to_string(static_cast<int>(std::round(cur_vol * 100.0f))) + "%";
        RECT vol_rc{insp_x + 14, insp_y + 54, insp_r - 14, insp_y + 72};
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
        RECT pan_rc{insp_x + 14, insp_y + 76, insp_r - 14, insp_y + 94};
        GuiRenderer::draw_slider_horizontal(mem_dc_, pan_rc, norm_pan, pan_lbl);

        // Mute & Solo Buttons
        bool is_mute = track ? track->muted() : false;
        bool is_solo = track ? track->solo() : false;
        int mid_w = (insp_r - 14) - (insp_x + 14);
        int btn_w = (mid_w - 6) / 2;

        RECT mute_rc{insp_x + 14, insp_y + 98, insp_x + 14 + btn_w, insp_y + 118};
        GuiRenderer::draw_button(mem_dc_, mute_rc, "MUTE", is_mute, t.danger, t.bg_control);

        RECT solo_rc{insp_x + 14 + btn_w + 6, insp_y + 98, insp_r - 14, insp_y + 118};
        GuiRenderer::draw_button(mem_dc_, solo_rc, "SOLO", is_solo, t.warning, t.bg_control);

        // FX Inserts Section Header
        size_t num_fx = track ? track->inserts().size() : 0;
        std::string fx_sec_title = "CHANNEL FX INSERTS (" + std::to_string(num_fx) + "/10)";
        SelectObject(mem_dc_, font_bold_);
        RECT fx_title_rc{insp_x + 12, insp_y + 132, insp_r - 12, insp_y + 152};
        GuiRenderer::draw_text(mem_dc_, fx_sec_title, fx_title_rc, t.text_secondary);

        // Render Insert Slots
        int slot_y_start = insp_y + 156;
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

    void render_inspector_gdi() {
        render_inspector_gdi(win_inspector_);
    }

    void render_audio_library(const DawWindow& win) {
        const auto& t = get_theme();
        int px = static_cast<int>(win.x);
        int py = static_cast<int>(win.y + DawWindow::kTitleBarHeight);
        int pw = static_cast<int>(win.w);
        int ph = static_cast<int>(win.h - DawWindow::kTitleBarHeight);

        RECT content_rc{px, py, px + pw, py + ph};
        GuiRenderer::draw_rounded_box(mem_dc_, content_rc, t.bg_surface, t.border_subtle, 0);

        // 1. Top Action Toolbar
        RECT browse_rc{px + 8, py + 6, px + 96, py + 30};
        GuiRenderer::draw_button(mem_dc_, browse_rc, "📂 Browse...", false, t.accent, t.bg_control);

        RECT rescan_rc{px + 102, py + 6, px + 174, py + 30};
        GuiRenderer::draw_button(mem_dc_, rescan_rc, "🔄 Rescan", false, t.bg_control, t.bg_control);

        RECT quick_rc{px + 180, py + 6, px + pw - 8, py + 30};
        GuiRenderer::draw_button(mem_dc_, quick_rc, "⚡ Quick Load", false, t.accent_bright, t.bg_control);

        // 2. Directory Path Bar
        int dir_y = py + 34;
        RECT dir_rc{px + 8, dir_y, px + pw - 8, dir_y + 24};
        GuiRenderer::draw_rounded_box(mem_dc_, dir_rc, t.bg_surface_2, t.border_subtle, 3);

        std::string cur_dir = sample_library_.current_directory();
        std::string dir_display = cur_dir.empty() ? "Folder: (No folder selected - Click Browse)" : ("📁 " + cur_dir);
        if (dir_display.length() > 36) {
            dir_display = dir_display.substr(0, 16) + "..." + dir_display.substr(dir_display.length() - 17);
        }
        RECT dir_txt_rc{px + 14, dir_y + 2, px + pw - 80, dir_y + 22};
        GuiRenderer::draw_text(mem_dc_, dir_display, dir_txt_rc, t.text_secondary, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        std::string count_txt = std::to_string(sample_library_.size()) + " files";
        RECT count_rc{px + pw - 78, dir_y + 2, px + pw - 12, dir_y + 22};
        GuiRenderer::draw_text(mem_dc_, count_txt, count_rc, t.accent_bright, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        // Divider
        HPEN div_pen = CreatePen(PS_SOLID, 1, t.border_subtle);
        HGDIOBJ old_div = SelectObject(mem_dc_, div_pen);
        MoveToEx(mem_dc_, px + 8, py + 62, NULL);
        LineTo(mem_dc_, px + pw - 8, py + 62);
        SelectObject(mem_dc_, old_div);
        DeleteObject(div_pen);

        // 3. Sample List Content Area
        int list_top = py + 66;
        int list_bot = py + ph - 8;
        int list_h = list_bot - list_top;
        int row_h = 36;
        int vis_rows = std::max(1, list_h / row_h);

        if (sample_library_.empty()) {
            RECT empty_rc1{px + 16, list_top + 40, px + pw - 16, list_top + 70};
            GuiRenderer::draw_text(mem_dc_, "📁 Sample Library Empty", empty_rc1, t.text_secondary, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            RECT empty_rc2{px + 16, list_top + 75, px + pw - 16, list_top + 140};
            std::string hint = "Click [Browse...] above to choose a folder of samples,\nor click [Quick Load] to load project sounds.\n\nDrag any audio file to Channel Rack or Playlist!";
            GuiRenderer::draw_text(mem_dc_, hint, empty_rc2, t.text_muted, DT_CENTER | DT_WORDBREAK);
            return;
        }

        int max_scroll = std::max(0, static_cast<int>(sample_library_.size()) - vis_rows);
        audio_lib_scroll_idx_ = std::clamp(audio_lib_scroll_idx_, 0, max_scroll);

        HRGN list_clip = CreateRectRgn(px + 4, list_top, px + pw - 4, list_bot);
        SelectClipRgn(mem_dc_, list_clip);

        for (int r = 0; r < vis_rows; ++r) {
            size_t s_idx = static_cast<size_t>(audio_lib_scroll_idx_ + r);
            if (s_idx >= sample_library_.size()) break;
            const auto& smp = sample_library_.samples()[s_idx];
            int ry = list_top + r * row_h;

            bool is_sel = (selected_sample_idx_ == static_cast<int>(s_idx));
            bool is_hov = (hover_sample_idx_ == static_cast<int>(s_idx));

            RECT row_rc{px + 8, ry, px + pw - 22, ry + row_h - 2};
            COLORREF bg_col = is_sel ? RGB(28, 55, 75)
                                     : (is_hov ? t.bg_surface_2
                                               : (r % 2 == 0 ? t.bg_surface : t.bg_app));
            COLORREF bdr_col = is_sel ? t.accent_bright : (is_hov ? t.border_default : t.border_subtle);
            GuiRenderer::draw_rounded_box(mem_dc_, row_rc, bg_col, bdr_col, 3);

            // Preview Play button [▶]
            RECT play_btn_rc{px + 12, ry + 5, px + 36, ry + row_h - 7};
            GuiRenderer::draw_button(mem_dc_, play_btn_rc, "▶", false, t.accent_bright, t.bg_control);

            // Format Badge
            std::string ext_badge = smp.extension;
            if (!ext_badge.empty() && ext_badge[0] == '.') ext_badge = ext_badge.substr(1);
            std::transform(ext_badge.begin(), ext_badge.end(), ext_badge.begin(), ::toupper);
            RECT badge_rc{px + 40, ry + 7, px + 76, ry + row_h - 9};
            GuiRenderer::draw_rounded_box(mem_dc_, badge_rc, t.bg_control, t.border_subtle, 2);
            GuiRenderer::draw_text(mem_dc_, ext_badge, badge_rc, t.accent, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            // Sample Name
            RECT name_rc{px + 82, ry + 2, px + pw - 28, ry + 19};
            GuiRenderer::draw_text(mem_dc_, smp.name, name_rc, t.text_primary, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            // Secondary Details
            std::string details = smp.formatted_duration() + " • " + smp.formatted_channels() + " • " + smp.formatted_size();
            RECT detail_rc{px + 82, ry + 18, px + pw - 28, ry + row_h - 4};
            GuiRenderer::draw_text(mem_dc_, details, detail_rc, t.text_secondary, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        SelectClipRgn(mem_dc_, NULL);
        DeleteObject(list_clip);

        // Vertical Scrollbar if needed
        if (max_scroll > 0) {
            int sb_x = px + pw - 18;
            int sb_w = 8;
            RECT track_rc{sb_x, list_top, sb_x + sb_w, list_bot};
            GuiRenderer::draw_rounded_box(mem_dc_, track_rc, t.bg_control, t.border_subtle, 2);

            float thumb_h = std::max(20.0f, (float(vis_rows) / float(sample_library_.size())) * float(list_h));
            float scroll_ratio = float(audio_lib_scroll_idx_) / float(max_scroll);
            int thumb_y = list_top + static_cast<int>(scroll_ratio * (float(list_h) - thumb_h));
            RECT thumb_rc{sb_x, thumb_y, sb_x + sb_w, static_cast<int>(thumb_y + thumb_h)};
            GuiRenderer::draw_rounded_box(mem_dc_, thumb_rc, t.accent_bright, t.accent_bright, 2);
        }
    }

    void render_mixer_panel(const DawWindow& win) {
        const auto& t = get_theme();
        int mx = static_cast<int>(win.x);
        int my = static_cast<int>(win.y + DawWindow::kTitleBarHeight);
        int mw = static_cast<int>(win.w);
        int mh = static_cast<int>(win.h - DawWindow::kTitleBarHeight);

        RECT mixer_rc{mx, my, mx + mw, my + mh};
        GuiRenderer::draw_rounded_box(mem_dc_, mixer_rc, t.bg_surface, t.border_subtle, 0);

        auto& proj = engine_.session().project();

        int strip_w = 96;
        int strip_gap = 10;
        int ty = my + 30;
        int strip_h = std::max(120, mh - 36);

        int master_x = mx + 12;
        int insert_start_x = master_x + strip_w + strip_gap;
        int avail_w = (mx + mw - 12) - insert_start_x;
        int vis_inserts = std::max(1, avail_w / (strip_w + strip_gap));
        size_t total_inserts = std::max(size_t(4), proj.channels().size());
        int max_mix_scroll = std::max(0, static_cast<int>(total_inserts) - vis_inserts);
        mixer_scroll_track_ = std::clamp(mixer_scroll_track_, 0, max_mix_scroll);

        SelectObject(mem_dc_, font_bold_);
        RECT title_rc{mx + 12, my + 5, mx + 120, my + 25};
        GuiRenderer::draw_text(mem_dc_, "MIXER", title_rc, t.text_secondary);

        if (max_mix_scroll > 0) {
            SelectObject(mem_dc_, font_small_);
            std::string mix_lbl = "Tracks " + std::to_string(1 + mixer_scroll_track_) + "-" +
                                  std::to_string(std::min(total_inserts, size_t(1 + mixer_scroll_track_ + vis_inserts - 1))) +
                                  " / " + std::to_string(total_inserts);
            RECT lbl_rc{mx + mw - 220, my + 5, mx + mw - 75, my + 25};
            GuiRenderer::draw_text(mem_dc_, mix_lbl, lbl_rc, t.text_secondary, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

            RECT btn_l{mx + mw - 68, my + 4, mx + mw - 40, my + 26};
            RECT btn_r{mx + mw - 36, my + 4, mx + mw - 8, my + 26};
            GuiRenderer::draw_button(mem_dc_, btn_l, "◀", false, t.bg_control, t.bg_control);
            GuiRenderer::draw_button(mem_dc_, btn_r, "▶", false, t.bg_control, t.bg_control);
        }

        auto render_gdi_strip = [&](int tid, int sx) {
            RECT strip_rc{sx, ty, sx + strip_w, ty + strip_h};
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
            int fader_bot = ty + strip_h - 52;
            float peak_l = (static_cast<size_t>(tid) < meter_peaks_l_.size()) ? meter_peaks_l_[tid] : 0.0f;
            float peak_r = (static_cast<size_t>(tid) < meter_peaks_r_.size()) ? meter_peaks_r_[tid] : 0.0f;
            RECT meter_rc{sx + 8, ty + 46, sx + 28, fader_bot};
            GuiRenderer::draw_meter_vertical_stereo(mem_dc_, meter_rc, peak_l, peak_r);

            // Vertical Volume Fader on Right
            RECT fader_rc{sx + 33, ty + 46, sx + 88, fader_bot};
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
            RECT pan_rc{sx + 6, fader_bot + 4, sx + strip_w - 6, ty + strip_h - 4};
            GuiRenderer::draw_pan_knob(mem_dc_, pan_rc, pan_val, "PAN");
        };

        render_gdi_strip(0, master_x);
        for (int s = 0; s < vis_inserts; ++s) {
            int tid = 1 + mixer_scroll_track_ + s;
            if (static_cast<size_t>(tid) > total_inserts) break;
            int sx = insert_start_x + s * (strip_w + strip_gap);
            render_gdi_strip(tid, sx);
        }
    }

    void render_mixer_panel() {
        render_mixer_panel(win_mixer_);
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
        if (active_editor_channel_ == 0) return;
        auto* dev = get_active_channel_synth();
        if (!dev) return;

        auto* clipper = dynamic_cast<plugins::AudioClipDevice*>(dev);
        if (clipper) {
            int mw = 760;
            int mh = 540;
            int mx = (client_w_ - mw) / 2;
            int my = (client_h_ - mh) / 2;
            editor_bounds_ = RECT{mx, my, mx + mw, my + mh};

            uint8_t m_track = 0;
            auto* ch = engine_.session().project().get_channel(active_editor_channel_);
            if (ch) m_track = ch->settings().mixer_track;

            AudioClipEditor::render_gdi(mem_dc_, clipper, editor_bounds_,
                                       clipper_active_tab_, clipper_env_subtab_, m_track);
            return;
        }

        auto* xsynth = dynamic_cast<plugins::XSynthDevice*>(dev);
        if (xsynth) {
            int mw = 700;
            int mh = 480;
            int mx = (client_w_ - mw) / 2;
            int my = (client_h_ - mh) / 2;
            editor_bounds_ = RECT{mx, my, mx + mw, my + mh};
            XAudioEditor::render_xsynth_gdi(mem_dc_, font_bold_, font_small_, xsynth, editor_bounds_);
            return;
        }

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

    void render_effect_editor_gdi() {
        if (active_editor_effect_track_ < 0 || active_editor_effect_slot_ < 0) return;
        auto* track = engine_.session().project().mixer_graph().get_track(static_cast<uint32_t>(active_editor_effect_track_));
        if (!track || static_cast<size_t>(active_editor_effect_slot_) >= track->inserts().size()) return;
        auto& ins = track->inserts()[active_editor_effect_slot_];
        if (!ins.device) return;

        int mw = std::clamp(client_w_ - 60, 840, 1100);
        int mh = std::clamp(client_h_ - 60, 640, 780);
        int mx = (client_w_ - mw) / 2;
        int my = (client_h_ - mh) / 2;
        effect_editor_bounds_ = RECT{mx, my, mx + mw, my + mh};

        XAudioEditor::render_effect_gdi(mem_dc_, font_bold_, font_small_,
                                       ins.device.get(), effect_editor_bounds_,
                                       ins.enabled, ins.wet_mix, active_editor_effect_track_,
                                       active_editor_effect_slot_, effect_editor_scroll_idx_);
    }

    void open_effect_editor(uint32_t tid, int slot_idx) {
        auto* track = engine_.session().project().mixer_graph().get_track(tid);
        if (!track || static_cast<size_t>(slot_idx) >= track->inserts().size()) return;
        if (!track->inserts()[slot_idx].device) return;
        active_editor_effect_track_ = static_cast<int>(tid);
        active_editor_effect_slot_ = slot_idx;
        active_editor_channel_ = 0;
        status_message_ = "Opened editor for " + track->inserts()[slot_idx].device->name();
        InvalidateRect(hwnd_, NULL, FALSE);
    }

    void handle_effect_editor_click(int x, int y) {
        if (active_editor_effect_track_ < 0 || active_editor_effect_slot_ < 0) return;
        auto* track = engine_.session().project().mixer_graph().get_track(static_cast<uint32_t>(active_editor_effect_track_));
        if (!track || static_cast<size_t>(active_editor_effect_slot_) >= track->inserts().size()) {
            active_editor_effect_track_ = -1;
            active_editor_effect_slot_ = -1;
            return;
        }
        auto& ins = track->inserts()[active_editor_effect_slot_];
        if (!ins.device) {
            active_editor_effect_track_ = -1;
            active_editor_effect_slot_ = -1;
            return;
        }

        bool should_close = XAudioEditor::handle_effect_click(
            ins.device.get(), effect_editor_bounds_, x, y, ins.enabled, ins.wet_mix,
            effect_editor_scroll_idx_, dragging_effect_param_idx_, drag_effect_start_x_,
            drag_effect_start_y_, drag_effect_orig_val_, status_message_);

        if (should_close) {
            active_editor_effect_track_ = -1;
            active_editor_effect_slot_ = -1;
            dragging_effect_param_idx_ = -1;
        }
    }

    void on_mouse_down(int x, int y) {
        // A. If plugin editor or effect editor modal is open, handle modal clicks
        if (active_editor_effect_track_ >= 0 && active_editor_effect_slot_ >= 0) {
            handle_effect_editor_click(x, y);
            return;
        }
        if (active_editor_channel_ != 0) {
            handle_editor_click(x, y);
            return;
        }

        // B. Top Transport Bar
        // Project Name badge click [ 200, 8, 310, 40 ]
        if (x >= 200 && x <= 310 && y >= 8 && y <= 40) {
            std::string cur_name = engine_.session().project().name();
            if (cur_name.empty()) cur_name = "Untitled Project";
            status_message_ = "Active Project: " + cur_name + " (" +
                              std::to_string(engine_.session().project().channels().size()) + " Channels, " +
                              std::to_string(static_cast<int>(engine_.session().project().time_map().get_bpm_at(0))) + " BPM)";
            return;
        }

        // Undo [↶] button
        if (x >= 316 && x <= 346 && y >= 8 && y <= 40) {
            do_undo();
            return;
        }

        // Redo [↷] button
        if (x >= 350 && x <= 380 && y >= 8 && y <= 40) {
            do_redo();
            return;
        }

        // 1. PLAY Button [▶]
        if (x >= 388 && x <= 424 && y >= 8 && y <= 40) {
            start_playback_from_spm();
            return;
        }

        // 2. PAUSE Button [❚❚]
        if (x >= 428 && x <= 464 && y >= 8 && y <= 40) {
            pause_playback();
            return;
        }

        // 3. STOP Button [■]
        if (x >= 468 && x <= 504 && y >= 8 && y <= 40) {
            stop_playback();
            return;
        }

        // 4. Playback Mode (PAT / SONG) Toggle Button
        if (x >= 508 && x <= 568 && y >= 8 && y <= 40) {
            toggle_playback_mode();
            return;
        }

        // Tempo - / +
        if (x >= 574 && x <= 598 && y >= 8 && y <= 40) {
            double bpm = engine_.session().project().time_map().get_bpm_at(0);
            engine_.session().project().time_map().set_tempo(std::max(20.0, bpm - 1.0));
            return;
        }
        if (x >= 686 && x <= 710 && y >= 8 && y <= 40) {
            double bpm = engine_.session().project().time_map().get_bpm_at(0);
            engine_.session().project().time_map().set_tempo(std::min(999.0, bpm + 1.0));
            return;
        }

        // 5. Window Toggle / Focus Buttons: Channel Rack, Playlist, Piano Roll, Mixer, Inspector, and Magnet
        if (x >= 854 && x <= 890 && y >= 8 && y <= 40) {
            toggle_or_focus_window(WindowId::ChannelRack);
            return;
        }
        if (x >= 894 && x <= 930 && y >= 8 && y <= 40) {
            toggle_or_focus_window(WindowId::Playlist);
            return;
        }
        if (x >= 934 && x <= 970 && y >= 8 && y <= 40) {
            toggle_or_focus_window(WindowId::PianoRoll);
            return;
        }
        if (x >= 974 && x <= 1010 && y >= 8 && y <= 40) {
            toggle_or_focus_window(WindowId::Mixer);
            return;
        }
        if (x >= 1014 && x <= 1050 && y >= 8 && y <= 40) {
            toggle_or_focus_window(WindowId::Inspector);
            return;
        }
        if (x >= 1054 && x <= 1090 && y >= 8 && y <= 40) {
            toggle_or_focus_window(WindowId::AudioLibrary);
            return;
        }
        if (x >= 1094 && x <= 1130 && y >= 8 && y <= 40) {
            magnet_enabled_ = !magnet_enabled_;
            status_message_ = magnet_enabled_ ? "Magnetic Snapping Enabled (Ctrl+M)" : "Magnetic Snapping Disabled (Ctrl+M)";
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

        // C. Windowed Desktop Panel Interaction (Reverse Z-Order: Top-to-Bottom)
        for (auto it = z_order_.rbegin(); it != z_order_.rend(); ++it) {
            auto* win = get_window(*it);
            if (!win || !win->visible) continue;
            if (!win->contains(static_cast<float>(x), static_cast<float>(y))) continue;

            // Click hit this window: bring to front!
            bring_to_front(*it);

            // 1. Close Button [✕]
            if (win->is_in_close_btn(static_cast<float>(x), static_cast<float>(y))) {
                win->visible = false;
                if (win->id == WindowId::Inspector) inspector_open_ = false;
                status_message_ = "Closed " + win->title;
                update_top_active_window();
                return;
            }

            // 2. Resize Border Edges & Corners
            auto edge = get_resize_edge(*win, static_cast<float>(x), static_cast<float>(y));
            if (edge != ResizeEdge::None) {
                is_dragging_window_ = true;
                window_drag_mode_ = WindowDragMode::Resize;
                resize_edge_ = edge;
                dragging_window_id_ = win->id;
                drag_win_start_mouse_x_ = x;
                drag_win_start_mouse_y_ = y;
                drag_win_orig_x_ = win->x;
                drag_win_orig_y_ = win->y;
                drag_win_orig_w_ = win->w;
                drag_win_orig_h_ = win->h;
                status_message_ = "Resizing " + win->title;
                return;
            }

            // 3. Title Bar Dragging (Move)
            if (win->is_in_title_bar(static_cast<float>(x), static_cast<float>(y))) {
                is_dragging_window_ = true;
                window_drag_mode_ = WindowDragMode::Move;
                resize_edge_ = ResizeEdge::None;
                dragging_window_id_ = win->id;
                drag_win_start_mouse_x_ = x;
                drag_win_start_mouse_y_ = y;
                drag_win_orig_x_ = win->x;
                drag_win_orig_y_ = win->y;
                drag_win_orig_w_ = win->w;
                drag_win_orig_h_ = win->h;
                status_message_ = "Moving " + win->title;
                return;
            }

            // 4. Content Area Click
            switch (win->id) {
                case WindowId::ChannelRack:
                    handle_channel_rack_click(*win, x, y);
                    return;
                case WindowId::Playlist:
                    handle_playlist_click(*win, x, y);
                    return;
                case WindowId::PianoRoll:
                    handle_piano_roll_click(*win, x, y);
                    return;
                case WindowId::Mixer:
                    handle_mixer_click(*win, x, y);
                    return;
                case WindowId::Inspector:
                    handle_inspector_click(*win, x, y);
                    return;
                case WindowId::AudioLibrary:
                    handle_audio_library_click(*win, x, y);
                    return;
            }
            return;
        }
    }

    void handle_inspector_click(const DawWindow& win, int x, int y) {
        int insp_x = static_cast<int>(win.x);
        int insp_y = static_cast<int>(win.y + DawWindow::kTitleBarHeight);
        int insp_r = static_cast<int>(win.x + win.w);
        int insp_bot = static_cast<int>(win.y + win.h);

        uint32_t tid = selected_mixer_track_;
        auto& proj = engine_.session().project();
        auto* track = proj.mixer_graph().get_track(tid);
        if (!track && tid > 0) {
            proj.mixer_graph().add_track(tid, "Track " + std::to_string(tid));
            track = proj.mixer_graph().get_track(tid);
        }
        if (!track) return;

        size_t num_fx = track->inserts().size();
        int slot_y_start = insp_y + 156;
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
                        AppendMenuA(hMenu, MF_STRING, 1001, "1. X-Eq (Parametric Equalizer)");
                        AppendMenuA(hMenu, MF_STRING, 1002, "2. X-Compressor");
                        AppendMenuA(hMenu, MF_STRING, 1003, "3. X-Multiband Dynamics");
                        AppendMenuA(hMenu, MF_STRING, 1004, "4. X-Reverb (Algorithmic)");
                        AppendMenuA(hMenu, MF_STRING, 1005, "5. X-Distortion (Waveshaper)");
                        AppendMenuA(hMenu, MF_STRING, 1006, "6. X-Limiter (Master Limiter)");
                        AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
                        AppendMenuA(hMenu, MF_STRING, 1007, "7. Stereo Delay");
                        AppendMenuA(hMenu, MF_STRING, 1008, "8. Algorithmic Reverb (Legacy)");
                        AppendMenuA(hMenu, MF_STRING, 1009, "9. Stereo Compressor (Legacy)");
                        AppendMenuA(hMenu, MF_STRING, 1010, "10. Master Limiter (Legacy)");

                        POINT pt{x, y};
                        ClientToScreen(hwnd_, &pt);
                        int cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, pt.x, pt.y, 0, hwnd_, NULL);
                        DestroyMenu(hMenu);

                        if (cmd == 1001) insert_effect_to_track(tid, "core.fx.x_eq");
                        else if (cmd == 1002) insert_effect_to_track(tid, "core.fx.x_compressor");
                        else if (cmd == 1003) insert_effect_to_track(tid, "core.fx.x_multiband");
                        else if (cmd == 1004) insert_effect_to_track(tid, "core.fx.x_reverb");
                        else if (cmd == 1005) insert_effect_to_track(tid, "core.fx.x_distortion");
                        else if (cmd == 1006) insert_effect_to_track(tid, "core.fx.x_limiter");
                        else if (cmd == 1007) insert_effect_to_track(tid, "core.fx.delay");
                        else if (cmd == 1008) insert_effect_to_track(tid, "core.fx.reverb");
                        else if (cmd == 1009) insert_effect_to_track(tid, "core.fx.compressor");
                        else if (cmd == 1010) insert_effect_to_track(tid, "core.fx.limiter");
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

        // 2. Volume slider: [insp_x + 14, insp_y + 54, insp_r - 14, insp_y + 72]
        if (x >= insp_x + 14 && x <= insp_r - 14 && y >= insp_y + 54 && y <= insp_y + 72) {
            dragging_inspector_vol_ = true;
            float norm = float(x - (insp_x + 14)) / float((insp_r - 14) - (insp_x + 14));
            norm = std::clamp(norm, 0.0f, 1.0f);
            track->set_volume(norm * 1.25f);
            int vol_pct = static_cast<int>(std::round(track->volume() * 100.0f));
            status_message_ = (tid == 0 ? "Master" : ("Track " + std::to_string(tid))) + " Volume: " + std::to_string(vol_pct) + "%";
            return;
        }

        // 3. Pan slider: [insp_x + 14, insp_y + 76, insp_r - 14, insp_y + 94]
        if (x >= insp_x + 14 && x <= insp_r - 14 && y >= insp_y + 76 && y <= insp_y + 94) {
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
        if (y >= insp_y + 98 && y <= insp_y + 118) {
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

            // Effect Name / row body click: show detailed status info & open editor
            if (x >= insp_x + 40 && x <= insp_r - (right_margin + 84) && y >= sy + 2 && y <= sy + slot_h - 2) {
                std::string name = track->inserts()[i].device ? track->inserts()[i].device->name() : "Effect";
                int pct = static_cast<int>(std::round(track->inserts()[i].wet_mix * 100.0f));
                status_message_ = "Insert " + std::to_string(i + 1) + ": " + name + " (" + (track->inserts()[i].enabled ? "Active" : "Bypassed") + ", Wet Mix: " + std::to_string(pct) + "%)";
                if (track->inserts()[i].device) {
                    open_effect_editor(tid, i);
                }
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

    void handle_inspector_click(int x, int y) {
        handle_inspector_click(win_inspector_, x, y);
    }

    void handle_inspector_right_click(const DawWindow& win, int x, int y) {
        int insp_x = static_cast<int>(win.x);
        int insp_y = static_cast<int>(win.y + DawWindow::kTitleBarHeight);
        int insp_r = static_cast<int>(win.x + win.w);
        int insp_bot = static_cast<int>(win.y + win.h);

        uint32_t tid = selected_mixer_track_;
        auto& proj = engine_.session().project();
        auto* track = proj.mixer_graph().get_track(tid);
        if (!track) return;

        // Right-click on Volume slider -> reset to default unity (1.0f = 0 dB)
        if (x >= insp_x + 14 && x <= insp_r - 14 && y >= insp_y + 54 && y <= insp_y + 72) {
            track->set_volume(1.0f);
            status_message_ = (tid == 0 ? "Master" : ("Track " + std::to_string(tid))) + " Volume reset to 0 dB (100%)";
            return;
        }

        // Right-click on Pan slider -> reset to Center (0.0f)
        if (x >= insp_x + 14 && x <= insp_r - 14 && y >= insp_y + 76 && y <= insp_y + 94) {
            track->set_pan(0.0f);
            status_message_ = (tid == 0 ? "Master" : ("Track " + std::to_string(tid))) + " Pan reset to Center";
            return;
        }

        // Right-click on FX Inserts:
        size_t num_fx = track->inserts().size();
        int slot_y_start = insp_y + 156;
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

    void handle_inspector_right_click(int x, int y) {
        handle_inspector_right_click(win_inspector_, x, y);
    }

    void handle_mixer_click(const DawWindow& win, int x, int y) {
        std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
        auto& proj = engine_.session().project();

        float mx = win.x;
        float my = win.y + DawWindow::kTitleBarHeight;
        float mw = win.w;
        float mh = win.h - DawWindow::kTitleBarHeight;

        float track_w = 96.0f;
        float gap = 10.0f;
        float ty = my + 30.0f;
        float strip_h = std::max(120.0f, mh - 36.0f);
        float master_x = mx + 12.0f;
        float insert_start_x = master_x + track_w + gap;
        float avail_w = (mx + mw - 12.0f) - insert_start_x;
        int vis_inserts = std::max(1, static_cast<int>(avail_w / (track_w + gap)));
        size_t total_inserts = std::max(size_t(4), proj.channels().size());
        int max_mix_scroll = std::max(0, static_cast<int>(total_inserts) - vis_inserts);

        // 1. Navigation buttons: [ ◀ ] and [ ▶ ]
        if (max_mix_scroll > 0) {
            if (x >= mx + mw - 68.0f && x <= mx + mw - 40.0f && y >= my + 4.0f && y <= my + 26.0f) {
                mixer_scroll_track_ = std::max(0, mixer_scroll_track_ - 1);
                status_message_ = "Scrolled mixer to Track " + std::to_string(1 + mixer_scroll_track_);
                return;
            }
            if (x >= mx + mw - 36.0f && x <= mx + mw - 8.0f && y >= my + 4.0f && y <= my + 26.0f) {
                mixer_scroll_track_ = std::min(max_mix_scroll, mixer_scroll_track_ + 1);
                status_message_ = "Scrolled mixer to Track " + std::to_string(1 + mixer_scroll_track_);
                return;
            }
        }

        // Helper lambda for track strip hit testing
        auto handle_strip_click = [&](int tid, float tx) -> bool {
            float fader_bot = ty + strip_h - 52.0f;
            if (x < tx || x > tx + track_w || y < ty || y > ty + strip_h) {
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
            if (x >= tx + 28.0f && x <= tx + 92.0f && y >= ty + 44.0f && y <= fader_bot + 2.0f) {
                dragging_mixer_track_ = tid;
                RECT fader_rc{static_cast<int>(tx + 33.0f), static_cast<int>(ty + 46.0f),
                              static_cast<int>(tx + 88.0f), static_cast<int>(fader_bot)};
                update_mixer_volume_from_mouse(tid, y, fader_rc);
                return true;
            }

            // D. Panning knob drag
            if (x >= tx + 6.0f && x <= tx + track_w - 6.0f && y >= fader_bot + 2.0f && y <= ty + strip_h - 2.0f) {
                dragging_mixer_pan_track_ = tid;
                pan_drag_start_y_ = y;
                pan_drag_start_val_ = mix_tr ? mix_tr->pan() : 0.0f;
                return true;
            }

            return true;
        };

        // Check Master Strip (tid = 0)
        if (handle_strip_click(0, master_x)) return;

        // Check Visible Insert Strips
        for (int s = 0; s < vis_inserts; ++s) {
            int tid = 1 + mixer_scroll_track_ + s;
            if (static_cast<size_t>(tid) > total_inserts) break;
            float tx = insert_start_x + s * (track_w + gap);
            if (handle_strip_click(tid, tx)) return;
        }
    }

    void handle_mixer_click(int x, int y) {
        handle_mixer_click(win_mixer_, x, y);
    }

    void handle_mixer_right_click(const DawWindow& win, int x, int y) {
        std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
        auto& proj = engine_.session().project();

        float mx = win.x;
        float my = win.y + DawWindow::kTitleBarHeight;
        float mw = win.w;
        float mh = win.h - DawWindow::kTitleBarHeight;

        float track_w = 96.0f;
        float gap = 10.0f;
        float ty = my + 30.0f;
        float strip_h = std::max(120.0f, mh - 36.0f);
        float master_x = mx + 12.0f;
        float insert_start_x = master_x + track_w + gap;
        float avail_w = (mx + mw - 12.0f) - insert_start_x;
        int vis_inserts = std::max(1, static_cast<int>(avail_w / (track_w + gap)));
        size_t total_inserts = std::max(size_t(4), proj.channels().size());

        auto handle_strip_right_click = [&](int tid, float tx) -> bool {
            float fader_bot = ty + strip_h - 52.0f;
            if (x < tx || x > tx + track_w || y < ty || y > ty + strip_h) {
                return false;
            }

            selected_mixer_track_ = static_cast<uint32_t>(tid);
            auto* mix_tr = proj.mixer_graph().get_track(tid);
            if (!mix_tr) return false;

            // Right-click on Panning knob -> reset to Center (0.0)
            if (x >= tx + 6.0f && x <= tx + track_w - 6.0f && y >= fader_bot + 2.0f && y <= ty + strip_h - 2.0f) {
                mix_tr->set_pan(0.0f);
                status_message_ = (tid == 0 ? "Master" : "Track " + std::to_string(tid)) + " Pan reset to Center";
                return true;
            }

            // Right-click on Fader -> reset to Unity Gain (1.0 = 0 dB)
            if (x >= tx + 28.0f && x <= tx + 92.0f && y >= ty + 44.0f && y <= fader_bot + 2.0f) {
                mix_tr->set_volume(1.0f);
                status_message_ = (tid == 0 ? "Master" : "Track " + std::to_string(tid)) + " Volume reset to 0 dB";
                return true;
            }

            return false;
        };

        if (handle_strip_right_click(0, master_x)) return;

        for (int s = 0; s < vis_inserts; ++s) {
            int tid = 1 + mixer_scroll_track_ + s;
            if (static_cast<size_t>(tid) > total_inserts) break;
            float tx = insert_start_x + s * (track_w + gap);
            if (handle_strip_right_click(tid, tx)) return;
        }
    }

    void handle_mixer_right_click(int x, int y) {
        handle_mixer_right_click(win_mixer_, x, y);
    }

    void handle_channel_rack_click(const DawWindow& win, int x, int y) {
        std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
        int px = static_cast<int>(win.x);
        int py = static_cast<int>(win.y + DawWindow::kTitleBarHeight);
        int pw = static_cast<int>(win.w);
        int ph = static_cast<int>(win.h - DawWindow::kTitleBarHeight);

        // 1. Header Toolbar (py + 4 to py + 30)
        if (y >= py + 4 && y <= py + 30) {
            // Pattern prev [ ◄ ]
            if (x >= px + 8 && x <= px + 28) {
                prev_pattern();
                return;
            }
            // Pattern next [ ► ]
            if (x >= px + 154 && x <= px + 174) {
                next_pattern();
                return;
            }
            // Add Pattern [ + ]
            if (x >= px + 178 && x <= px + 200) {
                add_new_pattern();
                return;
            }
            // Delete Pattern [ − ]
            if (x >= px + 204 && x <= px + 226) {
                delete_current_pattern();
                return;
            }
            // Steps Display Badge [ XX Steps (X Bars) ]
            if (x >= px + 232 && x <= px + 342) {
                status_message_ = "Channel Rack: " + std::to_string(channel_rack_steps_) + " Steps (" + std::to_string(channel_rack_steps_ / 16) + " Bars) - Drag window wider to expand steps/bars";
                return;
            }
            // [+ Add Instrument] button
            if (x >= px + 348 && x <= px + 460) {
                POINT pt;
                GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                AppendMenuA(hMenu, MF_STRING, 2001, "1. Clipper (AudioClip)");
                AppendMenuA(hMenu, MF_STRING, 2002, "2. 3xOsc Synthesizer");
                AppendMenuA(hMenu, MF_STRING, 2003, "3. DirectWave Sampler");
                AppendMenuA(hMenu, MF_STRING, 2004, "4. FPC Drum Machine");
                AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuA(hMenu, MF_STRING, 2010, "5. X-Synth Synthesizer");
                int cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, pt.x, pt.y, 0, hwnd_, nullptr);
                DestroyMenu(hMenu);
                if (cmd == 2001) add_channel_with_uid("core.generator.audioclip", "Clipper");
                else if (cmd == 2002) add_channel_with_uid("core.generator.3xosc", "3xOsc Synth");
                else if (cmd == 2003) add_channel_with_uid("core.generator.sampler", "DirectWave Sampler");
                else if (cmd == 2004) add_channel_with_uid("core.generator.drum_sampler", "FPC Drum Machine");
                else if (cmd == 2010) add_channel_with_uid("core.generator.x_synth", "X-Synth");
                return;
            }
            return;
        }

        // 2. Channel Rows (starts at py + 38, row_h = 38)
        float start_y = float(py + 38);
        float row_h = 38.0f;
        auto& proj = engine_.session().project();
        auto ppq = proj.time_map().ppq();
        auto* pat = get_active_pattern();

        int rack_avail_h = (py + ph - 8) - static_cast<int>(start_y);
        int visible_channels = std::max(1, rack_avail_h / static_cast<int>(row_h));
        int max_ch_scroll = std::max(0, static_cast<int>(proj.channels().size()) - visible_channels);
        channel_rack_scroll_ch_ = std::clamp(channel_rack_scroll_ch_, 0, max_ch_scroll);

        if (y >= static_cast<int>(start_y) && y <= py + ph - 4) {
            int rel_row = static_cast<int>((float(y) - start_y) / row_h);
            size_t ch_idx = static_cast<size_t>(channel_rack_scroll_ch_ + rel_row);
            if (ch_idx < proj.channels().size()) {
                auto& ch = proj.channels()[ch_idx];
                float row_y = start_y + static_cast<float>(rel_row) * row_h;

                // 1. Mute / Active LED indicator
                if (x >= px + 6 && x <= px + 24 && y >= row_y + 8 && y <= row_y + 28) {
                    ch.settings().muted = !ch.settings().muted;
                    selected_mixer_track_ = ch.settings().mixer_track;
                    status_message_ = ch.settings().name + (ch.settings().muted ? " Muted" : " Active / Unmuted");
                    return;
                }

                // 2. Pan Knob
                if (x >= px + 26 && x <= px + 50 && y >= row_y + 4 && y <= row_y + 30) {
                    dragging_channel_pan_idx_ = static_cast<int>(ch_idx);
                    drag_knob_start_mouse_y_ = y;
                    drag_knob_orig_val_ = ch.settings().pan;
                    selected_mixer_track_ = ch.settings().mixer_track;
                    status_message_ = ch.settings().name + " Pan (Drag up/down)";
                    return;
                }

                // 3. Vol Knob
                if (x >= px + 54 && x <= px + 78 && y >= row_y + 4 && y <= row_y + 30) {
                    dragging_channel_vol_idx_ = static_cast<int>(ch_idx);
                    drag_knob_start_mouse_y_ = y;
                    drag_knob_orig_val_ = ch.settings().volume;
                    selected_mixer_track_ = ch.settings().mixer_track;
                    int pct = static_cast<int>(std::round((ch.settings().volume / domain::kMaxChannelVolume) * 100.0f));
                    status_message_ = ch.settings().name + " Volume: " + std::to_string(pct) + "% (Drag up/down)";
                    return;
                }

                // 4. Target Mixer Track LCD Box
                if (x >= px + 82 && x <= px + 110 && y >= row_y + 5 && y <= row_y + 29) {
                    dragging_channel_target_track_idx_ = static_cast<int>(ch_idx);
                    drag_target_track_start_mouse_y_ = y;
                    drag_target_track_orig_val_ = static_cast<int>(ch.settings().mixer_track);
                    selected_mixer_track_ = ch.settings().mixer_track;
                    status_message_ = ch.settings().name + " Mixer Track LCD (Drag up/down or wheel)";
                    return;
                }

                // 5. Instrument Name Button -> Opens plugin editor
                if (x >= px + 114 && x <= px + 210 && y >= row_y + 5 && y <= row_y + 31) {
                    active_editor_channel_ = ch.id();
                    selected_mixer_track_ = ch.settings().mixer_track;
                    status_message_ = "Opened Instrument Editor: " + ch.settings().name;
                    return;
                }

                // 6. Piano Roll Button [ 🎹 ]
                if (x >= px + 214 && x <= px + 234 && y >= row_y + 6 && y <= row_y + 30) {
                    piano_roll_channel_ = ch.id();
                    selected_mixer_track_ = ch.settings().mixer_track;
                    toggle_or_focus_window(WindowId::PianoRoll);
                    status_message_ = "Opened Piano Roll for " + ch.settings().name;
                    return;
                }

                // 7. Beat Pattern Step Sequencer / Mini Piano Roll
                float grid_x = float(px + 240);
                float grid_w = std::max(60.0f, float((px + pw - 12) - grid_x));
                if (float(x) >= grid_x && float(x) <= grid_x + grid_w && y >= row_y + 4 && y <= row_y + 32) {
                    selected_mixer_track_ = ch.settings().mixer_track;
                    auto* note_set = pat ? pat->get_channel_notes(ch.id()) : nullptr;
                    bool is_melody = note_set && is_channel_piano_roll(*note_set, ppq);
                    if (is_melody) {
                        piano_roll_channel_ = ch.id();
                        toggle_or_focus_window(WindowId::PianoRoll);
                        status_message_ = "Opened Piano Roll for " + ch.settings().name;
                        return;
                    } else {
                        if (!pat) {
                            uint32_t new_id = proj.add_pattern();
                            select_pattern(new_id);
                            pat = get_active_pattern();
                        }
                        if (pat) {
                            int num_bars = get_channel_rack_num_bars(grid_w);
                            int num_steps = num_bars * 16;
                            channel_rack_steps_ = num_steps;
                            float pad_w = get_channel_rack_pad_width(grid_w, num_steps);
                            float total_pads_w = float(num_steps) * pad_w;
                            if (float(x) < grid_x + total_pads_w) {
                                int step = std::clamp(static_cast<int>((float(x) - grid_x) / pad_w), 0, num_steps - 1);
                                auto& notes = pat->get_or_create_channel_notes(ch.id());
                                notes.toggle_step(step, ppq, 60, 100);
                                if (notes.has_note_at_step(step, ppq, 60)) {
                                    audition_note(60);
                                }
                                status_message_ = ch.settings().name + ": Toggled step " + std::to_string(step + 1) + " (Bar " + std::to_string(step / 16 + 1) + ")";
                            }
                            return;
                        }
                    }
                }
            }
        }
    }

    void handle_channel_rack_click(int x, int y) {
        handle_channel_rack_click(win_channel_rack_, x, y);
    }

    void handle_playlist_click(const DawWindow& win, int x, int y) {
        std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
        int px = static_cast<int>(win.x);
        int py = static_cast<int>(win.y + DawWindow::kTitleBarHeight);
        int pw = static_cast<int>(win.w);
        int ph = static_cast<int>(win.h - DawWindow::kTitleBarHeight);

        auto& proj = engine_.session().project();
        auto ppq = proj.time_map().ppq();
        auto bar_ticks = 4 * ppq;

        // 1. Header Toolbar (py + 4 to py + 30)
        if (y >= py + 4 && y <= py + 30) {
            // Brush Pattern prev [ ◄ ]
            if (x >= px + 8 && x <= px + 28) {
                prev_pattern();
                return;
            }
            // Brush Pattern next [ ► ]
            if (x >= px + 164 && x <= px + 184) {
                next_pattern();
                return;
            }
            // [+ Add Track] button
            if (x >= px + 190 && x <= px + 270) {
                proj.add_track("Track " + std::to_string(proj.tracks().size() + 1));
                status_message_ = "Added arrangement Track " + std::to_string(proj.tracks().size());
                return;
            }
            // [− Del Track] button
            if (x >= px + 274 && x <= px + 354) {
                if (proj.tracks().size() > 1) {
                    size_t last_idx = proj.tracks().size() - 1;
                    std::string trk_name = proj.tracks()[last_idx].name();
                    proj.remove_track(last_idx);
                    if (sequencer_scroll_track_ >= static_cast<int>(proj.tracks().size())) {
                        sequencer_scroll_track_ = std::max(0, static_cast<int>(proj.tracks().size()) - 1);
                    }
                    status_message_ = "Removed " + trk_name;
                } else {
                    status_message_ = "Cannot remove last remaining track";
                }
                return;
            }
            return;
        }

        int start_x = px + 110;
        int total_seq_w = std::max(60, (px + pw - 12) - start_x);
        int bars_per_view = (total_seq_w > 900) ? 16 : ((total_seq_w > 550) ? 12 : 8);
        int max_bars = get_max_sequencer_bars();

        domain::Tick view_start_tick = static_cast<domain::Tick>(sequencer_scroll_bar_) * bar_ticks;
        domain::Tick view_duration = static_cast<domain::Tick>(bars_per_view) * bar_ticks;
        domain::Tick view_end_tick = view_start_tick + view_duration;

        // 2. Horizontal Scrollbar Track & Thumb Interaction
        int scroll_y = py + ph - 20;
        int scroll_h = 14;
        float max_scroll = std::max(1.0f, float(max_bars - bars_per_view));
        float thumb_w = std::max(35.0f, (float(bars_per_view) / float(max_bars)) * float(total_seq_w));
        float available_w = float(total_seq_w) - thumb_w;
        float scroll_ratio = std::clamp(float(sequencer_scroll_bar_) / max_scroll, 0.0f, 1.0f);
        float thumb_x = float(start_x) + scroll_ratio * available_w;

        if (x >= start_x && x <= start_x + total_seq_w && y >= scroll_y - 4 && y <= scroll_y + scroll_h + 4) {
            if (x >= thumb_x && x <= thumb_x + thumb_w) {
                dragging_seq_scrollbar_ = true;
                drag_seq_scroll_start_mouse_x_ = float(x);
                drag_seq_scroll_orig_bar_ = sequencer_scroll_bar_;
            } else {
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

        // 3. Timeline Ruler Click (Snap SPM to clicked beat)
        int ruler_y = py + 36;
        int ruler_h = 22;
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

        // 4. Arrangement Tracks & Lanes
        int start_y = py + 62;
        int row_h = 48;
        int step_h_i = 42;
        int rack_avail_h = (py + ph - 24) - start_y;
        int visible_tracks = std::max(1, rack_avail_h / row_h);

        if (proj.tracks().empty()) {
            proj.add_track("Track 1");
        }

        int max_track_scroll = std::max(0, static_cast<int>(proj.tracks().size()) - visible_tracks);
        sequencer_scroll_track_ = std::clamp(sequencer_scroll_track_, 0, max_track_scroll);

        size_t start_t = static_cast<size_t>(sequencer_scroll_track_);
        size_t end_t = std::min(proj.tracks().size(), start_t + static_cast<size_t>(visible_tracks) + 1);

        for (size_t t_idx = start_t; t_idx < end_t; ++t_idx) {
            auto& track = proj.tracks()[t_idx];
            int t_y = start_y + static_cast<int>((t_idx - start_t) * row_h);
            int t_top = t_y;
            int t_bot = t_y + step_h_i;

            // Track Mute [M]
            if (x >= px + 8 && x <= px + 28 && y >= t_top + 8 && y <= t_top + 34) {
                track.set_muted(!track.is_muted());
                status_message_ = track.name() + (track.is_muted() ? " Muted" : " Unmuted");
                return;
            }

            // Track Solo [S]
            if (x >= px + 32 && x <= px + 52 && y >= t_top + 8 && y <= t_top + 34) {
                bool any_unmuted = false;
                for (size_t oi = 0; oi < proj.tracks().size(); ++oi) {
                    if (oi != t_idx && !proj.tracks()[oi].is_muted()) any_unmuted = true;
                }
                for (size_t oi = 0; oi < proj.tracks().size(); ++oi) {
                    proj.tracks()[oi].set_muted(any_unmuted ? (oi != t_idx) : false);
                }
                status_message_ = track.name() + " Solo Toggled";
                return;
            }

            // Arrangement Lane Click: check for existing clip hit or place new pattern clip
            if (x >= start_x && x <= start_x + total_seq_w && y >= t_top && y <= t_bot) {
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
                        drag_clip_track_idx_ = t_idx;
                        drag_clip_idx_ = hit_ci;
                        drag_clip_orig_start_ = clip.start;
                        drag_clip_orig_len_ = clip.length;
                        drag_clip_start_mouse_x_ = x;
                        status_message_ = "Resizing clip length (Drag to change beats/bars)";
                    } else {
                        clip_drag_mode_ = ClipDragMode::Move;
                        drag_clip_track_idx_ = t_idx;
                        drag_clip_idx_ = hit_ci;
                        drag_clip_orig_start_ = clip.start;
                        drag_clip_orig_len_ = clip.length;
                        drag_clip_start_mouse_x_ = x;
                        status_message_ = "Moving clip (Drag to move across beats/bars)";
                    }
                    return;
                }

                // Place new pattern clip of currently selected brush pattern
                double norm_x = double(x - start_x) / double(total_seq_w);
                domain::Tick raw_tick = view_start_tick + static_cast<domain::Tick>(norm_x * view_duration);
                domain::Tick beat_ticks = ppq;
                domain::Tick snapped_start = (raw_tick / beat_ticks) * beat_ticks;

                auto* brush_pat = get_active_pattern();
                uint32_t pat_id = brush_pat ? brush_pat->id() : 1;
                domain::Tick pat_len = brush_pat ? std::max(domain::Tick(4 * ppq), brush_pat->length_ticks(ppq)) : (4 * ppq);

                track.add_clip(domain::Clip{pat_id, snapped_start, pat_len, false});
                size_t new_clip_idx = track.clips().size() - 1;

                clip_drag_mode_ = ClipDragMode::Resize;
                drag_clip_track_idx_ = t_idx;
                drag_clip_idx_ = new_clip_idx;
                drag_clip_orig_start_ = snapped_start;
                drag_clip_orig_len_ = pat_len;
                drag_clip_start_mouse_x_ = x;

                int bar_num = static_cast<int>(snapped_start / bar_ticks) + 1;
                int beat_num = static_cast<int>((snapped_start % bar_ticks) / beat_ticks) + 1;
                std::string p_name = brush_pat ? brush_pat->name() : ("Pattern " + std::to_string(pat_id));
                status_message_ = "Placed " + p_name + " clip at Bar " + std::to_string(bar_num) +
                                  " Beat " + std::to_string(beat_num) + " on " + track.name();
                return;
            }
        }
    }

    void handle_playlist_click(int x, int y) {
        handle_playlist_click(win_playlist_, x, y);
    }

    void handle_piano_roll_click(const DawWindow& win, int x, int y) {
        std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
        PianoRollLayout lay;
        lay.init(win, PianoRollNumPitches, piano_roll_steps_);
        auto ppq = engine_.session().project().time_map().ppq();
        auto step_ticks = ppq / 4;
        int max_base_pitch = 128 - PianoRollNumPitches;
        int max_steps = get_max_piano_roll_steps();
        int max_scroll_step = std::max(0, max_steps - piano_roll_steps_);
        int px = static_cast<int>(win.x);

        // 1. Toolbar clicks
        if (float(y) >= lay.toolbar_y && float(y) <= lay.toolbar_y + lay.toolbar_h) {
            // [ 16 Steps / 32 Steps ]
            if (x >= px + 198 && x <= px + 278) {
                piano_roll_steps_ = (piano_roll_steps_ == 16) ? 32 : 16;
                status_message_ = "Grid resolution set to " + std::to_string(piano_roll_steps_) + " Steps";
                return;
            }
            // [ 📏 Len: X ]
            if (x >= px + 284 && x <= px + 364) {
                if (piano_roll_note_len_steps_ == 1) piano_roll_note_len_steps_ = 2;
                else if (piano_roll_note_len_steps_ == 2) piano_roll_note_len_steps_ = 4;
                else if (piano_roll_note_len_steps_ == 4) piano_roll_note_len_steps_ = 8;
                else piano_roll_note_len_steps_ = 1;

                status_message_ = "Default Note Length set to " + std::to_string(piano_roll_note_len_steps_) + " Steps";
                return;
            }
            // [ Clear ]
            if (x >= px + 370 && x <= px + 430) {
                auto* pat = get_active_pattern();
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
            float(y) >= lay.h_scroll_y && float(y) <= lay.h_scroll_h + lay.h_scroll_y) {
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
            auto* pat = get_active_pattern();
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

    void handle_piano_roll_click(int x, int y) {
        handle_piano_roll_click(win_pianoroll_, x, y);
    }

    void handle_audio_library_click(const DawWindow& win, int x, int y) {
        float px = win.x;
        float py = win.y + DawWindow::kTitleBarHeight;
        float pw = win.w;
        float ph = win.h - DawWindow::kTitleBarHeight;

        // 1. Toolbar button clicks:
        // [📂 Browse...]
        if (x >= px + 8.0f && x <= px + 96.0f && y >= py + 6.0f && y <= py + 30.0f) {
            browse_for_sample_folder();
            return;
        }
        // [🔄 Rescan]
        if (x >= px + 102.0f && x <= px + 174.0f && y >= py + 6.0f && y <= py + 30.0f) {
            if (!sample_library_.current_directory().empty()) {
                load_sample_directory(sample_library_.current_directory());
            } else {
                browse_for_sample_folder();
            }
            return;
        }
        // [⚡ Quick Load]
        if (x >= px + 180.0f && x <= px + pw - 8.0f && y >= py + 6.0f && y <= py + 30.0f) {
            quick_load_samples();
            return;
        }

        // 2. Sample items list
        float list_top = py + 66.0f;
        float list_bot = py + ph - 8.0f;
        float row_h = 36.0f;

        if (y >= list_top && y <= list_bot) {
            int rel_row = static_cast<int>((float(y) - list_top) / row_h);
            size_t s_idx = static_cast<size_t>(audio_lib_scroll_idx_ + rel_row);
            if (s_idx < sample_library_.size()) {
                float ry = list_top + float(rel_row) * row_h;
                // Check if clicked [▶] Play button:
                if (x >= px + 12.0f && x <= px + 36.0f && y >= ry + 5.0f && y <= ry + row_h - 7.0f) {
                    selected_sample_idx_ = static_cast<int>(s_idx);
                    audition_sample(sample_library_.samples()[s_idx].path);
                    return;
                }

                // Clicked row body -> start potential drag or selection
                selected_sample_idx_ = static_cast<int>(s_idx);
                sample_down_idx_ = static_cast<int>(s_idx);
                sample_down_x_ = x;
                sample_down_y_ = y;
                dragged_sample_path_ = sample_library_.samples()[s_idx].path;
                dragged_sample_name_ = sample_library_.samples()[s_idx].name;
                is_potential_sample_drag_ = true;
                is_dragging_sample_ = false;
                SetCapture(hwnd_);
                status_message_ = "Selected: " + sample_library_.samples()[s_idx].name + " (Release to audition, Drag to Channel Rack/Playlist)";
                return;
            }
        }
    }

    void browse_for_sample_folder() {
        BROWSEINFOA bi{};
        bi.hwndOwner = hwnd_;
        bi.lpszTitle = "Select Sample Library Folder";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
        if (pidl != nullptr) {
            char path[MAX_PATH];
            if (SHGetPathFromIDListA(pidl, path)) {
                load_sample_directory(path);
            }
            CoTaskMemFree(pidl);
        }
    }

    void load_sample_directory(const std::string& path) {
        if (path.empty()) return;
        sample_library_.scan(path);
        if (config_) {
            config_->set_string("SampleLibrary", "RootDirectory", path);
            config_->flush();
        }
        audio_lib_scroll_idx_ = 0;
        selected_sample_idx_ = -1;
        hover_sample_idx_ = -1;
        status_message_ = "Scanned Sample Library: " + std::to_string(sample_library_.size()) + " audio files in " + path;
        InvalidateRect(hwnd_, NULL, FALSE);
    }

    void quick_load_samples() {
        std::string sample_dir = "samples";
        std::filesystem::create_directories(sample_dir);

        auto make_wav = [](const std::string& path, int type) {
            std::ofstream f(path, std::ios::binary);
            if (!f.is_open()) return;
            uint32_t sample_rate = 44100;
            uint16_t num_channels = 2;
            uint16_t bits_per_sample = 16;
            size_t num_samples = (type == 1) ? 22050 : ((type == 2) ? 14000 : 35000);
            uint32_t data_bytes = static_cast<uint32_t>(num_samples * num_channels * (bits_per_sample / 8));
            uint32_t chunk_size = 36 + data_bytes;

            f.write("RIFF", 4);
            f.write(reinterpret_cast<const char*>(&chunk_size), 4);
            f.write("WAVE", 4);
            f.write("fmt ", 4);
            uint32_t sub1_sz = 16;
            f.write(reinterpret_cast<const char*>(&sub1_sz), 4);
            uint16_t format = 1; // PCM
            f.write(reinterpret_cast<const char*>(&format), 2);
            f.write(reinterpret_cast<const char*>(&num_channels), 2);
            f.write(reinterpret_cast<const char*>(&sample_rate), 4);
            uint32_t byte_rate = sample_rate * num_channels * (bits_per_sample / 8);
            f.write(reinterpret_cast<const char*>(&byte_rate), 4);
            uint16_t block_align = num_channels * (bits_per_sample / 8);
            f.write(reinterpret_cast<const char*>(&block_align), 2);
            f.write(reinterpret_cast<const char*>(&bits_per_sample), 2);
            f.write("data", 4);
            f.write(reinterpret_cast<const char*>(&data_bytes), 4);

            for (size_t i = 0; i < num_samples; ++i) {
                float t_sec = float(i) / float(sample_rate);
                float val = 0.0f;
                if (type == 0) {
                    float f_inst = 45.0f + 110.0f * std::exp(-t_sec * 28.0f);
                    float env = std::exp(-t_sec * 6.5f);
                    val = std::sin(2.0f * 3.14159265f * f_inst * t_sec) * env;
                } else if (type == 1) {
                    float env = std::exp(-t_sec * 20.0f);
                    float tone = std::sin(2.0f * 3.14159265f * 185.0f * t_sec) * 0.4f;
                    float noise = (float(std::rand()) / float(RAND_MAX) * 2.0f - 1.0f) * 0.6f;
                    val = (tone + noise) * env;
                } else if (type == 2) {
                    float env = std::exp(-t_sec * 50.0f);
                    float noise = (float(std::rand()) / float(RAND_MAX) * 2.0f - 1.0f);
                    val = noise * env * 0.7f;
                } else {
                    float env = std::exp(-t_sec * 2.2f);
                    val = std::sin(2.0f * 3.14159265f * 55.0f * t_sec) * env;
                }
                int16_t s_pcm = static_cast<int16_t>(std::clamp(val, -1.0f, 1.0f) * 30000.0f);
                f.write(reinterpret_cast<const char*>(&s_pcm), 2);
                f.write(reinterpret_cast<const char*>(&s_pcm), 2);
            }
        };

        make_wav("samples/808_Kick_Punch.wav", 0);
        make_wav("samples/Analog_Snare_01.wav", 1);
        make_wav("samples/Crisp_HiHat_Closed.wav", 2);
        make_wav("samples/Sub_Bass_C.wav", 3);

        load_sample_directory("samples");
        status_message_ = "Loaded Quick Demo Samples into Audio Library!";
    }

    void audition_sample(const std::string& filepath) {
        std::vector<float> left, right;
        uint32_t sr = 44100;
        uint16_t ch = 2;
        if (app::SampleLibrary::decode_wav_samples(filepath, left, right, sr, ch)) {
            engine_.preview_sample_data(std::move(left), std::move(right));
            status_message_ = "Auditioning: " + filepath;
        } else {
            size_t num_s = 44100 / 2; // 0.5s preview chime
            left.resize(num_s);
            right.resize(num_s);
            for (size_t i = 0; i < num_s; ++i) {
                float t_sec = float(i) / 44100.0f;
                float env = std::exp(-t_sec * 6.0f);
                float s = std::sin(2.0f * 3.14159265f * 440.0f * t_sec) * 0.4f * env;
                left[i] = s;
                right[i] = s;
            }
            engine_.preview_sample_data(std::move(left), std::move(right));
            status_message_ = "Previewing: " + filepath;
        }
        InvalidateRect(hwnd_, NULL, FALSE);
    }

    void handle_sample_drop(const std::string& path, const std::string& filename, int x, int y) {
        DawWindow* target_win = nullptr;
        for (auto it = z_order_.rbegin(); it != z_order_.rend(); ++it) {
            auto* win = get_window(*it);
            if (win && win->visible && win->contains(static_cast<float>(x), static_cast<float>(y))) {
                target_win = win;
                break;
            }
        }

        if (!target_win) {
            status_message_ = "Sample drop cancelled (dropped outside active windows)";
            return;
        }

        if (target_win->id == WindowId::ChannelRack) {
            drop_sample_to_channel_rack(path, filename);
        } else if (target_win->id == WindowId::Playlist) {
            drop_sample_to_playlist(path, filename, x, y);
        } else {
            status_message_ = "Sample can only be dropped onto Channel Rack or Playlist";
        }
    }

    void drop_sample_to_channel_rack(const std::string& filepath, const std::string& filename) {
        std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
        auto& proj = engine_.session().project();
        const auto ppq = proj.time_map().ppq();

        domain::ChannelSettings s;
        size_t next_idx = proj.channels().size() + 1;
        s.name = "Clipper: " + filename;
        s.volume = domain::kDefaultChannelVolume;
        s.mixer_track = static_cast<uint8_t>(std::min(size_t(63), next_idx));
        auto new_cid = proj.add_channel("core.generator.audioclip", s);

        while (proj.tracks().size() < 4) {
            domain::TrackId tid = static_cast<domain::TrackId>(proj.tracks().size() + 1);
            proj.add_track("Track " + std::to_string(tid));
        }

        auto dev = engine_.get_or_create_channel_device(new_cid);
        auto* clipper = dynamic_cast<plugins::AudioClipDevice*>(dev.get());
        if (clipper) {
            clipper->set_root_key(60); // C5 default root
            bool loaded = clipper->load_wav_file(filepath);
            if (!loaded) {
                std::vector<float> l, r;
                uint32_t sr = 44100;
                uint16_t ch = 2;
                if (app::SampleLibrary::decode_wav_samples(filepath, l, r, sr, ch)) {
                    clipper->load_sample_data(std::move(l), std::move(r), filename, 60);
                } else {
                    app::SampleFileInfo temp_info;
                    temp_info.filename = filename;
                    temp_info.duration_sec = 2.0;
                    app::SampleLibrary::generate_preview_waveform(temp_info, l, r, sr);
                    clipper->load_sample_data(std::move(l), std::move(r), filename, 60);
                }
            }
        }

        // Arm step 0 in active pattern
        auto* pat = get_active_pattern();
        if (!pat) {
            uint32_t new_pat_id = proj.add_pattern();
            select_pattern(new_pat_id);
            pat = get_active_pattern();
        }
        if (pat) {
            pat->get_or_create_channel_notes(new_cid).toggle_step(0, ppq, 60, 100);
        }

        selected_mixer_track_ = s.mixer_track;
        bring_to_front(WindowId::ChannelRack);
        status_message_ = "Loaded Clipper: " + filename + " into Channel Rack (Pattern Step 1 armed)";
        InvalidateRect(hwnd_, NULL, FALSE);
    }

    void drop_sample_to_playlist(const std::string& filepath, const std::string& filename, int drop_x, int drop_y) {
        std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
        auto& proj = engine_.session().project();
        const auto ppq = proj.time_map().ppq();
        const auto bar_ticks = 4 * ppq;

        // Calculate target track index
        float px = win_playlist_.x;
        float py = win_playlist_.y + DawWindow::kTitleBarHeight;
        float pw = win_playlist_.w;

        int start_x = static_cast<int>(px + 110.0f);
        int total_seq_w = std::max(60, static_cast<int>((px + pw - 12.0f) - start_x));
        int start_y = static_cast<int>(py + 62.0f);
        int row_h = 48;

        int track_rel_idx = std::max(0, (drop_y - start_y) / row_h);
        size_t target_t = static_cast<size_t>(sequencer_scroll_track_ + track_rel_idx);

        while (proj.tracks().size() <= target_t) {
            domain::TrackId tid = static_cast<domain::TrackId>(proj.tracks().size() + 1);
            proj.add_track("Track " + std::to_string(tid));
        }

        // Calculate dropped tick and snap to beat
        int bars_per_view = (total_seq_w > 900) ? 16 : ((total_seq_w > 550) ? 12 : 8);
        domain::Tick view_start_tick = static_cast<domain::Tick>(sequencer_scroll_bar_) * bar_ticks;
        domain::Tick view_duration = static_cast<domain::Tick>(bars_per_view) * bar_ticks;

        float rel_x = std::clamp(static_cast<float>(drop_x - start_x), 0.0f, static_cast<float>(total_seq_w));
        double norm_x = double(rel_x) / double(total_seq_w);
        domain::Tick raw_tick = view_start_tick + static_cast<domain::Tick>(norm_x * view_duration);
        domain::Tick snap_ticks = ppq; // 1 beat snap
        domain::Tick start_tick = (raw_tick / snap_ticks) * snap_ticks;

        // Find or create Clipper channel for this sample
        domain::ChannelId clipper_cid = 0;
        for (const auto& ch : proj.channels()) {
            if (ch.device_uid() == "core.generator.audioclip" && ch.settings().name == "Clipper: " + filename) {
                clipper_cid = ch.id();
                break;
            }
        }

        if (clipper_cid == 0) {
            domain::ChannelSettings s;
            size_t next_idx = proj.channels().size() + 1;
            s.name = "Clipper: " + filename;
            s.volume = domain::kDefaultChannelVolume;
            s.mixer_track = static_cast<uint8_t>(std::min(size_t(63), next_idx));
            clipper_cid = proj.add_channel("core.generator.audioclip", s);
        }

        auto dev = engine_.get_or_create_channel_device(clipper_cid);
        auto* clipper = dynamic_cast<plugins::AudioClipDevice*>(dev.get());
        if (clipper) {
            clipper->set_root_key(60); // C5 default root
            bool loaded = clipper->load_wav_file(filepath);
            if (!loaded) {
                std::vector<float> l, r;
                uint32_t sr = 44100;
                uint16_t ch = 2;
                if (app::SampleLibrary::decode_wav_samples(filepath, l, r, sr, ch)) {
                    clipper->load_sample_data(std::move(l), std::move(r), filename, 60);
                } else {
                    app::SampleFileInfo temp_info;
                    temp_info.filename = filename;
                    temp_info.duration_sec = 2.0;
                    app::SampleLibrary::generate_preview_waveform(temp_info, l, r, sr);
                    clipper->load_sample_data(std::move(l), std::move(r), filename, 60);
                }
            }
        }

        // Calculate duration in ticks based on sample length
        domain::Tick clip_len_ticks = bar_ticks; // default 1 bar
        if (clipper && !clipper->sample_l().empty()) {
            double frames = static_cast<double>(clipper->sample_l().size());
            double sr = clipper->file_sample_rate() > 0 ? clipper->file_sample_rate() : 44100.0;
            double dur_sec = frames / sr;
            double bpm = proj.time_map().bpm();
            clip_len_ticks = std::max(ppq, static_cast<domain::Tick>(dur_sec * (bpm / 60.0) * ppq));
        }

        // Create dedicated pattern for this audio clip
        uint32_t new_pat_id = proj.add_pattern();
        auto* pat = proj.get_pattern(new_pat_id);
        if (pat) {
            pat->set_name(filename);
            pat->add_note(clipper_cid, domain::Note{0, clip_len_ticks, 60, 100, 0, 0});
        }

        // Place Clip on the target track
        proj.tracks()[target_t].add_clip(domain::Clip{new_pat_id, start_tick, clip_len_ticks, 0});

        bring_to_front(WindowId::Playlist);
        status_message_ = "Placed audio clip '" + filename + "' onto Track " + std::to_string(target_t + 1) +
                          " at Bar " + std::to_string(start_tick / bar_ticks + 1) + " (Showing waveform signal)";
        InvalidateRect(hwnd_, NULL, FALSE);
    }

    void handle_channel_rack_right_click(const DawWindow& win, int x, int y) {
        std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
        int px = static_cast<int>(win.x);
        int py = static_cast<int>(win.y + DawWindow::kTitleBarHeight);
        float start_y = float(py + 38);
        float row_h = 38.0f;
        auto& proj = engine_.session().project();

        if (y >= static_cast<int>(start_y)) {
            int rel_row = static_cast<int>((float(y) - start_y) / row_h);
            size_t ch_idx = static_cast<size_t>(channel_rack_scroll_ch_ + rel_row);
            if (ch_idx < proj.channels().size()) {
                auto& ch = proj.channels()[ch_idx];
                float row_y = start_y + static_cast<float>(rel_row) * row_h;

                // Right click Pan knob -> reset to center (0.0f)
                if (x >= px + 26 && x <= px + 50 && y >= row_y + 4 && y <= row_y + 30) {
                    ch.settings().pan = 0.0f;
                    status_message_ = ch.settings().name + " Pan reset to Center";
                    return;
                }

                // Right click Vol knob -> reset to default 0.8f (80%)
                if (x >= px + 54 && x <= px + 78 && y >= row_y + 4 && y <= row_y + 30) {
                    ch.settings().volume = domain::kDefaultChannelVolume;
                    status_message_ = ch.settings().name + " Volume reset to default (80%)";
                    return;
                }

                // Right click Target Mixer Track LCD -> reset to default track idx
                if (x >= px + 82 && x <= px + 110 && y >= row_y + 5 && y <= row_y + 29) {
                    ch.settings().mixer_track = static_cast<uint8_t>(ch_idx + 1);
                    status_message_ = ch.settings().name + " Mixer Track set to Track " + std::to_string(ch_idx + 1);
                    return;
                }

                // Right click on Step Sequencer pad -> Turn off/remove step note (FL Studio standard right-click behavior)
                float grid_x = float(px + 240);
                int pw = static_cast<int>(win.w);
                float grid_w = std::max(60.0f, float((px + pw - 12) - grid_x));
                if (float(x) >= grid_x && float(x) <= grid_x + grid_w && y >= row_y + 4 && y <= row_y + 32) {
                    auto ppq = proj.time_map().ppq();
                    auto* pat = get_active_pattern();
                    auto* note_set = pat ? pat->get_channel_notes(ch.id()) : nullptr;
                    bool is_melody = note_set && is_channel_piano_roll(*note_set, ppq);
                    if (!is_melody && pat) {
                        int num_bars = get_channel_rack_num_bars(grid_w);
                        int num_steps = num_bars * 16;
                        channel_rack_steps_ = num_steps;
                        float pad_w = get_channel_rack_pad_width(grid_w, num_steps);
                        float total_pads_w = float(num_steps) * pad_w;
                        if (float(x) < grid_x + total_pads_w) {
                            int step = std::clamp(static_cast<int>((float(x) - grid_x) / pad_w), 0, num_steps - 1);
                            auto& notes = pat->get_or_create_channel_notes(ch.id());
                            if (notes.has_note_at_step(step, ppq, 60)) {
                                notes.toggle_step(step, ppq, 60);
                                status_message_ = ch.settings().name + ": Removed note at step " + std::to_string(step + 1);
                            }
                        }
                        return;
                    }
                }

                // Right click Instrument Button or Piano Roll preview -> Open full Piano Roll
                piano_roll_channel_ = ch.id();
                selected_mixer_track_ = ch.settings().mixer_track;
                toggle_or_focus_window(WindowId::PianoRoll);
                status_message_ = "Opened Piano Roll for " + ch.settings().name;
                return;
            }
        }
    }

    void handle_channel_rack_right_click(int x, int y) {
        handle_channel_rack_right_click(win_channel_rack_, x, y);
    }

    void handle_playlist_right_click(const DawWindow& win, int x, int y) {
        std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
        int px = static_cast<int>(win.x);
        int py = static_cast<int>(win.y + DawWindow::kTitleBarHeight);
        int pw = static_cast<int>(win.w);
        int ph = static_cast<int>(win.h - DawWindow::kTitleBarHeight);

        int start_y = py + 62;
        int row_h = 48;
        int step_h_i = 42;
        int rack_avail_h = (py + ph - 24) - start_y;
        int start_x = px + 110;
        int total_seq_w = std::max(60, (px + pw - 12) - start_x);
        int bars_per_view = (total_seq_w > 900) ? 16 : ((total_seq_w > 550) ? 12 : 8);
        auto ppq = engine_.session().project().time_map().ppq();
        auto bar_ticks = 4 * ppq;
        domain::Tick view_start_tick = static_cast<domain::Tick>(sequencer_scroll_bar_) * bar_ticks;
        domain::Tick view_duration = static_cast<domain::Tick>(bars_per_view) * bar_ticks;
        domain::Tick view_end_tick = view_start_tick + view_duration;
        auto& proj = engine_.session().project();

        int visible_tracks = std::max(1, rack_avail_h / row_h);
        size_t start_t = static_cast<size_t>(sequencer_scroll_track_);
        size_t end_t = std::min(proj.tracks().size(), start_t + static_cast<size_t>(visible_tracks) + 1);

        for (size_t t_idx = start_t; t_idx < end_t; ++t_idx) {
            int t_y = start_y + static_cast<int>((t_idx - start_t) * row_h);
            int t_top = t_y;
            int t_bot = t_y + step_h_i;

            if (t_idx < proj.tracks().size() && y >= t_top && y <= t_bot) {
                // Right click on Track Header: Remove this track
                if (x >= px + 8 && x < start_x) {
                    if (proj.tracks().size() > 1) {
                        std::string trk_name = proj.tracks()[t_idx].name();
                        proj.remove_track(t_idx);
                        if (sequencer_scroll_track_ >= static_cast<int>(proj.tracks().size())) {
                            sequencer_scroll_track_ = std::max(0, static_cast<int>(proj.tracks().size()) - 1);
                        }
                        status_message_ = "Deleted " + trk_name;
                    } else {
                        status_message_ = "Cannot delete last remaining track";
                    }
                    return;
                }

                if (x >= start_x && x <= start_x + total_seq_w) {
                auto& track = proj.tracks()[t_idx];
                for (size_t ci = 0; ci < track.clips().size(); ++ci) {
                    const auto& clip = track.clips()[ci];
                    if (clip.end() <= view_start_tick || clip.start >= view_end_tick) continue;
                    double norm_start = double(clip.start - view_start_tick) / double(view_duration);
                    double norm_len = double(clip.length) / double(view_duration);
                    int cx = start_x + static_cast<int>(norm_start * total_seq_w);
                    int cw = std::max(16, static_cast<int>(norm_len * total_seq_w));

                    if (x >= cx && x <= cx + cw) {
                        track.remove_clip(ci);
                        status_message_ = "Deleted clip from " + track.name();
                        return;
                    }
                }
                return;
            }
        }
    }
}

    void handle_playlist_right_click(int x, int y) {
        handle_playlist_right_click(win_playlist_, x, y);
    }

    void on_passive_mouse_move(int x, int y) {
        int prev_hover_close = hover_close_win_id_;
        hover_close_win_id_ = -1;

        // 1. Header Bar buttons hover indicator & contextual hints
        if (y >= 8 && y <= 40) {
            if (x >= 200 && x <= 310) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                std::string cur_name = engine_.session().project().name();
                if (cur_name.empty()) cur_name = "Untitled Project";
                status_message_ = "Project: " + cur_name;
                return;
            }
            if (x >= 316 && x <= 346) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                std::string u_lbl = engine_.session().undo_stack().last_undo_label();
                status_message_ = engine_.session().undo_stack().can_undo() ? ("Undo: " + u_lbl + " (Ctrl+Z)") : "Undo (Ctrl+Z) - Nothing to undo";
                return;
            }
            if (x >= 350 && x <= 380) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                std::string r_lbl = engine_.session().undo_stack().last_redo_label();
                status_message_ = engine_.session().undo_stack().can_redo() ? ("Redo: " + r_lbl + " (Ctrl+Y)") : "Redo (Ctrl+Y) - Nothing to redo";
                return;
            }
            if (x >= 388 && x <= 424) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                status_message_ = "Start Audio Playback (Space)";
                return;
            }
            if (x >= 428 && x <= 464) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                status_message_ = "Pause Audio Playback (Space)";
                return;
            }
            if (x >= 468 && x <= 504) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                status_message_ = "Stop Audio Playback & Reset Playhead (Esc)";
                return;
            }
            if (x >= 512 && x <= 536) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                status_message_ = "Decrease Project Tempo (-1 BPM)";
                return;
            }
            if (x >= 624 && x <= 648) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                status_message_ = "Increase Project Tempo (+1 BPM)";
                return;
            }
            if (x >= 796 && x <= 832) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                status_message_ = "Toggle / Focus Playlist Window (F6)";
                return;
            }
            if (x >= 836 && x <= 872) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                status_message_ = "Toggle / Focus Piano Roll Window (F7)";
                return;
            }
            if (x >= 876 && x <= 912) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                status_message_ = "Toggle / Focus Mixer Window (F9)";
                return;
            }
            if (x >= 916 && x <= 952) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                status_message_ = "Toggle / Focus Inspector / Track FX Window (F8 / Ctrl+I)";
                return;
            }
            if (x >= 1054 && x <= 1090) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                status_message_ = "Toggle / Focus Audio Library Window (F4 / Ctrl+B)";
                return;
            }
            if (x >= 1094 && x <= 1130) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                status_message_ = magnet_enabled_ ? "Disable Magnetic Snapping (Ctrl+M)" : "Enable Magnetic Snapping (Ctrl+M)";
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

        // 2. Desktop Windowed Panels (Reverse Z-Order: Top-to-Bottom)
        for (auto it = z_order_.rbegin(); it != z_order_.rend(); ++it) {
            auto* win = get_window(*it);
            if (!win || !win->visible) continue;
            if (!win->contains(static_cast<float>(x), static_cast<float>(y))) continue;

            // Close button hover [✕]
            if (win->is_in_close_btn(static_cast<float>(x), static_cast<float>(y))) {
                hover_close_win_id_ = static_cast<int>(win->id);
                SetCursor(LoadCursor(NULL, IDC_HAND));
                status_message_ = "Close " + win->title;
                if (hover_close_win_id_ != prev_hover_close) {
                    InvalidateRect(hwnd_, NULL, FALSE);
                }
                return;
            }

            // Resize Border Edges & Corners hover
            auto edge = get_resize_edge(*win, static_cast<float>(x), static_cast<float>(y));
            if (edge != ResizeEdge::None) {
                switch (edge) {
                    case ResizeEdge::Left:
                    case ResizeEdge::Right:
                        SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                        break;
                    case ResizeEdge::Top:
                    case ResizeEdge::Bottom:
                        SetCursor(LoadCursor(NULL, IDC_SIZENS));
                        break;
                    case ResizeEdge::TopLeft:
                    case ResizeEdge::BottomRight:
                        SetCursor(LoadCursor(NULL, IDC_SIZENWSE));
                        break;
                    case ResizeEdge::TopRight:
                    case ResizeEdge::BottomLeft:
                        SetCursor(LoadCursor(NULL, IDC_SIZENESW));
                        break;
                    default:
                        break;
                }
                status_message_ = "Resize " + win->title + (magnet_enabled_ ? " (Magnet Snapping On)" : "");
                return;
            }

            // Title Bar hover
            if (win->is_in_title_bar(static_cast<float>(x), static_cast<float>(y))) {
                SetCursor(LoadCursor(NULL, IDC_SIZEALL));
                status_message_ = "Click and drag to move " + win->title + (magnet_enabled_ ? " (Magnet Snapping On)" : "");
                return;
            }

            // Content Area Hover inspection
            if (win->id == WindowId::Inspector) {
                int insp_x = static_cast<int>(win->x);
                int insp_r = static_cast<int>(win->x + win->w);
                int insp_y = static_cast<int>(win->y + DawWindow::kTitleBarHeight);
                int insp_bot = static_cast<int>(win->y + win->h);
                uint32_t tid = selected_mixer_track_;
                auto* track = engine_.session().project().mixer_graph().get_track(tid);

                if (x >= insp_x + 14 && x <= insp_r - 14 && y >= insp_y + 54 && y <= insp_y + 72) {
                    SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                    status_message_ = "Drag or Wheel to adjust Track Volume (Right-click resets to 0 dB)";
                    return;
                }
                if (x >= insp_x + 14 && x <= insp_r - 14 && y >= insp_y + 76 && y <= insp_y + 94) {
                    SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                    status_message_ = "Drag or Wheel to adjust Track Panning (Right-click resets to Center)";
                    return;
                }
                int mid_w = (insp_r - 14) - (insp_x + 14);
                int btn_w = (mid_w - 6) / 2;
                if (y >= insp_y + 98 && y <= insp_y + 118) {
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
                size_t num_fx = track ? track->inserts().size() : 0;
                int slot_y_start = insp_y + 156;
                int slot_h = 32;
                int slot_gap = 4;
                int fx_area_bot = insp_bot - 8;
                int fx_avail_h = fx_area_bot - slot_y_start;
                int vis_fx_items = std::max(1, fx_avail_h / (slot_h + slot_gap));
                int total_fx_items = static_cast<int>(num_fx) + (num_fx < 10 ? 1 : 0);
                int max_fx_scroll = std::max(0, total_fx_items - vis_fx_items);
                int right_margin = (max_fx_scroll > 0) ? 14 : 8;

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

            if (win->id == WindowId::AudioLibrary) {
                float px = win->x;
                float py = win->y + DawWindow::kTitleBarHeight;
                float pw = win->w;
                float ph = win->h - DawWindow::kTitleBarHeight;

                if (x >= px + 8.0f && x <= px + 96.0f && y >= py + 6.0f && y <= py + 30.0f) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    status_message_ = "Browse for audio samples directory";
                    return;
                }
                if (x >= px + 102.0f && x <= px + 174.0f && y >= py + 6.0f && y <= py + 30.0f) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    status_message_ = "Rescan current sample folder";
                    return;
                }
                if (x >= px + 180.0f && x <= px + pw - 8.0f && y >= py + 6.0f && y <= py + 30.0f) {
                    SetCursor(LoadCursor(NULL, IDC_HAND));
                    status_message_ = "Quick Load demo sample pack";
                    return;
                }

                float list_top = py + 66.0f;
                float list_bot = py + ph - 8.0f;
                float row_h = 36.0f;
                if (y >= list_top && y <= list_bot) {
                    int rel_row = static_cast<int>((float(y) - list_top) / row_h);
                    size_t s_idx = static_cast<size_t>(audio_lib_scroll_idx_ + rel_row);
                    if (s_idx < sample_library_.size()) {
                        hover_sample_idx_ = static_cast<int>(s_idx);
                        SetCursor(LoadCursor(NULL, IDC_HAND));
                        const auto& smp = sample_library_.samples()[s_idx];
                        status_message_ = smp.name + " (" + smp.formatted_duration() + ", " + smp.formatted_size() + ") — Click [▶] to preview, Drag to Channel Rack/Playlist";
                        InvalidateRect(hwnd_, NULL, FALSE);
                        return;
                    }
                }
                if (hover_sample_idx_ != -1) {
                    hover_sample_idx_ = -1;
                    InvalidateRect(hwnd_, NULL, FALSE);
                }
                SetCursor(LoadCursor(NULL, IDC_ARROW));
                return;
            }

            if (win->id == WindowId::ChannelRack) {
                int px = static_cast<int>(win->x);
                int py = static_cast<int>(win->y + DawWindow::kTitleBarHeight);
                float start_y = float(py + 38);
                float row_h = 38.0f;
                auto& proj = engine_.session().project();
                if (y >= static_cast<int>(start_y)) {
                    int rel_row = static_cast<int>((float(y) - start_y) / row_h);
                    size_t ch_idx = static_cast<size_t>(channel_rack_scroll_ch_ + rel_row);
                    if (ch_idx < proj.channels().size()) {
                        float row_y = start_y + static_cast<float>(rel_row) * row_h;
                        if (x >= px + 26 && x <= px + 50 && y >= row_y + 4 && y <= row_y + 30) {
                            SetCursor(LoadCursor(NULL, IDC_SIZENS));
                            status_message_ = proj.channels()[ch_idx].settings().name + " Pan (Drag up/down or wheel)";
                            return;
                        }
                        if (x >= px + 54 && x <= px + 78 && y >= row_y + 4 && y <= row_y + 30) {
                            SetCursor(LoadCursor(NULL, IDC_SIZENS));
                            const auto& ch = proj.channels()[ch_idx];
                            int pct = static_cast<int>(std::round((ch.settings().volume / domain::kMaxChannelVolume) * 100.0f));
                            status_message_ = ch.settings().name + " Volume: " + std::to_string(pct) + "% (Drag up/down or wheel)";
                            return;
                        }
                        if (x >= px + 82 && x <= px + 110 && y >= row_y + 5 && y <= row_y + 29) {
                            SetCursor(LoadCursor(NULL, IDC_SIZENS));
                            status_message_ = proj.channels()[ch_idx].settings().name + " Mixer Track LCD (Drag or wheel to change)";
                            return;
                        }
                        if (x >= px + 114 && x <= px + 210 && y >= row_y + 5 && y <= row_y + 31) {
                            SetCursor(LoadCursor(NULL, IDC_HAND));
                            status_message_ = "Open Instrument Editor: " + proj.channels()[ch_idx].settings().name;
                            return;
                        }
                        if (x >= px + 214 && x <= px + 234 && y >= row_y + 6 && y <= row_y + 30) {
                            SetCursor(LoadCursor(NULL, IDC_HAND));
                            status_message_ = "Open Piano Roll for " + proj.channels()[ch_idx].settings().name;
                            return;
                        }
                    }
                }
                SetCursor(LoadCursor(NULL, IDC_ARROW));
                return;
            }

            if (win->id == WindowId::Playlist) {
                int px = static_cast<int>(win->x);
                int py = static_cast<int>(win->y + DawWindow::kTitleBarHeight);
                int pw = static_cast<int>(win->w);
                int ph = static_cast<int>(win->h - DawWindow::kTitleBarHeight);
                int start_y = py + 62;
                int row_h = 48;
                int step_h_i = 42;
                int rack_avail_h = (py + ph - 24) - start_y;
                int start_x = px + 110;
                int total_seq_w = std::max(60, (px + pw - 12) - start_x);
                int bars_per_view = (total_seq_w > 900) ? 16 : ((total_seq_w > 550) ? 12 : 8);
                auto ppq = engine_.session().project().time_map().ppq();
                auto bar_ticks = 4 * ppq;
                domain::Tick view_start_tick = static_cast<domain::Tick>(sequencer_scroll_bar_) * bar_ticks;
                domain::Tick view_duration = static_cast<domain::Tick>(bars_per_view) * bar_ticks;
                domain::Tick view_end_tick = view_start_tick + view_duration;
                auto& proj = engine_.session().project();

                int visible_tracks = std::max(1, rack_avail_h / row_h);
                size_t start_t = static_cast<size_t>(sequencer_scroll_track_);
                size_t end_t = std::min(proj.tracks().size(), start_t + static_cast<size_t>(visible_tracks) + 1);

                bool hovering_edge = false;
                for (size_t t_idx = start_t; t_idx < end_t; ++t_idx) {
                    int t_y = start_y + static_cast<int>((t_idx - start_t) * row_h);
                    int t_top = t_y;
                    int t_bot = t_y + step_h_i;

                    if (y >= t_top && y <= t_bot && x >= start_x && x <= start_x + total_seq_w) {
                        const auto& track = proj.tracks()[t_idx];
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

                int scroll_y = py + ph - 20;
                int scroll_h = 14;
                is_hovering_seq_scrollbar_ = (x >= start_x && x <= start_x + total_seq_w && y >= scroll_y - 2 && y <= scroll_y + scroll_h + 2);

                if (is_hovering_seq_scrollbar_) {
                    SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                    return;
                }

                if (is_hovering_clip_edge_ != hovering_edge) {
                    is_hovering_clip_edge_ = hovering_edge;
                }
                SetCursor(LoadCursor(NULL, hovering_edge ? IDC_SIZEWE : IDC_ARROW));
                return;
            }

            if (win->id == WindowId::PianoRoll) {
                PianoRollLayout lay;
                lay.init(*win, PianoRollNumPitches, piano_roll_steps_);
                auto ppq = engine_.session().project().time_map().ppq();
                auto step_ticks = ppq / 4;

                is_hovering_note_edge_ = false;
                is_hovering_note_body_ = false;
                is_hovering_piano_v_scrollbar_ = false;
                is_hovering_piano_h_scrollbar_ = false;

                if (float(x) >= lay.v_scroll_x && float(x) <= lay.v_scroll_x + lay.v_scroll_w &&
                    float(y) >= lay.grid_top && float(y) <= lay.grid_bottom) {
                    is_hovering_piano_v_scrollbar_ = true;
                    SetCursor(LoadCursor(NULL, IDC_SIZENS));
                    return;
                }

                if (float(x) >= lay.grid_x && float(x) <= lay.grid_x + lay.grid_w &&
                    float(y) >= lay.h_scroll_y && float(y) <= lay.h_scroll_y + lay.h_scroll_h) {
                    is_hovering_piano_h_scrollbar_ = true;
                    SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                    return;
                }

                if (float(x) >= lay.grid_x && float(x) <= lay.grid_x + lay.grid_w &&
                    float(y) >= lay.grid_top && float(y) <= lay.grid_bottom) {
                    auto* pat = get_active_pattern();
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
                return;
            }

            if (win->id == WindowId::Mixer) {
                float mx = win->x;
                float my = win->y + DawWindow::kTitleBarHeight;
                float mw = win->w;
                float mh = win->h - DawWindow::kTitleBarHeight;
                float track_w = 96.0f;
                float gap = 10.0f;
                float ty = my + 30.0f;
                float strip_h = std::max(120.0f, mh - 36.0f);
                float master_x = mx + 12.0f;
                float insert_start_x = master_x + track_w + gap;
                float avail_w = (mx + mw - 12.0f) - insert_start_x;
                int vis_inserts = std::max(1, static_cast<int>(avail_w / (track_w + gap)));

                auto check_strip_hover = [&](int tid, float tx) -> bool {
                    float fader_bot = ty + strip_h - 52.0f;
                    if (x < tx || x > tx + track_w || y < ty || y > ty + strip_h) return false;
                    if (x >= tx + 28.0f && x <= tx + 92.0f && y >= ty + 44.0f && y <= fader_bot + 2.0f) {
                        SetCursor(LoadCursor(NULL, IDC_SIZENS));
                        status_message_ = (tid == 0 ? "Master" : "Track " + std::to_string(tid)) + " Volume (Drag to adjust)";
                        return true;
                    }
                    if (x >= tx + 6.0f && x <= tx + track_w - 6.0f && y >= fader_bot + 2.0f && y <= ty + strip_h - 2.0f) {
                        SetCursor(LoadCursor(NULL, IDC_SIZENS));
                        status_message_ = (tid == 0 ? "Master" : "Track " + std::to_string(tid)) + " Pan (Drag to adjust)";
                        return true;
                    }
                    return false;
                };

                if (check_strip_hover(0, master_x)) return;
                for (int s = 0; s < vis_inserts; ++s) {
                    int tid = 1 + mixer_scroll_track_ + s;
                    float tx = insert_start_x + s * (track_w + gap);
                    if (check_strip_hover(tid, tx)) return;
                }
                SetCursor(LoadCursor(NULL, IDC_ARROW));
                return;
            }
        }
        if (hover_close_win_id_ != prev_hover_close) {
            InvalidateRect(hwnd_, NULL, FALSE);
        }
        SetCursor(LoadCursor(NULL, IDC_ARROW));
    }

    void handle_piano_roll_right_click(const DawWindow& win, int x, int y) {
        PianoRollLayout lay;
        lay.init(win, PianoRollNumPitches, piano_roll_steps_);
        auto ppq = engine_.session().project().time_map().ppq();
        auto step_ticks = ppq / 4;

        if (float(x) >= lay.grid_x && float(x) <= lay.grid_x + lay.grid_w &&
            float(y) >= lay.grid_top && float(y) <= lay.grid_bottom) {
            auto* pat = get_active_pattern();
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

    void handle_piano_roll_right_click(int x, int y) {
        handle_piano_roll_right_click(win_pianoroll_, x, y);
    }

    void on_right_click(int x, int y) {
        std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
        // Reverse Z-order dispatch: Topmost window under cursor receives the right-click
        for (auto it = z_order_.rbegin(); it != z_order_.rend(); ++it) {
            auto* win = get_window(*it);
            if (!win || !win->visible) continue;
            if (!win->contains(static_cast<float>(x), static_cast<float>(y))) continue;

            bring_to_front(*it);
            switch (win->id) {
                case WindowId::ChannelRack:
                    handle_channel_rack_right_click(*win, x, y);
                    return;
                case WindowId::Playlist:
                    handle_playlist_right_click(*win, x, y);
                    return;
                case WindowId::PianoRoll:
                    handle_piano_roll_right_click(*win, x, y);
                    return;
                case WindowId::Mixer:
                    handle_mixer_right_click(*win, x, y);
                    return;
                case WindowId::Inspector:
                    handle_inspector_right_click(*win, x, y);
                    return;
                case WindowId::AudioLibrary:
                    engine_.stop_sample_preview();
                    status_message_ = "Stopped sample preview";
                    return;
            }
            return;
        }
    }

    void on_mouse_move(int x, int y) {
        if (is_potential_sample_drag_) {
            int dx = x - sample_down_x_;
            int dy = y - sample_down_y_;
            if (dx * dx + dy * dy > 25) {
                is_dragging_sample_ = true;
            }
        }

        if (is_dragging_window_) {
            auto* win = get_window(dragging_window_id_);
            if (!win) return;
            float dx = static_cast<float>(x - drag_win_start_mouse_x_);
            float dy = static_cast<float>(y - drag_win_start_mouse_y_);
            if (window_drag_mode_ == WindowDragMode::Move) {
                float new_x = drag_win_orig_x_ + dx;
                float new_y = drag_win_orig_y_ + dy;
                apply_magnet_snapping(new_x, new_y, win->w, win->h, win->id);
                float ws_l = 8.0f;
                float ws_t = 52.0f;
                float ws_r = static_cast<float>(client_w_ - 8);
                float ws_b = static_cast<float>(client_h_ - 30);
                win->x = std::clamp(new_x, ws_l, std::max(ws_l, ws_r - win->w));
                win->y = std::clamp(new_y, ws_t, std::max(ws_t, ws_b - win->h));
            } else if (window_drag_mode_ == WindowDragMode::Resize) {
                apply_resize_dragging(*win, dx, dy);
            }
            return;
        }

        if (active_editor_channel_ != 0 && dragging_clipper_knob_ != ClipperKnobId::None) {
            auto* dev = get_active_channel_synth();
            auto* clipper = dynamic_cast<plugins::AudioClipDevice*>(dev);
            if (clipper) {
                AudioClipEditor::handle_drag(clipper, dragging_clipper_knob_,
                                            drag_clipper_start_y_, drag_clipper_orig_val_, y, status_message_);
                return;
            }
        }

        if (active_editor_channel_ != 0 && dragging_xsynth_param_idx_ >= 0) {
            auto* dev = get_active_channel_synth();
            auto* xsynth = dynamic_cast<plugins::XSynthDevice*>(dev);
            if (xsynth) {
                XAudioEditor::handle_xsynth_drag(xsynth, dragging_xsynth_param_idx_,
                                                drag_xsynth_start_y_, drag_xsynth_orig_val_, y, status_message_);
                return;
            }
        }

        if (active_editor_effect_track_ >= 0 && active_editor_effect_slot_ >= 0 && dragging_effect_param_idx_ >= 0) {
            auto* track = engine_.session().project().mixer_graph().get_track(static_cast<uint32_t>(active_editor_effect_track_));
            if (track && static_cast<size_t>(active_editor_effect_slot_) < track->inserts().size()) {
                auto& ins = track->inserts()[active_editor_effect_slot_];
                if (ins.device) {
                    XAudioEditor::handle_effect_drag(ins.device.get(), effect_editor_bounds_,
                                                    dragging_effect_param_idx_,
                                                    drag_effect_start_x_, drag_effect_start_y_, drag_effect_orig_val_,
                                                    x, y, status_message_);
                    return;
                }
            }
        }

        if (dragging_inspector_vol_) {
            std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
            int insp_x = static_cast<int>(win_inspector_.x);
            int insp_r = static_cast<int>(win_inspector_.x + win_inspector_.w);
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
            int insp_x = static_cast<int>(win_inspector_.x);
            int insp_r = static_cast<int>(win_inspector_.x + win_inspector_.w);
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
            int insp_y = static_cast<int>(win_inspector_.y + DawWindow::kTitleBarHeight);
            int insp_bot = static_cast<int>(win_inspector_.y + win_inspector_.h);
            float slot_y_start = static_cast<float>(insp_y + 156);
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
            int insp_r = static_cast<int>(win_inspector_.x + win_inspector_.w);
            int right_margin = 8;
            float norm = float(x - (insp_r - (right_margin + 80))) / 54.0f;
            norm = std::clamp(norm, 0.0f, 1.0f);
            auto* track = engine_.session().project().mixer_graph().get_track(selected_mixer_track_);
            if (track && static_cast<size_t>(dragging_inspector_wet_slot_) < track->inserts().size()) {
                track->inserts()[dragging_inspector_wet_slot_].wet_mix = norm;
                int pct = static_cast<int>(std::round(norm * 100.0f));
                std::string name = track->inserts()[dragging_inspector_wet_slot_].device ?
                    track->inserts()[dragging_inspector_wet_slot_].device->name() : "Effect";
                status_message_ = name + " Wet Mix: " + std::to_string(pct) + "%";
            }
            return;
        }

        if (dragging_channel_pan_idx_ >= 0) {
            std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
            float dy = float(drag_knob_start_mouse_y_ - y);
            float new_pan = std::clamp(drag_knob_orig_val_ + dy / 75.0f, -1.0f, 1.0f);
            auto& proj = engine_.session().project();
            if (static_cast<size_t>(dragging_channel_pan_idx_) < proj.channels().size()) {
                auto& ch = proj.channels()[dragging_channel_pan_idx_];
                ch.settings().pan = new_pan;
                std::string pan_str;
                if (std::abs(new_pan) < 0.02f) pan_str = "Center";
                else if (new_pan < 0.0f) pan_str = "Left " + std::to_string(static_cast<int>(std::round(-new_pan * 100.0f))) + "%";
                else pan_str = "Right " + std::to_string(static_cast<int>(std::round(new_pan * 100.0f))) + "%";
                status_message_ = ch.settings().name + " Pan: " + pan_str;
            }
            return;
        }

        if (dragging_channel_vol_idx_ >= 0) {
            std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
            float dy = float(drag_knob_start_mouse_y_ - y);
            float new_vol = std::clamp(drag_knob_orig_val_ + dy / 100.0f, 0.0f, domain::kMaxChannelVolume);
            auto& proj = engine_.session().project();
            if (static_cast<size_t>(dragging_channel_vol_idx_) < proj.channels().size()) {
                auto& ch = proj.channels()[dragging_channel_vol_idx_];
                ch.settings().volume = new_vol;
                int pct = static_cast<int>(std::round((new_vol / domain::kMaxChannelVolume) * 100.0f));
                status_message_ = ch.settings().name + " Volume: " + std::to_string(pct) + "%";
            }
            return;
        }

        if (dragging_channel_target_track_idx_ >= 0) {
            std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
            float dy = float(drag_target_track_start_mouse_y_ - y);
            int delta_trk = static_cast<int>(std::round(dy / 15.0f));
            int new_trk = std::clamp(drag_target_track_orig_val_ + delta_trk, 0, 64);
            auto& proj = engine_.session().project();
            if (static_cast<size_t>(dragging_channel_target_track_idx_) < proj.channels().size()) {
                auto& ch = proj.channels()[dragging_channel_target_track_idx_];
                ch.settings().mixer_track = static_cast<uint8_t>(new_trk);
                if (new_trk > 0 && !proj.mixer_graph().get_track(new_trk)) {
                    proj.mixer_graph().add_track(new_trk, "Track " + std::to_string(new_trk));
                }
                selected_mixer_track_ = ch.settings().mixer_track;
                status_message_ = ch.settings().name + " Mixer Track: " + (new_trk == 0 ? "Master" : ("Track " + std::to_string(new_trk)));
            }
            return;
        }

        if (dragging_seq_v_scrollbar_) {
            int py = static_cast<int>(win_playlist_.y + DawWindow::kTitleBarHeight);
            int ph = static_cast<int>(win_playlist_.h - DawWindow::kTitleBarHeight);
            float rack_avail_h = float((py + ph - 24) - (py + 62));
            auto& proj = engine_.session().project();
            int visible_tracks = std::max(1, static_cast<int>(rack_avail_h / 48.0f));
            int max_trk_scroll = std::max(0, static_cast<int>(proj.tracks().size()) - visible_tracks);

            if (max_trk_scroll > 0) {
                float v_thumb_h = std::max(20.0f, (float(visible_tracks) / float(proj.tracks().size() + 1)) * rack_avail_h);
                float avail_scroll_h = rack_avail_h - v_thumb_h;
                if (avail_scroll_h > 0.0f) {
                    float dy = float(y) - drag_seq_v_scroll_start_y_;
                    float delta_trk = (dy / avail_scroll_h) * float(max_trk_scroll);
                    int new_scroll = std::clamp(static_cast<int>(std::round(float(drag_seq_v_scroll_orig_track_) + delta_trk)), 0, max_trk_scroll);
                    if (new_scroll != sequencer_scroll_track_) {
                        sequencer_scroll_track_ = new_scroll;
                    }
                }
            }
            return;
        }

        if (dragging_seq_scrollbar_) {
            int max_bars = get_max_sequencer_bars();
            int px = static_cast<int>(win_playlist_.x);
            int pw = static_cast<int>(win_playlist_.w);
            int start_x = px + 110;
            int total_seq_w = std::max(60, (px + pw - 12) - start_x);
            int bars_per_view = (total_seq_w > 900) ? 16 : ((total_seq_w > 550) ? 12 : 8);
            float max_scroll = std::max(1.0f, float(max_bars - bars_per_view));
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
            lay.init(win_pianoroll_, PianoRollNumPitches, piano_roll_steps_);
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
            lay.init(win_pianoroll_, PianoRollNumPitches, piano_roll_steps_);
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
                    int px = static_cast<int>(win_playlist_.x);
                    int pw = static_cast<int>(win_playlist_.w);
                    int start_x = px + 110;
                    int total_seq_w = std::max(60, (px + pw - 12) - start_x);
                    int bars_per_view = (total_seq_w > 900) ? 16 : ((total_seq_w > 550) ? 12 : 8);
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
                    int px = static_cast<int>(win_playlist_.x);
                    int pw = static_cast<int>(win_playlist_.w);
                    int start_x = px + 110;
                    int total_seq_w = std::max(60, (px + pw - 12) - start_x);
                    int bars_per_view = (total_seq_w > 900) ? 16 : ((total_seq_w > 550) ? 12 : 8);
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
            lay.init(win_pianoroll_, PianoRollNumPitches, piano_roll_steps_);
            auto ppq = engine_.session().project().time_map().ppq();
            auto step_ticks = ppq / 4;

            int dx = x - drag_note_start_mouse_x_;
            int delta_steps = static_cast<int>(std::round(float(dx) / lay.step_w));
            int orig_steps = std::max(1, static_cast<int>(drag_note_orig_len_ / step_ticks));
            int new_steps = std::max(1, orig_steps + delta_steps);
            domain::Tick new_len = new_steps * step_ticks;

            auto* pat = get_active_pattern();
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
            lay.init(win_pianoroll_, PianoRollNumPitches, piano_roll_steps_);
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
                auto* pat = get_active_pattern();
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

            if (active_window_ == WindowId::PianoRoll || view_mode_ == ViewMode::PianoRoll) {
                PianoRollLayout lay;
                lay.init(win_pianoroll_, PianoRollNumPitches, piano_roll_steps_);
                int rel_s = std::clamp(static_cast<int>((float(x) - lay.grid_x) / lay.step_w), 0, piano_roll_steps_ - 1);
                int abs_s = piano_roll_scroll_step_ + rel_s;
                song_position_marker_ = abs_s * step_ticks;
                status_message_ = "SPM dragged to Step " + std::to_string(abs_s + 1);
            } else {
                int px = static_cast<int>(win_playlist_.x);
                int pw = static_cast<int>(win_playlist_.w);
                int start_x = px + 110;
                int total_seq_w = std::max(60, (px + pw - 12) - start_x);
                int bars_per_view = (total_seq_w > 900) ? 16 : ((total_seq_w > 550) ? 12 : 8);
                domain::Tick view_duration = bars_per_view * bar_ticks;
                domain::Tick view_start_tick = sequencer_scroll_bar_ * bar_ticks;

                double norm_x = double(x - start_x) / double(total_seq_w);
                domain::Tick raw_tick = view_start_tick + static_cast<domain::Tick>(norm_x * view_duration);
                domain::Tick beat_ticks = ppq;
                song_position_marker_ = std::max(domain::Tick(0), (raw_tick / beat_ticks) * beat_ticks);

                int b_num = static_cast<int>(song_position_marker_ / bar_ticks) + 1;
                int bt_num = static_cast<int>((song_position_marker_ % bar_ticks) / beat_ticks) + 1;
                status_message_ = "SPM dragged to Bar " + std::to_string(b_num) + " Beat " + std::to_string(bt_num);
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
            float mx = win_mixer_.x;
            float my = win_mixer_.y + DawWindow::kTitleBarHeight;
            float mh = win_mixer_.h - DawWindow::kTitleBarHeight;
            float track_w = 96.0f;
            float gap = 10.0f;
            float ty = my + 30.0f;
            float strip_h = std::max(120.0f, mh - 36.0f);
            float master_x = mx + 12.0f;
            float insert_start_x = master_x + track_w + gap;
            float tx = (dragging_mixer_track_ == 0)
                ? master_x
                : (insert_start_x + static_cast<float>(dragging_mixer_track_ - 1 - mixer_scroll_track_) * (track_w + gap));

            float fader_bot = ty + strip_h - 52.0f;
            RECT fader_rc{static_cast<int>(tx + 33.0f), static_cast<int>(ty + 46.0f),
                          static_cast<int>(tx + 88.0f), static_cast<int>(fader_bot)};
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
        if (!dev) return;

        auto* clipper = dynamic_cast<plugins::AudioClipDevice*>(dev);
        if (clipper) {
            uint8_t m_track = 0;
            auto* ch = engine_.session().project().get_channel(active_editor_channel_);
            if (ch) m_track = ch->settings().mixer_track;

            bool should_close = AudioClipEditor::handle_click(
                hwnd_, clipper, editor_bounds_, x, y,
                clipper_active_tab_, clipper_env_subtab_, m_track,
                dragging_clipper_knob_, drag_clipper_start_y_, drag_clipper_orig_val_,
                status_message_, [this](uint8_t pitch) { audition_note(pitch); });

            if (ch && m_track != ch->settings().mixer_track) {
                ch->settings().mixer_track = m_track;
                selected_mixer_track_ = m_track;
            }

            if (should_close) {
                active_editor_channel_ = 0;
                dragging_clipper_knob_ = ClipperKnobId::None;
                status_message_ = "Closed Instrument Editor";
            }
            return;
        }

        auto* xsynth = dynamic_cast<plugins::XSynthDevice*>(dev);
        if (xsynth) {
            bool should_close = XAudioEditor::handle_xsynth_click(
                xsynth, editor_bounds_, x, y,
                dragging_xsynth_param_idx_, drag_xsynth_start_y_, drag_xsynth_orig_val_,
                status_message_, [this](uint8_t pitch) { audition_note(pitch); });
            if (should_close) {
                active_editor_channel_ = 0;
                dragging_xsynth_param_idx_ = -1;
            }
            return;
        }

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
            update_window_title();
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

    void add_channel_with_uid(const std::string& uid = "core.generator.audioclip", const std::string& name_prefix = "Clipper") {
        std::lock_guard<std::recursive_mutex> lock(engine_.audio_mutex());
        auto& proj = engine_.session().project();
        domain::ChannelSettings s;
        size_t next_idx = proj.channels().size() + 1;
        s.name = name_prefix + " #" + std::to_string(next_idx);
        s.volume = domain::kDefaultChannelVolume;
        s.mixer_track = static_cast<uint8_t>(std::min(size_t(63), next_idx));
        auto new_cid = proj.add_channel(uid, s);

        while (proj.tracks().size() < 4) {
            domain::TrackId tid = static_cast<domain::TrackId>(proj.tracks().size() + 1);
            proj.tracks().emplace_back(tid, "Track " + std::to_string(tid));
        }

        proj.mixer_graph().add_track(s.mixer_track, "Track " + std::to_string(s.mixer_track));

        // Pre-instantiate device on GUI thread so audio thread never has to allocate or load plugin during playback
        engine_.get_or_create_channel_device(new_cid);

        // Auto scroll Channel Rack to make newly added channel visible
        float row_h = 38.0f;
        float rack_avail_h = (win_channel_rack_.h - DawWindow::kTitleBarHeight) - 38.0f;
        int visible_channels = std::max(1, static_cast<int>(rack_avail_h / row_h));
        int max_ch_scroll = std::max(0, static_cast<int>(proj.channels().size()) - visible_channels);
        channel_rack_scroll_ch_ = max_ch_scroll;

        selected_mixer_track_ = s.mixer_track;

        // Automatically open editor for newly added instrument!
        active_editor_channel_ = new_cid;

        status_message_ = "Added channel: " + s.name + " (Mapped to Track " + std::to_string(s.mixer_track) + ")";
    }

    void add_channel() {
        add_channel_with_uid("core.generator.audioclip", "Clipper");
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
    int active_editor_effect_track_{-1};
    int active_editor_effect_slot_{-1};
    RECT effect_editor_bounds_{0, 0, 0, 0};
    int dragging_effect_param_idx_{-1};
    int drag_effect_start_x_{0};
    int drag_effect_start_y_{0};
    float drag_effect_orig_val_{0.0f};
    int effect_editor_scroll_idx_{0};
    int dragging_xsynth_param_idx_{-1};
    int drag_xsynth_start_y_{0};
    float drag_xsynth_orig_val_{0.0f};
    int clipper_active_tab_{0}; // 0 = Sample, 1 = Env/Inst, 2 = Misc
    int clipper_env_subtab_{1}; // 0=Pan, 1=Vol (Default!), 2=ModX, 3=ModY, 4=Pitch
    ClipperKnobId dragging_clipper_knob_{ClipperKnobId::None};
    int drag_clipper_start_y_{0};
    float drag_clipper_orig_val_{0.0f};
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

    // Channel Rack State
    int channel_rack_steps_{64}; // 4 bars = 64 steps by default
    int channel_rack_scroll_ch_{0}; // Vertical scroll offset for channel rows
    int dragging_channel_pan_idx_{-1};
    int dragging_channel_vol_idx_{-1};
    int dragging_channel_target_track_idx_{-1};
    int drag_knob_start_mouse_y_{0};
    float drag_knob_orig_val_{0.0f};
    int drag_target_track_start_mouse_y_{0};
    int drag_target_track_orig_val_{1};

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
