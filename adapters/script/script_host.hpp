#pragma once

#include "../../domain/common/result.hpp"
#include "../../sdk/digidaw_c_api.h"
#include <string>
#include <functional>

namespace digidaw::adapters::script {

using ScriptCallback = std::function<void(DigiDawEngineHandle)>;

class ScriptHost {
public:
    explicit ScriptHost(DigiDawEngineHandle engine) : engine_(engine) {}

    // Execute script logic inside exception containment boundary (DAW-FR-803)
    domain::Result<void> execute_script_safe(const std::string& script_name, ScriptCallback script_func) {
        if (!engine_ || !script_func) {
            return domain::Result<void>(domain::ErrorCode::InvalidArgument);
        }

        try {
            script_func(engine_);
            return domain::Result<void>::ok();
        } catch (const std::exception& ex) {
            // Contain exception: script crash must NEVER crash host!
            last_error_ = ex.what();
            disabled_scripts_.push_back(script_name);
            return domain::Result<void>(domain::ErrorCode::RuntimeException);
        } catch (...) {
            last_error_ = "Unknown unhandled script exception";
            disabled_scripts_.push_back(script_name);
            return domain::Result<void>(domain::ErrorCode::RuntimeException);
        }
    }

    [[nodiscard]] const std::string& last_error() const noexcept { return last_error_; }
    [[nodiscard]] const std::vector<std::string>& disabled_scripts() const noexcept { return disabled_scripts_; }

private:
    DigiDawEngineHandle engine_;
    std::string last_error_{""};
    std::vector<std::string> disabled_scripts_;
};

} // namespace digidaw::adapters::script
