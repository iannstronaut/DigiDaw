#pragma once

#include "../../domain/common/result.hpp"
#include <cstdint>
#include <vector>
#include <span>
#include <string>
#include <cstring>
#include <chrono>

namespace digidaw::adapters::ipc {

enum class BridgeMsgType : uint16_t {
    Hello = 0x0001,
    Ack = 0x0002,
    LoadReq = 0x0003,
    LoadOk = 0x0004,
    ProcessParams = 0x0010,
    Flush = 0x0011,
    SetState = 0x0012,
    GetStateReq = 0x0013,
    GetStateOk = 0x0014,
    ShowEditor = 0x0020,
    CloseEditor = 0x0021,
    Heartbeat = 0x0030,
    HeartbeatAck = 0x0031,
    Error = 0x0040,
    PluginCrashed = 0x0041,
    Bypass = 0x0050,
    TailRequest = 0x0051
};

constexpr uint32_t MaxPayloadBytes = 8 * 1024 * 1024; // 8 MB cap (18 §3)
constexpr uint32_t BridgeProtocolMajorVersion = 1;

#pragma pack(push, 1)
struct BridgeFrameHeader {
    uint32_t length{0}; // Length of payload
    uint16_t msg_type{0};
    uint16_t flags{0};  // bit0: compressed, bit1: urgent
};

struct ShmHeader {
    uint32_t magic{0x53484D42}; // 'SHMB'
    uint32_t version{1};
    uint32_t block_frames{512};
    uint32_t channel_count{2};
    volatile uint64_t write_seq{0};
    volatile uint64_t read_seq{0};
    uint32_t heart_beat_piggyback{0};
    uint32_t reserved{0};
};
#pragma pack(pop)

struct BridgeMessage {
    BridgeMsgType type{BridgeMsgType::Heartbeat};
    uint16_t flags{0};
    std::vector<uint8_t> payload{};

    [[nodiscard]] std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> buffer(sizeof(BridgeFrameHeader) + payload.size());
        auto* hdr = reinterpret_cast<BridgeFrameHeader*>(buffer.data());
        hdr->length = static_cast<uint32_t>(payload.size());
        hdr->msg_type = static_cast<uint16_t>(type);
        hdr->flags = flags;
        if (!payload.empty()) {
            std::memcpy(buffer.data() + sizeof(BridgeFrameHeader), payload.data(), payload.size());
        }
        return buffer;
    }

    static domain::Result<BridgeMessage> deserialize(std::span<const uint8_t> data) {
        if (data.size() < sizeof(BridgeFrameHeader)) {
            return domain::Result<BridgeMessage>(domain::ErrorCode::CorruptChunk);
        }

        const auto* hdr = reinterpret_cast<const BridgeFrameHeader*>(data.data());
        if (hdr->length > MaxPayloadBytes) {
            return domain::Result<BridgeMessage>(domain::ErrorCode::CorruptChunk);
        }

        if (data.size() < sizeof(BridgeFrameHeader) + hdr->length) {
            return domain::Result<BridgeMessage>(domain::ErrorCode::CorruptChunk);
        }

        BridgeMessage msg;
        msg.type = static_cast<BridgeMsgType>(hdr->msg_type);
        msg.flags = hdr->flags;
        if (hdr->length > 0) {
            msg.payload.assign(
                data.data() + sizeof(BridgeFrameHeader),
                data.data() + sizeof(BridgeFrameHeader) + hdr->length);
        }
        return domain::Result<BridgeMessage>(std::move(msg));
    }
};

// State supervisor for crash isolation (18 §5, DAW-NFR-005)
class BridgeSupervisor {
public:
    explicit BridgeSupervisor(std::string plugin_uid)
        : plugin_uid_(std::move(plugin_uid)) {}

    void on_heartbeat_received() noexcept {
        last_heartbeat_ = std::chrono::steady_clock::now();
        consecutive_misses_ = 0;
    }

    [[nodiscard]] bool check_timeout(std::chrono::milliseconds timeout_threshold = std::chrono::milliseconds(2000)) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat_);
        if (elapsed > timeout_threshold) {
            consecutive_misses_++;
            if (consecutive_misses_ >= 2) {
                bypassed_ = true;
                failed_ = true;
                return true; // Timeout detected!
            }
        }
        return false;
    }

    void on_process_crash() noexcept {
        bypassed_ = true;
        failed_ = true;
    }

    [[nodiscard]] bool is_bypassed() const noexcept { return bypassed_; }
    [[nodiscard]] bool has_failed() const noexcept { return failed_; }
    [[nodiscard]] const std::string& plugin_uid() const noexcept { return plugin_uid_; }

private:
    std::string plugin_uid_;
    std::chrono::steady_clock::time_point last_heartbeat_{std::chrono::steady_clock::now()};
    uint32_t consecutive_misses_{0};
    bool bypassed_{false};
    bool failed_{false};
};

} // namespace digidaw::adapters::ipc
