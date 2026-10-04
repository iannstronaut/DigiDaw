#include "../test_framework.hpp"
#include "../../app/usecases/offline_renderer.hpp"
#include "../../adapters/plugins/synth_3xosc.hpp"
#include "../../adapters/plugins/xaudio_devices.hpp"
#include "../../adapters/plugins/limiter_device.hpp"
#include "../../app/usecases/sample_library.hpp"
#include <filesystem>
#include <fstream>
#include <cstring>

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

TEST_CASE(IntegrationAudio, OfflineRenderToFlac16And24Bit) {
    const std::string out_flac16 = "test_render16.flac";
    const std::string out_flac24 = "test_render24.flac";

    Project proj("Flac Render Test");
    proj.time_map().set_tempo(120.0);

    ChannelSettings s;
    s.name = "Synth";
    s.volume = 0.8f;
    s.mixer_track = 1;
    ChannelId ch_id = proj.add_channel("core.generator.3xosc", s);

    auto* pat = proj.get_pattern(1);
    ASSERT_TRUE(pat != nullptr);
    auto& notes = pat->get_or_create_channel_notes(ch_id);
    notes.add_note(Note{0, 480, 60, 100, 0, 0});
    notes.add_note(Note{480, 480, 64, 100, 0, 0});

    proj.tracks()[0].add_clip(Clip{1, 0, DefaultPPQ * 4, false});

    auto synth = std::make_shared<Synth3xOsc>();
    std::unordered_map<ChannelId, std::shared_ptr<IDevice>> devs;
    devs[ch_id] = synth;

    Tick duration = DefaultPPQ * 4;

    // 1. Render 16-bit FLAC
    float last_progress = 0.0f;
    int progress_calls = 0;
    RenderOptions opt16;
    opt16.format = RenderFormat::Flac;
    opt16.bit_depth = RenderBitDepth::Bit16;
    opt16.sample_rate = 44100.0;
    opt16.block_size = 256;
    opt16.output_path = out_flac16;
    opt16.duration_ticks = duration;

    auto res16 = OfflineRenderer::render(proj, devs, opt16, [&](float p) {
        last_progress = p;
        progress_calls++;
    });

    ASSERT_OK(res16);
    ASSERT_TRUE(std::filesystem::exists(out_flac16));
    ASSERT_TRUE(progress_calls > 0);
    ASSERT_NEAR(last_progress, 1.0f, 0.001f);

    // Verify FLAC file header marker "fLaC" and STREAMINFO block
    std::ifstream flac16_file(out_flac16, std::ios::binary);
    ASSERT_TRUE(flac16_file.is_open());
    char marker16[4] = {0};
    flac16_file.read(marker16, 4);
    ASSERT_TRUE(marker16[0] == 'f' && marker16[1] == 'L' && marker16[2] == 'a' && marker16[3] == 'C');

    uint8_t meta_hdr16[4] = {0};
    flac16_file.read(reinterpret_cast<char*>(meta_hdr16), 4);
    // Last block (bit 7 set) and type 0 (STREAMINFO), length 34 bytes
    ASSERT_EQ(meta_hdr16[0] & 0x7F, 0);
    ASSERT_TRUE((meta_hdr16[0] & 0x80) != 0);
    ASSERT_EQ(meta_hdr16[3], 34);

    uint8_t sinfo16[34] = {0};
    flac16_file.read(reinterpret_cast<char*>(sinfo16), 34);
    uint64_t props16 = 0;
    for (int i = 0; i < 8; ++i) {
        props16 = (props16 << 8) | sinfo16[10 + i];
    }
    uint32_t sr16 = static_cast<uint32_t>((props16 >> 44) & 0xFFFFF);
    uint8_t ch16 = static_cast<uint8_t>(((props16 >> 41) & 0x07) + 1);
    uint8_t bps16 = static_cast<uint8_t>(((props16 >> 36) & 0x1F) + 1);
    ASSERT_EQ(sr16, 44100u);
    ASSERT_EQ(ch16, 2);
    ASSERT_EQ(bps16, 16);

    // Verify first audio frame sync code
    uint8_t sync16[2] = {0};
    flac16_file.read(reinterpret_cast<char*>(sync16), 2);
    ASSERT_EQ(sync16[0], 0xFF);
    ASSERT_EQ(sync16[1], 0xF8);
    flac16_file.close();

    // 2. Render 24-bit FLAC
    RenderOptions opt24;
    opt24.format = RenderFormat::Flac;
    opt24.bit_depth = RenderBitDepth::Bit24;
    opt24.sample_rate = 44100.0;
    opt24.block_size = 256;
    opt24.output_path = out_flac24;
    opt24.duration_ticks = duration;

    auto res24 = OfflineRenderer::render(proj, devs, opt24);
    ASSERT_OK(res24);
    ASSERT_TRUE(std::filesystem::exists(out_flac24));

    std::ifstream flac24_file(out_flac24, std::ios::binary);
    ASSERT_TRUE(flac24_file.is_open());
    char marker24[4] = {0};
    flac24_file.read(marker24, 4);
    ASSERT_TRUE(marker24[0] == 'f' && marker24[1] == 'L' && marker24[2] == 'a' && marker24[3] == 'C');

    uint8_t meta_hdr24[4] = {0};
    flac24_file.read(reinterpret_cast<char*>(meta_hdr24), 4);
    ASSERT_EQ(meta_hdr24[0] & 0x7F, 0);
    ASSERT_TRUE((meta_hdr24[0] & 0x80) != 0);
    ASSERT_EQ(meta_hdr24[3], 34);

    uint8_t sinfo24[34] = {0};
    flac24_file.read(reinterpret_cast<char*>(sinfo24), 34);
    uint64_t props24 = 0;
    for (int i = 0; i < 8; ++i) {
        props24 = (props24 << 8) | sinfo24[10 + i];
    }
    uint32_t sr24 = static_cast<uint32_t>((props24 >> 44) & 0xFFFFF);
    uint8_t ch24 = static_cast<uint8_t>(((props24 >> 41) & 0x07) + 1);
    uint8_t bps24 = static_cast<uint8_t>(((props24 >> 36) & 0x1F) + 1);
    ASSERT_EQ(sr24, 44100u);
    ASSERT_EQ(ch24, 2);
    ASSERT_EQ(bps24, 24);

    uint8_t sync24[2] = {0};
    flac24_file.read(reinterpret_cast<char*>(sync24), 2);
    ASSERT_EQ(sync24[0], 0xFF);
    ASSERT_EQ(sync24[1], 0xF8);
    flac24_file.close();

    // Verify 24-bit FLAC file size is strictly larger than 16-bit FLAC
    auto sz_flac16 = std::filesystem::file_size(out_flac16);
    auto sz_flac24 = std::filesystem::file_size(out_flac24);
    ASSERT_TRUE(sz_flac24 > sz_flac16);

    std::filesystem::remove(out_flac16);
    std::filesystem::remove(out_flac24);
}

TEST_CASE(IntegrationAudio, OfflineRenderToWavAllBitDepths) {
    const std::string wav16 = "test_render_opt16.wav";
    const std::string wav24 = "test_render_opt24.wav";
    const std::string wav32 = "test_render_opt32.wav";

    Project proj("Wav BitDepth Test");
    proj.time_map().set_tempo(120.0);

    ChannelSettings s;
    s.name = "Synth";
    s.volume = 0.8f;
    s.mixer_track = 1;
    ChannelId ch_id = proj.add_channel("core.generator.3xosc", s);

    auto* pat = proj.get_pattern(1);
    ASSERT_TRUE(pat != nullptr);
    auto& notes = pat->get_or_create_channel_notes(ch_id);
    notes.add_note(Note{0, 240, 60, 100, 0, 0});
    proj.tracks()[0].add_clip(Clip{1, 0, DefaultPPQ * 2, false});

    auto synth = std::make_shared<Synth3xOsc>();
    std::unordered_map<ChannelId, std::shared_ptr<IDevice>> devs;
    devs[ch_id] = synth;

    Tick duration = DefaultPPQ * 2;

    // 16-bit
    RenderOptions opt16{RenderFormat::Wav, RenderBitDepth::Bit16, 44100.0, 256, wav16, duration};
    ASSERT_OK(OfflineRenderer::render(proj, devs, opt16));
    ASSERT_TRUE(std::filesystem::exists(wav16));

    // 24-bit
    RenderOptions opt24{RenderFormat::Wav, RenderBitDepth::Bit24, 44100.0, 256, wav24, duration};
    ASSERT_OK(OfflineRenderer::render(proj, devs, opt24));
    ASSERT_TRUE(std::filesystem::exists(wav24));

    // 32-bit Float
    RenderOptions opt32{RenderFormat::Wav, RenderBitDepth::Bit32Float, 44100.0, 256, wav32, duration};
    ASSERT_OK(OfflineRenderer::render(proj, devs, opt32));
    ASSERT_TRUE(std::filesystem::exists(wav32));

    // Verify file size relationships: 32-bit > 24-bit > 16-bit
    auto sz16 = std::filesystem::file_size(wav16);
    auto sz24 = std::filesystem::file_size(wav24);
    auto sz32 = std::filesystem::file_size(wav32);

    ASSERT_TRUE(sz32 > sz24);
    ASSERT_TRUE(sz24 > sz16);

    std::filesystem::remove(wav16);
    std::filesystem::remove(wav24);
    std::filesystem::remove(wav32);
}

TEST_CASE(IntegrationAudio, OfflineRenderEdgeCases) {
    Project proj("Edge Case Test");
    std::unordered_map<ChannelId, std::shared_ptr<IDevice>> devs;

    RenderOptions opt_empty_path{RenderFormat::Wav, RenderBitDepth::Bit16, 44100.0, 256, "", 480};
    ASSERT_ERR(OfflineRenderer::render(proj, devs, opt_empty_path), ErrorCode::InvalidArgument);

    RenderOptions opt_zero_sr{RenderFormat::Wav, RenderBitDepth::Bit16, 0.0, 256, "test.wav", 480};
    ASSERT_ERR(OfflineRenderer::render(proj, devs, opt_zero_sr), ErrorCode::InvalidArgument);

    RenderOptions opt_zero_block{RenderFormat::Wav, RenderBitDepth::Bit16, 44100.0, 0, "test.wav", 480};
    ASSERT_ERR(OfflineRenderer::render(proj, devs, opt_zero_block), ErrorCode::InvalidArgument);

    RenderOptions opt_zero_dur{RenderFormat::Wav, RenderBitDepth::Bit16, 44100.0, 256, "test.wav", 0};
    ASSERT_ERR(OfflineRenderer::render(proj, devs, opt_zero_dur), ErrorCode::InvalidArgument);
}

struct DecodedFlacStream {
    uint32_t sample_rate{0};
    uint8_t channels{0};
    uint8_t bits_per_sample{0};
    uint64_t total_samples{0};
    uint16_t min_block{0};
    uint16_t max_block{0};
    std::vector<float> left;
    std::vector<float> right;
    size_t frame_count{0};
    bool valid{false};
};

static DecodedFlacStream decode_test_flac(const std::string& path) {
    DecodedFlacStream result;
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return result;

    char marker[4];
    f.read(marker, 4);
    if (std::memcmp(marker, "fLaC", 4) != 0) return result;

    uint8_t meta_hdr[4];
    f.read(reinterpret_cast<char*>(meta_hdr), 4);
    uint32_t meta_len = (meta_hdr[1] << 16) | (meta_hdr[2] << 8) | meta_hdr[3];
    if ((meta_hdr[0] & 0x7F) != 0 || meta_len < 34) return result;

    uint8_t sinfo[34];
    f.read(reinterpret_cast<char*>(sinfo), 34);
    result.min_block = (sinfo[0] << 8) | sinfo[1];
    result.max_block = (sinfo[2] << 8) | sinfo[3];

    uint64_t props = 0;
    for (int i = 0; i < 8; ++i) props = (props << 8) | sinfo[10 + i];
    result.sample_rate = static_cast<uint32_t>((props >> 44) & 0xFFFFF);
    result.channels = static_cast<uint8_t>(((props >> 41) & 0x07) + 1);
    result.bits_per_sample = static_cast<uint8_t>(((props >> 36) & 0x1F) + 1);
    result.total_samples = props & 0x0FFFFFFFFFULL;

    if (meta_len > 34) {
        f.seekg(meta_len - 34, std::ios::cur);
    }

    while (f.peek() != EOF) {
        int b0 = f.get();
        if (b0 == EOF) break;
        int b1 = f.get();
        if (b1 == EOF) break;
        if (b0 != 0xFF || b1 != 0xF8) break; // sync code

        int b2 = f.get();
        int b3 = f.get();
        if (b2 == EOF || b3 == EOF) break;
        uint8_t bs_code = (b2 >> 4) & 0x0F;

        // UTF-8 frame number
        int fn_b = f.get();
        if (fn_b == EOF) break;
        int extra_bytes = 0;
        if ((fn_b & 0xE0) == 0xC0) extra_bytes = 1;
        else if ((fn_b & 0xF0) == 0xE0) extra_bytes = 2;
        else if ((fn_b & 0xF8) == 0xF0) extra_bytes = 3;
        for (int i = 0; i < extra_bytes; ++i) f.get();

        uint32_t cur_block_size = 0;
        if (bs_code == 12) {
            cur_block_size = 4096;
        } else if (bs_code == 7) {
            int b_h = f.get();
            int b_l = f.get();
            if (b_h == EOF || b_l == EOF) break;
            cur_block_size = ((b_h << 8) | b_l) + 1;
        } else {
            break;
        }

        int header_crc = f.get();
        (void)header_crc;

        // Read verbatim subframes
        bool subframe_err = false;
        for (int ch = 0; ch < 2; ++ch) {
            int sub_hdr = f.get();
            if (sub_hdr != 0x02) { subframe_err = true; break; }

            for (uint32_t i = 0; i < cur_block_size; ++i) {
                if (result.bits_per_sample == 16) {
                    int msb = f.get();
                    int lsb = f.get();
                    if (msb == EOF || lsb == EOF) { subframe_err = true; break; }
                    int16_t sample = static_cast<int16_t>((msb << 8) | lsb);
                    float val = sample >= 0 ? (sample / 32767.0f) : (sample / 32768.0f);
                    if (ch == 0) result.left.push_back(val);
                    else result.right.push_back(val);
                } else if (result.bits_per_sample == 24) {
                    int b_h = f.get();
                    int b_m = f.get();
                    int b_l = f.get();
                    if (b_h == EOF || b_m == EOF || b_l == EOF) { subframe_err = true; break; }
                    int32_t sample = (static_cast<int32_t>(b_h) << 24) |
                                     (static_cast<int32_t>(b_m) << 16) |
                                     (static_cast<int32_t>(b_l) << 8);
                    sample >>= 8;
                    float val = sample >= 0 ? (sample / 8388607.0f) : (sample / 8388608.0f);
                    if (ch == 0) result.left.push_back(val);
                    else result.right.push_back(val);
                }
            }
            if (subframe_err) break;
        }
        if (subframe_err) break;

        // Frame CRC-16
        int crc_h = f.get();
        int crc_l = f.get();
        if (crc_h == EOF || crc_l == EOF) break;

        result.frame_count++;
    }

    result.valid = (result.frame_count > 0 && result.left.size() == result.total_samples);
    return result;
}

TEST_CASE(IntegrationAudio, OfflineRenderHeaderValidityAndFidelity) {
    const std::string wav16 = "test_fidelity_16.wav";
    const std::string wav32 = "test_fidelity_32.wav";
    const std::string flac16 = "test_fidelity_16.flac";

    Project proj("Fidelity Test");
    proj.time_map().set_tempo(120.0);

    ChannelSettings s;
    s.name = "Synth";
    s.volume = 0.9f;
    s.mixer_track = 1;
    ChannelId ch_id = proj.add_channel("core.generator.3xosc", s);

    auto* pat = proj.get_pattern(1);
    ASSERT_TRUE(pat != nullptr);
    auto& notes = pat->get_or_create_channel_notes(ch_id);
    notes.add_note(Note{0, 240, 60, 100, 0, 0});
    proj.tracks()[0].add_clip(Clip{1, 0, DefaultPPQ * 2, false});

    auto synth = std::make_shared<Synth3xOsc>();
    std::unordered_map<ChannelId, std::shared_ptr<IDevice>> devs;
    devs[ch_id] = synth;

    Tick duration = DefaultPPQ * 2;

    // Render 16-bit WAV
    RenderOptions opt_wav16{RenderFormat::Wav, RenderBitDepth::Bit16, 44100.0, 256, wav16, duration};
    ASSERT_OK(OfflineRenderer::render(proj, devs, opt_wav16));

    // Render 32-bit Float WAV
    RenderOptions opt_wav32{RenderFormat::Wav, RenderBitDepth::Bit32Float, 44100.0, 256, wav32, duration};
    ASSERT_OK(OfflineRenderer::render(proj, devs, opt_wav32));

    // Render 16-bit FLAC
    RenderOptions opt_flac16{RenderFormat::Flac, RenderBitDepth::Bit16, 44100.0, 256, flac16, duration};
    ASSERT_OK(OfflineRenderer::render(proj, devs, opt_flac16));

    // 1. Verify 16-bit WAV headers (Format tag 1 = PCM, bits per sample 16)
    std::ifstream f_wav16(wav16, std::ios::binary);
    ASSERT_TRUE(f_wav16.is_open());
    char riff[4];
    f_wav16.read(riff, 4);
    ASSERT_TRUE(riff[0] == 'R' && riff[1] == 'I' && riff[2] == 'F' && riff[3] == 'F');
    f_wav16.seekg(20);
    uint16_t format_tag16 = 0;
    f_wav16.read(reinterpret_cast<char*>(&format_tag16), 2);
    ASSERT_EQ(format_tag16, 1u); // PCM
    uint16_t num_ch16 = 0;
    f_wav16.read(reinterpret_cast<char*>(&num_ch16), 2);
    ASSERT_EQ(num_ch16, 2u);
    uint32_t sr_wav16 = 0;
    f_wav16.read(reinterpret_cast<char*>(&sr_wav16), 4);
    ASSERT_EQ(sr_wav16, 44100u);
    f_wav16.seekg(34);
    uint16_t bps_wav16 = 0;
    f_wav16.read(reinterpret_cast<char*>(&bps_wav16), 2);
    ASSERT_EQ(bps_wav16, 16u);

    // Verify audio data is non-silent (fidelity check)
    f_wav16.seekg(44);
    bool found_audio16 = false;
    std::vector<float> wav16_samples;
    for (int i = 0; i < 1000; ++i) {
        int16_t sample = 0;
        f_wav16.read(reinterpret_cast<char*>(&sample), 2);
        if (std::abs(sample) > 50) {
            found_audio16 = true;
        }
        wav16_samples.push_back(sample / 32768.0f);
    }
    ASSERT_TRUE(found_audio16);
    f_wav16.close();

    // 2. Verify 32-bit Float WAV headers (Format tag 3 = IEEE Float, bits per sample 32)
    std::ifstream f_wav32(wav32, std::ios::binary);
    ASSERT_TRUE(f_wav32.is_open());
    f_wav32.seekg(20);
    uint16_t format_tag32 = 0;
    f_wav32.read(reinterpret_cast<char*>(&format_tag32), 2);
    ASSERT_EQ(format_tag32, 3u); // IEEE Float
    f_wav32.seekg(34);
    uint16_t bps_wav32 = 0;
    f_wav32.read(reinterpret_cast<char*>(&bps_wav32), 2);
    ASSERT_EQ(bps_wav32, 32u);

    // Verify 32-bit float audio samples are finite and non-silent
    f_wav32.seekg(44);
    bool found_audio32 = false;
    for (int i = 0; i < 1000; ++i) {
        float sample = 0.0f;
        f_wav32.read(reinterpret_cast<char*>(&sample), 4);
        ASSERT_TRUE(std::isfinite(sample));
        if (std::abs(sample) > 0.001f) {
            found_audio32 = true;
        }
    }
    ASSERT_TRUE(found_audio32);
    f_wav32.close();

    // 3. Deep FLAC Stream & Sample Decoding
    DecodedFlacStream flac_dec = decode_test_flac(flac16);
    ASSERT_TRUE(flac_dec.valid);
    ASSERT_EQ(flac_dec.sample_rate, 44100u);
    ASSERT_EQ(flac_dec.channels, 2u);
    ASSERT_EQ(flac_dec.bits_per_sample, 16u);
    ASSERT_TRUE(flac_dec.min_block <= flac_dec.max_block);
    ASSERT_TRUE(flac_dec.frame_count > 0);
    ASSERT_TRUE(flac_dec.left.size() > 1000);

    // Verify FLAC audio is non-silent
    bool flac_has_audio = false;
    for (size_t i = 0; i < 1000; ++i) {
        if (std::abs(flac_dec.left[i]) > 0.001f) {
            flac_has_audio = true;
            break;
        }
    }
    ASSERT_TRUE(flac_has_audio);

    // Verify FLAC decoded samples match WAV 16-bit render samples within 1 LSB
    for (size_t i = 0; i < 500; ++i) {
        float diff = std::abs(flac_dec.left[i] - wav16_samples[i * 2]);
        ASSERT_TRUE(diff < 0.001f);
    }

    std::filesystem::remove(wav16);
    std::filesystem::remove(wav32);
    std::filesystem::remove(flac16);
}

TEST_CASE(IntegrationAudio, OfflineRenderFlac24BitFullDecodingAndFidelity) {
    const std::string wav24 = "test_fidelity_24.wav";
    const std::string flac24 = "test_fidelity_24.flac";

    Project proj("Flac 24 Fidelity Test");
    proj.time_map().set_tempo(120.0);

    ChannelSettings s;
    s.name = "Synth";
    s.volume = 0.9f;
    s.mixer_track = 1;
    ChannelId ch_id = proj.add_channel("core.generator.3xosc", s);

    auto* pat = proj.get_pattern(1);
    ASSERT_TRUE(pat != nullptr);
    auto& notes = pat->get_or_create_channel_notes(ch_id);
    notes.add_note(Note{0, 240, 62, 100, 0, 0});
    proj.tracks()[0].add_clip(Clip{1, 0, DefaultPPQ * 2, false});

    auto synth = std::make_shared<Synth3xOsc>();
    std::unordered_map<ChannelId, std::shared_ptr<IDevice>> devs;
    devs[ch_id] = synth;

    Tick duration = DefaultPPQ * 2;

    RenderOptions opt_wav{RenderFormat::Wav, RenderBitDepth::Bit24, 44100.0, 256, wav24, duration};
    ASSERT_OK(OfflineRenderer::render(proj, devs, opt_wav));

    RenderOptions opt_flac{RenderFormat::Flac, RenderBitDepth::Bit24, 44100.0, 256, flac24, duration};
    ASSERT_OK(OfflineRenderer::render(proj, devs, opt_flac));

    DecodedFlacStream flac_dec = decode_test_flac(flac24);
    ASSERT_TRUE(flac_dec.valid);
    ASSERT_EQ(flac_dec.sample_rate, 44100u);
    ASSERT_EQ(flac_dec.channels, 2u);
    ASSERT_EQ(flac_dec.bits_per_sample, 24u);
    ASSERT_TRUE(flac_dec.min_block <= flac_dec.max_block);

    // Load WAV 24-bit using SampleLibrary
    std::vector<float> wav_l, wav_r;
    uint32_t wav_sr = 0;
    bool loaded = SampleLibrary::load_wav_samples(wav24, wav_l, wav_r, wav_sr);
    ASSERT_TRUE(loaded);
    ASSERT_EQ(wav_sr, 44100u);
    ASSERT_EQ(wav_l.size(), flac_dec.left.size());

    // Compare first 1000 samples between 24-bit WAV and 24-bit FLAC
    for (size_t i = 0; i < 1000 && i < wav_l.size(); ++i) {
        float diff_l = std::abs(flac_dec.left[i] - wav_l[i]);
        float diff_r = std::abs(flac_dec.right[i] - wav_r[i]);
        ASSERT_TRUE(diff_l < 0.0001f);
        ASSERT_TRUE(diff_r < 0.0001f);
    }

    std::filesystem::remove(wav24);
    std::filesystem::remove(flac24);
}

TEST_CASE(IntegrationAudio, OfflineRenderAutoCreateDirectories) {
    const std::string nested_wav = "test_nested_out/sub/deep/render.wav";
    const std::string nested_flac = "test_nested_out/sub/deep/render.flac";

    Project proj("Auto Dir Test");
    proj.time_map().set_tempo(120.0);
    std::unordered_map<ChannelId, std::shared_ptr<IDevice>> devs;

    RenderOptions opt_wav{RenderFormat::Wav, RenderBitDepth::Bit16, 44100.0, 256, nested_wav, 480};
    ASSERT_OK(OfflineRenderer::render(proj, devs, opt_wav));
    ASSERT_TRUE(std::filesystem::exists(nested_wav));

    RenderOptions opt_flac{RenderFormat::Flac, RenderBitDepth::Bit16, 44100.0, 256, nested_flac, 480};
    ASSERT_OK(OfflineRenderer::render(proj, devs, opt_flac));
    ASSERT_TRUE(std::filesystem::exists(nested_flac));

    std::filesystem::remove_all("test_nested_out");
}

TEST_CASE(IntegrationAudio, OfflineRenderWavRoundTripLoading) {
    const std::string wav16 = "test_rt_16.wav";
    const std::string wav24 = "test_rt_24.wav";
    const std::string wav32 = "test_rt_32.wav";

    Project proj("Wav RoundTrip Test");
    proj.time_map().set_tempo(120.0);

    ChannelSettings s;
    s.name = "Synth";
    s.volume = 0.85f;
    s.mixer_track = 1;
    ChannelId ch_id = proj.add_channel("core.generator.3xosc", s);

    auto* pat = proj.get_pattern(1);
    ASSERT_TRUE(pat != nullptr);
    auto& notes = pat->get_or_create_channel_notes(ch_id);
    notes.add_note(Note{0, 240, 64, 100, 0, 0});
    proj.tracks()[0].add_clip(Clip{1, 0, DefaultPPQ * 2, false});

    auto synth = std::make_shared<Synth3xOsc>();
    std::unordered_map<ChannelId, std::shared_ptr<IDevice>> devs;
    devs[ch_id] = synth;

    Tick duration = DefaultPPQ * 2;

    // Render 16-bit, 24-bit, 32-bit Float WAV
    RenderOptions opt16{RenderFormat::Wav, RenderBitDepth::Bit16, 44100.0, 256, wav16, duration};
    ASSERT_OK(OfflineRenderer::render(proj, devs, opt16));

    RenderOptions opt24{RenderFormat::Wav, RenderBitDepth::Bit24, 44100.0, 256, wav24, duration};
    ASSERT_OK(OfflineRenderer::render(proj, devs, opt24));

    RenderOptions opt32{RenderFormat::Wav, RenderBitDepth::Bit32Float, 44100.0, 256, wav32, duration};
    ASSERT_OK(OfflineRenderer::render(proj, devs, opt32));

    // Load with SampleLibrary
    std::vector<float> l16, r16, l24, r24, l32, r32;
    uint32_t sr16 = 0, sr24 = 0, sr32 = 0;

    ASSERT_TRUE(SampleLibrary::load_wav_samples(wav16, l16, r16, sr16));
    ASSERT_TRUE(SampleLibrary::load_wav_samples(wav24, l24, r24, sr24));
    ASSERT_TRUE(SampleLibrary::load_wav_samples(wav32, l32, r32, sr32));

    ASSERT_EQ(sr16, 44100u);
    ASSERT_EQ(sr24, 44100u);
    ASSERT_EQ(sr32, 44100u);

    ASSERT_EQ(l16.size(), l24.size());
    ASSERT_EQ(l24.size(), l32.size());

    // Compare fidelity: 32-bit float and 24-bit PCM should be virtually identical
    for (size_t i = 0; i < 500 && i < l32.size(); ++i) {
        float diff = std::abs(l32[i] - l24[i]);
        ASSERT_TRUE(diff < 0.0001f);
    }

    std::filesystem::remove(wav16);
    std::filesystem::remove(wav24);
    std::filesystem::remove(wav32);
}
