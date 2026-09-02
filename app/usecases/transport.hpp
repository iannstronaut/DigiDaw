#pragma once

#include "../../domain/time/time_map.hpp"
#include "../../domain/buffer/audio_buffer.hpp"
#include "../../domain/project/project.hpp"
#include <atomic>
#include <vector>
#include <cmath>

namespace digidaw::app {

enum class TransportState : uint8_t {
    Stopped,
    Playing,
    Paused,
    Recording
};

enum class PlaybackMode : uint8_t {
    Pattern,
    Song
};

class Transport {
public:
    explicit Transport(domain::Project& project)
        : project_(project), state_(TransportState::Stopped), mode_(PlaybackMode::Pattern) {}

    void play() noexcept {
        state_ = TransportState::Playing;
    }

    void pause() noexcept {
        if (state_ == TransportState::Playing) {
            state_ = TransportState::Paused;
        } else if (state_ == TransportState::Paused) {
            state_ = TransportState::Playing;
        }
    }

    void stop() noexcept {
        state_ = TransportState::Stopped;
        current_tick_ = 0;
    }

    void seek(domain::Tick tick) noexcept {
        current_tick_ = (tick >= 0) ? tick : 0;
    }

    [[nodiscard]] TransportState state() const noexcept { return state_; }
    [[nodiscard]] bool is_playing() const noexcept { return state_ == TransportState::Playing; }

    [[nodiscard]] PlaybackMode mode() const noexcept { return mode_; }
    void set_mode(PlaybackMode m) noexcept { mode_ = m; }

    [[nodiscard]] domain::Tick current_tick() const noexcept { return current_tick_; }
    [[nodiscard]] double current_seconds() const noexcept {
        return project_.time_map().tick_to_seconds(current_tick_);
    }

    void set_loop(domain::Tick start, domain::Tick end, bool enabled = true) noexcept {
        loop_start_ = start;
        loop_end_ = (end > start) ? end : start + domain::DefaultPPQ * 4;
        loop_enabled_ = enabled;
    }

    [[nodiscard]] bool loop_enabled() const noexcept { return loop_enabled_; }

    // Advance transport by frames at given sample rate and collect MIDI events to trigger
    struct ScheduledChannelEvents {
        domain::ChannelId channel_id;
        std::vector<domain::MidiEvent> events;
    };

    std::vector<ScheduledChannelEvents> advance_block(size_t frames, double sample_rate) {
        std::vector<ScheduledChannelEvents> result;
        if (state_ != TransportState::Playing || frames == 0 || sample_rate <= 0.0) {
            return result;
        }

        const auto& time_map = project_.time_map();
        const double bpm = time_map.get_bpm_at(current_tick_);
        const double ticks_per_sec = (bpm * static_cast<double>(time_map.ppq())) / 60.0;
        const double block_duration_sec = static_cast<double>(frames) / sample_rate;
        const auto delta_ticks = static_cast<domain::Tick>(std::round(block_duration_sec * ticks_per_sec));

        const domain::Tick start_tick = current_tick_;
        domain::Tick end_tick = start_tick + delta_ticks;

        // Loop boundaries check
        domain::Tick loop_len = 0;
        if (mode_ == PlaybackMode::Pattern) {
            auto* pat = project_.get_pattern(project_.ui_state().selected_pattern_id);
            if (pat) {
                loop_len = pat->length_ticks(time_map.ppq());
            } else {
                loop_len = 4 * time_map.ppq();
            }
        } else if (loop_enabled_ && loop_end_ > loop_start_) {
            loop_len = loop_end_ - loop_start_;
        }

        // Collect events
        if (mode_ == PlaybackMode::Pattern) {
            auto* pat = project_.get_pattern(project_.ui_state().selected_pattern_id);
            if (pat) {
                for (const auto& [ch_id, note_set] : pat->all_notes()) {
                    ScheduledChannelEvents ch_ev{ch_id, {}};
                    for (const auto& note : note_set.notes()) {
                        // Check Note On
                        if (note.start >= start_tick && note.start < end_tick) {
                            ch_ev.events.push_back(domain::MidiEvent::make_note_on(
                                note.start, 0, note.pitch, note.velocity));
                        }
                        // Check Note Off
                        const domain::Tick note_end = note.start + note.length;
                        if (note_end >= start_tick && note_end < end_tick) {
                            ch_ev.events.push_back(domain::MidiEvent::make_note_off(
                                note_end, 0, note.pitch));
                        }
                    }
                    if (!ch_ev.events.empty()) {
                        result.push_back(std::move(ch_ev));
                    }
                }
            }
        } else {
            // Song mode: scan active clips on tracks
            for (const auto& track : project_.tracks()) {
                if (track.muted()) continue;
                for (const auto& clip : track.clips()) {
                    if (clip.muted) continue;
                    auto* pat = project_.get_pattern(clip.pattern_id);
                    if (!pat) continue;

                    // Does this clip overlap with the current block?
                    if (clip.end() <= start_tick || clip.start >= end_tick) continue;

                    for (const auto& [ch_id, note_set] : pat->all_notes()) {
                        ScheduledChannelEvents ch_ev{ch_id, {}};
                        for (const auto& note : note_set.notes()) {
                            const domain::Tick abs_note_start = clip.start + note.start;
                            const domain::Tick abs_note_end = abs_note_start + note.length;

                            if (abs_note_start >= start_tick && abs_note_start < end_tick) {
                                ch_ev.events.push_back(domain::MidiEvent::make_note_on(
                                    abs_note_start, 0, note.pitch, note.velocity));
                            }
                            if (abs_note_end >= start_tick && abs_note_end < end_tick) {
                                ch_ev.events.push_back(domain::MidiEvent::make_note_off(
                                    abs_note_end, 0, note.pitch));
                            }
                        }
                        if (!ch_ev.events.empty()) {
                            result.push_back(std::move(ch_ev));
                        }
                    }
                }
            }
        }

        // Advance playhead & handle looping
        if (loop_len > 0) {
            current_tick_ = (start_tick + delta_ticks) % loop_len;
        } else {
            current_tick_ = end_tick;
        }

        return result;
    }

private:
    domain::Project& project_;
    TransportState state_{TransportState::Stopped};
    PlaybackMode mode_{PlaybackMode::Pattern};
    domain::Tick current_tick_{0};
    domain::Tick loop_start_{0};
    domain::Tick loop_end_{0};
    bool loop_enabled_{false};
};

} // namespace digidaw::app
