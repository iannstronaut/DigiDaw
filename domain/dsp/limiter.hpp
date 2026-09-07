#pragma once

#include "envelope_follower.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

namespace digidaw::domain::dsp {

// Lookahead brickwall limiter (16 §2: 5ms delay line + gain reduction envelope)
class LookaheadLimiter {
public:
    explicit LookaheadLimiter(float ceiling_db = -0.2f, float lookahead_ms = 5.0f, double sample_rate = 44100.0)
        : ceiling_linear_(std::pow(10.0f, ceiling_db / 20.0f)),
          lookahead_ms_(lookahead_ms),
          follower_(0.1f, 100.0f, sample_rate) {
        prepare(sample_rate);
    }

    void prepare(double sample_rate) {
        sample_rate_ = (sample_rate > 0.0) ? sample_rate : 44100.0;
        lookahead_samples_ = static_cast<size_t>(std::ceil(lookahead_ms_ * 0.001 * sample_rate_));
        if (lookahead_samples_ < 1) lookahead_samples_ = 1;

        delay_l_.assign(lookahead_samples_, 0.0f);
        delay_r_.assign(lookahead_samples_, 0.0f);
        write_pos_ = 0;

        follower_.set_parameters(0.2f, 80.0f, sample_rate_);
        follower_.reset();
    }

    void set_ceiling_db(float db) noexcept {
        ceiling_linear_ = std::pow(10.0f, db / 20.0f);
    }

    void process(float& left, float& right) noexcept {
        // Measure current peak
        float current_peak = std::max(std::abs(left), std::abs(right));
        float env = follower_.process(current_peak);

        // Compute gain reduction needed
        float gain = 1.0f;
        if (env > ceiling_linear_) {
            gain = ceiling_linear_ / env;
        }

        // Store current sample in delay line
        delay_l_[write_pos_] = left;
        delay_r_[write_pos_] = right;

        // Advance read/write pos with fast branch-wrapping
        size_t read_pos = write_pos_ + 1;
        if (read_pos >= lookahead_samples_) read_pos = 0;
        write_pos_ = read_pos;

        // Output delayed sample with smoothed gain reduction applied
        left = delay_l_[read_pos] * gain;
        right = delay_r_[read_pos] * gain;

        // Final brickwall safety clamp to ceiling
        left = std::clamp(left, -ceiling_linear_, ceiling_linear_);
        right = std::clamp(right, -ceiling_linear_, ceiling_linear_);
    }

private:
    float ceiling_linear_{0.98f};
    float lookahead_ms_{5.0f};
    double sample_rate_{44100.0};
    size_t lookahead_samples_{220};
    size_t write_pos_{0};
    std::vector<float> delay_l_;
    std::vector<float> delay_r_;
    EnvelopeFollower follower_;
};

} // namespace digidaw::domain::dsp
