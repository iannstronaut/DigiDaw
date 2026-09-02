#include "../test_framework.hpp"
#include "../../adapters/plugins/plugin_compat_store.hpp"

using namespace digidaw::adapters::plugins;

TEST_CASE(UnitPluginCompat, StoresAndRetrievesVendorWorkarounds) {
    PluginCompatStore store;

    // Check factory entries
    ASSERT_TRUE(store.has_flag("vst.vendor.legacy_synth", CompatAlwaysRunInBridge));
    ASSERT_TRUE(store.has_flag("vst.vendor.legacy_synth", CompatForceDpiUnaware));
    ASSERT_FALSE(store.has_flag("vst.vendor.legacy_synth", CompatDontProcessWhileOpen));

    ASSERT_TRUE(store.has_flag("vst.vendor.buggy_filter", CompatDontProcessWhileOpen));

    // Dynamic user entry
    store.set_flags("vst.thirdparty.test", CompatAlwaysRunInBridge | CompatMonoOnly);
    ASSERT_TRUE(store.has_flag("vst.thirdparty.test", CompatAlwaysRunInBridge));
    ASSERT_TRUE(store.has_flag("vst.thirdparty.test", CompatMonoOnly));
}
