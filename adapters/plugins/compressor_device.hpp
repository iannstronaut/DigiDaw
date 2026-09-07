#pragma once

#include "../../domain/devices/device.hpp"
#include <cmath>
#include <vector>
#include <algorithm>

namespace digidaw::adapters::plugins {

class CompressorDevice : public domain::IDevice {
public:
    CompressorDevice() {
        set_threshold(-12.0f);
        set_ratio(4.0f);
        set_attack_ms(10.0f);
        set_release_ms(100.0f);
        set_makeup_db(0.0f);
    }

    [[nodiscard]] domain::DeviceUid uid() const override { return "core.fx.compressor"; }
    [[nodiscard]] std::string name() const override { return "Stereo Compressor"; }
    [[nodiscard]] domain::DeviceCategory category() const override { return domain::DeviceCategory::Effect; }

    void prepare(double sample_rate, size_t /*max_block_size*/) override {
        sample_rate_ = sample_rate > 0 ? sample_rate : 44100.0;
        update_time_constants();
        envelope_ = 0.0f;
    }

    void reset() override {
        envelope_ = 0.0f;
    }

    void process(domain::AudioBufferView& audio, std::span<const domain::MidiEvent> /*midi*/) override {
        const size_t frames = audio.frames;
        float* left_ptr = audio.left;
        float* right_ptr = audio.right;
        if (!left_ptr || !right_ptr || frames == 0) return;

        const float makeup_linear = std::pow(10.0f, makeup_db_ / 20.0f);
        const float slope = 1.0f - 1.0f / ratio_;
        const float thresh_lin = std::pow(10.0f, threshold_db_ / 20.0f);

        for (size_t i = 0; i < frames; ++i) {
            float in_l = left_ptr[i];
            float in_r = right_ptr[i];

            // Peak detector
            float in_peak = std::max(std::abs(in_l), std::abs(in_r));

            // Fast path: skip expensive log10 when peak is below threshold
            float target_reduction_db = 0.0f;
            if (in_peak > thresh_lin) {
                float in_db = 20.0f * std::log10(in_peak);
                target_reduction_db = slope * (in_db - threshold_db_);
            }

            // Ballistic filter (Attack / Release)
            if (target_reduction_db > envelope_) {
                envelope_ += attack_coeff_ * (target_reduction_db - envelope_);
            } else {
                envelope_ += release_coeff_ * (target_reduction_db - envelope_);
            }
            if (envelope_ < 1e-6f) envelope_ = 0.0f;

            // Fast path: skip pow when envelope is 0
            float gain = (envelope_ == 0.0f) ? makeup_linear : (std::pow(10.0f, -envelope_ * 0.05f) * makeup_linear);

            left_ptr[i] = in_l * gain;
            right_ptr[i] = in_r * gain;
        }
    }

    void set_parameter(uint32_t param_id, float val) override {
        float clamped = std::clamp(val, 0.0f, 1.0f);
        switch (param_id) {
            case 0: set_threshold(-60.0f + clamped * 60.0f); break;
            case 1: set_ratio(1.0f + clamped * 19.0f); break;
            case 2: set_attack_ms(0.1f + clamped * 99.9f); break;
            case 3: set_release_ms(10.0f + clamped * 990.0f); break;
            case 4: set_makeup_db(clamped * 24.0f); break;
            default: break;
        }
    }

    [[nodiscard]] float get_parameter(uint32_t param_id) const override {
        switch (param_id) {
            case 0: return (threshold_db_ + 60.0f) / 60.0f;
            case 1: return (ratio_ - 1.0f) / 19.0f;
            case 2: return (attack_ms_ - 0.1f) / 99.9f;
            case 3: return (release_ms_ - 10.0f) / 990.0f;
            case 4: return makeup_db_ / 24.0f;
            default: return 0.0f;
        }
    }

    [[nodiscard]] std::vector<domain::ParamDesc> parameters() const override {
        return {
            {0, "Threshold", 0.8f, -60.0f, 0.0f, "dB"},
            {1, "Ratio", 0.158f, 1.0f, 20.0f, ":1"},
            {2, "Attack", 0.1f, 0.1f, 100.0f, "ms"},
            {3, "Release", 0.091f, 10.0f, 1000.0f, "ms"},
            {4, "Makeup Gain", 0.0f, 0.0f, 24.0f, "dB"}
        };
    }

    // Direct Parameter Setters
    void set_threshold(float db) noexcept { threshold_db_ = std::clamp(db, -60.0f, 0.0f); }
    [[nodiscard]] float threshold() const noexcept { return threshold_db_; }

    void set_ratio(float ratio) noexcept { ratio_ = std::max(1.0f, ratio); }
    [[nodiscard]] float ratio() const noexcept { return ratio_; }

    void set_attack_ms(float ms) noexcept {
        attack_ms_ = std::max(0.1f, ms);
        update_time_constants();
    }
    [[nodiscard]] float attack_ms() const noexcept { return attack_ms_; }

    void set_release_ms(float ms) noexcept {
        release_ms_ = std::max(1.0f, ms);
        update_time_constants();
    }
    [[nodiscard]] float release_ms() const noexcept { return release_ms_; }

    void set_makeup_db(float db) noexcept { makeup_db_ = std::clamp(db, 0.0f, 24.0f); }
    [[nodiscard]] float makeup_db() const noexcept { return makeup_db_; }

    [[nodiscard]] float current_gain_reduction_db() const noexcept { return envelope_; }

    [[nodiscard]] std::vector<uint8_t> save_state() const override {
        std::vector<uint8_t> data(sizeof(float) * 5);
        float* p = reinterpret_cast<float*>(data.data());
        p[0] = threshold_db_;
        p[1] = ratio_;
        p[2] = attack_ms_;
        p[3] = release_ms_;
        p[4] = makeup_db_;
        return data;
    }

    domain::Result<void> load_state(std::span<const uint8_t> state_data) override {
        if (state_data.size() < sizeof(float) * 5) {
            return domain::Result<void>(domain::ErrorCode::StateIncompatible);
        }
        const float* p = reinterpret_cast<const float*>(state_data.data());
        set_threshold(p[0]);
        set_ratio(p[1]);
        set_attack_ms(p[2]);
        set_release_ms(p[3]);
        set_makeup_db(p[4]);
        return domain::Result<void>::ok();
    }

private:
    void update_time_constants() {
        if (sample_rate_ <= 0.0) return;
        attack_coeff_ = 1.0f - std::exp(-1.0f / (0.001f * attack_ms_ * static_cast<float>(sample_rate_)));
        release_coeff_ = 1.0f - std::exp(-1.0f / (0.001f * release_ms_ * static_cast<float>(sample_rate_)));
    }

    double sample_rate_{44100.0};
    float threshold_db_{-12.0f};
    float ratio_{4.0f};
    float attack_ms_{10.0f};
    float release_ms_{100.0f};
    float makeup_db_{0.0f};

    float attack_coeff_{0.01f};
    float release_coeff_{0.001f};
    float envelope_{0.0f};
};

} // namespace digidaw::adapters::plugins
