#pragma once

#include "../../app/ports/audio_device.hpp"
#include "../../domain/common/result.hpp"
#include "../../domain/dsp/denormal.hpp"
#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include "miniaudio_driver.hpp"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include "wasapi_driver.hpp"
#endif

namespace digidaw::adapters::audio {

enum class AudioDriverType : uint8_t {
    Miniaudio = 0,
    ASIO = 1,
    WASAPI = 2,
    DirectSound = 3,
    Null = 4
};

class NullAudioDevice : public app::IAudioDevice {
public:
    NullAudioDevice() = default;
    ~NullAudioDevice() override { stop(); close(); }

    domain::Result<void> open(double sample_rate, size_t buffer_size, app::AudioProcessCallback callback) override {
        sample_rate_ = sample_rate;
        buffer_size_ = buffer_size;
        callback_ = std::move(callback);
        opened_ = true;
        return domain::Result<void>::ok();
    }

    void close() override {
        stop();
        opened_ = false;
    }

    domain::Result<void> start() override {
        if (!opened_) return domain::Result<void>(domain::ErrorCode::DeviceOpenFailed);
        if (running_) return domain::Result<void>::ok();

        running_ = true;
        thread_ = std::thread([this]() {
            domain::dsp::enable_ftz_daz();
            domain::OwningAudioBuffer buf(buffer_size_);
            while (running_) {
                auto start_time = std::chrono::steady_clock::now();

                if (callback_) {
                    auto view = buf.view();
                    callback_(view);
                }

                // Simulate audio clock intervals
                const double interval_ms = (static_cast<double>(buffer_size_) / sample_rate_) * 1000.0;
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time);
                auto sleep_duration = std::chrono::milliseconds(static_cast<int>(interval_ms)) - elapsed;
                if (sleep_duration > std::chrono::milliseconds(0)) {
                    std::this_thread::sleep_for(sleep_duration);
                }
            }
        });
        return domain::Result<void>::ok();
    }

    void stop() override {
        if (running_) {
            running_ = false;
            if (thread_.joinable()) {
                thread_.join();
            }
        }
    }

    [[nodiscard]] bool is_running() const noexcept override { return running_; }
    [[nodiscard]] double sample_rate() const noexcept override { return sample_rate_; }
    [[nodiscard]] size_t buffer_size() const noexcept override { return buffer_size_; }
    [[nodiscard]] std::string device_name() const override { return "Null / Headless Audio Driver"; }

private:
    double sample_rate_{44100.0};
    size_t buffer_size_{512};
    app::AudioProcessCallback callback_;
    std::atomic<bool> running_{false};
    bool opened_{false};
    std::thread thread_;
};

#ifdef _WIN32
class WaveOutAudioDevice : public app::IAudioDevice {
public:
    WaveOutAudioDevice() = default;
    ~WaveOutAudioDevice() override { stop(); close(); }

    domain::Result<void> open(double sample_rate, size_t buffer_size, app::AudioProcessCallback callback) override {
        sample_rate_ = sample_rate;
        buffer_size_ = buffer_size;
        callback_ = std::move(callback);

        WAVEFORMATEX wfx{};
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = 2;
        wfx.nSamplesPerSec = static_cast<DWORD>(sample_rate);
        wfx.wBitsPerSample = 16;
        wfx.nBlockAlign = wfx.nChannels * (wfx.wBitsPerSample / 8); // 4 bytes per frame
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
        wfx.cbSize = 0;

        h_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!h_event_) {
            return domain::Result<void>(domain::ErrorCode::DeviceOpenFailed);
        }

        MMRESULT res = waveOutOpen(&h_wave_out_, WAVE_MAPPER, &wfx,
                                   reinterpret_cast<DWORD_PTR>(h_event_), 0, CALLBACK_EVENT);
        if (res != MMSYSERR_NOERROR) {
            CloseHandle(h_event_);
            h_event_ = NULL;
            h_wave_out_ = NULL;
            return domain::Result<void>(domain::ErrorCode::DeviceOpenFailed);
        }

        // 8 buffers provide 92ms jitter-absorption headroom with zero added write latency
        const size_t num_buffers = 8;
        buffer_bytes_ = buffer_size * wfx.nBlockAlign;
        headers_.resize(num_buffers);
        pcm_data_.resize(num_buffers * buffer_bytes_, 0);

        for (size_t i = 0; i < num_buffers; ++i) {
            ZeroMemory(&headers_[i], sizeof(WAVEHDR));
            headers_[i].lpData = reinterpret_cast<LPSTR>(&pcm_data_[i * buffer_bytes_]);
            headers_[i].dwBufferLength = static_cast<DWORD>(buffer_bytes_);
            headers_[i].dwFlags = 0;
            waveOutPrepareHeader(h_wave_out_, &headers_[i], sizeof(WAVEHDR));
            headers_[i].dwFlags |= WHDR_DONE; // Mark done so it can be filled immediately
        }

        opened_ = true;
        return domain::Result<void>::ok();
    }

    void close() override {
        stop();
        if (h_wave_out_) {
            for (auto& hdr : headers_) {
                waveOutUnprepareHeader(h_wave_out_, &hdr, sizeof(WAVEHDR));
            }
            waveOutClose(h_wave_out_);
            h_wave_out_ = NULL;
        }
        if (h_event_) {
            CloseHandle(h_event_);
            h_event_ = NULL;
        }
        opened_ = false;
    }

    domain::Result<void> start() override {
        if (!opened_ || !h_wave_out_) return domain::Result<void>(domain::ErrorCode::DeviceOpenFailed);
        if (running_) return domain::Result<void>::ok();

        timeBeginPeriod(1);
        running_ = true;

        thread_ = std::thread([this]() {
            // TIME_CRITICAL thread priority & MMCSS "Pro Audio" registration
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
            DWORD task_index = 0;
            HMODULE avrt = LoadLibraryA("avrt.dll");
            HANDLE h_task = NULL;
            if (avrt) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
                typedef HANDLE (WINAPI *AvSetMmThreadCharacteristicsW_t)(LPCWSTR, LPDWORD);
                auto pAvSet = reinterpret_cast<AvSetMmThreadCharacteristicsW_t>(GetProcAddress(avrt, "AvSetMmThreadCharacteristicsW"));
                if (pAvSet) h_task = pAvSet(L"Pro Audio", &task_index);
#pragma GCC diagnostic pop
            }

            // Flush denormal numbers to zero on CPU to completely eliminate denormal slowdowns
            domain::dsp::enable_ftz_daz();

            domain::OwningAudioBuffer float_buf(buffer_size_);
            size_t buf_idx = 0;

            // Pre-fill initial 4 buffers so DAC begins with steady pipeline
            for (size_t i = 0; i < 4 && i < headers_.size(); ++i) {
                WAVEHDR& hdr = headers_[buf_idx];
                auto view = float_buf.view();
                view.clear();
                if (callback_) {
                    callback_(view);
                }
                int16_t* pcm_out = reinterpret_cast<int16_t*>(hdr.lpData);
                for (size_t f = 0; f < buffer_size_; ++f) {
                    float sl = std::clamp(view.left ? view.left[f] : 0.0f, -1.0f, 1.0f);
                    float sr = std::clamp(view.right ? view.right[f] : 0.0f, -1.0f, 1.0f);
                    pcm_out[f * 2 + 0] = static_cast<int16_t>(sl * 32767.0f);
                    pcm_out[f * 2 + 1] = static_cast<int16_t>(sr * 32767.0f);
                }
                hdr.dwFlags &= ~WHDR_DONE;
                waveOutWrite(h_wave_out_, &hdr, sizeof(WAVEHDR));
                buf_idx = (buf_idx + 1) % headers_.size();
            }

            while (running_) {
                // Event-driven: wakes up immediately when sound hardware releases a buffer
                WaitForSingleObject(h_event_, 15);
                if (!running_) break;

                while (running_) {
                    WAVEHDR& hdr = headers_[buf_idx];
                    if (!(hdr.dwFlags & WHDR_DONE)) {
                        break;
                    }

                    auto view = float_buf.view();
                    view.clear();
                    if (callback_) {
                        callback_(view);
                    }

                    int16_t* pcm_out = reinterpret_cast<int16_t*>(hdr.lpData);
                    for (size_t f = 0; f < buffer_size_; ++f) {
                        float sl = std::clamp(view.left ? view.left[f] : 0.0f, -1.0f, 1.0f);
                        float sr = std::clamp(view.right ? view.right[f] : 0.0f, -1.0f, 1.0f);
                        pcm_out[f * 2 + 0] = static_cast<int16_t>(sl * 32767.0f);
                        pcm_out[f * 2 + 1] = static_cast<int16_t>(sr * 32767.0f);
                    }

                    hdr.dwFlags &= ~WHDR_DONE;
                    waveOutWrite(h_wave_out_, &hdr, sizeof(WAVEHDR));
                    buf_idx = (buf_idx + 1) % headers_.size();
                }
            }

            if (avrt) {
                if (h_task) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
                    typedef BOOL (WINAPI *AvRevertMmThreadCharacteristics_t)(HANDLE);
                    auto pAvRev = reinterpret_cast<AvRevertMmThreadCharacteristics_t>(GetProcAddress(avrt, "AvRevertMmThreadCharacteristics"));
                    if (pAvRev) pAvRev(h_task);
#pragma GCC diagnostic pop
                }
                FreeLibrary(avrt);
            }
        });

        return domain::Result<void>::ok();
    }

    void stop() override {
        if (running_) {
            running_ = false;
            if (h_event_) {
                SetEvent(h_event_);
            }
            if (thread_.joinable()) {
                thread_.join();
            }
            if (h_wave_out_) {
                waveOutReset(h_wave_out_);
            }
            timeEndPeriod(1);
        }
    }

    [[nodiscard]] bool is_running() const noexcept override { return running_; }
    [[nodiscard]] double sample_rate() const noexcept override { return sample_rate_; }
    [[nodiscard]] size_t buffer_size() const noexcept override { return buffer_size_; }
    [[nodiscard]] std::string device_name() const override { return "Windows Multimedia Audio (waveOut Event-Driven)"; }

private:
    double sample_rate_{44100.0};
    size_t buffer_size_{512};
    size_t buffer_bytes_{0};
    app::AudioProcessCallback callback_;
    std::atomic<bool> running_{false};
    bool opened_{false};
    std::thread thread_;
    HWAVEOUT h_wave_out_{NULL};
    HANDLE h_event_{NULL};
    std::vector<WAVEHDR> headers_;
    std::vector<uint8_t> pcm_data_;
};
#endif

// Fallback chain: WASAPI (Low-Latency) -> WaveOut (DirectSound/Multimedia) -> Null (DAW-FR-102, ERR-AUD-001)
class AudioDeviceChain : public app::IAudioDevice {
public:
    explicit AudioDeviceChain(bool force_dummy = false, bool simulate_asio_failure = false)
        : force_dummy_(force_dummy), simulate_asio_failure_(simulate_asio_failure) {
        if (force_dummy_) {
            active_driver_ = AudioDriverType::Null;
            active_driver_name_ = "Null Device";
        }
    }

    ~AudioDeviceChain() override {
        stop();
        close();
    }

    domain::Result<void> open(double sample_rate, size_t buffer_size, app::AudioProcessCallback callback) override {
        sample_rate_ = sample_rate;
        buffer_size_ = buffer_size;
        callback_ = std::move(callback);

        if (!force_dummy_) {
            // 1. Primary: Battle-tested industry-standard miniaudio driver (WASAPI / DirectSound / WinMM)
            auto ma_dev = std::make_unique<MiniaudioDevice>();
            if (ma_dev->open(sample_rate_, buffer_size_, callback_).is_ok()) {
                active_driver_ = AudioDriverType::Miniaudio;
                active_driver_name_ = ma_dev->device_name();
                current_device_ = std::move(ma_dev);
                return domain::Result<void>::ok();
            }

#ifdef _WIN32
            // 2. Secondary Fallback: Scratch-built WASAPI driver
            auto wasapi_dev = std::make_unique<WasapiAudioDevice>();
            if (wasapi_dev->open(sample_rate_, buffer_size_, callback_).is_ok()) {
                current_device_ = std::move(wasapi_dev);
                active_driver_ = AudioDriverType::WASAPI;
                active_driver_name_ = "Windows Audio Session API (WASAPI Low-Latency)";
                return domain::Result<void>::ok();
            }

            // 3. Tertiary Fallback: High-Performance Event-Driven WaveOut
            auto win_dev = std::make_unique<WaveOutAudioDevice>();
            if (win_dev->open(sample_rate_, buffer_size_, callback_).is_ok()) {
                current_device_ = std::move(win_dev);
                active_driver_ = AudioDriverType::DirectSound;
                active_driver_name_ = "Windows Multimedia Output (Real Speakers/Headphones)";
                return domain::Result<void>::ok();
            }
#endif
        }

        // Initialize fallback device
        return fallback_to_null();
    }

    void close() override {
        if (current_device_) {
            current_device_->close();
        }
    }

    domain::Result<void> start() override {
        if (current_device_) {
            return current_device_->start();
        }
        return domain::Result<void>(domain::ErrorCode::DeviceOpenFailed);
    }

    void stop() override {
        if (current_device_) {
            current_device_->stop();
        }
    }

    [[nodiscard]] bool is_running() const noexcept override {
        return current_device_ && current_device_->is_running();
    }

    [[nodiscard]] double sample_rate() const noexcept override {
        return current_device_ ? current_device_->sample_rate() : sample_rate_;
    }
    [[nodiscard]] size_t buffer_size() const noexcept override {
        return current_device_ ? current_device_->buffer_size() : buffer_size_;
    }

    [[nodiscard]] std::string device_name() const override {
        if (current_device_) {
            return current_device_->device_name();
        }
        return active_driver_name_;
    }

    [[nodiscard]] AudioDriverType active_driver_type() const noexcept {
        return active_driver_;
    }

    [[nodiscard]] app::IAudioDevice* current_device() noexcept {
        return current_device_.get();
    }

    [[nodiscard]] const app::IAudioDevice* current_device() const noexcept {
        return current_device_.get();
    }

private:
    domain::Result<void> fallback_to_null() {
        active_driver_ = AudioDriverType::Null;
        active_driver_name_ = "Null (Fallback) Device";
        current_device_ = std::make_unique<NullAudioDevice>();
        return current_device_->open(sample_rate_, buffer_size_, callback_);
    }

    bool force_dummy_{false};
    bool simulate_asio_failure_{false};
    bool has_asio_{false};
    double sample_rate_{44100.0};
    size_t buffer_size_{512};
    app::AudioProcessCallback callback_;
    AudioDriverType active_driver_{AudioDriverType::Null};
    std::string active_driver_name_{"miniaudio (Primary Engine: WASAPI / DirectSound)"};
    std::unique_ptr<app::IAudioDevice> current_device_;
};

} // namespace digidaw::adapters::audio
