#pragma once

#include "../../domain/common/result.hpp"
#include <string>
#include <unordered_map>
#include <cstdint>

namespace digidaw::adapters::plugins {

// Compatibility flags matching DAW-FR-403 & EV-007
enum PluginCompatFlags : uint32_t {
    CompatNone                = 0,
    CompatDontProcessWhileOpen = 1 << 0, // Bypass audio processing when GUI editor opens
    CompatNeedIdleCall        = 1 << 1, // Plugin requires regular idle / pump messages
    CompatForceDpiUnaware     = 1 << 2, // Plugin crashes on HiDPI displays
    CompatMonoOnly            = 1 << 3, // Force mono input/output configuration
    CompatDisableMulticore    = 1 << 4, // Plugin fails when called from multiple thread IDs
    CompatAlwaysRunInBridge   = 1 << 5  // Force out-of-process execution for high-crash plugins
};

struct PluginCompatEntry {
    std::string uid;
    std::string name;
    uint32_t flags{CompatNone};
};

class PluginCompatStore {
public:
    PluginCompatStore() {
        // Known factory compatibility workarounds (EV-007)
        set_flags("vst.vendor.legacy_synth", CompatAlwaysRunInBridge | CompatForceDpiUnaware);
        set_flags("vst.vendor.buggy_filter", CompatDontProcessWhileOpen);
        set_flags("vst.vendor.crashy_sampler", CompatAlwaysRunInBridge | CompatDisableMulticore);
    }

    void set_flags(const std::string& uid, uint32_t flags) {
        db_[uid] = flags;
    }

    [[nodiscard]] uint32_t get_flags(const std::string& uid) const noexcept {
        auto it = db_.find(uid);
        if (it != db_.end()) {
            return it->second;
        }
        return CompatNone;
    }

    [[nodiscard]] bool has_flag(const std::string& uid, PluginCompatFlags flag) const noexcept {
        return (get_flags(uid) & static_cast<uint32_t>(flag)) != 0;
    }

private:
    std::unordered_map<std::string, uint32_t> db_;
};

} // namespace digidaw::adapters::plugins
