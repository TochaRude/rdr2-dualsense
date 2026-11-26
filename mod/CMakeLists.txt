cmake_minimum_required(VERSION 3.20)
project(DualSenseRDR2 C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif ()

# Everything static: the .asi and core .dll must only depend on Windows DLLs and ScriptHookRDR2.dll.
add_compile_options(-Wall -Wno-unknown-pragmas -Wno-missing-field-initializers)

set(CORE_SOURCES
    src/log.cpp src/config.cpp src/ds5.cpp src/dsp.cpp src/gameaudio.cpp src/haptics.cpp
    src/audio.cpp src/catalog.cpp src/engine.cpp src/input.cpp src/features.cpp src/core.cpp src/surfaces.cpp src/voice.cpp src/settings_menu.cpp src/game_files.cpp src/awc.cpp src/gamemem.cpp src/dialogue.cpp)
set(SYSTEM_LIBS setupapi hid ole32 uuid avrt winmm)

# Import library for ScriptHookRDR2.dll from the .def (maps plain names to its MSVC exports)
find_program(DLLTOOL dlltool REQUIRED)
set(SHV_LIB ${CMAKE_BINARY_DIR}/libScriptHookRDR2.a)
add_custom_command(OUTPUT ${SHV_LIB}
    COMMAND ${DLLTOOL} -d ${CMAKE_CURRENT_SOURCE_DIR}/scripthook/ScriptHookRDR2.def -l libScriptHookRDR2.a -m i386:x86-64
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}     # relative name: dlltool puts it into the stub symbols
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/scripthook/ScriptHookRDR2.def)
add_custom_target(shv_import DEPENDS ${SHV_LIB})

# Pinned core DLL (holds XInput hooks, string-table nodes, and worker threads across ScriptHook reloads)
add_library(DualSenseCore SHARED ${CORE_SOURCES} src/world.cpp src/settings_ui.cpp src/text_inject.cpp src/io_probe.cpp src/main.cpp)
add_dependencies(DualSenseCore shv_import)
set_target_properties(DualSenseCore PROPERTIES PREFIX "" SUFFIX ".dll")
target_link_options(DualSenseCore PRIVATE -static -static-libgcc -static-libstdc++ $<$<CONFIG:Release>:-s>)
target_link_libraries(DualSenseCore PRIVATE ${SHV_LIB} ${SYSTEM_LIBS})

# Thin CRT-free ASI shim (zero TLS, zero persistent threads -> cleanly unloads/reloads on ScriptHook V2 save load)
add_library(DualSenseRDR2 SHARED src/asi_shim.c)
add_dependencies(DualSenseRDR2 shv_import)
set_target_properties(DualSenseRDR2 PROPERTIES PREFIX "" SUFFIX ".asi")
target_compile_options(DualSenseRDR2 PRIVATE -Os -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables)
target_link_options(DualSenseRDR2 PRIVATE -nostdlib -Wl,--entry,DllMain -Wl,--exclude-all-symbols $<$<CONFIG:Release>:-s>)
target_link_libraries(DualSenseRDR2 PRIVATE ${SHV_LIB} kernel32)

add_executable(dsr2_test ${CORE_SOURCES} src/world_stub.cpp src/standalone.cpp src/selftest.cpp)
target_link_options(dsr2_test PRIVATE -static -static-libgcc -static-libstdc++)
target_link_libraries(dsr2_test PRIVATE ${SYSTEM_LIBS})

# the test program reads the same data the mod ships (tools/trigger_lab/data), synced on every build
set(LAB_DATA ${CMAKE_CURRENT_SOURCE_DIR}/../tools/trigger_lab/data)
add_custom_target(test_data ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/DualSenseRDR2
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LAB_DATA}/haptics.json ${LAB_DATA}/weapons.json
            ${LAB_DATA}/sound_labels.json ${LAB_DATA}/speech_voices.txt ${LAB_DATA}/translations.json ${CMAKE_BINARY_DIR}/DualSenseRDR2/)
add_dependencies(dsr2_test test_data)

# Real Win32 reload test with a mock ScriptHookRDR2.dll verifying FreeLibrary/LoadLibrary hot-reload
add_library(mock_shv SHARED src/test_mock_shv.c src/test_mock_shv.def)
set_target_properties(mock_shv PROPERTIES
    PREFIX ""
    OUTPUT_NAME "ScriptHookRDR2"
    SUFFIX ".dll"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test_reload
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test_reload
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test_reload)
target_link_options(mock_shv PRIVATE -static-libgcc)

add_executable(dsr2_reload_test src/test_asi_reload.cpp)
set_target_properties(dsr2_reload_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test_reload)
target_link_options(dsr2_reload_test PRIVATE -static -static-libgcc -static-libstdc++)
target_link_libraries(dsr2_reload_test PRIVATE mock_shv)

add_custom_target(reload_test_stage ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/test_reload/DualSenseRDR2
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:DualSenseRDR2> ${CMAKE_BINARY_DIR}/test_reload/DualSenseRDR2.asi
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:DualSenseCore> ${CMAKE_BINARY_DIR}/test_reload/DualSenseRDR2/DualSenseCore.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LAB_DATA}/haptics.json ${LAB_DATA}/weapons.json
            ${LAB_DATA}/sound_labels.json ${LAB_DATA}/speech_voices.txt ${LAB_DATA}/translations.json ${CMAKE_BINARY_DIR}/test_reload/DualSenseRDR2/
    DEPENDS DualSenseRDR2 DualSenseCore mock_shv dsr2_reload_test test_data)

enable_testing()
add_test(NAME selftest COMMAND dsr2_test --selftest)
add_test(NAME asi_reload COMMAND dsr2_reload_test)
