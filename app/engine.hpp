#pragma once

#include "usecases/project_session.hpp"
#include "usecases/plugin_manager.hpp"
#include "usecases/offline_renderer.hpp"
#include "../adapters/audio/audio_device_chain.hpp"
#include "../adapters/project/odp_repository.hpp"
#include "../adapters/config/config_store.hpp"
#include "../adapters/config/user_tree.hpp"
#include "../domain/dsp/denormal.hpp"
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
    [[nodiscard]] std::recursive_mutex& audio_mutex() noexcept { return audio_mutex_; }

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

    void preview_sample_data(std::vector<float> left, std::vector<float> right) {
        std::lock_guard<std::mutex> lock(preview_mutex_);
        preview_l_ = std::move(left);
        preview_r_ = std::move(right);
        preview_pos_ = 0;
        preview_active_ = true;
    }

    void stop_sample_preview() {
        std::lock_guard<std::mutex> lock(preview_mutex_);
        preview_active_ = false;
        preview_pos_ = 0;
    }

    [[nodiscard]] bool is_sample_preview_active() const noexcept {
        return preview_active_;
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
        {
            std::lock_guard<std::mutex> plock(preview_mutex_);
            preview_active_ = false;
            preview_pos_ = 0;
            preview_l_.clear();
            preview_r_.clear();
        }
        for (auto& s : spectrum_bands_) {
            s.store(0.0f, std::memory_order_relaxed);
        }
        for (auto& p : track_peaks_l_) {
            p.store(0.0f, std::memory_order_relaxed);
        }
        for (auto& p : track_peaks_r_) {
            p.store(0.0f, std::memory_order_relaxed);
        }
        master_waveform_.fill(0.0f);
    }

    domain::Result<void> start_audio() {
        if (transport_ && session_) {
            transport_->set_project(&session_->project());
        }
        auto& mixer = session_->project().mixer_graph();
        mixer.prepare(44100.0, 512);

        // Preallocate scratch buffers for zero real-time audio thread allocations
        master_scratch_buf_ = domain::OwningAudioBuffer(2048);
        channel_scratch_buf_ = domain::OwningAudioBuffer(2048);
        mono_scratch_buf_.assign(2048, 0.0f);
        track_inputs_scratch_.clear();
        for (const auto& [track_id, _] : mixer.tracks()) {
            track_inputs_scratch_[track_id] = domain::OwningAudioBuffer(2048);
        }
        ch_midi_scratch_.reserve(128);

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
        domain::dsp::enable_ftz_daz();
        out.clear();
        const size_t frames = out.frames;
        if (frames == 0) return;

        // Try lock to prevent data races with GUI thread modifications
        std::unique_lock<std::recursive_mutex> lock(audio_mutex_, std::try_to_lock);
        if (!lock.owns_lock()) {
            return;
        }

        try {
            if (!session_ || !transport_) return;
            transport_->set_project(&session_->project());

            auto& proj = session_->project();
            auto& mixer = proj.mixer_graph();

            // Zero-allocation scratch buffers
            if (master_scratch_buf_.frames() < frames) {
                master_scratch_buf_ = domain::OwningAudioBuffer(std::max(frames, size_t(2048)));
            }
            master_scratch_buf_.resize_frames(frames);
            auto master_view = master_scratch_buf_.view();
            master_view.clear();

            // Prune deleted mixer track buffers if tracks were removed
            if (track_inputs_scratch_.size() != mixer.tracks().size()) {
                for (auto it = track_inputs_scratch_.begin(); it != track_inputs_scratch_.end(); ) {
                    if (!mixer.get_track(it->first)) {
                        it = track_inputs_scratch_.erase(it);
                    } else {
                        ++it;
                    }
                }
            }

            for (const auto& [track_id, _] : mixer.tracks()) {
                auto it = track_inputs_scratch_.find(track_id);
                if (it == track_inputs_scratch_.end() || it->second.frames() < frames) {
                    track_inputs_scratch_[track_id] = domain::OwningAudioBuffer(std::max(frames, size_t(2048)));
                }
                auto& t_buf = track_inputs_scratch_[track_id];
                t_buf.resize_frames(frames);
                t_buf.view().clear();
            }

            // 1. Advance transport if playing
            scheduled_events_scratch_.clear();
            if (transport_->is_playing()) {
                scheduled_events_scratch_ = transport_->advance_block(frames, 44100.0);
            }

            // 2. Fetch audition events
            cur_auditions_scratch_.clear();
            {
                std::lock_guard<std::mutex> alock(audition_mutex_);
                if (!audition_queue_.empty()) {
                    cur_auditions_scratch_ = std::move(audition_queue_);
                    audition_queue_.clear();
                }
            }

            // 3. Process each channel
            if (channel_scratch_buf_.frames() < frames) {
                channel_scratch_buf_ = domain::OwningAudioBuffer(std::max(frames, size_t(2048)));
            }

            for (const auto& ch : proj.channels()) {
                if (ch.settings().muted) continue;

                auto dev = get_or_create_channel_device(ch.id());
                if (!dev) continue;

                // Collect MIDI events for this channel in this block
                ch_midi_scratch_.clear();
                for (const auto& sch : scheduled_events_scratch_) {
                    if (sch.channel_id == ch.id()) {
                        ch_midi_scratch_.insert(ch_midi_scratch_.end(), sch.events.begin(), sch.events.end());
                    }
                }

                // Add live audition NoteOn
                for (const auto& [aid, ev] : cur_auditions_scratch_) {
                    if (aid == ch.id()) {
                        ch_midi_scratch_.push_back(ev);
                    }
                }

                // Check audition NoteOff
                auto it_rem = audition_frames_remaining_.find(ch.id());
                if (it_rem != audition_frames_remaining_.end() && it_rem->second > 0) {
                    if (it_rem->second <= frames) {
                        it_rem->second = 0;
                        ch_midi_scratch_.push_back(domain::MidiEvent::make_note_off(0, 0, audition_pitch_[ch.id()]));
                    } else {
                        it_rem->second -= frames;
                    }
                }

                // Synthesize using preallocated scratch buffer
                channel_scratch_buf_.resize_frames(frames);
                auto ch_view = channel_scratch_buf_.view();
                ch_view.clear();

                try {
                    dev->process(ch_view, ch_midi_scratch_);
                } catch (...) {
                    // Prevent single plugin crash from crashing host
                }
                ch_view.apply_gain(ch.settings().volume);
                ch_view.apply_pan(ch.settings().pan);

                // Accumulate into targeted mixer track input (0 = Unassigned -> routes directly to Master)
                const domain::MixerTrackId target_track = ch.settings().mixer_track;
                if (target_track == domain::MasterTrackId || target_track == 0) {
                    auto master_it = track_inputs_scratch_.find(domain::MasterTrackId);
                    if (master_it != track_inputs_scratch_.end()) {
                        master_it->second.view().add_from(ch_view);
                    }
                } else {
                    auto trk_it = track_inputs_scratch_.find(target_track);
                    if (trk_it != track_inputs_scratch_.end()) {
                        trk_it->second.view().add_from(ch_view);
                    } else {
                        // Fallback to Master if designated mixer track does not exist
                        auto master_it = track_inputs_scratch_.find(domain::MasterTrackId);
                        if (master_it != track_inputs_scratch_.end()) {
                            master_it->second.view().add_from(ch_view);
                        }
                    }
                }
            }

            // 4. Run Mixer Graph
            trk_views_scratch_.clear();
            for (auto& [id, buf] : track_inputs_scratch_) {
                trk_views_scratch_[id] = buf.view();
            }
            mixer.process(trk_views_scratch_, master_view);

            // 5. Measure real-time stereo peaks for Master and all mixer tracks
            float master_pl = 0.0f, master_pr = 0.0f;
            if (master_view.left && master_view.right) {
                for (size_t f = 0; f < frames; ++f) {
                    master_pl = std::max(master_pl, std::abs(master_view.left[f]));
                    master_pr = std::max(master_pr, std::abs(master_view.right[f]));
                }
            }
            track_peaks_l_[0].store(master_pl, std::memory_order_relaxed);
            track_peaks_r_[0].store(master_pr, std::memory_order_relaxed);

            // 5b. Compute 16 real-time analog spectrum frequency bands via Goertzel algorithm
            static const std::array<float, 16> kGoertzelCoeffs = []() {
                std::array<float, 16> c{};
                constexpr float freqs[16] = {
                    45.0f, 75.0f, 120.0f, 180.0f, 280.0f, 420.0f, 650.0f, 1000.0f,
                    1500.0f, 2200.0f, 3300.0f, 4800.0f, 7000.0f, 9500.0f, 12500.0f, 16000.0f
                };
                for (size_t i = 0; i < 16; ++i) {
                    float w = 2.0f * 3.141592653589793f * freqs[i] / 44100.0f;
                    c[i] = 2.0f * std::cos(w);
                }
                return c;
            }();

            // 4b. Mix Audition / Preview Sample Audio
            {
                std::lock_guard<std::mutex> plock(preview_mutex_);
                if (preview_active_ && !preview_l_.empty()) {
                    const size_t total_smp = preview_l_.size();
                    for (size_t f = 0; f < frames; ++f) {
                        if (preview_pos_ >= total_smp) {
                            preview_active_ = false;
                            break;
                        }
                        float pl = preview_l_[preview_pos_];
                        float pr = preview_r_.empty() ? pl : preview_r_[preview_pos_];
                        if (master_view.left) master_view.left[f] += pl;
                        if (master_view.right) master_view.right[f] += pr;
                        preview_pos_++;
                    }
                }
            }

            if (master_view.left && master_view.right && frames > 0) {
                // Record master waveform ring buffer with power-of-two mask
                size_t w_head = master_waveform_head_.load(std::memory_order_relaxed);
                if (mono_scratch_buf_.size() < frames) mono_scratch_buf_.resize(frames);
                for (size_t f = 0; f < frames; ++f) {
                    float mono = 0.5f * (master_view.left[f] + master_view.right[f]);
                    mono_scratch_buf_[f] = mono;
                    master_waveform_[w_head] = mono;
                    w_head = (w_head + 1) & (WaveformHistorySize - 1);
                }
                master_waveform_head_.store(w_head, std::memory_order_relaxed);

                if (master_pl < 0.0001f && master_pr < 0.0001f) {
                    for (size_t k = 0; k < 16; ++k) {
                        spectrum_bands_[k].store(0.0f, std::memory_order_relaxed);
                    }
                } else {
                    // Run 16-band Goertzel filters on precomputed mono signal
                    const float inv_frames = 1.0f / static_cast<float>(frames);
                    for (size_t k = 0; k < 16; ++k) {
                        float coeff = kGoertzelCoeffs[k];
                        float s1 = 0.0f, s2 = 0.0f;
                        for (size_t f = 0; f < frames; ++f) {
                            float s0 = mono_scratch_buf_[f] + coeff * s1 - s2;
                            s2 = s1;
                            s1 = s0;
                        }
                        float p = s1 * s1 + s2 * s2 - coeff * s1 * s2;
                        float mag = (p > 0.0f) ? (std::sqrt(p) * inv_frames) : 0.0f;
                        float weight = 2.4f + 0.38f * static_cast<float>(k);
                        spectrum_bands_[k].store(std::min(1.5f, mag * weight), std::memory_order_relaxed);
                    }
                }
            } else {
                for (size_t k = 0; k < 16; ++k) {
                    spectrum_bands_[k].store(0.0f, std::memory_order_relaxed);
                }
            }

            // Measure post-fader stereo peaks for all mixer insert tracks
            for (const auto& [tid, track] : mixer.tracks()) {
                if (tid == domain::MasterTrackId) continue;
                if (tid < track_peaks_l_.size()) {
                    const auto* buf = mixer.get_track_buffer(tid);
                    float tpl = 0.0f, tpr = 0.0f;
                    if (buf) {
                        auto view = buf->view();
                        if (view.left && view.right) {
                            for (size_t f = 0; f < frames; ++f) {
                                tpl = std::max(tpl, std::abs(view.left[f]));
                                tpr = std::max(tpr, std::abs(view.right[f]));
                            }
                        }
                    }
                    track_peaks_l_[tid].store(tpl, std::memory_order_relaxed);
                    track_peaks_r_[tid].store(tpr, std::memory_order_relaxed);
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
        if (track_idx < track_peaks_l_.size()) {
            return std::max(track_peaks_l_[track_idx].load(std::memory_order_relaxed),
                            track_peaks_r_[track_idx].load(std::memory_order_relaxed));
        }
        return 0.0f;
    }

    [[nodiscard]] std::pair<float, float> get_track_peaks_stereo(size_t track_idx) const noexcept {
        if (track_idx < track_peaks_l_.size()) {
            return { track_peaks_l_[track_idx].load(std::memory_order_relaxed),
                     track_peaks_r_[track_idx].load(std::memory_order_relaxed) };
        }
        return { 0.0f, 0.0f };
    }

    [[nodiscard]] float get_spectrum_band(size_t band) const noexcept {
        if (band < spectrum_bands_.size()) {
            return spectrum_bands_[band].load(std::memory_order_relaxed);
        }
        return 0.0f;
    }

    void get_spectrum(std::array<float, 16>& out) const noexcept {
        for (size_t i = 0; i < 16; ++i) {
            out[i] = spectrum_bands_[i].load(std::memory_order_relaxed);
        }
    }

    void get_waveform(std::array<float, 256>& out) const noexcept {
        size_t head = master_waveform_head_.load(std::memory_order_relaxed);
        for (size_t i = 0; i < 256; ++i) {
            out[i] = master_waveform_[(head + i) & (WaveformHistorySize - 1)];
        }
    }

private:
    std::shared_ptr<IProjectRepository> repo_;
    std::unique_ptr<ProjectSession> session_;
    std::unique_ptr<PluginManager> plugin_mgr_;
    std::unique_ptr<adapters::audio::AudioDeviceChain> audio_device_;
    std::unique_ptr<Transport> transport_;

    std::recursive_mutex audio_mutex_;
    std::mutex device_mutex_;
    std::unordered_map<domain::ChannelId, std::shared_ptr<domain::IDevice>> channel_devices_;

    std::mutex audition_mutex_;
    std::vector<std::pair<domain::ChannelId, domain::MidiEvent>> audition_queue_;
    std::unordered_map<domain::ChannelId, size_t> audition_frames_remaining_;
    std::unordered_map<domain::ChannelId, uint8_t> audition_pitch_;
    static constexpr size_t MaxTrackPeaks = 64;
    std::array<std::atomic<float>, MaxTrackPeaks> track_peaks_l_{};
    std::array<std::atomic<float>, MaxTrackPeaks> track_peaks_r_{};

public:
    static constexpr size_t NumSpectrumBands = 16;
    static constexpr size_t WaveformHistorySize = 256;
private:
    std::array<std::atomic<float>, NumSpectrumBands> spectrum_bands_{};
    std::array<float, WaveformHistorySize> master_waveform_{};
    std::atomic<size_t> master_waveform_head_{0};

    std::mutex preview_mutex_;
    std::vector<float> preview_l_;
    std::vector<float> preview_r_;
    size_t preview_pos_{0};
    bool preview_active_{false};

    // Preallocated real-time audio thread scratch buffers
    domain::OwningAudioBuffer master_scratch_buf_{2048};
    domain::OwningAudioBuffer channel_scratch_buf_{2048};
    std::vector<float> mono_scratch_buf_{2048};
    std::unordered_map<domain::MixerTrackId, domain::OwningAudioBuffer> track_inputs_scratch_;
    std::unordered_map<domain::MixerTrackId, domain::AudioBufferView> trk_views_scratch_;
    std::vector<domain::MidiEvent> ch_midi_scratch_;
    std::vector<Transport::ScheduledChannelEvents> scheduled_events_scratch_;
    std::vector<std::pair<domain::ChannelId, domain::MidiEvent>> cur_auditions_scratch_;
};

} // namespace digidaw::app
