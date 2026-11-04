cmake_minimum_required(VERSION 3.20)

project(DelayLamaWindows VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # Keep the release ZIP self-contained: do not require a separately
    # installed Visual C++ runtime on the target Windows machine.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(NOT WIN32)
    message(FATAL_ERROR "The native standalone target in Windows/ requires a Windows host.")
endif()

set(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

add_library(delay_lama_dsp STATIC
    "${PROJECT_ROOT}/DSP/delay.c"
    "${PROJECT_ROOT}/DSP/synth.c"
    "${PROJECT_ROOT}/DSP/voice.c"
)
target_include_directories(delay_lama_dsp PUBLIC "${PROJECT_ROOT}/DSP")

if(MSVC)
    target_compile_options(delay_lama_dsp PRIVATE /W4)
else()
    target_compile_options(delay_lama_dsp PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(delay_lama_windows_bridge STATIC standalone_bridge_win32.c)
target_include_directories(delay_lama_windows_bridge PRIVATE
    "${PROJECT_ROOT}/DSP"
    "${PROJECT_ROOT}/Sources"
)
target_link_libraries(delay_lama_windows_bridge PUBLIC delay_lama_dsp winmm)

if(MSVC)
    target_compile_options(delay_lama_windows_bridge PRIVATE /W4)
else()
    target_compile_options(delay_lama_windows_bridge PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(DelayLamaStandalone WIN32
    classic_window.cpp
    delay_lama.rc
    delay_lama_win32.cpp
    flag_window.cpp
    monk_art.cpp
    monk_window.cpp
    settings_store.cpp
    settings_window.cpp
    ui_common.cpp
)
target_link_libraries(DelayLamaStandalone PRIVATE
    delay_lama_windows_bridge
    comctl32
    gdiplus
    gdi32
    shell32
    user32
    winmm
)
target_compile_definitions(DelayLamaStandalone PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)

if(MSVC)
    target_compile_options(DelayLamaStandalone PRIVATE /W4 /EHsc /utf-8)
else()
    target_compile_options(DelayLamaStandalone PRIVATE -Wall -Wextra -Wpedantic)
endif()

set(WINDOWS_RESOURCE_FILES
    background.png
    faces.png
    about.png
    DelayLama-Original-Manual.pdf
)
add_custom_command(TARGET DelayLamaStandalone POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:DelayLamaStandalone>/Resources"
    VERBATIM
)
foreach(resource IN LISTS WINDOWS_RESOURCE_FILES)
    add_custom_command(TARGET DelayLamaStandalone POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${PROJECT_ROOT}/Resources/${resource}"
            "$<TARGET_FILE_DIR:DelayLamaStandalone>/Resources/${resource}"
        VERBATIM
    )
endforeach()
add_custom_command(TARGET DelayLamaStandalone POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${PROJECT_ROOT}/MONKSYNTH-LICENSE.txt"
        "$<TARGET_FILE_DIR:DelayLamaStandalone>/MONKSYNTH-LICENSE.txt"
    VERBATIM
)

# DelayLamaVoice: the helper that sings one MIDI channel fed through its stdin
# (Delay Lama Everything). The core (protocol, voice allocator, choir mix,
# offline render) has no window or audio device, so CTest links it directly.
add_library(delay_lama_voice_core STATIC
    voice_allocator.cpp
    voice_articulator.cpp
    voice_choir.cpp
    voice_protocol.cpp
    voice_render.cpp
)
target_include_directories(delay_lama_voice_core PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(delay_lama_voice_core PUBLIC delay_lama_windows_bridge)
target_compile_definitions(delay_lama_voice_core PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)

# A console program, so it can be typed into and piped to.
add_executable(DelayLamaVoice
    delay_lama.rc
    monk_art.cpp
    ui_common.cpp
    voice_main.cpp
    voice_window.cpp
)
target_link_libraries(DelayLamaVoice PRIVATE
    delay_lama_voice_core
    gdiplus
    gdi32
    shell32
    user32
    winmm
)
target_compile_definitions(DelayLamaVoice PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)

if(MSVC)
    target_compile_options(delay_lama_voice_core PRIVATE /W4 /EHsc /utf-8)
    target_compile_options(DelayLamaVoice PRIVATE /W4 /EHsc /utf-8)
else()
    target_compile_options(delay_lama_voice_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(DelayLamaVoice PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The helper only draws the monk, but ships with the same artwork terms.
add_custom_command(TARGET DelayLamaVoice POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:DelayLamaVoice>/Resources"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${PROJECT_ROOT}/Resources/faces.png"
        "$<TARGET_FILE_DIR:DelayLamaVoice>/Resources/faces.png"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${PROJECT_ROOT}/MONKSYNTH-LICENSE.txt"
        "$<TARGET_FILE_DIR:DelayLamaVoice>/MONKSYNTH-LICENSE.txt"
    VERBATIM
)

add_executable(delay_lama_dsp_smoke "${PROJECT_ROOT}/Tests/dsp_smoke.c")
target_link_libraries(delay_lama_dsp_smoke PRIVATE delay_lama_dsp)
target_include_directories(delay_lama_dsp_smoke PRIVATE "${PROJECT_ROOT}/DSP")
if(NOT MSVC)
    target_link_libraries(delay_lama_dsp_smoke PRIVATE m)
endif()

# These headless tests are deliberately independent of the GUI and audio
# device. They exercise the same native C sources that the executable links.
add_executable(delay_lama_windows_smoke "${PROJECT_ROOT}/Tests/windows_smoke.c")
target_link_libraries(delay_lama_windows_smoke PRIVATE delay_lama_dsp)
target_include_directories(delay_lama_windows_smoke PRIVATE "${PROJECT_ROOT}/DSP")
if(NOT MSVC)
    target_link_libraries(delay_lama_windows_smoke PRIVATE m)
endif()

add_executable(delay_lama_windows_bridge_smoke
    "${PROJECT_ROOT}/Tests/bridge_smoke.c"
)
target_link_libraries(delay_lama_windows_bridge_smoke PRIVATE delay_lama_windows_bridge)
if(MSVC)
    target_compile_options(delay_lama_windows_bridge_smoke PRIVATE /W4)
endif()

# The macOS bridge (Sources/standalone_bridge.c) built against stub CoreMIDI
# and os_unfair_lock headers, so its logic and MIDI parser are tested on
# Windows too. The real macOS build is tested by Tests/run_macos_tests.sh.
add_executable(delay_lama_macos_bridge_logic
    "${PROJECT_ROOT}/Sources/standalone_bridge.c"
    "${PROJECT_ROOT}/Tests/macos_bridge_logic.c"
)
target_include_directories(delay_lama_macos_bridge_logic PRIVATE
    "${PROJECT_ROOT}/Tests/apple_stubs"
    "${PROJECT_ROOT}/DSP"
)
target_link_libraries(delay_lama_macos_bridge_logic PRIVATE delay_lama_dsp)
if(MSVC)
    target_compile_options(delay_lama_macos_bridge_logic PRIVATE /W4)
else()
    target_compile_options(delay_lama_macos_bridge_logic PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(delay_lama_macos_bridge_logic PRIVATE m)
endif()

# Robustness tests ported from monksynth (cpp/tests); they read engine
# internals through synth_internal.h.
add_executable(delay_lama_dsp_hardening "${PROJECT_ROOT}/Tests/dsp_hardening.c")
target_link_libraries(delay_lama_dsp_hardening PRIVATE delay_lama_dsp)
target_include_directories(delay_lama_dsp_hardening PRIVATE "${PROJECT_ROOT}/DSP")
if(MSVC)
    target_compile_options(delay_lama_dsp_hardening PRIVATE /W4)
else()
    target_compile_options(delay_lama_dsp_hardening PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(delay_lama_dsp_hardening PRIVATE m)
endif()

foreach(check IN ITEMS voice_protocol voice_allocator voice_render)
    add_executable(delay_lama_${check} "${PROJECT_ROOT}/Tests/${check}_checks.cpp")
    target_link_libraries(delay_lama_${check} PRIVATE delay_lama_voice_core)
    if(MSVC)
        target_compile_options(delay_lama_${check} PRIVATE /W4 /EHsc /utf-8)
    else()
        target_compile_options(delay_lama_${check} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

enable_testing()
add_test(NAME delay_lama_dsp_smoke COMMAND delay_lama_dsp_smoke)
add_test(NAME delay_lama_windows_smoke COMMAND delay_lama_windows_smoke)
add_test(NAME delay_lama_windows_bridge_smoke COMMAND delay_lama_windows_bridge_smoke)
add_test(NAME delay_lama_dsp_hardening COMMAND delay_lama_dsp_hardening)
add_test(NAME delay_lama_macos_bridge_logic COMMAND delay_lama_macos_bridge_logic)
# A non-finite sample rate or vibrato phase used to spin forever, and so did
# SysEx in the macOS MIDI parser: fail instead of hanging.
set_tests_properties(delay_lama_dsp_hardening delay_lama_macos_bridge_logic PROPERTIES TIMEOUT 60)
add_test(NAME delay_lama_voice_protocol COMMAND delay_lama_voice_protocol)
add_test(NAME delay_lama_voice_allocator COMMAND delay_lama_voice_allocator)
add_test(NAME delay_lama_voice_render COMMAND delay_lama_voice_render)
# The helper's own --render path, end to end: a script in, a WAV out.
add_test(NAME delay_lama_voice_render_cli
    COMMAND DelayLamaVoice --render "${PROJECT_ROOT}/Tests/voice_scripts/chord.txt"
        "${CMAKE_CURRENT_BINARY_DIR}/voice_render_cli.wav" --monks 3 --tail 500
)
set_tests_properties(delay_lama_voice_render delay_lama_voice_render_cli PROPERTIES TIMEOUT 120)
