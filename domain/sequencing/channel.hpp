#pragma once

#include <string>
#include <cstdint>

namespace digidaw::domain {

using ChannelId = uint32_t;

inline constexpr float kDefaultChannelVolume = 0.8f;
inline constexpr float kMaxChannelVolume = 1.0f;

struct ChannelSettings {
    float volume{kDefaultChannelVolume};
    float pan{0.0f};          // -1.0 (Left) to +1.0 (Right)
    uint8_t mixer_track{0};   // 0 = Unassigned / Master (--), 1..N = Insert tracks
    bool muted{false};
    bool solo{false};
    std::string name{"Channel"};
    uint32_t color{0xFF808080};
};

class Channel {
public:
    Channel(ChannelId id, std::string device_uid, ChannelSettings settings)
        : id_(id), device_uid_(std::move(device_uid)), settings_(std::move(settings)) {}

    [[nodiscard]] ChannelId id() const noexcept { return id_; }
    [[nodiscard]] const std::string& device_uid() const noexcept { return device_uid_; }
    void set_device_uid(std::string uid) { device_uid_ = std::move(uid); }

    [[nodiscard]] ChannelSettings& settings() noexcept { return settings_; }
    [[nodiscard]] const ChannelSettings& settings() const noexcept { return settings_; }

private:
    ChannelId id_;
    std::string device_uid_;
    ChannelSettings settings_;
};

} // namespace digidaw::domain
