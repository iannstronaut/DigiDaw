#include "../test_framework.hpp"
#include "../../domain/licensing/licensing.hpp"

using namespace digidaw::domain::licensing;

TEST_CASE(UnitLicensing, TrialModeDefaults) {
    LicenseManager lic;
    ASSERT_TRUE(lic.is_trial());
    ASSERT_EQ(lic.licensee_name(), "Trial User");
    ASSERT_EQ(lic.edition_name(), "DigiDAW 2026 Trial Version");
}

TEST_CASE(UnitLicensing, OfflineKeyActivation) {
    LicenseManager lic;

    // Invalid keys rejected
    auto res_short = lic.activate_offline_key("DIGI-SHORT", "Studio User");
    ASSERT_FALSE(res_short.is_ok());

    auto res_bad_prefix = lic.activate_offline_key("INVALID-KEY-12345678", "Studio User");
    ASSERT_FALSE(res_bad_prefix.is_ok());

    // Valid key accepted
    auto res_valid = lic.activate_offline_key("DIGI-PROD-2026-ABCD", "Studio Producer");
    ASSERT_TRUE(res_valid.is_ok());
    ASSERT_FALSE(lic.is_trial());
    ASSERT_EQ(lic.licensee_name(), "Studio Producer");
    ASSERT_EQ(lic.edition_name(), "DigiDAW 2026 All Plugins Edition");

    // Clear license reverts to trial
    lic.clear_license();
    ASSERT_TRUE(lic.is_trial());
}
