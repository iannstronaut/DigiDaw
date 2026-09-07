#pragma once

#include "../../domain/devices/device.hpp"
#include "../../domain/dsp/biquad.hpp"
#include <cmath>
#include <vector>
#include <array>
#include <cstring>

namespace digidaw::adapters::plugins {

enum class OscWaveform : uint8_t {
    Sine = 0,
    Saw = 1,
    Square = 2,
    Noise = 3
};

struct Voice {
    bool active{false};
    uint8_t note{60};
    float velocity{0.8f};
    double phase1{0.0};
    double phase2{0.0};
    double phase3{0.0};
    // ADSR state
    enum class EnvStage { Idle, Attack, Decay, Sustain, Release } stage{EnvStage::Idle};
    float env_val{0.0f};
};

class Synth3xOsc : public domain::IDevice {
public:
    static constexpr size_t MaxVoices = 16;

    Synth3xOsc() {
        voices_.resize(MaxVoices);
        update_osc_multipliers();
    }

    [[nodiscard]] domain::DeviceUid uid() const override {
        return "core.generator.3xosc";
    }

    [[nodiscard]] std::string name() const override {
        return "3xOsc Synth";
    }

    [[nodiscard]] domain::DeviceCategory category() const override {
        return domain::DeviceCategory::Generator;
    }

    void prepare(double sample_rate, size_t /*max_block_size*/) override {
        sample_rate_ = (sample_rate > 0.0) ? sample_rate : 44100.0;
        update_osc_multipliers();
        filter_.set_parameters(domain::dsp::BiquadType::Lowpass, filter_cutoff_, filter_q_, 0.0, sample_rate_);
        filter_.reset();
    }

    void reset() override {
        for (auto& v : voices_) {
            v.active = false;
            v.stage = Voice::EnvStage::Idle;
            v.env_val = 0.0f;
        }
        filter_.reset();
    }

    void process(domain::AudioBufferView& buffer, std::span<const domain::MidiEvent> midi) override {
        // Handle incoming MIDI events
        for (const auto& ev : midi) {
            if (ev.is_note_on()) {
                trigger_note(ev.note_number(), static_cast<float>(ev.velocity()) / 127.0f);
            } else if (ev.is_note_off()) {
                release_note(ev.note_number());
            } else if (ev.is_control_change() && (ev.data1 == 120 || ev.data1 == 123)) {
                all_notes_off();
            }
        }

        const float inv_sr = static_cast<float>(1.0 / sample_rate_);
        const float attack_step = 1.0f / std::max(0.001f, attack_sec_ * static_cast<float>(sample_rate_));
        const float decay_step = 1.0f / std::max(0.001f, decay_sec_ * static_cast<float>(sample_rate_));
        const float release_step = 1.0f / std::max(0.001f, release_sec_ * static_cast<float>(sample_rate_));
        float* out_l = buffer.left;
        float* out_r = buffer.right;

        struct Active3xVoice {
            Voice* v;
            double step1;
            double step2;
            double step3;
        };
        std::array<Active3xVoice, MaxVoices> active_voices{};
        size_t num_active = 0;
        for (auto& v : voices_) {
            if (!v.active) continue;
            const double base_freq = 440.0 * std::pow(2.0, (v.note - 69) / 12.0);
            const double f1 = base_freq * osc1_mult_;
            const double f2 = base_freq * osc2_mult_;
            const double f3 = base_freq * osc3_mult_;
            active_voices[num_active++] = {&v, f1 * inv_sr, f2 * inv_sr, f3 * inv_sr};
        }

        if (num_active == 0) {
            return;
        }

        for (size_t f = 0; f < buffer.frames; ++f) {
            float sample_l = 0.0f;
            float sample_r = 0.0f;

            for (size_t vi = 0; vi < num_active; ++vi) {
                auto& v = *active_voices[vi].v;
                if (!v.active) continue;

                // Update envelope
                switch (v.stage) {
                    case Voice::EnvStage::Attack:
                        v.env_val += attack_step;
                        if (v.env_val >= 1.0f) {
                            v.env_val = 1.0f;
                            v.stage = Voice::EnvStage::Decay;
                        }
                        break;
                    case Voice::EnvStage::Decay:
                        v.env_val -= decay_step;
                        if (v.env_val <= sustain_level_) {
                            v.env_val = sustain_level_;
                            v.stage = Voice::EnvStage::Sustain;
                        }
                        break;
                    case Voice::EnvStage::Sustain:
                        v.env_val = sustain_level_;
                        break;
                    case Voice::EnvStage::Release:
                        v.env_val -= release_step;
                        if (v.env_val <= 0.0f) {
                            v.env_val = 0.0f;
                            v.active = false;
                            v.stage = Voice::EnvStage::Idle;
                        }
                        break;
                    default:
                        break;
                }

                if (!v.active) continue;

                float s1 = generate_osc(osc1_wave_, v.phase1) * osc1_vol_;
                float s2 = generate_osc(osc2_wave_, v.phase2) * osc2_vol_;
                float s3 = generate_osc(osc3_wave_, v.phase3) * osc3_vol_;

                v.phase1 += active_voices[vi].step1;
                if (v.phase1 >= 1.0) v.phase1 -= 1.0;

                v.phase2 += active_voices[vi].step2;
                if (v.phase2 >= 1.0) v.phase2 -= 1.0;

                v.phase3 += active_voices[vi].step3;
                if (v.phase3 >= 1.0) v.phase3 -= 1.0;

                float voice_out = (s1 + s2 + s3) * v.velocity * v.env_val;
                sample_l += voice_out;
                sample_r += voice_out;
            }

            // Apply filter
            sample_l = filter_.process(sample_l * master_vol_);
            sample_r = sample_l; // Mono to stereo

            if (out_l) out_l[f] += sample_l;
            if (out_r) out_r[f] += sample_r;
        }
    }

    void set_parameter(uint32_t param_id, float val) override {
        switch (param_id) {
            case 0: master_vol_ = val; break;
            case 1: osc1_wave_ = static_cast<OscWaveform>(static_cast<int>(val * 3.99f)); break;
            case 2: osc1_vol_ = val; break;
            case 3: osc2_wave_ = static_cast<OscWaveform>(static_cast<int>(val * 3.99f)); break;
            case 4: osc2_vol_ = val; break;
            case 5: osc2_semi_ = static_cast<int>((val - 0.5f) * 48.0f);
                    update_osc_multipliers();
                    break;
            case 6: filter_cutoff_ = 20.0 + std::pow(val, 2.0) * 18000.0;
                    filter_.set_parameters(domain::dsp::BiquadType::Lowpass, filter_cutoff_, filter_q_, 0.0, sample_rate_);
                    break;
            case 7: filter_q_ = 0.5 + val * 10.0;
                    filter_.set_parameters(domain::dsp::BiquadType::Lowpass, filter_cutoff_, filter_q_, 0.0, sample_rate_);
                    break;
            default: break;
        }
    }

    [[nodiscard]] float get_parameter(uint32_t param_id) const override {
        switch (param_id) {
            case 0: return master_vol_;
            case 1: return static_cast<float>(osc1_wave_) / 3.0f;
            case 2: return osc1_vol_;
            case 3: return static_cast<float>(osc2_wave_) / 3.0f;
            case 4: return osc2_vol_;
            case 5: return (osc2_semi_ / 48.0f) + 0.5f;
            case 6: return static_cast<float>(std::sqrt((filter_cutoff_ - 20.0) / 18000.0));
            case 7: return static_cast<float>((filter_q_ - 0.5) / 10.0);
            default: return 0.0f;
        }
    }

    [[nodiscard]] std::vector<domain::ParamDesc> parameters() const override {
        return {
            {0, "Master Volume", 0.8f, 0.0f, 1.0f, "%"},
            {1, "Osc1 Shape", 0.25f, 0.0f, 1.0f, ""},
            {2, "Osc1 Level", 1.0f, 0.0f, 1.0f, "%"},
            {3, "Osc2 Shape", 0.5f, 0.0f, 1.0f, ""},
            {4, "Osc2 Level", 0.5f, 0.0f, 1.0f, "%"},
            {5, "Osc2 Pitch", 0.5f, 0.0f, 1.0f, "semi"},
            {6, "Filter Cutoff", 0.8f, 0.0f, 1.0f, "Hz"},
            {7, "Filter Res", 0.2f, 0.0f, 1.0f, "Q"}
        };
    }

    [[nodiscard]] std::vector<uint8_t> save_state() const override {
        std::vector<uint8_t> data(sizeof(float) * 8);
        float params[8] = {
            master_vol_, static_cast<float>(osc1_wave_), osc1_vol_,
            static_cast<float>(osc2_wave_), osc2_vol_, static_cast<float>(osc2_semi_),
            static_cast<float>(filter_cutoff_), static_cast<float>(filter_q_)
        };
        std::memcpy(data.data(), params, data.size());
        return data;
    }

    domain::Result<void> load_state(std::span<const uint8_t> data) override {
        if (data.size() < sizeof(float) * 8) {
            return domain::Result<void>(domain::ErrorCode::StateIncompatible);
        }
        float params[8];
        std::memcpy(params, data.data(), sizeof(params));
        master_vol_ = params[0];
        osc1_wave_ = static_cast<OscWaveform>(static_cast<int>(params[1]));
        osc1_vol_ = params[2];
        osc2_wave_ = static_cast<OscWaveform>(static_cast<int>(params[3]));
        osc2_vol_ = params[4];
        osc2_semi_ = static_cast<int>(params[5]);
        update_osc_multipliers();
        filter_cutoff_ = params[6];
        filter_q_ = params[7];
        filter_.set_parameters(domain::dsp::BiquadType::Lowpass, filter_cutoff_, filter_q_, 0.0, sample_rate_);
        return domain::Result<void>::ok();
    }

private:
    void trigger_note(uint8_t note, float velocity) {
        // If this exact note is already playing, retrigger it cleanly
        for (auto& v : voices_) {
            if (v.active && v.note == note) {
                v.velocity = velocity;
                v.stage = Voice::EnvStage::Attack;
                v.env_val = 0.0f;
                v.phase1 = 0.0;
                v.phase2 = 0.0;
                v.phase3 = 0.0;
                return;
            }
        }

        // Find free voice or steal oldest
        Voice* chosen = nullptr;
        for (auto& v : voices_) {
            if (!v.active) {
                chosen = &v;
                break;
            }
        }
        if (!chosen) chosen = &voices_[0];

        chosen->active = true;
        chosen->note = note;
        chosen->velocity = velocity;
        chosen->stage = Voice::EnvStage::Attack;
        chosen->env_val = 0.0f;
        chosen->phase1 = 0.0;
        chosen->phase2 = 0.0;
        chosen->phase3 = 0.0;
    }

    void release_note(uint8_t note) {
        for (auto& v : voices_) {
            if (v.active && v.note == note && v.stage != Voice::EnvStage::Release) {
                v.stage = Voice::EnvStage::Release;
            }
        }
    }

    void all_notes_off() {
        for (auto& v : voices_) {
            if (v.active && v.stage != Voice::EnvStage::Release) {
                v.stage = Voice::EnvStage::Release;
            }
        }
    }

    float generate_osc(OscWaveform wave, double phase) const noexcept {
        switch (wave) {
            case OscWaveform::Sine:
                return static_cast<float>(std::sin(phase * 6.28318530718));
            case OscWaveform::Saw:
                return static_cast<float>(2.0 * phase - 1.0);
            case OscWaveform::Square:
                return (phase < 0.5) ? 1.0f : -1.0f;
            case OscWaveform::Noise: {
                static uint32_t seed = 123456789;
                seed = (1103515245 * seed + 12345) & 0x7FFFFFFF;
                return (static_cast<float>(seed) / 1073741824.0f) - 1.0f;
            }
        }
        return 0.0f;
    }

    double sample_rate_{44100.0};
    float master_vol_{0.8f};

    OscWaveform osc1_wave_{OscWaveform::Saw};
    float osc1_vol_{1.0f};
    int osc1_semi_{0};

    OscWaveform osc2_wave_{OscWaveform::Square};
    float osc2_vol_{0.5f};
    int osc2_semi_{0};
    float osc2_fine_{0.0f};

    OscWaveform osc3_wave_{OscWaveform::Sine};
    float osc3_vol_{0.0f};
    int osc3_semi_{-12};
    float osc3_fine_{0.0f};

    float attack_sec_{0.01f};
    float decay_sec_{0.1f};
    float sustain_level_{0.7f};
    float release_sec_{0.2f};

    double filter_cutoff_{10000.0};
    double filter_q_{1.0};
    domain::dsp::BiquadFilter filter_;

    double osc1_mult_{1.0};
    double osc2_mult_{1.0};
    double osc3_mult_{0.5};

    void update_osc_multipliers() noexcept {
        osc1_mult_ = std::pow(2.0, osc1_semi_ / 12.0);
        osc2_mult_ = std::pow(2.0, (osc2_semi_ + osc2_fine_ / 100.0) / 12.0);
        osc3_mult_ = std::pow(2.0, (osc3_semi_ + osc3_fine_ / 100.0) / 12.0);
    }

    std::vector<Voice> voices_;
};

} // namespace digidaw::adapters::plugins
