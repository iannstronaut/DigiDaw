#pragma once

#include "note.hpp"
#include "../time/time_map.hpp"
#include <algorithm>
#include <cstdint>

namespace digidaw::domain {

class ChannelRackLayout {
public:
    static constexpr float kPadComfortableWidth = 25.0f; // comfortable width ~24px - 26px
    static constexpr int kStepsPerBar = 16;
    static constexpr int kBeatsPerBar = 4;
    static constexpr int kStepsPerBeat = 4;

    [[nodiscard]] static constexpr Tick step_ticks(Tick ppq = DefaultPPQ) noexcept {
        return std::max<Tick>(1, ppq / kStepsPerBeat);
    }

    [[nodiscard]] static constexpr Tick bar_ticks(Tick ppq = DefaultPPQ) noexcept {
        return static_cast<Tick>(kBeatsPerBar) * std::max<Tick>(1, ppq);
    }

    [[nodiscard]] static constexpr int num_bars_for_width(float grid_w) noexcept {
        int bars = static_cast<int>(grid_w / (static_cast<float>(kStepsPerBar) * kPadComfortableWidth));
        return std::max(1, bars);
    }

    [[nodiscard]] static constexpr int num_steps_for_width(float grid_w) noexcept {
        return num_bars_for_width(grid_w) * kStepsPerBar;
    }

    [[nodiscard]] static constexpr float pad_width(float /*grid_w*/ = 0.0f, int /*num_steps*/ = 0) noexcept {
        return kPadComfortableWidth;
    }

    [[nodiscard]] static constexpr float tick_to_x(float grid_x, Tick tick, Tick ppq = DefaultPPQ, float pad_w = kPadComfortableWidth) noexcept {
        return grid_x + (static_cast<float>(tick) / static_cast<float>(step_ticks(ppq))) * pad_w;
    }

    [[nodiscard]] static constexpr float ticks_to_width(Tick length_ticks, Tick ppq = DefaultPPQ, float pad_w = kPadComfortableWidth) noexcept {
        return (static_cast<float>(length_ticks) / static_cast<float>(step_ticks(ppq))) * pad_w;
    }

    [[nodiscard]] static bool is_channel_piano_roll(const NoteSet& notes, Tick ppq = DefaultPPQ) noexcept {
        if (notes.notes().empty()) return false;
        const Tick step_ticks_val = step_ticks(ppq);
        for (const auto& n : notes.notes()) {
            if (n.pitch != 60) return true;
            if (n.start % step_ticks_val != 0) return true;
            if (n.length != step_ticks_val) return true;
        }
        return false;
    }
};

} // namespace digidaw::domain
