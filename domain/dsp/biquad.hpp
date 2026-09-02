#pragma once

#include <cmath>
#include <algorithm>

namespace digidaw::domain::dsp {

enum class BiquadType : uint8_t {
    Lowpass,
    Highpass,
    Bandpass,
    Notch,
    Peak,
    LowShelf,
    HighShelf
};

class BiquadFilter {
public:
    BiquadFilter() = default;

    void set_parameters(BiquadType type, double cutoff_hz, double q, double gain_db, double sample_rate) noexcept {
        type_ = type;
        sample_rate = (sample_rate > 1000.0) ? sample_rate : 44100.0;
        cutoff_hz = std::clamp(cutoff_hz, 10.0, sample_rate * 0.49);
        q = std::clamp(q, 0.1, 20.0);

        constexpr double pi = 3.14159265358979323846;
        const double omega = 2.0 * pi * cutoff_hz / sample_rate;
        const double sin_omega = std::sin(omega);
        const double cos_omega = std::cos(omega);
        const double alpha = sin_omega / (2.0 * q);
        const double a_gain = std::pow(10.0, gain_db / 40.0); // for shelving / peaking

        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;

        switch (type) {
            case BiquadType::Lowpass:
                b0 = (1.0 - cos_omega) * 0.5;
                b1 = 1.0 - cos_omega;
                b2 = (1.0 - cos_omega) * 0.5;
                a0 = 1.0 + alpha;
                a1 = -2.0 * cos_omega;
                a2 = 1.0 - alpha;
                break;

            case BiquadType::Highpass:
                b0 = (1.0 + cos_omega) * 0.5;
                b1 = -(1.0 + cos_omega);
                b2 = (1.0 + cos_omega) * 0.5;
                a0 = 1.0 + alpha;
                a1 = -2.0 * cos_omega;
                a2 = 1.0 - alpha;
                break;

            case BiquadType::Bandpass:
                b0 = alpha;
                b1 = 0.0;
                b2 = -alpha;
                a0 = 1.0 + alpha;
                a1 = -2.0 * cos_omega;
                a2 = 1.0 - alpha;
                break;

            case BiquadType::Notch:
                b0 = 1.0;
                b1 = -2.0 * cos_omega;
                b2 = 1.0;
                a0 = 1.0 + alpha;
                a1 = -2.0 * cos_omega;
                a2 = 1.0 - alpha;
                break;

            case BiquadType::Peak: {
                b0 = 1.0 + alpha * a_gain;
                b1 = -2.0 * cos_omega;
                b2 = 1.0 - alpha * a_gain;
                a0 = 1.0 + alpha / a_gain;
                a1 = -2.0 * cos_omega;
                a2 = 1.0 - alpha / a_gain;
                break;
            }

            case BiquadType::LowShelf: {
                const double sqrt_a = std::sqrt(a_gain);
                b0 = a_gain * ((a_gain + 1.0) - (a_gain - 1.0) * cos_omega + 2.0 * sqrt_a * alpha);
                b1 = 2.0 * a_gain * ((a_gain - 1.0) - (a_gain + 1.0) * cos_omega);
                b2 = a_gain * ((a_gain + 1.0) - (a_gain - 1.0) * cos_omega - 2.0 * sqrt_a * alpha);
                a0 = (a_gain + 1.0) + (a_gain - 1.0) * cos_omega + 2.0 * sqrt_a * alpha;
                a1 = -2.0 * ((a_gain - 1.0) + (a_gain + 1.0) * cos_omega);
                a2 = (a_gain + 1.0) + (a_gain - 1.0) * cos_omega - 2.0 * sqrt_a * alpha;
                break;
            }

            case BiquadType::HighShelf: {
                const double sqrt_a = std::sqrt(a_gain);
                b0 = a_gain * ((a_gain + 1.0) + (a_gain - 1.0) * cos_omega + 2.0 * sqrt_a * alpha);
                b1 = -2.0 * a_gain * ((a_gain - 1.0) + (a_gain + 1.0) * cos_omega);
                b2 = a_gain * ((a_gain + 1.0) + (a_gain - 1.0) * cos_omega - 2.0 * sqrt_a * alpha);
                a0 = (a_gain + 1.0) - (a_gain - 1.0) * cos_omega + 2.0 * sqrt_a * alpha;
                a1 = 2.0 * ((a_gain - 1.0) - (a_gain + 1.0) * cos_omega);
                a2 = (a_gain + 1.0) - (a_gain - 1.0) * cos_omega - 2.0 * sqrt_a * alpha;
                break;
            }
        }

        // Normalize coefficients
        const double inv_a0 = 1.0 / a0;
        b0_ = static_cast<float>(b0 * inv_a0);
        b1_ = static_cast<float>(b1 * inv_a0);
        b2_ = static_cast<float>(b2 * inv_a0);
        a1_ = static_cast<float>(a1 * inv_a0);
        a2_ = static_cast<float>(a2 * inv_a0);
    }

    void reset() noexcept {
        z1_ = 0.0f;
        z2_ = 0.0f;
    }

    // Direct Form II Transposed (numerically well-conditioned)
    [[nodiscard]] float process(float in) noexcept {
        float out = b0_ * in + z1_;
        z1_ = b1_ * in - a1_ * out + z2_;
        z2_ = b2_ * in - a2_ * out;

        // Denormal guard
        if (std::abs(z1_) < 1e-15f) z1_ = 0.0f;
        if (std::abs(z2_) < 1e-15f) z2_ = 0.0f;

        return out;
    }

private:
    BiquadType type_{BiquadType::Lowpass};
    float b0_{1.0f}, b1_{0.0f}, b2_{0.0f};
    float a1_{0.0f}, a2_{0.0f};
    float z1_{0.0f}, z2_{0.0f};
};

} // namespace digidaw::domain::dsp
