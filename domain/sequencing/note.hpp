#pragma once

#include "../time/time_map.hpp"
#include <cstdint>
#include <vector>
#include <algorithm>
#include <string>

namespace digidaw::domain {

using ChannelId = uint32_t;
using ColorId = uint32_t;

struct Note {
    Tick start{0};
    Tick length{DefaultPPQ / 4}; // Default 16th note
    uint8_t pitch{60};          // MIDI standard: 60 = Middle C (C5/C4)
    uint8_t velocity{100};       // 0..127
    uint8_t channel_in_pattern{0};
    ColorId color{0};

    [[nodiscard]] constexpr bool operator==(const Note& other) const noexcept {
        return start == other.start &&
               length == other.length &&
               pitch == other.pitch &&
               velocity == other.velocity &&
               channel_in_pattern == other.channel_in_pattern;
    }

    [[nodiscard]] constexpr bool operator<(const Note& other) const noexcept {
        if (start != other.start) return start < other.start;
        if (pitch != other.pitch) return pitch < other.pitch;
        return channel_in_pattern < other.channel_in_pattern;
    }
};

class NoteSet {
public:
    void add_note(Note note) {
        if (note.pitch > 127) note.pitch = 127;
        if (note.velocity > 127) note.velocity = 127;
        if (note.length <= 0) note.length = 1;

        auto it = std::lower_bound(notes_.begin(), notes_.end(), note);
        if (it != notes_.end() && *it == note) {
            *it = note; // Update existing
        } else {
            notes_.insert(it, note);
        }
    }

    bool remove_note(Tick start, uint8_t pitch) {
        auto it = std::find_if(notes_.begin(), notes_.end(), [start, pitch](const Note& n) {
            return n.start == start && n.pitch == pitch;
        });
        if (it != notes_.end()) {
            notes_.erase(it);
            return true;
        }
        return false;
    }

    void clear() noexcept {
        notes_.clear();
    }

    [[nodiscard]] const std::vector<Note>& notes() const noexcept {
        return notes_;
    }

    [[nodiscard]] std::vector<Note> get_notes_in_range(Tick from, Tick to) const {
        std::vector<Note> result;
        for (const auto& n : notes_) {
            if (n.start >= from && n.start < to) {
                result.push_back(n);
            }
        }
        return result;
    }

    [[nodiscard]] bool has_note_at_step(size_t step, Tick ppq = DefaultPPQ, uint8_t pitch = 60) const noexcept {
        const Tick step_ticks = ppq / 4; // 16th note
        const Tick target_tick = static_cast<Tick>(step) * step_ticks;
        for (const auto& n : notes_) {
            if (n.pitch == pitch && n.start == target_tick) {
                return true;
            }
        }
        return false;
    }

    void toggle_step(size_t step, Tick ppq = DefaultPPQ, uint8_t pitch = 60, uint8_t vel = 100) {
        const Tick step_ticks = ppq / 4;
        const Tick target_tick = static_cast<Tick>(step) * step_ticks;
        if (!remove_note(target_tick, pitch)) {
            add_note(Note{target_tick, step_ticks, pitch, vel, 0, 0});
        }
    }

private:
    std::vector<Note> notes_;
};

} // namespace digidaw::domain
