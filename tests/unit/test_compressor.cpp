#include "../test_framework.hpp"
#include "../../adapters/plugins/compressor_device.hpp"

using namespace digidaw::domain;
using namespace digidaw::adapters::plugins;

TEST_CASE(UnitCompressor, AttenuatesAboveThreshold) {
    CompressorDevice comp;
    comp.set_threshold(-12.0f); // ~0.25 linear
    comp.set_ratio(4.0f);
    comp.set_attack_ms(0.1f);   // Fast attack
    comp.set_release_ms(100.0f);
    comp.set_makeup_db(0.0f);
    comp.prepare(44100.0, 512);

    // 1. Below threshold signal: 0.1 (-20dB) -> Should pass without compression
    OwningAudioBuffer buf_quiet(64);
    for (size_t i = 0; i < 64; ++i) {
        buf_quiet.view().left[i] = 0.1f;
        buf_quiet.view().right[i] = 0.1f;
    }
    std::span<const MidiEvent> empty_midi{};
    auto view_quiet = buf_quiet.view();
    comp.process(view_quiet, empty_midi);

    auto [peak_l1, peak_r1] = view_quiet.compute_peak();
    ASSERT_NEAR(peak_l1, 0.1f, 0.005f);

    // 2. Above threshold signal: 1.0 (0dB)
    // 0dB is 12dB above -12dB threshold.
    // With 4:1 ratio, 12dB overage is compressed to 3dB above threshold -> -9dB output (~0.3548 linear)
    OwningAudioBuffer buf_loud(512);
    for (size_t i = 0; i < 512; ++i) {
        buf_loud.view().left[i] = 1.0f;
        buf_loud.view().right[i] = 1.0f;
    }
    auto view_loud = buf_loud.view();
    comp.process(view_loud, empty_midi);

    // Check last samples where attack envelope has stabilized
    float steady_val = view_loud.left[511];
    ASSERT_TRUE(steady_val < 0.5f); // Substantially compressed!
    ASSERT_NEAR(steady_val, 0.355f, 0.05f);
}

TEST_CASE(UnitCompressor, MakeupGainScalesOutput) {
    CompressorDevice comp;
    comp.set_threshold(0.0f);
    comp.set_makeup_db(6.0f); // +6dB is ~2.0x linear gain
    comp.prepare(44100.0, 64);

    OwningAudioBuffer buf(64);
    for (size_t i = 0; i < 64; ++i) {
        buf.view().left[i] = 0.2f;
        buf.view().right[i] = 0.2f;
    }
    std::span<const MidiEvent> empty_midi{};
    auto view = buf.view();
    comp.process(view, empty_midi);

    auto [peak_l, peak_r] = view.compute_peak();
    ASSERT_NEAR(peak_l, 0.4f, 0.02f);
}
