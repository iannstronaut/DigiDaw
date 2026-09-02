#pragma once

#include "../../domain/buffer/audio_buffer.hpp"
#include "../../domain/common/result.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <algorithm>

namespace digidaw::adapters::audio {

enum class WaveBitDepth : uint8_t {
    PCM16 = 16,
    PCM24 = 24,
    Float32 = 32
};

class WaveFileWriter {
public:
    static domain::Result<void> write_wav(
        const std::string& filepath,
        const std::vector<float>& left_channel,
        const std::vector<float>& right_channel,
        uint32_t sample_rate = 44100,
        WaveBitDepth bit_depth = WaveBitDepth::PCM16) {

        if (left_channel.size() != right_channel.size()) {
            return domain::Result<void>(domain::ErrorCode::InvalidArgument);
        }

        std::ofstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            return domain::Result<void>(domain::ErrorCode::FileLocked);
        }

        const uint16_t num_channels = 2;
        const uint32_t num_frames = static_cast<uint32_t>(left_channel.size());
        const uint16_t bits_per_sample = static_cast<uint16_t>(bit_depth);
        const uint16_t bytes_per_sample = bits_per_sample / 8;
        const uint16_t block_align = num_channels * bytes_per_sample;
        const uint32_t byte_rate = sample_rate * block_align;
        const uint32_t data_size = num_frames * block_align;
        const uint16_t format_tag = (bit_depth == WaveBitDepth::Float32) ? 3 : 1; // 1 = PCM, 3 = IEEE Float

        // Write RIFF header
        file.write("RIFF", 4);
        uint32_t chunk_size = 36 + data_size;
        write_u32(file, chunk_size);
        file.write("WAVE", 4);

        // Write fmt chunk
        file.write("fmt ", 4);
        write_u32(file, 16); // Subchunk1Size for PCM
        write_u16(file, format_tag);
        write_u16(file, num_channels);
        write_u32(file, sample_rate);
        write_u32(file, byte_rate);
        write_u16(file, block_align);
        write_u16(file, bits_per_sample);

        // Write data chunk
        file.write("data", 4);
        write_u32(file, data_size);

        // Write interleaved sample data
        for (size_t i = 0; i < num_frames; ++i) {
            float l = std::clamp(left_channel[i], -1.0f, 1.0f);
            float r = std::clamp(right_channel[i], -1.0f, 1.0f);

            if (bit_depth == WaveBitDepth::PCM16) {
                int16_t l16 = static_cast<int16_t>(l * 32767.0f);
                int16_t r16 = static_cast<int16_t>(r * 32767.0f);
                file.write(reinterpret_cast<const char*>(&l16), 2);
                file.write(reinterpret_cast<const char*>(&r16), 2);
            } else if (bit_depth == WaveBitDepth::PCM24) {
                int32_t l24 = static_cast<int32_t>(l * 8388607.0f);
                int32_t r24 = static_cast<int32_t>(r * 8388607.0f);
                file.write(reinterpret_cast<const char*>(&l24), 3);
                file.write(reinterpret_cast<const char*>(&r24), 3);
            } else if (bit_depth == WaveBitDepth::Float32) {
                file.write(reinterpret_cast<const char*>(&l), 4);
                file.write(reinterpret_cast<const char*>(&r), 4);
            }
        }

        file.flush();
        if (file.fail()) {
            return domain::Result<void>(domain::ErrorCode::AutosaveFailed);
        }

        return domain::Result<void>::ok();
    }

private:
    static void write_u16(std::ofstream& f, uint16_t v) {
        f.write(reinterpret_cast<const char*>(&v), 2);
    }

    static void write_u32(std::ofstream& f, uint32_t v) {
        f.write(reinterpret_cast<const char*>(&v), 4);
    }
};

} // namespace digidaw::adapters::audio
