#pragma once

#include "../../app/ports/audio_device.hpp"
#include "../../domain/common/result.hpp"
#include "../../domain/dsp/denormal.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "third_party/miniaudio.h"
#pragma GCC diagnostic pop

#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <memory>
#include <cmath>

namespace digidaw::adapters::audio {

class MiniaudioDevice : public app::IAudioDevice {
public:
    MiniaudioDevice() = default;
    ~MiniaudioDevice() override {
        stop();
        close();
    }

    // Non-copyable and non-movable to protect miniaudio internal pointer state
    MiniaudioDevice(const MiniaudioDevice&) = delete;
    MiniaudioDevice& operator=(const MiniaudioDevice&) = delete;
    MiniaudioDevice(MiniaudioDevice&&) = delete;
    MiniaudioDevice& operator=(MiniaudioDevice&&) = delete;

    domain::Result<void> open(double sample_rate, size_t buffer_size, app::AudioProcessCallback callback) override {
        if (device_initialized_ || context_initialized_) {
            close();
        }

        // Validate sample rate input
        if (sample_rate <= 0.0 || std::isnan(sample_rate) || std::isinf(sample_rate)) {
            return domain::Result<void>(domain::ErrorCode::SampleRateMismatch);
        }

        sample_rate_ = sample_rate;
        buffer_size_ = (buffer_size > 0) ? buffer_size : 512;
        callback_ = std::move(callback);

        // Preallocate scratch non-interleaved float buffers to guarantee ZERO allocations in audio callback
        const size_t scratch_capacity = std::max<size_t>(buffer_size_ * 8, 8192);
        scratch_left_.assign(scratch_capacity, 0.0f);
        scratch_right_.assign(scratch_capacity, 0.0f);

        // Initialize miniaudio backend priority hierarchy:
        // 1. WASAPI (Low-latency Windows audio)
        // 2. DirectSound (Legacy/fallback Windows audio)
        // 3. WinMM (Universal multimedia fallback)
        ma_backend backends[] = {
            ma_backend_wasapi,
            ma_backend_dsound,
            ma_backend_winmm
        };

        bool initialized = false;
        ma_context_config context_config = ma_context_config_init();

        for (ma_backend b : backends) {
            ma_result res = ma_context_init(&b, 1, &context_config, &context_);
            if (res != MA_SUCCESS) {
                continue;
            }
            context_initialized_ = true;

            ma_device_config device_config = ma_device_config_init(ma_device_type_playback);
            device_config.playback.format = ma_format_f32;
            device_config.playback.channels = 2;
            device_config.sampleRate = static_cast<ma_uint32>(sample_rate_);
            device_config.periodSizeInFrames = static_cast<ma_uint32>(buffer_size_);
            device_config.performanceProfile = ma_performance_profile_low_latency;
            device_config.noFixedSizedCallback = MA_FALSE; // Maintain consistent buffer callback frames
            device_config.dataCallback = &MiniaudioDevice::data_callback_thunk;
            device_config.pUserData = this;

            if (b == ma_backend_wasapi) {
                device_config.wasapi.noAutoConvertSRC = MA_FALSE;
                device_config.wasapi.noDefaultQualitySRC = MA_FALSE;
            }

            res = ma_device_init(&context_, &device_config, &device_);
            if (res != MA_SUCCESS && device_config.periodSizeInFrames != 0) {
                // Fallback: try default period size if hardware rejected requested period size
                device_config.periodSizeInFrames = 0;
                res = ma_device_init(&context_, &device_config, &device_);
            }

            if (res == MA_SUCCESS) {
                initialized = true;
                device_initialized_ = true;
                break;
            }

            // Cleanup context to retry with next backend in chain
            ma_context_uninit(&context_);
            context_initialized_ = false;
        }

        // Generic fallback with all available backends if prioritized sequence did not initialize
        if (!initialized) {
            ma_result res = ma_context_init(nullptr, 0, &context_config, &context_);
            if (res == MA_SUCCESS) {
                context_initialized_ = true;

                ma_device_config device_config = ma_device_config_init(ma_device_type_playback);
                device_config.playback.format = ma_format_f32;
                device_config.playback.channels = 2;
                device_config.sampleRate = static_cast<ma_uint32>(sample_rate_);
                device_config.periodSizeInFrames = static_cast<ma_uint32>(buffer_size_);
                device_config.performanceProfile = ma_performance_profile_low_latency;
                device_config.noFixedSizedCallback = MA_FALSE;
                device_config.dataCallback = &MiniaudioDevice::data_callback_thunk;
                device_config.pUserData = this;

                res = ma_device_init(&context_, &device_config, &device_);
                if (res != MA_SUCCESS && device_config.periodSizeInFrames != 0) {
                    device_config.periodSizeInFrames = 0;
                    res = ma_device_init(&context_, &device_config, &device_);
                }

                if (res == MA_SUCCESS) {
                    initialized = true;
                    device_initialized_ = true;
                } else {
                    ma_context_uninit(&context_);
                    context_initialized_ = false;
                }
            }
        }

        if (!initialized) {
            close();
            return domain::Result<void>(domain::ErrorCode::DeviceOpenFailed);
        }

        actual_sample_rate_ = (device_.sampleRate > 0) ? static_cast<double>(device_.sampleRate) : sample_rate_;
        actual_buffer_size_ = buffer_size_;

        return domain::Result<void>::ok();
    }

    void close() override {
        stop();
        if (device_initialized_) {
            ma_device_uninit(&device_);
            device_initialized_ = false;
        }
        if (context_initialized_) {
            ma_context_uninit(&context_);
            context_initialized_ = false;
        }
    }

    domain::Result<void> start() override {
        if (!device_initialized_) {
            return domain::Result<void>(domain::ErrorCode::DeviceOpenFailed);
        }
        if (is_running()) {
            return domain::Result<void>::ok();
        }

        ma_result res = ma_device_start(&device_);
        if (res != MA_SUCCESS) {
            return domain::Result<void>(domain::ErrorCode::DeviceOpenFailed);
        }
        return domain::Result<void>::ok();
    }

    void stop() override {
        if (device_initialized_) {
            ma_device_stop(&device_);
        }
    }

    [[nodiscard]] bool is_running() const noexcept override {
        return device_initialized_ && (ma_device_get_state(&device_) == ma_device_state_started);
    }

    [[nodiscard]] double sample_rate() const noexcept override {
        return device_initialized_ ? actual_sample_rate_ : sample_rate_;
    }

    [[nodiscard]] size_t buffer_size() const noexcept override {
        return device_initialized_ ? actual_buffer_size_ : buffer_size_;
    }

    [[nodiscard]] std::string device_name() const override {
        if (!device_initialized_) {
            return "miniaudio (Uninitialized)";
        }
        const char* backend_str = ma_get_backend_name(context_.backend);
        std::string dev_name = "Default Playback Device";
        if (device_.playback.name[0] != '\0') {
            dev_name = device_.playback.name;
        }
        return dev_name + " [miniaudio: " + (backend_str ? backend_str : "Unknown") + "]";
    }

    [[nodiscard]] ma_backend backend() const noexcept {
        return context_initialized_ ? context_.backend : ma_backend_null;
    }

    // Exposed for testing real-time callback processing directly without audio hardware dependency
    void process_audio_block_for_test(void* pOutput, ma_uint32 frameCount) noexcept {
        process_audio_block(pOutput, nullptr, frameCount);
    }

private:
    static void data_callback_thunk(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
        auto* self = static_cast<MiniaudioDevice*>(pDevice->pUserData);
        if (self) {
            self->process_audio_block(pOutput, pInput, frameCount);
        }
    }

    void process_audio_block(void* pOutput, const void* /*pInput*/, ma_uint32 frameCount) noexcept {
        // Enforce FTZ/DAZ to eliminate denormal float slowdowns in audio processing thread
        domain::dsp::enable_ftz_daz();

        if (!pOutput || frameCount == 0) return;

        float* out = static_cast<float*>(pOutput);

        if (!callback_) {
            std::memset(out, 0, frameCount * 2 * sizeof(float));
            return;
        }

        const size_t max_buf = scratch_left_.size();
        if (max_buf == 0) {
            std::memset(out, 0, frameCount * 2 * sizeof(float));
            return;
        }

        ma_uint32 frames_remaining = frameCount;
        ma_uint32 offset = 0;

        while (frames_remaining > 0) {
            const ma_uint32 chunk = (frames_remaining > max_buf) ? static_cast<ma_uint32>(max_buf) : frames_remaining;

            domain::AudioBufferView view{scratch_left_.data(), scratch_right_.data(), chunk};
            view.clear();

            try {
                callback_(view);
            } catch (...) {
                view.clear();
            }

            const float* l = view.left;
            const float* r = view.right;

            for (ma_uint32 i = 0; i < chunk; ++i) {
                float sl = (l && std::isfinite(l[i])) ? std::clamp(l[i], -1.0f, 1.0f) : 0.0f;
                float sr = (r && std::isfinite(r[i])) ? std::clamp(r[i], -1.0f, 1.0f) : 0.0f;
                out[(offset + i) * 2 + 0] = sl;
                out[(offset + i) * 2 + 1] = sr;
            }

            offset += chunk;
            frames_remaining -= chunk;
        }
    }

    double sample_rate_{44100.0};
    size_t buffer_size_{512};
    double actual_sample_rate_{44100.0};
    size_t actual_buffer_size_{512};
    app::AudioProcessCallback callback_;

    ma_context context_{};
    ma_device device_{};
    bool context_initialized_{false};
    bool device_initialized_{false};

    // Preallocated buffers for real-time safe audio view bridging (ZERO allocations in audio callback)
    std::vector<float> scratch_left_;
    std::vector<float> scratch_right_;
};

} // namespace digidaw::adapters::audio
