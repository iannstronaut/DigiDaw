#include "../test_framework.hpp"
#include "../../adapters/plugins/xaudio_devices.hpp"
#include "../../app/usecases/plugin_manager.hpp"
#include <cmath>
#include <vector>

using namespace digidaw::domain;
using namespace digidaw::adapters::plugins;
namespace xaudio = digidaw::xaudio;

// ============================================================================
// 1. X-Eq Unit Tests
// ============================================================================

TEST_CASE(UnitXAudio, XEqMetadataAndDefaultUnity) {
    XEqDevice eq;
    ASSERT_EQ(eq.uid(), "core.fx.x_eq");
    ASSERT_EQ(eq.name(), "X-Eq");
    ASSERT_TRUE(eq.category() == DeviceCategory::Effect);
    ASSERT_EQ(eq.parameters().size(), 28);

    eq.prepare(48000.0, 512);

    // Default EQ should pass signal with bitwise unity / transparency
    OwningAudioBuffer buf(1024);
    for (size_t i = 0; i < 1024; ++i) {
        float sig = 0.25f * std::sin(static_cast<float>(i) * 0.13f);
        buf.view().left[i] = sig;
        buf.view().right[i] = -sig;
    }

    std::span<const MidiEvent> empty_midi{};
    auto view = buf.view();
    eq.process(view, empty_midi);

    for (size_t i = 100; i < 1024; ++i) {
        float expected = 0.25f * std::sin(static_cast<float>(i) * 0.13f);
        ASSERT_NEAR(view.left[i], expected, 0.001f);
        ASSERT_NEAR(view.right[i], -expected, 0.001f);
    }
}

TEST_CASE(UnitXAudio, XEqFilterCutoffAndGainBoost) {
    XEqDevice eq;
    eq.prepare(48000.0, 512);

    // Band 1: Highpass at 1000 Hz (type 4)
    eq.set_band(0, 1000.0, 0.0, 0.707, 4);
    // Band 6: Lowpass at 1000 Hz (type 3)
    eq.set_band(5, 1000.0, 0.0, 0.707, 3);

    // Evaluate response magnitude
    double mag_at_1000 = eq.evaluate_response_db(1000.0);
    // Two filters at cutoff (-3dB each) -> approximately -6dB
    ASSERT_TRUE(mag_at_1000 < -3.0);

    // Reset Band 6 to flat so it doesn't cut high frequencies
    eq.set_band(5, 20000.0, 0.0, 0.707, 0);

    // Band 2: Peaking boost +12dB at 2500 Hz
    eq.set_band(1, 2500.0, 12.0, 1.0, 0);
    double mag_at_2500 = eq.evaluate_response_db(2500.0);
    ASSERT_TRUE(mag_at_2500 > 6.0); // Boosted significantly
}

TEST_CASE(UnitXAudio, XEqStateSerialization) {
    XEqDevice eq1;
    eq1.set_parameter(1, 0.75f); // Input gain
    eq1.set_band(2, 850.0, 6.0, 2.5, 0);

    auto state = eq1.save_state();
    ASSERT_TRUE(!state.empty());

    XEqDevice eq2;
    auto res = eq2.load_state(state);
    ASSERT_TRUE(res.is_ok());
    ASSERT_NEAR(eq2.get_parameter(1), 0.75f, 0.01f);
    ASSERT_NEAR(static_cast<float>(eq2.band_freq(2)), 850.0f, 1.0f);
    ASSERT_NEAR(static_cast<float>(eq2.band_gain(2)), 6.0f, 0.1f);
}

// ============================================================================
// 2. X-Compressor Unit Tests
// ============================================================================

TEST_CASE(UnitXAudio, XCompressorCompressionAndMakeup) {
    XCompressorDevice comp;
    ASSERT_EQ(comp.uid(), "core.fx.x_compressor");
    ASSERT_EQ(comp.name(), "X-Compressor");
    ASSERT_TRUE(comp.category() == DeviceCategory::Effect);
    ASSERT_EQ(comp.parameters().size(), 12);

    comp.set_threshold(-18.0);
    comp.set_ratio(4.0);
    comp.set_attack_ms(1.0);
    comp.set_release_ms(100.0);
    comp.set_knee(0.0);
    comp.set_makeup(0.0);
    comp.prepare(48000.0, 512);

    // 1. Quiet signal below threshold (-30 dB ~ 0.0316) -> uncompressed
    OwningAudioBuffer buf_quiet(256);
    for (size_t i = 0; i < 256; ++i) {
        buf_quiet.view().left[i] = 0.03f;
        buf_quiet.view().right[i] = 0.03f;
    }
    std::span<const MidiEvent> empty_midi{};
    auto view_quiet = buf_quiet.view();
    comp.process(view_quiet, empty_midi);
    ASSERT_NEAR(view_quiet.left[255], 0.03f, 0.002f);

    // 2. Loud AC audio signal above threshold (0 dB = 1.0 peak at 1 kHz)
    OwningAudioBuffer buf_loud(4096);
    for (size_t i = 0; i < 4096; ++i) {
        float s = std::sin(2.0 * xaudio::pi * 1000.0 * i / 48000.0);
        buf_loud.view().left[i] = s;
        buf_loud.view().right[i] = s;
    }
    auto view_loud = buf_loud.view();
    comp.process(view_loud, empty_midi);

    // With 0dB in and -18dB threshold with 4:1 ratio, 18dB overage reduces by 13.5dB -> -13.5dB (~0.211 linear peak)
    float steady_peak = 0.0f;
    for (size_t i = 4000; i < 4096; ++i) {
        steady_peak = std::max(steady_peak, std::abs(view_loud.left[i]));
    }
    ASSERT_TRUE(steady_peak < 0.45f);
    ASSERT_TRUE(comp.gain_reduction_db() > 5.0f);
}

TEST_CASE(UnitXAudio, XCompressorSidechainAndState) {
    XCompressorDevice comp1;
    comp1.set_threshold(-24.0);
    comp1.set_sidechain_hp(120.0);
    comp1.set_makeup(3.5);

    auto state = comp1.save_state();
    ASSERT_TRUE(!state.empty());

    XCompressorDevice comp2;
    auto res = comp2.load_state(state);
    ASSERT_TRUE(res.is_ok());
    ASSERT_NEAR(static_cast<float>(comp2.threshold()), -24.0f, 0.1f);
    ASSERT_NEAR(static_cast<float>(comp2.sidechain_hp()), 120.0f, 1.0f);
    ASSERT_NEAR(static_cast<float>(comp2.makeup()), 3.5f, 0.1f);
}

// ============================================================================
// 3. X-Multiband Unit Tests
// ============================================================================

TEST_CASE(UnitXAudio, XMultibandCrossoverFlatness) {
    XMultibandDevice mb;
    ASSERT_EQ(mb.uid(), "core.fx.x_multiband");
    ASSERT_EQ(mb.name(), "X-Multiband");
    ASSERT_TRUE(mb.category() == DeviceCategory::Effect);
    ASSERT_EQ(mb.parameters().size(), 39);

    // Set 1:1 ratio so all bands are linear (pure crossover pass-through)
    for (int b = 0; b < 4; ++b) {
        mb.set_band_params(b, -18.0, 1.0, 15.0, 150.0, 6.0, 0.0);
    }
    mb.prepare(48000.0, 512);

    // Test multiple frequencies spanning all 4 bands
    const double test_freqs[]{60.0, 200.0, 800.0, 2500.0, 7000.0, 14000.0};
    for (double freq : test_freqs) {
        OwningAudioBuffer buf(4096);
        for (size_t i = 0; i < 4096; ++i) {
            float s = 0.2f * std::sin(2.0 * xaudio::pi * freq * i / 48000.0);
            buf.view().left[i] = s;
            buf.view().right[i] = s;
        }
        std::span<const MidiEvent> empty_midi{};
        auto view = buf.view();
        mb.process(view, empty_midi);

        // Sum of power in second half of block should match input within 0.2 dB
        double in_pwr = 0.0, out_pwr = 0.0;
        for (size_t i = 2048; i < 4096; ++i) {
            float ref = 0.2f * std::sin(2.0 * xaudio::pi * freq * i / 48000.0);
            in_pwr += ref * ref;
            out_pwr += view.left[i] * view.left[i];
        }
        double diff_db = std::abs(xaudio::gainDb(std::sqrt(out_pwr / in_pwr)));
        ASSERT_TRUE(diff_db < 0.25);
    }
}

TEST_CASE(UnitXAudio, XMultibandSoloAndMute) {
    XMultibandDevice mb;
    mb.prepare(48000.0, 512);

    // Solo Band 1 (Low)
    mb.set_band_solo(0, true);
    ASSERT_TRUE(mb.band_solo(0));

    // High frequency (8000 Hz) input should be silenced by soloing low band
    OwningAudioBuffer buf(1024);
    for (size_t i = 0; i < 1024; ++i) {
        buf.view().left[i] = 0.5f * std::sin(2.0 * xaudio::pi * 8000.0 * i / 48000.0);
        buf.view().right[i] = buf.view().left[i];
    }
    std::span<const MidiEvent> empty_midi{};
    auto view = buf.view();
    mb.process(view, empty_midi);

    auto [peak_l, peak_r] = view.compute_peak();
    ASSERT_TRUE(peak_l < 0.05f); // High frequencies blocked by Low Band solo

    // Verify real-time band_level tracking used for 4-band histogram
    mb.reset();
    OwningAudioBuffer low_buf(1024);
    for (size_t i = 0; i < 1024; ++i) {
        low_buf.view().left[i] = 0.5f * std::sin(2.0 * xaudio::pi * 60.0 * i / 48000.0);
        low_buf.view().right[i] = low_buf.view().left[i];
    }
    auto low_view = low_buf.view();
    mb.process(low_view, empty_midi);
    ASSERT_TRUE(mb.band_level(0) > 0.1f); // Low band registers the bass energy
}

// ============================================================================
// 4. X-Reverb Unit Tests
// ============================================================================

TEST_CASE(UnitXAudio, XReverbPredelayAndTailDecay) {
    XReverbDevice rev;
    ASSERT_EQ(rev.uid(), "core.fx.x_reverb");
    ASSERT_EQ(rev.name(), "X-Reverb");
    ASSERT_TRUE(rev.category() == DeviceCategory::Effect);
    ASSERT_EQ(rev.parameters().size(), 10);

    rev.set_parameter(3, 1.0f); // 100% wet
    rev.set_predelay_ms(20.0);  // 20ms predelay (960 samples at 48kHz)
    rev.set_decay_s(0.8);
    rev.prepare(48000.0, 512);

    // Feed a single impulse on sample 0
    OwningAudioBuffer buf(48000);
    buf.view().clear();
    buf.view().left[0] = 1.0f;

    std::span<const MidiEvent> empty_midi{};
    auto view = buf.view();
    rev.process(view, empty_midi);

    // Check predelay: first 800 samples must be strictly 0
    float predelay_energy = 0.0f;
    for (size_t i = 0; i < 800; ++i) {
        predelay_energy += std::abs(view.left[i]) + std::abs(view.right[i]);
    }
    ASSERT_NEAR(predelay_energy, 0.0f, 1e-6f);

    // Check reverb tail is present and decaying
    float early_energy = 0.0f;
    for (size_t i = 1000; i < 10000; ++i) {
        early_energy += view.left[i] * view.left[i];
    }
    float late_energy = 0.0f;
    for (size_t i = 35000; i < 45000; ++i) {
        late_energy += view.left[i] * view.left[i];
    }
    ASSERT_TRUE(early_energy > 0.01f);
    ASSERT_TRUE(late_energy < early_energy * 0.05f); // Naturally decayed
}

// ============================================================================
// 5. X-Distortion Unit Tests
// ============================================================================

TEST_CASE(UnitXAudio, XDistortionHarmonicsAndDCBlocker) {
    XDistortionDevice dist;
    ASSERT_EQ(dist.uid(), "core.fx.x_distortion");
    ASSERT_EQ(dist.name(), "X-Distortion");
    ASSERT_TRUE(dist.category() == DeviceCategory::Effect);
    ASSERT_EQ(dist.parameters().size(), 7);

    dist.set_drive_db(24.0);
    dist.set_bias_pct(0.0);
    dist.set_parameter(3, 1.0f); // 100% wet
    dist.prepare(48000.0, 512);

    // 1000 Hz sine wave through saturation should produce odd harmonics (3000 Hz)
    OwningAudioBuffer buf(48000);
    for (size_t i = 0; i < 48000; ++i) {
        buf.view().left[i] = 0.6f * std::sin(2.0 * xaudio::pi * 1000.0 * i / 48000.0);
        buf.view().right[i] = buf.view().left[i];
    }
    std::span<const MidiEvent> empty_midi{};
    auto view = buf.view();
    dist.process(view, empty_midi);

    // Check Fourier coefficient of 3rd harmonic (3000 Hz)
    double harmonic3 = 0.0;
    for (size_t i = 0; i < 48000; ++i) {
        harmonic3 += view.left[i] * std::sin(2.0 * xaudio::pi * 3000.0 * i / 48000.0);
    }
    harmonic3 = std::abs(harmonic3) / 24000.0;
    ASSERT_TRUE(harmonic3 > 0.05); // 3rd harmonic is generated!

    // Check DC Blocker with DC offset signal
    XDistortionDevice dist_dc;
    dist_dc.set_drive_db(30.0);
    dist_dc.set_bias_pct(60.0);
    dist_dc.prepare(48000.0, 512);

    OwningAudioBuffer buf_dc(48000);
    for (size_t i = 0; i < 48000; ++i) {
        buf_dc.view().left[i] = 0.5f; // Pure DC
        buf_dc.view().right[i] = 0.5f;
    }
    auto view_dc = buf_dc.view();
    dist_dc.process(view_dc, empty_midi);

    // DC blocker should force steady-state output back to near zero
    double mean_tail = 0.0;
    for (size_t i = 30000; i < 48000; ++i) {
        mean_tail += std::abs(view_dc.left[i]);
    }
    mean_tail /= 18000.0;
    ASSERT_TRUE(mean_tail < 0.001); // DC successfully blocked!
}

// ============================================================================
// 6. X-Limiter Unit Tests
// ============================================================================

TEST_CASE(UnitXAudio, XLimiterPeakClampingAndCeiling) {
    XLimiterDevice limit;
    ASSERT_EQ(limit.uid(), "core.fx.x_limiter");
    ASSERT_EQ(limit.name(), "X-Limiter");
    ASSERT_TRUE(limit.category() == DeviceCategory::Effect);
    ASSERT_EQ(limit.parameters().size(), 7);

    limit.set_threshold_db(-12.0); // +12dB threshold boost
    limit.set_ceiling_db(-1.0);    // Ceiling at -1 dBFS (~0.891 linear)
    limit.set_release_ms(50.0);
    limit.prepare(48000.0, 512);

    const double ceiling_linear = xaudio::dbGain(-1.0);

    // Large signals up to +18 dBFS (8.0)
    OwningAudioBuffer buf(4096);
    for (size_t i = 0; i < 4096; ++i) {
        float s = (i % 2 == 0 ? 5.0f : -4.0f) * std::sin(static_cast<float>(i) * 0.1f);
        buf.view().left[i] = s;
        buf.view().right[i] = s * 0.5f;
    }

    std::span<const MidiEvent> empty_midi{};
    auto view = buf.view();
    limit.process(view, empty_midi);

    auto [peak_l, peak_r] = view.compute_peak();
    // Signal must NEVER exceed ceiling (+ epsilon for float precision)
    ASSERT_TRUE(peak_l <= ceiling_linear + 1e-4);
    ASSERT_TRUE(peak_r <= ceiling_linear + 1e-4);
    ASSERT_TRUE(limit.gain_reduction_db() > 10.0f);
}

// ============================================================================
// 7. X-Synth Unit Tests
// ============================================================================

TEST_CASE(UnitXAudio, XSynthNoteOnNoteOffAndPolyphony) {
    XSynthDevice synth;
    ASSERT_EQ(synth.uid(), "core.generator.x_synth");
    ASSERT_EQ(synth.name(), "X-Synth");
    ASSERT_TRUE(synth.category() == DeviceCategory::Generator);
    ASSERT_EQ(synth.parameters().size(), 17);

    synth.prepare(44100.0, 512);

    // 1. Without MIDI, synth outputs silence
    OwningAudioBuffer buf1(512);
    buf1.view().clear();
    std::span<const MidiEvent> empty_midi{};
    auto view1 = buf1.view();
    synth.process(view1, empty_midi);
    auto [p1_l, p1_r] = view1.compute_peak();
    ASSERT_NEAR(p1_l, 0.0f, 1e-6f);
    ASSERT_NEAR(p1_r, 0.0f, 1e-6f);

    // 2. NoteOn C4 (MIDI 60) triggers audio synthesis
    std::vector<MidiEvent> midi_on = { MidiEvent::make_note_on(0, 0, 60, 100) };
    OwningAudioBuffer buf2(512);
    buf2.view().clear();
    auto view2 = buf2.view();
    synth.process(view2, midi_on);
    auto [p2_l, p2_r] = view2.compute_peak();
    ASSERT_TRUE(p2_l > 0.05f); // Generating audio!

    // 3. Polyphony: NoteOn E4 (64) and G4 (67)
    std::vector<MidiEvent> midi_chord = {
        MidiEvent::make_note_on(0, 0, 64, 100),
        MidiEvent::make_note_on(0, 0, 67, 100)
    };
    OwningAudioBuffer buf3(512);
    buf3.view().clear();
    auto view3 = buf3.view();
    synth.process(view3, midi_chord);
    auto [p3_l, p3_r] = view3.compute_peak();
    ASSERT_TRUE(p3_l > 0.1f);

    // 4. NoteOff all notes
    std::vector<MidiEvent> midi_off = {
        MidiEvent::make_note_off(0, 0, 60),
        MidiEvent::make_note_off(0, 0, 64),
        MidiEvent::make_note_off(0, 0, 67)
    };
    OwningAudioBuffer buf4(512);
    buf4.view().clear();
    auto view4 = buf4.view();
    synth.process(view4, midi_off);

    // Run additional blocks to allow release stage to finish
    for (int block = 0; block < 30; ++block) {
        OwningAudioBuffer buf_decay(512);
        buf_decay.view().clear();
        auto view_decay = buf_decay.view();
        synth.process(view_decay, empty_midi);
    }

    OwningAudioBuffer buf_silent(512);
    buf_silent.view().clear();
    auto view_silent = buf_silent.view();
    synth.process(view_silent, empty_midi);
    auto [p_sil_l, p_sil_r] = view_silent.compute_peak();
    ASSERT_NEAR(p_sil_l, 0.0f, 1e-4f);
}

TEST_CASE(UnitXAudio, XSynthPolyphonyVoiceStealingStress) {
    XSynthDevice synth;
    synth.prepare(44100.0, 512);
    std::span<const MidiEvent> empty_midi{};

    // Trigger 32 rapid notes into a 16-voice synth
    for (uint8_t note = 40; note < 72; ++note) {
        std::vector<MidiEvent> evs = { MidiEvent::make_note_on(0, 0, note, 110) };
        OwningAudioBuffer buf(64);
        buf.view().clear();
        auto view = buf.view();
        synth.process(view, evs);
    }

    // Active voices capped at 16
    ASSERT_EQ(synth.active_voices(), 16);

    // Run a full buffer and verify clean audio without NaN or Inf
    OwningAudioBuffer buf(512);
    buf.view().clear();
    auto view = buf.view();
    synth.process(view, empty_midi);

    for (size_t i = 0; i < 512; ++i) {
        ASSERT_FALSE(std::isnan(view.left[i]));
        ASSERT_FALSE(std::isnan(view.right[i]));
        ASSERT_FALSE(std::isinf(view.left[i]));
        ASSERT_FALSE(std::isinf(view.right[i]));
    }
    auto [pk_l, pk_r] = view.compute_peak();
    ASSERT_TRUE(pk_l > 0.1f);
}

TEST_CASE(UnitXAudio, XSynthMidiAllNotesOff) {
    XSynthDevice synth;
    synth.prepare(44100.0, 512);
    synth.set_adsr(5.0, 50.0, 0.8, 10.0); // Fast release

    // Sound 4 notes
    std::vector<MidiEvent> notes = {
        MidiEvent::make_note_on(0, 0, 60, 100),
        MidiEvent::make_note_on(0, 0, 64, 100),
        MidiEvent::make_note_on(0, 0, 67, 100),
        MidiEvent::make_note_on(0, 0, 71, 100)
    };
    OwningAudioBuffer buf(256);
    buf.view().clear();
    auto view = buf.view();
    synth.process(view, notes);
    ASSERT_EQ(synth.active_voices(), 4);

    // Send MIDI CC 123 (All Notes Off)
    std::vector<MidiEvent> cc_off = { MidiEvent::make_cc(0, 0, 123, 0) };
    synth.process(view, cc_off);

    // Let voices release
    std::span<const MidiEvent> empty_midi{};
    for (int i = 0; i < 20; ++i) {
        OwningAudioBuffer rel_buf(256);
        rel_buf.view().clear();
        auto rel_view = rel_buf.view();
        synth.process(rel_view, empty_midi);
    }

    ASSERT_EQ(synth.active_voices(), 0);
}

TEST_CASE(UnitXAudio, XAudioFactoryPresets) {
    // 1. X-Eq Presets
    XEqDevice eq;
    ASSERT_EQ(eq.preset_names().size(), 3);
    eq.load_preset(1); // Vocal Air
    ASSERT_EQ(eq.active_preset(), 1);
    ASSERT_NEAR(static_cast<float>(eq.band_gain(4)), 2.0f, 0.01f);
    eq.load_preset(0); // Reset
    ASSERT_EQ(eq.active_preset(), 0);
    ASSERT_NEAR(static_cast<float>(eq.band_gain(4)), 0.0f, 0.01f);

    // 2. X-Compressor Presets
    XCompressorDevice comp;
    ASSERT_EQ(comp.preset_names().size(), 3);
    comp.load_preset(1); // Vocal Leveler
    ASSERT_EQ(comp.active_preset(), 1);
    ASSERT_NEAR(static_cast<float>(comp.get_plain(4)), -22.0f, 0.01f); // threshold

    // 3. X-Multiband Presets
    XMultibandDevice mb;
    ASSERT_EQ(mb.preset_names().size(), 3);
    mb.load_preset(2); // Master Punch
    ASSERT_EQ(mb.active_preset(), 2);

    // 4. X-Reverb Presets
    XReverbDevice rev;
    ASSERT_EQ(rev.preset_names().size(), 3);
    rev.load_preset(1); // Small Vocal Room
    ASSERT_EQ(rev.active_preset(), 1);
    ASSERT_NEAR(static_cast<float>(rev.get_plain(3)), 18.0f, 0.01f); // mix

    // 5. X-Distortion Presets
    XDistortionDevice dist;
    ASSERT_EQ(dist.preset_names().size(), 3);
    dist.load_preset(1); // Warm Saturation
    ASSERT_EQ(dist.active_preset(), 1);
    ASSERT_NEAR(static_cast<float>(dist.get_plain(4)), 6.0f, 0.01f); // drive

    // 6. X-Limiter Presets
    XLimiterDevice lim;
    ASSERT_EQ(lim.preset_names().size(), 3);
    lim.load_preset(2); // Transparent Master
    ASSERT_EQ(lim.active_preset(), 2);
    ASSERT_NEAR(static_cast<float>(lim.get_plain(6)), 60.0f, 0.01f); // release
}

TEST_CASE(UnitXAudio, XSynthStateSerialization) {
    XSynthDevice s1;
    s1.set_osc1(2, 0.85, 1); // Saw, 85% vol, +1 oct
    s1.set_osc2(3, 0.65, 7, -1); // Square, 65% vol, +7 semi, -1 oct
    s1.set_filter(3200.0, 4.5, 1); // Highpass, 3200Hz, Q=4.5
    s1.set_drive(18.0);
    s1.set_adsr(15.0, 120.0, 0.45, 350.0);

    auto state = s1.save_state();
    ASSERT_TRUE(!state.empty());

    XSynthDevice s2;
    auto res = s2.load_state(state);
    ASSERT_TRUE(res.is_ok());

    ASSERT_EQ(s2.osc1_shape(), 2);
    ASSERT_EQ(s2.osc1_octave(), 1);
    ASSERT_NEAR(static_cast<float>(s2.osc1_vol()), 0.85f, 0.02f);
    ASSERT_EQ(s2.osc2_shape(), 3);
    ASSERT_EQ(s2.osc2_octave(), -1);
    ASSERT_EQ(s2.filter_type(), 1);
    ASSERT_NEAR(static_cast<float>(s2.drive()), 18.0f, 0.5f);
}

// ============================================================================
// 8. PluginManager Registration & Instantiation
// ============================================================================

TEST_CASE(UnitXAudio, PluginManagerRegistersAllXAudioDevices) {
    digidaw::app::PluginManager mgr;

    // Check Generator
    auto res_synth = mgr.instantiate("core.generator.x_synth");
    ASSERT_TRUE(res_synth.is_ok());
    ASSERT_EQ(res_synth.value()->name(), "X-Synth");
    ASSERT_TRUE(res_synth.value()->category() == DeviceCategory::Generator);

    // Check all 6 Effects
    const std::vector<std::pair<std::string, std::string>> fx_list = {
        {"core.fx.x_eq", "X-Eq"},
        {"core.fx.x_compressor", "X-Compressor"},
        {"core.fx.x_multiband", "X-Multiband"},
        {"core.fx.x_reverb", "X-Reverb"},
        {"core.fx.x_distortion", "X-Distortion"},
        {"core.fx.x_limiter", "X-Limiter"}
    };

    for (const auto& [uid, expected_name] : fx_list) {
        auto res = mgr.instantiate(uid);
        ASSERT_TRUE(res.is_ok());
        ASSERT_EQ(res.value()->name(), expected_name);
        ASSERT_TRUE(res.value()->category() == DeviceCategory::Effect);
    }

    // Verify aliases
    auto res_alias_eq = mgr.instantiate("xaudio.fx.eq");
    ASSERT_TRUE(res_alias_eq.is_ok());
    ASSERT_EQ(res_alias_eq.value()->name(), "X-Eq");

    auto res_legacy_eq = mgr.instantiate("core.fx.parametric_eq");
    ASSERT_TRUE(res_legacy_eq.is_ok());
    ASSERT_TRUE(dynamic_cast<XEqDevice*>(res_legacy_eq.value().get()) != nullptr);

    auto res_alias_lim = mgr.instantiate("xaudio.fx.limiter");
    ASSERT_TRUE(res_alias_lim.is_ok());
    ASSERT_EQ(res_alias_lim.value()->name(), "X-Limiter");

    auto res_alias_syn = mgr.instantiate("xaudio.generator.synth");
    ASSERT_TRUE(res_alias_syn.is_ok());
    ASSERT_EQ(res_alias_syn.value()->name(), "X-Synth");
}

TEST_CASE(UnitXAudio, XAudioSignalVisualizationBuffers) {
    XEqDevice eq;
    eq.prepare(48000.0, 512);

    ASSERT_EQ(eq.vis_buf_size(), 1024);
    ASSERT_TRUE(eq.vis_in_data() != nullptr);
    ASSERT_TRUE(eq.vis_out_data() != nullptr);

    // Initial state after prepare should be clean
    ASSERT_EQ(eq.vis_write_pos(), 0);

    // Process audio buffer
    OwningAudioBuffer buf(512);
    for (size_t i = 0; i < 512; ++i) {
        float sig = 0.5f * std::sin(static_cast<float>(i) * 0.1f);
        buf.view().left[i] = sig;
        buf.view().right[i] = sig;
    }

    std::span<const MidiEvent> empty_midi{};
    auto view = buf.view();
    eq.process(view, empty_midi);

    // After processing 512 samples, write pos should have advanced to 512
    ASSERT_EQ(eq.vis_write_pos(), 512);

    // Check that samples were recorded
    bool found_nonzero = false;
    for (size_t i = 0; i < 512; ++i) {
        if (std::abs(eq.vis_in_data()[i]) > 0.01f) {
            found_nonzero = true;
            break;
        }
    }
    ASSERT_TRUE(found_nonzero);

    // Reset should zero write pos
    eq.reset();
    ASSERT_EQ(eq.vis_write_pos(), 0);
}

TEST_CASE(UnitXAudio, XAudioDynamicsRollingAnalysisHistory) {
    XCompressorDevice comp;
    comp.prepare(48000.0, 512);

    ASSERT_EQ(comp.hist_size(), 512);
    ASSERT_TRUE(comp.hist_in_data() != nullptr);
    ASSERT_TRUE(comp.hist_out_data() != nullptr);
    ASSERT_TRUE(comp.hist_gr_data() != nullptr);
    ASSERT_EQ(comp.hist_write_pos(), 0);

    // Process audio buffer with 512 samples (2 steps of 256 samples)
    OwningAudioBuffer buf(512);
    for (size_t i = 0; i < 512; ++i) {
        float sig = 0.8f * std::sin(static_cast<float>(i) * 0.1f);
        buf.view().left[i] = sig;
        buf.view().right[i] = sig;
    }

    std::span<const MidiEvent> empty_midi{};
    auto view = buf.view();
    comp.process(view, empty_midi);

    // 512 samples / 256 samples-per-slice = exactly 2 history points pushed
    ASSERT_EQ(comp.hist_write_pos(), 2);
    ASSERT_TRUE(comp.hist_in_data()[0] > 0.1f);
    ASSERT_TRUE(comp.hist_out_data()[0] > 0.1f);

    // Reset should clear history pos
    comp.reset();
    ASSERT_EQ(comp.hist_write_pos(), 0);

    // Verify XLimiterDevice also has identical history capabilities
    XLimiterDevice lim;
    lim.prepare(48000.0, 512);
    ASSERT_EQ(lim.hist_size(), 512);
    ASSERT_EQ(lim.hist_write_pos(), 0);

    auto view_lim = buf.view();
    lim.process(view_lim, empty_midi);
    ASSERT_EQ(lim.hist_write_pos(), 2);
    ASSERT_TRUE(lim.hist_in_data()[0] > 0.1f);
    lim.reset();
    ASSERT_EQ(lim.hist_write_pos(), 0);
}

