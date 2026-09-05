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
    Transport() = default;
    explicit Transport(domain::Project& project)
        : project_(&project), state_(TransportState::Stopped), mode_(PlaybackMode::Pattern) {}

    void set_project(domain::Project* p) noexcept { project_ = p; }

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
        return project_ ? project_->time_map().tick_to_seconds(current_tick_) : 0.0;
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
        if (!project_ || state_ != TransportState::Playing || frames == 0 || sample_rate <= 0.0) {
            return result;
        }

        const auto& time_map = project_->time_map();
        const double bpm = time_map.get_bpm_at(current_tick_);
        const double ticks_per_sec = (bpm * static_cast<double>(time_map.ppq())) / 60.0;
        const double block_duration_sec = static_cast<double>(frames) / sample_rate;
        const auto delta_ticks = static_cast<domain::Tick>(std::round(block_duration_sec * ticks_per_sec));

        // Determine loop boundaries
        domain::Tick loop_start = 0;
        domain::Tick loop_end = 0;
        bool is_looping = false;

        if (mode_ == PlaybackMode::Pattern) {
            auto* pat = project_->get_pattern(project_->ui_state().selected_pattern_id);
            if (pat) {
                loop_end = pat->length_ticks(time_map.ppq());
            } else {
                loop_end = 4 * time_map.ppq();
            }
            loop_start = 0;
            is_looping = (loop_end > 0);
        } else {
            // Song / Arranger mode: loop only when loop_enabled_ is explicitly activated
            if (loop_enabled_ && loop_end_ > loop_start_) {
                loop_start = loop_start_;
                loop_end = loop_end_;
                is_looping = true;
            } else {
                is_looping = false;
            }
        }

        struct Interval {
            domain::Tick start;
            domain::Tick end;
            bool is_end_of_loop;
        };
        std::vector<Interval> intervals;

        if (is_looping && loop_end > loop_start) {
            const domain::Tick loop_len = loop_end - loop_start;
            if (current_tick_ < loop_start || current_tick_ >= loop_end) {
                current_tick_ = loop_start + ((current_tick_ - loop_start) % loop_len);
                if (current_tick_ < loop_start) current_tick_ += loop_len;
            }

            const domain::Tick next_tick = current_tick_ + delta_ticks;
            if (next_tick < loop_end) {
                intervals.push_back({current_tick_, next_tick, false});
                current_tick_ = next_tick;
            } else {
                // Crosses loop boundary!
                intervals.push_back({current_tick_, loop_end, true});
                domain::Tick wrapped_ticks = (next_tick - loop_end) % loop_len;
                intervals.push_back({loop_start, loop_start + wrapped_ticks, false});
                current_tick_ = loop_start + wrapped_ticks;
            }
        } else {
            intervals.push_back({current_tick_, current_tick_ + delta_ticks, false});
            current_tick_ += delta_ticks;
        }

        std::unordered_map<domain::ChannelId, std::vector<domain::MidiEvent>> ch_map;

        for (const auto& span : intervals) {
            const domain::Tick s_start = span.start;
            const domain::Tick s_end = span.end;

            if (mode_ == PlaybackMode::Pattern) {
                auto* pat = project_->get_pattern(project_->ui_state().selected_pattern_id);
                if (pat) {
                    for (const auto& [ch_id, note_set] : pat->all_notes()) {
                        auto& ev_list = ch_map[ch_id];
                        for (const auto& note : note_set.notes()) {
                            // Note On
                            if (note.start >= s_start && note.start < s_end) {
                                ev_list.push_back(domain::MidiEvent::make_note_on(
                                    note.start, 0, note.pitch, note.velocity));
                            }
                            // Note Off
                            const domain::Tick note_end = note.start + note.length;
                            if (note_end >= s_start && note_end < s_end) {
                                ev_list.push_back(domain::MidiEvent::make_note_off(
                                    note_end, 0, note.pitch));
                            } else if (span.is_end_of_loop && note.start < loop_end && note_end >= loop_end) {
                                // Cut off sustained notes reaching/exceeding loop end so they don't hang into next loop!
                                ev_list.push_back(domain::MidiEvent::make_note_off(
                                    loop_end, 0, note.pitch));
                            }
                        }
                    }
                }
            } else {
                // Song / Arranger mode: scan all tracks and active placement clips
                bool any_solo = false;
                for (const auto& trk : project_->tracks()) {
                    if (trk.solo()) { any_solo = true; break; }
                }

                for (const auto& track : project_->tracks()) {
                    // Resource optimization: early-skip empty arrangement tracks
                    if (track.clips().empty()) continue;
                    if (any_solo ? !track.solo() : track.is_muted()) continue;

                    // Resource optimization: early-skip arrangement tracks with no unmuted clips in current block
                    bool has_clip_in_block = false;
                    for (const auto& clip : track.clips()) {
                        if (!clip.muted && clip.end() >= s_start && clip.start < s_end) {
                            has_clip_in_block = true;
                            break;
                        }
                    }
                    if (!has_clip_in_block) continue;

                    for (const auto& clip : track.clips()) {
                        if (clip.muted) continue;
                        auto* pat = project_->get_pattern(clip.pattern_id);
                        if (!pat) continue;

                        if (clip.end() < s_start || clip.start >= s_end) continue;

                        // Non-looping pattern clip extension:
                        // Pattern notes trigger only once at defined offset (n.start < clip.length).
                        // The extended portion beyond pattern notes remains completely silent.
                        for (const auto& [ch_id, note_set] : pat->all_notes()) {
                            auto& ev_list = ch_map[ch_id];

                            for (const auto& note : note_set.notes()) {
                                if (note.start >= clip.length) continue;
                                const domain::Tick abs_note_start = clip.start + note.start;
                                const domain::Tick abs_note_end = std::min(clip.end(), abs_note_start + note.length);

                                if (abs_note_start >= s_start && abs_note_start < s_end) {
                                    ev_list.push_back(domain::MidiEvent::make_note_on(
                                        abs_note_start, 0, note.pitch, note.velocity));
                                }
                                if (abs_note_end >= s_start && abs_note_end < s_end) {
                                    ev_list.push_back(domain::MidiEvent::make_note_off(
                                        abs_note_end, 0, note.pitch));
                                } else if (span.is_end_of_loop && abs_note_start < loop_end && abs_note_end >= loop_end) {
                                    ev_list.push_back(domain::MidiEvent::make_note_off(
                                        loop_end, 0, note.pitch));
                                }
                            }
                        }
                    }
                }
            }
        }

        for (auto& [cid, evs] : ch_map) {
            if (!evs.empty()) {
                result.push_back(ScheduledChannelEvents{cid, std::move(evs)});
            }
        }

        return result;
    }

private:
    domain::Project* project_{nullptr};
    TransportState state_{TransportState::Stopped};
    PlaybackMode mode_{PlaybackMode::Pattern};
    domain::Tick current_tick_{0};
    domain::Tick loop_start_{0};
    domain::Tick loop_end_{0};
    bool loop_enabled_{false};
};

} // namespace digidaw::app
