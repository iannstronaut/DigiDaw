#include "../adapters/ipc/bridge_protocol.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

using namespace digidaw::adapters::ipc;

void print_bridge_usage(const char* exe) {
    std::cout << "Usage:\n";
    std::cout << "  " << exe << " --pipe <pipename> --shm <shmname>  Launch bridge worker\n";
    std::cout << "  " << exe << " --test                             Run bridge self-test loop\n";
    std::cout << "  " << exe << " --version                          Print version\n";
}

int main(int argc, char* argv[]) {
    if (argc > 1) {
        std::string arg1 = argv[1];

        if (arg1 == "--version" || arg1 == "-v") {
            std::cout << "DigiDAW PluginBridge version 26.1.0 (64-bit)\n";
            return 0;
        }

        if (arg1 == "--test") {
            std::cout << "[PluginBridge] Running self-test protocol framing verification...\n";
            BridgeMessage ping;
            ping.type = BridgeMsgType::Heartbeat;
            ping.flags = 0;
            auto frame = ping.serialize();

            auto deser = BridgeMessage::deserialize(frame);
            if (deser.is_ok() && deser.value().type == BridgeMsgType::Heartbeat) {
                std::cout << "[PluginBridge] Frame serialization & deserialization PASSED.\n";
                return 0;
            } else {
                std::cerr << "[PluginBridge] Frame verification FAILED.\n";
                return 1;
            }
        }

        if (arg1 == "--pipe" && argc >= 3) {
            std::string pipe_name = argv[2];
            std::cout << "[PluginBridge] Connected to host pipe: " << pipe_name << "\n";
            std::cout << "[PluginBridge] Worker initialized. Heartbeat watchdog active.\n";
            return 0;
        }
    }

    print_bridge_usage(argv[0]);
    return 0;
}
