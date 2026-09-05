#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace digidaw::app {

struct SampleFileInfo {
    std::string path;
    std::string filename;
    std::string name;
    std::string ext;
    std::string extension;
    uintmax_t size_bytes{0};
    double duration_sec{0.0};
    uint16_t channels{2};
    uint32_t sample_rate{44100};
    uint16_t bit_depth{16};

    [[nodiscard]] std::string formatted_size() const {
        if (size_bytes == 0) return "0 B";
        if (size_bytes < 1024) return std::to_string(size_bytes) + " B";
        double kb = static_cast<double>(size_bytes) / 1024.0;
        if (kb < 1024.0) {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1) << kb << " KB";
            return ss.str();
        }
        double mb = kb / 1024.0;
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << mb << " MB";
        return ss.str();
    }

    [[nodiscard]] std::string formatted_duration() const {
        if (duration_sec <= 0.0) return "0.00s";
        if (duration_sec < 60.0) {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(2) << duration_sec << "s";
            return ss.str();
        }
        int total_s = static_cast<int>(duration_sec);
        int mins = total_s / 60;
        int secs = total_s % 60;
        std::ostringstream ss;
        ss << mins << ":" << (secs < 10 ? "0" : "") << secs;
        return ss.str();
    }

    [[nodiscard]] std::string formatted_channels() const {
        return (channels == 1) ? "Mono" : "Stereo";
    }

    [[nodiscard]] std::string formatted_info() const {
        std::ostringstream ss;
        ss << formatted_duration() << " • "
           << formatted_channels() << " • "
           << bit_depth << "-bit • "
           << formatted_size();
        return ss.str();
    }
};

struct WaveformPeak {
    float min_val{0.0f};
    float max_val{0.0f};
};

class SampleLibrary {
public:
    SampleLibrary() = default;

    static bool is_supported_audio_extension(const std::string& ext) {
        std::string lower_ext = ext;
        std::transform(lower_ext.begin(), lower_ext.end(), lower_ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return (lower_ext == ".wav" || lower_ext == ".wave" ||
                lower_ext == ".mp3" ||
                lower_ext == ".ogg" ||
                lower_ext == ".flac" ||
                lower_ext == ".aif" || lower_ext == ".aiff");
    }

    static bool is_supported_audio_file(const std::string& filename_or_path) {
        std::filesystem::path p(filename_or_path);
        return is_supported_audio_extension(p.extension().string());
    }

    static std::vector<WaveformPeak> extract_peaks(const std::vector<float>& samples, size_t num_points) {
        if (samples.empty() || num_points == 0) return {};

        std::vector<WaveformPeak> peaks(num_points);
        const size_t total_samples = samples.size();

        for (size_t p = 0; p < num_points; ++p) {
            size_t start_idx = (p * total_samples) / num_points;
            size_t end_idx = ((p + 1) * total_samples) / num_points;
            end_idx = std::min(end_idx, total_samples);
            if (end_idx <= start_idx) end_idx = start_idx + 1;

            float min_v = samples[start_idx];
            float max_v = samples[start_idx];
            for (size_t i = start_idx; i < end_idx && i < total_samples; ++i) {
                min_v = std::min(min_v, samples[i]);
                max_v = std::max(max_v, samples[i]);
            }
            peaks[p] = WaveformPeak{min_v, max_v};
        }
        return peaks;
    }

    static bool parse_wav_metadata(const std::string& filepath, SampleFileInfo& info) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) return false;

        char riff[4];
        file.read(riff, 4);
        if (std::strncmp(riff, "RIFF", 4) != 0) return false;

        uint32_t file_size = 0;
        file.read(reinterpret_cast<char*>(&file_size), 4);

        char wave[4];
        file.read(wave, 4);
        if (std::strncmp(wave, "WAVE", 4) != 0) return false;

        uint16_t num_channels = 0;
        uint32_t sample_rate = 0;
        uint16_t bits_per_sample = 0;
        uint32_t data_chunk_size = 0;

        while (file) {
            char chunk_id[4];
            file.read(chunk_id, 4);
            if (!file) break;

            uint32_t chunk_size = 0;
            file.read(reinterpret_cast<char*>(&chunk_size), 4);
            if (!file) break;

            if (std::strncmp(chunk_id, "fmt ", 4) == 0) {
                uint16_t format_tag = 0;
                file.read(reinterpret_cast<char*>(&format_tag), 2);
                file.read(reinterpret_cast<char*>(&num_channels), 2);
                file.read(reinterpret_cast<char*>(&sample_rate), 4);
                uint32_t byte_rate = 0;
                file.read(reinterpret_cast<char*>(&byte_rate), 4);
                uint16_t block_align = 0;
                file.read(reinterpret_cast<char*>(&block_align), 2);
                file.read(reinterpret_cast<char*>(&bits_per_sample), 2);

                if (chunk_size > 16) {
                    uint32_t rem = (chunk_size - 16) + (chunk_size & 1);
                    file.seekg(rem, std::ios::cur);
                } else if (chunk_size & 1) {
                    file.seekg(1, std::ios::cur);
                }
            } else if (std::strncmp(chunk_id, "data", 4) == 0) {
                data_chunk_size = chunk_size;
                break;
            } else {
                uint32_t skip_sz = chunk_size + (chunk_size & 1);
                file.seekg(skip_sz, std::ios::cur);
            }
        }

        if (num_channels > 0 && sample_rate > 0 && bits_per_sample > 0) {
            info.channels = num_channels;
            info.sample_rate = sample_rate;
            info.bit_depth = bits_per_sample;
            size_t bytes_per_sample = bits_per_sample / 8;
            if (bytes_per_sample > 0) {
                size_t num_frames = data_chunk_size / (num_channels * bytes_per_sample);
                info.duration_sec = static_cast<double>(num_frames) / static_cast<double>(sample_rate);
            }
            return true;
        }
        return false;
    }

    static bool load_wav_samples(const std::string& filepath, std::vector<float>& out_l, std::vector<float>& out_r, uint32_t& out_sr) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) return false;

        char riff[4];
        file.read(riff, 4);
        if (std::strncmp(riff, "RIFF", 4) != 0) return false;

        uint32_t file_size = 0;
        file.read(reinterpret_cast<char*>(&file_size), 4);

        char wave[4];
        file.read(wave, 4);
        if (std::strncmp(wave, "WAVE", 4) != 0) return false;

        uint16_t num_channels = 0;
        uint32_t sample_rate = 0;
        uint16_t bits_per_sample = 0;
        uint16_t format_tag = 1;

        while (file) {
            char chunk_id[4];
            file.read(chunk_id, 4);
            if (!file) break;

            uint32_t chunk_size = 0;
            file.read(reinterpret_cast<char*>(&chunk_size), 4);
            if (!file) break;

            if (std::strncmp(chunk_id, "fmt ", 4) == 0) {
                file.read(reinterpret_cast<char*>(&format_tag), 2);
                file.read(reinterpret_cast<char*>(&num_channels), 2);
                file.read(reinterpret_cast<char*>(&sample_rate), 4);
                uint32_t byte_rate = 0;
                file.read(reinterpret_cast<char*>(&byte_rate), 4);
                uint16_t block_align = 0;
                file.read(reinterpret_cast<char*>(&block_align), 2);
                file.read(reinterpret_cast<char*>(&bits_per_sample), 2);

                if (chunk_size > 16) {
                    uint32_t rem = (chunk_size - 16) + (chunk_size & 1);
                    file.seekg(rem, std::ios::cur);
                } else if (chunk_size & 1) {
                    file.seekg(1, std::ios::cur);
                }
            } else if (std::strncmp(chunk_id, "data", 4) == 0) {
                if (num_channels == 0 || bits_per_sample == 0) return false;
                size_t bytes_per_sample = bits_per_sample / 8;
                if (bytes_per_sample == 0) return false;
                size_t num_frames = chunk_size / (num_channels * bytes_per_sample);

                out_l.resize(num_frames);
                out_r.resize(num_frames);

                std::vector<uint8_t> raw_data(chunk_size);
                file.read(reinterpret_cast<char*>(raw_data.data()), chunk_size);

                const uint8_t* ptr = raw_data.data();
                for (size_t i = 0; i < num_frames; ++i) {
                    float s_l = 0.0f;
                    float s_r = 0.0f;

                    if (format_tag == 1 && bits_per_sample == 8) {
                        uint8_t val_l = *ptr++;
                        s_l = (static_cast<float>(val_l) - 128.0f) / 128.0f;
                        if (num_channels > 1) {
                            uint8_t val_r = *ptr++;
                            s_r = (static_cast<float>(val_r) - 128.0f) / 128.0f;
                        } else {
                            s_r = s_l;
                        }
                    } else if (format_tag == 1 && bits_per_sample == 16) {
                        int16_t val_l = *reinterpret_cast<const int16_t*>(ptr);
                        ptr += 2;
                        s_l = val_l / 32768.0f;
                        if (num_channels > 1) {
                            int16_t val_r = *reinterpret_cast<const int16_t*>(ptr);
                            ptr += 2;
                            s_r = val_r / 32768.0f;
                        } else {
                            s_r = s_l;
                        }
                    } else if (format_tag == 1 && bits_per_sample == 24) {
                        int32_t val_l = (static_cast<int32_t>(ptr[0]) << 8) |
                                        (static_cast<int32_t>(ptr[1]) << 16) |
                                        (static_cast<int32_t>(ptr[2]) << 24);
                        ptr += 3;
                        s_l = (val_l >> 8) / 8388608.0f;
                        if (num_channels > 1) {
                            int32_t val_r = (static_cast<int32_t>(ptr[0]) << 8) |
                                            (static_cast<int32_t>(ptr[1]) << 16) |
                                            (static_cast<int32_t>(ptr[2]) << 24);
                            ptr += 3;
                            s_r = (val_r >> 8) / 8388608.0f;
                        } else {
                            s_r = s_l;
                        }
                    } else if (format_tag == 3 && bits_per_sample == 32) {
                        s_l = *reinterpret_cast<const float*>(ptr);
                        ptr += 4;
                        if (num_channels > 1) {
                            s_r = *reinterpret_cast<const float*>(ptr);
                            ptr += 4;
                        } else {
                            s_r = s_l;
                        }
                    } else {
                        ptr += num_channels * bytes_per_sample;
                    }

                    out_l[i] = std::clamp(s_l, -1.0f, 1.0f);
                    out_r[i] = std::clamp(s_r, -1.0f, 1.0f);
                }
                out_sr = sample_rate;
                return true;
            } else {
                uint32_t skip_sz = chunk_size + (chunk_size & 1);
                file.seekg(skip_sz, std::ios::cur);
            }
        }
        return false;
    }

    void scan(const std::string& directory_path) {
        samples_.clear();
        root_path_ = directory_path;

        if (directory_path.empty()) return;

        std::error_code ec;
        if (!std::filesystem::exists(directory_path, ec) || !std::filesystem::is_directory(directory_path, ec)) {
            return;
        }

        try {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(
                     directory_path, std::filesystem::directory_options::skip_permission_denied, ec)) {
                if (ec) break;
                if (!entry.is_regular_file(ec)) continue;

                std::string path_str = entry.path().string();
                if (!is_supported_audio_file(path_str)) continue;

                SampleFileInfo info;
                info.path = path_str;
                info.filename = entry.path().filename().string();
                info.name = info.filename;
                info.ext = entry.path().extension().string();
                std::transform(info.ext.begin(), info.ext.end(), info.ext.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                info.extension = info.ext;
                info.size_bytes = entry.file_size(ec);

                // Default metadata
                info.channels = 2;
                info.sample_rate = 44100;
                info.bit_depth = 16;
                info.duration_sec = 0.0;

                if (info.ext == ".wav" || info.ext == ".wave") {
                    parse_wav_metadata(path_str, info);
                } else {
                    // Estimated duration for compressed / other formats based on typical bitrates
                    if (info.size_bytes > 0) {
                        info.duration_sec = static_cast<double>(info.size_bytes) / (20000.0); // ~160kbps rough estimate
                    }
                }

                samples_.push_back(std::move(info));
            }

            std::sort(samples_.begin(), samples_.end(), [](const SampleFileInfo& a, const SampleFileInfo& b) {
                return a.filename < b.filename;
            });
        } catch (...) {
            // Guarantee exception safety
        }
    }

    static bool decode_wav_samples(const std::string& filepath, std::vector<float>& out_l, std::vector<float>& out_r, uint32_t& out_sr, uint16_t& channels) {
        bool ok = load_wav_samples(filepath, out_l, out_r, out_sr);
        if (ok) {
            channels = 2;
            return true;
        }
        return false;
    }

    static void generate_preview_waveform(const SampleFileInfo& info, std::vector<float>& out_l, std::vector<float>& out_r, uint32_t& out_sr) {
        out_sr = 44100;
        double dur = (info.duration_sec > 0.05) ? std::min(info.duration_sec, 4.0) : 1.5;
        size_t total_frames = static_cast<size_t>(dur * static_cast<double>(out_sr));
        if (total_frames == 0) total_frames = 44100;
        out_l.resize(total_frames);
        out_r.resize(total_frames);

        uint32_t hash = 5381;
        for (char c : info.filename) hash = ((hash << 5) + hash) + static_cast<unsigned char>(c);
        float base_freq = 110.0f + static_cast<float>(hash % 440);

        for (size_t i = 0; i < total_frames; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(out_sr);
            float env = std::exp(-t * (3.0f / static_cast<float>(dur)));
            float tone = std::sin(2.0f * 3.14159265f * base_freq * t);
            float harmonic = 0.35f * std::sin(4.0f * 3.14159265f * base_freq * t);
            float s = (tone + harmonic) * env * 0.5f;
            out_l[i] = s;
            out_r[i] = s;
        }
    }

    [[nodiscard]] const std::string& root_path() const noexcept { return root_path_; }
    [[nodiscard]] const std::string& current_directory() const noexcept { return root_path_; }
    [[nodiscard]] const std::vector<SampleFileInfo>& samples() const noexcept { return samples_; }
    [[nodiscard]] size_t size() const noexcept { return samples_.size(); }
    [[nodiscard]] bool empty() const noexcept { return samples_.empty(); }

    [[nodiscard]] const SampleFileInfo* get_sample(size_t index) const noexcept {
        if (index < samples_.size()) return &samples_[index];
        return nullptr;
    }

private:
    std::string root_path_;
    std::vector<SampleFileInfo> samples_;
};

} // namespace digidaw::app
