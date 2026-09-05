#pragma once

#include "../time/time_map.hpp"
#include "../sequencing/channel.hpp"
#include "../sequencing/pattern.hpp"
#include "../sequencing/track.hpp"
#include "../mixer/mixer_graph.hpp"
#include "../parameter/automation_curve.hpp"
#include "../ui/ui_state.hpp"
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <concepts>

namespace digidaw::domain {

class Project {
public:
    explicit Project(std::string name = "Untitled")
        : name_(std::move(name)), time_map_(120.0) {
        // Initialize with standard starter structure
        // 1 Default Pattern
        patterns_.emplace_back(1, "Pattern 1");

        // 20 Default Playlist Tracks
        for (TrackId i = 1; i <= 20; ++i) {
            tracks_.emplace_back(i, "Track " + std::to_string(i));
        }

        // Mixer tracks 1..4 in addition to Master (track 0)
        for (MixerTrackId i = 1; i <= 4; ++i) {
            mixer_graph_.add_track(i, "Insert " + std::to_string(i));
        }
    }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    void set_name(std::string name) {
        name_ = std::move(name);
        dirty_ = true;
    }

    [[nodiscard]] TimeMap& time_map() noexcept { return time_map_; }
    [[nodiscard]] const TimeMap& time_map() const noexcept { return time_map_; }

    [[nodiscard]] std::vector<Channel>& channels() noexcept { return channels_; }
    [[nodiscard]] const std::vector<Channel>& channels() const noexcept { return channels_; }

    [[nodiscard]] std::vector<Pattern>& patterns() noexcept { return patterns_; }
    [[nodiscard]] const std::vector<Pattern>& patterns() const noexcept { return patterns_; }

    [[nodiscard]] std::vector<Track>& tracks() noexcept { return tracks_; }
    [[nodiscard]] const std::vector<Track>& tracks() const noexcept { return tracks_; }

    [[nodiscard]] MixerGraph& mixer_graph() noexcept { return mixer_graph_; }
    [[nodiscard]] const MixerGraph& mixer_graph() const noexcept { return mixer_graph_; }

    [[nodiscard]] std::vector<AutomationCurve>& automation_curves() noexcept { return automation_curves_; }
    [[nodiscard]] const std::vector<AutomationCurve>& automation_curves() const noexcept { return automation_curves_; }

    [[nodiscard]] UiState& ui_state() noexcept { return ui_state_; }
    [[nodiscard]] const UiState& ui_state() const noexcept { return ui_state_; }

    [[nodiscard]] bool is_dirty() const noexcept { return dirty_; }
    void mark_clean() noexcept { dirty_ = false; }
    void mark_dirty() noexcept { dirty_ = true; }

    // Channel helpers
    ChannelId add_channel(std::string device_uid, ChannelSettings settings) {
        ChannelId id = static_cast<ChannelId>(channels_.size() + 1);
        channels_.emplace_back(id, std::move(device_uid), std::move(settings));
        dirty_ = true;
        return id;
    }

    [[nodiscard]] Channel* get_channel(ChannelId id) noexcept {
        for (auto& ch : channels_) {
            if (ch.id() == id) return &ch;
        }
        return nullptr;
    }

    // Pattern helpers
    PatternId add_pattern(std::string name = "") {
        PatternId max_id = 0;
        for (const auto& pat : patterns_) {
            max_id = std::max(max_id, pat.id());
        }
        PatternId id = max_id + 1;
        if (name.empty()) {
            name = "Pattern " + std::to_string(id);
        }
        patterns_.emplace_back(id, std::move(name));
        dirty_ = true;
        return id;
    }

    bool remove_pattern(PatternId id) {
        if (patterns_.size() <= 1) return false;
        auto it = std::find_if(patterns_.begin(), patterns_.end(), [id](const Pattern& p) {
            return p.id() == id;
        });
        if (it != patterns_.end()) {
            patterns_.erase(it);
            if (ui_state_.selected_pattern_id == id) {
                ui_state_.selected_pattern_id = patterns_.front().id();
            }
            // Cascade remove clips referencing this deleted pattern from all arrangement tracks
            for (auto& trk : tracks_) {
                auto& clps = trk.clips_mut();
                clps.erase(std::remove_if(clps.begin(), clps.end(), [id](const Clip& c) {
                    return c.pattern_id == id;
                }), clps.end());
            }
            dirty_ = true;
            return true;
        }
        return false;
    }

    [[nodiscard]] Pattern* get_pattern(PatternId id) noexcept {
        for (auto& pat : patterns_) {
            if (pat.id() == id) return &pat;
        }
        return nullptr;
    }

    [[nodiscard]] const Pattern* get_pattern(PatternId id) const noexcept {
        for (const auto& pat : patterns_) {
            if (pat.id() == id) return &pat;
        }
        return nullptr;
    }

    // Playlist Track helpers
    TrackId add_track(std::string name = "") {
        TrackId max_id = 0;
        for (const auto& trk : tracks_) {
            max_id = std::max(max_id, trk.id());
        }
        TrackId id = max_id + 1;
        if (name.empty()) {
            name = "Track " + std::to_string(id);
        }
        tracks_.emplace_back(id, std::move(name));
        dirty_ = true;
        return id;
    }

    bool remove_track(size_t index) {
        if (tracks_.size() <= 1 || index >= tracks_.size()) return false;
        tracks_.erase(tracks_.begin() + index);
        dirty_ = true;
        return true;
    }

    bool remove_track_at(size_t index) {
        return remove_track(index);
    }

    bool remove_track_by_id(TrackId id) {
        if (tracks_.size() <= 1) return false;
        auto it = std::find_if(tracks_.begin(), tracks_.end(), [id](const Track& t) {
            return t.id() == id;
        });
        if (it != tracks_.end()) {
            tracks_.erase(it);
            dirty_ = true;
            return true;
        }
        return false;
    }

    [[nodiscard]] Track* get_track(TrackId id) noexcept {
        for (auto& trk : tracks_) {
            if (trk.id() == id) return &trk;
        }
        return nullptr;
    }

    [[nodiscard]] const Track* get_track(TrackId id) const noexcept {
        for (const auto& trk : tracks_) {
            if (trk.id() == id) return &trk;
        }
        return nullptr;
    }

private:
    std::string name_;
    TimeMap time_map_;
    std::vector<Channel> channels_;
    std::vector<Pattern> patterns_;
    std::vector<Track> tracks_;
    MixerGraph mixer_graph_;
    std::vector<AutomationCurve> automation_curves_;
    UiState ui_state_;
    bool dirty_{false};
};

} // namespace digidaw::domain
