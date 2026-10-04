#include "../test_framework.hpp"
#include "../../adapters/audio/miniaudio_driver.hpp"
#include "../../adapters/audio/audio_device_chain.hpp"
#include <thread>
#include <chrono>
#include <atomic>
#include <limits>
#include <cmath>
#include <stdexcept>

using namespace digidaw::domain;
using namespace digidaw::adapters::audio;

TEST_CASE(UnitMiniaudio, MiniaudioDeviceCreation) {
    MiniaudioDevice device;
    ASSERT_FALSE(device.is_running());
    ASSERT_NEAR(device.sample_rate(), 44100.0, 0.001);
    ASSERT_EQ(device.buffer_size(), static_cast<size_t>(512));
    ASSERT_TRUE(device.device_name().find("miniaudio") != std::string::npos);
}

TEST_CASE(UnitMiniaudio, MiniaudioDeviceInvalidParameters) {
    MiniaudioDevice device;

    // Sample rate <= 0
    auto res_neg = device.open(-44100.0, 512, [](AudioBufferView& out) { out.clear(); });
    ASSERT_TRUE(res_neg.is_error());
    ASSERT_EQ(res_neg.error().code, ErrorCode::SampleRateMismatch);

    auto res_zero = device.open(0.0, 512, [](AudioBufferView& out) { out.clear(); });
    ASSERT_TRUE(res_zero.is_error());
    ASSERT_EQ(res_zero.error().code, ErrorCode::SampleRateMismatch);

    // Sample rate NaN or Inf
    auto res_nan = device.open(std::numeric_limits<double>::quiet_NaN(), 512, [](AudioBufferView& out) { out.clear(); });
    ASSERT_TRUE(res_nan.is_error());
    ASSERT_EQ(res_nan.error().code, ErrorCode::SampleRateMismatch);

    auto res_inf = device.open(std::numeric_limits<double>::infinity(), 512, [](AudioBufferView& out) { out.clear(); });
    ASSERT_TRUE(res_inf.is_error());
    ASSERT_EQ(res_inf.error().code, ErrorCode::SampleRateMismatch);
}

TEST_CASE(UnitMiniaudio, MiniaudioDeviceOpenClose) {
    MiniaudioDevice device;
    std::atomic<size_t> callback_count{0};

    auto res = device.open(44100.0, 512, [&](AudioBufferView& out) {
        callback_count.fetch_add(1, std::memory_order_relaxed);
        ASSERT_TRUE(out.left != nullptr);
        ASSERT_TRUE(out.right != nullptr);
        ASSERT_TRUE(out.frames > 0);
        for (size_t i = 0; i < out.frames; ++i) {
            out.left[i] = 0.0f;
            out.right[i] = 0.0f;
        }
    });

    if (res.is_ok()) {
        ASSERT_TRUE(device.device_name().find("miniaudio") != std::string::npos);
        auto start_res = device.start();
        ASSERT_OK(start_res);
        ASSERT_TRUE(device.is_running());

        // Wait briefly for audio thread to process at least one block
        std::this_thread::sleep_for(std::chrono::milliseconds(30));

        device.stop();
        ASSERT_FALSE(device.is_running());
        device.close();
        ASSERT_FALSE(device.is_running());
        ASSERT_TRUE(callback_count.load() > 0);
    } else {
        // Headless environment without sound card fallback
        ASSERT_EQ(res.error().code, ErrorCode::DeviceOpenFailed);
    }
}

TEST_CASE(UnitMiniaudio, MiniaudioDeviceMultipleStartStopCycles) {
    MiniaudioDevice device;
    auto res = device.open(44100.0, 512, [](AudioBufferView& out) {
        out.clear();
    });

    if (res.is_ok()) {
        // First cycle
        ASSERT_OK(device.start());
        ASSERT_TRUE(device.is_running());
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        device.stop();
        ASSERT_FALSE(device.is_running());

        // Second cycle without reopen
        ASSERT_OK(device.start());
        ASSERT_TRUE(device.is_running());
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        device.stop();
        ASSERT_FALSE(device.is_running());

        device.close();
    }
}

TEST_CASE(UnitMiniaudio, MiniaudioDeviceReopenWithDifferentSettings) {
    MiniaudioDevice device;
    auto res1 = device.open(44100.0, 256, [](AudioBufferView& out) {
        out.clear();
    });

    if (res1.is_ok()) {
        ASSERT_OK(device.start());
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        device.stop();

        // Re-open with 48000 Hz and 1024 buffer size
        auto res2 = device.open(48000.0, 1024, [](AudioBufferView& out) {
            out.clear();
        });
        ASSERT_OK(res2);
        ASSERT_OK(device.start());
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        device.stop();
        device.close();
        ASSERT_FALSE(device.is_running());
    }
}

TEST_CASE(UnitMiniaudio, MiniaudioDeviceCallbackExceptionContainment) {
    MiniaudioDevice device;
    bool exception_thrown = false;

    // Direct block processing test: when callback throws, process_audio_block catches and silences
    auto res = device.open(44100.0, 256, [&](AudioBufferView& /*out*/) {
        exception_thrown = true;
        throw std::runtime_error("Simulated DSP exception in plugin");
    });

    if (res.is_ok()) {
        std::vector<float> output_buffer(256 * 2, 99.0f);
        device.process_audio_block_for_test(output_buffer.data(), 256);
        ASSERT_TRUE(exception_thrown);

        // Verification: output buffer must be silenced (zeroes), not containing unhandled trash or NaN
        for (size_t i = 0; i < 256 * 2; ++i) {
            ASSERT_NEAR(output_buffer[i], 0.0f, 0.0001f);
        }
        device.close();
    }
}

TEST_CASE(UnitMiniaudio, MiniaudioDeviceCallbackNanAndInfSanitization) {
    MiniaudioDevice device;

    auto res = device.open(44100.0, 4, [](AudioBufferView& out) {
        out.left[0] = std::numeric_limits<float>::quiet_NaN();
        out.right[0] = std::numeric_limits<float>::infinity();

        out.left[1] = -std::numeric_limits<float>::infinity();
        out.right[1] = 5.0f; // Excessive positive amplitude

        out.left[2] = -3.5f; // Excessive negative amplitude
        out.right[2] = 0.5f; // Normal valid amplitude

        out.left[3] = 0.0f;
        out.right[3] = -0.75f;
    });

    if (res.is_ok()) {
        std::vector<float> output(4 * 2, 0.0f);
        device.process_audio_block_for_test(output.data(), 4);

        // Sample 0: NaN and +Inf sanitized to 0.0f
        ASSERT_FALSE(std::isnan(output[0]));
        ASSERT_FALSE(std::isinf(output[0]));
        ASSERT_NEAR(output[0], 0.0f, 0.0001f);

        ASSERT_FALSE(std::isnan(output[1]));
        ASSERT_FALSE(std::isinf(output[1]));
        ASSERT_NEAR(output[1], 0.0f, 0.0001f);

        // Sample 1: -Inf sanitized to 0.0f; 5.0f clamped to +1.0f
        ASSERT_NEAR(output[2], 0.0f, 0.0001f);
        ASSERT_NEAR(output[3], 1.0f, 0.0001f);

        // Sample 2: -3.5f clamped to -1.0f; 0.5f kept as 0.5f
        ASSERT_NEAR(output[4], -1.0f, 0.0001f);
        ASSERT_NEAR(output[5], 0.5f, 0.0001f);

        // Sample 3: 0.0f kept; -0.75f kept
        ASSERT_NEAR(output[6], 0.0f, 0.0001f);
        ASSERT_NEAR(output[7], -0.75f, 0.0001f);

        device.close();
    }
}

TEST_CASE(UnitMiniaudio, AudioDeviceChainPrioritizesMiniaudio) {
    AudioDeviceChain chain(false); // force_dummy = false
    std::atomic<size_t> frames_rendered{0};

    auto res = chain.open(44100.0, 512, [&](AudioBufferView& out) {
        frames_rendered.fetch_add(out.frames, std::memory_order_relaxed);
        out.clear();
    });

    ASSERT_OK(res);
    ASSERT_OK(chain.start());
    ASSERT_TRUE(chain.is_running());

    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // If hardware is present, miniaudio must be selected as the primary driver
    if (chain.active_driver_type() == AudioDriverType::Miniaudio) {
        ASSERT_TRUE(chain.device_name().find("miniaudio") != std::string::npos);
        ASSERT_TRUE(chain.current_device() != nullptr);
    }

    chain.stop();
    ASSERT_FALSE(chain.is_running());
    chain.close();
}

TEST_CASE(UnitMiniaudio, AudioDeviceChainForceDummy) {
    AudioDeviceChain chain(true); // force_dummy = true
    std::atomic<size_t> frames_rendered{0};

    ASSERT_EQ(chain.active_driver_type(), AudioDriverType::Null);
    ASSERT_TRUE(chain.device_name().find("Null") != std::string::npos);

    auto res = chain.open(44100.0, 512, [&](AudioBufferView& out) {
        frames_rendered.fetch_add(out.frames, std::memory_order_relaxed);
        out.clear();
    });

    ASSERT_OK(res);
    ASSERT_EQ(chain.active_driver_type(), AudioDriverType::Null);
    ASSERT_TRUE(chain.device_name().find("Null") != std::string::npos);

    ASSERT_OK(chain.start());
    ASSERT_TRUE(chain.is_running());

    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    chain.stop();
    ASSERT_FALSE(chain.is_running());
    chain.close();
    ASSERT_TRUE(frames_rendered.load() > 0);
}

TEST_CASE(UnitMiniaudio, AudioDeviceChainSampleRateAndBufferDelegation) {
    AudioDeviceChain chain(false);

    auto res = chain.open(48000.0, 256, [](AudioBufferView& out) {
        out.clear();
    });

    ASSERT_OK(res);
    ASSERT_NEAR(chain.sample_rate(), 48000.0, 100.0); // Within reasonable hardware SRC tolerance
    ASSERT_TRUE(chain.buffer_size() > 0);

    chain.close();
}
