#pragma once

#include <cstdint>
#include <string>

namespace digidaw::domain {

struct WindowRect {
    int32_t x{100};
    int32_t y{100};
    int32_t width{1280};
    int32_t height{800};
    bool maximized{false};
};

struct UiState {
    WindowRect main_window{};
    bool channel_rack_visible{true};
    bool piano_roll_visible{false};
    bool playlist_visible{true};
    bool mixer_visible{false};
    bool browser_visible{true};

    float playlist_zoom_x{1.0f};
    float playlist_zoom_y{1.0f};
    float pianoroll_zoom_x{1.0f};
    float pianoroll_zoom_y{1.0f};

    uint32_t selected_pattern_id{1};
    uint32_t selected_channel_id{0};
    uint32_t selected_mixer_track_id{0};
};

} // namespace digidaw::domain
