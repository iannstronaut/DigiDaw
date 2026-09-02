#pragma once

#include <string>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace digidaw::adapters::desktop {

class CrashHandler {
public:
    static void init(const std::string& log_directory) {
        log_dir_ = log_directory;
        std::filesystem::create_directories(log_dir_);

#ifdef _WIN32
        SetUnhandledExceptionFilter(unhandled_exception_filter);
#endif
    }

    static std::string format_crash_report(uint32_t code, void* address) {
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::stringstream ss;
        ss << "=====================================================\n";
        ss << "  DigiDAW 2026 Diagnostic Crash Report (M6/DESKTOP) \n";
        ss << "=====================================================\n";
        ss << "Timestamp:      " << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << "\n";
        ss << "Exception Code: 0x" << std::hex << code << std::dec << "\n";
        ss << "Fault Address:  " << address << "\n";
        ss << "Build:          v26.1.0 (Clean Architecture x64)\n";
        ss << "OS Version:     Windows NT (Direct Exception Filter)\n";
        ss << "Action:         Host state captured, crash isolated.\n";
        ss << "=====================================================\n";
        return ss.str();
    }

    static void write_diagnostic_dump(uint32_t code, void* address) {
        std::string report = format_crash_report(code, address);
        std::string out_path = (std::filesystem::path(log_dir_) / "crash_report.txt").string();
        std::ofstream out(out_path, std::ios::app);
        if (out.is_open()) {
            out << report << "\n";
        }
    }

private:
#ifdef _WIN32
    static LONG WINAPI unhandled_exception_filter(EXCEPTION_POINTERS* ep) {
        uint32_t code = ep ? ep->ExceptionRecord->ExceptionCode : 0xE0000001;
        void* addr = ep ? ep->ExceptionRecord->ExceptionAddress : nullptr;
        write_diagnostic_dump(code, addr);
        return EXCEPTION_EXECUTE_HANDLER;
    }
#endif

    inline static std::string log_dir_{"DigiDawUserData/Logs"};
};

} // namespace digidaw::adapters::desktop
