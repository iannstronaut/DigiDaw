#include "../test_framework.hpp"
#include "../../adapters/plugins/sampler_device.hpp"

using namespace digidaw::domain;
using namespace digidaw::adapters::plugins;

TEST_CASE(UnitSampler, TriggersNoteAndResamples) {
    SamplerDevice sampler;
    sampler.prepare(44100.0, 512);

    // Send MIDI Note On (C4, pitch 60, vel 100)
    MidiEvent ev_on{0, 0x90, 60, 100};
    MidiEvent midi_events[1] = {ev_on};

    OwningAudioBuffer buf(512);
    auto view = buf.view();
    sampler.process(view, std::span<const MidiEvent>(midi_events, 1));

    auto [peak_l, peak_r] = view.compute_peak();
    ASSERT_TRUE(peak_l > 0.05f); // Sample is generating audio
    ASSERT_TRUE(peak_r > 0.05f);

    // Send Note Off
    MidiEvent ev_off{0, 0x80, 60, 0};
    MidiEvent midi_off[1] = {ev_off};
    sampler.process(view, std::span<const MidiEvent>(midi_off, 1));
}
