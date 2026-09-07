#pragma once

#include "../../app/ports/audio_device.hpp"
#include "../../domain/common/result.hpp"
#include "../../domain/dsp/denormal.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <iostream>

namespace digidaw::adapters::audio {

// COM GUID definitions for MinGW compatibility without requiring libksuser
#ifndef __CRT_UUID_DECL
#define __CRT_UUID_DECL(type,u1,u2,u3,u4,u5,u6,u7,u8,u9,u10,u11)
#endif

static const IID kCLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
static const IID kIID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
static const IID kIID_IAudioClient = __uuidof(IAudioClient);
static const IID kIID_IAudioRenderClient = __uuidof(IAudioRenderClient);
static const GUID kSubtypeIeeeFloat = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

class WasapiAudioDevice : public app::IAudioDevice {
public:
    WasapiAudioDevice() = default;
    ~WasapiAudioDevice() override { stop(); close(); }

    domain::Result<void> open(double sample_rate, size_t buffer_size, app::AudioProcessCallback callback) override {
        sample_rate_ = sample_rate;
        buffer_size_ = buffer_size;
        callback_ = std::move(callback);

        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        com_initialized_ = SUCCEEDED(hr);

        IMMDeviceEnumerator* enumerator = nullptr;
        hr = CoCreateInstance(kCLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
                              kIID_IMMDeviceEnumerator, reinterpret_cast<void**>(&enumerator));
        if (FAILED(hr) || !enumerator) {
            return domain::Result<void>(domain::ErrorCode::DeviceOpenFailed);
        }

        IMMDevice* endpoint = nullptr;
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &endpoint);
        enumerator->Release();
        if (FAILED(hr) || !endpoint) {
            return domain::Result<void>(domain::ErrorCode::DeviceOpenFailed);
        }

        hr = endpoint->Activate(kIID_IAudioClient, CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(&audio_client_));
        endpoint->Release();
        if (FAILED(hr) || !audio_client_) {
            return domain::Result<void>(domain::ErrorCode::DeviceOpenFailed);
        }

        WAVEFORMATEX* mix_format = nullptr;
        hr = audio_client_->GetMixFormat(&mix_format);
        if (FAILED(hr) || !mix_format) {
            audio_client_->Release();
            audio_client_ = nullptr;
            return domain::Result<void>(domain::ErrorCode::DeviceOpenFailed);
        }

        // Configure format: Prefer 32-bit Float Stereo at requested rate or hardware mix format
        is_float_ = false;
        format_ = *mix_format;
        if (mix_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            auto* ex = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix_format);
            if (IsEqualGUID(ex->SubFormat, kSubtypeIeeeFloat)) {
                is_float_ = true;
            }
        } else if (mix_format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
            is_float_ = true;
        }

        // Request low-latency buffer: buffer_size frames in 100ns units
        REFERENCE_TIME requested_duration = static_cast<REFERENCE_TIME>(
            (static_cast<double>(buffer_size_) / sample_rate_) * 10000000.0);
        if (requested_duration < 100000) requested_duration = 100000; // Minimum 10ms

        h_audio_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!h_audio_event_) {
            CoTaskMemFree(mix_format);
            audio_client_->Release();
            audio_client_ = nullptr;
            return domain::Result<void>(domain::ErrorCode::DeviceOpenFailed);
        }

#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#endif
#ifndef AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000
#endif

        DWORD stream_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        hr = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                       stream_flags,
                                       requested_duration,
                                       0,
                                       mix_format,
                                       nullptr);

        if (FAILED(hr)) {
            // Fallback without auto-convert flags
            stream_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
            hr = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                           stream_flags,
                                           requested_duration,
                                           0,
                                           mix_format,
                                           nullptr);
        }

        if (FAILED(hr)) {
            // Fallback to polling mode
            stream_flags = 0;
            hr = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                           stream_flags,
                                           requested_duration,
                                           0,
                                           mix_format,
                                           nullptr);
        }

        CoTaskMemFree(mix_format);

        if (FAILED(hr)) {
            CloseHandle(h_audio_event_);
            h_audio_event_ = nullptr;
            audio_client_->Release();
            audio_client_ = nullptr;
            return domain::Result<void>(domain::ErrorCode::DeviceOpenFailed);
        }

        if (stream_flags & AUDCLNT_STREAMFLAGS_EVENTCALLBACK) {
            audio_client_->SetEventHandle(h_audio_event_);
            event_driven_ = true;
        }

        hr = audio_client_->GetBufferSize(&buffer_frames_);
        if (FAILED(hr)) {
            close();
            return domain::Result<void>(domain::ErrorCode::DeviceOpenFailed);
        }

        hr = audio_client_->GetService(kIID_IAudioRenderClient,
                                       reinterpret_cast<void**>(&render_client_));
        if (FAILED(hr) || !render_client_) {
            close();
            return domain::Result<void>(domain::ErrorCode::DeviceOpenFailed);
        }

        opened_ = true;
        return domain::Result<void>::ok();
    }

    void close() override {
        stop();
        if (render_client_) {
            render_client_->Release();
            render_client_ = nullptr;
        }
        if (audio_client_) {
            audio_client_->Release();
            audio_client_ = nullptr;
        }
        if (h_audio_event_) {
            CloseHandle(h_audio_event_);
            h_audio_event_ = nullptr;
        }
        if (com_initialized_) {
            CoUninitialize();
            com_initialized_ = false;
        }
        opened_ = false;
    }

    domain::Result<void> start() override {
        if (!opened_ || !audio_client_) return domain::Result<void>(domain::ErrorCode::DeviceOpenFailed);
        if (running_) return domain::Result<void>::ok();

        // Prime buffer with silence before starting to avoid immediate underrun
        BYTE* initial_data = nullptr;
        if (SUCCEEDED(render_client_->GetBuffer(buffer_frames_, &initial_data))) {
            std::memset(initial_data, 0, buffer_frames_ * format_.nBlockAlign);
            render_client_->ReleaseBuffer(buffer_frames_, 0);
        }

        HRESULT hr = audio_client_->Start();
        if (FAILED(hr)) return domain::Result<void>(domain::ErrorCode::DeviceOpenFailed);

        timeBeginPeriod(1);
        running_ = true;

        thread_ = std::thread([this]() {
            // High priority & MMCSS Pro Audio setup
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
            DWORD task_index = 0;
            HMODULE avrt = LoadLibraryA("avrt.dll");
            HANDLE h_task = nullptr;
            if (avrt) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
                typedef HANDLE (WINAPI *AvSetMmThreadCharacteristicsW_t)(LPCWSTR, LPDWORD);
                auto pAvSet = reinterpret_cast<AvSetMmThreadCharacteristicsW_t>(GetProcAddress(avrt, "AvSetMmThreadCharacteristicsW"));
                if (pAvSet) {
                    h_task = pAvSet(L"Pro Audio", &task_index);
                }
#pragma GCC diagnostic pop
            }

            // FTZ and DAZ to avoid denormal CPU spikes
            domain::dsp::enable_ftz_daz();

            domain::OwningAudioBuffer float_buf(buffer_size_);

            // Lock-free Ring Buffer (FIFO) to bridge DAW block size (e.g. 512) and WASAPI packet size
            constexpr size_t kFifoCapacity = 32768;
            std::vector<float> fifo_l(kFifoCapacity, 0.0f);
            std::vector<float> fifo_r(kFifoCapacity, 0.0f);
            size_t fifo_head = 0;
            size_t fifo_tail = 0;
            size_t fifo_count = 0;

            // Pre-fill FIFO with 4 blocks of silence to provide jitter absorption headroom
            const size_t prefill_frames = std::min(kFifoCapacity / 2, buffer_size_ * 4);
            for (size_t i = 0; i < prefill_frames; ++i) {
                fifo_l[fifo_head] = 0.0f;
                fifo_r[fifo_head] = 0.0f;
                fifo_head = (fifo_head + 1) & (kFifoCapacity - 1);
                fifo_count++;
            }

            while (running_) {
                if (event_driven_) {
                    WaitForSingleObject(h_audio_event_, 20);
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                if (!running_) break;

                UINT32 padding = 0;
                if (FAILED(audio_client_->GetCurrentPadding(&padding))) continue;

                UINT32 available_frames = (buffer_frames_ > padding) ? (buffer_frames_ - padding) : 0;
                if (available_frames == 0) continue;

                // Ensure FIFO has sufficient frames to satisfy WASAPI's packet request
                while (fifo_count < available_frames && running_) {
                    auto view = float_buf.view();
                    view.clear();
                    if (callback_) {
                        callback_(view);
                    }
                    const size_t frames_in = view.frames;
                    const float* vl = view.left;
                    const float* vr = view.right;
                    for (size_t f = 0; f < frames_in; ++f) {
                        if (fifo_count < kFifoCapacity) {
                            fifo_l[fifo_head] = vl ? vl[f] : 0.0f;
                            fifo_r[fifo_head] = vr ? vr[f] : 0.0f;
                            fifo_head = (fifo_head + 1) & (kFifoCapacity - 1);
                            fifo_count++;
                        }
                    }
                }

                if (!running_) break;

                BYTE* render_buf = nullptr;
                // In WASAPI shared mode, request the exact available packet size
                if (FAILED(render_client_->GetBuffer(available_frames, &render_buf)) || !render_buf) {
                    continue;
                }

                const UINT32 channels = format_.nChannels;
                if (is_float_) {
                    float* out_f = reinterpret_cast<float*>(render_buf);
                    for (UINT32 f = 0; f < available_frames; ++f) {
                        float sl = 0.0f, sr = 0.0f;
                        if (fifo_count > 0) {
                            sl = fifo_l[fifo_tail];
                            sr = fifo_r[fifo_tail];
                            fifo_tail = (fifo_tail + 1) & (kFifoCapacity - 1);
                            fifo_count--;
                        }
                        out_f[f * channels + 0] = std::clamp(sl, -1.0f, 1.0f);
                        if (channels > 1) {
                            out_f[f * channels + 1] = std::clamp(sr, -1.0f, 1.0f);
                        }
                    }
                } else {
                    int16_t* out_s16 = reinterpret_cast<int16_t*>(render_buf);
                    for (UINT32 f = 0; f < available_frames; ++f) {
                        float sl = 0.0f, sr = 0.0f;
                        if (fifo_count > 0) {
                            sl = fifo_l[fifo_tail];
                            sr = fifo_r[fifo_tail];
                            fifo_tail = (fifo_tail + 1) & (kFifoCapacity - 1);
                            fifo_count--;
                        }
                        out_s16[f * channels + 0] = static_cast<int16_t>(std::clamp(sl, -1.0f, 1.0f) * 32767.0f);
                        if (channels > 1) {
                            out_s16[f * channels + 1] = static_cast<int16_t>(std::clamp(sr, -1.0f, 1.0f) * 32767.0f);
                        }
                    }
                }

                render_client_->ReleaseBuffer(available_frames, 0);
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
            if (h_audio_event_) {
                SetEvent(h_audio_event_);
            }
            if (thread_.joinable()) {
                thread_.join();
            }
            if (audio_client_) {
                audio_client_->Stop();
            }
            timeEndPeriod(1);
        }
    }

    [[nodiscard]] bool is_running() const noexcept override { return running_; }
    [[nodiscard]] double sample_rate() const noexcept override { return sample_rate_; }
    [[nodiscard]] size_t buffer_size() const noexcept override { return buffer_size_; }
    [[nodiscard]] std::string device_name() const override {
        return "Windows Audio Session API (WASAPI Low-Latency)";
    }

private:
    double sample_rate_{44100.0};
    size_t buffer_size_{512};
    UINT32 buffer_frames_{0};
    app::AudioProcessCallback callback_;
    std::atomic<bool> running_{false};
    bool opened_{false};
    bool event_driven_{false};
    bool is_float_{true};
    bool com_initialized_{false};
    HANDLE h_audio_event_{nullptr};
    IAudioClient* audio_client_{nullptr};
    IAudioRenderClient* render_client_{nullptr};
    WAVEFORMATEX format_{};
    std::thread thread_;
};

} // namespace digidaw::adapters::audio
#endif
