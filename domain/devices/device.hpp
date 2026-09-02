#pragma once

#include "../buffer/audio_buffer.hpp"
#include "../common/result.hpp"
#include <string>
#include <vector>
#include <span>
#include <cstdint>

namespace digidaw::domain {

using DeviceUid = std::string;

enum class DeviceCategory : uint8_t {
    Generator, // Instrument, Synth, Sampler
    Effect     // EQ, Compressor, Reverb, Delay, Distortion
};

struct ParamDesc {
    uint32_t id{0};
    std::string name{};
    float default_val{0.5f};
    float min_val{0.0f};
    float max_val{1.0f};
    std::string unit{""};
};

class IDevice {
public:
    virtual ~IDevice() = default;

    [[nodiscard]] virtual DeviceUid uid() const = 0;
    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual DeviceCategory category() const = 0;

    virtual void prepare(double sample_rate, size_t max_block_size) = 0;
    virtual void reset() = 0;

    // Real-time audio processing: must not allocate, block, or throw
    virtual void process(AudioBufferView& buffer, std::span<const MidiEvent> midi) = 0;

    virtual void set_parameter(uint32_t param_id, float normalized_value) = 0;
    [[nodiscard]] virtual float get_parameter(uint32_t param_id) const = 0;

    [[nodiscard]] virtual std::vector<ParamDesc> parameters() const = 0;

    // State persistence
    [[nodiscard]] virtual std::vector<uint8_t> save_state() const = 0;
    virtual Result<void> load_state(std::span<const uint8_t> data) = 0;
};

} // namespace digidaw::domain
