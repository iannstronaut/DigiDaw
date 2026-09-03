#include "../app/engine.hpp"
#include "../adapters/plugins/synth_3xosc.hpp"
#include "../adapters/plugins/limiter_device.hpp"
#include "../adapters/gui/win32_window.hpp"
#include "../adapters/desktop/file_association.hpp"
#include "../adapters/desktop/crash_handler.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <sstream>

void print_banner() {
    std::cout << "=========================================================\n";
    std::cout << "  DigiDAW 2026 - Clean-Room Desktop Audio Workstation     \n";
    std::cout << "  Architecture: Clean Architecture (C++20 Core)           \n";
    std::cout << "=========================================================\n\n";
}

void print_usage(const char* exe_name) {
    std::cout << "Usage:\n";
    std::cout << "  " << exe_name << "                        Start interactive DAW session (auto-creates new project)\n";
    std::cout << "  " << exe_name << " <project_file.odp>      Open project in interactive session\n";
    std::cout << "  " << exe_name << " --render <in.odp> <out.wav> [bars]  Batch render project to WAV\n";
    std::cout << "  " << exe_name << " --register-assoc       Register .odp file association in Windows Explorer\n";
    std::cout << "  " << exe_name << " --unregister-assoc     Unregister .odp file association\n";
    std::cout << "  " << exe_name << " --info                  Display engine audio and plugin status\n";
    std::cout << "  " << exe_name << " --version               Display version\n\n";
}

void setup_default_template(digidaw::app::Engine& engine) {
    auto& proj = engine.session().project();
    proj.time_map().set_tempo(128.0);

    // 1. Add default 3xOsc synth channel
    digidaw::domain::ChannelSettings s;
    s.name = "3xOsc Synth";
    s.volume = 0.85f;
    s.pan = 0.0f;
    s.mixer_track = 1;
    auto ch_id = proj.add_channel("core.generator.3xosc", s);

    // 2. Add Master limiter to track 0
    auto* master = proj.mixer_graph().get_track(digidaw::domain::MasterTrackId);
    if (master) {
        master->add_insert(std::make_shared<digidaw::adapters::plugins::LimiterDevice>());
    }

    // 3. Add default 4-step note pattern (C5 arpeggio: C, E, G, B)
    auto* pat = proj.get_pattern(1);
    if (pat) {
        pat->set_name("Default Beat");
        auto& notes = pat->get_or_create_channel_notes(ch_id);
        const auto ppq = proj.time_map().ppq();
        notes.add_note({0, ppq, 60, 100, 0, 0});             // C5
        notes.add_note({ppq, ppq, 64, 100, 0, 0});         // E5
        notes.add_note({ppq * 2, ppq, 67, 100, 0, 0});     // G5
        notes.add_note({ppq * 3, ppq, 71, 100, 0, 0});     // B5
    }

    // 4. Place clip in playlist track 1
    if (!proj.tracks().empty()) {
        proj.tracks()[0].add_clip({1, 0, digidaw::domain::DefaultPPQ * 4, false});
    }
}

void run_interactive_repl(digidaw::app::Engine& engine) {
    std::cout << "\n[Interactive Session Active]\n";
    std::cout << "Project: '" << engine.session().project().name() << "' ("
              << engine.session().project().channels().size() << " channel(s), "
              << engine.session().project().time_map().get_bpm_at(0) << " BPM)\n";
    std::cout << "Type 'help' for command list, or 'exit' to quit.\n\n";

    std::string line;
    while (true) {
        std::cout << "DigiDAW [" << engine.session().project().name() << "]> ";
        if (!std::getline(std::cin, line)) {
            break; // Stream ended (EOF or redirected input)
        }

        std::stringstream ss(line);
        std::string cmd;
        ss >> cmd;
        if (cmd.empty()) continue;

        if (cmd == "exit" || cmd == "quit" || cmd == "q") {
            std::cout << "Exiting DigiDAW. Goodbye!\n";
            break;
        } else if (cmd == "help") {
            std::cout << "\nAvailable Commands:\n"
                      << "  status              - Display project, channel, pattern, and mixer status\n"
                      << "  play                - Start audio playback transport\n"
                      << "  pause               - Pause playback\n"
                      << "  stop                - Stop playback and reset playhead to tick 0\n"
                      << "  tempo <bpm>         - Change project tempo (e.g. tempo 135)\n"
                      << "  render <file> [bar] - Export project to standard RIFF/WAVE file (e.g. render out.wav 4)\n"
                      << "  save [filepath]     - Save project to binary .odp file (e.g. save song.odp)\n"
                      << "  open <filepath>     - Open existing .odp project file\n"
                      << "  new [name]          - Create a fresh new project with starter template\n"
                      << "  exit                - Quit application\n\n";
        } else if (cmd == "status" || cmd == "info") {
            auto& proj = engine.session().project();
            std::cout << "\n--- Project Status ---\n"
                      << "Name:      " << proj.name() << "\n"
                      << "Tempo:     " << proj.time_map().get_bpm_at(0) << " BPM\n"
                      << "Transport: " << (engine.transport().is_playing() ? "PLAYING" : "STOPPED")
                      << " (Current Tick: " << engine.transport().current_tick() << ")\n"
                      << "Channels (" << proj.channels().size() << "):\n";
            for (const auto& ch : proj.channels()) {
                std::cout << "  - [" << ch.id() << "] " << ch.settings().name
                          << " | Vol: " << ch.settings().volume
                          << " | Pan: " << ch.settings().pan
                          << " | Track: " << (int)ch.settings().mixer_track << "\n";
            }
            std::cout << "Patterns (" << proj.patterns().size() << "):\n";
            for (const auto& pat : proj.patterns()) {
                std::cout << "  - [" << pat.id() << "] " << pat.name()
                          << " (" << pat.length_ticks(proj.time_map().ppq()) << " ticks)\n";
            }
            std::cout << "----------------------\n\n";
        } else if (cmd == "play") {
            engine.transport().play();
            std::cout << "[Transport] Playback started at tick " << engine.transport().current_tick() << ".\n";
        } else if (cmd == "pause") {
            engine.transport().pause();
            std::cout << "[Transport] Playback paused at tick " << engine.transport().current_tick() << ".\n";
        } else if (cmd == "stop") {
            engine.transport().stop();
            std::cout << "[Transport] Playback stopped. Reset to tick 0.\n";
        } else if (cmd == "tempo") {
            double bpm = 0;
            if (ss >> bpm && bpm > 10.0 && bpm <= 999.0) {
                engine.session().project().time_map().set_tempo(bpm);
                std::cout << "[Tempo] Updated tempo to " << bpm << " BPM.\n";
            } else {
                std::cout << "Usage: tempo <bpm> (e.g. tempo 130)\n";
            }
        } else if (cmd == "save") {
            std::string path;
            ss >> path;
            if (path.empty()) {
                path = engine.session().project().name() + ".odp";
            }
            auto res = engine.session().save_project(path);
            if (res.is_ok()) {
                std::cout << "[Save] Project saved successfully to: " << path << "\n";
            } else {
                std::cout << "[Error] Failed to save project: " << res.error().message() << "\n";
            }
        } else if (cmd == "open") {
            std::string path;
            ss >> path;
            if (path.empty()) {
                std::cout << "Usage: open <project_file.odp>\n";
            } else {
                auto res = engine.session().open_project(path);
                if (res.is_ok()) {
                    std::cout << "[Open] Successfully opened project: " << path << "\n";
                } else {
                    std::cout << "[Error] Failed to open project: " << res.error().message() << "\n";
                }
            }
        } else if (cmd == "new") {
            std::string name = "Untitled";
            ss >> name;
            engine.session().new_project(name);
            setup_default_template(engine);
            std::cout << "[New] Initialized new project '" << name << "' with default starter template.\n";
        } else if (cmd == "render") {
            std::string out_wav;
            int bars = 4;
            ss >> out_wav >> bars;
            if (out_wav.empty()) {
                std::cout << "Usage: render <output.wav> [bars]\n";
            } else {
                if (bars <= 0) bars = 4;
                std::cout << "[Render] Rendering " << bars << " bars of project to " << out_wav << "...\n";
                auto duration = engine.session().project().time_map().bar_to_tick(bars);

                std::unordered_map<digidaw::domain::ChannelId, std::shared_ptr<digidaw::domain::IDevice>> devs;
                for (const auto& ch : engine.session().project().channels()) {
                    auto inst_res = engine.plugin_manager().instantiate(ch.device_uid());
                    if (inst_res.is_ok()) {
                        devs[ch.id()] = inst_res.value();
                    }
                }

                auto rend_res = digidaw::app::OfflineRenderer::render_to_wav(
                    engine.session().project(), devs, out_wav, duration, 44100.0);

                if (rend_res.is_ok()) {
                    std::cout << "[Render] Finished! File exported: " << out_wav << "\n";
                } else {
                    std::cout << "[Render Error] " << rend_res.error().message() << "\n";
                }
            }
        } else {
            std::cout << "Unknown command: '" << cmd << "'. Type 'help' for command list.\n";
        }
    }
}

int main(int argc, char* argv[]) {
    // 0. Initialize Desktop Crash Handler (DESKTOP-FR-007)
    digidaw::adapters::desktop::CrashHandler::init("DigiDawUserData/Logs");

    print_banner();

    // 1. Initialize Engine & Config
    digidaw::app::Engine engine;
    digidaw::adapters::config::FileConfigStore config("digidaw_config.ini");
    const int64_t runs = config.increment_launch_counter();
    std::cout << "[System] Launch counter: " << runs << "\n";
    std::cout << "[Audio]  Active Driver: " << engine.audio_device().device_name() << "\n";

    bool cli_mode = false;

    // 2. Parse Command Line Arguments
    if (argc > 1) {
        std::string arg1 = argv[1];

        if (arg1 == "--version" || arg1 == "-v") {
            std::cout << "DigiDAW version 26.1.0-alpha (Clean-room reconstruction)\n";
            return 0;
        }

        if (arg1 == "--help" || arg1 == "-h") {
            print_usage(argv[0]);
            return 0;
        }

        if (arg1 == "--register-assoc") {
            auto res = digidaw::adapters::desktop::FileAssociation::register_association(argv[0]);
            if (res.is_ok()) {
                std::cout << "[Desktop] Successfully registered .odp file association to: " << argv[0] << "\n";
                return 0;
            } else {
                std::cerr << "[Desktop Error] Failed to register association: " << res.error().message() << "\n";
                return 1;
            }
        }

        if (arg1 == "--unregister-assoc") {
            auto res = digidaw::adapters::desktop::FileAssociation::unregister_association();
            if (res.is_ok()) {
                std::cout << "[Desktop] Successfully unregistered .odp file association.\n";
                return 0;
            } else {
                std::cerr << "[Desktop Error] Failed to unregister association: " << res.error().message() << "\n";
                return 1;
            }
        }

        if (arg1 == "--info") {
            std::cout << "[Engine Info]\n";
            std::cout << "  Sample Rate:  " << engine.audio_device().sample_rate() << " Hz\n";
            std::cout << "  Buffer Size:  " << engine.audio_device().buffer_size() << " frames\n";
            std::cout << "  Available Plugins:\n";
            for (const auto& p : engine.plugin_manager().available_plugins()) {
                std::cout << "    - " << p.name << " (" << p.uid << ")\n";
            }
            return 0;
        }

        if (arg1 == "--render" && argc >= 4) {
            std::string in_proj = argv[2];
            std::string out_wav = argv[3];
            int bars = (argc >= 5) ? std::stoi(argv[4]) : 4;

            std::cout << "[Renderer] Loading project: " << in_proj << "\n";
            auto open_res = engine.session().open_project(in_proj);
            if (open_res.is_error()) {
                std::cerr << "[Error] Failed to open project: " << open_res.error().message() << "\n";
                return 1;
            }

            std::cout << "[Renderer] Rendering " << bars << " bars to " << out_wav << "...\n";
            digidaw::domain::Tick duration = engine.session().project().time_map().bar_to_tick(bars);

            std::unordered_map<digidaw::domain::ChannelId, std::shared_ptr<digidaw::domain::IDevice>> devs;
            for (const auto& ch : engine.session().project().channels()) {
                auto inst_res = engine.plugin_manager().instantiate(ch.device_uid());
                if (inst_res.is_ok()) {
                    devs[ch.id()] = inst_res.value();
                }
            }

            auto rend_res = digidaw::app::OfflineRenderer::render_to_wav(
                engine.session().project(), devs, out_wav, duration, 44100.0);

            if (rend_res.is_ok()) {
                std::cout << "[Renderer] Successfully rendered WAV: " << out_wav << "\n";
                return 0;
            } else {
                std::cerr << "[Error] Rendering failed: " << rend_res.error().message() << "\n";
                return 1;
            }
        }

        if (arg1 == "--cli" || arg1 == "-c") {
            cli_mode = true;
        } else if (arg1[0] != '-') {
            // Treat as project file path from file association (%1)
            std::cout << "[Session] Opening project: " << arg1 << "\n";
            auto res = engine.session().open_project(arg1);
            if (res.is_error()) {
                std::cout << "[Session] Project not found, creating new project at: " << arg1 << "\n";
                engine.session().new_project(arg1);
                setup_default_template(engine);
                engine.session().save_project(arg1);
            }
        }
    } else {
        // Double-click / No arguments passed: Auto-create new project!
        std::cout << "[Session] No project file specified. Auto-creating new project...\n";
        engine.session().new_project("Untitled");
        setup_default_template(engine);
    }

    // 3. Set Song mode for Arranger / Placement Blocks and Start Audio
    engine.transport().set_mode(digidaw::app::PlaybackMode::Song);
    auto audio_res = engine.start_audio();
    if (audio_res.is_ok()) {
        std::cout << "[Audio Engine] Real-time audio output started: " << engine.audio_device().device_name() << "\n";
    } else {
        std::cout << "[Audio Engine] Audio output started in fallback mode\n";
    }

    // 4. Launch UI (Native Win32 GUI by default, CLI if --cli passed)
    if (cli_mode) {
        run_interactive_repl(engine);
    } else {
        std::cout << "[GUI] Launching DigiDAW 2026 Native Desktop Window...\n";
        digidaw::adapters::gui::DigiDawWindow window(engine);
        if (window.create_and_show(GetModuleHandle(NULL))) {
            window.run_message_loop();
        } else {
            std::cerr << "[GUI Error] Failed to create Win32 window, falling back to CLI session.\n";
            run_interactive_repl(engine);
        }
    }

    engine.stop_audio();
    return 0;
}
