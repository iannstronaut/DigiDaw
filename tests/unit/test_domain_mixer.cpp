#include "../test_framework.hpp"
#include "../../domain/mixer/mixer_graph.hpp"

using namespace digidaw::domain;

TEST_CASE(DomainMixer, MasterTrackExistsByDefault) {
    MixerGraph graph;
    auto* master = graph.get_track(MasterTrackId);
    ASSERT_TRUE(master != nullptr);
    ASSERT_EQ(master->id(), MasterTrackId);
    ASSERT_EQ(master->name(), "Master");
}

TEST_CASE(DomainMixer, CycleDetectionRejectsLoop) {
    MixerGraph graph;
    graph.add_track(1, "Track 1");
    graph.add_track(2, "Track 2");
    graph.add_track(3, "Track 3");

    // Connect 1 -> 2 -> 3
    ASSERT_OK(graph.connect_send(1, 2));
    ASSERT_OK(graph.connect_send(2, 3));

    // Trying to connect 3 -> 1 would form a cycle! (1->2->3->1)
    auto cycle_res = graph.connect_send(3, 1);
    ASSERT_TRUE(cycle_res.is_error());
    ASSERT_EQ(cycle_res.error_code(), ErrorCode::GraphCycleDetected);

    // Direct self-send (1 -> 1) must also be rejected
    auto self_res = graph.connect_send(1, 1);
    ASSERT_TRUE(self_res.is_error());
    ASSERT_EQ(self_res.error_code(), ErrorCode::GraphCycleDetected);
}

TEST_CASE(DomainMixer, TopologicalOrderValid) {
    MixerGraph graph;
    graph.add_track(1, "Vocal");
    graph.add_track(2, "Reverb Send");

    // Route Vocal -> Reverb Send -> Master
    ASSERT_OK(graph.connect_send(1, 2));
    ASSERT_OK(graph.connect_send(2, MasterTrackId));

    const auto& order = graph.topological_order();
    // 1 must come before 2, and 2 before Master
    auto pos_1 = std::find(order.begin(), order.end(), 1);
    auto pos_2 = std::find(order.begin(), order.end(), 2);
    auto pos_m = std::find(order.begin(), order.end(), MasterTrackId);

    ASSERT_TRUE(pos_1 != order.end());
    ASSERT_TRUE(pos_2 != order.end());
    ASSERT_TRUE(pos_m != order.end());
    ASSERT_TRUE(pos_1 < pos_2);
    ASSERT_TRUE(pos_2 < pos_m);
}

TEST_CASE(DomainMixer, AudioSummingAndPanLaws) {
    MixerGraph graph;
    graph.prepare(44100.0, 64);

    graph.add_track(1, "Synth Track");
    auto* t1 = graph.get_track(1);
    t1->set_volume(0.5f);
    t1->set_pan(0.0f); // Center

    OwningAudioBuffer input_buf(64);
    for (size_t i = 0; i < 64; ++i) {
        input_buf.view().left[i] = 1.0f;
        input_buf.view().right[i] = 1.0f;
    }

    std::unordered_map<MixerTrackId, AudioBufferView> inputs;
    inputs[1] = input_buf.view();

    OwningAudioBuffer master_out(64);
    auto master_view = master_out.view();
    graph.process(inputs, master_view);

    // With 0.5 volume and equal power center (~0.707 gain), master peak should be ~0.35
    auto [peak_l, peak_r] = master_view.compute_peak();
    ASSERT_NEAR(peak_l, 0.3535f, 0.01f);
    ASSERT_NEAR(peak_r, 0.3535f, 0.01f);
}
