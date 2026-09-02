#pragma once

#include "../../app/ports/config_store.hpp"
#include "../../domain/common/result.hpp"
#include <unordered_map>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace digidaw::adapters::config {

class FileConfigStore : public app::IConfigStore {
public:
    explicit FileConfigStore(std::string config_filepath)
        : filepath_(std::move(config_filepath)) {
        load();
    }

    [[nodiscard]] std::string get_string(const std::string& section, const std::string& key, const std::string& default_val = "") override {
        const std::string full_key = section + "." + key;
        auto it = values_.find(full_key);
        if (it != values_.end()) {
            return it->second;
        }
        return default_val;
    }

    domain::Result<void> set_string(const std::string& section, const std::string& key, const std::string& value) override {
        const std::string full_key = section + "." + key;
        values_[full_key] = value;
        return flush();
    }

    [[nodiscard]] int64_t get_int(const std::string& section, const std::string& key, int64_t default_val = 0) override {
        std::string str_val = get_string(section, key);
        if (str_val.empty()) return default_val;
        try {
            return std::stoll(str_val);
        } catch (...) {
            return default_val;
        }
    }

    domain::Result<void> set_int(const std::string& section, const std::string& key, int64_t value) override {
        return set_string(section, key, std::to_string(value));
    }

    // Launch counter parity EV-030 / DESKTOP-FR-008
    int64_t increment_launch_counter() {
        int64_t count = get_int("General", "RunCounter", 0) + 1;
        set_int("General", "RunCounter", count);
        return count;
    }

    domain::Result<void> flush() override {
        if (filepath_.empty()) return domain::Result<void>::ok();

        std::ofstream f(filepath_, std::ios::trunc);
        if (!f.is_open()) {
            return domain::Result<void>(domain::ErrorCode::FileLocked);
        }

        for (const auto& [k, v] : values_) {
            f << k << "=" << v << "\n";
        }
        f.flush();
        if (f.fail()) {
            return domain::Result<void>(domain::ErrorCode::AutosaveFailed);
        }
        return domain::Result<void>::ok();
    }

private:
    void load() {
        if (filepath_.empty()) return;
        std::ifstream f(filepath_);
        if (!f.is_open()) return;

        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            auto sep = line.find('=');
            if (sep != std::string::npos) {
                std::string k = line.substr(0, sep);
                std::string v = line.substr(sep + 1);
                values_[k] = v;
            }
        }
    }

    std::string filepath_;
    std::unordered_map<std::string, std::string> values_;
};

} // namespace digidaw::adapters::config
