#pragma once

#include "../../domain/devices/device.hpp"
#include "../../domain/dsp/biquad.hpp"
#include <cmath>
#include <cstring>

namespace digidaw::adapters::plugins {

class ParametricEQ : public domain::IDevice {
public:
    ParametricEQ() = default;

    [[nodiscard]] domain::DeviceUid uid() const override {
        return "core.fx.parametric_eq";
    }

    [[nodiscard]] std::string name() const override {
        return "Parametric EQ";
    }

    [[nodiscard]] domain::DeviceCategory category() const override {
        return domain::DeviceCategory::Effect;
    }

    void prepare(double sample_rate, size_t /*max_block_size*/) override {
        sample_rate_ = (sample_rate > 0.0) ? sample_rate : 44100.0;
        update_filters();
        reset();
    }

    void reset() override {
        low_l_.reset(); low_r_.reset();
        mid_l_.reset(); mid_r_.reset();
        high_l_.reset(); high_r_.reset();
    }

    void process(domain::AudioBufferView& buffer, std::span<const domain::MidiEvent> /*midi*/) override {
        for (size_t f = 0; f < buffer.frames; ++f) {
            float l = buffer.left ? buffer.left[f] : 0.0f;
            float r = buffer.right ? buffer.right[f] : 0.0f;

            // Low Shelf
            l = low_l_.process(l);
            r = low_r_.process(r);

            // Mid Peaking EQ
            l = mid_l_.process(l);
            r = mid_r_.process(r);

            // High Shelf
            l = high_l_.process(l);
            r = high_r_.process(r);

            if (buffer.left) buffer.left[f] = l;
            if (buffer.right) buffer.right[f] = r;
        }
    }

    void set_parameter(uint32_t param_id, float val) override {
        switch (param_id) {
            case 0: low_gain_db_ = (val - 0.5f) * 36.0f; break;   // -18dB to +18dB
            case 1: low_freq_hz_ = 40.0 + val * 460.0; break;     // 40Hz to 500Hz
            case 2: mid_gain_db_ = (val - 0.5f) * 36.0f; break;   // -18dB to +18dB
            case 3: mid_freq_hz_ = 200.0 + val * 4800.0; break;   // 200Hz to 5000Hz
            case 4: mid_q_ = 0.5 + val * 5.0; break;
            case 5: high_gain_db_ = (val - 0.5f) * 36.0f; break;  // -18dB to +18dB
            case 6: high_freq_hz_ = 2000.0 + val * 16000.0; break;// 2kHz to 18kHz
            default: break;
        }
        update_filters();
    }

    [[nodiscard]] float get_parameter(uint32_t param_id) const override {
        switch (param_id) {
            case 0: return (low_gain_db_ / 36.0f) + 0.5f;
            case 1: return static_cast<float>((low_freq_hz_ - 40.0) / 460.0);
            case 2: return (mid_gain_db_ / 36.0f) + 0.5f;
            case 3: return static_cast<float>((mid_freq_hz_ - 200.0) / 4800.0);
            case 4: return static_cast<float>((mid_q_ - 0.5) / 5.0);
            case 5: return (high_gain_db_ / 36.0f) + 0.5f;
            case 6: return static_cast<float>((high_freq_hz_ - 2000.0) / 16000.0);
            default: return 0.0f;
        }
    }

    [[nodiscard]] std::vector<domain::ParamDesc> parameters() const override {
        return {
            {0, "Low Gain", 0.5f, 0.0f, 1.0f, "dB"},
            {1, "Low Freq", 0.15f, 0.0f, 1.0f, "Hz"},
            {2, "Mid Gain", 0.5f, 0.0f, 1.0f, "dB"},
            {3, "Mid Freq", 0.25f, 0.0f, 1.0f, "Hz"},
            {4, "Mid Q", 0.2f, 0.0f, 1.0f, ""},
            {5, "High Gain", 0.5f, 0.0f, 1.0f, "dB"},
            {6, "High Freq", 0.5f, 0.0f, 1.0f, "Hz"}
        };
    }

    [[nodiscard]] std::vector<uint8_t> save_state() const override {
        std::vector<uint8_t> data(sizeof(float) * 7);
        float params[7] = {
            low_gain_db_, static_cast<float>(low_freq_hz_),
            mid_gain_db_, static_cast<float>(mid_freq_hz_), static_cast<float>(mid_q_),
            high_gain_db_, static_cast<float>(high_freq_hz_)
        };
        std::memcpy(data.data(), params, data.size());
        return data;
    }

    domain::Result<void> load_state(std::span<const uint8_t> data) override {
        if (data.size() < sizeof(float) * 7) {
            return domain::Result<void>(domain::ErrorCode::StateIncompatible);
        }
        float params[7];
        std::memcpy(params, data.data(), sizeof(params));
        low_gain_db_ = params[0];
        low_freq_hz_ = params[1];
        mid_gain_db_ = params[2];
        mid_freq_hz_ = params[3];
        mid_q_ = params[4];
        high_gain_db_ = params[5];
        high_freq_hz_ = params[6];
        update_filters();
        return domain::Result<void>::ok();
    }

private:
    void update_filters() {
        low_l_.set_parameters(domain::dsp::BiquadType::LowShelf, low_freq_hz_, 0.707, low_gain_db_, sample_rate_);
        low_r_.set_parameters(domain::dsp::BiquadType::LowShelf, low_freq_hz_, 0.707, low_gain_db_, sample_rate_);

        mid_l_.set_parameters(domain::dsp::BiquadType::Peak, mid_freq_hz_, mid_q_, mid_gain_db_, sample_rate_);
        mid_r_.set_parameters(domain::dsp::BiquadType::Peak, mid_freq_hz_, mid_q_, mid_gain_db_, sample_rate_);

        high_l_.set_parameters(domain::dsp::BiquadType::HighShelf, high_freq_hz_, 0.707, high_gain_db_, sample_rate_);
        high_r_.set_parameters(domain::dsp::BiquadType::HighShelf, high_freq_hz_, 0.707, high_gain_db_, sample_rate_);
    }

    double sample_rate_{44100.0};
    float low_gain_db_{0.0f};
    double low_freq_hz_{100.0};

    float mid_gain_db_{0.0f};
    double mid_freq_hz_{1000.0};
    double mid_q_{1.0};

    float high_gain_db_{0.0f};
    double high_freq_hz_{8000.0};

    domain::dsp::BiquadFilter low_l_, low_r_;
    domain::dsp::BiquadFilter mid_l_, mid_r_;
    domain::dsp::BiquadFilter high_l_, high_r_;
};

} // namespace digidaw::adapters::plugins
