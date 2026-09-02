#include "../test_framework.hpp"
#include "../../adapters/desktop/crash_handler.hpp"

using namespace digidaw::adapters::desktop;

TEST_CASE(UnitDesktop, CrashReportFormatting) {
    void* fake_addr = reinterpret_cast<void*>(0x7FFA12345678ULL);
    uint32_t fake_code = 0xC0000005; // Access violation

    std::string report = CrashHandler::format_crash_report(fake_code, fake_addr);

    ASSERT_TRUE(report.find("DigiDAW 2026 Diagnostic Crash Report") != std::string::npos);
    ASSERT_TRUE(report.find("c0000005") != std::string::npos || report.find("C0000005") != std::string::npos);
    ASSERT_TRUE(report.find("7ffa12345678") != std::string::npos || report.find("7FFA12345678") != std::string::npos);
    ASSERT_TRUE(report.find("v26.1.0") != std::string::npos);
}
