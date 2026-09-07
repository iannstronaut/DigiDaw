#pragma once

#include "../../domain/devices/device.hpp"
#include "../../domain/dsp/one_pole.hpp"
#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace digidaw::adapters::plugins {

class DelayDevice : public domain::IDevice {
public:
    DelayDevice() = default;

    [[nodiscard]] domain::DeviceUid uid() const override {
        return "core.fx.delay";
    }

    [[nodiscard]] std::string name() const override {
        return "Stereo Delay";
    }

    [[nodiscard]] domain::DeviceCategory category() const override {
        return domain::DeviceCategory::Effect;
    }

    void prepare(double sample_rate, size_t /*max_block_size*/) override {
        sample_rate_ = (sample_rate > 0.0) ? sample_rate : 44100.0;
        // Max 2 seconds delay
        max_delay_samples_ = static_cast<size_t>(sample_rate_ * 2.0);
        buffer_l_.assign(max_delay_samples_, 0.0f);
        buffer_r_.assign(max_delay_samples_, 0.0f);
        write_pos_ = 0;
        damp_l_.set_sample_rate(sample_rate_);
        damp_r_.set_sample_rate(sample_rate_);
        update_delay_times();
    }

    void reset() override {
        std::fill(buffer_l_.begin(), buffer_l_.end(), 0.0f);
        std::fill(buffer_r_.begin(), buffer_r_.end(), 0.0f);
        write_pos_ = 0;
    }

    void process(domain::AudioBufferView& buffer, std::span<const domain::MidiEvent> /*midi*/) override {
        if (max_delay_samples_ == 0) return;

        const float damp_val = damp_l_.current();
        const float fb_gain = damp_val * feedback_;
        const float wet = wet_mix_;
        const float dry = 1.0f - wet;
        float* left_ptr = buffer.left;
        float* right_ptr = buffer.right;

        for (size_t f = 0; f < buffer.frames; ++f) {
            float in_l = left_ptr ? left_ptr[f] : 0.0f;
            float in_r = right_ptr ? right_ptr[f] : 0.0f;

            // Fast branch-wrapping instead of 64-bit integer division
            size_t read_l = write_pos_ + max_delay_samples_ - delay_samples_l_;
            if (read_l >= max_delay_samples_) read_l -= max_delay_samples_;
            size_t read_r = write_pos_ + max_delay_samples_ - delay_samples_r_;
            if (read_r >= max_delay_samples_) read_r -= max_delay_samples_;

            float delayed_l = buffer_l_[read_l];
            float delayed_r = buffer_r_[read_r];

            // Feedback with anti-denormal flush
            float fb_l = delayed_l * fb_gain;
            float fb_r = delayed_r * fb_gain;
            if (std::abs(fb_l) < 1e-15f) fb_l = 0.0f;
            if (std::abs(fb_r) < 1e-15f) fb_r = 0.0f;

            // Write to buffer
            buffer_l_[write_pos_] = in_l + fb_l;
            buffer_r_[write_pos_] = in_r + fb_r;

            if (++write_pos_ >= max_delay_samples_) write_pos_ = 0;

            // Mix wet & dry
            if (left_ptr) left_ptr[f] = in_l * dry + delayed_l * wet;
            if (right_ptr) right_ptr[f] = in_r * dry + delayed_r * wet;
        }
    }

    void set_parameter(uint32_t param_id, float val) override {
        switch (param_id) {
            case 0: time_ms_l_ = 10.0f + val * 990.0f; update_delay_times(); break;
            case 1: time_ms_r_ = 10.0f + val * 990.0f; update_delay_times(); break;
            case 2: feedback_ = std::clamp(val * 0.95f, 0.0f, 0.95f); break;
            case 3: wet_mix_ = std::clamp(val, 0.0f, 1.0f); break;
            default: break;
        }
    }

    [[nodiscard]] float get_parameter(uint32_t param_id) const override {
        switch (param_id) {
            case 0: return (time_ms_l_ - 10.0f) / 990.0f;
            case 1: return (time_ms_r_ - 10.0f) / 990.0f;
            case 2: return feedback_ / 0.95f;
            case 3: return wet_mix_;
            default: return 0.0f;
        }
    }

    [[nodiscard]] std::vector<domain::ParamDesc> parameters() const override {
        return {
            {0, "Time L", 0.35f, 0.0f, 1.0f, "ms"},
            {1, "Time R", 0.5f, 0.0f, 1.0f, "ms"},
            {2, "Feedback", 0.4f, 0.0f, 1.0f, "%"},
            {3, "Wet Mix", 0.3f, 0.0f, 1.0f, "%"}
        };
    }

    [[nodiscard]] std::vector<uint8_t> save_state() const override {
        std::vector<uint8_t> data(sizeof(float) * 4);
        float params[4] = {time_ms_l_, time_ms_r_, feedback_, wet_mix_};
        std::memcpy(data.data(), params, data.size());
        return data;
    }

    domain::Result<void> load_state(std::span<const uint8_t> data) override {
        if (data.size() < sizeof(float) * 4) {
            return domain::Result<void>(domain::ErrorCode::StateIncompatible);
        }
        float params[4];
        std::memcpy(params, data.data(), sizeof(params));
        time_ms_l_ = params[0];
        time_ms_r_ = params[1];
        feedback_ = params[2];
        wet_mix_ = params[3];
        update_delay_times();
        return domain::Result<void>::ok();
    }

private:
    void update_delay_times() {
        delay_samples_l_ = static_cast<size_t>(time_ms_l_ * 0.001 * sample_rate_);
        delay_samples_r_ = static_cast<size_t>(time_ms_r_ * 0.001 * sample_rate_);
        if (delay_samples_l_ >= max_delay_samples_) delay_samples_l_ = max_delay_samples_ - 1;
        if (delay_samples_r_ >= max_delay_samples_) delay_samples_r_ = max_delay_samples_ - 1;
    }

    double sample_rate_{44100.0};
    size_t max_delay_samples_{88200};
    std::vector<float> buffer_l_;
    std::vector<float> buffer_r_;
    size_t write_pos_{0};

    float time_ms_l_{350.0f};
    float time_ms_r_{500.0f};
    size_t delay_samples_l_{15435};
    size_t delay_samples_r_{22050};
    float feedback_{0.4f};
    float wet_mix_{0.3f};

    domain::dsp::OnePoleSmoother damp_l_{0.8f, 0.05f};
    domain::dsp::OnePoleSmoother damp_r_{0.8f, 0.05f};
};

} // namespace digidaw::adapters::plugins
