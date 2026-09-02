#pragma once

#include <cmath>
#include <algorithm>

namespace digidaw::domain::dsp {

// Andy Simper (Cytomic) State Variable Filter (SVF) with zero-delay feedback
class StateVariableFilter {
public:
    StateVariableFilter() = default;

    void set_parameters(double cutoff_hz, double resonance, double sample_rate) noexcept {
        sample_rate = (sample_rate > 1000.0) ? sample_rate : 44100.0;
        cutoff_hz = std::clamp(cutoff_hz, 10.0, sample_rate * 0.49);
        resonance = std::clamp(resonance, 0.0, 1.0);

        constexpr double pi = 3.14159265358979323846;
        g_ = static_cast<float>(std::tan(pi * cutoff_hz / sample_rate));
        // k is damping: 2.0 = no resonance, 0.0 = self-oscillation
        k_ = static_cast<float>(2.0 * (1.0 - resonance * 0.99));
        a1_ = 1.0f / (1.0f + g_ * (g_ + k_));
        a2_ = g_ * a1_;
        a3_ = g_ * a2_;
    }

    void reset() noexcept {
        ic1eq_ = 0.0f;
        ic2eq_ = 0.0f;
    }

    struct FilterOutputs {
        float lowpass;
        float bandpass;
        float highpass;
        float notch;
    };

    [[nodiscard]] FilterOutputs process(float v0) noexcept {
        float v3 = v0 - ic2eq_;
        float v1 = a1_ * ic1eq_ + a2_ * v3;
        float v2 = ic2eq_ + a2_ * ic1eq_ + a3_ * v3;

        ic1eq_ = 2.0f * v1 - ic1eq_;
        ic2eq_ = 2.0f * v2 - ic2eq_;

        // Denormal guard
        if (std::abs(ic1eq_) < 1e-15f) ic1eq_ = 0.0f;
        if (std::abs(ic2eq_) < 1e-15f) ic2eq_ = 0.0f;

        float lp = v2;
        float bp = v1;
        float hp = v0 - k_ * v1 - v2;
        float notch = hp + lp;

        return {lp, bp, hp, notch};
    }

private:
    float g_{0.1f};
    float k_{2.0f};
    float a1_{0.0f}, a2_{0.0f}, a3_{0.0f};
    float ic1eq_{0.0f}, ic2eq_{0.0f};
};

} // namespace digidaw::domain::dsp
