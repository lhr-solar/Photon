cmake_minimum_required(VERSION 3.25)

# ---------------------------------------------------------------------------
# RapidCheck — fetch if not already present under .external/rapidcheck
# ---------------------------------------------------------------------------
include(FetchContent)

set(_rc_vendor "${CMAKE_SOURCE_DIR}/.external/rapidcheck")

if(EXISTS "${_rc_vendor}/CMakeLists.txt")
    # Already vendored — just add it without any network access
    set(FETCHCONTENT_SOURCE_DIR_RAPIDCHECK "${_rc_vendor}" CACHE PATH "" FORCE)
endif()

# Disable RapidCheck's own tests/extras to keep the build minimal
set(RC_ENABLE_TESTS       OFF CACHE BOOL "" FORCE)
set(RC_ENABLE_RPCHECKS    OFF CACHE BOOL "" FORCE)
set(RC_ENABLE_DOCTEST     OFF CACHE BOOL "" FORCE)
set(RC_ENABLE_CATCH       OFF CACHE BOOL "" FORCE)
set(RC_ENABLE_BOOST       OFF CACHE BOOL "" FORCE)
set(RC_ENABLE_GMOCK       OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    rapidcheck
    GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
    GIT_TAG        master
    SOURCE_DIR     "${_rc_vendor}"
)
FetchContent_MakeAvailable(rapidcheck)

# ---------------------------------------------------------------------------
# Helper macro to create a test executable
# Only io and parse — intentionally no gpu, gui, network, synth, imgui_runtime, or Vulkan
# ---------------------------------------------------------------------------
enable_testing()

macro(add_io_test target_name source_file)
    add_executable(${target_name} ${source_file})
    target_compile_features(${target_name} PRIVATE cxx_std_23)
    target_link_libraries(${target_name}
        PRIVATE
        io
        parse
        rapidcheck
    )
    target_include_directories(${target_name}
        PRIVATE
        "${CMAKE_SOURCE_DIR}/photon/io"
        "${CMAKE_SOURCE_DIR}/photon/parse"
    )
    add_test(NAME ${target_name} COMMAND ${target_name})
endmacro()

# Property-based tests
add_io_test(pbt_arena             pbt_arena.cpp)
add_io_test(pbt_csv_exporter      pbt_csv_exporter.cpp)
add_io_test(pbt_replay_controller pbt_replay_controller.cpp)

# Unit tests
add_io_test(test_csv_exporter      test_csv_exporter.cpp)
add_io_test(test_replay_controller test_replay_controller.cpp)
add_io_test(test_integration       test_integration.cpp)

# Pre-Fault Recorder tests
add_io_test(pbt_pre_fault_recorder       pbt_pre_fault_recorder.cpp)
add_io_test(test_pre_fault_recorder      test_pre_fault_recorder.cpp)
add_io_test(test_pre_fault_integration   test_pre_fault_integration.cpp)
