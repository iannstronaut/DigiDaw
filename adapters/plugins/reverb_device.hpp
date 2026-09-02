#pragma once

#include "../../domain/devices/device.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

namespace digidaw::adapters::plugins {

class CombFilter {
public:
    void set_buffer(size_t size) {
        buffer_.assign(size, 0.0f);
        index_ = 0;
        filter_store_ = 0.0f;
    }

    float process(float input, float feedback, float damp) {
        if (buffer_.empty()) return input;
        float output = buffer_[index_];
        filter_store_ = (output * (1.0f - damp)) + (filter_store_ * damp);
        buffer_[index_] = input + (filter_store_ * feedback);
        if (++index_ >= buffer_.size()) index_ = 0;
        return output;
    }

    void reset() {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        filter_store_ = 0.0f;
        index_ = 0;
    }

private:
    std::vector<float> buffer_;
    size_t index_{0};
    float filter_store_{0.0f};
};

class AllpassFilter {
public:
    void set_buffer(size_t size) {
        buffer_.assign(size, 0.0f);
        index_ = 0;
    }

    float process(float input) {
        if (buffer_.empty()) return input;
        float buf_out = buffer_[index_];
        float output = -input + buf_out;
        buffer_[index_] = input + (buf_out * 0.5f);
        if (++index_ >= buffer_.size()) index_ = 0;
        return output;
    }

    void reset() {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        index_ = 0;
    }

private:
    std::vector<float> buffer_;
    size_t index_{0};
};

class ReverbDevice : public domain::IDevice {
public:
    ReverbDevice() {
        set_room_size(0.75f);
        set_damping(0.3f);
        set_wet(0.35f);
        set_dry(0.85f);
        prepare(44100.0, 512);
    }

    [[nodiscard]] domain::DeviceUid uid() const override { return "core.fx.reverb"; }
    [[nodiscard]] std::string name() const override { return "Algorithmic Reverb"; }
    [[nodiscard]] domain::DeviceCategory category() const override { return domain::DeviceCategory::Effect; }

    void prepare(double sample_rate, size_t /*max_block_size*/) override {
        double sr_scale = (sample_rate > 0 ? sample_rate : 44100.0) / 44100.0;

        const size_t comb_tuning[4] = {
            static_cast<size_t>(1116 * sr_scale),
            static_cast<size_t>(1188 * sr_scale),
            static_cast<size_t>(1277 * sr_scale),
            static_cast<size_t>(1356 * sr_scale)
        };

        const size_t allpass_tuning[2] = {
            static_cast<size_t>(225 * sr_scale),
            static_cast<size_t>(556 * sr_scale)
        };

        for (int i = 0; i < 4; ++i) {
            comb_l_[i].set_buffer(comb_tuning[i]);
            comb_r_[i].set_buffer(comb_tuning[i] + 23); // Decorrelation offset for stereo
        }

        for (int i = 0; i < 2; ++i) {
            allpass_l_[i].set_buffer(allpass_tuning[i]);
            allpass_r_[i].set_buffer(allpass_tuning[i] + 23);
        }
    }

    void reset() override {
        for (int i = 0; i < 4; ++i) {
            comb_l_[i].reset();
            comb_r_[i].reset();
        }
        for (int i = 0; i < 2; ++i) {
            allpass_l_[i].reset();
            allpass_r_[i].reset();
        }
    }

    void process(domain::AudioBufferView& audio, std::span<const domain::MidiEvent> /*midi*/) override {
        const size_t frames = audio.frames;
        if (!audio.left || !audio.right || frames == 0) return;

        const float feedback = 0.7f + room_size_ * 0.28f;

        for (size_t i = 0; i < frames; ++i) {
            float in_l = audio.left[i];
            float in_r = audio.right[i];
            float in_mono = (in_l + in_r) * 0.5f;

            // 1. Parallel Comb Filters
            float out_comb_l = 0.0f;
            float out_comb_r = 0.0f;
            for (int c = 0; c < 4; ++c) {
                out_comb_l += comb_l_[c].process(in_mono, feedback, damping_);
                out_comb_r += comb_r_[c].process(in_mono, feedback, damping_);
            }

            // 2. Series Allpass Diffusers
            for (int a = 0; a < 2; ++a) {
                out_comb_l = allpass_l_[a].process(out_comb_l);
                out_comb_r = allpass_r_[a].process(out_comb_r);
            }

            // 3. Dry / Wet Mix
            audio.left[i] = (in_l * dry_) + (out_comb_l * wet_);
            audio.right[i] = (in_r * dry_) + (out_comb_r * wet_);
        }
    }

    void set_parameter(uint32_t param_id, float val) override {
        float clamped = std::clamp(val, 0.0f, 1.0f);
        switch (param_id) {
            case 0: set_room_size(clamped); break;
            case 1: set_damping(clamped); break;
            case 2: set_wet(clamped); break;
            case 3: set_dry(clamped); break;
            default: break;
        }
    }

    [[nodiscard]] float get_parameter(uint32_t param_id) const override {
        switch (param_id) {
            case 0: return room_size_;
            case 1: return damping_;
            case 2: return wet_;
            case 3: return dry_;
            default: return 0.0f;
        }
    }

    [[nodiscard]] std::vector<domain::ParamDesc> parameters() const override {
        return {
            {0, "Room Size", 0.75f, 0.0f, 1.0f, "%"},
            {1, "Damping", 0.3f, 0.0f, 1.0f, "%"},
            {2, "Wet", 0.35f, 0.0f, 1.0f, "%"},
            {3, "Dry", 0.85f, 0.0f, 1.0f, "%"}
        };
    }

    void set_room_size(float size) noexcept { room_size_ = std::clamp(size, 0.0f, 1.0f); }
    [[nodiscard]] float room_size() const noexcept { return room_size_; }

    void set_damping(float damp) noexcept { damping_ = std::clamp(damp, 0.0f, 1.0f); }
    [[nodiscard]] float damping() const noexcept { return damping_; }

    void set_wet(float wet) noexcept { wet_ = std::clamp(wet, 0.0f, 1.0f); }
    [[nodiscard]] float wet() const noexcept { return wet_; }

    void set_dry(float dry) noexcept { dry_ = std::clamp(dry, 0.0f, 1.0f); }
    [[nodiscard]] float dry() const noexcept { return dry_; }

    [[nodiscard]] std::vector<uint8_t> save_state() const override {
        std::vector<uint8_t> data(sizeof(float) * 4);
        float* p = reinterpret_cast<float*>(data.data());
        p[0] = room_size_;
        p[1] = damping_;
        p[2] = wet_;
        p[3] = dry_;
        return data;
    }

    domain::Result<void> load_state(std::span<const uint8_t> state_data) override {
        if (state_data.size() < sizeof(float) * 4) {
            return domain::Result<void>(domain::ErrorCode::StateIncompatible);
        }
        const float* p = reinterpret_cast<const float*>(state_data.data());
        set_room_size(p[0]);
        set_damping(p[1]);
        set_wet(p[2]);
        set_dry(p[3]);
        return domain::Result<void>::ok();
    }

private:
    CombFilter comb_l_[4];
    CombFilter comb_r_[4];
    AllpassFilter allpass_l_[2];
    AllpassFilter allpass_r_[2];

    float room_size_{0.75f};
    float damping_{0.3f};
    float wet_{0.35f};
    float dry_{0.85f};
};

} // namespace digidaw::adapters::plugins
