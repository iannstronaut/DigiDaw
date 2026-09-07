#!/usr/bin/env python3
import os
import sys
import subprocess
import time

def main():
    root_dir = os.path.dirname(os.path.abspath(__file__))
    bin_dir = os.path.join(root_dir, "bin")
    os.makedirs(bin_dir, exist_ok=True)

    possible_gpp = [
        r"D:\mingw64\bin\g++.exe",
        r"C:\mingw64\bin\g++.exe",
        r"C:\msys64\mingw64\bin\g++.exe",
        r"C:\tools\mingw64\bin\g++.exe"
    ]
    gpp = None
    for p in possible_gpp:
        if os.path.exists(p):
            gpp = p
            break
    if not gpp:
        import shutil
        gpp = shutil.which("g++")
        if not gpp or not os.path.exists(gpp):
            print("Error: g++.exe not found in MinGW directories or system PATH!")
            sys.exit(1)

    print("=========================================================")
    print("  DigiDAW Build & Verification Engine                    ")
    print(f"  Compiler: MinGW GCC 16.2.0 (C++20)                     ")
    print("=========================================================\n")

    common_flags = [
        "-std=c++20",
        "-O3",
        "-msse4.2",
        "-mfpmath=sse",
        "-Wall",
        "-Wextra",
        "-static",
        "-static-libgcc",
        "-static-libstdc++",
        f"-I{root_dir}",
        f"-I{os.path.join(root_dir, 'sdk')}",
    ]

    test_sources = [
        os.path.join(root_dir, "tests", "main_test.cpp"),
        os.path.join(root_dir, "tests", "unit", "test_domain_time.cpp"),
        os.path.join(root_dir, "tests", "unit", "test_domain_mixer.cpp"),
        os.path.join(root_dir, "tests", "unit", "test_domain_sequencing.cpp"),
        os.path.join(root_dir, "tests", "unit", "test_domain_dsp.cpp"),
        os.path.join(root_dir, "tests", "unit", "test_compressor.cpp"),
        os.path.join(root_dir, "tests", "unit", "test_reverb.cpp"),
        os.path.join(root_dir, "tests", "unit", "test_sampler.cpp"),
        os.path.join(root_dir, "tests", "unit", "test_drum_sampler.cpp"),
        os.path.join(root_dir, "tests", "unit", "test_audioclip.cpp"),
        os.path.join(root_dir, "tests", "unit", "test_audio_library.cpp"),
        os.path.join(root_dir, "tests", "unit", "test_plugin_compat.cpp"),
        os.path.join(root_dir, "tests", "unit", "test_file_association.cpp"),
        os.path.join(root_dir, "tests", "unit", "test_crash_handler.cpp"),
        os.path.join(root_dir, "tests", "unit", "test_licensing.cpp"),
        os.path.join(root_dir, "tests", "unit", "test_xaudio_plugins.cpp"),
        os.path.join(root_dir, "tests", "integration", "test_project_odp.cpp"),
        os.path.join(root_dir, "tests", "integration", "test_offline_renderer.cpp"),
        os.path.join(root_dir, "tests", "integration", "test_bridge_and_scanner.cpp"),
        os.path.join(root_dir, "tests", "integration", "test_script_host.cpp"),
        os.path.join(root_dir, "tests", "integration", "test_script_automation.cpp"),
        os.path.join(root_dir, "adapters", "c_api", "c_api_impl.cpp")
    ]

    test_exe = os.path.join(bin_dir, "run_tests.exe")

    print("[Build] Compiling Test Suite (run_tests.exe)...")
    cmd_tests = [gpp] + common_flags + test_sources + ["-o", test_exe, "-lshell32", "-lwinmm", "-lole32", "-lavrt"]
    t0 = time.time()
    res = subprocess.run(cmd_tests, cwd=root_dir)
    if res.returncode != 0:
        print("\n[Build FAILED] Failed to compile test suite!")
        sys.exit(res.returncode)
    print(f"[Build SUCCESS] Compiled run_tests.exe in {time.time() - t0:.2f}s\n")

    # Compile Standalone PluginBridge.exe (M4)
    bridge_sources = [os.path.join(root_dir, "bridge", "plugin_bridge_main.cpp")]
    bridge_exe = os.path.join(bin_dir, "PluginBridge.exe")
    print("[Build] Compiling PluginBridge (PluginBridge.exe)...")
    cmd_bridge = [gpp] + common_flags + bridge_sources + ["-o", bridge_exe]
    t0 = time.time()
    res = subprocess.run(cmd_bridge, cwd=root_dir)
    if res.returncode != 0:
        print("\n[Build FAILED] Failed to compile PluginBridge.exe!")
        sys.exit(res.returncode)
    print(f"[Build SUCCESS] Compiled PluginBridge.exe in {time.time() - t0:.2f}s\n")

    # Compile Standalone PluginScanner.exe (M4)
    scanner_sources = [os.path.join(root_dir, "scanner", "plugin_scanner_main.cpp")]
    scanner_exe = os.path.join(bin_dir, "PluginScanner.exe")
    print("[Build] Compiling PluginScanner (PluginScanner.exe)...")
    cmd_scanner = [gpp] + common_flags + scanner_sources + ["-o", scanner_exe]
    t0 = time.time()
    res = subprocess.run(cmd_scanner, cwd=root_dir)
    if res.returncode != 0:
        print("\n[Build FAILED] Failed to compile PluginScanner.exe!")
        sys.exit(res.returncode)
    print(f"[Build SUCCESS] Compiled PluginScanner.exe in {time.time() - t0:.2f}s\n")

    # Ensure any running DigiDAW.exe instance is terminated before linking
    subprocess.run("taskkill /f /im DigiDAW.exe 2>nul", shell=True)

    shell_sources = [
        os.path.join(root_dir, "shell", "main.cpp"),
        os.path.join(root_dir, "adapters", "c_api", "c_api_impl.cpp")
    ]

    # Compile and embed Windows Resource Script (Per-Monitor V2 High-DPI Manifest & Comctl32 v6)
    windres = os.path.join(os.path.dirname(gpp), "windres.exe")
    resource_rc = os.path.join(root_dir, "shell", "resource.rc")
    resource_obj = os.path.join(bin_dir, "resource.o")
    if os.path.exists(windres) and os.path.exists(resource_rc):
        print("[Build] Compiling Windows Resource Script & High-DPI Manifest (resource.o)...")
        cmd_rc = [windres, "-I", os.path.join(root_dir, "shell"), "-i", resource_rc, "-o", resource_obj]
        res_rc = subprocess.run(cmd_rc, cwd=root_dir)
        if res_rc.returncode == 0:
            shell_sources.append(resource_obj)
            print("[Build SUCCESS] Embedded Per-Monitor V2 Manifest Resource directly into DigiDAW.exe")

    # Deploy High-DPI Per-Monitor V2 Application Manifest next to executable
    manifest_src = os.path.join(root_dir, "shell", "DigiDAW.manifest")
    manifest_dst = os.path.join(bin_dir, "DigiDAW.exe.manifest")
    if os.path.exists(manifest_src):
        import shutil
        shutil.copyfile(manifest_src, manifest_dst)
        print("[Build] Deployed Per-Monitor V2 High-DPI Manifest (DigiDAW.exe.manifest)")

    # Deploy Assets folder to bin/assets
    assets_src = os.path.join(root_dir, "assets")
    assets_dst = os.path.join(bin_dir, "assets")
    if os.path.exists(assets_src):
        import shutil
        if os.path.exists(assets_dst):
            shutil.rmtree(assets_dst)
        shutil.copytree(assets_src, assets_dst)
        print("[Build] Deployed Application Assets (bin/assets/)")

    # Override MinGW default-manifest.o to cleanly embed our custom Per-Monitor V2 manifest without conflict
    spec_path = os.path.join(bin_dir, "no-default-manifest.spec")
    with open(spec_path, "w") as sf:
        sf.write("*endfile:\n%{mdaz-ftz:crtfastmath.o%s;Ofast|ffast-math|funsafe-math-optimizations:%{!shared:%{!mno-daz-ftz:crtfastmath.o%s}}} %{fvtable-verify=none:%s; fvtable-verify=preinit:vtv_end.o%s; fvtable-verify=std:vtv_end.o%s} crtend.o%s\n")

    app_exe = os.path.join(bin_dir, "DigiDAW.exe")
    print("[Build] Compiling Application Shell (DigiDAW.exe)...")
    cmd_app = [gpp, f"-specs={spec_path}"] + common_flags + shell_sources + [
        "-o", app_exe,
        "-lgdi32", "-luser32", "-lkernel32", "-lcomctl32", "-lcomdlg32", "-lshell32", "-lwinmm",
        "-ld2d1", "-ldwrite", "-ld3d11", "-ld3dcompiler", "-ldxgi", "-lole32", "-lavrt",
        "-ldwmapi"
    ]
    t0 = time.time()
    res = subprocess.run(cmd_app, cwd=root_dir)
    if res.returncode != 0:
        print("\n[Build FAILED] Failed to compile DigiDAW.exe!")
        sys.exit(res.returncode)
    print(f"[Build SUCCESS] Compiled DigiDAW.exe in {time.time() - t0:.2f}s\n")

    # Run tests if requested or by default
    if "--no-test" not in sys.argv:
        print("[Test] Executing Test Suite...")
        test_run = subprocess.run([test_exe], cwd=root_dir)
        if test_run.returncode != 0:
            print("\n[Test FAILED] One or more tests failed!")
            sys.exit(test_run.returncode)
        print("\n[Verification Complete] All unit, integration, and parity tests PASSED!")

if __name__ == "__main__":
    main()
