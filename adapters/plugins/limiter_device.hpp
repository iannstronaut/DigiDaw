#pragma once

#include "../../domain/devices/device.hpp"
#include "../../domain/dsp/limiter.hpp"
#include <cmath>
#include <cstring>

namespace digidaw::adapters::plugins {

class LimiterDevice : public domain::IDevice {
public:
    LimiterDevice() : limiter_(-0.2f, 5.0f, 44100.0) {}

    [[nodiscard]] domain::DeviceUid uid() const override {
        return "core.fx.limiter";
    }

    [[nodiscard]] std::string name() const override {
        return "Master Limiter";
    }

    [[nodiscard]] domain::DeviceCategory category() const override {
        return domain::DeviceCategory::Effect;
    }

    void prepare(double sample_rate, size_t /*max_block_size*/) override {
        sample_rate_ = (sample_rate > 0.0) ? sample_rate : 44100.0;
        limiter_.prepare(sample_rate_);
        limiter_.set_ceiling_db(ceiling_db_);
    }

    void reset() override {
        limiter_.prepare(sample_rate_);
    }

    void process(domain::AudioBufferView& buffer, std::span<const domain::MidiEvent> /*midi*/) override {
        for (size_t f = 0; f < buffer.frames; ++f) {
            float l = buffer.left ? buffer.left[f] * pre_gain_ : 0.0f;
            float r = buffer.right ? buffer.right[f] * pre_gain_ : 0.0f;

            limiter_.process(l, r);

            if (buffer.left) buffer.left[f] = l;
            if (buffer.right) buffer.right[f] = r;
        }
    }

    void set_parameter(uint32_t param_id, float val) override {
        switch (param_id) {
            case 0: // Pre-gain (0 to +12 dB)
                pre_gain_db_ = val * 12.0f;
                pre_gain_ = std::pow(10.0f, pre_gain_db_ / 20.0f);
                break;
            case 1: // Ceiling (-12 dB to 0 dB)
                ceiling_db_ = -12.0f + val * 12.0f;
                limiter_.set_ceiling_db(ceiling_db_);
                break;
            default: break;
        }
    }

    [[nodiscard]] float get_parameter(uint32_t param_id) const override {
        switch (param_id) {
            case 0: return pre_gain_db_ / 12.0f;
            case 1: return (ceiling_db_ + 12.0f) / 12.0f;
            default: return 0.0f;
        }
    }

    [[nodiscard]] std::vector<domain::ParamDesc> parameters() const override {
        return {
            {0, "Pre-Gain", 0.0f, 0.0f, 1.0f, "dB"},
            {1, "Ceiling", 0.98f, 0.0f, 1.0f, "dB"}
        };
    }

    [[nodiscard]] std::vector<uint8_t> save_state() const override {
        std::vector<uint8_t> data(sizeof(float) * 2);
        float params[2] = {pre_gain_db_, ceiling_db_};
        std::memcpy(data.data(), params, data.size());
        return data;
    }

    domain::Result<void> load_state(std::span<const uint8_t> data) override {
        if (data.size() < sizeof(float) * 2) {
            return domain::Result<void>(domain::ErrorCode::StateIncompatible);
        }
        float params[2];
        std::memcpy(params, data.data(), sizeof(params));
        pre_gain_db_ = params[0];
        pre_gain_ = std::pow(10.0f, pre_gain_db_ / 20.0f);
        ceiling_db_ = params[1];
        limiter_.set_ceiling_db(ceiling_db_);
        return domain::Result<void>::ok();
    }

private:
    double sample_rate_{44100.0};
    float pre_gain_db_{0.0f};
    float pre_gain_{1.0f};
    float ceiling_db_{-0.2f};
    domain::dsp::LookaheadLimiter limiter_;
};

} // namespace digidaw::adapters::plugins
