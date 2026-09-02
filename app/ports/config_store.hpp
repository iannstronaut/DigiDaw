#pragma once

#include "../../domain/common/result.hpp"
#include <string>
#include <cstdint>

namespace digidaw::app {

class IConfigStore {
public:
    virtual ~IConfigStore() = default;

    [[nodiscard]] virtual std::string get_string(const std::string& section, const std::string& key, const std::string& default_val = "") = 0;
    virtual domain::Result<void> set_string(const std::string& section, const std::string& key, const std::string& value) = 0;

    [[nodiscard]] virtual int64_t get_int(const std::string& section, const std::string& key, int64_t default_val = 0) = 0;
    virtual domain::Result<void> set_int(const std::string& section, const std::string& key, int64_t value) = 0;

    virtual domain::Result<void> flush() = 0;
};

} // namespace digidaw::app
