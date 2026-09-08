#pragma once

#include "../../domain/devices/device.hpp"
#include "../../domain/common/result.hpp"
#include "../../adapters/plugins/synth_3xosc.hpp"
#include "../../adapters/plugins/sampler_device.hpp"
#include "../../adapters/plugins/drum_sampler_device.hpp"
#include "../../adapters/plugins/audioclip_device.hpp"
#include "../../adapters/plugins/limiter_device.hpp"
#include "../../adapters/plugins/xaudio_devices.hpp"
#include "../../adapters/plugins/xosc_device.hpp"
#include <unordered_map>
#include <functional>
#include <memory>
#include <vector>

namespace digidaw::app {

using DeviceFactory = std::function<std::shared_ptr<domain::IDevice>()>;

struct PluginMetadata {
    domain::DeviceUid uid;
    std::string name;
    domain::DeviceCategory category;
    bool is_native{true};
    bool is_failed{false}; // Marked failed if crashed (DAW-NFR-005, ERR-PLG-003)
};

class PluginManager {
public:
    PluginManager() {
        register_builtin_devices();
    }

    void register_factory(domain::DeviceUid uid, std::string name, domain::DeviceCategory cat, DeviceFactory factory) {
        registry_[uid] = PluginMetadata{uid, std::move(name), cat, true, false};
        factories_[uid] = std::move(factory);
    }

    [[nodiscard]] domain::Result<std::shared_ptr<domain::IDevice>> instantiate(const domain::DeviceUid& uid) {
        auto it = registry_.find(uid);
        if (it == registry_.end()) {
            return domain::Result<std::shared_ptr<domain::IDevice>>(domain::ErrorCode::MissingPlugin);
        }

        if (it->second.is_failed) {
            return domain::Result<std::shared_ptr<domain::IDevice>>(domain::ErrorCode::NodeBypassed);
        }

        auto fact_it = factories_.find(uid);
        if (fact_it != factories_.end()) {
            auto dev = fact_it->second();
            return domain::Result<std::shared_ptr<domain::IDevice>>(dev);
        }

        return domain::Result<std::shared_ptr<domain::IDevice>>(domain::ErrorCode::ScanFailed);
    }

    void mark_plugin_failed(const domain::DeviceUid& uid) {
        auto it = registry_.find(uid);
        if (it != registry_.end()) {
            it->second.is_failed = true;
        }
    }

    [[nodiscard]] std::vector<PluginMetadata> available_plugins() const {
        std::vector<PluginMetadata> list;
        for (const auto& [_, meta] : registry_) {
            list.push_back(meta);
        }
        return list;
    }

private:
    void register_builtin_devices() {
        register_factory(
            "core.generator.3xosc", "3xOsc Synth", domain::DeviceCategory::Generator,
            []() { return std::make_shared<adapters::plugins::Synth3xOsc>(); });

        register_factory(
            "core.generator.sampler", "DirectWave Sampler", domain::DeviceCategory::Generator,
            []() { return std::make_shared<adapters::plugins::SamplerDevice>(); });

        register_factory(
            "core.generator.drum_sampler", "FPC Drum Machine", domain::DeviceCategory::Generator,
            []() { return std::make_shared<adapters::plugins::DrumSamplerDevice>(); });

        register_factory(
            "core.generator.audioclip", "Clipper", domain::DeviceCategory::Generator,
            []() { return std::make_shared<adapters::plugins::AudioClipDevice>(); });

        // Backward compatibility mapping for projects requesting legacy EQ
        register_factory(
            "core.fx.parametric_eq", "X-Eq (Default EQ)", domain::DeviceCategory::Effect,
            []() { return std::make_shared<adapters::plugins::XEqDevice>(); });

        register_factory(
            "core.fx.limiter", "Master Limiter", domain::DeviceCategory::Effect,
            []() { return std::make_shared<adapters::plugins::LimiterDevice>(); });

        // --- XOSC Ported Instrument (Built-in) ---
        register_factory(
            "core.generator.xosc", "XOSC", domain::DeviceCategory::Generator,
            []() { return std::make_shared<adapters::plugins::XOSCDevice>(); });

        // --- XAudio Ported Devices (Built-in) ---
        register_factory(
            "core.generator.x_synth", "X-Synth", domain::DeviceCategory::Generator,
            []() { return std::make_shared<adapters::plugins::XSynthDevice>(); });

        register_factory(
            "core.fx.x_eq", "X-Eq", domain::DeviceCategory::Effect,
            []() { return std::make_shared<adapters::plugins::XEqDevice>(); });

        register_factory(
            "core.fx.x_compressor", "X-Compressor", domain::DeviceCategory::Effect,
            []() { return std::make_shared<adapters::plugins::XCompressorDevice>(); });

        register_factory(
            "core.fx.x_multiband", "X-Multiband", domain::DeviceCategory::Effect,
            []() { return std::make_shared<adapters::plugins::XMultibandDevice>(); });

        register_factory(
            "core.fx.x_reverb", "X-Reverb", domain::DeviceCategory::Effect,
            []() { return std::make_shared<adapters::plugins::XReverbDevice>(); });

        register_factory(
            "core.fx.x_distortion", "X-Distortion", domain::DeviceCategory::Effect,
            []() { return std::make_shared<adapters::plugins::XDistortionDevice>(); });

        register_factory(
            "core.fx.x_limiter", "X-Limiter", domain::DeviceCategory::Effect,
            []() { return std::make_shared<adapters::plugins::XLimiterDevice>(); });

        // XAudio Aliases
        register_factory(
            "xaudio.generator.synth", "X-Synth", domain::DeviceCategory::Generator,
            []() { return std::make_shared<adapters::plugins::XSynthDevice>(); });
        register_factory(
            "xaudio.fx.eq", "X-Eq", domain::DeviceCategory::Effect,
            []() { return std::make_shared<adapters::plugins::XEqDevice>(); });
        register_factory(
            "xaudio.fx.compressor", "X-Compressor", domain::DeviceCategory::Effect,
            []() { return std::make_shared<adapters::plugins::XCompressorDevice>(); });
        register_factory(
            "xaudio.fx.multiband", "X-Multiband", domain::DeviceCategory::Effect,
            []() { return std::make_shared<adapters::plugins::XMultibandDevice>(); });
        register_factory(
            "xaudio.fx.reverb", "X-Reverb", domain::DeviceCategory::Effect,
            []() { return std::make_shared<adapters::plugins::XReverbDevice>(); });
        register_factory(
            "xaudio.fx.distortion", "X-Distortion", domain::DeviceCategory::Effect,
            []() { return std::make_shared<adapters::plugins::XDistortionDevice>(); });
        register_factory(
            "xaudio.fx.limiter", "X-Limiter", domain::DeviceCategory::Effect,
            []() { return std::make_shared<adapters::plugins::XLimiterDevice>(); });
    }

    std::unordered_map<domain::DeviceUid, PluginMetadata> registry_;
    std::unordered_map<domain::DeviceUid, DeviceFactory> factories_;
};

} // namespace digidaw::app
