#pragma once

#include <windows.h>

namespace digidaw::adapters::gui {

struct Theme {
    // Backgrounds
    COLORREF bg_main          = RGB(20, 22, 26);
    COLORREF bg_panel         = RGB(30, 33, 40);
    COLORREF bg_header        = RGB(25, 27, 33);
    COLORREF bg_card          = RGB(38, 42, 52);
    COLORREF bg_input         = RGB(15, 17, 21);

    // Borders & Dividers
    COLORREF border_dark      = RGB(48, 53, 66);
    COLORREF border_light     = RGB(65, 72, 90);

    // Accents
    COLORREF accent_orange    = RGB(255, 140, 20); // Active glowing elements
    COLORREF accent_amber     = RGB(255, 185, 35);
    COLORREF accent_cyan      = RGB(0, 215, 255);  // Indicators & secondary active
    COLORREF accent_green     = RGB(46, 213, 115); // Play active / meters normal
    COLORREF accent_red       = RGB(255, 71, 87);  // Record / clipping meter

    // Step Sequencer Buttons
    COLORREF step_off_light   = RGB(55, 60, 75);   // Beats 1, 5, 9, 13
    COLORREF step_off_dark    = RGB(42, 46, 58);   // Other steps
    COLORREF step_on          = RGB(255, 140, 20); // Active step note
    COLORREF step_playhead    = RGB(0, 215, 255);  // Step current playhead cursor

    // Typography
    COLORREF text_primary     = RGB(240, 243, 250);
    COLORREF text_secondary   = RGB(155, 165, 185);
    COLORREF text_dim         = RGB(100, 108, 125);
    COLORREF text_accent      = RGB(255, 185, 35);

    // Level Meters
    COLORREF meter_green      = RGB(46, 213, 115);
    COLORREF meter_yellow     = RGB(255, 215, 0);
    COLORREF meter_red        = RGB(255, 60, 60);
    COLORREF meter_bg         = RGB(18, 20, 25);
};

inline const Theme& get_theme() {
    static Theme theme;
    return theme;
}

} // namespace digidaw::adapters::gui
