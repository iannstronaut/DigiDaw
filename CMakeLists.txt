cmake_minimum_required(VERSION 3.20)
project(DigiDAW VERSION 26.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk
)

# Common sources
set(COMMON_SOURCES
    adapters/c_api/c_api_impl.cpp
)

# Main executable
add_executable(DigiDAW
    shell/main.cpp
    ${COMMON_SOURCES}
)

# Test executable
add_executable(run_tests
    tests/main_test.cpp
    tests/unit/test_domain_time.cpp
    tests/unit/test_domain_mixer.cpp
    tests/unit/test_domain_sequencing.cpp
    tests/unit/test_domain_dsp.cpp
    tests/integration/test_project_odp.cpp
    tests/integration/test_offline_renderer.cpp
    tests/integration/test_bridge_and_scanner.cpp
    tests/integration/test_script_host.cpp
    ${COMMON_SOURCES}
)

enable_testing()
add_test(NAME AllTests COMMAND run_tests)
