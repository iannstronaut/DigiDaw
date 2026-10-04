#pragma once

#include "../../domain/buffer/audio_buffer.hpp"
#include "../../domain/common/result.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace digidaw::adapters::audio {

enum class FlacBitDepth : uint8_t {
    PCM16 = 16,
    PCM24 = 24
};

class FlacFileWriter {
public:
    static domain::Result<void> write_flac(
        const std::string& filepath,
        const std::vector<float>& left_channel,
        const std::vector<float>& right_channel,
        uint32_t sample_rate = 44100,
        FlacBitDepth bit_depth = FlacBitDepth::PCM16) {

        if (left_channel.size() != right_channel.size()) {
            return domain::Result<void>(domain::ErrorCode::InvalidArgument);
        }

        // Ensure parent directory exists if specified
        std::filesystem::path out_p(filepath);
        if (out_p.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(out_p.parent_path(), ec);
        }

        std::ofstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            return domain::Result<void>(domain::ErrorCode::FileLocked);
        }

        const uint32_t total_frames = static_cast<uint32_t>(left_channel.size());
        const uint8_t bits = static_cast<uint8_t>(bit_depth);
        const uint16_t num_channels = 2;
        const uint32_t block_size = 4096;

        // 1. "fLaC" 4-byte marker
        file.write("fLaC", 4);

        // 2. STREAMINFO Metadata Block (34 bytes body)
        // Header: 1 bit last_block (1), 7 bits block_type (0 = STREAMINFO), 24 bits length (34 = 0x22)
        uint8_t meta_header[4] = {
            0x80, // Last metadata block, type 0
            0x00,
            0x00,
            0x22  // 34 bytes
        };
        file.write(reinterpret_cast<const char*>(meta_header), 4);

        // Calculate accurate min_block and max_block across all frames
        uint32_t rem = total_frames % block_size;
        uint32_t actual_min_block = (total_frames < block_size) ? total_frames : ((rem > 0) ? std::min(block_size, rem) : block_size);
        actual_min_block = std::max<uint32_t>(16, actual_min_block);
        uint32_t actual_max_block = std::min<uint32_t>(block_size, std::max<uint32_t>(16, total_frames));
        uint16_t min_block = static_cast<uint16_t>(actual_min_block);
        uint16_t max_block = static_cast<uint16_t>(actual_max_block);

        uint8_t streaminfo[34] = {0};
        // min block size (16 bits)
        streaminfo[0] = static_cast<uint8_t>((min_block >> 8) & 0xFF);
        streaminfo[1] = static_cast<uint8_t>(min_block & 0xFF);
        // max block size (16 bits)
        streaminfo[2] = static_cast<uint8_t>((max_block >> 8) & 0xFF);
        streaminfo[3] = static_cast<uint8_t>(max_block & 0xFF);
        // min frame size (24 bits) = 0
        // max frame size (24 bits) = 0
        // sample rate (20 bits), channels - 1 (3 bits), bits per sample - 1 (5 bits), total samples (36 bits)
        uint64_t important_props = 0;
        important_props |= (static_cast<uint64_t>(sample_rate & 0x0FFFFF)) << 44;
        important_props |= (static_cast<uint64_t>((num_channels - 1) & 0x07)) << 41;
        important_props |= (static_cast<uint64_t>((bits - 1) & 0x1F)) << 36;
        important_props |= (static_cast<uint64_t>(total_frames) & 0x0FFFFFFFFFULL);

        for (int i = 0; i < 8; ++i) {
            streaminfo[10 + i] = static_cast<uint8_t>((important_props >> ((7 - i) * 8)) & 0xFF);
        }
        // MD5 signature (16 bytes): streaminfo[18..33] remain 0 (valid according to FLAC spec)

        file.write(reinterpret_cast<const char*>(streaminfo), 34);

        // 3. Audio Frames
        uint32_t frame_index = 0;
        uint32_t frames_encoded = 0;

        std::vector<uint8_t> frame_buf;
        frame_buf.reserve(block_size * 6 + 32);

        while (frames_encoded < total_frames) {
            const uint32_t cur_block = std::min<uint32_t>(block_size, total_frames - frames_encoded);
            frame_buf.clear();

            // Frame Header:
            // Sync code: 14 bits 0x3FFE (11111111 111110)
            // Reserved: 1 bit 0
            // Blocking strategy: 1 bit 0 (fixed-blocksize stream)
            frame_buf.push_back(0xFF);
            frame_buf.push_back(0xF8);

            // Block size code (4 bits) & Sample rate code (4 bits)
            uint8_t block_size_code = 0;
            bool custom_block_size = false;
            if (cur_block == 4096) {
                block_size_code = 12; // 1100 = 4096
            } else {
                block_size_code = 7;  // 0111 = get 16 bit (blocksize-1) from end of header
                custom_block_size = true;
            }
            uint8_t sample_rate_code = 0; // 0000 = get from STREAMINFO
            frame_buf.push_back(static_cast<uint8_t>((block_size_code << 4) | (sample_rate_code & 0x0F)));

            // Channel assignment (4 bits) & Bits per sample (3 bits) & Reserved (1 bit: 0)
            uint8_t channel_assignment = 1; // 0001 = 2 channels: left, right
            uint8_t bps_code = 0;           // 000 = get from STREAMINFO
            frame_buf.push_back(static_cast<uint8_t>((channel_assignment << 4) | (bps_code << 1)));

            // Coded frame number (UTF-8 variable length)
            std::vector<uint8_t> utf8_num = encode_utf8(frame_index);
            for (uint8_t b : utf8_num) {
                frame_buf.push_back(b);
            }

            // Custom block size if code == 7
            if (custom_block_size) {
                uint16_t bs_val = static_cast<uint16_t>(cur_block - 1);
                frame_buf.push_back(static_cast<uint8_t>((bs_val >> 8) & 0xFF));
                frame_buf.push_back(static_cast<uint8_t>(bs_val & 0xFF));
            }

            // Compute Frame Header CRC-8 over all header bytes
            uint8_t crc8 = 0;
            for (uint8_t b : frame_buf) {
                crc8 = flac_crc8_byte(crc8, b);
            }
            frame_buf.push_back(crc8);

            // Subframe 0: Left Channel (Verbatim subframe: 1 byte header 0x02, followed by raw samples)
            frame_buf.push_back(0x02);
            for (uint32_t i = 0; i < cur_block; ++i) {
                float s = std::clamp(left_channel[frames_encoded + i], -1.0f, 1.0f);
                if (bit_depth == FlacBitDepth::PCM16) {
                    int16_t val = (s < 0.0f)
                        ? static_cast<int16_t>(std::clamp(s * 32768.0f, -32768.0f, 32767.0f))
                        : static_cast<int16_t>(std::clamp(s * 32767.0f, -32768.0f, 32767.0f));
                    frame_buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
                    frame_buf.push_back(static_cast<uint8_t>(val & 0xFF));
                } else {
                    int32_t val = (s < 0.0f)
                        ? static_cast<int32_t>(std::clamp(s * 8388608.0f, -8388608.0f, 8388607.0f))
                        : static_cast<int32_t>(std::clamp(s * 8388607.0f, -8388608.0f, 8388607.0f));
                    frame_buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
                    frame_buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
                    frame_buf.push_back(static_cast<uint8_t>(val & 0xFF));
                }
            }

            // Subframe 1: Right Channel (Verbatim subframe: 1 byte header 0x02, followed by raw samples)
            frame_buf.push_back(0x02);
            for (uint32_t i = 0; i < cur_block; ++i) {
                float s = std::clamp(right_channel[frames_encoded + i], -1.0f, 1.0f);
                if (bit_depth == FlacBitDepth::PCM16) {
                    int16_t val = (s < 0.0f)
                        ? static_cast<int16_t>(std::clamp(s * 32768.0f, -32768.0f, 32767.0f))
                        : static_cast<int16_t>(std::clamp(s * 32767.0f, -32768.0f, 32767.0f));
                    frame_buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
                    frame_buf.push_back(static_cast<uint8_t>(val & 0xFF));
                } else {
                    int32_t val = (s < 0.0f)
                        ? static_cast<int32_t>(std::clamp(s * 8388608.0f, -8388608.0f, 8388607.0f))
                        : static_cast<int32_t>(std::clamp(s * 8388607.0f, -8388608.0f, 8388607.0f));
                    frame_buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
                    frame_buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
                    frame_buf.push_back(static_cast<uint8_t>(val & 0xFF));
                }
            }

            // Frame Footer: CRC-16 of entire frame
            uint16_t crc16 = 0x0000;
            for (uint8_t b : frame_buf) {
                crc16 = flac_crc16_byte(crc16, b);
            }
            frame_buf.push_back(static_cast<uint8_t>((crc16 >> 8) & 0xFF));
            frame_buf.push_back(static_cast<uint8_t>(crc16 & 0xFF));

            file.write(reinterpret_cast<const char*>(frame_buf.data()), frame_buf.size());

            frames_encoded += cur_block;
            frame_index++;
        }

        file.flush();
        if (file.fail()) {
            return domain::Result<void>(domain::ErrorCode::AutosaveFailed);
        }

        return domain::Result<void>::ok();
    }

private:
    static std::vector<uint8_t> encode_utf8(uint32_t v) {
        std::vector<uint8_t> out;
        if (v <= 0x7F) {
            out.push_back(static_cast<uint8_t>(v));
        } else if (v <= 0x7FF) {
            out.push_back(static_cast<uint8_t>(0xC0 | (v >> 6)));
            out.push_back(static_cast<uint8_t>(0x80 | (v & 0x3F)));
        } else if (v <= 0xFFFF) {
            out.push_back(static_cast<uint8_t>(0xE0 | (v >> 12)));
            out.push_back(static_cast<uint8_t>(0x80 | ((v >> 6) & 0x3F)));
            out.push_back(static_cast<uint8_t>(0x80 | (v & 0x3F)));
        } else {
            out.push_back(static_cast<uint8_t>(0xF0 | (v >> 18)));
            out.push_back(static_cast<uint8_t>(0x80 | ((v >> 12) & 0x3F)));
            out.push_back(static_cast<uint8_t>(0x80 | ((v >> 6) & 0x3F)));
            out.push_back(static_cast<uint8_t>(0x80 | (v & 0x3F)));
        }
        return out;
    }

    static inline uint8_t flac_crc8_byte(uint8_t crc, uint8_t data) {
        static constexpr uint8_t kFlacCrc8Table[256] = {
            0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15, 0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
            0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65, 0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
            0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5, 0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
            0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85, 0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
            0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2, 0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
            0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2, 0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
            0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32, 0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
            0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42, 0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
            0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C, 0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
            0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC, 0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
            0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C, 0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
            0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C, 0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
            0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B, 0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
            0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B, 0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
            0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB, 0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
            0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB, 0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3
        };
        return kFlacCrc8Table[crc ^ data];
    }

    static inline uint16_t flac_crc16_byte(uint16_t crc, uint8_t data) {
        static constexpr uint16_t kFlacCrc16Table[256] = {
            0x0000, 0x8005, 0x800F, 0x000A, 0x801B, 0x001E, 0x0014, 0x8011,
            0x8033, 0x0036, 0x003C, 0x8039, 0x0028, 0x802D, 0x8027, 0x0022,
            0x8063, 0x0066, 0x006C, 0x8069, 0x0078, 0x807D, 0x8077, 0x0072,
            0x0050, 0x8055, 0x805F, 0x005A, 0x804B, 0x004E, 0x0044, 0x8041,
            0x80C3, 0x00C6, 0x00CC, 0x80C9, 0x00D8, 0x80DD, 0x80D7, 0x00D2,
            0x00F0, 0x80F5, 0x80FF, 0x00FA, 0x80EB, 0x00EE, 0x00E4, 0x80E1,
            0x00A0, 0x80A5, 0x80AF, 0x00AA, 0x80BB, 0x00BE, 0x00B4, 0x80B1,
            0x8093, 0x0096, 0x009C, 0x8099, 0x0088, 0x808D, 0x8087, 0x0082,
            0x8183, 0x0186, 0x018C, 0x8189, 0x0198, 0x819D, 0x8197, 0x0192,
            0x01B0, 0x81B5, 0x81BF, 0x01BA, 0x81AB, 0x01AE, 0x01A4, 0x81A1,
            0x01E0, 0x81E5, 0x81EF, 0x01EA, 0x81FB, 0x01FE, 0x01F4, 0x81F1,
            0x81D3, 0x01D6, 0x01DC, 0x81D9, 0x01C8, 0x81CD, 0x81C7, 0x01C2,
            0x0140, 0x8145, 0x814F, 0x014A, 0x815B, 0x015E, 0x0154, 0x8151,
            0x8173, 0x0176, 0x017C, 0x8179, 0x0168, 0x816D, 0x8167, 0x0162,
            0x8123, 0x0126, 0x012C, 0x8129, 0x0138, 0x813D, 0x8137, 0x0132,
            0x0110, 0x8115, 0x811F, 0x011A, 0x810B, 0x010E, 0x0104, 0x8101,
            0x8303, 0x0306, 0x030C, 0x8309, 0x0318, 0x831D, 0x8317, 0x0312,
            0x0330, 0x8335, 0x833F, 0x033A, 0x832B, 0x032E, 0x0324, 0x8321,
            0x0360, 0x8365, 0x836F, 0x036A, 0x837B, 0x037E, 0x0374, 0x8371,
            0x8353, 0x0356, 0x035C, 0x8359, 0x0348, 0x834D, 0x8347, 0x0342,
            0x03C0, 0x83C5, 0x83CF, 0x03CA, 0x83DB, 0x03DE, 0x03D4, 0x83D1,
            0x83F3, 0x03F6, 0x03FC, 0x83F9, 0x03E8, 0x83ED, 0x83E7, 0x03E2,
            0x83A3, 0x03A6, 0x03AC, 0x83A9, 0x03B8, 0x83BD, 0x83B7, 0x03B2,
            0x0390, 0x8395, 0x839F, 0x039A, 0x838B, 0x038E, 0x0384, 0x8381,
            0x0280, 0x8285, 0x828F, 0x028A, 0x829B, 0x029E, 0x0294, 0x8291,
            0x82B3, 0x02B6, 0x02BC, 0x82B9, 0x02A8, 0x82AD, 0x82A7, 0x02A2,
            0x82E3, 0x02E6, 0x02EC, 0x82E9, 0x02F8, 0x82FD, 0x82F7, 0x02F2,
            0x02D0, 0x82D5, 0x82DF, 0x02DA, 0x82CB, 0x02CE, 0x02C4, 0x82C1,
            0x8243, 0x0246, 0x024C, 0x8249, 0x0258, 0x825D, 0x8257, 0x0252,
            0x0270, 0x8275, 0x827F, 0x027A, 0x826B, 0x026E, 0x0264, 0x8261,
            0x0220, 0x8225, 0x822F, 0x022A, 0x823B, 0x023E, 0x0234, 0x8231,
            0x8213, 0x0216, 0x021C, 0x8219, 0x0208, 0x820D, 0x8207, 0x0202
        };
        return (crc << 8) ^ kFlacCrc16Table[(uint8_t)(crc >> 8) ^ data];
    }
};

} // namespace digidaw::adapters::audio
