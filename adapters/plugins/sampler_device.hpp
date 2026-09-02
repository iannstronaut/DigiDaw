#pragma once

#include "../../domain/devices/device.hpp"
#include "../../domain/dsp/resampler.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

namespace digidaw::adapters::plugins {

struct SamplerVoice {
    bool active{false};
    uint8_t note{60};
    uint8_t velocity{100};
    double position{0.0};
    double speed{1.0};
    float env_level{0.0f};
    int env_stage{0}; // 0 = Idle, 1 = Attack, 2 = Decay, 3 = Sustain, 4 = Release
};

class SamplerDevice : public domain::IDevice {
public:
    SamplerDevice() {
        generate_default_sample();
    }

    [[nodiscard]] domain::DeviceUid uid() const override { return "core.generator.sampler"; }
    [[nodiscard]] std::string name() const override { return "DirectWave Sampler"; }
    [[nodiscard]] domain::DeviceCategory category() const override { return domain::DeviceCategory::Generator; }

    void prepare(double sample_rate, size_t /*max_block_size*/) override {
        sample_rate_ = sample_rate > 0 ? sample_rate : 44100.0;
        reset();
    }

    void reset() override {
        for (auto& v : voices_) {
            v.active = false;
            v.env_stage = 0;
            v.env_level = 0.0f;
            v.position = 0.0;
        }
    }

    void load_sample(std::vector<float> left, std::vector<float> right, uint8_t root_key = 60) {
        sample_l_ = std::move(left);
        sample_r_ = std::move(right);
        root_key_ = root_key;
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

        const float attack_step = 1.0f / (std::max(0.001f, attack_s_) * static_cast<float>(sample_rate_));
        const float release_step = 1.0f / (std::max(0.01f, release_s_) * static_cast<float>(sample_rate_));

        for (size_t f = 0; f < frames; ++f) {
            float out_l = 0.0f;
            float out_r = 0.0f;

            for (auto& v : voices_) {
                if (!v.active) continue;

                if (v.env_stage == 1) {
                    v.env_level += attack_step;
                    if (v.env_level >= 1.0f) {
                        v.env_level = 1.0f;
                        v.env_stage = 3;
                    }
                } else if (v.env_stage == 4) {
                    v.env_level -= release_step;
                    if (v.env_level <= 0.0f) {
                        v.env_level = 0.0f;
                        v.active = false;
                        continue;
                    }
                }

                size_t pos_int = static_cast<size_t>(v.position);
                float frac = static_cast<float>(v.position - pos_int);

                if (pos_int + 2 >= sample_l_.size()) {
                    v.active = false;
                    continue;
                }

                float ym1_l = (pos_int > 0) ? sample_l_[pos_int - 1] : sample_l_[0];
                float y0_l  = sample_l_[pos_int];
                float y1_l  = sample_l_[pos_int + 1];
                float y2_l  = (pos_int + 2 < sample_l_.size()) ? sample_l_[pos_int + 2] : y1_l;

                float ym1_r = (pos_int > 0) ? sample_r_[pos_int - 1] : sample_r_[0];
                float y0_r  = sample_r_[pos_int];
                float y1_r  = sample_r_[pos_int + 1];
                float y2_r  = (pos_int + 2 < sample_r_.size()) ? sample_r_[pos_int + 2] : y1_r;

                float sample_val_l = domain::dsp::HermiteResampler::interpolate_4pt(ym1_l, y0_l, y1_l, y2_l, frac);
                float sample_val_r = domain::dsp::HermiteResampler::interpolate_4pt(ym1_r, y0_r, y1_r, y2_r, frac);

                float gain = v.env_level * (v.velocity / 127.0f);
                out_l += sample_val_l * gain;
                out_r += sample_val_r * gain;

                v.position += v.speed;
            }

            audio.left[f] += out_l;
            audio.right[f] += out_r;
        }
    }

    void set_parameter(uint32_t param_id, float val) override {
        float clamped = std::clamp(val, 0.0f, 1.0f);
        switch (param_id) {
            case 0: root_key_ = static_cast<uint8_t>(clamped * 127.0f); break;
            case 1: attack_s_ = 0.001f + clamped * 1.999f; break;
            case 2: release_s_ = 0.01f + clamped * 4.99f; break;
            default: break;
        }
    }

    [[nodiscard]] float get_parameter(uint32_t param_id) const override {
        switch (param_id) {
            case 0: return root_key_ / 127.0f;
            case 1: return (attack_s_ - 0.001f) / 1.999f;
            case 2: return (release_s_ - 0.01f) / 4.99f;
            default: return 0.0f;
        }
    }

    [[nodiscard]] std::vector<domain::ParamDesc> parameters() const override {
        return {
            {0, "Root Key", 60.0f / 127.0f, 0.0f, 127.0f, "MIDI"},
            {1, "Attack", 0.005f, 0.001f, 2.0f, "s"},
            {2, "Release", 0.25f, 0.01f, 5.0f, "s"}
        };
    }

    void set_root_key(uint8_t key) noexcept { root_key_ = key; }
    [[nodiscard]] uint8_t root_key() const noexcept { return root_key_; }

    [[nodiscard]] std::vector<uint8_t> save_state() const override {
        std::vector<uint8_t> state(sizeof(uint8_t) + sizeof(float) * 2);
        state[0] = root_key_;
        float* p = reinterpret_cast<float*>(state.data() + 1);
        p[0] = attack_s_;
        p[1] = release_s_;
        return state;
    }

    domain::Result<void> load_state(std::span<const uint8_t> state_data) override {
        if (state_data.size() < sizeof(uint8_t) + sizeof(float) * 2) {
            return domain::Result<void>(domain::ErrorCode::StateIncompatible);
        }
        root_key_ = state_data[0];
        const float* p = reinterpret_cast<const float*>(state_data.data() + 1);
        attack_s_ = p[0];
        release_s_ = p[1];
        return domain::Result<void>::ok();
    }

private:
    void note_on(uint8_t pitch, uint8_t velocity) {
        for (auto& v : voices_) {
            if (!v.active) {
                v.active = true;
                v.note = pitch;
                v.velocity = velocity;
                v.position = 0.0;
                v.speed = std::pow(2.0, (static_cast<int>(pitch) - static_cast<int>(root_key_)) / 12.0);
                v.env_level = 0.0f;
                v.env_stage = 1;
                return;
            }
        }
    }

    void note_off(uint8_t pitch) {
        for (auto& v : voices_) {
            if (v.active && v.note == pitch) {
                v.env_stage = 4;
            }
        }
    }

    void generate_default_sample() {
        const size_t len = 44100;
        sample_l_.resize(len);
        sample_r_.resize(len);

        for (size_t i = 0; i < len; ++i) {
            double t = static_cast<double>(i) / 44100.0;
            double decay = std::exp(-3.0 * t);
            double sig = std::sin(2.0 * 3.14159265 * 261.63 * t) * 0.6
                       + std::sin(2.0 * 3.14159265 * 523.25 * t) * 0.3
                       + std::sin(2.0 * 3.14159265 * 784.88 * t) * 0.1;
            float val = static_cast<float>(sig * decay);
            sample_l_[i] = val;
            sample_r_[i] = val;
        }
    }

    double sample_rate_{44100.0};
    uint8_t root_key_{60};
    float attack_s_{0.005f};
    float release_s_{0.25f};

    std::vector<float> sample_l_;
    std::vector<float> sample_r_;
    SamplerVoice voices_[16];
};

} // namespace digidaw::adapters::plugins
