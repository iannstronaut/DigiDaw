#pragma once

#include <cmath>

namespace digidaw::domain::dsp {

// 1-pole parameter smoother to prevent clicks and pops (DAW-FR-107, 16 §1)
class OnePoleSmoother {
public:
    explicit OnePoleSmoother(float initial_value = 0.0f, float time_constant_seconds = 0.01f)
        : current_(initial_value), target_(initial_value), tau_(time_constant_seconds) {
        set_sample_rate(44100.0);
    }

    void set_sample_rate(double sample_rate) noexcept {
        sample_rate_ = (sample_rate > 0.0) ? sample_rate : 44100.0;
        recompute_coeff();
    }

    void set_time_constant(float tau_seconds) noexcept {
        tau_ = (tau_seconds > 0.0001f) ? tau_seconds : 0.0001f;
        recompute_coeff();
    }

    void set_target(float target) noexcept {
        target_ = target;
    }

    void reset(float value) noexcept {
        current_ = value;
        target_ = value;
    }

    [[nodiscard]] float process() noexcept {
        current_ += coeff_ * (target_ - current_);
        // Denormal guard
        if (std::abs(current_ - target_) < 1e-7f) {
            current_ = target_;
        }
        return current_;
    }

    [[nodiscard]] float current() const noexcept { return current_; }
    [[nodiscard]] float target() const noexcept { return target_; }
    [[nodiscard]] bool is_target_reached() const noexcept { return current_ == target_; }

private:
    void recompute_coeff() noexcept {
        coeff_ = 1.0f - std::exp(-1.0f / static_cast<float>(tau_ * sample_rate_));
    }

    float current_{0.0f};
    float target_{0.0f};
    float tau_{0.01f};
    float coeff_{0.1f};
    double sample_rate_{44100.0};
};

// DC Blocker filter (y[n] = x[n] - x[n-1] + R * y[n-1])
class DCBlocker {
public:
    explicit DCBlocker(float r = 0.995f) : r_(r) {}

    void reset() noexcept {
        x_prev_ = 0.0f;
        y_prev_ = 0.0f;
    }

    [[nodiscard]] float process(float x) noexcept {
        float y = x - x_prev_ + r_ * y_prev_;
        x_prev_ = x;
        y_prev_ = y;
        return y;
    }

private:
    float r_{0.995f};
    float x_prev_{0.0f};
    float y_prev_{0.0f};
};

} // namespace digidaw::domain::dsp
