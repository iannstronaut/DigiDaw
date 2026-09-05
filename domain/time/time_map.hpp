#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>
#include <cmath>

namespace digidaw::domain {

using Tick = int64_t;
using Bar = int32_t;

constexpr Tick DefaultPPQ = 960; // 960 pulses per quarter note (standard FL Studio PPQ)

struct TempoEvent {
    Tick tick{0};
    double bpm{120.0};
    double time_in_seconds{0.0}; // Pre-computed accumulated time at this tick
};

struct TimeSignatureEvent {
    Tick tick{0};
    uint8_t numerator{4};
    uint8_t denominator{4};
};

class TimeMap {
public:
    explicit TimeMap(double initial_bpm = 120.0, Tick ppq = DefaultPPQ)
        : ppq_(ppq > 0 ? ppq : DefaultPPQ) {
        set_tempo(initial_bpm);
    }

    [[nodiscard]] Tick ppq() const noexcept { return ppq_; }
    [[nodiscard]] double bpm() const noexcept { return get_bpm_at(0); }

    void set_tempo(double bpm) {
        tempo_events_.clear();
        add_tempo_point(0, bpm);
    }

    void add_tempo_point(Tick tick, double bpm) {
        if (bpm < 10.0) bpm = 10.0;
        if (bpm > 999.0) bpm = 999.0;

        // Remove any existing tempo point at this exact tick
        tempo_events_.erase(
            std::remove_if(tempo_events_.begin(), tempo_events_.end(),
                           [tick](const TempoEvent& e) { return e.tick == tick; }),
            tempo_events_.end());

        tempo_events_.push_back(TempoEvent{tick, bpm, 0.0});
        std::sort(tempo_events_.begin(), tempo_events_.end(),
                  [](const TempoEvent& a, const TempoEvent& b) { return a.tick < b.tick; });

        recompute_accumulated_times();
    }

    void add_time_signature(Tick tick, uint8_t numerator, uint8_t denominator) {
        if (numerator == 0) numerator = 4;
        if (denominator == 0) denominator = 4;

        time_sig_events_.erase(
            std::remove_if(time_sig_events_.begin(), time_sig_events_.end(),
                           [tick](const TimeSignatureEvent& e) { return e.tick == tick; }),
            time_sig_events_.end());

        time_sig_events_.push_back(TimeSignatureEvent{tick, numerator, denominator});
        std::sort(time_sig_events_.begin(), time_sig_events_.end(),
                  [](const TimeSignatureEvent& a, const TimeSignatureEvent& b) { return a.tick < b.tick; });
    }

    [[nodiscard]] double tick_to_seconds(Tick t) const noexcept {
        if (t <= 0 || tempo_events_.empty()) {
            return 0.0;
        }

        // Binary search for the last tempo event with tick <= t
        auto it = std::upper_bound(
            tempo_events_.begin(), tempo_events_.end(), t,
            [](Tick val, const TempoEvent& e) { return val < e.tick; });

        if (it != tempo_events_.begin()) {
            --it;
        }

        const double seconds_per_tick = 60.0 / (it->bpm * static_cast<double>(ppq_));
        const Tick delta_ticks = t - it->tick;
        return it->time_in_seconds + (delta_ticks * seconds_per_tick);
    }

    [[nodiscard]] Tick seconds_to_tick(double s) const noexcept {
        if (s <= 0.0 || tempo_events_.empty()) {
            return 0;
        }

        // Binary search for the last tempo event with time_in_seconds <= s
        auto it = std::upper_bound(
            tempo_events_.begin(), tempo_events_.end(), s,
            [](double val, const TempoEvent& e) { return val < e.time_in_seconds; });

        if (it != tempo_events_.begin()) {
            --it;
        }

        const double ticks_per_second = (it->bpm * static_cast<double>(ppq_)) / 60.0;
        const double delta_seconds = s - it->time_in_seconds;
        return it->tick + static_cast<Tick>(std::round(delta_seconds * ticks_per_second));
    }

    [[nodiscard]] double get_bpm_at(Tick t) const noexcept {
        if (tempo_events_.empty()) return 120.0;

        auto it = std::upper_bound(
            tempo_events_.begin(), tempo_events_.end(), t,
            [](Tick val, const TempoEvent& e) { return val < e.tick; });

        if (it != tempo_events_.begin()) {
            --it;
        }
        return it->bpm;
    }

    [[nodiscard]] Tick bar_to_tick(Bar bar) const noexcept {
        // Standard 4/4: 1 bar = 4 beats = 4 * ppq ticks
        if (bar <= 0) return 0;
        return static_cast<Tick>(bar) * 4 * ppq_;
    }

    [[nodiscard]] Bar tick_to_bar(Tick tick) const noexcept {
        if (tick <= 0) return 0;
        return static_cast<Bar>(tick / (4 * ppq_));
    }

    [[nodiscard]] const std::vector<TempoEvent>& tempo_events() const noexcept {
        return tempo_events_;
    }

private:
    void recompute_accumulated_times() {
        if (tempo_events_.empty()) return;

        tempo_events_[0].time_in_seconds = 0.0;
        for (size_t i = 1; i < tempo_events_.size(); ++i) {
            const double prev_bpm = tempo_events_[i - 1].bpm;
            const Tick delta_ticks = tempo_events_[i].tick - tempo_events_[i - 1].tick;
            const double seconds_per_tick = 60.0 / (prev_bpm * static_cast<double>(ppq_));
            tempo_events_[i].time_in_seconds = tempo_events_[i - 1].time_in_seconds + (delta_ticks * seconds_per_tick);
        }
    }

    Tick ppq_{DefaultPPQ};
    std::vector<TempoEvent> tempo_events_;
    std::vector<TimeSignatureEvent> time_sig_events_;
};

} // namespace digidaw::domain
