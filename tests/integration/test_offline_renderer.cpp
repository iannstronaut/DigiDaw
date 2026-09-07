#include "../test_framework.hpp"
#include "../../app/usecases/offline_renderer.hpp"
#include "../../adapters/plugins/synth_3xosc.hpp"
#include "../../adapters/plugins/xaudio_devices.hpp"
#include "../../adapters/plugins/limiter_device.hpp"
#include <filesystem>
#include <fstream>

using namespace digidaw::domain;
using namespace digidaw::app;
using namespace digidaw::adapters::plugins;
using namespace digidaw::adapters::audio;

TEST_CASE(IntegrationAudio, OfflineRenderToWav) {
    const std::string out_wav = "test_render.wav";

    Project proj("Render Test");
    proj.time_map().set_tempo(120.0);

    // Add synth channel
    ChannelSettings s;
    s.name = "Synth";
    s.volume = 0.8f;
    s.mixer_track = 1;
    ChannelId ch_id = proj.add_channel("core.generator.3xosc", s);

    // Add notes to pattern 1
    auto* pat = proj.get_pattern(1);
    ASSERT_TRUE(pat != nullptr);
    auto& notes = pat->get_or_create_channel_notes(ch_id);
    notes.add_note(Note{0, 480, 60, 100, 0, 0});
    notes.add_note(Note{480, 480, 64, 100, 0, 0});

    // Add clip to playlist track 1
    proj.tracks()[0].add_clip(Clip{1, 0, DefaultPPQ * 4, false});

    // Add EQ to insert 1, Limiter to Master
    auto synth = std::make_shared<Synth3xOsc>();
    auto eq = std::make_shared<XEqDevice>();
    auto limiter = std::make_shared<LimiterDevice>();

    auto* trk1 = proj.mixer_graph().get_track(1);
    ASSERT_TRUE(trk1 != nullptr);
    trk1->add_insert(eq);

    auto* master = proj.mixer_graph().get_track(MasterTrackId);
    ASSERT_TRUE(master != nullptr);
    master->add_insert(limiter);

    std::unordered_map<ChannelId, std::shared_ptr<IDevice>> devs;
    devs[ch_id] = synth;

    // Render 1 bar (4 beats = 2.0s @ 120 BPM)
    Tick duration = DefaultPPQ * 4;
    auto render_res = OfflineRenderer::render_to_wav(
        proj, devs, out_wav, duration, 44100.0, 256, WaveBitDepth::PCM16);

    ASSERT_OK(render_res);
    ASSERT_TRUE(std::filesystem::exists(out_wav));

    // Verify WAV file size: 44100 samples/s * 2s = 88200 frames * 4 bytes/frame = 352800 data bytes + 44 header = 352844 bytes
    auto file_size = std::filesystem::file_size(out_wav);
    ASSERT_TRUE(file_size >= 352844);

    std::filesystem::remove(out_wav);
}

TEST_CASE(ParityPERF04, BitwiseDeterministicRender) {
    const std::string wav1 = "test_perf04_a.wav";
    const std::string wav2 = "test_perf04_b.wav";

    Project proj("Deterministic Render");
    proj.time_map().set_tempo(130.0);

    ChannelSettings s;
    s.name = "Synth";
    s.volume = 0.9f;
    s.mixer_track = 1;
    ChannelId ch_id = proj.add_channel("core.generator.3xosc", s);

    auto* pat = proj.get_pattern(1);
    auto& notes = pat->get_or_create_channel_notes(ch_id);
    notes.add_note(Note{0, 240, 57, 100, 0, 0});
    notes.add_note(Note{240, 240, 60, 100, 0, 0});
    notes.add_note(Note{480, 480, 64, 100, 0, 0});

    proj.tracks()[0].add_clip(Clip{1, 0, DefaultPPQ * 4, false});

    auto synth1 = std::make_shared<Synth3xOsc>();
    std::unordered_map<ChannelId, std::shared_ptr<IDevice>> devs1;
    devs1[ch_id] = synth1;

    // Render pass 1
    ASSERT_OK(OfflineRenderer::render_to_wav(
        proj, devs1, wav1, DefaultPPQ * 2, 44100.0, 128, WaveBitDepth::Float32));

    auto synth2 = std::make_shared<Synth3xOsc>();
    std::unordered_map<ChannelId, std::shared_ptr<IDevice>> devs2;
    devs2[ch_id] = synth2;

    // Render pass 2
    ASSERT_OK(OfflineRenderer::render_to_wav(
        proj, devs2, wav2, DefaultPPQ * 2, 44100.0, 128, WaveBitDepth::Float32));

    // Compare byte-for-byte
    std::ifstream f1(wav1, std::ios::binary);
    std::ifstream f2(wav2, std::ios::binary);

    std::vector<char> buf1((std::istreambuf_iterator<char>(f1)), std::istreambuf_iterator<char>());
    std::vector<char> buf2((std::istreambuf_iterator<char>(f2)), std::istreambuf_iterator<char>());

    ASSERT_EQ(buf1.size(), buf2.size());
    bool identical = (buf1 == buf2);
    ASSERT_TRUE(identical); // PERF-04 Gate Satisfied: Bitwise Identical Render!

    f1.close();
    f2.close();

    std::filesystem::remove(wav1);
    std::filesystem::remove(wav2);
}
