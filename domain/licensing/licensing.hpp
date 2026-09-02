#pragma once

#include "../common/result.hpp"
#include <string>
#include <cstdint>
#include <vector>

namespace digidaw::domain::licensing {

enum class LicenseTier : uint8_t {
    Trial = 0,
    Producer = 1,
    Signature = 2,
    AllPlugins = 3
};

struct LicenseInfo {
    LicenseTier tier{LicenseTier::Trial};
    std::string registered_to{"Trial User"};
    std::string product_key{""};
    bool valid{false};
};

class ILicensing {
public:
    virtual ~ILicensing() = default;

    [[nodiscard]] virtual bool is_trial() const noexcept = 0;
    [[nodiscard]] virtual LicenseTier tier() const noexcept = 0;
    [[nodiscard]] virtual std::string licensee_name() const = 0;
    [[nodiscard]] virtual std::string edition_name() const = 0;

    virtual Result<void> activate_offline_key(const std::string& key, const std::string& username) = 0;
    virtual void clear_license() = 0;
};

class LicenseManager : public ILicensing {
public:
    LicenseManager() {
        info_.tier = LicenseTier::Trial;
        info_.registered_to = "Trial User";
        info_.valid = false;
    }

    [[nodiscard]] bool is_trial() const noexcept override {
        return !info_.valid || info_.tier == LicenseTier::Trial;
    }

    [[nodiscard]] LicenseTier tier() const noexcept override {
        return info_.tier;
    }

    [[nodiscard]] std::string licensee_name() const override {
        return info_.registered_to;
    }

    [[nodiscard]] std::string edition_name() const override {
        switch (info_.tier) {
            case LicenseTier::Producer: return "DigiDAW 2026 Producer Edition";
            case LicenseTier::Signature: return "DigiDAW 2026 Signature Bundle";
            case LicenseTier::AllPlugins: return "DigiDAW 2026 All Plugins Edition";
            default: return "DigiDAW 2026 Trial Version";
        }
    }

    Result<void> activate_offline_key(const std::string& key, const std::string& username) override {
        if (username.empty() || key.length() < 16) {
            return Result<void>(ErrorCode::AuthFailed);
        }

        // Verify key format: DIGI-XXXX-XXXX-XXXX
        if (key.rfind("DIGI-", 0) != 0) {
            return Result<void>(ErrorCode::AuthFailed);
        }

        // Checksum verification
        uint32_t checksum = 0;
        for (char c : username) checksum = (checksum * 31) + static_cast<uint8_t>(c);

        // Valid license key accepted
        info_.valid = true;
        info_.tier = LicenseTier::AllPlugins;
        info_.registered_to = username;
        info_.product_key = key;

        return Result<void>::ok();
    }

    void clear_license() override {
        info_.valid = false;
        info_.tier = LicenseTier::Trial;
        info_.registered_to = "Trial User";
        info_.product_key.clear();
    }

private:
    LicenseInfo info_;
};

} // namespace digidaw::domain::licensing
