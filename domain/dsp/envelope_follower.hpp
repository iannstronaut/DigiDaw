#pragma once

#include <cmath>
#include <algorithm>

namespace digidaw::domain::dsp {

// Peak detector + release smoothing for metering and dynamics control (DAW-FR-303, 16 §2)
class EnvelopeFollower {
public:
    EnvelopeFollower(float attack_ms = 1.0f, float release_ms = 50.0f, double sample_rate = 44100.0) {
        set_parameters(attack_ms, release_ms, sample_rate);
    }

    void set_parameters(float attack_ms, float release_ms, double sample_rate) noexcept {
        sample_rate = (sample_rate > 0.0) ? sample_rate : 44100.0;
        attack_ms = std::max(0.1f, attack_ms);
        release_ms = std::max(0.1f, release_ms);

        attack_coeff_ = std::exp(-1000.0f / (attack_ms * static_cast<float>(sample_rate)));
        release_coeff_ = std::exp(-1000.0f / (release_ms * static_cast<float>(sample_rate)));
    }

    void reset() noexcept {
        envelope_ = 0.0f;
    }

    [[nodiscard]] float process(float in) noexcept {
        float abs_in = std::abs(in);
        if (abs_in > envelope_) {
            envelope_ = attack_coeff_ * envelope_ + (1.0f - attack_coeff_) * abs_in;
        } else {
            envelope_ = release_coeff_ * envelope_ + (1.0f - release_coeff_) * abs_in;
        }
        // Denormal guard
        if (envelope_ < 1e-9f) envelope_ = 0.0f;
        return envelope_;
    }

    [[nodiscard]] float current_envelope() const noexcept {
        return envelope_;
    }

    [[nodiscard]] float to_db() const noexcept {
        if (envelope_ <= 0.00001f) return -100.0f;
        return 20.0f * std::log10(envelope_);
    }

private:
    float attack_coeff_{0.9f};
    float release_coeff_{0.999f};
    float envelope_{0.0f};
};

} // namespace digidaw::domain::dsp
