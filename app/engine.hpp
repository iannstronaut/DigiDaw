#pragma once

#include "usecases/project_session.hpp"
#include "usecases/plugin_manager.hpp"
#include "usecases/offline_renderer.hpp"
#include "../adapters/audio/audio_device_chain.hpp"
#include "../adapters/project/odp_repository.hpp"
#include "../adapters/config/config_store.hpp"
#include "../adapters/config/user_tree.hpp"
#include <mutex>
#include <unordered_map>
#include <vector>
#include <memory>
#include <cstring>
#include <iostream>

namespace digidaw::app {

class Engine {
public:
    Engine()
        : repo_(std::make_shared<adapters::project::OdpFileRepository>()),
          session_(std::make_unique<ProjectSession>(repo_)),
          plugin_mgr_(std::make_unique<PluginManager>()),
          audio_device_(std::make_unique<adapters::audio::AudioDeviceChain>()),
          transport_(std::make_unique<Transport>(session_->project())) {

        // Initialize user tree in standard path
        adapters::config::UserTreeManager::ensure_user_tree("DigiDawUserData");
    }

    [[nodiscard]] ProjectSession& session() noexcept { return *session_; }
    [[nodiscard]] PluginManager& plugin_manager() noexcept { return *plugin_mgr_; }
    [[nodiscard]] adapters::audio::AudioDeviceChain& audio_device() noexcept { return *audio_device_; }
    [[nodiscard]] Transport& transport() noexcept {
        if (transport_ && session_) {
            transport_->set_project(&session_->project());
        }
        return *transport_;
    }
    [[nodiscard]] std::mutex& audio_mutex() noexcept { return audio_mutex_; }

    std::shared_ptr<domain::IDevice> get_or_create_channel_device(domain::ChannelId cid) {
        std::lock_guard<std::mutex> lock(device_mutex_);
        auto it = channel_devices_.find(cid);
        if (it != channel_devices_.end() && it->second) return it->second;

        auto* ch = session_->project().get_channel(cid);
        if (ch) {
            auto res = plugin_mgr_->instantiate(ch->device_uid());
            if (res.is_ok()) {
                res.value()->prepare(44100.0, 512);
                channel_devices_[cid] = res.value();
                return res.value();
            }
        }
        return nullptr;
    }

    void audition_note(domain::ChannelId cid, uint8_t pitch, uint8_t vel = 100) {
        std::lock_guard<std::mutex> lock(audition_mutex_);
        audition_queue_.push_back({cid, domain::MidiEvent::make_note_on(0, 0, pitch, vel)});
        audition_frames_remaining_[cid] = static_cast<size_t>(44100.0 * 0.35); // 350ms note length
        audition_pitch_[cid] = pitch;
    }

    void all_notes_off() {
        {
            std::lock_guard<std::mutex> lock(device_mutex_);
            for (auto& [cid, dev] : channel_devices_) {
                if (dev) {
                    dev->reset();
                }
            }
        }
        {
            std::lock_guard<std::mutex> alock(audition_mutex_);
            audition_queue_.clear();
            audition_frames_remaining_.clear();
            audition_pitch_.clear();
        }
    }

    domain::Result<void> start_audio() {
        if (transport_ && session_) {
            transport_->set_project(&session_->project());
        }
        session_->project().mixer_graph().prepare(44100.0, 512);

        auto res = audio_device_->open(44100.0, 512, [this](domain::AudioBufferView& out) {
            process_realtime_audio(out);
        });
        if (res.is_ok()) {
            return audio_device_->start();
        }
        return res;
    }

    void stop_audio() {
        audio_device_->stop();
        audio_device_->close();
    }

    void process_realtime_audio(domain::AudioBufferView& out) {
        out.clear();
        const size_t frames = out.frames;
        if (frames == 0) return;

        // Try lock to prevent data races with GUI thread modifications
        std::unique_lock<std::mutex> lock(audio_mutex_, std::try_to_lock);
        if (!lock.owns_lock()) {
            return;
        }

        try {
            if (!session_ || !transport_) return;
            transport_->set_project(&session_->project());

            auto& proj = session_->project();
            auto& mixer = proj.mixer_graph();

            // Scratch buffers per track
            domain::OwningAudioBuffer master_buf(frames);
            auto master_view = master_buf.view();
            master_view.clear();

            std::unordered_map<domain::MixerTrackId, domain::OwningAudioBuffer> track_inputs;
            for (const auto& [track_id, _] : mixer.tracks()) {
                track_inputs[track_id] = domain::OwningAudioBuffer(frames);
            }

            // 1. Advance transport if playing
            std::vector<Transport::ScheduledChannelEvents> scheduled_events;
            if (transport_->is_playing()) {
                scheduled_events = transport_->advance_block(frames, 44100.0);
            }

            // 2. Fetch audition events
            std::vector<std::pair<domain::ChannelId, domain::MidiEvent>> cur_auditions;
            {
                std::lock_guard<std::mutex> alock(audition_mutex_);
                cur_auditions = std::move(audition_queue_);
                audition_queue_.clear();
            }

            // 3. Process each channel
            for (const auto& ch : proj.channels()) {
                if (ch.settings().muted) continue;

                auto dev = get_or_create_channel_device(ch.id());
                if (!dev) continue;

                // Collect MIDI events for this channel in this block
                std::vector<domain::MidiEvent> ch_midi;
                for (const auto& sch : scheduled_events) {
                    if (sch.channel_id == ch.id()) {
                        ch_midi.insert(ch_midi.end(), sch.events.begin(), sch.events.end());
                    }
                }

                // Add live audition NoteOn
                for (const auto& [aid, ev] : cur_auditions) {
                    if (aid == ch.id()) {
                        ch_midi.push_back(ev);
                    }
                }

                // Check audition NoteOff
                auto it_rem = audition_frames_remaining_.find(ch.id());
                if (it_rem != audition_frames_remaining_.end() && it_rem->second > 0) {
                    if (it_rem->second <= frames) {
                        it_rem->second = 0;
                        ch_midi.push_back(domain::MidiEvent::make_note_off(0, 0, audition_pitch_[ch.id()]));
                    } else {
                        it_rem->second -= frames;
                    }
                }

                // Synthesize
                domain::OwningAudioBuffer ch_buf(frames);
                auto ch_view = ch_buf.view();
                ch_view.clear();

                try {
                    dev->process(ch_view, ch_midi);
                } catch (...) {
                    // Prevent single plugin crash from crashing host
                }
                ch_view.apply_gain(ch.settings().volume);

                // Accumulate into targeted mixer track input
                const domain::MixerTrackId target_track = ch.settings().mixer_track;
                auto trk_it = track_inputs.find(target_track);
                if (trk_it != track_inputs.end()) {
                    auto trk_view = trk_it->second.view();
                    trk_view.add_from(ch_view);
                }
            }

            // 4. Run Mixer Graph
            std::unordered_map<domain::MixerTrackId, domain::AudioBufferView> trk_views;
            for (auto& [id, buf] : track_inputs) {
                trk_views[id] = buf.view();
            }
            mixer.process(trk_views, master_view);

            // 5. Measure real-time peaks for Master and all mixer tracks
            float master_p = 0.0f;
            if (master_view.left && master_view.right) {
                for (size_t f = 0; f < frames; ++f) {
                    master_p = std::max(master_p, std::abs(master_view.left[f]));
                    master_p = std::max(master_p, std::abs(master_view.right[f]));
                }
            }
            track_peaks_[0].store(master_p);

            for (auto& [tid, buf] : track_inputs) {
                if (tid < track_peaks_.size()) {
                    auto view = buf.view();
                    float tp = 0.0f;
                    if (view.left && view.right) {
                        for (size_t f = 0; f < frames; ++f) {
                            tp = std::max(tp, std::abs(view.left[f]));
                            tp = std::max(tp, std::abs(view.right[f]));
                        }
                    }
                    track_peaks_[tid].store(tp);
                }
            }

            // 6. Copy master output to hardware out buffer
            if (out.left && master_view.left) {
                std::memcpy(out.left, master_view.left, frames * sizeof(float));
            }
            if (out.right && master_view.right) {
                std::memcpy(out.right, master_view.right, frames * sizeof(float));
            }
        } catch (...) {
            out.clear();
        }
    }

    [[nodiscard]] float get_track_peak(size_t track_idx) const noexcept {
        if (track_idx < track_peaks_.size()) {
            return track_peaks_[track_idx].load();
        }
        return 0.0f;
    }

private:
    std::shared_ptr<IProjectRepository> repo_;
    std::unique_ptr<ProjectSession> session_;
    std::unique_ptr<PluginManager> plugin_mgr_;
    std::unique_ptr<adapters::audio::AudioDeviceChain> audio_device_;
    std::unique_ptr<Transport> transport_;

    std::mutex audio_mutex_;
    std::mutex device_mutex_;
    std::unordered_map<domain::ChannelId, std::shared_ptr<domain::IDevice>> channel_devices_;

    std::mutex audition_mutex_;
    std::vector<std::pair<domain::ChannelId, domain::MidiEvent>> audition_queue_;
    std::unordered_map<domain::ChannelId, size_t> audition_frames_remaining_;
    std::unordered_map<domain::ChannelId, uint8_t> audition_pitch_;
    std::array<std::atomic<float>, 8> track_peaks_{};
};

} // namespace digidaw::app
