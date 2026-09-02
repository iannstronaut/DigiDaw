#pragma once

#include <cmath>
#include <algorithm>

namespace digidaw::domain::dsp {

enum class SaturationMode : uint8_t {
    Tanh,
    SoftClip,
    HardClip
};

class Saturator {
public:
    explicit Saturator(float drive_db = 0.0f, SaturationMode mode = SaturationMode::Tanh)
        : mode_(mode) {
        set_drive_db(drive_db);
    }

    void set_drive_db(float drive_db) noexcept {
        drive_gain_ = std::pow(10.0f, drive_db / 20.0f);
    }

    void set_mode(SaturationMode mode) noexcept {
        mode_ = mode;
    }

    [[nodiscard]] float process(float in) const noexcept {
        float x = in * drive_gain_;
        switch (mode_) {
            case SaturationMode::Tanh:
                return std::tanh(x);

            case SaturationMode::SoftClip: {
                // Cubic soft clipper: f(x) = x - x^3 / 3 for |x| < 1.5, else +/- 1.0
                if (x > 1.5f) return 1.0f;
                if (x < -1.5f) return -1.0f;
                return x - (x * x * x) / (27.0f / 4.0f);
            }

            case SaturationMode::HardClip:
                return std::clamp(x, -1.0f, 1.0f);
        }
        return in;
    }

private:
    SaturationMode mode_{SaturationMode::Tanh};
    float drive_gain_{1.0f};
};

} // namespace digidaw::domain::dsp
