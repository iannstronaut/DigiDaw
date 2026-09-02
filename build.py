#!/usr/bin/env python3
import os
import sys
import subprocess
import time

def main():
    root_dir = os.path.dirname(os.path.abspath(__file__))
    bin_dir = os.path.join(root_dir, "bin")
    os.makedirs(bin_dir, exist_ok=True)

    gpp = r"D:\mingw64\bin\g++.exe"
    if not os.path.exists(gpp):
        print(f"Error: {gpp} not found!")
        sys.exit(1)

    print("=========================================================")
    print("  DigiDAW Build & Verification Engine                    ")
    print(f"  Compiler: MinGW GCC 16.2.0 (C++20)                     ")
    print("=========================================================\n")

    common_flags = [
        "-std=c++20",
        "-O2",
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
        os.path.join(root_dir, "tests", "integration", "test_project_odp.cpp"),
        os.path.join(root_dir, "tests", "integration", "test_offline_renderer.cpp"),
        os.path.join(root_dir, "tests", "integration", "test_bridge_and_scanner.cpp"),
        os.path.join(root_dir, "tests", "integration", "test_script_host.cpp"),
        os.path.join(root_dir, "adapters", "c_api", "c_api_impl.cpp")
    ]

    test_exe = os.path.join(bin_dir, "run_tests.exe")

    print("[Build] Compiling Test Suite (run_tests.exe)...")
    cmd_tests = [gpp] + common_flags + test_sources + ["-o", test_exe]
    t0 = time.time()
    res = subprocess.run(cmd_tests, cwd=root_dir)
    if res.returncode != 0:
        print("\n[Build FAILED] Failed to compile test suite!")
        sys.exit(res.returncode)
    print(f"[Build SUCCESS] Compiled run_tests.exe in {time.time() - t0:.2f}s\n")

    shell_sources = [
        os.path.join(root_dir, "shell", "main.cpp"),
        os.path.join(root_dir, "adapters", "c_api", "c_api_impl.cpp")
    ]
    app_exe = os.path.join(bin_dir, "DigiDAW.exe")
    print("[Build] Compiling Application Shell (DigiDAW.exe)...")
    cmd_app = [gpp] + common_flags + shell_sources + ["-o", app_exe]
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
