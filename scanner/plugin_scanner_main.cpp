#include "../adapters/ipc/plugin_scanner.hpp"
#include <iostream>
#include <string>
#include <vector>

using namespace digidaw::adapters::ipc;

void print_scanner_usage(const char* exe) {
    std::cout << "Usage:\n";
    std::cout << "  " << exe << " <plugin_path.dll>   Scan plugin and emit JSON\n";
    std::cout << "  " << exe << " --test              Run scanner self-test\n";
    std::cout << "  " << exe << " --version           Print version\n";
}

int main(int argc, char* argv[]) {
    if (argc > 1) {
        std::string arg1 = argv[1];

        if (arg1 == "--version" || arg1 == "-v") {
            std::cout << "DigiDAW PluginScanner version 26.1.0\n";
            return 0;
        }

        if (arg1 == "--test") {
            std::cout << "[PluginScanner] Testing isolated scan...\n";
            auto res = PluginScanner::scan_plugin("TestPlugin.dll");
            if (res.is_ok()) {
                std::cout << "[PluginScanner] Self-test scan PASSED:\n" << res.value().to_json() << "\n";
                return 0;
            } else {
                std::cerr << "[PluginScanner] Self-test FAILED: " << res.error().message() << "\n";
                return 1;
            }
        }

        if (arg1[0] != '-') {
            auto res = PluginScanner::scan_plugin(arg1);
            if (res.is_ok()) {
                std::cout << res.value().to_json() << "\n";
                return 0;
            } else {
                std::cerr << "{\"error\": \"" << res.error().message() << "\", \"code\": "
                          << static_cast<int>(res.error_code()) << "}\n";
                return 1;
            }
        }
    }

    print_scanner_usage(argv[0]);
    return 0;
}
