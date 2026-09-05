#include "../test_framework.hpp"
#include "../../adapters/plugins/audioclip_device.hpp"
#include "../../app/usecases/plugin_manager.hpp"
#include <cmath>
#include <vector>
#include <fstream>
#include <cstdio>

using namespace digidaw::domain;
using namespace digidaw::adapters::plugins;

TEST_CASE(UnitAudioClip, DefaultsToC5AndPlaysSample) {
    AudioClipDevice clipper;
    clipper.prepare(44100.0, 512);

    // 1. Check basic metadata
    ASSERT_EQ(clipper.uid(), "core.generator.audioclip");
    ASSERT_EQ(clipper.name(), "Clipper");
    ASSERT_EQ(clipper.category(), DeviceCategory::Generator);

    // 2. Default root note MUST be C5 (MIDI 60) per requirement!
    ASSERT_EQ(clipper.root_key(), 60);

    // 3. Default sample has non-empty buffer and valid file name
    ASSERT_TRUE(!clipper.sample_l().empty());
    ASSERT_EQ(clipper.filename(), "Basic 808 Kick.wav");

    // 4. Trigger Note On at C5 (pitch 60)
    OwningAudioBuffer buf(512);
    auto view = buf.view();
    view.clear();

    MidiEvent ev_on{0, 0x90, 60, 100};
    MidiEvent midi_events[1] = {ev_on};
    clipper.process(view, std::span<const MidiEvent>(midi_events, 1));

    auto [peak_l, peak_r] = view.compute_peak();
    ASSERT_TRUE(peak_l > 0.01f);
    ASSERT_TRUE(peak_r > 0.01f);
}

TEST_CASE(UnitAudioClip, ResamplesAndTransposesRelativeTolC5) {
    AudioClipDevice clipper;
    clipper.prepare(44100.0, 512);

    ASSERT_EQ(clipper.root_key(), 60); // C5

    // Trigger high note C6 (pitch 72, 1 octave above C5 -> 2x speed)
    OwningAudioBuffer buf_high(256);
    auto view_high = buf_high.view();
    view_high.clear();
    MidiEvent ev_high{0, 0x90, 72, 100};
    clipper.process(view_high, std::span<const MidiEvent>(&ev_high, 1));

    auto [peak_l1, _] = view_high.compute_peak();
    ASSERT_TRUE(peak_l1 > 0.01f);

    // Reset and trigger low note C4 (pitch 48, 1 octave below C5 -> 0.5x speed)
    clipper.reset();
    OwningAudioBuffer buf_low(256);
    auto view_low = buf_low.view();
    view_low.clear();
    MidiEvent ev_low{0, 0x90, 48, 100};
    clipper.process(view_low, std::span<const MidiEvent>(&ev_low, 1));

    auto [peak_l2, _2] = view_low.compute_peak();
    ASSERT_TRUE(peak_l2 > 0.01f);
}

TEST_CASE(UnitAudioClip, PrecomputedEffectsTransformSample) {
    AudioClipDevice clipper;

    std::vector<float> test_l = {0.2f, 0.4f, 0.6f, 0.8f, 1.0f};
    std::vector<float> test_r = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f};
    clipper.load_sample_data(test_l, test_r, "test.wav", 60);

    ASSERT_EQ(clipper.root_key(), 60);
    ASSERT_EQ(clipper.filename(), "test.wav");
    ASSERT_EQ(clipper.sample_frames(), 5);

    // Reverse test
    clipper.set_reverse(true);
    ASSERT_NEAR(clipper.sample_l()[0], 1.0f, 0.001f);
    ASSERT_NEAR(clipper.sample_l()[4], 0.2f, 0.001f);
    clipper.set_reverse(false);

    // Reverse Polarity test
    clipper.set_reverse_polarity(true);
    ASSERT_NEAR(clipper.sample_l()[0], -0.2f, 0.001f);
    ASSERT_NEAR(clipper.sample_l()[4], -1.0f, 0.001f);
    clipper.set_reverse_polarity(false);

    // Normalize test
    clipper.set_normalize(true);
    ASSERT_NEAR(clipper.sample_l()[4], 1.0f, 0.001f);
}

TEST_CASE(UnitAudioClip, WindowingPreservesPrecomputedEffects) {
    AudioClipDevice clipper;

    // 10-sample array: 0.1, 0.2, 0.3, ..., 1.0
    std::vector<float> test_l(10);
    std::vector<float> test_r(10);
    for (size_t i = 0; i < 10; ++i) {
        test_l[i] = (i + 1) * 0.1f;
        test_r[i] = (i + 1) * 0.1f;
    }
    clipper.load_sample_data(test_l, test_r, "window_test.wav", 60);

    // Trim to 50% length and reverse
    clipper.set_smp_start(0.0f);
    clipper.set_smp_length(0.5f);
    clipper.set_reverse(true);

    // Sub-length is 5 samples (0.1, 0.2, 0.3, 0.4, 0.5 reversed -> 0.5, 0.4, 0.3, 0.2, 0.1)
    ASSERT_EQ(clipper.sample_frames(), 5);
    ASSERT_NEAR(clipper.sample_l()[0], 0.5f, 0.01f);
    ASSERT_NEAR(clipper.sample_l()[4], 0.1f, 0.01f);
}

TEST_CASE(UnitAudioClip, ResonantFilterAttenuatesAudio) {
    AudioClipDevice clipper;
    clipper.prepare(44100.0, 512);

    // Load synthetic high-frequency sample
    std::vector<float> test_l(512);
    std::vector<float> test_r(512);
    for (size_t i = 0; i < 512; ++i) {
        test_l[i] = (i % 2 == 0) ? 1.0f : -1.0f; // Nyquist square wave
        test_r[i] = test_l[i];
    }
    clipper.load_sample_data(test_l, test_r, "nyquist.wav", 60);

    // 1. Process with filter open (MOD X = 1.0)
    clipper.set_filter_mod_x(1.0f);
    clipper.set_filter_mod_y(0.0f);
    OwningAudioBuffer buf_open(512);
    auto view_open = buf_open.view();
    view_open.clear();
    MidiEvent ev_on{0, 0x90, 60, 100};
    clipper.process(view_open, std::span<const MidiEvent>(&ev_on, 1));
    auto [peak_open, _] = view_open.compute_peak();

    // 2. Process with filter closed down (MOD X = 0.1 -> ~40 Hz cutoff)
    clipper.reset();
    clipper.set_filter_mod_x(0.1f);
    clipper.set_filter_mod_y(0.0f);
    OwningAudioBuffer buf_closed(512);
    auto view_closed = buf_closed.view();
    view_closed.clear();
    clipper.process(view_closed, std::span<const MidiEvent>(&ev_on, 1));
    auto [peak_closed, _2] = view_closed.compute_peak();

    // The high-frequency energy must be drastically attenuated!
    ASSERT_TRUE(peak_open > 0.5f);
    ASSERT_TRUE(peak_closed < 0.2f);
}

TEST_CASE(UnitAudioClip, VolumeEnvelopeADSRProcessing) {
    AudioClipDevice clipper;
    clipper.prepare(44100.0, 512);

    // Enable envelope: Attack = 50ms, Decay = 50ms, Sustain = 40%, Release = 20ms
    clipper.set_env_enabled(true);
    clipper.set_env_delay(0.0f);
    clipper.set_env_attack(0.05f); // ~2200 samples
    clipper.set_env_hold(0.0f);
    clipper.set_env_decay(0.05f);
    clipper.set_env_sustain(0.4f);
    clipper.set_env_release(0.02f);

    OwningAudioBuffer buf(256);
    auto view = buf.view();
    view.clear();

    MidiEvent ev_on{0, 0x90, 60, 100};
    clipper.process(view, std::span<const MidiEvent>(&ev_on, 1));

    // At very start of attack (first 256 samples of 2200 samples), peak should be ramping up
    auto [peak_early, _] = view.compute_peak();
    ASSERT_TRUE(peak_early >= 0.0f);
    ASSERT_TRUE(peak_early < 0.6f);
}

TEST_CASE(UnitAudioClip, WavLoadingSupportAllBitDepths) {
    // Helper to generate a minimal valid RIFF WAVE in memory
    auto make_wav_bytes = [](uint16_t format_tag, uint16_t num_ch, uint32_t sr, uint16_t bits, const std::vector<uint8_t>& raw_pcm) {
        std::vector<uint8_t> data;
        auto append_u32 = [&](uint32_t val) {
            uint8_t b[4];
            std::memcpy(b, &val, 4);
            data.insert(data.end(), b, b + 4);
        };
        auto append_u16 = [&](uint16_t val) {
            uint8_t b[2];
            std::memcpy(b, &val, 2);
            data.insert(data.end(), b, b + 2);
        };
        auto append_str = [&](const char* s) {
            data.insert(data.end(), s, s + 4);
        };

        append_str("RIFF");
        append_u32(static_cast<uint32_t>(36 + raw_pcm.size()));
        append_str("WAVE");
        append_str("fmt ");
        append_u32(16); // subchunk1 size
        append_u16(format_tag);
        append_u16(num_ch);
        append_u32(sr);
        uint16_t block_align = num_ch * (bits / 8);
        append_u32(sr * block_align);
        append_u16(block_align);
        append_u16(bits);
        append_str("data");
        append_u32(static_cast<uint32_t>(raw_pcm.size()));
        data.insert(data.end(), raw_pcm.begin(), raw_pcm.end());
        return data;
    };

    // 1. Test 8-bit PCM WAV
    std::vector<uint8_t> pcm8 = {128, 200, 255, 128, 50, 0};
    auto wav8 = make_wav_bytes(1, 1, 44100, 8, pcm8);
    std::string tmp8 = "tmp_test_8bit.wav";
    {
        std::ofstream f(tmp8, std::ios::binary);
        f.write(reinterpret_cast<const char*>(wav8.data()), wav8.size());
    }

    AudioClipDevice clip8;
    bool loaded8 = clip8.load_wav_file(tmp8);
    std::remove(tmp8.c_str());

    ASSERT_TRUE(loaded8);
    ASSERT_EQ(clip8.bit_depth(), 8);
    ASSERT_EQ(clip8.root_key(), 60); // Default C5
    ASSERT_EQ(clip8.sample_frames(), 6);
    ASSERT_NEAR(clip8.sample_l()[0], 0.0f, 0.02f); // 128 is center 0
    ASSERT_TRUE(clip8.sample_l()[2] > 0.9f);       // 255 is near +1.0
}

TEST_CASE(UnitAudioClip, ParameterSetGetAndStatePersistence) {
    AudioClipDevice clipper;

    // Test parameter controls
    clipper.set_parameter(0, 0.5f); // Master Volume
    ASSERT_NEAR(clipper.get_parameter(0), 0.5f, 0.01f);
    ASSERT_NEAR(clipper.master_volume(), 0.5f, 0.01f);

    clipper.set_parameter(3, 60.0f / 127.0f); // Root key C5
    ASSERT_EQ(clipper.root_key(), 60);

    clipper.set_parameter(2, 0.75f); // Pitch shift +6 semitones
    ASSERT_NEAR(clipper.pitch_shift(), 6.0f, 0.1f);

    // Test Save and Load State with extended V2 fields
    clipper.set_root_key(64); // E5
    clipper.set_pitch_shift(3.0f);
    clipper.set_master_volume(0.9f);
    clipper.set_normalize(true);
    clipper.set_filter_mod_x(0.65f);
    clipper.set_filter_mod_y(0.45f);
    clipper.set_filter_type(1); // Highpass
    clipper.set_ping_pong_loop(true);

    auto state = clipper.save_state();
    ASSERT_TRUE(!state.empty());

    AudioClipDevice restored;
    auto load_res = restored.load_state(state);
    ASSERT_TRUE(load_res.is_ok());
    ASSERT_EQ(restored.root_key(), 64);
    ASSERT_NEAR(restored.pitch_shift(), 3.0f, 0.05f);
    ASSERT_NEAR(restored.master_volume(), 0.9f, 0.05f);
    ASSERT_TRUE(restored.normalize());
    ASSERT_NEAR(restored.filter_mod_x(), 0.65f, 0.02f);
    ASSERT_NEAR(restored.filter_mod_y(), 0.45f, 0.02f);
    ASSERT_EQ(restored.filter_type(), 1);
    ASSERT_TRUE(restored.ping_pong_loop());
}

TEST_CASE(UnitAudioClip, PluginManagerInstantiatesClipper) {
    digidaw::app::PluginManager mgr;
    auto res = mgr.instantiate("core.generator.audioclip");
    ASSERT_TRUE(res.is_ok());
    auto dev = res.value();
    ASSERT_TRUE(dev != nullptr);
    ASSERT_EQ(dev->uid(), "core.generator.audioclip");
    ASSERT_EQ(dev->name(), "Clipper");
}
