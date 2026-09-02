#include "../test_framework.hpp"
#include "../../domain/dsp/one_pole.hpp"
#include "../../domain/dsp/biquad.hpp"
#include "../../domain/dsp/limiter.hpp"
#include "../../domain/dsp/saturator.hpp"
#include "../../domain/parameter/formula_evaluator.hpp"

using namespace digidaw::domain;
using namespace digidaw::domain::dsp;

TEST_CASE(DomainDSP, OnePoleSmootherRamp) {
    OnePoleSmoother smoother(0.0f, 0.01f);
    smoother.set_sample_rate(44100.0);
    smoother.set_target(1.0f);

    float val = smoother.current();
    ASSERT_EQ(val, 0.0f);

    // After 100 samples, value should be steadily increasing
    for (int i = 0; i < 100; ++i) {
        val = smoother.process();
    }
    ASSERT_TRUE(val > 0.05f && val < 1.0f);

    // After 2000 samples (~45ms), target should be reached
    for (int i = 0; i < 2000; ++i) {
        val = smoother.process();
    }
    ASSERT_NEAR(val, 1.0f, 0.01f);
}

TEST_CASE(DomainDSP, BiquadLowpassAttenuatesHighs) {
    BiquadFilter lp;
    lp.set_parameters(BiquadType::Lowpass, 500.0, 0.707, 0.0, 44100.0);

    // Test with low freq sine (100 Hz): should pass with minimal attenuation
    double omega_low = 2.0 * 3.14159265 * 100.0 / 44100.0;
    float peak_low = 0.0f;
    for (int i = 0; i < 500; ++i) {
        float in = std::sin(i * omega_low);
        float out = lp.process(in);
        if (i > 200) peak_low = std::max(peak_low, std::abs(out));
    }
    ASSERT_NEAR(peak_low, 1.0f, 0.1f);

    // Test with high freq sine (10000 Hz): should be heavily attenuated
    lp.reset();
    double omega_high = 2.0 * 3.14159265 * 10000.0 / 44100.0;
    float peak_high = 0.0f;
    for (int i = 0; i < 500; ++i) {
        float in = std::sin(i * omega_high);
        float out = lp.process(in);
        if (i > 200) peak_high = std::max(peak_high, std::abs(out));
    }
    ASSERT_TRUE(peak_high < 0.1f);
}

TEST_CASE(DomainDSP, LookaheadLimiterEnforcesCeiling) {
    LookaheadLimiter limiter(-0.2f, 5.0f, 44100.0); // Ceiling = ~0.9772 linear
    limiter.prepare(44100.0);

    const float ceiling_linear = std::pow(10.0f, -0.2f / 20.0f);

    // Feed in massive +12dB signal (amplitude 4.0)
    float max_out_l = 0.0f;
    float max_out_r = 0.0f;

    for (int i = 0; i < 1000; ++i) {
        float in_l = 4.0f * std::sin(i * 0.1f);
        float in_r = 4.0f * std::cos(i * 0.1f);

        limiter.process(in_l, in_r);

        max_out_l = std::max(max_out_l, std::abs(in_l));
        max_out_r = std::max(max_out_r, std::abs(in_r));
    }

    // Output must NEVER exceed ceiling
    ASSERT_TRUE(max_out_l <= ceiling_linear + 0.001f);
    ASSERT_TRUE(max_out_r <= ceiling_linear + 0.001f);
}

TEST_CASE(DomainDSP, FormulaEvaluatorMath) {
    ASSERT_NEAR(FormulaEvaluator::evaluate("x", 0.5), 0.5, 0.0001);
    ASSERT_NEAR(FormulaEvaluator::evaluate("x^2", 0.5), 0.25, 0.0001);
    ASSERT_NEAR(FormulaEvaluator::evaluate("1 - x", 0.3), 0.7, 0.0001);
    ASSERT_NEAR(FormulaEvaluator::evaluate("sin(x * pi * 0.5)", 1.0), 1.0, 0.001);

    // Robustness: division by zero or invalid expression must not crash
    ASSERT_NEAR(FormulaEvaluator::evaluate("x / 0", 0.5), 0.0, 0.0001);
    ASSERT_NEAR(FormulaEvaluator::evaluate("invalid!!expr", 0.7), 0.7, 0.0001);
}
