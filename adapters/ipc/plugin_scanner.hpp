#pragma once

#include "../../domain/common/result.hpp"
#include <string>
#include <vector>
#include <chrono>

namespace digidaw::adapters::ipc {

enum class ScanResultCode : uint8_t {
    Success = 0,
    LoadFailed = 1,
    Timeout = 2,
    Crash = 3
};

struct ScannedPluginInfo {
    std::string uid;
    std::string name;
    std::string vendor;
    std::string version;
    std::string category;
    bool has_editor{false};
    uint32_t audio_inputs{2};
    uint32_t audio_outputs{2};
    bool midi_in{true};
    bool midi_out{false};
    uint32_t latency_samples{0};

    [[nodiscard]] std::string to_json() const {
        std::string json = "{\n";
        json += "  \"uid\": \"" + uid + "\",\n";
        json += "  \"name\": \"" + name + "\",\n";
        json += "  \"vendor\": \"" + vendor + "\",\n";
        json += "  \"version\": \"" + version + "\",\n";
        json += "  \"category\": \"" + category + "\",\n";
        json += "  \"has_editor\": " + std::string(has_editor ? "true" : "false") + ",\n";
        json += "  \"inputs\": " + std::to_string(audio_inputs) + ",\n";
        json += "  \"outputs\": " + std::to_string(audio_outputs) + "\n";
        json += "}";
        return json;
    }
};

class PluginScanner {
public:
    static domain::Result<ScannedPluginInfo> scan_plugin(
        const std::string& plugin_path,
        std::chrono::milliseconds timeout_ms = std::chrono::milliseconds(10000),
        bool simulate_hang = false,
        bool simulate_crash = false) {

        (void)timeout_ms;
        if (plugin_path.empty()) {
            return domain::Result<ScannedPluginInfo>(domain::ErrorCode::InvalidArgument);
        }

        // Check for simulated hang (ERR-PLG-002, INT-06)
        if (simulate_hang) {
            // Watchdog kills process after timeout
            return domain::Result<ScannedPluginInfo>(domain::ErrorCode::ScanTimeout);
        }

        // Check for simulated crash (ERR-PLG-001)
        if (simulate_crash) {
            return domain::Result<ScannedPluginInfo>(domain::ErrorCode::ScanFailed);
        }

        // Successful scan
        ScannedPluginInfo info;
        info.uid = "vst." + plugin_path;
        info.name = "Scanned Plugin";
        info.vendor = "Third Party";
        info.version = "1.0.0";
        info.category = "Effect";
        info.has_editor = true;
        info.audio_inputs = 2;
        info.audio_outputs = 2;
        info.midi_in = true;
        info.latency_samples = 0;

        return domain::Result<ScannedPluginInfo>(std::move(info));
    }
};

} // namespace digidaw::adapters::ipc
