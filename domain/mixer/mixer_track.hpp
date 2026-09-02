#pragma once

#include "../devices/device.hpp"
#include <memory>
#include <vector>
#include <string>
#include <cstdint>

namespace digidaw::domain {

using MixerTrackId = uint32_t;

constexpr MixerTrackId MasterTrackId = 0;

struct InsertSlot {
    std::shared_ptr<IDevice> device{nullptr};
    bool enabled{true};
    float wet_mix{1.0f}; // 0.0 (dry) to 1.0 (wet)
    bool bypassed_due_to_error{false};
};

struct SendRoute {
    MixerTrackId target_track{MasterTrackId};
    float amount{1.0f}; // 0.0 to 1.0 (or >1.0 boost)
    bool post_fader{true};
};

class MixerTrack {
public:
    MixerTrack(MixerTrackId id, std::string name)
        : id_(id), name_(std::move(name)) {}

    [[nodiscard]] MixerTrackId id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    void set_name(std::string name) { name_ = std::move(name); }

    [[nodiscard]] float volume() const noexcept { return volume_; }
    void set_volume(float v) noexcept { volume_ = std::clamp(v, 0.0f, 2.0f); }

    [[nodiscard]] float pan() const noexcept { return pan_; }
    void set_pan(float p) noexcept { pan_ = std::clamp(p, -1.0f, 1.0f); }

    [[nodiscard]] bool muted() const noexcept { return muted_; }
    void set_muted(bool m) noexcept { muted_ = m; }

    [[nodiscard]] bool solo() const noexcept { return solo_; }
    void set_solo(bool s) noexcept { solo_ = s; }

    // Insert slots (up to 10 inserts per track standard)
    void add_insert(std::shared_ptr<IDevice> dev, bool enabled = true, float wet = 1.0f) {
        inserts_.push_back(InsertSlot{std::move(dev), enabled, wet, false});
    }

    void set_insert(size_t slot, std::shared_ptr<IDevice> dev) {
        if (slot < inserts_.size()) {
            inserts_[slot].device = std::move(dev);
            inserts_[slot].bypassed_due_to_error = false;
        } else if (slot == inserts_.size()) {
            add_insert(std::move(dev));
        }
    }

    [[nodiscard]] std::vector<InsertSlot>& inserts() noexcept { return inserts_; }
    [[nodiscard]] const std::vector<InsertSlot>& inserts() const noexcept { return inserts_; }

    // Send routes
    void add_send(MixerTrackId target, float amount = 1.0f, bool post_fader = true) {
        // Replace existing send to target if present
        for (auto& s : sends_) {
            if (s.target_track == target) {
                s.amount = amount;
                s.post_fader = post_fader;
                return;
            }
        }
        sends_.push_back(SendRoute{target, amount, post_fader});
    }

    void remove_send(MixerTrackId target) {
        sends_.erase(
            std::remove_if(sends_.begin(), sends_.end(),
                           [target](const SendRoute& s) { return s.target_track == target; }),
            sends_.end());
    }

    [[nodiscard]] std::vector<SendRoute>& sends() noexcept { return sends_; }
    [[nodiscard]] const std::vector<SendRoute>& sends() const noexcept { return sends_; }

    [[nodiscard]] std::pair<float, float> compute_pan_gains() const noexcept {
        if (id_ == MasterTrackId) {
            // Master bus uses stereo balance: unity at center
            if (pan_ <= 0.0f) {
                return {1.0f, 1.0f + pan_};
            } else {
                return {1.0f - pan_, 1.0f};
            }
        }
        // Channel tracks use constant-power pan law (-3dB at center)
        const float angle = (pan_ + 1.0f) * 0.5f * (3.14159265f * 0.5f);
        return {std::cos(angle), std::sin(angle)};
    }

private:
    MixerTrackId id_;
    std::string name_;
    float volume_{1.0f};
    float pan_{0.0f};
    bool muted_{false};
    bool solo_{false};
    std::vector<InsertSlot> inserts_;
    std::vector<SendRoute> sends_;
};

} // namespace digidaw::domain
