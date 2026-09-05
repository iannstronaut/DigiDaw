#pragma once

#include "../../domain/devices/device.hpp"
#include "../../domain/dsp/resampler.hpp"
#include "../../domain/dsp/biquad.hpp"
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <span>

namespace digidaw::adapters::plugins {

struct AudioClipVoice {
    bool active{false};
    uint8_t note{60};
    uint8_t velocity{100};
    double position{0.0};
    double speed{1.0};
    int direction{1}; // +1 = Forward, -1 = Reverse (Ping-pong)
    int env_stage{0}; // 0=Idle, 1=Delay, 2=Attack, 3=Hold, 4=Decay, 5=Sustain, 6=Release
    float env_level{0.0f};
    float stage_time{0.0f};
    double lfo_phase{0.0};
    float declick_gain{1.0f};
    float declick_step{0.05f};
};

class AudioClipDevice : public domain::IDevice {
public:
    AudioClipDevice() {
        init_default_sample();
        update_filter();
    }

    [[nodiscard]] domain::DeviceUid uid() const override { return "core.generator.audioclip"; }
    [[nodiscard]] std::string name() const override { return "Clipper"; }
    [[nodiscard]] domain::DeviceCategory category() const override { return domain::DeviceCategory::Generator; }

    void prepare(double sample_rate, size_t /*max_block_size*/) override {
        sample_rate_ = sample_rate > 0.0 ? sample_rate : 44100.0;
        update_filter();
        reset();
    }

    void reset() override {
        for (auto& v : voices_) {
            v.active = false;
            v.env_stage = 0;
            v.env_level = 0.0f;
            v.stage_time = 0.0f;
            v.position = 0.0;
            v.direction = 1;
            v.lfo_phase = 0.0;
            v.declick_gain = 1.0f;
            v.declick_step = 0.05f;
        }
        filter_l_.reset();
        filter_r_.reset();
    }

    void process(domain::AudioBufferView& audio, std::span<const domain::MidiEvent> midi) override {
        for (const auto& ev : midi) {
            uint8_t status = ev.status & 0xF0;
            if (status == 0x90 && ev.data2 > 0) {
                note_on(ev.data1, ev.data2);
            } else if (status == 0x80 || (status == 0x90 && ev.data2 == 0)) {
                note_off(ev.data1);
            }
        }

        const size_t frames = audio.frames;
        if (!audio.left || !audio.right || frames == 0 || sample_l_.empty()) return;

        const float inv_sr = 1.0f / static_cast<float>(sample_rate_);
        const size_t sample_size = sample_l_.size();

        // Calculate envelope rates
        const float att_time = std::max(0.001f, env_attack_);
        const float dec_time = std::max(0.001f, env_decay_);
        const float rel_time = std::max(0.001f, env_release_);

        for (size_t f = 0; f < frames; ++f) {
            float out_l = 0.0f;
            float out_r = 0.0f;

            for (auto& v : voices_) {
                if (!v.active) continue;

                // Process Envelope
                float env_mult = 1.0f;
                if (env_enabled_) {
                    v.stage_time += inv_sr;
                    switch (v.env_stage) {
                        case 1: // Delay
                            v.env_level = 0.0f;
                            if (v.stage_time >= env_delay_) {
                                v.env_stage = 2;
                                v.stage_time = 0.0f;
                            }
                            break;
                        case 2: // Attack
                            v.env_level = std::clamp(v.stage_time / att_time, 0.0f, 1.0f);
                            if (v.stage_time >= att_time) {
                                v.env_stage = 3;
                                v.stage_time = 0.0f;
                                v.env_level = 1.0f;
                            }
                            break;
                        case 3: // Hold
                            v.env_level = 1.0f;
                            if (v.stage_time >= env_hold_) {
                                v.env_stage = 4;
                                v.stage_time = 0.0f;
                            }
                            break;
                        case 4: // Decay
                            {
                                float frac = std::clamp(v.stage_time / dec_time, 0.0f, 1.0f);
                                v.env_level = 1.0f - frac * (1.0f - env_sustain_);
                                if (v.stage_time >= dec_time) {
                                    v.env_stage = 5;
                                    v.stage_time = 0.0f;
                                    v.env_level = env_sustain_;
                                }
                            }
                            break;
                        case 5: // Sustain
                            v.env_level = env_sustain_;
                            break;
                        case 6: // Release
                            {
                                float frac = std::clamp(v.stage_time / rel_time, 0.0f, 1.0f);
                                v.env_level = env_sustain_ * (1.0f - frac);
                                if (v.stage_time >= rel_time || v.env_level <= 0.0001f) {
                                    v.active = false;
                                    continue;
                                }
                            }
                            break;
                        default:
                            break;
                    }
                    env_mult = v.env_level;
                } else if (v.env_stage == 6) {
                    // Release de-click when envelope is off
                    v.declick_gain -= v.declick_step;
                    if (v.declick_gain <= 0.0f) {
                        v.active = false;
                        continue;
                    }
                    env_mult = v.declick_gain;
                }

                // Process LFO
                if (lfo_amount_ > 0.001f) {
                    v.lfo_phase += 2.0 * 3.141592653589793 * lfo_speed_ * inv_sr;
                    if (v.lfo_phase > 2.0 * 3.141592653589793) v.lfo_phase -= 2.0 * 3.141592653589793;
                    float lfo_val = 0.0f;
                    if (lfo_shape_ == 0) { // Sine
                        lfo_val = static_cast<float>(std::sin(v.lfo_phase));
                    } else if (lfo_shape_ == 1) { // Triangle
                        lfo_val = static_cast<float>(2.0 / 3.141592653589793 * std::asin(std::sin(v.lfo_phase)));
                    } else { // Square
                        lfo_val = (std::sin(v.lfo_phase) >= 0.0) ? 1.0f : -1.0f;
                    }
                    env_mult *= std::clamp(1.0f + lfo_val * lfo_amount_ * 0.5f, 0.0f, 1.5f);
                }

                // Sample playback position
                size_t pos_int = static_cast<size_t>(v.position);
                float frac = static_cast<float>(v.position - pos_int);

                if (pos_int >= sample_size) {
                    if (ping_pong_loop_ && sample_size > 2) {
                        v.direction = -1;
                        v.position = static_cast<double>(sample_size - 1);
                        pos_int = sample_size - 1;
                        frac = 0.0f;
                    } else if (use_loop_points_ && sample_size > 0) {
                        v.position = 0.0;
                        pos_int = 0;
                        frac = 0.0f;
                    } else {
                        v.active = false;
                        continue;
                    }
                } else if (v.position < 0.0) {
                    if (ping_pong_loop_ && sample_size > 2) {
                        v.direction = 1;
                        v.position = 0.0;
                        pos_int = 0;
                        frac = 0.0f;
                    } else {
                        v.active = false;
                        continue;
                    }
                }

                float ym1_l = (pos_int > 0) ? sample_l_[pos_int - 1] : sample_l_[0];
                float y0_l  = sample_l_[pos_int];
                float y1_l  = (pos_int + 1 < sample_size) ? sample_l_[pos_int + 1] : y0_l;
                float y2_l  = (pos_int + 2 < sample_size) ? sample_l_[pos_int + 2] : y1_l;

                float ym1_r = (pos_int > 0) ? sample_r_[pos_int - 1] : sample_r_[0];
                float y0_r  = sample_r_[pos_int];
                float y1_r  = (pos_int + 1 < sample_size) ? sample_r_[pos_int + 1] : y0_r;
                float y2_r  = (pos_int + 2 < sample_size) ? sample_r_[pos_int + 2] : y1_r;

                float sample_val_l, sample_val_r;
                if (resample_enabled_) {
                    sample_val_l = domain::dsp::Resampler::interpolate_4pt(ym1_l, y0_l, y1_l, y2_l, frac);
                    sample_val_r = domain::dsp::Resampler::interpolate_4pt(ym1_r, y0_r, y1_r, y2_r, frac);
                } else {
                    sample_val_l = y0_l + frac * (y1_l - y0_l);
                    sample_val_r = y0_r + frac * (y1_r - y0_r);
                }

                float gain = env_mult * (v.velocity / 127.0f) * master_vol_;
                float pan_l = (pan_ <= 0.0f) ? 1.0f : (1.0f - pan_);
                float pan_r = (pan_ >= 0.0f) ? 1.0f : (1.0f + pan_);

                out_l += sample_val_l * gain * pan_l;
                out_r += sample_val_r * gain * pan_r;

                v.position += v.speed * v.direction;
            }

            // Apply Resonant Filter
            if (filter_mod_x_ < 0.995f || filter_mod_y_ > 0.01f || filter_type_ != 0) {
                out_l = filter_l_.process(out_l);
                out_r = filter_r_.process(out_r);
            }

            audio.left[f] += out_l;
            audio.right[f] += out_r;
        }
    }

    void set_parameter(uint32_t param_id, float val) override {
        float clamped = std::clamp(val, 0.0f, 1.0f);
        switch (param_id) {
            case 0: master_vol_ = clamped; break;
            case 1: pan_ = clamped * 2.0f - 1.0f; break;
            case 2: pitch_shift_ = (clamped - 0.5f) * 24.0f; break; // -12 to +12 semitones
            case 3: root_key_ = static_cast<uint8_t>(std::round(clamped * 127.0f)); break;
            case 4: fine_tune_cents_ = (clamped - 0.5f) * 200.0f; break; // -100 to +100 cents
            case 5: time_mul_ = 0.5f + clamped * 1.5f; break;
            case 6: env_enabled_ = (clamped >= 0.5f); break;
            case 7: env_attack_ = 0.001f + clamped * 1.999f; break;
            case 8: env_hold_ = clamped * 2.0f; break;
            case 9: env_decay_ = 0.001f + clamped * 4.999f; break;
            case 10: env_sustain_ = clamped; break;
            case 11: env_release_ = 0.001f + clamped * 4.999f; break;
            case 12: filter_mod_x_ = clamped; update_filter(); break;
            case 13: filter_mod_y_ = clamped; update_filter(); break;
            case 14: lfo_amount_ = clamped; break;
            case 15: lfo_speed_ = 0.1f + clamped * 19.9f; break;
            case 16: set_normalize(clamped >= 0.5f); break;
            case 17: set_reverse(clamped >= 0.5f); break;
            case 18: set_remove_dc(clamped >= 0.5f); break;
            case 19: set_reverse_polarity(clamped >= 0.5f); break;
            default: break;
        }
    }

    [[nodiscard]] float get_parameter(uint32_t param_id) const override {
        switch (param_id) {
            case 0: return master_vol_;
            case 1: return (pan_ + 1.0f) * 0.5f;
            case 2: return (pitch_shift_ / 24.0f) + 0.5f;
            case 3: return root_key_ / 127.0f;
            case 4: return (fine_tune_cents_ / 200.0f) + 0.5f;
            case 5: return (time_mul_ - 0.5f) / 1.5f;
            case 6: return env_enabled_ ? 1.0f : 0.0f;
            case 7: return (env_attack_ - 0.001f) / 1.999f;
            case 8: return env_hold_ / 2.0f;
            case 9: return (env_decay_ - 0.001f) / 4.999f;
            case 10: return env_sustain_;
            case 11: return (env_release_ - 0.001f) / 4.999f;
            case 12: return filter_mod_x_;
            case 13: return filter_mod_y_;
            case 14: return lfo_amount_;
            case 15: return (lfo_speed_ - 0.1f) / 19.9f;
            case 16: return normalize_ ? 1.0f : 0.0f;
            case 17: return reverse_ ? 1.0f : 0.0f;
            case 18: return remove_dc_ ? 1.0f : 0.0f;
            case 19: return reverse_polarity_ ? 1.0f : 0.0f;
            default: return 0.0f;
        }
    }

    [[nodiscard]] std::vector<domain::ParamDesc> parameters() const override {
        return {
            {0, "Master Volume", 0.8f, 0.0f, 1.0f, "%"},
            {1, "Pan", 0.5f, 0.0f, 1.0f, "Pan"},
            {2, "Pitch Shift", 0.5f, -12.0f, 12.0f, "st"},
            {3, "Root Key", 60.0f / 127.0f, 0.0f, 127.0f, "MIDI"},
            {4, "Fine Tune", 0.5f, -100.0f, 100.0f, "cent"},
            {5, "Time Mul", 0.333f, 0.5f, 2.0f, "x"},
            {6, "Env Enable", 0.0f, 0.0f, 1.0f, "bool"},
            {7, "Attack", 0.025f, 0.001f, 2.0f, "s"},
            {8, "Hold", 0.025f, 0.0f, 2.0f, "s"},
            {9, "Decay", 0.04f, 0.001f, 5.0f, "s"},
            {10, "Sustain", 0.5f, 0.0f, 1.0f, "%"},
            {11, "Release", 0.04f, 0.001f, 5.0f, "s"},
            {12, "Filter MOD X", 1.0f, 0.0f, 1.0f, "%"},
            {13, "Filter MOD Y", 0.0f, 0.0f, 1.0f, "%"},
            {14, "LFO Amount", 0.0f, 0.0f, 1.0f, "%"},
            {15, "LFO Speed", 0.1f, 0.1f, 20.0f, "Hz"},
            {16, "Normalize", 0.0f, 0.0f, 1.0f, "bool"},
            {17, "Reverse", 0.0f, 0.0f, 1.0f, "bool"},
            {18, "Remove DC", 0.0f, 0.0f, 1.0f, "bool"},
            {19, "Reverse Polarity", 0.0f, 0.0f, 1.0f, "bool"}
        };
    }

    [[nodiscard]] std::vector<uint8_t> save_state() const override {
        std::vector<uint8_t> state;
        state.push_back(0xAC); // Magic 'AC' (AudioClip)
        state.push_back(2);    // Format Version 2
        state.push_back(root_key_);
        auto append_f = [&](float val) {
            uint8_t buf[sizeof(float)];
            std::memcpy(buf, &val, sizeof(float));
            state.insert(state.end(), buf, buf + sizeof(float));
        };
        append_f(master_vol_);
        append_f(pan_);
        append_f(pitch_shift_);
        append_f(fine_tune_cents_);
        append_f(time_mul_);
        append_f(env_attack_);
        append_f(env_decay_);
        append_f(env_sustain_);
        append_f(env_release_);
        state.push_back(env_enabled_ ? 1 : 0);
        state.push_back(normalize_ ? 1 : 0);
        state.push_back(reverse_ ? 1 : 0);
        state.push_back(remove_dc_ ? 1 : 0);
        state.push_back(reverse_polarity_ ? 1 : 0);

        // Extended V2 parameters
        append_f(env_delay_);
        append_f(env_hold_);
        append_f(env_att_tension_);
        append_f(env_dec_tension_);
        append_f(filter_mod_x_);
        append_f(filter_mod_y_);
        append_f(lfo_amount_);
        append_f(lfo_speed_);
        append_f(smp_start_);
        append_f(smp_length_);
        state.push_back(static_cast<uint8_t>(filter_type_));
        state.push_back(static_cast<uint8_t>(lfo_shape_));
        state.push_back(use_loop_points_ ? 1 : 0);
        state.push_back(ping_pong_loop_ ? 1 : 0);
        state.push_back(resample_enabled_ ? 1 : 0);
        state.push_back(enable_main_pitch_ ? 1 : 0);
        state.push_back(add_to_key_ ? 1 : 0);

        // Filename
        uint16_t fn_len = static_cast<uint16_t>(filename_.size());
        state.push_back(static_cast<uint8_t>(fn_len & 0xFF));
        state.push_back(static_cast<uint8_t>((fn_len >> 8) & 0xFF));
        state.insert(state.end(), filename_.begin(), filename_.end());

        return state;
    }

    domain::Result<void> load_state(std::span<const uint8_t> data) override {
        if (data.empty()) {
            return domain::Result<void>(domain::ErrorCode::StateIncompatible);
        }

        size_t offset = 0;
        auto read_f = [&]() -> float {
            if (offset + sizeof(float) > data.size()) return 0.0f;
            float v = 0.0f;
            std::memcpy(&v, data.data() + offset, sizeof(float));
            offset += sizeof(float);
            return v;
        };

        if (data[0] == 0xAC && data.size() >= 3 && data[1] == 2) {
            // Version 2 format
            offset = 2;
            root_key_ = data[offset++];
            master_vol_ = read_f();
            pan_ = read_f();
            pitch_shift_ = read_f();
            fine_tune_cents_ = read_f();
            time_mul_ = read_f();
            env_attack_ = read_f();
            env_decay_ = read_f();
            env_sustain_ = read_f();
            env_release_ = read_f();
            if (offset < data.size()) env_enabled_ = (data[offset++] != 0);
            if (offset < data.size()) normalize_ = (data[offset++] != 0);
            if (offset < data.size()) reverse_ = (data[offset++] != 0);
            if (offset < data.size()) remove_dc_ = (data[offset++] != 0);
            if (offset < data.size()) reverse_polarity_ = (data[offset++] != 0);

            // Extended V2 parameters
            env_delay_ = read_f();
            env_hold_ = read_f();
            env_att_tension_ = read_f();
            env_dec_tension_ = read_f();
            filter_mod_x_ = read_f();
            filter_mod_y_ = read_f();
            lfo_amount_ = read_f();
            lfo_speed_ = read_f();
            smp_start_ = read_f();
            smp_length_ = read_f();
            if (offset < data.size()) filter_type_ = data[offset++];
            if (offset < data.size()) lfo_shape_ = data[offset++];
            if (offset < data.size()) use_loop_points_ = (data[offset++] != 0);
            if (offset < data.size()) ping_pong_loop_ = (data[offset++] != 0);
            if (offset < data.size()) resample_enabled_ = (data[offset++] != 0);
            if (offset < data.size()) enable_main_pitch_ = (data[offset++] != 0);
            if (offset < data.size()) add_to_key_ = (data[offset++] != 0);

            if (offset + 2 <= data.size()) {
                uint16_t fn_len = static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8);
                offset += 2;
                if (offset + fn_len <= data.size()) {
                    filename_.assign(reinterpret_cast<const char*>(data.data() + offset), fn_len);
                }
            }
        } else {
            // Version 1 backward compatibility
            if (data.size() < 1 + sizeof(float) * 9 + 5) {
                return domain::Result<void>(domain::ErrorCode::StateIncompatible);
            }
            root_key_ = data[offset++];
            master_vol_ = read_f();
            pan_ = read_f();
            pitch_shift_ = read_f();
            fine_tune_cents_ = read_f();
            time_mul_ = read_f();
            env_attack_ = read_f();
            env_decay_ = read_f();
            env_sustain_ = read_f();
            env_release_ = read_f();
            env_enabled_ = (data[offset++] != 0);
            normalize_ = (data[offset++] != 0);
            reverse_ = (data[offset++] != 0);
            remove_dc_ = (data[offset++] != 0);
            reverse_polarity_ = (data[offset++] != 0);
        }

        update_filter();
        update_precomputed_samples();
        return domain::Result<void>::ok();
    }

    // --- Audio Clip & WAV Operations ---

    bool load_wav_file(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) return false;

        char riff[4];
        file.read(riff, 4);
        if (std::strncmp(riff, "RIFF", 4) != 0) return false;

        uint32_t file_size = 0;
        file.read(reinterpret_cast<char*>(&file_size), 4);

        char wave[4];
        file.read(wave, 4);
        if (std::strncmp(wave, "WAVE", 4) != 0) return false;

        uint16_t num_channels = 0;
        uint32_t sample_rate = 0;
        uint16_t bits_per_sample = 0;
        uint16_t format_tag = 1; // 1 = PCM, 3 = Float

        std::vector<float> loaded_l;
        std::vector<float> loaded_r;

        while (file) {
            char chunk_id[4];
            file.read(chunk_id, 4);
            if (!file) break;

            uint32_t chunk_size = 0;
            file.read(reinterpret_cast<char*>(&chunk_size), 4);
            if (!file) break;

            if (std::strncmp(chunk_id, "fmt ", 4) == 0) {
                file.read(reinterpret_cast<char*>(&format_tag), 2);
                file.read(reinterpret_cast<char*>(&num_channels), 2);
                file.read(reinterpret_cast<char*>(&sample_rate), 4);
                uint32_t byte_rate = 0;
                file.read(reinterpret_cast<char*>(&byte_rate), 4);
                uint16_t block_align = 0;
                file.read(reinterpret_cast<char*>(&block_align), 2);
                file.read(reinterpret_cast<char*>(&bits_per_sample), 2);

                if (chunk_size > 16) {
                    uint32_t rem = (chunk_size - 16) + (chunk_size & 1);
                    file.seekg(rem, std::ios::cur);
                } else if (chunk_size & 1) {
                    file.seekg(1, std::ios::cur);
                }
            } else if (std::strncmp(chunk_id, "data", 4) == 0) {
                if (num_channels == 0 || bits_per_sample == 0) return false;
                size_t bytes_per_sample = bits_per_sample / 8;
                if (bytes_per_sample == 0) return false;
                size_t num_frames = chunk_size / (num_channels * bytes_per_sample);

                loaded_l.resize(num_frames);
                loaded_r.resize(num_frames);

                std::vector<uint8_t> raw_data(chunk_size);
                file.read(reinterpret_cast<char*>(raw_data.data()), chunk_size);

                const uint8_t* ptr = raw_data.data();
                for (size_t i = 0; i < num_frames; ++i) {
                    float s_l = 0.0f;
                    float s_r = 0.0f;

                    if (format_tag == 1 && bits_per_sample == 8) {
                        uint8_t val_l = *ptr++;
                        s_l = (static_cast<float>(val_l) - 128.0f) / 128.0f;
                        if (num_channels > 1) {
                            uint8_t val_r = *ptr++;
                            s_r = (static_cast<float>(val_r) - 128.0f) / 128.0f;
                        } else {
                            s_r = s_l;
                        }
                    } else if (format_tag == 1 && bits_per_sample == 16) {
                        int16_t val_l = *reinterpret_cast<const int16_t*>(ptr);
                        ptr += 2;
                        s_l = val_l / 32768.0f;
                        if (num_channels > 1) {
                            int16_t val_r = *reinterpret_cast<const int16_t*>(ptr);
                            ptr += 2;
                            s_r = val_r / 32768.0f;
                        } else {
                            s_r = s_l;
                        }
                    } else if (format_tag == 1 && bits_per_sample == 24) {
                        int32_t val_l = (static_cast<int32_t>(ptr[0]) << 8) |
                                        (static_cast<int32_t>(ptr[1]) << 16) |
                                        (static_cast<int32_t>(ptr[2]) << 24);
                        ptr += 3;
                        s_l = (val_l >> 8) / 8388608.0f;
                        if (num_channels > 1) {
                            int32_t val_r = (static_cast<int32_t>(ptr[0]) << 8) |
                                            (static_cast<int32_t>(ptr[1]) << 16) |
                                            (static_cast<int32_t>(ptr[2]) << 24);
                            ptr += 3;
                            s_r = (val_r >> 8) / 8388608.0f;
                        } else {
                            s_r = s_l;
                        }
                    } else if (format_tag == 3 && bits_per_sample == 32) {
                        s_l = *reinterpret_cast<const float*>(ptr);
                        ptr += 4;
                        if (num_channels > 1) {
                            s_r = *reinterpret_cast<const float*>(ptr);
                            ptr += 4;
                        } else {
                            s_r = s_l;
                        }
                    } else {
                        ptr += num_channels * bytes_per_sample;
                    }

                    loaded_l[i] = std::clamp(s_l, -1.0f, 1.0f);
                    loaded_r[i] = std::clamp(s_r, -1.0f, 1.0f);
                }
                break;
            } else {
                uint32_t skip_sz = chunk_size + (chunk_size & 1);
                file.seekg(skip_sz, std::ios::cur);
            }
        }

        if (loaded_l.empty()) return false;

        size_t last_slash = filepath.find_last_of("/\\");
        filename_ = (last_slash != std::string::npos) ? filepath.substr(last_slash + 1) : filepath;

        raw_sample_l_ = std::move(loaded_l);
        raw_sample_r_ = std::move(loaded_r);
        bit_depth_ = bits_per_sample;
        file_sample_rate_ = sample_rate;

        // Default to C5 (MIDI note 60) for all audio as specified in task!
        root_key_ = 60;

        update_precomputed_samples();
        return true;
    }

    void load_sample_data(std::vector<float> left, std::vector<float> right, std::string filename = "Sample.wav", uint8_t root_key = 60, int bit_depth = 16) {
        filename_ = std::move(filename);
        raw_sample_l_ = std::move(left);
        raw_sample_r_ = std::move(right);
        root_key_ = root_key;
        bit_depth_ = bit_depth;
        update_precomputed_samples();
    }

    void clear_sample() {
        filename_ = "(None)";
        raw_sample_l_.clear();
        raw_sample_r_.clear();
        sample_l_.clear();
        sample_r_.clear();
        bit_depth_ = 0;
    }

    void update_precomputed_samples() {
        if (raw_sample_l_.empty()) return;

        const size_t raw_len = raw_sample_l_.size();

        // 1. Length & Start windowing FIRST so subsequent effects operate on trimmed region!
        size_t start_idx = static_cast<size_t>(smp_start_ * raw_len);
        size_t avail = (start_idx < raw_len) ? (raw_len - start_idx) : 0;
        size_t sub_len = static_cast<size_t>(smp_length_ * avail);
        if (sub_len < 4) sub_len = avail;
        if (sub_len > 0 && start_idx + sub_len <= raw_len) {
            sample_l_.assign(raw_sample_l_.begin() + start_idx, raw_sample_l_.begin() + start_idx + sub_len);
            sample_r_.assign(raw_sample_r_.begin() + start_idx, raw_sample_r_.begin() + start_idx + sub_len);
        } else {
            sample_l_ = raw_sample_l_;
            sample_r_ = raw_sample_r_;
        }

        const size_t len = sample_l_.size();
        if (len == 0) return;

        // 2. Remove DC Offset
        if (remove_dc_) {
            double sum_l = 0.0, sum_r = 0.0;
            for (size_t i = 0; i < len; ++i) {
                sum_l += sample_l_[i];
                sum_r += sample_r_[i];
            }
            float dc_l = static_cast<float>(sum_l / len);
            float dc_r = static_cast<float>(sum_r / len);
            for (size_t i = 0; i < len; ++i) {
                sample_l_[i] -= dc_l;
                sample_r_[i] -= dc_r;
            }
        }

        // 3. Reverse Polarity (Invert Phase)
        if (reverse_polarity_) {
            for (size_t i = 0; i < len; ++i) {
                sample_l_[i] = -sample_l_[i];
                sample_r_[i] = -sample_r_[i];
            }
        }

        // 4. Reverse Sample
        if (reverse_) {
            std::reverse(sample_l_.begin(), sample_l_.end());
            std::reverse(sample_r_.begin(), sample_r_.end());
        }

        // 5. Normalize
        if (normalize_) {
            float max_peak = 0.0f;
            for (size_t i = 0; i < len; ++i) {
                max_peak = std::max(max_peak, std::abs(sample_l_[i]));
                max_peak = std::max(max_peak, std::abs(sample_r_[i]));
            }
            if (max_peak > 1e-5f) {
                float norm_scale = 1.0f / max_peak;
                for (size_t i = 0; i < len; ++i) {
                    sample_l_[i] *= norm_scale;
                    sample_r_[i] *= norm_scale;
                }
            }
        }

        // 6. Fade stereo
        if (fade_stereo_) {
            for (size_t i = 0; i < len; ++i) {
                float frac = static_cast<float>(i) / len;
                sample_l_[i] *= (1.0f - frac);
                sample_r_[i] *= frac;
            }
        }

        // 7. Swap stereo
        if (swap_stereo_) {
            std::swap(sample_l_, sample_r_);
        }

        // 8. In / Out Fades
        const size_t cur_len = sample_l_.size();
        if (in_fade_ > 0.001f && cur_len > 0) {
            size_t fade_frames = std::min(cur_len, static_cast<size_t>(in_fade_ * cur_len));
            for (size_t i = 0; i < fade_frames; ++i) {
                float g = static_cast<float>(i) / fade_frames;
                sample_l_[i] *= g;
                sample_r_[i] *= g;
            }
        }
        if (out_fade_ > 0.001f && cur_len > 0) {
            size_t fade_frames = std::min(cur_len, static_cast<size_t>(out_fade_ * cur_len));
            size_t start_fade = cur_len - fade_frames;
            for (size_t i = 0; i < fade_frames; ++i) {
                float g = 1.0f - (static_cast<float>(i) / fade_frames);
                sample_l_[start_fade + i] *= g;
                sample_r_[start_fade + i] *= g;
            }
        }
    }

    void init_default_sample() {
        filename_ = "Basic 808 Kick.wav";
        root_key_ = 60; // C5 (Default root key)
        bit_depth_ = 16;
        file_sample_rate_ = 44100;

        const size_t len = 44100; // 1 second punchy 808 kick
        raw_sample_l_.resize(len);
        raw_sample_r_.resize(len);

        double cur_phase = 0.0;
        for (size_t i = 0; i < len; ++i) {
            double t = static_cast<double>(i) / 44100.0;
            double freq = 45.0 + 115.0 * std::exp(-26.0 * t);
            cur_phase += 2.0 * 3.141592653589793 * freq / 44100.0;
            double amp = std::exp(-4.2 * t);
            double click = (t < 0.012) ? (std::sin(2.0 * 3.14159265 * 750.0 * t) * (1.0 - t / 0.012) * 0.45) : 0.0;
            double sig = std::sin(cur_phase) * amp + click;
            sig = std::tanh(sig * 1.25);
            float val = static_cast<float>(sig);
            raw_sample_l_[i] = val;
            raw_sample_r_[i] = val;
        }

        update_precomputed_samples();
    }

    // --- Getters & Setters ---

    [[nodiscard]] const std::string& filename() const noexcept { return filename_; }
    void set_filename(std::string name) { filename_ = std::move(name); }

    [[nodiscard]] uint8_t root_key() const noexcept { return root_key_; }
    void set_root_key(uint8_t key) noexcept { root_key_ = key; }

    [[nodiscard]] float fine_tune_cents() const noexcept { return fine_tune_cents_; }
    void set_fine_tune_cents(float cents) noexcept { fine_tune_cents_ = cents; }

    [[nodiscard]] float pitch_shift() const noexcept { return pitch_shift_; }
    void set_pitch_shift(float semi) noexcept { pitch_shift_ = semi; }

    [[nodiscard]] float master_volume() const noexcept { return master_vol_; }
    void set_master_volume(float vol) noexcept { master_vol_ = std::clamp(vol, 0.0f, 1.0f); }

    [[nodiscard]] float pan() const noexcept { return pan_; }
    void set_pan(float pan) noexcept { pan_ = std::clamp(pan, -1.0f, 1.0f); }

    [[nodiscard]] float time_mul() const noexcept { return time_mul_; }
    void set_time_mul(float mul) noexcept { time_mul_ = std::clamp(mul, 0.5f, 2.0f); }

    [[nodiscard]] float time_length() const noexcept { return time_length_; }
    void set_time_length(float len) noexcept { time_length_ = std::clamp(len, 0.0f, 1.0f); }

    [[nodiscard]] int stretch_mode() const noexcept { return stretch_mode_; }
    void set_stretch_mode(int mode) noexcept { stretch_mode_ = mode; }

    // Precomputed Effects Getters / Setters
    [[nodiscard]] bool remove_dc() const noexcept { return remove_dc_; }
    void set_remove_dc(bool v) { remove_dc_ = v; update_precomputed_samples(); }

    [[nodiscard]] bool reverse_polarity() const noexcept { return reverse_polarity_; }
    void set_reverse_polarity(bool v) { reverse_polarity_ = v; update_precomputed_samples(); }

    [[nodiscard]] bool normalize() const noexcept { return normalize_; }
    void set_normalize(bool v) { normalize_ = v; update_precomputed_samples(); }

    [[nodiscard]] bool reverse() const noexcept { return reverse_; }
    void set_reverse(bool v) { reverse_ = v; update_precomputed_samples(); }

    [[nodiscard]] bool fade_stereo() const noexcept { return fade_stereo_; }
    void set_fade_stereo(bool v) { fade_stereo_ = v; update_precomputed_samples(); }

    [[nodiscard]] bool swap_stereo() const noexcept { return swap_stereo_; }
    void set_swap_stereo(bool v) { swap_stereo_ = v; update_precomputed_samples(); }

    [[nodiscard]] float smp_start() const noexcept { return smp_start_; }
    void set_smp_start(float v) { smp_start_ = std::clamp(v, 0.0f, 1.0f); update_precomputed_samples(); }

    [[nodiscard]] float smp_length() const noexcept { return smp_length_; }
    void set_smp_length(float v) { smp_length_ = std::clamp(v, 0.01f, 1.0f); update_precomputed_samples(); }

    [[nodiscard]] float in_fade() const noexcept { return in_fade_; }
    void set_in_fade(float v) { in_fade_ = std::clamp(v, 0.0f, 1.0f); update_precomputed_samples(); }

    [[nodiscard]] float out_fade() const noexcept { return out_fade_; }
    void set_out_fade(float v) { out_fade_ = std::clamp(v, 0.0f, 1.0f); update_precomputed_samples(); }

    [[nodiscard]] float crossfade() const noexcept { return crossfade_; }
    void set_crossfade(float v) { crossfade_ = std::clamp(v, 0.0f, 1.0f); }

    [[nodiscard]] float trim() const noexcept { return trim_; }
    void set_trim(float v) { trim_ = std::clamp(v, 0.0f, 1.0f); }

    // Content & Playback Options
    [[nodiscard]] bool keep_on_disk() const noexcept { return keep_on_disk_; }
    void set_keep_on_disk(bool v) noexcept { keep_on_disk_ = v; }

    [[nodiscard]] bool resample_enabled() const noexcept { return resample_enabled_; }
    void set_resample_enabled(bool v) noexcept { resample_enabled_ = v; }

    [[nodiscard]] bool load_regions() const noexcept { return load_regions_; }
    void set_load_regions(bool v) noexcept { load_regions_ = v; }

    [[nodiscard]] bool load_slice_markers() const noexcept { return load_slice_markers_; }
    void set_load_slice_markers(bool v) noexcept { load_slice_markers_ = v; }

    [[nodiscard]] int declicking_mode() const noexcept { return declicking_mode_; }
    void set_declicking_mode(int v) noexcept { declicking_mode_ = v; }

    [[nodiscard]] float start_offset() const noexcept { return start_offset_; }
    void set_start_offset(float v) noexcept { start_offset_ = std::clamp(v, 0.0f, 1.0f); }

    [[nodiscard]] bool use_loop_points() const noexcept { return use_loop_points_; }
    void set_use_loop_points(bool v) noexcept { use_loop_points_ = v; }

    [[nodiscard]] bool ping_pong_loop() const noexcept { return ping_pong_loop_; }
    void set_ping_pong_loop(bool v) noexcept { ping_pong_loop_ = v; }

    // Envelope Parameters
    [[nodiscard]] bool env_enabled() const noexcept { return env_enabled_; }
    void set_env_enabled(bool v) noexcept { env_enabled_ = v; }

    [[nodiscard]] float env_delay() const noexcept { return env_delay_; }
    void set_env_delay(float v) noexcept { env_delay_ = std::clamp(v, 0.0f, 2.0f); }

    [[nodiscard]] float env_attack() const noexcept { return env_attack_; }
    void set_env_attack(float v) noexcept { env_attack_ = std::clamp(v, 0.001f, 2.0f); }

    [[nodiscard]] float env_hold() const noexcept { return env_hold_; }
    void set_env_hold(float v) noexcept { env_hold_ = std::clamp(v, 0.0f, 2.0f); }

    [[nodiscard]] float env_decay() const noexcept { return env_decay_; }
    void set_env_decay(float v) noexcept { env_decay_ = std::clamp(v, 0.001f, 5.0f); }

    [[nodiscard]] float env_sustain() const noexcept { return env_sustain_; }
    void set_env_sustain(float v) noexcept { env_sustain_ = std::clamp(v, 0.0f, 1.0f); }

    [[nodiscard]] float env_release() const noexcept { return env_release_; }
    void set_env_release(float v) noexcept { env_release_ = std::clamp(v, 0.001f, 5.0f); }

    [[nodiscard]] float env_att_tension() const noexcept { return env_att_tension_; }
    void set_env_att_tension(float v) noexcept { env_att_tension_ = std::clamp(v, -1.0f, 1.0f); }

    [[nodiscard]] float env_dec_tension() const noexcept { return env_dec_tension_; }
    void set_env_dec_tension(float v) noexcept { env_dec_tension_ = std::clamp(v, -1.0f, 1.0f); }

    [[nodiscard]] bool env_tempo_sync() const noexcept { return env_tempo_sync_; }
    void set_env_tempo_sync(bool v) noexcept { env_tempo_sync_ = v; }

    // LFO Parameters
    [[nodiscard]] int lfo_shape() const noexcept { return lfo_shape_; }
    void set_lfo_shape(int s) noexcept { lfo_shape_ = s % 3; }

    [[nodiscard]] float lfo_delay() const noexcept { return lfo_delay_; }
    void set_lfo_delay(float v) noexcept { lfo_delay_ = std::clamp(v, 0.0f, 2.0f); }

    [[nodiscard]] float lfo_attack() const noexcept { return lfo_attack_; }
    void set_lfo_attack(float v) noexcept { lfo_attack_ = std::clamp(v, 0.0f, 2.0f); }

    [[nodiscard]] float lfo_amount() const noexcept { return lfo_amount_; }
    void set_lfo_amount(float v) noexcept { lfo_amount_ = std::clamp(v, 0.0f, 1.0f); }

    [[nodiscard]] float lfo_speed() const noexcept { return lfo_speed_; }
    void set_lfo_speed(float v) noexcept { lfo_speed_ = std::clamp(v, 0.1f, 20.0f); }

    [[nodiscard]] bool lfo_tempo_sync() const noexcept { return lfo_tempo_sync_; }
    void set_lfo_tempo_sync(bool v) noexcept { lfo_tempo_sync_ = v; }

    [[nodiscard]] bool lfo_global() const noexcept { return lfo_global_; }
    void set_lfo_global(bool v) noexcept { lfo_global_ = v; }

    // Filter Parameters
    [[nodiscard]] float filter_mod_x() const noexcept { return filter_mod_x_; }
    void set_filter_mod_x(float v) noexcept { filter_mod_x_ = std::clamp(v, 0.0f, 1.0f); update_filter(); }

    [[nodiscard]] float filter_mod_y() const noexcept { return filter_mod_y_; }
    void set_filter_mod_y(float v) noexcept { filter_mod_y_ = std::clamp(v, 0.0f, 1.0f); update_filter(); }

    [[nodiscard]] int filter_type() const noexcept { return filter_type_; }
    void set_filter_type(int t) noexcept { filter_type_ = t % 3; update_filter(); }

    // Bottom Controls
    [[nodiscard]] bool enable_main_pitch() const noexcept { return enable_main_pitch_; }
    void set_enable_main_pitch(bool v) noexcept { enable_main_pitch_ = v; }

    [[nodiscard]] bool add_to_key() const noexcept { return add_to_key_; }
    void set_add_to_key(bool v) noexcept { add_to_key_ = v; }

    // Samples Accessors
    [[nodiscard]] const std::vector<float>& sample_l() const noexcept { return sample_l_; }
    [[nodiscard]] const std::vector<float>& sample_r() const noexcept { return sample_r_; }
    [[nodiscard]] size_t sample_frames() const noexcept { return sample_l_.size(); }
    [[nodiscard]] int bit_depth() const noexcept { return bit_depth_; }
    [[nodiscard]] uint32_t file_sample_rate() const noexcept { return file_sample_rate_; }

private:
    void update_filter() {
        domain::dsp::BiquadType type = domain::dsp::BiquadType::Lowpass;
        if (filter_type_ == 1) type = domain::dsp::BiquadType::Highpass;
        else if (filter_type_ == 2) type = domain::dsp::BiquadType::Bandpass;

        // Logarithmic cutoff mapping from 20 Hz to 20,000 Hz
        double cutoff = 20.0 * std::pow(1000.0, static_cast<double>(filter_mod_x_));
        double q = 0.707 + static_cast<double>(filter_mod_y_) * 9.293;
        filter_l_.set_parameters(type, cutoff, q, 0.0, sample_rate_);
        filter_r_.set_parameters(type, cutoff, q, 0.0, sample_rate_);
    }

    void note_on(uint8_t pitch, uint8_t velocity) {
        // Calculate semitone transposition relative to root key (default 60 = C5)
        double semi = static_cast<int>(pitch) - static_cast<int>(root_key_);
        if (add_to_key_) {
            semi += static_cast<int>(root_key_) - 60;
        }
        if (enable_main_pitch_) {
            semi += pitch_shift_ + (fine_tune_cents_ / 100.0);
        }
        double sr_ratio = (file_sample_rate_ > 0 && sample_rate_ > 0) ? (static_cast<double>(file_sample_rate_) / sample_rate_) : 1.0;
        double speed = std::pow(2.0, semi / 12.0) * time_mul_ * sr_ratio;

        for (auto& v : voices_) {
            if (!v.active) {
                v.active = true;
                v.note = pitch;
                v.velocity = velocity;
                v.position = static_cast<double>(start_offset_ * sample_l_.size());
                v.speed = speed;
                v.direction = 1;
                v.env_stage = env_enabled_ ? (env_delay_ > 0.0f ? 1 : 2) : 0;
                v.env_level = env_enabled_ ? 0.0f : 1.0f;
                v.stage_time = 0.0f;
                v.lfo_phase = 0.0;
                v.declick_gain = 1.0f;
                v.declick_step = 0.05f;
                return;
            }
        }
        // Voice stealing: re-use first voice if all 16 are full
        voices_[0].active = true;
        voices_[0].note = pitch;
        voices_[0].velocity = velocity;
        voices_[0].position = static_cast<double>(start_offset_ * sample_l_.size());
        voices_[0].speed = speed;
        voices_[0].direction = 1;
        voices_[0].env_stage = env_enabled_ ? (env_delay_ > 0.0f ? 1 : 2) : 0;
        voices_[0].env_level = env_enabled_ ? 0.0f : 1.0f;
        voices_[0].stage_time = 0.0f;
        voices_[0].lfo_phase = 0.0;
        voices_[0].declick_gain = 1.0f;
        voices_[0].declick_step = 0.05f;
    }

    void note_off(uint8_t pitch) {
        for (auto& v : voices_) {
            if (v.active && v.note == pitch) {
                v.env_stage = 6; // Release stage
                v.stage_time = 0.0f;
                float step = 0.05f; // default ~5ms
                if (declicking_mode_ == 1) step = 0.2f; // ~1ms transient
                else if (declicking_mode_ == 2) step = 0.01f; // ~20ms smooth
                v.declick_step = step;
            }
        }
    }

    double sample_rate_{44100.0};
    uint8_t root_key_{60}; // C5 by default (MIDI 60)
    float fine_tune_cents_{0.0f};
    float pitch_shift_{0.0f};
    float master_vol_{0.8f};
    float pan_{0.0f};
    float time_mul_{1.0f};
    float time_length_{0.0f};
    int stretch_mode_{0}; // 0 = Resample, 1 = Auto, 2 = Stretch, 3 = Slice

    // Precomputed Effects
    bool remove_dc_{false};
    bool reverse_polarity_{false};
    bool normalize_{false};
    bool reverse_{false};
    bool fade_stereo_{false};
    bool swap_stereo_{false};
    float smp_start_{0.0f};
    float smp_length_{1.0f};
    float in_fade_{0.0f};
    float out_fade_{0.0f};
    float crossfade_{0.0f};
    float trim_{0.0f};

    // Content & Playback
    bool keep_on_disk_{false};
    bool resample_enabled_{true};
    bool load_regions_{false};
    bool load_slice_markers_{false};
    int declicking_mode_{0}; // 0=Out only, 1=Transient, 2=Smooth
    float start_offset_{0.0f};
    bool use_loop_points_{false};
    bool ping_pong_loop_{false};

    // Envelope
    bool env_enabled_{false};
    float env_delay_{0.0f};
    float env_attack_{0.05f};
    float env_hold_{0.05f};
    float env_decay_{0.2f};
    float env_sustain_{0.5f};
    float env_release_{0.2f};
    float env_att_tension_{0.0f};
    float env_dec_tension_{0.0f};
    bool env_tempo_sync_{false};

    // LFO
    int lfo_shape_{0}; // 0=Sine, 1=Triangle, 2=Square
    float lfo_delay_{0.0f};
    float lfo_attack_{0.0f};
    float lfo_amount_{0.0f};
    float lfo_speed_{2.0f};
    bool lfo_tempo_sync_{false};
    bool lfo_global_{false};

    // Filter
    float filter_mod_x_{1.0f}; // Cutoff
    float filter_mod_y_{0.0f}; // Resonance
    int filter_type_{0};       // 0=Fast LP, 1=Fast HP, 2=Fast BP
    domain::dsp::BiquadFilter filter_l_;
    domain::dsp::BiquadFilter filter_r_;

    // Bottom
    bool enable_main_pitch_{true};
    bool add_to_key_{false};

    std::string filename_{"Basic 808 Kick.wav"};
    int bit_depth_{16};
    uint32_t file_sample_rate_{44100};

    std::vector<float> raw_sample_l_;
    std::vector<float> raw_sample_r_;
    std::vector<float> sample_l_;
    std::vector<float> sample_r_;

    AudioClipVoice voices_[16];
};

} // namespace digidaw::adapters::plugins
