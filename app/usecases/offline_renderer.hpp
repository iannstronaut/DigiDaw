#pragma once

#include "transport.hpp"
#include "../../domain/project/project.hpp"
#include "../../domain/devices/device.hpp"
#include "../../adapters/audio/wave_file_writer.hpp"
#include "../../domain/common/result.hpp"
#include <unordered_map>
#include <memory>
#include <vector>

namespace digidaw::app {

class OfflineRenderer {
public:
    static domain::Result<void> render_to_wav(
        domain::Project& project,
        const std::unordered_map<domain::ChannelId, std::shared_ptr<domain::IDevice>>& channel_devices,
        const std::string& output_wav_path,
        domain::Tick duration_ticks,
        double sample_rate = 44100.0,
        size_t block_size = 512,
        adapters::audio::WaveBitDepth bit_depth = adapters::audio::WaveBitDepth::PCM16) {

        if (sample_rate <= 0.0 || block_size == 0 || duration_ticks <= 0) {
            return domain::Result<void>(domain::ErrorCode::InvalidArgument);
        }

        // Prepare devices
        for (const auto& [_, dev] : channel_devices) {
            if (dev) {
                dev->prepare(sample_rate, block_size);
                dev->reset();
            }
        }

        auto& mixer = project.mixer_graph();
        mixer.prepare(sample_rate, block_size);

        Transport transport(project);
        transport.set_mode(PlaybackMode::Song);
        transport.seek(0);
        transport.play();

        const double total_seconds = project.time_map().tick_to_seconds(duration_ticks);
        const auto total_frames = static_cast<size_t>(std::ceil(total_seconds * sample_rate));

        std::vector<float> recorded_left;
        std::vector<float> recorded_right;
        recorded_left.reserve(total_frames);
        recorded_right.reserve(total_frames);

        domain::OwningAudioBuffer master_buf(block_size);
        std::unordered_map<domain::MixerTrackId, domain::OwningAudioBuffer> track_inputs;
        // Allocate track input buffers for each track in mixer
        for (const auto& [track_id, _] : mixer.tracks()) {
            track_inputs[track_id] = domain::OwningAudioBuffer(block_size);
        }

        // Channel output scratch buffers
        std::unordered_map<domain::ChannelId, domain::OwningAudioBuffer> channel_buffers;
        for (const auto& ch : project.channels()) {
            channel_buffers[ch.id()] = domain::OwningAudioBuffer(block_size);
        }

        size_t frames_rendered = 0;
        while (frames_rendered < total_frames) {
            const size_t current_block_frames = std::min(block_size, total_frames - frames_rendered);
            master_buf.resize_frames(current_block_frames);
            auto master_view = master_buf.view();
            master_view.clear();

            // Clear track inputs
            for (auto& [_, buf] : track_inputs) {
                buf.resize_frames(current_block_frames);
                buf.clear();
            }

            // 1. Advance transport and get MIDI events for channels
            auto scheduled_events = transport.advance_block(current_block_frames, sample_rate);

            // 2. Synthesize audio from each channel device
            for (const auto& ch : project.channels()) {
                if (ch.settings().muted) continue;

                auto dev_it = channel_devices.find(ch.id());
                if (dev_it == channel_devices.end() || !dev_it->second) continue;

                auto& ch_buf = channel_buffers[ch.id()];
                ch_buf.resize_frames(current_block_frames);
                auto ch_view = ch_buf.view();
                ch_view.clear();

                // Find MIDI events for this channel in this block
                std::span<const domain::MidiEvent> midi_span{};
                for (const auto& ch_ev : scheduled_events) {
                    if (ch_ev.channel_id == ch.id()) {
                        midi_span = ch_ev.events;
                        break;
                    }
                }

                // Process channel instrument
                dev_it->second->process(ch_view, midi_span);

                // Apply channel volume & pan
                ch_view.apply_gain(ch.settings().volume);

                // Sum into targeted mixer track
                const domain::MixerTrackId target_track = ch.settings().mixer_track;
                auto track_in_it = track_inputs.find(target_track);
                if (track_in_it != track_inputs.end()) {
                    auto target_view = track_in_it->second.view();
                    target_view.add_from(ch_view, 1.0f);
                }
            }

            // 3. Process mixer graph
            std::unordered_map<domain::MixerTrackId, domain::AudioBufferView> mixer_views;
            for (auto& [id, buf] : track_inputs) {
                mixer_views[id] = buf.view();
            }

            mixer.process(mixer_views, master_view);

            // 4. Record output samples
            for (size_t f = 0; f < current_block_frames; ++f) {
                recorded_left.push_back(master_view.left ? master_view.left[f] : 0.0f);
                recorded_right.push_back(master_view.right ? master_view.right[f] : 0.0f);
            }

            frames_rendered += current_block_frames;
        }

        // 5. Write to WAV
        return adapters::audio::WaveFileWriter::write_wav(
            output_wav_path, recorded_left, recorded_right,
            static_cast<uint32_t>(sample_rate), bit_depth);
    }
};

} // namespace digidaw::app
