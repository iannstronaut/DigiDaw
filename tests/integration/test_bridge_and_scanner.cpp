#include "../test_framework.hpp"
#include "../../adapters/ipc/bridge_protocol.hpp"
#include "../../adapters/ipc/plugin_scanner.hpp"
#include <thread>

using namespace digidaw::adapters::ipc;
using namespace digidaw::domain;

TEST_CASE(IntegrationBridge, MessageFramingRoundTrip) {
    BridgeMessage msg;
    msg.type = BridgeMsgType::SetState;
    msg.flags = 0x02; // Urgent
    msg.payload = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};

    std::vector<uint8_t> frame = msg.serialize();
    ASSERT_TRUE(frame.size() > sizeof(BridgeFrameHeader));

    auto deser_res = BridgeMessage::deserialize(frame);
    ASSERT_OK(deser_res);

    const auto& deserialized = deser_res.value();
    ASSERT_EQ(static_cast<uint16_t>(deserialized.type), static_cast<uint16_t>(BridgeMsgType::SetState));
    ASSERT_EQ(deserialized.flags, 0x02);
    ASSERT_EQ(deserialized.payload.size(), 8);
    ASSERT_EQ(deserialized.payload[0], 0xDE);
    ASSERT_EQ(deserialized.payload[3], 0xEF);
}

TEST_CASE(IntegrationBridge, CrashIsolationAndNodeBypass) {
    BridgeSupervisor supervisor("vst.vendor.buggy_plugin");
    ASSERT_FALSE(supervisor.is_bypassed());
    ASSERT_FALSE(supervisor.has_failed());

    // Normal heartbeat
    supervisor.on_heartbeat_received();

    // Simulate plugin process crash (AC-2, DAW-NFR-005)
    supervisor.on_process_crash();
    ASSERT_TRUE(supervisor.is_bypassed());
    ASSERT_TRUE(supervisor.has_failed());
}

TEST_CASE(IntegrationBridge, ScannerTimeoutAndCrashDetection) {
    // Normal scan
    auto ok_scan = PluginScanner::scan_plugin("C:\\VST\\Synth.dll");
    ASSERT_OK(ok_scan);
    ASSERT_EQ(ok_scan.value().uid, "vst.C:\\VST\\Synth.dll");

    // Simulated hang: scanner kills it and reports timeout (INT-06)
    auto hang_scan = PluginScanner::scan_plugin("C:\\VST\\HangingPlugin.dll", std::chrono::milliseconds(100), true, false);
    ASSERT_TRUE(hang_scan.is_error());
    ASSERT_EQ(hang_scan.error_code(), ErrorCode::ScanTimeout);

    // Simulated crash: reported safely as scan failed without host crash
    auto crash_scan = PluginScanner::scan_plugin("C:\\VST\\CrashPlugin.dll", std::chrono::milliseconds(100), false, true);
    ASSERT_TRUE(crash_scan.is_error());
    ASSERT_EQ(crash_scan.error_code(), ErrorCode::ScanFailed);
}
