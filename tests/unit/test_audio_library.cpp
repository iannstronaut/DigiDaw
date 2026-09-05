#include "../test_framework.hpp"
#include "../../app/usecases/sample_library.hpp"
#include "../../domain/sequencing/pattern.hpp"
#include "../../domain/time/time_map.hpp"
#include "../../adapters/config/config_store.hpp"
#include <filesystem>
#include <fstream>
#include <vector>
#include <cmath>

using namespace digidaw::app;

TEST_CASE(UnitAudioLibrary, AudioExtensionFiltering) {
    ASSERT_TRUE(SampleLibrary::is_supported_audio_extension(".wav"));
    ASSERT_TRUE(SampleLibrary::is_supported_audio_extension(".WAV"));
    ASSERT_TRUE(SampleLibrary::is_supported_audio_extension(".wave"));
    ASSERT_TRUE(SampleLibrary::is_supported_audio_extension(".mp3"));
    ASSERT_TRUE(SampleLibrary::is_supported_audio_extension(".MP3"));
    ASSERT_TRUE(SampleLibrary::is_supported_audio_extension(".ogg"));
    ASSERT_TRUE(SampleLibrary::is_supported_audio_extension(".flac"));
    ASSERT_TRUE(SampleLibrary::is_supported_audio_extension(".aif"));
    ASSERT_TRUE(SampleLibrary::is_supported_audio_extension(".aiff"));

    ASSERT_FALSE(SampleLibrary::is_supported_audio_extension(".txt"));
    ASSERT_FALSE(SampleLibrary::is_supported_audio_extension(".exe"));
    ASSERT_FALSE(SampleLibrary::is_supported_audio_extension(".dll"));
    ASSERT_FALSE(SampleLibrary::is_supported_audio_extension(".png"));
    ASSERT_FALSE(SampleLibrary::is_supported_audio_extension(""));

    ASSERT_TRUE(SampleLibrary::is_supported_audio_file("C:/Samples/kick.wav"));
    ASSERT_TRUE(SampleLibrary::is_supported_audio_file("snare.flac"));
    ASSERT_TRUE(SampleLibrary::is_supported_audio_file("hihat.ogg"));
    ASSERT_FALSE(SampleLibrary::is_supported_audio_file("notes.txt"));
}

TEST_CASE(UnitAudioLibrary, FormattingHelpers) {
    SampleFileInfo info;
    info.size_bytes = 500;
    ASSERT_EQ(info.formatted_size(), "500 B");

    info.size_bytes = 2048;
    ASSERT_EQ(info.formatted_size(), "2.0 KB");

    info.size_bytes = 1048576 * 3;
    ASSERT_EQ(info.formatted_size(), "3.0 MB");

    info.duration_sec = 0.85;
    ASSERT_EQ(info.formatted_duration(), "0.85s");

    info.duration_sec = 65.0;
    ASSERT_EQ(info.formatted_duration(), "1:05");
}

TEST_CASE(UnitAudioLibrary, WaveformPeakExtraction) {
    // 1. Empty buffer
    std::vector<float> empty_buf;
    auto peaks_empty = SampleLibrary::extract_peaks(empty_buf, 64);
    ASSERT_TRUE(peaks_empty.empty());

    // 2. Zero points requested
    std::vector<float> data = {0.1f, -0.2f, 0.5f, -0.8f};
    auto peaks_zero = SampleLibrary::extract_peaks(data, 0);
    ASSERT_TRUE(peaks_zero.empty());

    // 3. Known sine signal
    constexpr size_t N = 1000;
    std::vector<float> sine_wave(N);
    for (size_t i = 0; i < N; ++i) {
        sine_wave[i] = std::sin(2.0 * 3.141592653589793 * double(i) / 100.0);
    }

    constexpr size_t num_pts = 50;
    auto peaks = SampleLibrary::extract_peaks(sine_wave, num_pts);
    ASSERT_EQ(peaks.size(), num_pts);

    // Peak values across full sine cycles should reach approximately +1.0 and -1.0
    float global_max = -999.0f;
    float global_min = 999.0f;
    for (const auto& p : peaks) {
        global_max = std::max(global_max, p.max_val);
        global_min = std::min(global_min, p.min_val);
    }
    ASSERT_TRUE(global_max > 0.95f);
    ASSERT_TRUE(global_min < -0.95f);
}

TEST_CASE(UnitAudioLibrary, DirectoryScanningAndWavParsing) {
    std::string test_dir = "tests/test_audio_library_temp";
    std::filesystem::create_directories(test_dir);

    // Create test WAV file: 16-bit mono, 44100Hz, 1000 frames
    std::string wav_path = test_dir + "/kick_sample.wav";
    {
        std::ofstream f(wav_path, std::ios::binary);
        uint16_t channels = 1;
        uint32_t sample_rate = 44100;
        uint16_t bits = 16;
        uint32_t num_frames = 1000;
        uint32_t data_size = num_frames * channels * (bits / 8);
        uint32_t file_size = 36 + data_size;

        f.write("RIFF", 4);
        f.write(reinterpret_cast<const char*>(&file_size), 4);
        f.write("WAVE", 4);

        // fmt chunk
        f.write("fmt ", 4);
        uint32_t fmt_size = 16;
        uint16_t fmt_tag = 1; // PCM
        uint32_t byte_rate = sample_rate * channels * (bits / 8);
        uint16_t block_align = channels * (bits / 8);
        f.write(reinterpret_cast<const char*>(&fmt_size), 4);
        f.write(reinterpret_cast<const char*>(&fmt_tag), 2);
        f.write(reinterpret_cast<const char*>(&channels), 2);
        f.write(reinterpret_cast<const char*>(&sample_rate), 4);
        f.write(reinterpret_cast<const char*>(&byte_rate), 4);
        f.write(reinterpret_cast<const char*>(&block_align), 2);
        f.write(reinterpret_cast<const char*>(&bits), 2);

        // data chunk
        f.write("data", 4);
        f.write(reinterpret_cast<const char*>(&data_size), 4);
        std::vector<int16_t> pcm_data(num_frames, 16384); // 0.5 amplitude
        f.write(reinterpret_cast<const char*>(pcm_data.data()), data_size);
    }

    // Create dummy mp3 file
    std::string mp3_path = test_dir + "/snare_sample.mp3";
    {
        std::ofstream f(mp3_path, std::ios::binary);
        f << "DUMMY_MP3_DATA_12345";
    }

    // Create non-audio file
    std::string txt_path = test_dir + "/readme.txt";
    {
        std::ofstream f(txt_path);
        f << "Ignore this text file";
    }

    // Perform scan
    SampleLibrary lib;
    lib.scan(test_dir);

    ASSERT_EQ(lib.size(), 2); // kick_sample.wav and snare_sample.mp3, readme.txt ignored
    ASSERT_FALSE(lib.empty());

    const auto* s0 = lib.get_sample(0);
    const auto* s1 = lib.get_sample(1);
    ASSERT_TRUE(s0 != nullptr);
    ASSERT_TRUE(s1 != nullptr);

    // Check wav metadata was parsed
    const SampleFileInfo* wav_info = (s0->filename == "kick_sample.wav") ? s0 : s1;
    ASSERT_EQ(wav_info->filename, "kick_sample.wav");
    ASSERT_EQ(wav_info->channels, 1);
    ASSERT_EQ(wav_info->sample_rate, 44100);
    ASSERT_EQ(wav_info->bit_depth, 16);
    ASSERT_NEAR(wav_info->duration_sec, 1000.0 / 44100.0, 0.001);

    // Clean up
    std::filesystem::remove_all(test_dir);
}

TEST_CASE(UnitAudioLibrary, WavSampleLoading) {
    std::string wav_path = "tests/test_temp_load.wav";
    {
        std::ofstream f(wav_path, std::ios::binary);
        uint16_t channels = 2;
        uint32_t sample_rate = 44100;
        uint16_t bits = 16;
        uint32_t num_frames = 100;
        uint32_t data_size = num_frames * channels * (bits / 8);
        uint32_t file_size = 36 + data_size;

        f.write("RIFF", 4);
        f.write(reinterpret_cast<const char*>(&file_size), 4);
        f.write("WAVE", 4);

        f.write("fmt ", 4);
        uint32_t fmt_size = 16;
        uint16_t fmt_tag = 1;
        uint32_t byte_rate = sample_rate * channels * (bits / 8);
        uint16_t block_align = channels * (bits / 8);
        f.write(reinterpret_cast<const char*>(&fmt_size), 4);
        f.write(reinterpret_cast<const char*>(&fmt_tag), 2);
        f.write(reinterpret_cast<const char*>(&channels), 2);
        f.write(reinterpret_cast<const char*>(&sample_rate), 4);
        f.write(reinterpret_cast<const char*>(&byte_rate), 4);
        f.write(reinterpret_cast<const char*>(&block_align), 2);
        f.write(reinterpret_cast<const char*>(&bits), 2);

        f.write("data", 4);
        f.write(reinterpret_cast<const char*>(&data_size), 4);
        std::vector<int16_t> pcm(num_frames * 2);
        for (size_t i = 0; i < num_frames; ++i) {
            pcm[i * 2] = 16384;     // +0.5
            pcm[i * 2 + 1] = -16384; // -0.5
        }
        f.write(reinterpret_cast<const char*>(pcm.data()), data_size);
    }

    std::vector<float> l, r;
    uint32_t sr = 0;
    bool ok = SampleLibrary::load_wav_samples(wav_path, l, r, sr);
    ASSERT_TRUE(ok);
    ASSERT_EQ(l.size(), 100);
    ASSERT_EQ(r.size(), 100);
    ASSERT_EQ(sr, 44100);
    ASSERT_NEAR(l[0], 0.5f, 0.01f);
    ASSERT_NEAR(r[0], -0.5f, 0.01f);

    std::filesystem::remove(wav_path);
}

TEST_CASE(UnitAudioLibrary, EmptyAndInvalidPathsHandledGracefully) {
    SampleLibrary lib;
    lib.scan("");
    ASSERT_TRUE(lib.empty());
    ASSERT_EQ(lib.size(), 0);
    ASSERT_EQ(lib.current_directory(), "");

    lib.scan("C:/This/Path/Definitely/Does/Not/Exist_DigiDawTest_12345");
    ASSERT_TRUE(lib.empty());
    ASSERT_EQ(lib.size(), 0);
    ASSERT_EQ(lib.get_sample(0), nullptr);
}

TEST_CASE(UnitAudioLibrary, AliasesAndIntegration) {
    SampleFileInfo info;
    info.filename = "snare.wav";
    info.name = info.filename;
    info.ext = ".wav";
    info.extension = info.ext;
    info.channels = 2;
    info.duration_sec = 1.25;
    info.size_bytes = 10240;

    ASSERT_EQ(info.formatted_channels(), "Stereo");
    ASSERT_EQ(info.name, "snare.wav");
    ASSERT_EQ(info.extension, ".wav");

    info.channels = 1;
    ASSERT_EQ(info.formatted_channels(), "Mono");

    // Pattern::add_note and TimeMap::bpm verification
    digidaw::domain::Pattern pat(1, "TestPattern");
    pat.add_note(1, digidaw::domain::Note{0, 96, 60, 100, 0, 0});
    ASSERT_EQ(pat.channel_notes().size(), 1);
    ASSERT_EQ(pat.channel_notes().at(1).notes().size(), 1);
    ASSERT_EQ(pat.channel_notes().at(1).notes()[0].pitch, 60);

    digidaw::domain::TimeMap tm(130.0, 96);
    ASSERT_NEAR(tm.bpm(), 130.0, 0.001);
}

TEST_CASE(UnitAudioLibrary, RiffWavOddChunkPaddingHandling) {
    std::string test_dir = "tests/test_riff_odd_padding";
    std::filesystem::create_directories(test_dir);
    std::string odd_wav = test_dir + "/odd_chunks.wav";

    {
        std::ofstream f(odd_wav, std::ios::binary);
        // RIFF header
        f.write("RIFF", 4);
        uint32_t total_size = 36 + 15 + 1 + 200 * 2 * 2;
        f.write(reinterpret_cast<const char*>(&total_size), 4);
        f.write("WAVE", 4);

        // Unknown metadata chunk with ODD size (15 bytes) + 1 pad byte
        f.write("JUNK", 4);
        uint32_t odd_sz = 15;
        f.write(reinterpret_cast<const char*>(&odd_sz), 4);
        f.write("123456789012345", 15);
        char pad = 0;
        f.write(&pad, 1); // 2-byte alignment pad

        // fmt chunk (16 bytes)
        f.write("fmt ", 4);
        uint32_t fmt_sz = 16;
        uint16_t fmt_tag = 1;
        uint16_t channels = 2;
        uint32_t sr = 48000;
        uint16_t bits = 16;
        uint32_t byte_rate = sr * channels * (bits / 8);
        uint16_t block_align = channels * (bits / 8);
        f.write(reinterpret_cast<const char*>(&fmt_sz), 4);
        f.write(reinterpret_cast<const char*>(&fmt_tag), 2);
        f.write(reinterpret_cast<const char*>(&channels), 2);
        f.write(reinterpret_cast<const char*>(&sr), 4);
        f.write(reinterpret_cast<const char*>(&byte_rate), 4);
        f.write(reinterpret_cast<const char*>(&block_align), 2);
        f.write(reinterpret_cast<const char*>(&bits), 2);

        // data chunk (200 frames)
        uint32_t num_frames = 200;
        uint32_t data_bytes = num_frames * channels * (bits / 8);
        f.write("data", 4);
        f.write(reinterpret_cast<const char*>(&data_bytes), 4);
        std::vector<int16_t> pcm(num_frames * 2, 8000);
        f.write(reinterpret_cast<const char*>(pcm.data()), data_bytes);
    }

    SampleFileInfo info;
    bool parsed = SampleLibrary::parse_wav_metadata(odd_wav, info);
    ASSERT_TRUE(parsed);
    ASSERT_EQ(info.sample_rate, 48000);
    ASSERT_EQ(info.channels, 2);
    ASSERT_EQ(info.bit_depth, 16);
    ASSERT_NEAR(info.duration_sec, 200.0 / 48000.0, 0.001);

    std::vector<float> l, r;
    uint32_t out_sr = 0;
    bool loaded = SampleLibrary::load_wav_samples(odd_wav, l, r, out_sr);
    ASSERT_TRUE(loaded);
    ASSERT_EQ(l.size(), 200);
    ASSERT_EQ(r.size(), 200);
    ASSERT_EQ(out_sr, 48000);
    ASSERT_NEAR(l[0], 8000.0f / 32768.0f, 0.01f);

    std::filesystem::remove_all(test_dir);
}

TEST_CASE(UnitAudioLibrary, NonWavPreviewWaveformSynthesis) {
    SampleFileInfo info;
    info.filename = "bassline_drop.mp3";
    info.name = info.filename;
    info.duration_sec = 2.5;
    info.size_bytes = 20000;

    std::vector<float> l, r;
    uint32_t sr = 0;
    SampleLibrary::generate_preview_waveform(info, l, r, sr);

    ASSERT_EQ(sr, 44100);
    ASSERT_EQ(l.size(), static_cast<size_t>(2.5 * 44100));
    ASSERT_EQ(r.size(), l.size());

    // Non-silent audio
    float max_sample = 0.0f;
    for (float s : l) {
        max_sample = std::max(max_sample, std::abs(s));
    }
    ASSERT_TRUE(max_sample > 0.1f);
}

TEST_CASE(UnitAudioLibrary, WaveformPeakPolarityAndAsymmetry) {
    // Waveform with positive DC offset: all values between +0.2 and +0.8
    std::vector<float> asymmetric_data(100);
    for (size_t i = 0; i < 100; ++i) {
        asymmetric_data[i] = 0.5f + 0.3f * std::sin(float(i) * 0.2f);
    }

    auto peaks = SampleLibrary::extract_peaks(asymmetric_data, 10);
    ASSERT_EQ(peaks.size(), 10);
    for (const auto& p : peaks) {
        ASSERT_TRUE(p.max_val >= p.min_val);
        // None of the values should be negative
        ASSERT_TRUE(p.min_val >= 0.19f);
        ASSERT_TRUE(p.max_val <= 0.81f);
    }
}

TEST_CASE(UnitAudioLibrary, SampleLibraryConfigPersistence) {
    std::string test_ini = "tests/test_sample_lib_config.ini";
    {
        digidaw::adapters::config::FileConfigStore store(test_ini);
        ASSERT_EQ(store.get_string("SampleLibrary", "RootDirectory", ""), "");

        store.set_string("SampleLibrary", "RootDirectory", "D:/MySamples/Drums");
        store.flush();
    }

    {
        digidaw::adapters::config::FileConfigStore reload(test_ini);
        ASSERT_EQ(reload.get_string("SampleLibrary", "RootDirectory"), "D:/MySamples/Drums");
    }

    std::filesystem::remove(test_ini);
}
