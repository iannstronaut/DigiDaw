#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace digidaw::domain {

struct AudioBufferView {
    float* left{nullptr};
    float* right{nullptr};
    size_t frames{0};

    void clear() noexcept {
        if (left) std::fill_n(left, frames, 0.0f);
        if (right) std::fill_n(right, frames, 0.0f);
    }

    void copy_from(const AudioBufferView& src) noexcept {
        const size_t count = std::min(frames, src.frames);
        if (left && src.left) std::copy_n(src.left, count, left);
        if (right && src.right) std::copy_n(src.right, count, right);
    }

    void add_from(const AudioBufferView& src, float gain = 1.0f) noexcept {
        const size_t count = std::min(frames, src.frames);
        for (size_t i = 0; i < count; ++i) {
            if (left && src.left) left[i] += src.left[i] * gain;
            if (right && src.right) right[i] += src.right[i] * gain;
        }
    }

    void apply_gain(float gain) noexcept {
        for (size_t i = 0; i < frames; ++i) {
            if (left) left[i] *= gain;
            if (right) right[i] *= gain;
        }
    }

    [[nodiscard]] std::pair<float, float> compute_peak() const noexcept {
        float peak_l = 0.0f;
        float peak_r = 0.0f;
        for (size_t i = 0; i < frames; ++i) {
            if (left) peak_l = std::max(peak_l, std::abs(left[i]));
            if (right) peak_r = std::max(peak_r, std::abs(right[i]));
        }
        return {peak_l, peak_r};
    }
};

class OwningAudioBuffer {
public:
    explicit OwningAudioBuffer(size_t max_frames = 2048)
        : left_(max_frames, 0.0f), right_(max_frames, 0.0f), frames_(max_frames) {}

    void resize_frames(size_t new_frames) {
        if (new_frames > left_.size()) {
            left_.resize(new_frames, 0.0f);
            right_.resize(new_frames, 0.0f);
        }
        frames_ = new_frames;
    }

    [[nodiscard]] AudioBufferView view() noexcept {
        return AudioBufferView{left_.data(), right_.data(), frames_};
    }

    [[nodiscard]] const AudioBufferView view() const noexcept {
        return AudioBufferView{
            const_cast<float*>(left_.data()),
            const_cast<float*>(right_.data()),
            frames_
        };
    }

    [[nodiscard]] size_t frames() const noexcept { return frames_; }

    void clear() noexcept {
        view().clear();
    }

private:
    std::vector<float> left_;
    std::vector<float> right_;
    size_t frames_;
};

struct MidiEvent {
    int64_t tick{0};
    uint8_t status{0};
    uint8_t data1{0};
    uint8_t data2{0};

    [[nodiscard]] constexpr bool is_note_on() const noexcept {
        return (status & 0xF0) == 0x90 && data2 > 0;
    }

    [[nodiscard]] constexpr bool is_note_off() const noexcept {
        return ((status & 0xF0) == 0x80) || ((status & 0xF0) == 0x90 && data2 == 0);
    }

    [[nodiscard]] constexpr uint8_t channel() const noexcept {
        return status & 0x0F;
    }

    [[nodiscard]] constexpr uint8_t note_number() const noexcept {
        return data1;
    }

    [[nodiscard]] constexpr uint8_t velocity() const noexcept {
        return data2;
    }

    static constexpr MidiEvent make_note_on(int64_t t, uint8_t ch, uint8_t note, uint8_t vel) noexcept {
        return MidiEvent{t, static_cast<uint8_t>(0x90 | (ch & 0x0F)), note, vel};
    }

    static constexpr MidiEvent make_note_off(int64_t t, uint8_t ch, uint8_t note) noexcept {
        return MidiEvent{t, static_cast<uint8_t>(0x80 | (ch & 0x0F)), note, 0};
    }
};

} // namespace digidaw::domain
