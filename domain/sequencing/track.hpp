#pragma once

#include "pattern.hpp"
#include <vector>
#include <string>
#include <algorithm>

namespace digidaw::domain {

using TrackId = uint32_t;

struct Clip {
    PatternId pattern_id{1};
    Tick start{0};
    Tick length{DefaultPPQ * 4}; // Default 1 bar
    bool muted{false};

    [[nodiscard]] constexpr Tick end() const noexcept {
        return start + length;
    }

    [[nodiscard]] constexpr bool contains_tick(Tick t) const noexcept {
        return t >= start && t < end();
    }
};

class Track {
public:
    Track(TrackId id, std::string name)
        : id_(id), name_(std::move(name)) {}

    [[nodiscard]] TrackId id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    void set_name(std::string name) { name_ = std::move(name); }

    [[nodiscard]] bool muted() const noexcept { return muted_; }
    [[nodiscard]] bool is_muted() const noexcept { return muted_; }
    void set_muted(bool m) noexcept { muted_ = m; }

    [[nodiscard]] bool solo() const noexcept { return solo_; }
    void set_solo(bool s) noexcept { solo_ = s; }

    void add_clip(Clip clip) {
        clips_.push_back(clip);
        std::sort(clips_.begin(), clips_.end(), [](const Clip& a, const Clip& b) {
            return a.start < b.start;
        });
    }

    void remove_clip(size_t index) {
        if (index < clips_.size()) {
            clips_.erase(clips_.begin() + index);
        }
    }

    bool remove_clip_at(Tick start_tick) {
        auto it = std::find_if(clips_.begin(), clips_.end(), [start_tick](const Clip& c) {
            return c.start == start_tick;
        });
        if (it != clips_.end()) {
            clips_.erase(it);
            return true;
        }
        return false;
    }

    bool remove_clip_containing(Tick t) {
        auto it = std::find_if(clips_.begin(), clips_.end(), [t](const Clip& c) {
            return c.contains_tick(t);
        });
        if (it != clips_.end()) {
            clips_.erase(it);
            return true;
        }
        return false;
    }

    [[nodiscard]] bool has_clip_at(Tick start_tick) const noexcept {
        for (const auto& c : clips_) {
            if (c.start == start_tick) return true;
        }
        return false;
    }

    [[nodiscard]] const Clip* get_clip_at_tick(Tick t) const noexcept {
        for (const auto& c : clips_) {
            if (c.contains_tick(t)) return &c;
        }
        return nullptr;
    }

    [[nodiscard]] Clip* get_clip_at_tick_mut(Tick t) noexcept {
        for (auto& c : clips_) {
            if (c.contains_tick(t)) return &c;
        }
        return nullptr;
    }

    [[nodiscard]] Clip* get_clip_at_start_mut(Tick start_tick) noexcept {
        for (auto& c : clips_) {
            if (c.start == start_tick) return &c;
        }
        return nullptr;
    }

    void sort_clips() {
        std::sort(clips_.begin(), clips_.end(), [](const Clip& a, const Clip& b) {
            return a.start < b.start;
        });
    }

    [[nodiscard]] std::vector<Clip>& clips_mut() noexcept {
        return clips_;
    }

    void clear_clips() noexcept {
        clips_.clear();
    }

    [[nodiscard]] const std::vector<Clip>& clips() const noexcept {
        return clips_;
    }

    [[nodiscard]] std::vector<Clip> get_clips_at(Tick t) const {
        std::vector<Clip> active;
        if (muted_) return active;
        for (const auto& clip : clips_) {
            if (!clip.muted && clip.contains_tick(t)) {
                active.push_back(clip);
            }
        }
        return active;
    }

    [[nodiscard]] Tick total_length_ticks() const noexcept {
        Tick max_t = 0;
        for (const auto& clip : clips_) {
            max_t = std::max(max_t, clip.end());
        }
        return max_t;
    }

private:
    TrackId id_{1};
    std::string name_{"Track 1"};
    bool muted_{false};
    bool solo_{false};
    std::vector<Clip> clips_;
};

} // namespace digidaw::domain
