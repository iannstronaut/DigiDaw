#pragma once

#include "../../app/ports/project_repository.hpp"
#include "../../domain/common/result.hpp"
#include "../../adapters/audio/wave_file_writer.hpp"
#include <fstream>
#include <vector>
#include <cstring>
#include <cstdio>
#include <filesystem>

namespace digidaw::adapters::project {

enum class ChunkType : uint16_t {
    Meta = 0x0001,
    TimeMap = 0x0002,
    Patterns = 0x0010,
    Channels = 0x0011,
    Tracks = 0x0012,
    Mixer = 0x0020,
    Automation = 0x0030,
    PluginState = 0x0040,
    UiState = 0x0050,
    UnknownFuture = 0x00FF // Test chunk for forward-compatibility
};

constexpr uint32_t MagicOdaw = 0x5741444F; // 'ODAW' in Little-Endian
constexpr uint32_t CurrentVersion = 1;

class OdpFileRepository : public app::IProjectRepository {
public:
    domain::Result<void> save(const domain::Project& project, const std::string& filepath) override {
        // Atomic save: write to .tmp then atomic rename (DAW-NFR-004)
        const std::string tmp_path = filepath + ".tmp";
        std::ofstream file(tmp_path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            return domain::Result<void>(domain::ErrorCode::FileLocked);
        }

        // Prepare chunk payloads
        std::vector<uint8_t> meta_payload = serialize_meta(project);
        std::vector<uint8_t> timemap_payload = serialize_timemap(project);
        std::vector<uint8_t> patterns_payload = serialize_patterns(project);
        std::vector<uint8_t> channels_payload = serialize_channels(project);
        std::vector<uint8_t> tracks_payload = serialize_tracks(project);
        std::vector<uint8_t> uistate_payload = serialize_uistate(project);

        const uint32_t chunk_count = 6;

        // Write Header
        write_u32(file, MagicOdaw);
        write_u32(file, CurrentVersion);
        write_u32(file, chunk_count);

        // Write Chunks
        write_chunk(file, ChunkType::Meta, meta_payload);
        write_chunk(file, ChunkType::TimeMap, timemap_payload);
        write_chunk(file, ChunkType::Patterns, patterns_payload);
        write_chunk(file, ChunkType::Channels, channels_payload);
        write_chunk(file, ChunkType::Tracks, tracks_payload);
        write_chunk(file, ChunkType::UiState, uistate_payload);

        file.flush();
        if (file.fail()) {
            file.close();
            std::remove(tmp_path.c_str());
            return domain::Result<void>(domain::ErrorCode::AutosaveFailed);
        }
        file.close();

        // Atomic replace
        std::error_code ec;
        std::filesystem::rename(tmp_path, filepath, ec);
        if (ec) {
            // Fallback: copy and delete
            std::filesystem::copy_file(tmp_path, filepath, std::filesystem::copy_options::overwrite_existing, ec);
            std::filesystem::remove(tmp_path, ec);
            if (ec) {
                return domain::Result<void>(domain::ErrorCode::FileLocked);
            }
        }

        return domain::Result<void>::ok();
    }

    domain::Result<domain::Project> load(const std::string& filepath) override {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            return domain::Result<domain::Project>(domain::ErrorCode::FileNotFound);
        }

        uint32_t magic = 0;
        uint32_t version = 0;
        uint32_t chunk_count = 0;

        if (!read_u32(file, magic) || magic != MagicOdaw) {
            return domain::Result<domain::Project>(domain::ErrorCode::InvalidMagic);
        }

        if (!read_u32(file, version) || version > CurrentVersion) {
            return domain::Result<domain::Project>(domain::ErrorCode::UnsupportedVersion);
        }

        if (!read_u32(file, chunk_count)) {
            return domain::Result<domain::Project>(domain::ErrorCode::CorruptChunk);
        }

        domain::Project project("Loaded Project");

        for (uint32_t i = 0; i < chunk_count; ++i) {
            uint16_t type_val = 0;
            uint32_t length = 0;

            if (!read_u16(file, type_val) || !read_u32(file, length)) {
                // Unexpected EOF / partial file
                return domain::Result<domain::Project>(domain::ErrorCode::CorruptChunk);
            }

            // Cap on chunk size to avoid OOM from corrupt length
            if (length > 64 * 1024 * 1024) { // 64 MB guard (04-data-design §6)
                return domain::Result<domain::Project>(domain::ErrorCode::CorruptChunk);
            }

            std::vector<uint8_t> payload(length);
            if (length > 0) {
                file.read(reinterpret_cast<char*>(payload.data()), length);
                if (file.gcount() != static_cast<std::streamsize>(length)) {
                    return domain::Result<domain::Project>(domain::ErrorCode::CorruptChunk);
                }
            }

            auto type = static_cast<ChunkType>(type_val);
            switch (type) {
                case ChunkType::Meta:
                    deserialize_meta(project, payload);
                    break;
                case ChunkType::TimeMap:
                    deserialize_timemap(project, payload);
                    break;
                case ChunkType::Patterns:
                    deserialize_patterns(project, payload);
                    break;
                case ChunkType::Channels:
                    deserialize_channels(project, payload);
                    break;
                case ChunkType::Tracks:
                    deserialize_tracks(project, payload);
                    break;
                case ChunkType::UiState:
                    deserialize_uistate(project, payload);
                    break;
                default:
                    // Unknown future chunk: forward-compatibility rule DAW-DATA-002: skip safely
                    break;
            }
        }

        project.mark_clean();
        return domain::Result<domain::Project>(std::move(project));
    }

private:
    static void write_u16(std::ofstream& f, uint16_t v) {
        f.write(reinterpret_cast<const char*>(&v), 2);
    }

    static void write_u32(std::ofstream& f, uint32_t v) {
        f.write(reinterpret_cast<const char*>(&v), 4);
    }

    static bool read_u16(std::ifstream& f, uint16_t& v) {
        return bool(f.read(reinterpret_cast<char*>(&v), 2));
    }

    static bool read_u32(std::ifstream& f, uint32_t& v) {
        return bool(f.read(reinterpret_cast<char*>(&v), 4));
    }

    static void write_chunk(std::ofstream& f, ChunkType type, const std::vector<uint8_t>& payload) {
        write_u16(f, static_cast<uint16_t>(type));
        write_u32(f, static_cast<uint32_t>(payload.size()));
        if (!payload.empty()) {
            f.write(reinterpret_cast<const char*>(payload.data()), payload.size());
        }
    }

    // Serializers
    static std::vector<uint8_t> serialize_meta(const domain::Project& project) {
        std::vector<uint8_t> buf;
        const auto& name = project.name();
        uint32_t len = static_cast<uint32_t>(name.size());
        buf.resize(sizeof(uint32_t) + len);
        std::memcpy(buf.data(), &len, sizeof(uint32_t));
        if (len > 0) {
            std::memcpy(buf.data() + sizeof(uint32_t), name.data(), len);
        }
        return buf;
    }

    static void deserialize_meta(domain::Project& project, const std::vector<uint8_t>& buf) {
        if (buf.size() < sizeof(uint32_t)) return;
        uint32_t len = 0;
        std::memcpy(&len, buf.data(), sizeof(uint32_t));
        if (buf.size() >= sizeof(uint32_t) + len) {
            std::string name(reinterpret_cast<const char*>(buf.data() + sizeof(uint32_t)), len);
            project.set_name(std::move(name));
        }
    }

    static std::vector<uint8_t> serialize_timemap(const domain::Project& project) {
        std::vector<uint8_t> buf;
        double bpm = project.time_map().get_bpm_at(0);
        buf.resize(sizeof(double));
        std::memcpy(buf.data(), &bpm, sizeof(double));
        return buf;
    }

    static void deserialize_timemap(domain::Project& project, const std::vector<uint8_t>& buf) {
        if (buf.size() >= sizeof(double)) {
            double bpm = 120.0;
            std::memcpy(&bpm, buf.data(), sizeof(double));
            project.time_map().set_tempo(bpm);
        }
    }

    static std::vector<uint8_t> serialize_channels(const domain::Project& project) {
        std::vector<uint8_t> buf;
        uint32_t count = static_cast<uint32_t>(project.channels().size());
        // Append count
        buf.resize(sizeof(uint32_t));
        std::memcpy(buf.data(), &count, sizeof(uint32_t));

        for (const auto& ch : project.channels()) {
            uint32_t id = ch.id();
            float vol = ch.settings().volume;
            float pan = ch.settings().pan;
            uint8_t mix_track = ch.settings().mixer_track;

            uint32_t uid_len = static_cast<uint32_t>(ch.device_uid().size());
            uint32_t name_len = static_cast<uint32_t>(ch.settings().name.size());

            size_t offset = buf.size();
            buf.resize(offset + sizeof(uint32_t) + sizeof(float) * 2 + 1 + sizeof(uint32_t) + uid_len + sizeof(uint32_t) + name_len);

            uint8_t* ptr = buf.data() + offset;
            std::memcpy(ptr, &id, sizeof(uint32_t)); ptr += sizeof(uint32_t);
            std::memcpy(ptr, &vol, sizeof(float)); ptr += sizeof(float);
            std::memcpy(ptr, &pan, sizeof(float)); ptr += sizeof(float);
            *ptr = mix_track; ptr += 1;

            std::memcpy(ptr, &uid_len, sizeof(uint32_t)); ptr += sizeof(uint32_t);
            if (uid_len > 0) {
                std::memcpy(ptr, ch.device_uid().data(), uid_len);
                ptr += uid_len;
            }

            std::memcpy(ptr, &name_len, sizeof(uint32_t)); ptr += sizeof(uint32_t);
            if (name_len > 0) {
                std::memcpy(ptr, ch.settings().name.data(), name_len);
            }
        }
        return buf;
    }

    static void deserialize_channels(domain::Project& project, const std::vector<uint8_t>& buf) {
        if (buf.size() < sizeof(uint32_t)) return;
        const uint8_t* ptr = buf.data();
        uint32_t count = 0;
        std::memcpy(&count, ptr, sizeof(uint32_t)); ptr += sizeof(uint32_t);

        project.channels().clear();
        for (uint32_t i = 0; i < count; ++i) {
            if (ptr + sizeof(uint32_t) + sizeof(float) * 2 + 1 + sizeof(uint32_t) > buf.data() + buf.size()) break;

            uint32_t id = 0;
            float vol = 0.8f, pan = 0.0f;
            uint8_t mix_track = 0;

            std::memcpy(&id, ptr, sizeof(uint32_t)); ptr += sizeof(uint32_t);
            std::memcpy(&vol, ptr, sizeof(float)); ptr += sizeof(float);
            std::memcpy(&pan, ptr, sizeof(float)); ptr += sizeof(float);
            mix_track = *ptr++;

            uint32_t uid_len = 0;
            std::memcpy(&uid_len, ptr, sizeof(uint32_t)); ptr += sizeof(uint32_t);
            std::string uid;
            if (uid_len > 0 && ptr + uid_len <= buf.data() + buf.size()) {
                uid.assign(reinterpret_cast<const char*>(ptr), uid_len);
                ptr += uid_len;
            }

            uint32_t name_len = 0;
            std::string name = "Channel";
            if (ptr + sizeof(uint32_t) <= buf.data() + buf.size()) {
                std::memcpy(&name_len, ptr, sizeof(uint32_t)); ptr += sizeof(uint32_t);
                if (name_len > 0 && ptr + name_len <= buf.data() + buf.size()) {
                    name.assign(reinterpret_cast<const char*>(ptr), name_len);
                    ptr += name_len;
                }
            }

            domain::ChannelSettings s;
            s.volume = vol;
            s.pan = pan;
            s.mixer_track = mix_track;
            s.name = name;
            project.channels().emplace_back(id, uid, s);
        }
    }

    static std::vector<uint8_t> serialize_patterns(const domain::Project& project) {
        std::vector<uint8_t> buf;
        uint32_t count = static_cast<uint32_t>(project.patterns().size());
        buf.resize(sizeof(uint32_t));
        std::memcpy(buf.data(), &count, sizeof(uint32_t));

        for (const auto& pat : project.patterns()) {
            uint32_t id = pat.id();
            uint32_t name_len = static_cast<uint32_t>(pat.name().size());

            size_t off = buf.size();
            buf.resize(off + sizeof(uint32_t) + sizeof(uint32_t) + name_len);
            uint8_t* ptr = buf.data() + off;

            std::memcpy(ptr, &id, sizeof(uint32_t)); ptr += sizeof(uint32_t);
            std::memcpy(ptr, &name_len, sizeof(uint32_t)); ptr += sizeof(uint32_t);
            if (name_len > 0) {
                std::memcpy(ptr, pat.name().data(), name_len);
            }

            // Write notes
            uint32_t ch_count = static_cast<uint32_t>(pat.all_notes().size());
            off = buf.size();
            buf.resize(off + sizeof(uint32_t));
            std::memcpy(buf.data() + off, &ch_count, sizeof(uint32_t));

            for (const auto& [ch_id, note_set] : pat.all_notes()) {
                uint32_t note_count = static_cast<uint32_t>(note_set.notes().size());
                off = buf.size();
                buf.resize(off + sizeof(uint32_t) + sizeof(uint32_t) + note_count * sizeof(domain::Note));
                ptr = buf.data() + off;

                std::memcpy(ptr, &ch_id, sizeof(uint32_t)); ptr += sizeof(uint32_t);
                std::memcpy(ptr, &note_count, sizeof(uint32_t)); ptr += sizeof(uint32_t);
                if (note_count > 0) {
                    std::memcpy(ptr, note_set.notes().data(), note_count * sizeof(domain::Note));
                }
            }
        }
        return buf;
    }

    static void deserialize_patterns(domain::Project& project, const std::vector<uint8_t>& buf) {
        if (buf.size() < sizeof(uint32_t)) return;
        const uint8_t* ptr = buf.data();
        uint32_t pat_count = 0;
        std::memcpy(&pat_count, ptr, sizeof(uint32_t)); ptr += sizeof(uint32_t);

        project.patterns().clear();
        for (uint32_t i = 0; i < pat_count; ++i) {
            if (ptr + sizeof(uint32_t) * 2 > buf.data() + buf.size()) break;

            uint32_t id = 0;
            uint32_t name_len = 0;
            std::memcpy(&id, ptr, sizeof(uint32_t)); ptr += sizeof(uint32_t);
            std::memcpy(&name_len, ptr, sizeof(uint32_t)); ptr += sizeof(uint32_t);

            std::string name = "Pattern";
            if (name_len > 0 && ptr + name_len <= buf.data() + buf.size()) {
                name.assign(reinterpret_cast<const char*>(ptr), name_len);
                ptr += name_len;
            }

            domain::Pattern pat(id, name);

            if (ptr + sizeof(uint32_t) <= buf.data() + buf.size()) {
                uint32_t ch_count = 0;
                std::memcpy(&ch_count, ptr, sizeof(uint32_t)); ptr += sizeof(uint32_t);

                for (uint32_t c = 0; c < ch_count; ++c) {
                    if (ptr + sizeof(uint32_t) * 2 > buf.data() + buf.size()) break;
                    uint32_t ch_id = 0;
                    uint32_t note_count = 0;
                    std::memcpy(&ch_id, ptr, sizeof(uint32_t)); ptr += sizeof(uint32_t);
                    std::memcpy(&note_count, ptr, sizeof(uint32_t)); ptr += sizeof(uint32_t);

                    auto& note_set = pat.get_or_create_channel_notes(ch_id);
                    for (uint32_t n = 0; n < note_count; ++n) {
                        if (ptr + sizeof(domain::Note) > buf.data() + buf.size()) break;
                        domain::Note note;
                        std::memcpy(&note, ptr, sizeof(domain::Note));
                        ptr += sizeof(domain::Note);
                        note_set.add_note(note);
                    }
                }
            }
            project.patterns().push_back(std::move(pat));
        }
    }

    static std::vector<uint8_t> serialize_tracks(const domain::Project& project) {
        std::vector<uint8_t> buf;
        uint32_t count = static_cast<uint32_t>(project.tracks().size());
        buf.resize(sizeof(uint32_t));
        std::memcpy(buf.data(), &count, sizeof(uint32_t));

        for (const auto& trk : project.tracks()) {
            uint32_t id = trk.id();
            uint32_t name_len = static_cast<uint32_t>(trk.name().size());
            uint32_t clip_count = static_cast<uint32_t>(trk.clips().size());

            size_t off = buf.size();
            buf.resize(off + sizeof(uint32_t) + sizeof(uint32_t) + name_len + sizeof(uint32_t) + clip_count * sizeof(domain::Clip));
            uint8_t* ptr = buf.data() + off;

            std::memcpy(ptr, &id, sizeof(uint32_t)); ptr += sizeof(uint32_t);
            std::memcpy(ptr, &name_len, sizeof(uint32_t)); ptr += sizeof(uint32_t);
            if (name_len > 0) {
                std::memcpy(ptr, trk.name().data(), name_len);
                ptr += name_len;
            }
            std::memcpy(ptr, &clip_count, sizeof(uint32_t)); ptr += sizeof(uint32_t);
            if (clip_count > 0) {
                std::memcpy(ptr, trk.clips().data(), clip_count * sizeof(domain::Clip));
            }
        }
        return buf;
    }

    static void deserialize_tracks(domain::Project& project, const std::vector<uint8_t>& buf) {
        if (buf.size() < sizeof(uint32_t)) return;
        const uint8_t* ptr = buf.data();
        uint32_t count = 0;
        std::memcpy(&count, ptr, sizeof(uint32_t)); ptr += sizeof(uint32_t);

        project.tracks().clear();
        for (uint32_t i = 0; i < count; ++i) {
            if (ptr + sizeof(uint32_t) * 2 > buf.data() + buf.size()) break;

            uint32_t id = 0;
            uint32_t name_len = 0;
            std::memcpy(&id, ptr, sizeof(uint32_t)); ptr += sizeof(uint32_t);
            std::memcpy(&name_len, ptr, sizeof(uint32_t)); ptr += sizeof(uint32_t);

            std::string name = "Track";
            if (name_len > 0 && ptr + name_len <= buf.data() + buf.size()) {
                name.assign(reinterpret_cast<const char*>(ptr), name_len);
                ptr += name_len;
            }

            domain::Track trk(id, name);
            if (ptr + sizeof(uint32_t) <= buf.data() + buf.size()) {
                uint32_t clip_count = 0;
                std::memcpy(&clip_count, ptr, sizeof(uint32_t)); ptr += sizeof(uint32_t);
                for (uint32_t c = 0; c < clip_count; ++c) {
                    if (ptr + sizeof(domain::Clip) > buf.data() + buf.size()) break;
                    domain::Clip clip;
                    std::memcpy(&clip, ptr, sizeof(domain::Clip));
                    ptr += sizeof(domain::Clip);
                    trk.add_clip(clip);
                }
            }
            project.tracks().push_back(std::move(trk));
        }
    }

    static std::vector<uint8_t> serialize_uistate(const domain::Project& project) {
        std::vector<uint8_t> buf(sizeof(domain::UiState));
        std::memcpy(buf.data(), &project.ui_state(), sizeof(domain::UiState));
        return buf;
    }

    static void deserialize_uistate(domain::Project& project, const std::vector<uint8_t>& buf) {
        if (buf.size() >= sizeof(domain::UiState)) {
            std::memcpy(&project.ui_state(), buf.data(), sizeof(domain::UiState));
        }
    }
};

} // namespace digidaw::adapters::project
