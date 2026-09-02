#pragma once

#include "../../domain/devices/device.hpp"
#include "../../domain/common/result.hpp"
#include "../../adapters/plugins/synth_3xosc.hpp"
#include "../../adapters/plugins/sampler_device.hpp"
#include "../../adapters/plugins/drum_sampler_device.hpp"
#include "../../adapters/plugins/parametric_eq.hpp"
#include "../../adapters/plugins/compressor_device.hpp"
#include "../../adapters/plugins/delay_device.hpp"
#include "../../adapters/plugins/reverb_device.hpp"
#include "../../adapters/plugins/limiter_device.hpp"
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
            "core.fx.parametric_eq", "Parametric EQ", domain::DeviceCategory::Effect,
            []() { return std::make_shared<adapters::plugins::ParametricEQ>(); });

        register_factory(
            "core.fx.compressor", "Stereo Compressor", domain::DeviceCategory::Effect,
            []() { return std::make_shared<adapters::plugins::CompressorDevice>(); });

        register_factory(
            "core.fx.delay", "Stereo Delay", domain::DeviceCategory::Effect,
            []() { return std::make_shared<adapters::plugins::DelayDevice>(); });

        register_factory(
            "core.fx.reverb", "Algorithmic Reverb", domain::DeviceCategory::Effect,
            []() { return std::make_shared<adapters::plugins::ReverbDevice>(); });

        register_factory(
            "core.fx.limiter", "Master Limiter", domain::DeviceCategory::Effect,
            []() { return std::make_shared<adapters::plugins::LimiterDevice>(); });
    }

    std::unordered_map<domain::DeviceUid, PluginMetadata> registry_;
    std::unordered_map<domain::DeviceUid, DeviceFactory> factories_;
};

} // namespace digidaw::app
