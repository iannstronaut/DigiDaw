#pragma once

#include "../../domain/devices/device.hpp"
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>

namespace digidaw::adapters::plugins {

struct DrumPad {
    std::string name{"Pad"};
    uint8_t midi_note{36};
    float volume{1.0f};
    float pan{0.0f};
    int mute_group{0};

    std::vector<float> sample;
    size_t playback_pos{0};
    bool playing{false};
    float current_velocity{1.0f};
};

class DrumSamplerDevice : public domain::IDevice {
public:
    DrumSamplerDevice() {
        init_default_kit();
    }

    [[nodiscard]] domain::DeviceUid uid() const override { return "core.generator.drum_sampler"; }
    [[nodiscard]] std::string name() const override { return "FPC Drum Machine"; }
    [[nodiscard]] domain::DeviceCategory category() const override { return domain::DeviceCategory::Generator; }

    void prepare(double /*sample_rate*/, size_t /*max_block_size*/) override {
        reset();
    }

    void reset() override {
        for (auto& pad : pads_) {
            pad.playing = false;
            pad.playback_pos = 0;
        }
    }

    void process(domain::AudioBufferView& audio, std::span<const domain::MidiEvent> midi) override {
        for (const auto& ev : midi) {
            uint8_t status = ev.status & 0xF0;
            if (status == 0x90 && ev.data2 > 0) {
                trigger_pad_by_note(ev.data1, ev.data2);
            }
        }

        const size_t frames = audio.frames;
        if (!audio.left || !audio.right || frames == 0) return;

        for (size_t f = 0; f < frames; ++f) {
            float out_l = 0.0f;
            float out_r = 0.0f;

            for (auto& pad : pads_) {
                if (!pad.playing) continue;

                if (pad.playback_pos < pad.sample.size()) {
                    float s = pad.sample[pad.playback_pos++];
                    float gain = pad.volume * pad.current_velocity * master_vol_;

                    float pan_l = (pad.pan <= 0.0f) ? 1.0f : (1.0f - pad.pan);
                    float pan_r = (pad.pan >= 0.0f) ? 1.0f : (1.0f + pad.pan);

                    out_l += s * gain * pan_l;
                    out_r += s * gain * pan_r;
                } else {
                    pad.playing = false;
                }
            }

            audio.left[f] += out_l;
            audio.right[f] += out_r;
        }
    }

    void set_parameter(uint32_t param_id, float val) override {
        if (param_id == 0) {
            master_vol_ = std::clamp(val, 0.0f, 1.0f);
        }
    }

    [[nodiscard]] float get_parameter(uint32_t param_id) const override {
        if (param_id == 0) return master_vol_;
        return 0.0f;
    }

    [[nodiscard]] std::vector<domain::ParamDesc> parameters() const override {
        return {
            {0, "Master Volume", 1.0f, 0.0f, 1.0f, "%"}
        };
    }

    void trigger_pad(size_t pad_index, uint8_t velocity = 100) {
        if (pad_index >= pads_.size()) return;
        auto& target = pads_[pad_index];

        if (target.mute_group > 0) {
            for (auto& pad : pads_) {
                if (pad.mute_group == target.mute_group) {
                    pad.playing = false;
                }
            }
        }

        target.playing = true;
        target.playback_pos = 0;
        target.current_velocity = velocity / 127.0f;
    }

    void trigger_pad_by_note(uint8_t note, uint8_t velocity) {
        for (size_t i = 0; i < pads_.size(); ++i) {
            if (pads_[i].midi_note == note) {
                trigger_pad(i, velocity);
                return;
            }
        }
    }

    [[nodiscard]] std::vector<DrumPad>& pads() noexcept { return pads_; }
    [[nodiscard]] const std::vector<DrumPad>& pads() const noexcept { return pads_; }

    [[nodiscard]] std::vector<uint8_t> save_state() const override {
        std::vector<uint8_t> state;
        const uint8_t* mv = reinterpret_cast<const uint8_t*>(&master_vol_);
        state.insert(state.end(), mv, mv + sizeof(float));

        for (const auto& pad : pads_) {
            state.push_back(pad.midi_note);
            const uint8_t* v = reinterpret_cast<const uint8_t*>(&pad.volume);
            state.insert(state.end(), v, v + sizeof(float));
        }
        return state;
    }

    domain::Result<void> load_state(std::span<const uint8_t> state_data) override {
        if (state_data.size() < sizeof(float) + pads_.size() * (1 + sizeof(float))) {
            return domain::Result<void>(domain::ErrorCode::StateIncompatible);
        }
        master_vol_ = *reinterpret_cast<const float*>(state_data.data());
        size_t offset = sizeof(float);
        for (auto& pad : pads_) {
            pad.midi_note = state_data[offset++];
            pad.volume = *reinterpret_cast<const float*>(state_data.data() + offset);
            offset += sizeof(float);
        }
        return domain::Result<void>::ok();
    }

private:
    void init_default_kit() {
        pads_.resize(16);

        const uint8_t notes[16] = {
            36, 38, 42, 46,
            37, 39, 41, 43,
            45, 47, 48, 50,
            49, 51, 53, 56
        };

        const char* names[16] = {
            "Kick", "Snare", "Closed HH", "Open HH",
            "Side Stick", "Clap", "Low Tom", "Mid Tom",
            "High Tom", "Crash", "Ride", "Cowbell",
            "Perc 1", "Perc 2", "Shaker", "Rimshot"
        };

        for (int i = 0; i < 16; ++i) {
            pads_[i].midi_note = notes[i];
            pads_[i].name = names[i];
            pads_[i].volume = 0.9f;
            pads_[i].pan = 0.0f;
            pads_[i].mute_group = (i == 2 || i == 3) ? 1 : 0;

            generate_drum_sound(pads_[i], i);
        }
    }

    void generate_drum_sound(DrumPad& pad, int index) {
        size_t len = (index == 2 || index == 4) ? 8000 : 18000;
        pad.sample.resize(len);

        for (size_t i = 0; i < len; ++i) {
            double t = static_cast<double>(i) / 44100.0;
            float val = 0.0f;

            if (index == 0) {
                double freq = 45.0 + 105.0 * std::exp(-25.0 * t);
                double env = std::exp(-10.0 * t);
                val = static_cast<float>(std::sin(2.0 * 3.14159265 * freq * t) * env);
            } else if (index == 1) {
                double tone = std::sin(2.0 * 3.14159265 * 180.0 * t) * std::exp(-18.0 * t);
                double noise = ((rand() / (double)RAND_MAX) * 2.0 - 1.0) * std::exp(-12.0 * t);
                val = static_cast<float>((tone * 0.4 + noise * 0.6));
            } else if (index == 2) {
                double noise = ((rand() / (double)RAND_MAX) * 2.0 - 1.0) * std::exp(-35.0 * t);
                val = static_cast<float>(noise * 0.6);
            } else {
                double freq = 200.0 + index * 40.0;
                double env = std::exp(-15.0 * t);
                val = static_cast<float>(std::sin(2.0 * 3.14159265 * freq * t) * env);
            }

            pad.sample[i] = val;
        }
    }

    float master_vol_{1.0f};
    std::vector<DrumPad> pads_;
};

} // namespace digidaw::adapters::plugins
