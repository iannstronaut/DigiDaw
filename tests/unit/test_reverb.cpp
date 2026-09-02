#include "../test_framework.hpp"
#include "../../adapters/plugins/reverb_device.hpp"

using namespace digidaw::domain;
using namespace digidaw::adapters::plugins;

TEST_CASE(UnitReverb, ImpulseResponseDecaysNaturally) {
    ReverbDevice verb;
    verb.set_room_size(0.7f);
    verb.set_wet(1.0f);
    verb.set_dry(0.0f);
    verb.prepare(44100.0, 512);

    // Send single impulse at sample 0
    OwningAudioBuffer impulse_buf(512);
    impulse_buf.view().left[0] = 1.0f;
    impulse_buf.view().right[0] = 1.0f;

    std::span<const MidiEvent> empty_midi{};
    auto view = impulse_buf.view();
    verb.process(view, empty_midi);

    // After impulse, comb filters must produce distributed energy
    OwningAudioBuffer tail_buf(4096);
    auto tail_view = tail_buf.view();
    verb.process(tail_view, empty_midi);

    auto [peak_l, peak_r] = tail_view.compute_peak();
    ASSERT_TRUE(peak_l > 0.01f); // Reverb tail is audible!
    ASSERT_TRUE(peak_r > 0.01f);

    // Tail must not blow up (stability check)
    ASSERT_TRUE(peak_l <= 2.0f);
    ASSERT_TRUE(peak_r <= 2.0f);
}
