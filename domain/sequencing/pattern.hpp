#pragma once

#include "note.hpp"
#include <unordered_map>
#include <string>

namespace digidaw::domain {

using PatternId = uint32_t;

class Pattern {
public:
    Pattern(PatternId id, std::string name)
        : id_(id), name_(std::move(name)) {}

    [[nodiscard]] PatternId id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    void set_name(std::string name) { name_ = std::move(name); }

    [[nodiscard]] NoteSet& get_or_create_channel_notes(ChannelId ch) {
        return channel_notes_[ch];
    }

    [[nodiscard]] const NoteSet* get_channel_notes(ChannelId ch) const noexcept {
        auto it = channel_notes_.find(ch);
        if (it != channel_notes_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    [[nodiscard]] const std::unordered_map<ChannelId, NoteSet>& all_notes() const noexcept {
        return channel_notes_;
    }

    [[nodiscard]] const std::unordered_map<ChannelId, NoteSet>& channel_notes() const noexcept {
        return channel_notes_;
    }

    [[nodiscard]] std::unordered_map<ChannelId, NoteSet>& channel_notes() noexcept {
        return channel_notes_;
    }

    [[nodiscard]] Tick length_ticks(Tick ppq = DefaultPPQ) const noexcept {
        const Tick safe_ppq = std::max<Tick>(1, ppq);
        const Tick bar_ticks = 4 * safe_ppq;
        Tick max_tick = bar_ticks; // Minimum 1 bar
        for (const auto& [ch, note_set] : channel_notes_) {
            for (const auto& n : note_set.notes()) {
                max_tick = std::max(max_tick, n.start + n.length);
            }
        }
        // Round up to nearest bar
        return ((max_tick + bar_ticks - 1) / bar_ticks) * bar_ticks;
    }

private:
    PatternId id_{1};
    std::string name_{"Pattern 1"};
    std::unordered_map<ChannelId, NoteSet> channel_notes_;
};

} // namespace digidaw::domain
