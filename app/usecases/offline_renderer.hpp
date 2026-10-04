#pragma once

#include "transport.hpp"
#include "../../domain/project/project.hpp"
#include "../../domain/devices/device.hpp"
#include "../../adapters/audio/wave_file_writer.hpp"
#include "../../adapters/audio/flac_file_writer.hpp"
#include "../../domain/common/result.hpp"
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <vector>
#include <functional>
#include <cmath>

namespace digidaw::app {

enum class RenderFormat : uint8_t {
    Wav = 0,
    Flac = 1
};

enum class RenderBitDepth : uint8_t {
    Bit16 = 16,
    Bit24 = 24,
    Bit32Float = 32
};

struct RenderOptions {
    RenderFormat format{RenderFormat::Wav};
    RenderBitDepth bit_depth{RenderBitDepth::Bit16};
    double sample_rate{44100.0};
    size_t block_size{512};
    std::string output_path{};
    domain::Tick duration_ticks{0};
};

class OfflineRenderer {
public:
    using ProgressCallback = std::function<void(float progress)>;

    static domain::Result<void> render(
        domain::Project& project,
        const std::unordered_map<domain::ChannelId, std::shared_ptr<domain::IDevice>>& channel_devices,
        const RenderOptions& options,
        ProgressCallback progress_cb = nullptr) {

        if (options.sample_rate <= 0.0 || options.block_size == 0 || options.duration_ticks <= 0 || options.output_path.empty()) {
            return domain::Result<void>(domain::ErrorCode::InvalidArgument);
        }

        // Prepare devices
        for (const auto& [_, dev] : channel_devices) {
            if (dev) {
                dev->prepare(options.sample_rate, options.block_size);
                dev->reset();
            }
        }

        auto& mixer = project.mixer_graph();
        mixer.prepare(options.sample_rate, options.block_size);

        Transport transport(project);
        transport.set_mode(PlaybackMode::Song);
        transport.seek(0);
        transport.play();

        const double total_seconds = project.time_map().tick_to_seconds(options.duration_ticks);
        const auto total_frames = static_cast<size_t>(std::ceil(total_seconds * options.sample_rate));

        std::vector<float> recorded_left;
        std::vector<float> recorded_right;
        recorded_left.reserve(total_frames);
        recorded_right.reserve(total_frames);

        domain::OwningAudioBuffer master_buf(options.block_size);
        std::unordered_map<domain::MixerTrackId, domain::OwningAudioBuffer> track_inputs;
        // Allocate track input buffers for each track in mixer
        for (const auto& [track_id, _] : mixer.tracks()) {
            track_inputs[track_id] = domain::OwningAudioBuffer(options.block_size);
        }

        // Resource optimization: identify active channels from non-empty, unmuted arrangement tracks.
        bool any_solo = false;
        for (const auto& trk : project.tracks()) {
            if (trk.solo()) { any_solo = true; break; }
        }

        std::unordered_set<domain::ChannelId> active_arrangement_channels;
        for (const auto& trk : project.tracks()) {
            if (trk.clips().empty()) continue;
            if (any_solo ? !trk.solo() : trk.is_muted()) continue;
            for (const auto& clip : trk.clips()) {
                if (clip.muted) continue;
                if (const auto* pat = project.get_pattern(clip.pattern_id)) {
                    for (const auto& [cid, nset] : pat->all_notes()) {
                        for (const auto& note : nset.notes()) {
                            if (note.start < clip.length) {
                                active_arrangement_channels.insert(cid);
                                break;
                            }
                        }
                    }
                }
            }
        }

        // Channel output scratch buffers: allocate only for channels present in active arrangement tracks
        std::unordered_map<domain::ChannelId, domain::OwningAudioBuffer> channel_buffers;
        for (const auto& ch : project.channels()) {
            if (active_arrangement_channels.contains(ch.id())) {
                channel_buffers[ch.id()] = domain::OwningAudioBuffer(options.block_size);
            }
        }

        size_t frames_rendered = 0;
        while (frames_rendered < total_frames) {
            const size_t current_block_frames = std::min(options.block_size, total_frames - frames_rendered);
            master_buf.resize_frames(current_block_frames);
            auto master_view = master_buf.view();
            master_view.clear();

            // Clear track inputs
            for (auto& [_, buf] : track_inputs) {
                buf.resize_frames(current_block_frames);
                buf.clear();
            }

            // 1. Advance transport and get MIDI events for channels
            auto scheduled_events = transport.advance_block(current_block_frames, options.sample_rate);

            // 2. Synthesize audio from each active channel device
            for (const auto& ch : project.channels()) {
                if (ch.settings().muted) continue;
                if (!active_arrangement_channels.contains(ch.id())) continue;

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
                ch_view.apply_pan(ch.settings().pan);

                // Sum into targeted mixer track (0 = Unassigned -> routes directly to Master)
                const domain::MixerTrackId target_track = ch.settings().mixer_track;
                if (target_track == domain::MasterTrackId || target_track == 0) {
                    auto master_it = track_inputs.find(domain::MasterTrackId);
                    if (master_it != track_inputs.end()) {
                        auto target_view = master_it->second.view();
                        target_view.add_from(ch_view, 1.0f);
                    }
                } else {
                    auto track_in_it = track_inputs.find(target_track);
                    if (track_in_it != track_inputs.end()) {
                        auto target_view = track_in_it->second.view();
                        target_view.add_from(ch_view, 1.0f);
                    } else {
                        auto master_it = track_inputs.find(domain::MasterTrackId);
                        if (master_it != track_inputs.end()) {
                            auto target_view = master_it->second.view();
                            target_view.add_from(ch_view, 1.0f);
                        }
                    }
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
            if (progress_cb && total_frames > 0) {
                progress_cb(static_cast<float>(frames_rendered) / static_cast<float>(total_frames));
            }
        }

        if (progress_cb) {
            progress_cb(1.0f);
        }

        // 5. Output encoding
        if (options.format == RenderFormat::Flac) {
            adapters::audio::FlacBitDepth flac_depth = adapters::audio::FlacBitDepth::PCM16;
            if (options.bit_depth == RenderBitDepth::Bit24 || options.bit_depth == RenderBitDepth::Bit32Float) {
                flac_depth = adapters::audio::FlacBitDepth::PCM24;
            }
            return adapters::audio::FlacFileWriter::write_flac(
                options.output_path, recorded_left, recorded_right,
                static_cast<uint32_t>(options.sample_rate), flac_depth);
        } else {
            adapters::audio::WaveBitDepth wav_depth = adapters::audio::WaveBitDepth::PCM16;
            if (options.bit_depth == RenderBitDepth::Bit24) {
                wav_depth = adapters::audio::WaveBitDepth::PCM24;
            } else if (options.bit_depth == RenderBitDepth::Bit32Float) {
                wav_depth = adapters::audio::WaveBitDepth::Float32;
            }
            return adapters::audio::WaveFileWriter::write_wav(
                options.output_path, recorded_left, recorded_right,
                static_cast<uint32_t>(options.sample_rate), wav_depth);
        }
    }

    // Backwards compatibility convenience function
    static domain::Result<void> render_to_wav(
        domain::Project& project,
        const std::unordered_map<domain::ChannelId, std::shared_ptr<domain::IDevice>>& channel_devices,
        const std::string& output_wav_path,
        domain::Tick duration_ticks,
        double sample_rate = 44100.0,
        size_t block_size = 512,
        adapters::audio::WaveBitDepth bit_depth = adapters::audio::WaveBitDepth::PCM16) {

        RenderOptions opt;
        opt.format = RenderFormat::Wav;
        if (bit_depth == adapters::audio::WaveBitDepth::Float32) {
            opt.bit_depth = RenderBitDepth::Bit32Float;
        } else if (bit_depth == adapters::audio::WaveBitDepth::PCM24) {
            opt.bit_depth = RenderBitDepth::Bit24;
        } else {
            opt.bit_depth = RenderBitDepth::Bit16;
        }
        opt.output_path = output_wav_path;
        opt.duration_ticks = duration_ticks;
        opt.sample_rate = sample_rate;
        opt.block_size = block_size;

        return render(project, channel_devices, opt);
    }
};

} // namespace digidaw::app
