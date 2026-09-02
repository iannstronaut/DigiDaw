#pragma once

#include "../../domain/buffer/audio_buffer.hpp"
#include "../../domain/common/result.hpp"
#include <functional>
#include <string>

namespace digidaw::app {

using AudioProcessCallback = std::function<void(domain::AudioBufferView& out_buffer)>;

class IAudioDevice {
public:
    virtual ~IAudioDevice() = default;

    virtual domain::Result<void> open(double sample_rate, size_t buffer_size, AudioProcessCallback callback) = 0;
    virtual void close() = 0;

    virtual domain::Result<void> start() = 0;
    virtual void stop() = 0;

    [[nodiscard]] virtual bool is_running() const noexcept = 0;
    [[nodiscard]] virtual double sample_rate() const noexcept = 0;
    [[nodiscard]] virtual size_t buffer_size() const noexcept = 0;
    [[nodiscard]] virtual std::string device_name() const = 0;
};

} // namespace digidaw::app
