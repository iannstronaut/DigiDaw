#pragma once

#include "mixer_track.hpp"
#include "../buffer/audio_buffer.hpp"
#include "../common/result.hpp"
#include <unordered_map>
#include <vector>
#include <queue>
#include <algorithm>
#include <memory>
#include <set>

namespace digidaw::domain {

class MixerGraph {
public:
    MixerGraph() {
        // Track 0 is always the Master Track
        tracks_.emplace(MasterTrackId, MixerTrack{MasterTrackId, "Master"});
        track_buffers_[MasterTrackId] = std::make_unique<OwningAudioBuffer>(max_block_size_);
        rebuild_topological_order();
    }

    void add_track(MixerTrackId id, std::string name) {
        if (id == MasterTrackId) return; // Master already exists
        if (tracks_.find(id) == tracks_.end()) {
            MixerTrack track{id, std::move(name)};
            // By default, route to Master
            track.add_send(MasterTrackId, 1.0f, true);
            tracks_.emplace(id, std::move(track));
            track_buffers_[id] = std::make_unique<OwningAudioBuffer>(max_block_size_);
            rebuild_topological_order();
        }
    }

    [[nodiscard]] MixerTrack* get_track(MixerTrackId id) noexcept {
        auto it = tracks_.find(id);
        if (it != tracks_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    [[nodiscard]] const MixerTrack* get_track(MixerTrackId id) const noexcept {
        auto it = tracks_.find(id);
        if (it != tracks_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    [[nodiscard]] const std::unordered_map<MixerTrackId, MixerTrack>& tracks() const noexcept {
        return tracks_;
    }

    Result<void> connect_send(MixerTrackId from, MixerTrackId to, float amount = 1.0f, bool post_fader = true) {
        if (from == to) {
            return Result<void>(ErrorCode::GraphCycleDetected);
        }
        if (tracks_.find(from) == tracks_.end() || tracks_.find(to) == tracks_.end()) {
            return Result<void>(ErrorCode::InvalidArgument);
        }

        // Cycle check: can 'to' reach 'from'?
        if (can_reach(to, from)) {
            return Result<void>(ErrorCode::GraphCycleDetected);
        }

        tracks_.at(from).add_send(to, amount, post_fader);
        rebuild_topological_order();
        return Result<void>::ok();
    }

    void disconnect_send(MixerTrackId from, MixerTrackId to) {
        auto it = tracks_.find(from);
        if (it != tracks_.end()) {
            it->second.remove_send(to);
            rebuild_topological_order();
        }
    }

    void prepare(double sample_rate, size_t max_block_size) {
        sample_rate_ = sample_rate;
        max_block_size_ = max_block_size;

        // Allocate scratch buffers per track
        track_buffers_.clear();
        for (const auto& [id, track] : tracks_) {
            track_buffers_[id] = std::make_unique<OwningAudioBuffer>(max_block_size);
        }

        // Prepare devices in all tracks
        for (auto& [id, track] : tracks_) {
            for (auto& slot : track.inserts()) {
                if (slot.device) {
                    slot.device->prepare(sample_rate, max_block_size);
                }
            }
        }
    }

    // RT-safe: Preallocated buffers, no heap allocs
    void process(const std::unordered_map<MixerTrackId, AudioBufferView>& track_inputs,
                 AudioBufferView& master_out) {
        const size_t frames = master_out.frames;

        // 1. Clear all track scratch buffers and populate with direct inputs
        for (const auto& id : topo_order_) {
            auto& buf = track_buffers_[id];
            if (!buf) {
                buf = std::make_unique<OwningAudioBuffer>(std::max(frames, max_block_size_));
            }
            buf->resize_frames(frames);
            auto view = buf->view();
            view.clear();

            auto input_it = track_inputs.find(id);
            if (input_it != track_inputs.end()) {
                view.copy_from(input_it->second);
            }
        }

        // Check if any track is soloed
        bool any_solo = false;
        for (const auto& [id, track] : tracks_) {
            if (id != MasterTrackId && track.solo()) {
                any_solo = true;
                break;
            }
        }

        // 2. Process tracks in topological order
        for (const auto& id : topo_order_) {
            auto track_it = tracks_.find(id);
            if (track_it == tracks_.end()) continue;
            auto& track = track_it->second;
            auto track_view = track_buffers_[id]->view();

            // Mute / Solo check
            bool is_active = true;
            if (id != MasterTrackId) {
                if (any_solo) {
                    is_active = track.solo();
                } else if (track.muted()) {
                    is_active = false;
                }
            } else if (track.muted()) {
                is_active = false;
            }

            if (!is_active) {
                track_view.clear();
                continue;
            }

            // Process inserts
            for (auto& slot : track.inserts()) {
                if (slot.enabled && slot.device && !slot.bypassed_due_to_error) {
                    try {
                        std::span<const MidiEvent> empty_midi{};
                        slot.device->process(track_view, empty_midi);
                    } catch (...) {
                        // Crash isolation: bypass problematic insert
                        slot.bypassed_due_to_error = true;
                    }
                }
            }

            // Apply Track Volume and Pan
            const float vol = track.volume();
            const auto [pan_l, pan_r] = track.compute_pan_gains();

            for (size_t f = 0; f < frames; ++f) {
                if (track_view.left) track_view.left[f] *= (vol * pan_l);
                if (track_view.right) track_view.right[f] *= (vol * pan_r);
            }

            // Route to target sends
            for (const auto& send : track.sends()) {
                auto target_it = track_buffers_.find(send.target_track);
                if (target_it != track_buffers_.end()) {
                    auto target_view = target_it->second->view();
                    target_view.add_from(track_view, send.amount);
                }
            }
        }

        // 3. Final master track buffer is copied to master_out
        auto master_buf_it = track_buffers_.find(MasterTrackId);
        if (master_buf_it != track_buffers_.end()) {
            master_out.copy_from(master_buf_it->second->view());
        } else {
            master_out.clear();
        }
    }

    [[nodiscard]] const std::vector<MixerTrackId>& topological_order() const noexcept {
        return topo_order_;
    }

private:
    [[nodiscard]] bool can_reach(MixerTrackId start, MixerTrackId target) const {
        if (start == target) return true;
        std::set<MixerTrackId> visited;
        std::queue<MixerTrackId> q;
        q.push(start);
        visited.insert(start);

        while (!q.empty()) {
            MixerTrackId cur = q.front();
            q.pop();

            auto it = tracks_.find(cur);
            if (it == tracks_.end()) continue;

            for (const auto& send : it->second.sends()) {
                if (send.target_track == target) {
                    return true;
                }
                if (visited.find(send.target_track) == visited.end()) {
                    visited.insert(send.target_track);
                    q.push(send.target_track);
                }
            }
        }
        return false;
    }

    void rebuild_topological_order() {
        // Kahn's algorithm for topological sorting
        std::unordered_map<MixerTrackId, int> in_degree;
        for (const auto& [id, _] : tracks_) {
            in_degree[id] = 0;
        }

        for (const auto& [from_id, track] : tracks_) {
            for (const auto& send : track.sends()) {
                if (tracks_.find(send.target_track) != tracks_.end()) {
                    in_degree[send.target_track]++;
                }
            }
        }

        std::queue<MixerTrackId> q;
        for (const auto& [id, deg] : in_degree) {
            if (deg == 0) {
                q.push(id);
            }
        }

        topo_order_.clear();
        while (!q.empty()) {
            MixerTrackId u = q.front();
            q.pop();
            topo_order_.push_back(u);

            auto it = tracks_.find(u);
            if (it != tracks_.end()) {
                for (const auto& send : it->second.sends()) {
                    if (tracks_.find(send.target_track) != tracks_.end()) {
                        if (--in_degree[send.target_track] == 0) {
                            q.push(send.target_track);
                        }
                    }
                }
            }
        }

        // If cycle or unvisited nodes exist, ensure all tracks are present
        if (topo_order_.size() < tracks_.size()) {
            for (const auto& [id, _] : tracks_) {
                if (std::find(topo_order_.begin(), topo_order_.end(), id) == topo_order_.end()) {
                    topo_order_.push_back(id);
                }
            }
        }
    }

    double sample_rate_{44100.0};
    size_t max_block_size_{2048};
    std::unordered_map<MixerTrackId, MixerTrack> tracks_;
    std::vector<MixerTrackId> topo_order_;
    std::unordered_map<MixerTrackId, std::unique_ptr<OwningAudioBuffer>> track_buffers_;
};

} // namespace digidaw::domain
