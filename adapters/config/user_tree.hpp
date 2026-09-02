#pragma once

#include "../../domain/common/result.hpp"
#include <string>
#include <filesystem>
#include <vector>

namespace digidaw::adapters::config {

class UserTreeManager {
public:
    static domain::Result<void> ensure_user_tree(const std::string& base_dir) {
        if (base_dir.empty()) {
            return domain::Result<void>(domain::ErrorCode::InvalidArgument);
        }

        const std::vector<std::string> subdirs = {
            "Projects",
            "Presets",
            "Audio",
            "Settings",
            "Support/CrashDumps",
            "Support/Logs",
            "Downloads"
        };

        std::error_code ec;
        for (const auto& sub : subdirs) {
            auto full_path = std::filesystem::path(base_dir) / sub;
            if (!std::filesystem::exists(full_path, ec)) {
                if (!std::filesystem::create_directories(full_path, ec) && ec) {
                    return domain::Result<void>(domain::ErrorCode::UserTreeMissing);
                }
            }
        }

        return domain::Result<void>::ok();
    }
};

} // namespace digidaw::adapters::config
