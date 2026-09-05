#pragma once

#include <windows.h>

namespace digidaw::adapters::gui {

enum class SvgIconType {
    Play,
    Pause,
    Stop,
    ChannelRack,
    Playlist,
    PianoRoll,
    Inspector,
    TrackFx,
    Mixer,
    Magnet,
    Save,
    Export
};

struct Theme {
    // 0. DESIGN.md Color Tokens
    // Backgrounds (Near-black layered surfaces)
    COLORREF bg_app           = RGB(9, 10, 12);     // #090a0c
    COLORREF bg_surface       = RGB(13, 15, 17);    // #0d0f11
    COLORREF bg_surface_2     = RGB(17, 19, 22);    // #111316
    COLORREF bg_elevated      = RGB(21, 24, 27);    // #15181b
    COLORREF bg_control       = RGB(25, 28, 32);    // #191c20

    // Compatibility aliases
    COLORREF bg_main          = RGB(9, 10, 12);     // #090a0c
    COLORREF bg_panel         = RGB(13, 15, 17);    // #0d0f11
    COLORREF bg_header        = RGB(17, 19, 22);    // #111316
    COLORREF bg_card          = RGB(21, 24, 27);    // #15181b
    COLORREF bg_input         = RGB(25, 28, 32);    // #191c20

    // Borders & Dividers (Low-contrast 1px hairlines)
    COLORREF border_subtle    = RGB(32, 35, 40);    // #202328
    COLORREF border_default   = RGB(42, 46, 52);    // #2a2e34
    COLORREF border_strong    = RGB(52, 56, 62);    // #34383e
    COLORREF border_dark      = RGB(32, 35, 40);    // #202328
    COLORREF border_light     = RGB(42, 46, 52);    // #2a2e34

    // Primary Accents (Electric violet / purple signature)
    COLORREF accent           = RGB(168, 85, 247);  // #a855f7
    COLORREF accent_bright    = RGB(183, 102, 255); // #b766ff
    COLORREF accent_deep      = RGB(124, 58, 237);  // #7c3aed

    // Compatibility accent aliases
    COLORREF accent_orange    = RGB(168, 85, 247);  // Active signature accent
    COLORREF accent_amber     = RGB(230, 184, 74);  // #e6b84a (warning)
    COLORREF accent_cyan      = RGB(183, 102, 255); // #b766ff (bright violet)
    COLORREF accent_green     = RGB(123, 227, 106); // #7be36a (success)
    COLORREF accent_red       = RGB(225, 91, 100);  // #e15b64 (danger)

    // Semantic
    COLORREF success          = RGB(123, 227, 106); // #7be36a
    COLORREF warning          = RGB(230, 184, 74);  // #e6b84a
    COLORREF danger           = RGB(225, 91, 100);  // #e15b64

    // Creative / Track Colors (Muted / translucent tints)
    COLORREF track_melody     = RGB(216, 117, 98);  // #d87562
    COLORREF track_chords     = RGB(214, 169, 54);  // #d6a936
    COLORREF track_drums      = RGB(155, 108, 219); // #9b6cdb

    // Step Sequencer Buttons
    COLORREF step_off_light   = RGB(32, 35, 40);    // Beats 1, 5, 9, 13
    COLORREF step_off_dark    = RGB(25, 28, 32);    // Other steps
    COLORREF step_on          = RGB(168, 85, 247);  // Active step note (violet)
    COLORREF step_playhead    = RGB(183, 102, 255); // Step current playhead cursor
    COLORREF note_silver      = RGB(220, 225, 235); // Miniature notes in channel rack

    // Typography
    COLORREF text_primary     = RGB(232, 232, 234); // #e8e8ea
    COLORREF text_secondary   = RGB(154, 157, 163); // #9a9da3
    COLORREF text_muted       = RGB(98, 102, 109);  // #62666d
    COLORREF text_dim         = RGB(98, 102, 109);  // #62666d
    COLORREF text_disabled    = RGB(68, 72, 78);    // #44484e
    COLORREF text_accent      = RGB(183, 102, 255); // #b766ff

    // Level Meters
    COLORREF meter_green      = RGB(123, 227, 106); // #7be36a
    COLORREF meter_yellow     = RGB(230, 184, 74);  // #e6b84a
    COLORREF meter_red        = RGB(225, 91, 100);  // #e15b64
    COLORREF meter_bg         = RGB(9, 10, 12);     // #090a0c
};

inline const Theme& get_theme() {
    static Theme theme;
    return theme;
}

} // namespace digidaw::adapters::gui
