#include "../test_framework.hpp"
#include "../../adapters/plugins/xosc_device.hpp"
#include "../../app/usecases/plugin_manager.hpp"
#include <cmath>
#include <vector>

using namespace digidaw::domain;
using namespace digidaw::adapters::plugins;

// ============================================================================
// 1. Metadata and Parameter Descriptors
// ============================================================================

TEST_CASE(UnitXOSC, MetadataAndDescriptors) {
    XOSCDevice dev;
    ASSERT_EQ(dev.uid(), "core.generator.xosc");
    ASSERT_EQ(dev.name(), "XOSC");
    ASSERT_TRUE(dev.category() == DeviceCategory::Generator);

    auto params = dev.parameters();
    ASSERT_EQ(params.size(), XOSCDevice::kNumParams);
    ASSERT_EQ(params.size(), 145);

    for (size_t i = 0; i < params.size(); ++i) {
        const auto& p = params[i];
        ASSERT_EQ(p.id, static_cast<uint32_t>(i));
        ASSERT_FALSE(p.name.empty());
        ASSERT_TRUE(p.min_val <= p.max_val);
        ASSERT_TRUE(p.default_val >= 0.0f && p.default_val <= 1.0f);
    }
}

// ============================================================================
// 2. Parameter Normalization and Plain Value Conversions
// ============================================================================

TEST_CASE(UnitXOSC, ParameterNormalizationAndPlain) {
    XOSCDevice dev;
    dev.prepare(44100.0, 512);

    // Test specific well-known parameters
    // 1. Master gain (param id 144)
    int mg_idx = xosc::index("master_gain");
    ASSERT_TRUE(mg_idx >= 0);
    dev.set_param_by_id("master_gain", -6.0f);
    ASSERT_NEAR(dev.get_param_by_id("master_gain"), -6.0f, 0.01f);

    // 2. Filter 0 Cutoff (param id 14)
    int f0_cutoff = xosc::index("f0_cutoff");
    ASSERT_TRUE(f0_cutoff >= 0);
    dev.set_parameter(static_cast<uint32_t>(f0_cutoff), 0.5f);
    float norm_read = dev.get_parameter(static_cast<uint32_t>(f0_cutoff));
    ASSERT_NEAR(norm_read, 0.5f, 0.01f);

    // Check bounds safety
    dev.set_parameter(999, 0.5f);
    ASSERT_EQ(dev.get_parameter(999), 0.0f);
    dev.set_param_plain(999, 1.0f);
    ASSERT_EQ(dev.get_param_plain(999), 0.0f);
    ASSERT_EQ(dev.get_param_by_id("invalid_param_name_xyz"), 0.0f);
}

// ============================================================================
// 3. Audio Synthesis - Note On and Note Off
// ============================================================================

TEST_CASE(UnitXOSC, AudioGenerationNoteOnAndOff) {
    XOSCDevice dev;
    dev.prepare(44100.0, 512);

    OwningAudioBuffer buf(512);
    auto view = buf.view();

    // Process silent buffer with no MIDI
    std::span<const MidiEvent> empty_midi{};
    dev.process(view, empty_midi);
    auto [quiet_l, quiet_r] = view.compute_peak();
    ASSERT_NEAR(quiet_l, 0.0f, 0.0001f);
    ASSERT_NEAR(quiet_r, 0.0f, 0.0001f);
    ASSERT_EQ(dev.active_voices(), 0);

    // Send MIDI Note On (C4, pitch 60, vel 100)
    buf.clear();
    MidiEvent ev_on{0, 0x90, 60, 100};
    MidiEvent midi_on[1] = {ev_on};
    dev.process(view, std::span<const MidiEvent>(midi_on, 1));

    auto [peak_l, peak_r] = view.compute_peak();
    ASSERT_TRUE(peak_l > 0.01f);
    ASSERT_TRUE(peak_r > 0.01f);
    ASSERT_TRUE(dev.active_voices() >= 1);

    // Let voice sound for 2 more blocks
    for (int i = 0; i < 2; ++i) {
        buf.clear();
        dev.process(view, empty_midi);
        auto [pl, pr] = view.compute_peak();
        ASSERT_TRUE(pl > 0.01f);
        ASSERT_TRUE(pr > 0.01f);
    }

    // Send Note Off with fast release
    dev.set_param_by_id("a0_release", 0.01f); // 10 ms release
    buf.clear();
    MidiEvent ev_off{0, 0x80, 60, 0};
    MidiEvent midi_off[1] = {ev_off};
    dev.process(view, std::span<const MidiEvent>(midi_off, 1));

    // Release phase: process blocks until release finishes
    for (int i = 0; i < 20; ++i) {
        buf.clear();
        dev.process(view, empty_midi);
    }
    ASSERT_EQ(dev.active_voices(), 0);
}

// ============================================================================
// 4. Polyphony and Voice Stealing Stress Test
// ============================================================================

TEST_CASE(UnitXOSC, PolyphonyAndVoiceStealing) {
    XOSCDevice dev;
    dev.prepare(44100.0, 256);

    OwningAudioBuffer buf(256);
    auto view = buf.view();

    // Trigger 24 notes (exceeding maximum 16 voices)
    std::vector<MidiEvent> chord;
    for (int n = 40; n < 64; ++n) {
        chord.push_back(MidiEvent{0, 0x90, static_cast<uint8_t>(n), 100});
    }

    dev.process(view, chord);

    // Active voices must be capped at polyphony limit (16)
    ASSERT_TRUE(dev.active_voices() <= 16);
    ASSERT_TRUE(dev.active_voices() > 0);

    // Verify audio signal stability (no NaN or Inf)
    for (size_t s = 0; s < 256; ++s) {
        ASSERT_FALSE(std::isnan(view.left[s]));
        ASSERT_FALSE(std::isnan(view.right[s]));
        ASSERT_FALSE(std::isinf(view.left[s]));
        ASSERT_FALSE(std::isinf(view.right[s]));
    }
}

// ============================================================================
// 5. Pitch Bend and Sustain Pedal (CC 64)
// ============================================================================

TEST_CASE(UnitXOSC, PitchBendAndSustainPedal) {
    XOSCDevice dev;
    dev.prepare(44100.0, 256);

    OwningAudioBuffer buf(256);
    auto view = buf.view();

    // 1. Note On C4
    MidiEvent ev_note_on{0, 0x90, 60, 90};
    MidiEvent events1[1] = {ev_note_on};
    dev.process(view, std::span<const MidiEvent>(events1, 1));
    ASSERT_EQ(dev.active_voices(), 1);

    // 2. Sustain pedal DOWN (CC 64 = 127)
    buf.clear();
    MidiEvent ev_pedal_down{0, 0xB0, 64, 127};
    MidiEvent events2[1] = {ev_pedal_down};
    dev.process(view, std::span<const MidiEvent>(events2, 1));

    // 3. Note Off C4 - with sustain pedal down, voice should remain active
    buf.clear();
    MidiEvent ev_note_off{0, 0x80, 60, 0};
    MidiEvent events3[1] = {ev_note_off};
    dev.process(view, std::span<const MidiEvent>(events3, 1));
    ASSERT_EQ(dev.active_voices(), 1);

    // 4. Pitch bend +1 semitone
    buf.clear();
    MidiEvent ev_pb{0, 0xE0, 0x00, 0x60}; // pitch bend
    MidiEvent events4[1] = {ev_pb};
    dev.process(view, std::span<const MidiEvent>(events4, 1));
    ASSERT_EQ(dev.active_voices(), 1);

    // 5. Release sustain pedal (CC 64 = 0)
    dev.set_param_by_id("a0_release", 0.01f);
    buf.clear();
    MidiEvent ev_pedal_up{0, 0xB0, 64, 0};
    MidiEvent events5[1] = {ev_pedal_up};
    dev.process(view, std::span<const MidiEvent>(events5, 1));

    // Drain release
    std::span<const MidiEvent> empty_midi{};
    for (int i = 0; i < 20; ++i) {
        buf.clear();
        dev.process(view, empty_midi);
    }
    ASSERT_EQ(dev.active_voices(), 0);
}

// ============================================================================
// 6. Panic and All Notes Off
// ============================================================================

TEST_CASE(UnitXOSC, PanicAndAllNotesOff) {
    XOSCDevice dev;
    dev.prepare(44100.0, 256);

    OwningAudioBuffer buf(256);
    auto view = buf.view();

    // Trigger 4 notes
    std::vector<MidiEvent> chord = {
        MidiEvent{0, 0x90, 60, 100},
        MidiEvent{0, 0x90, 64, 100},
        MidiEvent{0, 0x90, 67, 100},
        MidiEvent{0, 0x90, 71, 100}
    };
    dev.process(view, chord);
    ASSERT_EQ(dev.active_voices(), 4);

    // Panic call
    dev.panic();

    buf.clear();
    std::span<const MidiEvent> empty_midi{};
    dev.process(view, empty_midi);

    // Active voices should be immediately extinguished
    // Test CC 120 (All Sound Off - immediate)
    dev.process(view, chord);
    ASSERT_EQ(dev.active_voices(), 4);

    buf.clear();
    MidiEvent all_off{0, 0xB0, 120, 0};
    MidiEvent all_off_ev[1] = {all_off};
    dev.process(view, std::span<const MidiEvent>(all_off_ev, 1));
    ASSERT_EQ(dev.active_voices(), 0);
}

// ============================================================================
// 7. Factory Presets
// ============================================================================

TEST_CASE(UnitXOSC, FactoryPresets) {
    XOSCDevice dev;
    dev.prepare(44100.0, 512);

    OwningAudioBuffer buf(512);
    auto view = buf.view();

    // Preset 1: Thick
    dev.load_factory(1);
    ASSERT_NEAR(dev.get_param_by_id("o0_voices"), 6.0f, 0.01f);
    ASSERT_NEAR(dev.get_param_by_id("o0_detune"), 16.0f, 0.01f);
    ASSERT_NEAR(dev.get_param_by_id("o1_on"), 1.0f, 0.01f);

    MidiEvent ev{0, 0x90, 60, 100};
    MidiEvent ev_arr[1] = {ev};
    buf.clear();
    dev.process(view, std::span<const MidiEvent>(ev_arr, 1));
    auto [p1_l, p1_r] = view.compute_peak();
    ASSERT_TRUE(p1_l > 0.01f);
    ASSERT_TRUE(p1_r > 0.01f);

    // Preset 2: Bass
    dev.load_factory(2);
    ASSERT_NEAR(dev.get_param_by_id("o0_tune"), -12.0f, 0.01f);
    ASSERT_NEAR(dev.get_param_by_id("f0_cutoff"), 350.0f, 1.0f);
    ASSERT_NEAR(dev.get_param_by_id("e0_mix"), 0.65f, 0.01f);

    buf.clear();
    dev.process(view, std::span<const MidiEvent>(ev_arr, 1));
    auto [p2_l, p2_r] = view.compute_peak();
    ASSERT_TRUE(p2_l > 0.01f);
    ASSERT_TRUE(p2_r > 0.01f);

    // Preset 3: Pad
    dev.load_factory(3);
    ASSERT_NEAR(dev.get_param_by_id("a0_attack"), 1.2f, 0.01f);
    ASSERT_NEAR(dev.get_param_by_id("reverb_on"), 1.0f, 0.01f);

    buf.clear();
    dev.process(view, std::span<const MidiEvent>(ev_arr, 1));
    // Pad has slow attack so sound will be growing but peak should not be NaN
    for (size_t s = 0; s < 512; ++s) {
        ASSERT_FALSE(std::isnan(view.left[s]));
        ASSERT_FALSE(std::isnan(view.right[s]));
    }
}

// ============================================================================
// 8. State Serialization & Deserialization
// ============================================================================

TEST_CASE(UnitXOSC, StateSerialization) {
    XOSCDevice dev1;
    dev1.prepare(44100.0, 256);

    // Set distinctive values
    dev1.set_param_by_id("master_gain", -9.5f);
    dev1.set_param_by_id("f0_cutoff", 1337.0f);
    dev1.set_param_by_id("o0_wave", 1.0f); // Square
    dev1.set_param_by_id("dist_drive", 15.0f);

    auto state = dev1.save_state();
    ASSERT_EQ(state.size(), 12 + XOSCDevice::kNumParams * sizeof(float));

    // Deserialization into a fresh device
    XOSCDevice dev2;
    dev2.prepare(44100.0, 256);

    auto res = dev2.load_state(state);
    ASSERT_OK(res);

    ASSERT_NEAR(dev2.get_param_by_id("master_gain"), -9.5f, 0.01f);
    ASSERT_NEAR(dev2.get_param_by_id("f0_cutoff"), 1337.0f, 0.5f);
    ASSERT_NEAR(dev2.get_param_by_id("o0_wave"), 1.0f, 0.01f);
    ASSERT_NEAR(dev2.get_param_by_id("dist_drive"), 15.0f, 0.01f);

    // Test corrupted states
    std::vector<uint8_t> bad_magic = state;
    bad_magic[0] = 'B'; bad_magic[1] = 'A'; bad_magic[2] = 'D'; bad_magic[3] = '!';
    auto res_bad_magic = dev2.load_state(bad_magic);
    ASSERT_ERR(res_bad_magic, ErrorCode::StateIncompatible);

    std::vector<uint8_t> truncated(state.begin(), state.begin() + 10);
    auto res_trunc = dev2.load_state(truncated);
    ASSERT_ERR(res_trunc, ErrorCode::StateIncompatible);
}

// ============================================================================
// 9. PluginManager Registration and Instantiation
// ============================================================================

TEST_CASE(UnitXOSC, PluginManagerRegistration) {
    digidaw::app::PluginManager pm;

    // Check device descriptor is registered
    auto plugins = pm.available_plugins();
    bool found = false;
    for (const auto& p : plugins) {
        if (p.uid == "core.generator.xosc") {
            found = true;
            ASSERT_EQ(p.name, "XOSC");
            ASSERT_TRUE(p.category == DeviceCategory::Generator);
            break;
        }
    }
    ASSERT_TRUE(found);

    // Test instantiation
    auto res = pm.instantiate("core.generator.xosc");
    ASSERT_OK(res);
    auto inst = res.value();
    ASSERT_TRUE(inst != nullptr);
    ASSERT_EQ(inst->uid(), "core.generator.xosc");
    ASSERT_EQ(inst->name(), "XOSC");
    ASSERT_TRUE(inst->category() == DeviceCategory::Generator);

    auto* xosc_ptr = dynamic_cast<XOSCDevice*>(inst.get());
    ASSERT_TRUE(xosc_ptr != nullptr);
}

// ============================================================================
// 10. Monophonic Legato and Portamento Glide
// ============================================================================

TEST_CASE(UnitXOSC, MonophonicLegatoAndPortamento) {
    XOSCDevice dev;
    dev.prepare(44100.0, 512);

    // Set Monophonic mode (mono_legato = 1) and 50ms glide
    dev.set_param_by_id("mono_legato", 1.0f);
    dev.set_param_by_id("glide_on", 1.0f);
    dev.set_param_by_id("glide_time", 0.05f);

    OwningAudioBuffer buf(512);
    auto view = buf.view();

    // Note 1: C3 (48)
    MidiEvent ev1{0, 0x90, 48, 100};
    MidiEvent ev_arr1[1] = {ev1};
    dev.process(view, std::span<const MidiEvent>(ev_arr1, 1));
    ASSERT_EQ(dev.active_voices(), 1);

    // Note 2: C4 (60) while C3 is still held
    buf.clear();
    MidiEvent ev2{0, 0x90, 60, 100};
    MidiEvent ev_arr2[1] = {ev2};
    dev.process(view, std::span<const MidiEvent>(ev_arr2, 1));

    // In monophonic mode, only 1 voice should be active, glided to new pitch
    ASSERT_EQ(dev.active_voices(), 1);

    // Output must be valid audio without NaNs
    auto [pl, pr] = view.compute_peak();
    ASSERT_TRUE(pl > 0.01f);
    ASSERT_TRUE(pr > 0.01f);
}

TEST_CASE(UnitXOSC, ParameterClampingAndSanitization) {
    XOSCDevice dev;
    dev.prepare(44100.0, 256);

    // 1. Direct out-of-bounds parameter clamping
    dev.set_param_by_id("o0_wave", 99.0f);
    ASSERT_EQ(dev.get_param_by_id("o0_wave"), 2.0f);

    dev.set_param_by_id("o0_wave", -10.0f);
    ASSERT_EQ(dev.get_param_by_id("o0_wave"), 0.0f);

    dev.set_param_by_id("f0_cutoff", 50000.0f);
    ASSERT_EQ(dev.get_param_by_id("f0_cutoff"), 20000.0f);

    dev.set_param_by_id("f0_cutoff", 5.0f);
    ASSERT_EQ(dev.get_param_by_id("f0_cutoff"), 20.0f);

    // NaN / Inf rejection
    float prev_cutoff = dev.get_param_by_id("f0_cutoff");
    dev.set_param_by_id("f0_cutoff", std::numeric_limits<float>::quiet_NaN());
    ASSERT_EQ(dev.get_param_by_id("f0_cutoff"), prev_cutoff);

    dev.set_param_by_id("f0_cutoff", std::numeric_limits<float>::infinity());
    ASSERT_EQ(dev.get_param_by_id("f0_cutoff"), prev_cutoff);

    // 2. Corrupted float data in state payload deserialization
    auto state = dev.save_state();
    ASSERT_TRUE(state.size() >= 12 + XOSCDevice::kNumParams * sizeof(float));

    // Overwrite some float parameters inside state with NaN and out-of-bounds floats
    float* raw_floats = reinterpret_cast<float*>(state.data() + 12);
    size_t wave_idx = static_cast<size_t>(xosc::index("o0_wave"));
    size_t gain_idx = static_cast<size_t>(xosc::index("master_gain"));

    raw_floats[wave_idx] = 999.0f; // Beyond max 2.0f
    raw_floats[gain_idx] = std::numeric_limits<float>::quiet_NaN(); // Corrupted NaN

    XOSCDevice dev2;
    dev2.prepare(44100.0, 256);
    auto res = dev2.load_state(state);
    ASSERT_OK(res);

    // Wave must be clamped to 2.0f
    ASSERT_EQ(dev2.get_param_by_id("o0_wave"), 2.0f);

    // NaN must be sanitized to initial default or valid range
    float loaded_gain = dev2.get_param_by_id("master_gain");
    ASSERT_FALSE(std::isnan(loaded_gain));
    ASSERT_FALSE(std::isinf(loaded_gain));
    ASSERT_TRUE(loaded_gain >= -60.0f && loaded_gain <= 6.0f);
}

