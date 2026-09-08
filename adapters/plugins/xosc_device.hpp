#pragma once

#include "../../domain/devices/device.hpp"
#include "../../domain/dsp/denormal.hpp"
#include "xosc_dsp.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace digidaw::adapters::plugins {

class XOSCDevice final : public domain::IDevice {
public:
    static constexpr size_t kNumParams = 145;

    XOSCDevice() {
        const auto& s_list = xosc::specs();
        for (size_t i = 0; i < kNumParams && i < s_list.size(); ++i) {
            raw_params_[i].store(s_list[i].initial, std::memory_order_relaxed);
        }
    }

    [[nodiscard]] domain::DeviceUid uid() const override { return "core.generator.xosc"; }
    [[nodiscard]] std::string name() const override { return "XOSC"; }
    [[nodiscard]] domain::DeviceCategory category() const override { return domain::DeviceCategory::Generator; }

    void prepare(double sample_rate, size_t /*max_block_size*/) override {
        sample_rate_ = sample_rate > 0.0 ? sample_rate : 44100.0;
        engine_.prepare(static_cast<float>(sample_rate_));
        peak_l_.store(0.0f, std::memory_order_relaxed);
        peak_r_.store(0.0f, std::memory_order_relaxed);
        active_voices_.store(0, std::memory_order_relaxed);
        panic_.store(false, std::memory_order_relaxed);
    }

    void reset() override {
        engine_.prepare(static_cast<float>(sample_rate_));
        peak_l_.store(0.0f, std::memory_order_relaxed);
        peak_r_.store(0.0f, std::memory_order_relaxed);
        active_voices_.store(0, std::memory_order_relaxed);
    }

    void panic() noexcept {
        panic_.store(true, std::memory_order_relaxed);
    }

    [[nodiscard]] size_t active_voices() const noexcept {
        return static_cast<size_t>(active_voices_.load(std::memory_order_relaxed));
    }

    [[nodiscard]] float peak_l() const noexcept {
        return peak_l_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] float peak_r() const noexcept {
        return peak_r_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] double sample_rate() const noexcept {
        return sample_rate_;
    }

    [[nodiscard]] const xosc::Engine& engine() const noexcept {
        return engine_;
    }

    [[nodiscard]] xosc::Engine& engine() noexcept {
        return engine_;
    }

    void set_param_plain(size_t index, float value) noexcept {
        if (index < kNumParams) {
            if (!std::isfinite(value)) return;
            const auto& s = xosc::specs()[index];
            float clamped = std::clamp(value, s.lo, s.hi);
            raw_params_[index].store(clamped, std::memory_order_relaxed);
        }
    }

    [[nodiscard]] float get_param_plain(size_t index) const noexcept {
        if (index < kNumParams) {
            return raw_params_[index].load(std::memory_order_relaxed);
        }
        return 0.0f;
    }

    void set_param_by_id(const std::string& id, float value) noexcept {
        int idx = xosc::index(id);
        if (idx >= 0 && idx < static_cast<int>(kNumParams)) {
            set_param_plain(static_cast<size_t>(idx), value);
        }
    }

    [[nodiscard]] float get_param_by_id(const std::string& id) const noexcept {
        int idx = xosc::index(id);
        if (idx >= 0 && idx < static_cast<int>(kNumParams)) {
            return raw_params_[static_cast<size_t>(idx)].load(std::memory_order_relaxed);
        }
        return 0.0f;
    }

    void load_factory(int preset) noexcept {
        const auto& s_list = xosc::specs();
        for (size_t i = 0; i < kNumParams && i < s_list.size(); ++i) {
            raw_params_[i].store(s_list[i].initial, std::memory_order_relaxed);
        }
        auto set = [this](const std::string& id, float value) {
            int idx = xosc::index(id);
            if (idx >= 0 && idx < static_cast<int>(kNumParams)) {
                raw_params_[static_cast<size_t>(idx)].store(value, std::memory_order_relaxed);
            }
        };

        if (preset == 1) { // Thick / stereo stack
            set("o0_voices", 6);
            set("o0_detune", 16);
            set("o1_on", 1);
            set("o1_wave", 0);
            set("o1_tune", -12);
            set("o1_vol", -15);
            set("a0_route1", 1);
            set("f0_on", 1);
            set("f0_route0", 1);
            set("f0_cutoff", 4200);
            set("reverb_on", 1);
            set("master_gain", -10);
        } else if (preset == 2) { // Bass / punch
            set("o0_voices", 2);
            set("o0_detune", 7);
            set("o0_tune", -12);
            set("f0_on", 1);
            set("f0_route0", 1);
            set("f0_cutoff", 350);
            set("f0_drive", 5);
            set("e0_on", 1);
            set("e0_route0", 1);
            set("e0_target", 2);
            set("e0_mix", .65f);
            set("e0_decay", .2f);
            set("e0_sustain", 0);
            set("a0_release", .12f);
            set("dist_on", 1);
        } else if (preset == 3) { // Pad / slow bloom
            set("o0_voices", 8);
            set("o0_detune", 22);
            set("o1_on", 1);
            set("o1_wave", 0);
            set("o1_tune", 12);
            set("o1_vol", -18);
            set("a0_route1", 1);
            set("a0_attack", 1.2f);
            set("a0_release", 3);
            set("f0_on", 1);
            set("f0_route0", 1);
            set("f0_cutoff", 2400);
            set("delay_on", 1);
            set("reverb_on", 1);
            set("reverb_mix", .3f);
            set("master_gain", -12);
        }
        panic_.store(true, std::memory_order_relaxed);
    }

    void process(domain::AudioBufferView& buffer, std::span<const domain::MidiEvent> midi) override {
        if (buffer.frames == 0) return;
        domain::dsp::enable_ftz_daz();

        if (panic_.exchange(false, std::memory_order_relaxed)) {
            engine_.allNotesOff(0, true);
        }

        engine_.setPatch(xosc::decode([this](int i) {
            return raw_params_[static_cast<size_t>(i)].load(std::memory_order_relaxed);
        }));

        auto dispatch_midi = [this](const domain::MidiEvent& ev) {
            int c = ev.channel() + 1; // 1-based channel 1..16
            if (ev.is_note_on()) {
                engine_.noteOn(ev.note_number(), static_cast<float>(ev.velocity()) / 127.0f, c);
            } else if (ev.is_note_off()) {
                engine_.noteOff(ev.note_number(), c);
            } else if (ev.is_control_change()) {
                if (ev.data1 == 64) {
                    engine_.pedal(ev.data2 >= 64, c);
                } else if (ev.data1 == 120) {
                    engine_.allNotesOff(c, true);
                } else if (ev.data1 == 123) {
                    engine_.allNotesOff(c, false);
                }
            } else if ((ev.status & 0xF0) == 0xE0) {
                int val = (static_cast<int>(ev.data2) << 7) | static_cast<int>(ev.data1);
                engine_.pitchBend(static_cast<float>(val - 8192) / 8192.0f * 2.0f, c);
            }
        };

        for (const auto& ev : midi) {
            dispatch_midi(ev);
        }

        float pk_l = 0.0f;
        float pk_r = 0.0f;

        for (size_t s = 0; s < buffer.frames; ++s) {
            auto frame = engine_.tick();
            if (buffer.left) buffer.left[s] += frame[0];
            if (buffer.right) buffer.right[s] += frame[1];

            pk_l = std::max(pk_l, std::abs(frame[0]));
            pk_r = std::max(pk_r, std::abs(frame[1]));
        }

        peak_l_.store(pk_l, std::memory_order_relaxed);
        peak_r_.store(pk_r, std::memory_order_relaxed);
        active_voices_.store(engine_.activeVoices(), std::memory_order_relaxed);
    }

    void set_parameter(uint32_t param_id, float normalized_value) override {
        if (param_id < kNumParams) {
            if (!std::isfinite(normalized_value)) return;
            const auto& s = xosc::specs()[param_id];
            float plain = xosc::to_plain(s, normalized_value);
            raw_params_[param_id].store(plain, std::memory_order_relaxed);
        }
    }

    [[nodiscard]] float get_parameter(uint32_t param_id) const override {
        if (param_id < kNumParams) {
            const auto& s = xosc::specs()[param_id];
            float plain = raw_params_[param_id].load(std::memory_order_relaxed);
            return xosc::to_normalized(s, plain);
        }
        return 0.0f;
    }

    [[nodiscard]] std::vector<domain::ParamDesc> parameters() const override {
        std::vector<domain::ParamDesc> descs;
        descs.reserve(kNumParams);
        const auto& s_list = xosc::specs();
        for (size_t i = 0; i < kNumParams && i < s_list.size(); ++i) {
            const auto& s = s_list[i];
            descs.push_back(domain::ParamDesc{
                static_cast<uint32_t>(i),
                s.name,
                xosc::to_normalized(s, s.initial),
                0.0f,
                1.0f,
                s.unit
            });
        }
        return descs;
    }

    [[nodiscard]] std::vector<uint8_t> save_state() const override {
        // Format: Magic [4 bytes] + Version [4 bytes] + Count [4 bytes] + Raw Floats [Count * 4 bytes]
        std::vector<uint8_t> data(12 + kNumParams * sizeof(float));
        std::memcpy(data.data(), "XOSC", 4);
        uint32_t ver = 1;
        uint32_t count = static_cast<uint32_t>(kNumParams);
        std::memcpy(data.data() + 4, &ver, 4);
        std::memcpy(data.data() + 8, &count, 4);
        float* ptr = reinterpret_cast<float*>(data.data() + 12);
        for (size_t i = 0; i < kNumParams; ++i) {
            ptr[i] = raw_params_[i].load(std::memory_order_relaxed);
        }
        return data;
    }

    domain::Result<void> load_state(std::span<const uint8_t> data) override {
        if (data.size() < 12 + kNumParams * sizeof(float)) {
            return domain::Result<void>(domain::ErrorCode::StateIncompatible);
        }
        if (std::memcmp(data.data(), "XOSC", 4) != 0) {
            return domain::Result<void>(domain::ErrorCode::StateIncompatible);
        }
        uint32_t ver = 0;
        std::memcpy(&ver, data.data() + 4, 4);
        if (ver != 1) {
            return domain::Result<void>(domain::ErrorCode::StateIncompatible);
        }
        uint32_t count = 0;
        std::memcpy(&count, data.data() + 8, 4);
        if (count != kNumParams) {
            return domain::Result<void>(domain::ErrorCode::StateIncompatible);
        }
        const float* ptr = reinterpret_cast<const float*>(data.data() + 12);
        const auto& s_list = xosc::specs();
        for (size_t i = 0; i < kNumParams; ++i) {
            float val = ptr[i];
            const auto& s = s_list[i];
            if (!std::isfinite(val)) {
                val = s.initial;
            }
            float clamped = std::clamp(val, s.lo, s.hi);
            raw_params_[i].store(clamped, std::memory_order_relaxed);
        }
        panic_.store(true, std::memory_order_relaxed);
        return domain::Result<void>::ok();
    }

private:
    xosc::Engine engine_;
    std::array<std::atomic<float>, kNumParams> raw_params_{};
    std::atomic<float> peak_l_{0.0f};
    std::atomic<float> peak_r_{0.0f};
    std::atomic<int> active_voices_{0};
    std::atomic<bool> panic_{false};
    double sample_rate_{44100.0};
};

} // namespace digidaw::adapters::plugins
