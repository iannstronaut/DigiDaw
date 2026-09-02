#include "../test_framework.hpp"
#include "../../adapters/plugins/drum_sampler_device.hpp"

using namespace digidaw::domain;
using namespace digidaw::adapters::plugins;

TEST_CASE(UnitDrumSampler, TriggersPadsAndMuteGroups) {
    DrumSamplerDevice drums;
    drums.prepare(44100.0, 512);

    ASSERT_EQ(drums.pads().size(), 16);

    // Trigger Kick (Pad 0, Note 36)
    drums.trigger_pad(0, 110);
    ASSERT_TRUE(drums.pads()[0].playing);

    OwningAudioBuffer buf(512);
    auto view = buf.view();
    std::span<const MidiEvent> empty_midi{};
    drums.process(view, empty_midi);

    auto [peak_l, peak_r] = view.compute_peak();
    ASSERT_TRUE(peak_l > 0.05f);

    // Test Mute Group: Open Hi-Hat (Pad 3, Note 46) muted by Closed Hi-Hat (Pad 2, Note 42)
    drums.trigger_pad(3, 100); // Open HH playing
    ASSERT_TRUE(drums.pads()[3].playing);

    // Trigger Closed HH (mute group 1)
    drums.trigger_pad(2, 100);
    ASSERT_TRUE(drums.pads()[2].playing);
    ASSERT_FALSE(drums.pads()[3].playing); // Open HH was instantly cut by mute group!
}
