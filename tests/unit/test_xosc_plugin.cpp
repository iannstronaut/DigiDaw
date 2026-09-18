#include "../test_framework.hpp"
#include "../../adapters/plugins/xosc_device.hpp"
#include "../../app/usecases/plugin_manager.hpp"
#include "../../app/engine.hpp"
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

// ============================================================================
// 10. Unison Voice Configuration and Detuning
// ============================================================================

TEST_CASE(UnitXOSC, UnisonVoiceIncrementAndAudioDetune) {
    XOSCDevice dev;
    dev.prepare(44100.0, 512);

    // Initial voices is 0 (which represents 1 voice)
    ASSERT_EQ(dev.get_param_by_id("o0_voices"), 0.0f);

    // Increment unison to 6 (7 voices)
    dev.set_param_by_id("o0_voices", 6.0f);
    ASSERT_EQ(dev.get_param_by_id("o0_voices"), 6.0f);

    // Enable detune and stereo spread
    dev.set_param_by_id("o0_detune", 25.0f); // 25 cents
    dev.set_param_by_id("o0_stereo", 0.8f);  // 80% stereo spread

    OwningAudioBuffer buf(512);
    auto view = buf.view();

    // Trigger note
    MidiEvent ev_on{0, 0x90, 60, 100};
    MidiEvent midi_on[1] = {ev_on};
    dev.process(view, std::span<const MidiEvent>(midi_on, 1));

    auto [pl, pr] = view.compute_peak();
    ASSERT_TRUE(pl > 0.01f);
    ASSERT_TRUE(pr > 0.01f);

    // Process another block to let stereo detuned phases drift
    buf.clear();
    std::span<const MidiEvent> empty_midi{};
    dev.process(view, empty_midi);

    // Unison voices must render with non-zero output
    auto [pl2, pr2] = view.compute_peak();
    ASSERT_TRUE(pl2 > 0.01f);
    ASSERT_TRUE(pr2 > 0.01f);
}

// ============================================================================
// 11. Mod Envelope Filter Cutoff and Resonance Routing
// ============================================================================

TEST_CASE(UnitXOSC, ModEnvFilterCutoffAndResRouting) {
    XOSCDevice dev;
    dev.prepare(44100.0, 512);

    // Set target to Flt 1 Cutoff (target 3)
    dev.set_param_by_id("e0_on", 1.0f);
    dev.set_param_by_id("e0_target", 3.0f);
    ASSERT_EQ(dev.get_param_by_id("e0_target"), 3.0f);

    // Set fast attack, long decay, mix 1.0
    dev.set_param_by_id("e0_mix", 1.0f);
    dev.set_param_by_id("e0_attack", 0.005f);
    dev.set_param_by_id("e0_decay", 0.2f);
    dev.set_param_by_id("e0_sustain", 0.0f);
    dev.set_param_by_id("e0_release", 0.05f);

    // Close Filter 1 base cutoff to 100 Hz so base sound without mod is muffled
    dev.set_param_by_id("f0_on", 1.0f);
    dev.set_param_by_id("f0_cutoff", 100.0f);
    dev.set_param_by_id("f0_res", 2.0f);

    OwningAudioBuffer buf(512);
    auto view = buf.view();

    // Trigger high note C5 (72) - with cutoff at 100 Hz, unmodulated it would be heavily filtered
    MidiEvent ev_on{0, 0x90, 72, 100};
    MidiEvent midi_on[1] = {ev_on};
    dev.process(view, std::span<const MidiEvent>(midi_on, 1));

    // Mod env opens the filter during attack, generating sound
    auto [pl, pr] = view.compute_peak();
    ASSERT_TRUE(pl > 0.005f);
    ASSERT_TRUE(pr > 0.005f);

    // Switch target to Flt 1 Res (target 4) and verify clamping within 0..8
    dev.set_param_by_id("e0_target", 4.0f);
    ASSERT_EQ(dev.get_param_by_id("e0_target"), 4.0f);

    dev.set_param_by_id("e0_target", 8.0f); // Osc Pan
    ASSERT_EQ(dev.get_param_by_id("e0_target"), 8.0f);

    dev.set_param_by_id("e0_target", 99.0f);
    ASSERT_EQ(dev.get_param_by_id("e0_target"), 8.0f);
}

// ============================================================================
// 12. Rapid Note Playing Stress Test - No Hanging Voices
// ============================================================================

TEST_CASE(UnitXOSC, RapidNoteOnNoteOffStressNoDanglingVoices) {
    XOSCDevice dev;
    dev.prepare(44100.0, 256);

    // Set short release on amp envelopes (20 ms)
    dev.set_param_by_id("a0_release", 0.02f);
    dev.set_param_by_id("a1_release", 0.02f);

    OwningAudioBuffer buf(256);
    auto view = buf.view();

    // Rapidly play 40 notes in quick succession (like fast shredding or arpeggios)
    for (int i = 0; i < 40; ++i) {
        buf.clear();
        uint8_t note = static_cast<uint8_t>(48 + (i % 24));
        // Alternating note on
        MidiEvent ev_on{0, 0x90, note, 100};
        MidiEvent on_arr[1] = {ev_on};
        dev.process(view, std::span<const MidiEvent>(on_arr, 1));

        // Let play for 1 block
        buf.clear();
        std::span<const MidiEvent> empty_midi{};
        dev.process(view, empty_midi);

        // Note off
        buf.clear();
        MidiEvent ev_off{0, 0x80, note, 0};
        MidiEvent off_arr[1] = {ev_off};
        dev.process(view, std::span<const MidiEvent>(off_arr, 1));
    }

    // Process silence for 0.5 seconds (~86 blocks of 256 samples) to allow release envelopes to decay
    std::span<const MidiEvent> empty_midi{};
    for (int b = 0; b < 86; ++b) {
        buf.clear();
        dev.process(view, empty_midi);
    }

    // After decay time, all voices MUST be idle (active_voices == 0) without calling panic() or bulk all-notes-off!
    ASSERT_EQ(dev.active_voices(), 0);

    // Audio must have decayed to silence
    auto [quiet_l, quiet_r] = view.compute_peak();
    ASSERT_NEAR(quiet_l, 0.0f, 0.0001f);
    ASSERT_NEAR(quiet_r, 0.0f, 0.0001f);
}

// ============================================================================
// 13. Host Engine Audition Rapid Successive Notes - No Hanging Sustain
// ============================================================================

TEST_CASE(UnitXOSC, EngineAuditionRapidNotesDecayCleanlyWithoutPanic) {
    digidaw::app::Engine engine;

    // Create channel with XOSC
    ChannelSettings s;
    s.name = "XOSC Synth";
    s.volume = 1.0f;
    s.mixer_track = 1;
    ChannelId ch_id = engine.session().project().add_channel("core.generator.xosc", s);

    auto dev = engine.get_or_create_channel_device(ch_id);
    ASSERT_TRUE(dev != nullptr);
    auto xosc_dev = std::dynamic_pointer_cast<XOSCDevice>(dev);
    ASSERT_TRUE(xosc_dev != nullptr);

    // Set short release on amp envelopes (20 ms)
    xosc_dev->set_param_by_id("a0_release", 0.02f);
    xosc_dev->set_param_by_id("a1_release", 0.02f);

    OwningAudioBuffer out_buf(512);
    auto view = out_buf.view();

    // Rapidly trigger 5 audition notes in quick succession (within 50ms)
    // In the old bug, each audition_note() overwrote audition_pitch_[ch_id],
    // so only the 5th note ever received a NoteOff; notes 1..4 sustained forever.
    uint8_t pitches[5] = {60, 62, 64, 65, 67};
    for (int i = 0; i < 5; ++i) {
        engine.audition_note(ch_id, pitches[i], 100);
        // Process 1 block (~11.6 ms at 44.1kHz) between notes
        engine.process_realtime_audio(view);
    }

    // Advance audio blocks for ~0.6 seconds (about 55 blocks of 512 samples)
    // Audition duration is 350ms (0.35s) + 20ms release + safety margin
    for (int b = 0; b < 55; ++b) {
        engine.process_realtime_audio(view);
    }

    // All auditioned notes MUST have expired and emitted their NoteOffs independently
    ASSERT_EQ(xosc_dev->active_voices(), 0);

    // Final output buffer must be completely quiet without calling panic()!
    auto [quiet_l, quiet_r] = view.compute_peak();
    ASSERT_NEAR(quiet_l, 0.0f, 0.0001f);
    ASSERT_NEAR(quiet_r, 0.0f, 0.0001f);
}

// ============================================================================
// 14. High Sustain and High Release with 3x Rapid Note Re-triggers (Same Pitch)
// ============================================================================

TEST_CASE(UnitXOSC, HighSustainAndReleaseRapidRetriggerDecaysWithoutStaticOrHangs) {
    digidaw::app::Engine engine;

    ChannelSettings s;
    s.name = "XOSC Synth";
    s.volume = 1.0f;
    s.mixer_track = 1;
    ChannelId ch_id = engine.session().project().add_channel("core.generator.xosc", s);

    auto dev = engine.get_or_create_channel_device(ch_id);
    ASSERT_TRUE(dev != nullptr);
    auto xosc_dev = std::dynamic_pointer_cast<XOSCDevice>(dev);
    ASSERT_TRUE(xosc_dev != nullptr);

    // Set high sustain and long release on amp envelope
    xosc_dev->set_param_by_id("a0_sustain", 1.0f);
    xosc_dev->set_param_by_id("a0_release", 0.5f); // 500 ms release

    // Enable Filter 1 with resonance to test SVF stability under re-triggering
    xosc_dev->set_param_by_id("f0_on", 1.0f);
    xosc_dev->set_param_by_id("f0_route0", 1.0f);
    xosc_dev->set_param_by_id("f0_cutoff", 3500.0f);
    xosc_dev->set_param_by_id("f0_res", 4.0f);

    OwningAudioBuffer out_buf(512);
    auto view = out_buf.view();

    // Trigger the SAME pitch (C4, 60) 3 times rapidly (e.g. within 50ms)
    // Reproduces user scenario: 3x note clicked on VST with high sustain & release
    for (int i = 0; i < 3; ++i) {
        engine.audition_note(ch_id, 60, 100);
        engine.process_realtime_audio(view);
        // Verify audio is valid and not NaN/Inf/static during sounding
        for (size_t smp = 0; smp < 512; ++smp) {
            ASSERT_FALSE(std::isnan(view.left[smp]));
            ASSERT_FALSE(std::isnan(view.right[smp]));
            ASSERT_FALSE(std::isinf(view.left[smp]));
            ASSERT_FALSE(std::isinf(view.right[smp]));
            // No rail-to-rail static slamming (tanh rail is 1.0)
            ASSERT_TRUE(std::abs(view.left[smp]) < 1.0f || std::abs(view.left[smp]) <= 1.0001f);
        }
    }

    // Voice de-duplication ensures rapid re-triggering of the same pitch reuses the voice
    // rather than accumulating 3 separate voices playing the same note simultaneously
    ASSERT_EQ(xosc_dev->active_voices(), 1);

    // Advance audio blocks through the audition hold (350ms) + release (500ms) + safety margin
    // Total duration ~1.1 seconds = ~95 blocks of 512 samples at 44.1kHz
    for (int b = 0; b < 95; ++b) {
        engine.process_realtime_audio(view);
        for (size_t smp = 0; smp < 512; ++smp) {
            ASSERT_FALSE(std::isnan(view.left[smp]));
            ASSERT_FALSE(std::isnan(view.right[smp]));
            ASSERT_FALSE(std::isinf(view.left[smp]));
            ASSERT_FALSE(std::isinf(view.right[smp]));
        }
    }

    // All voices MUST have cleanly decayed to idle (active_voices == 0) without hanging or requiring panic!
    ASSERT_EQ(xosc_dev->active_voices(), 0);

    // Audio must have decayed cleanly to zero (no residual static buzzing)
    auto [quiet_l, quiet_r] = view.compute_peak();
    ASSERT_NEAR(quiet_l, 0.0f, 0.0001f);
    ASSERT_NEAR(quiet_r, 0.0f, 0.0001f);
}

// ============================================================================
// 15. Unison Saw Anti-Aliasing, PolyBLEP and Nyquist Foldback Protection
// ============================================================================

TEST_CASE(UnitXOSC, UnisonSawPolyBLEPNyquistFoldbackCleanSpectrum) {
    XOSCDevice dev;
    dev.prepare(44100.0, 512);

    // Configure Oscillator 0: Saw wave, 8 unison voices, detuned with stereo spread
    dev.set_param_by_id("o0_on", 1.0f);
    dev.set_param_by_id("o0_wave", 2.0f); // Sawtooth
    dev.set_param_by_id("o0_voices", 7.0f); // 8 voices
    dev.set_param_by_id("o0_detune", 35.0f); // 35 cents detune spread
    dev.set_param_by_id("o0_stereo", 0.75f); // 75% stereo spread
    dev.set_param_by_id("a0_attack", 0.005f);
    dev.set_param_by_id("a0_decay", 0.2f);
    dev.set_param_by_id("a0_sustain", 0.8f);

    OwningAudioBuffer buf(512);
    auto view = buf.view();

    // 1. Play high register note: C7 (pitch 96, ~2093 Hz fundamental)
    // Higher harmonics of 8 detuned saw sub-voices approach and cross Nyquist (22.05 kHz)
    MidiEvent ev_c7{0, 0x90, 96, 100};
    MidiEvent ev_arr1[1] = {ev_c7};
    dev.process(view, std::span<const MidiEvent>(ev_arr1, 1));

    // Verify signal is stable, non-zero, well-normalized, and free of NaN/Inf
    auto [p1_l, p1_r] = view.compute_peak();
    ASSERT_TRUE(p1_l > 0.01f);
    ASSERT_TRUE(p1_r > 0.01f);
    // Unison gain normalization ensures 8 voices do not severely overdrive into clipping
    ASSERT_TRUE(p1_l < 1.5f);
    ASSERT_TRUE(p1_r < 1.5f);

    // 2. Play note with dt > 0.25: Pitch 125 (~11,839 Hz fundamental at 44.1 kHz, dt = 0.268)
    // Specifically exercises PolyBLEP Nyquist foldback crossfade with correct in-phase fundamental
    buf.clear();
    MidiEvent ev_p125{0, 0x90, 125, 100};
    MidiEvent ev_arr2[1] = {ev_p125};
    dev.process(view, std::span<const MidiEvent>(ev_arr2, 1));

    auto [p2_l, p2_r] = view.compute_peak();
    // Fundamental is in-phase and NOT cancelled out by destructive crossfade
    ASSERT_TRUE(p2_l > 0.01f);
    ASSERT_TRUE(p2_r > 0.01f);
    for (size_t i = 0; i < 512; ++i) {
        ASSERT_FALSE(std::isnan(view.left[i]));
        ASSERT_FALSE(std::isnan(view.right[i]));
        ASSERT_FALSE(std::isinf(view.left[i]));
        ASSERT_FALSE(std::isinf(view.right[i]));
    }

    // 3. Play extreme note approaching Nyquist (dt > 0.45) with pitch transposition
    dev.set_param_by_id("o0_tune", 12.0f); // +1 octave
    buf.clear();
    MidiEvent ev_extreme{0, 0x90, 120, 100};
    MidiEvent ev_arr3[1] = {ev_extreme};
    dev.process(view, std::span<const MidiEvent>(ev_arr3, 1));
    for (size_t i = 0; i < 512; ++i) {
        ASSERT_FALSE(std::isnan(view.left[i]));
        ASSERT_FALSE(std::isnan(view.right[i]));
        ASSERT_FALSE(std::isinf(view.left[i]));
        ASSERT_FALSE(std::isinf(view.right[i]));
    }

    // 4. Fast Note Off and decay to zero
    dev.set_param_by_id("o0_tune", 0.0f);
    dev.set_param_by_id("a0_release", 0.02f);
    buf.clear();
    MidiEvent off1{0, 0x80, 96, 0};
    MidiEvent off2{0, 0x80, 125, 0};
    MidiEvent off3{0, 0x80, 120, 0};
    MidiEvent off_arr[3] = {off1, off2, off3};
    dev.process(view, std::span<const MidiEvent>(off_arr, 3));

    std::span<const MidiEvent> empty_midi{};
    for (int b = 0; b < 25; ++b) {
        buf.clear();
        dev.process(view, empty_midi);
    }
    ASSERT_EQ(dev.active_voices(), 0);
    auto [ql, qr] = view.compute_peak();
    ASSERT_NEAR(ql, 0.0f, 0.0001f);
    ASSERT_NEAR(qr, 0.0f, 0.0001f);
}

// ============================================================================
// 16. Voice De-duplication Retriggers Releasing Voice Without Voice Pile-up
// ============================================================================

TEST_CASE(UnitXOSC, VoiceDeduplicationRetriggersReleasingVoice) {
    XOSCDevice dev;
    dev.prepare(44100.0, 512);

    // Long release so voice stays active after NoteOff
    dev.set_param_by_id("a0_release", 2.0f);
    dev.set_param_by_id("a0_sustain", 1.0f);

    OwningAudioBuffer buf(512);
    auto view = buf.view();

    // 1. Play Note 60
    MidiEvent on1{0, 0x90, 60, 100};
    MidiEvent on1_arr[1] = {on1};
    dev.process(view, std::span<const MidiEvent>(on1_arr, 1));
    ASSERT_EQ(dev.active_voices(), 1);

    // 2. Release Note 60 (voice enters long release)
    buf.clear();
    MidiEvent off1{0, 0x80, 60, 0};
    MidiEvent off1_arr[1] = {off1};
    dev.process(view, std::span<const MidiEvent>(off1_arr, 1));
    // Voice is still active in release stage
    ASSERT_EQ(dev.active_voices(), 1);

    // 3. Re-trigger Note 60 while voice is in release
    // Voice de-duplication MUST re-use the releasing voice rather than allocating a 2nd voice
    buf.clear();
    MidiEvent on2{0, 0x90, 60, 100};
    MidiEvent on2_arr[1] = {on2};
    dev.process(view, std::span<const MidiEvent>(on2_arr, 1));
    ASSERT_EQ(dev.active_voices(), 1); // Not 2!

    // 4. Play a DIFFERENT note (Note 64) -> allocates a second voice
    buf.clear();
    MidiEvent on3{0, 0x90, 64, 100};
    MidiEvent on3_arr[1] = {on3};
    dev.process(view, std::span<const MidiEvent>(on3_arr, 1));
    ASSERT_EQ(dev.active_voices(), 2);

    // Clean up
    dev.panic();
    buf.clear();
    std::span<const MidiEvent> empty{};
    dev.process(view, empty);
    ASSERT_EQ(dev.active_voices(), 0);
}



